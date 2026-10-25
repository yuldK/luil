#include "mobile_demo/device_page.h"

#include "luil/app/secret_store.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/ui_platform.h"
#include "luil/ui/wrap_element.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mobile_demo {
    namespace {
        // 비밀 저장소를 한 바퀴 돌린다: 쓰기 → 읽기 → 덮어쓰기 → 읽기 → 지우기 → 없는지 읽기 (secret-store-design.md).
        // 사람이 읽을 결과 한 줄을 돌려준다. 단계가 어긋나면 그 자리와 OS의 이유를 적는다.
        [[nodiscard]] std::u8string check_secret_store()
        {
            constexpr std::u8string_view service { u8"luil-demo" };
            constexpr std::u8string_view name { u8"sample" };
            const auto bytes = [](const std::u8string_view text) { return std::vector<std::uint8_t> { text.begin(), text.end() }; };
            const auto failed = [](const std::u8string_view step, const luil::secret_store_result& result) {
                return std::u8string { step } + u8"에서 멈췄다 — " + (result.status == luil::secret_store_status::unsupported ? std::u8string { u8"이 플랫폼에는 저장소가 없다" } : result.error);
            };
            std::vector<std::uint8_t> read {};
            if (const auto result { luil::store_secret(service, name, bytes(u8"첫 값")) }; result.status != luil::secret_store_status::ok)
                return failed(u8"쓰기", result);
            if (const auto result { luil::load_secret(service, name, read) }; result.status != luil::secret_store_status::ok || read != bytes(u8"첫 값"))
                return failed(u8"읽기", result);
            if (const auto result { luil::store_secret(service, name, bytes(u8"둘째 값")) }; result.status != luil::secret_store_status::ok)
                return failed(u8"덮어쓰기", result);
            if (const auto result { luil::load_secret(service, name, read) }; result.status != luil::secret_store_status::ok || read != bytes(u8"둘째 값"))
                return failed(u8"다시 읽기", result);
            if (const auto result { luil::erase_secret(service, name) }; result.status != luil::secret_store_status::ok)
                return failed(u8"지우기", result);
            if (const auto result { luil::load_secret(service, name, read) }; result.status != luil::secret_store_status::not_found)
                return failed(u8"지운 뒤 읽기", result);
            return u8"쓰기·읽기·덮어쓰기·지우기를 모두 확인했다 (luil-demo/sample).";
        }

        struct orientation_choice
        {
            luil::screen_orientation value {};
            std::u8string_view key {};
            std::u8string_view label {};
        };

        struct haptic_choice
        {
            luil::haptic_feedback feedback {};
            std::u8string_view key {};
            std::u8string_view label {};
        };

        constexpr std::array<haptic_choice, 5> haptic_choices {
            haptic_choice { luil::haptic_feedback::long_press, u8"long-press", u8"길게 누름" },
            haptic_choice { luil::haptic_feedback::drag_start, u8"drag-start", u8"끌기 시작" },
            haptic_choice { luil::haptic_feedback::tick, u8"tick", u8"눈금" },
            haptic_choice { luil::haptic_feedback::confirm, u8"confirm", u8"확인" },
            haptic_choice { luil::haptic_feedback::reject, u8"reject", u8"거절" },
        };

        [[nodiscard]] std::u8string_view width_class_name(const luil::window_width_class value) noexcept
        {
            switch (value)
            {
            case luil::window_width_class::compact:
                return u8"compact";
            case luil::window_width_class::medium:
                return u8"medium";
            case luil::window_width_class::expanded:
                return u8"expanded";
            }
            return u8"compact";
        }

        constexpr std::array<orientation_choice, 4> orientation_choices {
            orientation_choice { luil::screen_orientation::system, u8"system", u8"시스템" },
            orientation_choice { luil::screen_orientation::portrait, u8"portrait", u8"세로" },
            orientation_choice { luil::screen_orientation::landscape, u8"landscape", u8"가로" },
            orientation_choice { luil::screen_orientation::current, u8"current", u8"지금 고정" },
        };
    } // namespace

    bool device_page::handle(const luil::app_message& message)
    {
        if (const auto* const orientation { message.get<orientation_intent>() }; orientation != nullptr)
        {
            request_.orientation = orientation->orientation;
            return true;
        }
        if (const auto* const bars { message.get<system_bars_intent>() }; bars != nullptr)
        {
            request_.system_bars = bars->mode;
            return true;
        }
        if (const auto* const cutout { message.get<cutout_intent>() }; cutout != nullptr)
        {
            request_.draw_under_cutout = cutout->draw_under;
            return true;
        }
        if (const auto* const volume { message.get<volume_keys_intent>() }; volume != nullptr)
        {
            request_.volume_keys = volume->claim;
            return true;
        }
        if (const auto* const key { message.get<volume_key_intent>() }; key != nullptr)
        {
            ++(key->up ? volume_up_count_ : volume_down_count_);
            if (key->repeat)
                ++volume_repeat_count_;
            return true;
        }
        if (message.get<secret_check_intent>() != nullptr)
        {
            // logic thread에서 돈다 — Keystore 호출은 수십 ms까지 걸려 UI thread의 자리가 아니다.
            secret_report_ = check_secret_store();
            return true;
        }
        if (const auto* const local { message.get<local_network_intent>() }; local != nullptr)
        {
            local_network_ = local->state;
            local_network_answered_ = local_network_answered_ || local->answered;
            return true;
        }
        return false;
    }

    page_content device_page::build(const float width, const float scale, const window_metrics_intent& window)
    {
        static_cast<void>(scale);
        const luil::ui_platform_capabilities capabilities { luil::current_ui_platform().capabilities };
        const float choice_width { std::min(width - 2.0f * page_padding, 360.0f) };

        page_column column { u8"device" };
        // 창 폭 등급은 모든 플랫폼에서 같은 함수다. 화면 분할·창 크기를 바꾸면 이 줄과 셸의 판 수가 함께 바뀐다.
        const float logical_width { window.scale > 0.0f ? window.width / window.scale : 0.0f };
        column.heading(u8"window-heading", u8"창 폭");
        column.body(u8"window-class", to_u8(static_cast<int>(logical_width)) + u8"dp · " + std::u8string { width_class_name(window.width_class) });
        column.note(u8"window-note", u8"600dp부터 두 판, 840dp부터 넓은 판이다.");
        column.gap(20.0f);
        column.heading(u8"orientation-heading", u8"화면 방향");
        if (capabilities.screen_orientation == false)
            column.note(u8"orientation-none", u8"이 플랫폼은 화면 방향을 요청할 수 없다.");
        else
        {
            luil::choice_group_config orientations {};
            orientations.owner = u8"orientation";
            orientations.style = luil::choice_style::toggle;
            // 폰 화면은 묶음을 화면 폭에 채운다 — 손가락이 닿는 칸이 넓다.
            orientations.width = luil::choice_width::fill;
            orientations.name = u8"화면 방향";
            for (const orientation_choice& choice : orientation_choices)
            {
                orientations.items.push_back({ std::u8string { choice.key }, std::u8string { choice.label } });
                if (choice.value == request_.orientation)
                    orientations.selected = std::u8string { choice.key };
            }
            orientations.select = [](const std::u8string& key) {
                for (const orientation_choice& choice : orientation_choices)
                    if (key == choice.key)
                        return luil::make_app_action(orientation_intent { choice.value });
                return luil::input_action {};
            };
            const float height { luil::choice_group_element::height_for(orientations) };
            column.gap(6.0f);
            column.add(std::make_unique<luil::choice_group_element>(std::move(orientations)), height, choice_width);
            column.gap(6.0f);
            column.note(u8"orientation-note-1", u8"지금 고정은 고른 순간의 방향에 묶는다.");
            column.note(u8"orientation-note-2", u8"다른 페이지로 가도 요청은 그대로다.");
        }

        column.gap(20.0f);
        column.heading(u8"bars-heading", u8"시스템 막대");
        if (capabilities.system_bars == false)
            column.note(u8"bars-none", u8"이 플랫폼은 시스템 막대를 숨길 수 없다.");
        else
        {
            luil::choice_group_config bars {};
            bars.owner = u8"system-bars";
            bars.style = luil::choice_style::toggle;
            bars.width = luil::choice_width::fill;
            bars.name = u8"시스템 막대";
            bars.items = { { u8"visible", u8"보임" }, { u8"immersive", u8"몰입" } };
            bars.selected = request_.system_bars == luil::system_bars_mode::immersive ? u8"immersive" : u8"visible";
            bars.select
                = [](const std::u8string& key) { return luil::make_app_action(system_bars_intent { key == u8"immersive" ? luil::system_bars_mode::immersive : luil::system_bars_mode::visible }); };
            const float bars_height { luil::choice_group_element::height_for(bars) };
            column.gap(6.0f);
            column.add(std::make_unique<luil::choice_group_element>(std::move(bars)), bars_height, std::min(choice_width, 240.0f));
            column.gap(6.0f);
            column.note(u8"bars-note", u8"몰입이면 가장자리에서 쓸 때만 잠시 보인다.");

            luil::check_config cutout {};
            cutout.owner = u8"cutout";
            cutout.style = luil::check_style::toggle_switch;
            cutout.label = u8"컷아웃 밑까지 그리기";
            cutout.checked = request_.draw_under_cutout;
            cutout.toggle = luil::make_message_action(cutout_intent { request_.draw_under_cutout == false });
            const float cutout_height { luil::check_element::height_for(cutout) };
            column.gap(10.0f);
            column.add(std::make_unique<luil::check_element>(std::move(cutout)), std::max(cutout_height, touch_height), choice_width);
        }

        column.gap(20.0f);
        column.heading(u8"volume-heading", u8"볼륨 키");
        if (capabilities.volume_keys == false)
            column.note(u8"volume-none", u8"이 플랫폼은 볼륨 키를 앱에 주지 않는다.");
        else
        {
            luil::check_config volume {};
            volume.owner = u8"volume-keys";
            volume.style = luil::check_style::toggle_switch;
            volume.label = u8"볼륨 키로 쪽 넘기기";
            volume.checked = request_.volume_keys;
            volume.toggle = luil::make_message_action(volume_keys_intent { request_.volume_keys == false });
            const float volume_height { luil::check_element::height_for(volume) };
            column.gap(6.0f);
            column.add(std::make_unique<luil::check_element>(std::move(volume)), std::max(volume_height, touch_height), choice_width);
            column.body(u8"volume-count", u8"받은 키: 올림 " + to_u8(volume_up_count_) + u8" · 내림 " + to_u8(volume_down_count_) + u8" · 반복 " + to_u8(volume_repeat_count_));
            column.note(u8"volume-note", u8"끄면 시스템이 소리 크기를 바꾼다.");
        }

        column.gap(20.0f);
        column.heading(u8"haptic-heading", u8"햅틱");
        if (capabilities.haptics == false)
            column.note(u8"haptic-none", u8"이 플랫폼에는 햅틱이 없다.");
        else
        {
            column.note(u8"haptic-note-1", u8"메뉴·팝업 페이지의 카드를 길게 누르면 울린다.");
            column.note(u8"haptic-note-2", u8"단추는 액션으로 햅틱을 직접 낸다.");
            column.gap(8.0f);
            // 단추의 액션이 햅틱 요청을 그대로 돌려준다. 앱 상태가 아니라 logic을 거치지 않는다.
            luil::wrap_config grid_config {};
            grid_config.item_width = 104.0f;
            grid_config.item_height = touch_height;
            grid_config.spacing = 8.0f;
            grid_config.line_spacing = 8.0f;
            auto grid { std::make_unique<luil::wrap_element>(luil::ui_element_id { kind_layout, u8"haptics" }, grid_config) };
            for (const haptic_choice& choice : haptic_choices)
            {
                const luil::ui_element_id id { kind_layout, u8"haptic-" + std::u8string { choice.key } };
                auto button { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .text = std::u8string { choice.label } }) };
                button->set_action(luil::ui_trigger::left_click,
                    [feedback = choice.feedback](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::haptic_request { feedback } } }; });
                grid->add(std::move(button));
            }
            column.add(std::move(grid), luil::wrap_height_for(grid_config, width - 2.0f * page_padding, haptic_choices.size()));
        }

        // 집 안 NAS 같은 같은 네트워크의 기기에 붙으려면 Android 17부터 이 권한이 있어야 한다.
        column.gap(20.0f);
        column.heading(u8"local-network-heading", u8"로컬 네트워크 권한");
        switch (local_network_)
        {
        case luil::permission_state::not_required:
            column.note(u8"local-network-state", u8"묻지 않아도 된다.");
            column.note(u8"local-network-when", u8"Android 17부터 targetSdk 37 이상인 앱만 묻는다.");
            break;
        case luil::permission_state::granted:
            column.note(u8"local-network-state", local_network_answered_ ? u8"받았다 (방금 허락)." : u8"받았다.");
            break;
        case luil::permission_state::denied:
            column.note(u8"local-network-state", local_network_answered_ ? u8"받지 못했다 (거절)." : u8"아직 받지 못했다.");
            column.gap(8.0f);
            // 묻기는 UI thread의 일이라 앱 UI 명령으로 delegate에 넘긴다.
            auto ask { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"local-network-ask" }, luil::text_button_config { .text = u8"묻기" }) };
            ask->set_action(luil::ui_trigger::left_click,
                [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_ui_command { command_request_local_network } } }; });
            column.add(std::move(ask), touch_height);
            break;
        }

        // 비밀 저장소다. Keystore의 AES-GCM 키로 암호화해 앱 내부 파일에 둔다 (Java 0줄, JNI).
        column.gap(20.0f);
        column.heading(u8"secret-heading", u8"비밀 저장소");
        auto check { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"secret-check" }, luil::text_button_config { .text = u8"한 바퀴 돌려 보기" }) };
        check->set_action(luil::ui_trigger::left_click, luil::make_message_action(secret_check_intent {}));
        column.add(std::move(check), touch_height);
        if (secret_report_.empty() == false)
            column.note(u8"secret-report", secret_report_);
        return column.finish();
    }
} // namespace mobile_demo

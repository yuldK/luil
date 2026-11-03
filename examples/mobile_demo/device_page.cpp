#include "mobile_demo/device_page.h"

#include "luil/app/secret_store.h"
#include "luil/app/updater.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/spinner_element.h"
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

        [[nodiscard]] std::u8string_view apply_mode_text(const luil::update_apply_mode mode) noexcept
        {
            switch (mode)
            {
            case luil::update_apply_mode::replace_executable:
                return u8"실행 파일 바꾸기";
            case luil::update_apply_mode::open_store:
                return u8"스토어 열기";
            }
            return u8"스토어 열기";
        }

        constexpr std::array<orientation_choice, 4> orientation_choices {
            orientation_choice { luil::screen_orientation::system, u8"system", u8"시스템" },
            orientation_choice { luil::screen_orientation::portrait, u8"portrait", u8"세로" },
            orientation_choice { luil::screen_orientation::landscape, u8"landscape", u8"가로" },
            orientation_choice { luil::screen_orientation::current, u8"current", u8"지금 고정" },
        };
    }

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
        if (const auto* const transition { message.get<page_transition_toggle_intent>() }; transition != nullptr)
        {
            page_transition_ = transition->enabled;
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
        if (const auto* const permission { message.get<notification_permission_intent>() }; permission != nullptr)
        {
            notifications_ = permission->state;
            notifications_answered_ = notifications_answered_ || permission->answered;
            return true;
        }
        if (const auto* const sent { message.get<notification_send_intent>() }; sent != nullptr)
        {
            notification_sent_ = sent->result;
            return true;
        }
        if (const auto* const launch { message.get<notification_launch_intent>() }; launch != nullptr)
        {
            notification_launch_ = launch->id;
            return true;
        }
        return false;
    }

    std::u8string device_page::page_transition_text() const
    {
        if (page_transition_ == false)
            return u8"페이지 전환: 꺼짐";
        return page_transition_turning_ ? u8"페이지 전환: 켜짐 · 도는 중" : u8"페이지 전환: 켜짐 · 멈춤";
    }

    page_content device_page::build(const float width, const float scale, const window_metrics_intent& window)
    {
        const luil::ui_platform_capabilities capabilities { luil::current_ui_platform().capabilities };
        const float choice_width { std::min(width - 2.0f * page_padding, 360.0f) };

        page_column column { u8"device" };
        // 창 폭 등급은 모든 플랫폼에서 같은 함수다. 화면 분할·창 크기를 바꾸면 이 줄과 셸의 판 수가 함께 바뀐다.
        const float logical_width { window.scale > 0.0f ? window.width / window.scale : 0.0f };
        column.heading(u8"window-heading", u8"창 폭");
        column.body(u8"window-class", to_u8(static_cast<int>(logical_width)) + u8"dp · " + std::u8string { width_class_name(window.width_class) });
        column.note(u8"window-note", u8"600dp부터 두 판, 840dp부터 넓은 판이다.");
        column.gap(20.0f);
        // 움직임 줄이기는 시스템의 값이다. 켜지면 아래 표시가 멈춘 그림으로 서고, 앱이 든 전환은 길이 없이 끝값에 선다.
        column.heading(u8"motion-heading", u8"움직임 줄이기");
        column.body(u8"motion-state", window.reduce_motion ? u8"켜짐 · 도는 표시가 멈춘다" : u8"꺼짐 · 도는 표시가 돈다");
        column.gap(8.0f);
        column.add(std::make_unique<luil::spinner_element>(luil::ui_element_id { kind_layout, u8"motion-spinner" }, luil::spinner_config { .name = u8"도는 표시" }), 24.0f, 24.0f);
        // 페이지를 바꿀 때의 전환이다. 좁은 화면은 목록과 페이지가 가로로 밀리고, 넓은 화면의 오른쪽 판은 옅어진다.
        //  - 움직임 줄이기가 켜져 있으면 켜 두어도 바로 바뀐다.
        luil::check_config transition {};
        transition.owner = u8"page-transition";
        transition.style = luil::check_style::toggle_switch;
        transition.label = u8"페이지 전환";
        transition.checked = page_transition_;
        transition.toggle = luil::make_message_action(page_transition_toggle_intent { page_transition_ == false });
        const float transition_height { luil::check_element::height_for(transition) };
        column.gap(10.0f);
        column.add(std::make_unique<luil::check_element>(std::move(transition)), std::max(transition_height, touch_height), choice_width);
        column.body(u8"page-transition-state", page_transition_text());
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

        // 작업 표시줄은 데스크톱의 창 단추다. 요청(`ui_frame::taskbar`)은 어느 플랫폼에서나 실을 수 있고 하지 못하는 플랫폼은 받기만 한다.
        column.gap(20.0f);
        column.heading(u8"taskbar-heading", u8"작업 표시줄");
        if (capabilities.taskbar == false)
            column.note(u8"taskbar-none", u8"이 플랫폼에는 작업 표시줄이 없다.");
        else
            column.note(u8"taskbar-note", u8"창 단추에 진행률과 오버레이를 걸 수 있다. 데스크톱 데모의 창 페이지에서 본다.");

        // 알림 영역 아이콘도 데스크톱의 것이다 (tray-icon-design.md). 요청(`ui_frame::tray`)은 실을 수 있고 하지 못하는 플랫폼은 받기만 한다.
        column.gap(20.0f);
        column.heading(u8"tray-heading", u8"알림 영역 아이콘");
        // 없다는 설명은 좁은 화면과 큰 글꼴 배율에서 한 줄 끝이 말줄임으로 잘리므로 줄을 바꾼다.
        if (capabilities.tray == false)
            column.wrapped_note(u8"tray-none", u8"이 플랫폼에는 알림 영역 아이콘이 없다. 알림은 OS 알림이 맡는다.", width, scale);
        else
            column.note(u8"tray-note", u8"알림 영역에 아이콘을 세울 수 있다. 데스크톱 데모의 창 페이지에서 본다.");

        // 앱이 뒤에 있을 때 받는 전역 단축키도 데스크톱의 것이다 (global-hotkey-design.md). 요청(`ui_frame::hotkeys`)은 실을 수 있고 하지 못하는 플랫폼은 받기만 한다.
        column.gap(20.0f);
        column.heading(u8"hotkeys-heading", u8"전역 단축키");
        if (capabilities.global_hotkeys == false)
            column.note(u8"hotkeys-none", u8"이 플랫폼에는 전역 단축키가 없다.");
        else
            column.note(u8"hotkeys-note", u8"앱이 뒤에 있어도 받는 키 조합을 걸 수 있다. 데스크톱 데모의 창 페이지에서 본다.");

        // 자동 업데이트의 적용 방식이다 (auto-update-apply-design.md). Android는 스토어 페이지를 연다. 이 페이지는 적용하지 않고 방식만 보인다.
        column.gap(20.0f);
        column.heading(u8"update-heading", u8"자동 업데이트");
        column.note(u8"update-apply-mode", u8"업데이트 적용 방식: " + std::u8string { apply_mode_text(luil::platform_update_apply_mode()) });

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

        // OS 알림이다 (os-notification-design.md). 알림 창에 올리고, 꺼진 앱에서 누르면 그 id로 앱이 뜬다.
        column.gap(20.0f);
        column.heading(u8"notification-heading", u8"알림");
        if (capabilities.notifications == false)
            column.note(u8"notification-none", u8"이 플랫폼은 OS 알림을 올리지 못한다.");
        else
        {
            switch (notifications_)
            {
            case luil::permission_state::not_required:
                column.note(u8"notification-state", u8"알림 권한: 묻지 않아도 된다.");
                break;
            case luil::permission_state::granted:
                column.note(u8"notification-state", notifications_answered_ ? u8"알림 권한: 받았다 (방금 허락)." : u8"알림 권한: 받았다.");
                break;
            case luil::permission_state::denied:
                column.note(u8"notification-state", notifications_answered_ ? u8"알림 권한: 받지 못했다 (거절)." : u8"알림 권한: 아직 받지 못했다.");
                column.gap(8.0f);
                auto ask { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"notification-ask" }, luil::text_button_config { .text = u8"알림 권한 묻기" }) };
                ask->set_action(luil::ui_trigger::left_click,
                    [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_ui_command { command_request_notifications } } }; });
                column.add(std::move(ask), touch_height);
                break;
            }
            column.gap(8.0f);
            // 올리기는 앱 UI 명령으로 delegate에 넘긴다. 페이지는 플랫폼을 부르지 않는다.
            auto send { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"notification-send" }, luil::text_button_config { .text = u8"알림 보내기" }) };
            send->set_action(luil::ui_trigger::left_click,
                [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_ui_command { command_send_notification } } }; });
            column.add(std::move(send), touch_height);
            std::u8string sent { u8"알림 결과: 없음" };
            if (notification_sent_.has_value())
                sent = notification_sent_->ok() ? std::u8string { u8"알림 결과: ok" } : u8"알림 결과: 실패 · " + notification_sent_->error;
            column.note(u8"notification-result", std::move(sent));
            column.note(u8"notification-launch", u8"알림으로 시작: " + (notification_launch_.empty() ? std::u8string { u8"아니오" } : notification_launch_));
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
}

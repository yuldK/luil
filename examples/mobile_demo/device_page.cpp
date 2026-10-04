#include "mobile_demo/device_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/ui_platform.h"

#include <algorithm>
#include <array>
#include <utility>

namespace mobile_demo {
    namespace {
        struct orientation_choice
        {
            luil::screen_orientation value {};
            std::u8string_view key {};
            std::u8string_view label {};
        };

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
        return false;
    }

    page_content device_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        const luil::ui_platform_capabilities capabilities { luil::current_ui_platform().capabilities };
        const float choice_width { std::min(width - 2.0f * page_padding, 360.0f) };

        page_column column { u8"device" };
        column.heading(u8"orientation-heading", u8"화면 방향");
        if (capabilities.screen_orientation == false)
            column.note(u8"orientation-none", u8"이 플랫폼은 화면 방향을 요청할 수 없다.");
        else
        {
            luil::choice_group_config orientations {};
            orientations.owner = u8"orientation";
            orientations.style = luil::choice_style::toggle;
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
        return column.finish();
    }
} // namespace mobile_demo

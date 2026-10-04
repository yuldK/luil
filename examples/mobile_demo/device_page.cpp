#include "mobile_demo/device_page.h"

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
        return column.finish();
    }
} // namespace mobile_demo

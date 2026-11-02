#include "luil/ui/field_popup.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 수식 키가 하나도 눌리지 않았는가. 물리 Ctrl과 플랫폼이 알린 주 단축키 역할을 모두 본다.
        [[nodiscard]] bool bare_key(const key_pressed_event& event) noexcept
        {
            return event.shift == false && event.shortcut_modifier_down() == false;
        }

        // Alt만 눌렸는가. Ctrl·Shift·Meta가 함께면 앱 단축키다.
        [[nodiscard]] bool alt_only(const key_pressed_event& event) noexcept
        {
            return event.alt && event.shift == false && event.control == false && event.meta == false && event.primary_shortcut_down() == false;
        }
    }

    field_popup_position place_field_popup(const field_popup_anchor& anchor, const size_f size, const float scale) noexcept
    {
        const float factor { std::isfinite(scale) && scale > 0.0f ? scale : 1.0f };
        const float width { std::max(0.0f, size.width) * factor };
        const float height { std::max(0.0f, size.height) * factor };
        const float gap { std::isfinite(anchor.gap) ? std::max(0.0f, anchor.gap) * factor : 0.0f };
        const rect_f& field { anchor.field };
        const rect_f& limit { anchor.limit };

        // 가로는 맞춘 뒤 limit 안으로 민다. 오른쪽 끝을 먼저 들이고 왼쪽 끝을 나중에 지켜, popup이 limit보다
        // 넓으면 왼쪽 가장자리에 붙는다.
        float x { anchor.alignment == field_popup_alignment::end ? field.x + field.width - width : field.x };
        x = std::min(x, limit.x + limit.width - width);
        x = std::max(x, limit.x);

        // 기본은 아래다. 아래가 모자라고 위가 들어갈 때만 뒤집고, 둘 다 안 되면 아래에 두어 host가 밀게 한다.
        const float below { field.y + field.height + gap };
        const float above { field.y - gap - height };
        const bool flip { below + height > limit.y + limit.height && above >= limit.y };
        return { x / factor, (flip ? above : below) / factor, flip };
    }

    key_handler field_popup_key_handler(const bool open, std::function<input_action(bool open)> set_open, const bool down_opens)
    {
        if (set_open == nullptr)
            return {};
        return [open, set_open = std::move(set_open), down_opens](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
            const bool arrow_down { event.key == key_code::arrow_down };
            const bool opens { (arrow_down && alt_only(event)) || (event.key == key_code::f4 && bare_key(event)) || (down_opens && arrow_down && bare_key(event)) };
            if (opens == false)
                return std::nullopt;
            // 이미 열려 있으면 삼켜서 앱 단축키로 새지 않게 한다.
            if (open)
                return std::vector<input_action> {};
            return std::vector<input_action> { set_open(true) };
        };
    }
}

#include "luil/ui/field_popup.h"

#include <algorithm>
#include <array>
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

        [[nodiscard]] field_popup_side opposite_side(const field_popup_side side) noexcept
        {
            switch (side)
            {
            case field_popup_side::above:
                return field_popup_side::below;
            case field_popup_side::left:
                return field_popup_side::right;
            case field_popup_side::right:
                return field_popup_side::left;
            case field_popup_side::below:
                break;
            }
            return field_popup_side::above;
        }

        [[nodiscard]] bool vertical_side(const field_popup_side side) noexcept
        {
            return side == field_popup_side::below || side == field_popup_side::above;
        }

        // 처음 가장자리를 나중에 지켜, `length`가 `start`부터의 폭보다 크면 처음 가장자리에 붙는다.
        [[nodiscard]] float push_inside(const float value, const float length, const float start, const float extent) noexcept
        {
            return std::max(std::min(value, start + extent - length), start);
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

    field_popup_side_position place_field_popup_at_side(const field_popup_anchor& anchor, const size_f size, const float scale, const field_popup_side preferred) noexcept
    {
        const float factor { std::isfinite(scale) && scale > 0.0f ? scale : 1.0f };
        const float width { std::max(0.0f, size.width) * factor };
        const float height { std::max(0.0f, size.height) * factor };
        const float gap { std::isfinite(anchor.gap) ? std::max(0.0f, anchor.gap) * factor : 0.0f };
        const rect_f& field { anchor.field };
        const rect_f& limit { anchor.limit };

        // 그 방향의 주 축 자리다 (왼쪽·위 좌표).
        const auto main_of = [&](const field_popup_side side) {
            switch (side)
            {
            case field_popup_side::above:
                return field.y - gap - height;
            case field_popup_side::left:
                return field.x - gap - width;
            case field_popup_side::right:
                return field.x + field.width + gap;
            case field_popup_side::below:
                break;
            }
            return field.y + field.height + gap;
        };
        const auto fits = [&](const field_popup_side side) {
            const float main { main_of(side) };
            if (vertical_side(side))
                return main >= limit.y && main + height <= limit.y + limit.height;
            return main >= limit.x && main + width <= limit.x + limit.width;
        };

        // 바라는 방향, 반대, 남은 둘 순서다. 남은 둘은 아래·위면 오른쪽·왼쪽, 옆이면 아래·위다.
        const bool vertical { vertical_side(preferred) };
        const std::array<field_popup_side, 4> order {
            preferred,
            opposite_side(preferred),
            vertical ? field_popup_side::right : field_popup_side::below,
            vertical ? field_popup_side::left : field_popup_side::above,
        };
        field_popup_side chosen { preferred };
        bool found { false };
        for (const field_popup_side side : order)
        {
            if (fits(side) == false)
                continue;
            chosen = side;
            found = true;
            break;
        }

        // 다른 축은 필드 가운데에 맞춘 뒤 민다. 들어가는 방향이 없으면 주 축도 민다 (그때 필드를 덮을 수 있다).
        float x { 0.0f };
        float y { 0.0f };
        if (vertical_side(chosen))
        {
            x = push_inside(field.x + (field.width - width) / 2.0f, width, limit.x, limit.width);
            y = main_of(chosen);
            if (found == false)
                y = push_inside(y, height, limit.y, limit.height);
        }
        else
        {
            x = main_of(chosen);
            y = push_inside(field.y + (field.height - height) / 2.0f, height, limit.y, limit.height);
            if (found == false)
                x = push_inside(x, width, limit.x, limit.width);
        }
        return { x / factor, y / factor, chosen, found };
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

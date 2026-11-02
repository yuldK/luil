#pragma once

// 두 데모의 검사기 페이지가 함께 쓰는 상태다 (inspector-design.md의 데모 절). 켜기, 집는 길, 조상 단계, 선택, 윤곽과 팔레트 견본을
// 앱 상태로 들고 `inspector_config`로 풀어 frame에 싣는다. 화면 배치만 데모마다 다르다.

#include "luil/ui/app_message.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_tree.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace demo {
    // 단추와 단축키가 낸다. 같은 메시지를 메뉴와 단축키에 거는 것이 앱의 몫이다.
    enum class inspector_command
    {
        toggle,
        levels_up,
        levels_down,
        select_parent,
        select_child,
        select_previous,
        select_next,
        toggle_outline,
        toggle_palette,
    };

    struct inspector_command_intent
    {
        inspector_command command { inspector_command::toggle };
    };

    class inspector_state
    {
    public:
        // `base_pick`은 켤 때 서는 집는 길이다. 마우스가 있는 창은 hovered, 손가락만 있는 화면은 focused다.
        explicit inspector_state(const luil::inspector_pick base_pick) noexcept
            : base_pick_ { base_pick }
            , pick_ { base_pick }
        {
        }

        // 선택을 옮기는 명령은 방금 지은 tree가 있어야 한다.
        [[nodiscard]] static bool moves_selection(const inspector_command command) noexcept
        {
            return command == inspector_command::select_parent || command == inspector_command::select_child || command == inspector_command::select_previous
                || command == inspector_command::select_next;
        }

        // 명령을 적용한다. 선택 이동은 `tree` 위에서 움직이고, 옮길 곳이 없으면 선택을 그대로 둔다.
        void apply(const inspector_command command, const luil::ui_tree* const tree)
        {
            switch (command)
            {
            case inspector_command::toggle:
                inspecting_ = inspecting_ == false;
                // 켤 때마다 처음 모습이다. 선택은 비우고 집는 길은 기본 길로 돌아간다.
                if (inspecting_)
                {
                    pick_ = base_pick_;
                    selected_ = {};
                }
                break;
            case inspector_command::levels_up:
                ++levels_;
                break;
            case inspector_command::levels_down:
                if (levels_ > 0)
                    --levels_;
                break;
            case inspector_command::select_parent:
                move_selection(tree, luil::inspect_move::parent);
                break;
            case inspector_command::select_child:
                move_selection(tree, luil::inspect_move::first_child);
                break;
            case inspector_command::select_previous:
                move_selection(tree, luil::inspect_move::previous);
                break;
            case inspector_command::select_next:
                move_selection(tree, luil::inspect_move::next);
                break;
            case inspector_command::toggle_outline:
                outline_ = outline_ == false;
                break;
            case inspector_command::toggle_palette:
                palette_ = palette_ == false;
                break;
            }
        }

        // 페이지가 짓는 tree의 요소 수다 (`inspect_nodes`가 센 수).
        void note_elements(const std::size_t count) noexcept
        {
            elements_ = count;
        }

        // frame에 실을 설정이다. 앱 kind의 이름은 값으로 싣는다.
        [[nodiscard]] luil::inspector_config config(std::vector<luil::inspector_kind_name> kind_names) const
        {
            luil::inspector_config result {};
            result.enabled = inspecting_;
            result.pick = pick_;
            result.ancestor_levels = levels_;
            result.selected = selected_;
            result.outline_all = outline_;
            result.show_palette = palette_;
            result.kind_names = std::move(kind_names);
            return result;
        }

        [[nodiscard]] bool inspecting() const noexcept
        {
            return inspecting_;
        }

        [[nodiscard]] luil::inspector_pick pick() const noexcept
        {
            return pick_;
        }

        [[nodiscard]] std::size_t levels() const noexcept
        {
            return levels_;
        }

        [[nodiscard]] const luil::ui_element_id& selected() const noexcept
        {
            return selected_;
        }

        [[nodiscard]] bool outline() const noexcept
        {
            return outline_;
        }

        [[nodiscard]] bool palette() const noexcept
        {
            return palette_;
        }

        // 상태 글이다. 화면의 label과 UIA Name이 같은 글이다.
        [[nodiscard]] std::u8string state_text() const
        {
            return inspecting_ ? u8"검사기: 켬" : u8"검사기: 끔";
        }

        [[nodiscard]] std::u8string pick_text() const
        {
            return u8"집기: " + std::u8string { pick_name(pick_) };
        }

        [[nodiscard]] std::u8string levels_text() const
        {
            return u8"조상 단계: " + number(levels_);
        }

        [[nodiscard]] std::u8string selected_text(const luil::inspector_config& names) const
        {
            if (selected_ == luil::ui_element_id {})
                return u8"선택: 없음";
            return u8"선택: " + luil::kind_display_name(selected_.kind, names) + u8":" + selected_.owner;
        }

        [[nodiscard]] std::u8string count_text() const
        {
            return u8"요소 " + number(elements_) + u8"개";
        }

        [[nodiscard]] std::u8string outline_text() const
        {
            return outline_ ? u8"모든 윤곽: 켬" : u8"모든 윤곽: 끔";
        }

        [[nodiscard]] std::u8string palette_text() const
        {
            return palette_ ? u8"팔레트 견본: 켬" : u8"팔레트 견본: 끔";
        }

        [[nodiscard]] static std::u8string_view pick_name(const luil::inspector_pick pick) noexcept
        {
            switch (pick)
            {
            case luil::inspector_pick::hovered:
                return u8"hovered";
            case luil::inspector_pick::focused:
                return u8"focused";
            case luil::inspector_pick::pressed:
                return u8"pressed";
            case luil::inspector_pick::selected:
                return u8"selected";
            case luil::inspector_pick::point:
                return u8"point";
            }
            return u8"";
        }

    private:
        [[nodiscard]] static std::u8string number(const std::size_t value)
        {
            const std::string digits { std::to_string(value) };
            return std::u8string { digits.begin(), digits.end() };
        }

        void move_selection(const luil::ui_tree* const tree, const luil::inspect_move move)
        {
            if (tree == nullptr)
                return;
            const luil::ui_element_id next { luil::inspect_step(*tree, selected_, move) };
            if (next == luil::ui_element_id {})
                return;
            selected_ = next;
            // 선택을 옮겼으니 그 선택을 집는다.
            pick_ = luil::inspector_pick::selected;
        }

        luil::inspector_pick base_pick_ { luil::inspector_pick::hovered };
        luil::inspector_pick pick_ { luil::inspector_pick::hovered };
        bool inspecting_ { false };
        std::size_t levels_ { 0 };
        luil::ui_element_id selected_ {};
        bool outline_ { false };
        bool palette_ { false };
        std::size_t elements_ { 0 };
    };
}

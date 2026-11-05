#include "demo/shell_menu.h"

#include "demo/common.h"

#include "luil/app/menu_popup.h"
#include "luil/ui/menu_element.h"

#include <utility>

namespace demo {
    namespace {
        // 하위 메뉴를 담는 보기 메뉴 항목의 키다.
        constexpr std::u8string_view theme_submenu { u8"view.theme" };
        constexpr std::u8string_view page_transition_submenu { u8"view.page-transition" };
        // 막대 메뉴의 popup id다. 하위 메뉴는 부모 popup 기준 자리에 서고 id는 `submenu_popup_id`가 짓는다.
        constexpr std::u8string_view bar_popup_id { u8"shell-menu" };

        // 하위 메뉴의 popup id다. "view.theme"이면 "shell-menu-theme"이다.
        [[nodiscard]] std::u8string submenu_popup_id(const std::u8string_view key)
        {
            const std::size_t dot { key.find(u8'.') };
            return std::u8string { bar_popup_id } + u8"-" + std::u8string { dot == std::u8string_view::npos ? key : key.substr(dot + 1) };
        }

        struct command_name
        {
            std::u8string_view key {};
            std::u8string_view label {};
        };

        // 마지막 명령 label에 쓸 이름이다. 메뉴 라벨과 같다.
        constexpr command_name command_names[] {
            { u8"file.new", u8"새로 만들기" },
            { u8"file.open", u8"열기" },
            { u8"file.save", u8"저장" },
            { u8"edit.undo", u8"실행 취소" },
            { u8"edit.redo", u8"다시 실행" },
            { u8"edit.cut", u8"잘라내기" },
            { u8"edit.copy", u8"복사" },
            { u8"edit.paste", u8"붙여넣기" },
            { shell_menu_sidebar, u8"사이드바" },
            { shell_menu_theme_light, u8"밝게" },
            { shell_menu_theme_system, u8"시스템" },
            { shell_menu_theme_dark, u8"어둡게" },
            { shell_menu_page_transition_none, u8"없음" },
            { shell_menu_page_transition_slide, u8"밀기" },
            { shell_menu_page_transition_fade, u8"옅어짐" },
            { shell_menu_page_transition_zoom, u8"확대" },
            { shell_menu_page_transition_spring, u8"스프링 밀기" },
        };

        [[nodiscard]] std::u8string_view label_of(const std::u8string_view key) noexcept
        {
            for (const command_name& name : command_names)
                if (name.key == key)
                    return name.label;
            return key;
        }

        [[nodiscard]] std::u8string_view bar_label_of(const std::u8string_view key) noexcept
        {
            if (key == u8"file")
                return u8"파일";
            if (key == u8"edit")
                return u8"편집";
            if (key == u8"view")
                return u8"보기";
            return key;
        }

        [[nodiscard]] luil::menu_item_config command(const std::u8string_view key, const char32_t access_key, std::u8string shortcut = {})
        {
            return { .key = std::u8string { key }, .label = std::u8string { label_of(key) }, .shortcut_text = std::move(shortcut), .access_key = access_key };
        }

        [[nodiscard]] std::vector<luil::menu_item_config> items_of(const std::u8string_view menu, const bool sidebar_visible, const luil::theme_preference theme, const std::u8string_view page_transition)
        {
            if (menu == u8"file")
                return { command(u8"file.new", U'n', u8"Ctrl+N"), command(u8"file.open", U'o', u8"Ctrl+O"), command(u8"file.save", U's', u8"Ctrl+S") };
            if (menu == u8"edit")
            {
                std::vector<luil::menu_item_config> items { command(u8"edit.undo", U'u', u8"Ctrl+Z"), command(u8"edit.redo", U'r', u8"Ctrl+Y") };
                luil::menu_item_config cut { command(u8"edit.cut", U't', u8"Ctrl+X") };
                cut.separator_above = true;
                items.push_back(std::move(cut));
                items.push_back(command(u8"edit.copy", U'c', u8"Ctrl+C"));
                items.push_back(command(u8"edit.paste", U'p', u8"Ctrl+V"));
                return items;
            }
            if (menu != u8"view")
                return {};
            luil::menu_item_config sidebar { command(shell_menu_sidebar, U's', u8"Ctrl+B") };
            sidebar.kind = luil::menu_item_kind::check;
            sidebar.checked = sidebar_visible;
            const auto radio = [theme](const std::u8string_view key, const char32_t access_key, const luil::theme_preference value) {
                luil::menu_item_config item { command(key, access_key) };
                item.kind = luil::menu_item_kind::radio;
                item.checked = theme == value;
                return item;
            };
            luil::menu_item_config themes {};
            themes.key = std::u8string { theme_submenu };
            themes.label = u8"테마";
            themes.access_key = U't';
            themes.children = {
                radio(shell_menu_theme_light, U'l', luil::theme_preference::light),
                radio(shell_menu_theme_system, U's', luil::theme_preference::system),
                radio(shell_menu_theme_dark, U'd', luil::theme_preference::dark),
            };
            // 페이지를 바꿀 때의 전환이다. 고른 것이 라디오로 선다.
            const auto transition = [page_transition](const std::u8string_view key, const char32_t access_key) {
                luil::menu_item_config item { command(key, access_key) };
                item.kind = luil::menu_item_kind::radio;
                item.checked = page_transition == key;
                return item;
            };
            luil::menu_item_config transitions {};
            transitions.key = std::u8string { page_transition_submenu };
            transitions.label = u8"페이지 전환";
            transitions.access_key = U'p';
            transitions.children = {
                transition(shell_menu_page_transition_none, U'n'),
                transition(shell_menu_page_transition_slide, U's'),
                transition(shell_menu_page_transition_fade, U'f'),
                transition(shell_menu_page_transition_zoom, U'z'),
                transition(shell_menu_page_transition_spring, U'p'),
            };
            return { std::move(sidebar), std::move(themes), std::move(transitions) };
        }
    }

    bool shell_menu::handle(const luil::app_message& message)
    {
        if (const auto* const next { message.get<shell_menu_state_intent>() }; next != nullptr)
        {
            // 막대가 쉬거나 다른 메뉴로 옮기면 열린 하위 메뉴도 닫는다.
            if (next->state.open == false || next->state.current != state_.current)
                submenu_.clear();
            state_ = next->state;
            return true;
        }
        if (const auto* const submenu { message.get<shell_menu_submenu_intent>() }; submenu != nullptr)
        {
            submenu_ = submenu->key;
            return true;
        }
        if (const auto* const select { message.get<shell_menu_select_intent>() }; select != nullptr)
        {
            last_command_ = select->key;
            if (select->key == shell_menu_sidebar || select->key.starts_with(u8"theme.") || select->key.starts_with(shell_menu_page_transition_prefix))
                pending_ = select->key;
            return true;
        }
        return false;
    }

    std::optional<std::u8string> shell_menu::take_command()
    {
        return std::exchange(pending_, std::nullopt);
    }

    luil::menu_bar_config shell_menu::bar_config() const
    {
        luil::menu_bar_config config {};
        config.owner = u8"shell-menu";
        // 한글 라벨에는 글자가 없어 "파일(F)"로 그린다. 접근 이름은 "파일"이고 키는 AccessKey로 읽힌다.
        config.items = {
            { .key = u8"file", .label = u8"파일", .access_key = U'f' },
            { .key = u8"edit", .label = u8"편집", .access_key = U'e' },
            { .key = u8"view", .label = u8"보기", .access_key = U'v' },
        };
        config.state = state_;
        config.set_state = [](const luil::menu_bar_state& state) { return luil::make_app_action(shell_menu_state_intent { state }); };
        return config;
    }

    std::unique_ptr<luil::menu_bar_element> shell_menu::make_bar() const
    {
        return std::make_unique<luil::menu_bar_element>(bar_config());
    }

    std::u8string shell_menu::state_text() const
    {
        if (state_.current.empty())
            return u8"메뉴 막대: 쉼";
        std::u8string text { bar_label_of(state_.current) };
        if (state_.open)
            text += u8" 열림";
        text += state_.keyboard ? u8" · 키보드" : u8" · 포인터";
        return text;
    }

    std::u8string shell_menu::last_command_text() const
    {
        return u8"마지막 메뉴 명령: " + (last_command_.empty() ? std::u8string { u8"없음" } : std::u8string { label_of(last_command_) });
    }

    std::unique_ptr<luil::label_element> shell_menu::make_state_label() const
    {
        return make_label({ kind_text, u8"shell-menu-state" }, state_text(), 11.0f, luil::label_color_role::dim);
    }

    std::unique_ptr<luil::label_element> shell_menu::make_last_label() const
    {
        return make_label({ kind_text, u8"shell-menu-last" }, last_command_text(), 11.0f, luil::label_color_role::dim);
    }

    std::vector<luil::ui_popup> shell_menu::make_popups(
        const luil::menu_bar_element& bar, const float scale, const luil::rect_f& limit, const bool sidebar_visible, const luil::theme_preference theme, const std::u8string_view page_transition) const
    {
        std::vector<luil::ui_popup> popups {};
        if (state_.open == false || state_.current.empty())
            return popups;
        std::vector<luil::menu_item_config> items { items_of(state_.current, sidebar_visible, theme, page_transition) };
        if (items.empty())
            return popups;

        const auto select = [](const std::u8string& key) { return luil::make_app_action(shell_menu_select_intent { key }); };
        luil::menu_config menu {};
        menu.items = items;
        menu.select = select;
        // 하위 메뉴의 열림 경로는 앱 상태다. 오른쪽·Enter·hover·접근 키가 열고 왼쪽·Esc가 한 단계만 닫는다.
        menu.open_submenu = [](const std::u8string& key) { return luil::make_app_action(shell_menu_submenu_intent { key }); };
        menu.close_submenu = luil::make_message_action(shell_menu_submenu_intent {});
        menu.opened_key = submenu_;
        // 고르면 막대가 쉰다 (`make_menu_bar_popup`이 `after_select`를 채운다).
        luil::menu_popup_config config {};
        config.id = std::u8string { bar_popup_id };
        config.scale = scale;
        luil::ui_popup top { luil::make_menu_bar_popup(bar, std::move(menu), std::move(config), limit) };

        if (submenu_.empty() == false)
        {
            // 하위 메뉴는 부모 popup 기준 자리에 선다. 붙는 항목의 위 끝까지의 높이를 앞 항목들로 센다.
            float submenu_y { luil::menu_padding };
            for (const luil::menu_item_config& item : items)
            {
                if (item.separator_above)
                    submenu_y += luil::menu_separator_height;
                if (item.key == submenu_ && item.children.empty() == false)
                {
                    const std::u8string popup_id { submenu_popup_id(item.key) };
                    luil::menu_config child {};
                    child.owner = popup_id;
                    child.items = item.children;
                    child.select = select;
                    // 고르면 하위 메뉴와 막대가 함께 닫힌다.
                    child.after_select = luil::make_message_action(shell_menu_state_intent {});
                    child.close_submenu = luil::make_message_action(shell_menu_submenu_intent {});
                    child.depth = 1;
                    child.parent_surface = std::u8string { bar_popup_id };
                    child.anchor_key = item.key;
                    luil::menu_popup_config child_popup {};
                    child_popup.id = popup_id;
                    child_popup.anchor = std::u8string { bar_popup_id };
                    child_popup.x = top.width;
                    child_popup.y = submenu_y - luil::menu_padding;
                    child_popup.scale = scale;
                    // Esc는 하위 메뉴만 닫는다. 그 밖의 닫힘은 막대도 쉬게 한다.
                    child_popup.dismiss = [](const luil::popup_dismiss_reason reason) {
                        return reason == luil::popup_dismiss_reason::escape_key ? luil::make_app_action(shell_menu_submenu_intent {}) : luil::make_app_action(shell_menu_state_intent {});
                    };
                    popups.push_back(std::move(top));
                    popups.push_back(luil::make_menu_popup(std::move(child), std::move(child_popup)));
                    return popups;
                }
                submenu_y += luil::menu_row_height();
            }
        }
        popups.push_back(std::move(top));
        return popups;
    }
}

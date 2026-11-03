#pragma once

#include "luil/app/app_host.h"
#include "luil/theme/appearance.h"
#include "luil/ui/app_message.h"
#include "luil/ui/label_element.h"
#include "luil/ui/menu_bar_element.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demo {
    // 셸 메뉴 막대의 상태를 이것으로 하라는 메시지다 (`menu_bar_config::set_state`).
    struct shell_menu_state_intent
    {
        luil::menu_bar_state state {};
    };

    // 막대 메뉴에서 고른 명령이다. 키는 `file.new`처럼 메뉴 이름과 명령을 점으로 잇는다.
    struct shell_menu_select_intent
    {
        std::u8string key {};
    };

    // 보기 메뉴의 하위 메뉴("테마")를 연다. 빈 키는 닫는다.
    struct shell_menu_submenu_intent
    {
        std::u8string key {};
    };

    // 셸이 실행할 명령의 키다. 나머지 명령은 마지막 명령 label만 바꾼다.
    inline constexpr std::u8string_view shell_menu_sidebar { u8"view.sidebar" };
    inline constexpr std::u8string_view shell_menu_theme_light { u8"theme.light" };
    inline constexpr std::u8string_view shell_menu_theme_system { u8"theme.system" };
    inline constexpr std::u8string_view shell_menu_theme_dark { u8"theme.dark" };

    // 창 위의 "파일(F)·편집(E)·보기(V)" 막대다 (docs/menu-bar-design.md의 확인 절).
    //  - 열림·선 자리·하위 메뉴는 이 상태가 든다. 막대와 popup은 frame마다 이 상태로 새로 짓는다.
    //  - 막대 오른쪽 label 둘이 상태("파일 · 키보드")와 마지막 명령을 그려 UIA가 이름으로 되읽는다.
    //  - 고른 명령 중 셸의 것(사이드바, 테마)은 `take_command`로 넘긴다. 이 상태는 셸을 모른다.
    class shell_menu
    {
    public:
        // 막대 메시지면 참이다.
        bool handle(const luil::app_message& message);
        // 고른 셸 명령이다 (`shell_menu_sidebar` 같은 것). 한 번 꺼내면 빈다.
        [[nodiscard]] std::optional<std::u8string> take_command();

        [[nodiscard]] const luil::menu_bar_state& state() const noexcept
        {
            return state_;
        }

        [[nodiscard]] luil::menu_bar_config bar_config() const;
        [[nodiscard]] std::unique_ptr<luil::menu_bar_element> make_bar() const;
        // "메뉴 막대: 쉼", "파일 · 키보드", "편집 열림 · 포인터"다.
        [[nodiscard]] std::u8string state_text() const;
        // "마지막 메뉴 명령: 새로 만들기"다. 고른 적이 없으면 "없음"이다.
        [[nodiscard]] std::u8string last_command_text() const;
        // 막대 오른쪽의 label 둘이다 (owner `shell-menu-state`·`shell-menu-last`).
        [[nodiscard]] std::unique_ptr<luil::label_element> make_state_label() const;
        [[nodiscard]] std::unique_ptr<luil::label_element> make_last_label() const;

        // 열린 메뉴와 하위 메뉴다. `bar`는 이번 frame에 배치한 막대이고 `limit`은 주 창 client(물리 픽셀)다.
        //  - 보기 메뉴의 체크와 라디오는 셸 상태(`sidebar_visible`, `theme`)를 그대로 그린다.
        [[nodiscard]] std::vector<luil::ui_popup> make_popups(const luil::menu_bar_element& bar, float scale, const luil::rect_f& limit, bool sidebar_visible, luil::theme_preference theme) const;

    private:
        luil::menu_bar_state state_ {};
        std::u8string submenu_ {};
        std::u8string last_command_ {};
        std::optional<std::u8string> pending_ {};
    };
}

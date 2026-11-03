#include "demo/windows_page.h"

#include "luil/ui/scroll_area_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 시험이 바꾼 플랫폼 성질을 되돌린다. 성질은 프로세스 전역이다.
    struct platform_scope
    {
        explicit platform_scope(const luil::ui_platform& platform)
            : previous { luil::current_ui_platform() }
        {
            luil::set_ui_platform(platform);
        }

        platform_scope(const platform_scope&) = delete;
        platform_scope(platform_scope&&) = delete;
        platform_scope& operator=(const platform_scope&) = delete;
        platform_scope& operator=(platform_scope&&) = delete;

        ~platform_scope()
        {
            luil::set_ui_platform(previous);
        }

        luil::ui_platform previous;
    };

    void deliver(demo::windows_page& page, luil::app_message message)
    {
        REQUIRE(page.handle(message));
    }

    template<typename Message>
    void deliver(demo::windows_page& page, Message message)
    {
        deliver(page, luil::app_message { std::move(message) });
    }

    // 900 × 900 논리 픽셀에 배치한 창 페이지다. 작업 표시줄 구역이 모두 보이도록 넉넉히 잡는다.
    [[nodiscard]] luil::ui_tree page_tree(demo::windows_page& page)
    {
        return luil::make_arranged_tree(page.build(900.0f, 900.0f, 1.0f), { 0.0f, 0.0f, 900.0f, 900.0f }, 1.0f);
    }

    // 데모 셸이 페이지에 주는 본문 칸의 크기다 (논리 픽셀).
    struct page_size
    {
        float width { 0.0f };
        float height { 0.0f };
    };

    // 기본 창(1280 x 720 물리 픽셀)에서 사이드바와 캡션을 뺀 본문 칸이다.
    // 사이드바 폭과 캡션 높이는 셸이 정하는 값이라 어림이다. 요점은 구역을 모두 쌓은 높이가 이보다 길다는 것이고,
    // 배율이 커질수록 같은 창이 논리 픽셀로는 작아진다.
    [[nodiscard]] page_size default_page_size(const float scale)
    {
        constexpr float window_width { 1280.0f };
        constexpr float window_height { 720.0f };
        constexpr float sidebar_width { 240.0f };
        constexpr float caption_height { 32.0f };
        return { window_width / scale - sidebar_width, window_height / scale - caption_height };
    }

    // 셸이 하는 대로 본문 칸에 배치한 창 페이지다.
    [[nodiscard]] luil::ui_tree sized_page_tree(demo::windows_page& page, const page_size size, const float scale)
    {
        return luil::make_arranged_tree(page.build(size.width, size.height, scale), { 0.0f, 0.0f, size.width * scale, size.height * scale }, scale);
    }

    [[nodiscard]] const luil::scroll_area_element& scroll_area_of(const luil::ui_tree& tree)
    {
        const auto* const area { dynamic_cast<const luil::scroll_area_element*>(tree.find(luil::ui_element_id { luil::ui_element_kind::scroll_area, u8"windows" })) };
        REQUIRE(area != nullptr);
        return *area;
    }

    // 맨 아래 구역의 단추다 (전역 단축키 줄 끝).
    constexpr std::u8string_view bottom_button_key { u8"hotkey-conflict" };

    [[nodiscard]] const luil::ui_element& bottom_button_of(const luil::ui_tree& tree)
    {
        const luil::ui_element* const button { tree.find(luil::ui_element_id { demo::kind_tool_toggle, std::u8string { bottom_button_key } }) };
        REQUIRE(button != nullptr);
        return *button;
    }

    // 상태 줄이 말해야 할 글이다: 흘린 양 / 흘릴 수 있는 최대 (논리 픽셀, 반올림).
    [[nodiscard]] std::u8string scroll_status_of(const float offset, const float maximum)
    {
        return u8"페이지 흘림: " + demo::to_u8(static_cast<int>(std::lround(offset))) + u8" / " + demo::to_u8(static_cast<int>(std::lround(maximum)));
    }

    // 첫 input_action에서 창 페이지의 흘림 메시지를 꺼낸다.
    [[nodiscard]] const demo::windows_scroll_intent* single_scroll(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        return message != nullptr ? message->get<demo::windows_scroll_intent>() : nullptr;
    }

    // 화면의 글이 곧 보조 기술이 읽는 이름이다.
    [[nodiscard]] std::u8string name_of(const luil::ui_tree& tree, const std::u8string_view owner)
    {
        const luil::ui_element* const label { tree.find({ demo::kind_text, std::u8string { owner } }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] const luil::win32::ui_window* find_window(const std::vector<luil::win32::ui_window>& windows, const std::u8string_view id)
    {
        for (const luil::win32::ui_window& window : windows)
        {
            if (window.id == id)
                return &window;
        }
        return nullptr;
    }
}

TEST_CASE("The taskbar section reads its capability, request and report as three lines", "[windows-page][taskbar]")
{
    demo::windows_page page {};
    {
        const platform_scope scope { luil::ui_platform {} };
        const auto tree { page_tree(page) };
        REQUIRE(name_of(tree, u8"windows-taskbar-capability") == u8"작업 표시줄 능력: 없음");
        REQUIRE(name_of(tree, u8"windows-taskbar-request") == u8"작업 표시줄 요청: 없음 · 오버레이 끔");
        REQUIRE(name_of(tree, u8"windows-taskbar-report") == u8"작업 표시줄 반영: 아직 보고 없음");
        REQUIRE(name_of(tree, u8"windows-taskbar-window") == u8"작업 표시줄 독립 창: 닫힘");
    }
    {
        luil::ui_platform desktop {};
        desktop.capabilities.taskbar = true;
        const platform_scope scope { desktop };
        REQUIRE(name_of(page_tree(page), u8"windows-taskbar-capability") == u8"작업 표시줄 능력: 있음");
    }
}

TEST_CASE("The request line follows the state, the value and the overlay", "[windows-page][taskbar]")
{
    demo::windows_page page {};
    deliver(page, demo::taskbar_state_intent { luil::taskbar_progress_state::normal });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 정상 42% · 오버레이 끔");

    // 막대를 끌면 변화량이, 보조 기술은 목표 값을 보낸다. 범위 밖은 자른다.
    deliver(page, demo::taskbar_value_intent { 70.0f, false });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 정상 70% · 오버레이 끔");
    deliver(page, demo::taskbar_value_intent { 5.0f, true });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 정상 75% · 오버레이 끔");
    deliver(page, demo::taskbar_value_intent { 90.0f, true });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 정상 100% · 오버레이 끔");
    deliver(page, demo::taskbar_value_intent { -500.0f, false });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 정상 0% · 오버레이 끔");

    deliver(page, demo::taskbar_state_intent { luil::taskbar_progress_state::error });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 오류 0% · 오버레이 끔");
    deliver(page, demo::taskbar_state_intent { luil::taskbar_progress_state::indeterminate });
    deliver(page, demo::taskbar_overlay_intent { true });
    // 값 없는 상태는 퍼센트를 적지 않는다.
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-request") == u8"작업 표시줄 요청: 불확정 · 오버레이 켬");
}

TEST_CASE("The page hands the shell the same overlay picture every frame", "[windows-page][taskbar]")
{
    demo::windows_page page {};
    REQUIRE(page.make_taskbar_request().overlay.icon.valid() == false);

    deliver(page, demo::taskbar_overlay_intent { true });
    const luil::taskbar_request first { page.make_taskbar_request() };
    const luil::taskbar_request second { page.make_taskbar_request() };
    REQUIRE(first.overlay.icon.valid());
    REQUIRE(first.overlay.description.empty() == false);
    // 새로 지은 그림이면 셸 쪽이 매 frame 바뀐 그림으로 읽는다.
    REQUIRE(first.overlay.icon.backing() == second.overlay.icon.backing());

    // 껐다 켜도 처음에 지어 둔 그림이다.
    deliver(page, demo::taskbar_overlay_intent { false });
    REQUIRE(page.make_taskbar_request().overlay.icon.valid() == false);
    deliver(page, demo::taskbar_overlay_intent { true });
    REQUIRE(page.make_taskbar_request().overlay.icon.backing() == first.overlay.icon.backing());
}

TEST_CASE("The independent window carries its own taskbar request and cycles it", "[windows-page][taskbar]")
{
    demo::windows_page page {};
    REQUIRE(find_window(page.make_windows(), u8"taskbar-window") == nullptr);

    deliver(page, demo::taskbar_window_toggle_intent { true });
    {
        const std::vector<luil::win32::ui_window> windows { page.make_windows() };
        const luil::win32::ui_window* const window { find_window(windows, u8"taskbar-window") };
        REQUIRE(window != nullptr);
        // 독립 창이어야 작업 표시줄에 따로 서서 자기 단추가 있다.
        REQUIRE(window->independent);
        REQUIRE(window->tree != nullptr);
        REQUIRE(window->taskbar.progress.state == luil::taskbar_progress_state::normal);
        REQUIRE(window->taskbar.progress.value == 0.6f);
        REQUIRE(window->taskbar.overlay.icon.valid() == false);
        REQUIRE(window->metrics != nullptr);
        REQUIRE(window->close != nullptr);
    }
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-window") == u8"작업 표시줄 독립 창: 요청 정상 60% · 반영 아직 보고 없음");

    const std::vector<luil::taskbar_progress_state> cycle {
        luil::taskbar_progress_state::error,
        luil::taskbar_progress_state::paused,
        luil::taskbar_progress_state::indeterminate,
        luil::taskbar_progress_state::none,
        luil::taskbar_progress_state::normal,
    };
    for (const luil::taskbar_progress_state expected : cycle)
    {
        deliver(page, demo::taskbar_window_step_intent {});
        const std::vector<luil::win32::ui_window> windows { page.make_windows() };
        const luil::win32::ui_window* const window { find_window(windows, u8"taskbar-window") };
        REQUIRE(window != nullptr);
        REQUIRE(window->taskbar.progress.state == expected);
    }

    // 주 창의 요청은 독립 창과 따로다.
    REQUIRE(page.make_taskbar_request().progress.state == luil::taskbar_progress_state::none);
}

TEST_CASE("Reports reach the line of the window they name and closing forgets them", "[windows-page][taskbar]")
{
    demo::windows_page page {};
    deliver(page, demo::taskbar_window_toggle_intent { true });

    luil::win32::taskbar_report main_report {};
    main_report.status = luil::win32::taskbar_status::waiting;
    deliver(page, demo::taskbar_report_intent { main_report });
    luil::win32::taskbar_report window_report {};
    window_report.surface = u8"taskbar-window";
    window_report.status = luil::win32::taskbar_status::failed;
    window_report.error = u8"SetProgressState failed (HRESULT=0x80004005)";
    deliver(page, demo::taskbar_report_intent { window_report });

    const auto tree { page_tree(page) };
    REQUIRE(name_of(tree, u8"windows-taskbar-report") == u8"작업 표시줄 반영: 대기 중");
    REQUIRE(name_of(tree, u8"windows-taskbar-window") == u8"작업 표시줄 독립 창: 요청 정상 60% · 반영 실패 (SetProgressState failed (HRESULT=0x80004005))");

    // 창을 닫으면 그 창의 보고는 잊는다. 다시 열면 처음부터 다시 받는다.
    deliver(page, demo::taskbar_window_toggle_intent { false });
    deliver(page, demo::taskbar_window_toggle_intent { true });
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-window") == u8"작업 표시줄 독립 창: 요청 정상 60% · 반영 아직 보고 없음");
    // 주 창의 보고는 그대로다.
    REQUIRE(name_of(page_tree(page), u8"windows-taskbar-report") == u8"작업 표시줄 반영: 대기 중");
}

TEST_CASE("The instance line names the arguments and the working directory a second instance sent", "[windows-page][instance]")
{
    demo::windows_page page {};
    REQUIRE(name_of(page_tree(page), u8"windows-instance-arguments") == u8"둘째 인스턴스 인자: 없음");

    luil::instance_arguments received {};
    received.arguments = { u8"a.txt", u8"b c" };
    received.working_directory = u8"C:\\work dir";
    deliver(page, demo::instance_arguments_intent { received });
    REQUIRE(name_of(page_tree(page), u8"windows-instance-arguments") == u8"둘째 인스턴스 인자: 2개 [a.txt] [b c] · 작업 폴더 C:\\work dir");

    // 다음에 받은 것이 앞의 것을 덮는다. 인자가 없어도 받은 것이라 없음과 다르다.
    luil::instance_arguments none {};
    none.working_directory = u8"D:\\";
    deliver(page, demo::instance_arguments_intent { none });
    REQUIRE(name_of(page_tree(page), u8"windows-instance-arguments") == u8"둘째 인스턴스 인자: 0개 · 작업 폴더 D:\\");

    // 구역의 안내 글도 읽힌다.
    REQUIRE(name_of(page_tree(page), u8"windows-instance-hint").empty() == false);
}

TEST_CASE("The taskbar controls are buttons a person can read by name", "[windows-page][taskbar]")
{
    demo::windows_page page {};
    const auto tree { page_tree(page) };
    for (const std::u8string_view key : { u8"taskbar-none", u8"taskbar-indeterminate", u8"taskbar-normal", u8"taskbar-error", u8"taskbar-paused", u8"taskbar-overlay", u8"taskbar-window-toggle", u8"taskbar-window-step" })
    {
        const luil::ui_element* const button { tree.find({ demo::kind_tool_toggle, std::u8string { key } }) };
        REQUIRE(button != nullptr);
        REQUIRE(button->accessibility().name.empty() == false);
    }
    REQUIRE(tree.find({ demo::kind_layout, u8"windows-taskbar-value" }) != nullptr);
}

TEST_CASE("The jump list lines read the request and the report", "[windows-page][jump-list]")
{
    demo::windows_page page {};
    {
        const auto tree { page_tree(page) };
        REQUIRE(name_of(tree, u8"windows-jump-list-request") == u8"점프 목록 요청: 손대지 않음");
        REQUIRE(name_of(tree, u8"windows-jump-list-report") == u8"점프 목록 반영: 아직 보고 없음");
        REQUIRE(name_of(tree, u8"windows-jump-list-hint").empty() == false);
        for (const std::u8string_view key : { u8"jump-list-fill", u8"jump-list-clear" })
        {
            const luil::ui_element* const button { tree.find({ demo::kind_tool_toggle, std::u8string { key } }) };
            REQUIRE(button != nullptr);
            REQUIRE(button->accessibility().name.empty() == false);
        }
    }
    // 누르기 전에는 셸의 목록을 만지지 않는다.
    REQUIRE(page.make_taskbar_request().jump_list.has_value() == false);

    deliver(page, demo::jump_list_fill_intent { true });
    REQUIRE(name_of(page_tree(page), u8"windows-jump-list-request") == u8"점프 목록 요청: 작업 2개");
    const luil::taskbar_request filled { page.make_taskbar_request() };
    REQUIRE(filled.jump_list.has_value());
    REQUIRE(filled.jump_list->tasks.size() == 2);
    // 작업은 데모를 둘째로 띄워 떠 있는 데모에 인자를 넘긴다.
    REQUIRE(filled.jump_list->tasks[0].title == u8"메모로 보내기");
    REQUIRE(filled.jump_list->tasks[0].arguments == std::vector<std::u8string> { u8"--send-to-first", u8"jump:note" });
    REQUIRE(filled.jump_list->tasks[1].arguments == std::vector<std::u8string> { u8"--send-to-first", u8"jump:image" });
    REQUIRE(filled.jump_list->tasks[0].description.empty() == false);
    // 같은 목록을 frame마다 실어도 값이 같아 셸은 한 번만 짓는다.
    REQUIRE(page.make_taskbar_request().jump_list == filled.jump_list);

    luil::win32::jump_list_report applied {};
    applied.status = luil::win32::jump_list_status::applied;
    applied.tasks = 2;
    deliver(page, demo::jump_list_report_intent { applied });
    REQUIRE(name_of(page_tree(page), u8"windows-jump-list-report") == u8"점프 목록 반영: 적용됨 · 작업 2개");

    deliver(page, demo::jump_list_fill_intent { false });
    REQUIRE(name_of(page_tree(page), u8"windows-jump-list-request") == u8"점프 목록 요청: 비움");
    const luil::taskbar_request cleared { page.make_taskbar_request() };
    REQUIRE(cleared.jump_list.has_value());
    REQUIRE(cleared.jump_list->tasks.empty());

    luil::win32::jump_list_report removed {};
    removed.status = luil::win32::jump_list_status::removed;
    deliver(page, demo::jump_list_report_intent { removed });
    REQUIRE(name_of(page_tree(page), u8"windows-jump-list-report") == u8"점프 목록 반영: 지움");

    luil::win32::jump_list_report failed {};
    failed.status = luil::win32::jump_list_status::failed;
    failed.error = u8"CommitList failed (HRESULT=0x80004005)";
    deliver(page, demo::jump_list_report_intent { failed });
    REQUIRE(name_of(page_tree(page), u8"windows-jump-list-report") == u8"점프 목록 반영: 실패 (CommitList failed (HRESULT=0x80004005))");

    // 목록은 앱마다 하나라 독립 창의 요청에는 싣지 않는다.
    deliver(page, demo::taskbar_window_toggle_intent { true });
    const std::vector<luil::win32::ui_window> windows { page.make_windows() };
    const luil::win32::ui_window* const window { find_window(windows, u8"taskbar-window") };
    REQUIRE(window != nullptr);
    REQUIRE(window->taskbar.jump_list.has_value() == false);
}

TEST_CASE("The page body scrolls so the bottom section is reachable at the default window size", "[windows-page][scroll]")
{
    // 구역을 모두 쌓은 높이는 기본 창보다 길다. 흘리지 못하던 때는 맨 아래 구역이 잘렸다.
    // 배율이 커도 같은 창이 논리 픽셀로 작아질 뿐이라 어느 배율에서나 닿아야 한다.
    for (const float scale : { 1.0f, 1.25f, 1.5f, 2.0f })
    {
        CAPTURE(scale);
        const page_size size { default_page_size(scale) };
        demo::windows_page page {};
        {
            const auto tree { sized_page_tree(page, size, scale) };
            const luil::scroll_area_element& area { scroll_area_of(tree) };
            REQUIRE(area.metrics().overflowing());
            // 흘리기 전에는 가려져 있다: 보이는 자리가 없다.
            REQUIRE(tree.visible_bounds(bottom_button_of(tree)).has_value() == false);
            REQUIRE(name_of(tree, u8"windows-scroll") == scroll_status_of(0.0f, area.metrics().maximum_scroll));
            REQUIRE(tree.unarranged().empty());
        }

        // 아래로 끝까지 흘린다. 위치는 지을 때 끝에서 다듬는다.
        deliver(page, demo::windows_scroll_intent { 100000.0f });
        const auto tree { sized_page_tree(page, size, scale) };
        const luil::scroll_area_element& area { scroll_area_of(tree) };
        const luil::scroll_metrics& metrics { area.metrics() };
        REQUIRE(metrics.scroll_offset == metrics.maximum_scroll);
        REQUIRE(name_of(tree, u8"windows-scroll") == scroll_status_of(metrics.maximum_scroll, metrics.maximum_scroll));

        // 가려졌던 단추가 통째로 영역 안에 보인다.
        const luil::ui_element& button { bottom_button_of(tree) };
        const std::optional<luil::rect_f> shown { tree.visible_bounds(button) };
        REQUIRE(shown.has_value());
        REQUIRE(shown->height == button.bounds().height);
        REQUIRE(shown->width == button.bounds().width);
        const luil::rect_f viewport { area.viewport() };
        REQUIRE(button.bounds().y >= viewport.y);
        REQUIRE(button.bounds().y + button.bounds().height <= viewport.y + viewport.height);
    }
}

TEST_CASE("The status line stays outside the scrolling body", "[windows-page][scroll]")
{
    // 흘려도 같은 자리에 서야 UIA가 위치를 되읽는다.
    demo::windows_page page {};
    const page_size size { default_page_size(1.0f) };
    const auto top { sized_page_tree(page, size, 1.0f) };
    const luil::ui_element* const label_top { top.find(luil::ui_element_id { demo::kind_text, u8"windows-scroll" }) };
    REQUIRE(label_top != nullptr);
    const luil::rect_f resting { label_top->bounds() };
    REQUIRE(top.visible_bounds(*label_top).has_value());

    deliver(page, demo::windows_scroll_intent { 100000.0f });
    const auto bottom { sized_page_tree(page, size, 1.0f) };
    const luil::ui_element* const label_bottom { bottom.find(luil::ui_element_id { demo::kind_text, u8"windows-scroll" }) };
    REQUIRE(label_bottom != nullptr);
    REQUIRE(label_bottom->bounds().y == resting.y);
    REQUIRE(bottom.visible_bounds(*label_bottom).has_value());
}

TEST_CASE("The wheel and the focus reveal find the page body without a routing table", "[windows-page][scroll]")
{
    // 셸의 `on_wheel`·`on_focus_moved`는 표에 없는 컨테이너를 `route_wheel`·`route_reveal`의 표 없는 짝에 맡긴다.
    // 이 페이지가 셸을 고치지 않고 흐르는 까닭이라 여기서 잠근다.
    demo::windows_page page {};
    const page_size size { default_page_size(1.0f) };
    {
        const auto tree { sized_page_tree(page, size, 1.0f) };
        const luil::rect_f viewport { scroll_area_of(tree).viewport() };
        const std::vector<luil::input_action> wheel { luil::route_wheel(tree, viewport.x + viewport.width / 2.0f, viewport.y + viewport.height / 2.0f, 48.0f) };
        const demo::windows_scroll_intent* const scroll { single_scroll(wheel) };
        REQUIRE(scroll != nullptr);
        REQUIRE(scroll->delta > 0.0f);
        deliver(page, *scroll);
    }
    {
        const auto tree { sized_page_tree(page, size, 1.0f) };
        REQUIRE(scroll_area_of(tree).metrics().scroll_offset > 0.0f);

        // 가려진 단추에 초점이 가면 그 단추를 들이는 만큼 흐른다.
        const luil::ui_element_id bottom_id { demo::kind_tool_toggle, std::u8string { bottom_button_key } };
        const std::vector<luil::input_action> reveal { luil::route_reveal(tree, bottom_id) };
        const demo::windows_scroll_intent* const scroll { single_scroll(reveal) };
        REQUIRE(scroll != nullptr);
        REQUIRE(scroll->delta > 0.0f);
        deliver(page, *scroll);
    }
    const auto tree { sized_page_tree(page, size, 1.0f) };
    REQUIRE(tree.visible_bounds(bottom_button_of(tree)).has_value());
    // 이미 보이면 되살릴 것이 없다.
    REQUIRE(luil::route_reveal(tree, luil::ui_element_id { demo::kind_tool_toggle, std::u8string { bottom_button_key } }).empty());
}

TEST_CASE("The scroll position follows the window and never leaves its range", "[windows-page][scroll]")
{
    demo::windows_page page {};
    const page_size size { default_page_size(1.0f) };
    const float maximum { scroll_area_of(sized_page_tree(page, size, 1.0f)).metrics().maximum_scroll };
    REQUIRE(maximum > 0.0f);

    // 위쪽 끝은 메시지가, 아래쪽 끝은 지을 때가 다듬는다.
    deliver(page, demo::windows_scroll_intent { -500.0f });
    REQUIRE(name_of(sized_page_tree(page, size, 1.0f), u8"windows-scroll") == scroll_status_of(0.0f, maximum));
    deliver(page, demo::windows_scroll_to_intent { 100.0f });
    REQUIRE(name_of(sized_page_tree(page, size, 1.0f), u8"windows-scroll") == scroll_status_of(100.0f, maximum));
    deliver(page, demo::windows_scroll_to_intent { -40.0f });
    REQUIRE(name_of(sized_page_tree(page, size, 1.0f), u8"windows-scroll") == scroll_status_of(0.0f, maximum));
    deliver(page, demo::windows_scroll_to_intent { maximum * 10.0f });
    REQUIRE(name_of(sized_page_tree(page, size, 1.0f), u8"windows-scroll") == scroll_status_of(maximum, maximum));

    // 창이 구역을 다 담을 만큼 커지면 흘릴 것이 없고, 끝에 쌓였던 위치도 되돌아간다.
    const page_size tall { size.width, 4000.0f };
    {
        const auto tree { sized_page_tree(page, tall, 1.0f) };
        REQUIRE(scroll_area_of(tree).metrics().overflowing() == false);
        REQUIRE(name_of(tree, u8"windows-scroll") == scroll_status_of(0.0f, 0.0f));
        // 모든 구역이 흘리지 않고도 보인다.
        REQUIRE(tree.visible_bounds(bottom_button_of(tree)).has_value());
    }
    // 되돌아간 위치가 남아, 창이 다시 작아지면 맨 위에서 시작한다.
    REQUIRE(name_of(sized_page_tree(page, size, 1.0f), u8"windows-scroll") == scroll_status_of(0.0f, maximum));
}

TEST_CASE("The page keeps the status labels it had before it scrolled", "[windows-page][scroll]")
{
    // 본문을 영역에 담아도 기존 줄의 이름은 그대로 읽힌다. 흘리기 전의 시험들이 쓰는 이름 몇 개를 기본 창에서도 확인한다.
    demo::windows_page page {};
    const auto tree { sized_page_tree(page, default_page_size(1.0f), 1.0f) };
    REQUIRE(name_of(tree, u8"windows-taskbar-request") == u8"작업 표시줄 요청: 없음 · 오버레이 끔");
    REQUIRE(name_of(tree, u8"windows-instance-arguments") == u8"둘째 인스턴스 인자: 없음");
    REQUIRE(name_of(tree, u8"windows-jump-list-request") == u8"점프 목록 요청: 손대지 않음");
    REQUIRE(tree.find(luil::ui_element_id { demo::kind_tool_toggle }) != nullptr);
}

namespace {
    // 메시지 하나를 담은 input_action에서 그 메시지를 꺼낸다.
    [[nodiscard]] luil::app_message message_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        REQUIRE(message != nullptr);
        return *message;
    }

    [[nodiscard]] luil::win32::tray_report tray_report_with(const luil::win32::tray_status status, std::u8string error = {})
    {
        luil::win32::tray_report report {};
        report.status = status;
        report.error = std::move(error);
        return report;
    }
}

TEST_CASE("The tray section reads its capability, request, report and event as four lines", "[windows-page][tray]")
{
    demo::windows_page page {};
    {
        const platform_scope scope { luil::ui_platform {} };
        const auto tree { page_tree(page) };
        REQUIRE(name_of(tree, u8"windows-tray-capability") == u8"트레이 능력: 없음");
        REQUIRE(name_of(tree, u8"windows-tray-request") == u8"트레이 요청: 끔");
        REQUIRE(name_of(tree, u8"windows-tray-report") == u8"트레이 반영: 아직 보고 없음");
        REQUIRE(name_of(tree, u8"windows-tray-event") == u8"트레이 사건: 없음");
    }
    {
        luil::ui_platform desktop {};
        desktop.capabilities.tray = true;
        const platform_scope scope { desktop };
        REQUIRE(name_of(page_tree(page), u8"windows-tray-capability") == u8"트레이 능력: 있음");
    }
    REQUIRE(page.make_tray_request().has_value() == false);
    REQUIRE(page.main_hidden() == false);
}

TEST_CASE("The tray buttons reach the request and keep the same picture every frame", "[windows-page][tray]")
{
    demo::windows_page page {};
    deliver(page, demo::tray_toggle_intent { true });
    REQUIRE(name_of(page_tree(page), u8"windows-tray-request") == u8"트레이 요청: 켬 · 그림 1 · 메뉴 있음");
    const std::optional<luil::tray_icon> first { page.make_tray_request() };
    REQUIRE(first.has_value());
    REQUIRE(first->icon.valid());
    REQUIRE(first->tooltip == u8"luil 데모 · 그림 1");
    REQUIRE(first->menu.has_value());
    REQUIRE(first->menu->items.size() == 3);
    REQUIRE(first->menu->items[0].label == u8"창 보이기");
    REQUIRE(first->menu->items[1].label == u8"주 창 숨기기");
    REQUIRE(first->menu->items[2].label == u8"끝내기");
    REQUIRE(first->menu->items[2].separator_above);
    REQUIRE(first->activate != nullptr);
    // 같은 그림을 frame마다 실어야 셸에 다시 내지 않는다.
    REQUIRE(page.make_tray_request()->icon.backing() == first->icon.backing());

    deliver(page, demo::tray_picture_intent {});
    REQUIRE(name_of(page_tree(page), u8"windows-tray-request") == u8"트레이 요청: 켬 · 그림 2 · 메뉴 있음");
    const std::optional<luil::tray_icon> second { page.make_tray_request() };
    REQUIRE(second->icon.backing() != first->icon.backing());
    REQUIRE(second->tooltip == u8"luil 데모 · 그림 2");
    deliver(page, demo::tray_picture_intent {});
    REQUIRE(page.make_tray_request()->icon.backing() == first->icon.backing());

    deliver(page, demo::tray_toggle_intent { false });
    REQUIRE(page.make_tray_request().has_value() == false);
    REQUIRE(name_of(page_tree(page), u8"windows-tray-request") == u8"트레이 요청: 끔");
}

TEST_CASE("Tray reports and events reach their lines", "[windows-page][tray]")
{
    demo::windows_page page {};
    deliver(page, demo::tray_toggle_intent { true });
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::waiting) });
    REQUIRE(name_of(page_tree(page), u8"windows-tray-report") == u8"트레이 반영: 대기 중");
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::failed, u8"boom") });
    REQUIRE(name_of(page_tree(page), u8"windows-tray-report") == u8"트레이 반영: 실패 (boom)");
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::off) });
    REQUIRE(name_of(page_tree(page), u8"windows-tray-report") == u8"트레이 반영: 끔");
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::shown) });
    REQUIRE(name_of(page_tree(page), u8"windows-tray-report") == u8"트레이 반영: 보임");

    // 사건은 아이콘의 `activate`가 낸 메시지로 온다.
    const std::optional<luil::tray_icon> icon { page.make_tray_request() };
    deliver(page, message_of(icon->activate(luil::tray_icon_event { .action = luil::tray_icon_action::select, .keyboard = true })));
    REQUIRE(name_of(page_tree(page), u8"windows-tray-event") == u8"트레이 사건: 고름 · 키보드");
    deliver(page, message_of(icon->activate(luil::tray_icon_event { .action = luil::tray_icon_action::select })));
    REQUIRE(name_of(page_tree(page), u8"windows-tray-event") == u8"트레이 사건: 고름");
    deliver(page, message_of(icon->activate(luil::tray_icon_event { .action = luil::tray_icon_action::double_click })));
    REQUIRE(name_of(page_tree(page), u8"windows-tray-event") == u8"트레이 사건: 두 번 클릭");
}

TEST_CASE("The tray menu hides and shows the main window and ends the app", "[windows-page][tray]")
{
    demo::windows_page page {};
    deliver(page, demo::tray_toggle_intent { true });
    // 아이콘이 서기 전에는 숨기기가 꺼져 있다. 숨기면 되살릴 길이 없다.
    REQUIRE(page.make_tray_request()->menu->items[1].enabled == false);
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::shown) });
    REQUIRE(page.make_tray_request()->menu->items[1].enabled);

    const luil::menu_message_factory select { page.make_tray_request()->menu->select };
    deliver(page, message_of(select(u8"hide")));
    REQUIRE(page.main_hidden());
    REQUIRE(name_of(page_tree(page), u8"windows-tray-event") == u8"트레이 사건: 메뉴: 주 창 숨기기");
    REQUIRE(page.make_tray_request()->menu->items[1].enabled == false);

    deliver(page, message_of(select(u8"show")));
    REQUIRE(page.main_hidden() == false);
    REQUIRE(name_of(page_tree(page), u8"windows-tray-event") == u8"트레이 사건: 메뉴: 창 보이기");
    // 되살린 주 창을 앞으로 세운다. 라이브러리는 사건에서 창을 세우지 않는다.
    const std::optional<luil::focus_request> focus { page.make_focus_request() };
    REQUIRE(focus.has_value());
    REQUIRE(focus->surface.empty());
    REQUIRE(focus->activate);
    REQUIRE(focus->target == luil::ui_element_id {});

    // 끝내기는 창 명령이다. 닫힘 훅을 비켜 간다.
    const luil::input_action exit { select(u8"exit") };
    const auto* const command { std::get_if<luil::ui_command>(&exit) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == luil::ui_command::application_exit);

    // 아이콘이 내려가거나 끄면 숨은 주 창이 되살아난다.
    deliver(page, message_of(select(u8"hide")));
    REQUIRE(page.main_hidden());
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::waiting) });
    REQUIRE(page.main_hidden() == false);
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::shown) });
    deliver(page, message_of(select(u8"hide")));
    deliver(page, demo::tray_toggle_intent { false });
    REQUIRE(page.main_hidden() == false);
}

TEST_CASE("The tray focus request gives way to a later tool window request", "[windows-page][tray]")
{
    demo::windows_page page {};
    deliver(page, demo::tray_toggle_intent { true });
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::shown) });
    deliver(page, message_of(page.make_tray_request()->menu->select(u8"show")));
    const std::uint64_t main_id { page.make_focus_request()->id };
    deliver(page, demo::tool_focus_intent {});
    const std::optional<luil::focus_request> tool { page.make_focus_request() };
    REQUIRE(tool->id > main_id);
    REQUIRE(tool->surface == u8"tool");
}

TEST_CASE("The tray controls are buttons a person can read by name", "[windows-page][tray]")
{
    demo::windows_page page {};
    const auto tree { page_tree(page) };
    const std::pair<std::u8string_view, std::u8string_view> buttons[] {
        { u8"tray-on", u8"트레이 아이콘 켜기" },
        { u8"tray-off", u8"트레이 아이콘 끄기" },
        { u8"tray-picture", u8"트레이 그림 바꾸기" },
    };
    for (const auto& [key, name] : buttons)
    {
        const luil::ui_element* const button { tree.find({ demo::kind_tool_toggle, std::u8string { key } }) };
        REQUIRE(button != nullptr);
        REQUIRE(button->accessibility().name == name);
    }
}

namespace {
    [[nodiscard]] luil::win32::hotkey_report hotkey_report_with(std::u8string id, const luil::win32::hotkey_status status, std::u8string error = {})
    {
        luil::win32::hotkey_report report {};
        report.id = std::move(id);
        report.status = status;
        report.error = std::move(error);
        return report;
    }
}

TEST_CASE("The hotkey section reads its capability, request, report and pressed count as four lines", "[windows-page][hotkey]")
{
    demo::windows_page page {};
    {
        const platform_scope scope { luil::ui_platform {} };
        const auto tree { page_tree(page) };
        REQUIRE(name_of(tree, u8"windows-hotkey-capability") == u8"전역 단축키 능력: 없음");
        REQUIRE(name_of(tree, u8"windows-hotkey-request") == u8"전역 단축키 요청: 끔");
        REQUIRE(name_of(tree, u8"windows-hotkey-report") == u8"전역 단축키 반영: 끔");
        REQUIRE(name_of(tree, u8"windows-hotkey-pressed") == u8"전역 단축키 눌림: 0번");
    }
    {
        luil::ui_platform desktop {};
        desktop.capabilities.global_hotkeys = true;
        const platform_scope scope { desktop };
        REQUIRE(name_of(page_tree(page), u8"windows-hotkey-capability") == u8"전역 단축키 능력: 있음");
    }
    REQUIRE(page.make_hotkeys_request().empty());
}

TEST_CASE("The hotkey buttons reach the request and the conflict test adds a second id with the same chord", "[windows-page][hotkey]")
{
    demo::windows_page page {};
    // 켜기 전에는 충돌 시험을 받지 않는다.
    deliver(page, demo::hotkey_conflict_intent {});
    REQUIRE(page.make_hotkeys_request().empty());

    deliver(page, demo::hotkey_toggle_intent { true });
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-request") == u8"전역 단축키 요청: 켬 · Ctrl+Shift+Alt+F11");
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: 아직 보고 없음");
    std::vector<luil::global_hotkey> hotkeys { page.make_hotkeys_request() };
    REQUIRE(hotkeys.size() == 1);
    REQUIRE(hotkeys[0].id == u8"demo");
    REQUIRE(hotkeys[0].chord == luil::key_chord { luil::key_code::f11, true, true, true });
    REQUIRE(hotkeys[0].pressed != nullptr);

    deliver(page, demo::hotkey_conflict_intent {});
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-request") == u8"전역 단축키 요청: 켬 · Ctrl+Shift+Alt+F11 · 충돌 시험");
    hotkeys = page.make_hotkeys_request();
    REQUIRE(hotkeys.size() == 2);
    REQUIRE(hotkeys[1].id == u8"demo-extra");
    // 앞 항목이 이기려면 같은 조합이어야 한다.
    REQUIRE(hotkeys[1].chord == hotkeys[0].chord);

    // 끄면 목록이 비고 충돌 시험도 꺼진다.
    deliver(page, demo::hotkey_toggle_intent { false });
    REQUIRE(page.make_hotkeys_request().empty());
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-request") == u8"전역 단축키 요청: 끔");
    deliver(page, demo::hotkey_toggle_intent { true });
    REQUIRE(page.make_hotkeys_request().size() == 1);
}

TEST_CASE("Hotkey reports reach the report line and the second id rides beside the first", "[windows-page][hotkey]")
{
    demo::windows_page page {};
    deliver(page, demo::hotkey_toggle_intent { true });
    const std::pair<luil::win32::hotkey_status, std::u8string_view> texts[] {
        { luil::win32::hotkey_status::registered, u8"등록됨" },
        { luil::win32::hotkey_status::conflict, u8"충돌" },
        { luil::win32::hotkey_status::reserved, u8"예약된 조합" },
        { luil::win32::hotkey_status::needs_modifier, u8"수정자 필요" },
        { luil::win32::hotkey_status::invalid_key, u8"쓸 수 없는 키" },
        { luil::win32::hotkey_status::invalid_id, u8"쓸 수 없는 id" },
    };

    for (const auto& [status, text] : texts)
    {
        deliver(page, demo::hotkey_report_intent { hotkey_report_with(u8"demo", status) });
        REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: " + std::u8string { text });
    }
    deliver(page, demo::hotkey_report_intent { hotkey_report_with(u8"demo", luil::win32::hotkey_status::failed, u8"boom") });
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: 실패 (boom)");

    // 같은 id의 새 보고가 앞 것을 덮고, 둘째 id의 보고는 옆에 붙는다.
    deliver(page, demo::hotkey_report_intent { hotkey_report_with(u8"demo", luil::win32::hotkey_status::registered) });
    deliver(page, demo::hotkey_report_intent { hotkey_report_with(u8"demo-extra", luil::win32::hotkey_status::conflict) });
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: 등록됨 · 충돌 (demo-extra)");

    // 끄면 `끔`이고 늦게 온 보고는 버린다. 다시 켜면 보고를 기다린다.
    deliver(page, demo::hotkey_toggle_intent { false });
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: 끔");
    deliver(page, demo::hotkey_report_intent { hotkey_report_with(u8"demo", luil::win32::hotkey_status::registered) });
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: 끔");
    deliver(page, demo::hotkey_toggle_intent { true });
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-report") == u8"전역 단축키 반영: 아직 보고 없음");
}

TEST_CASE("A hotkey press counts and the front toggle brings the hidden main window back", "[windows-page][hotkey]")
{
    demo::windows_page page {};
    deliver(page, demo::hotkey_toggle_intent { true });
    deliver(page, message_of(page.make_hotkeys_request()[0].pressed()));
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-pressed") == u8"전역 단축키 눌림: 1번");
    // 눌러도 창은 건드리지 않는다.
    REQUIRE(page.make_focus_request().has_value() == false);

    // 트레이 메뉴로 숨긴 주 창이다.
    deliver(page, demo::tray_toggle_intent { true });
    deliver(page, demo::tray_report_intent { tray_report_with(luil::win32::tray_status::shown) });
    deliver(page, message_of(page.make_tray_request()->menu->select(u8"hide")));
    REQUIRE(page.main_hidden());
    deliver(page, message_of(page.make_hotkeys_request()[0].pressed()));
    REQUIRE(page.main_hidden());
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-pressed") == u8"전역 단축키 눌림: 2번");

    deliver(page, demo::hotkey_front_intent { true });
    deliver(page, message_of(page.make_hotkeys_request()[0].pressed()));
    REQUIRE(page.main_hidden() == false);
    const std::optional<luil::focus_request> focus { page.make_focus_request() };
    REQUIRE(focus.has_value());
    REQUIRE(focus->surface.empty());
    REQUIRE(focus->activate);
    REQUIRE(name_of(page_tree(page), u8"windows-hotkey-pressed") == u8"전역 단축키 눌림: 3번");
}

TEST_CASE("The hotkey controls are buttons and a checkbox a person can read by name", "[windows-page][hotkey]")
{
    demo::windows_page page {};
    const auto tree { page_tree(page) };
    const std::pair<std::u8string_view, std::u8string_view> buttons[] {
        { u8"hotkey-on", u8"전역 단축키 켜기" },
        { u8"hotkey-off", u8"전역 단축키 끄기" },
        { u8"hotkey-conflict", u8"충돌 시험 켜기" },
    };

    for (const auto& [key, name] : buttons)
    {
        const luil::ui_element* const button { tree.find({ demo::kind_tool_toggle, std::u8string { key } }) };
        REQUIRE(button != nullptr);
        REQUIRE(button->accessibility().name == name);
    }
    const luil::ui_element* const front { tree.find({ luil::ui_element_kind::check, u8"hotkey-front" }) };
    REQUIRE(front != nullptr);
    REQUIRE(front->accessibility().name == u8"눌리면 창 앞으로");
}

#include "demo/windows_page.h"

#include "luil/ui/ui_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>
#include <utility>
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

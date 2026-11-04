#include "host/taskbar_sync.h"

#include "luil/app/app_host.h"
#include "luil/ui/image_element.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {
    using luil::taskbar_call;
    using luil::taskbar_call_kind;
    using luil::taskbar_progress_state;
    using luil::taskbar_request;
    using luil::taskbar_sync;
    using luil::taskbar_sync_state;

    [[nodiscard]] taskbar_request progress_request(const taskbar_progress_state state, const float value = 0.0f)
    {
        taskbar_request request {};
        request.progress.state = state;
        request.progress.value = value;
        return request;
    }

    // 1x1 불투명 이미지다. 만들 때마다 새 몸통이라 `backing()`이 서로 다르다.
    [[nodiscard]] luil::ui_image make_dot()
    {
        constexpr std::array<std::uint8_t, 4> pixel { 255, 255, 255, 255 };
        return luil::make_rgba_image(1, 1, pixel);
    }

    [[nodiscard]] taskbar_sync make_ready_sync()
    {
        taskbar_sync sync {};
        sync.button_created();
        return sync;
    }

    // 호출 목록을 "종류:값" 글로 줄여 순서를 한눈에 견준다.
    [[nodiscard]] std::string describe(const std::vector<taskbar_call>& calls)
    {
        std::string text {};
        for (const taskbar_call& call : calls)
        {
            if (text.empty() == false)
                text += ' ';
            switch (call.kind)
            {
            case taskbar_call_kind::progress_value:
                text += "value:" + std::to_string(call.units);
                break;
            case taskbar_call_kind::progress_state:
                text += "state:" + std::to_string(static_cast<int>(call.state));
                break;
            case taskbar_call_kind::overlay_set:
                text += "overlay_set";
                break;
            case taskbar_call_kind::overlay_description:
                text += "overlay_description";
                break;
            case taskbar_call_kind::overlay_clear:
                text += "overlay_clear";
                break;
            }
        }
        return text;
    }

    constexpr int none { static_cast<int>(taskbar_progress_state::none) };
    constexpr int indeterminate { static_cast<int>(taskbar_progress_state::indeterminate) };
    constexpr int normal { static_cast<int>(taskbar_progress_state::normal) };
    constexpr int error { static_cast<int>(taskbar_progress_state::error) };
    constexpr int paused { static_cast<int>(taskbar_progress_state::paused) };

    [[nodiscard]] std::string state_call(const int state)
    {
        return "state:" + std::to_string(state);
    }
}

TEST_CASE("The default taskbar request touches nothing", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    REQUIRE(sync.update(taskbar_request {}).empty());
    REQUIRE(sync.state() == taskbar_sync_state::off);

    taskbar_sync waiting {};
    REQUIRE(waiting.update(taskbar_request {}).empty());
    REQUIRE(waiting.state() == taskbar_sync_state::off);
}

TEST_CASE("A request waits for the button and goes out in one go once it stands", "[host][taskbar]")
{
    taskbar_sync sync {};
    taskbar_request request { progress_request(taskbar_progress_state::normal, 0.5f) };
    request.overlay.icon = make_dot();
    request.overlay.description = u8"새 소식";

    REQUIRE(sync.update(request).empty());
    REQUIRE(sync.state() == taskbar_sync_state::waiting);
    REQUIRE(sync.ready() == false);

    sync.button_created();
    REQUIRE(sync.ready());
    const std::vector<taskbar_call> calls { sync.update(request) };
    REQUIRE(describe(calls) == "value:500 " + state_call(normal) + " overlay_set");
    REQUIRE(calls.back().description == u8"새 소식");
    REQUIRE(sync.state() == taskbar_sync_state::applied);

    // 같은 요청은 다시 내지 않는다.
    REQUIRE(sync.update(request).empty());
}

TEST_CASE("Progress states go out in order and the value is followed by its state", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };

    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::indeterminate))) == state_call(indeterminate));
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::normal, 0.25f))) == "value:250 " + state_call(normal));
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::error, 0.25f))) == "value:250 " + state_call(error));
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::paused, 0.25f))) == "value:250 " + state_call(paused));
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::none))) == state_call(none));
    REQUIRE(sync.state() == taskbar_sync_state::off);
}

TEST_CASE("Entering a state with a value from one without sends the value first", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::indeterminate))) == state_call(indeterminate));
    // 값을 먼저 보내면 셸이 상태를 바꿔 두므로 상태를 곧바로 뒤따라 다시 보낸다.
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::normal, 0.0f))) == "value:0 " + state_call(normal));
}

TEST_CASE("The same request and changes below one step make no calls", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    REQUIRE(sync.update(progress_request(taskbar_progress_state::normal, 0.5f)).empty() == false);

    REQUIRE(sync.update(progress_request(taskbar_progress_state::normal, 0.5f)).empty());
    REQUIRE(sync.update(progress_request(taskbar_progress_state::normal, 0.5004f)).empty());
    REQUIRE(sync.update(progress_request(taskbar_progress_state::normal, 0.4996f)).empty());
    // 값이 없는 상태에서는 value를 읽지 않는다.
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::indeterminate, 0.9f))) == state_call(indeterminate));
    REQUIRE(sync.update(progress_request(taskbar_progress_state::indeterminate, 0.1f)).empty());
    // 한 단계가 넘는 변화만 값 호출이 된다. 한 진행 동안 많아야 1001번이다.
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::normal, 0.001f))) == "value:1 " + state_call(normal));
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::normal, 0.002f))) == "value:2 " + state_call(normal));
}

TEST_CASE("Progress values are clamped and not-a-number is zero", "[host][taskbar]")
{
    REQUIRE(luil::taskbar_progress_units(0.0f) == 0u);
    REQUIRE(luil::taskbar_progress_units(1.0f) == luil::taskbar_progress_total);
    REQUIRE(luil::taskbar_progress_units(-3.0f) == 0u);
    REQUIRE(luil::taskbar_progress_units(7.5f) == luil::taskbar_progress_total);
    REQUIRE(luil::taskbar_progress_units(std::numeric_limits<float>::quiet_NaN()) == 0u);
    REQUIRE(luil::taskbar_progress_units(std::numeric_limits<float>::infinity()) == luil::taskbar_progress_total);
    REQUIRE(luil::taskbar_progress_units(-std::numeric_limits<float>::infinity()) == 0u);
    REQUIRE(luil::taskbar_progress_units(0.3333f) == 333u);
    REQUIRE(luil::taskbar_progress_units(0.9996f) == luil::taskbar_progress_total);

    taskbar_sync sync { make_ready_sync() };
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::normal, std::numeric_limits<float>::quiet_NaN()))) == "value:0 " + state_call(normal));
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::normal, 2.0f))) == "value:1000 " + state_call(normal));
}

TEST_CASE("A button that stands again gets the whole request from the start", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    taskbar_request request { progress_request(taskbar_progress_state::paused, 0.75f) };
    request.overlay.icon = make_dot();
    REQUIRE(describe(sync.update(request)) == "value:750 " + state_call(paused) + " overlay_set");
    REQUIRE(sync.update(request).empty());

    // 탐색기가 다시 뜨면 단추가 새로 서고 같은 메시지가 다시 온다.
    sync.button_created();
    REQUIRE(describe(sync.update(request)) == "value:750 " + state_call(paused) + " overlay_set");
    REQUIRE(sync.update(request).empty());
}

TEST_CASE("A default request makes no calls when the button stands again", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    REQUIRE(sync.update(taskbar_request {}).empty());
    sync.button_created();
    REQUIRE(sync.update(taskbar_request {}).empty());
    REQUIRE(sync.state() == taskbar_sync_state::off);
}

TEST_CASE("A request is held while the button is gone and sent again when it stands", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    const taskbar_request request { progress_request(taskbar_progress_state::normal, 0.5f) };
    REQUIRE(sync.update(request).empty() == false);

    // 주 창이 숨으면 단추가 없다. 요청은 쥐고 호출하지 않는다.
    sync.button_lost();
    REQUIRE(sync.ready() == false);
    REQUIRE(sync.update(request).empty());
    REQUIRE(sync.state() == taskbar_sync_state::waiting);
    REQUIRE(sync.update(progress_request(taskbar_progress_state::error, 0.9f)).empty());

    sync.button_created();
    REQUIRE(describe(sync.update(progress_request(taskbar_progress_state::error, 0.9f))) == "value:900 " + state_call(error));
}

TEST_CASE("Overlay images are compared by identity", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    const luil::ui_image first { make_dot() };
    taskbar_request request {};
    request.overlay.icon = first;
    request.overlay.description = u8"하나";
    REQUIRE(describe(sync.update(request)) == "overlay_set");

    // 같은 이미지의 복사본은 같은 그림이다.
    request.overlay.icon = luil::ui_image { first };
    REQUIRE(sync.update(request).empty());

    // 새로 지은 이미지는 모양이 같아도 새 그림이다.
    request.overlay.icon = make_dot();
    REQUIRE(describe(sync.update(request)) == "overlay_set");
    REQUIRE(sync.update(request).empty());

    // 설명만 바뀌면 같은 그림에 설명만 다시 건다.
    request.overlay.description = u8"둘";
    const std::vector<taskbar_call> described { sync.update(request) };
    REQUIRE(describe(described) == "overlay_description");
    REQUIRE(described.front().description == u8"둘");
    REQUIRE(sync.update(request).empty());

    // 빈 이미지는 오버레이를 거둔다. 거둔 뒤에는 다시 거두지 않는다.
    request.overlay.icon = {};
    REQUIRE(describe(sync.update(request)) == "overlay_clear");
    REQUIRE(sync.update(request).empty());
    REQUIRE(sync.state() == taskbar_sync_state::off);
}

TEST_CASE("A description without an image is not an overlay", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    taskbar_request request {};
    request.overlay.description = u8"그림이 없다";
    REQUIRE(sync.update(request).empty());
    REQUIRE(sync.state() == taskbar_sync_state::off);
}

TEST_CASE("Forgetting the overlay sends it again at the new size and leaves progress alone", "[host][taskbar]")
{
    taskbar_sync sync { make_ready_sync() };
    taskbar_request request { progress_request(taskbar_progress_state::normal, 0.5f) };
    request.overlay.icon = make_dot();
    REQUIRE(sync.update(request).empty() == false);

    sync.forget_overlay();
    REQUIRE(describe(sync.update(request)) == "overlay_set");
    REQUIRE(sync.update(request).empty());
}

TEST_CASE("A failed call is not retried with the same request", "[host][taskbar]")
{
    // 호출이 나가는 순간 낸 것으로 기록한다. 실패를 알아도 같은 요청으로는 다시 내지 않는다.
    taskbar_sync sync { make_ready_sync() };
    const taskbar_request request { progress_request(taskbar_progress_state::normal, 0.5f) };
    REQUIRE(sync.update(request).empty() == false);
    REQUIRE(sync.update(request).empty());
    REQUIRE(sync.update(request).empty());

    // 다음 변화나 단추 재생성에서 다시 한다.
    REQUIRE(sync.update(progress_request(taskbar_progress_state::normal, 0.6f)).empty() == false);
}

TEST_CASE("A frame and a window carry a default taskbar request", "[host][taskbar]")
{
    const luil::ui_frame frame {};
    REQUIRE(frame.taskbar.progress.state == taskbar_progress_state::none);
    REQUIRE(frame.taskbar.overlay.icon.valid() == false);
    const luil::ui_window window {};
    REQUIRE(window.taskbar.progress.state == taskbar_progress_state::none);
    REQUIRE(window.taskbar.overlay.icon.valid() == false);
    REQUIRE(window.independent == false);
    // 점프 목록은 손대지 않음이 기본이다. 앱이 싣지 않으면 셸의 목록을 부르지 않는다.
    REQUIRE(frame.taskbar.jump_list.has_value() == false);
    REQUIRE(window.taskbar.jump_list.has_value() == false);
}

namespace {
    using luil::jump_list_call_kind;
    using luil::jump_list_sync;
    using luil::taskbar_jump_list;
    using luil::taskbar_jump_task;

    [[nodiscard]] taskbar_jump_list two_tasks()
    {
        taskbar_jump_list list {};
        list.tasks.push_back(taskbar_jump_task { .title = u8"새 메모", .arguments = { u8"--new-note" }, .description = u8"메모를 연다" });
        list.tasks.push_back(taskbar_jump_task { .title = u8"그림", .arguments = { u8"--image", u8"a b.png" }, .icon_path = u8"C:\\icons\\image.ico", .icon_index = 2 });
        return list;
    }
}

TEST_CASE("An empty jump list request never calls the shell", "[host][taskbar][jump-list]")
{
    jump_list_sync sync {};
    REQUIRE(sync.update(std::nullopt).has_value() == false);
    REQUIRE(sync.update(std::nullopt).has_value() == false);

    // 낸 뒤에 비어 있음으로 돌아가도 손대지 않는다. 같은 목록이 다시 와도 이미 낸 것이다.
    REQUIRE(sync.update(two_tasks()).has_value());
    REQUIRE(sync.update(std::nullopt).has_value() == false);
    REQUIRE(sync.update(two_tasks()).has_value() == false);
}

TEST_CASE("The first jump list is committed and the first empty list removes", "[host][taskbar][jump-list]")
{
    {
        jump_list_sync sync {};
        const auto call { sync.update(two_tasks()) };
        REQUIRE(call.has_value());
        REQUIRE(call->kind == jump_list_call_kind::commit);
        REQUIRE(call->list == two_tasks());
        REQUIRE(sync.update(two_tasks()).has_value() == false);
    }
    {
        // 셸에는 읽는 길이 없어 앞 실행이 남긴 목록을 지우려면 첫 빈 목록도 낸다.
        jump_list_sync sync {};
        const auto call { sync.update(taskbar_jump_list {}) };
        REQUIRE(call.has_value());
        REQUIRE(call->kind == jump_list_call_kind::remove);
        REQUIRE(call->list.tasks.empty());
        REQUIRE(sync.update(taskbar_jump_list {}).has_value() == false);
    }
}

TEST_CASE("Any changed jump task field rebuilds the whole list", "[host][taskbar][jump-list]")
{
    jump_list_sync sync {};
    REQUIRE(sync.update(two_tasks()).has_value());

    taskbar_jump_list title { two_tasks() };
    title.tasks[0].title = u8"새 메모 열기";
    REQUIRE(sync.update(title).has_value());

    taskbar_jump_list arguments { title };
    arguments.tasks[1].arguments.push_back(u8"--again");
    REQUIRE(sync.update(arguments).has_value());

    taskbar_jump_list description { arguments };
    description.tasks[0].description.clear();
    REQUIRE(sync.update(description).has_value());

    taskbar_jump_list icon { description };
    icon.tasks[1].icon_index = 3;
    const auto call { sync.update(icon) };
    REQUIRE(call.has_value());
    REQUIRE(call->kind == jump_list_call_kind::commit);
    REQUIRE(call->list == icon);
    REQUIRE(sync.update(icon).has_value() == false);
}

TEST_CASE("Emptying a committed jump list removes it once", "[host][taskbar][jump-list]")
{
    jump_list_sync sync {};
    REQUIRE(sync.update(two_tasks()).has_value());
    const auto removed { sync.update(taskbar_jump_list {}) };
    REQUIRE(removed.has_value());
    REQUIRE(removed->kind == jump_list_call_kind::remove);
    REQUIRE(sync.update(taskbar_jump_list {}).has_value() == false);
    REQUIRE(sync.update(std::nullopt).has_value() == false);
    REQUIRE(sync.update(two_tasks())->kind == jump_list_call_kind::commit);
}

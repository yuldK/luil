#include "luil/ui/toast_element.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    [[nodiscard]] luil::toast_config make_toast(std::u8string id)
    {
        luil::toast_config config {};
        config.id = std::move(id);
        config.text = u8"알림";
        return config;
    }
} // namespace

TEST_CASE("A toast stack anchors toasts to the chosen corner", "[ui][toast]")
{
    const luil::rect_f slot { 0.0f, 0.0f, 1000.0f, 600.0f };
    const float scale { 2.0f };

    // 아래 모서리는 첫 장이 모서리에 가장 가깝고 위로 쌓인다.
    luil::toast_stack_config bottom_right {};
    bottom_right.corner = luil::toast_corner::bottom_right;
    bottom_right.margin = 16.0f;
    bottom_right.spacing = 8.0f;
    bottom_right.width = 320.0f;
    std::vector<luil::toast_config> toasts {};
    toasts.push_back(make_toast(u8"first"));
    toasts.push_back(make_toast(u8"second"));
    luil::toast_stack_element stack { bottom_right, std::move(toasts) };
    stack.arrange({ slot, scale });

    const auto children { stack.children() };
    REQUIRE(children.size() == 2);
    const luil::rect_f first { children[0]->bounds() };
    const luil::rect_f second { children[1]->bounds() };
    // 배율이 곱해진 물리 픽셀이다.
    REQUIRE(first.x == 1000.0f - (16.0f + 320.0f) * scale);
    REQUIRE(first.width == 320.0f * scale);
    REQUIRE(first.height == luil::toast_height * scale);
    REQUIRE(first.y == 600.0f - (16.0f + luil::toast_height) * scale);
    REQUIRE(second.y == first.y - (luil::toast_height + 8.0f) * scale);

    // 위 모서리는 첫 장이 가장 위에 오고 아래로 쌓인다.
    luil::toast_stack_config top_left {};
    top_left.corner = luil::toast_corner::top_left;
    std::vector<luil::toast_config> more {};
    more.push_back(make_toast(u8"first"));
    more.push_back(make_toast(u8"second"));
    luil::toast_stack_element left_stack { top_left, std::move(more) };
    left_stack.arrange({ slot, 1.0f });
    REQUIRE(left_stack.children()[0]->bounds().x == 16.0f);
    REQUIRE(left_stack.children()[0]->bounds().y == 16.0f);
    REQUIRE(left_stack.children()[1]->bounds().y == 16.0f + luil::toast_height + 8.0f);
}

TEST_CASE("A toast body absorbs hits even without an action button", "[ui][toast][hit]")
{
    // 버튼 없는 토스트도 위에 뜨는 표면이다.
    // 몸통 클릭이 아래 element(내용 위 버튼 등)로 새면 유령 클릭이 된다.
    luil::toast_element toast { make_toast(u8"plain") };
    toast.arrange({ { 100.0f, 100.0f, 320.0f, 40.0f }, 1.0f });

    const luil::ui_element* const hit { toast.hit_test(200.0f, 120.0f) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id().kind == luil::ui_element_kind::toast);
    // 밖은 여전히 아무것도 아니다.
    REQUIRE(toast.hit_test(50.0f, 50.0f) == nullptr);
}

TEST_CASE("A toast stack builds only up to its maximum count", "[ui][toast]")
{
    luil::toast_stack_config config {};
    config.maximum_count = 2;
    std::vector<luil::toast_config> toasts {};
    toasts.push_back(make_toast(u8"1"));
    toasts.push_back(make_toast(u8"2"));
    toasts.push_back(make_toast(u8"3"));
    const luil::toast_stack_element stack { config, std::move(toasts) };

    // 넘치는 설정은 자식이 되지 않는다.
    REQUIRE(stack.children().size() == 2);
}

TEST_CASE("A toast makes its action button only when both label and action are given", "[ui][toast]")
{
    luil::toast_config with_action { make_toast(u8"undoable") };
    with_action.action_label = u8"실행 취소";
    with_action.action = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    const luil::toast_element actionable { std::move(with_action) };
    REQUIRE(actionable.children().size() == 1);
    REQUIRE(actionable.children()[0]->id() == luil::ui_element_id { luil::ui_element_kind::toast_action, u8"undoable" });

    // 라벨만 있거나 액션만 있으면 "누를 수 없는 버튼"이 되므로 만들지 않는다.
    luil::toast_config label_only { make_toast(u8"label") };
    label_only.action_label = u8"실행 취소";
    const luil::toast_element labeled { std::move(label_only) };
    REQUIRE(labeled.children().empty());

    luil::toast_config action_only { make_toast(u8"action") };
    action_only.action = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    const luil::toast_element silent { std::move(action_only) };
    REQUIRE(silent.children().empty());
}

TEST_CASE("A toast fades only near the end of its duration", "[ui][toast]")
{
    const std::chrono::steady_clock::time_point shown {};
    luil::toast_config config { make_toast(u8"fading") };
    config.shown_at = shown;
    config.duration = 3000ms;
    const luil::toast_element toast { std::move(config) };

    REQUIRE(toast.opacity_at(shown) == 1.0f);
    REQUIRE(toast.opacity_at(shown + 3000ms - luil::toast_fade) == 1.0f);
    // 흐려지는 구간의 한가운데는 절반이다.
    REQUIRE(toast.opacity_at(shown + 3000ms - luil::toast_fade / 2) == 0.5f);
    // 끝난 뒤에는 앱이 지울 때까지 보이지 않는다.
    REQUIRE(toast.opacity_at(shown + 3001ms) == 0.0f);

    // duration이 없는 토스트는 흐려지지 않는다.
    const luil::toast_element sticky { make_toast(u8"sticky") };
    REQUIRE(sticky.opacity_at(shown + 999h) == 1.0f);
}

TEST_CASE("A timed toast announces its fade as the next update", "[ui][toast][update]")
{
    const std::chrono::steady_clock::time_point shown { std::chrono::seconds { 100 } };
    const luil::interaction_snapshot idle {};
    luil::toast_stack_config config {};
    std::vector<luil::toast_config> toasts {};
    luil::toast_config timed { make_toast(u8"timed") };
    timed.shown_at = shown;
    timed.duration = 1000ms;
    toasts.push_back(std::move(timed));
    auto stack { std::make_unique<luil::toast_stack_element>(config, std::move(toasts)) };
    stack->arrange({ { 0.0f, 0.0f, 800.0f, 600.0f }, 1.0f });
    const luil::ui_tree tree { std::move(stack) };

    // 흐려지기 전에는 그 시작 시각까지 그대로다.
    REQUIRE(tree.next_update({ shown }, idle) == shown + 1000ms - luil::toast_fade);
    // 흐려지는 동안은 계속 움직인다 ("지금"을 답한다).
    const luil::update_context fading { shown + 1000ms - luil::toast_fade / 2 };
    REQUIRE(tree.next_update(fading, idle) == fading.now);
    // 다 사라진 뒤에는 앱이 지울 때까지 다시 그릴 것이 없다.
    REQUIRE(tree.next_update({ shown + 2000ms }, idle).has_value() == false);

    // duration이 없으면 다시 그릴 이유도 없다.
    std::vector<luil::toast_config> sticky {};
    sticky.push_back(make_toast(u8"sticky"));
    auto sticky_stack { std::make_unique<luil::toast_stack_element>(luil::toast_stack_config {}, std::move(sticky)) };
    sticky_stack->arrange({ { 0.0f, 0.0f, 800.0f, 600.0f }, 1.0f });
    const luil::ui_tree sticky_tree { std::move(sticky_stack) };
    REQUIRE(sticky_tree.next_update({ shown }, idle).has_value() == false);
}

#include "luil/ui/undo_commands.h"
#include "luil/ui/undo_stack.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
    // 걸음이 싣는 메시지다. 값이 곧 "이 값으로 정함"이다.
    struct set_value
    {
        int value { 0 };
    };

    [[nodiscard]] luil::app_message value_message(const int value)
    {
        return luil::app_message { set_value { value } };
    }

    [[nodiscard]] luil::undo_step make_step(std::u8string label, const int undo, const int redo, std::u8string key = {}, const std::size_t weight = 0)
    {
        return luil::undo_step { .label = std::move(label), .undo = value_message(undo), .redo = value_message(redo), .merge_key = std::move(key), .weight = weight };
    }

    // `apply`로 받은 값을 받은 차례로 모은다.
    struct recorder
    {
        std::vector<int> values {};

        [[nodiscard]] luil::undo_apply apply()
        {
            return [this](const luil::app_message& message) {
                const auto* const value { message.get<set_value>() };
                REQUIRE(value != nullptr);
                values.push_back(value->value);
            };
        }

        [[nodiscard]] std::vector<int> take()
        {
            return std::exchange(values, {});
        }
    };

    [[nodiscard]] std::vector<int> undone(luil::undo_stack& stack)
    {
        recorder log {};
        REQUIRE(stack.undo(log.apply()));
        return log.take();
    }

    [[nodiscard]] std::vector<int> redone(luil::undo_stack& stack)
    {
        recorder log {};
        REQUIRE(stack.redo(log.apply()));
        return log.take();
    }

    using values = std::vector<int>;
}

TEST_CASE("A step goes back and forth through undo and redo in order", "[ui][undo]")
{
    luil::undo_stack stack {};
    recorder log {};
    REQUIRE(stack.can_undo() == false);
    REQUIRE(stack.can_redo() == false);
    REQUIRE(stack.undo_label().empty());
    REQUIRE(stack.redo_label().empty());
    REQUIRE(stack.clean());
    // 걸음이 없으면 거짓이고 apply를 부르지 않는다.
    REQUIRE(stack.undo(log.apply()) == false);
    REQUIRE(stack.redo(log.apply()) == false);
    REQUIRE(log.values.empty());

    REQUIRE(stack.push(make_step(u8"항목 삭제", 10, 11)));
    REQUIRE(stack.push(make_step(u8"체크", 20, 21)));
    REQUIRE(stack.undo_count() == 2u);
    REQUIRE(stack.undo_label() == u8"체크");
    REQUIRE(stack.can_redo() == false);

    REQUIRE(undone(stack) == values { 20 });
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.redo_count() == 1u);
    REQUIRE(stack.undo_label() == u8"항목 삭제");
    REQUIRE(stack.redo_label() == u8"체크");

    REQUIRE(undone(stack) == values { 10 });
    REQUIRE(stack.can_undo() == false);
    REQUIRE(stack.undo_label().empty());
    REQUIRE(stack.undo(log.apply()) == false);

    REQUIRE(redone(stack) == values { 11 });
    REQUIRE(redone(stack) == values { 21 });
    REQUIRE(stack.can_redo() == false);
    REQUIRE(stack.redo_label().empty());
    REQUIRE(stack.redo(log.apply()) == false);
    REQUIRE(stack.undo_count() == 2u);
    REQUIRE(stack.redo_count() == 0u);
}

TEST_CASE("A step needs both messages and a stack needs an apply function", "[ui][undo]")
{
    luil::undo_stack stack {};
    luil::undo_step no_undo { .label = u8"a", .redo = value_message(1) };
    luil::undo_step no_redo { .label = u8"b", .undo = value_message(1) };
    REQUIRE(stack.push(std::move(no_undo)) == false);
    REQUIRE(stack.push(std::move(no_redo)) == false);
    REQUIRE(stack.push({}) == false);
    REQUIRE(stack.undo_count() == 0u);
    REQUIRE(stack.bytes() == 0u);

    REQUIRE(stack.push(make_step(u8"c", 1, 2, {}, 5)));
    // 빈 apply는 거짓이고 걸음은 제자리에 있다.
    REQUIRE(stack.undo(luil::undo_apply {}) == false);
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.redo_count() == 0u);
    REQUIRE(undone(stack) == values { 1 });
    REQUIRE(stack.redo(luil::undo_apply {}) == false);
    REQUIRE(stack.redo_count() == 1u);
}

TEST_CASE("A new step discards the redo side", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    REQUIRE(stack.push(make_step(u8"b", 3, 4)));
    static_cast<void>(undone(stack));
    REQUIRE(stack.can_redo());
    REQUIRE(stack.push(make_step(u8"c", 5, 6)));
    REQUIRE(stack.can_redo() == false);
    REQUIRE(stack.redo_count() == 0u);
    REQUIRE(stack.undo_label() == u8"c");
    REQUIRE(undone(stack) == values { 5 });
    REQUIRE(undone(stack) == values { 1 });
}

TEST_CASE("Calls made while replaying leave the stack alone", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    REQUIRE(stack.push(make_step(u8"b", 3, 4)));
    REQUIRE(stack.push(make_step(u8"c", 5, 6)));
    static_cast<void>(undone(stack));
    REQUIRE(stack.redo_count() == 1u);

    int calls { 0 };
    const luil::undo_apply reentrant = [&](const luil::app_message&) {
        ++calls;
        // 위치는 apply를 부르기 전에 옮겨 둔다. 질의는 된다.
        REQUIRE(stack.undo_count() == 1u);
        REQUIRE(stack.redo_count() == 2u);
        REQUIRE(stack.undo_label() == u8"a");
        REQUIRE(stack.redo_label() == u8"b");
        // 쌓기는 거짓이고 redo를 지우지 않는다.
        REQUIRE(stack.push(make_step(u8"x", 7, 8)) == false);
        REQUIRE(stack.end() == false);
        REQUIRE(stack.undo(luil::undo_apply { [](const luil::app_message&) {} }) == false);
        REQUIRE(stack.redo(luil::undo_apply { [](const luil::app_message&) {} }) == false);
        // 나머지는 아무 일도 하지 않는다.
        stack.begin(u8"묶음");
        stack.set_clean();
        stack.break_merge();
        stack.set_limits({ 1, 1 });
        stack.clear();
        REQUIRE(stack.undo_count() == 1u);
        REQUIRE(stack.redo_count() == 2u);
        REQUIRE(stack.clean() == false);
    };
    REQUIRE(stack.undo(reentrant));
    REQUIRE(calls == 1);
    // 열린 묶음도 없고 상한도 그대로다. 재생이 끝나면 다시 쌓인다.
    REQUIRE(stack.end() == false);
    REQUIRE(stack.can_undo());
    REQUIRE(stack.push(make_step(u8"y", 9, 10)));
    REQUIRE(stack.undo_count() == 2u);
    REQUIRE(stack.redo_count() == 0u);
}

TEST_CASE("A throwing apply does not leave the stack replaying", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    const luil::undo_apply throwing { [](const luil::app_message&) { throw std::runtime_error { "apply" }; } };
    REQUIRE_THROWS_AS(stack.undo(throwing), std::runtime_error);
    // 위치는 이미 옮겼고 stack은 잠기지 않았다.
    REQUIRE(stack.redo_count() == 1u);
    REQUIRE(stack.push(make_step(u8"b", 3, 4)));
}

TEST_CASE("Consecutive steps with the same key merge into one step", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.push(make_step(u8"폭 조절", 1, 2, u8"resize:path", 10)));
    REQUIRE(stack.push(make_step(u8"폭 바꿈", 2, 3, u8"resize:path", 30)));
    REQUIRE(stack.push(make_step(u8"폭 끝", 3, 4, u8"resize:path", 20)));
    REQUIRE(stack.undo_count() == 1u);
    // 이름은 뒤 걸음의 것이고 무게는 큰 쪽이다.
    REQUIRE(stack.undo_label() == u8"폭 끝");
    REQUIRE(stack.bytes() == 30u);
    // 앞 걸음의 되돌림과 뒤 걸음의 다시 실행이 남는다.
    REQUIRE(undone(stack) == values { 1 });
    REQUIRE(redone(stack) == values { 4 });
}

TEST_CASE("Steps with another key, no key or a break in between do not merge", "[ui][undo]")
{
    SECTION("another key")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"y")));
        REQUIRE(stack.undo_count() == 2u);
    }
    SECTION("an empty key never merges even with an empty key before")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2)));
        REQUIRE(stack.push(make_step(u8"b", 2, 3)));
        REQUIRE(stack.undo_count() == 2u);
    }
    SECTION("a step without a key between two with the same key")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        REQUIRE(stack.push(make_step(u8"b", 2, 3)));
        REQUIRE(stack.push(make_step(u8"c", 3, 4, u8"x")));
        REQUIRE(stack.undo_count() == 3u);
    }
    SECTION("break_merge")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        stack.break_merge();
        REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"x")));
        REQUIRE(stack.undo_count() == 2u);
        // 새 걸음에서 다시 합쳐진다.
        REQUIRE(stack.push(make_step(u8"c", 3, 4, u8"x")));
        REQUIRE(stack.undo_count() == 2u);
    }
    SECTION("undo then redo")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"y")));
        static_cast<void>(undone(stack));
        static_cast<void>(redone(stack));
        REQUIRE(stack.push(make_step(u8"c", 3, 4, u8"y")));
        REQUIRE(stack.undo_count() == 3u);
    }
    SECTION("set_clean")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        stack.set_clean();
        REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"x")));
        REQUIRE(stack.undo_count() == 2u);
        // 저장한 상태로 돌아갈 수 있다.
        static_cast<void>(undone(stack));
        REQUIRE(stack.clean());
    }
    SECTION("clear")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        stack.clear();
        REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"x")));
        REQUIRE(stack.undo_count() == 1u);
        REQUIRE(stack.undo_label() == u8"b");
    }
    SECTION("a macro")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
        stack.begin(u8"묶음");
        stack.end();
        REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"x")));
        REQUIRE(stack.undo_count() == 2u);
    }
}

TEST_CASE("A macro is one step that undoes backwards and redoes forwards", "[ui][undo]")
{
    luil::undo_stack stack {};
    stack.begin(u8"변경 버리기");
    REQUIRE(stack.push(make_step(u8"하나", 1, 2, {}, 4)));
    REQUIRE(stack.push(make_step(u8"둘", 3, 4, {}, 6)));
    REQUIRE(stack.push(make_step(u8"셋", 5, 6)));
    // 확정하기 전에는 걸음이 없다.
    REQUIRE(stack.undo_count() == 0u);
    REQUIRE(stack.end());
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.undo_label() == u8"변경 버리기");
    REQUIRE(stack.bytes() == 10u);
    REQUIRE(undone(stack) == values { 5, 3, 1 });
    REQUIRE(redone(stack) == values { 2, 4, 6 });
}

TEST_CASE("Macros nest and the outermost end commits under the outermost name", "[ui][undo]")
{
    luil::undo_stack stack {};
    stack.begin(u8"바깥");
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    stack.begin(u8"안쪽");
    REQUIRE(stack.push(make_step(u8"b", 3, 4)));
    REQUIRE(stack.end());
    REQUIRE(stack.undo_count() == 0u);
    REQUIRE(stack.push(make_step(u8"c", 5, 6)));
    REQUIRE(stack.end());
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.undo_label() == u8"바깥");
    REQUIRE(undone(stack) == values { 5, 3, 1 });
    REQUIRE(stack.end() == false);
}

TEST_CASE("An empty macro makes no step and keeps the redo side", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    static_cast<void>(undone(stack));
    stack.begin(u8"빈 묶음");
    REQUIRE(stack.end());
    REQUIRE(stack.undo_count() == 0u);
    REQUIRE(stack.redo_count() == 1u);
    {
        const luil::undo_macro macro { stack, u8"범위 빈 묶음" };
    }
    REQUIRE(stack.undo_count() == 0u);
    REQUIRE(stack.can_redo());
}

TEST_CASE("While a macro is open the stack neither undoes nor marks a save point", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    REQUIRE(stack.push(make_step(u8"b", 3, 4)));
    static_cast<void>(undone(stack));
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.redo_count() == 1u);
    REQUIRE(stack.clean() == false);

    stack.begin(u8"묶음");
    REQUIRE(stack.can_undo() == false);
    REQUIRE(stack.can_redo() == false);
    REQUIRE(stack.undo_label().empty());
    REQUIRE(stack.redo_label().empty());
    recorder log {};
    REQUIRE(stack.undo(log.apply()) == false);
    REQUIRE(stack.redo(log.apply()) == false);
    REQUIRE(log.values.empty());
    // 저장 지점을 찍지 않는다. 찍었다면 빈 묶음 위에서 clean이 참이 된다.
    stack.set_clean();
    REQUIRE(stack.clean() == false);
    REQUIRE(stack.push(make_step(u8"c", 5, 6)));
    // redo는 확정할 때 비운다.
    REQUIRE(stack.redo_count() == 1u);
    REQUIRE(stack.end());
    REQUIRE(stack.redo_count() == 0u);
    REQUIRE(stack.undo_count() == 2u);
    REQUIRE(stack.clean() == false);
    static_cast<void>(undone(stack));
    REQUIRE(stack.clean() == false);
}

TEST_CASE("A macro with messages already applied is not the save point", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.clean());
    stack.begin(u8"묶음");
    // 열린 묶음에 담긴 메시지는 앱이 이미 적용했다.
    REQUIRE(stack.clean());
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    REQUIRE(stack.clean() == false);
    REQUIRE(stack.end());
    REQUIRE(stack.clean() == false);
    static_cast<void>(undone(stack));
    REQUIRE(stack.clean());
}

TEST_CASE("The macro guard closes on destruction and clear drops an open macro", "[ui][undo]")
{
    luil::undo_stack stack {};
    {
        const luil::undo_macro macro { stack, u8"범위" };
        REQUIRE(stack.push(make_step(u8"a", 1, 2)));
        REQUIRE(stack.push(make_step(u8"b", 3, 4)));
        REQUIRE(stack.undo_count() == 0u);
    }
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.undo_label() == u8"범위");
    REQUIRE(stack.end() == false);

    stack.begin(u8"버려질 묶음");
    REQUIRE(stack.push(make_step(u8"c", 5, 6)));
    stack.clear();
    REQUIRE(stack.undo_count() == 0u);
    REQUIRE(stack.can_undo() == false);
    // 묶음이 사라졌으니 짝 없는 end다.
    REQUIRE(stack.end() == false);
    REQUIRE(stack.push(make_step(u8"d", 7, 8)));
    REQUIRE(stack.undo_count() == 1u);
}

TEST_CASE("Keyed steps inside a macro do not merge with each other or with the next step", "[ui][undo]")
{
    luil::undo_stack stack {};
    stack.begin(u8"묶음");
    REQUIRE(stack.push(make_step(u8"a", 1, 2, u8"x")));
    REQUIRE(stack.push(make_step(u8"b", 2, 3, u8"x")));
    REQUIRE(stack.end());
    REQUIRE(undone(stack) == values { 2, 1 });
    static_cast<void>(redone(stack));
    REQUIRE(stack.push(make_step(u8"c", 3, 4, u8"x")));
    REQUIRE(stack.undo_count() == 2u);
}

TEST_CASE("The step limit drops the oldest steps and keeps the newest", "[ui][undo]")
{
    luil::undo_stack stack { { .steps = 3, .bytes = 0 } };
    for (int index { 1 }; index <= 5; ++index)
        REQUIRE(stack.push(make_step(u8"s", index, index + 100)));
    REQUIRE(stack.undo_count() == 3u);
    REQUIRE(undone(stack) == values { 5 });
    REQUIRE(undone(stack) == values { 4 });
    REQUIRE(undone(stack) == values { 3 });
    REQUIRE(stack.can_undo() == false);
}

TEST_CASE("The weight limit drops the oldest steps and the newest step stays even when it is larger", "[ui][undo]")
{
    luil::undo_stack stack { { .steps = 0, .bytes = 100 } };
    REQUIRE(stack.push(make_step(u8"a", 1, 2, {}, 40)));
    REQUIRE(stack.push(make_step(u8"b", 3, 4, {}, 40)));
    REQUIRE(stack.bytes() == 80u);
    REQUIRE(stack.push(make_step(u8"c", 5, 6, {}, 40)));
    REQUIRE(stack.undo_count() == 2u);
    REQUIRE(stack.bytes() == 80u);
    REQUIRE(stack.undo_label() == u8"c");

    // 상한보다 큰 걸음은 홀로 남아 한 번은 되돌린다.
    REQUIRE(stack.push(make_step(u8"큰 걸음", 7, 8, {}, 500)));
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.bytes() == 500u);
    REQUIRE(undone(stack) == values { 7 });
    REQUIRE(stack.bytes() == 0u);
    REQUIRE(redone(stack) == values { 8 });
    REQUIRE(stack.bytes() == 500u);
}

TEST_CASE("A zero limit means no limit and a step without weight only counts as a step", "[ui][undo]")
{
    luil::undo_stack stack { { .steps = 0, .bytes = 0 } };
    for (int index { 0 }; index < 1500; ++index)
        REQUIRE(stack.push(make_step(u8"s", index, index + 1, {}, 1000000)));
    REQUIRE(stack.undo_count() == 1500u);

    const luil::undo_stack_limits defaults {};
    REQUIRE(defaults.steps == luil::undo_stack_default_steps);
    REQUIRE(defaults.bytes == luil::undo_stack_default_bytes);
    REQUIRE(luil::undo_stack_default_steps == 1000u);
    REQUIRE(luil::undo_stack_default_bytes == std::size_t { 32 } * 1024 * 1024);

    // 무게를 알리지 않으면 바이트 상한이 걸리지 않는다.
    luil::undo_stack unweighted { { .steps = 0, .bytes = 10 } };
    for (int index { 0 }; index < 50; ++index)
        REQUIRE(unweighted.push(make_step(u8"s", index, index + 1)));
    REQUIRE(unweighted.undo_count() == 50u);
    REQUIRE(unweighted.bytes() == 0u);
}

TEST_CASE("Shrinking the limits trims the undo side only", "[ui][undo]")
{
    luil::undo_stack stack {};
    for (int index { 1 }; index <= 4; ++index)
        REQUIRE(stack.push(make_step(u8"s", index, index + 100)));
    static_cast<void>(undone(stack));
    static_cast<void>(undone(stack));
    REQUIRE(stack.undo_count() == 2u);
    REQUIRE(stack.redo_count() == 2u);

    stack.set_limits({ .steps = 1, .bytes = 0 });
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.redo_count() == 2u);
    // 돌아온 걸음도 되돌릴 쪽의 상한을 지킨다.
    REQUIRE(redone(stack) == values { 103 });
    REQUIRE(stack.undo_count() == 1u);
    REQUIRE(stack.redo_count() == 1u);
}

TEST_CASE("The save point follows undo and redo", "[ui][undo]")
{
    luil::undo_stack stack {};
    REQUIRE(stack.clean());
    REQUIRE(stack.push(make_step(u8"a", 1, 2)));
    REQUIRE(stack.clean() == false);
    static_cast<void>(undone(stack));
    REQUIRE(stack.clean());
    static_cast<void>(redone(stack));
    REQUIRE(stack.clean() == false);

    stack.set_clean();
    REQUIRE(stack.clean());
    REQUIRE(stack.push(make_step(u8"b", 3, 4)));
    REQUIRE(stack.clean() == false);
    static_cast<void>(undone(stack));
    REQUIRE(stack.clean());
    static_cast<void>(undone(stack));
    REQUIRE(stack.clean() == false);
    static_cast<void>(redone(stack));
    REQUIRE(stack.clean());
}

TEST_CASE("The save point is lost when its redo side is discarded or its base step is dropped", "[ui][undo]")
{
    SECTION("a push discards a save point on the redo side")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2)));
        REQUIRE(stack.push(make_step(u8"b", 3, 4)));
        stack.set_clean();
        static_cast<void>(undone(stack));
        REQUIRE(stack.clean() == false);
        REQUIRE(stack.push(make_step(u8"c", 5, 6)));
        // 같은 걸음 수에 다른 상태라 저장 지점이 아니다.
        REQUIRE(stack.undo_count() == 2u);
        REQUIRE(stack.clean() == false);
        static_cast<void>(undone(stack));
        static_cast<void>(undone(stack));
        REQUIRE(stack.clean() == false);
        stack.set_clean();
        REQUIRE(stack.clean());
    }
    SECTION("the step limit drops the bottom step")
    {
        luil::undo_stack stack { { .steps = 2, .bytes = 0 } };
        // 처음 자리가 저장 지점인 채로 맨 아래 걸음이 버려진다.
        REQUIRE(stack.push(make_step(u8"a", 1, 2)));
        REQUIRE(stack.push(make_step(u8"b", 3, 4)));
        REQUIRE(stack.clean() == false);
        REQUIRE(stack.push(make_step(u8"c", 5, 6)));
        static_cast<void>(undone(stack));
        static_cast<void>(undone(stack));
        REQUIRE(stack.can_undo() == false);
        REQUIRE(stack.clean() == false);
        stack.set_clean();
        REQUIRE(stack.clean());
    }
    SECTION("a save point above the bottom step shifts with the dropped step")
    {
        luil::undo_stack stack { { .steps = 2, .bytes = 0 } };
        REQUIRE(stack.push(make_step(u8"a", 1, 2)));
        REQUIRE(stack.push(make_step(u8"b", 3, 4)));
        stack.set_clean();
        REQUIRE(stack.push(make_step(u8"c", 5, 6)));
        REQUIRE(stack.undo_count() == 2u);
        REQUIRE(stack.clean() == false);
        static_cast<void>(undone(stack));
        REQUIRE(stack.clean());
    }
    SECTION("clear makes the empty place the save point")
    {
        luil::undo_stack stack {};
        REQUIRE(stack.push(make_step(u8"a", 1, 2)));
        REQUIRE(stack.clean() == false);
        stack.clear();
        REQUIRE(stack.clean());
        REQUIRE(stack.bytes() == 0u);
        REQUIRE(stack.redo_count() == 0u);
    }
}

TEST_CASE("The undo and redo commands carry the ids, the shortcuts and the window scope", "[ui][undo][commands]")
{
    const std::array<luil::app_command, 2> commands { luil::make_undo_commands({}) };
    REQUIRE(luil::undo_command_id == u8"edit.undo");
    REQUIRE(luil::redo_command_id == u8"edit.redo");
    REQUIRE(commands[0].id == luil::undo_command_id);
    REQUIRE(commands[1].id == luil::redo_command_id);
    REQUIRE(commands[0].scope == luil::command_scope::window);
    REQUIRE(commands[1].scope == luil::command_scope::window);
    // 판정용 표는 정적이라 둘 다 켠다. 앱이 글을 주지 않으면 기본 문구다 (docs/default-text-design.md).
    REQUIRE(commands[0].enabled);
    REQUIRE(commands[1].enabled);
    REQUIRE(commands[0].label == u8"실행 취소");
    REQUIRE(commands[1].label == u8"다시 실행");

    REQUIRE(luil::effective_shortcuts(commands[0]) == std::vector<luil::key_chord> { { .key = luil::key_code::key_z, .control = true } });
    REQUIRE(luil::effective_shortcuts(commands[1])
        == std::vector<luil::key_chord> { { .key = luil::key_code::key_y, .control = true }, { .key = luil::key_code::key_z, .control = true, .shift = true } });
    REQUIRE(luil::find_conflicts(commands).empty());
}

TEST_CASE("The three undo shortcuts match their commands and Ctrl+Z never matches redo", "[ui][undo][commands]")
{
    const std::array<luil::app_command, 2> commands { luil::make_undo_commands({}) };
    const auto match = [&commands](const luil::key_code key, const bool shift, const luil::command_scope active = luil::command_scope::window) {
        return luil::match_command(commands, luil::key_pressed_event { .key = key, .control = true, .shift = shift }, active);
    };
    REQUIRE(match(luil::key_code::key_z, false) == u8"edit.undo");
    REQUIRE(match(luil::key_code::key_y, false) == u8"edit.redo");
    REQUIRE(match(luil::key_code::key_z, true) == u8"edit.redo");
    // Ctrl+Y에 Shift가 붙으면 맞지 않는다. 수정자를 정확히 견준다.
    REQUIRE(match(luil::key_code::key_y, true).has_value() == false);
    // 활성 범위가 modal이면 window 명령으로 물러서지 않는다. 열린 대화 상자 뒤의 문서가 되돌아가지 않는다.
    REQUIRE(match(luil::key_code::key_z, false, luil::command_scope::modal).has_value() == false);
    // 초점 범위는 window로 물러선다.
    REQUIRE(match(luil::key_code::key_z, false, luil::command_scope::focused) == u8"edit.undo");
}

TEST_CASE("A disabled undo command does not match while the redo command still does", "[ui][undo][commands]")
{
    const std::array<luil::app_command, 2> commands { luil::make_undo_commands({ .undo_label = u8"실행 취소", .redo_label = u8"다시 실행: 체크", .can_undo = false, .can_redo = true }) };
    REQUIRE(commands[0].label == u8"실행 취소");
    REQUIRE(commands[1].label == u8"다시 실행: 체크");
    REQUIRE(commands[0].enabled == false);
    REQUIRE(commands[1].enabled);
    REQUIRE(luil::match_command(commands, luil::key_pressed_event { .key = luil::key_code::key_z, .control = true }, luil::command_scope::window).has_value() == false);
    REQUIRE(luil::match_command(commands, luil::key_pressed_event { .key = luil::key_code::key_y, .control = true }, luil::command_scope::window) == u8"edit.redo");

    // 꺼진 항목도 단축키 열에 실제 단축키를 보인다.
    const luil::menu_item_config undo_item { luil::menu_item_from(commands[0]) };
    REQUIRE(undo_item.enabled == false);
    REQUIRE(undo_item.shortcut_text == u8"Ctrl+Z");
    REQUIRE(undo_item.key == u8"edit.undo");
    REQUIRE(luil::menu_item_from(commands[1]).shortcut_text == u8"Ctrl+Y");
}

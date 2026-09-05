#include "win32/webview_message_gate.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using luil::win32::webview_message_gate;
    using luil::win32::webview_message_verdict;
} // namespace

TEST_CASE("Web messages under both limits are accepted", "[win32][webview]")
{
    webview_message_gate gate {};
    for (std::uint64_t index = 0; index < 5; ++index)
    {
        const auto decision { gate.admit(1000 + index, 100, 1024, 10) };
        REQUIRE(decision.verdict == webview_message_verdict::accept);
        REQUIRE(decision.notify == false);
    }
}

TEST_CASE("An oversized web message is dropped and the first drop in a window is announced", "[win32][webview]")
{
    webview_message_gate gate {};
    const auto first { gate.admit(1000, 2048, 1024, 10) };
    REQUIRE(first.verdict == webview_message_verdict::drop_size);
    REQUIRE(first.notify);

    // 같은 창 안의 두 번째 버림은 조용하다 — 알림도 상한 안에 있어야 한다.
    const auto second { gate.admit(1500, 4096, 1024, 10) };
    REQUIRE(second.verdict == webview_message_verdict::drop_size);
    REQUIRE(second.notify == false);

    // 버림은 받아들인 수를 늘리지 않는다 — 큰 메시지가 작은 메시지의 자리를 먹지 않는다.
    const auto small { gate.admit(1600, 10, 1024, 10) };
    REQUIRE(small.verdict == webview_message_verdict::accept);
}

TEST_CASE("Web messages beyond the per-second limit are dropped until the window ends", "[win32][webview]")
{
    webview_message_gate gate {};
    for (std::uint64_t index = 0; index < 3; ++index)
        REQUIRE(gate.admit(1000 + index, 1, 1024, 3).verdict == webview_message_verdict::accept);

    const auto fourth { gate.admit(1003, 1, 1024, 3) };
    REQUIRE(fourth.verdict == webview_message_verdict::drop_rate);
    REQUIRE(fourth.notify);
    const auto fifth { gate.admit(1004, 1, 1024, 3) };
    REQUIRE(fifth.verdict == webview_message_verdict::drop_rate);
    REQUIRE(fifth.notify == false);

    // 창은 첫 메시지부터 1초다. 1초 뒤의 메시지가 새 창을 연다.
    const auto next_window { gate.admit(2000, 1, 1024, 3) };
    REQUIRE(next_window.verdict == webview_message_verdict::accept);
    // 새 창의 첫 버림은 다시 알린다.
    REQUIRE(gate.admit(2001, 1, 1024, 3).verdict == webview_message_verdict::accept);
    REQUIRE(gate.admit(2002, 1, 1024, 3).verdict == webview_message_verdict::accept);
    const auto again { gate.admit(2003, 1, 1024, 3) };
    REQUIRE(again.verdict == webview_message_verdict::drop_rate);
    REQUIRE(again.notify);
}

TEST_CASE("The web message window is opened by the first message, not by a fixed grid", "[win32][webview]")
{
    webview_message_gate gate {};
    // 창이 1700에 열렸으면 2699까지가 그 창이다.
    REQUIRE(gate.admit(1700, 1, 1024, 1).verdict == webview_message_verdict::accept);
    REQUIRE(gate.admit(2699, 1, 1024, 1).verdict == webview_message_verdict::drop_rate);
    REQUIRE(gate.admit(2700, 1, 1024, 1).verdict == webview_message_verdict::accept);
}

TEST_CASE("A per-second limit of zero drops every web message", "[win32][webview]")
{
    webview_message_gate gate {};
    const auto decision { gate.admit(1000, 1, 1024, 0) };
    REQUIRE(decision.verdict == webview_message_verdict::drop_rate);
    REQUIRE(decision.notify);
}

TEST_CASE("A clock that runs backwards opens a new web message window", "[win32][webview]")
{
    // 시각이 되돌아가면 창을 다시 연다 — 영영 닫히지 않는 창보다 낫다.
    webview_message_gate gate {};
    REQUIRE(gate.admit(5000, 1, 1024, 1).verdict == webview_message_verdict::accept);
    REQUIRE(gate.admit(5001, 1, 1024, 1).verdict == webview_message_verdict::drop_rate);
    REQUIRE(gate.admit(4000, 1, 1024, 1).verdict == webview_message_verdict::accept);
}

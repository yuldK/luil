#include "win32/webview_message_gate.h"

namespace luil::win32 {
    namespace {
        constexpr std::uint64_t window_length_ms { 1000 };
    } // namespace

    webview_message_decision webview_message_gate::admit(
        const std::uint64_t now_ms, const std::size_t bytes, const std::size_t maximum_bytes, const std::uint32_t maximum_per_second) noexcept
    {
        // 창이 끝났으면 새 창을 연다. 첫 메시지가 창을 연다 (헤더의 이유).
        if (has_window_ == false || now_ms < window_start_ms_ || now_ms - window_start_ms_ >= window_length_ms)
        {
            has_window_ = true;
            window_start_ms_ = now_ms;
            accepted_ = 0;
            dropped_ = 0;
        }

        webview_message_decision decision {};
        if (bytes > maximum_bytes)
            decision.verdict = webview_message_verdict::drop_size;
        else if (accepted_ >= maximum_per_second)
            decision.verdict = webview_message_verdict::drop_rate;

        if (decision.verdict == webview_message_verdict::accept)
        {
            ++accepted_;
            return decision;
        }
        // 창 안의 첫 버림만 알린다 (헤더의 이유).
        decision.notify = dropped_ == 0;
        ++dropped_;
        return decision;
    }
} // namespace luil::win32

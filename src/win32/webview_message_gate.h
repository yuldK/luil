#pragma once

#include <cstddef>
#include <cstdint>

namespace luil::win32 {
    // 페이지가 보낸 web message 하나에 내린 판정이다.
    enum class webview_message_verdict
    {
        // 앱으로 넘긴다.
        accept,
        // 크기 상한을 넘겨 버린다.
        drop_size,
        // 초당 건수 상한을 넘겨 버린다.
        drop_rate,
    };

    struct webview_message_decision
    {
        webview_message_verdict verdict { webview_message_verdict::accept };
        // 이 버림을 앱에 알릴 것인가.
        //
        // **버림 알림도 상한 안에 있어야 한다.** 버릴 때마다 알리면 적대적 페이지가
        // 쏟는 만큼 알림이 inbox로 들어가, 상한이 막으려던 바로 그 일이 알림으로
        // 일어난다. 그래서 한 창(1초) 안의 첫 버림만 알린다 — 앱이 아는 것은
        // "이 1초에 무언가 버려졌다"이고, 그것으로 자기 화면이 왜 낡았는지는 안다.
        bool notify { false };
    };

    // 웹뷰 하나의 web message 상한을 집행한다 (webview.h의 `webview_policy`).
    //
    // WebView2는 크기 상한도 초당 건수 상한도 주지 않는다 (문서 조사 결과이고 폭주
    // 실험은 하지 않았다 — webview-composition-design.md). 우리가 걸지 않으면
    // 아무도 걸지 않고, 적대적 페이지가 쏟으면 app inbox가 포화해 **앱의 정상
    // 메시지가 조용히 사라진다** (`app_host::post_app_message`는 reject_newest다).
    //
    // 창은 **첫 메시지부터 1초**다. 고정 눈금이 아니라 첫 메시지가 창을 연다 —
    // 눈금 경계에 몰아 보내 상한의 두 배를 넘기는 길을 막는 것은 아니지만
    // (그러려면 토큰 버킷이다), 이 상한의 목적은 정밀한 유량 제어가 아니라 inbox를
    // 살리는 것이라 1초에 상한의 두 배까지가 최악이면 충분하다.
    //
    // 창을 모르는 순수 상태 기계라 test로 잠긴다. 시각은 부르는 쪽이 준다 (ms).
    class webview_message_gate
    {
    public:
        // 이 메시지를 넘길지 정한다.
        //  - `bytes`는 메시지의 UTF-8 크기다.
        //  - `maximum_per_second`가 0이면 전부 버린다 (초당 0건).
        [[nodiscard]] webview_message_decision admit(std::uint64_t now_ms, std::size_t bytes, std::size_t maximum_bytes, std::uint32_t maximum_per_second) noexcept;

    private:
        bool has_window_ { false };
        std::uint64_t window_start_ms_ { 0 };
        std::uint32_t accepted_ { 0 };
        std::uint32_t dropped_ { 0 };
    };
} // namespace luil::win32

#pragma once

#include <windows.h>

#include <memory>

namespace luil::win32 {
    // 사용자가 설정 > 접근성 > 텍스트 크기에서 고른 배율이다 (`UISettings.TextScaleFactor`, 1~2.25).
    // 읽지 못하면(WinRT를 열 수 없는 환경) 1이다.
    //  - 창 thread(COM STA)에서 부른다.
    [[nodiscard]] float read_text_scale_factor() noexcept;

    // 텍스트 크기가 바뀌면 그 창에 메시지 하나를 게시한다 (`UISettings.TextScaleFactorChanged`).
    // 알림은 OS의 배경 thread에서 오므로 창 메시지로 창 thread에 넘긴다 — 무엇을 할지는 창이 정한다.
    //  - 구독하지 못하면 아무 일도 없다. 그때는 창이 `WM_SETTINGCHANGE`에서 다시 읽는 것이 남은 길이다.
    //  - 파괴하면 구독을 푼다. 창보다 먼저 파괴한다.
    class text_scale_watcher
    {
    public:
        text_scale_watcher(HWND window, UINT message) noexcept;
        text_scale_watcher(const text_scale_watcher&) = delete;
        text_scale_watcher(text_scale_watcher&&) = delete;
        text_scale_watcher& operator=(const text_scale_watcher&) = delete;
        text_scale_watcher& operator=(text_scale_watcher&&) = delete;
        ~text_scale_watcher();

    private:
        struct subscription;
        std::unique_ptr<subscription> subscription_ {};
    };
}

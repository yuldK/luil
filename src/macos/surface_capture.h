#pragma once

#include "host/frame_state.h"

#include "include/core/SkBitmap.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

// 화면 남기기·녹화다 (`capture_request`·`record_request`, Win32 `window_surface`와 같은 길).
//  - 화면을 긁지 않고 그리기와 같은 `frame_state`를 raster로 한 번 더 태운다. 가려지거나 최소화된 창도 같은 그림이다.
//  - 웹뷰는 그 view가 그리는 층이라 담기지 않는다. 자리는 비우지 않는다 (구멍을 내면 검게 남는다).
//  - 파일은 무손실 WebP다 (녹화는 애니메이션 WebP).
namespace luil::macos {
    // 그 상태를 N32 premul 비트맵에 그린다.
    [[nodiscard]] bool draw_to_bitmap(const frame_state& state, SkBitmap& image, std::u8string& error);

    // 그 상태를 그려 파일로 남긴다. 이미 있으면 덮어쓴다.
    [[nodiscard]] bool write_capture(const frame_state& state, const std::u8string& path, std::u8string& error);

    // 한 표면의 녹화다. 그릴 때마다 `collect`로 한 장씩 모으고 `stop`에서 한 번에 인코드한다.
    //  - 상한이 없다. 언제 멈출지는 부르는 쪽이 안다.
    //  - 첫 장의 크기가 캔버스다. 크기가 다른 장은 담지 못하고 세었다가 멈출 때 알린다.
    class frame_recorder
    {
    public:
        [[nodiscard]] bool start(const std::u8string& path, std::u8string& error);
        void collect(const frame_state& state);
        [[nodiscard]] bool stop(std::u8string& error);

        [[nodiscard]] bool recording() const noexcept
        {
            return recording_;
        }

    private:
        struct recorded_frame
        {
            SkBitmap image {};
            std::chrono::steady_clock::time_point time {};
        };

        bool recording_ { false };
        std::u8string path_ {};
        std::vector<recorded_frame> frames_ {};
        int width_ { 0 };
        int height_ { 0 };
        std::size_t dropped_ { 0 };
    };
}

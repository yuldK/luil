#pragma once

#include "luil/win32/media_player.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

// macOS 재생기(AVFoundation)의 AppKit을 모르는 몫이다 (video-playback-design.md의 "macOS").
// AVPlayer 쪽(media_player_macos.mm·video_frame_player_macos.mm)이 값을 옮겨 오고 여기서 정한다. core test에서 돈다.
namespace luil::macos {
    // AVPlayer가 알린 것을 MFPlay 사건의 자리로 옮긴 값이다.
    enum class media_signal
    {
        item_ready,      // 항목의 `status`가 readyToPlay다 (MEDIAITEM_SET)
        item_failed,     // 항목의 `status`가 failed다
        playing,         // `timeControlStatus`가 playing이다 (PLAY)
        paused,          // `pause`를 불렀다 (PAUSE)
        stopped,         // `rewind`가 멈췄다 (STOP)
        ended,           // 끝까지 재생했다 (PLAYBACK_ENDED)
        playback_failed, // 재생 중 끝까지 가지 못했다
    };

    // 지금 상태에서 신호가 낼 다음 상태다. 배달할 것이 없으면 빈 값이다.
    //  - 열기 전·여는 중의 재생·정지 신호는 버린다. `item_ready`는 여는 중에만 받는다 (같은 항목의 두 번째 알림을 버린다).
    //  - 실패는 어디서든 한 번이다. 이미 실패했으면 다시 알리지 않는다.
    [[nodiscard]] std::optional<win32::media_state> next_media_state(win32::media_state current, media_signal signal) noexcept;

    // `CMTime`의 값과 눈금을 100 ns로 옮긴다. 눈금이 0 이하이거나 넘치면 빈 값이다.
    [[nodiscard]] std::optional<std::int64_t> media_time_100ns(std::int64_t value, std::int32_t timescale) noexcept;
    // 같은 값을 ms로 옮긴다. 옮길 수 없거나 음수면 0이다 (MFPlay의 `GetPosition`이 그렇게 답한다).
    [[nodiscard]] std::chrono::milliseconds media_time_ms(std::int64_t value, std::int32_t timescale) noexcept;

    struct media_frame_size
    {
        int width { 0 };
        int height { 0 };
        bool operator==(const media_frame_size&) const = default;
    };

    // 복사할 장의 크기다. 원본 종횡비를 지키며 긴 변을 `limit`으로 줄인다 (Windows와 같은 규칙이다).
    // 크기가 유한한 양수가 아니면 0×0이다.
    [[nodiscard]] media_frame_size capped_frame_size(double width, double height, int limit = 1920) noexcept;

    // `open`의 원본이 URL인가다. 두 글자 이상의 scheme 뒤에 `:`가 오면 URL이고, 나머지는 파일 경로다.
    //  - MFPlay의 `CreateMediaItemFromURL`이 경로와 URL을 함께 받는 것과 같다. `C:`처럼 한 글자 scheme은 경로로 본다.
    [[nodiscard]] bool media_source_is_url(std::u8string_view source) noexcept;
}

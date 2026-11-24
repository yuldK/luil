#include "macos/media_rules.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

// macOS 재생기의 상태 짝과 시각 변환이다 (src/macos/media_rules.h). AVFoundation을 모르는 순수 층이라 core test에서 돈다.
namespace {
    using luil::macos::media_signal;
    using luil::macos::next_media_state;
    using luil::win32::media_state;
}

TEST_CASE("media state: opening becomes ready once and failures report once", "[macos][media]")
{
    CHECK(next_media_state(media_state::opening, media_signal::item_ready) == media_state::ready);
    // 같은 항목의 두 번째 readyToPlay는 버린다.
    CHECK(next_media_state(media_state::ready, media_signal::item_ready) == std::nullopt);
    CHECK(next_media_state(media_state::playing, media_signal::item_ready) == std::nullopt);
    CHECK(next_media_state(media_state::opening, media_signal::item_failed) == media_state::failed);
    CHECK(next_media_state(media_state::playing, media_signal::playback_failed) == media_state::failed);
    CHECK(next_media_state(media_state::failed, media_signal::item_failed) == std::nullopt);
    CHECK(next_media_state(media_state::empty, media_signal::item_failed) == std::nullopt);
}

TEST_CASE("media state: transport signals follow MFPlay events", "[macos][media]")
{
    for (const media_state from : { media_state::ready, media_state::paused, media_state::stopped, media_state::ended })
        CHECK(next_media_state(from, media_signal::playing) == media_state::playing);
    CHECK(next_media_state(media_state::playing, media_signal::playing) == std::nullopt);
    CHECK(next_media_state(media_state::playing, media_signal::paused) == media_state::paused);
    CHECK(next_media_state(media_state::ready, media_signal::paused) == media_state::paused);
    CHECK(next_media_state(media_state::ended, media_signal::paused) == std::nullopt);
    CHECK(next_media_state(media_state::playing, media_signal::stopped) == media_state::stopped);
    CHECK(next_media_state(media_state::ended, media_signal::stopped) == media_state::stopped);
    CHECK(next_media_state(media_state::playing, media_signal::ended) == media_state::ended);
    CHECK(next_media_state(media_state::stopped, media_signal::ended) == std::nullopt);
    // 열기 전·여는 중·실패 뒤의 재생 신호는 버린다.
    for (const media_state from : { media_state::empty, media_state::opening, media_state::failed })
    {
        CHECK(next_media_state(from, media_signal::playing) == std::nullopt);
        CHECK(next_media_state(from, media_signal::paused) == std::nullopt);
        CHECK(next_media_state(from, media_signal::stopped) == std::nullopt);
        CHECK(next_media_state(from, media_signal::ended) == std::nullopt);
    }
}

TEST_CASE("media time: CMTime values convert to 100 ns and milliseconds", "[macos][media]")
{
    CHECK(luil::macos::media_time_100ns(0, 600) == 0);
    CHECK(luil::macos::media_time_100ns(1, 24) == 416'666);
    CHECK(luil::macos::media_time_100ns(25, 24) == 10'416'666);
    CHECK(luil::macos::media_time_100ns(1'500, 1'000) == 15'000'000);
    CHECK(luil::macos::media_time_100ns(-3, 2) == -15'000'000);
    CHECK(luil::macos::media_time_100ns(1, 0) == std::nullopt);
    CHECK(luil::macos::media_time_100ns(1, -1) == std::nullopt);
    // 큰 눈금은 몫과 나머지로 나눠 곱해도 넘치지 않는다.
    CHECK(luil::macos::media_time_100ns(std::numeric_limits<std::int64_t>::max(), 1'000'000'000) == std::numeric_limits<std::int64_t>::max() / 1'000'000'000 * 10'000'000 + 8'547'758);
    CHECK(luil::macos::media_time_100ns(std::numeric_limits<std::int64_t>::max(), 1) == std::nullopt);
    CHECK(luil::macos::media_time_ms(3, 2) == std::chrono::milliseconds { 1500 });
    CHECK(luil::macos::media_time_ms(-1, 2) == std::chrono::milliseconds { 0 });
    CHECK(luil::macos::media_time_ms(1, 0) == std::chrono::milliseconds { 0 });
}

TEST_CASE("media frame size: the long side is capped and the aspect ratio kept", "[macos][media]")
{
    using luil::macos::media_frame_size;
    CHECK(luil::macos::capped_frame_size(960, 540) == media_frame_size { 960, 540 });
    CHECK(luil::macos::capped_frame_size(3840, 2160) == media_frame_size { 1920, 1080 });
    CHECK(luil::macos::capped_frame_size(1080, 3840) == media_frame_size { 540, 1920 });
    CHECK(luil::macos::capped_frame_size(7680, 1) == media_frame_size { 1920, 1 });
    CHECK(luil::macos::capped_frame_size(0, 540) == media_frame_size {});
    CHECK(luil::macos::capped_frame_size(std::nan(""), 540) == media_frame_size {});
}

TEST_CASE("media source: URLs need a scheme of two or more letters", "[macos][media]")
{
    CHECK(luil::macos::media_source_is_url(u8"https://example.com/a.mp4"));
    CHECK(luil::macos::media_source_is_url(u8"file:///tmp/a.mp4"));
    CHECK(luil::macos::media_source_is_url(u8"rtsp+tcp://host/a"));
    CHECK_FALSE(luil::macos::media_source_is_url(u8"/Users/me/a.mp4"));
    CHECK_FALSE(luil::macos::media_source_is_url(u8"movies/a:b.mp4"));
    CHECK_FALSE(luil::macos::media_source_is_url(u8"C:\\video.mp4"));
    CHECK_FALSE(luil::macos::media_source_is_url(u8"1http://a"));
    CHECK_FALSE(luil::macos::media_source_is_url(u8""));
}

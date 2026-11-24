#include "luil/win32/video_frame_player.h"

#include "macos/media_rules.h"
#include "text/library_text.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

// `video_frame_player`의 macOS 몸이다 (video-playback-design.md의 "macOS"). AVPlayer와 AVPlayerItemVideoOutput이다.
//  - 재생·소리·시계는 AVPlayer가 한다 (Windows의 Media Engine 자리). 앱은 `update`에서 지금 표시할 장을 BGRA로 받아
//    불변 `ui_image`로 옮긴다. 장의 시각은 AVPlayer 항목 시계의 표시 시각이다.
//  - 만든 thread(앱의 논리 thread)에서 조작과 소멸까지 한다. 알림(KVO)은 쓰지 않고 `update`가 상태를 읽는다.
//    끝 알림 하나만 아무 thread에서 와서 원자 값을 세운다.
//  - 긴 변을 1,920픽셀로 줄이는 규칙은 Windows와 같다. 열린 뒤 크기를 알면 그 크기로 장을 받는 출력으로 바꾼다.
namespace luil::win32 {
    namespace {
        using library_text::key;

        // 열린 뒤 처음 장을 기다리는 한도다.
        constexpr std::chrono::milliseconds first_frame_wait { 500 };

        [[nodiscard]] NSDictionary* output_attributes(const macos::media_frame_size size)
        {
            NSMutableDictionary* const attributes { [NSMutableDictionary dictionary] };
            attributes[(__bridge NSString*)kCVPixelBufferPixelFormatTypeKey] = @(kCVPixelFormatType_32BGRA);
            if (size.width > 0 && size.height > 0)
            {
                attributes[(__bridge NSString*)kCVPixelBufferWidthKey] = @(size.width);
                attributes[(__bridge NSString*)kCVPixelBufferHeightKey] = @(size.height);
            }
            return attributes;
        }

        [[nodiscard]] std::u8string error_code(NSError* const error)
        {
            if (error == nil)
                return u8"unknown";
            NSString* const text { [NSString stringWithFormat:@"%@ %ld", error.domain, static_cast<long>(error.code)] };
            const char* const bytes { text.UTF8String };
            return bytes != nullptr ? std::u8string { reinterpret_cast<const char8_t*>(bytes) } : std::u8string { u8"unknown" };
        }
    }

    struct video_frame_player::engine
    {
        AVPlayer* player { nil };
        AVPlayerItem* item { nil };
        AVPlayerItemVideoOutput* output { nil };
        id end_observer { nil };
        // 끝 알림이 세운다. 아무 thread에서 온다.
        std::shared_ptr<std::atomic<bool>> ended { std::make_shared<std::atomic<bool>>(false) };
        std::int64_t timestamp_100ns {};
        std::int64_t first_timestamp_100ns {};
        luil::ui_image frame {};
        luil::ui_image first_frame {};
        std::u8string error {};
        media_state state { media_state::empty };
        // 열린 뒤 처음 장을 청한 때다.
        std::optional<std::chrono::steady_clock::time_point> prerolled {};

        ~engine()
        {
            if (end_observer != nil)
                [NSNotificationCenter.defaultCenter removeObserver:end_observer];
            [player pause];
            if (item != nil && output != nil)
                [item removeOutput:output];
            [player replaceCurrentItemWithPlayerItem:nil];
        }

        bool check(const bool succeeded, const key message, NSError* const failure = nil)
        {
            if (succeeded)
                return true;
            const std::u8string code { error_code(failure) };
            error = library_text::format(library_strings(), message, std::array { string_argument { u8"code", code } });
            state = media_state::failed;
            return false;
        }

        // 원본 크기를 알면 상한에 맞춘 크기로 받는 출력으로 바꾼다. 상한 안이면 처음 출력을 그대로 쓴다.
        void fit_output()
        {
            const CGSize presentation { item.presentationSize };
            const macos::media_frame_size capped { macos::capped_frame_size(presentation.width, presentation.height) };
            if (capped.width == 0 || (capped.width == static_cast<int>(presentation.width) && capped.height == static_cast<int>(presentation.height)))
                return;
            AVPlayerItemVideoOutput* const sized { [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:output_attributes(capped)] };
            [item removeOutput:output];
            [item addOutput:sized];
            output = sized;
        }

        void transfer()
        {
            const CMTime now { [output itemTimeForHostTime:CACurrentMediaTime()] };
            if (CMTIME_IS_VALID(now) == false || [output hasNewPixelBufferForItemTime:now] == NO)
                return;
            CMTime shown { kCMTimeInvalid };
            const CVPixelBufferRef buffer { [output copyPixelBufferForItemTime:now itemTimeForDisplay:&shown] };
            if (buffer == nullptr)
                return;
            const CVReturn locked { CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) };
            if (locked == kCVReturnSuccess)
            {
                const auto width { static_cast<int>(CVPixelBufferGetWidth(buffer)) };
                const auto height { static_cast<int>(CVPixelBufferGetHeight(buffer)) };
                const std::size_t row { CVPixelBufferGetBytesPerRow(buffer) };
                const auto* const base { static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer)) };
                if (base != nullptr && width > 0 && height > 0 && CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_32BGRA)
                {
                    // 행 간격을 존중해 불변 이미지로 옮긴다. 마지막 행은 그 폭만 읽는다.
                    const std::size_t bytes { static_cast<std::size_t>(height - 1) * row + static_cast<std::size_t>(width) * 4 };
                    frame = luil::make_bgrx_image(width, height, row, std::span { base, bytes });
                }
                CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            }
            CVPixelBufferRelease(buffer);
            if (check(locked == kCVReturnSuccess, key::video_error_read_pixels) == false || check(frame.valid(), key::video_error_copy) == false)
                return;
            const CMTime stamp { CMTIME_IS_NUMERIC(shown) ? shown : now };
            timestamp_100ns = macos::media_time_100ns(stamp.value, stamp.timescale).value_or(0);
            if (first_frame.valid() == false)
            {
                first_frame = frame;
                first_timestamp_100ns = timestamp_100ns;
            }
        }

        void rewind()
        {
            [player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
            ended->store(false);
        }
    };

    video_frame_player::video_frame_player() : engine_ { std::make_unique<engine>() }
    {
    }

    video_frame_player::~video_frame_player() = default;

    void video_frame_player::open(const std::filesystem::path& path)
    {
        engine_.reset();
        engine_ = std::make_unique<engine>();
        auto& e { *engine_ };
        e.state = media_state::opening;
        std::error_code path_error {};
        const auto absolute { std::filesystem::absolute(path, path_error) };
        const bool resolved { path.empty() == false && static_cast<bool>(path_error) == false };
        if (e.check(resolved, key::video_error_path) == false)
            return;
        const std::string text { absolute.string() };
        NSString* const file { [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding] };
        if (e.check(file != nil, key::video_error_path) == false)
            return;
        e.item = [AVPlayerItem playerItemWithURL:[NSURL fileURLWithPath:file]];
        e.output = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:output_attributes({})];
        if (e.check(e.item != nil && e.output != nil, key::video_error_open) == false)
            return;
        [e.item addOutput:e.output];
        e.player = [AVPlayer playerWithPlayerItem:e.item];
        if (e.check(e.player != nil, key::video_error_frame_server) == false)
            return;
        e.player.actionAtItemEnd = AVPlayerActionAtItemEndPause;
        std::shared_ptr<std::atomic<bool>> ended { e.ended };
        e.end_observer = [NSNotificationCenter.defaultCenter addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                                                                         object:e.item
                                                                          queue:nil
                                                                     usingBlock:^(NSNotification*) {
                                                                         ended->store(true);
                                                                     }];
    }

    void video_frame_player::play()
    {
        auto& e { *engine_ };
        if (e.state != media_state::ready && e.state != media_state::stopped && e.state != media_state::ended)
            return;
        if (e.ended->load())
            e.rewind();
        [e.player play];
        e.state = media_state::playing;
    }

    void video_frame_player::stop()
    {
        auto& e { *engine_ };
        if (e.state != media_state::playing && e.state != media_state::ended && e.state != media_state::ready)
            return;
        [e.player pause];
        e.rewind();
        e.state = media_state::stopped;
        e.frame = e.first_frame;
        e.timestamp_100ns = e.first_timestamp_100ns;
    }

    void video_frame_player::update()
    {
        auto& e { *engine_ };
        if (needs_tick() == false)
            return;
        if (e.item.status == AVPlayerItemStatusFailed)
        {
            (void)e.check(false, key::video_error_read_media, e.item.error);
            return;
        }
        if (e.state == media_state::opening)
        {
            if (e.item.status != AVPlayerItemStatusReadyToPlay)
                return;
            // 출력은 멈춘 재생기에 장을 주지 않는다. 처음 장으로 한 번 탐색해 받는다 (Media Engine의 HAVE_CURRENT_DATA 자리다).
            //  - 영상 track이 없거나 받기가 늦으면 장 없이 ready다.
            const auto now { std::chrono::steady_clock::now() };
            if (e.prerolled.has_value() == false)
            {
                e.fit_output();
                [e.player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
                e.prerolled = now;
            }
            e.transfer();
            const CGSize presentation { e.item.presentationSize };
            if (e.state == media_state::opening && (e.frame.valid() || presentation.width <= 0 || presentation.height <= 0 || now - *e.prerolled >= first_frame_wait))
                e.state = media_state::ready;
            return;
        }
        e.transfer();
        if (e.state == media_state::playing && e.ended->load())
            e.state = media_state::ended;
    }

    bool video_frame_player::needs_tick() const
    {
        return engine_->state == media_state::opening || engine_->state == media_state::playing;
    }

    std::chrono::milliseconds video_frame_player::position() const
    {
        if (engine_->state != media_state::playing && engine_->state != media_state::ended)
            return {};
        const CMTime time { engine_->player.currentTime };
        if (CMTIME_IS_NUMERIC(time) == false)
            return {};
        return macos::media_time_ms(time.value, time.timescale);
    }

    const luil::ui_image& video_frame_player::image() const
    {
        return engine_->frame;
    }

    media_state video_frame_player::state() const
    {
        return engine_->state;
    }

    std::int64_t video_frame_player::frame_timestamp_100ns() const
    {
        return engine_->timestamp_100ns;
    }

    video_frame_statistics video_frame_player::statistics() const
    {
        // AVFoundation은 장 수를 접근 기록(access log)으로만 준다. 로컬 파일에는 기록이 없을 때가 많고, 그리기 수는 주지 않는다.
        AVPlayerItemAccessLog* const log { engine_->item.accessLog };
        if (log == nil)
            return {};
        std::optional<std::uint32_t> dropped {};
        for (AVPlayerItemAccessLogEvent* const event in log.events)
        {
            if (event.numberOfDroppedVideoFrames < 0)
                continue;
            dropped = dropped.value_or(0) + static_cast<std::uint32_t>(event.numberOfDroppedVideoFrames);
        }
        return { .dropped = dropped, .rendered = std::nullopt };
    }

    const std::u8string& video_frame_player::error() const
    {
        return engine_->error;
    }
}

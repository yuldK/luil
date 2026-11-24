#include "luil/win32/media_player.h"
#include "luil/win32/video_frame_player.h"

#include <catch2/catch_test_macros.hpp>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

// 동영상 재생기의 macOS 쪽을 실제 AVFoundation으로 본다 (video-playback-design.md의 "macOS").
// Windows의 selected_nodes_tests.cpp가 OS 견본 파일로 보는 경우를, 여기서 AVAssetWriter로 그 자리에서 만든 짧은 영상으로 본다.
//  - 영상은 단색 장 몇 개(H.264, 24 fps)라 1초 안에 끝난다. 소리는 없다.
//  - 메인 run loop를 돌려 AVFoundation의 알림을 받는다. `deliver`는 재생기의 직렬 queue에서 온다.
namespace {
    using namespace std::chrono_literals;

    constexpr std::int32_t frames_per_second { 24 };

    // 메인 run loop를 돌린다. 조건이 서면 참이다.
    bool pump_until(const std::function<bool()>& done, const std::chrono::milliseconds timeout = 3s)
    {
        const auto deadline { std::chrono::steady_clock::now() + timeout };
        while (done() == false)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.004, true));
        }
        return true;
    }

    // 단색 BGRA 장을 `frames`개 적은 H.264 영상이다. 장마다 색이 다르다.
    [[nodiscard]] bool write_video(const std::filesystem::path& path, const int frames, const int width = 64, const int height = 48)
    {
        std::error_code ignored {};
        std::filesystem::remove(path, ignored);
        NSURL* const url { [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]] };
        NSError* error { nil };
        AVAssetWriter* const writer { [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&error] };
        if (writer == nil)
            return false;
        NSDictionary* const settings { @{ AVVideoCodecKey : AVVideoCodecTypeH264, AVVideoWidthKey : @(width), AVVideoHeightKey : @(height) } };
        AVAssetWriterInput* const input { [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings] };
        input.expectsMediaDataInRealTime = NO;
        NSDictionary* const pixels { @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
            (__bridge NSString*)kCVPixelBufferWidthKey : @(width),
            (__bridge NSString*)kCVPixelBufferHeightKey : @(height),
        } };
        AVAssetWriterInputPixelBufferAdaptor* const adaptor { [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input sourcePixelBufferAttributes:pixels] };
        if ([writer canAddInput:input] == NO)
            return false;
        [writer addInput:input];
        if ([writer startWriting] == NO)
            return false;
        [writer startSessionAtSourceTime:kCMTimeZero];
        for (int index { 0 }; index < frames; ++index)
        {
            const auto deadline { std::chrono::steady_clock::now() + 2s };
            while (input.readyForMoreMediaData == NO)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(1ms);
            }
            CVPixelBufferRef buffer { nullptr };
            if (CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor.pixelBufferPool, &buffer) != kCVReturnSuccess || buffer == nullptr)
                return false;
            CVPixelBufferLockBaseAddress(buffer, 0);
            auto* const base { static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer)) };
            const std::size_t row { CVPixelBufferGetBytesPerRow(buffer) };
            for (int y { 0 }; y < height; ++y)
                for (int x { 0 }; x < width; ++x)
                {
                    std::uint8_t* const pixel { base + static_cast<std::size_t>(y) * row + static_cast<std::size_t>(x) * 4 };
                    pixel[0] = static_cast<std::uint8_t>(index * 40);
                    pixel[1] = static_cast<std::uint8_t>(255 - index * 20);
                    pixel[2] = static_cast<std::uint8_t>(index * 10);
                    pixel[3] = 255;
                }
            CVPixelBufferUnlockBaseAddress(buffer, 0);
            const BOOL appended { [adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(index, frames_per_second)] };
            CVPixelBufferRelease(buffer);
            if (appended == NO)
                return false;
        }
        [input markAsFinished];
        [writer endSessionAtSourceTime:CMTimeMake(frames, frames_per_second)];
        const dispatch_semaphore_t finished { dispatch_semaphore_create(0) };
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(finished);
        }];
        if (dispatch_semaphore_wait(finished, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) != 0)
            return false;
        return writer.status == AVAssetWriterStatusCompleted;
    }

    // 이 test 실행만의 임시 폴더다. 끝나면 지운다.
    class scratch_folder
    {
    public:
        scratch_folder() : path_ { std::filesystem::temp_directory_path() / ("luil-media-tests-" + std::to_string(::getpid())) }
        {
            std::filesystem::create_directories(path_);
        }
        scratch_folder(const scratch_folder&) = delete;
        scratch_folder& operator=(const scratch_folder&) = delete;
        ~scratch_folder()
        {
            std::error_code ignored {};
            std::filesystem::remove_all(path_, ignored);
        }
        [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    private:
        std::filesystem::path path_ {};
    };

    class media_log
    {
    public:
        void push(luil::win32::media_event event)
        {
            const std::scoped_lock lock { mutex_ };
            if (std::this_thread::get_id() == main_)
                on_main_ = true;
            events_.push_back(std::move(event));
        }
        [[nodiscard]] std::vector<luil::win32::media_event> snapshot()
        {
            const std::scoped_lock lock { mutex_ };
            return events_;
        }
        [[nodiscard]] bool saw(const luil::win32::media_state state)
        {
            const std::scoped_lock lock { mutex_ };
            for (const auto& event : events_)
                if (event.state == state)
                    return true;
            return false;
        }
        [[nodiscard]] bool on_main()
        {
            const std::scoped_lock lock { mutex_ };
            return on_main_;
        }
        [[nodiscard]] luil::win32::media_player_config config()
        {
            return { .parent_window = nullptr, .deliver = [this](luil::win32::media_event event) { push(std::move(event)); } };
        }

    private:
        std::mutex mutex_ {};
        std::vector<luil::win32::media_event> events_ {};
        std::thread::id main_ { std::this_thread::get_id() };
        bool on_main_ { false };
    };

    [[nodiscard]] std::u8string text(const std::filesystem::path& path)
    {
        return path.u8string();
    }

    // 장 재생기를 한 바퀴 돌린 기록이다.
    struct frame_run
    {
        luil::win32::media_state opened { luil::win32::media_state::empty };
        bool ready_image { false };
        std::u8string error {};
        std::vector<std::int64_t> timestamps {};
        luil::win32::media_state finished { luil::win32::media_state::empty };
        std::chrono::milliseconds end_position {};
        int width { 0 };
        int height { 0 };
        luil::win32::media_state stopped { luil::win32::media_state::empty };
        std::int64_t stopped_timestamp { -1 };
        bool stopped_ticks { true };
        luil::win32::media_state replayed { luil::win32::media_state::empty };
    };

    // 앱의 논리 thread처럼 `needs_tick` 동안 `update`를 부른다. `idle`은 한 번 쉴 때마다 부른다.
    [[nodiscard]] frame_run run_frames(const std::filesystem::path& path, const std::function<void()>& idle)
    {
        using luil::win32::media_state;
        frame_run run {};
        luil::win32::video_frame_player player {};
        player.open(path);
        const auto wait = [&](const std::function<bool()>& done, const std::chrono::milliseconds timeout) {
            const auto deadline { std::chrono::steady_clock::now() + timeout };
            while (done() == false && std::chrono::steady_clock::now() < deadline)
            {
                const auto previous { player.image().backing() };
                player.update();
                if (player.image().valid() && player.image().backing() != previous)
                    run.timestamps.push_back(player.frame_timestamp_100ns());
                idle();
            }
        };
        wait([&] { return player.needs_tick() == false; }, 3s);
        run.opened = player.state();
        run.ready_image = player.image().valid();
        run.error = player.error();
        if (run.opened != media_state::ready)
            return run;
        player.play();
        wait([&] { return player.state() != media_state::playing; }, 3s);
        run.finished = player.state();
        run.end_position = player.position();
        run.width = player.image().width();
        run.height = player.image().height();
        player.stop();
        run.stopped = player.state();
        run.stopped_timestamp = player.frame_timestamp_100ns();
        run.stopped_ticks = player.needs_tick();
        player.play();
        run.replayed = player.state();
        static_cast<void>(player.statistics());
        return run;
    }

    void check_frames(const frame_run& run, const int frames)
    {
        using luil::win32::media_state;
        INFO("error " << reinterpret_cast<const char*>(run.error.c_str()));
        REQUIRE(run.opened == media_state::ready);
        CHECK(run.ready_image);
        CHECK(run.finished == media_state::ended);
        // 실시간 재생이라 몇 장은 건너뛸 수 있다. 시각은 늘 앞으로 가고 원본 장의 자리(1/24초)에 선다.
        REQUIRE(run.timestamps.size() >= 3);
        for (std::size_t index { 1 }; index < run.timestamps.size(); ++index)
            CHECK(run.timestamps[index] > run.timestamps[index - 1]);
        for (const std::int64_t stamp : run.timestamps)
        {
            const std::int64_t index { (stamp * frames_per_second + 5'000'000) / 10'000'000 };
            CHECK(index >= 0);
            CHECK(index < frames);
            CHECK(stamp == index * 10'000'000 / frames_per_second);
        }
        CHECK(run.width == 64);
        CHECK(run.height == 48);
        CHECK(run.end_position.count() >= 1000 * (frames - 2) / frames_per_second);
        CHECK(run.stopped == media_state::stopped);
        CHECK(run.stopped_timestamp == run.timestamps.front());
        CHECK_FALSE(run.stopped_ticks);
        CHECK(run.replayed == media_state::playing);
    }
}

TEST_CASE("video frames: a generated clip decodes into images with increasing timestamps", "[macos][media]")
{
    const scratch_folder folder {};
    const std::filesystem::path clip { folder.path() / "clip.mp4" };
    constexpr int frames { 12 };
    REQUIRE(write_video(clip, frames));

    SECTION("on the main thread")
    {
        check_frames(run_frames(clip, [] { static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.004, true)); }), frames);
    }
    SECTION("on a logic thread while the main run loop runs")
    {
        frame_run run {};
        std::atomic<bool> done { false };
        std::thread logic { [&] {
            run = run_frames(clip, [] { std::this_thread::sleep_for(4ms); });
            done.store(true);
        } };
        const bool finished { pump_until([&] { return done.load(); }, 10s) };
        logic.join();
        REQUIRE(finished);
        check_frames(run, frames);
    }
}

TEST_CASE("video frames: a wide clip is copied with its long side capped at 1920 pixels", "[macos][media]")
{
    const scratch_folder folder {};
    const std::filesystem::path clip { folder.path() / "wide.mp4" };
    REQUIRE(write_video(clip, 3, 2400, 80));
    luil::win32::video_frame_player player {};
    player.open(clip);
    const auto deadline { std::chrono::steady_clock::now() + 3s };
    while (player.needs_tick() && std::chrono::steady_clock::now() < deadline)
    {
        player.update();
        static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.004, true));
    }
    REQUIRE(player.state() == luil::win32::media_state::ready);
    REQUIRE(player.image().valid());
    CHECK(player.image().width() == 1920);
    CHECK(player.image().height() == 64);
}

// 예제의 견본(MP4·AVI·TS)을 실제로 여는가다. 어느 형식을 여는지는 OS 판의 코덱에 달려 숨은 test로 둔다.
//  - macOS 27에서 셋 다 열리고 재생 시계가 간다. MPEG-TS는 AVPlayerItemVideoOutput이 장을 주지 않아 소리와 시계뿐이다(실측).
TEST_CASE("video frames: the example samples decode", "[macos][media][.samples]")
{
    for (const auto& [name, decodes] : { std::pair { "ridge.mp4", true }, std::pair { "ridge.avi", true }, std::pair { "ridge.ts", false } })
    {
        luil::win32::video_frame_player player {};
        player.open(std::filesystem::path { LUIL_VIDEO_SAMPLES } / name);
        int frames { 0 };
        const auto update = [&](const std::chrono::milliseconds timeout) {
            const auto deadline { std::chrono::steady_clock::now() + timeout };
            while (player.needs_tick() && std::chrono::steady_clock::now() < deadline)
            {
                const auto previous { player.image().backing() };
                player.update();
                if (player.image().valid() && player.image().backing() != previous)
                    ++frames;
                static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.004, true));
            }
        };
        update(3s);
        INFO(name << " error " << reinterpret_cast<const char*>(player.error().c_str()));
        REQUIRE(player.state() == luil::win32::media_state::ready);
        player.play();
        update(500ms);
        CHECK(player.state() == luil::win32::media_state::playing);
        if (decodes)
            CHECK(frames >= 5);
        else
            CHECK(frames == 0);
        CHECK(player.position().count() > 200);
    }
}

TEST_CASE("video frames: missing and non-media files fail with a message", "[macos][media]")
{
    const scratch_folder folder {};
    const std::filesystem::path note { folder.path() / "note.mp4" };
    std::ofstream { note } << "not a movie";
    for (const std::filesystem::path& path : { folder.path() / "missing.mp4", note })
    {
        luil::win32::video_frame_player player {};
        player.open(path);
        const auto deadline { std::chrono::steady_clock::now() + 3s };
        while (player.needs_tick() && std::chrono::steady_clock::now() < deadline)
        {
            player.update();
            static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.004, true));
        }
        INFO("path " << path.string());
        CHECK(player.state() == luil::win32::media_state::failed);
        CHECK(player.error().empty() == false);
        CHECK(player.image().valid() == false);
    }
    luil::win32::video_frame_player empty {};
    empty.open({});
    CHECK(empty.state() == luil::win32::media_state::failed);
}

TEST_CASE("media: open, muted play, pause, seek, end, rewind, and stop without later callbacks", "[macos][media]")
{
    using luil::win32::media_state;
    const scratch_folder folder {};
    const std::filesystem::path clip { folder.path() / "clip.mp4" };
    REQUIRE(write_video(clip, 12));
    media_log log {};
    std::u8string error {};
    auto player { luil::win32::media_player::create(log.config(), error) };
    REQUIRE(player != nullptr);
    REQUIRE(player->open(text(clip), error));
    REQUIRE(pump_until([&] { return log.saw(media_state::ready) || log.saw(media_state::failed); }));
    REQUIRE(log.saw(media_state::ready));
    const auto ready { player->snapshot() };
    CHECK(ready.state == media_state::ready);
    CHECK(ready.duration.count() == 500);
    CHECK(log.snapshot().front().duration == ready.duration);
    CHECK_FALSE(log.on_main());
    REQUIRE(player->volume(0.0f, true, error));
    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.saw(media_state::playing); }));
    REQUIRE(player->pause(error));
    REQUIRE(pump_until([&] { return log.saw(media_state::paused); }));
    REQUIRE(player->seek(std::chrono::milliseconds { ready.duration.count() / 2 }, error));
    REQUIRE(pump_until([&] { return player->snapshot().position.count() >= ready.duration.count() / 2 - 50; }));

    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.saw(media_state::ended); }));
    CHECK(log.snapshot().back().duration == ready.duration);
    CHECK(player->snapshot().state == media_state::ended);
    CHECK_FALSE(player->seek(std::chrono::milliseconds { -1 }, error));
    CHECK_FALSE(player->volume(1.5f, false, error));
    CHECK_FALSE(player->volume(std::numeric_limits<float>::quiet_NaN(), false, error));
    REQUIRE(player->rewind(error));
    REQUIRE(pump_until([&] { return log.saw(media_state::stopped) && player->snapshot().position.count() == 0; }));
    // 끝난 뒤의 재생은 처음부터 다시 간다.
    const auto before_replay { log.snapshot().size() };
    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.snapshot().size() > before_replay && log.snapshot().back().state == media_state::playing; }));
    player->stop();
    const auto count { log.snapshot().size() };
    static_cast<void>(pump_until([] { return false; }, 300ms));
    CHECK(log.snapshot().size() == count);
    player->stop();
    CHECK_FALSE(player->play(error));
    CHECK_FALSE(player->open(text(clip), error));
    CHECK(player->snapshot().state == media_state::empty);
}

TEST_CASE("media: a missing or non-media file reports failed with a native error", "[macos][media]")
{
    using luil::win32::media_state;
    const scratch_folder folder {};
    const std::filesystem::path note { folder.path() / "note.mp4" };
    std::ofstream { note } << "not a movie";
    for (const std::filesystem::path& path : { folder.path() / "missing.mp4", note })
    {
        media_log log {};
        std::u8string error {};
        auto player { luil::win32::media_player::create(log.config(), error) };
        REQUIRE(player != nullptr);
        REQUIRE(player->open(text(path), error));
        INFO("path " << path.string());
        REQUIRE(pump_until([&] { return log.saw(media_state::failed) || log.saw(media_state::ready); }));
        CHECK(log.saw(media_state::failed));
        const auto events { log.snapshot() };
        CHECK(events.back().native_error != 0);
        CHECK(events.back().error.empty() == false);
        player->stop();
    }
    media_log log {};
    std::u8string error {};
    auto player { luil::win32::media_player::create(log.config(), error) };
    REQUIRE(player != nullptr);
    CHECK_FALSE(player->open(u8"", error));
    CHECK_FALSE(player->open(std::u8string_view { u8"a\0b", 3 }, error));
    const std::string invalid { "\xff\xfe" };
    CHECK_FALSE(player->open(std::u8string { invalid.begin(), invalid.end() }, error));
    CHECK_FALSE(player->play(error));
}

TEST_CASE("media: opening twice keeps only the latest source", "[macos][media]")
{
    using luil::win32::media_state;
    const scratch_folder folder {};
    const std::filesystem::path first { folder.path() / "first.mp4" };
    const std::filesystem::path second { folder.path() / "second.mp4" };
    REQUIRE(write_video(first, 12));
    REQUIRE(write_video(second, 6));
    media_log log {};
    std::u8string error {};
    auto player { luil::win32::media_player::create(log.config(), error) };
    REQUIRE(player != nullptr);
    REQUIRE(player->open(text(first), error));
    REQUIRE(player->open(text(second), error));
    REQUIRE(pump_until([&] { return log.saw(media_state::ready) || log.saw(media_state::failed); }));
    static_cast<void>(pump_until([] { return false; }, 300ms));
    const auto events { log.snapshot() };
    CHECK(std::ranges::count_if(events, [](const auto& e) { return e.state == media_state::ready; }) == 1);
    CHECK(player->snapshot().duration.count() == 250);
    player->stop();
}

// 뒤집힌 그릇이다. host의 `LuilContainerView`처럼 왼쪽 위 원점으로 적는다.
@interface LuilMediaTestContainer : NSView
@end

@implementation LuilMediaTestContainer
- (BOOL)isFlipped
{
    return YES;
}
@end

TEST_CASE("media: a video surface follows set_video_bounds above its siblings and is removed by stop", "[macos][media][video]")
{
    using luil::win32::media_state;
    const scratch_folder folder {};
    const std::filesystem::path clip { folder.path() / "clip.mp4" };
    REQUIRE(write_video(clip, 12));
    [NSApplication sharedApplication];
    // 화면에 띄우지 않는 창이다.
    NSWindow* const window { [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 480) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO] };
    window.releasedWhenClosed = NO;
    LuilMediaTestContainer* const container { [[LuilMediaTestContainer alloc] initWithFrame:NSMakeRect(0, 0, 640, 480)] };
    NSView* const skia { [[NSView alloc] initWithFrame:container.bounds] };
    [container addSubview:skia];
    window.contentView = container;
    const CGFloat scale { window.backingScaleFactor > 0 ? window.backingScaleFactor : 1.0 };

    media_log log {};
    std::u8string error {};
    auto config { log.config() };
    config.parent_window = (__bridge void*)window;
    auto player { luil::win32::media_player::create(std::move(config), error) };
    REQUIRE(player != nullptr);
    REQUIRE(container.subviews.count == 2);
    NSView* const surface { container.subviews.lastObject };
    CHECK(surface != skia);
    CHECK(surface.hidden);
    CHECK([surface.layer isKindOfClass:[AVPlayerLayer class]]);
    REQUIRE(player->open(text(clip), error));
    REQUIRE(pump_until([&] { return log.saw(media_state::ready) || log.saw(media_state::failed); }));
    REQUIRE(log.saw(media_state::ready));

    // 나중에 선 형제(웹뷰)가 있어도 맨 위로 다시 선다.
    NSView* const later { [[NSView alloc] initWithFrame:container.bounds] };
    [container addSubview:later];
    player->set_video_bounds({ 20.0f, 40.0f, 320.0f, 180.0f }, true);
    CHECK(container.subviews.lastObject == surface);
    CHECK(NSEqualRects(surface.frame, NSMakeRect(20 / scale, 40 / scale, 320 / scale, 180 / scale)));
    CHECK_FALSE(surface.hidden);
    // 눌림은 받지 않는다.
    CHECK([surface hitTest:NSMakePoint(30 / scale, 50 / scale)] == nil);
    REQUIRE(player->volume(0.0f, true, error));
    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.saw(media_state::playing); }));
    REQUIRE(pump_until([&] { return player->snapshot().position.count() > 100; }));
    player->set_video_bounds({ 0.0f, 0.0f, 0.0f, 0.0f }, false);
    CHECK(surface.hidden);
    // 무한·NaN bounds는 무시한다.
    player->set_video_bounds({ std::numeric_limits<float>::infinity(), 0.0f, 10.0f, 10.0f }, true);
    CHECK(surface.hidden);
    player->set_video_bounds({ 0.0f, std::numeric_limits<float>::quiet_NaN(), 10.0f, 10.0f }, true);
    CHECK(surface.hidden);
    player->stop();
    CHECK(surface.superview == nil);
    CHECK(container.subviews.count == 2);
    [window close];
}

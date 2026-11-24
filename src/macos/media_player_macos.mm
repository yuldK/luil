#include "luil/win32/media_player.h"

#include "macos/media_rules.h"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

// `media_player`의 macOS 몸이다 (video-playback-design.md의 "macOS"). AVPlayer와 AVPlayerLayer다.
//  - `parent_window`는 `NSWindow*`다 (host의 `on_native_window_created`가 건네는 값). 영상 view를 그 창 내용 view의
//    맨 위 자식으로 둔다. Windows의 `HWND_TOP` 자식 창과 같은 자리다. 눌림은 받지 않고 아래 Skia view로 넘긴다.
//  - AVFoundation의 알림(KVO·NSNotification)은 아무 thread에서나 온다. 모두 재생기의 직렬 queue로 옮겨, 거기서 상태를
//    정하고 `deliver`를 부른다. MFPlay의 worker callback과 같은 자리다. `stop`은 같은 잠금으로 진행 중인 배달을 기다린다.
//  - 사건의 짝은 MFPlay와 같다. 열기 완료 → ready, 실제 재생 시작 → playing, `pause` → paused, `rewind` → stopped,
//    끝 → ended, 열기·재생 실패 → failed다. 탐색·볼륨은 사건을 내지 않는다.
namespace luil::win32 {
    struct media_channel;
}

// AVFoundation 알림을 재생기의 신호로 옮긴다. 항목 하나(열 때마다 새로)나 재생기 자체를 본다.
@interface LuilMediaObserver : NSObject
- (instancetype)initWithChannel:(std::shared_ptr<luil::win32::media_channel>)channel generation:(std::uint64_t)generation;
@end

// 영상 표면이다. 받침 layer가 AVPlayerLayer이고, 왼쪽 위 원점으로 적는다.
@interface LuilMediaSurfaceView : NSView
@end

namespace luil::win32 {
    namespace {
        constexpr char8_t stopped_error[] { u8"The AVFoundation player is stopped." };

        media_event read_snapshot(AVPlayer* const player, const media_state state)
        {
            media_event event { .state = state };
            if (player == nil)
                return event;
            const CMTime position { player.currentTime };
            if (CMTIME_IS_NUMERIC(position))
                event.position = macos::media_time_ms(position.value, position.timescale);
            AVPlayerItem* const item { player.currentItem };
            if (item != nil)
            {
                // 생방송처럼 길이가 정해지지 않은 원본은 0이다 (MFPlay와 같다).
                const CMTime duration { item.duration };
                if (CMTIME_IS_NUMERIC(duration))
                    event.duration = macos::media_time_ms(duration.value, duration.timescale);
            }
            return event;
        }
    }

    // 알림 쪽과 재생기 쪽이 함께 쥐는 몫이다. `stop` 뒤에 늦게 온 블록도 이것을 보고 그냥 돌아간다.
    struct media_channel
    {
        std::recursive_mutex gate {};
        std::function<void(media_event)> deliver {};
        // 잠금 안에서만 읽는다. `stop`이 비운다.
        AVPlayer* player { nil };
        dispatch_queue_t queue { dispatch_queue_create("io.github.yuldk.luil.media", DISPATCH_QUEUE_SERIAL) };
        // 지금 연 원본의 번호다. 이전 원본의 늦은 알림을 버린다.
        std::atomic<std::uint64_t> generation { 0 };
        media_state state { media_state::empty };
        bool stopped { false };
    };

    namespace {
        void handle(media_channel& channel, const std::uint64_t generation, const macos::media_signal signal, NSError* const error)
        {
            const std::scoped_lock lock { channel.gate };
            if (channel.stopped || generation != channel.generation.load())
                return;
            // KVO는 늦게 온다. 그 사이 `pause`를 불렀으면 지금 값이 재생이 아니다.
            if (signal == macos::media_signal::playing && channel.player.timeControlStatus != AVPlayerTimeControlStatusPlaying)
                return;
            const std::optional<media_state> next { macos::next_media_state(channel.state, signal) };
            if (next.has_value() == false)
                return;
            channel.state = *next;
            try
            {
                media_event event { read_snapshot(channel.player, channel.state) };
                if (channel.state == media_state::failed)
                {
                    event.native_error = error != nil && error.code != 0 ? static_cast<long>(error.code) : -1;
                    event.error = u8"AVFoundation could not load or play the source.";
                }
                if (channel.deliver != nullptr)
                    channel.deliver(std::move(event));
            }
            catch (...)
            {
            }
            // 열기 전에 부른 `play`는 열린 뒤 바로 재생한다. 그 KVO는 ready보다 먼저 와서 버려졌다.
            if (signal == macos::media_signal::item_ready && channel.player.timeControlStatus == AVPlayerTimeControlStatusPlaying)
                handle(channel, generation, macos::media_signal::playing, nil);
        }

        // 신호를 직렬 queue로 옮긴다. 부른 thread(UI thread의 KVO일 수도 있다)에서 바로 배달하지 않는다.
        void post(const std::shared_ptr<media_channel>& channel, const std::uint64_t generation, const macos::media_signal signal, NSError* const error = nil)
        {
            std::shared_ptr<media_channel> target { channel };
            dispatch_async(channel->queue, ^{
                handle(*target, generation, signal, error);
            });
        }
    }
}

@implementation LuilMediaObserver
{
    std::shared_ptr<luil::win32::media_channel> _channel;
    std::uint64_t _generation;
}

- (instancetype)initWithChannel:(std::shared_ptr<luil::win32::media_channel>)channel generation:(const std::uint64_t)generation
{
    self = [super init];
    if (self != nil)
    {
        _channel = std::move(channel);
        _generation = generation;
    }
    return self;
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey, id>*)change context:(void*)context
{
    static_cast<void>(change);
    static_cast<void>(context);
    using luil::macos::media_signal;
    if ([keyPath isEqualToString:@"status"] && [object isKindOfClass:[AVPlayerItem class]])
    {
        AVPlayerItem* const item { object };
        if (item.status == AVPlayerItemStatusReadyToPlay)
            luil::win32::post(_channel, _generation, media_signal::item_ready);
        else if (item.status == AVPlayerItemStatusFailed)
            luil::win32::post(_channel, _generation, media_signal::item_failed, item.error);
        return;
    }
    // 재생기는 원본을 가리지 않는다. 그때 연 원본의 번호로 보낸다.
    if ([keyPath isEqualToString:@"timeControlStatus"])
    {
        if (static_cast<AVPlayer*>(object).timeControlStatus == AVPlayerTimeControlStatusPlaying)
            luil::win32::post(_channel, _channel->generation.load(), media_signal::playing);
    }
}

- (void)itemEnded:(NSNotification*)notification
{
    static_cast<void>(notification);
    luil::win32::post(_channel, _generation, luil::macos::media_signal::ended);
}

- (void)itemFailed:(NSNotification*)notification
{
    NSError* const error { notification.userInfo[AVPlayerItemFailedToPlayToEndTimeErrorKey] };
    luil::win32::post(_channel, _generation, luil::macos::media_signal::playback_failed, error);
}
@end

@implementation LuilMediaSurfaceView
- (BOOL)isFlipped
{
    return YES;
}

- (CALayer*)makeBackingLayer
{
    AVPlayerLayer* const layer { [AVPlayerLayer playerLayerWithPlayer:nil] };
    layer.videoGravity = AVLayerVideoGravityResizeAspect;
    layer.backgroundColor = NSColor.blackColor.CGColor;
    return layer;
}

// 표면은 보이기만 한다. 눌림·끌기는 아래의 Skia view(그 위 overlay 요소)가 받는다.
- (NSView*)hitTest:(NSPoint)point
{
    static_cast<void>(point);
    return nil;
}
@end

namespace luil::win32 {
    struct media_player::engine
    {
        std::shared_ptr<media_channel> channel { std::make_shared<media_channel>() };
        AVPlayer* player { nil };
        LuilMediaObserver* player_observer { nil };
        AVPlayerItem* item { nil };
        LuilMediaObserver* item_observer { nil };
        LuilMediaSurfaceView* view { nil };

        void detach_item()
        {
            if (item == nil)
                return;
            [item removeObserver:item_observer forKeyPath:@"status"];
            [NSNotificationCenter.defaultCenter removeObserver:item_observer];
            item = nil;
            item_observer = nil;
        }

        [[nodiscard]] media_state state()
        {
            const std::scoped_lock lock { channel->gate };
            return channel->state;
        }
    };

    media_player::media_player(std::unique_ptr<engine> engine) : engine_ { std::move(engine) }
    {
    }

    media_player::~media_player()
    {
        stop();
    }

    std::unique_ptr<media_player> media_player::create(media_player_config config, std::u8string& error)
    {
        assert(NSThread.isMainThread);
        auto engine { std::make_unique<media_player::engine>() };
        engine->channel->deliver = std::move(config.deliver);
        engine->player = [AVPlayer new];
        if (engine->player == nil)
        {
            error = u8"Could not create the AVFoundation player.";
            return nullptr;
        }
        engine->player.actionAtItemEnd = AVPlayerActionAtItemEndPause;
        if (config.parent_window != nullptr)
        {
            NSWindow* const window { (__bridge NSWindow*)config.parent_window };
            NSView* const content { window.contentView };
            if (content == nil)
            {
                error = u8"The parent window has no content view for the video surface.";
                return nullptr;
            }
            engine->view = [[LuilMediaSurfaceView alloc] initWithFrame:NSZeroRect];
            engine->view.wantsLayer = YES;
            engine->view.hidden = YES;
            static_cast<AVPlayerLayer*>(engine->view.layer).player = engine->player;
            [content addSubview:engine->view positioned:NSWindowAbove relativeTo:nil];
        }
        engine->channel->player = engine->player;
        engine->player_observer = [[LuilMediaObserver alloc] initWithChannel:engine->channel generation:0];
        [engine->player addObserver:engine->player_observer forKeyPath:@"timeControlStatus" options:NSKeyValueObservingOptionNew context:nullptr];
        return std::unique_ptr<media_player> { new media_player { std::move(engine) } };
    }

    bool media_player::open(const std::u8string_view source, std::u8string& error)
    {
        if (source.empty() || source.find(char8_t {}) != std::u8string_view::npos || engine_->player == nil)
        {
            error = u8"Invalid media source or stopped player.";
            return false;
        }
        NSString* const text { [[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding] };
        if (text == nil)
        {
            error = u8"Media source is not valid UTF-8.";
            return false;
        }
        NSURL* const url { macos::media_source_is_url(source) ? [NSURL URLWithString:text] : [NSURL fileURLWithPath:text] };
        if (url == nil)
        {
            error = u8"Media source is not a valid URL.";
            return false;
        }
        engine_->detach_item();
        const std::uint64_t generation { ++engine_->channel->generation };
        {
            const std::scoped_lock lock { engine_->channel->gate };
            engine_->channel->state = media_state::opening;
        }
        engine_->item = [AVPlayerItem playerItemWithURL:url];
        engine_->item_observer = [[LuilMediaObserver alloc] initWithChannel:engine_->channel generation:generation];
        [engine_->item addObserver:engine_->item_observer forKeyPath:@"status" options:NSKeyValueObservingOptionNew context:nullptr];
        NSNotificationCenter* const center { NSNotificationCenter.defaultCenter };
        [center addObserver:engine_->item_observer selector:@selector(itemEnded:) name:AVPlayerItemDidPlayToEndTimeNotification object:engine_->item];
        [center addObserver:engine_->item_observer selector:@selector(itemFailed:) name:AVPlayerItemFailedToPlayToEndTimeNotification object:engine_->item];
        [engine_->player replaceCurrentItemWithPlayerItem:engine_->item];
        return true;
    }

    bool media_player::play(std::u8string& error)
    {
        if (engine_->player == nil || engine_->item == nil)
        {
            error = engine_->player == nil ? stopped_error : u8"No media source is open.";
            return false;
        }
        // AVPlayer는 끝에 선 채로는 다시 재생하지 않는다. MFPlay처럼 처음부터 다시 간다.
        if (engine_->state() == media_state::ended)
            [engine_->player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        [engine_->player play];
        return true;
    }

    bool media_player::pause(std::u8string& error)
    {
        if (engine_->player == nil)
        {
            error = stopped_error;
            return false;
        }
        [engine_->player pause];
        post(engine_->channel, engine_->channel->generation.load(), macos::media_signal::paused);
        return true;
    }

    bool media_player::rewind(std::u8string& error)
    {
        if (engine_->player == nil)
        {
            error = stopped_error;
            return false;
        }
        [engine_->player pause];
        post(engine_->channel, engine_->channel->generation.load(), macos::media_signal::stopped);
        return seek(std::chrono::milliseconds { 0 }, error);
    }

    bool media_player::seek(const std::chrono::milliseconds position, std::u8string& error)
    {
        if (position.count() < 0 || position.count() > std::numeric_limits<std::int64_t>::max() / 10000 || engine_->player == nil)
        {
            error = u8"Invalid media seek position or stopped player.";
            return false;
        }
        [engine_->player seekToTime:CMTimeMake(position.count(), 1000) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        return true;
    }

    bool media_player::volume(const float value, const bool muted, std::u8string& error)
    {
        if (std::isfinite(value) == false || value < 0.0f || value > 1.0f || engine_->player == nil)
        {
            error = u8"Invalid media volume or stopped player.";
            return false;
        }
        engine_->player.volume = value;
        engine_->player.muted = muted;
        return true;
    }

    void media_player::set_video_bounds(const rect_f bounds, const bool visible) noexcept
    {
        assert(NSThread.isMainThread);
        LuilMediaSurfaceView* const view { engine_->view };
        NSView* const container { view.superview };
        if (view == nil || container == nil)
            return;
        if (std::isfinite(bounds.x) == false || std::isfinite(bounds.y) == false || std::isfinite(bounds.width) == false || std::isfinite(bounds.height) == false)
            return;
        // 물리 픽셀을 창의 배율로 나눈 점이다. 그릇이 뒤집혀 있지 않으면(host 밖의 창) 아래 원점으로 바꾼다.
        const CGFloat scale { view.window != nil && view.window.backingScaleFactor > 0 ? view.window.backingScaleFactor : 1.0 };
        const auto point = [scale](const float value) { return static_cast<CGFloat>(std::clamp(value, -1000000.0f, 1000000.0f)) / scale; };
        NSRect frame { NSMakeRect(point(bounds.x), point(bounds.y), point(std::max(0.0f, bounds.width)), point(std::max(0.0f, bounds.height))) };
        if (container.isFlipped == false)
            frame.origin.y = container.bounds.size.height - frame.origin.y - frame.size.height;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        view.frame = frame;
        view.hidden = visible == false;
        // 뒤에 선 웹뷰보다 위에 둔다 (Windows의 `HWND_TOP`).
        if (container.subviews.lastObject != view)
            [container addSubview:view positioned:NSWindowAbove relativeTo:nil];
        [CATransaction commit];
        repaint();
    }

    void media_player::repaint() noexcept
    {
        // AVPlayerLayer가 제 장을 스스로 그린다. MFPlay의 `UpdateVideo` 자리라 할 일이 없다.
    }

    media_event media_player::snapshot() const
    {
        const std::scoped_lock lock { engine_->channel->gate };
        return engine_->channel->player != nil ? read_snapshot(engine_->channel->player, engine_->channel->state) : media_event {};
    }

    void media_player::stop() noexcept
    {
        assert(NSThread.isMainThread);
        {
            const std::scoped_lock lock { engine_->channel->gate };
            engine_->channel->stopped = true;
            engine_->channel->deliver = {};
            engine_->channel->player = nil;
        }
        if (engine_->player != nil)
        {
            engine_->detach_item();
            [engine_->player removeObserver:engine_->player_observer forKeyPath:@"timeControlStatus"];
            engine_->player_observer = nil;
            [engine_->player pause];
            [engine_->player replaceCurrentItemWithPlayerItem:nil];
            engine_->player = nil;
        }
        if (engine_->view != nil)
        {
            static_cast<AVPlayerLayer*>(engine_->view.layer).player = nil;
            [engine_->view removeFromSuperview];
            engine_->view = nil;
        }
    }
}

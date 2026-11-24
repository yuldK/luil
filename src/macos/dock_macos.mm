#include "macos/dock_macos.h"

#include "host/taskbar_sync.h"
#include "macos/dock_plan.h"
#include "macos/macos_text.h"
#include "ui/image_pixels.h"

#include "include/core/SkImageInfo.h"

#import <AppKit/AppKit.h>

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Dock 타일 content view다. 앱 아이콘을 그리고 그 위에 진행 막대와 오버레이를 얹는다.
@interface LuilDockTileView : NSView
- (void)showBar:(const luil::macos::dock_bar&)bar overlay:(NSImage*)overlay;
@end

// Dock 메뉴 항목의 대상이다. 항목의 `representedObject`가 인자 목록(NSArray<NSString*>)이다.
@interface LuilDockMenuTarget : NSObject
- (void)launchTask:(NSMenuItem*)sender;
@end

namespace luil::macos {
    namespace {
        void log_error(const std::u8string_view message)
        {
            std::fprintf(stderr, "luil: %.*s\n", static_cast<int>(message.size()), reinterpret_cast<const char*>(message.data()));
        }

        [[nodiscard]] NSRect ns_rect(const dock_rect& rect)
        {
            return NSMakeRect(rect.x, rect.y, rect.width, rect.height);
        }

        [[nodiscard]] NSColor* tint_color(const dock_bar_tint tint)
        {
            switch (tint)
            {
            case dock_bar_tint::error:
                return [NSColor systemRedColor];
            case dock_bar_tint::paused:
                return [NSColor systemYellowColor];
            case dock_bar_tint::accent:
            default:
                return [NSColor controlAccentColor];
            }
        }

        // 막대를 그린다. 바탕은 어두운 반투명, 채운 몫은 색, 불확정은 그 위에 밝은 빗금이다. 둥근 바탕 밖으로는 나가지 않는다.
        void draw_bar(const dock_rect& area, const dock_bar& bar)
        {
            const NSRect track { ns_rect(area) };
            const CGFloat radius { track.size.height / 2.0 };
            NSBezierPath* const outline { [NSBezierPath bezierPathWithRoundedRect:track xRadius:radius yRadius:radius] };
            [[NSColor colorWithWhite:0.0 alpha:0.55] setFill];
            [outline fill];

            NSRect filled { track };
            filled.size.width *= static_cast<CGFloat>(bar.fraction);
            if (filled.size.width <= 0.0)
                return;
            [NSGraphicsContext saveGraphicsState];
            [outline addClip];
            [tint_color(bar.tint) setFill];
            [NSBezierPath fillRect:filled];
            if (bar.striped)
            {
                // 높이만큼 기운 평행사변형을 높이의 두 배 간격으로 늘어놓는다.
                const CGFloat height { track.size.height };
                [[NSColor colorWithWhite:1.0 alpha:0.35] setFill];
                for (CGFloat x { NSMinX(track) - height }; x < NSMaxX(track); x += height * 2.0)
                {
                    NSBezierPath* const stripe { [NSBezierPath bezierPath] };
                    [stripe moveToPoint:NSMakePoint(x, NSMinY(track))];
                    [stripe lineToPoint:NSMakePoint(x + height, NSMinY(track))];
                    [stripe lineToPoint:NSMakePoint(x + height * 2.0, NSMaxY(track))];
                    [stripe lineToPoint:NSMakePoint(x + height, NSMaxY(track))];
                    [stripe closePath];
                    [stripe fill];
                }
            }
            [NSGraphicsContext restoreGraphicsState];
        }

        // 그림을 곱한 RGBA로 펴서 CGImage에 싣는다 (macos_clipboard.mm과 같은 길, 그쪽은 곱하지 않은 값이다).
        [[nodiscard]] NSImage* make_ns_image(const ui_image& image, std::u8string& error)
        {
            SkBitmap pixels {};
            if (read_image_pixels(image, pixels, error) == false)
                return nil;
            const int width { pixels.width() };
            const int height { pixels.height() };
            if (width <= 0 || height <= 0)
            {
                error = u8"The Dock overlay image is empty.";
                return nil;
            }
            const std::size_t stride { static_cast<std::size_t>(width) * 4 };
            NSMutableData* const bytes { [NSMutableData dataWithLength:stride * static_cast<std::size_t>(height)] };
            const SkImageInfo information { SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType) };
            if (pixels.readPixels(information, bytes.mutableBytes, stride, 0, 0) == false)
            {
                error = u8"Cannot copy the pixels of the Dock overlay image.";
                return nil;
            }
            CGDataProviderRef const provider { CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes) };
            CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
            CGImageRef const picture { CGImageCreate(static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, 32, stride, space,
                kCGBitmapByteOrderDefault | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast), provider, nullptr, false, kCGRenderingIntentDefault) };
            CGColorSpaceRelease(space);
            CGDataProviderRelease(provider);
            if (picture == nullptr)
            {
                error = u8"Cannot build the Dock overlay image.";
                return nil;
            }
            NSImage* const result { [[NSImage alloc] initWithCGImage:picture size:NSMakeSize(width, height)] };
            CGImageRelease(picture);
            return result;
        }

        // 작업 하나를 메뉴 항목으로 옮긴다. 글이 UTF-8이 아니면 nil이다.
        [[nodiscard]] NSMenuItem* make_item(const taskbar_jump_task& task, LuilDockMenuTarget* const target)
        {
            NSString* const title { make_ns_string(task.title) };
            NSString* const description { make_ns_string(task.description) };
            if (title == nil || description == nil)
                return nil;
            NSMutableArray<NSString*>* const arguments { [NSMutableArray arrayWithCapacity:task.arguments.size()] };
            for (const std::u8string& argument : task.arguments)
            {
                NSString* const text { make_ns_string(argument) };
                if (text == nil)
                    return nil;
                [arguments addObject:text];
            }
            NSMenuItem* const item { [[NSMenuItem alloc] initWithTitle:title action:@selector(launchTask:) keyEquivalent:@""] };
            item.target = target;
            item.representedObject = [arguments copy];
            if (description.length > 0)
                item.toolTip = description;
            return item;
        }

        // 이 실행 파일을 인자로 새로 띄운다. 기다리지 않는다. 실패는 표준 오류에 남긴다 (고른 순간이라 알릴 보고가 없다).
        //  - 번들(.app)이면 LaunchServices에 새 인스턴스를 청한다. 번들이 아닌 맨 실행 파일이면 NSTask로 바로 띄운다
        //    (NSTask가 끝난 자식을 거둔다).
        void launch_instance(NSArray<NSString*>* const arguments)
        {
            NSBundle* const bundle { [NSBundle mainBundle] };
            if ([bundle.bundlePath.pathExtension isEqualToString:@"app"])
            {
                NSWorkspaceOpenConfiguration* const configuration { [NSWorkspaceOpenConfiguration configuration] };
                configuration.arguments = arguments;
                configuration.createsNewApplicationInstance = YES;
                [[NSWorkspace sharedWorkspace] openApplicationAtURL:bundle.bundleURL configuration:configuration completionHandler:^(NSRunningApplication*, NSError* const error) {
                    if (error != nil)
                        log_error(u8"Starting a new instance for the Dock menu task failed: " + from_ns_string(error.localizedDescription));
                }];
                return;
            }
            NSString* const path { bundle.executablePath };
            if (path == nil)
            {
                log_error(u8"The executable path for the Dock menu task could not be read.");
                return;
            }
            NSError* error { nil };
            if ([NSTask launchedTaskWithExecutableURL:[NSURL fileURLWithPath:path] arguments:arguments error:&error terminationHandler:nil] == nil)
                log_error(u8"Starting the executable for the Dock menu task failed: " + from_ns_string(error.localizedDescription));
        }
    }

    struct dock_host::state
    {
        taskbar_sync sync {};
        jump_list_sync jump_lists {};
        std::function<void(const win32::taskbar_report&)> taskbar_reporter {};
        std::function<void(const win32::jump_list_report&)> jump_list_reporter {};
        // 마지막으로 알린 보고다. 처음은 `off`라 기본 요청은 알리지 않는다 (Win32와 같다).
        win32::taskbar_report reported {};
        // 마지막 호출 묶음이 실패했다. 다음 변화에서 다시 본다.
        bool failed { false };
        std::u8string error {};

        // 타일에 낸 상태다.
        taskbar_progress_state progress { taskbar_progress_state::none };
        std::uint32_t units { 0 };
        NSImage* overlay { nil };
        std::u8string description {};

        LuilDockTileView* view { nil };
        LuilDockMenuTarget* target { [[LuilDockMenuTarget alloc] init] };
        NSMenu* menu { nil };

        void execute(const taskbar_call& call, std::u8string& failure)
        {
            switch (call.kind)
            {
            case taskbar_call_kind::progress_value:
                units = call.units;
                break;
            case taskbar_call_kind::progress_state:
                progress = call.state;
                if (progress == taskbar_progress_state::none || progress == taskbar_progress_state::indeterminate)
                    units = 0;
                break;
            case taskbar_call_kind::overlay_set:
            {
                std::u8string reason {};
                overlay = make_ns_image(call.icon, reason);
                description = call.description;
                if (overlay == nil && failure.empty())
                    failure = std::move(reason);
                break;
            }
            case taskbar_call_kind::overlay_description:
                description = call.description;
                break;
            case taskbar_call_kind::overlay_clear:
            default:
                overlay = nil;
                description.clear();
                break;
            }
        }

        // 낸 상태를 타일에 그린다. 그릴 것이 없으면 우리 view를 떼어 평소 아이콘으로 돌린다.
        void redraw()
        {
            NSDockTile* const tile { NSApp.dockTile };
            if (tile == nil)
                return;
            const dock_bar bar { make_dock_bar(progress, units) };
            if (bar.visible == false && overlay == nil)
            {
                if (view != nil && tile.contentView == view)
                    tile.contentView = nil;
                [tile display];
                return;
            }
            if (view == nil)
                view = [[LuilDockTileView alloc] initWithFrame:NSMakeRect(0.0, 0.0, tile.size.width, tile.size.height)];
            else
                [view setFrameSize:tile.size];
            [view showBar:bar overlay:overlay];
            view.accessibilityLabel = description.empty() ? nil : make_ns_string(description);
            if (tile.contentView != view)
                tile.contentView = view;
            [tile display];
        }

        void report_taskbar()
        {
            win32::taskbar_report report {};
            if (failed)
            {
                report.status = win32::taskbar_status::failed;
                report.error = error;
            }
            else
                report.status = sync.state() == taskbar_sync_state::off ? win32::taskbar_status::off : win32::taskbar_status::applied;
            if (report == reported)
                return;
            reported = report;
            if (taskbar_reporter)
                taskbar_reporter(report);
        }

        [[nodiscard]] win32::jump_list_report run(const jump_list_call& call)
        {
            // 규칙 밖의 목록은 메뉴를 바꾸지 않는다. 앞서 지은 메뉴는 그대로 남는다 (Win32와 같다).
            if (call.kind == jump_list_call_kind::remove)
            {
                menu = nil;
                return win32::jump_list_report { .status = win32::jump_list_status::removed };
            }
            if (const std::optional<std::u8string> problem { dock_menu_problem(call.list) }; problem.has_value())
                return win32::jump_list_report { .status = win32::jump_list_status::failed, .error = *problem };
            NSMenu* const built { [[NSMenu alloc] initWithTitle:@""] };
            for (std::size_t index = 0; index < call.list.tasks.size(); ++index)
            {
                NSMenuItem* const item { make_item(call.list.tasks[index], target) };
                if (item == nil)
                    return win32::jump_list_report { .status = win32::jump_list_status::failed, .error = dock_task_error(index, u8"has text that is not valid UTF-8.") };
                [built addItem:item];
            }
            menu = built;
            return win32::jump_list_report { .status = win32::jump_list_status::applied, .tasks = call.list.tasks.size() };
        }
    };

    dock_host::dock_host()
        : state_ { std::make_unique<state>() }
    {
        // 타일은 앱이 떠 있는 동안 늘 있다. 처음부터 단추가 선 것으로 본다.
        state_->sync.button_created();
    }

    dock_host::~dock_host()
    {
        NSDockTile* const tile { NSApp.dockTile };
        if (tile != nil && state_->view != nil && tile.contentView == state_->view)
        {
            tile.contentView = nil;
            [tile display];
        }
    }

    void dock_host::set_report(std::function<void(const win32::taskbar_report&)> report)
    {
        state_->taskbar_reporter = std::move(report);
    }

    void dock_host::set_jump_list_report(std::function<void(const win32::jump_list_report&)> report)
    {
        state_->jump_list_reporter = std::move(report);
    }

    void dock_host::update(const taskbar_request& wanted)
    {
        @autoreleasepool
        {
            if (const std::vector<taskbar_call> calls { state_->sync.update(wanted) }; calls.empty() == false)
            {
                std::u8string failure {};
                for (const taskbar_call& call : calls)
                    state_->execute(call, failure);
                state_->failed = failure.empty() == false;
                state_->error = std::move(failure);
                state_->redraw();
            }
            state_->report_taskbar();

            if (const std::optional<jump_list_call> call { state_->jump_lists.update(wanted.jump_list) }; call.has_value())
            {
                const win32::jump_list_report report { state_->run(*call) };
                if (state_->jump_list_reporter)
                    state_->jump_list_reporter(report);
            }
        }
    }

    NSMenu* dock_host::dock_menu() const noexcept
    {
        return state_->menu;
    }
}

@implementation LuilDockTileView {
    luil::macos::dock_bar bar_;
    NSImage* overlay_;
}

- (void)showBar:(const luil::macos::dock_bar&)bar overlay:(NSImage*)overlay
{
    bar_ = bar;
    overlay_ = overlay;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirty
{
    static_cast<void>(dirty);
    const NSRect bounds { self.bounds };
    [NSApp.applicationIconImage drawInRect:bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
    const luil::macos::dock_tile_layout layout { luil::macos::layout_dock_tile(static_cast<float>(bounds.size.width), static_cast<float>(bounds.size.height), bar_.visible) };
    if (bar_.visible)
        luil::macos::draw_bar(layout.track, bar_);
    if (overlay_ != nil)
    {
        const NSSize size { overlay_.size };
        const luil::macos::dock_rect target { luil::macos::fit_dock_rect(layout.overlay, static_cast<float>(size.width), static_cast<float>(size.height)) };
        [NSGraphicsContext currentContext].imageInterpolation = NSImageInterpolationHigh;
        [overlay_ drawInRect:luil::macos::ns_rect(target) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
    }
}
@end

@implementation LuilDockMenuTarget

- (void)launchTask:(NSMenuItem*)sender
{
    NSArray<NSString*>* const arguments { [sender.representedObject isKindOfClass:[NSArray class]] ? sender.representedObject : @[] };
    luil::macos::launch_instance(arguments);
}
@end

#include "macos/tray_icon_macos.h"

#include "macos/macos_text.h"
#include "ui/image_pixels.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

// 상태 항목 단추의 동작·메뉴 항목 선택·메뉴 막대 모양의 변화를 받아 host 객체에 넘긴다.
//  - host 객체가 먼저 사라지면 `owner`가 nullptr로 비워진다. 그 뒤의 소식은 버린다.
@interface LuilTrayTarget : NSObject
@property(nonatomic, assign) luil::macos::tray_icon_host::impl* owner;
- (void)buttonPressed:(id)sender;
- (void)menuItemChosen:(NSMenuItem*)sender;
@end

namespace luil::macos {
    namespace {
        // 단추 모양의 KVO 표지다. 이 표지가 아닌 소식은 부모에게 넘긴다.
        char appearance_context { 0 };

        // 메뉴 막대가 밝은가다. 상태 항목이 섰으면 그 단추의 모양(메뉴 막대의 것)을, 아니면 앱의 모양을 읽는다.
        [[nodiscard]] bool light_menu_bar(NSStatusItem* const item)
        {
            NSAppearance* const appearance { item != nil && item.button != nil ? item.button.effectiveAppearance : [NSApp effectiveAppearance] };
            if (appearance == nil)
                return true;
            NSAppearanceName const match { [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] };
            return [match isEqualToString:NSAppearanceNameDarkAqua] == NO;
        }

        // 그림 한 변의 물리 픽셀이다. 메뉴 막대는 모든 화면에 서므로 가장 큰 배율에 맞춘다 (작은 배율에서는 AppKit이 줄인다).
        [[nodiscard]] int icon_pixels()
        {
            CGFloat scale { 1.0 };
            for (NSScreen* const screen in [NSScreen screens])
                scale = std::max(scale, screen.backingScaleFactor);
            return static_cast<int>(std::lround(tray_icon_points * scale));
        }

        [[nodiscard]] screen_point screen_pixels(const NSPoint point)
        {
            NSArray<NSScreen*>* const screens { [NSScreen screens] };
            const CGFloat height { screens.count > 0 ? screens[0].frame.size.height : 0.0 };
            const CGFloat scale { screens.count > 0 ? screens[0].backingScaleFactor : 1.0 };
            return tray_screen_point(point.x, point.y, height, scale);
        }

        // 단추 가운데의 화면 점이다 (마우스가 아닌 누름의 자리).
        [[nodiscard]] NSPoint button_center(NSStatusBarButton* const button)
        {
            if (button == nil || button.window == nil)
                return [NSEvent mouseLocation];
            const NSRect frame { [button.window convertRectToScreen:[button convertRect:button.bounds toView:nil]] };
            return NSMakePoint(NSMidX(frame), NSMidY(frame));
        }

        [[nodiscard]] NSString* application_name()
        {
            NSString* const name { [NSRunningApplication currentApplication].localizedName };
            if (name != nil && name.length > 0)
                return name;
            return [NSProcessInfo processInfo].processName;
        }

        // 그림이 빈 요청의 물러설 자리다. 앱 아이콘의 사본을 그림 크기로 둔다 (공유 그림의 크기를 바꾸지 않는다).
        [[nodiscard]] NSImage* application_icon()
        {
            NSImage* const shared { NSApp != nil && NSApp.applicationIconImage != nil ? NSApp.applicationIconImage : [NSImage imageNamed:NSImageNameApplicationIcon] };
            if (shared == nil)
                return nil;
            NSImage* const copy { [shared copy] };
            copy.size = NSMakeSize(tray_icon_points, tray_icon_points);
            return copy;
        }

        // 앱 그림을 `size`×`size` 픽셀의 정사각형 안에 비율을 지켜 가운데에 맞춰 편다 (Win32 `make_icon`과 같은 맞춤이다).
        [[nodiscard]] NSImage* make_status_image(const ui_image& image, const int size, std::u8string& error)
        {
            if (size <= 0 || image.valid() == false)
            {
                error = u8"The tray icon needs a valid image and a positive size.";
                return nil;
            }
            SkBitmap source {};
            if (read_image_pixels(image, source, error) == false)
                return nil;
            if (source.width() <= 0 || source.height() <= 0)
            {
                error = u8"The tray icon image is empty.";
                return nil;
            }
            SkBitmap scaled {};
            if (scaled.tryAllocN32Pixels(size, size) == false)
            {
                error = u8"Cannot allocate the pixels of the tray icon.";
                return nil;
            }
            scaled.eraseColor(SK_ColorTRANSPARENT);
            {
                const float fit { std::min(static_cast<float>(size) / static_cast<float>(source.width()), static_cast<float>(size) / static_cast<float>(source.height())) };
                const float width { static_cast<float>(source.width()) * fit };
                const float height { static_cast<float>(source.height()) * fit };
                const SkRect target { SkRect::MakeXYWH((static_cast<float>(size) - width) / 2.0f, (static_cast<float>(size) - height) / 2.0f, width, height) };
                SkCanvas canvas { scaled };
                SkPaint paint {};
                paint.setAntiAlias(true);
                canvas.drawImageRect(source.asImage().get(), target, SkSamplingOptions { SkFilterMode::kLinear, SkMipmapMode::kLinear }, &paint);
            }

            // N32의 바이트 차례는 빌드마다 다를 수 있어 곱한 RGBA로 다시 읽어 CGImage에 넘긴다.
            const std::size_t stride { static_cast<std::size_t>(size) * 4 };
            NSMutableData* const bytes { [NSMutableData dataWithLength:stride * static_cast<std::size_t>(size)] };
            const SkImageInfo information { SkImageInfo::Make(size, size, kRGBA_8888_SkColorType, kPremul_SkAlphaType) };
            if (scaled.readPixels(information, bytes.mutableBytes, stride, 0, 0) == false)
            {
                error = u8"Cannot copy the pixels of the tray icon.";
                return nil;
            }
            CGDataProviderRef const provider { CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes) };
            CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
            CGImageRef const picture { CGImageCreate(static_cast<std::size_t>(size), static_cast<std::size_t>(size), 8, 32, stride, space,
                kCGBitmapByteOrderDefault | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast), provider, nullptr, false, kCGRenderingIntentDefault) };
            CGColorSpaceRelease(space);
            CGDataProviderRelease(provider);
            if (picture == nullptr)
            {
                error = u8"Cannot build the tray icon image.";
                return nil;
            }
            NSImage* const result { [[NSImage alloc] initWithCGImage:picture size:NSMakeSize(tray_icon_points, tray_icon_points)] };
            CGImageRelease(picture);
            return result;
        }

        [[nodiscard]] NSString* menu_label(const std::u8string& text)
        {
            NSString* const label { make_ns_string(text) };
            return label != nil ? label : @"";
        }

        // 한 메뉴 층의 단축키 글을 오른쪽 탭에 맞출 자리다 (점). 단축키 글이 없으면 0이다.
        //  - 단축키 글은 보이기만 한다 (Win32의 탭 뒤 글과 같다). 키 등가물로 걸지 않는다 — 글이 어느 수정자를 뜻하는지
        //    (Ctrl이 Command인지) 앱만 안다.
        [[nodiscard]] CGFloat shortcut_tab(const std::vector<menu_item_config>& items, NSDictionary<NSAttributedStringKey, id>* const attributes)
        {
            CGFloat label { 0.0 };
            CGFloat shortcut { 0.0 };
            for (const menu_item_config& item : items)
            {
                if (item.shortcut_text.empty())
                    continue;
                label = std::max(label, [menu_label(item.label) sizeWithAttributes:attributes].width);
                shortcut = std::max(shortcut, [menu_label(item.shortcut_text) sizeWithAttributes:attributes].width);
            }
            if (shortcut <= 0.0)
                return 0.0;
            // 라벨과 단축키 사이를 막대 메뉴의 키 등가물 칸쯤 띄운다.
            constexpr CGFloat gap { 24.0 };
            return std::ceil(label + gap + shortcut);
        }

        // `menu_config::items`를 NSMenu로 옮긴다 (tray-icon-design.md의 "메뉴", Win32 `make_tray_menu`와 같은 규칙).
        //  - 앞 항목이 있을 때만 `separator_above`가 구분선이다. `enabled == false`는 흐림, `check`·`radio`의 `checked`는
        //    체크 표시(macOS 메뉴에는 라디오 표시가 따로 없다), `children`은 하위 메뉴다.
        //  - 항목 번호(`tag`)는 하위 메뉴까지 펼친 항목 순서 + 1이다. 하위 메뉴를 여는 항목도 번호를 하나 차지한다.
        //  - 항목 아이콘 글리프와 접근 키는 쓰지 않는다 (macOS 메뉴에는 밑줄 글자가 없다).
        void append_menu_items(NSMenu* const menu, const std::vector<menu_item_config>& items, std::vector<std::u8string>& keys, LuilTrayTarget* const target)
        {
            NSFont* const font { [NSFont menuFontOfSize:0.0] };
            NSDictionary<NSAttributedStringKey, id>* const measure { @{ NSFontAttributeName : font } };
            const CGFloat tab { shortcut_tab(items, measure) };
            for (const menu_item_config& item : items)
            {
                if (item.separator_above && menu.numberOfItems > 0)
                    [menu addItem:[NSMenuItem separatorItem]];
                keys.push_back(item.key);
                NSString* const label { menu_label(item.label) };
                const bool leaf { item.children.empty() };
                NSMenuItem* const entry { [[NSMenuItem alloc] initWithTitle:label action:leaf ? @selector(menuItemChosen:) : nil keyEquivalent:@""] };
                entry.target = leaf ? target : nil;
                entry.tag = static_cast<NSInteger>(keys.size());
                entry.enabled = item.enabled;
                entry.state = item.kind != menu_item_kind::action && item.checked ? NSControlStateValueOn : NSControlStateValueOff;
                if (tab > 0.0 && item.shortcut_text.empty() == false)
                {
                    NSMutableParagraphStyle* const paragraph { [[NSMutableParagraphStyle alloc] init] };
                    paragraph.tabStops = @[ [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight location:tab options:@{}] ];
                    NSMutableDictionary<NSAttributedStringKey, id>* const attributes { [NSMutableDictionary dictionaryWithDictionary:@{ NSFontAttributeName : font, NSParagraphStyleAttributeName : paragraph }] };
                    // 꾸민 제목은 흐림을 스스로 칠하지 않을 수 있어 흐린 항목은 색을 정해 둔다. 켠 항목은 색을 비워 강조 색을 따른다.
                    if (item.enabled == false)
                        attributes[NSForegroundColorAttributeName] = [NSColor disabledControlTextColor];
                    NSString* const joined { [NSString stringWithFormat:@"%@\t%@", label, menu_label(item.shortcut_text)] };
                    entry.attributedTitle = [[NSAttributedString alloc] initWithString:joined attributes:attributes];
                }
                if (leaf == false)
                {
                    NSMenu* const submenu { [[NSMenu alloc] initWithTitle:label] };
                    submenu.autoenablesItems = NO;
                    append_menu_items(submenu, item.children, keys, target);
                    entry.submenu = submenu;
                }
                [menu addItem:entry];
            }
        }
    }

    struct tray_icon_host::impl
    {
        LuilTrayTarget* target { nil };
        NSStatusItem* item { nil };
        // 화면 배율 변화의 알림 관찰자다.
        id screen_observer { nil };
        // 단추의 모양을 관찰하는가다 (KVO는 짝을 맞춰 거둔다).
        bool observing { false };

        std::optional<tray_icon> wanted {};
        bool high_contrast { false };
        tray_sync sync {};
        tray_appearance appearance {};
        // 마지막 호출 묶음의 첫 실패다. 다음 호출 묶음까지 남는다.
        std::u8string error {};
        win32::tray_report reported {};

        // 메뉴가 떠 있는 동안이다. 맞추기는 메뉴가 닫힌 뒤로 미룬다.
        NSMenu* open_menu { nil };
        bool pending { false };
        bool applying { false };
        // 연 순간의 메뉴 설정 사본과 항목 번호 → 키다. 다음 메뉴를 열 때까지 쥔다 (선택이 닫힌 뒤에 와도 읽는다).
        std::optional<menu_config> menu {};
        std::vector<std::u8string> keys {};
        std::optional<NSInteger> chosen {};

        std::function<void(input_action)> dispatch {};
        std::function<void(const win32::tray_report&)> report {};
        std::function<void(const std::u8string&)> log {};

        [[nodiscard]] tray_appearance read_appearance() const
        {
            return tray_appearance {
                .high_contrast = high_contrast
                , .light_taskbar = light_menu_bar(item)
                , .icon_size = icon_pixels()
            };
        }

        void note(const std::u8string_view message)
        {
            if (error.empty())
                error = message;
        }

        void log_error(const std::u8string& message) const
        {
            if (log != nullptr)
                log(message);
        }

        void deliver(input_action action) const
        {
            if (dispatch != nullptr)
                dispatch(std::move(action));
        }

        void observe(const bool on)
        {
            if (on == observing || item == nil || item.button == nil)
                return;
            if (on)
                [item.button addObserver:target forKeyPath:@"effectiveAppearance" options:0 context:&appearance_context];
            else
                [item.button removeObserver:target forKeyPath:@"effectiveAppearance" context:&appearance_context];
            observing = on;
        }

        [[nodiscard]] tray_call_outcome create()
        {
            NSStatusItem* const created { [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength] };
            if (created == nil || created.button == nil)
            {
                if (created != nil)
                    [[NSStatusBar systemStatusBar] removeStatusItem:created];
                note(u8"Cannot create the status bar item for the tray icon.");
                return tray_call_outcome::failed;
            }
            item = created;
            NSStatusBarButton* const button { item.button };
            button.target = target;
            button.action = @selector(buttonPressed:);
            [button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
            button.imagePosition = NSImageOnly;
            button.imageScaling = NSImageScaleProportionallyDown;
            observe(true);
            return tray_call_outcome::succeeded;
        }

        void destroy()
        {
            if (item == nil)
                return;
            observe(false);
            item.button.target = nil;
            [[NSStatusBar systemStatusBar] removeStatusItem:item];
            item = nil;
        }

        [[nodiscard]] tray_call_outcome execute(const tray_call& call)
        {
            if (call.kind == tray_call_kind::remove)
            {
                destroy();
                return tray_call_outcome::succeeded;
            }
            const bool adding { call.kind == tray_call_kind::add };
            if (adding && item == nil)
                if (const tray_call_outcome outcome { create() }; outcome != tray_call_outcome::succeeded)
                    return outcome;
            if (item == nil)
                return tray_call_outcome::failed;
            NSStatusBarButton* const button { item.button };

            bool applied { false };
            if (call.icon)
            {
                NSImage* image { nil };
                if (call.image.valid())
                {
                    std::u8string reason {};
                    image = make_status_image(call.image, call.icon_size, reason);
                    if (image == nil)
                        note(reason);
                }
                // 빈 그림은 앱 아이콘이다. 더하는 아이콘에는 그림이 있어야 하므로 앱의 그림을 옮기지 못해도 앱 아이콘으로 세운다.
                if (image == nil && (call.image.valid() == false || adding))
                {
                    image = application_icon();
                    if (image == nil && call.image.valid() == false)
                        note(u8"Cannot load the application icon for the tray.");
                }
                if (image != nil)
                {
                    button.image = image;
                    applied = true;
                }
            }
            if (call.tooltip)
            {
                NSString* text { call.text.empty() ? nil : make_ns_string(call.text) };
                if (call.text.empty() == false && text == nil)
                    note(u8"The tray tooltip is not valid UTF-8, so the application name is shown instead.");
                if (text == nil || text.length == 0)
                    text = application_name();
                button.toolTip = text;
                button.accessibilityLabel = text;
                applied = true;
            }
            // 그림만 바꾸려다 변환이 실패했다. 걸린 그림을 그대로 둔다.
            if (adding == false && applied == false)
                return tray_call_outcome::failed;
            return tray_call_outcome::succeeded;
        }

        void run(std::optional<tray_call> call)
        {
            while (call.has_value())
            {
                const tray_call_outcome outcome { execute(*call) };
                call = sync.completed(*call, outcome);
            }
        }

        void apply()
        {
            if (open_menu != nil)
            {
                pending = true;
                return;
            }
            if (applying)
                return;
            applying = true;
            pending = false;
            appearance = read_appearance();
            if (std::optional<tray_call> call { sync.update(wanted, appearance) }; call.has_value())
            {
                error.clear();
                run(std::move(call));
                // 더하기로 단추가 서면 메뉴 막대의 모양을 처음으로 읽는다. 앱의 모양과 다르면 그림을 다시 고른다.
                if (const tray_appearance next { read_appearance() }; next != appearance)
                {
                    appearance = next;
                    run(sync.update(wanted, appearance));
                }
            }
            applying = false;

            win32::tray_report next { .status = tray_status_of(sync.state()) };
            if (next.status == win32::tray_status::failed && error.empty())
                error = u8"Cannot show the tray icon.";
            if (error.empty() == false && sync.state() != tray_sync_state::off)
            {
                next.status = win32::tray_status::failed;
                next.error = error;
            }
            if (next == reported)
                return;
            reported = next;
            if (report != nullptr)
                report(reported);
        }

        // 메뉴 막대의 모양이나 화면 배율이 바뀌었다. 마지막 요청으로 다시 맞춘다.
        void refresh()
        {
            if (read_appearance() != appearance)
                apply();
        }

        void clicked(const tray_click& click)
        {
            if (wanted.has_value() == false || open_menu != nil)
                return;
            const tray_icon_event event { tray_event_of_click(click) };
            if (event.action == tray_icon_action::context && wanted->menu.has_value())
            {
                show_menu(*wanted->menu);
                return;
            }
            const std::function<input_action(const tray_icon_event&)> activate { wanted->activate };
            if (activate == nullptr)
                return;
            try
            {
                deliver(activate(event));
            }
            catch (...)
            {
                log_error(u8"The tray icon action threw an exception.");
            }
        }

        // 상태 항목 아래에 시스템 메뉴를 띄운다 (결정 1의 길 a). 고른 키는 연 순간의 설정 사본의 `select`로 낸다.
        //  - 상태 항목에 메뉴를 잠시 걸고 단추를 누른다. 메뉴는 단추를 강조한 채 그 아래에 서고, 누르기는 메뉴가 닫힐
        //    때까지 돌아오지 않는다 (메뉴 추적 루프). 다른 클릭이 메뉴를 다시 띄우지 않도록 돌아오면 곧바로 뗀다.
        void show_menu(const menu_config& config)
        {
            if (item == nil || item.button == nil)
                return;
            menu = config;
            keys.clear();
            chosen.reset();
            NSMenu* const built { [[NSMenu alloc] initWithTitle:@""] };
            built.autoenablesItems = NO;
            append_menu_items(built, config.items, keys, target);
            if (built.numberOfItems == 0)
                return;
            open_menu = built;
            item.menu = built;
            [item.button performClick:nil];
            if (item != nil)
                item.menu = nil;
            open_menu = nil;
            if (chosen.has_value())
            {
                const NSInteger tag { *chosen };
                chosen.reset();
                choose(tag);
            }
            if (pending)
                apply();
        }

        void menu_item_chosen(const NSInteger tag)
        {
            // 추적 루프 안에서 왔으면 메뉴가 닫힌 뒤에 낸다. 닫힌 뒤에 왔으면 곧바로 낸다.
            if (open_menu != nil)
            {
                chosen = tag;
                return;
            }
            choose(tag);
        }

        void choose(const NSInteger tag)
        {
            if (menu.has_value() == false || tag <= 0 || static_cast<std::size_t>(tag) > keys.size() || menu->select == nullptr)
                return;
            const menu_message_factory select { menu->select };
            const std::u8string key { keys[static_cast<std::size_t>(tag) - 1] };
            try
            {
                deliver(select(key));
            }
            catch (...)
            {
                log_error(u8"The tray menu action threw an exception.");
            }
        }
    };

    tray_icon_host::tray_icon_host()
        : impl_ { std::make_unique<impl>() }
    {
        impl_->target = [[LuilTrayTarget alloc] init];
        impl_->target.owner = impl_.get();
        impl* const owner { impl_.get() };
        // 화면이 늘거나 배율이 바뀌면 그림의 픽셀 크기를 다시 고른다. 관찰자는 소멸자가 먼저 거두므로 맨 포인터로 쥔다.
        impl_->screen_observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidChangeScreenParametersNotification
                                                                                   object:nil
                                                                                    queue:[NSOperationQueue mainQueue]
                                                                               usingBlock:^(NSNotification*) {
                                                                                   owner->refresh();
                                                                               }];
    }

    tray_icon_host::~tray_icon_host()
    {
        if (impl_->screen_observer != nil)
            [[NSNotificationCenter defaultCenter] removeObserver:impl_->screen_observer];
        impl_->screen_observer = nil;
        if (impl_->open_menu != nil)
            [impl_->open_menu cancelTrackingWithoutAnimation];
        impl_->destroy();
        impl_->target.owner = nullptr;
    }

    void tray_icon_host::set_dispatch(std::function<void(input_action)> dispatch)
    {
        impl_->dispatch = std::move(dispatch);
    }

    void tray_icon_host::set_report(std::function<void(const win32::tray_report&)> report)
    {
        impl_->report = std::move(report);
    }

    void tray_icon_host::set_error_log(std::function<void(const std::u8string&)> log)
    {
        impl_->log = std::move(log);
    }

    void tray_icon_host::update(const std::optional<tray_icon>& wanted, const bool high_contrast)
    {
        impl_->wanted = wanted;
        impl_->high_contrast = high_contrast;
        if (wanted.has_value() == false && impl_->open_menu != nil)
            [impl_->open_menu cancelTracking];
        impl_->apply();
    }

    const win32::tray_report& tray_icon_host::report() const noexcept
    {
        return impl_->reported;
    }
}

@implementation LuilTrayTarget

- (void)buttonPressed:(id)sender
{
    static_cast<void>(sender);
    luil::macos::tray_icon_host::impl* const owner { self.owner };
    if (owner == nullptr)
        return;
    NSEvent* const event { [NSApp currentEvent] };
    luil::macos::tray_click click {};
    if (event != nil && event.type == NSEventTypeLeftMouseUp)
    {
        click.button = luil::macos::tray_mouse_button::left;
        click.click_count = static_cast<int>(event.clickCount);
        click.control = (event.modifierFlags & NSEventModifierFlagControl) != 0;
    }
    else if (event != nil && event.type == NSEventTypeRightMouseUp)
        click.button = luil::macos::tray_mouse_button::right;
    const bool mouse { click.button != luil::macos::tray_mouse_button::none };
    click.point = luil::macos::screen_pixels(mouse ? [NSEvent mouseLocation] : luil::macos::button_center(owner->item.button));
    owner->clicked(click);
}

- (void)menuItemChosen:(NSMenuItem*)sender
{
    if (self.owner != nullptr && sender != nil)
        self.owner->menu_item_chosen(sender.tag);
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey, id>*)change context:(void*)context
{
    if (context != &luil::macos::appearance_context)
    {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
        return;
    }
    if (self.owner != nullptr)
        self.owner->refresh();
}

@end

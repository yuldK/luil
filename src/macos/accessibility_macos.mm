#include "macos/accessibility_macos.h"

#include "luil/ui/accessibility.h"
#include "macos/accessibility_mapping.h"
#include "macos/macos_text.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

@class LuilAccessibilityElement;

namespace luil::macos {
    namespace {
        [[nodiscard]] std::chrono::steady_clock::time_point now() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        // 요소 객체의 열쇠다. id는 tree 안에서만 유일하므로 표면 표식을 앞에 붙인다.
        [[nodiscard]] std::u8string key_of(const std::u8string& surface, const ui_element_id& id)
        {
            std::u8string key { surface };
            key.push_back(u8'\x1f');
            for (const char digit : std::to_string(static_cast<std::uint32_t>(id.kind)))
                key.push_back(static_cast<char8_t>(digit));
            key.push_back(u8':');
            key += id.owner;
            return key;
        }

        [[nodiscard]] NSString* ns_string(const std::u8string_view text)
        {
            NSString* const converted { make_ns_string(text) };
            return converted != nil ? converted : @"";
        }

        [[nodiscard]] NSString* ns_name(const std::string_view name)
        {
            if (name.empty())
                return nil;
            return [[NSString alloc] initWithBytes:name.data() length:name.size() encoding:NSUTF8StringEncoding];
        }

        // 텍스트 칸의 표시 문서다 — 확정 글, 조합 중에는 조합 글이다 (UIA·입력기와 같은 원천). 암호 칸은 내주지 않는다.
        struct text_document
        {
            std::u8string_view text {};
            std::size_t caret { 0 };
            std::size_t anchor { 0 };
            bool composing { false };
            bool multiline { false };
            bool read_only { false };
        };

        [[nodiscard]] std::optional<text_document> text_document_of(const ui_element& element)
        {
            const std::optional<text_input_snapshot> snapshot { element.text_input() };
            if (snapshot.has_value() == false || snapshot->secret)
                return std::nullopt;
            if (snapshot->composing)
                return text_document { snapshot->composition_text, snapshot->composition_caret, snapshot->composition_caret, true, snapshot->multiline, snapshot->read_only };
            return text_document { snapshot->text, snapshot->caret, snapshot->anchor, false, snapshot->multiline, snapshot->read_only };
        }

        [[nodiscard]] bool row_role(const access_role role) noexcept
        {
            return role == access_role::tree_item || role == access_role::data_item;
        }

        // 글을 받는 칸인가 (Value·Text의 자리). 콤보도 글 칸이면 글을 받는다.
        [[nodiscard]] bool text_role(const access_role role) noexcept
        {
            return role == access_role::edit || role == access_role::combo_box;
        }
    }

    struct accessibility_bridge::state
    {
        // view 안에 앉은 tree 하나다 (주 tree 또는 popup).
        struct surface_view
        {
            std::u8string surface {};
            std::shared_ptr<const ui_tree> tree {};
            ax_placement placement {};
            bool overlay { false };
            // 알림 대조의 기준선이다 (직전에 알린 발행본).
            access_snapshot announced {};
            const ui_tree* announced_tree { nullptr };
            bool announced_valid { false };
        };

        // 물음 하나가 지금 tree에서 찾은 자리다.
        struct resolved
        {
            const surface_view* surface { nullptr };
            const ui_element* element { nullptr };

            [[nodiscard]] const ui_tree& tree() const noexcept
            {
                return *surface->tree;
            }
        };

        // 초점 칸의 선택 구간이다. 옮겨 가면 SelectedTextChanged를 알린다.
        struct text_selection
        {
            std::u8string key {};
            std::u8string text {};
            std::size_t caret { 0 };
            std::size_t anchor { 0 };
        };

        __weak NSView* view { nil };
        accessibility_callbacks callbacks {};
        bool attached { true };
        std::vector<surface_view> surfaces {};
        interaction_snapshot interaction {};
        float scale { 1.0f };
        std::unordered_map<std::u8string, LuilAccessibilityElement*> elements {};
        std::u8string focus_key {};
        std::optional<text_selection> selection {};
        std::vector<std::function<void()>> pending {};
        bool flush_scheduled { false };
        std::weak_ptr<state> self {};

        [[nodiscard]] const surface_view* find_surface(const std::u8string& surface) const
        {
            const auto found { std::ranges::find(surfaces, surface, &surface_view::surface) };
            return found != surfaces.end() && found->tree != nullptr ? &*found : nullptr;
        }

        [[nodiscard]] resolved resolve(const std::u8string& surface, const ui_element_id& id) const
        {
            if (attached == false)
                return {};
            const surface_view* const found { find_surface(surface) };
            if (found == nullptr)
                return {};
            return { found, found->tree->find(id) };
        }

        [[nodiscard]] LuilAccessibilityElement* element_for(const std::u8string& surface, const ui_element_id& id);

        [[nodiscard]] LuilAccessibilityElement* existing(const std::u8string& surface, const ui_element_id& id) const
        {
            const auto found { elements.find(key_of(surface, id)) };
            return found != elements.end() ? found->second : nil;
        }

        [[nodiscard]] NSArray* elements_of(const surface_view& surface, const std::vector<const ui_element*>& found)
        {
            NSMutableArray* const result { [NSMutableArray arrayWithCapacity:found.size()] };
            for (const ui_element* const element : found)
                [result addObject:element_for(surface.surface, element->id())];
            return result;
        }

        // view의 접근 조상이다. 최상위 요소의 부모이고 알림의 표면 root다.
        [[nodiscard]] id root() const
        {
            NSView* const host { view };
            return host != nil ? NSAccessibilityUnignoredAncestor(host) : nil;
        }

        // view 좌표(점)를 luil 쪽 view 점(왼쪽 위 원점)으로 옮긴다.
        [[nodiscard]] point_f luil_point(const NSPoint local) const
        {
            NSView* const host { view };
            if (host == nil || host.isFlipped)
                return { static_cast<float>(local.x), static_cast<float>(local.y) };
            return { static_cast<float>(local.x), static_cast<float>(NSHeight(host.bounds) - local.y) };
        }

        // tree 사각형(물리 픽셀)을 화면 사각형(AppKit의 왼쪽 아래 원점)으로 옮긴다. 판 밖이면 빈 사각형이다.
        [[nodiscard]] NSRect screen_rect(const surface_view& surface, const rect_f& bounds) const
        {
            NSView* const host { view };
            const std::optional<rect_f> local { ax_view_rect(bounds, surface.placement) };
            if (host == nil || local.has_value() == false)
                return NSZeroRect;
            NSRect rect { NSMakeRect(local->x, local->y, local->width, local->height) };
            if (host.isFlipped == NO)
                rect.origin.y = NSHeight(host.bounds) - rect.origin.y - rect.size.height;
            NSWindow* const window { host.window };
            if (window == nil)
                return rect;
            return [window convertRectToScreen:[host convertRect:rect toView:nil]];
        }

        // 화면 점을 tree 점(물리 픽셀)으로 옮긴다. 판 밖이면 nullopt다.
        [[nodiscard]] std::optional<point_f> tree_point(const surface_view& surface, const NSPoint screen) const
        {
            NSView* const host { view };
            if (host == nil)
                return std::nullopt;
            NSWindow* const window { host.window };
            const NSPoint in_window { window != nil ? [window convertPointFromScreen:screen] : screen };
            return ax_tree_point(luil_point([host convertPoint:in_window fromView:nil]), surface.placement);
        }

        [[nodiscard]] id element_at(const point_f point)
        {
            // 위의 popup부터 묻는다. 판 안을 맞혔으면 그 아래는 가려진 자리다.
            for (auto surface { surfaces.rbegin() }; surface != surfaces.rend(); ++surface)
            {
                if (surface->tree == nullptr)
                    continue;
                const std::optional<point_f> local { ax_tree_point(point, surface->placement) };
                if (local.has_value() == false)
                    continue;
                const ui_element* const hit { access_element_at(*surface->tree, local->x, local->y) };
                return hit != nullptr ? element_for(surface->surface, hit->id()) : nil;
            }
            return nil;
        }

        // 보조 기술에 알릴 초점이다. 위의 popup이 먼저다 (popup의 초점은 OS focus로는 보이지 않는다).
        [[nodiscard]] std::optional<std::pair<const surface_view*, ui_element_id>> focus_target() const
        {
            for (auto surface { surfaces.rbegin() }; surface != surfaces.rend(); ++surface)
            {
                if (surface->tree == nullptr)
                    continue;
                ui_element_id focused { ax_focus_of(*surface->tree, interaction_for_surface(interaction, surface->surface)) };
                if (focused != ui_element_id {})
                    return std::pair { &*surface, std::move(focused) };
            }
            return std::nullopt;
        }

        // 보조 기술의 호출 안에서 앱을 부르지 않는다. 메인 run loop의 다음 차례에 몰아 부른다 (창 크기를 끄는 동안에도 돈다).
        void defer(std::function<void()> work)
        {
            pending.push_back(std::move(work));
            if (flush_scheduled)
                return;
            flush_scheduled = true;
            const std::weak_ptr<state> weak { self };
            CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
              if (const std::shared_ptr<state> alive { weak.lock() }; alive != nullptr)
                  alive->flush();
            });
            CFRunLoopWakeUp(CFRunLoopGetMain());
        }

        void flush()
        {
            flush_scheduled = false;
            std::vector<std::function<void()>> work { std::exchange(pending, {}) };
            for (std::function<void()>& item : work)
                if (attached)
                    item();
        }

        void dispatch(std::vector<input_action> actions)
        {
            if (actions.empty())
                return;
            defer([this, actions = std::move(actions)]() mutable {
                if (callbacks.dispatch)
                    callbacks.dispatch(std::move(actions));
            });
        }

        void edit(text_edit_request request)
        {
            defer([this, request = std::move(request)]() mutable {
                if (callbacks.text_edit)
                    callbacks.text_edit(std::move(request));
            });
        }

        void post_input(raw_input_event event)
        {
            if (callbacks.post_input)
                callbacks.post_input(std::move(event));
        }

        // 명령 하나를 지금 tree에 물어 낸다. UIA의 `perform`과 같은 거절이다.
        //  - 비활성이거나 손이 닿지 않는 자리(가둠 뒤·숨은 것)면 거절이다.
        //  - 빈 계획은 "이미 그 상태"다. 누름에는 그 뜻이 없으므로 거절이다.
        //  - 눌러서 하는 명령(누름·켬끔·고르기)은 마우스 클릭처럼 정책 훅 뒤에 나가도록 input thread로 보낸다 (`access_click_event`).
        [[nodiscard]] bool perform(const std::u8string& surface, const ui_element_id& id, const access_request& request)
        {
            const resolved found { resolve(surface, id) };
            if (found.element == nullptr)
                return false;
            if (found.element->enabled() == false || access_reachable(found.tree(), id) == false)
                return false;
            std::optional<std::vector<input_action>> planned { plan_access_request(*found.element, request) };
            if (planned.has_value() == false)
                return false;
            if (planned->empty())
                return request.command != access_command::invoke;
            const bool click { request.command == access_command::invoke || request.command == access_command::toggle || request.command == access_command::select };
            if (click && callbacks.post_input)
                post_input(access_click_event { id, surface, now(), std::move(*planned) });
            else
                dispatch(std::move(*planned));
            return true;
        }

        // 논리 초점을 청한다. 초점은 입력 상태라 input thread가 세운다 (`access_focus_event`).
        [[nodiscard]] bool request_focus(const std::u8string& surface, const ui_element_id& id)
        {
            const resolved found { resolve(surface, id) };
            if (found.element == nullptr || found.element->focusable() == false || access_reachable(found.tree(), id) == false)
                return false;
            post_input(access_focus_event { id, surface, now() });
            return true;
        }

        // 편집이 갈 대상이다. 편집 파이프라인에 이어지지 않았거나 읽기 전용·비활성·닿지 않는 칸이면 nullopt다.
        [[nodiscard]] std::optional<text_input_target> edit_target(const resolved& found) const
        {
            if (found.element == nullptr || callbacks.text_target == nullptr)
                return std::nullopt;
            const std::optional<text_input_snapshot> snapshot { found.element->text_input() };
            if (snapshot.has_value() == false || snapshot->read_only || text_role(found.element->accessibility().role) == false)
                return std::nullopt;
            if (found.element->enabled() == false || access_reachable(found.tree(), found.element->id()) == false)
                return std::nullopt;
            return callbacks.text_target(found.element->id().kind);
        }

        void post(id element, const NSAccessibilityNotificationName notification) const
        {
            if (element != nil)
                NSAccessibilityPostNotification(element, notification);
        }

        void announce(const std::u8string_view text, const ax_priority priority) const
        {
            const NSAccessibilityPriorityLevel level { priority == ax_priority::high ? NSAccessibilityPriorityHigh
                    : priority == ax_priority::low                                   ? NSAccessibilityPriorityLow
                                                                                     : NSAccessibilityPriorityMedium };
            NSDictionary* const info { @{ NSAccessibilityAnnouncementKey : ns_string(text), NSAccessibilityPriorityKey : @(level) } };
            // 앱 요소에 쏘라는 것이 AppKit의 약속이다. 앱이 없으면(test) view에 쏜다.
            id const target { NSApp != nil ? static_cast<id>(NSApp) : root() };
            if (target != nil)
                NSAccessibilityPostNotificationWithUserInfo(target, NSAccessibilityAnnouncementRequestedNotification, info);
        }

        void post_notifications(const surface_view& surface, const std::vector<ax_notification>& notifications)
        {
            for (const ax_notification& notification : notifications)
            {
                if (notification.kind == ax_notification_kind::announcement)
                {
                    announce(notification.text, notification.priority);
                    continue;
                }
                if (notification.kind == ax_notification_kind::layout_changed && notification.id == ui_element_id {})
                {
                    post(root(), NSAccessibilityLayoutChangedNotification);
                    continue;
                }
                // 보조 기술이 아직 받아 가지 않은 요소는 알릴 상대가 없다.
                LuilAccessibilityElement* const element { existing(surface.surface, notification.id) };
                if (element == nil)
                    continue;
                switch (notification.kind)
                {
                case ax_notification_kind::value_changed:
                    post(element, NSAccessibilityValueChangedNotification);
                    break;
                case ax_notification_kind::title_changed:
                    post(element, NSAccessibilityTitleChangedNotification);
                    break;
                case ax_notification_kind::selected_children_changed:
                    post(element, NSAccessibilitySelectedChildrenChangedNotification);
                    break;
                case ax_notification_kind::selected_rows_changed:
                    post(element, NSAccessibilitySelectedRowsChangedNotification);
                    break;
                case ax_notification_kind::row_expanded:
                    post(element, NSAccessibilityRowExpandedNotification);
                    break;
                case ax_notification_kind::row_collapsed:
                    post(element, NSAccessibilityRowCollapsedNotification);
                    break;
                case ax_notification_kind::layout_changed:
                    post(element, NSAccessibilityLayoutChangedNotification);
                    break;
                case ax_notification_kind::announcement:
                    break;
                }
            }
        }

        void update(accessibility_update next);
        void detach() noexcept;
    };
}

// 접근 요소 하나다. (표면, id)만 쥐고 물을 때마다 다리의 지금 tree에서 다시 찾는다.
@interface LuilAccessibilityElement : NSAccessibilityElement
- (instancetype)initWithState:(luil::macos::accessibility_bridge::state*)state surface:(const std::u8string&)surface identity:(const luil::ui_element_id&)identity;
- (void)luilDetach;
@end

@implementation LuilAccessibilityElement
{
    luil::macos::accessibility_bridge::state* _state;
    std::u8string _surface;
    luil::ui_element_id _identity;
}

- (instancetype)initWithState:(luil::macos::accessibility_bridge::state*)state surface:(const std::u8string&)surface identity:(const luil::ui_element_id&)identity
{
    self = [super init];
    if (self != nil)
    {
        _state = state;
        _surface = surface;
        _identity = identity;
    }
    return self;
}

- (void)luilDetach
{
    _state = nullptr;
}

- (luil::macos::accessibility_bridge::state::resolved)luilResolve
{
    if (_state == nullptr)
        return {};
    return _state->resolve(_surface, _identity);
}

- (luil::access_info)luilInfo
{
    const auto found { [self luilResolve] };
    return found.element != nullptr ? luil::access_info_of(*found.element) : luil::access_info {};
}

- (luil::macos::ax_role_context)luilContext:(const luil::macos::accessibility_bridge::state::resolved&)found
{
    luil::macos::ax_role_context context {};
    if (found.element == nullptr)
        return context;
    if (const std::optional<luil::text_input_snapshot> snapshot { found.element->text_input() }; snapshot.has_value())
    {
        context.text_input = true;
        context.multiline = snapshot->multiline;
    }
    if (const luil::ui_element* const parent { luil::access_parent(found.tree(), _identity) }; parent != nullptr)
        context.parent = parent->accessibility().role;
    return context;
}

- (luil::macos::ax_role_names)luilRole
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return { "AXGroup" };
    return luil::macos::ax_role_of(luil::access_info_of(*found.element), [self luilContext:found]);
}

- (BOOL)luilPerform:(luil::macos::ax_action)action
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NO;
    const luil::access_info info { luil::access_info_of(*found.element) };
    if (action == luil::macos::ax_action::confirm && luil::macos::ax_confirms_with_enter(info, [self luilContext:found]))
    {
        // 한 줄 칸의 확정은 Enter다. 키는 초점에 가므로 초점이 아니면 먼저 청한다 (둘 다 input thread의 같은 줄로 간다).
        if (found.element->enabled() == false || luil::access_reachable(found.tree(), _identity) == false)
            return NO;
        if (luil::interaction_for_surface(_state->interaction, _surface).focused != _identity && _state->request_focus(_surface, _identity) == false)
            return NO;
        luil::key_pressed_event key {};
        key.key = luil::key_code::enter;
        key.time = std::chrono::steady_clock::now();
        // popup은 키보드 초점을 받지 못해 그 view의 창이 키를 나른다 (key-surface-routing-design.md).
        key.surface = _state->surfaces.empty() ? std::u8string {} : _state->surfaces.front().surface;
        _state->post_input(std::move(key));
        return YES;
    }
    const std::optional<luil::access_request> request { luil::macos::ax_action_request(info, action, luil::access_multiple_selection(found.tree(), _identity)) };
    if (request.has_value() == false)
        return NO;
    return _state->perform(_surface, _identity, *request) ? YES : NO;
}

// 이 실행이 이 자리에 있는가. 누름은 계획까지 묻는다 (UIA가 Invoke 패턴을 내걸 때와 같다).
- (BOOL)luilAllows:(luil::macos::ax_action)action
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NO;
    const luil::access_info info { luil::access_info_of(*found.element) };
    if (action == luil::macos::ax_action::confirm)
        return luil::macos::ax_confirms_with_enter(info, [self luilContext:found]) ? YES : NO;
    const std::optional<luil::access_request> request { luil::macos::ax_action_request(info, action) };
    if (request.has_value() == false)
        return NO;
    if (request->command == luil::access_command::invoke)
        return luil::plan_access_request(*found.element, *request).has_value() ? YES : NO;
    return YES;
}

- (std::optional<luil::macos::text_document>)luilDocument
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr || luil::macos::text_role(found.element->accessibility().role) == false)
        return std::nullopt;
    return luil::macos::text_document_of(*found.element);
}

- (NSString*)luilDocumentString
{
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    return document.has_value() ? luil::macos::ns_string(document->text) : nil;
}

// --- 정체와 역할 ---

- (BOOL)isAccessibilityElement
{
    return YES;
}

- (NSAccessibilityRole)accessibilityRole
{
    return luil::macos::ns_name([self luilRole].role);
}

- (NSAccessibilitySubrole)accessibilitySubrole
{
    return luil::macos::ns_name([self luilRole].subrole);
}

- (NSString*)accessibilityRoleDescription
{
    const luil::macos::ax_role_names names { [self luilRole] };
    NSString* const role { luil::macos::ns_name(names.role) };
    NSString* const subrole { luil::macos::ns_name(names.subrole) };
    // AppKit에 없는 하위 역할(view 안의 dialog 등)은 역할만의 설명으로 물러선다.
    NSString* const described { subrole != nil ? NSAccessibilityRoleDescription(role, subrole) : nil };
    return described.length > 0 ? described : NSAccessibilityRoleDescription(role, nil);
}

// kind:owner다 — 자동화 script가 요소를 집는 손잡이다 (UIA AutomationId와 같은 꼴). popup의 요소는 그 popup 표식이 앞에 붙는다.
- (NSString*)accessibilityIdentifier
{
    std::u8string identifier {};
    if (_state != nullptr && _state->surfaces.empty() == false && _surface != _state->surfaces.front().surface)
        identifier = _surface + u8"/";
    for (const char digit : std::to_string(static_cast<std::uint32_t>(_identity.kind)))
        identifier.push_back(static_cast<char8_t>(digit));
    identifier.push_back(u8':');
    identifier += _identity.owner;
    return luil::macos::ns_string(identifier);
}

// --- 글과 값 ---

- (NSString*)accessibilityLabel
{
    const luil::access_info info { [self luilInfo] };
    const std::u8string_view label { luil::macos::ax_label_of(info) };
    return label.empty() ? nil : luil::macos::ns_string(label);
}

- (NSString*)accessibilityHelp
{
    const luil::access_info info { [self luilInfo] };
    const std::u8string_view help { luil::macos::ax_help_of(info) };
    return help.empty() ? nil : luil::macos::ns_string(help);
}

- (id)accessibilityValue
{
    const luil::macos::ax_value value { luil::macos::ax_value_of([self luilInfo]) };
    switch (value.kind)
    {
    case luil::macos::ax_value_kind::number:
        return @(value.number);
    case luil::macos::ax_value_kind::text:
        return luil::macos::ns_string(value.text);
    case luil::macos::ax_value_kind::none:
        break;
    }
    return nil;
}

// 텍스트 칸은 글 전체를 바꾸고(편집 파이프라인의 replace_all — 입력기가 확정 글을 넣는 그 길), 범위 값은 그 값으로 정한다.
- (void)setAccessibilityValue:(id)value
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return;
    const luil::access_info info { luil::access_info_of(*found.element) };
    if ([value isKindOfClass:[NSString class]] && luil::macos::text_role(info.role))
    {
        const std::optional<luil::text_input_target> target { _state->edit_target(found) };
        if (target.has_value() == false)
            return;
        luil::text_edit_request request {};
        request.target = *target;
        request.command = luil::text::text_edit_command::replace_all;
        request.text = luil::macos::from_ns_string(value);
        request.offset = request.text.size();
        _state->edit(std::move(request));
        return;
    }
    if ([value isKindOfClass:[NSNumber class]])
        if (const std::optional<luil::access_request> request { luil::macos::ax_range_request(info, [value doubleValue]) }; request.has_value())
            static_cast<void>(_state->perform(_surface, _identity, *request));
}

- (id)accessibilityMinValue
{
    const luil::access_info info { [self luilInfo] };
    return info.range.has_value() ? @(static_cast<double>(info.range->minimum)) : nil;
}

- (id)accessibilityMaxValue
{
    const luil::access_info info { [self luilInfo] };
    return info.range.has_value() ? @(static_cast<double>(info.range->maximum)) : nil;
}

- (NSAccessibilityOrientation)accessibilityOrientation
{
    const std::optional<luil::access_orientation> orientation { [self luilInfo].orientation };
    if (orientation.has_value() == false)
        return NSAccessibilityOrientationUnknown;
    return *orientation == luil::access_orientation::horizontal ? NSAccessibilityOrientationHorizontal : NSAccessibilityOrientationVertical;
}

// --- 상태 ---

- (BOOL)isAccessibilityEnabled
{
    const auto found { [self luilResolve] };
    return found.element != nullptr && found.element->enabled() ? YES : NO;
}

- (BOOL)isAccessibilityFocused
{
    if (_state == nullptr)
        return NO;
    const auto target { _state->focus_target() };
    return target.has_value() && target->first->surface == _surface && target->second == _identity ? YES : NO;
}

- (void)setAccessibilityFocused:(BOOL)focused
{
    // 초점을 놓으라는 요청은 받을 자리가 없다 — 초점은 다른 곳으로 옮겨서 놓는다.
    if (focused && _state != nullptr)
        static_cast<void>(_state->request_focus(_surface, _identity));
}

- (BOOL)isAccessibilitySelected
{
    return [self luilInfo].selected == true ? YES : NO;
}

- (void)setAccessibilitySelected:(BOOL)selected
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return;
    const std::optional<luil::access_request> request {
        luil::macos::ax_selection_request(luil::access_info_of(*found.element), selected == YES, luil::access_multiple_selection(found.tree(), _identity))
    };
    if (request.has_value())
        static_cast<void>(_state->perform(_surface, _identity, *request));
}

- (BOOL)isAccessibilityExpanded
{
    return [self luilInfo].expanded == true ? YES : NO;
}

- (void)setAccessibilityExpanded:(BOOL)expanded
{
    if (const std::optional<luil::access_request> request { luil::macos::ax_disclosure_request([self luilInfo], expanded == YES) }; request.has_value())
        static_cast<void>(_state->perform(_surface, _identity, *request));
}

- (BOOL)isAccessibilityDisclosed
{
    return [self isAccessibilityExpanded];
}

- (void)setAccessibilityDisclosed:(BOOL)disclosed
{
    [self setAccessibilityExpanded:disclosed];
}

// 계층의 깊이다 (뿌리 줄이 0). 감싼 계층 항목을 센다.
- (NSInteger)accessibilityDisclosureLevel
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return 0;
    NSInteger level { 0 };
    luil::ui_element_id walk { _identity };
    while (const luil::ui_element* const parent { luil::access_parent(found.tree(), walk) })
    {
        if (parent->accessibility().role == luil::access_role::tree_item)
            ++level;
        walk = parent->id();
    }
    return level;
}

// --- 자리 ---

- (NSRect)accessibilityFrame
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NSZeroRect;
    // 조상의 잘라내기를 거친 사각형이다 — 스크롤로 잘린 행이 화면 밖 사각형을 내면 안 된다.
    const std::optional<luil::rect_f> visible { found.tree().visible_bounds(*found.element) };
    return visible.has_value() ? _state->screen_rect(*found.surface, *visible) : NSZeroRect;
}

- (id)accessibilityParent
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return nil;
    if (const luil::ui_element* const parent { luil::access_parent(found.tree(), _identity) }; parent != nullptr)
        return _state->element_for(_surface, parent->id());
    return _state->root();
}

- (NSArray*)accessibilityChildren
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return @[];
    return _state->elements_of(*found.surface, found.tree().access_children_of(*found.element));
}

- (NSArray*)accessibilitySelectedChildren
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr || luil::access_selection_container(luil::access_info_of(*found.element)) == false)
        return nil;
    return _state->elements_of(*found.surface, luil::access_selected_items(*found.element));
}

- (NSArray*)accessibilityTabs
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return nil;
    std::vector<const luil::ui_element*> tabs {};
    for (const luil::ui_element* const child : found.tree().access_children_of(*found.element))
        if (child->accessibility().role == luil::access_role::tab)
            tabs.push_back(child);
    return _state->elements_of(*found.surface, tabs);
}

// 표·계층의 행이다. 머리글·본문 같은 묶음 아래에 선 행도 센다 (행 안으로는 들어가지 않는다).
- (NSArray*)accessibilityRows
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return nil;
    std::vector<const luil::ui_element*> rows {};
    std::vector<const luil::ui_element*> stack { found.element };
    while (stack.empty() == false)
    {
        const luil::ui_element* const current { stack.back() };
        stack.pop_back();
        const std::vector<const luil::ui_element*>& children { found.tree().access_children_of(*current) };
        // 발행 순서를 지키도록 뒤에서부터 쌓는다.
        for (auto child { children.rbegin() }; child != children.rend(); ++child)
            stack.push_back(*child);
        if (current != found.element && luil::macos::row_role(current->accessibility().role))
        {
            rows.push_back(current);
            // 행의 자식은 행이 아니다 — 쌓은 것을 되돌린다.
            stack.resize(stack.size() - children.size());
        }
    }
    return _state->elements_of(*found.surface, rows);
}

- (NSArray*)accessibilitySelectedRows
{
    return [self accessibilitySelectedChildren];
}

- (id)accessibilityWindow
{
    NSView* const host { _state != nullptr ? static_cast<NSView*>(_state->view) : nil };
    return host.window;
}

- (id)accessibilityTopLevelUIElement
{
    return [self accessibilityWindow];
}

- (id)accessibilityHitTest:(NSPoint)point
{
    static_cast<void>(point);
    return self;
}

// --- 실행 ---

- (BOOL)accessibilityPerformPress
{
    return [self luilPerform:luil::macos::ax_action::press];
}

- (BOOL)accessibilityPerformIncrement
{
    return [self luilPerform:luil::macos::ax_action::increment];
}

- (BOOL)accessibilityPerformDecrement
{
    return [self luilPerform:luil::macos::ax_action::decrement];
}

- (BOOL)accessibilityPerformConfirm
{
    return [self luilPerform:luil::macos::ax_action::confirm];
}

- (BOOL)accessibilityPerformCancel
{
    return [self luilPerform:luil::macos::ax_action::cancel];
}

- (BOOL)accessibilityPerformShowMenu
{
    return [self luilPerform:luil::macos::ax_action::show_menu];
}

// --- 텍스트 칸 ---
// 문서는 요소가 내주는 표시 문서다 (확정 글, 조합 중에는 조합 글). offset은 UTF-8 byte로 들고 경계에서 UTF-16으로 옮긴다.

- (NSInteger)accessibilityNumberOfCharacters
{
    NSString* const text { [self luilDocumentString] };
    return text != nil ? static_cast<NSInteger>(text.length) : 0;
}

- (NSRange)accessibilitySelectedTextRange
{
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (document.has_value() == false)
        return NSMakeRange(0, 0);
    const std::size_t begin { luil::macos::ax_utf16_offset(document->text, std::min(document->caret, document->anchor)) };
    const std::size_t end { luil::macos::ax_utf16_offset(document->text, std::max(document->caret, document->anchor)) };
    return NSMakeRange(begin, end - begin);
}

// 선택은 caret 두 번 놓기다 — anchor 자리에 놓고 끝까지 늘린다 (드래그와 같은 편집 명령).
- (void)setAccessibilitySelectedTextRange:(NSRange)range
{
    const auto found { [self luilResolve] };
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (found.element == nullptr || document.has_value() == false || document->composing)
        return;
    const std::optional<luil::text_input_target> target { _state->edit_target(found) };
    if (target.has_value() == false)
        return;
    const std::size_t begin { luil::macos::ax_utf8_offset(document->text, range.location) };
    const std::size_t end { luil::macos::ax_utf8_offset(document->text, range.location + range.length) };
    luil::text_edit_request place {};
    place.target = *target;
    place.command = luil::text::text_edit_command::place_caret;
    place.offset = begin;
    _state->edit(place);
    if (end == begin)
        return;
    place.offset = end;
    place.extend = true;
    _state->edit(std::move(place));
}

- (NSString*)accessibilitySelectedText
{
    NSString* const text { [self luilDocumentString] };
    if (text == nil)
        return nil;
    const NSRange range { [self accessibilitySelectedTextRange] };
    return NSMaxRange(range) <= text.length ? [text substringWithRange:range] : @"";
}

// 고른 글을 바꾼다 (선택을 대체하는 넣기).
- (void)setAccessibilitySelectedText:(NSString*)text
{
    const auto found { [self luilResolve] };
    const std::optional<luil::text_input_target> target { _state != nullptr ? _state->edit_target(found) : std::nullopt };
    if (target.has_value() == false)
        return;
    luil::text_edit_request request {};
    request.target = *target;
    request.command = luil::text::text_edit_command::insert;
    request.text = luil::macos::from_ns_string(text);
    _state->edit(std::move(request));
}

- (NSRange)accessibilityVisibleCharacterRange
{
    NSString* const text { [self luilDocumentString] };
    return NSMakeRange(0, text != nil ? text.length : 0);
}

- (NSInteger)accessibilityInsertionPointLineNumber
{
    const auto found { [self luilResolve] };
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (found.element == nullptr || document.has_value() == false)
        return 0;
    return static_cast<NSInteger>(luil::macos::ax_line_of(found.element->text_line_starts(), document->caret));
}

- (NSString*)accessibilityStringForRange:(NSRange)range
{
    NSString* const text { [self luilDocumentString] };
    if (text == nil || NSMaxRange(range) > text.length)
        return nil;
    return [text substringWithRange:range];
}

- (NSAttributedString*)accessibilityAttributedStringForRange:(NSRange)range
{
    NSString* const text { [self accessibilityStringForRange:range] };
    return text != nil ? [[NSAttributedString alloc] initWithString:text] : nil;
}

- (NSInteger)accessibilityLineForIndex:(NSInteger)index
{
    const auto found { [self luilResolve] };
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (found.element == nullptr || document.has_value() == false || index < 0)
        return 0;
    const std::size_t offset { luil::macos::ax_utf8_offset(document->text, static_cast<std::size_t>(index)) };
    return static_cast<NSInteger>(luil::macos::ax_line_of(found.element->text_line_starts(), offset));
}

- (NSRange)accessibilityRangeForLine:(NSInteger)line
{
    const auto found { [self luilResolve] };
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (found.element == nullptr || document.has_value() == false || line < 0)
        return NSMakeRange(0, 0);
    // 줄 목록이 비면 문서 하나가 한 줄이다 (한 줄 칸).
    const std::vector<std::size_t> starts { found.element->text_line_starts() };
    const std::size_t index { static_cast<std::size_t>(line) };
    if (starts.empty() || index >= starts.size())
    {
        const std::size_t length { luil::macos::ax_utf16_length(document->text) };
        return starts.empty() && index == 0 ? NSMakeRange(0, length) : NSMakeRange(length, 0);
    }
    const std::size_t begin { luil::macos::ax_utf16_offset(document->text, starts[index]) };
    const std::size_t end { luil::macos::ax_utf16_offset(document->text, index + 1 < starts.size() ? starts[index + 1] : document->text.size()) };
    return NSMakeRange(begin, end - begin);
}

- (NSRange)accessibilityRangeForIndex:(NSInteger)index
{
    NSString* const text { [self luilDocumentString] };
    if (text == nil || index < 0 || static_cast<NSUInteger>(index) >= text.length)
        return NSMakeRange(index < 0 ? 0 : static_cast<NSUInteger>(index), 0);
    return [text rangeOfComposedCharacterSequenceAtIndex:static_cast<NSUInteger>(index)];
}

- (NSRange)accessibilityStyleRangeForIndex:(NSInteger)index
{
    static_cast<void>(index);
    return [self accessibilityVisibleCharacterRange];
}

// 구간이 화면에서 차지하는 자리다. 측정은 입력기의 후보 창 자리와 같은 함수다 (`text_span_bounds`).
- (NSRect)accessibilityFrameForRange:(NSRange)range
{
    const auto found { [self luilResolve] };
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (found.element == nullptr || document.has_value() == false)
        return NSZeroRect;
    const std::size_t begin { luil::macos::ax_utf8_offset(document->text, range.location) };
    const std::size_t end { luil::macos::ax_utf8_offset(document->text, range.location + range.length) };
    const std::optional<luil::rect_f> span { found.element->text_span_bounds(luil::text_span_query { document->text, document->caret, begin, end }, luil::default_text_measurer()) };
    return span.has_value() ? _state->screen_rect(*found.surface, *span) : NSZeroRect;
}

- (NSRange)accessibilityRangeForPosition:(NSPoint)point
{
    const auto found { [self luilResolve] };
    const std::optional<luil::macos::text_document> document { [self luilDocument] };
    if (found.element == nullptr || document.has_value() == false)
        return NSMakeRange(0, 0);
    const std::optional<luil::point_f> local { _state->tree_point(*found.surface, point) };
    if (local.has_value() == false)
        return NSMakeRange(0, 0);
    const std::optional<std::size_t> offset { found.element->offset_at_point(local->x, local->y, luil::default_text_measurer()) };
    if (offset.has_value() == false)
        return NSMakeRange(0, 0);
    return [self accessibilityRangeForIndex:static_cast<NSInteger>(luil::macos::ax_utf16_offset(document->text, *offset))];
}

// --- 내걸기 ---
// 값이 있는 것만 내건다 — "없는 것은 두지 않는다" (UIA가 패턴을 고르는 것과 같은 자리다).
- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return [super isAccessibilitySelectorAllowed:selector];
    const luil::access_info info { luil::access_info_of(*found.element) };
    if (selector == @selector(accessibilityPerformPress))
        return [self luilAllows:luil::macos::ax_action::press];
    if (selector == @selector(accessibilityPerformIncrement))
        return [self luilAllows:luil::macos::ax_action::increment];
    if (selector == @selector(accessibilityPerformDecrement))
        return [self luilAllows:luil::macos::ax_action::decrement];
    if (selector == @selector(accessibilityPerformConfirm))
        return [self luilAllows:luil::macos::ax_action::confirm];
    if (selector == @selector(accessibilityPerformCancel))
        return [self luilAllows:luil::macos::ax_action::cancel];
    if (selector == @selector(accessibilityPerformShowMenu))
        return [self luilAllows:luil::macos::ax_action::show_menu];
    if (selector == @selector(setAccessibilityValue:))
    {
        if (luil::macos::text_role(info.role) && found.element->text_input().has_value())
            return _state->edit_target(found).has_value() ? YES : NO;
        // 범위 값은 계획이 답한다 — 표시 전용 막대·진행률은 읽기 전용이다 (UIA의 `get_IsReadOnly`와 같다).
        if (info.range.has_value())
            return luil::plan_access_request(*found.element, luil::access_request { luil::access_command::set_value, info.range->value }).has_value() ? YES : NO;
        return NO;
    }
    if (selector == @selector(setAccessibilityFocused:))
        return found.element->focusable() ? YES : NO;
    if (selector == @selector(setAccessibilitySelected:) || selector == @selector(isAccessibilitySelected))
        return info.selected.has_value() ? YES : NO;
    if (selector == @selector(setAccessibilityExpanded:) || selector == @selector(isAccessibilityExpanded))
        return info.expanded.has_value() && luil::macos::row_role(info.role) == false ? YES : NO;
    if (selector == @selector(setAccessibilityDisclosed:) || selector == @selector(isAccessibilityDisclosed) || selector == @selector(accessibilityDisclosureLevel))
        return info.role == luil::access_role::tree_item ? YES : NO;
    if (selector == @selector(accessibilityMinValue) || selector == @selector(accessibilityMaxValue))
        return info.range.has_value() ? YES : NO;
    if (selector == @selector(accessibilityOrientation))
        return info.orientation.has_value() ? YES : NO;
    if (selector == @selector(accessibilitySelectedChildren))
        return luil::access_selection_container(info) ? YES : NO;
    if (selector == @selector(accessibilityTabs))
        return info.role == luil::access_role::tab_list ? YES : NO;
    if (selector == @selector(accessibilityRows) || selector == @selector(accessibilitySelectedRows))
        return info.role == luil::access_role::table || info.role == luil::access_role::tree ? YES : NO;
    if (selector == @selector(setAccessibilitySelectedTextRange:) || selector == @selector(setAccessibilitySelectedText:))
        return luil::macos::text_document_of(*found.element).has_value() && _state->edit_target(found).has_value() ? YES : NO;
    if (selector == @selector(accessibilityNumberOfCharacters) || selector == @selector(accessibilitySelectedTextRange) || selector == @selector(accessibilitySelectedText)
        || selector == @selector(accessibilityVisibleCharacterRange) || selector == @selector(accessibilityInsertionPointLineNumber) || selector == @selector(accessibilityStringForRange:)
        || selector == @selector(accessibilityAttributedStringForRange:) || selector == @selector(accessibilityLineForIndex:) || selector == @selector(accessibilityRangeForLine:)
        || selector == @selector(accessibilityRangeForIndex:) || selector == @selector(accessibilityStyleRangeForIndex:) || selector == @selector(accessibilityFrameForRange:)
        || selector == @selector(accessibilityRangeForPosition:))
        return luil::macos::text_role(info.role) && luil::macos::text_document_of(*found.element).has_value() ? YES : NO;
    return [super isAccessibilitySelectorAllowed:selector];
}

@end

namespace luil::macos {
    LuilAccessibilityElement* accessibility_bridge::state::element_for(const std::u8string& surface, const ui_element_id& id)
    {
        const std::u8string key { key_of(surface, id) };
        if (const auto found { elements.find(key) }; found != elements.end())
            return found->second;
        LuilAccessibilityElement* const element { [[LuilAccessibilityElement alloc] initWithState:this surface:surface identity:id] };
        elements.emplace(key, element);
        return element;
    }

    void accessibility_bridge::state::update(accessibility_update next)
    {
        if (attached == false)
            return;
        const float next_scale { next.scale > 0.0f ? next.scale : 1.0f };
        std::vector<surface_view> previous { std::exchange(surfaces, {}) };
        // 기준선은 표면 표식으로 잇는다. popup이 다른 자리로 옮겨 앉아도 같은 popup이다.
        const auto carry = [&previous](surface_view& entry) {
            const auto old { std::ranges::find(previous, entry.surface, &surface_view::surface) };
            if (old == previous.end())
                return false;
            entry.announced = std::move(old->announced);
            entry.announced_tree = old->announced_tree;
            entry.announced_valid = old->announced_valid;
            return true;
        };
        surface_view main_view { next.surface, std::move(next.tree), ax_placement { 0.0f, 0.0f, std::nullopt, next_scale }, false };
        static_cast<void>(carry(main_view));
        surfaces.push_back(std::move(main_view));
        // popup이 들거나 났으면 view의 자식 구성이 바뀌었다.
        bool layout_root { false };
        for (accessibility_overlay& overlay : next.overlays)
        {
            surface_view layer { overlay.surface, std::move(overlay.tree), ax_overlay_placement(overlay.bounds, overlay.scroll_y, next_scale), true };
            if (carry(layer) == false)
                layout_root = true;
            surfaces.push_back(std::move(layer));
        }
        for (const surface_view& old : previous)
            if (old.overlay && std::ranges::find(surfaces, old.surface, &surface_view::surface) == surfaces.end())
                layout_root = true;
        interaction = std::move(next.interaction);
        scale = next_scale;

        // 지금 tree에 없는 요소 객체를 놓는다. VoiceOver가 쥔 자리를 버리라고 알린다.
        for (auto entry { elements.begin() }; entry != elements.end();)
        {
            LuilAccessibilityElement* const element { entry->second };
            if ([element luilResolve].element != nullptr)
            {
                ++entry;
                continue;
            }
            NSAccessibilityPostNotification(element, NSAccessibilityUIElementDestroyedNotification);
            [element luilDetach];
            entry = elements.erase(entry);
        }

        // 발행본이 바뀐 tree마다 직전 것과 견준다. 같은 발행본의 재그리기는 변화가 아니다.
        for (surface_view& surface : surfaces)
        {
            if (surface.tree == nullptr)
            {
                surface.announced_valid = false;
                surface.announced_tree = nullptr;
                continue;
            }
            if (surface.announced_tree == surface.tree.get())
                continue;
            access_snapshot current { make_access_snapshot(*surface.tree) };
            if (surface.announced_valid)
                post_notifications(surface, ax_notifications(surface.announced, current));
            else if (surface.overlay == false && surface.announced_tree == nullptr && current.entries.empty() == false)
                layout_root = true;
            surface.announced = std::move(current);
            surface.announced_tree = surface.tree.get();
            surface.announced_valid = true;
        }
        if (layout_root)
            post(root(), NSAccessibilityLayoutChangedNotification);

        // 초점이 옮겨 갔으면 그 요소가 알린다. 같은 요소의 재게시는 알리지 않는다.
        const std::optional<std::pair<const surface_view*, ui_element_id>> target { focus_target() };
        const std::u8string key { target.has_value() ? key_of(target->first->surface, target->second) : std::u8string {} };
        LuilAccessibilityElement* const focused { target.has_value() ? element_for(target->first->surface, target->second) : nil };
        if (key != focus_key)
        {
            focus_key = key;
            post(focused, NSAccessibilityFocusedUIElementChangedNotification);
        }

        // 초점 칸의 caret·선택이 옮겨 갔으면 알린다 (글이 바뀐 것은 값 변화가 따로 알린다).
        std::optional<text_selection> now_selection {};
        if (target.has_value())
            if (const ui_element* const element { target->first->tree->find(target->second) }; element != nullptr)
                if (const std::optional<text_document> document { text_document_of(*element) }; document.has_value())
                    now_selection = text_selection { key, std::u8string { document->text }, document->caret, document->anchor };
        if (now_selection.has_value() && selection.has_value() && selection->key == now_selection->key
            && (selection->caret != now_selection->caret || selection->anchor != now_selection->anchor || selection->text != now_selection->text))
            post(focused, NSAccessibilitySelectedTextChangedNotification);
        selection = std::move(now_selection);
    }

    void accessibility_bridge::state::detach() noexcept
    {
        attached = false;
        for (auto& [key, element] : elements)
            [element luilDetach];
        elements.clear();
        pending.clear();
        surfaces.clear();
        callbacks = {};
        focus_key.clear();
        selection.reset();
    }

    accessibility_bridge::accessibility_bridge(NSView* const view, accessibility_callbacks callbacks)
        : state_ { std::make_shared<state>() }
    {
        state_->view = view;
        state_->callbacks = std::move(callbacks);
        state_->self = state_;
    }

    accessibility_bridge::~accessibility_bridge()
    {
        detach();
    }

    void accessibility_bridge::update(accessibility_update update)
    {
        state_->update(std::move(update));
    }

    void accessibility_bridge::detach() noexcept
    {
        state_->detach();
    }

    NSArray* accessibility_bridge::children()
    {
        NSMutableArray* const result { [NSMutableArray array] };
        if (state_->attached == false)
            return result;
        for (const state::surface_view& surface : state_->surfaces)
            if (surface.tree != nullptr)
                [result addObjectsFromArray:state_->elements_of(surface, surface.tree->access_top_level())];
        return result;
    }

    id accessibility_bridge::hit_test(const NSPoint screen_point)
    {
        NSView* const view { state_->view };
        if (state_->attached == false || view == nil)
            return nil;
        NSWindow* const window { view.window };
        const NSPoint in_window { window != nil ? [window convertPointFromScreen:screen_point] : screen_point };
        return element_at([view convertPoint:in_window fromView:nil]);
    }

    id accessibility_bridge::element_at(const NSPoint view_point)
    {
        if (state_->attached == false)
            return nil;
        return state_->element_at(state_->luil_point(view_point));
    }

    id accessibility_bridge::focused_element()
    {
        if (state_->attached == false)
            return nil;
        const auto target { state_->focus_target() };
        return target.has_value() ? state_->element_for(target->first->surface, target->second) : nil;
    }
}

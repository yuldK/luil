#include "ios/accessibility_ios.h"

#include "luil/ui/accessibility.h"
#include "macos/accessibility_mapping.h"
#include "macos/macos_text.h"

#import <UIKit/UIKit.h>

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

namespace luil::ios {
    namespace {
        using macos::ax_action;
        using macos::ax_notification;
        using macos::ax_notification_kind;
        using macos::ax_placement;
        using macos::ax_priority;

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
            NSString* const converted { macos::make_ns_string(text) };
            return converted != nil ? converted : @"";
        }

        [[nodiscard]] NSString* ns_text_or_nil(const std::u8string_view text)
        {
            return text.empty() ? nil : ns_string(text);
        }

        // 글을 받는 칸인가. 콤보도 글 칸이면 글을 받는다.
        [[nodiscard]] bool text_role(const access_role role) noexcept
        {
            return role == access_role::edit || role == access_role::combo_box;
        }

        // 자식이 있어도 잎으로 서는 역할이다 — 안쪽은 그 내용이라 제 label로 모은다 (UITableViewCell·UIButton과 같다).
        [[nodiscard]] bool leaf_role(const access_role role) noexcept
        {
            switch (role)
            {
            case access_role::button:
            case access_role::link:
            case access_role::check_box:
            case access_role::radio_button:
            case access_role::toggle_switch:
            case access_role::slider:
            case access_role::scroll_bar:
            case access_role::progress:
            case access_role::handle:
            case access_role::image:
            case access_role::edit:
            case access_role::combo_box:
            case access_role::list_item:
            case access_role::tab:
            case access_role::menu_item:
            case access_role::static_text:
            case access_role::tree_item:
            case access_role::data_item:
            case access_role::header_item:
            case access_role::separator:
                return true;
            default:
                return false;
            }
        }

        // 묶음일 뿐인 역할이다. 자식이 없으면 이름이 있을 때만 읽는다.
        [[nodiscard]] bool structure_role(const access_role role) noexcept
        {
            return leaf_role(role) == false;
        }

        // 묶음(UIAccessibilityContainer)으로 서는가. 잎 역할이라도 안쪽에 행이 있으면(펼친 계층의 줄) 그 행들을 보여야 한다.
        [[nodiscard]] bool container_of(const ui_tree& tree, const ui_element& element)
        {
            const std::vector<const ui_element*>& children { tree.access_children_of(element) };
            if (children.empty())
                return false;
            if (leaf_role(element.accessibility().role) == false)
                return true;
            return std::ranges::any_of(children, [](const ui_element* const child) { return macos::ax_row_role(child->accessibility().role); });
        }

        // 자식을 제 내용으로 삼킨 잎인가.
        [[nodiscard]] bool absorbs(const ui_tree& tree, const ui_element& element)
        {
            return tree.access_children_of(element).empty() == false && container_of(tree, element) == false;
        }

        // 그 id가 VoiceOver에 서는 자리다. 잎 안쪽이면 가장 바깥의 삼킨 잎이다.
        [[nodiscard]] ui_element_id exposed_id(const ui_tree& tree, const ui_element_id& id)
        {
            ui_element_id exposed { id };
            ui_element_id walk { id };
            while (const ui_element* const parent { access_parent(tree, walk) })
            {
                if (absorbs(tree, *parent))
                    exposed = parent->id();
                walk = parent->id();
            }
            return exposed;
        }

        // 요소 하나가 읽히는 글이다 (이름, 이름과 다르면 값). accessibility_mapping의 live region 글과 같은 규칙이다.
        void append_readable(std::u8string& text, const access_info& info)
        {
            const auto append = [&text](const std::u8string_view piece) {
                if (piece.empty())
                    return;
                if (text.empty() == false)
                    text += u8" ";
                text += piece;
            };
            append(info.name);
            if (info.value != info.name && info.secret == false)
                append(info.value);
        }

        // 삼킨 잎의 안쪽 글을 발행 순서로 잇는다. 이름 없는 행·단추가 이것을 label로 읽힌다.
        [[nodiscard]] std::u8string inner_text(const ui_tree& tree, const ui_element& element)
        {
            // 큰 행 하나가 VoiceOver를 붙잡지 않도록 센다.
            constexpr std::size_t limit { 64 };
            std::u8string text {};
            std::vector<const ui_element*> stack {};
            const std::vector<const ui_element*>& top { tree.access_children_of(element) };
            for (auto child { top.rbegin() }; child != top.rend(); ++child)
                stack.push_back(*child);
            std::size_t visited { 0 };
            while (stack.empty() == false && visited < limit)
            {
                const ui_element* const current { stack.back() };
                stack.pop_back();
                ++visited;
                append_readable(text, access_info_of(*current));
                const std::vector<const ui_element*>& children { tree.access_children_of(*current) };
                for (auto child { children.rbegin() }; child != children.rend(); ++child)
                    stack.push_back(*child);
            }
            return text;
        }

        // accessibilityLabel이다. 글(`static_text`)은 그 글이 label이다 — iOS는 글을 값이 아니라 label로 읽는다.
        [[nodiscard]] std::u8string label_of(const ui_tree& tree, const ui_element& element, const access_info& info)
        {
            if (macos::ax_reads_name_as_value(info.role))
                return info.value.empty() ? info.name : info.value;
            if (info.name.empty() && absorbs(tree, element))
                return inner_text(tree, element);
            return info.name;
        }

        // VoiceOver가 읽는 잎인가 (`isAccessibilityElement`).
        [[nodiscard]] bool reads_as_element(const ui_tree& tree, const ui_element& element)
        {
            if (container_of(tree, element))
                return false;
            const access_info info { access_info_of(element) };
            // 구분선은 읽을 것도 할 것도 없다 (UIKit도 내걸지 않는다).
            if (info.role == access_role::separator)
                return false;
            if (structure_role(info.role))
                return info.name.empty() == false;
            return true;
        }

        // 값을 걸음으로 오르내리는 칸인가 (Adjustable). 표시 전용 막대·진행률은 아니다.
        [[nodiscard]] bool adjustable(const ui_element& element, const access_info& info)
        {
            if (info.range.has_value() == false || info.role == access_role::progress)
                return false;
            if (macos::ax_range_step(*info.range) <= 0.0f)
                return false;
            return plan_access_request(element, access_request { access_command::set_value, info.range->value }).has_value();
        }

        [[nodiscard]] NSString* number_text(const double value)
        {
            NSNumberFormatter* const formatter { [[NSNumberFormatter alloc] init] };
            formatter.numberStyle = NSNumberFormatterDecimalStyle;
            formatter.maximumFractionDigits = 2;
            return [formatter stringFromNumber:@(value)];
        }

        [[nodiscard]] NSString* percent_text(const double fraction)
        {
            NSNumberFormatter* const formatter { [[NSNumberFormatter alloc] init] };
            formatter.numberStyle = NSNumberFormatterPercentStyle;
            formatter.maximumFractionDigits = 0;
            return [formatter stringFromNumber:@(std::clamp(fraction, 0.0, 1.0))];
        }

        // 범위 값의 글이다. 요소가 글을 주면 그것이고, 걸음을 알린 이산 값(별점)은 그 수, 그 밖은 범위의 백분율이다 (UISlider와 같다).
        [[nodiscard]] NSString* range_text(const access_info& info, const float value)
        {
            if (info.value.empty() == false)
                return ns_string(info.value);
            const access_range& range { *info.range };
            if (range.step > 0.0f || range.maximum <= range.minimum)
                return number_text(static_cast<double>(value));
            return percent_text(static_cast<double>(value - range.minimum) / static_cast<double>(range.maximum - range.minimum));
        }

        // accessibilityValue다. 켬·고름은 값이 아니라 Selected trait이 말한다. 글은 label이 말한다. 암호 칸은 내주지 않는다.
        [[nodiscard]] NSString* value_of(const access_info& info)
        {
            switch (info.role)
            {
            case access_role::check_box:
            case access_role::toggle_switch:
            case access_role::radio_button:
            case access_role::tab:
            case access_role::static_text:
                return nil;
            default:
                break;
            }
            if (info.secret)
                return nil;
            if (info.range.has_value())
                return range_text(info, info.range->value);
            return ns_text_or_nil(info.value);
        }

        [[nodiscard]] UIAccessibilityTraits traits_of(const ui_tree& tree, const ui_element& element, const access_info& info)
        {
            UIAccessibilityTraits traits { UIAccessibilityTraitNone };
            switch (info.role)
            {
            case access_role::button:
            case access_role::radio_button:
            case access_role::tab:
            case access_role::menu_item:
                traits |= UIAccessibilityTraitButton;
                break;
            case access_role::check_box:
            case access_role::toggle_switch:
                traits |= UIAccessibilityTraitButton;
                if (@available(iOS 17.0, *))
                    traits |= UIAccessibilityTraitToggleButton;
                break;
            // 표의 열 머리글이다. 누르면 정렬한다.
            case access_role::header_item:
                traits |= UIAccessibilityTraitButton | UIAccessibilityTraitHeader;
                break;
            case access_role::link:
                traits |= UIAccessibilityTraitLink;
                break;
            case access_role::image:
                traits |= UIAccessibilityTraitImage;
                break;
            case access_role::static_text:
                traits |= UIAccessibilityTraitStaticText;
                break;
            case access_role::progress:
                traits |= UIAccessibilityTraitUpdatesFrequently;
                break;
            // 글을 받지 않는 콤보는 팝업 단추다.
            case access_role::combo_box:
                if (element.text_input().has_value() == false)
                    traits |= UIAccessibilityTraitButton;
                break;
            // 탭 줄은 묶음 자신이 TabBar다 (UITabBar와 같다). VoiceOver가 "탭, n/전체"로 읽는다.
            case access_role::tab_list:
                traits |= UIAccessibilityTraitTabBar;
                break;
            default:
                // 이름만 든 묶음은 글로 읽힌다.
                if (structure_role(info.role) && container_of(tree, element) == false)
                    traits |= UIAccessibilityTraitStaticText;
                break;
            }
            if (adjustable(element, info))
                traits |= UIAccessibilityTraitAdjustable;
            // 일부만 켜진 체크는 켜졌다고 말하지 않는다 (UIKit에 일부 상태가 없다).
            if (info.selected == true || (info.checked == true && info.mixed == false))
                traits |= UIAccessibilityTraitSelected;
            if (info.enabled == false)
                traits |= UIAccessibilityTraitNotEnabled;
            return traits;
        }

        [[nodiscard]] UIAccessibilityContainerType container_type_of(const access_info& info)
        {
            switch (info.role)
            {
            case access_role::list:
            case access_role::tree:
            case access_role::table:
                return UIAccessibilityContainerTypeList;
            case access_role::navigation:
            case access_role::status_bar:
                return UIAccessibilityContainerTypeLandmark;
            case access_role::dialog:
            case access_role::alert:
            case access_role::radio_group:
            case access_role::tab_list:
            case access_role::menu:
            case access_role::menu_bar:
            case access_role::tool_bar:
                return UIAccessibilityContainerTypeSemanticGroup;
            // 이름을 든 묶음만 VoiceOver가 들어설 때 그 이름을 읽는다.
            default:
                return info.name.empty() ? UIAccessibilityContainerTypeNone : UIAccessibilityContainerTypeSemanticGroup;
            }
        }

        // 대화 상자 popup인가. 들고 날 때 화면이 바뀐 것으로 알린다 (ScreenChanged).
        [[nodiscard]] bool dialog_tree(const ui_tree* const tree)
        {
            if (tree == nullptr)
                return false;
            return std::ranges::any_of(tree->access_top_level(), [](const ui_element* const element) {
                const access_role role { element->accessibility().role };
                return role == access_role::dialog || role == access_role::alert;
            });
        }

        // 고르는 popup인가 (메뉴·콤보의 목록). 들면 VoiceOver 커서를 그 안으로 들인다 (UIKit의 메뉴와 같다).
        [[nodiscard]] bool choice_tree(const ui_tree* const tree)
        {
            if (tree == nullptr)
                return false;
            return std::ranges::any_of(tree->access_top_level(), [](const ui_element* const element) {
                const access_role role { element->accessibility().role };
                return role == access_role::menu || role == access_role::list || role == access_role::tree || role == access_role::table || role == access_role::radio_group;
            });
        }

        // VoiceOver 커서가 선 요소의 다시 읽을 몫이다. 바뀌면 그 요소를 다시 읽힌다.
        struct readout
        {
            std::u8string label {};
            access_info info {};

            [[nodiscard]] bool operator==(const readout&) const = default;
        };
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

        // 걸음으로 옮긴 값의 앞선 답이다. VoiceOver는 Increment가 돌아오자마자 값을 읽는데 실행은 다음 차례라, 앱이 받기 전까지 옮길
        // 값을 답한다.
        struct value_prediction
        {
            std::u8string key {};
            float before { 0.0f };
            float after { 0.0f };
            std::chrono::steady_clock::time_point at {};
        };

        __weak UIView* view { nil };
        accessibility_callbacks callbacks {};
        bool attached { true };
        std::vector<surface_view> surfaces {};
        interaction_snapshot interaction {};
        float scale { 1.0f };
        std::unordered_map<std::u8string, LuilAccessibilityElement*> elements {};
        std::u8string focus_key {};
        // VoiceOver 커서가 선 요소다 (`accessibilityElementDidBecomeFocused`).
        std::u8string voiceover_key {};
        std::u8string voiceover_surface {};
        ui_element_id voiceover_id {};
        std::optional<readout> voiceover_readout {};
        std::optional<value_prediction> prediction {};
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

        // tree 사각형(물리 픽셀)을 화면 사각형(UIKit 화면 좌표)으로 옮긴다. 판 밖이면 빈 사각형이다.
        [[nodiscard]] CGRect screen_rect(const surface_view& surface, const rect_f& bounds) const
        {
            UIView* const host { view };
            const std::optional<rect_f> local { macos::ax_view_rect(bounds, surface.placement) };
            if (host == nil || local.has_value() == false)
                return CGRectZero;
            const CGRect rect { CGRectMake(local->x, local->y, local->width, local->height) };
            if (host.window == nil)
                return rect;
            return UIAccessibilityConvertFrameToScreenCoordinates(rect, host);
        }

        // 보조 기술에 알릴 초점이다. 위의 popup이 먼저다 (popup의 초점은 OS focus로는 보이지 않는다).
        //  - 삼킨 잎의 안쪽이면 그 잎이다 — VoiceOver가 닿는 자리는 그것뿐이다.
        [[nodiscard]] std::optional<std::pair<const surface_view*, ui_element_id>> focus_target() const
        {
            for (auto surface { surfaces.rbegin() }; surface != surfaces.rend(); ++surface)
            {
                if (surface->tree == nullptr)
                    continue;
                const ui_element_id focused { macos::ax_focus_of(*surface->tree, interaction_for_surface(interaction, surface->surface)) };
                if (focused != ui_element_id {})
                    return std::pair { &*surface, exposed_id(*surface->tree, focused) };
            }
            return std::nullopt;
        }

        // 이 tree에서 VoiceOver가 처음 읽을 잎이다 (발행 순서). 없으면 nullptr다.
        [[nodiscard]] const ui_element* first_readable(const surface_view& surface) const
        {
            if (surface.tree == nullptr)
                return nullptr;
            const ui_tree& tree { *surface.tree };
            std::vector<const ui_element*> stack {};
            const std::vector<const ui_element*>& top { tree.access_top_level() };
            for (auto element { top.rbegin() }; element != top.rend(); ++element)
                stack.push_back(*element);
            while (stack.empty() == false)
            {
                const ui_element* const current { stack.back() };
                stack.pop_back();
                if (reads_as_element(tree, *current))
                    return current;
                if (container_of(tree, *current) == false)
                    continue;
                const std::vector<const ui_element*>& children { tree.access_children_of(*current) };
                for (auto child { children.rbegin() }; child != children.rend(); ++child)
                    stack.push_back(*child);
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<readout> readout_of(const std::u8string& surface, const ui_element_id& id) const
        {
            const resolved found { resolve(surface, id) };
            if (found.element == nullptr)
                return std::nullopt;
            access_info info { access_info_of(*found.element) };
            std::u8string label { label_of(found.tree(), *found.element, info) };
            return readout { std::move(label), std::move(info) };
        }

        // 보조 기술의 호출 안에서 앱을 부르지 않는다. 메인 큐의 다음 차례에 몰아 부른다.
        void defer(std::function<void()> work)
        {
            pending.push_back(std::move(work));
            if (flush_scheduled)
                return;
            flush_scheduled = true;
            const std::weak_ptr<state> weak { self };
            dispatch_async(dispatch_get_main_queue(), ^{
              if (const std::shared_ptr<state> alive { weak.lock() }; alive != nullptr)
                  alive->flush();
            });
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

        void post_input(raw_input_event event)
        {
            if (callbacks.post_input)
                callbacks.post_input(std::move(event));
        }

        // 명령 하나를 지금 tree에 물어 낸다. macOS·UIA의 `perform`과 같은 거절이다.
        //  - 비활성이거나 손이 닿지 않는 자리(가둠 뒤·숨은 것)면 거절이다.
        //  - 빈 계획은 "이미 그 상태"다. 누름에는 그 뜻이 없으므로 거절이다.
        //  - 눌러서 하는 명령(누름·켬끔·고르기)은 손가락 탭처럼 정책 훅 뒤에 나가도록 input thread로 보낸다 (`access_click_event`).
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

        // 물리기 키다. popup은 키보드 초점을 받지 못해 주 표면이 키를 나른다 (key-surface-routing-design.md).
        void post_escape()
        {
            key_pressed_event key {};
            key.key = key_code::escape;
            key.time = now();
            key.surface = surfaces.empty() ? std::u8string {} : surfaces.front().surface;
            post_input(std::move(key));
        }

        // 읽을 글이다. 높은 우선은 하던 말을 끊고, 그 밖은 줄을 선다.
        //  - 같은 차례에 화면·배치 알림을 쐈으면 VoiceOver가 그 요소를 읽느라 글을 끊으므로 잠시 뒤에 쏜다.
        void announce(const std::u8string_view text, const ax_priority priority, const bool delayed) const
        {
            NSNumber* const queued { @(priority != ax_priority::high) };
            NSAttributedString* const speech { [[NSAttributedString alloc] initWithString:ns_string(text) attributes:@{ UIAccessibilitySpeechAttributeQueueAnnouncement : queued }] };
            if (delayed == false)
            {
                UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, speech);
                return;
            }
            const std::weak_ptr<state> weak { self };
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
              if (const std::shared_ptr<state> alive { weak.lock() }; alive != nullptr && alive->attached)
                  UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, speech);
            });
        }

        // 앞선 답이 아직 살아 있는가 (앱이 받기 전이고 오래되지 않았다).
        [[nodiscard]] const value_prediction* live_prediction(const std::u8string& key, const access_info& info) const
        {
            if (prediction.has_value() == false || prediction->key != key || info.range.has_value() == false)
                return nullptr;
            if (info.range->value != prediction->before || now() - prediction->at > std::chrono::milliseconds { 500 })
                return nullptr;
            return &*prediction;
        }

        void update(accessibility_update next);
        void detach() noexcept;
    };
}

// 접근 요소 하나다. (표면, id)만 쥐고 물을 때마다 다리의 지금 tree에서 다시 찾는다.
@interface LuilAccessibilityElement : UIAccessibilityElement
- (instancetype)initWithState:(luil::ios::accessibility_bridge::state*)state
                         view:(UIView*)view
                      surface:(const std::u8string&)surface
                     identity:(const luil::ui_element_id&)identity;
- (luil::ios::accessibility_bridge::state::resolved)luilResolve;
- (void)luilDetach;
@end

@implementation LuilAccessibilityElement
{
    luil::ios::accessibility_bridge::state* _state;
    std::u8string _surface;
    luil::ui_element_id _identity;
}

- (instancetype)initWithState:(luil::ios::accessibility_bridge::state*)state
                         view:(UIView*)view
                      surface:(const std::u8string&)surface
                     identity:(const luil::ui_element_id&)identity
{
    self = [super initWithAccessibilityContainer:view];
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

- (luil::ios::accessibility_bridge::state::resolved)luilResolve
{
    if (_state == nullptr)
        return {};
    return _state->resolve(_surface, _identity);
}

- (BOOL)luilPerform:(luil::macos::ax_action)action
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NO;
    const luil::access_info info { luil::access_info_of(*found.element) };
    const std::optional<luil::access_request> request { luil::macos::ax_action_request(info, action, luil::access_multiple_selection(found.tree(), _identity)) };
    if (request.has_value() == false)
        return NO;
    return _state->perform(_surface, _identity, *request) ? YES : NO;
}

// --- 정체와 역할 ---

- (BOOL)isAccessibilityElement
{
    const auto found { [self luilResolve] };
    return found.element != nullptr && luil::ios::reads_as_element(found.tree(), *found.element) ? YES : NO;
}

- (UIAccessibilityTraits)accessibilityTraits
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return UIAccessibilityTraitNone;
    return luil::ios::traits_of(found.tree(), *found.element, luil::access_info_of(*found.element));
}

// kind:owner다 — 자동화 script(XCUITest)가 요소를 집는 손잡이다 (UIA AutomationId·macOS와 같은 꼴). popup의 요소는 그 popup 표식이 앞에 붙는다.
- (NSString*)accessibilityIdentifier
{
    std::u8string identifier {};
    if (_state != nullptr && _state->surfaces.empty() == false && _surface != _state->surfaces.front().surface)
        identifier = _surface + u8"/";
    for (const char digit : std::to_string(static_cast<std::uint32_t>(_identity.kind)))
        identifier.push_back(static_cast<char8_t>(digit));
    identifier.push_back(u8':');
    identifier += _identity.owner;
    return luil::ios::ns_string(identifier);
}

// --- 글과 값 ---

- (NSString*)accessibilityLabel
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return nil;
    return luil::ios::ns_text_or_nil(luil::ios::label_of(found.tree(), *found.element, luil::access_info_of(*found.element)));
}

- (NSString*)accessibilityHint
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return nil;
    return luil::ios::ns_text_or_nil(luil::macos::ax_help_of(luil::access_info_of(*found.element)));
}

- (NSString*)accessibilityValue
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return nil;
    const luil::access_info info { luil::access_info_of(*found.element) };
    // 걸음을 옮겼는데 앱이 아직 받지 않았으면 옮길 값이다. 요소가 글을 주는 값은 앞서 알 수 없어 앞선 답이 없다.
    if (const auto* const predicted { _state->live_prediction(luil::ios::key_of(_surface, _identity), info) }; predicted != nullptr)
        return luil::ios::range_text(info, predicted->after);
    return luil::ios::value_of(info);
}

// --- 묶음 ---

- (NSArray*)accessibilityElements
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr || luil::ios::container_of(found.tree(), *found.element) == false)
        return nil;
    return _state->elements_of(*found.surface, found.tree().access_children_of(*found.element));
}

- (NSInteger)accessibilityElementCount
{
    NSArray* const children { [self accessibilityElements] };
    return children != nil ? static_cast<NSInteger>(children.count) : 0;
}

- (id)accessibilityElementAtIndex:(NSInteger)index
{
    NSArray* const children { [self accessibilityElements] };
    if (children == nil || index < 0 || static_cast<NSUInteger>(index) >= children.count)
        return nil;
    return children[static_cast<NSUInteger>(index)];
}

- (NSInteger)indexOfAccessibilityElement:(id)element
{
    NSArray* const children { [self accessibilityElements] };
    if (children == nil)
        return NSNotFound;
    const NSUInteger index { [children indexOfObjectIdenticalTo:element] };
    return index == NSNotFound ? NSNotFound : static_cast<NSInteger>(index);
}

- (UIAccessibilityContainerType)accessibilityContainerType
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr || luil::ios::container_of(found.tree(), *found.element) == false)
        return UIAccessibilityContainerTypeNone;
    return luil::ios::container_type_of(luil::access_info_of(*found.element));
}

// 감싸는 접근 요소, 최상위면 view다. 재빌드로 부모가 바뀌어도 지금 tree를 따른다.
- (id)accessibilityContainer
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return _state != nullptr ? static_cast<id>(static_cast<UIView*>(_state->view)) : nil;
    if (const luil::ui_element* const parent { luil::access_parent(found.tree(), _identity) }; parent != nullptr)
        return _state->element_for(_surface, parent->id());
    return static_cast<UIView*>(_state->view);
}

// --- 자리 ---

- (CGRect)accessibilityFrame
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return CGRectZero;
    // 조상의 잘라내기를 거친 사각형이다 — 스크롤로 잘린 행이 화면 밖 사각형을 내면 안 된다.
    const std::optional<luil::rect_f> visible { found.tree().visible_bounds(*found.element) };
    return visible.has_value() ? _state->screen_rect(*found.surface, *visible) : CGRectZero;
}

// --- 실행 ---

// 두 번 탭이다. 상태를 든 것은 그 상태의 명령, 누르는 역할은 누름이다 (macOS AXPress와 같은 갈래).
//  - 편집 파이프라인에 이어진 글 칸은 논리 초점을 청한다 — host가 키보드를 올린다.
//  - 할 명령이 없거나 거절이면 NO다. UIKit이 그 자리에 탭을 흉내 내 보낸다 — 사람이 그 자리를 누른 것과 같다.
- (BOOL)accessibilityActivate
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NO;
    if (luil::ios::text_role(found.element->accessibility().role) && _state->edit_target(found).has_value())
        return _state->request_focus(_surface, _identity) ? YES : NO;
    return [self luilPerform:luil::macos::ax_action::press];
}

// 한 걸음 오르내린다 (Adjustable의 위·아래 쓸기). 옮길 값을 앞서 답해 VoiceOver가 새 값을 읽게 한다.
- (void)luilStep:(luil::macos::ax_action)action
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return;
    const luil::access_info info { luil::access_info_of(*found.element) };
    if (luil::ios::adjustable(*found.element, info) == false)
        return;
    const std::optional<luil::access_request> request { luil::macos::ax_action_request(info, action) };
    if (request.has_value() == false || _state->perform(_surface, _identity, *request) == false)
        return;
    if (info.value.empty())
        _state->prediction = luil::ios::accessibility_bridge::state::value_prediction { luil::ios::key_of(_surface, _identity), info.range->value, request->value, luil::ios::now() };
}

- (void)accessibilityIncrement
{
    [self luilStep:luil::macos::ax_action::increment];
}

- (void)accessibilityDecrement
{
    [self luilStep:luil::macos::ax_action::decrement];
}

// 두 손가락 Z다. 펼친 것(팝업 단추·하위 메뉴)이면 접고, popup이나 대화 상자 안이면 Esc 키다 (사람이 물리는 그 길).
//  - 행의 펼침은 물리기가 아니다 — 계층의 줄을 접는 것은 누름이다.
- (BOOL)accessibilityPerformEscape
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NO;
    const luil::access_info info { luil::access_info_of(*found.element) };
    if (luil::macos::ax_row_role(info.role) == false)
        if (const std::optional<luil::access_request> request { luil::macos::ax_action_request(info, luil::macos::ax_action::cancel) }; request.has_value())
            if (_state->perform(_surface, _identity, *request))
                return YES;
    bool escapable { found.surface->overlay };
    luil::ui_element_id walk { _identity };
    while (escapable == false)
    {
        const luil::ui_element* const parent { luil::access_parent(found.tree(), walk) };
        if (parent == nullptr)
            break;
        const luil::access_role role { parent->accessibility().role };
        escapable = role == luil::access_role::dialog || role == luil::access_role::alert;
        walk = parent->id();
    }
    if (info.role == luil::access_role::dialog || info.role == luil::access_role::alert)
        escapable = true;
    if (escapable == false)
        return NO;
    _state->post_escape();
    return YES;
}

// 세 손가락 쓸기다. 이 요소나 감싼 흘리는 창 가운데 그 축으로 흘릴 수 있는 가장 가까운 것을 한 쪽 흘린다 (UIA Scroll의 LargeIncrement).
//  - 방향은 손가락의 방향이다: 위로 쓸면 내용이 올라가 아래가 보인다 (UIScrollView와 같다).
//  - 끝에 닿은 창은 건너뛰어 바깥 창에 묻는다.
- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction
{
    const auto found { [self luilResolve] };
    if (found.element == nullptr)
        return NO;
    std::optional<bool> vertical {};
    float sign { 1.0f };
    switch (direction)
    {
    case UIAccessibilityScrollDirectionUp:
        vertical = true;
        break;
    case UIAccessibilityScrollDirectionDown:
        vertical = true;
        sign = -1.0f;
        break;
    case UIAccessibilityScrollDirectionLeft:
        vertical = false;
        break;
    case UIAccessibilityScrollDirectionRight:
        vertical = false;
        sign = -1.0f;
        break;
    // 다음·이전은 세로로 흘리는 창이면 세로, 아니면 가로다.
    case UIAccessibilityScrollDirectionNext:
        break;
    case UIAccessibilityScrollDirectionPrevious:
        sign = -1.0f;
        break;
    }
    const luil::ui_element* current { found.element };
    while (current != nullptr)
    {
        const luil::ui_element_id current_id { current->id() };
        if (const std::optional<luil::access_scroll_state> scroll { luil::access_info_of(*current).scroll }; scroll.has_value())
        {
            const bool along_vertical { vertical.value_or(scroll->vertical_percent >= 0.0f) };
            const float percent { along_vertical ? scroll->vertical_percent : scroll->horizontal_percent };
            const float view_size { along_vertical ? scroll->vertical_view_size : scroll->horizontal_view_size };
            if (percent >= 0.0f)
            {
                // 쪽은 창이 보이는 몫이다: 창이 내용의 v%를 보이면 한 쪽은 흘릴 수 있는 범위의 v / (100 - v)다 (UIA와 같은 식).
                const float page { view_size >= 100.0f ? 100.0f : std::min(100.0f, view_size * 100.0f / std::max(1.0f, 100.0f - view_size)) };
                const float target { std::clamp(percent + sign * page, 0.0f, 100.0f) };
                if (target != percent)
                {
                    luil::access_request request { luil::access_command::set_scroll_percent };
                    (along_vertical ? request.vertical_percent : request.horizontal_percent) = target;
                    if (_state->perform(_surface, current_id, request))
                    {
                        UIAccessibilityPostNotification(UIAccessibilityPageScrolledNotification, luil::ios::percent_text(static_cast<double>(target) / 100.0));
                        return YES;
                    }
                }
            }
        }
        current = luil::access_parent(found.tree(), current_id);
    }
    return NO;
}

// --- VoiceOver 커서 ---
// 커서가 서도 논리 초점은 옮기지 않는다 — 읽으며 훑는 것이 키보드 초점과 글 칸의 키보드를 끌고 다니면 안 된다 (UIA·macOS와 같다).
// 커서가 선 요소만 기억해 그 요소가 바뀌면 다시 읽힌다.

- (void)accessibilityElementDidBecomeFocused
{
    if (_state == nullptr)
        return;
    _state->voiceover_key = luil::ios::key_of(_surface, _identity);
    _state->voiceover_surface = _surface;
    _state->voiceover_id = _identity;
    _state->voiceover_readout = _state->readout_of(_surface, _identity);
}

- (void)accessibilityElementDidLoseFocus
{
    if (_state == nullptr || _state->voiceover_key != luil::ios::key_of(_surface, _identity))
        return;
    _state->voiceover_key.clear();
    _state->voiceover_surface.clear();
    _state->voiceover_id = {};
    _state->voiceover_readout.reset();
    _state->prediction.reset();
}

@end

namespace luil::ios {
    LuilAccessibilityElement* accessibility_bridge::state::element_for(const std::u8string& surface, const ui_element_id& id)
    {
        const std::u8string key { key_of(surface, id) };
        if (const auto found { elements.find(key) }; found != elements.end())
            return found->second;
        LuilAccessibilityElement* const element { [[LuilAccessibilityElement alloc] initWithState:this view:view surface:surface identity:id] };
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
        surface_view main_view { next.surface, std::move(next.tree), ax_placement { next.origin_x, next.origin_y, std::nullopt, next_scale }, false };
        static_cast<void>(carry(main_view));
        surfaces.push_back(std::move(main_view));
        // popup이 들거나 났으면 view의 요소 구성이 바뀌었다. 대화 상자면 화면이 바뀐 것이다.
        //  - 새로 든 대화 상자·고르는 popup(메뉴·목록)에는 VoiceOver 커서를 들인다. 도움말 같은 그 밖의 popup은 커서를 끌지 않는다.
        bool structure { false };
        bool screen { false };
        std::optional<std::u8string> opened {};
        for (accessibility_overlay& overlay : next.overlays)
        {
            surface_view layer { overlay.surface, std::move(overlay.tree), macos::ax_overlay_placement(overlay.bounds, overlay.scroll_y, next_scale), true };
            if (carry(layer) == false)
            {
                structure = true;
                if (dialog_tree(layer.tree.get()))
                {
                    screen = true;
                    opened = layer.surface;
                }
                else if (opened.has_value() == false && choice_tree(layer.tree.get()))
                    opened = layer.surface;
            }
            surfaces.push_back(std::move(layer));
        }
        for (const surface_view& old : previous)
            if (old.overlay && std::ranges::find(surfaces, old.surface, &surface_view::surface) == surfaces.end())
            {
                structure = true;
                screen = screen || dialog_tree(old.tree.get());
            }
        interaction = std::move(next.interaction);
        scale = next_scale;

        // 지금 tree에 없는 요소 객체를 놓는다. UIKit에는 요소가 죽었다는 알림이 없다 — 커서가 선 요소였으면 배치가 바뀐 것으로 알린다.
        for (auto entry { elements.begin() }; entry != elements.end();)
        {
            LuilAccessibilityElement* const element { entry->second };
            if ([element luilResolve].element != nullptr)
            {
                ++entry;
                continue;
            }
            if (entry->first == voiceover_key)
                structure = true;
            [element luilDetach];
            entry = elements.erase(entry);
        }

        // 발행본이 바뀐 tree마다 직전 것과 견준다. 같은 발행본의 재그리기는 변화가 아니다.
        //  - iOS가 받는 것은 구조(LayoutChanged)와 읽을 글(announcement)뿐이다. 값·이름·선택은 VoiceOver가 물을 때 지금 것을 읽는다.
        std::vector<ax_notification> announcements {};
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
            {
                for (ax_notification& notification : macos::ax_notifications(surface.announced, current))
                {
                    if (notification.kind == ax_notification_kind::layout_changed)
                        structure = true;
                    else if (notification.kind == ax_notification_kind::announcement)
                        announcements.push_back(std::move(notification));
                }
            }
            else if (surface.overlay == false && surface.announced_tree == nullptr && current.entries.empty() == false)
                structure = true;
            surface.announced = std::move(current);
            surface.announced_tree = surface.tree.get();
            surface.announced_valid = true;
        }

        // 논리 초점이 옮겨 갔는가. 같은 요소의 재게시는 옮김이 아니다.
        const std::optional<std::pair<const surface_view*, ui_element_id>> target { focus_target() };
        const std::u8string key { target.has_value() ? key_of(target->first->surface, target->second) : std::u8string {} };
        LuilAccessibilityElement* const focused { target.has_value() ? element_for(target->first->surface, target->second) : nil };
        const bool focus_moved { key != focus_key && focused != nil };
        focus_key = key;

        // VoiceOver 커서가 선 요소가 바뀌었는가.
        //  - 걸음으로 옮긴 값은 앞선 답이 이미 읽혔다. 앱이 다른 값으로 받았을 때만 그 값을 읽는다.
        //  - 진행률은 VoiceOver가 스스로 다시 묻는다 (UpdatesFrequently). 그 밖의 값·이름·상태 변화는 그 요소를 다시 읽힌다.
        bool reread { false };
        std::optional<std::u8string> spoken_value {};
        if (voiceover_key.empty() == false && voiceover_readout.has_value())
        {
            const std::optional<readout> now_readout { readout_of(voiceover_surface, voiceover_id) };
            if (now_readout.has_value() && *now_readout != *voiceover_readout)
            {
                const access_info& before { voiceover_readout->info };
                const access_info& after { now_readout->info };
                access_info before_rest { before };
                access_info after_rest { after };
                if (before_rest.range.has_value() && after_rest.range.has_value())
                    before_rest.range->value = after_rest.range->value;
                before_rest.value = after_rest.value;
                const bool only_value { now_readout->label == voiceover_readout->label && before_rest == after_rest };
                // 글 칸의 글은 입력기(host의 UITextInput)가 친 글자를 읽는다 — 칸 전체를 다시 읽히면 글자마다 겹친다.
                const bool quiet { only_value && (after.role == access_role::progress || text_role(after.role)) };
                const resolved found { resolve(voiceover_surface, voiceover_id) };
                if (quiet == false && only_value && found.element != nullptr && adjustable(*found.element, after))
                {
                    const bool predicted { prediction.has_value() && prediction->key == voiceover_key && after.range->value == prediction->after };
                    if (predicted == false)
                        spoken_value = macos::from_ns_string(range_text(after, after.range->value));
                }
                else if (quiet == false)
                    reread = true;
            }
            voiceover_readout = now_readout;
        }
        // 앱이 받았거나(값이 옮겨 갔다) 오래되었거나 커서가 떠난 앞선 답은 놓는다.
        if (prediction.has_value())
        {
            const bool expired { now() - prediction->at > std::chrono::milliseconds { 500 } || prediction->key != voiceover_key };
            const bool applied { voiceover_readout.has_value() && voiceover_readout->info.range.has_value() && voiceover_readout->info.range->value != prediction->before };
            if (expired || applied)
                prediction.reset();
        }

        // 한 차례에 VoiceOver 커서를 옮기는 알림은 하나다. 화면이 바뀐 것이 가장 크고, 다음이 든 popup, 다음이 초점의 옮김이다.
        //  - 든 popup 안에 초점이 있으면 그 요소, 없으면 그 popup의 첫 잎이다. 대화 상자가 닫혔으면 지금 초점(없으면 UIKit이 고른다)이다.
        id argument { focused };
        if (opened.has_value() && (target.has_value() == false || target->first->surface != *opened))
            if (const surface_view* const popup { find_surface(*opened) }; popup != nullptr)
                if (const ui_element* const first { first_readable(*popup) }; first != nullptr)
                    argument = element_for(popup->surface, first->id());
        bool posted { true };
        if (screen)
            UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification, argument);
        else if (opened.has_value())
            UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, argument);
        else if (focus_moved)
            UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, focused);
        else if (reread)
            UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, existing(voiceover_surface, voiceover_id));
        else if (structure)
            UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
        else
            posted = false;

        if (spoken_value.has_value() && spoken_value->empty() == false)
            announce(*spoken_value, ax_priority::high, posted);
        for (const ax_notification& notification : announcements)
            announce(notification.text, notification.priority, posted);
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
        voiceover_key.clear();
        voiceover_surface.clear();
        voiceover_id = {};
        voiceover_readout.reset();
        prediction.reset();
    }

    accessibility_bridge::accessibility_bridge(UIView* const view, accessibility_callbacks callbacks)
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

    NSArray* accessibility_bridge::elements()
    {
        NSMutableArray* const result { [NSMutableArray array] };
        if (state_->attached == false)
            return result;
        for (const state::surface_view& surface : state_->surfaces)
            if (surface.tree != nullptr)
                [result addObjectsFromArray:state_->elements_of(surface, surface.tree->access_top_level())];
        return result;
    }

    id accessibility_bridge::focused_element()
    {
        if (state_->attached == false)
            return nil;
        const auto target { state_->focus_target() };
        return target.has_value() ? state_->element_for(target->first->surface, target->second) : nil;
    }
}

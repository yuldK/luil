#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef __OBJC__
#import <UIKit/UIKit.h>
#endif

// VoiceOver(UIAccessibility)의 창구다 — macOS 다리(macos/accessibility_macos.h)의 iOS 짝이다.
//  - iOS의 표면은 하나다. 그 Skia view가 이 다리 하나를 쥐고, view의 `accessibilityElements`가 tree의 접근 요소다.
//  - popup은 같은 view에 겹쳐 그리므로, 그 tree가 같은 view 아래 따로 선 가지가 된다 (자리는 그 판이다).
//  - 요소는 element 포인터가 아니라 (표면, id)를 쥐고 물을 때마다 지금 tree에서 다시 찾는다. 같은 id는 frame을 넘어 같은
//    UIAccessibilityElement 객체다 — VoiceOver가 쥔 자리가 tree 재빌드를 넘어 이어진다.
//  - 자식이 있는 묶음은 UIAccessibilityContainer(`accessibilityElements`)이고, 누르는 것·행·글은 잎이다 (UITableViewCell처럼
//    안쪽 글을 제 label로 모은다).
//  - 실행은 macOS와 같은 원료를 낸다: 누름은 `access_click_event`, 초점은 `access_focus_event`, 물리기는 Esc 키, 그 밖은 계획된
//    액션이다. 모두 메인 thread의 다음 차례에 배분한다 (`accessibility_callbacks`).
//  - VoiceOver 커서가 옮겨 가도 논리 초점은 옮기지 않는다 (UIA·macOS의 보조 기술 커서와 같다). 논리 초점이 옮겨 가면 커서가 따라간다.
//  - UIKit을 모르는 계산(값·실행의 번역·좌표·알림 대조)은 macOS의 accessibility_mapping.h를 그대로 쓴다.
namespace luil::ios {
    // view에 겹쳐 그린 popup 하나다 (`overlay_entry`의 접근 몫).
    struct accessibility_overlay
    {
        // popup의 표면 표식이다 (`ui_popup::id`). 상호작용 발행본이 이 표식으로 거른다.
        std::u8string surface {};
        std::shared_ptr<const ui_tree> tree {};
        // 판의 자리다 (view 물리 픽셀, `overlay_entry::bounds` — 안전 영역 offset이 이미 들어 있다).
        rect_f bounds {};
        // 판 안에서 흘린 양이다 (물리 픽셀, `overlay_entry::scroll_y`).
        float scroll_y { 0.0f };
    };

    // 새 frame이나 상호작용이 게시될 때마다 넘기는 지금 상태다.
    struct accessibility_update
    {
        // 주 표면의 표식이다. iOS는 표면이 하나라 빈 것이다.
        std::u8string surface {};
        // 이 view가 그리는 tree다. 없으면(첫 frame 전) 요소가 없다.
        std::shared_ptr<const ui_tree> tree {};
        // 그 위에 겹쳐 그린 popup들이다 (그리는 순서, 뒤가 위다).
        std::vector<accessibility_overlay> overlays {};
        // 거르지 않은 상호작용 발행본이다 (`app_host::acquire_interaction`). 다리가 표면마다 거른다.
        interaction_snapshot interaction {};
        // UIKit 점 하나가 몇 물리 픽셀인가 (`UIView::contentScaleFactor`).
        float scale { 1.0f };
        // 주 tree의 (0, 0)이 view에서 앉은 자리다 (물리 픽셀, 안전 영역 inset).
        float origin_x { 0.0f };
        float origin_y { 0.0f };
    };

    // 다리가 host에 돌려주는 실행이다. 모두 메인 thread에서 부른다.
    struct accessibility_callbacks
    {
        // input thread로 보낼 사건이다 (`app_host::post_raw_input`). 누름(`access_click_event`)·초점(`access_focus_event`)·
        // 물리기 키(`key_pressed_event`)가 온다. 곧바로 부른다 — 입력 대기열이라 재진입이 없다.
        std::function<void(raw_input_event)> post_input {};
        // 계획된 액션을 통상 규칙대로 배분한다 (host의 `dispatch` — 앱 메시지·창 명령·클립보드).
        //  - 보조 기술의 호출 안에서 부르지 않는다. 다리가 메인 큐의 다음 차례로 미룬다.
        std::function<void(std::vector<input_action>)> dispatch {};
        // 이 kind의 텍스트 칸이 편집 파이프라인에 이어져 있는가 (`interaction_policy::text_target_of`). 없으면 읽기 전용이다.
        //  - 이어진 칸을 VoiceOver로 누르면(두 번 탭) 논리 초점을 청한다. 키보드와 글은 host의 UITextInput 몫이다.
        std::function<std::optional<text_input_target>(ui_element_kind)> text_target {};
        // 편집 하나를 앱으로 보낸다 (host의 `post_edit`). macOS와 짝을 맞춘 자리다.
        //  - UIKit은 접근 요소에 글을 쓰라고 하지 않는다 (VoiceOver의 편집은 first responder의 UITextInput으로 간다). 지금은 부르지 않는다.
        std::function<void(text_edit_request)> text_edit {};
    };

#ifdef __OBJC__
    // 표면 하나의 접근 다리다. 메인 thread 전용이다.
    //
    // host가 할 일:
    //  - view를 만들 때 다리를 짓고, 새 frame이나 상호작용이 게시될 때마다 `update`를 부른다 (그릴 때마다 불러도 같은 발행본은
    //    대조하지 않는다).
    //  - view의 `isAccessibilityElement`는 NO, `accessibilityElements`는 `elements`를 답한다 (UIKit이 개수·색인 질의를 그 배열로 답한다).
    //  - view가 사라지기 전에 `detach`하거나 다리를 지운다. VoiceOver가 아직 쥔 요소는 그 뒤로 비어 있는 채 낡는다.
    class accessibility_bridge
    {
    public:
        accessibility_bridge(UIView* view, accessibility_callbacks callbacks);
        accessibility_bridge(const accessibility_bridge&) = delete;
        accessibility_bridge(accessibility_bridge&&) = delete;
        accessibility_bridge& operator=(const accessibility_bridge&) = delete;
        accessibility_bridge& operator=(accessibility_bridge&&) = delete;
        ~accessibility_bridge();

        // 지금 상태를 받는다. 발행본이 바뀐 tree마다 직전 것과 견줘 알림을 쏜다.
        //  - 대화 상자 popup이 들거나 나면 ScreenChanged, 구조가 바뀌거나 그 밖의 popup이 들고 나면 LayoutChanged다.
        //  - 논리 초점이 옮겨 갔으면 그 요소를 든 LayoutChanged다 (VoiceOver 커서가 따라간다).
        //  - VoiceOver 커서가 선 요소의 이름·값·상태가 바뀌면 그 요소를 다시 읽힌다. 오류 글·live region은 announcement다.
        void update(accessibility_update update);
        // view가 죽었다. 이후 모든 질문은 빈 답이고 실행은 거절이다.
        void detach() noexcept;

        // view의 접근 요소다 — 주 tree의 최상위 접근 요소 뒤에 popup마다 그 최상위 접근 요소가 선다.
        [[nodiscard]] NSArray* elements();
        // 논리 초점의 요소다 (popup의 초점이면 그 가지의 요소, 잎 안쪽이면 그 잎). 없으면 nil이다.
        [[nodiscard]] id focused_element();

        // 다리의 안쪽 상태다 (요소 객체가 되묻는다).
        struct state;

    private:
        std::shared_ptr<state> state_ {};
    };
#endif
}

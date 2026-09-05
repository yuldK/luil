#pragma once

#include "luil/ui/ui_element.h"

#include <string>

namespace luil {
    // 웹뷰 자리표의 설정이다.
    struct webview_config
    {
        // 이 자리가 가리키는 웹뷰다 (`ui_webview::id`).
        // 비어 있으면 자리만 차지하고 아무 웹뷰도 붙지 않는다.
        std::u8string webview {};
        // 보조 기술이 읽을 이 영역의 이름이다.
        //  - **페이지 내용은 여기 없다.** 웹 콘텐츠는 창의 UIA tree에 저절로 들어와
        //    스크린 리더가 따로 읽는다 (webview-composition-design.md).
        //    이 이름은 그 영역이 무엇인지를 말할 뿐이다.
        std::u8string name {};
        // 웹뷰가 그 자리를 채우지 **못했을 때** 보이는 글이다.
        //
        // 늘 그리고, 웹뷰가 실제로 서면 그 위가 구멍으로 비워져 페이지가 대신 보인다.
        // 그래서 "웹뷰가 섰는가"를 앱에 되묻는 상태가 하나도 필요 없다 — 서면 덮이고
        // 못 서면 남는다. 런타임이 없는 컴퓨터, 만들기 실패, CPU로 물러선 창이 모두
        // 이 하나로 설명된다.
        std::u8string placeholder {};
        // 그 글의 크기다 (논리 픽셀).
        float placeholder_font_size { 13.0f };
    };

    // 웹 콘텐츠가 앉을 **자리**다.
    //
    // 픽셀을 칠하는 일은 거의 하지 않는다. 이 element가 하는 일은 tree 안에서
    // 자리를 차지하고, 그 자리가 어디인지를 `ui_tree::visible_bounds`로 답할 수 있게
    // 하는 것이다 — 웹뷰의 수명은 `ui_frame::webviews`가 지고 자리는 여기가 진다
    // (webview-composition-design.md).
    //
    // **가둠(`focus_trap`)이 아니다.** tree의 가둠은 *우리* Tab 순회를 그 subtree에
    // 가두는 것인데, 이 자리표는 자식이 없어 그것을 걸면 Tab이 여기 닿는 순간 영영
    // 빠져나가지 못한다 — 웹뷰에 들어가기도 전에. 페이지 **안에서** Tab이 새어 나오지
    // 않게 하는 것은 WebView2 쪽의 `MoveFocusRequested`가 맡고, 거기서 나오는 길이
    // `Ctrl+Tab`이다.
    class webview_element final : public ui_element
    {
    public:
        webview_element(ui_element_id id, webview_config config);

        [[nodiscard]] const webview_config& config() const noexcept;
        // 이 자리표가 **배치된 배율**이다 (`arrange_context::scale`, 0 이하는 1).
        //
        // 웹뷰의 래스터 배율이 이 값을 따른다. 표면의 배율을 따로 묻지 않는 이유:
        // `bounds()`는 이 배율로 낸 물리 픽셀이라, 배율이 바뀐 직후 아직 옛 tree를
        // 그리는 frame에서 표면의 새 배율을 붙이면 자리와 배율이 어긋난 쌍이 된다.
        // 자리를 낸 tree가 배율도 말하면 그 쌍이 어긋날 길이 없다.
        [[nodiscard]] float scale() const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        webview_config config_ {};
        float scale_ { 1.0f };
    };
} // namespace luil

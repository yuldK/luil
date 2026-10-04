#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace luil {
    // 시트가 서는 자리다 (sheet-design.md).
    enum class sheet_kind
    {
        // 아래에서 올라오는 시트다. 끌기 손잡이가 있고 반·전체 높이에 멈춘다.
        bottom,
        // 오른쪽에서 들어오는 옆 시트다 (오른쪽에서 왼쪽으로 읽는 화면에서도 오른쪽이다).
        end,
        // 왼쪽에서 들어오는 탐색 서랍이다.
        start,
    };

    // 아래 시트의 멈춤점이다.
    enum class sheet_detent
    {
        // 화면 높이의 반이다.
        half,
        // 위에 `full_top_margin`만 남기고 끝까지다.
        full,
    };

    // 시트가 닫힐 만한 계기다. 회전과 크기 변경은 계기가 아니다 — 시트는 화면의 일부라 남는다.
    enum class sheet_dismiss_reason
    {
        // modal 시트 바깥의 scrim을 눌렀다.
        scrim,
        // 뒤로 가기다. host가 아니라 앱이 `ui_frame::back`에서 이 값으로 부른다.
        back,
        // modal 시트가 떠 있는 동안 Esc를 눌렀다.
        escape,
        // 아래 시트를 아래로 끌어 놓았다.
        drag,
    };

    // 같은 표면 안에 겹쳐 그리는 시트·서랍의 설정이다.
    //
    // **열림·멈춤점·끄는 양은 앱 상태다.** 열려 있으면 앱이 매 frame tree에 담고, 닫으면 빼고, host는 받은 값으로
    // 그 frame의 자리에 배치한다. 그래서 회전·화면 분할·창 크기 변경 뒤에도 열림·멈춤점과 시트 안의 스크롤이 그대로
    // 남는다. popup(`ui_popup`)과 달리 표면이 바뀌어도 닫히지 않는다.
    struct sheet_host_config
    {
        // 같은 화면에 시트가 여럿일 때 구분하는 키다.
        std::u8string owner {};
        // 보조 기술이 시트에 들어설 때 말하는 이름이다.
        std::u8string name {};
        sheet_kind kind { sheet_kind::bottom };
        // 적응형이다. 아래 시트가 폭이 이 값(논리 픽셀) 이상인 자리에서는 오른쪽 옆 시트로 선다. 비어 있으면 바꾸지
        // 않는다. 멈춤점은 앱 상태라 옆 시트로 갔다 돌아와도 그대로다.
        std::optional<float> side_from_width {};
        // 참이면 scrim으로 뒤를 덮고 포인터를 막고, 초점을 가두고, Esc를 받는다 (modal dialog와 같다). 거짓이면
        // 뒤의 화면을 그대로 쓸 수 있는 비modal 시트다.
        bool modal { true };
        // 아래 시트의 지금 멈춤점이다 (앱 상태).
        sheet_detent detent { sheet_detent::half };
        // 아래 시트를 끄는 중인 이동량이다 (앱 상태, 논리 픽셀, 아래가 양수). `drag` 메시지를 쌓은 값이고, 놓으면
        // 앱이 0으로 되돌린다.
        float drag_offset { 0.0f };
        // 옆 시트·서랍의 폭이다. 자리가 좁으면 오른쪽(서랍은 왼쪽)에 56을 남기고 줄인다.
        float side_width { 360.0f };
        // 전체 높이에서 위에 남기는 자리다. modal이면 그 자리로 뒤가 보여 시트임을 말한다.
        float full_top_margin { 24.0f };
        // 아래 시트의 최대 폭이다. 넓은 자리에서는 가운데에 선다.
        float bottom_max_width { 640.0f };
        // scrim의 진하기다 (0~1). modal일 때만 그린다.
        float scrim_opacity { 0.32f };
        // 닫힘 계기에서 낼 메시지다. 비어 있으면 계기를 감지해도 아무것도 내지 않는다.
        std::function<input_action(sheet_dismiss_reason)> dismiss {};
        // 아래 시트를 끈 만큼이다 (논리 픽셀, 아래가 양수). 앱은 `drag_offset`에 더한다.
        std::function<input_action(float delta)> drag {};
        // 끌기를 놓았고 가장 가까운 멈춤점이 이것이다. 앱은 `detent`에 넣고 `drag_offset`을 0으로 되돌린다.
        // 닫는 쪽이 가까우면 이것 대신 `dismiss(drag)`가 나간다.
        std::function<input_action(sheet_detent)> settle {};
        // modal 시트가 뜨는 순간 초점이 설 자리와 닫힌 뒤 돌아갈 바깥 자리다 (`modal_host_config`와 같다).
        ui_element_id focus_entry {};
        ui_element_id focus_return {};
    };

    // 이 폭(논리 픽셀)에서 실제로 설 시트의 종류다 (적응형).
    [[nodiscard]] sheet_kind resolve_sheet_kind(const sheet_host_config& config, float width) noexcept;
    // 아래 시트의 멈춤점 높이다 (논리 픽셀). 자리 높이가 `height`다.
    [[nodiscard]] float sheet_detent_height(const sheet_host_config& config, sheet_detent detent, float height) noexcept;
    // 아래 시트가 지금 보이는 높이다. 멈춤점 높이에서 끄는 양을 빼고 0과 전체 높이 사이로 다듬는다.
    [[nodiscard]] float sheet_visible_height(const sheet_host_config& config, float height) noexcept;

    // 아래 시트를 놓은 자리에서 정한 결과다.
    struct sheet_settle
    {
        sheet_detent detent { sheet_detent::half };
        // 닫는 쪽(높이 0)이 가장 가깝다.
        bool dismiss { false };

        [[nodiscard]] bool operator==(const sheet_settle&) const noexcept = default;
    };

    // 놓은 자리에서 가장 가까운 멈춤점이다 — 닫힘(0)·반·전체 중에서 지금 보이는 높이와 가장 가까운 것이다.
    // 속도는 보지 않는다.
    [[nodiscard]] sheet_settle settle_sheet(const sheet_host_config& config, float height) noexcept;

    // 시트 표면의 자리다 (`slot` 안, 물리 픽셀). `scale`은 물리 / 논리 배율이다.
    [[nodiscard]] rect_f sheet_surface_bounds(const sheet_host_config& config, const rect_f& slot, float scale) noexcept;

    // 같은 표면 안에서 화면 위에 겹치는 시트·서랍이다 (아래 시트, 오른쪽 옆 시트, 왼쪽 서랍).
    //  - 자리를 통째로 받아 그 안에 선다. modal이면 자리 전체를 scrim이 덮는다.
    //  - 아래 시트는 위에 끌기 손잡이가 선다. 손잡이와 시트 몸의 끌기가 `drag`를, 놓으면 `settle`이나 `dismiss(drag)`를
    //    낸다. 시트 몸의 끌기는 안쪽 세로 목록이 맨 위(`scroll_source::at_start`)일 때만 넘겨받는다.
    //  - modal이면 초점 가둠과 dialog 역할, 아니면 pane 역할이다.
    class sheet_host_element final : public ui_element
    {
    public:
        explicit sheet_host_element(sheet_host_config config);

        // 시트의 내용이다. 표면(아래 시트는 손잡이 아래)의 자리를 통째로 받는다.
        void set_content(std::unique_ptr<ui_element> content);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 배치한 자리에서 실제로 선 종류다 (적응형이면 폭에 따라 다르다).
        [[nodiscard]] sheet_kind kind() const noexcept;
        // 배치한 시트 표면의 자리다 (물리 픽셀).
        [[nodiscard]] rect_f surface_bounds() const noexcept;

    private:
        sheet_host_config config_ {};
        ui_element* scrim_ { nullptr };
        ui_element* surface_ { nullptr };
        ui_element* handle_ { nullptr };
        ui_element* content_ { nullptr };
        sheet_kind kind_ { sheet_kind::bottom };
    };
} // namespace luil

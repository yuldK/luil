#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <string>
#include <vector>

namespace luil {
    // 경로 조각 하나다 (`breadcrumb_config::segments`).
    struct breadcrumb_segment
    {
        // 앱이 조각을 가르는 키다 (자식 id의 owner에 들어간다).
        std::u8string key {};
        std::u8string label {};
        // 아래 단계가 있다. 참이면 조각 뒤에 그것을 여는 꺾쇠 단추가 선다.
        bool has_children { false };
    };

    // 경로 조각 막대다 (breadcrumb-design.md).
    struct breadcrumb_config
    {
        // 막대를 가르는 키다. 막대·조각·꺾쇠·접은 단추 id의 owner가 이것으로 시작한다.
        std::u8string owner {};
        // 맨 위에서 지금 자리까지다. 마지막이 지금 자리다.
        std::vector<breadcrumb_segment> segments {};
        // 조각을 눌렀다 (왼쪽 누름·Enter·Space). 지금 자리(마지막 조각)는 누를 것이 없다.
        std::function<input_action(const std::u8string& key)> navigate {};
        // 꺾쇠를 눌렀다. 앱이 그 조각의 아래 단계를 메뉴로 연다 — 자리는 문맥의 누른 점이고, 앵커가 필요하면
        // `breadcrumb_chevron_id`로 그 단추를 찾는다.
        std::function<input_action(const std::u8string& key, const ui_action_context& context)> open_children {};
        // 폭이 모자라 앞쪽 조각을 접었을 때 그 "…" 단추를 눌렀다. 접힌 조각의 키다 (앞에서부터).
        std::function<input_action(const std::vector<std::u8string>& keys, const ui_action_context& context)> open_collapsed {};
        // 조각을 가운데 버튼으로 눌렀다 (새 탭에서 열기). 비어 있으면 받지 않는다.
        std::function<input_action(const std::u8string& key)> middle_click {};
        // 조각을 오른쪽 버튼으로 눌렀다 (컨텍스트 메뉴). 비어 있으면 받지 않는다.
        std::function<input_action(const std::u8string& key, const ui_action_context& context)> context_menu {};
        float font_size { 12.0f };
        // 보조 기술이 읽는 이름이다 (앱의 언어 — "경로"). 막대는 navigation 랜드마크로 선다.
        std::u8string name {};
        // 지금 자리 조각의 이름 뒤에 붙는 상태다 ("현재 위치"). 비어 있으면 붙이지 않는다.
        std::u8string current_label {};
        // 꺾쇠 단추의 tooltip이자 이름의 뒤다 ("하위 항목" → "문서 · 하위 항목").
        std::u8string children_label {};
        // 접은 단추의 tooltip이자 이름이다 ("앞 경로").
        std::u8string collapsed_label {};
    };

    // 꺾쇠·접은 단추의 id다. 메뉴의 앵커로 그 자리를 찾을 때 쓴다.
    [[nodiscard]] ui_element_id breadcrumb_segment_id(std::u8string_view owner, std::u8string_view key);
    [[nodiscard]] ui_element_id breadcrumb_chevron_id(std::u8string_view owner, std::u8string_view key);
    [[nodiscard]] ui_element_id breadcrumb_collapsed_id(std::u8string_view owner);

    // 경로 조각과 조각 사이 꺾쇠를 한 줄에 늘어놓는 막대다 (파일 관리자의 주소 막대).
    //
    // **폭이 모자라면 앞부터 접는다.** 앞쪽 조각을 "…" 단추 하나로 접고, 그 단추가 접힌 조각을 메뉴로 열게 한다
    // (`open_collapsed`). 지금 자리는 끝까지 남는다 — 그마저 들어가지 않으면 그 글의 끝을 줄인다.
    //  - 접기는 배치에서 정한다. 같은 설정이라도 폭이 바뀌면 다시 접는다.
    //  - 막대는 Tab의 한 자리다(`focus_group` 가로). ←/→가 조각·꺾쇠 사이를 돈다.
    class breadcrumb_element final : public ui_element
    {
    public:
        explicit breadcrumb_element(breadcrumb_config config);

        // 한 줄의 높이다 (논리 픽셀).
        [[nodiscard]] static float height_for(const breadcrumb_config& config) noexcept;
        // 폭 `available_width`(논리 픽셀)에서 앞에서부터 접을 조각의 수다. 배치와 같은 규칙이다.
        //  - 조각이 하나면 0이다. 지금 자리는 접지 않는다.
        [[nodiscard]] static std::size_t collapsed_count(const breadcrumb_config& config, const measure_context& context, float available_width);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 접지 않은 모든 조각을 늘어놓은 폭과 한 줄 높이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

        // 지난 배치가 접은 조각의 수다.
        [[nodiscard]] std::size_t collapsed() const noexcept;

    private:
        breadcrumb_config config_ {};
        // 조각마다의 글 element와 꺾쇠(없으면 nullptr)다 (설정 차례).
        std::vector<ui_element*> items_ {};
        std::vector<ui_element*> chevrons_ {};
        ui_element* overflow_ { nullptr };
        std::size_t collapsed_ { 0 };
    };
} // namespace luil

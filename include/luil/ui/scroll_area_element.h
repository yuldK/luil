#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <string>

namespace luil {
    // 스크롤 막대를 언제 세우는가.
    enum class scrollbar_visibility
    {
        // 흘릴 것이 있을 때만 세운다.
        // 짧은 내용에서는 칸을 통째로 내주므로 글이 넓게 선다.
        automatic,
        // 흘릴 것이 없어도 자리를 지킨다.
        // 내용이 늘고 주는 창에서 글의 폭이 흔들리지 않는다 — 흔들리면 한 줄
        // 늘어난 것만으로 문단 전체가 다시 접힌다.
        always,
        // 세우지 않는다 (휠과 키만 쓰는 창).
        //  - `scroll` factory가 없는 것과 다르다. 그쪽은 흘리는 것 자체를 하지
        //    않겠다는 뜻이고, 이쪽은 흘리되 막대를 보이지 않겠다는 뜻이다.
        never,
    };

    // 흘리는 창 하나의 치수다 (전부 논리 픽셀).
    // `arrange` 뒤에 유효하며, 앱이 스크롤 상태를 다듬고 화면에 표시할 때 읽는다.
    //  - 네 값을 한 자리에서 내주는 것이 요점이다. 앱이 내용 높이는 자기 식에서,
    //    창 높이는 배치 상수에서, 최대치는 `clamp_scroll`에서 따로 구하면 그 셋이
    //    어긋나는 frame이 반드시 생긴다 (`layout_metrics.h`가 경고한 "식이 두 벌").
    struct scroll_metrics
    {
        float content_height { 0.0f };
        float viewport_height { 0.0f };
        // `arrange`가 범위 안으로 다듬은 값이다.
        float scroll_offset { 0.0f };
        float maximum_scroll { 0.0f };

        // 흘릴 것이 있는가 (내용이 창보다 긴가).
        [[nodiscard]] bool overflowing() const noexcept
        {
            return maximum_scroll > 0.0f;
        }

        [[nodiscard]] bool operator==(const scroll_metrics&) const noexcept = default;
    };

    struct scroll_area_config
    {
        // 같은 화면에 흘리는 창이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        // 내용 전체 높이다 (논리 픽셀).
        // 측정 단계가 없으므로 담는 쪽이 알려 준다.
        float content_height { 0.0f };
        // 지금 흘러간 양이다 (논리 픽셀, 앱 상태).
        // 범위 밖 값은 `arrange`가 다듬고 `metrics()`가 다듬은 값을 되돌려 준다.
        float scroll_offset { 0.0f };
        // 스크롤 위치를 옮기자는 메시지다 (delta는 논리 픽셀).
        //
        // **이 하나가 넷을 함께 켠다** — 휠, 막대의 끌기와 키, 그리고 초점
        // 되살리기다. 지금까지 앱은 같은 factory를 휠 표와 되살리기 표에 두 번
        // 적어야 했고, 두 표가 서로 다른 id를 이름 대야 했다 (한쪽은 막대까지
        // 품은 바깥, 다른 쪽은 실제로 자르는 안쪽). 여기 한 줄이 그 넷의 임자다.
        //  - 없으면 흘리지 않는 창이다. 막대도 만들지 않고 휠도 받지 않는다
        //    ("없는 것은 두지 않는다").
        std::function<input_action(float delta)> scroll {};
        // 스크롤 위치를 **이 자리로** 하라는 절대 메시지다 (offset은 논리 픽셀).
        // 없으면 보조 기술이 자리를 정할 수 없다 (UIA `SetValue`가 읽기 전용으로
        // 선다). 델타로 환산해 보내면 오래된 발행본 기준의 변화량이 겹쳐 쌓인다.
        std::function<input_action(float offset)> scroll_to {};
        scrollbar_visibility bar { scrollbar_visibility::automatic };
        // 막대가 Tab의 자리인가.
        //
        // 홀로 서는 흘리는 창에서는 참이 옳다 — 막대가 자리를 가져야 키보드만으로
        // 긴 글을 훑을 수 있고, 그것이 `scrollbar_element`가 스스로 세우는 기본값이다.
        // 거짓으로 두는 것은 **담는 쪽이 이미 세로 키를 가진 자리**다 (목록·가상
        // 목록). 그러지 않으면 같은 목록에 Tab의 자리가 둘 서고, 사용자는 아무것도
        // 하지 않는 자리를 한 번 더 지나야 한다 (list-view-design.md).
        //  - 흘릴 것이 없는 frame에서는 이 값과 무관하게 자리가 아니다. 끌 수도
        //    누를 수도 없는 막대에 초점이 서면 키가 조용히 사라진다.
        bool bar_tab_stop { true };
        // 창의 위·아래 가장자리 표시다 (구분선과 흘린 쪽의 그림자).
        // 영역이 자기 치수로 그리므로 앱이 offset을 따로 읽어 겹쳐 그리지 않는다 —
        // 그 offset은 `arrange`가 다듬은 뒤에야 맞고, 앱이 앞서 든 값은 한 frame 낡다.
        // 기본값은 전부 거짓이라 지금까지의 영역이 그대로다.
        scroll_edges edges {};
    };

    // 창 오른쪽에 서는 스크롤 막대의 폭이다 (논리 픽셀).
    // 막대의 보이는 폭에 좌우 여백을 더한 값이고, `list_scrollbar_width`와 같은
    // 값이다 — 목록과 그냥 흘리는 창이 나란히 서도 오른쪽 끝이 맞는다.
    inline constexpr float scroll_area_bar_width { scrollbar_visual_width + 8.0f };

    // 흘리는 창·막대·치수·휠·초점 되살리기를 한 자리로 묶은 영역이다.
    //
    // `scroll_view_element`는 자르고 흘리기만 하는 원시 도구라, 그 옆에 막대를
    // 세우려면 앱이 내용 높이·창 높이·스크롤 값 셋을 손으로 맞춰 넘겨야 했다.
    // 창 높이는 배치가 정해져야 알 수 있는 값이라 앱은 그것을 **추측**했고,
    // 추측이 틀리면 막대의 thumb 길이와 실제로 흘릴 수 있는 양이 어긋난다.
    // 그래서 저장소 안의 흘리는 창 절반이 막대 없이 서 있다.
    //  - `list_element`가 이미 목록에서 같은 조립을 한다. 이쪽은 그 조립에서
    //    **행 모델을 뺀 것**이라, 목록이 아닌 내용(글 미리보기, 설정 판, 그림
    //    묶음)도 같은 값을 누린다.
    //
    // 넷을 한곳으로 모은다.
    //  1. **막대를 안에 담는다** — 내용 높이·창 높이·스크롤 값을 `arrange`가
    //     자기 손으로 맞춘다. 창이 실제로 받은 높이가 곧 막대가 재는 높이다.
    //  2. **치수를 내준다** (`metrics`) — 앱이 같은 값을 상태에 되돌린다.
    //  3. **휠을 받는다** — `scroll_source`를 스스로 세우므로 앱의 휠 표에
    //     오르지 않는다 (`route_wheel`의 표 없는 짝).
    //  4. **초점을 따라간다** — `scroll_delta_to_reveal`을 안쪽 창에 넘긴다.
    //     바깥(막대 칸까지 품은 이 영역)을 이름 대도 마지막 행이 막대 밑에
    //     남지 않는다.
    //
    // 열림·스크롤 값은 여전히 앱 상태다. 영역은 그 값을 받아 자리를 잡고
    // 메시지만 낸다 (게시된 tree의 규칙 그대로다).
    class scroll_area_element final : public ui_element
    {
    public:
        explicit scroll_area_element(scroll_area_config config);

        // 흘릴 내용이다.
        // 창 폭은 막대가 가져간 만큼 좁아진 값이라, 내용의 글이 막대 밑을
        // 지나지 않는다.
        void set_content(std::unique_ptr<ui_element> content);

        // `arrange` 뒤에 유효한 이 창의 치수다.
        [[nodiscard]] const scroll_metrics& metrics() const noexcept;
        // 내용이 보이는 자리다 (물리 픽셀). `arrange` 뒤에 유효하다.
        [[nodiscard]] const rect_f& viewport() const noexcept;

        // 그 자리를 창 안으로 들이는 스크롤 변화량이다 (논리 픽셀).
        // 안쪽 창이 답한다 — 배율을 아는 것이 그쪽뿐이다.
        [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        // 막대가 이번 배치에서 자리를 차지하는가.
        [[nodiscard]] bool bar_takes_space() const noexcept;

        scroll_area_config config_ {};
        scroll_view_element* view_ { nullptr };
        // `scroll` factory가 없거나 `bar`가 `never`면 nullptr다.
        scrollbar_element* bar_ { nullptr };
        scroll_metrics metrics_ {};
    };
} // namespace luil

#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

// 대체 typeface는 소유권을 함께 건네므로 `sk_sp`의 정의가 필요하다.
// (전방 선언으로 끝나던 `SkTypeface`와 달리 값으로 담긴다.)
#include "include/core/SkRefCnt.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

class SkFont;
class SkPaint;
class SkTypeface;

namespace luil {
    // element 구현들이 공유하는 Skia 그리기 조각이다.
    // canvas 상태를 저장·복원하지 않으므로 호출자가 paint를 소유한다.
    [[nodiscard]] SkPaint solid_paint(ui_color color);

    // Skia로 가는 모든 글은 이 관문을 지난다.
    // 잘못된 UTF-8을 Skia에 넘기면 `SkFont::countText`가 -1을 돌려주고 그 값이
    // `AutoSTArray::reset`의 `SkASSERT(count >= 0)`에 걸려 **프로세스가 죽는다**.
    // 밖에서 오는 글(프로세스 출력·경로·설정 파일·클립보드)은
    // 언제든 깨질 수 있으므로 근본 원인을 고쳐도 관문은 남긴다.
    //
    // 온전한 글이면 `text`를 그대로 돌려주고 `storage`를 건드리지 않는다.
    //  - 위반이 없으면 할당도 복사도 없다.
    [[nodiscard]] std::u8string_view drawable_text(std::u8string_view text, std::u8string& storage);

    // 고른 글꼴에 없는 글자를 대신 그릴 typeface를 찾는다.
    // `Cascadia Code`에는 한글이 없고 어느 코드 글꼴에도 이모지가 없다.
    // 대체가 없으면 그 자리는 빈 네모가 된다.
    //
    // 해석은 platform이 하고 presentation은 이 interface만 안다.
    // 앱 상태가 아니라 OS 자원 cache라 단일 소유 규칙
    // 밖이다 (`win32_fonts`의 registry와 같은 성격이다).
    struct font_fallback
    {
        font_fallback() = default;
        font_fallback(const font_fallback&) = delete;
        font_fallback(font_fallback&&) = delete;
        font_fallback& operator=(const font_fallback&) = delete;
        font_fallback& operator=(font_fallback&&) = delete;
        virtual ~font_fallback() = default;

        // 찾지 못하면 nullptr다.
        // 그리기와 입력 thread가 함께 부르므로 구현은 thread-safe해야 한다.
        //
        // **소유권을 함께 건넨다.** 날 포인터를 돌려주면 부르는 쪽이 쓰는 동안
        // 그 typeface가 살아 있다는 것을 **해석기의 cache가 절대 놓지 않는다**는
        // 사실에만 기대게 된다 — 그래서 cache에 상한도 무효화도 둘 수 없었다
        // (글꼴이 설치·삭제되면 낡은 답이 그대로 남는다). `sk_sp`를 돌려주면
        // 그 의존이 사라져 해석기가 자기 cache를 자유롭게 비울 수 있다.
        [[nodiscard]] virtual sk_sp<SkTypeface> for_codepoint(char32_t codepoint) const = 0;
    };

    // platform이 시작할 때 한 번 등록하고 끝날 때 nullptr로 되돌린다.
    // 등록하지 않으면 대체 없이 기본 typeface로만 그린다.
    //  - test가 이 경로다.
    void set_font_fallback(const font_fallback* fallback) noexcept;

    // 글자를 그리는 방식이다.
    // `grayscale`은 회색조 anti-alias, `subpixel_lcd`는 LCD 서브픽셀(ClearType 계열)이다.
    // 어느 쪽이든 글리프는 픽셀 격자에 스냅하지 않고 소수 자리에 놓는다 —
    // 분수 배율(125%·150%)에서 자간이 뭉치고 caret과 어긋나는 것을 막는다.
    enum class text_render_quality
    {
        grayscale,
        subpixel_lcd,
    };

    // platform이 시작할 때와 OS 설정이 바뀔 때 OS의 글꼴 다듬기 설정을 읽어 정한다.
    // 정하지 않으면 회색조다 — LCD 서브픽셀은 표면의 픽셀 배치를 알아야 해서
    // 렌더러가 그 정보를 준 표면에서만 실제로 켜진다.
    void set_text_render_quality(text_render_quality quality) noexcept;

    // 이어 그릴 한 조각이다.
    // `typeface`가 nullptr이면 기본 글꼴 그대로다.
    struct text_run
    {
        std::u8string_view text {};
        // nullptr이면 기본 글꼴이다.
        // 대체 typeface는 **run이 소유한다** — 그리는 동안 해석기가 cache를
        // 비워도 이 run은 자기 것을 들고 있다.
        sk_sp<SkTypeface> typeface {};
    };

    // 기본 typeface에 **없는 글자만** 대체 typeface로 갈라낸다.
    //
    // 규칙은 하나다: 기본 글꼴이 그릴 수 있으면 기본 글꼴이다.
    // 그래서 한글 뒤의 영문이 다시 원래 글꼴로 돌아오고, 코드 글꼴에서 공백이 고정폭을 유지한다.
    //
    // 측정과 그리기가 **이 함수 하나를** 함께 써야 caret 자리와 글자가 어긋나지 않는다.
    // 경계를 test가 직접 확인할 수 있도록 노출한다.
    [[nodiscard]] std::vector<text_run> split_text_runs(std::u8string_view text, const SkFont& font);

    void draw_text(SkCanvas& canvas, std::u8string_view text, float x, float y, const SkFont& font, const SkPaint& paint);

    // 같은 색의 투명도만 바꾼 값이다.
    // chip 배경처럼 강조색을 옅게 깔 때 쓴다.
    [[nodiscard]] ui_color with_alpha(ui_color color, float alpha) noexcept;

    // hover·눌림 배경을 그린다.
    // 상태 판정(눌림 > hover)과 색 역할 선택이 한곳이라 element마다 어긋나지 않는다.
    // 비활성이면 아무것도 그리지 않고, radius는 논리 픽셀이다 (0이면 직각).
    // 반투명 색이라 기존 배경 위에 겹쳐 칠한다 — 배경을 대체하지 않는다.
    void draw_hover_fill(draw_context& context, const rect_f& box, const ui_element_id& id, const interaction_snapshot& interaction, bool enabled = true, float radius = 0.0f);

    [[nodiscard]] float measure_text(std::u8string_view text, const SkFont& font);

    // `max_width` 안에 들어가도록 뒤를 잘라 `…`를 붙인 문자열이다.
    // `…`조차 들어가지 않으면 빈 값이다.
    // 자르는 위치는 UTF-8 문자 경계다.
    [[nodiscard]] std::u8string elide_text(std::u8string_view text, float max_width, const SkFont& font);

    // `max_width` 안에서 잘라 그린다.
    // 좁은 창에서 글자가 옆 UI를 침범하지 않게 하는 공통 경로다.
    // 반환값은 실제로 그린 폭이다.
    float draw_text_within(SkCanvas& canvas, std::u8string_view text, float x, float y, float max_width, const SkFont& font, const SkPaint& paint);

    // `area` 위쪽 경계에서 아래로 옅어지는 그림자다.
    // 스크롤된 내용이 상단 막대 아래로 지나간다는 것을 보여 준다.
    // 셰이더 없이 알파를 낮춘 띠를 쌓는다.
    void draw_downward_shadow(SkCanvas& canvas, const rect_f& area, ui_color color, float strength);

    // `area` 아래쪽 경계에서 위로 옅어지는 그림자다.
    // 흘린 내용이 바닥 아래로 더 이어진다는 것을 보여 준다 — `draw_downward_shadow`의 짝이다.
    void draw_upward_shadow(SkCanvas& canvas, const rect_f& area, ui_color color, float strength);

    // 둥근 사각형 `box` 바깥으로 드리우는 그림자다 (dialog·카드가 바탕에서 떠 있음을 말한다).
    // 바깥으로 넓힌 둥근 사각형을 옅은 알파로 겹쳐 쌓아 가장자리로 갈수록 진해지고, 아래로 조금
    // 밀어 빛이 위에서 오는 것처럼 보인다. 셰이더 없이 그린다. 색은 `content_shadow`다.
    //  - `radius`는 상자의 모서리 반지름이다 (`box`와 같은 물리 픽셀).
    //  - `strength`는 가장자리에 닿는 진하기의 대략값이다 (0~1). 0 이하면 아무것도 그리지 않는다.
    //  - 바깥에 그릴 자리가 있을 때만 뜻이 있다. 표면(창·popup)의 가장자리에 붙은 상자에서는 잘린다.
    void draw_surface_shadow(draw_context& context, const rect_f& box, float radius, float strength);

    // 흘리는 창의 위·아래 가장자리를 그린다 (`scroll_edges`의 규칙).
    // 흘린 내용이 위로 지나갔으면(`scroll_offset > 0`) 위에, 아래에 더 있으면
    // (`scroll_offset < maximum_scroll`) 아래에 그림자를 드리운다. 구분선은 설정이 켠 쪽에 늘 긋는다.
    // 값은 전부 논리 픽셀이고 `box`는 물리 픽셀이다 (element의 bounds).
    //  - `scroll_area_element`·`list_element`가 부르고, 자기 창을 손으로 지은 앱도 같은 함수로
    //    같은 그림을 얻는다.
    void draw_scroll_edges(draw_context& context, const rect_f& box, float scroll_offset, float maximum_scroll, const scroll_edges& edges);

    // 고른 행의 표시다 — 행 안쪽의 둥근 옅은 채움과 왼쪽 가장자리의 키 컬러 표식이다.
    // 목록·가상 목록의 행과 앱이 지은 행이 같은 함수를 써야 한 화면의 고름이 한 모양이다.
    //  - 채움은 `accent_soft`의 낮은 알파라 위의 글이 읽히고, 표식은 고대비 팔레트가 옅은 바탕을
    //    접어도 어느 행인지 남긴다.
    //  - 안쪽으로 조금 들여 그린다. 진행 막대나 hover 채움이 같은 행에 겹쳐도 각각의 경계가 남는다.
    void draw_row_selection(draw_context& context, const rect_f& box);

    // target 중앙에 글리프 하나를 그린다.
    // 글리프가 없으면 아무것도 그리지 않는다.
    void draw_centered_glyph(SkCanvas& canvas, char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint);

    // 진행 표시 글리프가 한 바퀴 도는 데 걸리는 시간이다.
    // 정지 글리프는 "멈췄다"로 읽혀 작업이 살아 있다는 것을 전하지 못한다.
    inline constexpr std::chrono::milliseconds spinner_period { 1000 };

    // `target` 중앙을 축으로 글리프를 돌려 그린다.
    // 각도는 `now`만의 함수라 어느 frame에 그려도 위상이 같다.
    //  - caret 깜빡임과 같은 규칙이다.
    void draw_spinning_glyph(SkCanvas& canvas, char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint, std::chrono::steady_clock::time_point now);

    // 지금 시각의 회전 각도(도)다.
    // [0, 360)이며 test가 위상을 확인한다.
    [[nodiscard]] float spinner_angle(std::chrono::steady_clock::time_point now) noexcept;

    // target_height 안에서 세로 중앙 정렬된 텍스트 baseline을 돌려준다.
    [[nodiscard]] float centered_text_baseline(const SkFont& font, float target_height);

} // namespace luil

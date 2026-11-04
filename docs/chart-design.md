# 차트

계열 값을 선·막대·영역으로 그리고, 같은 간격의 범주 x축과 왼쪽 y축의 눈금·격자, 범례를 붙인다. 키보드·누름·마우스 hover로 고른 범주의 값을 요소 안의 값 상자에 띄우고, 보조 기술에는 요약 한 줄과 고른 점의 글을 낸다. 자료 움직임은 없다. luil core가 아니라 저장소가 따로 세우는 선택 라이브러리 `luil_chart`에 있다(꼴과 빌드는 [차트 라이브러리](chart-library-design.md)). 공개 형식은 `chart/include/luil/chart/chart_element.h`의 `chart_series_kind`·`chart_marker`·`chart_series`·`chart_point`·`chart_legend`·`chart_config`·`chart_element`와 `chart_scale.h`의 `chart_range`·`chart_ticks`·`nice_ticks`·`format_chart_number`·`chart_value_to_pixel`이고, 우산 헤더는 `luil/chart.h`다.

## 문제

앱이 차트를 띄우려면 그림부터 접근 정보까지 모두 짓는다.

- 그리는 요소가 없다. `include`·`src`에 계열이나 축을 그리는 코드가 없고, 가장 가까운 것은 값 막대 하나인 [progress_element.h](../include/luil/ui/progress_element.h)다.
- 앱이 직접 그리면 같은 것을 다시 짓는다. 둥근 눈금, 눈금 글 폭에 따른 플롯 자리, 값 상자 자리, 키보드로 점 옮기기, 접근 요약이다. 커스텀 요소 개념 문서([ui-element.md](concepts/ui-element.md), 57줄)에는 접근 정보를 내는 절이 없다.
- 요소는 포인터가 어디 머무는지 모른다. `interaction_snapshot::hover_point`는 tooltip이 있는 요소에서만 기록되고 지연 뒤 멈춘다([ui_element.h](../include/luil/ui/ui_element.h) 956~961행). hover로 범주를 고르려면 자식 칸이 그 단위가 되어야 한다. 별점의 걸음 칸이 그 선례다([rating_element.cpp](../src/ui/rating_element.cpp) 36~51행).
- UIA에 차트 꼴이 없다. `access_role`에 차트가 없고([accessibility.h](../include/luil/ui/accessibility.h) 21~78행), Grid·Table 패턴도 없다. 공유 파일을 고치지 않는 꼴은 잉크의 이미지 역할 요약([ink_canvas_element.cpp](../src/ui/ink_canvas_element.cpp) 125~135행)과 색 판의 범위 없는 두 값 글([color-picker-design.md](color-picker-design.md) 127행)이다.
- 숫자 서식이 없다. 확정 G6 범위 B는 조회·자리 값·복수형뿐이라 눈금 글은 지역에 매이지 않는 기본과 앱 함수로 선다([rating-design.md](rating-design.md) 235행과 같은 규칙).

## 공개 형식

```cpp
// chart/include/luil/chart/chart_scale.h (요소 없는 순수 함수, calendar_date.h 선례)
struct chart_range { double minimum { 0.0 }; double maximum { 1.0 }; };
struct chart_ticks
{
    double first { 0.0 };    // 첫 눈금. 범위의 최소 이하
    double step { 1.0 };     // 1·2·5 x 10^k
    std::size_t count { 0 }; // 마지막 눈금이 범위의 최대 이상이 되는 수
    int decimals { 0 };      // 눈금 글의 소수 자리. step에서 나온다
};
// 범위를 덮는 둥근 눈금. target_count는 눈금 사이 칸 수의 상한이고 2~10으로 접는다. 칸이 상한을 넘지 않는 가장 작은 걸음을 고른다.
// 최소와 최대가 같거나 끝의 크기에 견줘 폭이 없으면 그 값의 자릿수 단위(5면 1, 120이면 100, 0이면 1)로 앞뒤 한 걸음씩 덮는다.
// 끝이 유한하지 않거나 절댓값이 1e300을 넘으면 {0, 1}로 본다.
[[nodiscard]] chart_ticks nice_ticks(chart_range range, std::size_t target_count) noexcept;
// 지역 무관 숫자 글("-1250.5"). 소수 자리 고정(0~20으로 접는다), 천 단위 구분 없음, 0으로 반올림된 음수는 부호를 뗀다("-0"은 "0"). 유한하지 않으면 빈 글.
[[nodiscard]] std::u8string format_chart_number(double value, int decimals);
// 값을 [start, end] 픽셀로 옮긴다. end가 start보다 작으면 축이 뒤집힌다(y축). 범위 밖은 자르지 않고, 폭이 0이거나 유한하지 않으면 가운데다.
[[nodiscard]] float chart_value_to_pixel(double value, chart_range range, float start, float end) noexcept;

// chart/include/luil/chart/chart_element.h
enum class chart_series_kind { line, bar, area };
enum class chart_marker { automatic, none, circle, square, triangle, diamond };
struct chart_series
{
    std::u8string name {};
    chart_series_kind kind { chart_series_kind::line };
    std::shared_ptr<const std::vector<double>> values {};  // 범주 하나에 값 하나. NaN은 빈 점
    std::optional<ui_color> color {};                       // 비면 계열 표의 차례 색(결정 E2)
    chart_marker marker { chart_marker::automatic };
};
struct chart_point
{
    std::size_t series { 0 };
    std::size_t category { 0 };
    [[nodiscard]] bool operator==(const chart_point&) const = default;
};
enum class chart_legend { bottom, top, none };
struct chart_config
{
    std::u8string owner {};
    std::u8string name {};                    // 접근 이름. 비면 기본 문구(차트)
    std::vector<std::u8string> categories {}; // x 이름표. 모자라면 1부터의 번호
    std::vector<chart_series> series {};
    std::optional<chart_range> y_range {};    // 비면 자료에서. 막대·영역이 있으면 0을 넣는다
    chart_legend legend { chart_legend::bottom };
    bool grid { true };
    std::optional<chart_point> focused {};    // 고른 점(앱 상태)
    std::function<input_action(chart_point)> focus {};  // 비면 고를 수 없는 그림이다
    std::function<std::u8string(double value)> format_value {};  // 비면 format_chart_number
    std::function<std::u8string(const chart_config&)> access_summary {};
    std::function<std::u8string(const chart_config&, chart_point)> access_point {};
};
class chart_element final : public ui_element
{
public:
    explicit chart_element(chart_config config);
    ~chart_element() override;
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;  // image, 이름, 요약
    [[nodiscard]] rect_f plot_bounds() const noexcept;         // 배치 뒤 플롯 사각형(물리 픽셀)
private:
    struct state;  // 자료 요약과 배치 결과(chart_element.cpp). 큰 자료의 열 표를 공개 헤더에 드러내지 않는다
    class plot_part;
    std::unique_ptr<state> state_;
    plot_part* plot_ { nullptr };
};
```

자식은 요소가 스스로 짓는다. 플롯(`chart_plot`, Tab 자리, 키와 점 글)과 그 아래 hover 칸(`chart_cell`, 그리지 않고 hit·hover·누름·끌기만)이다. kind는 [차트 라이브러리](chart-library-design.md) 결정 L2를 따른다.

## 규칙

**자료.** 범주 수는 `categories`와 가장 긴 계열 가운데 큰 쪽이다. 짧은 계열의 뒤는 빈 점이다. NaN과 무한은 빈 점이라 선과 영역은 그 자리에서 끊고 막대는 그리지 않는다. 값은 `shared_ptr<const vector>`로 나눠 쥐어 frame마다 복사하지 않는다(게시된 tree는 불변이다). 계열이나 범주가 없으면 플롯 가운데에 기본 문구 "자료 없음"을 그린다.

**축과 눈금.** x는 같은 간격의 범주다. 막대 계열이 있으면 범주마다 칸 가운데에, 없으면 첫·끝 범주를 플롯 양 끝에 둔다. y 범위는 `y_range`, 비면 빈 점을 뺀 자료의 최소·최대이고 막대·영역 계열이 있으면 0을 넣는다. 눈금은 `nice_ticks`이고 칸 수의 상한은 플롯 높이를 글 높이의 2.5배로 나눈 값이라 눈금 글 간격이 글 높이의 2.5배보다 좁아지지 않는다. x 이름표가 겹치면 겹치지 않는 가장 작은 n을 골라 n개마다 하나를 그린다. 범주가 4096개를 넘으면 재는 수가 범주 수를 따라 늘지 않게 겹친 쌍이 요구하는 걸음으로 건너뛰어 찾는다. 폭이 크게 갈리는 이름표에서는 가장 작은 n이 아닐 수 있지만 겹치지는 않는다.

**플롯 자리.** `arrange`에서 배치 문맥의 측정기로 y 눈금 글의 가장 넓은 폭과 글 높이를 잰다. 요소 자리에서 왼쪽 눈금 글 폭, 아래 x 이름표 한 줄, 범례 줄을 뺀 나머지가 플롯이다. 맨 위 눈금 글의 절반이 플롯 위로 나가므로 위에 글 높이의 절반을 비우고, 끝 범주 이름표의 절반이 나가는 오른쪽에 12 논리 픽셀을 비운다. 눈금 글 폭이 요소 폭의 절반을 넘으면 글을 줄여 플롯이 남게 한다. 치수는 논리 픽셀이고 배율을 곱한다. 원하는 높이는 앱이 준다(목록 행처럼 담는 쪽의 길이를 쓴다).

**그리기.** 격자는 `divider`, 축 선은 `control_border`, 눈금 글과 범례 글은 `secondary_foreground`다. 선은 2 논리 픽셀이고 영역은 계열색을 알파 0.25로 0까지 채운 뒤 같은 선을 긋는다. 막대는 범주 칸의 70%를 막대 계열 수로 나눠 나란히 세운다(쌓지 않는다). 막대를 맨 아래에 두고 그 위에 영역, 선 차례로 그린다. 넓은 면이 선을 가리지 않게 하려는 것이다. 앞뒤가 빈 점인 값은 긋는 선이 없어 점으로 찍는다. 범례는 한 줄이고 넘치는 계열은 잘린다. 비활성이면 모든 계열이 `disabled_foreground`다. 글은 [text_block.h](../include/luil/ui/text_block.h)의 `make_text_font`·`draw_text_block`으로 그린다.

**고대비.** `draw_context::theme`이 고대비면([ui_element.h](../include/luil/ui/ui_element.h) 860~862행) 모든 계열을 `primary_foreground` 한 색으로 접는다. 계열은 선 모양(실선, 긴 점선, 점선, 쇄선 차례)과 표식(원, 네모, 세모, 마름모 차례)으로 가르고, 막대는 첫 계열만 채우고 나머지는 같은 선 모양의 테로 그린다. 범례도 같은 선 모양과 표식을 그린다. 표식 `automatic`은 고대비와 고른 점에서만 그린다. 범주 간격이 8 논리 픽셀보다 좁거나 큰 자료면 점마다의 표식은 그리지 않고 고른 점에만 그린다. 표식이 선을 덮어 계열을 가르지 못해서다. `none`은 고른 점에도 그리지 않는다. 점선은 `SkDashPathEffect`다(두 Skia 패키지에 헤더가 있고 [images_page.cpp](../examples/demo/images_page.cpp) 189행이 쓴다). 구문 색이 고대비에서 한 색으로 접히고 굵기로 가르는 것과 같은 방침이다([syntax-highlight-design.md](syntax-highlight-design.md) 173행).

**값 상자.** 고른 점(`focused`)이 있으면 그 범주, 없으면 마우스가 머문 칸의 범주에 띄운다. 첫 줄은 범주 이름표, 그 아래 줄마다 계열 표식·이름·값이다. 자리는 [field_popup.h](../include/luil/ui/field_popup.h) 84행 `place_field_popup_at_side`다. `field`는 그 범주 점(막대면 막대 꼭대기)의 사각형이고, 마우스가 머문 범주에서는 가장 높은 점이라 상자가 그 범주의 점을 가리지 않는다. `limit`은 요소 자리, 바라는 방향은 위다. 요소 높이에 들어가는 줄까지만 싣는다. 돌려받은 논리 값에 배율을 곱해 요소가 자기 안에 그린다. popup이 아니라서 `ui_frame::popups`를 쓰지 않는다. 바탕은 `tooltip_background`, 테는 `tooltip_border`, 글은 `primary_foreground`다.

**포인터.** 칸 수는 범주 수와 "플롯 폭(논리 픽셀) / 4" 가운데 작은 값이고, 칸마다 가운데에 가장 가까운 범주를 맡는다. 점이 많아도 tree는 플롯 폭만큼만 커진다. 칸은 폭이 있는 첫 `arrange`에서 한 번만 짓는다([virtual_list_element.cpp](../src/ui/virtual_list_element.cpp) 838~870행 선례). 폭이 0인 배치는 짓지 않는다. 창 크기가 바뀌면 앱이 tree를 다시 지으므로 칸 수도 새로 정해진다. 칸은 누름(`left_click`)과 끌기(`pointer_drag_target`, 시작 거리를 둔다)를 들고, 그 범주에서 누른 y에 가장 가까운 계열의 점으로 `focus`를 낸다. 같은 점이면 빈 목록이다. 칸은 Tab 자리가 아니고, 눌러서 얻는 초점은 `set_pointer_focus_target`으로 플롯에 보낸다(별점 칸과 같은 꼴, [rating-design.md](rating-design.md) 95행). 칸을 누른 뒤 곧바로 화살표가 먹힌다. `focus`가 비면 칸은 hover만 맡는다. 액션도 끌기도 없으면 hit 대상이 아니라서(`ui_element::interactive`) 커서 `arrow`를 들어 마우스 hover가 칸에 서게 한다.

**키보드.** 플롯이 Tab 자리 하나다. ←·→는 이전·다음 범주, Home·End는 처음·끝, PageUp·PageDown은 범주 수의 10분의 1(적어도 1)이다. 이 넷은 가로 축 `key_step_target`이다. ↑·↓는 같은 범주에서 이전·다음 계열이고 `set_key_handler`가 받는다(색 고르기의 두 축 키와 같은 조합). 고른 점이 없으면 어느 키든 첫 점에서 시작한다. 끝에 닿은 키는 빈 목록으로 삼키고 Ctrl·Alt를 든 키는 앱 단축키로 흐른다([value-step-design.md](value-step-design.md) 54행). 범위 밖의 `focused`는 고르지 않은 것으로 보고, 자료가 없으면 키를 삼킨다. 초점 테는 `focus_ring_bounds`가 고른 점 표식 둘레(없으면 플롯)로 준다.

**접근.** root는 `image`이고 이름은 `name`, 비면 기본 문구다. 값은 `access_summary`, 비면(빈 글을 돌려줘도) 기본 요약("계열 2개, 범주 12개, 최솟값 3.0, 최댓값 120.0")이다. 요약의 최솟값과 최댓값은 생성자에서 세어 `arrange` 전에도 답한다. 플롯은 범위 없는 `slider`이고 이름은 "자료 점", 값은 고른 점의 글이다. 고른 점이 없으면 값이 비어 Value 패턴이 서지 않는다. 글은 `access_point`, 비면(빈 글을 돌려줘도) 계열이 하나일 때 "3월, 120.0", 여럿일 때 "매출, 3월, 120.0"이고 빈 점의 값은 "값 없음"이다. `format_value`를 주면 값 자리는 그 글이다. `focus`가 비면 고를 수 없는 그림이라 플롯은 `none`이고 접근 노드는 root 하나다. 칸은 `none`이라 접근 tree에서 접힌다. 값이 비지 않으면 UIA Value 패턴이 읽기 전용으로 서고([uia_provider.cpp](../src/win32/uia_provider.cpp) 1123~1126행), 점을 옮겨 값이 바뀌면 값 바뀜 이벤트가 난다(629행). `accessibility.h`·`uia_provider.cpp`를 고치지 않는다. 접근 노드는 점 수와 무관하게 둘이다.

**숫자 글.** `format_value`가 비면 `format_chart_number`다. 눈금은 `chart_ticks::decimals` 자리, 값 상자와 접근 글은 거기에 한 자리를 더한다(6 자리까지. 눈금이 이미 더 길면 눈금 자리를 그대로 둬 작은 값이 0으로 뭉개지지 않는다). 배치 전에는 칸 상한 5로 잰 눈금의 자리다. 안쪽은 `std::to_chars(double, std::chars_format::fixed, 자리)`라 로케일에 매이지 않는다. 부동소수 `to_chars`는 core가 이미 두 플랫폼에서 쓴다([ui_style_json.cpp](../src/theme/ui_style_json.cpp) 77행). 천 단위 구분, 큰 수 접미, 지역 소수점은 넣지 않고 앱이 함수로 준다.

**큰 자료.** 범주 수가 플롯 폭의 물리 픽셀 수를 넘으면, 선과 영역은 픽셀 열마다 그 열에 든 값의 최소·최대만 잇는다. 모양은 같고 긋는 수는 폭의 두 배를 넘지 않는다. 막대는 열마다 절댓값이 가장 큰 값 하나를 1픽셀 폭으로 그리고, 나란히 세울 폭이 없어 계열은 같은 열에 겹친다. 열 표는 `arrange`에서 플롯 폭이 바뀔 때만 세고 그리기와 hover는 그 표만 읽는다. 선은 32점씩 나눠 긋고 영역의 면은 열마다 한 픽셀 폭으로 칠한다. 경로 하나로 그으면 한 줄을 지나는 변이 폭만큼 쌓여 칠하는 비용이 그 곱으로 늘기 때문이다(아래 확인 절의 잰 값). 눈금 범위는 `draw` 전에 요소 수명 동안 한 번만 센다.

**움직임.** 자료 움직임과 값 상자의 나타남 움직임이 없다. 움직임 줄이기와 만나지 않고 `next_update`를 청하지 않는다.

**마크업.** 어휘에 넣지 않는다. 앱이 `make_slot`에서 `chart_element`를 돌려준다([ui_markup.h](../include/luil/ui/ui_markup.h) 184~187행). 모델을 쥔 요소는 slot으로 짓는다는 규칙([markup-design.md](markup-design.md) 222행)을 차트에 그대로 쓴다.

## 결정

확정(2026-11-04): N5. 차트는 core 밖 선택 라이브러리다([차트 라이브러리](chart-library-design.md)). 그래서 이 요소는 core 공개 헤더만 쓰고 공유 파일(팔레트, 접근 역할, UIA, controller)을 고치지 않는 길을 고른다.

확정(2026-11-02): G6 범위 B. 숫자 서식이 없어 눈금 글은 지역 무관 기본과 `format_value`다.

**E1. 기능 범위.** 질문: 첫 판의 차트가 무엇까지 그리는가.

- 길 a: 결정 조사 길 b의 범위. 선·막대·영역 한 요소, 같은 간격의 범주 x축 하나와 왼쪽 y축 하나, 둥근 눈금·격자·범례·값 상자·키보드·접근 요약이다. 자료 움직임은 없다. 되는 것: 흔한 시계열과 범주 비교를 한 요소로 그린다. 공유 파일을 고치지 않는다. 대가: 수·시간 x축과 둘째 y축이 없어 고르지 않은 간격의 자료는 앱이 범주로 옮긴다.
- 길 b: a에 오른쪽 둘째 y축, 쌓은 막대, 수 x축(시간)을 더한다. 되는 것: 단위가 다른 두 계열과 고르지 않은 시각을 그린다. 대가: 축 배치·눈금·접근 글이 두 벌이 되어 크기가 L에서 XL 쪽으로 간다.
- 길 c: 선과 막대만, 범례·값 상자·키보드 없이 그림과 요약만 낸다. 되는 것: 가장 작다. 대가: 값을 읽을 길이 접근 요약뿐이라 "차트 라이브러리"의 몫이 레시피와 크게 다르지 않다.
- 권장: a. 대가: 위. 승인: 길 a (2026-11-04).

**E2. 계열색.** 질문: 앱이 색을 주지 않은 계열의 색을 어디서 얻는가.

- 길 a: 차트 안의 고정 계열 표. 8색을 어두운 바탕용·밝은 바탕용 두 벌로 두고, 바탕은 이름이 아니라 `is_dark_background(palette.window_background)`로 고른다([ui_theme.h](../include/luil/theme/ui_theme.h) 37~42행). 두 내장 팔레트의 창 바탕과 대비 3:1 이상을 test로 잠근다. 9번째 계열부터는 처음으로 돌고 표식이 가른다. 되는 것: core를 고치지 않고, 계열끼리 구별이 검증된 색이다. 대가: 스타일 JSON으로 계열색을 바꿀 수 없어 앱이 `color`로 덮는다.
- 길 b: `palette.accent`의 색상을 돌려 짓는다([color_space.h](../include/luil/ui/color_space.h)의 HSV 변환). 되는 것: 앱의 강조색과 어울린다. 대가: 같은 채도·명도에서 색상만 돌리면 노랑과 파랑의 밝기가 갈려 대비가 고르지 않다.
- 길 c: core 팔레트에 계열색 역할을 더한다. 역할마다 두 팔레트 구조체 필드, 키 표 한 행, `style.json` 한 줄이고 합성·고대비 줄은 손으로 쓴다([style-sheet-design.md](style-sheet-design.md) 127~128행). 되는 것: 스타일 JSON과 hot_reload가 계열색까지 닿는다. 대가: 확정 N5의 "core에 넣지 않는다"와 갈리고 차트를 끈 빌드에도 역할이 남는다.
- 권장: a. 대가: 위. 권장대로 진행.

**E3. 값 상자를 여는 길.** 질문: 마우스가 머문 범주의 값을 어떻게 보이는가.

- 길 a: hover 칸 자식(별점 선례)이 마우스 hover를 받고, 누름·끌기와 키보드가 고른 점을 바꾼다. 되는 것: 마우스는 누르지 않고 읽고 터치·펜·키보드도 같은 상자를 쓴다. controller를 고치지 않는다. 대가: 플롯 폭에 비례한 자식 칸이 tree에 선다.
- 길 b: hover 없이 누름·끌기와 키보드만. 되는 것: 자식이 플롯 하나다. 대가: 마우스 사용자가 눌러야 값을 본다.
- 길 c: controller가 tooltip 없는 요소에도 hover 자리를 기록한다. 되는 것: 칸 없이 좌표로 고른다. 대가: 이번 라운드 줄 3만 고치는 controller 변경이고 이동마다 발행본이 바뀐다(ui_element.h 959행이 막은 이유).
- 권장: a. 대가: 위. 권장대로 진행.

**E4. 접근 꼴.** 질문: 보조 기술에 차트를 어떻게 내는가.

- 길 a: root `image`에 이름과 요약, 플롯은 범위 없는 `slider`에 고른 점의 글이다. 되는 것: 잉크와 색 판이 선 꼴이라 공유 파일을 고치지 않고, 접근 노드가 점 수와 무관하게 둘이다. 대가: 화면 읽기는 한 번에 한 점만 읽고 자료 표를 훑지 못한다.
- 길 b: root `list`, 범주마다 `list_item`(달력 격자 선례, [date-picker-design.md](date-picker-design.md) 165~166행). 되는 것: 화면 읽기가 항목을 훑는다. 대가: 범주 수만큼 접근 노드가 서고, 큰 자료면 `access_item_model`로 화면 밖 항목을 답해야 한다.
- 길 c: 접근 전용 표(`table`·`data_item`)를 곁에 둔다. 되는 것: 화면 읽기가 행·열 구조로 자료를 훑는다. 대가: 행 고르기가 없는 정적 표라 DataGrid 번역과 맞지 않는다(accessibility.h 60~63행).
- 권장: a. 대가: 위. 권장대로 진행.

**구현 때 정한 것.** 위 결정이 비워 둔 자리다.

- 그림만 내는 차트의 칸은 커서 `arrow`로 hit 대상이 된다. `hover_region_target`은 hit를 받지 않아 칸의 `hovered`가 서지 않고, `set_hit_opaque`는 아래로 가는 클릭까지 막는다.
- `focus`가 비면 플롯은 접근 tree에서 접힌다. 키로 닿지 못하는 slider는 화면 읽기에 소음이고, 요약은 root가 이미 말한다.
- 고른 점이 없을 때 플롯의 값은 비운다. "값 없음"은 빈 점의 말이라 뜻이 갈린다. 없는 동안 Value 패턴이 서지 않는다.
- 고른 점이 없으면 어느 키든 첫 계열의 첫 범주 하나를 고른다. 걸음을 함께 옮기지 않는다.
- 칸의 owner는 별점처럼 차트 owner, U+001F, 1부터의 번호다. 플롯의 owner는 차트와 같고 kind로 갈린다.
- 값 글의 소수 자리는 배치 전에 칸 상한 5로 잰 눈금을 쓴다. 접근 정보는 `arrange` 없이도 답해야 하고, 앱이 `format_value`를 주면 자리와 무관하다.
- 큰 자료는 선을 32점씩 나눠 긋는다. 불투명한 계열은 겹친 자리가 보이지 않는다. 알파가 있는 계열색은 조각 이음에서 한 점이 진해질 수 있다.

## 플랫폼

**Win32.** UIA는 root가 Image와 Value(요약), 플롯이 Slider와 Value(점 글)다. RangeValue는 서지 않는다. `uia_provider.cpp`·`win32_window.cpp`를 고치지 않는다.

**Android.** Java 소스는 0줄이고 JNI도 없다. 같은 코드다. hover가 없어 값 상자는 탭·끌기·키보드로 연다. 칸의 끌기는 시작 거리를 두어 터치 몸짓 규칙을 탄다. 축에 맞는 빠른 쓸기는 감싼 창의 스크롤, 허용치 안에서 떼면 탭(점 고르기), 오래 눌렀다 옮기면 훑기다(ui_element.h 384~387행). 그래서 세로로 흘리는 페이지 안의 차트가 스크롤을 막지 않는다. 회전 뒤에도 고른 점은 앱 상태라 남는다. Android의 luil에는 접근성 계층이 없다. 접근성 규칙은 Win32 UIA 기준이다.

## 다른 노드와의 약속

- **core와 빌드.** [차트 라이브러리](chart-library-design.md)의 약속 절이 고칠 core 줄과 자리를 적는다. 이 요소는 그 밖의 core 파일을 고치지 않는다.
- **줄 4(i18n).** 기본 문구는 `chart_text` 함수이고 이름이 키다. 라이브러리 커밋에서는 한국어 몸통이고, 줄 1이 쓰는 표 전환 커밋이 `chart/text/ko.json`·`en.json`과 `luil_add_string_table`로 몸통을 `select_strings` 조회로 바꾼다. 통합 때 줄 4 표 커밋 뒤, 줄 4 검사 넓힘 커밋 앞에 쌓는다([차트 라이브러리](chart-library-design.md) 약속 절). 요약의 "계열 N개"·"범주 N개"가 복수형 항목, 점 글이 자리 값 문장(계열, 범주, 값)이 된다. 숫자 서식이 없어 `format_value` 칸은 i18n 뒤에도 남는다.
- **줄 2(hot_reload·markup).** 스타일 키를 더하지 않아 다시 읽기와 무관하다. 마크업 어휘에 들지 않는다.
- **줄 3.** controller와 `ui_tree.*`를 고치지 않는다(결정 E3).
- **데모.** 라이브러리 커밋과 데모 연결 커밋을 나누고, 연결은 통합 때 다른 줄의 데모 커밋 뒤에 직렬로 쌓는다. 차트 페이지는 새 파일이라 다른 줄의 페이지와 겹치지 않고, 두 셸의 페이지 목록·`handle`·`build` 자리에 한 줄씩 든다.

**미룬 것을 닫는다.** 설계 단계에서는 기존 문서를 고치지 않고 구현 커밋이 고친다.

| 위치 | 닫는 방법 |
| --- | --- |
| teaching-tip-design.md 219행 | 값 상자 자리는 `place_field_popup_at_side`다. 차트는 마크업 어휘에 들지 않고 slot으로 짓는다 |
| style-sheet-design.md 231행, syntax-highlight-design.md 225행 | 차트는 core 팔레트에 역할을 더하지 않는다(결정 E2). 두 팔레트 구조체 끝 자리는 차트 몫이 아니다 |
| markup-design.md 222행 | 모델을 쥔 요소의 slot 규칙을 차트에도 쓴다(위 마크업 규칙) |
| default-text-design.md 170·174행 | 차트 문구는 core `default_text` 끝이 아니라 `chart_text`에 두고, 표 전환 뒤 원본은 `chart/text/ko.json`이다(라이브러리 결정 L4) |

## 확인

자동 test는 `luil_chart_tests`(`chart/tests`) 하나라 Windows와 Android 실기기에서 함께 돈다. core 밖이라 core의 플랫폼 hook 대역(`tests/core_platform_stub.cpp`)과 raster 도우미(`tests/raster_probe.cpp`)를 같은 파일 그대로 컴파일한다.

- `chart_scale_tests.cpp`. `nice_ticks`: 0~97은 칸 상한 5에서 0부터 20 걸음 6개, 음수 범위, 최소와 최대가 같은 범위, 1e-9와 1e12 크기, NaN 범위. `format_chart_number`: 자리 고정, 반올림, "-0", 1e15, 무한은 빈 글. 값과 픽셀의 왕복과 뒤집힌 축.
- `chart_text_tests.cpp`. 기본 문구와 요약·점 글의 조립. 값이 없는 요약, 계열 이름이 없는 점 글.
- `chart_element_tests.cpp`. 플롯 자리가 눈금 글 폭만큼 왼쪽을 비우는지(배율 1·1.5·2에서 같은 논리 자리), x 이름표 솎기, 칸 수 상한과 칸마다 맡은 범주, 폭 0 배치 뒤 폭 있는 배치에서 칸이 한 벌인지, 키(←·→·Home·End·Page·↑·↓, 끝의 빈 목록, Ctrl의 흐름, 고른 점이 없을 때 첫 점), 누름·끌기의 `focus` 메시지와 같은 점의 빈 목록, `focus`가 비면 Tab 자리 없음, 접근(root image 이름·요약, 플롯 slider 값 글, 기본 문구와 덮는 함수, 빈 점), 계열 표의 대비 3:1.
- 같은 파일의 raster 단언(`raster_probe`). 막대 높이가 값에 비례하는 픽셀, 선이 지나는 자리, 빈 점에서 끊김, 고대비에서 한 색과 점선 간격, 값 상자가 요소 안에 서는지. 글은 비워 글꼴에 매이지 않는다.
- 큰 자료. 픽셀 열 표가 치솟은 값과 끊김을 지키는지, 10만 점 선·영역·막대 계열 한 번 그리기가 상한 안인지(800 x 600, 배율 2, 세 번 가운데 가장 빠른 값). Debug Windows에서 잰 값은 선 38ms, 영역 83ms, 막대 6ms다. 선을 경로 하나로 그으면 326ms였고, 영역의 면까지 경로면 220ms였다. 상한은 200ms다. 잰 값의 두 배가 아닌 것은 같은 실행 파일이 아직 재지 않은 실기기에서도 돌기 때문이고, 경로 하나로 긋는 회귀는 이 상한이 잡는다.
- `chart_page_tests.cpp`. 두 데모의 차트 페이지가 내는 메시지와 상태 label, 종류 바꾸기와 큰 자료 켜기.
- UIA 형 번역은 기존 role(Image·Slider)이라 새 provider test가 없다.

데모 칸. 데스크톱 데모와 모바일 데모에 새 페이지 `chart`("차트")를 둔다(`examples/demo/chart_page.*`, `examples/mobile_demo/chart_page.*`). 차트 하나(owner `chart/sales`)에 선 계열 "매출"과 막대 계열 "비용", 1월~12월 범주를 싣는다. 종류 고르기(선·막대·영역, owner `chart/kind`), 큰 자료 켜기(10만 점, `chart/large`), 상태 label `chart/state`("고른 점: 매출, 3월, 120 · 계열 2 · 범주 12", 없으면 "고른 점: 없음"), 접근 요약 label `chart/summary`(root의 Value와 같은 글)를 둔다. 종류 고르기는 "매출" 계열의 꼴만 바꾼다. 페이지가 `format_value`로 정수 글을 주어 값 글이 "120"이다. 큰 자료를 켜거나 끄면 고른 점을 지운다. 모바일은 같은 owner에서 `chart/` 접두를 뺀다(상태 label은 `state`, 요약 label은 `summary`). 모바일 목록의 `차트` 칸(`kind_page_entry`, owner `chart`)이 button이고 Invoke가 `navigate_intent{chart}`를 내는지는 [mobile_demo_page_list_tests.cpp](../tests/mobile_demo_page_list_tests.cpp)가 항목 수(46행)만 바꿔 자동으로 잰다. 이름이 겹치지 않는지(61~66행)도 그대로 본다. 격리 데스크톱에서 Windows로 지은 모바일 데모를 띄우면 UIA로 이름 `차트`인 Button을 찾아 Invoke하고, 열린 페이지의 `state` label을 되읽는다. 격리 데스크톱에서 luil-uia로 데모를 띄워 root Image의 Value(요약)를 읽고, 플롯 Slider에 초점을 준 뒤 PostMessage 오른쪽 키로 Value와 label을 되읽는다. 고른 점이 없으면 첫 키가 첫 점("매출, 1월, 80")을 고르고 둘째 키가 "매출, 2월, 95"로 옮긴다. 실제 입력이 필요하면 먼저 요청하고 허락한 시간만 쓴다.

사람 확인: 밝은·어두운·고대비와 색각 이상 흉내에서 계열 구별, 배율 100%·150%·200%와 큰 글자에서 눈금 글 겹침과 잘림, Narrator가 요약과 점 글을 읽는 방식, 실기기에서 탭·훑기와 세로 스크롤의 충돌, 10만 점에서 hover와 창 크기 바꾸기의 체감.

## 범위 밖

- 둘째 y축, 로그 축, 수·시간 x축, 산점도, 쌓은 막대, 원형 차트(결정 E1).
- 확대·이동([zoom_view_element.h](../include/luil/ui/zoom_view_element.h) 연동), 이어 들어오는 자료, 자료 움직임.
- 범례를 눌러 계열 숨기기, 점을 끌어 값 바꾸기.
- 점마다 접근 노드와 UIA Grid·Table 패턴(결정 E4의 b·c).
- 숫자의 지역 서식(확정 G6 B 밖).
- 마크업 어휘의 차트 요소.
- Android 접근성 계층.

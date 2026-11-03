# 색 고르기

사용자가 채도·명도 판, 색상 막대, 알파 막대, 미리 정한 견본, 16진 칸으로 색 하나를 고른다. 값은 앱이 드는 HSV(`hsv_color`)이고 판·막대·키·견본·보조 기술이 모두 절대 메시지 하나(`change_to`)를 탄다. 본체는 인라인으로 놓거나, 닫힌 색 칸이 여는 popup에 싣는다. 공개 형식은 [color_picker_element.h](../include/luil/ui/color_picker_element.h)의 `color_picker_config`·`color_picker_element`·`color_field_config`·`color_field_element`, [color_picker_popup.h](../include/luil/app/color_picker_popup.h)의 `make_color_picker_popup`이다. 값 형과 16진 함수([color_space.h](../include/luil/ui/color_space.h))는 [색 공간과 16진 색](color-space-design.md)으로 나눴다. 한 문서가 250줄을 넘어서다.

값 조작은 [별점](rating-design.md)과 `slider_element`의 절대 메시지 규약을, 닫힌 칸과 popup은 [분할 버튼](split-button-design.md)의 "필드 popup 열림 규약"을, 기본 이름은 [기본 문구](default-text-design.md)의 헤더를 따른다.

## 문제

앱이 색 고르기를 만들려면 색 계산부터 그리기까지 모두 짓는다.

- 색 계산이 없다. HSV 변환과 공개 16진 함수가 없다(16진은 비공개로 스타일 읽기·검사기·데모가 각자 짓는다). [색 공간과 16진 색](color-space-design.md)이 푼다.
- 그라디언트를 그리는 요소가 없다. `src`에 `SkShader`를 쓰는 곳이 없고 slider 트랙은 단색이다. `SkShaders::LinearGradient`는 두 패키지의 `include/effects/SkGradient.h`에 있고, 그 기호가 Windows `skia.lib`와 Android `libskia.a`에 있음을 글자열로 확인했다(빌드는 하지 않았다).
- 2차원 끌기와 키가 없다. `slider_element`는 가로 한 축이고 끌기와 키는 델타다([slider_element.h](../include/luil/ui/slider_element.h) 11~16행). 판은 x·y 두 값을 절대로 낸다. 두 축 키 조합의 선례는 가상 목록 격자다([virtual_list_element.cpp](../src/ui/virtual_list_element.cpp) 540행 ←→ 처리기, 576행 세로 `key_step_target`).
- 보조 기술의 범위 값은 노드마다 하나다(`access_info::range`, [uia_provider.cpp](../src/win32/uia_provider.cpp) 942행). 판은 값이 둘이다.
- `accent_picker_element`는 카탈로그 강조색 id를 고르는 묶음이라 임의 색을 고르지 못한다([theme_picker_element.h](../include/luil/ui/theme_picker_element.h)).
- 요소는 앱에서 `ui_color`를 받지 않는다는 규칙이 있고 예외는 내용의 색(잉크 획)뿐이다([theming.md](concepts/theming.md) 72·74행). 고를 색은 내용이므로 예외를 넓혀야 한다.

앞 문서가 이 노드로 넘긴 것은 넷이다. [field_popup.h](../include/luil/ui/field_popup.h) 9행(색 고르개가 규약을 따른다), [split-button-design.md](split-button-design.md) 156행(메뉴가 아닌 popup의 키 경로), [rating-design.md](rating-design.md) 238행(`access_range`의 `step`·`page`), [message-box-design.md](message-box-design.md) 222행(데모 파일)이다. "다른 노드와의 약속" 끝 표에서 닫는다.

## 공개 형식

```cpp
// include/luil/ui/color_picker_element.h (치수는 논리 픽셀)
inline constexpr float color_picker_plane_size { 200.0f };
inline constexpr float color_picker_bar_height { 16.0f };
inline constexpr float color_picker_gap { 8.0f };
inline constexpr float color_picker_swatch_size { 32.0f };
inline constexpr float color_picker_preset_size { 24.0f };
inline constexpr float color_picker_hex_width { 96.0f };
inline constexpr float color_picker_hex_height { 28.0f };  // 견본 줄의 세로 가운데 (구현에서 더했다)
inline constexpr float color_picker_drag_start_distance { 6.0f };  // 물리 픽셀 (결정 5)

struct color_picker_preset { ui_color color { 0 }; std::u8string name {}; };  // 이름이 비면 16진 글
struct color_picker_hex
{
    ui_element_id id {};  // 앱 kind다. 앱 정책의 text_target_of가 편집 대상으로 옮긴다 (결정 2)
    text_input_view value {};  // 앱의 초안이다
    bool invalid { false };  // 앱이 check_hex_color로 정한다
    ui_action commit {};  // 같은 표면 안에서 칸을 떠날 때와 Enter(Android 확정 단추)에 낸다 (결정 8). popup이 닫힐 때는 앱이 dismiss에서 확정한다
    std::u8string name {};  // 비면 기본 문구
};
struct color_picker_config
{
    std::u8string owner {};
    hsv_color value {};  // 앱 상태다
    bool alpha { false };  // 알파 막대와 8자리 16진 (색 공간 결정 2)
    std::function<input_action(hsv_color value)> change_to {};  // 없으면 표시 전용
    std::optional<color_picker_hex> hex {};
    std::vector<color_picker_preset> presets {};  // 내용의 색이다 (결정 6)
    float plane_size { 0.0f };  // 0이면 200. 자리 폭보다 크면 줄인다
    float bar_height { 0.0f };  // 0이면 16. 터치 앱이 올린다
    std::u8string name {}, plane_name {}, hue_name {}, alpha_name {};  // 비면 기본 문구
};
class color_picker_element final : public ui_element
{
public:
    explicit color_picker_element(color_picker_config config);
    [[nodiscard]] static size_f size_for(const color_picker_config& config) noexcept;
    [[nodiscard]] static ui_element_id plane_id(std::u8string_view owner);  // popup 초점 요청의 대상 (결정 3)
    // arrange 뒤에 유효하다. 물리 좌표를 받고 트랙 밖은 끝으로 자른다. 끌기·클릭·test가 같은 함수를 쓴다.
    [[nodiscard]] hsv_color value_at_plane(float x, float y) const noexcept;
    [[nodiscard]] float hue_at(float x) const noexcept;
    [[nodiscard]] float alpha_at(float x) const noexcept;
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;
};

struct color_field_config
{
    std::u8string owner {};
    hsv_color value {};
    bool alpha { false };
    bool open { false };  // popup이 떠 있는지는 앱 상태다
    std::function<input_action(bool open)> set_open {};
    std::u8string name {};  // 비면 기본 문구
};
class color_field_element final : public ui_element  // 높이는 dropdown_height(28)
{
public:
    explicit color_field_element(color_field_config config);
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;
    [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;
};

// include/luil/app/color_picker_popup.h (make_menu_popup과 같은 꼴, 정의는 src/host/color_picker_popup.cpp)
inline constexpr float color_picker_popup_padding { 8.0f };  // 본체를 두르는 여백 (구현에서 더했다)
struct color_picker_popup_config
{
    std::u8string id {}, anchor {};
    float scale { 1.0f };
    field_popup_anchor below {};  // place_field_popup으로 x·y를 정한다
    std::function<input_action(popup_dismiss_reason)> dismiss {};
    std::optional<popup_corner> corner {};
};
[[nodiscard]] ui_popup make_color_picker_popup(color_picker_config picker, color_picker_popup_config popup);

// include/luil/ui/ui_element_id.h, end_of_library_kinds 바로 위 (frozen_col의 table_row_lane 뒤)
color_picker, color_picker_plane, color_picker_bar, color_picker_preset, color_field,
```

## 규칙

**구성.** root(`color_picker`)가 견본을 직접 그리고 `draw_children`을 부른다. 자식은 판(`color_picker_plane`), 색상 막대와 알파 막대(`color_picker_bar`), 견본 칸(`color_picker_preset`), 앱 id의 16진 칸(`text_input_element`, [combo_box_element.cpp](../src/ui/combo_box_element.cpp) 41행처럼 앱이 준 id를 쓴다)이다. owner는 `owner`, U+001F, `plane`·`hue`·`alpha`·견본 번호(1부터) 순이다. 세로로 판, 색상 막대, 알파 막대, 견본과 16진 칸의 줄, 견본 칸 줄이 `color_picker_gap`을 두고 선다. 견본 칸은 폭에 맞춰 다음 줄로 넘긴다. 비활성은 앱이 root에 거는 `set_enabled(false)`이고 자식이 arrange에서 따라간다(별점 결정 12).

**값과 메시지.** 메시지는 `change_to(hsv_color)` 하나다(결정 1). 부분은 자기 채널만 바꾸고 나머지는 그 frame의 `value`에서 옮긴다. 판은 채도와 명도, 막대는 색상이나 알파, 견본 칸은 `hsv_from_color(견본, value)`다. 끌기는 자기 채널의 지금 값을 읽지 않는다. 앞뒤 포인터의 값이 같으면 메시지가 없다. 키는 지금 값에서 한 걸음이고, 값이 격자 사이면 이웃 격자점으로 간다(별점과 같다). 클릭은 뗀 자리의 값이다. 범위는 `clamp_hsv`로 자르고 8비트 색은 `color_from_hsv`로만 짓는다. 16진 확정은 앱이 `hsv_from_color(색, 지금 값)`로 받아 회색·검정에서도 색상을 지킨다. 앱은 `change_to`를 받을 때 16진 초안을 `format_hex_color`로 다시 쓴다.

**입력.**

| 부분 | 포인터 | 키 |
| --- | --- | --- |
| 판 | 클릭·탭은 그 자리의 채도·명도다. 시작 거리를 넘기면 끌기이고 넘는 순간 누른 자리 값을 낸다 | ←→ 채도 1%, ↑↓ 명도 1%, PageUp·PageDown 명도 10%, Home·End 명도 0·100% |
| 색상 막대 | 같다(가로 자리의 색상) | ←→ 1°, PageUp·PageDown 10°, Home·End 0°·360° |
| 알파 막대 | 같다 | ←→ 1%, PageUp·PageDown 10%, Home·End 0·100% |
| 견본 칸 | 클릭·탭 | Enter·Space. 칸 사이는 ←→ |

- 판의 ↑↓·Page·Home·End는 세로 `key_step_target`(`up_increases`), ←→는 `set_key_handler`다. 가상 목록 격자의 조합이다. 막대는 가로 `key_step_target`이다.
- 끝에 닿은 키는 빈 목록으로 삼킨다. Ctrl·Alt를 든 키는 앱 단축키로 흐른다([값 조절](value-step-design.md)).
- 16진 칸은 같은 표면 안에서 떠날 때와 수식 키 없는 Enter에 `commit`을 낸다(결정 8). Enter는 칸의 키 처리기가 받는다. 한 줄 칸이 쓰지 않은 Enter가 조상 전파의 첫 자리인 칸에 온다.
- Tab 자리는 판, 색상 막대, 알파 막대, 16진 칸, 견본 칸 줄(가로 `set_focus_group`, 한 자리) 순이다. root는 자리가 아니다. 커서는 `ui_cursor`에 십자가 없어 손 모양이다.

**접근성.** 키보드 초점이 서는 노드가 곧 보조 기술이 보는 노드다.

- root는 `group`이고 이름은 `name`, 비면 기본 문구(색)다. 값 글은 `format_hex_color`라 UIA Value 패턴이 읽기 전용으로 선다(989행, `SetValue`는 1798행에서 `E_ACCESSDENIED`). 견본은 root가 그리고 노드가 아니다.
- 판은 `slider`, 이름 기본 "채도와 명도", 값 글 "채도 40%, 명도 80%"이고 범위는 없다(결정 4). RangeValue가 서지 않는다.
- 막대는 `slider`이고 범위는 색상 `{ 0, 360, h, 1, 10 }`, 알파 `{ 0, 100, a * 100, 1, 10 }`이다. 별점이 연 `step`·`page`(SmallChange·LargeChange, 1426~1456행)를 채운다. `set_value`는 그 채널만 바꾼 `change_to`이고 같은 값이면 빈 목록, `change_to`가 없으면 nullopt라 읽기 전용이다.
- 견본 칸은 `button`이고 이름은 `name`, 비면 16진 글이다. 16진 칸은 `edit`이라 UIA `SetValue`가 앱의 편집 대상을 탄다.

**그리기·테마·고대비.** 판은 색상의 순색을 칠하고, 가로 흰색에서 투명한 흰색으로, 세로 투명에서 검정으로 그라디언트 둘을 얹는다. 결과가 sRGB 부호 값에서 HSV 식 `v * ((1 - s) * 흰색 + s * 순색)`과 같다. 색상 막대는 일곱 정지점(빨강, 노랑, 초록, 청록, 파랑, 자홍, 빨강)이다. 알파 막대와 견본은 4 논리 픽셀 체커(흰색, #CCCCCC) 위에 색을 얹는다.

- 판·막대·견본·견본 칸은 내용의 색이라 모든 테마와 고대비에서 실제 색이다(theming.md 74행의 예외를 넓힌다). 고대비에서 그림을 덮을지 요소가 고르는 선례는 `palette_scope_element.cpp` 27행(`in_high_contrast`)이다. 둘레 1px은 `control_border`(고대비에서 `window_foreground`, ui_theme.cpp 245행), 초점 테는 `ui_tree::draw_focus_ring`이다.
- 손잡이는 원(판, 반지름 7)과 세로 막대(막대, 폭 6)다. 안쪽 2px 테는 손잡이 아래 색이 `is_dark_background`면 흰색, 아니면 검정이고 바깥 1px은 반대 색이다(강조색 견본의 체크 색 판정, theme_picker_element.cpp 107행). 어느 색 위에서도 보인다.
- 비활성은 내용을 40% 불투명으로, 손잡이를 `disabled_foreground`로 그린다. 움직임이 없어 움직임 줄이기와 무관하다. 치수는 논리 픽셀이고 그릴 때 배율을 곱한다.

**색 칸과 popup.** `color_field_element`는 `dropdown_element`의 모양이다. 견본 사각형(18), 16진 글, 펼침 화살표를 그린다.

- 접근 역할 `combo_box`, 이름 기본(색), `expanded = open`, 값은 16진 글이다. expand·collapse는 `set_open(목표)`다. 클릭은 `set_open(!open)`, 키는 `field_popup_key_handler(open, set_open, true)`, 열린 동안 root에 `set_focus_leave_action`을 단다(분할 버튼 규약).
- `make_color_picker_popup`이 `size_for`로 크기를 정하고, tree를 `scale`로 배치하고, `place_field_popup`으로 자리를 놓는다. 크기는 논리 상수의 합이라 글 폭을 재지 않는다. R2 메뉴 폭 고침(배율로 잰 글 폭)이 막은 잘림은 이 popup에 생기지 않는다.
- 열 때 앱은 `focus_request { surface = popup id, target = plane_id(owner) }`를 싣는다. 다른 표면으로 옮긴 초점은 떠남으로 치지 않아([ui_interaction.cpp](../src/ui/ui_interaction.cpp) 4464~4467행) 칸이 스스로 닫히지 않는다. Tab은 초점이 선 표면의 tree에서 돈다(4061행). Esc 닫기(`escape_key`)에서는 앱이 칸으로 돌아오는 초점 요청을 한 번 더 싣는다. popup이 사라지면 초점은 지워질 뿐이다. 다른 닫는 계기에서는 돌리지 않는다. 사용자가 누른 자리가 초점을 가진다.
- 닫는 계기는 분할 버튼 표 그대로다. 16진 칸을 실은 popup은 글을 치는 popup이라 앱이 `surface_moved`·`activation_changed`에 빈 액션을 줘 남긴다. popup이 사라지면 그 tree가 없어 초점 떠남 훅이 서지 않으므로(ui_interaction.cpp 4460~4461행) 앱은 닫는 메시지를 처리할 때 읽히는 16진 초안을 확정한다. 한 번에 하나만은 앱의 열린 필드 owner 하나(`open_popup_`)로 지킨다.
- 열린 동안 칸을 다시 누르면 X-4 규칙(줄 3, "펼친 요소의 클릭은 누를 때의 액션")이 닫는다. 칸이 `expanded`를 알리므로 이 노드가 할 일은 없다.

## 결정

색 모델(HSV, `hsv_color`를 앱이 든다)과 8자리 순서(`#RRGGBBAA`)는 [색 공간과 16진 색](color-space-design.md)의 결정 1·2다.

**1. 메시지 모양.** 질문: 부분마다 메시지를 따로 두는가.

- 길 a: `change_to(hsv_color)` 하나. 되는 것: 앱 처리기가 하나이고 견본·16진·보조 기술이 같은 길이다. 대가: 판을 끄는 메시지가 build 때의 색상·알파를 담는다. 같은 frame 안에서 두 부분을 함께 조작하면 앞의 것이 덮인다.
- 길 b: `set_hue`·`set_plane(s, v)`·`set_alpha` 셋. 되는 것: 채널끼리 덮지 않는다. 대가: 앱 처리기 셋, 견본은 넷을 다 낸다.
- 길 c: slider처럼 델타. 되는 것: 선례가 있다. 대가: 2차원에서 델타가 쌓이고 절대 값을 앱이 다시 자른다.
- 권장: a. 대가: 위의 덮기다. 한 사람이 두 부분을 같은 frame에 다루는 일은 드물다. 권장대로 진행.

**2. 16진 칸.** 질문: 16진 칸을 컨트롤이 품는가.

- 길 a: 앱이 준 id(앱 kind)로 `text_input_element`를 품는다. 거르기·검사·쓰기는 순수 함수다. 되는 것: 콤보의 선례대로 자리·폭·오류 테두리·접근 이름이 한 컨트롤 안에서 서고 popup에 한 덩어리로 실린다. 날짜 칸(R4)의 시·분 칸도 같은 길을 쓸 수 있다. 대가: 앱은 여전히 초안을 들고 정책 `text_target_of`에 한 줄, `apply_text_edit`에 `hex_color_filter`를 준다. 설정 칸이 하나 늘고 popup에 IME 칸이 선다.
- 길 b: 순수 함수만 주고 앱이 칸을 옆에 둔다(숫자 칸의 선례). 되는 것: 요소가 글 편집과 무관하다. 대가: 앱마다 칸 자리·이름·popup 크기를 맞추고, 16진 칸이 popup에 들어갈 자리를 앱이 계산한다.
- 길 c: 16진 없음. 되는 것: 요소와 앱 모두 글 편집이 없다. 대가: 정확한 색을 넣을 길이 보조 기술에도 없다.
- 권장: a. 대가: 위의 설정 칸과 앱의 한 줄이다. 승인: 길 a (2026-11-03).

**3. 놓는 자리와 popup 초점.** 질문: popup을 지원하는가, 열린 동안 키보드 초점은 어디에 두는가.

- 길 a: 인라인 본체만. 되는 것: 새 popup helper와 칸이 없다. 대가: 칸에 붙여 여는 흔한 꼴을 앱이 `ui_popup`·자리·초점까지 짓는다.
- 길 b: 인라인 본체, 색 칸, `make_color_picker_popup`, 열 때 초점을 판으로 옮긴다. 되는 것: 열자마자 화살표가 판을 움직이고 화면 읽기가 판을 읽는다. 초점 요청은 popup 표면을 받고(데모 명령 팔레트, [palette.cpp](../examples/demo/palette.cpp) 105행) Android도 overlay에서 이행한다([android_app.cpp](../src/android/android_app.cpp) 1434행). controller를 고치지 않는다. 대가: popup이 사라지면 초점이 지워지므로 Esc 뒤 칸으로 돌리는 요청을 앱이 싣는다. kind 하나와 helper 헤더 하나가 는다.
- 길 c: b와 같되 초점을 칸에 두고 칸의 키 처리기가 판을 움직인다(메뉴 꼴). 되는 것: 되돌릴 초점이 없다. 대가: 막대·견본·16진 칸에 키로 닿지 못하고 보조 기술은 칸만 읽는다.
- 권장: b. 날짜 칸(R4)이 이 초점 규약을 따른다. 대가: 위의 되돌림 요청이다. 승인: 길 b (2026-11-03).

**4. 판의 접근성.** 질문: 값이 둘인 판을 어떻게 알리는가.

- 길 a: `slider` 한 노드에 두 값을 글로 알리고 범위는 없다. 되는 것: 초점 노드와 읽히는 노드가 같고 공유 파일을 고치지 않는다. 값을 정하는 보조 기술 길은 막대와 16진 칸이다. 대가: 판을 숫자로 직접 정할 수 없다. 범위 없는 slider를 Narrator가 어떻게 읽는지는 재지 못했다.
- 길 b: 명도를 범위로, 두 값을 글로. 되는 것: 명도는 SetValue가 된다. 대가: 채도는 여전히 글뿐이고 한 노드의 범위가 한 축만 말해 헷갈린다.
- 길 c: 판은 none으로 접고 채도·명도 막대를 화면에 더한다. 되는 것: 모든 값이 RangeValue다. 대가: 막대가 둘 늘어 화면이 커지고 판과 같은 값을 두 번 보인다.
- 권장: a. 대가: 위. 권장대로 진행.

**5. 터치 시작 거리.** 질문: 판과 막대를 누르는 순간 조작하는가.

- 길 a: 세 부분 모두 시작 거리 6 물리 픽셀(별점과 같다). 되는 것: 터치에서 판 위 쓸기가 감싼 창을 흘리고, 탭과 클릭이 그 자리 값이다([touch-pen-input-design.md](touch-pen-input-design.md) 154행). 대가: 마우스로 눌러도 떼거나 6을 넘기 전까지 손잡이가 오지 않는다. 판을 끌려면 터치에서 잠깐 눌러야 한다.
- 길 b: slider처럼 0. 되는 것: 누르는 순간 움직인다. 대가: 판이 화면을 크게 덮는 모바일에서 판 위의 쓸기가 스크롤하지 못한다.
- 길 c: 판 6, 막대 0. 되는 것: 막대는 즉각이다. 대가: 규칙이 둘이고 막대 위 쓸기도 스크롤하지 못한다.
- 길 d: 마우스·펜은 0, 터치만 6(포인터 종류로 가른다). 되는 것: 마우스로 누르는 순간 손잡이가 오고 터치의 쓸기는 창을 흘린다. 대가: 지금은 가를 수 없다. `pointer_drag_target::start_distance`는 칸 하나이고 터치는 그 픽셀 값 대신 0보다 큰지만 본다([ui_element.h](../include/luil/ui/ui_element.h) 369~379행). 가르려면 그 공유 형식과 `ui_interaction.cpp`에 포인터 종류별 칸을 더해야 하고, 두 파일은 줄 3의 것이다.
- 권장: a. 대가: 위의 지연이다. 실기기에서 확인한다. 권장대로 진행.

**6. 미리 정한 견본.** 질문: 견본 칸 줄을 컨트롤에 두는가.

- 길 a: `presets`(색과 이름)를 설정에 둔다. 비면 줄이 없다. 되는 것: 자주 쓰는 색, 원래 색 되돌리기를 앱이 첫 견본으로 준다. 대가: 요소가 앱의 `ui_color`를 받는 둘째 자리라 내용의 색 예외가 넓어진다.
- 길 b: 두지 않는다. 되는 것: 요소가 앱 색을 받는 자리가 고를 값 하나뿐이다. 대가: 같은 줄을 앱마다 단추로 짓는다.
- 길 c: 강조색 고르기와 합친다. 되는 것: 강조색과 임의 색을 한 곳에서 고른다. 대가: `appearance`의 강조색 id 계약을 건드린다.
- 권장: a. 대가: 위. 권장대로 진행.

**7. 판 그리기.** 질문: 판과 막대를 무엇으로 그리는가.

- 길 a: Skia 선형 그라디언트. 되는 것: 크기·배율과 무관하게 한 번에 그리고 식이 HSV와 같다. 대가: 저장소 첫 셰이더라 두 렌더러(Vulkan·CPU)에서 색이 같은지 처음 잰다.
- 길 b: CPU에서 색상마다 그림을 지어 `ui_image`로 그린다. 되는 것: 셰이더가 없다. 대가: 색상이 바뀔 때마다 판 크기의 픽셀을 다시 지어 끌기마다 비용이 든다.
- 권장: a. 대가: 위의 첫 실측이다. 구현 첫 걸음에서 두 렌더러를 잰다. 어긋나면 b로 간다. 권장대로 진행.

**8. 16진 칸의 Enter.** 질문: 칸을 떠나지 않고 확정하는 키를 두는가(구현에서 정했다).

- 길 a: 수식 키 없는 Enter와 Android 확정 단추(`text_input_config::submit`)가 `commit`을 낸다. 되는 것: 글을 치고 Enter로 곧바로 색이 바뀐다. 칸에 머물러 이어 고칠 수 있다. 대가: 칸에 초점이 있는 동안 Enter가 기본 단추로 가지 않는다.
- 길 b: 떠날 때만 확정한다. 되는 것: 규칙이 하나다. 대가: popup 안에서 확정하려면 Tab을 쳐야 하고, Android 확정 단추는 초점을 옮기지 않아 확정하지 못한다.
- 권장: a. 대가: 위의 기본 단추다. 권장대로 진행.

## 플랫폼

**Win32.** `win32_window.cpp`·`window_surface.cpp`·`uia_provider.cpp`·`accessibility.h`를 고치지 않는다. UIA는 root에 Value(읽기 전용), 판에 Value, 막대에 RangeValue(SmallChange·LargeChange 포함), 색 칸에 ExpandCollapse와 Value를 낸다. popup은 활성화를 받지 않는 별도 창이라 키는 앵커 창에서 오고 논리 초점이 popup tree로 보낸다. popup 안 16진 칸의 IME는 [popup-ime-design.md](popup-ime-design.md)의 길이다.

**Android.** Java 소스는 0줄이다. 코어 요소와 overlay라 JNI도 unsupported도 없다. 시스템 대화를 부르지 않고 같은 요소를 그린다.

- 시작 거리와 스크롤을 가르는 허용치는 Android 설정의 `touch_slop`이다([android_settings.cpp](../src/android/android_settings.cpp) 120행). 터치 앱은 `bar_height`를 올리고 `plane_size`는 폭에 맞춰 준다.
- 그라디언트가 Vulkan과 CPU에서 같은지는 실기기에서 본다(결정 7). 소프트 키보드에서 16진 칸은 일반 키보드다.
- Android의 luil에는 접근성 계층이 없다. 접근성 규칙은 Win32 UIA 기준이다.

## 다른 노드와의 약속

- **frozen_col(같은 줄 앞).** 겹치는 파일은 `ui_element_id.h`와 `src/ui/inspect_names.cpp`뿐이고 같은 줄이라 차례로 쌓는다. kind 다섯은 `table_row_lane` 뒤, `end_of_library_kinds` 바로 위에 두고 `library_kind_name`의 case는 같은 커밋에 끝에 더한다. [ui_element_tests.cpp](../tests/ui_element_tests.cpp) 84행 `last[]`는 이 노드가 `color_field`를 더했다. 병합 때 다른 줄의 마지막 kind와 함께 다시 맞춘다. kind 숫자를 test·UIA 스크립트에 박지 않는다.
- **줄 3(X-3·X-4·menu_bar).** `ui_interaction.cpp`·`.h`를 고치지 않는다. 색 칸이 `expanded`를 알려 X-4를 받는다. 열린 칸을 누르고 게시가 낀 뒤 떼는 경우의 test는 X-4가 들어온 R3 통합 때 통합 커밋이 `tests/color_picker_element_tests.cpp`에 더한다. 줄 3의 `menu-focus-fixes-design.md`(통합 뒤 이 폴더에 선다) 76행이 색 고르개는 따로 할 일이 없다고 적었다. 견본 칸 줄은 묶음이라 X-3이 대표가 아닌 칸의 Tab을 고친다. menu_bar의 kind 둘과 `menu_bar_name`도 같은 두 파일과 `default_text` 끝에 붙어 병합은 기계적이다.
- **줄 2(rich_edit).** 그 줄의 파일(`text_edit.h`, `text_document.h`, `text_area_element.*`, `text_layout.*`, 클립보드)을 고치지 않는다. `text_input_element`는 그대로 쓴다. `default_text.h/.cpp` 끝에 함께 붙는다.
- **줄 4(tray·hotkey).** `win32_window.cpp`·`app_host.h`·`app_delegate.h`·`ui_platform.h`를 고치지 않는다. 새 헤더 `color_picker_popup.h`는 `menu_popup.h` 곁의 새 파일이다.
- **default_text.** 끝에 `color_picker_name()`(색), `color_picker_plane_name()`(채도와 명도), `color_picker_plane_value(s, v)`("채도 40%, 명도 80%", 백분율은 lround 정수), `color_picker_hue_name()`(색상), `color_picker_alpha_name()`(불투명도), `color_picker_hex_name()`(16진 색)을 더한다.
- **date_picker(R4).** 결정 3의 초점 규약, 결정 2의 칸 품기, 판의 두 축 키 조합을 따른다.
- **데모.** 라이브러리 커밋과 데모 연결 커밋을 나누고 연결은 통합 때 직렬로 쌓는다. 갤러리 `build_controls`의 별점 카드 뒤, `examples/demo/popups_page.*`의 `open_popup_`, 모바일 `basics_page.cpp`의 별점 줄 뒤, 16진 칸의 앱 kind는 `examples/demo/common.h` 끝과 [text_targets.h](../examples/demo/text_targets.h)다. 갤러리와 popups 페이지가 칸을 하나씩 가져 kind와 편집 대상이 둘이다(편집 메시지가 대상으로만 갈려 한 kind면 앞 페이지가 다 가져간다).

**미룬 것을 닫는다.** 이 단계에서는 기존 문서를 고치지 않고 구현 커밋이 고친다.

| 위치 | 닫는 방법 |
| --- | --- |
| field_popup.h 9행 | 색 칸이 규약을 따른다. 주석은 그대로다 |
| split-button-design.md 156행 | 메뉴가 아닌 popup은 초점을 popup으로 옮긴다고 이 문서(결정 3)로 잇는다 |
| rating-design.md 238행 | 막대가 `step`·`page`를 채운다. 공유 파일은 고치지 않는다 |
| message-box-design.md 222행 | 데스크톱은 `basics_page` 대신 갤러리와 popups 페이지에 싣고 `common.h`에는 kind 한 줄만 더한다. 모바일 `basics_page`는 msg_box·rating 연결 뒤에 쌓는다 |
| theming.md 74행 | 내용의 색 예외에 고를 색과 견본 칸을 더한다 |
| components.md | 색 고르기 행을 더한다 |

## 확인

자동 test는 UIA 하나만 빼고 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다.

- 색 계산의 test(`tests/color_space_tests.cpp`)는 [색 공간과 16진 색](color-space-design.md)에 있다.
- `tests/color_picker_element_tests.cpp`(새 파일). 자식 id와 owner, `size_for`, 배율 2의 `value_at_plane` 네 모서리와 밖 자르기, 클릭은 그 자리 값을 한 번, 시작 거리 안의 뗌은 클릭이고 넘으면 누른 자리 값과 이동 값을 내며 같은 값의 이동은 메시지가 없다. 판 끌기 메시지가 build 때의 색상·알파를 담는다. 키는 표의 걸음, 끝의 빈 목록, Ctrl 조합 nullopt. `change_to`가 없으면 Tab 자리도 끌기도 없다. 비활성에서 말이 없다. 견본 칸이 `hsv_from_color`를 낸다. 16진 칸이 앱 kind이고 같은 표면 안에서 떠날 때 `commit`을 낸다. popup이 닫힌 경우는 데모 test가 닫는 메시지에서 초안이 확정되는지 잡는다. 색 칸의 클릭·Alt+↓·F4·↓와 열린 동안의 초점 떠남 훅. 열린 동안 칸을 누르고 게시 없이 떼면 `set_open(false)`다. 게시가 낀 경우는 X-4가 들어온 R3 통합 때 통합 커밋이 같은 파일에 더한다. `make_color_picker_popup`의 크기와 아래 자리, 뒤집기.
- `tests/accessibility_tests.cpp`에 tree 모양(root group 아래 판·막대·16진·견본 칸), 막대의 범위와 `step`·`page`, `set_value`가 그 채널만 바꾸는지. `tests/default_text_tests.cpp`에 여섯 함수. `tests/ui_element_tests.cpp`의 `last[]`는 이 노드가 `color_field`로 맞춘다.
- `tests/raster_draw_tests.cpp`. 배율 2에서 판의 왼쪽 위 흰색, 오른쪽 위 순색, 아래 검정, 가운데 `color_from_hsv({ h, 0.5, 0.5 })`를 허용치 2로(투명 정지점이 검정이면 가운데가 회색으로 틀린다), 색상 막대의 정지점 색, 알파 막대의 체커, 손잡이 테가 밝은 색 위 검정·어두운 색 위 흰색인지, 고대비 팔레트에서 판이 실제 색이고 둘레가 `window_foreground`인지.
- `tests/uia_provider_tests.cpp`(`luil_tests`, Windows). 판은 Value만 있고 RangeValue가 없다. 막대의 SmallChange·LargeChange가 1·10이다. root의 `SetValue`는 `E_ACCESSDENIED`다. 색 칸의 Expand가 `set_open(true)`다.

데모 칸. 갤러리 컨트롤 페이지의 "색 고르기" 카드가 알파와 견본 넷과 16진 칸을 단 인라인 본체(owner `gallery/color`)와 label `gallery/color-value`("색: #3366FF80 · 색상 225° · 채도 80% · 명도 100% · 알파 50%". 220°는 #3377FF라 처음 값을 225°로 둔다)를 보인다. 이 label의 16진 글은 `format_hex_color`라 대문자여서, 같은 데모의 `style_color_text`(소문자 `#rrggbb[aa]`, 테마 페이지의 스타일 파일 상태 줄)와 글을 견주지 않는다. popups 페이지는 색 칸(owner `popups/color-field`)과 label `popups/color-state`("색 칸: 닫힘 · #3366FF")를 둔다. 모바일 기본 페이지는 별점 줄 뒤에 폭에 맞춘 판과 `bar_height` 32의 본체(owner `color`)와 label `color-value`를 더한다. 격리 데스크톱에서 luil-uia로 막대 `SetValue`, 16진 칸 `SetValue`, root와 label 되읽기, 색 칸 Expand 뒤 판에 초점이 섰는지, Esc 뒤 칸으로 돌아왔는지를 본다. 실제 입력이 필요하면 먼저 요청하고 허락한 시간만 쓴다. 두 데모 칸의 조립과 intent는 `tests/demo_gallery_tests.cpp`와 `tests/mobile_demo_page_tests.cpp`가 잡는다.

사람 확인: 판과 막대의 끌기 느낌과 마우스 누름의 지연, 실기기에서 판 위 쓸기가 스크롤하고 잠깐 눌러 끄는 느낌, Vulkan·CPU에서 그라디언트 색, 밝은·어두운·고대비에서 손잡이와 둘레, Narrator가 판의 두 값과 막대를 읽는 방식, 배율 150%·200%에서 popup 자리.

## 범위 밖

- 화면에서 색 뽑기(스포이트), OKLCH·HSL 판, 넓은 색역, 색 이름.
- RGB 숫자 칸. 앱이 `number_input_range { 0, 255 }`로 둔다.
- 이전 색 견본과 사용자 견본 저장. 앱이 첫 견본으로 원래 색을 준다.
- 강조색 고르기와 합치기, 고른 색으로 강조색 만들기. `accent_picker_element`를 대신하지 않는다.
- 세로 막대 배치와 원형 색상환. Android 접근성 계층.

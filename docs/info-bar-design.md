# 정보 막대

화면 안에 남아 있는 상태 알림을 라이브러리가 짓는다. 토스트는 시간이 지나면 사라지고 정보 막대는 사용자가 닫거나 앱이 지울 때까지 남는다. 심각도 글리프, 제목, 줄이 바뀌는 본문, 동작 단추 하나, 닫기 단추를 담는다. 공개 형식은 [info_bar_element.h](../include/luil/ui/info_bar_element.h)의 `info_bar_config`·`info_bar_element`와 [severity.h](../include/luil/ui/severity.h)의 `ui_severity`다.

이 노드는 심각도 표현의 소유자이기도 하다. 토스트 안에 갇힌 심각도 색과 글리프를 공용으로 꺼내고, 토스트와 뒤에 오는 msg_box가 그것을 쓴다. 공통 부분은 비공개 [severity_paint.h](../src/ui/severity_paint.h)의 `severity_color`·`severity_glyph`다.

## 문제

앱이 화면 안에 남는 알림을 만들려면 토스트를 흉내 내거나 전부 직접 짓는다.

- `toast_element`는 상주형으로 쓸 수 없다. `toast_stack_element`가 높이를 `toast_height`(40)로 고정하고 본문은 한 줄로 줄인다(`draw_text_within`). 닫기 단추가 없고, 동작 단추는 `set_tab_stop(false)`라 키보드로 누를 수 없다([toast_element.cpp](../src/ui/toast_element.cpp)). 스스로 사라지는 알림에 초점이 얹혀 증발하지 않게 한 선택이다.
- 심각도 색·글리프가 `toast_element.cpp`의 익명 namespace 함수 `severity_color`·`severity_glyph`에 갇혀 있다. 배지는 두 심각도의 글리프를 `badge_paint.cpp`의 `tone_glyph`에 따로 적었다. 같은 경고가 자리마다 달라지고, 고대비 규칙을 한쪽만 지킬 수 있다.
- 줄이 바뀌는 본문의 높이를 행 stack이 답하지 못한다. 가로 stack은 자식에게 폭 제한 없이 크기를 묻는다([stack_element.cpp](../src/ui/stack_element.cpp)의 `preferred_size`). 글리프·`wrap` label·단추를 행으로 묶으면 label은 한 줄 높이를 답하고, 담는 쪽의 `add_fit`이 그 높이로 자리를 잡아 줄 바뀐 글이 넘친다. 앱은 `measure_text_block`으로 폭을 빼 가며 높이를 직접 센다.
- 접근 이름에 심각도가 없다. 토스트의 이름은 본문뿐이라 화면을 보지 않는 사용자는 경고와 정보를 가를 수 없다.
- 나타나는 순간을 보조 기술에 알릴 길이 없다. `access_role::alert`는 UIA Text로만 번역되고(`uia_provider.cpp`의 `control_type_for`), 새 요소는 `diff_access_snapshots`에서 부모의 structure 변화로 접힌다. UIA Notification은 입력 칸 오류 글에만 쓴다(`announce_changes`의 `after.error`).

## 공개 형식

```cpp
// include/luil/ui/severity.h
enum class ui_severity { info, success, warning, error };

// include/luil/ui/toast_element.h (severity.h를 include한다)
using toast_severity = ui_severity;  // 옛 이름이다. toast_severity::info 같은 값 이름이 그대로 통한다.
```

```cpp
// src/ui/severity_paint.h (비공개, badge_paint.h 모양)
// 지금 토스트가 쓰는 값 그대로다. info는 보조 글자색, success는 키 컬러, warning·error는 각자의 강조색이다.
[[nodiscard]] ui_color severity_color(const ui_color_palette& palette, ui_severity severity) noexcept;
// info·pass·warning·error codicon이다.
[[nodiscard]] char32_t severity_glyph(ui_severity severity) noexcept;
```

```cpp
// include/luil/ui/info_bar_element.h (severity.h와 text_block.h를 include한다. 비공개 배치 결과가 줄 구간을 든다)
inline constexpr float info_bar_minimum_height { 40.0f };  // 논리 픽셀
inline constexpr float info_bar_close_size { 24.0f };
// 동작 단추를 옆에 둘 때 글에 남아야 하는 폭이다. 모자라면 단추가 글 아래로 간다.
inline constexpr float info_bar_side_action_minimum_text { 200.0f };

struct info_bar_config
{
    // 막대와 두 단추의 owner다.
    std::u8string owner {};
    ui_severity severity { ui_severity::info };
    // 한 줄이다. 길면 끝을 줄인다. 비면 줄이 서지 않는다.
    std::u8string title {};
    // 폭에서 줄을 바꾼다. 규칙은 label의 `wrap`과 같다.
    std::u8string message {};
    // 본문의 최대 줄 수다. 0이면 제한이 없다. 넘치면 마지막 줄의 끝을 줄인다.
    std::size_t max_lines { 0 };
    // 글과 액션이 모두 있어야 동작 단추를 만든다.
    std::u8string action_label {};
    ui_action action {};
    // 있어야 닫기 단추를 만든다. 열려 있는지는 앱 상태다.
    ui_action close {};
    // 닫기 단추의 툴팁이자 접근 이름이다. 비면 기본 문구("닫기")다.
    std::u8string close_tooltip {};
    // 접근 이름 앞에 붙는 심각도 낱말이다. 비면 기본 문구(정보·성공·경고·오류)다.
    std::u8string severity_name {};
    // 닫기 단추의 한 변과 동작 단추의 높이다(논리 픽셀). 0이면 `info_bar_close_size`와 `text_button_height`다.
    float control_size { 0.0f };
};

struct info_bar_size
{
    float height { 0.0f };  // 논리 픽셀
    bool action_below { false };  // 동작 단추가 글 아래 줄에 서는가
};

class info_bar_element final : public ui_element
{
public:
    explicit info_bar_element(info_bar_config config);
    // 그 폭(논리 픽셀)에서 막대가 차지할 높이와 단추 자리다. arrange·draw와 같은 규칙이다.
    [[nodiscard]] static info_bar_size measure(const info_bar_config& config, const measure_context& context, float width);
    // arrange·draw·accessibility는 아래 규칙을 따른다.
    // 폭이 정해지지 않았으면(가로 stack의 자식) 답하지 않는다. 폭은 받은 폭이고 높이는 `measure`의 것이다.
    [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;
};
```

```cpp
luil::info_bar_config bar {};
bar.owner = u8"sync-error";
bar.severity = luil::ui_severity::error;
bar.title = u8"동기화하지 못했습니다";
bar.message = u8"서버에 닿지 않습니다. 연결을 확인한 뒤 다시 시도하세요.";
bar.action_label = u8"다시 시도";
bar.action = luil::make_message_action(retry_intent {});
bar.close = luil::make_message_action(dismiss_intent {});
column.add_fit(std::make_unique<luil::info_bar_element>(std::move(bar)));  // 폭에서 높이를 잰다
```

## 규칙

**구성.** root(`info_bar`)는 `access_role::alert` 몸통이다. 바탕·테·글리프·제목·본문은 root가 `draw_text_block`으로 직접 그린다. 접근 tree에 글 노드가 따로 서지 않는다. hit를 흡수한다(`set_hit_opaque`, 토스트와 같다). 자식은 최대 둘이고 owner는 셋 다 `config.owner`다.

- `info_bar_action`: `text_button_element`(normal). `action_label`과 `action`이 둘 다 있을 때만 만든다. 하나만 있으면 눌러도 아무 일 없는 단추라 만들지 않는다.
- `info_bar_close`: `button_element(codicons::icon_close, button_visual_role::toolbar)`. `close`가 있을 때만 만든다.
- 둘 다 손 모양 커서이고 **Tab 자리다.** 토스트가 비운 까닭(곧 사라지는 알림)이 상주 알림에는 없다.
- kind 셋은 `ui_element_id.h`의 `end_of_library_kinds` 바로 위에 더한다.

**그리기.** 치수는 논리 픽셀이고 그릴 때 배율을 곱한다.

- 바탕은 `raised_background` 둥근 사각형(반지름 6)이고 테는 심각도 색의 1px 선이다. 글리프는 16 칸에 심각도 색으로 그린다. 제목은 semibold, 본문은 `body_text` 굵기이고 둘 다 `primary_foreground`다. 제목 굵기를 스타일 `title`이 아니라 고정하는 까닭은 그 기본이 regular라 본문과 갈리지 않아서다.
- 안쪽 가로 여백 12, 글리프와 글 사이 10, 오른쪽 부품 사이와 글 앞 간격 8이다. 맨 오른쪽 부품은 오른쪽 가장자리에서 8, 부품이 없으면 글이 12를 비운다.
- 글리프는 늘 그린다. 색만으로 뜻을 말하지 않는다(`badge_element::shows_glyph`와 같은 규칙).
- 움직임이 없다. 나타나고 사라지는 것은 앱이 tree에 싣고 빼는 것이라 움직임 줄이기와 무관하다.

| 심각도 | 테·글리프 색 | 고대비의 색 | 글리프 |
| --- | --- | --- | --- |
| info | `secondary_foreground` | 창 전경색 | `icon_info` |
| success | `accent` | hotlight | `icon_pass` |
| warning | `warning_accent` | 창 전경색 | `icon_warning` |
| error | `error_accent` | 창 전경색 | `icon_error` |

고대비는 `raised_background`를 창 배경으로 접고 info·warning·error의 색을 같게 접는다(`high_contrast_palette_for`). 테가 경계를, 글리프가 뜻을 남긴다.

**높이와 줄바꿈.** `measure`와 arrange가 같은 식을 쓴다. arrange는 `slot.width / scale`로 다시 잰다(`label_element::arrange`와 같다).

- 글 폭은 폭에서 왼쪽 몫(12 + 16 + 10)과 오른쪽 몫(닫기, 옆에 선 동작 단추)을 뺀 값이다. 제목은 한 줄이고 끝을 줄인다. 본문은 `wrap`이고 `max_lines`를 따른다.
- 동작 단추는 옆에 둘 때 글 폭이 200 미만이면 글 아래 줄로 간다. 글의 왼쪽에 맞추고 본문과 8 떨어진다. 폭은 `text_button_element::preferred_size`가 답한다.
- 위·아래 여백은 `max(8, (최소 높이 - 한 줄 높이) / 2)`이다. 최소 높이는 `max(40, control_size)`다. 그래서 한 줄 막대는 최소 높이이고(한 줄 높이가 24 이하일 때) 첫 줄이 가운데 선다.
- 글리프와 닫기 단추, 옆에 선 동작 단추는 첫 줄의 가운데에 맞춘다. 여러 줄이면 막대 가운데가 아니라 첫 줄에 붙는다.
- 높이는 담는 쪽 몫이다. 모자라게 담으면 아래가 넘쳐 그려지고 자르지 않는다. 맞춤 담기(`add_fit`)가 규칙이다.
- 좁은 막대의 경계는 자른다. 글 폭이 0 이하이면 본문을 재지 않고(한 글자 폭으로 접어 막대가 끝없이 길어지지 않는다) 그리지 않는다. 글 아래로 간 동작 단추는 오른쪽 가장자리 안(8 안쪽)으로 줄인다. `control_size`가 0 이하이거나 유한하지 않으면 정하지 않은 것으로 읽는다.

**접근성.**

- root는 `alert`이고 이름은 심각도 낱말, 제목, 본문을 공백으로 이은 글이다(빈 것은 건너뛴다). 줄인 것과 상관없이 전체 글이다.
- 단추 둘은 `button`이다. 이름은 `action_label`과 `close_tooltip`이고 Invoke를 낸다. UIA에서는 Text 아래에 Button이 선다(토스트와 같은 모양).
- 닫으면 앱이 막대를 짓지 않고, 막대 안에 있던 초점은 거둬진다([keyboard-focus-design.md](keyboard-focus-design.md)). 다음 Tab은 처음부터다. 어디로 옮길지는 앱이 `ui_frame::focus`로 정한다([deferred-focus-design.md](deferred-focus-design.md)).
- Esc는 처리하지 않는다. 막대는 초점 밖에서도 남는 글이다.

## 결정

**1. 심각도 표현.** 질문: 막대가 심각도를 어떤 모양으로 말하는가.

- 길 a: 중립 바탕에 심각도 색 테·글리프. 되는 것: 팔레트·생성기·스타일 JSON을 건드리지 않아 R1에 서고, 고대비가 바탕을 접어도 테와 글리프가 남는다. 대가: 옅은 색 면으로 심각도를 알리는 모양보다 눈에 덜 띈다.
- 길 b: 심각도별 옅은 바탕. 되는 것: 멀리서도 심각도가 색 면으로 읽힌다. 대가: `accent_tones`에 톤 하나와 `ui_color_palette`에 파생 역할 넷(`ui_theme.h`), `compose_palette`(`ui_theme.cpp`), `generate_style.cmake`의 톤 목록, `assets/style.json`의 `tones`, `ui_style_tests.cpp`가 닿는다. 줄 2의 syntax(역할 추가)와 style_sheet(생성기 표)가 같은 곳을 고쳐서 그 뒤로 미뤄야 한다. 이 노드의 팔레트 부분이 R1을 벗어난다. 고대비에서는 이 바탕도 접혀 a와 같아진다.
- 길 c: 기존 `notice_background` 재사용. 되는 것: 팔레트를 건드리지 않는다. 대가: 안 된다. 값이 분홍 계열(light `#fde7e9`, dark `#422427`)이라 info·success에 쓰면 오류처럼 읽힌다. 토스트 test(`toast_element_tests.cpp`)가 같은 까닭으로 이 색을 쓰지 않음을 잠가 두었다.
- 권장: a. 팔레트를 건드리지 않고 고대비에서도 뜻이 남는다. 대가: 위의 눈에 띄는 정도다. 옅은 바탕이 필요해지면 b를 줄 2의 팔레트 작업 뒤에 따로 낸다.
- 승인: 길 a (2026-11-02).

**2. 심각도 타입과 색·글리프 helper.** 질문: 심각도 타입과 색·글리프 helper를 어디에 두는가.

- 길 a: 새 공개 `ui_severity`와 비공개 `severity_paint.h`. `toast_severity`는 별칭으로 남긴다. 되는 것: 막대·토스트·msg_box가 한 타입과 한 그림을 쓴다. 별칭이라 데모 호출부가 그대로 서고, [theming.md](concepts/theming.md)의 링크는 새 이름으로 옮겼다. 대가: 같은 타입에 이름이 둘 남는다. 별칭을 언제 지울지는 정하지 않았다.
- 길 b: `toast_severity`를 그대로 쓰고 막대가 `toast_element.h`를 include한다. 되는 것: 이름도 파일도 그대로다. 대가: 막대와 msg_box의 설정이 토스트의 이름과 헤더에 기댄다.
- 길 c: 막대에 복제한다. 되는 것: 다른 파일을 건드리지 않는다. 대가: 고대비 규칙이 갈라진다.
- 권장: a. 이름만 바뀌고 동작은 같다. 대가: 공개 헤더 하나와 공개 타입 이름 하나가 늘고, 옛 이름은 별칭으로 남는다. 권장대로 진행.

**3. 막대를 짓는 방식.** 질문: 막대를 어떻게 짓는가.

- 길 a: `info_bar_element` 한 클래스와 정적 `measure`. 되는 것: 폭에서 높이를 한 곳이 답한다. 데스크톱 데모는 `stack_item`의 맞춤(`fit`)으로 담되 `page_layout`이 페이지 높이를 `length`로 세므로 `measure`의 높이를 `length`로도 주고, 모바일 데모는 `page_column`이 높이만 받으므로 `measure`의 높이로 담는다. 대가: 클래스 하나가 그리기와 두 단추의 자리를 모두 안다.
- 길 b: `make_info_bar`가 stack을 조립한다. 되는 것: 클래스가 늘지 않는다. 대가: 위 문제의 까닭으로 줄 바뀐 높이를 답하지 못한다. 앱이 높이를 센다.
- 길 c: 부품(글리프 칸, 본문)을 공개해 앱이 조립한다. 되는 것: 모양을 앱이 마음대로 짠다. 대가: 같은 문제를 앱마다 푼다.
- 권장: a. 폭에서 높이를 한 곳이 답한다. 대가: 클래스 하나가 그리기와 두 단추의 자리를 모두 안다. 권장대로 진행.

**4. 본문 줄바꿈.** 질문: 본문을 줄바꿈하는가.

- 길 a: 한 줄 말줄임만(토스트). 되는 것: 높이가 고정이라 담는 쪽이 재지 않는다. 대가: 상주 알림은 긴 글이 본뜻인데 잘린다. 접근 이름만 전체 글이다.
- 길 b: `wrap`과 `max_lines`(기본 0, 제한 없음). 되는 것: 글이 잘리지 않고 앱이 상한을 건다. 대가: 높이가 폭에 달려 창 크기와 회전에서 바뀐다. 담는 쪽이 `add_fit`이나 `measure`로 재야 한다. 폭을 받아 매번 짓는 페이지는 바로 따라간다.
- 권장: b. 상주 알림의 긴 글이 잘리지 않는다. 대가: 위의 높이가 폭에 달린다는 점이다. 권장대로 진행.

**5. 동작 단추 자리.** 질문: 동작 단추를 어디에 두는가.

- 길 a: 항상 오른쪽(토스트와 같다). 되는 것: 높이가 단추에 달리지 않는다. 대가: 좁은 화면에서 단추가 글 폭을 먹는다.
- 길 b: 항상 글 아래. 되는 것: 글 폭이 폭에만 달린다. 대가: 넓은 화면에서도 막대가 한 줄 높다.
- 길 c: 폭에 따라 정한다(글 폭이 200 미만이면 아래). 되는 것: 넓으면 막대가 한 줄로 낮게 서고 좁으면 글 폭이 지켜진다. 대가: 높이가 폭에 따라 두 갈래로 갈린다.
- 권장: c. 갈리는 경계는 순수 함수(`measure`의 `action_below`)가 정하고 test가 잠근다. 대가: 위의 두 갈래 높이다. 권장대로 진행.

**6. 나타남 알림.** 질문: 나타나는 순간을 보조 기술에 알리는가.

- 길 a: `alert` 역할과 이름만(토스트와 같다). 되는 것: 공유 파일을 건드리지 않는다. 대가: 화면 읽기 사용자는 막대가 나타나는 순간을 듣지 못하고 탐색하다 만나야 안다.
- 길 b: 나타날 때 UIA Notification. 되는 것: 입력 칸 오류가 쓰는 길을 그대로 쓴다. 바뀌는 것: `access_change_kind`에 "나타남" 갈래, `diff_access_snapshots`가 새로 생긴 이름 있는 `alert`에 내기, `uia_event_sink::notification`의 활동 id(`uia_provider.cpp`에 `luil.input-error`로 박혀 있다)를 인자로 올리기, `announce_changes` 한 갈래다. 첫 발행본은 기준선이라(`window_surface.cpp`) 처음부터 있던 막대는 읽지 않는다. 토스트도 `alert`라 함께 읽힌다. 대가: 공개 enum이 늘고, `access_role`을 더하는 노드·rich_edit와 같은 파일을 만진다.
- 길 c: LiveSetting 속성과 LiveRegionChanged 이벤트(SDK에 `UIA_LiveSettingPropertyId`, `UIA_LiveRegionChangedEventId`가 있다). 되는 것: UIA의 정식 live region 모양이다. 대가: `access_info`에 필드가 늘고 provider가 속성을 답해야 하며, 읽기 프로그램마다 읽는지 사람이 확인해야 한다.
- 권장: a. 토스트와 막대에 함께 걸리는 공용 동작이라 이 노드의 병합과 섞지 않는다. 승인이 나면 b를 같은 줄에서 별 커밋으로 낸다. 대가: 위 a의 대가다.
- 승인: 길 a (2026-11-02). 길 b는 승인되지 않아 만들지 않았다. 나타남을 읽게 하려면 b를 따로 설계한다.

**7. 단추 크기.** 질문: 단추 크기를 어떻게 여는가.

- 길 a: `control_size` 한 칸. 0이면 닫기 24, 동작 단추 28. 되는 것: 터치 앱이 올리면 최소 높이도 따라 오른다. 대가: 두 단추의 크기를 따로 못 준다.
- 길 b: `app_bar_metrics`처럼 구조체. 되는 것: 닫기와 동작 단추를 따로 정한다. 대가: 필요한 값이 하나뿐인데 칸이 늘어 읽기만 나빠진다.
- 길 c: 고정 24·28. 되는 것: 설정이 없다. 대가: 손가락에 작고 판정 영역을 넓히는 장치가 없다.
- 권장: a. 터치 앱이 한 값으로 단추와 최소 높이를 함께 올린다. 대가: 위의 두 단추 크기를 따로 못 주는 점이다. 권장대로 진행.

## 플랫폼

**Win32.** 막대는 UIA Text, 단추는 Button(Invoke)이다. `win32_window.cpp`는 건드리지 않고, 결정 6이 a면 `uia_provider.cpp`도 건드리지 않는다. 고대비는 위 표를 따른다.

**Android.** Java 소스는 0줄이다. 그리기·hit가 모두 공용 층의 element라 JNI도 unsupported도 없다.

- 터치 앱은 `control_size`를 키운다. 모바일 데모는 `touch_height`(44)를 준다. hover가 없어 눌림 강조만 선다.
- 접근성은 Android에서 소비되지 않는다([number-input-design.md](number-input-design.md)와 같은 까닭). 심각도 낱말과 알림은 Win32 한정이다.
- 회전하면 폭이 바뀌어 높이가 달라진다. 폭을 받아 매번 짓는 페이지가 다시 잰다.

## 다른 노드와의 약속

- **msg_box**(R2)는 `ui_severity`와 `severity_paint.h`를 그대로 쓴다. 대화 상자 머리의 글리프 칸(24칸에 크기 20)과 접근 이름 앞에 붙는 심각도 낱말(화면에는 그리지 않는다)은 [확인 대화 상자 문서](message-box-design.md)가 정했다.
- **toast**는 모양을 그대로 두고 `severity_color`·`severity_glyph` 호출만 공용 helper로 바꾼다. 기존 raster test가 지킨다.
- **teaching_tip**(R4)은 단추 모양을 맞춘다. 닫기는 `button_element`(`icon_close`, role `toolbar`, 손 모양 커서, Tab 자리)이고 동작은 `action_label`과 `action` 쌍이다.
- **syntax·style_sheet**(줄 2)는 결정 1이 b일 때만 `ui_theme.h`·`ui_theme.cpp`·`generate_style.cmake`·`style.json`에서 겹친다. a면 겹치지 않는다.
- **os_notify**(줄 4 R2)와 `examples/demo/toasts_page.*`를 함께 고친다. 이 노드는 카드를 더하기만 한다.
- **split_button**과 같은 줄이라 병합 순서가 정해져 있다. `ui_element_id.h`의 kind 추가가 같은 자리이고 AutomationId는 kind의 수다. test와 luil-uia 스크립트는 번호를 박지 않는다.
- 기본 문구("닫기", 심각도 낱말)는 [default-text-design.md](default-text-design.md)의 헤더로 옮겼다. 분할 버튼의 기본 문구와 같은 취급이다.
- `badge_paint.cpp`의 `tone_glyph`를 helper로 잇는 정리는 이 노드에서 하지 않는다.

## 확인

- `tests/info_bar_element_tests.cpp`(고정 폭 측정기):
  - 동작 단추는 글과 액션이 둘 다 있을 때만, 닫기는 `close`가 있을 때만 서고 kind·owner가 맞는지;
  - 두 단추가 Tab 자리이고 클릭이 `action`·`close` 메시지를 내며, 몸통이 hit를 흡수하는지;
  - `measure`: 한 줄은 최소 높이, 폭을 좁히면 줄 수만큼 높이가 늘고, `max_lines`가 상한이고, 제목이 한 줄을 더하고, `control_size` 44가 최소 높이를 올리고, 높이가 배율과 무관한지;
  - 글 폭 200이 `action_below`를 가르고 아래일 때 8과 단추 높이가 더해지는지, 폭이 정해지지 않으면 `nullopt`인지;
  - arrange: 한 줄이면 닫기와 동작 단추가 막대 가운데, 여러 줄이면 첫 줄 가운데, 닫기가 오른쪽에서 8, 동작 단추가 닫기 왼쪽 8 또는 글 아래 8, 배율 2의 물리 좌표. 글리프는 root가 직접 그려 따로 bounds가 없으므로 같은 첫 줄 식을 쓰는 닫기의 가운데로 잠근다;
  - `accessibility()`: alert, 이름이 심각도 낱말·제목·본문 순(빈 것 건너뜀, `severity_name` 우선), 단추 이름.
  - 측정기는 코드포인트마다 글자 크기의 절반 폭을 재고 글은 공백으로만 나뉜 ASCII다. 줄 나눔 규칙(uax14가 나중에 바꾼다)과 무관하게 줄 수가 정해진다.
- `tests/severity_paint_tests.cpp`: 네 심각도의 색과 글리프가 토스트가 쓰던 값 그대로인지, 고대비 팔레트에서 info·warning·error의 색은 같고 글리프는 모두 다른지.
- `tests/toast_element_tests.cpp`: `toast_severity`가 `ui_severity`와 같은 타입인지.
- `tests/ui_element_tests.cpp`: 라이브러리 kind의 `last[]`에 앞 노드 분할 버튼의 마지막 kind(`split_button_arrow`)와 이 노드의 마지막 kind(`info_bar_close`)를 더해 앱 대역과 겹치지 않는지.
- `tests/raster_draw_tests.cpp`: 네 심각도의 테 색(info의 보조 글자색은 알파가 있어 바탕과 섞인 값), 밝은·어두운·고대비에서 바탕과 테가 남는지, 닫기 hover와 동작 단추(옆·아래)가 그려지는지, 바탕이 `raised_background`인지. raster 축은 글꼴을 싣지 않아 글리프 픽셀은 볼 수 없다. 글리프가 네 심각도마다 다르다는 것은 `severity_paint_tests.cpp`가 값으로 잠그고, 그려지는 모양은 사람 확인이다.
- `tests/accessibility_tests.cpp`(토스트 test 옆): 접근 tree에서 막대 아래 단추 둘이 서는지. `tests/uia_provider_tests.cpp`: 막대가 Text로 이름을 내고 단추 Invoke가 메시지를 나르는지. 결정 6이 a라 나타남 알림 test는 없다. b가 승인되면 나타난 `alert`가 알림 한 번을 내고 첫 발행본은 내지 않는지, 토스트도 읽히는지를 더한다.
- 데모(`examples/demo`, `examples/mobile_demo`의 토스트 페이지): "정보 막대" 카드에 네 심각도 막대(제목, 긴 본문, 동작 단추, 닫기)를 놓는다. 심각도별 "정보 막대: 열림/닫힘" 꼴의 label과 "마지막 동작: …"을 그려 UIA가 되읽고, 닫으면 다시 여는 단추가 선다. 격리 데스크톱에서 luil-uia로 막대 노드의 이름과 두 단추의 Invoke를 돌린다. `demo_gallery_tests.cpp`·`mobile_demo_page_tests.cpp`가 페이지 짓기를 지킨다.
- 사람 확인: Narrator의 막대 읽기(결정 6이 b면 나타나는 순간까지), 밝은·어두운·고대비 테마에서 네 심각도 구분, 실기기에서 닫기·동작 단추의 탭 크기, 긴 글 줄바꿈과 회전 뒤 높이.

## 범위 밖

- 닫은 뒤 초점 이동. 앱의 `ui_frame::focus`다.
- 시간이 지나 저절로 닫히는 막대. 토스트의 몫이거나 앱이 시각으로 지운다.
- 동작 단추 둘 이상, 링크가 든 본문, 사용자 지정 글리프, 나타나고 사라지는 움직임.
- 여러 막대를 쌓는 컨테이너. 앱의 세로 stack이다.
- 심각도 색·글리프 helper의 공개. 앱은 팔레트 역할을 쓴다.
- 심각도별 옅은 바탕(결정 1의 b)과 나타남 알림(결정 6의 b). 승인이 나면 따로 설계한다.

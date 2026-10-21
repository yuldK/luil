# UI 공백 보강 구현 인계 (Codex 트랙)

luil의 범용 UI 공백 보강을 Claude와 Codex가 반씩 나눠 구현한다. 이 문서는 Codex가 맡는 일과 두 트랙이 만나는 계약을 정한다. 설계의 근거와 목업은 별도 설계 보고서 "luil 범용 UI 보강안"에 있다. 그 보고서는 비공개 링크라 필요하면 사용자에게 공유를 요청한다. 이 문서만 읽어도 작업할 수 있도록 필요한 내용을 옮겨 적었다.

- 기준 커밋: `dcd29088ee44411cb4d82bfe6aa006888c28a44e`
- 비교 대상: Qt 6.8.0 공개 API, WPF 공식 문서와 소스
- 목표: 앱이 기본 컨트롤을 버리고 선택·키보드 입력·배치·그리기·접근성을 직접 구현하는 상황을 줄인다.

## 1. 확정된 결정

### 1.1 Backspace 단위

- ←·→·Delete·클릭 위치·낱말 선택은 **글자소(extended grapheme cluster)** 경계를 따른다.
- Backspace도 글자소 하나를 지운다. 예외는 하나다. 글자소의 마지막 코드포인트가 **결합 부호**(일반 범주 Mn·Mc·Me)이면 그 코드포인트 하나만 지운다.
  - `e` + U+0301: Backspace 한 번에 악센트만 지워지고 `e`가 남는다.
  - 이모지 시퀀스(ZWJ, 이체 선택자 U+FE0E·U+FE0F, 피부색 수식자 U+1F3FB–U+1F3FF, keycap U+20E3), 국기(RI 쌍), 조합형 한글 자모(L·V·T)는 결합 부호 예외에 들지 않는다. 시퀀스 전체가 한 번에 지워진다.
- IME 조합 중인 구간은 이 규칙 밖이다. 조합 중 caret과 삭제는 IME가 정한다.
- 결정과 근거를 `docs/concepts/text-editing.md`에 남긴다. 그 문서의 "grapheme cluster 편집 계약 아님" 문장은 고친다.

### 1.2 키 전파 규칙

**가장 작은 단위가 먼저 받고, 소비하지 않은 키는 상위 UI로 전파한다.** 마지막에 남은 키는 앱 정책(`interaction_policy::on_key`)이 받는다.

- 목록의 Ctrl+A는 목록이 받는다. 초점이 목록 행 안의 버튼에 있어도, 그 버튼이 Ctrl+A를 소비하지 않으면 목록까지 올라간다.
- 텍스트 칸에 초점이 있으면 Ctrl+A는 칸이 먼저 소비한다(칸 전체 선택). 지금 동작 그대로다.
- 이 규칙은 Codex가 만드는 모든 새 요소에도 적용한다. 쓰지 않는 키는 반드시 "소비하지 않음"(`std::nullopt`)으로 돌려줘서 위로 흐르게 한다. 빈 vector는 "소비했다"는 뜻이다(기존 `key_step_target`과 같은 규약).
- 지금 `interaction_controller::process_key`는 다음 순서로 키를 나눈다(`src/ui/ui_interaction.cpp:2727`).
  1. 메뉴
  2. 실행 키(Space·Enter)
  3. Tab
  4. 텍스트 칸
  5. 확대 보기
  6. 값 단계(`process_step_key`)
  7. 묶음(`process_group_key`)
  8. 끌기 취소 Esc
  9. 모달 Esc
  10. 기본 버튼 Enter
  11. 앱 정책

  초점이 선 요소 자신만 보고 조상은 보지 않는다.
- Claude가 C0 단계에서 **조상 전파 단계**를 넣는다. 끌기 취소 Esc(8번) 다음, 모달 Esc(9번) 앞에서 초점 요소부터 루트까지 조상을 차례로 묻는다. 계약은 5.1절에 있다.

### 1.3 팝업에 앵커하는 팝업

이번 범위에 넣는다. 하위 메뉴를 위해 필요하며 Codex 트랙 X4가 맡는다. 지금 `ui_popup::anchor`는 표면 이름(주 창 또는 `ui_window::id`)만 받는다. 팝업은 팝업에 앵커할 수 없다(`include/luil/app/app_host.h:82`, `docs/popup-ime-design.md`).

## 2. 배분

| 트랙 | 맡는 영역 | 이유 |
| --- | --- | --- |
| **Claude** | 키 전파 기반, 선택, 목록·테이블, 삼상 체크, 접근성 역할과 대규모 목록 접근성, 내용 크기 배치, 항목 모델 도우미 | `virtual_list_element`·`check_element`·`uia_provider`의 역할·패턴 표, controller의 단계·묶음·휠 경로를 한 사람이 쥔다 |
| **Codex** | 텍스트 편집 경계, 문서 보기, label 정렬, 가로 스크롤 막대, 스피너, 가운데 버튼, 메뉴 항목·하위 메뉴·명령표, 팝업 앵커, 편집형 콤보, 파일 대화상자, (장기) UI 텍스트 shaping | `text/`·`text_area`·`label`·`menu`·popup 표면·플랫폼 대화상자를 한 사람이 쥔다 |

## 3. 전체 순서

같은 행의 두 트랙은 서로 기다리지 않는다. "기다림" 열에 적힌 것만 다른 트랙을 기다린다.

| 시점 | Claude 트랙 | Codex 트랙 | 기다림 |
| --- | --- | --- | --- |
| M1 | **C0** 공통 기반: 키 조상 전파, 접근성 역할 추가, 가로 휠 축 수정, 가상 목록 계측 | **X0** 공용 작은 보강: 가로 스크롤 막대, label 정렬·가운데 말줄임, 스피너 요소, 가운데 버튼 | 없음 |
| M2 | **C1** 키보드 선택 문맥과 목록 Ctrl+A, **C2** 삼상 체크 | **X1** 글자소 경계, **X2** 문서 보기 1단계, **X3** 메뉴 항목 종류 | X2는 X0의 가로 막대를 쓴다(같은 트랙) |
| M3 | **C3** 테이블 1단계 | **X4** 팝업 앵커 + 하위 메뉴 + 명령표, **X5** 문서 보기 2단계 | C3은 X0(가로 막대, label 정렬·말줄임)을 쓴다 |
| M4 | **C4** 테이블 2단계, **C5** 대규모 목록 접근성 | **X6** 편집형 콤보, **X7** 파일 대화상자 | X6은 C0(조상 전파)을 쓴다 |
| M5 | **C6** 내용 크기 배치, **C7** 항목 모델 도우미 | **X8** UI 텍스트 shaping (설계 문서 먼저) | C7은 X0(스피너)을 쓴다. X8은 X1·X5 뒤 |
| M6 | **C8** 예제 "변경 작업대" 조립 | X2·X5·X6·X4 부품을 예제에 맞춰 보완 | 두 트랙의 M1–M4 |

**Codex의 우선순위:** X0은 Claude의 C3(테이블)과 C7이 기다린다. X0을 가장 먼저 내보낸다. 그다음은 X1, X2, X3 순이다.

## 4. 파일 소유권

### 4.1 Codex가 쥐는 파일

- `include/luil/text/*`, `src/text/*` (새 `text/grapheme.h` 포함)
- `include/luil/ui/text_area_element.h`, `src/ui/text_area_element.cpp`
- `include/luil/ui/dialog_elements.h`, `src/ui/dialog_elements.cpp` (text_input, text_button)
- `include/luil/ui/label_element.h`, `src/ui/label_element.cpp`
- `include/luil/ui/draw_primitives.h`, `src/ui/draw_primitives.cpp`
- `include/luil/ui/scrollbar_element.h`, `src/ui/scrollbar_element.cpp`
- `include/luil/ui/menu_element.h`, `src/ui/menu_element.cpp`
- `include/luil/ui/dropdown_element.h`, 새 콤보 요소 파일
- popup 경로: `src/host/popup_*`, `src/win32/popup_surface.*`, `src/host/overlay_input.*`
- `src/win32/surface_input.cpp` (가운데 버튼)
- 파일 대화상자: `src/win32/` 아래 새 파일, `app_host.h`의 해당 필드
- `src/ui/ui_interaction.cpp` 안의 `process_menu_key`·`process_text_input_key`와 텍스트 편집 함수들

### 4.2 Claude가 쥐는 파일

- `include/luil/ui/virtual_list_element.h`, `list_element.h`, `grouped_list_element.h`와 각 구현
- `include/luil/ui/check_element.h`와 구현
- 새 `table_element`, `selection_model.h`, 배치 컨테이너(`grid_element` 등), 항목 모델 도우미
- `src/ui/ui_interaction.cpp` 안의 `process_step_key`·`process_group_key`·휠 라우팅·새 조상 전파 단계
- `include/luil/ui/accessibility.h`의 `access_role` enum과 `src/win32/uia_provider.cpp`의 역할 → ControlType 표

### 4.3 함께 고치는 파일

`ui_element.h`, `ui_events.h`, `ui_interaction.h`, `accessibility.h`, `uia_provider.cpp`, `app_host.h`, `luil.h`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/components.md`, `docs/README.md`가 여기 든다.

- 추가만 한다. 기존 줄의 순서·형식을 바꾸지 않는다.
- 자기 기능의 줄은 한곳에 모은다(목록 끝 또는 관련 항목 바로 아래).
- `uia_provider.cpp`에 Codex가 더하는 것은 다음 셋이다.
  - Text 패턴의 읽기 전용(X2)
  - 메뉴 항목의 Toggle·SelectionItem(X3)
  - ComboBox 동작(X6)

  역할 enum과 ControlType 표는 Claude의 C0 이후에 그 위로 더한다.
- 충돌이 나면 나중에 합치는 쪽이 해결한다. 상대 트랙의 기능 코드를 다시 쓰지 않는다.

## 5. 두 트랙 사이의 계약

### 5.1 Claude → Codex: 키 조상 전파 (C0)

Codex의 콤보(X6)가 쓴다. 이름은 C0 PR에서 확정되면 이 문서를 갱신한다.

```cpp
// ui_element.h
// 초점이 이 요소이거나 그 자손일 때, 더 안쪽이 소비하지 않은 키를 받는다.
// nullopt = 소비하지 않음 (위로 흐른다), 빈 vector = 소비했고 낼 메시지 없음.
using key_handler = std::function<std::optional<std::vector<input_action>>(const key_pressed_event&)>;
void set_key_handler(key_handler handler);
[[nodiscard]] const key_handler* key_handler_of() const noexcept;
```

- **불리는 자리:** 끌기 취소 Esc 다음, 모달 Esc 앞이다.
- **전파 순서:** 초점 요소부터 루트까지 조상 순서다. 처음으로 `std::nullopt`가 아닌 값을 돌려준 요소에서 멈춘다.
- **비활성 요소:** 묻지 않고 건너뛴다.
- **C0이 늦을 때:** 콤보 내부에서 policy hook 없이 처리할 방법이 없으면, 그 부분만 C0 이후로 미루고 나머지를 먼저 진행한다.

### 5.2 Claude → Codex: 접근성 역할 (C0)

`access_role`에 다음 역할이 더해진다.

- `tree`, `tree_item`
- `table`, `data_item`, `header_item`
- `tool_bar`, `separator`, `status_bar`

메뉴 구분선을 접근성에 내보내려면 `separator`를 쓴다.

### 5.2a 진행 상황 (Claude 트랙)

- C0·C1·C2·C3(1차)이 `claude/pensive-dirac-ufbj2q`에 들어갔다. 5.1절의 이름(`key_handler`, `set_key_handler`, `key_handler_of`)과 자리는 그대로 확정됐다.
- 키 단계에 `key_step_context`·`on_step_with`가 생겼다. Codex가 새로 만드는 값 컨트롤은 `on_step`만 쓰면 지금과 같다.
- 표(`table_element`)는 X0을 기다리지 않고 들어갔다. 칸 정렬은 표 자체의 `column_align`으로 그린다. X0이 들어오면 Claude가 표에 가로 막대(5.3절)와 가운데 말줄임(5.4절)을 붙인다. X0의 `text_align`과 이름이 겹치지 않게 표는 `column_align`을 쓴다.
- `access_info`에 `mixed`가 생겼다. Codex의 메뉴 체크 항목(X3)이 Toggle을 낼 때 `toggle_state_of`(uia_provider.cpp)를 같이 쓰면 된다.

### 5.3 Codex → Claude: 가로 스크롤 막대 (X0)

Claude의 테이블(C3)이 쓴다.

```cpp
enum class scrollbar_axis { vertical, horizontal };
struct scrollbar_config
{
    // 기존 필드 그대로. content_height·viewport_height는 축이 가로일 때 폭으로 읽는다
    // (이름을 바꾸지 않는다 — 호환).
    scrollbar_axis axis { scrollbar_axis::vertical };
};
```

- **키 단계:** 가로 막대는 ←·→·Home·End·Page를 받는다(`key_step_target::axis = horizontal`).
- **접근성:** UIA RangeValue와 ScrollBar 방향(Orientation)을 낸다.
- **배치:** `thumb_top()`·`thumb_height()`에 가로 대응 접근자를 더하거나 축 중립 이름을 더한다. 기존 이름은 지우지 않는다.

### 5.4 Codex → Claude: label 정렬과 말줄임 (X0)

Claude의 테이블 셀(C3)이 쓴다.

```cpp
enum class text_align { start, center, end };
enum class text_elide { end, middle };
struct label_config
{
    // 기존 필드 그대로
    text_align align { text_align::start };
    text_elide elide { text_elide::end };
};
// draw_primitives.h: 가운데 말줄임
[[nodiscard]] std::u8string elide_text_middle(std::u8string_view text, float max_width, const SkFont& font);
```

- 가운데 말줄임은 경로용이다. 앞 절반과 뒤 절반을 남기고 가운데에 `…`을 넣는다. 자르는 자리는 코드포인트 경계여야 하고, X1 이후에는 글자소 경계여야 한다.
- 오른쪽 정렬 숫자 열을 위해 `end` 정렬은 글 폭을 재서 오른쪽 padding에 붙인다.

### 5.5 Codex → Claude: 스피너 요소 (X0)

Claude의 항목 모델 도우미(C7)가 "불러오는 중" 행에 쓴다.

```cpp
struct spinner_config { std::u8string name {}; float size { 16.0f }; };  // name은 보조 기술이 읽는 이름
class spinner_element final : public ui_element { /* draw_spinning_glyph + next_update */ };
```

- 접근성 역할은 `progress`이고 범위 값은 없다.
- 보이지 않으면 다시 그리기를 예약하지 않는다.

## 6. Codex 작업 단위

모든 단위는 기존 공개 API에 **추가만** 한다. 새 필드를 쓰지 않는 앱은 그리기·메시지·키 동작이 바뀌지 않아야 하고, 이를 회귀 테스트로 잠근다.

### X0. 공용 작은 보강

**범위**

- 가로 스크롤 막대(5.3절).
- label 정렬·가운데 말줄임(5.4절).
- 스피너 요소(5.5절).
- 가운데 버튼:
  - 표면은 이미 `WM_MBUTTONDOWN·DBLCLK·UP`을 받는다(`src/win32/window_surface.cpp:523`).
  - 입력 변환기는 왼쪽·오른쪽만 옮긴다(`src/win32/surface_input.cpp:24`).
  - `pointer_button::middle`을 더하고 변환기에서 옮긴다. 컨트롤러는 가운데 버튼을 클릭·끌기로 해석하지 않고 그대로 흘려보낸다. 웹뷰 전달은 바꾸지 않는다.
  - X1·X2 버튼은 이번에 넣지 않는다.

**수용 기준**

- 가로 막대:
  - 세로 막대와 같은 키 단계·끌기·RangeValue 규약을 따른다.
  - 기존 세로 막대 테스트는 그대로 통과한다.
- label:
  - start·center·end 정렬과 끝·가운데 말줄임 raster가 light·dark·고대비에서 통과한다.
  - 기본값 label의 raster가 바뀌지 않는다.
- 스피너:
  - 보이는 동안만 `next_update`를 예약한다.
  - 접근성 역할은 progress이고 범위가 없다.
- 가운데 버튼:
  - left·right 경로와 웹뷰 전달이 바뀌지 않는다.
  - 가운데 버튼은 기존 요소의 클릭을 일으키지 않는다.
- `docs/components.md`에 스피너를 더한다.

### X1. 글자소 경계

**범위**

- 새 공개 헤더 `include/luil/text/grapheme.h`를 둔다.
  ```cpp
  [[nodiscard]] std::size_t grapheme_previous(std::u8string_view text, std::size_t offset) noexcept;
  [[nodiscard]] std::size_t grapheme_next(std::u8string_view text, std::size_t offset) noexcept;
  ```
- **끈 구성**(`LUIL_ENABLE_TEXT_SHAPING` 꺼짐)은 내장 UAX #29 부분 집합을 쓴다. 다음 규칙으로 충분하다. 필요한 Unicode 표는 생성 스크립트나 상수 표로 넣고 출처와 Unicode 판을 적는다.
  - 결합 부호(Mn·Mc·Me)
  - ZWJ + Extended_Pictographic
  - 이체 선택자
  - 피부색 수식자
  - RI 쌍
  - 한글 L·V·T·LV·LVT
  - CR LF
- **켠 구성**은 플랫폼 ICU 또는 libgrapheme 경계를 쓴다(`docs/vector-image-design.md`의 공장과 같은 경로). 두 구성은 같은 테스트를 통과해야 한다.
- `src/text/text_edit.cpp`의 left·right·delete·backspace·place_caret·select_word를 경계에 맞춘다.
  - 지금은 코드포인트 단위다(`src/text/text_edit.cpp:25`의 `previous_boundary`).
  - Backspace는 1.1절 정책을 따른다.

**수용 기준**

- 끝에서 ← 한 번을 누르면 다음 네 견본 모두 caret이 0이다.
  - `u8"é"`
  - `u8"\U0001F468‍\U0001F469‍\U0001F467‍\U0001F466"`
  - `u8"각"`
  - `u8"\U0001F1F0\U0001F1F7"`

  지금은 각각 1, 21, 6, 4다(기준 커밋에서 실행해 확인).
- Backspace를 누르면 `é`은 `e`가 남고, 나머지 셋은 빈 글이 된다.
- 포인터 클릭 위치와 더블 클릭 낱말 선택이 글자소 경계로 맞춰진다.
- 켠 구성과 끈 구성이 같은 경계 테스트를 통과한다.
- IME 조합 구간과 `text_input_view`의 조합 필드는 바뀌지 않는다.
- 기존 `tests/text_edit_tests.cpp`가 모두 통과한다.
- `docs/concepts/text-editing.md`를 갱신한다(1.1절).

### X2. 문서 보기 1단계 (읽기 전용 `text_area`)

**범위**

`text_area_config`(`include/luil/ui/text_area_element.h:61`)에 다음을 더한다.

```cpp
bool read_only { false };
bool wrap { true };                     // false면 가장 긴 줄 폭이 내용 폭
text_face face { text_face::ui };       // ui | code (draw_context::code_typeface)
float font_size { 0.0f };               // 0이면 text_area_font_size
float scroll_x { 0.0f };
std::function<input_action(float delta)> scroll_horizontal {};
std::function<input_action(float offset)> scroll_horizontal_to {};
```

- **읽기 전용:**
  - 막는 것: 글자 삽입, 붙여넣기, 잘라내기, 삭제, undo·redo, IME 조합 시작.
  - 남기는 것: caret 이동, 선택, 복사, Ctrl+A, 포인터 선택, 그리고 Text 패턴(IsReadOnly 참).
  - TSF에 읽기 전용 문서로 알려 조합이 시작되지 않게 한다(`src/win32/win32_tsf_input.cpp`). Android는 IME를 띄우지 않는다.
- **줄 넘김 해제:** `wrap=false`면 줄을 접지 않는다. 가로 휠·Shift+휠·가로 막대(X0)로 흐르고, caret을 따라 가로 스크롤한다.
- **보존할 것:** `text_area_wrap_cache`의 "바뀐 문단만 다시 접기"와 "보이는 줄만 그리기"(`a606411`)는 그대로 둔다.
- **줄 높이:** 글자 크기를 바꾸면 줄 높이도 비례해서 바뀐다. 지금 `text_area_line_height` 18 상수는 11pt 기준이다.

**수용 기준**

- 읽기 전용에서 글자 키·붙여넣기·잘라내기·Backspace·Delete가 메시지를 내지 않는다.
- 같은 상황에서 Shift+방향키·Ctrl+C·Ctrl+A는 지금처럼 동작한다.
- 읽기 전용 칸에 초점이 있을 때 TSF 조합이 시작되지 않는다(TSF 테스트 또는 수동 확인 기록).
- `wrap=false`에서 내용 폭이 가장 긴 줄 폭이고, 가로 막대가 서고, 가로 휠이 가로로 흐른다.
- 큰 글(수 MB)에서 끝에 줄을 더하면 바뀐 문단만 다시 접는다(기존 성능 테스트 유지).
- 새 필드를 쓰지 않는 `text_area`의 동작·raster가 같다.

### X3. 메뉴 항목 종류

**범위**

`menu_item_config`(`include/luil/ui/menu_element.h:11`)에 다음을 더한다.

```cpp
enum class menu_item_kind { action, check, radio };
menu_item_kind kind { menu_item_kind::action };
bool checked { false };
std::u8string shortcut_text {};   // 표시만 한다. 단축키 연결은 X4의 명령표
```

- **그리기:** 체크는 체크 글리프, 라디오는 점이다. 아이콘 칸과 겹치면 체크가 앞선다.
- **단축키 열:** 단축키 글은 오른쪽 열에 정렬한다.
- **폭:** 메뉴 폭은 가장 긴 "라벨 + 단축키"에 맞춘다. `menu_element::height_for`처럼 정적 `width_for`를 더해 담는 쪽이 popup 크기를 알 수 있게 한다.
- **UIA:** 체크 항목은 MenuItem + Toggle, 라디오 항목은 MenuItem + SelectionItem이다.

**수용 기준**

- Narrator가 체크·라디오 상태를 읽는다(UIA 테스트).
- 단축키 글이 한 열로 오른쪽 정렬된다(raster).
- `width_for`가 가장 긴 항목에 맞는다.
- 새 필드를 쓰지 않는 메뉴의 모양·키 동작이 같다.

### X4. 팝업 앵커, 하위 메뉴, 명령표

**범위**

- **팝업 앵커:** `ui_popup::anchor`가 다른 popup의 `id`도 받게 한다.
  - 자리(x, y)는 앵커 popup 기준 좌표다.
  - 앵커 popup이 닫히면 자식도 닫힘 계기를 받는다(`popup_dismiss_reason`에 앵커 닫힘 이유를 더한다).
  - 창 이동·활성 전환·바깥 클릭 판정은 popup 사슬 전체를 하나로 본다. 자식 popup 안의 클릭은 부모의 "바깥 클릭"이 아니다.
  - 모니터 경계 다듬기는 자식 popup에도 적용한다. 오른쪽이 모자라면 부모의 왼쪽에 연다.
  - TSF host 선택(`docs/popup-ime-design.md`)이 사슬에서도 맞는지 확인한다.
- **하위 메뉴:** `menu_item_config::children`(`std::vector<menu_item_config>`)를 더한다.
  - →·Enter로 열고 ←·Esc로 한 단계씩 닫는다.
  - hover로 여는 데 지연(약 300ms)을 두고, 대각선으로 움직이는 동안에는 닫지 않는다.
  - 하위 메뉴가 열려 있으면 키는 가장 안쪽 메뉴가 받는다(`process_menu_key`).
  - 하위 메뉴를 여닫는 것은 지금 메뉴처럼 앱 상태다. 라이브러리는 메시지만 낸다.
- **명령표 도우미:** 순수 값과 함수만 둔다. 실행은 앱이 한다.
  ```cpp
  struct key_chord { key_code key; bool control, shift, alt; };
  enum class command_scope { window, focused, modal };
  struct app_command { std::u8string id, label; key_chord shortcut; command_scope scope; bool enabled { true }; bool checked { false }; };
  [[nodiscard]] std::optional<std::u8string> match_command(std::span<const app_command>, const key_pressed_event&, command_scope active);
  [[nodiscard]] menu_item_config menu_item_from(const app_command&);
  [[nodiscard]] std::vector<chord_conflict> find_conflicts(std::span<const app_command>);
  [[nodiscard]] std::u8string shortcut_label(const key_chord&);   // "Ctrl+Shift+F5"
  ```
  - `match_command`는 앱이 `interaction_policy::on_key` 안에서 부르는 것을 기본으로 한다. 1.2절의 전파 규칙에서 마지막 단계다.
  - 모달이 열려 있으면 `window` 범위 명령은 일치하지 않는다.

**수용 기준**

- 3단 메뉴를 키보드로 왕복하고, Esc가 한 단계씩 닫으며, 초점이 앵커 항목으로 돌아간다.
- 부모 popup을 닫으면 자식도 닫힘 메시지를 받는다.
- 자식 popup 안의 클릭이 부모를 닫지 않는다.
- 오른쪽 모니터 경계에서 자식이 왼쪽으로 열린다.
- `find_conflicts`가 같은 범위의 같은 단축키를 보고한다.
- 모달이 열린 상태에서 창 범위 명령이 일치하지 않는다.
- 하위 메뉴를 쓰지 않는 기존 메뉴의 동작이 같다.

### X5. 문서 보기 2단계 (스타일 구간·줄 장식·여백·검색 강조)

**범위**

`text_area_config`에 선택적 필드를 더한다. 앱은 구간만 준다. diff·구문 분석 알고리즘은 넣지 않는다.

```cpp
struct text_span { std::size_t begin, end; text_color_role color; bool bold; text_background_role background; };
struct line_decoration { std::size_t line; line_background_role background; char32_t marker; };
struct gutter_column { gutter_kind kind; /* line_number | custom */ std::function<std::u8string(std::size_t line)> text; };
struct text_highlight { std::size_t begin, end; bool current; };

std::shared_ptr<const std::vector<text_span>> spans {};          // begin 순 정렬, 겹치지 않음
std::shared_ptr<const std::vector<line_decoration>> lines {};    // line 순 정렬
std::vector<gutter_column> gutters {};
std::vector<text_highlight> highlights {};
```

- **색:** 구간 색은 팔레트 역할로만 받는다(임의 색 금지). 고대비에서는 대비 색으로 접는다.
- **질의:** 배치 결과 질의 `line_rect(line)`, `offset_at(point)`, `scroll_to_offset` 메시지 경로를 더한다.
- **성능:** 보이는 줄의 바이트 범위로 구간을 이진 탐색한다. frame마다 구간 전체를 복사하거나 훑지 않는다.

**수용 기준**

- 구간 50만 개, 20MB 글에서 frame마다 보이는 줄의 구간만 찾는다(계측 기록).
- 선택 띠가 구간 배경 위에 그려진다.
- 여백 글은 선택과 복사에 들어가지 않는다.
- 현재 검색 위치를 `scroll_to_offset`으로 화면에 들인다.
- 구간이 바뀌고 글이 같으면 다시 접지 않는다.
- 고대비 raster가 통과한다.

### X6. 편집형 콤보

**범위**

새 `combo_box_element`를 둔다. 입력 칸과 후보 popup을 묶는다.

- **초점과 키:**
  - 초점은 늘 입력 칸에 있다.
  - ↑↓·Page·Home·End가 후보 강조를 옮긴다. 1.2절 전파 규칙에 따라 칸이 소비하지 않은 키를 콤보가 조상 전파(5.1절)로 받는다.
  - Enter는 강조 후보를 확정한다. 후보가 없으면 입력 글로 확정을 요청한다.
  - Esc는 세 단계다. 조합 중이면 조합만 취소한다(IME 몫). 목록이 열려 있으면 닫는다. 닫혀 있으면 편집 전 값으로 되돌린다(`restore`).
- **필터:** IME 조합 중에는 `text_input_view::displayed_text()`로 거른다.
- **비동기 후보:** `request_suggestions(token, query)`를 낸다. 앱이 같은 token으로 결과를 실어 오고, 오래된 token의 결과는 무시한다. 불러오는 동안 스피너(X0)를 둔다.
- **검증:** 목록 밖 값을 허용할지는 설정으로 정한다. 검증 메시지 자리를 둔다.
- **접근성:**
  - 콤보: ComboBox + ExpandCollapse + Value.
  - 후보 목록: List. 강조 후보: SelectionItem.
  - 초점은 칸에 남는다.

**수용 기준**

- ↑↓로 후보를 옮기는 동안 초점 테가 칸에 남는다.
- 조합 중 필터가 표시 글 기준이다.
- Esc가 위 세 단계를 지킨다.
- 오래된 token의 후보가 버려진다.
- UIA로 펼침 상태·값·강조 후보를 읽는다.
- 목록 밖 값을 허용하지 않을 때 확정이 거절되고 검증 메시지가 보인다.

### X7. 파일 대화상자

**범위**

```cpp
enum class file_dialog_kind { open, open_multiple, save, folder };
struct file_filter { std::u8string label; std::vector<std::u8string> extensions; };
struct file_dialog_request
{
    std::uint64_t id {};            // 같은 id는 한 번만 연다 (window_placement_revision과 같은 규약)
    file_dialog_kind kind {};
    std::u8string title {}, initial_folder {}, default_name {};
    std::vector<file_filter> filters {};
    std::u8string owner_surface {}; // 비면 주 창
};
// ui_frame
std::optional<file_dialog_request> file_dialog {};
enum class file_dialog_status { chosen, cancelled, failed, unsupported };
struct file_dialog_result { std::uint64_t id; file_dialog_status status; std::vector<std::u8string> paths; std::u8string error; };
// app_delegate: 결과를 앱 메시지로 바꾸는 virtual 하나
```

- **Win32:** UI thread에서 IFileOpenDialog·IFileSaveDialog를 연다. 소유 창은 `owner_surface` 표면의 HWND다. 지금 예제 코드(`examples/demo_main.cpp:801`)를 라이브러리로 옮기고, 예제는 새 API를 쓰게 고친다.
- **Android:** `unsupported`로 답한다.

**수용 기준**

- 취소와 실패가 다른 status로 온다.
- 보조 창 위에서 열어도 소유 창 뒤로 숨지 않는다.
- 같은 id를 다시 게시해도 한 번만 열린다.
- 여러 경로가 UTF-8로 온다.
- 예제의 이미지 열기가 새 API로 동작한다.

### X8. UI 텍스트 shaping (장기)

착수 전에 설계 문서 `docs/text-layout-design.md`를 먼저 쓰고 사용자 검토를 받는다.

- **방향:**
  - `text_layout` 값 하나가 글리프 run·클러스터 표·줄을 든다.
  - 측정·그리기·hit test·caret·선택·줄 넘김이 모두 그 값을 쓴다.
  - HarfBuzz 공장은 기존 `text_shaping_factory()`를 쓴다.
  - 끈 구성은 지금 경로(`drawSimpleText`, `src/ui/draw_primitives.cpp:147`)를 그대로 둔다.
- **순서:** X1·X5 뒤에 한다. UAX #14 줄 나눔과 양방향 caret은 그 다음 단계로 나눈다.

## 7. Claude 트랙 요약 (참고)

Codex가 기다리거나 만나는 지점을 알기 위한 요약이다.

- **C0** 공통 기반:
  - 키 조상 전파(5.1절), 접근성 역할(5.2절).
  - 가로 휠 축 수정: 세로 전용 스크롤 영역에 `event.horizontal` 휠을 보내지 않는다(`src/ui/ui_interaction.cpp:789`).
  - 가상 목록 10만·100만 행 계측.
- **C1** 키보드 선택 문맥:
  - `key_step_context`(Shift=확장, Ctrl=선택 유지)와 `on_step_with`를 둔다.
  - 가상 목록에 `move_with`·`toggle_at_cursor`·`select_all`을 더한다. Ctrl+A는 목록이 받는다.
  - `ui/selection_model.h`(`selection_pick`·`selection_move`·`selection_reconcile`)를 둔다.
- **C2** 삼상 체크: `check_config::mixed`, `access_info::mixed`, UIA Indeterminate, `aggregate_check`.
- **C3** 테이블 1단계:
  - 열 모델, 머리글, 폭 끌기, 정렬 요청, 가로 동기, 셀 렌더러, 행 선택, Grid·Table 접근성.
  - X0의 가로 막대·label 정렬을 쓴다.
- **C4** 테이블 2단계: 셀 커서, F2·Enter·Esc 편집, 열 재배치·숨김, 트리 열, 체크 열.
- **C5** 대규모 목록 접근성: ItemContainer·VirtualizedItem, 상한 없는 GetSelection.
- **C6** 내용 크기 배치:
  - `preferred_size`, `add_fit`, stack 상한, `grid_element`, 겹침 컨테이너, 여러 줄 label.
  - 여러 줄 label은 X0의 label 변경 위에 더한다.
- **C7** 항목 모델 도우미: `flatten_tree`, `sort_filter`, `node_load`, `request_token`. X0 스피너를 쓴다.
- **C8** 예제 "변경 작업대":
  - 테이블·문서 보기·콤보·메뉴·파일 대화상자를 커스텀 요소 없이 조립한다.
  - 예제 안에 `ui_element` 상속, `hit_test`·`accessibility()`·`draw` 재정의가 0건이어야 한다.

## 8. 공통 작업 규칙

### 8.1 코드와 문서 형식

- **식별자:** snake_case다(형식 검사가 형과 이름 공간을 본다).
- **줄 끝과 공백:** 들여쓰기는 공백 4칸이다. 줄 끝은 CRLF(`.gitattributes`)이고, BOM·탭·줄 끝 공백이 없어야 한다.
- **형식 검사:** `scripts\check_source_style.ps1`이 통과해야 한다.
- **초기화와 반환값:** 중괄호 초기화 `{}`를 쓰고, 반환값을 버리면 안 되는 함수에 `[[nodiscard]]`를 단다. 주변 코드의 형식을 따른다.
- **주석:** 한국어 평서문("…다")으로 쓴다. 공개 헤더 주석은 "왜 그런가"와 규약을 적는다. 기존 헤더의 밀도와 말투를 따른다.

### 8.2 호환성

- 기존 필드·함수의 이름과 기본 동작을 바꾸지 않는다.
- 새 동작은 새 필드를 채운 요소에서만 켜진다.

### 8.3 테스트

- 순수 로직은 `luil_core_tests`(`tests/CMakeLists.txt`)에, Win32 경로는 해당 Win32 테스트에 Catch2로 더한다.
- 그리기는 raster 테스트(`docs/raster-test-design.md`), 키 동작은 창 없는 `interaction_controller` 시나리오로 잠근다.
- 빌드와 실행:
  ```powershell
  cmake --preset vs2026-tests
  cmake --build --preset vs2026-tests-debug
  ctest --preset vs2026-tests-debug
  cmake --build --preset vs2026-tests-release
  ctest --preset vs2026-tests-release
  ```
- Windows 빌드 환경이 없으면 PR 설명에 "빌드·테스트 미실행"과 그 이유를 적는다. 통과했다고 쓰지 않는다.

### 8.4 문서

- 기능마다 설계 문서를 더하거나 기존 문서를 갱신한다(예: `docs/text-area-wrap-design.md`, `docs/popup-anchor-design.md`).
- 새 공개 요소는 `docs/components.md`와 `docs/README.md`에 더한다.

### 8.5 커밋과 PR

- **커밋 제목:** 기존 형식 `<type>: <한국어 평서문>`을 따른다. 예: `feat: 여러 줄 칸이 읽기 전용으로 선택·복사만 받는다`. type은 feat·fix·perf·docs·test·build다.
- **PR 크기:** 한 PR은 작업 단위 하나(X0은 항목별로 나눠도 된다)다. 다른 트랙의 파일을 고치는 PR은 설명에 그 이유와 범위를 적는다.
- **브랜치:** `codex/` 접두사를 쓴다. 기본 브랜치에서 새로 따고, 다른 트랙의 PR이 합쳐지면 기본 브랜치를 merge해 따라간다.

### 8.6 진행 보고

각 PR 설명에 다음을 남긴다. 이 문서의 계약이 바뀌면 같은 PR에서 이 문서도 고친다.

- 수용 기준별 통과 여부
- 계측 수치
- 남은 일
- 계약(5절)을 바꾼 점

## 9. 참고 근거 (기준 커밋)

| 내용 | 위치 |
| --- | --- |
| 키 라우팅 순서 | `src/ui/ui_interaction.cpp:2727` |
| Shift는 단축키 수식이 아님 | `include/luil/ui/ui_events.h:390` |
| 편집 경계가 코드포인트 단위 | `src/text/text_edit.cpp:25` |
| UI 글자는 `drawSimpleText` | `src/ui/draw_primitives.cpp:147, 162` |
| shaping은 SVG·Lottie 전용 | `docs/vector-image-design.md:107` |
| `text_area_config` | `include/luil/ui/text_area_element.h:61` |
| 글자 크기·줄 높이 상수 | `include/luil/ui/text_area_element.h:78` |
| label은 한 줄 | `include/luil/ui/label_element.h:23, 33` |
| 끝 말줄임만 있음 | `src/ui/draw_primitives.cpp:221` |
| 세로 전용 스크롤 막대 | `include/luil/ui/scrollbar_element.h:38` |
| 메뉴 항목 설정 | `include/luil/ui/menu_element.h:11` |
| popup 앵커는 표면 이름 | `include/luil/app/app_host.h:82` |
| 진행 막대는 값을 모르는 진행을 제외 | `include/luil/ui/progress_element.h:27` |
| 회전 글리프 그리기 함수 | `include/luil/ui/draw_primitives.h:166` |
| 가운데 버튼을 표면은 받음 | `src/win32/window_surface.cpp:523` |
| 변환기는 왼쪽·오른쪽만 | `src/win32/surface_input.cpp:24` |
| 예제의 파일 대화상자 | `examples/demo_main.cpp:801` |
| 앱 UI 명령 확장 지점 | `include/luil/app/app_delegate.h:69` |
| 드롭다운 설정 | `include/luil/ui/dropdown_element.h:11` |

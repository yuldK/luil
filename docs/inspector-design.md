# 검사기

실행 중인 화면 위에 요소의 윤곽과 정보를 얹는 개발 도구다. 앱이 `ui_frame::inspector` 칸으로 켜면 공용 그리기(`draw_frame`)가 집은 요소의 윤곽, 안쪽 여백, 정보 상자를 그린다. tree를 평탄화하고 요소를 글로 풀어 쓰는 순수 함수도 함께 내서 앱의 로그와 test가 같은 글을 쓴다. 공개 형식은 새 헤더 [ui_inspect.h](../include/luil/ui/ui_inspect.h)의 `inspector_config`·`inspect_node`와 질의 함수들, [app_host.h](../include/luil/app/app_host.h)의 `ui_frame::inspector`다.

overlay는 그리기만 한다. 요소가 아니라서 hit test, 입력, 초점, 접근성 tree에 닿지 않는다.

## 문제

- 게시된 tree를 한눈에 볼 길이 없다. [ui_tree.h](../include/luil/ui/ui_tree.h)의 `find`·`ancestry_of`·`visible_bounds`·`unarranged`·`duplicate_ids`는 id를 알아야 부르거나 test가 한 번 확인하는 질의다. 화면이 어긋난 앱은 어느 요소가 어디에 배치됐는지 직접 센다.
- UIA는 이 일을 대신하지 못한다. `access_role::none`인 stack·panel은 접근 tree에서 접히고(ui_tree.cpp `index_element`), 배치 전용 컨테이너의 상자와 hit 흡수는 알리지 않는다.
- kind를 사람이 읽을 이름으로 바꾸는 표가 라이브러리에 없다. `ui_element_kind`는 숫자이고 AutomationId도 그 수다([ui_element_id.h](../include/luil/ui/ui_element_id.h)). 게시 뒤에 되읽히는 값은 getter뿐이다. 요소의 config는 private이라(예: info_bar_element.h 91행) padding·spacing·weight는 읽을 수 없다.
- 포인터 자리는 발행되지 않는다. hover는 id 하나로만 오고, 자리(`hover_point`)는 tooltip이 있는 요소 위에서만 기록한다(`record_hover_point`, ui_interaction.cpp 3793행). 이동마다 발행본을 바꾸면 모든 창이 다시 그려져서다(ui_element.h 877·1243행). 그래서 포인터 아래의 label이나 배치 전용 컨테이너를 짚는 길이 지금은 없다.
- 얹을 자리는 이미 한 곳이다. Win32의 CPU·Direct3D 렌더러와 캡처, Android의 CPU·Vulkan 렌더러가 모두 `draw_frame`(src/host/frame_state.cpp 77행)을 부른다.

## 공개 형식

```cpp
// include/luil/ui/ui_inspect.h
// 요소를 집는 길이다. 집은 요소에서 `ancestor_levels`만큼 조상으로 올라간 요소가 대상이다.
enum class inspector_pick { hovered, focused, pressed, selected, point };

// 앱 kind의 이름이다. 값이라 UI thread가 thread 경계 없이 읽는다.
struct inspector_kind_name { ui_element_kind kind {}; std::u8string name {}; };

// frame마다 앱이 싣는다 (`ui_frame::inspector`). 기본값이면 아무것도 그리지 않는다.
//  - `selected`는 pick이 selected일 때, `point`는 point일 때 쓴다 (`ui_tree::hit_test`와 같은 tree 좌표다).
struct inspector_config
{
    bool enabled { false };
    inspector_pick pick { inspector_pick::hovered };
    std::size_t ancestor_levels { 0 };
    ui_element_id selected {};
    std::optional<point_f> point {};
    bool outline_all { false };   // 보이는 모든 요소의 윤곽도 옅게 긋는다
    bool show_info { true };      // 정보 상자
    bool show_palette { false };  // 팔레트 견본
    std::vector<inspector_kind_name> kind_names {};
};
```

```cpp
// 아래는 모두 순수 함수다. 게시된 tree는 불변이라 어느 thread에서나 부른다.
inline constexpr std::size_t no_inspect_parent { static_cast<std::size_t>(-1) };

// tree를 그리기 순서(pre-order)로 평탄화한 줄이다. 좌표는 물리 픽셀(tree 좌표)이다.
struct inspect_node
{
    const ui_element* element { nullptr };
    std::size_t depth { 0 };
    std::size_t parent { no_inspect_parent };  // 목록 안의 자리
    rect_f bounds {};
    std::optional<rect_f> visible_bounds {};   // 조상의 잘림을 거치고 남은 자리. 완전히 잘리면 비어 있다
    bool visible_path { true };                // 뿌리부터 모두 보인다
};
[[nodiscard]] std::vector<inspect_node> inspect_nodes(const ui_tree& tree);
// 좌표 아래의 가장 깊은 보이는 요소다. `hit_test`와 달리 interactive가 아니어도 집는다.
[[nodiscard]] const ui_element* inspect_at(const ui_tree& tree, float x, float y);
// `config`가 가리키는 대상이다. 이 표면의 tree에 없으면 nullptr이다.
[[nodiscard]] const ui_element* pick_inspected(const ui_tree& tree, const interaction_snapshot& interaction, const inspector_config& config);
// 앱이 선택을 옮기는 길이다. 앱은 방금 지은 tree로 부른다. 옮길 곳이 없으면 빈 id다.
enum class inspect_move { parent, first_child, previous_sibling, next_sibling, previous, next };
[[nodiscard]] ui_element_id inspect_step(const ui_tree& tree, const ui_element_id& from, inspect_move move);

// 열거자 이름 그대로다(`info_bar_close`). 라이브러리 kind가 아니면 빈 값이다.
[[nodiscard]] std::u8string_view library_kind_name(ui_element_kind kind) noexcept;
// 라이브러리 이름 → `config.kind_names` → `kind#<수>` 순이다.
[[nodiscard]] std::u8string kind_display_name(ui_element_kind kind, const inspector_config& config);
[[nodiscard]] std::u8string_view access_role_name(access_role role) noexcept;  // 모르는 값은 `role#<수>`
[[nodiscard]] std::u8string element_class_name(const ui_element& element);     // typeid에서 푼 이름
[[nodiscard]] std::u8string describe_element(const ui_tree& tree, const ui_element& element, float scale, const inspector_config& config);
[[nodiscard]] std::u8string dump_tree(const ui_tree& tree, float scale, const inspector_config& config);

// 핵심 역할 24개의 이름과 색이다 (결정 7).
struct inspector_swatch { std::u8string_view name {}; ui_color color { 0 }; };
[[nodiscard]] std::vector<inspector_swatch> inspector_palette_swatches(const ui_color_palette& palette);
```

```cpp
// include/luil/app/app_host.h: ui_frame의 꼬리에 둔다. src/host/frame_state.h: frame_state의 꼬리에 `const inspector_config* inspector`를 둔다.
inspector_config inspector {};

// 앱은 상태 하나로 켠다. 켜는 단축키는 앱이 정한다 (같은 메시지를 메뉴와 단축키에 건다).
frame->inspector.enabled = state_.inspecting;
frame->inspector.ancestor_levels = state_.inspect_levels;
frame->inspector.kind_names = { { card_kind, u8"card" }, { chip_kind, u8"chip" } };
```

## 규칙

**그리기만 한다.**

- overlay는 `draw_frame`에서 `ui_tree::draw` 바로 뒤에 그린다. tooltip과 끌기 그림 위다. 그리는 본문은 새 파일 `src/host/inspector_overlay.cpp`에 두고 `draw_frame`과 `draw_overlay`는 호출만 더한다. `frame_state`는 `const`라 tree, `interaction_snapshot`, frame을 바꾸지 않는다. 정보 상자와 팔레트 견본도 접근성 tree에 없다. 같은 글을 보조 기술로 읽히려면 앱이 `dump_tree`를 label로 올린다.
- 켠다고 다시 그릴 계기가 늘지 않는다. 집은 요소가 바뀌는 것은 hover·초점 발행본이나 새 frame으로 오고, 그때 어차피 다시 그린다. 움직임이 없어 `next_update`를 답하지 않는다. [움직임 줄이기](reduce-motion-design.md)와 닿지 않는다.
- `enabled`가 거짓이거나 tree가 없으면 `draw_frame`의 결과 픽셀은 전과 같다. 비용은 포인터 널 검사 하나다.

**집기.**

- `pick`이 요소를 정한다. `hovered`·`focused`·`pressed`는 `interaction`의 id를 이 표면의 tree에서 `find`로 찾고, `selected`는 `config.selected`를, `point`는 `inspect_at(config.point)`를 쓴다. 그다음 `ancestry_of`로 `ancestor_levels`만큼 올라가고 뿌리에서 멈춘다. 찾지 못하거나 보이지 않는 요소는 대상이 없다.
- hover는 `interactive()`이거나 `hit_opaque()`인 요소만 잡힌다. stack·panel·label은 `ancestor_levels`나 `inspect_step`으로 간다. 포인터를 따라가며 label을 짚는 길은 이 노드에 없다(결정 5).
- 표면마다 자기 tree와 걸러진 `interaction`(`interaction_for_surface`)으로 따로 고른다. 남의 표면에서 난 hover는 이 표면에 없다. 같은 id가 두 표면에 있으면 `selected`는 둘 다에 선다.
- `inspect_at`은 `children()`을 역순으로 걷는다. 숨은 요소는 건너뛰고 `clip_children()` 밖의 좌표는 그 아래를 보지 않으며, 가장 깊은 요소를 돌려준다. 가상 함수 `hit_test`를 재정의한 요소(가상 목록, 링크)의 규칙은 따르지 않는다. `inspect_step`도 보이지 않는 가지를 건너뛴다.

**윤곽.** 선 폭은 논리 값에 `context.scale`을 곱한다.

| 대상 | 선 | 색 역할 |
| --- | --- | --- |
| 집은 요소 | 2px 실선, 바깥에 1px 후광 | `accent`, 후광은 `window_background` |
| 집은 요소의 부모 | 1px 점선 | `primary_foreground` |
| `outline_all`의 나머지 | 1px 실선 | `secondary_foreground`를 옅게 |
| 안쪽 여백 | 집은 요소의 bounds에서 자식 bounds의 합집합을 뺀 띠를 칠한다 | `accent_soft` |

- 선은 `visible_bounds`로 긋는다. 잘려 안 보이는 곳으로 새지 않는다(`draw_focus_ring`과 같다). 통째로 돌려 그리는 요소는 축 정렬 slot을 긋는다([rotate-gesture-design.md](rotate-gesture-design.md) 95행).
- 색만으로 뜻을 말하지 않는다. 굵기와 점선이 역할을 가르므로 팔레트가 색을 접어도 남는다. 고대비에서 `accent`는 hotlight이고 `accent_soft`는 불투명 highlight라 칠은 하지 않는다.
- 안쪽 여백의 수치는 config가 아니라 배치 결과에서 잰다. 자식이 없거나 배치되지 않았으면 띠도 수치도 없다.

**정보 상자.** 글은 개발자용 ASCII 식별자다. 한국어 낱말을 쓰지 않는다. 앱이 준 접근 이름은 그대로 보인다. 값(`access_info::value`)은 보이지 않는다. 암호 칸과 긴 글을 새지 않으려는 것이다.

```
info_bar:sync-error  luil::info_bar_element
bounds 12,48 420x72 @2.0x  inset 8 12 8 12
children 2  depth 4  flags enabled opaque tab
handles click tooltip  access alert "Sync failed"
elements 214  dup 0  unarranged 0
```

- 첫 줄은 `<kind 이름>:<owner>`와 클래스 이름이다. 둘째 줄의 bounds는 논리 픽셀이다. 가려졌으면 `visible x,y WxH`가 붙는다. 마지막 줄은 tree 전체의 요소 수와 `duplicate_ids()`·`unarranged()` 수이고 0이 아니면 뒤에 `!`가 붙는다.
- 자리는 tree root bounds의 왼쪽 위 모서리다. 집은 요소의 보이는 영역과 겹치면 왼쪽 아래로 간다. 폭이 논리 240 미만이거나 높이가 140 미만인 표면(메뉴 popup)은 상자 없이 윤곽만 긋는다. 글꼴은 `make_text_font`의 고정폭 글꼴이다. 바탕은 `tooltip_background`, 테는 `tooltip_border`, 글은 `primary_foreground`다.
- `show_palette`는 root의 오른쪽 위에 견본 한 열을 그린다. 12px 사각, 역할 이름, `#AARRGGBB`다. 색은 `context.palette`, 곧 `frame_palette`가 합성한 지금 팔레트라 테마·키 컬러·고대비를 따른다.

**좌표.**

- overlay는 그 tree를 그린 변환 안에서 그린다. 주 tree는 `draw_frame`의 `restore_origin` 범위 안(안전 영역 원점으로 옮긴 좌표)이고, 모바일 popup layer는 `draw_overlay`의 layer 원점 안이다. 표면 좌표인 popup 테두리와 웹뷰 구멍은 섞지 않는다. 테두리는 overlay 뒤에 긋고 구멍은 맨 뒤에 비운다. 그래서 웹뷰 자리에는 윤곽이 남지 않는다.
- 정보 상자는 그 tree의 root bounds 안에 놓는다. 배치의 `surface`는 root 밖(시스템 막대 자리)까지 닿을 수 있지만([tree-arrange-design.md](tree-arrange-design.md)) 상자는 안전 영역 안이다.
- tooltip 내용 tree와 끌기 그림은 집지 않는다. 집는 것은 표면의 주 tree와 popup tree다.

**이름.**

- 표시는 `<kind 이름>:<owner>`다. 클래스 이름은 `typeid(*element)`에서 얻는다. MSVC의 이름은 읽히므로 `class `·`struct ` 접두만 떼고, `__GNUG__` 계열은 `abi::__cxa_demangle`로 푼다. 풀지 못하면 있는 그대로다.
- 앱 kind는 frame의 `kind_names`가 이름을 준다. 전역 등록표나 콜백이 아니라 값이라서 UI thread가 앱 상태를 잡은 함수를 부르지 않는다.

## 결정

**1. 형태.** 질문: 검사기를 어디에 세우는가.

- 길 a: 창 안 overlay. 되는 것: `draw_frame` 한 곳에 얹어 두 플랫폼의 렌더러에 한 번에 서고, 집는 대상을 UI thread가 그릴 때 정해 hover를 곧바로 따른다. 앱 몫은 `ui_frame` 칸 하나다. 대가: 정보 상자가 앱 화면을 일부 가린다(자리는 두 곳).
- 길 b: 별도 창(`ui_window`). 되는 것: 화면을 가리지 않고 tree 패널을 luil 요소로 짓는다. 대가: 그 tree는 logic thread가 짓는데 hover는 UI thread의 발행본에 있어 선택을 메시지로 왕복해야 한다. 앱이 창, 패널, 메시지를 짓고, Android는 `ui_frame::windows`를 무시해 unsupported다([android_app.h](../include/luil/android/android_app.h) 67행).
- 길 c: 둘 다. 대가: 둘의 짐을 모두 진다.
- 권장: a. 데이터 함수가 순수 함수라 b는 같은 함수 위에서 앱이나 후속 노드가 짓는다. 문서 예시의 보조 창 id `inspector`는 그때 쓸 수 있다. 대가: 가려진 자리 아래를 보려면 선택을 옮겨 상자 자리를 바꿔야 한다. **승인 필요.**

**2. 켜는 길.** 질문: 누가 어떻게 켜는가.

- 길 a: 앱이 `ui_frame::inspector`로 켜고 단축키는 앱이 정한다. 선례는 명령 팔레트다. 단축키 `palette_shortcut`은 데모에 있고 라이브러리가 예약하지 않는다([command-palette-design.md](command-palette-design.md) 54행). 되는 것: 컨트롤러와 키 경로를 고치지 않고 Android도 같은 칸을 쓴다. 대가: 앱이 켜는 코드를 쓴다(메시지 하나, 단축키 한 줄).
- 길 b: 라이브러리가 기본 단축키(예: Ctrl+Shift+I)를 예약한다. 대가: 앱 단축키와 겹치고, 컨트롤러(`ui_interaction.cpp`)와 Win32 키 경로를 고쳐야 하며 Android는 못 쓴다.
- 길 c: 환경 변수나 명령줄이 켠다. 되는 것: 앱 코드를 안 고친다. 대가: 플랫폼이 읽는 코드(`win32_window.cpp`)가 는다. 켠 상태를 앱이 모른 채 화면이 바뀌고 Android에는 환경 변수가 없다.
- 권장: a. 대가: 앱마다 켜는 코드 몇 줄이다. **권장대로 진행.**

**3. 빌드 포함.** 질문: 릴리스에서 빼는가.

- 길 a: 항상 포함. 켜는 것은 실행 중 칸이고 기본은 꺼짐이다. 되는 것: 구성이 갈리지 않아 test 한 벌이 모든 구성을 덮는다. RTTI는 이미 켜져 있어(`app_message`가 `typeid`를 쓴다, app_message.h 24행) 클래스 이름이 더하는 것이 없다. 코드는 천 줄 안팎(추정)이고 kind 이름은 문자열 86개다. 대가: 릴리스에도 코드가 들어간다. 단축키를 릴리스에 남기지 않는 것은 앱의 책임이다.
- 길 b: `LUIL_ENABLE_INSPECTOR` 옵션(`LUIL_ENABLE_*` 선례, CMakeLists.txt 266~310행). 끄면 overlay 소스와 `draw_frame`의 호출이 빠진다. 대가: 옵션, `#if`, 설치 설정, test 구성이 하나 는다. 줄어드는 것은 overlay 그리기(약 400줄, 추정)뿐이다. 데이터 함수와 이름표는 test가 쓰므로 남는다.
- 권장: a. 대가: 위의 코드 크기다. 줄여야 할 앱이 생기면 b를 따로 낸다. **권장대로 진행.**

**4. 보여 줄 범위.** 질문: 요소에서 무엇까지 읽는가.

- 길 a: 게시 뒤 getter로 읽히는 것만. `id()`, `bounds()`, `visible_bounds`, `arranged()`, `visible()`, `enabled()`, `interactive()`, `tab_stop()`, `hit_opaque()`, `clip_children()`, `focus_group()`, `accessibility()`의 역할과 이름, 액션과 역할 포인터(`drag()`, `scroll()`, `zoom()` 따위)의 유무, `tooltip()`, 클래스 이름이다. 안쪽 여백은 자식 bounds에서 잰다. 되는 것: `ui_element.h`와 요소를 건드리지 않는다. 대가: spacing, weight, 글 같은 config 값은 안 보인다.
- 길 b: `ui_element`에 `describe()` 가상 함수를 더해 모든 요소가 config 요약을 낸다. 되는 것: 정보가 가장 풍부하다. 대가: `ui_element.h`와 요소 수십 개를 고치고 새 요소는 이 함수도 짓는다. 같은 라운드에 `ui_element.h`의 역할 칸을 더하는 노드(ink)와 겹친다.
- 길 c: 기본이 빈 목록인 가상 함수(`inspect_fields`)를 더하고 필요한 요소부터 채운다. 대가: `ui_element.h`가 닿고 반쯤 채워진 표가 오래 남는다.
- 권장: a. 대가: 위의 config 값이다. 필요가 생긴 요소부터 c로 후속에 더한다. **권장대로 진행.**

**5. 집기.** 질문: 어떤 요소를 어떻게 집는가.

- 길 a: 집힌 요소(hover, 초점, 눌림)와 조상 오르기, 앱이 `inspect_step`으로 옮기는 선택. 되는 것: 컨트롤러와 플랫폼을 고치지 않고, 부모를 올라 stack·panel의 안쪽 여백을 보고, 선택 이동으로 label까지 간다. 대가: 포인터 아래의 label을 곧바로 짚지 못한다.
- 길 b: 포인터 자리를 라이브로 따라 `inspect_at`으로 가장 깊은 요소를 집는다. 되는 것: label과 여백을 마우스로 곧장 짚는다. 대가: 자리가 발행본에 없다. 컨트롤러가 이동마다 싣거나(`ui_interaction.cpp`, ink·menu_bar와 직렬, 이동마다 모든 표면이 다시 그려진다) 플랫폼이 그릴 때 커서를 읽어야 한다(`win32_window.cpp`는 줄 4의 파일이고 Android에는 커서가 없다).
- 길 c: a에 더해 순수 질의 `inspect_at`과 `point`를 낸다. 앱이 이미 아는 좌표(클릭 액션의 `ui_action_context` 자리 따위)로 label과 여백까지 집는다. 되는 것: b가 와도 `pick` 값은 이미 있다. 대가: 공개 질의 하나와 칸 하나가 늘고 채울 길이 앱이 아는 좌표뿐이라 쓰임이 좁다.
- 권장: c. 라이브 추적은 파일 경계를 넘는 큰 변경이라 나누고, 좌표를 아는 앱에는 지금 길을 연다. 대가: 위의 하나다. **승인 필요.**

**6. kind 이름.** 질문: 사람이 읽을 이름을 어디서 얻는가.

- 길 a: 라이브러리 kind 이름표(열거자 이름 그대로)와 전수 test, 앱 kind는 frame의 값. 되는 것: AutomationId의 수를 사람이 읽는 이름으로 바꾼다. 대가: 라이브러리 kind를 더하는 모든 노드가 같은 커밋에 이름 한 줄을 더한다. 짝이 하나 는다.
- 길 b: 클래스 이름만. 되는 것: 표가 없다. 대가: 앱 요소도 자동으로 나오지만 컴파일러마다 이름이 다르고, 한 클래스가 여러 kind를 맡는 요소(분할 버튼, 정보 막대)에서 kind가 안 보인다.
- 길 c: 숫자만. 대가: 사람이 읽을 수 없다.
- 권장: a(클래스 이름도 함께 보인다). 이름표는 `default` 없는 `switch`로 짓는다. 컴파일러의 누락 경고(clang `-Wswitch`, MSVC C4062)에 기대지 않는다. 경고가 꺼진 구성에서도 전수 test가 막는다. 대가: 위의 짝이다. **권장대로 진행.**

**7. 팔레트 견본.** 질문: 팔레트 역할을 검사기에서 보이는가.

- 길 a: 넣지 않는다. 대가: 색 역할을 검사기로 못 본다.
- 길 b: 핵심 역할 24개를 손으로 적은 표다. `window_background`, `surface_background`, `primary_foreground`, `secondary_foreground`, `disabled_foreground`, `divider`, `input_background`, `input_border`, `control_border`, `accent`, `accent_hover`, `accent_soft`, `accent_foreground`, `selection_background`, `selection_foreground`, `raised_background`, `notice_background`, `warning_accent`, `error_accent`, `tooltip_background`, `tooltip_border`, `button_hover_background`, `button_pressed_background`, `backdrop_background`다. 읽는 것은 `context.palette` 하나이고 `ui_style`을 직접 읽지 않으며 style_sheet 파서에 기대지 않는다. 되는 것: 지금의 테마·고대비 팔레트를 바로 본다. 대가: 완전하지 않다. 새 역할이 빠져도 빌드가 막지 않는다. G4의 키 표 헤더가 서면 그 표를 읽도록 바꾼다.
- 길 c: 전 역할 표와 `static_assert(sizeof)`로 역할이 늘면 빌드를 막는다(`ui_color`는 4바이트라 가능하다). 대가: 팔레트에 역할을 더하는 모든 노드가 표를 고치고, G4가 낼 키 표와 같은 목록이 둘이 된다.
- 권장: b. 대가: 위의 불완전함이다. 전 역할은 G4 키 표를 읽는 후속에서 채운다. **권장대로 진행.**

## 플랫폼

### Win32

- 모든 표면(주 창, 보조 창, popup)이 `window_surface::build_frame`의 `apply_frame_appearance`(window_surface.cpp 811행, win32_window.cpp 2038행)를 지난다. 그 함수의 `state.style` 옆 한 줄(`state.inspector = frame != nullptr ? &frame->inspector : nullptr;`)이 모든 표면에 선다. `win32_window.cpp`는 줄 4의 파일이라 그 한 줄은 줄 4가 별도 커밋으로 낸다.
- 표면마다 자기 tree와 걸러진 interaction으로 따로 그린다. 작은 popup 창(메뉴)에는 윤곽만 선다. 캡처와 녹화도 `draw_frame`을 지나므로 overlay가 사진에 찍힌다. 앱이 켠 채 찍지 않는 것은 앱의 몫이다.
- 고대비는 시스템 색을 읽은 팔레트라 윤곽은 hotlight와 창 글자색으로 선다. UIA provider는 tree만 읽으므로 켜도 달라지지 않는다. 별도 창(결정 1의 b)은 Win32 전용으로 열어 둔다.

### Android

- 같은 `draw_frame`이다. 한 줄은 `render()`의 `state.style` 다음에 낸다(android_app.cpp 1690행). popup은 주 표면 위의 layer로 그려지므로 `draw_overlay` 안에서 layer마다 선다.
- hover가 없다(마우스를 꽂은 때를 빼고). 집기는 `pressed`(손가락이 누르는 동안), `focused`(탭한 칸), 앱이 준 `selected`와 `point`다. 모바일 데모는 선택 이동 단추를 둔다.
- Java 소스 0줄이다. Skia 그리기와 `typeid`뿐이고 JNI가 없다. `abi::__cxa_demangle`은 NDK의 `<cxxabi.h>`이고 [core 이식성 검사](../cmake/verify_core_portability.cmake)의 플랫폼 헤더 패턴 밖이다. `luil_core_tests`가 기기에서 풀기를 실제로 돈다. 별도 창은 unsupported다. 안전 영역은 `origin_x/y`와 root bounds가 이미 정하므로 상자가 시스템 막대 아래로 가지 않는다.

## 다른 노드와의 약속

- **R1에서 미룬 것.** R1 설계 문서에 이 노드로 미룬 항목은 없다. 그 문서들이 G4–G7로 미룬 것도 이 노드와 닿지 않는다. R1에서 넘어온 controller 한계 둘, 메뉴 폭, `local_frame` 복제는 이 노드의 범위 밖이다.
- **kind를 더하지 않는다.** overlay는 요소가 아니라 그리기다. 그래서 `ui_element_id.h`를 고치지 않고, 라운드 계획의 "inspector도 kind를 더한다"는 가정은 닫는다.
- **이름표를 채우는 이는 통합이다.** 이름표(`src/ui/inspect_names.cpp`)는 이 브랜치에만 생기므로 msg_box, rating, ink(`ink_canvas`)의 브랜치는 이 파일을 건드리지 않는다. inspector가 먼저 들어오면 전수 test와 컴파일러가 뒤따르는 kind의 누락을 알리고, 통합 담당이 "이름 채우기" 커밋 하나로 채운다. inspector가 마지막이면 이 브랜치의 병합 커밋이 그때 있는 kind를 한꺼번에 채운다. 이름은 열거자 이름 그대로다. 그 뒤 새 kind를 더하는 노드는 같은 커밋에 case를 더한다. [ink-design.md](ink-design.md)의 "`ink_canvas` 한 줄은 통합 때"와 같은 규칙이다.
- **ui_frame·frame_state 꼬리.** 같은 라운드에 `ui_frame`이나 `frame_state` 꼬리에 칸을 더하는 노드가 있어도 덧붙임이라 병합이 기계적이다. `frame_state.cpp`는 `draw_frame`과 `draw_overlay`를 고친다. page_anim(R4)이 `draw_frame`의 그리기를 바꿀 때 overlay 호출은 지금의 tree에만 둔다.
- **ink·image_clip·os_notify(줄 3·4, `android_app.cpp`).** 이 노드는 `render()`의 한 줄만 낸다. ink는 `copy_motion`과 `start`의 축 켜기, image_clip은 클립보드 실행과 액션 분기, os_notify는 권한 다시 읽기다. 각자 자기 함수만 고친다.
- **`ui_tree.*`·컨트롤러를 고치지 않는다.** `inspect_at`과 평탄화는 `ui_inspect.cpp`의 자유 함수라 teaching_tip(R4)이 `ui_tree.cpp`를 고치는 것과 겹치지 않는다. `ui_events.h`, `ui_interaction.*`, `window_surface.cpp`도 건드리지 않는다.
- **기본 문구 정책(줄 1).** 검사기 글은 ASCII 식별자라 그 노드의 한국어 리터럴 검사에 걸리지 않고 R5 i18n의 번역 대상도 아니다.
- **style_sheet·G4(줄 2).** 팔레트 견본은 `context.palette`만 읽어 파서에 기대지 않는다. G4의 키 표 헤더가 서면 24개 표를 그 표로 바꾸는 후속이 된다.
- **데모.** 라이브러리 커밋과 데모 연결 커밋을 나눈다. `examples/demo_main.cpp`와 `examples/mobile_demo/app.cpp`의 `frame->inspector` 연결은 통합 때 다른 줄의 연결과 직렬로 쌓는다.

## 확인

자동 test.

- `tests/ui_inspect_tests.cpp`(`luil_core_tests`, 기기에서도 돈다):
  - `inspect_nodes`: 그리기 순서, 깊이, 부모. 숨은 가지는 `visible_path`가 거짓이다. `clip_children` 안에 반쯤 걸친 자식은 교차 영역이고 완전히 잘리면 비어 있다. `access_role::none` 컨테이너도 한 줄이다.
  - `inspect_at`: 중첩 stack·panel·clip 안에서 가장 깊은 보이는 요소, interactive가 아닌 label과 컨테이너도 집는 것, 숨은 요소와 잘린 자리를 집지 않는 것, 겹치면 위에 그려진 것이 먼저, 밖의 좌표는 nullptr.
  - `pick_inspected`: 다섯 `pick`, `ancestor_levels`가 뿌리에서 멈춤, tree에 없는 id와 숨은 요소는 nullptr, 걸러진 interaction에서 남의 표면 hover는 없음. `inspect_step`: 여섯 이동의 순서, 끝의 빈 id, 숨은 가지 건너뜀, 빈 `from`의 `next`는 뿌리.
  - 이름: `library_kind_name`이 0부터 `end_of_library_kinds` 직전까지 모두 비지 않고 서로 다르다(새 kind를 이름 없이 더하면 실패한다). 표본 열거자는 이름이 같다. 앱 kind는 `kind_names`이고 없으면 `kind#1024`다. `element_class_name`은 `label_element`를 담고 `class ` 접두가 없다.
  - `describe_element`: 배율 2에서 논리 픽셀, 가려진 자리, 안쪽 여백, 플래그, 접근 역할과 이름. 값(`access_info::value`)은 안 나온다. `dump_tree`는 작은 tree의 글을 머리줄까지 그대로 잠근다. `inspector_palette_swatches`는 24개 멤버에 서로 다른 색을 넣은 팔레트에서 이름마다 제 멤버의 색을 낸다.
- `tests/inspector_overlay_tests.cpp`(`luil_core_tests`, `draw_frame`을 `frame_state`로 부르는 래스터):
  - 끈 frame과 같은 픽셀(`enabled` 거짓, tree 없음)이다. 켠 frame은 윤곽 띠와 정보 상자 밖이 같다. 집은 요소의 변 위 픽셀이 `accent`이고 바깥 후광이 `window_background`이며 상자 자리가 `tooltip_background`·`tooltip_border`다. 글은 래스터 축에 글꼴이 없어 단언하지 않는다.
  - 밝은·어두운·고대비 팔레트에서 윤곽이 바탕과 구별되고 고대비는 칠하지 않는다. `origin`을 준 frame에서 윤곽이 옮겨진 요소 위에 서고 상자가 root bounds 밖을 칠하지 않는다. overlay layer의 윤곽은 layer 원점 기준이고 layer 밖 픽셀은 그대로다. 작은 표면은 상자 없이 윤곽만 선다. `outline_all`은 보이는 요소마다 선을 긋고 숨은 요소는 긋지 않는다.
  - 그리기 전후 `make_access_snapshot`, `hit_test` 격자, `interaction_snapshot`이 같다.
- `tests/uia_provider_tests.cpp`(`luil_tests`, Windows): 켠 frame을 그린 뒤의 UIA provider가 낸 자식 열거의 이름, 역할, bounds, AutomationId가 끈 상태와 같다.

데모. 상태를 label로 그려 `luil-uia`로 되읽는다. `examples/demo/inspector_page.cpp`·`.h`와 `examples/mobile_demo/inspector_page.cpp`·`.h`에 같은 것을 둔다.

- label `inspector-state`, `inspector-pick`, `inspector-levels`, `inspector-selected`, `inspector-count`(`inspect_nodes`가 센 요소 수)를 둔다. 단추 `inspector-toggle`, `inspector-up`·`inspector-down`(조상 단계), `inspector-parent`·`inspector-child`·`inspector-prev`·`inspector-next`(선택 이동), `inspector-outline`, `inspector-palette`를 둔다. 데스크톱은 Ctrl+Shift+I로도 켠다(팔레트의 Ctrl+Shift+P와 겹치지 않는다).
- UIA(격리 데스크톱): `Get-LuilTree`로 끈 트리를 받고 `inspector-toggle`을 Invoke한 뒤 다시 받아 `inspector-*` label 값을 뺀 나머지가 같다. 이동 단추를 누를 때마다 `inspector-selected`가 이동 순서대로 바뀐다. 윤곽은 UIA로 읽지 못해 사람 확인이다.
- `tests/mobile_demo_page_tests.cpp`(`[mobile-demo][inspector]`)와 `demo_gallery_tests.cpp`가 페이지 짓기와 `inspector-*` 상태 label을 지킨다.

사람 확인.

- Win32: hover를 움직이며 윤곽과 상자가 따라오는지, 150% 배율·다중 모니터·작은 요소·메뉴 popup 위에서 읽히는지, 고대비에서 눈으로 윤곽이 보이는지, 정보 글의 크기. Android 실기기: 탭한 칸(`focused`)과 선택 이동의 윤곽과 상자, 상태 표시줄과 제스처 막대 아래로 가려지지 않는지, 손가락에 가려지는지, 회전 뒤.

## 범위 밖

- 포인터를 라이브로 따라 label과 여백을 짚는 일(결정 5의 b), hover한 요소를 선택으로 고정하는 일(hover 발행본을 앱 메시지로 올리는 길이 필요하다).
- 별도 창 패널(결정 1의 b), 요소 값 고치기, 이벤트 기록, frame 시간 그래프.
- `describe()` 가상 함수와 config 값 표시(결정 4의 b·c), 전 역할 팔레트 견본(결정 7).
- tooltip 내용 tree, 끌기 그림, 다른 프로세스의 창, 검사기 글의 번역.

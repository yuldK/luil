# 확인 대화 상자

제목, 본문, 단추 행을 담은 modal 대화 상자를 한 번에 짓는다. 앱은 단추의 key·글·역할과 결과를 받을 메시지만 준다. scrim, 초점 가둠, Esc는 [modal_host_element](../include/luil/ui/modal_host_element.h)가 맡고 크기는 내용에서 나온다. 공개 형식은 [message_box.h](../include/luil/ui/message_box.h)의 `message_box_config`·`message_box_button`과 `make_message_box`다.

심각도 글리프와 낱말은 R1 정보 막대가 세운 `ui_severity`([severity.h](../include/luil/ui/severity.h))와 비공개 [severity_paint.h](../src/ui/severity_paint.h)를 쓴다. 표준 단추의 기본 문구는 [default-text-design.md](default-text-design.md)의 헤더 규칙을 따른다.

## 문제

앱이 확인 대화 상자를 만들려면 modal host, 표면, 본문 stack, 단추 행을 손으로 이어야 한다. 데모의 대화([basics_page.cpp](../examples/demo/basics_page.cpp) 696~807행의 `add_overlay`·`make_dialog_body`·`make_dialog_buttons`)가 그 조립이고, 모바일 데모([basics_page.cpp](../examples/mobile_demo/basics_page.cpp) 460~573행)가 거의 같은 복사본이다.

- 높이를 어림한다. 대화 높이는 `caption_height + 148`이고 본문은 한 줄 label이다. host는 앱이 준 `content_height`를 그대로 쓰고 내용에 묻지 않는다([modal_host_element.cpp](../src/ui/modal_host_element.cpp)의 `arrange` 87~88행). 본문이 길어지거나 글자 크기가 커지면 앱이 숫자를 다시 적어야 한다. "측정 단계가 없다"는 전제([modal-dialog-design.md](modal-dialog-design.md) 79행)는 `preferred_size`가 생겨 낡았다.
- 좁은 화면의 폭을 앱이 줄인다. 모바일 데모가 `min(360, width / scale - 2 * page_padding)`를 적는다. 단추 행이 넘칠 때 쌓는 규칙은 어느 데모에도 없다.
- 채움과 초점이 어긋난다. 데모는 취소에 `focus_entry`를, 초기화에 `default_button`과 강조 채움을 둔다. Enter는 초점 element를 먼저 실행하고 기본 버튼은 그 뒤다([ui_interaction.cpp](../src/ui/ui_interaction.cpp)의 `process_activation_key`가 `process_default_key`보다 앞, 3266·3325행). 코드 순서대로 읽으면 대화가 뜨자마자 친 Enter는 취소를 누르고, 채움은 Enter의 자리를 잘못 말한다. 데모의 안내 글도 같은 말을 한다. 본문 아래 줄이 "Esc로 닫고 Enter로 확인한다"인데([basics_page.cpp](../examples/demo/basics_page.cpp) 768행) 초점은 취소에 있다. 실행해 본 것이 아니라 읽은 결과이고 구현 때 test로 잠근다.
- 닫는 길이 여럿이고 각자 메시지를 단다. 취소, 초기화, 캡션 ×, Esc, 바깥 누름이 `dialog_intent { false }`를 따로 내고 초기화는 메시지 둘을 낸다. 모바일은 뒤로 가기도 따로 건다([app.cpp](../examples/mobile_demo/app.cpp) 253행). 하나를 빼먹으면 대화가 열린 채 남는다.
- 심각도 글리프와 접근 이름의 심각도 낱말이 없다. 정보 막대 문서가 이 몫을 이 노드로 넘겼다.

## 공개 형식

```cpp
// include/luil/ui/message_box.h
inline constexpr float message_box_default_width { 360.0f };  // 논리 픽셀
inline constexpr float message_box_screen_margin { 16.0f };  // 자리 가장자리와 상자 사이의 최소 여백
inline constexpr float message_box_button_minimum_width { 88.0f };

enum class message_box_role
{
    normal,
    accept,  // 긍정. Enter의 대상이고 강조색이다
    reject,  // 거절. Esc·바깥 누름·뒤로 가기의 결과다
    destructive,  // 되돌릴 수 없는 일. 오류색이고 Enter의 대상이 되지 않는다
};

struct message_box_button
{
    std::u8string key {};  // 한 상자 안에서 겹치지 않는다. 눌림이 낼 결과의 이름이자 단추 owner의 한 부분이다
    std::u8string text {};
    message_box_role role { message_box_role::normal };
};

struct message_box_check  // "다시 묻지 않기" 같은 칸이다. 켜졌는지는 앱 상태다
{
    std::u8string label {};
    bool checked { false };
    ui_action toggle {};
};

struct message_box_config
{
    std::u8string owner {};  // host·내용·단추의 owner다
    std::optional<ui_severity> severity {};  // 있으면 제목 앞에 글리프를 그리고 접근 이름에 낱말을 붙인다
    std::u8string title {};
    std::u8string body {};
    std::vector<message_box_button> buttons {};  // 왼쪽에서 오른쪽, 쌓이면 위에서 아래의 순서다
    std::optional<message_box_check> check {};
    // 단추 눌림, Esc, 바깥 누름이 낸 결과를 앱 메시지로 바꾼다. 비면 단추도 Esc도 메시지를 내지 않는다.
    std::function<input_action(std::u8string_view key)> result {};
    bool outside_dismisses { true };  // 거짓이면 바깥 누름은 흡수만 한다
    ui_element_id focus_return {};  // 닫힌 뒤 초점이 돌아갈 바깥 자리 (modal_host_config와 같다)
    float width { message_box_default_width };
    float control_size { 0.0f };  // 단추 높이다. 0이면 `text_button_height`. 터치 앱이 올린다
    std::u8string severity_name {};  // 비면 기본 문구(정보·성공·경고·오류)
};

[[nodiscard]] std::unique_ptr<modal_host_element> make_message_box(message_box_config config);
// Esc가 낼 결과다. 닫는 길이 없으면 nullopt다. 앱이 뒤로 가기(`ui_frame::back`)에 단다.
[[nodiscard]] std::optional<input_action> message_box_dismiss_message(const message_box_config& config);
[[nodiscard]] ui_element_id message_box_button_id(std::u8string_view owner, std::u8string_view key);
[[nodiscard]] ui_element_id message_box_check_id(std::u8string_view owner);  // 확인 칸의 id (구현 중에 더했다, 결정 7)
// 표준 단추의 key 상수다: message_box_ok_key·message_box_cancel_key·message_box_yes_key·message_box_no_key (u8"ok"·u8"cancel"·u8"yes"·u8"no")
// 표준 단추다. 글이 비면 기본 문구(확인·취소·예·아니요)다. key는 ok·cancel·yes·no다 (`message_box_ok_key` 같은 상수도 둔다).
[[nodiscard]] message_box_button message_box_ok(std::u8string text = {});  // accept
[[nodiscard]] message_box_button message_box_cancel(std::u8string text = {});  // reject
[[nodiscard]] message_box_button message_box_yes(std::u8string text = {});  // accept
[[nodiscard]] message_box_button message_box_no(std::u8string text = {});  // reject
```

```cpp
// include/luil/ui/modal_host_element.h, modal_host_config 끝 (결정 1의 b)
// 참이면 내용에게 높이를 묻는다 (`ui_element::preferred_size`, 배치 문맥의 측정기·치수·배율).
// 표면이 있으면 표면이 아니라 안쪽 내용에게 묻는다. panel은 크기를 답하지 않는다. 답이 없으면 `content_height`다.
bool fit_height { false };
// 0보다 크면 내용의 폭과 높이를 자리에서 이 여백(논리 픽셀)의 두 배를 뺀 값으로 자른다. 0이면 자르지 않는다.
float edge_margin { 0.0f };
```

```cpp
luil::message_box_config box {};
box.owner = u8"reset";
box.severity = luil::ui_severity::warning;
box.title = u8"카운터를 0으로 되돌릴까요?";
box.body = u8"누른 횟수가 사라진다.";
box.buttons = { luil::message_box_cancel(), { .key = u8"reset", .text = u8"초기화", .role = luil::message_box_role::destructive } };
box.result = [](const std::u8string_view key) { return luil::make_app_action(reset_dialog_result { std::u8string { key } }); };
box.focus_return = luil::ui_element_id { kind_dialog_open };
auto host { luil::make_message_box(std::move(box)) };
host->arrange({ { 0.0f, 0.0f, width, height }, scale });  // 지금 데모가 하는 배치 그대로다
root.add(std::move(host));
```

## 규칙

**구성.** host(`modal_host`, 접근 역할 dialog) 안에 표면 panel(`surface_background`, 모서리 8, `divider` 1px 테)이 서고, 그 안에 비공개 내용 element 하나(`message_box`)가 선다. 제목·본문·글리프는 내용이 `draw_text_block`으로 직접 그린다. 단추와 확인 칸만 자식이다. 한 element로 짓는 까닭은 [정보 막대 결정 3](info-bar-design.md)과 같다. 단추를 행으로 둘지 쌓을지는 폭을 알아야 정하는데 stack은 만들 때 방향을 정한다.

**치수.** 논리 픽셀이고 그릴 때 배율을 곱한다. 글은 `measure_context`로 그리기와 같은 글꼴로 재므로 시스템 글자 크기가 커지면 높이가 따라온다.

| 부분 | 값 |
| --- | --- |
| 상자 폭 | `min(width, 자리 폭 - 2 * message_box_screen_margin)` |
| 안쪽 여백 | 사방 20. 글 폭은 안쪽 폭에서 글리프 몫(24 + 12)을 뺀 값이다 |
| 글리프 | `severity`가 있을 때만. 24칸에 크기 20, `severity_color`와 `severity_glyph` |
| 제목 | `body_font_size + 2`, 굵기는 스타일 `font_weights.title`, 줄을 바꾼다 |
| 본문 | `body_font_size`, `body_text` 굵기, 줄을 바꾼다. 제목과 8 |
| 확인 칸 | 본문과 12. `check_element::height_for`와 `control_size` 중 큰 높이의 줄. 칸은 그 줄의 세로 가운데에 서고 자기 높이를 지킨다 |
| 단추 | 높이 `control_size`(0이면 28), 폭 `max(88, preferred_size)`, 사이 8, 글과 20 |

높이는 `20 + 머리 + (칸이 있으면 12 + 칸) + 20 + 단추 영역 + 20`이다. 머리는 글리프 칸과 글 높이 중 큰 쪽이다. 글 열(제목·본문·확인 칸)은 글리프 칸이 있으면 그 오른쪽 36 지점에서, 없으면 안쪽 여백에서 시작하고 단추 영역은 안쪽 폭 전체를 쓴다. 제목과 본문의 글 색은 둘 다 `primary_foreground`이고 크기와 굵기가 둘을 가른다(정보 막대와 같다). 제목이나 본문이 비면 그 몫과 사이 간격(8, 칸이 있으면 12)이 빠진다.

**쌓기.** 단추 폭의 합에 8 × (개수 - 1)을 더한 값이 안쪽 폭 이하이면 한 행이고 오른쪽 끝에 붙인다. 넘치면 모두 안쪽 폭 전체로 쌓고 순서는 그대로 위에서 아래다. 경계(같으면 행)는 비공개 순수 함수 `message_box_stacks_buttons`가 정하고 test가 잠근다. 배율을 곱했다 나눈 폭이 한 부동소수점 단위 어긋나도 경계가 뒤집히지 않게 0.01 논리 픽셀의 여유를 둔다.

**자리가 모자랄 때.** host가 높이를 자리 - 32로 자르면 내용은 그 높이를 받는다. 제목, 확인 칸, 단추는 줄이지 않고 본문의 줄 수를 줄여(`max_lines`) 마지막 줄을 `…`로 줄인다. 한 줄로도 모자라면 아래가 넘쳐 그려진다. 본문 스크롤은 없다. 접근 이름은 전체 본문이다. 줄일 줄 수는 넘치는 높이를 본문 한 줄 높이로 나눈 올림이고 한 줄은 남긴다. 높이를 모르는 자리(0)는 자르지 않는다.

**역할과 키.** 결정 3의 a다. 초점이 이기므로 첫 초점을 Enter의 대상과 같은 단추에 묶는다.

| 입력 | 결과 |
| --- | --- |
| Enter | 초점이 선 element를 실행한다. 첫 초점이 Enter의 대상이라 뜨자마자 친 Enter는 그 대상이다 |
| Enter의 대상 | 마지막 accept 단추(`default_button`, 강조 채움). destructive는 되지 않는다 |
| 첫 초점(`focus_entry`) | 마지막 accept. 없으면 마지막 reject, 그것도 없으면 첫 단추(역할이 모두 normal이거나 destructive뿐일 때). 테는 첫 Tab에서 선다 |
| Esc | reject 단추(여럿이면 마지막)의 key를 `result`에 넘긴다. reject가 없고 단추가 하나면 그 단추. 둘 다 없으면 Esc는 앱으로 흐른다 |
| 바깥 누름 | `outside_dismisses`가 참이고 Esc의 결과가 있으면 같은 결과. 아니면 흡수만 한다 |
| 단추 눌림 | `result(그 key)` 하나다 |

닫는 모든 계기가 같은 `result`를 부르므로 앱은 메시지 하나를 받아 닫고 key로 가른다. 열림은 앱 상태이고 host는 메시지만 낸다([modal-dialog-design.md](modal-dialog-design.md)).

**초점과 Tab.** Tab 순서는 자식 순서다(`focus_order`가 tree를 훑는다). 확인 칸, 단추들(준 순서)이다. 칸에 초점이 있으면 Enter는 칸을 켜고 끈다(초점이 이긴다). 단추 행은 focus group이 아니라 단추마다 Tab 자리라서 R1에서 넘어온 controller 한계(묶음의 비대표 구성원에서 Tab이 처음으로 가는 것)에 걸리지 않는다.

**접근성.**

- host: 역할 dialog, 이름은 `심각도 낱말 + 제목`(심각도가 없으면 제목)이다. 화면 읽기가 뜨는 순간 말하는 이름이다.
- 내용: 본문이 있으면 `static_text`(이름은 전체 본문), 없으면 구조(none)다. 단추는 그 아래 `button`(Invoke)이고 칸은 `check_box`다. 정보 막대와 같은 모양이다.
- 글리프는 장식이라 접근 노드가 없다. 색만으로 뜻을 말하지 않는다. 고대비는 info·warning·error의 색을 접어도 글리프가 남는다.
- 본문이 열릴 때 함께 읽히는지는 화면 읽기마다 다르다. 설명 칸이 `access_info`에 없어 이 노드는 만들지 않고 사람이 확인한다.

**테마·움직임.** 색은 팔레트 역할뿐이고 표면의 `divider` 테가 고대비에서 경계를 남긴다. 움직임이 없어 움직임 줄이기와 무관하다.

**kind.** `ui_element_id.h`의 `end_of_library_kinds` 바로 위에 `message_box`, `message_box_button`을 더한다. 단추의 owner는 상자 owner와 key 사이에 U+001F를 둔다(표의 부품 owner와 같은 꼴이다). 확인 칸은 기존 `check` kind에 같은 꼴의 owner(끝 조각이 `check`)다. 둘 다 공개 helper `message_box_button_id`·`message_box_check_id`가 짓는다. test와 UIA 스크립트에 kind 숫자를 박지 않는다.

## 결정

**1. 크기.** 질문: 상자의 폭과 높이를 누가, 어떤 문맥으로 정하는가.

- 길 a: 만들 때 재서 `content_height`에 넣는다. 되는 것: host를 건드리지 않는다. `command_palette_height`가 같은 모양이다. 대가: 호출자가 배율·측정기·치수를 그리기와 같게 넘겨야 한다. 두 데모는 측정기 없이 `host->arrange`로 배치하므로 만들 때의 문맥이 배치와 갈리면 줄 수가 달라져 글이 잘린다([ui_element.h](../include/luil/ui/ui_element.h)의 `measure_context` 주석이 경고한 점). 창 크기(폭 자르기)도 앱이 넘긴다.
- 길 b: host가 배치할 때 내용에 묻는다(`fit_height`·`edge_margin`). 되는 것: 측정 문맥이 배치 문맥이라 갈릴 수 없다. 앱은 창 크기도 문맥도 넘기지 않고 직접 짠 대화 상자도 같은 옵션을 쓴다. 대가: 공유 요소를 고친다(약 40줄). "host는 크기를 재지 않는다"를 켠 host에 한해 개정한다. 고칠 곳은 `modal-dialog-design.md` 79·122행과 `modal_host_element.h` 74~76행 주석이다. 꺼진 host(기본)의 배치는 그대로고 기존 host test가 잠근다.
- 길 c: 상자가 정적 `measure`를 공개하고 앱이 높이를 `modal_host_config`에 넣는다(`info_bar_element::measure` 모양). 되는 것: host 무변경이고 앱이 크기를 알아 끌기 offset을 다듬을 수 있다. 대가: a와 같은 문맥 어긋남이 남고 앱이 재기, 설정, 짓기 세 단계를 적는다.
- 권장: b. 대가: 공유 요소 수정과 불변식 개정. 이번 라운드에 `modal_host_element.*`를 고치는 노드는 이 노드뿐이다. 승인: 길 b (2026-11-02).

**2. 결과 전달.** 질문: 눌림을 앱에 어떻게 알리는가.

- 길 a: 단추마다 key·글·역할을 주고 눌림·Esc·바깥이 `result(key)` 하나를 부른다. 되는 것: 닫는 모든 길이 같은 메시지라 앱은 받아서 닫고 key로 가른다. 하나를 빼먹지 않는다. 라이브러리는 글을 모른다는 규칙에 맞고 `set_open`·`activate_link`처럼 factory 하나로 메시지를 짓는 기존 모양이다. 대가: 한 단추가 메시지를 둘 낼 수 없다. 앱이 처리기에서 둘을 한다.
- 길 b: 표준 단추 열거(확인·취소·예·아니요·저장…)와 결과 enum. 되는 것: 흔한 경우가 한 줄이다. 대가: 열거에 없는 단추("초기화")는 따로 길이 필요하고 글은 번역 때문에 앱이 주입해야 해서 열거와 글이 두 겹이다. 열거가 늘면 기본 문구도 는다.
- 길 c: 단추마다 `ui_action`을 앱이 준다. 되는 것: 기존 액션 관용구이고 한 단추가 메시지 여럿을 낸다. 대가: Esc·바깥이 어느 단추의 액션인지 정해야 하고 닫는 메시지를 단추마다 앱이 달아야 한다(지금 초기화의 두 메시지).
- 권장: a. 대가: 한 결과에 메시지 하나. 표준 단추는 helper가 key·역할·기본 문구를 채운다. 승인: 길 a (2026-11-02). [default-text 결정 1](default-text-design.md)이 enum 키(c)로 바뀌어도 `message_box_*`가 키 이름이 되어 호출부는 그대로다.

**3. 역할과 Enter·Esc·바깥·첫 초점.** 질문: 역할이 키와 초점에 어떻게 이어지는가. destructive가 Enter의 대상이 될 수 있는가.

- 길 a: 역할이 모두 정한다(위 표). destructive는 Enter의 대상도 첫 초점도 되지 않는다. 되는 것: 채움, 첫 초점, Enter가 한 단추를 가리켜 데모의 어긋남이 생기지 않는다. 한 번의 Enter가 되돌릴 수 없는 일을 하지 않는다. 대가: destructive 단추를 Enter로 실행하게 만들 수 없다. 그런 대화는 앱이 `modal_host`로 직접 짓는다.
- 길 b: 역할은 모양만 정하고 앱이 `default_key`·`dismiss_key`·`initial_key`를 따로 준다(Qt의 defaultButton·escapeButton). 되는 것: 모든 조합이 된다. 대가: 칸이 셋이고 앱이 서로 맞춰야 한다. 데모의 어긋남을 되풀이할 수 있다.
- 길 c: a에서 destructive도 `default_key`로 허용한다. 대가: 한 번의 Enter가 되돌릴 수 없는 일이 된다. [text_button_fill_for](../include/luil/ui/dialog_elements.h)는 이를 앱의 판단으로 남겨 막지 않는다.
- 권장: a. 대가: 위의 직접 짓기. 권장대로 진행.

**4. 단추 순서와 쌓기.** 질문: 단추 순서를 누가 정하고 쌓일 때는 어떤가.

- 길 a: 앱이 준 순서 그대로 왼쪽에서 오른쪽, 오른쪽 끝에 붙인다. 표준 조합은 거절, 긍정 순이다. 쌓이면 같은 순서로 위에서 아래다. 되는 것: Tab 순서와 눈 순서가 어긋나지 않고 규칙이 한 줄이다. 대가: Windows 관례(긍정이 왼쪽)를 따르려면 앱이 순서를 바꿔 준다.
- 길 b: 플랫폼 관례를 따른다. 대가: `ui_platform`에는 `form_factor`뿐이고 OS 구분이 없어 공개 값이 새로 든다. 같은 앱 코드가 플랫폼마다 다른 순서가 된다.
- 길 c: 라이브러리가 역할로 정렬한다. 대가: 앱이 순서를 못 정한다.
- 권장: a. 대가: 위의 관례 선택은 앱 몫이다. 권장대로 진행.

**5. 제목 줄 형태.** 질문: 제목을 어디에 두는가.

- 길 a: 본문 안의 제목 줄이다. 캡션 바가 없다. 되는 것: 크기가 모두 내용에서 나오고(`dialog_caption_element`는 `preferred_size`가 없다) 끌기 offset과 그 다듬기가 앱에서 사라진다. 대가: 닫기 ×와 끌기가 없다(Esc, 바깥, reject 단추가 닫는다). 데모의 끌기 시연이 이 대화에서 빠진다.
- 길 b: `dialog_caption_element`를 쓴다. 대가: 높이를 `height_for`로 따로 더하고, 끌린 offset을 앱이 다듬으려면 상자의 높이를 알아야 해서 결정 1의 b와 부딪힌다.
- 길 c: a가 기본이고 `move`와 `offset_x/y`를 주면 캡션 바가 선다. 되는 것: 둘 다 된다. 대가: 칸이 늘고 `message_box_close`(닫기 ×의 기본 이름)가 default_text에 든다. 끝에 더하는 칸이라 나중에 더해도 호환된다.
- 권장: a. 대가: 위의 × 와 끌기. `dialog_caption_element`의 예제 소비자는 두 데모의 이 대화뿐이라 치환하면 예제에서 끌기 시연이 사라지고 단위 test(`dialog_elements_tests.cpp`)만 남는다. 시연을 지키려면 데모에 직접 짠 두 번째 대화를 남긴다. [default-text 문서](default-text-design.md) 163행이 약속한 "msg_box의 닫기 기본값"은 a에서 쓸 일이 없다. 공개 칸의 모양과 데모 범위를 가르는 결정이다. 승인: 길 a (2026-11-02).

**6. 안에 담는 것.** 질문: 제목·본문·단추 밖에 무엇을 넣는가.

- 길 a: 심각도 글리프(선택)와 확인 칸 하나를 넣는다. 세부 내용과 입력 칸은 넣지 않는다. 되는 것: 표준 확인 대화의 흔한 변형이 한 상자다. 대가: 칸은 Tab 자리와 Enter 규칙이 하나 늘고 test가 는다.
- 길 b: 글리프까지만. 되는 것: 가장 작다. 대가: "다시 묻지 않기"를 앱이 상자 밖에 직접 지어야 해서 접기 전의 조립이 다시 생긴다. 칸은 나중에 더해도 호환된다.
- 길 c: a에 접히는 세부 내용(markdown)까지. 대가: 접힘 상태·스크롤·높이 변화가 따라와 크기가 훨씬 커진다.
- 권장: a. 대가: 위의 칸 규칙 하나. 권장대로 진행.

## 구현에서 정한 결정

구현하며 문서가 정하지 않았거나 모호했던 것이다. 모두 권장대로 진행한 것이 아니라 구현 중에 고른 것이라 따로 적는다(2026-11-03).

**7. 공개 helper 둘과 key 상수.** `message_box_check_id`와 표준 key 상수 넷을 공개 형식에 더했다. 앱과 test가 단추 id는 helper로 얻는데 칸 id는 owner 꼴을 손으로 맞춰야 하는 비대칭을 없애고, `result`에서 표준 key를 가를 때 글자를 반복하지 않게 한다. 비공개로 두는 길도 있었으나 owner 꼴(U+001F)이 공개 계약이 되는 이상 짓는 함수도 공개가 맞다.

**8. 글 열의 시작.** 글리프 칸(24 + 12)은 `severity`가 있을 때만 폭을 잡는다. 없으면 제목·본문·칸이 안쪽 여백(20)에서 시작한다. 있으면 셋 모두 36 오른쪽에서 시작하고 단추 영역만 안쪽 폭 전체를 쓴다. 위 치수 표의 "글 폭은 안쪽 폭에서 글리프 몫을 뺀 값"이 `severity`가 없을 때도 빼는지 모호했다. 글리프가 없는 상자가 빈 36을 남길 까닭이 없다.

**9. 여럿일 때와 모두 위험할 때.** reject가 여럿이면 Esc·바깥 누름·첫 초점은 마지막 reject를 따른다(accept의 "마지막"과 같은 규칙으로 맞췄다). destructive만 있는 상자는 첫 단추에 초점이 서고 Enter의 대상은 없다. 결정 3의 "destructive는 첫 초점이 되지 않는다"는 다른 역할이 있을 때의 규칙이고, 역할이 하나뿐일 때 초점을 아예 두지 않으면 키보드 사용자가 Tab을 한 번 더 쳐야 해서 택하지 않았다.

**10. 확인 칸의 줄.** 칸은 글 열의 왼쪽에 서고 `control_size` 줄 안에서 세로 가운데에 선다. `check_element`가 자기 높이(24)를 고정하므로 터치 앱이 44를 주어도 칸은 24이고 줄만 44가 된다. 칸의 누르는 자리가 줄 전체가 되게 하려면 `check_element`를 고쳐야 해서 이 노드의 범위 밖이다.

## 플랫폼

**Win32.** 같은 표면 안 modal이라 `win32_window.cpp`·`window_surface.cpp`를 고치지 않는다. UIA는 host가 Window(접근 역할 dialog는 ControlType Window로 간다, `uia_provider.cpp`)와 Name을, 단추가 Invoke를, 칸이 Toggle을 낸다. 고대비와 글자 크기는 위 규칙을 따른다.

**Android.** Java 소스는 0줄이다. 코어 element라 JNI도 unsupported도 없다.

- Esc는 `AKEYCODE_ESCAPE`가 `key_code::escape`로 오므로(`android_input.cpp` 61~62행) 그대로 닫힌다. 뒤로 가기는 플랫폼의 계기라 앱이 `message_box_dismiss_message`를 `ui_frame::back`([app_host.h](../include/luil/app/app_host.h) 380행)에 단다. 지금 모바일 데모가 `dialog_intent`로 하는 일이다.
- 좁은 화면은 폭을 `자리 폭 - 32`로 줄이고 단추가 넘치면 쌓는다. 터치 앱은 `control_size`에 44(`touch_height`)를 준다. 소프트 키보드는 입력 칸이 없어 무관하다.
- 접근성은 Android에서 소비되지 않는다([number-input-design.md](number-input-design.md)와 같은 까닭). 모바일 관례인 아래 시트(`sheet_host_element`)는 범위 밖이다.

## 다른 노드와의 약속

- **default_text**(이 줄의 앞 노드). `default_text.h`에 `message_box_ok_label`·`message_box_cancel_label`·`message_box_yes_label`·`message_box_no_label` 넷을 더한다. 이름은 `<요소>_<부품>` 규칙이고 부품은 `_label`이다. 공개 helper `luil::message_box_ok`와 이름이 같으면 한정 없이 부를 때 둘이 헷갈려 접미사로 가른다(`message_box.cpp`는 그래도 늘 `default_text::`로 한정해 부른다). G6 정책 a에 따라 표준 단추의 글은 앱이 `text` 인자로 덮고 비면 이 기본 문구다. 심각도 낱말은 `severity_name`을 쓴다. 결정 5가 a면 `message_box_close`는 더하지 않는다. `verify_default_text`가 서면 `message_box.cpp`에도 한글 리터럴을 두지 않는다.
- **info_bar**(R1). `ui_severity`와 `severity_paint.h`를 그대로 쓴다. [정보 막대 문서](info-bar-design.md) 201행이 넘긴 "글리프 칸과 접근 이름의 심각도 낱말"을 여기서 정했다. 글리프는 24칸에 20 크기이고 낱말은 화면에 그리지 않고 접근 이름 앞에 붙는다.
- **split_button**(R1)의 필드 popup 열림 규약과는 겹치지 않는다. 앵커도 `set_open`도 `expanded`도 `ui_frame::popups`도 없고 대화 상자는 tree 안 modal이다. 닿는 것은 "열림은 앱 상태이고 닫는 모든 계기가 같은 메시지를 낸다"는 원칙뿐이고 여기서는 `result(key)`다. 열린 동안 여는 단추를 다시 눌러 되열리는 controller 한계도 해당 없다. 여는 단추가 scrim 뒤에 있다.
- **modal_host**. 개정은 위 세 곳이다. `docs/modal-dialog-design.md`에 이 문서로 가는 링크와 `fit_height` 절을 더하는 일도 구현 커밋이 한다.
- **같은 라운드 같은 파일.** `ui_element_id.h`는 rating(`rating`·`rating_cell`)과 ink(`ink_canvas`)도 kind를 더한다. inspector(줄 3)는 더하지 않는다고 닫았다. [ui_element_tests.cpp](../tests/ui_element_tests.cpp) 84행의 `last[]`는 노드마다 같은 줄을 고쳐 병합에서 겹치므로 통합 때 직렬로 합친다. `src/CMakeLists.txt`(`modal_host_element` 옆 `ui/overlays` 묶음)와 `tests/CMakeLists.txt`, `luil.h`는 한 줄씩 덧붙인다. `win32_window.cpp`와 `android_app.cpp`는 건드리지 않는다.
- **inspector의 kind 이름표.** inspector(줄 3)는 kind를 더하지 않지만 이름표 `src/ui/inspect_names.cpp`의 `library_kind_name`을 세운다. 그 파일은 inspector 브랜치에만 있어 이 브랜치는 건드리지 않는다. 이름표는 `default` 없는 `switch`이고 전수 test가 지킨다. 병합 순서가 둘이다. inspector가 이미 main에 있으면 이 노드를 병합하는 통합 커밋이 `message_box`·`message_box_button` case(열거자 이름 그대로)를 더한다. 빠지면 Android clang의 `-Wall -Werror`(`-Wswitch`)가 빌드를 막는다. MSVC `/W4`([msvc.cmake](../cmake/compiler/msvc.cmake) 82행)는 이 경고(C4062)를 기본으로 켜지 않아 Windows에서는 전수 test가 먼저 실패한다. 이 노드가 먼저 들어오면 inspector의 병합 커밋이 그때 있는 kind를 채운다.
- **데모.** `examples/demo/basics_page.*`, `common.h`, `examples/mobile_demo/basics_page.*`는 rating·color_picker도 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고 연결은 통합 때 직렬로 쌓는다. `tests/CMakeLists.txt`에 `basics_page.cpp`를 올리는 등록(아래 확인)은 이 노드의 연결 커밋이 처음 하고, 모바일 `basics_page`를 같은 목록에 올리는 rating(줄 1)과 image_clip(줄 4)의 연결 커밋은 그 뒤에 쌓는다. 결정 5가 a면 연결 커밋은 `dialog_x_/y_`, `move_dialog_intent`, `kind_dialog`·`kind_dialog_cancel`·`kind_dialog_confirm`(`common.h` 44~48행·51~54행)을 지우고 `kind_dialog_open`을 `focus_return`에 쓴다.
- **teaching_tip**(R4)은 단추 모양을 `text_button_visual`의 accent·danger로 맞추면 된다. 이 노드의 헤더를 요구하지 않는다. **R5 i18n**은 위 넷을 표로 흡수하고 호출부는 그대로다.

**R1이 미룬 것을 닫는다.** 이 단계에서는 R1 문서를 고치지 않고 구현 커밋이 아래 문장을 고친다.

| 위치 | 닫는 방법 |
| --- | --- |
| `info-bar-design.md` 201행 | "그 노드가 정한다"를 이 문서의 글리프(24칸, 크기 20)와 낱말(접근 이름 앞)로 바꾼다 |
| `default-text-design.md` 163행 | 표준 단추 helper는 넷으로 닫는다. 캡션 ×의 기본값은 결정 5가 a면 쓸 일이 없다 |
| `modal-dialog-design.md` 79·122행 | 결정 1이 b면 "재지 않는다"를 `fit_height`를 켠 host가 아닌 경우로 좁힌다 |

## 확인

자동 test는 데스크톱 `basics_page` 연결 하나만 빼고 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다. 설계할 때는 `basics_page`를 짓는 test가 없었다. 두 데모 test가 컴파일하는 목록(`tests/CMakeLists.txt` 142~149·154~161행)에 `basics_page.cpp`가 없어서 치환이 기존 test를 깨지 않는다.

- `tests/message_box_tests.cpp`(고정 폭 측정기): 단추 id가 `message_box_button_id`와 같고 눌림이 `result(key)`를 한 번 내는지, `result`가 비면 메시지가 없는지; 역할이 모양과 `default_button`으로 번지는지(accept만 기본 버튼, destructive는 danger이고 기본 아님); 첫 초점이 Enter의 대상과 같고 destructive만 있을 때는 reject에 서며 Enter가 그 단추를 누르는지; Esc가 reject의 key(없고 단추 하나면 그 단추, 둘 다 없으면 앱으로 흐름)를 내고 `message_box_dismiss_message`와 같은지; `outside_dismisses`; Tab이 안을 벗어나지 않고 칸, 단추 순으로 도는지.
- 같은 파일의 크기 단언: 본문 줄 수가 늘면 host가 정한 내용 높이가 늘고, 폭이 `width + 32`보다 좁으면 상자 폭이 `자리 폭 - 32`이고, 단추 폭의 합이 안쪽 폭과 같으면 행·1 더하면 쌓이며 쌓이면 폭 전체로 준 순서대로 서는지, 높이가 모자라면 본문 줄이 줄고 단추가 자리 안에 서는지, 배율 2의 물리 좌표; 접근성(host 이름, 본문 `static_text`, 단추·칸의 역할과 이름, `severity_name` 우선); 표준 단추의 key·역할·기본 문구·글 덮기.
- `tests/modal_host_element_tests.cpp`에 덧붙임: `fit_height`가 내용의 답을 높이로 쓰고, 답이 없으면 `content_height`이며, 표면이 있어도 안쪽 내용에게 묻고, `edge_margin`이 폭과 높이를 자르며 0이면 자르지 않는지. 기본 배치가 그대로임은 기존 test가 지킨다.
- `tests/ui_element_tests.cpp`의 `last[]`에 `message_box_button`을 더한다. `tests/default_text_tests.cpp`(앞 노드가 만든 파일)에 넷을 더한다. `tests/raster_draw_tests.cpp`에는 표면의 테와 accept 단추 채움을 더한다. 글리프 모양은 raster 축이 글꼴을 싣지 않아 사람이 본다.

데모 칸(`examples/demo`, `examples/mobile_demo`의 기본 페이지). "카운터 초기화" 대화를 `make_message_box`로 바꾸고 "대화: 열림/닫힘"과 "마지막 결과: reset|cancel|없음" label을 그려 UIA가 되읽게 한다. 단추 셋과 긴 본문과 확인 칸을 가진 두 번째 대화를 더해 좁은 화면의 쌓임을 보인다. 연결 커밋이 첫 `basics_page` test(열기, 결과 메시지, 닫기)를 더한다. 모바일은 `tests/mobile_demo_page_tests.cpp`에 두고 `tests/CMakeLists.txt`의 모바일 데모 목록(141~151행)에 `basics_page.cpp`를 올린다. 이 페이지는 `mobile_demo_gradient_png` 같은 외부 기호 여섯을 부르는데, 기호는 앱 빌드가 [mobile_demo.cmake](../examples/mobile_demo/mobile_demo.cmake)에서 `cmake/embed_binary.cmake`로 만든 소스에 있어 core test에는 없다. 링크가 막히므로 `secret_store_stub.cpp`(150행)처럼 빈 바이트 대역(`tests/mobile_demo_assets_stub.cpp`)을 목록에 함께 올린다. 빈 바이트는 디코드 오류로 끝나고 페이지는 그 오류를 이미 다루므로(`load_demo_picture`, `read_films`) 대화 test는 그림 칸을 읽지 않는다. 빈 입력의 디코더 답은 구현 때 확인한다. 데스크톱 `basics_page.cpp`는 `<windows.h>`(`MAX_PATH`, 96행)와 비밀 저장소를 불러 `luil_core_tests`에 못 들어간다. Windows 전용 `luil_tests`의 `windows_page.cpp` 곁(415~418행 꼴)에 `basics_page.cpp`와 새 `tests/basics_page_tests.cpp`를 올려 같은 세 가지를 시험한다. 격리 데스크톱에서 luil-uia로 단추 이름과 Invoke를, 키 합성으로 Esc·Enter 결과를 읽는다.

구현한 데모(2026-11-03)는 두 데모의 기본 페이지 맨 아래에 있다. 단추 둘, "카운터 초기화"(위험한 초기화와 취소, 경고 글리프)와 "긴 확인 대화"(단추 셋과 확인 칸, 정보 글리프)가 연다. 아래 두 줄이 label(UIA Name)로 상태를 읽힌다: `대화: 열림` 또는 `대화: 닫힘`, `마지막 결과: reset|cancel|later|discard|ok|없음`. 모바일은 뒤로 가기가 `basics_page::back_action`으로 같은 결과를 낸다. 데스크톱 `basics_page`의 test는 `luil_tests`의 `tests/basics_page_tests.cpp`, 모바일은 `mobile_demo_page_tests.cpp`에 있고 모바일 기본 페이지를 core test 목록에 올리면서 `tests/mobile_demo_assets_stub.cpp`(빈 견본 바이트)를 함께 두었다.

사람 확인: Narrator가 대화 Window의 이름을 읽고 본문과 단추를 순서대로 탐색하는지, 실기기의 좁은 화면·회전·긴 본문·제스처 막대 겹침, 밝은·어두운·고대비에서 표면 경계와 글리프 구분, 끌기 시연이 빠진 데모가 허용되는지.

## 범위 밖

- 캡션 바, 닫기 ×, 끌기(결정 5의 c). 입력 칸이 든 대화, 세부 내용, 본문 스크롤.
- 아래 시트로 뜨는 모바일 관례(`sheet_host_element`).
- 열릴 때 UIA 알림. 대화 상자는 OS가 이름을 읽는다. [정보 막대 결정 6](info-bar-design.md)의 b는 승인되지 않았다.
- 플랫폼 관례에 따른 단추 순서(결정 4의 b), 시간이 지나면 저절로 닫히는 대화.
- 앱이 주지 않은 글(제목, 본문, 확인 칸 글, 표준이 아닌 단추 글)의 기본값. [default-text-design.md](default-text-design.md)의 규칙대로 앱이 준다.

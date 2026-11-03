# 안내 풍선

화면의 한 요소를 꼬리로 가리키며 제목, 본문, 선택적 그림, 행동 단추 0~2개, 닫기 단추를 띄운다. 처음 쓰는 기능을 알리거나 다음 할 일을 짚는 상주 안내다. 주 tree 안에 겹치는 요소로 그리고, modal 칸 하나로 덮개(light-dismiss)와 비modal을 고른다. 열림, 가리킬 사각형, 연 시각은 앱 상태다. 공개 형식은 [teaching_tip_element.h](../include/luil/ui/teaching_tip_element.h)의 `teaching_tip_config`·`teaching_tip_button`·`teaching_tip_dismiss_reason`·`teaching_tip_placement`·`teaching_tip_element`·`teaching_tip_target`과 [field_popup.h](../include/luil/ui/field_popup.h) 끝에 더하는 `field_popup_side`·`field_popup_side_position`·`place_field_popup_at_side`다.

자리 잡기는 [분할 버튼](split-button-design.md)의 `place_field_popup` 규칙을 네 방향으로 늘리고, 가둠·Esc·진입·복귀는 [modal dialog](modal-dialog-design.md)와 [시트](sheet-design.md)가 쓰는 `ui_element` 부품을 그대로 쓴다. controller(`ui_interaction.*`)와 `ui_tree.*`를 고치지 않는다.

## 문제

앱이 대상을 가리키는 안내를 띄우려면 자리부터 닫힘까지 모두 짓는다.

- 대상 옆에 놓는 host가 없다. `modal_host_element`는 내용을 자리 가운데에 놓고 앱이 준 만큼 밀 뿐이다([modal_host_element.cpp](../src/ui/modal_host_element.cpp) 112~118행). `sheet_host_element`는 화면 가장자리에 붙는다.
- 꼬리 달린 상자가 없다. tooltip은 hover 수명이고 초점과 단추를 받지 않는다([tooltip-design.md](tooltip-design.md)의 "Hover와 입력 수명"). 자리 계산 `tooltip_bounds`는 [ui_tree.cpp](../src/ui/ui_tree.cpp) 익명 namespace(32~57행)라 밖에서 못 쓰고 아래·위만 안다.
- `place_field_popup`도 아래·위만 알고 `ui_popup`에 넣을 논리 x·y를 낸다([field_popup.h](../include/luil/ui/field_popup.h) 34~48행). 옆 방향과 꼬리 자리는 앱 몫이다.
- 배치 중인 element는 다른 element의 자리를 모른다. `arrange_context`에는 자리·배율·측정기·스타일뿐이다([ui_element.h](../include/luil/ui/ui_element.h) 740~798행). 이름으로 찾아 보이는 자리를 재는 `ui_tree::find`·`visible_bounds`는 게시된 tree에서만 된다([ui_tree.h](../include/luil/ui/ui_tree.h) 96·112행). 지금 앱은 같은 frame에 먼저 배치한 element의 `bounds()`를 읽어 popup 자리에 쓴다([popups_page.cpp](../examples/demo/popups_page.cpp) 470·494·521행).
- 바깥 누름이 대상까지 누르지 않게 하려면 같은 tree의 덮개가 흡수해야 한다. popup 밖 누름은 닫자는 메시지를 내고 클릭을 그대로 보낸다([window_surface.cpp](../src/win32/window_surface.cpp) 600~606행).

이웃 요소와는 이렇게 갈린다.

| 요소 | 수명 | 자리 | 초점 |
| --- | --- | --- | --- |
| tooltip | hover 동안 | 대상 아래·위 | 받지 않는다 |
| 토스트 | 시간이 지나면 사라진다 | 창 구석 | 동작 단추가 Tab 자리가 아니다 |
| 정보 막대 | 앱이 지울 때까지 | 배치 안의 한 줄 | 단추가 Tab 자리다 |
| msg_box | 답할 때까지 | 창 가운데 | 가둔다 |
| 안내 풍선 | 닫을 때까지 | 대상 옆, 꼬리 | 덮개면 가두고 비modal이면 옮기지 않는다 |

앞 문서가 이 노드로 넘긴 것은 [split-button-design.md](split-button-design.md) 184·230·231·259행, [info-bar-design.md](info-bar-design.md) 203행, [message-box-design.md](message-box-design.md) 223행, [inspector-design.md](inspector-design.md) 237행, [ink-design.md](ink-design.md) 253행, [menu-focus-fixes-design.md](menu-focus-fixes-design.md) 104행, [date-picker-design.md](date-picker-design.md) 202·215행, [calendar-date-design.md](calendar-date-design.md) 94행이다. "다른 노드와의 약속" 끝 표에서 닫는다.

## 공개 형식

```cpp
// include/luil/ui/field_popup.h 끝에 더한다. place_field_popup은 그대로다
enum class field_popup_side { below, above, left, right };  // 왼쪽·오른쪽은 읽는 방향과 무관하다
struct field_popup_side_position
{
    float x { 0.0f }, y { 0.0f };  // place_field_popup과 같은 논리 픽셀이다
    field_popup_side side { field_popup_side::below };  // 실제로 놓은 방향
    bool fits { true };  // 네 방향 모두 모자라 바라는 방향에 두고 limit 안으로 밀었으면 거짓 (대상을 덮을 수 있다)
};
// 바라는 방향, 반대 방향, 남은 두 방향(아래·위 또는 오른쪽·왼쪽) 순으로 주 축이 limit에 들어가는 첫 방향에 놓는다.
// 다른 축은 필드 가운데에 맞춘 뒤 limit 안으로 민다. `anchor.alignment`는 보지 않는다.
[[nodiscard]] field_popup_side_position place_field_popup_at_side(const field_popup_anchor& anchor, size_f size, float scale, field_popup_side preferred) noexcept;

// include/luil/ui/teaching_tip_element.h (치수는 논리 픽셀)
inline constexpr float teaching_tip_default_width { 320.0f };
inline constexpr float teaching_tip_tail_width { 16.0f };  // 꼬리 밑변
inline constexpr float teaching_tip_tail_length { 8.0f };
inline constexpr float teaching_tip_screen_margin { 8.0f };

enum class teaching_tip_dismiss_reason
{
    close_button,
    outside,  // modal일 때 덮개를 눌렀다
    escape,
    back,  // 요소가 아니라 앱이 ui_frame::back에서 이 값으로 부른다 (sheet_dismiss_reason::back과 같다)
};
struct teaching_tip_button  // 정보 막대의 action_label·action 쌍과 같다. 둘 다 있어야 단추가 선다
{
    std::u8string text {};
    ui_action action {};
};
struct teaching_tip_config
{
    std::u8string owner {};
    // 가리킬 자리다. 물리 픽셀, tree 좌표(`ui_element::bounds()`와 같다). 비거나 자리와 겹치지 않으면 대상 없음 (결정 2)
    std::optional<rect_f> target {};
    field_popup_side side { field_popup_side::below };  // 바라는 방향
    bool modal { true };  // 참이면 덮개, 거짓이면 비modal
    std::u8string title {}, body {};  // 둘 다 폭에서 줄을 바꾼다
    std::optional<image_config> image {};  // 풍선 위쪽의 그림. 폭은 안쪽 폭, 높이는 그림 비율 (최대 160)
    std::optional<teaching_tip_button> primary {}, secondary {};  // 결정 7
    std::function<input_action(teaching_tip_dismiss_reason)> dismiss {};  // 비면 닫기 단추가 없다. 덮개는 누름만 흡수하고 Esc는 앱으로 흐른다(가둠에 dismiss 액션이 없을 때의 controller 규칙)
    std::u8string name {};  // 접근 이름. 비면 title, 둘 다 비면 기본 문구 (안내)
    std::u8string close_tooltip {};  // 닫기 단추의 툴팁이자 이름. 비면 기본 문구 (닫기)
    ui_element_id focus_return {};  // modal이 닫힌 뒤 돌아갈 바깥 자리 (modal_host_config와 같다)
    std::chrono::steady_clock::time_point opened_at {};  // 비면 나타남 움직임이 없다 (결정 5)
    float width { teaching_tip_default_width };
    float control_size { 0.0f };  // 단추 높이와 닫기 한 변. 0이면 text_button_height와 24다. 터치 앱이 올린다
};
struct teaching_tip_placement  // 물리 픽셀. arrange·draw·test가 같은 답을 쓴다
{
    rect_f bubble {};
    std::optional<field_popup_side> side {};  // 꼬리가 선 변. 비면 꼬리가 없다
    float tail_center { 0.0f };  // 꼬리 밑변 가운데의 다른 축 좌표
};
class teaching_tip_element final : public ui_element
{
public:
    explicit teaching_tip_element(teaching_tip_config config);
    [[nodiscard]] static teaching_tip_placement place(const teaching_tip_config& config, const measure_context& context, const rect_f& slot);
    [[nodiscard]] static ui_element_id close_id(std::u8string_view owner);
    [[nodiscard]] static ui_element_id primary_id(std::u8string_view owner);
    [[nodiscard]] static ui_element_id secondary_id(std::u8string_view owner);
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
};
// 배치가 끝난 가지 `root`에서 id의 보이는 자리를 찾는다 (결정 1). 숨은 가지는 건너뛰고 자르는 조상(`clip_children`)과
// 겹친 자리만 남긴다. 없거나 다 잘렸으면 nullopt다. 자르기 규칙은 ui_tree.cpp의 `visible_bounds_of`(596~613행)와 같다.
[[nodiscard]] std::optional<rect_f> teaching_tip_target(const ui_element& root, const ui_element_id& id);

// include/luil/ui/ui_element_id.h, end_of_library_kinds 바로 위 (date_field 뒤)
teaching_tip, teaching_tip_scrim, teaching_tip_body, teaching_tip_image, teaching_tip_close, teaching_tip_action,
```

## 규칙

**구성.** root(`teaching_tip`)는 앱이 준 자리 전체를 bounds로 받는다. 앱은 modal host처럼 창 전체로 배치해 root 끝에 담는다. 자식은 차례로 덮개(`teaching_tip_scrim`, modal일 때만)와 몸(`teaching_tip_body`)이다. 몸이 풍선과 꼬리, 제목, 본문을 직접 그리고(msg_box 내용과 같은 꼴) 자식으로 그림(`image_element`), 닫기, 행동 단추를 둔다. 위에서 아래로 그림, 머리 줄(제목과 닫기), 본문, 단추 줄이고 안쪽 여백은 12다(결정 8). 몸은 `hit_opaque`라 빈 자리 누름이 덮개나 뒤로 새지 않는다(modal_host_element.cpp 57행). 단추의 owner는 `owner`, U+001F, `close`·`primary`·`secondary`다.

**자리.**

- 폭은 `min(width, 자리 폭 - 2 × 8)`이다. 높이는 그림, 제목, 본문, 단추 줄의 합이고 `place`가 배치 문맥의 측정기로 잰다. 자리가 모자라면 msg_box처럼 본문 줄을 줄여 마지막 줄을 말줄임한다([message-box-design.md](message-box-design.md) 120행).
- limit은 root 자리에서 여백 8을 뺀 사각형이다. 대상은 root 자리와 겹친 부분만 본다. 대상이 있으면 `place_field_popup_at_side({ .field = 대상, .limit = limit, .gap = 꼬리 길이 + 2 }, 크기, 배율, side)`가 놓는다.
- 꼬리는 놓인 방향 쪽 변에 서고 꼭짓점이 대상 가운데(다른 축)를 향한다. 밑변은 모서리 반지름 안쪽으로 물리고, 대상 가운데가 그 밖이면 끝에 멈춘다. `fits`가 거짓이면 꼬리를 그리지 않는다. 대상 없음(결정 2)이면 limit 가운데에 꼬리 없이 둔다.
- 매 frame 다시 놓는다. 창 크기·배율·스크롤이 바뀌면 앱이 그 frame의 사각형을 주므로 따라간다. 풍선은 root 자리 밖으로 걸치지 않는다(확정 G2-surface a의 대가).

**덮개(modal 참, 기본).**

- root에 `set_focus_trap(true)`, `dismiss`가 있으면 `set_dismiss_action`(Esc는 `escape`), `set_focus_entry`, `set_focus_return`을 건다. [sheet_host_element.cpp](../src/ui/sheet_host_element.cpp) 271~278행과 같은 셋이다. 진입은 `primary`, 없으면 `secondary`, 없으면 닫기다. 진입이 비면 자동 초점이 없다(ui_element.h 1130~1137행, [ui_interaction.cpp](../src/ui/ui_interaction.cpp) 4570~4575행).
- 덮개는 투명 panel이고(panel의 빈 바탕 선택자는 `surface_background`라 알파 0 색을 고른다) root 자리 전체를 덮는다. `hit_opaque`라 누름과 휠을 흡수하고([ui_element.cpp](../src/ui/ui_element.cpp) 586행, ui_interaction.cpp 993~996행) 누르면 `dismiss(outside)`다. 대상 위를 눌러도 대상은 눌리지 않는다.
- Esc는 가둠의 것이다. 처리기 키 뒤에 와서(ui_interaction.cpp 3590~3597행) tree의 가둠을 찾는다(3654~3671행). 풍선에는 글 칸이 없어 Esc를 먼저 쓰는 처리기가 없다.
- 가둠은 pre-order의 마지막 보이는 것이다(ui_tree.cpp 363~374행). 한 번에 하나만 띄우고 다른 modal과 겹치지 않게 하는 것은 앱 몫이다.

**비modal(modal 거짓).**

- 덮개와 가둠이 없다. 뒤 화면을 그대로 쓰고 풍선 밖 누름은 뒤 요소의 것이다. 사용자가 닫는 길은 닫기 단추다. `focus_return`은 쓰이지 않는다.
- 초점을 옮기지 않는다. 풍선은 root 끝에 담겨 그 단추가 Tab 순서의 끝이다. 요소는 Esc를 받지 않는다. 초점이 풍선 안이든 밖이든 Esc는 앱의 `interaction_policy::on_key`로 흐르고(ui_interaction.cpp 3607행), Esc로도 닫고 싶은 앱은 거기서 닫는다(결정 4).
- 사용자의 명령으로 열었으면 앱이 열 때 `focus_request { target = primary_id(owner) }`(없으면 `close_id`)를, 닫을 때 연 자리로 요청을 싣는다. [색 고르기](color-picker-design.md) 결정 3의 두 줄이다. 스스로 뜬 안내에는 싣지 않는다.

**단추.**

- 닫기는 정보 막대와 같은 `button_element`(`icon_close`, role `toolbar`, 손 모양 커서, Tab 자리, 한 변 24)이고 머리 줄 오른쪽 위에 선다. 그림이 없으면 풍선 오른쪽 위다(결정 8). 누르면 `dismiss(close_button)`이다.
- 행동 단추는 `text_button_element`이고 `primary`는 accent, `secondary`는 normal이다. danger는 없다. 되돌릴 수 없는 일을 묻는 자리는 msg_box다. 기본 버튼(`default_button`)은 세우지 않는다. modal 진입이 primary라 Enter가 그 단추로 간다. 줄은 오른쪽 정렬이고 폭이 모자라면 세로로 쌓는다. 누름은 앱 액션이고 닫지 않는다. 닫을지는 앱이 정한다.
- Tab 순서는 그리기 순서다. 닫기, primary, secondary 순이다.

**그리기·테마·고대비·배율.** 몸통(모서리 8)과 꼬리를 한 path로 묶어 `raised_background`로 칠하고 같은 path에 `control_border` 1px 테를 긋는다. 그림자는 `draw_surface_shadow`다. 제목은 `primary_foreground` 굵게, 본문은 `secondary_foreground`다. 고대비 팔레트도 같은 역할을 채우고, 테가 꼬리까지 이어져 바탕이 창 배경으로 접혀도 풍선과 꼬리가 남는다. 치수는 논리 픽셀이고 배율을 곱한다. 글을 배치 측정기로 재므로 큰 글자에서 높이가 늘 뿐 잘리지 않는다.

**움직임(결정 5).** `opened_at`이 있으면 `motion_duration(transition_duration, context.reduce_motion)` 동안 불투명도가 0에서 1로 오르고 풍선이 꼬리 반대쪽에서 4px 다가온다(`ease_in_out`). 몸 전체를 `saveLayerAlphaf`로 그린다([toast_element.cpp](../src/ui/toast_element.cpp) 76행). 도는 동안 `next_update`가 `transition_frame` 뒤를 청한다. 움직임 줄이기면 길이가 0이라 처음부터 끝 모습이다. 나타나는 동안에도 입력을 받는다. 사라짐은 즉시다. [transition.h](../include/luil/ui/transition.h)는 읽기만 한다.

**접근성.**

- root는 modal이면 `dialog`(UIA Window), 아니면 `pane`(sheet_host_element.cpp 360행)이다. 이름은 `name`, 비면 `title`, 둘 다 비면 기본 문구(안내)다.
- 몸은 본문이 있으면 `static_text`(이름은 전체 본문), 없으면 구조다([message_box_element.cpp](../src/ui/message_box_element.cpp) 354~358행). 그림은 `image_config::description`이 있을 때만 읽힌다. 단추는 `button`(Invoke)이다. 나타남 알림은 없다(결정 6). modal은 진입 초점의 FocusChanged에서 화면 읽기가 창 이름을 함께 읽기를 기대한다(사람 확인). 대상과의 관계(UIA DescribedBy·ControllerFor)는 provider에 없어 범위 밖이다.

**기본 문구.** [default_text.h](../src/ui/default_text.h)·`.cpp` 끝에 `teaching_tip_name()`(안내)과 `teaching_tip_close()`(닫기)를 더한다. 정보 막대의 `info_bar_close()`와 같은 낱말이지만 요소마다 함수를 둔다([default-text-design.md](default-text-design.md) 159행). 한글은 `default_text.cpp`에만 둔다.

## 결정

확정(2026-11-03): G2-dismiss 길 c. modal 칸 하나로 고르고 기본은 덮개(light-dismiss)다. 비modal은 닫기 단추로만 닫고 Esc는 앱으로 흐른다(확정 설명). 근거는 [sheet_host_element.h](../include/luil/ui/sheet_host_element.h) 84~86행과 [sheet-design.md](sheet-design.md) 93~97행.

확정(2026-11-03): G2-surface 길 a. 주 tree 안의 겹침 요소로 그리고 `ui_frame::popups`에 싣지 않는다. 근거는 window_surface.cpp 600~606행과 [menu-focus-fixes-design.md](menu-focus-fixes-design.md) 58~61행.

**1. 대상의 사각형.** 질문: tree 안 요소가 배치가 끝난 대상의 자리를 어떻게 아는가.

- 길 a: 앱이 `target`으로 준다. 대상을 먼저 배치해 root에 담는 앱([demo_main.cpp](../examples/demo_main.cpp) 690~731행)은 그 가지에서 `teaching_tip_target(가지, id)`로 같은 frame의 보이는 자리를 얻는다. tree를 `make_arranged_tree` 한 번으로 짓는 앱은 게시된 tree의 `find`·`visible_bounds`로 앞 frame 자리를 준다. 되는 것: 필드 popup(`field_popup_anchor::field`)과 같은 꼴이다. `ui_tree.*`·controller·host를 고치지 않는다. 스크롤에 잘린 대상과 사라진 대상을 helper가 nullopt로 알려 앱이 닫을지 정한다. 대가: 앱이 가지 포인터나 앞 tree를 들고, 한 번에 배치하는 앱은 창 크기·스크롤이 바뀌는 frame에 한 frame 늦는다.
- 길 b: 풍선 host가 화면 내용을 감싸(`set_content`) 먼저 배치하고 그 안에서 id로 찾는다. 되는 것: 앱이 사각형을 다루지 않고 늘 같은 frame이다. 대가: 대상이 host 안에 있어야 하고 덮개가 host 자리만 덮는다. 앱이 따로 배치하던 가지를 host가 다시 배치하면 가상 목록이 첫 배치로 굳는다([virtual_list_element.h](../include/luil/ui/virtual_list_element.h) 446~448행). 화면 내용을 품은 host는 "tip은 이전 화면 tree에 넣지 않는다"는 page_anim의 약속과 부딪힌다.
- 길 c: `make_arranged_tree`가 배치 뒤 id를 풀어 풍선을 다시 놓는 둘째 단계를 둔다. 되는 것: 앱은 id만 주고 같은 frame이다. 대가: `ui_tree.cpp`와 host 두 곳의 frame 경로를 고치는 tree 수준 기능이고, 손으로 배치해 담는 앱 꼴에는 따로 길이 필요하다.
- 권장: a. 대가: 위의 앱 몫과 한 frame 늦음이다. 승인: 길 a (2026-11-03).

**2. 대상이 보이지 않을 때.** 질문: `target`이 비거나 자리와 겹치지 않으면(스크롤로 잘림, tree에서 빠짐) 어떻게 하는가.

- 길 a: 대상 없음과 같이 가운데에 꼬리 없이 둔다. 되는 것: tree 모양이 그대로라 가둠·초점·덮개가 흔들리지 않는다. 배치는 메시지를 낼 수 없으니 닫을지는 nullopt를 본 앱이 정한다. 대가: 가리키던 것이 사라진 안내가 가운데로 뛴다.
- 길 b: 풍선을 숨긴다. 되는 것: 흘려 지나간 비modal 안내가 사라졌다 돌아온다. 대가: 숨은 가지의 가둠은 없는 것이라(ui_tree.cpp 365행) modal이면 되돌리기가 터지고, 풍선 안 초점은 거둬진다(ui_interaction.cpp 4522~4528행).
- 길 c: 앞 자리를 지킨다. 되는 것: 대상이 흘러 나가도 풍선이 제자리에 남아 가둠·초점이 흔들리지 않는다. 대가: 앱이 마지막 사각형을 들고, 대상과 어긋난 꼬리가 남는다.
- 권장: a. 대가: 위. 권장대로 진행.

**3. 방향 자리 함수의 집.** 질문: 네 방향 자리 계산을 어디에 두는가.

- 길 a: `field_popup.h` 끝에 `place_field_popup_at_side`를 더하고 `place_field_popup`과 같은 논리 x·y를 낸다. 풍선은 배율을 곱해 쓴다. 되는 것: 분할 버튼 문서의 약속(230행)대로 앵커 산술이 한 헤더에 모이고 뒤 노드도 쓴다. `place_field_popup`을 고치지 않아 이미 선 popup의 test가 그대로다. 대가: 아래·위 뒤집기가 두 함수에 있다. 머리 주석(10~11행)에 tree 안 풍선 한 줄을 더한다.
- 길 b: 풍선 헤더에 꼬리까지 한 함수로 둔다. 되는 것: 답 하나에 꼬리가 든다. 대가: 앵커 산술이 두 헤더로 갈리고 230행 약속을 거둔다.
- 권장: a. 대가: 위. 권장대로 진행.

**4. 비modal의 키보드.** 질문: 비modal 풍선에 키보드 사용자는 어떻게 닿고 닫는가.

- 길 a: 초점을 옮기지 않고, 풍선 안 Esc는 몸의 처리기가 `dismiss(escape)`로 받는다. 되는 것: 앱 코드 없이 풍선 안 Esc로 닫힌다. 대가: 확정 c의 "닫기 단추로만"에 닫는 길 하나를 더해 확정 문구를 넓힌다. 몸 처리기를 비modal에서만 다는 갈래와 그 test가 는다.
- 길 b: F6을 진입 키로 둔다. 되는 것: 초점이 어디에 있든 한 키로 풍선에 닿는다. 대가: 초점이 풍선 밖이면 처리기가 키를 받지 못해(처리기는 초점에서 root 쪽으로만 흐른다, ui_interaction.cpp 3610행) 앱 단축키나 controller(줄 3)가 맡는다. 라이브러리가 탈출 키를 예약하지 않는 규칙(ui_element.h 1141행)과도 갈린다.
- 길 c: 초점을 옮기지 않고 풍선 안 Esc도 앱으로 흘린다. 풍선 단추는 Tab 끝 자리이고, 명령으로 연 앱은 초점 요청 두 줄을 싣는다. 되는 것: 확정 c의 "닫기 단추로만 닫는다"와 "Esc는 앱으로 흐른다"를 글자 그대로 지킨다. controller를 고치지 않고 하던 일을 끊지 않는다. Esc로도 닫고 싶은 앱은 `on_key`에서 열림을 보고 닫는다. 대가: 그 한 갈래는 앱 몫이고, 스스로 뜬 안내에는 Tab을 끝까지 쳐야 닿는다.
- 권장: c. 확정 설명을 따르는 길이라 따로 묻지 않는다. 대가: 위. 바깥 누름으로 닫지 않는 확정 내용도 그대로다. 권장대로 진행.

**5. 나타남 움직임.** 질문: 나타날 때 움직이는가.

- 길 a: 움직이지 않는다. 되는 것: 그리기가 가장 짧다. 대가: 갑자기 뜬 풍선이 눈에 덜 들어온다.
- 길 b: `opened_at`을 받아 요소가 시각만으로 흐려짐과 다가옴을 그린다(토스트 `opacity_at`의 규칙). 되는 것: 앱은 연 시각 하나만 들고, 움직임 줄이기는 `motion_duration`이 0으로 접는다. 대가: 칸 하나와 `next_update`가 는다.
- 권장: b. 대가: 위. 권장대로 진행.

**6. 나타남 알림.** 질문: 나타나는 순간을 보조 기술에 알리는가.

- 길 a: 알리지 않는다. 되는 것: `accessibility.h`·`uia_provider.cpp`를 고치지 않는다. modal은 진입 초점이 창 이름을 읽힌다. 대가: 스스로 뜬 비modal 안내는 읽히지 않는다.
- 길 b: UIA Notification. 되는 것: 스스로 뜬 비modal 안내도 화면 읽기가 읽는다. 대가: [정보 막대](info-bar-design.md) 결정 6의 b와 같은 변경(`access_change_kind` 갈래, 활동 id 인자, `announce_changes` 한 갈래)이고 지금 알림은 입력 칸 오류 글뿐이다([uia_provider.cpp](../src/win32/uia_provider.cpp) 437~449·643~647행).
- 권장: a. 정보 막대와 같은 선택이다. 대가: 위. 권장대로 진행.

**7. 행동 단추의 칸.** 질문: 0~2개 단추를 어떻게 받는가.

- 길 a: `primary`·`secondary` 두 이름 칸. 되는 것: 개수 상한과 모양(accent·normal)이 형에 드러나고 id가 늘 같다. 대가: 셋째가 필요하면 칸을 더한다.
- 길 b: `std::vector<teaching_tip_button>`(셋째부터 버림)과 key. 되는 것: msg_box 단추와 꼴이 같다. 대가: 버리는 규칙과 key 중복을 문서로 막는다.
- 권장: a. 대가: 위. 권장대로 진행.

**8. 그림과 닫기 단추의 자리(구현 중 정함).** 질문: 그림이 있을 때 닫기 단추는 어디 서는가.

- 길 a: 그림이 맨 위이고 닫기는 그 아래 머리 줄 오른쪽에 제목과 나란히 선다. 제목 첫 줄은 닫기의 세로 가운데에 맞춘다. 되는 것: 그림 색과 상관없이 닫기 글리프가 풍선 바탕 위에 서고 자리 계산이 줄 하나다. 대가: 그림이 있으면 닫기가 풍선의 맨 위가 아니다.
- 길 b: 닫기를 그림 오른쪽 위에 겹친다. 되는 것: 늘 풍선 오른쪽 위다. 대가: 밝은 그림 위에서 도구 막대 글리프가 묻히고, 겹친 단추의 hover 바탕을 따로 정해야 한다.
- 고름: a.

**9. 데모가 대상을 찾는 자리(구현 중 정함).** 질문: 데모의 `make_overlay`는 어느 가지에서 대상을 찾는가.

- 고름: 데스크톱은 toasts 페이지가 build에서 만든 스크롤 가지를 들고, 모바일은 셸이 `make_overlay(root.get())`로 이번 frame의 root를 넘긴다. 모바일은 셸이 페이지를 흘리는 창에 담아 페이지가 자르는 조상을 들지 않기 때문이다. 어느 쪽이든 쓴 뒤 잊는다. 상태 label은 build에서 짓고 대상은 그 뒤 `make_overlay`에서 찾으므로 label의 "대상 없음"은 한 frame 늦다. 페이지는 label이 그 frame의 답과 다르면 `next_expiry`로 지금을 답해 한 frame을 더 짓는다. 페이지를 짓지 않은 frame에는 청하지 않는다.

## 플랫폼

**Win32.** tree 안 요소라 `win32_window.cpp`·`window_surface.cpp`·`uia_provider.cpp`를 고치지 않는다. UIA는 root가 Window 또는 Pane과 Name을, 단추가 Invoke를 낸다. 풍선은 창 client 안에만 그리고 popup 배경 효과(아크릴)를 쓰지 않는다.

**Android.** Java 소스는 0줄이다. 코어 요소라 JNI도 unsupported도 없고 데스크톱과 같은 코드다. 탭은 클릭이라 덮개 탭이 `outside`, 단추 탭이 그 액션이다. 뒤로 가기는 앱이 `ui_frame::back`에 `dismiss(back)`을 싣는다([app_host.h](../include/luil/app/app_host.h) 382~387행). hover가 없어 머물면 여는 계기는 쓰지 않고 앱이 연다. 터치 앱은 `control_size`를 44 안팎으로 올린다. 회전·화면 분할 뒤에도 열림은 앱 상태라 남고 새 자리에 다시 놓인다. root 자리가 안전 영역이면 풍선도 그 안이다. Android의 luil에는 접근성 계층이 없다. 접근성 규칙은 Win32 UIA 기준이다.

## 다른 노드와의 약속

- **date_picker(R4, 같은 줄 앞).** `ui_element_id.h`(`date_field` 뒤), `inspect_names.cpp` case 여섯, `default_text.h/.cpp` 끝에 차례로 쌓는다. [ui_element_tests.cpp](../tests/ui_element_tests.cpp) 84행 `last[]`는 `teaching_tip_action`으로 맞추고 병합 때 다른 줄의 마지막 kind와 다시 맞춘다. kind 숫자를 test·UIA 스크립트에 박지 않는다. popups 페이지는 date_picker의 것이고 이 노드는 toasts 페이지에 싣는다. `field_popup.h`는 date_picker가 고치지 않으므로(date-picker-design.md 201행) 이 노드만 끝에 더한다.
- **줄 3(page_anim).** `ui_interaction.*`, `transition.h`·`spring.h`, 움직임 줄이기 연결을 고치지 않고 읽기만 한다. [page-transition-design.md](page-transition-design.md) 다른 노드와의 약속 절대로 풍선은 이전 화면 tree에 넣지 않고, 화면이 바뀌면 앱이 닫는다. 데모의 페이지 바꿈 자리는 그 줄이 고친다. 이 노드의 데모 연결은 page_anim의 `navigate_intent`·`back_intent` 변경 뒤에 쌓고, 이전 화면을 짓기 전에 풍선을 닫는다. 새 화면에서 바로 여는 안내는 `page_transition_end` 뒤에 연다. 전환 중에는 대상이 최종 자리에 있고 그림만 움직여 꼬리가 잠깐 어긋나 보이고, modal이면 덮개가 전환 위에 선다.
- **줄 2(markup)와 R5 chart.** markup 1단계 어휘에 들지 않고 이 노드가 들어온 뒤 어휘 행으로 더한다. chart의 값 상자는 `place_field_popup_at_side`를 쓸 수 있다.
- **줄 4.** `app_host.h`·`ui_platform.h`·`win32_window.cpp`·`window_surface.cpp`를 고치지 않는다. `ui_frame::back`과 `focus`는 있는 칸이다. `ui_events.h`·`accessibility.h`·`uia_provider.cpp`도 고치지 않는다.
- **데모.** 라이브러리 커밋과 데모 연결 커밋을 나누고, 연결은 통합 때 직렬로 쌓는다. `make_overlay`는 어느 페이지에서나 불리고(demo_main.cpp 727행, 모바일 app.cpp 172행) 대상 포인터는 toasts 페이지 build에서 오므로, 페이지를 떠날 때 풍선을 닫아야 낡은 포인터가 남지 않는다. 통합 때 이었다. [demo_main.cpp](../examples/demo_main.cpp)의 `navigate_intent` 갈래와 모바일 `app.cpp`의 `navigate_intent`·`back_intent` 갈래는 페이지가 바뀔 때 `make_page_motion`으로 떠나는 화면을 짓기 전에 `close_tip`을 부른다(`close_popups`에 넣지 않은 것은 메뉴가 닫힐 때 오는 `popup_close_intent`도 그 함수를 불러 풍선까지 닫기 때문이다). 모바일 `frame->back`은 시트 갈래 뒤, 페이지 닫기 앞에 열린 풍선의 `back_action`을 싣는다. demo_main.cpp 초점 요청 합치기에 toasts 갈래(`seen_toasts_focus_`)가 하나 늘어 명령으로 연 비modal 풍선이 초점을 옮긴다.

**미룬 것을 닫는다.** 설계 단계에서는 기존 문서를 고치지 않았고 구현 커밋이 고쳤다.

| 위치 | 닫는 방법 |
| --- | --- |
| split-button-design.md 184·230·259행 | `field_popup.h` 끝에 네 방향 함수를 더하고(결정 3) 꼬리는 이 노드가 그린다. 아래 한 방향 시그니처는 그대로다 |
| split-button-design.md 231행, date-picker-design.md 202·215행 | popups 페이지는 date_picker의 것이고 이 노드는 toasts 페이지로 간다 |
| info-bar-design.md 203행 | 닫기 단추 모양과 `text`·`action` 쌍을 따른다 |
| message-box-design.md 223행 | accent·normal을 쓰고 danger는 쓰지 않는다 |
| inspector-design.md 237행 | `ui_tree.*`를 고치지 않는다. 보이는 자리 helper는 이 노드의 헤더에 둔다(결정 1) |
| ink-design.md 253행, menu-focus-fixes-design.md 104행 | controller를 고치지 않아 직렬이 풀린다 |
| calendar-date-design.md 94행 | `default_text` 함수 둘을 date_picker 뒤에 붙인다 |
| components.md, tooltip-design.md | 안내 풍선 행과 "단추가 있는 상주 안내는 안내 풍선" 한 줄을 더한다 |

## 확인

자동 test는 UIA 하나만 빼고 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다.

- `tests/field_popup_tests.cpp`. 네 방향, 뒤집기 순서(바라는 방향, 반대, 남은 둘), 다른 축 가운데 맞춤과 밀기, `fits` 거짓, 배율 2에서 논리 값, 잘못된 배율. `place_field_popup`의 기존 단언은 그대로 통과.
- `tests/teaching_tip_element_tests.cpp`(새 파일). `place`의 방향별 풍선·꼬리 자리, 꼬리 밑변 물림, 대상 없음과 자리 밖 대상의 가운데·꼬리 없음, 폭 자름과 본문 줄 줄임, 배율 1·1.5·2에서 같은 논리 자리. 자식 id, `dismiss`가 비면 닫기 없음, 글과 액션이 다 있어야 행동 단추. `teaching_tip_target`의 자르는 조상·스크롤 창 밖·없는 id는 `ui_tree::visible_bounds`와 같은 답, 숨은 가지는 helper만 nullopt(`visible_bounds`는 숨김을 보지 않아 견주지 않는 따로 된 test). controller로 modal 진입(primary)·Tab 가둠·Esc의 `escape`·덮개 누름의 `outside`와 대상 미실행·닫힌 뒤 `focus_return`, 비modal의 초점 그대로·Tab 끝 자리·풍선 안·밖 Esc 모두 `dismiss` 없이 policy `on_key`로 흐름·뒤 화면 클릭. `next_update`는 나타나는 동안만이고 움직임 줄이기면 없다.
- `tests/accessibility_tests.cpp`. dialog·pane, 이름 우선순위(name, title, 기본 문구), 몸의 static_text, 단추. `tests/ui_element_tests.cpp`의 `last[]`.
- `tests/raster_draw_tests.cpp`. 배율 2에서 꼬리가 몸과 같은 바탕으로 이어지는지, 테가 꼬리까지 도는지, 고대비 팔레트, 나타나는 중간의 투명도.
- `tests/uia_provider_tests.cpp`(`luil_tests`, Windows). Window·Pane ControlType, 닫기 Invoke가 `dismiss(close_button)`을 나르는지.
- `tests/demo_gallery_tests.cpp`와 `tests/mobile_demo_page_tests.cpp`. 두 모드로 열고 닫는 메시지와 상태 label, 비modal의 초점 요청, 뒤로 가기, 대상을 흘려 내보냈을 때 label의 "대상 없음".

데모 칸. 데스크톱 toasts 페이지 첫 구역에 대상 단추(owner `toasts/tip-target`, "새 기능"), 여는 단추 둘(`toasts/tip-open-modal`·`toasts/tip-open-modeless`), label `toasts/tip-state`("안내 풍선: 열림(덮개) · 방향: 아래 · 마지막 닫힘: 닫기 단추")를 둔다. 방향은 바라는 방향이고 대상을 찾지 못한 frame 뒤에는 "대상 없음", 닫혀 있으면 "없음"이다. 행동 단추 둘도 이 데모에서는 풍선을 닫고 그 글을 마지막 닫힘에 적는다. 페이지는 build에서 대상 가지 포인터를 들고(popups_page.h 162행 `dropdown_`과 같은 꼴) `make_overlay`가 토스트 더미와 함께 풍선을 낸다(둘 다 있으면 `layer_element`에 풍선을 뒤로 담는다). root에 담는 자리는 그대로이고 모바일 호출만 root를 넘긴다(결정 9). 두 셸은 오버레이를 frame에 싣는 것과 같은 스타일 포인터로 배치한다(통합 때 더했다. 풍선이 잰 줄과 그린 글이 같은 치수다). 모바일 toasts 페이지는 같은 칸(owner `tip-target`, `tip-open-modal`·`tip-open-modeless`, 설명 `tip-state`)에 `control_size` 44와 뒤로 가기 닫힘(`back_action`)을 둔다. 두 페이지 모두 셸이 부를 `close_tip`·`tip_open`을, 데스크톱은 `make_focus_request`도 낸다. 격리 데스크톱에서 luil-uia로 여는 단추 Invoke 뒤 Window·Pane과 초점, 닫기 Invoke와 PostMessage Esc 뒤 label을 되읽는다. 실제 입력이 필요하면 먼저 요청하고 허락한 시간만 쓴다.

사람 확인: 꼬리가 대상을 바로 가리키는지와 창 가장자리 뒤집기, 배율 100%·150%·200%와 큰 글자, 밝은·어두운·고대비의 테와 꼬리, Narrator가 열 때 창 이름과 본문을 읽는 방식, 실기기에서 덮개 탭·뒤로 가기·회전 뒤 자리.

## 범위 밖

- 창 밖 걸침과 popup 배경 효과(확정 G2-surface a의 대가).
- 임의 내용 tree, 본문 마크다운과 링크. 자유로운 내용은 앱이 `modal_host`로 짓는다.
- 심각도 글리프. 정보 막대의 몫이다.
- 사라짐 움직임과 스프링 나타남(결정 5).
- 대상 id의 자동 해석(결정 1의 c), UIA 대상 관계, 나타남 알림(결정 6의 b), 비modal 풍선 안 Esc를 요소가 받는 길(결정 4의 a).
- 대상 없음일 때 구석 자리를 고르는 칸. 지금은 가운데뿐이다.
- Android 접근성 계층.

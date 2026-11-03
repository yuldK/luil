# 화면 전환 움직임

페이지를 바꿀 때 이전 화면이 밀려 나가고 새 화면이 들어오는 움직임을 요소 하나가 그린다. 앱은 떠나는 화면을 따로 지은 tree로 한 번 넘기고, 요소는 시각만의 함수로 두 화면을 겹쳐 그린다. 앱은 시작과 끝에만 tree를 짓는다. 공개 형식은 새 헤더 `include/luil/ui/page_transition.h`의 `page_transition`·`page_transition_style`·`page_transition_curve`와 짝 함수(`start_page_transition`·`page_transition_progress`·`page_transition_finished`·`page_transition_end`·`page_transition_next_update`), 새 헤더 `include/luil/ui/page_transition_element.h`의 `page_transition_element`, [spring.h](../include/luil/ui/spring.h)의 `spring_rest_time`(결정 2), 새 kind `page_transition`·`page_transition_cover`다.

이 문서는 [검사기](inspector-design.md)가 남긴 "`draw_frame`의 overlay 호출은 지금의 tree에만 둔다"(235줄), [움직임 줄이기](reduce-motion-design.md)의 page_anim 약속(149줄)과 "화면 전환 구현"(183줄), [스프링](spring-design.md)의 page_anim 곡선 자리(187줄)를 닫는다.

## 문제

- 데모의 페이지 바꿈은 값 하나를 바꿀 뿐이다. [demo_main.cpp](../examples/demo_main.cpp) 441~449줄이 `navigate_intent`에서 `page_`를 바꾸고 popup을 닫는다. 다음 `make_frame`이 `build_page`로 새 페이지만 짓는다(689줄). 이전 페이지는 그 frame에 통째로 사라진다. 모바일 데모도 같다([app.cpp](../examples/mobile_demo/app.cpp) 39~51줄의 이동·뒤로, 144~162줄의 본문).
- 움직임 값은 앱이 든다([transition.h](../include/luil/ui/transition.h) 9~16줄). 사이드바는 `spring_value`로 폭을 config에 넣고(demo_main.cpp 935줄), 도는 동안 `next_tick`이 `spring_next_update`를 내 16ms마다 tree를 다시 짓는다(836~845줄). 이 길로는 값 하나가 움직일 뿐 두 화면을 겹쳐 그릴 수 없다. 이전 화면은 tree에 없다.
- 하위 tree를 시각만으로 옮겨 그리는 요소가 없다. 독립 tree를 그리는 선례는 tooltip 하나다(`tooltip_config::content`, [ui_element.h](../include/luil/ui/ui_element.h) 1031줄). `draw_tooltip_content`([ui_tree.cpp](../src/ui/ui_tree.cpp) 60~89줄)가 옮기고 자른 canvas에 빈 snapshot으로 그린다. 그룹 알파는 요소 안의 `saveLayerAlphaf`뿐이다([color_picker_element.cpp](../src/ui/color_picker_element.cpp) 364줄, [list_element.cpp](../src/ui/list_element.cpp) 199줄).
- 그리기 밖은 모두 배치한 `bounds()`를 읽는다. hit test([ui_element.cpp](../src/ui/ui_element.cpp) 566~589줄), 놓을 자리(617~641줄), 초점 테(ui_tree.cpp 711~757줄의 `visible_bounds`), UIA 좌표다. 앱이 이전 화면을 같은 tree에 넣으면 id가 겹치고([ui_tree.h](../include/luil/ui/ui_tree.h) 94~100줄) 두 화면이 함께 눌리고 읽힌다.

## 공개 형식

`include/luil/ui/page_transition.h`, namespace `luil`. 값과 순수 함수라 Skia가 없고 core 검사 범위다. `transition.h`·`spring.h`를 들인다. `luil.h`가 include한다.

```cpp
namespace luil {
    class ui_tree;

    // 밀기(두 화면이 축을 따라 나란히 움직인다)·옅어짐(앞 절반에 이전 화면이 사라지고 뒤 절반에 새 화면이 나타난다)·확대(옅어짐 + 배율)다.
    enum class page_transition_effect { slide, fade, zoom };
    enum class page_transition_axis { horizontal, vertical };
    // 앞으로(깊이 들어감)인가 뒤로(돌아옴)인가다. 밀기의 쪽과 확대의 쪽을 정한다.
    enum class page_transition_direction { forward, backward };
    inline constexpr std::chrono::milliseconds page_transition_duration { 250 };
    using page_transition_easing = float (*)(float) noexcept;
    // 길이 곡선이다. easing은 [0, 1] → [0, 1]이다. 길이가 0 이하면 전환이 없다.
    struct page_transition_timing
    {
        std::chrono::milliseconds duration { page_transition_duration };
        page_transition_easing easing { ease_in_out };
    };
    // 스프링 곡선이다. 진행이 0에서 1로 간다. velocity는 진행 / 초다 (끌다 놓은 속도 ÷ 요소의 폭).
    struct page_transition_spring
    {
        spring_config config { spring_smooth };
        float velocity { 0.0f };
    };
    using page_transition_curve = std::variant<page_transition_timing, page_transition_spring>;
    struct page_transition_style
    {
        page_transition_effect effect { page_transition_effect::slide };
        page_transition_axis axis { page_transition_axis::horizontal };
        page_transition_direction direction { page_transition_direction::forward };
        page_transition_curve curve { page_transition_timing {} };
    };
    // 도는 전환이다. 앱 상태다 (`transition`·`spring`과 같다).
    struct page_transition
    {
        // 떠나는 화면이다. 비면 전환이 없다 (언제 물어도 끝).
        std::shared_ptr<const ui_tree> prior {};
        std::chrono::steady_clock::time_point started {};
        page_transition_style style {};
    };
    // `reduce_motion`(`window_metrics::reduce_motion`)이 참이면 prior를 버린 값이다.
    [[nodiscard]] page_transition start_page_transition(
        std::shared_ptr<const ui_tree> prior, const page_transition_style& style, std::chrono::steady_clock::time_point now, bool reduce_motion);
    // [0, 1]이다 (결정 6). 시작 전이면 0, 끝났거나 prior가 없으면 1이다.
    [[nodiscard]] float page_transition_progress(const page_transition& value, std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] bool page_transition_finished(const page_transition& value, std::chrono::steady_clock::time_point now) noexcept;
    // 끝나는 시각이다 (prior가 없으면 nullopt). 앱이 `next_tick`에 싣는다 (결정 2).
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> page_transition_end(const page_transition& value) noexcept;
    // `transition_next_update`와 같은 계약이다. 요소의 `next_update`가 쓴다.
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> page_transition_next_update(
        const page_transition& value, std::chrono::steady_clock::time_point now) noexcept;
}
```

`spring.h` 끝에는 `[[nodiscard]] inline std::chrono::steady_clock::time_point spring_rest_time(const spring& value, std::chrono::milliseconds limit = std::chrono::seconds { 10 }) noexcept`를 더한다. `started`부터 `transition_frame` 간격으로 재어 처음 끝난 시각을 답하고, `limit` 안에 끝나지 않으면 `started + limit`이다.

`include/luil/ui/page_transition_element.h`다.

```cpp
namespace luil {
    struct page_transition_config
    {
        // 요소 id의 owner다 (`{ page_transition, owner }`, 덮개는 `{ page_transition_cover, owner }`).
        std::u8string owner {};
        page_transition transition {};
    };

    // 새 화면(content)을 담고, 전환이 도는 동안 이전 화면을 시각만으로 함께 그린다.
    //  - 이전 tree는 자식이 아니다. 색인·hit·초점·접근 tree에 없다.
    //  - prior가 있는 동안 마지막 자식으로 투명한 덮개(hit_opaque)를 둔다.
    class page_transition_element final : public ui_element
    {
    public:
        page_transition_element(page_transition_config config, std::unique_ptr<ui_element> content);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
    };
}
```

앱은 이렇게 쓴다(데스크톱 데모의 모양). 이동 메시지에서 `page_`가 아직 옛 값일 때 지난 frame의 본문 자리(`content_slot_`)와 배율로 떠나는 화면 `prior`를 `build_page`로 따로 짓고 `arrange`한 뒤 넘기고, 그다음 `page_`를 바꾼다. `make_frame`은 늘 감싼다(prior가 비면 새 화면만 그리는 껍질). `next_tick`은 `page_transition_end`를 견주고 `tick`은 끝났으면 놓는다.

```cpp
page_motion_ = luil::start_page_transition(std::make_shared<const luil::ui_tree>(std::move(prior)), page_style_, now, metrics_.reduce_motion);
shell->add_flexible(std::make_unique<luil::page_transition_element>(luil::page_transition_config { u8"page", page_motion_ }, build_page(w, h, scale)));
```

## 규칙

**효과.** p는 `page_transition_progress`다. 이전 화면을 먼저, 새 화면을 위에 그린다.

| 효과 | forward | backward |
| --- | --- | --- |
| `slide` | 새 화면이 끝 가장자리(가로는 오른쪽, 세로는 아래)에서 `(1 - p)·폭`만큼 떨어져 들어오고 이전 화면이 `p·폭`만큼 시작 쪽으로 나간다 | 쪽이 반대다 |
| `fade` | p < 0.5에서 이전 화면 알파 `1 - 2p`, 뒤 절반에 새 화면 알파 `2p - 1`이다. 두 화면이 반투명으로 겹치지 않는다 | 같다 |
| `zoom` | `fade`의 알파에 이전 화면 1 → 1.04배, 새 화면 0.94 → 1배를 얹는다 | 이전 1 → 0.94배, 새 1.04 → 1배 |

- 배율은 요소 bounds의 가운데가 기준이다. 밀기 거리는 기기 픽셀로 반올림해 글이 반 픽셀에 걸려 번지지 않게 한다.
- 요소는 `clip_children`이 참이다. 밀리는 화면이 칸 밖으로 새지 않고, frozen_col 선례대로 그리기·hit·`visible_bounds`·접근 좌표가 같은 상자를 따른다([frozen-columns-design.md](frozen-columns-design.md) 13줄).

**이전 tree.**

- 앱이 지은 독립 tree다. root를 요소가 설 자리와 **같은 크기·같은 배율**(물리 픽셀)로 배치한다. 원점은 달라도 된다. 요소가 root의 왼쪽 위를 자기 bounds의 왼쪽 위에 맞춰 옮긴다. tooltip처럼 배율 1로 짓고 키워 그리면 글이 번지므로 그렇게 하지 않는다.
- 넘기는 tree에는 전환 요소를 넣지 않는다. 넣으면 안의 전환이 `draw_context::now`로 따로 돈다. modal·시트·토스트처럼 root에 겹치는 것도 넣지 않는다. 화면 본문만이다.
- 요소는 `prior->root()->draw`를 빈 snapshot(`interaction_snapshot {}`)으로 부른다. hover·누름·caret·초점 테·tooltip이 없다(`draw_focus_ring`은 `focus_visible`이 거짓이면 돌아간다). 이전 tree의 `next_update`는 합치지 않는다. tooltip은 내용의 예고를 합치지만(ui_tree.cpp 519줄) 떠나는 화면의 도는 표시는 전환 frame에 그려질 뿐이다.
- 넘기는 순간은 페이지를 바꾸는 메시지를 처리할 때 한 번이다. 놓는 순간은 `tick`에서 `page_transition_finished`가 참일 때, 새 전환이 덮을 때(결정 4), 움직임 줄이기가 켜질 때다. 놓기 전까지 이전 화면의 그림·글 배치 cache가 산다.
- `shared_ptr<const ui_tree>`라 logic thread가 짓고 UI thread가 읽기만 한다. tooltip 내용과 같은 규칙이다.

**곡선.** 길이는 `easing(clamp((now - started) / duration, 0, 1))`이고 끝은 `started + duration`이다. 스프링은 `spring { 0, 1, started, config, velocity }`를 `spring_value`로 묻고 끝은 `spring_rest_time`이다. 허용치는 0.001을 넘지 않게 쓰고 진행은 [0, 1]로 자른다(결정 6). 두 곡선 모두 시작 전이면 0이다.

**입력과 초점.**

- prior가 있는 동안(시각과 무관하게) 요소는 마지막 자식으로 덮개를 담는다. 덮개는 칠하지 않는 `hit_opaque` 판이다(scrim은 진하기 0이어도 막는다는 [modal_host_element.h](../include/luil/ui/modal_host_element.h) 29~31줄 선례). 누름·hover·커서·끌어 놓기가 덮개에서 멈춘다. `drop_hit_test`는 virtual이 아니고 자식부터 걷기 때문에(ui_element.cpp 617~641줄) `hit_test` 재정의가 아니라 자식으로 둔다. `hit_test`는 시각을 받지 않아 요소가 끝을 스스로 알 수 없다. 입력이 풀리는 것은 앱이 prior 없는 tree를 낼 때다. 그래서 끝 시각을 앱에 알리는 길이 필요하다(결정 2).
- 키는 막지 않는다. 키는 자리와 무관하고 controller가 새 tree의 초점 자리에 보낸다. Tab은 새 화면으로 들어간다.
- 초점은 새 tree에만 선다. controller가 받는 것은 발행한 tree 하나이고(`set_tree`, [ui_interaction.cpp](../src/ui/ui_interaction.cpp) 168~172줄) 사라진 자리의 초점은 `update_focus`가 거둔다. controller의 `prior_tree_`([ui_interaction.h](../include/luil/ui/ui_interaction.h) 904줄)는 닫힌 popup 누름 판정 전용이라 이 전환과 무관하다. **controller를 고치지 않는다.**
- 새 화면 안에 키보드 초점이 있는 채 밀기·확대가 돌면 초점 테와 IME 후보 창은 최종 자리에 먼저 선다(결정 1의 대가). 전환이 끝나 tree가 바뀌면 `set_tree`가 hover를 다시 판정해 포인터 아래 자리가 곧 선다.

**접근성(Win32 UIA).**

- 새 화면은 시작 frame부터 접근 tree에 서고 이전 화면은 어느 frame에도 없다. 요소와 덮개는 역할 `none`이라 접혀, 새 화면이 부모 아래 바로 읽힌다.
- 새 화면 알림 API는 두지 않는다. 자식 목록이 바뀌면 `diff_access_snapshots`가 부모의 structure 변화를 내고([accessibility.cpp](../src/ui/accessibility.cpp) 437~443줄) UIA가 StructureChanged를 쏜다([uia_provider.cpp](../src/win32/uia_provider.cpp) 589~599줄). 제목을 읽히려는 앱은 새 화면의 머리글로 `focus_request`를 싣는다([색 고르기](color-picker-design.md) 결정 3의 길).
- UIA 좌표는 최종 bounds다. Invoke 같은 패턴은 포인터가 아니라 덮개에 막히지 않는다.

**배율·크기·회전.** 그릴 때 이전 root의 크기가 요소 bounds의 크기와 다르면(창 크기 바뀜·배율 바뀜·기기 회전) 새 화면만 그리고 예고를 멈춘다(결정 5). R1의 두 손가락 회전([rotate-gesture-design.md](rotate-gesture-design.md))은 확대 보기 내용이 `canvas.rotate`로 그리는 일이라 이 요소와 닿지 않는다.

**테마·고대비.** 요소와 덮개는 제 색이 없다. 이전 tree는 지금 문맥의 팔레트로 그리므로 도중에 테마나 고대비가 바뀌면 이전 화면도 새 색이다. layered presentation에서 옅어짐의 반투명 픽셀은 그 아래 바탕과 합성된다. 바탕 알파가 0인 창의 규칙은 그대로다([layered-presentation-design.md](layered-presentation-design.md) 95줄).

**움직임 줄이기.** [reduce-motion-design.md](reduce-motion-design.md) 149줄의 약속대로다.

- `start_page_transition`은 `reduce_motion`이 참이면 prior를 버린다. 짓는 비용까지 아끼려는 앱은 먼저 `window_metrics::reduce_motion`을 보고 이전 화면을 짓지 않는다. 옅어짐으로 바꾸지 않는다. `motion_duration`이 모든 전환을 0으로 두는 규칙과 같다.
- 도는 중에 켜지면 요소가 `draw_context::reduce_motion`을 읽어 `transition_*`·`spring_*`를 부르기 전에 끝으로 보고 새 화면만 그린다. `update_context::reduce_motion`이면 예고도 없다. 앱은 metrics를 받으면 전환을 놓는다(사이드바 선례, demo_main.cpp 433~438줄).

**기본 문구.** 없다. 요소와 덮개는 이름이 없어 `src/ui/default_text.*`에 더할 것이 없다.

## 결정

- 확정(2026-11-03): G1-state 길 a. 앱이 이전 화면을 따로 지은 tree로 한 번 넘기고 전환 요소가 시각만으로 두 화면을 그린다. 근거: [transition.h](../include/luil/ui/transition.h) 9~16줄, `tooltip_config::content`, [inspector-design.md](inspector-design.md) 235줄.
- 확정(2026-11-03): G1-curve 길 c. 길이(+ easing) 또는 `spring_config`를 받는 곡선 칸이다. 근거: [spring-design.md](spring-design.md) 187줄, [spring.h](../include/luil/ui/spring.h) 144~178줄.

**1. 새 화면도 움직이는가.** 질문: 전환 동안 새 화면을 옮겨 그리는가, 제자리에 두고 이전 화면만 움직이는가.

- 길 a: 이전 그림만 움직인다. 새 화면은 제자리에 그리고 이전 화면이 그 위에서 밀려 나가거나 사라진다. 되는 것: 그림과 hit·초점 테·IME·UIA 좌표가 늘 맞아 덮개 없이 입력을 받을 수 있다. 대가: 앞으로 가기가 위 화면을 걷어 내는 모양이 되어 새 화면이 들어오는 흔한 밀기를 못 한다. 옅어짐은 새 화면 위에 이전 화면이 겹쳐 비친다.
- 길 b: 두 화면이 함께 움직인다(위 표). 되는 것: 플랫폼들의 익숙한 밀기·확대이고 옅어짐에서 두 화면이 겹쳐 비치지 않는다. 대가: 도는 동안 포인터 입력을 막고(확정 길 a의 대가와 같다) 새 화면 안 키보드 초점의 테가 먼저 제자리에 선다.
- 권장: b. 대가: 위. 입력 막기는 길 a를 고를 때도 남겨 두면 되돌리기 쉽다. **승인 필요.**

**2. 끝을 앱이 아는 길.** 질문: 앱은 언제 prior를 놓는가.

- 길 a: 스프링은 앱이 frame마다 깨어 본다(`spring_next_update`, 사이드바 선례 demo_main.cpp 841줄). 되는 것: 새 함수가 없다. 대가: 스프링 전환 동안 앱이 16ms마다 tree를 다시 짓는다. 확정 길 a가 피하려던 비용이다.
- 길 b: `spring.h`에 `spring_rest_time`을 더하고 `page_transition_end`가 두 곡선 모두 시각 하나를 답한다. 되는 것: 앱은 `next_tick`에 시각 하나를 싣고 `tick` 한 번에 놓는다. 끝 판정의 척도는 줄기만 하므로([spring-design.md](spring-design.md) 108줄) 처음 끝난 frame이 하나로 정해진다. 대가: 줄 3의 `spring.h`에 함수 하나와 test가 는다. 10초 상한에서 평가는 많아야 625번이다.
- 길 c: 요소가 끝에 앱 메시지를 낸다. 되는 것: 앱이 시각을 셈하지 않는다. 대가: 요소는 그리기에서 메시지를 낼 길이 없다. host가 예고 시각에 action을 내는 새 경로를 `frame_state`와 두 플랫폼에 세워야 한다.
- 권장: b. 대가: 위. 권장대로 진행.

**3. 이전 tree 그리기.** 질문: 이전 화면을 매 frame 그리는가, 그림으로 굳히는가.

- 길 a: 매 frame `prior->root()->draw`를 부른다. 되는 것: 배율·팔레트·고대비가 지금 문맥을 따르고 상태가 없어 시각만의 함수 규칙이 그대로다. 대가: 전환 동안 그리기가 두 배다. 무거운 화면은 CPU 렌더러와 큰 창에서 frame이 늦을 수 있다(사람 확인).
- 길 b: 첫 frame에 `SkPicture`나 `SkImage`로 굳힌다. 되는 것: 매 frame 비용이 그림 한 장이다. 대가: const `draw` 안에 mutable cache가 서고 테마·배율이 바뀌면 다시 굳힌다. 이미지면 GPU·CPU 렌더러마다 면 수명이 갈린다.
- 권장: a. 대가: 위. 실측에서 늦으면 굳히기를 요소 안에서 더할 수 있고 공개 형식은 그대로다. 권장대로 진행.

**4. 도는 중에 또 바뀔 때.** 질문: 전환이 끝나기 전에 다시 페이지를 바꾸면 무엇을 이전 tree로 두는가.

- 길 a: 끊고 새 전환. 앱은 들어오던 화면을 독립 tree로 다시 지어 새 prior로 넘기고 `started`는 지금이다. 옛 prior는 놓는다. 되는 것: 늘 두 화면이고 규칙이 하나다. 대가: 반쯤 들어온 화면이 제자리로 한 번 튄 뒤 나간다.
- 길 b: 진행을 이어받는다(`page_transition_style`에 시작 진행 칸). 되는 것: 튐이 없다. 대가: 옛·중간·새 세 화면의 자리를 정해야 하고 효과마다 잇는 식이 다르다.
- 길 c: 도는 동안 바꿈을 미룬다(앱의 대기 칸). 되는 것: 튐이 없다. 대가: 사이드바를 빨리 누르면 반응이 늦고 앱 상태가 는다.
- 권장: a. 대가: 위. 권장대로 진행.

**5. 도중에 크기·배율이 바뀔 때.** 질문: 이전 tree의 배치가 지금 자리와 어긋나면 어떻게 하는가.

- 길 a: 요소가 이전 root와 자기 크기가 다르면 새 화면만 그린다. 되는 것: 앱 코드 없이 창 끌기·배율 바뀜·기기 회전 도중 옛 크기 그림이 남지 않는다. 대가: 전환이 한 번에 끝나 보이고, 포인터 입력은 앱이 prior를 놓을 때까지(남은 시간만큼) 막힌다.
- 길 b: 앱이 metrics 메시지에서 prior를 놓는다. 되는 것: 입력 막힘도 바로 풀린다. 대가: 앱마다 한 줄을 잊을 수 있고 잊으면 옛 크기 그림이 남는다.
- 길 c: 옛 크기 그대로 그리고 자른다. 되는 것: 할 일이 없다. 대가: 빈 띠나 잘린 그림이 보인다.
- 권장: a. 대가: 위. 데모는 b를 함께 한다. 권장대로 진행.

**6. 스프링 진행 규칙.** 질문: 0~1 진행에 스프링의 허용치와 넘침을 어떻게 맞추는가.

- 길 a: config를 그대로 쓴다. 되는 것: 앱이 준 값이 곧 동작이다. 대가: 기본 허용치 0.01이면 마지막 1%가 한 번에 붙고(1280픽셀 밀기에서 13픽셀), ζ가 1보다 작으면 밀기가 넘쳐 가장자리에 바탕 띠가 보인다.
- 길 b: 허용치를 0.001 이하로 쓰고 진행을 [0, 1]로 자른다. 되는 것: `spring_snappy` 같은 프리셋을 그대로 넣어도 끝이 매끄럽고 빈 띠가 없다([spring.h](../include/luil/ui/spring.h) 20~22줄이 권하는 값이다). 대가: ζ < 1 곡선은 넘침이 잘려 끝에서 잠깐 멈춘 듯 보인다. 앱이 준 더 큰 허용치는 쓰이지 않는다.
- 길 c: 진행을 논리 픽셀 거리(요소 폭)로 돌린다. 되는 것: 허용치가 스프링 기본의 뜻 그대로다. 대가: `page_transition_end`가 배치 크기를 알아야 해 앱이 폭을 넘기고, 옅어짐·확대에는 거리가 없다.
- 권장: b. 대가: 위. 권장대로 진행.

## 플랫폼

### Win32

고칠 플랫폼 파일이 없다. 요소의 `next_update`를 [win32_window.cpp](../src/win32/win32_window.cpp) 2070~2113줄의 다시 그리기 timer가 받고, 움직임 줄이기는 `read_reduce_motion`(202줄)이 이미 채운다. `win32_window.cpp`·`window_surface.cpp`·`surface_input.cpp`를 고치지 않는다. `draw_frame`([frame_state.cpp](../src/host/frame_state.cpp) 107~115줄)도 그대로라 검사기는 지금 tree만 본다. WebView2와 네이티브 자식 표면은 표면 좌표의 구멍과 창으로 서서 함께 움직이지 않는다. 그런 화면은 전환하지 않는다.

### Android

같은 Skia 코드이고 Java 소스는 0줄이다. 예고는 [android_app.cpp](../src/android/android_app.cpp) 1838~1839줄이 받고, `reduce_motion`은 R1 이후 1774·1838줄이 채운다(조사 자료의 "Android는 채우지 않는다"는 낡았다). 뒤로 가기는 `ui_frame::back`([app_host.h](../include/luil/app/app_host.h) 387줄)이 내는 메시지에서 앱이 backward 전환을 시작한다. 손가락을 따라가는 예측형 뒤로 가기는 `OnBackAnimationCallback` 인터페이스 구현이 필요해 unsupported다. Android에는 접근성 계층이 없어 위 접근성 규칙은 Win32에만 해당한다.

## 다른 노드와의 약속

- **줄 3 안**: controller(`ui_interaction.*`)와 `transition.h`를 고치지 않는다. `spring.h`는 끝에 `spring_rest_time` 하나를 더한다(결정 2). 다른 줄은 이 둘을 읽어 쓰기만 한다.
- **teaching_tip(줄 1)**: 나타남은 `transition.h`와 움직임 줄이기를 읽어 쓰고 이 요소를 쓰지 않는다. tip·modal은 root에 겹치는 것이라 이전 tree에 넣지 않는다. 화면이 바뀌면 앱이 닫는다(데모가 popup을 닫는 자리와 같다).
- **kind와 이름표**: `page_transition`·`page_transition_cover` 두 줄을 [ui_element_id.h](../include/luil/ui/ui_element_id.h)의 `end_of_library_kinds` 바로 위에, case 둘을 `src/ui/inspect_names.cpp` 끝에 같은 커밋으로 더한다. date_picker·teaching_tip·markup도 끝에 더하므로 병합은 기계적이다. `tests/ui_element_tests.cpp`의 `last[]`는 병합 때 마지막 kind로 맞춘다.
- **markup(줄 2)**: 마크업이 화면을 선언하면 이 요소의 이름과 config를 쓴다. 이 노드는 마크업 어휘를 정하지 않는다.
- **검사기**: [inspector-design.md](inspector-design.md) 235줄의 약속을 `draw_frame`을 고치지 않는 것으로 지킨다. 도는 중에 `inspect_at`은 덮개를 답한다.
- **auto_update(줄 4)**: 닿는 파일이 없다. `app_host.h`·`app_delegate.h`·`ui_platform.h`에 칸을 더하지 않는다.
- **R5 i18n**: 오른쪽에서 왼쪽으로 쓰는 앱에서 가로 forward의 쪽을 뒤집는 것은 그 노드가 정한다. 지금은 가로 forward가 오른쪽에서 들어온다. **R5 hot_reload**: 다시 읽은 화면으로 바꿀 때 이 요소를 쓸 수 있다.
- **데모 파일**: `examples/demo_main.cpp`(이동 441~449줄, `make_frame` 689줄, `next_tick`·`tick` 836~851줄, 사이드바 label 716~722줄 옆), `examples/demo/shell_menu.cpp`·`.h`(보기 메뉴), `examples/mobile_demo/app.cpp`·`app.h`·`device_page.cpp`는 여러 줄이 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고, 데모 연결은 통합 때 직렬로 쌓는다.

## 확인

자동 test. 모두 `luil_core_tests`다(마지막 UIA 한 건만 `luil_tests`).

- `tests/page_transition_tests.cpp`(추가), 순수 함수: 길이 곡선은 시작 전 0, 가운데 `ease_in_out(0.5)`, 끝 1이고 `page_transition_end`는 `started + duration`이다. 예고는 시작 전 시작 시각, 도는 중 16ms 뒤, 끝나면 nullopt다. 스프링은 `spring_rest_time` 한 frame 앞에서 끝나지 않고 그 시각에 끝난다. 프리셋 셋의 진행이 늘 [0, 1]이고, 허용치 0.01을 준 config도 끝 직전 진행이 0.999 이상이다. `reduce_motion`이 참인 `start_page_transition`은 prior가 비고 끝나 있으며 `page_transition_end`가 nullopt다.
- 같은 파일, 그림: `raster_frame::draw`([raster_probe.h](../tests/raster_probe.h) 39줄)에 고정 시각을 준다. 글 없는 색 판 두 장(이전·새)으로 밀기 forward·backward의 시작·가운데(경계가 가운데 열)·끝, 옅어짐 p = 0.25에서 이전 알파 0.5, 확대 끝의 새 화면만, 요소 bounds 밖 픽셀 불변, `reduce_motion` 문맥과 크기가 다른 이전 root에서 새 화면만을 본다.
- 같은 파일, tree: 이전 tree에만 있는 id는 `find`·`focus_order`·`access_top_level`·`ids_of_kind`에 없고, 같은 id가 두 tree에 있어도 `duplicate_ids`가 빈다. prior가 있으면 `hit_test`가 덮개를, `find_drop_target`이 막힘을 답하고, 없으면 새 화면의 단추를 답한다. 요소의 `next_update`는 도는 중 `now + 16ms`, 끝과 `reduce_motion`에서 nullopt다.
- `tests/spring_tests.cpp`: `spring_rest_time`이 `spring_finished`와 맞고 끝나지 않는 config에서 상한을 답한다.
- `tests/ui_inspect_tests.cpp`의 kind 이름 전수 test와 `tests/ui_element_tests.cpp`의 kind 대역 test(`library_kind_name`은 `default` 없는 switch라 Android 빌드도 case를 요구한다), 우산 헤더 검사, `luil_core_portability`, `scripts/check_source_style.ps1`.
- `tests/mobile_demo_page_tests.cpp`: 기기 페이지의 전환 스위치와 label이 두 상태에서 맞는 글이다.
- `luil_tests`의 `tests/uia_provider_tests.cpp`: 전환 중인 tree의 UIA 자식이 새 화면 것뿐이다.

데모.

- 데스크톱: 보기 메뉴에 "페이지 전환" 하위 메뉴(라디오: 없음·밀기·옅어짐·확대·스프링 밀기)를 둔다. 사이드바 목록이 세로라 밀기는 세로 축이고, 목록에서 아래 페이지로 가면 forward, 위면 backward다. `sidebar-motion` 옆에 label `page-transition-state`("페이지 전환: 밀기 · 도는 중" / "· 멈춤" / "꺼짐")를 둔다. 인자 `--page-transition=slide|fade|zoom|spring`과 UIA가 도는 중을 읽도록 길이를 늘리는 `--page-transition-ms=`를 더한다. 웹뷰 페이지로 오가는 바꿈은 전환하지 않는다. UIA(격리 데스크톱, luil-uia)는 `--page-transition-ms=3000`으로 띄워 페이지를 바꾸고, 도는 동안 label이 "도는 중"이고 새 페이지 요소가 있으며 옛 페이지 요소가 없는지, 끝난 뒤 "멈춤"인지 되읽는다.
- 모바일: 좁은 화면의 목록 → 페이지는 가로 forward 밀기, 앱 바·시스템 뒤로는 backward, 넓은 화면의 오른쪽 칸은 옅어짐이다. 기기 페이지의 움직임 줄이기 칸 아래에 "페이지 전환" 스위치와 label `page-transition-state`를 둔다.

사람 확인.

- 60Hz에서 끊기지 않는지(GPU·CPU 렌더러, 큰 창, layered presentation), 밀기 중 글이 번지지 않는지와 확대 중 글 품질, 키보드로 사이드바를 오가며 초점 테가 튀지 않는지.
- Windows 설정에서 애니메이션 효과를 끄면 바로 바뀌는지(시스템 설정을 바꾸는 일이라 사람이 한다), 실기기에서 목록 탭과 뒤로 가기 키의 체감과 개발자 옵션의 애니메이터 배율 0에서 바로 바뀌는지.

## 범위 밖

- 공유 요소(Hero) 짝짓기와 사각형 보간.
- popup·보조 창 표면의 전환. 주 tree 안의 요소만이다.
- 끄는 동안 진행을 손가락이 정하는 뒤로 가기. 놓은 뒤는 스프링 `velocity`로 된다. Android의 예측형 뒤로 가기는 Java가 필요하다.
- WebView·네이티브 자식 표면이 든 화면의 전환.
- 앱이 주는 효과 함수(사용자 정의 그리기). `page_transition_effect` 끝에 값을 더하는 길로 자란다.
- [docs/concepts/immutable-tree.md](concepts/immutable-tree.md)의 보강은 구현 커밋에서 한다.

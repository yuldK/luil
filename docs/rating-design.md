# 별점

사용자가 별 몇 개로 점수를 매기게 하고, 같은 모양으로 평균 점수를 보여 준다. 별을 누르거나 끌거나 키로 옮기면 앱이 준 절대 메시지(`change_to`)가 나가고, 그 메시지가 없으면 표시 전용이다. 마우스와 펜은 별 위에서 채워질 모양을 미리 보여 준다. 공개 형식은 [rating_element.h](../include/luil/ui/rating_element.h)의 `rating_config`·`rating_precision`·`rating_element`다.

값을 조작하는 규약은 `slider_element`를, 표시 전용의 선례는 `progress_element`를 따른다. 접근 이름과 값 글은 [default-text-design.md](default-text-design.md)의 헤더 규칙을 따른다.

## 문제

앱이 별점을 만들려면 slider를 비틀거나 단추 다섯을 직접 놓아야 한다.

- `slider_element`는 연속 막대다. 눈금 반올림은 값을 가진 앱의 몫이라고 못 박혀 있어([slider_element.h](../include/luil/ui/slider_element.h) 25행) 값이 별 수로 떨어지지 않는다. 별 모양도, 같은 별을 다시 눌러 지우는 규칙도 없다.
- 단추 다섯이면 접근 tree에 단추 다섯이 선다. 보조 기술이 읽어야 하는 것은 값 하나를 가진 슬라이더 하나다. 키 걸음, UIA 절대 값 요청, 값 글을 앱마다 다시 짓는다.
- 미리 보기를 그릴 근거가 없다. `interaction_snapshot::hovered`는 id 하나이고([ui_element.h](../include/luil/ui/ui_element.h) 859행), 포인터 자리 `hover_point`는 tooltip이 있는 요소에서만 기록되며 지연이 지나면 멈춘다(874~879행). element 하나의 `draw`는 포인터가 어느 별 위인지 모른다.
- 평균 같은 소수 값을 별의 일부로 채워 보이는 표시 요소가 없다. `progress_element`는 0..1 막대이고 눌리지 않는다([progress_element.h](../include/luil/ui/progress_element.h)).
- 스크롤 안에서 끌기가 부딪힌다. slider가 쓰는 시작 거리 0의 손잡이는 누르는 즉시 조작이라 스크롤 후보가 아니다([touch-pen-input-design.md](touch-pen-input-design.md) 145행). 별 줄 위에서 시작한 쓸기가 창을 흘리지 못한다.
- 기본 문구가 없다. [default-text-design.md](default-text-design.md) 165행이 이 노드로 넘긴 "별점 N / M" 꼴의 값 이름을 앱이 매번 짓는다.

R1 설계 문서 아홉 편에서 별점을 찾았으나 이 노드로 미룬 항목은 없다.

## 공개 형식

```cpp
// include/luil/ui/rating_element.h
inline constexpr std::size_t rating_default_count { 5 };
inline constexpr std::size_t rating_max_count { 20 };
inline constexpr float rating_default_cell_size { 28.0f };  // 논리 픽셀. 별 한 칸의 한 변이다
inline constexpr float rating_star_inset { 4.0f };  // 칸 안쪽 여백이다. 별은 칸보다 한 변이 8 작다
inline constexpr float rating_drag_start_distance { 6.0f };  // 물리 픽셀. 누른 채 이 거리를 넘으면 끌기다

enum class rating_precision
{
    whole,  // 한 별이 한 걸음이다
    half,  // 반 별이 한 걸음이다
};

struct rating_config
{
    std::u8string owner {};  // 상자와 칸의 owner다
    std::size_t count { rating_default_count };  // 별 수다. 1 이상 `rating_max_count` 이하로 자른다
    // 앱 상태다. 0은 아직 매기지 않음이다. [0, count]로 자르고 소수는 별을 일부만 채워 그린다.
    float value { 0.0f };
    rating_precision precision { rating_precision::whole };
    // 값을 이것으로 하라는 절대 메시지다. 눌림, 끌기, 키, 보조 기술이 모두 이 하나를 탄다.
    // 없으면 표시 전용이다. 칸도 커서도 Tab 자리도 서지 않고 보조 기술은 읽기만 한다 (slider에서는 `change`가 가르던 자리다).
    std::function<input_action(float value)> change_to {};
    // 참이면 칸의 값이 현재 값과 같은 칸을 다시 눌러 0으로 지울 수 있다. 거짓이면 한 번 매긴 뒤에는 0으로 돌아갈 수 없다(최소가 한 걸음이다).
    bool clearable { true };
    float control_size { 0.0f };  // 별 칸의 한 변이다. 0이면 `rating_default_cell_size`. 터치 앱이 올린다
    std::u8string name {};  // 접근 이름이다. 비면 기본 문구(별점)
    // 접근 값 글이다. 비면 기본 문구("3 / 5", 소수면 "3.5 / 5")
    std::function<std::u8string(float value, std::size_t count)> value_name {};
};

class rating_element final : public ui_element
{
public:
    explicit rating_element(rating_config config);
    // 둘 다 헤더 안에 정의한다. 선언만 보이면 다른 번역 단위에서 링크가 깨진다 (badge_element.h의 height_for와 같다).
    [[nodiscard]] static constexpr float height_for(const rating_config& config) noexcept;  // 칸의 한 변
    [[nodiscard]] static constexpr float width_for(const rating_config& config) noexcept;  // 별 수 곱하기 칸의 한 변
    [[nodiscard]] static constexpr std::size_t count_of(const rating_config& config) noexcept;  // [1, rating_max_count]로 자른 별 수 (결정 9)
    [[nodiscard]] static constexpr float step_of(const rating_config& config) noexcept;  // 한 걸음의 값. 정수 별 1, 반 별 0.5 (결정 9)
    // 범위 안으로 자른 값이다. NaN은 0이다. 그리기와 test가 같은 함수를 쓴다.
    [[nodiscard]] static float clamp_value(const rating_config& config) noexcept;
    // 만들 수 있는 가장 작은 값이다. 지울 수 있거나 값이 0이면 0, 아니면 한 걸음이다.
    [[nodiscard]] static float minimum_for(const rating_config& config) noexcept;
    // arrange 뒤에 유효하다. 가로 자리(물리 픽셀)가 정하는 값이다. 왼쪽 밖은 최소, 오른쪽 밖은 `count`다.
    // 왼쪽 밖의 최소는 `minimum_for`와 달리 지금 값을 읽지 않는다 (결정 10).
    [[nodiscard]] float value_at(float x) const noexcept;
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;
    [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;
};

// include/luil/ui/accessibility.h, access_range 끝 (결정 7의 b)
float step { 0.0f };  // 화살표 한 번이 옮기는 값이다. 0이면 알리지 않는다
float page { 0.0f };  // Page 키 한 번이 옮기는 값이다. 0이면 알리지 않는다
```

```cpp
luil::rating_config rating {};
rating.owner = u8"review";
rating.value = review_stars_;
rating.change_to = [](const float value) { return luil::make_app_action(review_rating_intent { value }); };
const float height { luil::rating_element::height_for(rating) };
const float width { luil::rating_element::width_for(rating) };
column.add(std::make_unique<luil::rating_element>(std::move(rating)), { .length = height, .cross_length = width });
```

## 규칙

**구성.** root(`rating`)는 접근 역할 slider다. 별, 채움, 미리 보기는 root가 직접 그리고 `draw_children`을 부른다. `change_to`가 있으면 걸음 하나마다 자식 칸(`rating_cell`)을 더한다. 정수 별은 `count`개, 반 별은 `2 * count`개다. 칸의 owner는 `owner`, U+001F, 칸 번호(1부터 십진수) 순이다.

- 칸은 hit와 hover와 끌기만 맡는다. 그리지 않고, 접근 역할은 none, Tab 자리는 아니며(`set_tab_stop(false)`), 손 모양 커서를 든다. 눌러서 얻는 초점은 root로 보낸다(`set_pointer_focus_target`). 칸을 누른 뒤 곧바로 ← →가 먹히게 하려는 것이다.
- root는 Tab 자리이고 `key_step_target`(가로 축)을 든다. 키는 초점을 따르므로 hit 대상은 넓히지 않는다. 표시 전용에는 칸도 커서도 키 역할도 없다.
- kind 둘(`rating`, `rating_cell`)은 `ui_element_id.h`의 `end_of_library_kinds` 바로 위에 더한다. test와 UIA 스크립트에 kind 숫자를 박지 않는다.
- root의 bounds는 별 줄이 차지한 폭이다 (결정 11). 비활성은 앱이 root에 거는 `set_enabled(false)`이고 칸이 arrange에서 따라간다 (결정 12).

**치수.** 논리 픽셀이고 그릴 때 배율을 곱한다.

| 부분 | 값 |
| --- | --- |
| 칸의 한 변 | `control_size`, 0이면 28. 자리 폭 / 별 수보다 크면 그 값으로 줄인다. 0 이하나 NaN은 기본 28이다 |
| 높이와 폭 | `height_for`는 한 변, `width_for`는 별 수 곱하기 한 변. 자리가 더 넓으면 왼쪽에 붙이고 남는 자리는 비운다. root의 bounds가 별 줄의 폭이라 남는 자리는 hit도 초점 테도 받지 않는다 |
| 별 | 칸 안 가운데, 한 변은 칸보다 8 작다. 칸이 16보다 좁으면 여백이 칸의 1/4로 줄어 별이 칸의 절반 아래로 작아지지 않는다. 윗꼭짓점이 아랫꼭짓점보다 더 오르므로 세로 가운데는 별의 경계 상자로 맞춘다. 안쪽 반지름은 바깥 반지름의 0.45배다 (결정 13) |
| hit 칸 | 걸음 하나의 폭 곱하기 자리 높이. 칸이 빈틈없이 이어져 별 사이도 칸이다 |

터치 앱은 한 변을 44(모바일 데모의 `touch_height`)로 올린다. 별 다섯이 220이다. 반 별은 칸 폭이 별의 절반(28이면 14, 44면 22)이라 터치에서는 더 올려야 한다.

**값.** 걸음 `s`는 정수 별 1, 반 별 0.5다. 격자점은 `s`의 정수배다. 증가 걸음은 현재 값보다 큰 가장 가까운 격자점, 감소 걸음은 작은 가장 가까운 격자점이고 `[minimum_for, count]`로 자른다(← →는 `value_step`의 decrease·increase로, PageUp·PageDown은 decrease_page·increase_page로 온다). 부동소수 오차는 걸음의 1/1000 안에서 같다고 본다. 메시지는 절대 값만이다. 값이 이산이라 델타(`change`)를 두지 않는다. 다음 frame 전에 같은 키가 겹쳐도 같은 목표라 한 걸음만 가는 것은 받아들인다(slider의 `change_to`와 같은 이유, [slider_element.h](../include/luil/ui/slider_element.h) 28~34행).

**그리기.** 별 `i`(0부터)의 채움 비율은 `clamp(값 - i, 0, 1)`이다. 0이면 빈 별(선), 1이면 채운 별(면), 그 사이는 별 가로의 그 비율 지점에서 clip을 둘로 나눠 왼쪽은 채움, 오른쪽은 빈 별로 그린다. 4.3이면 별 넷과 다섯째의 30%다.

| 상태 | 채움 | 빈 별의 선 |
| --- | --- | --- |
| 보통 | `accent` | `secondary_foreground` 1.5px |
| 미리 보기 | `accent_hover` | 같음 |
| 비활성 | `disabled_foreground` | `disabled_foreground` |

미리 보기는 활성이고 `change_to`가 있으며 `interaction.hovered`가 이 root의 칸이고 `interaction.pressed`가 이 root의 칸이 아닐 때 선다. 값을 그 칸의 값으로 바꿔 그린다. 칸의 값이 현재 값보다 작으면 사이의 별이 빈 별로 돌아간다. 누른 동안은 미리 보기를 끄고 값을 그린다. 끌기가 서면 controller가 hover를 갱신하지 않아(`ui_interaction.cpp` 547~570행의 끌기 분기는 `update_hover`를 부르지 않고 돌아간다) `hovered`가 누른 칸에 머무는데, 값은 절대 메시지가 이동마다 나가 포인터의 칸을 따르기 때문이다. 대가는 클릭 하나다. 시작 거리를 둔 손잡이라 누름과 뗌 사이에 메시지가 없어, 그 짧은 구간 동안 그림이 현재 값으로 돌아갔다가 뗀 뒤 새 값으로 간다. 받아들이고 실기기에서 눈으로 본다. 움직임이 없어 움직임 줄이기와 무관하다.

**입력.** 칸의 값은 칸 번호 곱하기 걸음이다.

| 입력 | 결과 |
| --- | --- |
| 칸을 눌렀다 뗌(클릭, 탭) | 칸의 값이 현재 값과 같고 `clearable`이면 `change_to(0)`, 아니면 `change_to(칸의 값)`. 초점이 root에 선다 |
| 시작 거리를 넘긴 끌기 | 넘는 순간 눌린 자리의 칸 값을, 이어서 포인터의 칸이 바뀔 때마다 그 칸의 값을 낸다. 줄 왼쪽 밖은 지울 수 있으면 0, 아니면 한 걸음(`minimum_for`와 달리 지금 값을 읽지 않는다, 결정 10), 오른쪽 밖은 `count`. 지우기 규칙은 없다 |
| ← → | 격자 한 걸음 |
| PageUp PageDown | 한 별(정수 별에서는 화살표와 같고 반 별에서는 걸음 둘이다). PageUp이 감소, PageDown이 증가다 (결정 14) |
| Home End | `minimum_for`, `count` |
| 끝에 닿은 키 | 빈 목록으로 삼킨다. 앱 단축키로 새지 않는다 |
| Ctrl·Alt를 든 키 | 앱 단축키로 흐른다([값 조절](value-step-design.md)) |

끌기는 지금 값을 읽지 않는다. 앞뒤 포인터의 칸이 같으면 메시지가 없고 다르면 뒤 칸의 값이다(slider의 [손잡이 규칙](slider-magnet-design.md)과 같다). 클릭은 그 frame의 `value`로 지우기를 판정한다. 빠른 두 번 누름은 클릭이 두 번이라 매기고 바로 지우는 데서 끝난다.

**접근성.** root는 역할 slider다(UIA Slider).

- 이름은 `name`, 비면 기본 문구다. 범위는 `{ minimum_for, count, 값, step, page }`다. `step`은 걸음이고 `page`는 한 별이다. 최소는 값을 품도록 `minimum_for`와 값 중 작은 쪽이다 (결정 15). 값 글(`access_info::value`)은 `value_name`, 비면 기본 문구다. 값 글이 비지 않아 UIA Value 패턴도 함께 선다(결정 8).
- `minimum_for`가 지우기 규칙을 범위로 말한다. 지울 수 없고 값이 있으면 최소가 한 걸음이라 UIA `SetValue(0)`은 provider가 먼저 `E_INVALIDARG`로 거절한다. 값이 0이면 최소가 0이다. 최소가 값에 따라 바뀌어도 UIA는 경계 변화를 알리지 않으므로(`uia_provider.cpp` 500행) 클라이언트가 쥔 최소가 낡을 수 있다. 거절은 호출 때 현재 범위로 하니 값은 어긋나지 않는다.
- `access_actions`의 `set_value`는 요청을 가장 가까운(같은 거리면 큰 쪽) 격자점으로 반올림하고 `[minimum_for, count]`로 자른다. 현재 값과 같으면 빈 목록, 아니면 `change_to(값)`이다. `change_to`가 없으면 nullopt라 읽기 전용이다. `get_IsReadOnly`가 현재 값으로 이 계획을 묻기 때문에 표시 전용만 읽기 전용으로 읽힌다([uia_provider.cpp](../src/win32/uia_provider.cpp) 1362행).
- 칸은 역할이 none이라 접근 tree에서 접히고, 별 위의 좌표 질의(`access_element_at`)는 root를 돌려준다.

**테마와 배율.** 색은 팔레트 역할뿐이고 글꼴 크기 설정은 보지 않는다. 별은 글이 아니다. 큰 글꼴 앱은 `control_size`를 올린다. 고대비에서는 채운 별이 면이고 빈 별이 선이라 색을 접어도 모양이 남는다. 미리 보기와 확정은 색만 다르다. 미리 보기는 포인터가 그 칸 위에 있는 동안만 서므로 받아들인다.

## 결정

**1. 미리 보기와 hit 구조.** 질문: 별 위에 포인터를 올릴 때 채워질 모양을 어떻게 그리는가.

- 길 a: 걸음마다 자식 칸을 두고 root가 별을 모두 그린다. 되는 것: `hovered`가 칸 하나를 가리켜 미리 보기가 정확하다(반 별도). 칸이 이어져 별 사이를 지나도 깜빡이지 않는다. 끌기와 클릭이 칸 단위다. 칸은 접근 tree에서 접힌다. 대가: 칸 수(5 또는 10)만큼 element가 서고 라이브러리 kind 둘이 는다. 칸마다 Tab 자리 끄기, 초점 돌리기, 역할 none 설정이 든다.
- 길 b: element 하나에 미리 보기 없음(slider 꼴). 되는 것: kind 하나에 설정이 가장 적다. 대가: hover에 반응이 없어 눌러 보기 전에는 결과를 모른다. 마우스에서 별점답지 않다.
- 길 c: controller가 hover 자리를 모든 요소에 발행한다. 되는 것: 단일 element로 미리 보기. 대가: 포인터가 움직일 때마다 발행본이 바뀌어 모든 창이 다시 그려진다(877행이 피한 일). `ui_element.h`와 controller를 고쳐 줄 3의 파일과 겹친다.
- 권장: a. 대가: 위의 element 수와 kind 둘. 승인: 길 a (2026-11-02).

**2. 정수 별과 반 별.** 질문: 입력의 걸음을 어디까지 두는가.

- 길 a: 정수만. 되는 것: 칸 수가 별 수와 같고 규칙이 가장 짧다. 대가: 3.5점 입력이 없다. 표시는 소수 부분 채움으로 된다.
- 길 b: `precision`으로 정수 또는 반 별(기본 정수). 되는 것: 걸음이 곧 칸이라 구조가 그대로이고 미리 보기도 반 별로 정확하다. 대가: 칸 폭이 별의 절반이라 터치에서 `control_size`를 올려야 하고, 칸이 두 배라 test가 는다.
- 길 c: 임의 걸음(`float step`). 되는 것: 0.25도 된다. 대가: 칸이 별 폭을 가르는 걸음은 손으로 못 맞춘다. 공개 형식을 지금 넓게 박는다.
- 권장: b. 대가: 위의 칸 폭. 권장대로 진행.

**3. 지우기와 값 0.** 질문: 값 0과 지우기를 어떻게 다루는가.

- 길 a: 0은 아직 매기지 않음이고 값이다. `clearable`(기본 참)이면 같은 별을 다시 눌러 0으로 지우고, ← 끝과 Home도 0까지 간다. 거짓이면 한 번 매긴 뒤 최소가 한 걸음이다. 되는 것: 앱이 칸 하나로 둘을 다 얻고 UIA 범위가 거절 규칙을 대신한다. 대가: 빠른 두 번 누름은 매기고 지우는 데서 끝난다. 끌기는 지우지 않아 클릭과 끌기의 규칙이 갈린다.
- 길 b: 최소 1점 고정만. 되는 것: 규칙이 가장 짧다. 대가: 점수를 뺄 길이 없는 앱은 단추를 따로 짓는다.
- 길 c: 지우기를 앱의 단추로 둔다. 되는 것: 오조작이 없다. 대가: 모든 앱이 같은 단추를 만든다.
- 권장: a. 대가: 위의 두 번 누름. 권장대로 진행.

**4. 표시 전용과 소수 값.** 질문: 평균 같은 소수 값과 표시 전용을 어떻게 다루는가.

- 길 a: `change_to`가 없으면 표시 전용이고, 모든 경우에 소수 값을 별의 일부 채움으로 그린다. 되는 것: slider와 같은 "조작 메시지가 없으면 읽기 전용" 규칙이고 모드 칸이 없다. 접근 값도 소수 그대로다. 대가: 한 컨트롤에 평균과 내 점수를 함께 보이는 용도는 못 한다. 요소 둘을 쓴다.
- 길 b: 값을 걸음 격자로 반올림해 그린다. 되는 것: 그림이 입력 값과 같다. 대가: 평균 4.3이 4.5로 그려져 정보를 잃는다. 반올림이 필요한 앱은 스스로 하면 된다.
- 길 c: `rating_mode`(input, display)를 따로 둔다. 되는 것: 의도가 드러난다. 대가: `change_to`가 있는 display, 없는 input 같은 불가능한 조합이 생긴다([progress_element.h](../include/luil/ui/progress_element.h) 19~21행이 막은 "누를 수 있는 진행률"과 같은 종류).
- 권장: a. 대가: 위의 한 컨트롤 두 값. 권장대로 진행.

**5. 별 모양과 그리는 방법.** 질문: 별을 어떻게 그리는가.

- 길 a: `SkPathBuilder` 경로. 채움은 면, 빈 별은 선, 부분 채움은 clip 둘. 되는 것: raster test가 픽셀로 잠근다(harness는 typeface를 싣지 않는다, [raster-test-design.md](raster-test-design.md)). 글꼴 크기와 hinting에 달리지 않고 반쪽이 정확하며 두 플랫폼이 같다. 대가: 별 기하(안쪽 반지름 비율, 세로 가운데)를 직접 정하고, codicon 아이콘과 선 굵기가 다를 수 있다.
- 길 b: codicon 글리프. `icon_star`(0xEA6A, 외곽), `icon_star_full`(0xEB59), `icon_star_half`(0xEB5A)이고 번호는 [mapping.json](../assets/codicons/mapping.json)의 60010·60249·60250이다. 되는 것: 다른 아이콘과 모양이 같고 코드가 짧다. 대가: raster test가 글리프를 그리지 못해 사람이 본다. 반 별을 `icon_star_half`로 하면 글꼴에 달리고, clip으로 하면 외곽과 채움 글리프의 경계 상자가 같아야 한다(`draw_centered_glyph`는 글리프마다 가운데를 맞춘다). 이 단계에서는 재지 못했다.
- 길 c: a가 기본이고 앱이 글리프 쌍을 줄 수 있다. 되는 것: 하트 같은 모양. 대가: 그리기 길이 둘이고 글리프 쪽은 raster로 못 잠근다.
- 권장: a. 대가: 위의 기하. 안쪽 반지름 비율은 구현 때 raster와 눈으로 정한다. 권장대로 진행.

**6. 터치의 시작 거리와 스크롤.** 질문: 별 위에서 시작한 끌기와 감싼 창의 스크롤을 어떻게 가르는가.

- 길 a: 칸의 끌기 손잡이에 시작 거리(`rating_drag_start_distance`)를 둔다. 터치는 탭이 클릭(칸의 값)이고 축에 맞는 빠른 쓸기는 감싼 창의 스크롤이며 스크롤이 될 수 없게 된 뒤 허용치를 넘기면 끌기다([touch-pen-input-design.md](touch-pen-input-design.md) 151행). 마우스·펜은 누른 채 6 물리 픽셀을 넘기면 끌기다. 되는 것: 세로·가로 스크롤 안에서도 별 줄 위의 쓸기가 창을 흘린다. 클릭이 살아 있어 지우기(결정 3)가 클릭이다. 목록 행을 본뜬 test 모형([pointer_drag_start_tests.cpp](../tests/pointer_drag_start_tests.cpp) 100~130행)과 같은 조합이다. 대가: 누르는 순간에는 값이 바뀌지 않고 뗄 때 또는 거리를 넘길 때 바뀐다. 가로로 흐르는 창 안에서 별을 훑으려면 잠시 눌러 스크롤 후보가 닫힌 뒤 옮겨야 한다. `src/ui`의 element 중 시작 거리를 쓰는 것이 아직 없어(예제 [files_page.cpp](../examples/demo/files_page.cpp) 506행과 test뿐) 이 노드가 첫 라이브러리 소비자다.
- 길 b: slider처럼 시작 거리 0. 되는 것: 누르는 순간 값이 바뀌고 가장 즉각이다. 대가: 별 줄 위의 쓸기는 스크롤하지 못한다. 클릭이 없어 지우기는 `on_press`가 현재 값을 읽어야 해 slider가 지킨 "끄는 중에는 지금 값을 읽지 않는다"를 깬다.
- 길 c: 끌기 없이 클릭과 탭만. 되는 것: 충돌이 없다. 대가: 마우스와 터치 모두 훑어 정할 수 없다.
- 권장: a. 대가: 위의 지연과 첫 소비자. 실기기에서 확인한다. 권장대로 진행.

**7. 접근성 단계.** 질문: UIA의 SmallChange·LargeChange로 별 걸음을 알리는가.

- 길 a: 알리지 않는다. 지금처럼 0이다. 되는 것: 공유 파일을 건드리지 않는다. 절대 값 요청은 element가 격자로 반올림해 받아 값은 어긋나지 않는다. 대가: 값을 단계만큼 올리는 클라이언트는 별 단위를 모른다.
- 길 b: `access_range` 끝에 `step`·`page`(기본 0이며 0이면 알리지 않는다)를 더하고 `get_SmallChange`·`get_LargeChange`가 읽는다. 별점은 걸음과 한 별을 채운다. 되는 것: 걸음이 이 컨트롤의 본질이고 [uia_provider.cpp](../src/win32/uia_provider.cpp)의 `get_LargeChange`가 "아직 어휘에 없다"고 남겼던 자리를 연다. 기본 0이라 slider·scrollbar·진행률의 동작은 바뀌지 않는다. 대가: 공유 파일 둘, `accessibility.h` 79행의 구조체와 `uia_provider.cpp`의 `get_LargeChange`·`get_SmallChange`, test 둘(`uia_provider_tests.cpp`, `accessibility_tests.cpp`)이다. 이번 라운드에 두 파일을 고치는 다른 줄 문서는 없다(줄 2·3·4 문서를 열어 확인했다. 줄 3의 ink는 새 role을 쓰는 길을 고르지 않았고 inspector는 test만 더한다). 병합 충돌이 아니라 공개 구조체의 필드 둘이 대가다. 어떤 클라이언트가 이 값을 읽는지는 이 단계에서 재지 못했다.
- 권장: b. 대가: 위의 필드 둘과 함수 둘. 승인: 길 b (2026-11-02).

**8. 접근 이름과 값 글.** 질문: 보조 기술에 값을 숫자로만 알리는가, 글로도 알리는가.

- 길 a: 이름만(`name`)이고 값은 RangeValue 숫자다. 되는 것: Value 패턴이 서지 않는다([uia_provider.cpp](../src/win32/uia_provider.cpp) 987행은 값 글이 비지 않으면 세운다). 대가: "3"만 읽혀 5점 만점인 줄 모를 수 있다. 최댓값은 RangeValue Maximum으로 읽힌다.
- 길 b: 이름과 값 글(`access_info::value`, "3 / 5")을 따로 둔다. 되는 것: default-text 165행의 약속(값과 최댓값을 받는 함수와 앱이 덮는 `std::function`)을 지킨다. 대가: 슬라이더에 Value 패턴이 RangeValue와 함께 선다. Value 쪽 `IsReadOnly`는 같은 함수가 답하고, `SetValue(글)`은 텍스트 칸이 아니므로 `E_ACCESSDENIED`다(1772행). 값이 바뀌면 Value와 RangeValue 알림이 둘 다 난다. 화면 읽기가 둘을 겹쳐 읽는지는 재지 못했다.
- 길 c: 이름에 값을 넣는다("별점 3 / 5"). 되는 것: 속성 하나로 끝난다. 대가: 값이 바뀔 때마다 Name 속성이 바뀌고 RangeValue와 겹쳐 읽힌다.
- 권장: b. 대가: 위의 두 패턴. Narrator가 어색하게 읽으면 기본 값 글 함수를 빈 글로 바꿔 a로 돌린다. 호출부는 그대로다. 권장대로 진행.

## 구현에서 정한 결정

구현하며 문서가 정하지 않았거나 모호했던 것이다. 모두 권장대로 진행한 것이 아니라 구현 중에 고른 것이라 따로 적는다(2026-11-03).

**9. 공개 helper 둘.** `count_of`(자른 별 수)와 `step_of`(한 걸음의 값)를 공개 형식에 더했다. `height_for`·`width_for`가 쓰는 같은 식을 test와 앱이 다시 짓지 않게 한다. 둘 다 헤더 안의 `constexpr`이라 다른 번역 단위에서 링크가 깨지지 않는다.

**10. 끌기의 왼쪽 밖.** `value_at`의 왼쪽 밖은 지울 수 있으면 0, 아니면 한 걸음이다. `minimum_for`는 지금 값을 읽어 값이 0이면 0을 주는데, 끌기가 그것을 쓰면 지울 수 없는 별점이 아직 매기지 않은 frame에서 끌 때 왼쪽 밖이 0이 되어 같은 끌기가 frame에 따라 다른 값을 낸다. 키와 보조 기술은 지금 값을 읽는 `minimum_for`를 그대로 쓴다.

**11. root의 bounds.** 슬롯 전체가 아니라 별 줄이 차지한 폭이다. 초점 테와 보조 기술의 사각형이 별에 붙고 빈 자리는 누르는 자리가 아니다. 높이는 슬롯 그대로이고 별은 그 안에서 세로 가운데에 선다.

**12. 비활성.** `rating_config`에 `enabled` 칸을 더하지 않는다. 다른 요소처럼 앱이 root에 `set_enabled(false)`를 걸고, 칸은 arrange에서 root의 상태를 따라가 hit와 초점을 받지 않는다. 메시지를 내는 길(`emit`)도 한 번 더 막아 칸을 거치지 않는 호출도 비활성에서 말이 없다. 그림은 채움과 선이 모두 `disabled_foreground`다.

**13. 별 기하.** 안쪽 반지름은 바깥 반지름의 0.45배(정오각 별은 0.382), 빈 별의 선은 1.5 논리 픽셀에 둥근 이음이다. 칸이 16보다 좁으면 여백이 칸의 1/4로 줄어 별이 칸의 절반 아래로 작아지지 않는다. 비율은 raster test가 채움·선·부분 채움의 위치를 잠그지만 모양 자체는 사람이 눈으로 정한다(결정 5의 대가에 적은 대로다).

**14. PageUp과 PageDown의 방향.** controller가 PageUp을 `decrease_page`, PageDown을 `increase_page`로 보낸다(슬라이더와 같고 세로 스크롤 막대의 관례다). 가로 별점에서는 PageUp이 줄이고 PageDown이 늘린다. 한 별은 정수 별에서 걸음 하나, 반 별에서 걸음 둘이다.

**15. 알리는 범위의 최소.** `minimum_for`와 값 중 작은 쪽이다. 표시 전용 평균이 한 걸음보다 작은 소수 값(0.4)을 가지면서 지울 수 없는 설정이면 `minimum_for`(한 걸음)가 값보다 커 UIA가 범위 밖 값을 읽는다. 값 정하기(`set_value`)는 `minimum_for`만 쓴다.

**16. 범위가 없는 요소의 변화량.** `get_SmallChange`·`get_LargeChange`는 `access_range`가 없는 요소에서 `get_Minimum`처럼 `UIA_E_INVALIDOPERATION`을 내지 않고 지금처럼 `S_OK`와 0을 돌려준다. 걸음을 알리지 않는 요소의 동작을 바꾸지 않으려는 것이다. 요소가 tree에서 사라졌을 때만 `UIA_E_ELEMENTNOTAVAILABLE`을 낸다(전에는 늘 `S_OK`였다). `get_Minimum`과 같은 COM 경계 가드를 거친다.

## 플랫폼

**Win32.** `win32_window.cpp`와 `window_surface.cpp`를 고치지 않는다. UIA는 Slider에 RangeValue(Value·Minimum·Maximum·IsReadOnly)와 값 글의 Value 패턴을 낸다. SmallChange·LargeChange는 별점이 걸음과 한 별을 알린다(결정 7 b). 다른 요소는 지금처럼 0이다. 마우스·펜의 hover 미리 보기와 손 모양 커서는 지금의 입력 경로 그대로고, Windows 터치도 같은 controller라 결정 6의 규칙을 따른다.

**Android.** Java 소스는 0줄이다. 코어 element라 JNI도 unsupported도 없다.

- 별은 Skia 경로라 글꼴 없이 CPU와 Vulkan이 같다. 터치에는 hover가 없어 미리 보기는 마우스·펜이 연결된 때뿐이다. 터치는 탭이 칸의 값이고 끌기는 결정 6의 규칙이다. 스크롤과 가르는 허용치는 Android 설정의 `touch_slop`이 정한다([android_settings.cpp](../src/android/android_settings.cpp) 148행).
- 터치 앱은 `control_size`에 44를 준다. 별 다섯이 220이라 폭 360의 화면에서 양쪽 여백 16을 두고도 들어간다. 별이 10개인 앱은 폭에 맞춰 칸이 줄므로 목표가 작아진다.
- 접근성은 Android에서 소비되지 않는다([number-input-design.md](number-input-design.md)와 같은 까닭). 이름과 값 글은 Windows UIA만 읽는다.

## 다른 노드와의 약속

- **default_text**(같은 줄 앞 문서). `default_text.h`에 `rating_name()`(별점)과 `rating_value_name(value, count)`("3 / 5")를 더한다. 이름은 `<요소>_<부품>` 규칙이다. 165행의 "별점 3 / 5" 꼴은 이름과 값 글 둘로 나눠 닫는다. 값 글은 값을 10배 반올림한 정수로 정수부와 소수 첫째 자리를 지어 로케일과 부동소수 서식 함수에 달리지 않게 한다. 숫자 서식은 R5 몫이다. 구현은 default_text 뒤에 쌓는다. `verify_default_text`가 서면 `rating_element.cpp`에 한글 리터럴을 두지 않는다. R5는 두 함수의 몸통만 표 조회로 바꾼다.
- **msg_box**(같은 줄). 같은 자리를 고친다. `ui_element_id.h`(`message_box`·`message_box_button`과 이 노드의 `rating`·`rating_cell`, 줄 3 ink의 `ink_canvas`가 `end_of_library_kinds` 위에 선다), `tests/ui_element_tests.cpp` 84행 `last[]`, `src/CMakeLists.txt`(이 노드는 `ui/controls`의 slider 곁), `tests/CMakeLists.txt`, `luil.h`, [mobile basics_page.cpp](../examples/mobile_demo/basics_page.cpp)다. kind 병합은 기계적이고 AutomationId가 kind 수라 새 kind의 번호는 병합 순서가 정한다. `last[]`와 모바일 `basics_page.cpp`는 직렬로 쌓는다. inspector 문서는 kind를 더하지 않는다고 닫았지만 kind 이름표(`src/ui/inspect_names.cpp`의 `library_kind_name`, `default` 없는 `switch`)를 세운다. 이 브랜치에는 그 파일이 없어 건드리지 않는다. inspector가 먼저 main에 있으면 이 노드를 병합하는 통합 커밋이 `rating`·`rating_cell` case(열거자 이름 그대로)를 더한다. 빠지면 Android clang의 `-Wswitch`(`-Wall -Werror`)가 빌드를 막고 Windows에서는 inspector의 전수 test가 실패한다. 늦게 들어오면 inspector의 병합 커밋이 채운다.
- **slider·progress**. 고치지 않는다. [value-step-design.md](value-step-design.md)의 변화량 계약은 델타라고 적는다. 별점은 절대 값만 나르므로 구현 커밋이 그 문서에 예외를 더했다.
- **공유 파일 `accessibility.h`·`uia_provider.cpp`**. 결정 7 b(승인)로 고쳤다. 위에서 확인한 대로 이번 라운드의 다른 줄은 두 파일을 고치지 않는다. R3의 색 고르기가 색상·알파 막대에서 같은 칸(`step`·`page`)을 채운다([색 고르기](color-picker-design.md)). 공유 파일은 고치지 않았다.
- **ink**(줄 3). `ui_events.h`의 `pointer_*_event` 끝에 필드를 더한다. 이 노드는 그 구조체를 읽지 않고 `ui_action_context`만 쓰므로 겹치지 않는다.
- **R1에서 넘어온 controller 한계**. 닿지 않는다. 칸은 묶음 구성원이 아니고 Tab 자리는 root 하나이며 열림 상태가 없다. 메뉴 폭이 그리는 배율로 재지 않아 배율 1.1부터 항목이 말줄임되던 일은 이 노드 밖이었고, 메뉴 폭 노드가 `menu_element::width_for`의 `scale` 칸으로 고쳤다([popup-overlay-design.md](popup-overlay-design.md)).
- **데모**. 갤러리(`examples/demo/gallery_page.*`)는 이번 라운드에 이 노드만 고친다(color_picker는 R3). 모바일 `basics_page.cpp`는 msg_box와 같은 파일이다. 라이브러리 커밋과 데모 연결 커밋을 나누고 연결은 통합 때 직렬로 쌓는다. msg_box 연결이 먼저고 이 노드의 행은 카운터 행 뒤(249행 근처)에 둬 대화 쪽 수정(369행, 460행 이후)과 줄이 떨어진다. `docs/components.md` 행은 라이브러리 커밋이 붙였고 `docs/README.md` 색인은 통합 때 붙인다.

## 확인

자동 test는 UIA 하나만 빼고 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다.

- `tests/rating_element_tests.cpp`(새 파일). 치수: 칸이 이어져 빈틈이 없고, 정수 별 `count`개와 반 별 `2 * count`개, `count` 0은 1과 99는 20, 자리가 좁으면 칸이 줄고, 배율 2의 물리 좌표, `value_at`의 왼쪽·오른쪽 밖. 값: `clamp_value`(NaN, 음수, 초과), `minimum_for`, 격자 이웃(4.3의 ↑ 5와 ↓ 4, 반 별). 클릭: 칸 3이 `change_to(3)`을 한 번 내고, 같은 값이면 `change_to(0)`, `clearable` 거짓이면 같은 값을 다시 낸다. `change_to`가 없으면 칸·커서·Tab 자리가 없다. 비활성이면 메시지가 없다. 끌기: 시작 거리 안에서 뗌은 클릭이고, 넘으면 눌린 자리 값과 이동한 칸 값을 내며 같은 칸 안 이동은 메시지가 없다. 터치: 탭은 클릭이고, 가로로 흐르는 창 안의 빠른 쓸기는 창을 흘리고 값 메시지가 없으며, 흘릴 창이 없으면 허용치를 넘긴 이동이 끌기다. 키: 칸을 누른 뒤 초점이 root에 서고 ← → PageUp Home End가 절대 값을 내며, 끝에서는 빈 목록이라 단축키로 새지 않는다.
- 같은 파일의 접근성: 역할 slider, 이름 기본과 덮기, 범위의 `step`·`page`, 값 글 기본("3 / 5", "3.5 / 5")과 `value_name` 덮기, 칸이 접근 tree에 없고 `access_element_at`이 root를 돌려주는지, `set_value`의 반올림과 같은 값의 빈 목록, `clearable` 거짓일 때 최소가 한 걸음, 표시 전용이 `plan_access_request`에서 nullopt인지.
- `tests/raster_draw_tests.cpp` 덧붙임. 배율 2에서 채운 별 중심이 `accent`이고 빈 별 중심이 바탕이며 빈 별의 선이 `secondary_foreground`인지. 4.3에서 다섯째 별의 왼쪽 30%만 채워졌는지. `hovered`가 칸일 때 그 칸까지 `accent_hover`이고 `pressed`가 칸이면 미리 보기가 없는지. 비활성 색.
- `tests/accessibility_tests.cpp`에 접근 tree 한 노드와 `step`·`page`(결정 7 b). `tests/default_text_tests.cpp`(앞 노드의 파일)에 두 함수. `tests/ui_element_tests.cpp`의 `last[]`에 `rating_cell`. `tests/demo_gallery_tests.cpp`가 갤러리 별점 줄의 조립과 intent를 잡는다.
- `tests/uia_provider_tests.cpp`(`luil_tests`, Windows). RangeValue의 Value·Minimum·Maximum·IsReadOnly, 범위 밖 `SetValue`의 `E_INVALIDARG`, 표시 전용의 `E_ACCESSDENIED`, 값 글의 Value 패턴, 칸이 자식 열거에 없는지. 결정 7 b라 SmallChange·LargeChange가 걸음과 한 별이고 slider는 여전히 0인지.

데모 칸(`examples/demo` 갤러리의 컨트롤 페이지, `examples/mobile_demo` 기본 페이지). 컨트롤 페이지의 "별점" 카드가 정수 별(값 3), 반 별(값 3.5), 표시 전용 평균(4.3) 한 줄씩과 "별점 초기화" 단추를 보인다. 별점 요소의 owner는 `gallery/rating-whole`·`gallery/rating-half`·`gallery/rating-average`이고 접근 이름은 "별점"·"반 별 점수"·"평균 점수"다. 줄마다 kind `text`인 label이 "별점: 3 / 5"·"반 별: 3.5 / 5"·"평균: 4.3 / 5" 꼴의 글을 그려 UIA가 되읽게 한다(owner는 `gallery/rating-whole-value`·`gallery/rating-half-value`·`gallery/rating-average-value`). 단추의 owner는 `gallery/rating-reset`이고 두 조작 줄을 0으로 비운다. 모바일 기본 페이지는 카운터 행 뒤에 `control_size` 44의 별 다섯(owner `rating`, label `rating-value` "별점: 0 / 5")과, 가로로 흐르는 창(`strip_element`, owner `rating-strip-window`) 안의 별 열 개(owner `rating-strip`, label `rating-strip-value` "가로 창 별점: 0 / 10")를 더한다. 격자 안에서 luil-uia로 RangeValue `SetValue`(범위 안, 범위 밖, 표시 전용)와 Value 글을 읽고, 초기화 단추를 Invoke해 되읽는다. 마우스 hover는 UIA로 못 만드니 raster test가 잠근다. 두 데모 칸의 조립과 intent는 `tests/demo_gallery_tests.cpp`와 `tests/mobile_demo_page_tests.cpp`가 잡는다. 모바일 기본 페이지는 msg_box 연결이 이미 core test 목록에 올려 컴파일 검증이 붙는다.

사람 확인: 마우스 hover 미리 보기가 별 사이를 지날 때 깜빡이지 않는지와 클릭하는 순간 그림이 현재 값으로 돌아갔다 새 값으로 가는 것이 거슬리지 않는지, 밝은·어두운·고대비에서 채운 별과 빈 별과 미리 보기가 구분되는지와 별 모양, Narrator가 이름과 값을 겹치지 않고 읽는지, 실기기에서 44 칸의 탭과 끌기, 가로로 흐르는 창 안에서 쓸기가 창을 흘리고 잠깐 눌러야 별이 끌리는 느낌이 받아들일 만한지.

## 범위 밖

- 별 모양을 앱이 바꾸기(하트, 이미지)와 색 지정. 요소가 앱에서 색을 받는 자리는 없다([theming.md](concepts/theming.md)).
- 평균과 내 점수를 한 컨트롤에 함께 보이기(결정 4의 a)와 별 옆 설명 글.
- 수 키(1~5)로 정하기, 세로 배치, 오른쪽에서 왼쪽 글 방향.
- 값 글의 언어별 문장은 R5 표, 숫자 서식은 범위 C다.
- slider·scrollbar가 `step`·`page`를 채우는 일(결정 7 b에서도 이 노드는 별점만 채운다).
- Android 접근성 계층.

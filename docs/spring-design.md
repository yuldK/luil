# 스프링

값 하나가 목표를 향해 스프링처럼 움직이게 한다. 손을 뗀 속도에서 이어 출발하고, 도는 중에 목표가 바뀌어도 값과 속도가 끊기지 않는다. 공개 형식은 새 헤더 [spring.h](../include/luil/ui/spring.h)의 `spring`·`spring_config`와 짝 함수(`spring_value`·`spring_to`·`spring_release`)다. [transition.h](../include/luil/ui/transition.h)의 `transition`과 같은 규칙으로, 앱이 값을 들고 시각만의 함수로 지금 값을 묻는다.

## 문제

- `transition_to`는 도는 중에 값은 잇지만 속도는 잇지 못한다. 새 전환은 `ease_in_out`으로 출발하고 그 곡선의 시작 기울기는 0이라(`2t²`), 움직이던 값이 멈칫했다가 다시 출발한다. 데모의 사이드바 접힘(`examples/demo_main.cpp`의 `sidebar_motion_`)을 연타하면 그렇게 된다.
- 손을 뗀 속도는 멈출 자리를 고르는 데만 쓰인다. `settle_sheet(config, height, velocity)`가 속도로 멈춤점을 고르고, 앱은 `settle` 메시지를 받으면 `drag_offset_`을 0으로 바로 붙인다(`examples/mobile_demo/sheets_page.cpp`의 `handle`). 시트가 놓은 자리에서 멈춤점으로 순간이동한다.
- 그 속도는 앱에 닿지도 않는다. `sheet_host_config::settle`은 멈춤점만, `dismiss`는 계기만 싣는다(`src/ui/sheet_host_element.cpp`의 `release_action`).
- 속도의 단위가 곳마다 다르다. `drag_velocity`는 초당 물리 픽셀이고, `pan_handoff_target::release`는 초당 논리 픽셀이다. 시트 손잡이의 `on_release`는 물리 값을 받아 `/ scale`로 바꿔 쓴다.
- 가까운 것으로 스크롤 관성(`interaction_controller::advance_inertia`)이 있다. 지수 감쇠이고 경계에서 멈추는 스크롤 전용이라 목표를 향하는 스프링과 다르다. 이 노드는 건드리지 않는다.

## 공개 형식

헤더 하나(header-only)이고 함수는 그 안의 `inline`이다. `transition.h`는 건드리지 않는다. 계산은 `exp`·`cos`·`sin`을 쓰므로 `constexpr`이 아니다. 값은 `float`이고 안쪽 계산은 `double`이다. 안쪽 구현은 `luil::spring_detail`에 있고 약속이 아니다.

```cpp
// include/luil/ui/spring.h
namespace luil {
    // 스프링의 성질이다. 값의 단위와 무관하다 (질량은 1로 둔다).
    struct spring_config
    {
        // 감쇠가 없을 때 한 번 진동하는 시간이다. 짧을수록 빠르다. 0 이하면 전환이 없다.
        std::chrono::milliseconds response { 350 };
        // 감쇠 비율이다. 1이면 넘치지 않고 가장 빨리 선다. 작을수록 목표를 넘었다 돌아온다. 0 이하면 전환이 없다.
        float damping_fraction { 1.0f };
        // 이만큼 가까워지면 끝이다 (값의 단위, 규칙 절). 0 이하면 기본값으로 본다.
        float rest_threshold { 0.01f };
    };

    inline constexpr spring_config spring_smooth { std::chrono::milliseconds { 350 }, 1.0f };
    inline constexpr spring_config spring_snappy { std::chrono::milliseconds { 300 }, 0.75f };
    inline constexpr spring_config spring_bouncy { std::chrono::milliseconds { 500 }, 0.5f };

    // (질량, 강성, 감쇠)와 같은 운동의 config다. 하나라도 0 이하이거나 유한하지 않으면 전환 없는 config다.
    [[nodiscard]] inline spring_config spring_from_physics(float mass, float stiffness, float damping, float rest_threshold = 0.01f) noexcept;

    // 값 하나가 목표로 가는 중이다. 앱이 들고, 지금 값을 물어 element에 넘긴다 (`transition`과 같다).
    struct spring
    {
        float from { 0.0f };
        float to { 0.0f };
        std::chrono::steady_clock::time_point started {};
        spring_config config {};
        // 출발할 때의 속도다 (값의 단위 / 초).
        float velocity { 0.0f };
    };

    // 정지한 스프링이다. 언제 물어도 `value`이고 끝나 있다.
    [[nodiscard]] constexpr spring spring_at_rest(const float value) noexcept
    {
        return spring { value, value, {}, {}, 0.0f };
    }

    [[nodiscard]] inline float spring_value(const spring& value, std::chrono::steady_clock::time_point now) noexcept;
    // 값의 시간 미분이다 (값의 단위 / 초). 끝났으면 0이다.
    [[nodiscard]] inline float spring_velocity(const spring& value, std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] inline bool spring_finished(const spring& value, std::chrono::steady_clock::time_point now) noexcept;
    // `transition_next_update`와 같은 계약이다. 시작 전이면 시작 시각, 도는 중이면 `transition_frame` 뒤, 끝났으면 nullopt.
    [[nodiscard]] inline std::optional<std::chrono::steady_clock::time_point> spring_next_update(const spring& value, std::chrono::steady_clock::time_point now) noexcept;

    // 도는 중이면 지금 값과 속도에서 `target`으로 이어 출발한다. `reduce_motion`이 참이면 목표에 정지한 스프링이다.
    [[nodiscard]] inline spring spring_to(const spring& value, float target, std::chrono::steady_clock::time_point now, const spring_config& config, bool reduce_motion) noexcept;
    // 손을 뗀 값과 속도에서 출발한다 (`velocity`는 값의 단위 / 초). `reduce_motion`이 참이면 속도도 쓰지 않는다.
    [[nodiscard]] inline spring spring_release(float value, float velocity, float target, std::chrono::steady_clock::time_point now, const spring_config& config, bool reduce_motion) noexcept;
}
```

앱은 지금처럼 값을 들고 config에 먹인다. 사이드바 접힘은 `transition`을 `spring`으로 바꾼다.

```cpp
luil::spring sidebar_motion_ { luil::spring_at_rest(sidebar_default_width) };
sidebar_motion_ = luil::spring_to(sidebar_motion_, target_sidebar_width(), now, luil::spring_smooth, metrics_.reduce_motion);
sidebar_motion_ = luil::spring_at_rest(sidebar_width_);                       // 손잡이로 끄는 동안
config.width = luil::spring_value(sidebar_motion_, now);
```

시트는 결정 7이 b이므로 놓은 속도를 받는 칸을 하나 더한다. `sheet_settle`은 `sheet_release`가 멤버로 들기 때문에 정의를 `sheet_host_config` 앞으로 옮겼다(모양은 그대로다).

```cpp
// include/luil/ui/sheet_host_element.h
struct sheet_release
{
    sheet_settle settle {};        // `settle_sheet`가 정한 결과 (닫는 쪽이면 `settle.dismiss`)
    float visible_height { 0.0f }; // 놓은 순간 보이던 높이 (논리 픽셀, 마지막 이동까지 더한 값)
    float target_height { 0.0f };  // 멈출 자리의 높이 (논리 픽셀, 닫으면 0)
    float velocity { 0.0f };       // 놓은 순간 높이가 변하는 속도 (초당 논리 픽셀, 높아지는 쪽이 양수). 취소·입력 유실이면 0
};
// sheet_host_config: 있으면 끌기를 놓은 결과가 `settle`·`dismiss(drag)` 대신 이것으로 나간다.
std::function<input_action(const sheet_release&)> release {};
```

## 규칙

1. **앱이 든다.** 라이브러리는 `spring`을 들지 않는다. element는 스프링을 모르고 앱이 준 값(`sidebar_config::width`, `sheet_host_config::drag_offset`)을 그린다.
2. **닫힌 해다.** 목표와의 차를 `y`, `ω = 2π / response`, `ζ = damping_fraction`, `y0 = from - to`, `v0 = velocity`라 하면 `y'' + 2ζω·y' + ω²·y = 0`의 해다. 단계 적분이 아니라서 시각만의 함수 계약이 선다.

   | ζ | y(t) |
   | --- | --- |
   | 1 미만 | `e^(-ζωt)·(y0·cos ω_d·t + (v0 + ζω·y0) / ω_d · sin ω_d·t)`, `ω_d = ω√(1-ζ²)` |
   | 1 | `e^(-ωt)·(y0 + (v0 + ω·y0)·t)` |
   | 1 초과 | `c1·e^(r1·t) + c2·e^(r2·t)`, `r = -ω(ζ ∓ √(ζ²-1))`, `c2 = (v0 - r1·y0) / (r2 - r1)`, `c1 = y0 - c2` |

   - `|ζ - 1| < 1e-6`이면 임계해를 쓴다. 두 쪽 식이 나눗셈에서 불안정해지는 띠이고, 띠 안의 오차는 띠의 폭과 같은 비율(1e-6)이다.
   - 느린 뿌리 `r1`은 `-ω / (ζ + √(ζ²-1))`로 구한다. `ζ - √(ζ²-1)`을 그대로 빼면 큰 ζ에서 두 수가 같아져 0으로 뭉개진다.
   - 시간은 `started`부터 초 단위 `double`이다. 밀리초로 자르지 않는다. `now`가 `started`보다 이르면 `from`과 `velocity`다.
   - 속도는 같은 해의 미분이라 값과 어긋나지 않는다.
3. **끝 판정.** `s = √(y² + (y' / ω)²)`가 `rest_threshold` 이하면 끝이다. `E = ½(y'² + ω²·y²)`는 `dE/dt = -2ζω·y'² ≤ 0`이라 `s`는 줄기만 한다. 한 번 끝난 스프링은 다시 움직이지 않으므로 `spring_next_update`가 잠든 뒤 되살아날 일이 없다. 끝나면 값은 정확히 `to`이고 속도는 0이다(`transition`과 같다).
   - 값과 속도에 허용치를 따로 두면 상자 모서리에서 끝났다가 다시 나갈 수 있다. 속도의 허용치는 `ω·rest_threshold`로 따라온다.
   - 기본 0.01은 논리 픽셀 값에 맞췄다. 불투명도나 배율처럼 0~1 쪽 값은 앱이 0.001쯤으로 낮춘다. 안 낮추면 마지막 1%가 한 번에 붙는다.
   - 허용치가 작을수록 꼬리가 길다. 이동 100, 허용치 0.01에서 끝나기까지 `spring_smooth`는 약 0.67초, `spring_snappy`는 약 0.61초, `spring_bouncy`는 약 1.5초다. 허용치 0.1이면 각각 약 0.53초, 0.42초, 1.1초다. 그동안 앱은 `transition_frame`(16ms)마다 tree를 다시 짓는다. 사이드바 선례와 비용은 같고 길이만 길다.
4. **넘침.** ζ가 1보다 작으면 목표를 넘었다 돌아온다. `spring_smooth`는 넘지 않는다. 폭처럼 배치에 닿는 값은 ζ가 1 이상이어야 이웃이 따라 흔들리지 않는다. 값을 범위로 자르는 쪽은 넘침이 잘려 보인다(시트 높이는 `sheet_visible_height`가 0과 전체 높이 사이로 자른다).
5. **속도의 단위는 값의 단위 / 초이고 값이 커지는 쪽이 양수다.** 값이 논리 픽셀이면 논리 픽셀 / 초다. 라이브러리가 주는 속도는 다음처럼 바꾼다.

   | 출처 | 단위 | 논리 픽셀 값에 넣을 때 |
   | --- | --- | --- |
   | `drag_velocity` (`pointer_drag_target::on_release`) | 초당 물리 픽셀, 아래·오른쪽이 양수 | `/ scale` |
   | `pan_handoff_target::release`의 `velocity` | 초당 논리 픽셀, 손가락 방향이 양수 | 그대로 |
   | `sheet_release::velocity` | 초당 논리 픽셀, 높아지는 쪽이 양수 | 그대로 |

   값이 줄어드는 쪽이 손가락 방향이면 부호를 뒤집는다(시트 높이는 아래로 끌면 줄어든다).

6. **재목표와 놓기를 나눈다.** `spring_to`는 이어받는 값과 속도를 쓰고 속도 인자가 없다. 같은 목표로 매 frame 불러도 궤적이 같다(같은 값과 속도에서 다시 출발하면 같은 미래다. 부동소수 오차 안에서다). 손을 뗀 속도는 `spring_release`로만 준다. 이어받는 속도를 실수로 덮지 않으려는 것이다.
7. **움직임 줄이기.** `reduce_motion`은 필수 인자다. 참이면 `spring_to`·`spring_release` 모두 목표에 정지한 스프링을 돌려주고 놓은 속도도 쓰지 않는다. 값은 앱의 `window_metrics::reduce_motion`([움직임 줄이기 연결](reduce-motion-design.md))이다. 도는 중에 참으로 바뀌면 앱이 `spring_at_rest(target)`으로 붙인다.
8. **잘못된 값.** `response`가 0 이하, `damping_fraction`이 0 이하(끝나지 않는 스프링)이거나 유한하지 않음, `from`·`to`·`velocity`가 유한하지 않으면 전환이 없다. 언제 물어도 `to`이고 NaN을 내지 않는다. `to`가 유한하지 않으면 `from`, 그것도 아니면 0이다. `rest_threshold`가 유한하지 않은 것은 0 이하와 같이 기본값으로 본다.
9. 입력·초점·접근성·테마·고대비와 닿는 곳이 없다. 값만 계산한다. 배율은 값의 단위가 정한다.

## 결정

**1. 새 타입인가, transition 확장인가.** 질문: 스프링을 어떤 타입으로 두는가.
- 길 a: 새 값 타입 `spring`과 짝 함수. `transition.h`는 그대로다. 기존 `luil::transition { a, b, {}, ms }`와 test·데모가 변하지 않고, 길이가 없고 끝이 허용치로 정해진다는 차이가 타입에 드러난다. 대가: 앱이 곡선마다 값 타입을 고르고, 곡선을 받는 자리(page_anim)가 둘을 받는 방법을 정해야 한다.
- 길 b: `transition`에 곡선 칸을 뒤에 덧붙인다. 앱은 값 하나로 두 곡선을 다룬다. 대가: 스프링에서 `duration`이 뜻을 잃고 속도 칸이 필요하다. `transition_value`·`transition_to`가 곡선으로 갈라져 `constexpr` 검산이 한쪽에서 깨진다.
- 권장: a. 기존 test와 데모를 건드리지 않고 두 곡선의 성질 차이를 타입으로 말한다. 대가: 위의 하나는 a의 것이다. **승인: 길 a (2026-11-02).**

**2. 계산.** 질문: 값과 속도를 어떻게 구하는가.
- 길 a: 닫힌 해(규칙 2). 시각만의 함수 계약과 test의 시각 지정이 그대로 서고, 재목표 속도를 해에서 바로 얻는다. 대가: 영역이 셋이고 ζ=1 근처의 띠 처리가 든다.
- 길 b: 단계 적분. 외력을 더하기 쉽다. 대가: 상태가 있어 frame을 건너뛰면 값이 달라지고(느린 frame에서 재현되지 않는다) 계약이 깨진다.
- 권장: a. 계약이 서고 속도가 값과 어긋나지 않는다. 대가: test가 띠 양쪽의 연속과 큰 ζ를 따로 보는 것이다. **승인: 길 a (2026-11-02).**

**3. 매개변수 어휘.** 질문: 스프링의 성질을 어떤 손잡이로 고르는가.
- 길 a: `response` + `damping_fraction`(SwiftUI의 `response`·`dampingFraction` 어휘)을 주로 두고 `spring_from_physics`로 (질량, 강성, 감쇠)를 바꾼다. "얼마나 빠르게, 얼마나 튕기게" 두 손잡이만 있다. 질량은 운동을 바꾸지 않고 시간 척도와 감쇠로 흡수된다.
- 길 b: (질량, 강성, 감쇠)(Flutter의 `SpringDescription` 어휘)를 주로 둔다. 세 값이 서로 영향을 줘 시간 감각을 잡기 어렵고 같은 운동에 조합이 무한하다.
- 길 c: `duration` + `bounce`. 끝이 길이가 아니라 허용치로 정해지므로 `duration`이 거짓 이름이 된다.
- 권장: a. 시간 감각과 튕김이 각자 한 손잡이다. 대가: 변환의 `response`가 밀리초로 반올림되는 것이다(1ms보다 짧아지지 않는다). **승인: 길 a (2026-11-02).**

**4. 끝 판정과 허용치.** 질문: 언제 끝났다고 보는가.
- 길 a: `rest_threshold` 한 칸(기본 0.01)과 값·속도를 합친 척도 `s`(규칙 3). 한 번 끝나면 되돌아오지 않는다. 대가: 속도 허용치를 따로 못 고르고, 0~1 값은 앱이 낮춰야 한다.
- 길 b: 값 허용치와 속도 허용치 둘. 대가: 상자 모서리에서 끝났다가 나가므로 "그 뒤로 안에 머문다"를 수치로 찾아야 한다.
- 길 c: 이동 거리에 비례. 대가: 같은 값에서 속도만 주는 놓기(`from == to`)의 허용치가 0이다.
- 권장: a. 끝이 한 번이라 `spring_next_update`가 되살아나지 않는다. 대가: 위의 둘이고, 프리셋과 데모가 값 단위에 맞는 허용치를 보여 준다. **승인: 길 a (2026-11-02).**

**5. 속도 입력.** 질문: 놓은 속도를 어떻게 주는가.
- 길 a: 두 함수. 이어받기(`spring_to`)와 손을 뗀 값에서의 출발(`spring_release`)을 이름으로 가른다. 대가: 함수가 둘이다.
- 길 b: 한 함수에 속도 인자를 더한다. 대가: 이어받은 속도에 더하는지 덮는지 읽히지 않고, 손을 뗀 값이 이전 스프링의 값과 다르니 정지한 스프링을 꾸며 넘겨야 한다.
- 권장: a. 이어받는 속도를 실수로 덮지 않는다. 대가: 함수가 둘인 것이다. **승인: 길 a (2026-11-02).**

**6. 움직임 줄이기.** 질문: 시스템 설정을 어떻게 지키게 하는가.
- 길 a: 필수 `bool reduce_motion` 인자. 앞 문서가 "같은 이름의 인자"로 약속한 모양이다. 대가: 호출마다 인자가 하나 늘고, 앱이 `window_metrics::reduce_motion`을 상태에 들고 있어야 한다.
- 길 b: `motion_spring(config, reduce_motion)` helper(`motion_duration`과 같은 모양). 대가: 앱이 거치는 것을 잊을 수 있다.
- 권장: a. 잊을 수 없다. 대가: 위의 둘이다. **승인: 길 a (2026-11-02).**

**7. 시트가 놓은 속도를 앱에 건네는 길.** 질문: 시트를 놓은 속도를 앱이 어떻게 받는가.
- 길 a: 시트를 바꾸지 않는다. 데모는 속도 0에서 출발하고, 속도 연속은 사이드바 재목표로만 보인다. 대가: 놓는 빠르기와 무관하게 같은 움직임이고 이 노드의 핵심 시연이 시트에 없다. 이 길이면 데모는 `settle` 메시지에서 `add_overlay`가 저장해 둔 자리 높이로 멈춤점 높이를 셈해 `drag_offset`을 속도 0으로 되돌린다.
- 길 b: `sheet_host_config::release` 칸과 `sheet_release`를 더한다(공개 형식). 놓은 순간의 높이·멈출 자리 높이·속도가 한 번에 와서 앱이 화면 높이를 몰라도 스프링을 짠다. 기존 칸은 그대로다. 대가: 시트 공개 API가 늘고, `release`를 쓰면 끌기 끝이 `settle`·`dismiss(drag)` 대신 이 칸으로 나간다.
- 길 c: `settle`·`dismiss`의 서명에 속도를 더한다. 대가: 기존 앱과 `tests/sheet_host_element_tests.cpp`가 깨진다.
- 권장: b. 칸이 없으면 지금과 같게 두고, [sheet-design.md](sheet-design.md)에 단락을 더해 대가를 줄인다. 대가: 위의 둘이다. **승인: 길 b (2026-11-02).**

**8. 적용 범위.** 질문: 어디까지 스프링을 쓰게 하는가.
- 길 a: 값 타입과 데모 두 곳(사이드바 접힘, 시트 놓기).
- 길 b: 시트·사이드바가 스프링을 내장한다. 앱 코드가 줄지만 라이브러리가 시간 상태를 쥐어 "앱 상태" 규칙과 어긋난다.
- 길 c: 스크롤 관성과 가장자리 튕김까지. controller(`src/ui/ui_interaction.cpp`)를 고쳐야 해서 그 파일을 맡은 줄의 노드와 겹친다.
- 권장: a. 라이브러리가 시간 상태를 쥐지 않는다. 대가: 앱이 값을 직접 들어야 하는 것이다. **승인: 길 a (2026-11-02).**

## 플랫폼

### Win32

플랫폼 호출이 없는 순수 계산이라 `src/win32`는 고치지 않는다. 손잡이를 마우스로 끌 때도 `end_pointer_drag`가 `on_release`에 `drag_velocity`를 준다. 데스크톱 데모는 `window_metrics_intent`(`examples/demo/common.h`)에 `reduce_motion` 꼬리 칸을 더하고, 세 값짜리 `make_window_metrics_message` 대신 `make_metrics_message`를 넓혀 받는다. 값은 Win32 전달이 서기 전에는 항상 거짓이다. 그 전달은 같은 라운드의 taskbar가 낸다([움직임 줄이기 연결](reduce-motion-design.md)의 결정 4).

### Android

같은 코드다. Java 소스와 닿는 곳이 없고 unsupported로 둘 것도 없다. 터치 접촉의 속도는 controller 공용 경로(`motion_track::velocity_at`)가 `drag_velocity`로 만든다. `luil_core_tests`는 플랫폼을 모르는 층의 test라 Android에서도 그대로 돌린다. 움직임 줄이기는 앞 노드가 Android의 `window_metrics::reduce_motion`을 채운다. 모바일 데모는 `make_metrics_message`를 이미 넓혔으므로 값이 서면 `app.cpp`가 `sheets_page::set_reduce_motion`으로 넘긴다(`sheets_page::handle`은 metrics 메시지를 보지 못한다).

## 다른 노드와의 약속

- **움직임 줄이기 연결**(앞 노드): `window_metrics::reduce_motion`을 쓴다. 스프링에는 길이가 없어 `motion_duration`을 못 쓰므로, 그 문서의 약속대로 같은 이름의 인자를 `spring_to`·`spring_release`가 받는다. 이 노드는 앞 노드가 선 뒤에 구현했다.
- **page_anim**: 곡선을 받는 자리는 (시각 → 값)과 (시각 → 끝났는가) 둘이면 된다. 0에서 1로 가는 스프링 `spring { 0, 1, started, config }`의 `spring_value`·`spring_finished`가 그 둘이다. 길이 기반 `ease_in_out`과 달리 끝 시각이 없고, ζ가 1보다 작으면 진행률이 [0, 1]을 벗어난다. 곡선 자리는 `spring_config`를 받고(포인터 없는 값이라 element config에 둘 수 있다), 효과가 벗어남을 받거나 자르거나 `spring_smooth`를 쓴다. 끝은 되돌아오지 않으므로 끝난 뒤 이전 tree를 놓아도 된다. 전환 element가 UI thread에서 `draw_context::reduce_motion`을 읽는 쪽은 `spring_*`를 부르기 전에 끝으로 처리한다.
- **놓은 속도를 앱에 건네는 뒤 노드**(controller를 고치는 ink·rotate 같은 것): 속도는 값의 단위 / 초로 건넨다(규칙 5의 표).
- **시트**: 결정 7이 b이므로 `sheet_release`를 이 노드가 세웠다.
- **같은 라운드의 줄 4**: `examples/demo_main.cpp`는 줄 4의 file_watch·taskbar도 고친다. 이 노드는 사이드바(`sidebar_motion_`)와 metrics override(`make_window_metrics_message` → `make_metrics_message`)만 바꾼다. 줄 4는 `bind_host`·`stop_workers`에 한 줄씩, `make_backdrop_message` 뒤에 `make_taskbar_message`를 더해 metrics override 자리를 건드리지 않는다. 모바일 데모의 `app.cpp`는 file_watch가 새 페이지를 이으며 `page_entries`·`handle` 끝·`stop_workers`·`build`를 고치고, 이 노드는 metrics 처리와 `next_tick`만 고친다. 병합은 줄 3이 먼저다.

## 확인

자동 test.

- `tests/spring_tests.cpp`(core, 새 파일, `Catch::Approx`):
  - 출발과 수렴: 시작 시각에 `from`과 `velocity`, 시작 전도 같다. ζ 0.4·1·2.5에서 충분히 뒤에 값이 `to`이고 속도 0이며 끝나 있고 예고가 비었다.
  - 넘침: ζ 1 이상은 정지에서 출발해 1ms마다 3초를 훑어도 `to`를 넘지 않는다. ζ 0.5의 첫 마루는 이동 거리의 `e^(-πζ/√(1-ζ²))`배(약 16.3%)다.
  - 해석 대조: ζ 0.1·0.4·1·2.5·4를 정지·바깥·안쪽 속도로 출발시켜 0.1ms 간격 RK4 적분과 값 1e-3 안에서 견주고, 끝 판정도 같은 적분의 `s`로 견준다. 속도는 RK4의 속도와 맞고 값의 중앙 차분과도 맞는다. ζ 0.1·4·1000과 긴 시간에서도 유한하다.
  - 임계 띠: ζ = 1 ± 2e-6과 ± 5e-7에서 임계해와 값 차가 1e-4 안이다(이동 10).
  - 재목표: 도는 중 `spring_to` 직전과 직후의 값·속도가 같다. 같은 목표로 60번 다시 불러도 한 번에 간 궤적과 1e-3 안이다.
  - 놓기: `spring_release`는 이전 스프링의 속도와 무관하게 준 속도에서 출발하고, 같은 값에서 속도만 주어도(`from == to`) 움직인다.
  - 끝 판정: 훑은 `s`가 줄기만 하고, 끝난 뒤에는 끝나지 않은 표본이 없으며, 끝나면 값이 정확히 `to`다. 프리셋 셋의 끝나는 시각이 위의 값과 맞는다. 허용치를 키우면 일찍 끝나고 0 이하는 기본값이다.
  - 시각만의 함수: 같은 시각은 같은 값이고, 늦은 시각을 먼저 물어도 이른 시각의 답이 같다. 예고는 시작 전 시작 시각, 도는 중 `now + transition_frame`, 끝나면 nullopt다.
  - 움직임 줄이기: 참이면 속도를 줘도 언제나 목표이고 끝나 있다. 잘못된 값(`response` 0·음수, ζ 0·음수, NaN·무한대, `from`·`to`·`velocity`의 NaN·무한대)은 NaN 없이 `to`다.
  - 변환과 상수: `spring_from_physics(1, 100, 20)`은 response 628ms·ζ 1, `(2, 200, 8)`은 ζ 0.2이고 직접 적분한 운동과 반올림 안에서 맞는다. 0 이하는 전환이 없다. `STATIC_REQUIRE`로 `spring_at_rest`와 프리셋을 본다.
- 기존 `tests/transition_tests.cpp`, `tests/sidebar_element_tests.cpp`, `tests/mobile_demo_page_tests.cpp`의 시트 test는 무변경으로 통과한다. 데모의 즉시 적용 메시지(`sheet_settle_intent`·`sheet_drag_intent`)는 그대로 둔다.
- `tests/sheet_host_element_tests.cpp`(결정 7이 b이므로): `release`가 있으면 놓기가 `settle`·`dismiss(drag)` 대신 이것으로 나가고 없으면 지금과 같다. 보이는 높이는 마지막 이동을 더한 값, 목표 높이는 멈출 자리(닫으면 0)다. 속도는 배율 2에서도 논리 픽셀이고 아래로 튕기면 음수다. 손잡이·시트 몸·마우스가 같은 값을 내고, 멈췄다 놓기와 취소·입력 유실은 0이다.
- `tests/mobile_demo_page_tests.cpp`(추가, 시각을 인자로 준다): 놓은 직후의 높이가 `visible_height`이고 기울기가 놓은 속도와 맞으며, 끝나면 정확히 멈춤점이다. 도는 중 다시 잡으면 지금 높이에서 이어 끈다. 움직임 줄이기면 놓자마자 멈춤점이다. 닫는 놓기는 전과 같이 바로 거둔다. `sheet-fling`을 누르면 반과 전체를 오가고 `sheets-motion`이 도는 중과 멈춤을 말한다.
- `luil_umbrella_header`(우산 헤더에 `spring.h`), `luil_core_portability`, `luil_source_style`(`scripts/check_source_style.ps1`).

데모. 상태를 label로 그려 `luil-uia`로 되읽는다.

- 데스크톱 데모: 사이드바 접힘이 `spring_smooth`로 돈다. 셸 왼쪽 아래 구석에 label `sidebar-motion`("사이드바 44 · 멈춤", 도는 중에는 "도는 중")을 root 자식으로 얹는다. 접힌 동안은 사이드바 내용이 없어서 이 label이 폭을 말한다.
- 모바일 데모의 시트 페이지: 놓기(`release`)가 `spring_snappy`로 보이는 높이를 멈춤점까지 보낸다. 높이를 직접 태우지 않고 **끄는 양(`drag_offset`)을 0으로 보내는 스프링**으로 짠다. 놓은 순간의 끄는 양이 `target_height - visible_height`이고 속도는 `-velocity`(높이가 오르면 끄는 양이 줄어든다)다. 이렇게 하면 도는 중에 손잡이를 다시 잡을 때 지금 끄는 양이 곧 스프링의 지금 값이라 화면 높이를 몰라도 이어 끌 수 있다(`drag_offset_ = 지금 끄는 양 + 새로 끈 양`). 닫는 놓기는 전처럼 바로 거둔다(범위 밖). 페이지의 `handle`·`add_overlay`·`build`는 시각을 인자로 받는다(기본은 지금). 시트 안에 한 줄을 더한다(아래 시트일 때만이다. 옆 시트와 서랍에는 멈춤점이 없다).
  - 단추 `sheet-fling`("튕기기")이 UIA가 같은 놓기를 되풀이하게 한다. 반 → 전체는 위로 초당 1800, 전체 → 반은 아래로 1800 논리 픽셀이고, 시트가 `settle_sheet`로 멈춤점을 고르는 규칙은 손으로 놓을 때와 같다.
  - label `sheets-motion`이 상태를 말한다("멈춤" / "도는 중 · 목표 높이 n").
  - 도는 중은 `mobile_driver::next_tick`이 `sheets_page::next_update`를 함께 모아 깨운다.
- UIA(격리 데스크톱): 접기 단추를 Invoke하고 1초 뒤 `sidebar-motion`이 44·멈춤, 다시 펴면 200·멈춤이다. 도는 중에 한 번 더 눌러도 끝은 목표다. 모바일 데모(Windows 창)는 시트를 열고 `sheet-fling`을 Invoke한 뒤 1초 안에 `sheets-motion`이 멈춤이고 시트가 전체 높이다. 도는 중의 값은 시각에 달려 읽지 않는다. Android에는 접근성 계층이 없어 사람이 화면으로 읽는다.

사람 확인.

- Android 실기기에서 시트를 느리게·빠르게 위아래로 놓아 손을 뗀 속도가 이어지는 체감과 튕김 정도(`spring_snappy`)가 자연스러운지 본다.
- 사이드바 접기를 연타했을 때 멈칫함이 사라졌는지 본다.
- 느린 PC나 원격 세션에서 16ms마다 tree를 다시 짓는 데서 끊기는지 본다. 꼬리가 0.6초쯤이라 전보다 오래 돈다.

## 범위 밖

- 스크롤 관성(`advance_inertia`)과 가장자리 튕김. controller를 고치는 일이라 다른 줄의 노드와 겹친다.
- 시트가 닫히며 날아가는 움직임. `dismiss`는 tree에서 빼는 일이고 scrim도 함께 사라진다. 페이드와 초점 복귀를 같이 정해야 해서 따로 설계한다.
- 시트·사이드바에 스프링을 라이브러리가 내장하는 것.
- 벡터(2차원) 값. 축마다 `spring`을 하나씩 든다.
- 외력, 경계 반발, 서로 이어진 스프링.

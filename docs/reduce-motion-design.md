# 움직임 줄이기 연결

시스템의 움직임 줄이기 설정을 앱이 든 값(전환·스프링)까지 닿게 하고, Android가 이 설정을 처음으로 읽게 한다. 공개 형식은 [app_delegate.h](../include/luil/app/app_delegate.h)의 `window_metrics::reduce_motion`과 [transition.h](../include/luil/ui/transition.h)의 `motion_duration`이다. Android가 값을 채우면 `spinner_element`·배지·`tab_bar_element`도 Android에서 멈춘 그림으로 선다.

## 문제

설정은 UI thread 안에서만 쓰인다.

- Win32는 `src/win32/win32_window.cpp`의 `read_reduce_motion`이 `SPI_GETCLIENTAREAANIMATION`을 읽는다. `WM_SETTINGCHANGE`·`WM_THEMECHANGED`에서 다시 읽는다. 값은 `frame_state::reduce_motion`을 거쳐 `draw_context::reduce_motion`으로, 예고 시각을 묻는 `arm_update_timer`에서 `update_context::reduce_motion`으로 간다.
- logic thread에는 닿는 길이 없다. `window_metrics`·`app_delegate`·`ui_frame` 어디에도 이 값이 없다. 앱이 든 `transition`(데모의 사이드바 접힘 폭)은 앱이 logic thread에서 현재 값을 셈해 config로 먹인다. element는 전환 중인지 모르므로 앱은 움직임 줄이기를 지킬 방법이 없다. 곧 오는 스프링과 화면 전환도 같은 모양이다.
- Android는 이 값을 읽지 않는다. `src/android/android_app.cpp`의 `render`는 `frame_state::reduce_motion`을 채우지 않고, 예고 시각을 묻는 `update_context { now }`도 그렇다. 그래서 `spinner_element`·`badge_paint`·`tab_bar_element`가 Android에서 계속 돈다.
- 토스트의 흐려짐(`toast_element::opacity_at`)은 Win32에서도 이 값을 읽지 않는다. 캐럿 깜빡임(`text_input_element::next_update`)과 움직이는 이미지(`image_element::next_update`)도 읽지 않는다.

## 공개 형식

```cpp
// include/luil/app/app_delegate.h
struct window_metrics
{
    // ... 기존 칸
    float text_scale { 1.0f };
    // 시스템의 움직임 줄이기 설정이다 (`draw_context::reduce_motion`과 같은 값).
    // 참이면 앱이 든 전환·스프링은 길이 없이 끝값으로 선다 (`motion_duration`).
    bool reduce_motion { false };
};
```

```cpp
// include/luil/ui/transition.h
// 움직임 줄이기면 0이다. 길이가 0인 `transition`은 언제 물어도 끝값이다.
[[nodiscard]] constexpr std::chrono::milliseconds motion_duration(std::chrono::milliseconds duration, bool reduce_motion) noexcept
{
    return reduce_motion ? std::chrono::milliseconds { 0 } : duration;
}
```

앱은 metrics 메시지를 받으면 상태에 두고, 새 전환을 시작할 때 읽는다.

```cpp
sidebar_motion_ = luil::transition_to(sidebar_motion_, target_sidebar_width(), now, luil::motion_duration(luil::transition_duration, metrics_.reduce_motion));
```

Android의 읽기는 비공개다 (`src/android/android_settings.h`).

```cpp
// 시스템의 애니메이션 배율이 0인가다 (`Settings.Global.ANIMATOR_DURATION_SCALE`). 읽지 못하면 거짓이다.
[[nodiscard]] bool read_reduce_motion(JNIEnv& env, jobject context);
```

`window_metrics`의 새 칸은 꼬리에 둔다. `make_window_metrics`가 쓰는 위치 초기화와 `operator==`(기본)가 그대로 선다.

## 규칙

- 값의 주인은 시스템이다. 앱이 고르지 않고 라이브러리가 해석하지도 않는다.
- 같은 값이 둘로 간다. UI thread의 `draw_context`·`update_context`와 logic thread의 `window_metrics`다. 둘 다 플랫폼이 읽어 둔 값 하나에서 나온다. logic thread에는 메시지로 늦게 닿으므로, 바뀐 직후 한두 frame은 element가 멈춘 그림을 그리는데 앱 값은 아직 움직일 수 있다. 어느 쪽도 틀린 그림은 아니다.
- 배율은 싣지 않는다. 앱이 정할 것은 줄일지 여부이고 Windows에는 배율이 없다.
- 바뀔 때만 다시 알린다. 값이 같으면 메시지가 늘지 않는다. 크기·배율이 바뀌어 보내는 metrics도 지금 값을 싣는다.
- 시작 값은 첫 metrics가 싣는다. Win32의 첫 metrics는 최초 `make_frame()` 앞에서 처리된다(`app_host::config::initial_messages`). Android는 첫 창이 설 때 알린다. 그 전까지 앱 상태의 기본값은 거짓이다.
- 읽지 못하면 거짓이다. 움직임을 두는 쪽이 지금까지의 그림이다.
- 앱은 새 전환을 시작할 때 `motion_duration`을 거친다. 길이 0이면 전환이 없다는 `transition`의 규칙이 그대로 선다.
- 도는 중에 참으로 바뀌면 앱이 끝값으로 붙인다(`transition { to, to, {}, 0ms }`, 데모의 사이드바가 손으로 끌 때 쓰는 모양). 두면 남은 길이만큼 더 돌고 끝난다.
- 보조 창은 시스템 값이라 창마다 다르지 않다. `ui_window::metrics` 콜백(세 값)은 바꾸지 않는다. 앱은 주 창 metrics로 받은 값을 보조 창 tree에도 쓴다.
- UI thread의 element는 지금처럼 `draw_context::reduce_motion`·`update_context::reduce_motion`을 읽는다. 이 노드는 그 코드를 고치지 않는다.
- 세 값짜리 `make_window_metrics_message`만 넓힌 앱은 이 값을 받지 못한다. 폭 등급(`width_class`)과 같은 규칙이다. 받으려면 `make_metrics_message`를 넓힌다.
- 입력·초점·접근성·배율·테마·고대비와 닿는 곳이 없다.

## 결정

### 1. 앱에 닿는 길

logic thread의 앱이 이 값을 받는 길을 어디에 두는가.

- a. `window_metrics`에 칸을 더한다. 크기·배율이 바뀔 때와 같은 `make_metrics_message` 길로 온다. 앱은 메시지 하나만 처리한다. 대가: 창 크기 값에 시스템 설정이 얹힌다(`text_scale`이 선례다). 세 값짜리만 넓힌 앱과 보조 창 콜백은 받지 못한다.
- b. `app_delegate`에 새 가상 함수(`make_system_settings_message` 같은 것)를 더한다. 접근성 설정이 늘 때 한 곳에 모인다. 대가: 훅이 하나 늘고, 플랫폼마다 시작과 바뀔 때의 호출을 따로 짓는다. 앱이 처리할 메시지도 하나 는다.
- c. `app_host`에 조회 함수를 둔다. 대가: logic thread를 깨우는 메시지가 없어 다음 frame까지 모른다. 도는 중인 전환을 바뀐 순간에 붙일 수 없다.

권장은 a다. 대가는 위의 두 가지이고, 보조 창 콜백을 바꾸지 않는 규칙으로 줄인다. 권장대로 진행.

### 2. Android가 읽는 값

- a. `Settings.Global`의 `animator_duration_scale`이 0이다. 앱 animator가 따르는 배율이고 Context만 있으면 읽는다. 대가: 설정 앱의 접근성 토글이 이 값을 쓰는지는 이 저장소에서 확인하지 못했다. 실기기에서 확인한다.
- b. 세 배율(`animator_duration_scale`·`window_animation_scale`·`transition_animation_scale`)이 모두 0이다. 대가: 개발자 옵션에서 animator 배율만 끈 기기는 움직임이 남는다. 읽는 호출이 셋이다.
- c. `ValueAnimator.areAnimatorsEnabled()`(API 26)다. Context 없이 부르고 시스템이 앱 animator에 주는 값을 묻는다. 대가: 설정 값 밖의 사유로도 꺼지는지, 설정이 바뀐 뒤 프로세스에 닿는 때가 언제인지 확인하지 못했다.

권장은 a다. 대가는 접근성 토글과의 관계를 실기기로 확인해야 한다는 것이다. 어긋나면 b나 c로 바꾸는 일은 `read_reduce_motion` 안에서 끝난다. 권장대로 진행.

### 3. Android가 설정 변경을 아는 때

- a. 시작, `APP_CMD_RESUME`, `APP_CMD_GAINED_FOCUS`에서 다시 읽는다. 호출이 한 번씩이고 frame 경로에 들지 않는다. 대가: 앱이 보이고 초점도 있는 채로 바뀌면 다음 재개나 초점까지 모른다.
- b. 변경을 관찰한다(`ContentObserver`, API 33의 `DurationScaleChangeListener`). 둘 다 Java 클래스나 interface 구현이 필요하다. Java 0줄 방침 때문에 unsupported다.
- c. 일정 간격으로 읽는다. 대가: 보이는 동안 주기적으로 깨어나는데 설정은 거의 바뀌지 않는다.

권장은 a다. 대가는 위의 드문 경우 하나다. 권장대로 진행.

### 4. Win32 변경을 누가 고치나

logic thread로 알리려면 `src/win32/win32_window.cpp`의 두 곳을 고쳐야 한다(아래 플랫폼 절). 이 파일은 같은 시기에 작업표시줄 연동과 비공개 helper 추출이 크게 고치는 파일이다.

- a. 이 노드가 직접 고친다(약 10줄). Win32까지 한 번에 끝난다. 대가: 두 작업이 같은 파일의 가까운 구간을 고쳐 합칠 때 충돌할 수 있다. helper 추출이 이 구간을 옮기면 손으로 맞춘다.
- b. 이 노드는 core·Android·데모·test까지 하고, Win32 두 곳은 `win32_window.cpp`를 고치는 플랫폼 작업이 이 문서의 변경대로 함께 고친다. 대가: 그 작업이 들어올 때까지 Windows의 `reduce_motion`은 거짓이다. 앱이 든 값이 Windows에서 움직임 줄이기를 지키지 못하는 것은 지금과 같다.
- c. 노드 전체를 플랫폼 작업으로 옮긴다. 대가: Android 쪽 이득이 미뤄지고, 스프링이 이 필드를 약속으로만 쓰고 먼저 나간다.

권장은 b다. 대가는 Windows 연결이 늦어지는 것이고, 변경이 이 문서에 그대로 적혀 있어 옮기는 쪽이 새로 설계할 것은 없다. 승인 필요.

### 5. 앱이 값을 쓰는 helper

- a. `motion_duration` 하나를 `transition.h`에 둔다. 전환·스프링·화면 전환이 같은 말을 쓴다. 대가: 공개 함수가 하나 늘고, 앱이 거치는 것을 잊을 수 있다.
- b. 두지 않는다. 앱이 `reduce ? 0ms : transition_duration`을 쓴다. 대가: 앱마다 다시 짓고 스프링이 같은 규칙을 따로 정한다.
- c. `transition_to`에 `reduce_motion` 인자를 더한다. 대가: 인자가 길어지고 스프링에 같은 인자가 다시 필요하다.

권장은 a다. 대가는 위의 두 가지이고, 스프링도 같은 이름을 쓰게 해 줄인다. 권장대로 진행.

## 플랫폼

### Win32

읽기와 다시 읽기는 이미 있다. `WM_SETTINGCHANGE`와 `WM_THEMECHANGED`가 같은 case이고 `wParam`으로 거르지 않고 모두 다시 읽는다. 이 방식을 그대로 둔다. 이 메시지가 이 설정에서 오는지는 이 저장소에서 확인하지 못했고 사람이 본다. 값이 바뀌면 `invalidate_all_surfaces`가 다시 그리고 `arm_update_timer`가 새 값으로 예고를 다시 모은다.

빠진 것은 logic thread 알림이고 고칠 곳은 둘이다.

```cpp
// make_window_metrics_message(): 칸 하나를 채운다.
window_metrics metrics { make_window_metrics(width, height, scale) };
metrics.reduce_motion = reduce_motion_;
return environment_.delegate->make_metrics_message(metrics);

// WM_SETTINGCHANGE: 기존 갱신을 모두 한 뒤, 값이 달라졌으면 알린다.
const bool was_reduced { std::exchange(reduce_motion_, read_reduce_motion()) };
// ... 기존 갱신
if (was_reduced != reduce_motion_)
    post_window_metrics();
```

- 레이어드 창이 크기 변경을 미룬 동안에도 `make_window_metrics_message`가 가장 새 미룬 배치의 크기를 싣는다. 크기가 어긋난 metrics는 나가지 않는다. 메시지의 표는 inbox 순서라, 뒤따라 보내는 이 메시지가 앞선 미룬 배치의 표 비교(`deferred_placements::take_ready`)를 흔들지 않는다.
- host가 아직 없을 때(`host_ == nullptr`)의 변경은 `post_window_metrics`가 버린다. 첫 metrics가 그때의 값을 싣는다.
- 보조 창·popup은 같은 `reduce_motion_`을 `apply_frame_appearance`로 받는다. 따로 할 일이 없다.
- 이 두 곳의 소유는 결정 4가 정한다.

### Android

읽기는 JNI 호출 둘이고 Java 소스가 필요 없다. `Context.getContentResolver()`(Activity에서)와 정적 `Settings$Global.getFloat(ContentResolver, String, float)`다. 기존 `android_settings.cpp`의 `read_system_input_settings`와 같은 모양이며 `animator_duration_scale`을 기본값 1로 묻는다. 0 이하면 참이다. 0.5배 같은 배율은 움직임 줄이기가 아니다.

플랫폼 SDK(`android-37.0`)의 `android.jar`와 `api-versions.xml`에서 확인한 것이다.

- `Settings.Global`은 API 17부터이고 `getFloat`은 정적이다. `ANIMATOR_DURATION_SCALE`은 `"animator_duration_scale"`이다.
- `ValueAnimator.areAnimatorsEnabled()`는 API 26이다. `registerDurationScaleChangeListener`와 `DurationScaleChangeListener` interface는 API 33이다.
- `ContentObserver`는 abstract class이고 `DurationScaleChangeListener`는 interface다. 둘 다 구현하는 Java 클래스가 필요해 unsupported다(`java.lang.reflect.Proxy`는 런타임 코드 생성이라 쓰지 않는다).
- `Configuration`에는 `fontScale`만 있고 애니메이션 배율이 없다. 그래서 `APP_CMD_CONFIG_CHANGED`에서는 읽지 않는다. 배율은 구성 변경으로 오지 않는다.

연결 지점은 `android_app.cpp` 다섯 곳이다.

- 멤버 `reduce_motion_`과 `refresh_reduce_motion()`. 다시 읽어 값이 달라졌으면 `dirty_`를 세우고 `post_metrics()`를 부른다.
- `start()`에서 `refresh_system_theme()` 곁에서 한 번, `APP_CMD_RESUME`(설정 앱에서 돌아올 때, 고대비와 같은 이유)과 `APP_CMD_GAINED_FOCUS`(분할 화면처럼 재개 상태인 채 초점만 오가는 경우)에서 부른다. 글자 크기 표처럼 큰 조회가 아니므로 `refresh_system_settings()`에 묶지 않는다.
- `post_metrics()`가 `metrics.reduce_motion`을 채우고, 같은 값 걸러내기에 `reported_reduce_motion_`을 더한다.
- `render()`가 `state.reduce_motion = reduce_motion_`을 채운다.
- `render()` 끝의 `update_context { now }` 둘(주 tree와 overlay)이 `reduce_motion_`을 싣는다.

## 다른 노드와의 약속

- **spring**: 이 노드가 세운 `window_metrics::reduce_motion`과 `motion_duration`을 쓴다. `spring_to`는 참이면 목표에 정지한 값(속도 0)을 돌려주는 길을 같은 이름의 인자로 연다. 놓은 속도(`drag_velocity`)가 있어도 참이면 쓰지 않는다. 모양은 스프링 설계가 정한다.
- **page_anim**: 전환 element가 UI thread에서 `draw_context::reduce_motion`을 읽어 끝 상태를 바로 그리면 앱은 몰라도 된다. 앱이 이전 tree를 쥐고 전환을 시작하는 쪽은 `window_metrics::reduce_motion`이 참이면 전환을 시작하지 않는다. 이전 tree를 쥘 이유가 없다.
- **`win32_window.cpp`를 고치는 플랫폼 작업**(결정 4가 b일 때): 위 Win32 절의 두 변경과 `tests/win32_window_tests.cpp`의 test를 함께 맡는다.
- **앱이 시간 기반 값을 소유하는 노드 모두**: 새 값의 시작 길이를 `motion_duration`으로 거친다. 도는 중에 바뀌면 끝값으로 붙인다.

## 확인

자동 test.

- `tests/window_metrics_tests.cpp`(추가): `make_window_metrics`가 만든 값의 `reduce_motion`은 거짓이다. 이 칸만 다른 두 metrics는 같지 않다. `make_metrics_message`를 넓힌 delegate는 참인 값을 그대로 받고, 세 값짜리만 넓힌 delegate는 지금처럼 돈다(기존 test가 지킨다).
- `tests/transition_tests.cpp`(추가, `STATIC_REQUIRE`): `motion_duration`은 참이면 0이고 거짓이면 그대로다. 참인 길이로 시작한 `transition_to`는 지금 값에서 출발해도 어느 시각에 물어도 목표값이고, `transition_finished`가 참이며 `transition_next_update`가 비어 있다.
- 기존 `tests/ui_foundation_tests.cpp`(spinner), `tests/tab_bar_style_tests.cpp`, `tests/raster_draw_tests.cpp`(`derive_draw_context`)는 무변경으로 통과해야 한다.
- 결정 4가 b면 Win32 몫이다. `tests/win32_window_tests.cpp`는 첫 metrics의 `reduce_motion`이 `SystemParametersInfoW`로 읽은 값과 같은지 본다. `on_started`에서 같은 창에 `WM_SETTINGCHANGE`를 보내고 값이 그대로면 delegate의 `make_metrics_message` 호출이 늘지 않는지 본다. OS 설정을 바꾸는 test는 두지 않는다.
- Android의 JNI 읽기는 자동 test가 없다. 기기 test 실행기(`app_process`)에는 앱 Context가 없다.
- `scripts/check_source_style.ps1`. 새 공개 헤더가 없어 우산 헤더 검사는 그대로다.

데모. 모바일 데모의 기기 페이지(`examples/mobile_demo/device_page.cpp`)에 칸을 더한다.

- `motion-heading`("움직임 줄이기")과 label `motion-state`("켜짐 · 도는 표시가 멈춘다" / "꺼짐 · 도는 표시가 돈다"), 그 아래 `spinner_element` 하나다. 값은 `window_metrics_intent`의 꼬리 칸으로 `build`에 온다(`common.h`, `app.cpp`의 `make_metrics_message`).
- 모바일 데모는 Windows 창으로도 뜬다(`examples/mobile_demo/main.cpp`). Win32 변경이 서면 `luil-uia`로 `motion-state`를 되읽는다. Android에는 접근성 계층이 없어 사람이 화면으로 읽는다.
- 데스크톱 데모(`examples/demo`)는 건드리지 않는다. 같은 값을 모바일 데모가 Windows에서도 보이고, `demo_main.cpp`는 스프링 시연이 고치는 공용 파일이다.

사람 확인. 시스템 설정을 바꾸는 일이라 사람이 한다.

- Windows 설정의 애니메이션 효과를 껐다 켠다. 재시작 없이 `motion-state`가 바뀌고 spinner가 멈추는지 본다(Win32 변경 뒤).
- Android 실기기에서 개발자 옵션의 애니메이터 길이 배율과 접근성의 애니메이션 제거(있다면)를 각각 껐다 켠다. 설정 앱에서 돌아오면 바뀌는지, 분할 화면에서 다른 창의 설정을 바꾸고 이 창을 눌렀을 때 바뀌는지 본다. 어느 설정이 `animator_duration_scale`을 바꾸는지도 이때 적는다.

## 범위 밖

- 토스트 흐려짐·캐럿 깜빡임·움직이는 이미지의 재생. 어느 플랫폼도 지금 읽지 않는다. 흐려짐은 움직임이 아니라는 견해도 있어 따로 정한다. 이미지 재생은 앱이 `playback`으로 정하므로, 앱이 이 칸을 보고 멈출 수 있다.
- 애니메이션 배율(느리게·빠르게) 값 전달.
- 다른 접근성 설정(Windows의 메뉴 애니메이션·투명 효과 등)의 logic thread 전달.
- Android의 변경 관찰(`ContentObserver`, `DurationScaleChangeListener`). Java 0줄 방침 때문에 unsupported다.
- 스프링과 화면 전환 구현. 각각의 설계 문서가 맡는다.
- iOS.

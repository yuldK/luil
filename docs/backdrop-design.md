# 창 배경 효과

주 창 뒤에 mica나 acrylic 같은 배경 효과를 깐다. 앱은 효과를 **뜻으로** 고르고, 어떤 길로 세울지는 luil이 표시 방식(표준·레이어드)과 OS 사정에 맞춰 고른다. 효과가 실제로 선 frame에서만 창 바탕이 옅어지고, 서지 않으면 창은 지금처럼 불투명하며 그 까닭을 앱에 알린다.

공개 형식은 [app_host.h](../include/luil/app/app_host.h)의 `window_backdrop`·`ui_frame::backdrop`과 [win32_window.h](../include/luil/win32/win32_window.h)의 `backdrop_status`·`backdrop_report`·`window_delegate::make_backdrop_message`다.

```cpp
// make_frame
frame->backdrop = luil::window_backdrop::acrylic;

// window_delegate
luil::app_message make_backdrop_message(const luil::win32::backdrop_report& report) override
{
    return luil::app_message { backdrop_state_intent { report } };
}
```

## 고를 수 있는 효과

| 값 | 뜻 |
| --- | --- |
| `none` | 효과 없음. 지금까지의 창이다. |
| `mica` | 바탕화면 색을 옅게 깐다. 뒤 창은 비치지 않는다. |
| `mica_alt` | mica보다 진한 변형이다. |
| `acrylic` | 뒤에 있는 것을 흐리게 비춘다. |

효과는 테마처럼 **frame마다** 싣는다. 바뀐 frame을 받은 UI thread가 DWM 속성을 다시 건다. 설정 화면의 토글이 다시 띄우지 않고 바로 반영되는 것이 이 자리의 이유다.

## 어떤 길로 서는가

빨강·파랑 반반 판 위에 창을 띄우고 창 안 경계가 번지는지로 잰 결과다(실기기, 활성 창).

| 효과 | 표준 · CPU 렌더러 | 표준 · GPU 렌더러 | 레이어드 |
| --- | --- | --- | --- |
| 시스템 acrylic `DWMSBT_TRANSIENTWINDOW` | 흐림 | 흐림 | 단색 판 |
| 시스템 mica·mica alt | 바탕화면 색 | 바탕화면 색 | 단색 판 |
| accent acrylic (비공개 API) | 회색 판 | 회색 판 | 흐림 |
| accent blur (비공개 API) | 회색 판 | 회색 판 | 아주 어두운 흐림 |
| 효과 없이 투명한 바탕 | 검정 | 창이 통째로 비침 | 창이 통째로 비침 |

그래서 길을 이렇게 고른다([`decide_backdrop`](../src/win32/backdrop.h)).

- 표준 표시는 `DWMWA_SYSTEMBACKDROP_TYPE`이다. 렌더러가 GPU에서 CPU로 물러서도 효과가 그대로다.
- 레이어드 표시의 `acrylic`은 비공개 `SetWindowCompositionAttribute`의 acrylic blur behind다. 레이어드에서 `mica`·`mica_alt`는 길이 없어 `unsupported_presentation`이다.
- accent blur는 열지 않는다. 쓸 만한 그림이 서는 조합이 없다.

비공개 API는 문서화되지 않아 Windows 업데이트에서 바뀔 수 있다. 그 위험을 luil 안에 가두려고 효과를 뜻으로 고르게 했다 — 앱 코드에는 그 이름이 나오지 않는다.

## 서지 않는 까닭

| `backdrop_status` | 까닭 | 사용자가 바꿀 수 있나 |
| --- | --- | --- |
| `off` | `none`을 골랐다 | — |
| `applied` | 섰다 | — |
| `unsupported_presentation` | 레이어드 표시에서 mica 계열 | 아니다 |
| `unsupported_system` | 시스템 배경 효과가 없는 Windows(11 22H2 이전), 또는 accent API를 찾지 못함 | 아니다 |
| `high_contrast` | 고대비 | 그렇다 |
| `remote_session` | 원격 데스크톱 — DWM 효과를 그리지 않는다 | 그렇다 |
| `transparency_disabled` | 설정 앱의 "투명 효과"가 꺼짐 | 그렇다 |

바꿀 수 없는 까닭을 먼저 답한다. 레이어드 창에서 mica를 골랐는데 투명 효과까지 꺼져 있을 때 "투명 효과를 켜세요"라고 안내하면, 켜도 서지 않으므로 안내가 틀린 것이 된다.

상태가 바뀔 때마다 한 번 알린다(`make_backdrop_message`). 앱이 고른 효과가 바뀌었을 때, 그리고 OS 설정·고대비가 바뀌었을 때(`WM_SETTINGCHANGE`·`WM_THEMECHANGED`)다. 시스템 배경 효과를 받는지는 주 창을 만들 때 기본값(`DWMSBT_AUTO`)을 걸어 한 번 묻는다. 모서리 선택을 묻는 것과 같은 방식이다.

## 바탕을 옅게 하는 것

효과가 비치려면 창의 바탕이 반투명해야 한다. 그런데 효과가 서지 않은 창에 반투명 바탕을 칠하면 위 표의 마지막 줄처럼 창이 검게 되거나 통째로 비쳐 글을 읽을 수 없다. 그래서 바탕을 옅게 하는 판단은 앱 스타일이 아니라 luil이 든다.

- 효과가 **실제로 선** frame에서만 `frame_state::backdrop`이 참이다. 그때 창 전체를 칠하는 바탕(`draw_frame`의 `clear`)과 캡션 바탕이 `backdrop_background`가 된다.
- `backdrop_background`는 스타일의 중립 색이다(`assets/style.json`의 `backdropBackground`, 테마마다). 알파가 효과를 얼마나 비출지다. 내장값은 어두운 테마 `#1e1e1e59`, 밝은 테마 `#f8f8f866`이다. 이 키가 없는 옛 스타일은 `windowBackground`를 쓴다 — 효과가 서도 불투명해 지금과 같은 그림이다.
- 창 바탕 **역할**(`window_background`)은 그대로 둔다. 체크 표시처럼 그 색을 대비로 쓰는 자리가 있어 옅게 하면 표시가 흐려진다. 표면(`surface_background`)·입력칸·떠 있는 판도 불투명하다(실측: 효과가 선 창의 표면 칸은 효과 없는 창과 같은 색이다).

## DWM 프레임

효과가 선 표준 창은 DWM 프레임을 창 전체로 넓힌다(`MARGINS` -1, `caption_surface::set_backdrop_glass`). 넓히지 않은 자리는 client의 알파가 버려져 검게 보인다. 표면이 프레임을 다시 거는 자리(전체 화면 전환·합성 재시작)도 이 값을 따른다.

시스템 배경 효과를 건 창은 캡션이 없는데도(`WM_NCCALCSIZE`가 0) 위쪽 캡션 높이만큼을 회색 띠로 따로 칠한다(실측). `DWMWA_CAPTION_COLOR`를 `DWMWA_COLOR_NONE`으로 두면 그 띠가 효과와 같아진다. 효과를 거둘 때 `DWMWA_COLOR_DEFAULT`로 되돌린다.

레이어드 창의 accent acrylic은 플래그 0으로 건다. 플래그 2(색 막을 쓴다)는 색 막 알파가 0x10 이하에서 거의 검게 되고 0x40에서도 한 단 어둡다(실측). 색 막은 창 요소의 명암(`apply_frame_theme`)을 따라 어두운 쪽 `0x40202020`, 밝은 쪽 `0x40f3f3f3`이다.

레이어드 창은 알파 0 픽셀을 마우스가 지나가게 한다. 옅은 바탕은 알파가 0이 아니라 클릭이 창에 온다. 바탕 알파를 0으로 둔 스타일은 [레이어드 표시](layered-presentation-design.md)의 알파 바닥(1로 올림)이 받는다.

## 비활성 창

시스템 배경 효과는 비활성 창에서 단색 대체 색이 된다(실측: 활성으로 그리지 않은 시험 창은 acrylic도 (84,84,84) 판이었다). Windows의 동작이고 luil이 바꾸지 않는다. 앱이 그 상태를 따로 알 필요는 없다 — 바탕은 여전히 옅게 칠하지만 DWM이 그 아래를 단색으로 깐다.

## 검증

[backdrop_tests.cpp](../tests/backdrop_tests.cpp)가 표시 방식·OS 사정별로 고르는 길과 까닭의 순서를 창 없이 본다. [style_tests.cmake](../tests/style_tests.cmake)는 `backdropBackground`가 없는 스타일이 창 바탕으로 물러서는지 본다.

합성된 화면은 [`luil_backdrop_screen_check`](../tests/backdrop_screen_check.cpp)가 본다. 빨강·파랑 반반 판 위에 luil 창을 띄우고, 창 위쪽은 창 바탕만 두고 아래쪽에는 불투명한 표면 칸을 둔다. 사람의 입력 데스크톱에 창을 띄우므로 `LUIL_SCREEN_TESTS=1`일 때만 돈다(CTest 레이블 `screen`). 초점을 빼앗지 않으려고 `WM_NCACTIVATE`로 활성 모습만 세운다.

| 표시 · 효과 | 상태 | 창 바탕(왼/오) | 경계 번짐 | 위쪽(캡션 자리) | 표면 칸 |
| --- | --- | --- | --- | --- | --- |
| 표준 CPU · none | `off` | (30,30,30)/(30,30,30) | 0 | (30,30,30) | (45,45,48) |
| 표준 CPU · mica | `applied` | (31,31,30)/(30,32,27) | 0 | (30,31,30) | (45,45,48) |
| 표준 CPU · acrylic | `applied` | (157,27,27)/(43,43,170) | 76 | (157,27,27) | (45,45,48) |
| 표준 GPU · none | `off` | (30,30,30)/(30,30,30) | 0 | (30,30,30) | (45,45,48) |
| 표준 GPU · mica | `applied` | (31,31,30)/(30,32,27) | 0 | (30,31,30) | (45,45,48) |
| 표준 GPU · acrylic | `applied` | (157,27,27)/(43,43,170) | 76 | (157,27,27) | (45,45,48) |
| 레이어드 · none | `off` | (30,30,30)/(30,30,30) | 0 | (30,30,30) | (45,45,48) |
| 레이어드 · mica | `unsupported_presentation` | (30,30,30)/(30,30,30) | 0 | (30,30,30) | (45,45,48) |
| 레이어드 · acrylic | `applied` | (114,31,31)/(29,29,128) | 75 | (111,31,31) | (45,45,48) |
| 표준 GPU · acrylic · 밝은 테마 | `applied` | (247,179,179)/(184,184,247) | 70 | (247,179,179) | (45,45,48) |
| 레이어드 · acrylic · 밝은 테마 | `applied` | (235,168,168)/(166,166,232) | 73 | (235,170,170) | (45,45,48) |

캡션 색을 "없음"으로 두기 전에는 표준 창의 위쪽이 (70,70,70)이었다. 레이어드 acrylic을 플래그 2·색 막 알파 0x10으로 걸었을 때는 창 바탕이 (18,13,13)/(12,12,17)로 거의 검었다.

격리 데스크톱에서는 데모(`--page=theme`)의 효과 고르기를 UIA로 몰아 표준·레이어드 표시에서 상태 문구가 맞게 바뀌는 것을 확인했다.

## 범위

- 주 창만이다. 보조 창과 popup은 효과가 없다.
- Android는 `ui_frame::backdrop`을 보지 않는다.
- 효과의 세기(흐림 반경·색 막)는 OS가 정한다. 앱이 고르는 것은 효과의 종류와 `backdropBackground`의 알파다.
- 전체 화면 창에 효과를 건 경우는 재지 않았다. 효과가 선 창은 전체 화면에서도 DWM 프레임을 창 전체로 넓힌다(`MARGINS` -1). 효과가 없는 전체 화면 창이 위쪽 여백을 0으로 두어 피한 이음매가 이 경우에 다시 서는지는 확인하지 않았다.

## 반드시 유지할 불변식

- 바탕을 옅게 칠하는 것은 효과가 실제로 선 frame뿐이다.
- 창 바탕 역할(`window_background`)의 색은 효과와 상관없이 그대로다.
- 시스템 효과와 accent 효과를 한 창에 함께 걸지 않는다.
- 효과가 선 표준 창의 DWM 프레임은 창 전체다. 다시 거는 자리도 그 값을 따른다.
- 바꿀 수 없는 까닭을 바꿀 수 있는 까닭보다 먼저 알린다.

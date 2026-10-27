# 레이어드 표시: 창 배치와 frame을 한 합성에

Win32 주 창의 위쪽이나 왼쪽 끝이 움직이는 크기 변경에서, 아래·오른쪽에 붙은 내용이 한 합성 동안 다른 자리에 보인다. 캡션을 창 아래에 둔 창이 위로 커지면 크기를 바꾼 직후 한 frame 동안 캡션이 창 맨 위에 그려진다. `window_presentation::layered`는 창 배치와 그 배치에 맞는 frame을 **같은 합성에** 내서 이 frame을 없앤다.

공개 형식은 [win32_window.h](../include/luil/win32/win32_window.h)의 `window_config::presentation`과 `layered_click_through`다.

```cpp
luil::win32::window_config config {};
config.presentation = luil::win32::window_presentation::layered;
```

## 왜 생기는가

DWM은 창의 새 위치·크기를 다음 합성에 바로 반영한다. 그런데 그 합성에서 새 창 영역에 보이는 것은 **이전 frame을 새 창 왼쪽 위에 붙인 것**이고, 새로 그린 frame은 그다음 합성에야 보인다. 이전 frame은 이전 크기에 맞게 배치된 그림이라 아래·오른쪽 기준 내용은 그 한 합성 동안 반드시 엉뚱한 자리에 보인다. 앱이 어떤 순서로 그리든 피할 수 없다.

`SWP_NOCOPYBITS`, 새 tree가 온 뒤에만 `SetWindowPos`하기, `WM_NCCALCSIZE`의 `WVR_VALIDRECTS`는 모두 효과가 없었다. 효과가 있었던 것은 레이어드 창(`WS_EX_LAYERED`)에 `UpdateLayeredWindow`로 위치·크기·그림을 **한 호출로** 넘기는 길뿐이다.

## 흐름

```
크기 변경 (끌기·최대화·배율·전체 화면·앱의 배치 요청)
  → WM_WINDOWPOSCHANGING
      기본 처리로 크기 한계를 다듬은 뒤 SWP_NOSIZE | SWP_NOMOVE를 더한다 — 창은 아직 그대로다
      미룬 배치에 쌓고, 그 크기의 metrics를 앱에 알린다 (표를 받는다)
  → logic thread: metrics 처리 → make_frame → tree 게시 (그 tree가 어느 표까지 처리했는지 함께)
  → UI thread: 게시 wake → 바로 그린다
      tree의 표로 미룬 배치를 꺼낸다 → frame을 그 크기로 세워 그린다
      → UpdateLayeredWindow(위치, 크기, 그림) 한 번
```

- 시스템 크기 조절 루프는 창의 실제 크기가 아니라 처음 사각형과 마우스 변위로 다음 사각형을 셈한다. 그래서 위치·크기 변경을 걷어 내도 끌기가 어긋나지 않는다(실측: 끄는 동안 창 위 끝이 마우스를 따라 120..240을 오갔다).
- 크기가 화면의 frame과 같은 배치(자리만 옮기기, 최소화에서 되살리기)는 그대로 OS에 맡긴다. 화면의 그림이 이미 그 크기에 맞는다. 다만 앞서 미룬 배치가 남아 있으면 그 뒤에 줄을 세운다.
- 창이 보이지 않거나 최소화돼 있거나 host가 없으면(smoke) 미루지 않는다.

### 어느 tree가 새 크기인가 — 표

UI thread는 "이 tree가 새 크기로 배치됐는가"를 앱의 협조 없이 알아야 한다. tree의 root 크기를 보는 방법은 앱이 root를 창 크기로 배치한다는 가정에 기대므로 쓰지 않는다. 대신 메시지 순서를 쓴다.

- `messaging::channel::post`가 받은 envelope의 접수 번호를 돌려준다. `app_host::post_app_message_ticketed`는 그 번호 + 1을 **표**로 돌려준다(0은 "표 없음").
- logic thread는 처리한 메시지의 표를 들고 있다가 frame을 게시할 때 tree와 **한 자물쇠 안에서** 함께 게시한다. 따로 두면 UI thread가 tree를 집은 뒤 표를 읽기 전에 다음 판이 끼어들어, 옛 tree에 새 표가 붙는다.
- UI thread는 `acquire_ui_tree` 뒤 `acquired_tree_ticket`으로 그 tree의 표를 읽는다. metrics의 표 이하이면 그 tree는 그 크기 메시지를 처리한 뒤에 지어졌다.

### 미룬 배치를 꺼내는 규칙

[`deferred_placements`](../src/win32/layered_placement.h)가 규칙을 든다. 창 없이 test한다([layered_placement_tests.cpp](../tests/layered_placement_tests.cpp)).

| 상황 | 낼 배치 |
| --- | --- |
| tree가 반영한 배치가 있다 | 그 가운데 **가장 새 것**. 그보다 오래된 배치는 버린다 |
| 반영한 배치가 없다 | 내지 않는다 (지금 자리·크기로 그린다) |
| 가장 오래 기다린 배치가 250ms를 넘었다 | 가장 새 배치를 가진 tree로 내고 모두 비운다 |
| 표가 0인 배치 (앱이 metrics를 받지 않는다) | 다음 frame에 바로 낸다 |
| 자리만 옮긴 배치 | 앞 배치의 표를 이어받는다 |

마지막 배치의 tree만 기다리면 끄는 동안 logic이 몇 개를 한 batch로 처리할 때마다 기다림이 밀려 창이 멈춘다. 그래서 온 tree가 반영한 가장 새 배치를 낸다.

250ms 한도는 앱이 크기 메시지를 받고도 tree를 다시 짓지 않거나 오래 걸릴 때 창이 굳어 보이지 않게 하는 것이다. 한도를 넘겨 낸 frame에는 표준 표시와 같은 어긋남이 한 번 보일 수 있다.

### `UpdateLayeredWindow`가 보내지 않는 것

`UpdateLayeredWindow`는 위치·크기를 바꿔도 `WM_WINDOWPOSCHANGING`·`WM_WINDOWPOSCHANGED`·`WM_SIZE`·`WM_MOVE`를 보내지 않는다(실측). `GetWindowRect`와 `GetWindowPlacement`의 `rcNormalPosition`은 새 값을 돌려준다. 그 메시지들이 하던 일은 이렇게 옮겼다.

| 일 | 표준 표시 | 레이어드 표시 |
| --- | --- | --- |
| 렌더러 크기 | `WM_SIZE` | 그 frame의 크기로 렌더러가 스스로 맞춘다 |
| metrics 알림 | `WM_SIZE` | 미룰 때 미룬 크기로 알린다 |
| popup 닫힘(`surface_resized`) | `WM_SIZE` | 미룰 때 |
| popup 따라가기(`surface_moved`) | `WM_MOVE` | 낸 뒤, 자리가 바뀌었으면 |
| 배치 보고 | `WM_EXITSIZEMOVE`·최대화 전환 | 미룬 배치가 남은 동안은 보류했다가 마지막 배치를 낸 뒤 |

최대화·복원처럼 상태가 바뀌는 변경은 미뤄도 `WM_SIZE`가 옛 크기로 온다(`SWP_STATECHANGED`). 그 `WM_SIZE`는 최대화 전환의 보고만 하고 렌더러·metrics는 건드리지 않는다. 미룬 동안 `rcNormalPosition`은 아직 옛 값이라 배치 보고를 낸 뒤로 미룬다.

## 배치 요청 API

새 API를 더하지 않았다. 앱의 배치 요청(`ui_frame::window_placement_request`, 번호가 바뀔 때만 이행)이 `SetWindowPlacement`를 지나므로 레이어드 표시에서는 그대로 위의 흐름을 탄다.

- 요청한 크기의 frame이 준비되기 전에는 창 배치를 바꾸지 않는다.
- 요청이 연달아 오면 tree가 반영한 가장 새 것을 낸다.
- 반영한 크기는 metrics 알림으로 간다. 앱의 요청을 배치 보고로 되울리지는 않는다(표준 표시와 같다). 끌기 끝·최대화 전환·종료의 보고는 그대로다.

## 렌더러

[`cpu_layered_renderer`](../src/win32/cpu_skia_renderer.cpp)는 Skia가 DIB section의 픽셀에 바로 그리게 한다(`SkSurfaces::WrapPixels`). Windows의 N32는 premultiplied BGRA라 `UpdateLayeredWindow`가 요구하는 형식과 같아 복사가 없다. 매 frame `UpdateLayeredWindow(…, ULW_ALPHA)`로 내고, 미룬 배치를 낼 때만 `pptDst`를 준다. `SetLayeredWindowAttributes`는 부르지 않는다 — 부르면 그 창은 `UpdateLayeredWindow`를 받지 않는다.

레이어드 창에는 합성 스왑체인이 붙지 않으므로 CPU 렌더러만 쓴다.

- `renderer`가 `direct3d`·`gpu`면 창을 만들기 전에 시작 실패로 알린다. GPU라고 믿는 앱을 조용히 CPU로 돌리지 않는다.
- `automatic`은 CPU다.
- 웹뷰는 설 자리가 없다. CPU 렌더러와 같다 — 자리표가 placeholder를 그린다([웹뷰 합성](webview-composition-design.md)).

## 마우스 판정

레이어드 창은 알파 0인 픽셀을 마우스 판정에서 투명하게 본다(문서화된 동작). 그 자리의 클릭은 뒤 창으로 간다.

- 기본(`layered_click_through = false`)은 바탕 알파가 0인 frame에서 알파 0 픽셀을 1로 올려 낸다. premultiplied라 1/255 검정이고 눈에는 투명하다. 창 안의 빈 자리 클릭이 창에 온다.
- 바탕 알파가 0이 아니면 알파 0 픽셀이 생길 수 없어 전체를 훑지 않는다. 기본 테마와 배경 효과의 옅은 바탕(`backdrop_background`)이 그렇다.
- `layered_click_through = true`면 알파 0 픽셀을 그대로 둔다.

## 모서리·테두리·그림자

레이어드 창은 `DWMWCP_DEFAULT`에서 모서리도 DWM 테두리도 그림자도 없는 직각 판이다(실측). `apply_dwm_frame`이 레이어드 창에 `DWMWCP_ROUND`를 명시하면 둥근 모서리·1px 테두리·그림자가 표준 표시의 주 창과 같은 모양으로 선다(실측, 같은 자리의 화면 캡처를 견줬다). 전체 화면에서는 `DWMWCP_DONOTROUND`다.

## 배경 효과

배경 효과(`ui_frame::backdrop`)는 [창 배경 효과](backdrop-design.md)가 다룬다. 레이어드 표시에서는 시스템 배경 효과(`DWMSBT_*`)가 흐림 없는 단색 판이 되므로(실측), `acrylic`은 비공개 accent acrylic으로 세우고 `mica`·`mica_alt`는 `backdrop_status::unsupported_presentation`으로 알린다.

## 범위

- **주 창만**이다. 보조 창과 popup은 표준 표시로 남는다. popup도 위·왼쪽 끝이 움직이면 같은 어긋남이 있을 수 있다.
- Direct3D·DirectComposition 경로는 재지 않았다. 스왑체인 visual도 창 왼쪽 위에 붙고 Present가 창 이동보다 늦게 반영되므로 같은 종류의 문제가 있을 가능성이 높다.
- 이동만 하는 끌기(시스템 이동 루프)는 크기가 같아 미루지 않고 OS가 그대로 옮긴다. 다시 그리지 않는다. 이동 루프에서 커서 대비 창 지연이 일반 창과 같다는 것은 요청과 함께 온 측정이고, luil 쪽에서는 다시 재지 않았다.

## 검증

단위 test는 미룬 배치의 꺼내기 규칙과 채널의 접수 번호를 본다([layered_placement_tests.cpp](../tests/layered_placement_tests.cpp), [messaging_channel_tests.cpp](../tests/messaging_channel_tests.cpp)).

합성된 화면은 [`luil_layered_screen_check`](../tests/layered_screen_check.cpp)가 본다. 창 아래에 자홍 막대, 오른쪽에 청록 막대를 두고 위·왼쪽 끝만 120px씩 옮기며, `DwmFlush`마다 화면 DC를 `BitBlt`해 두 막대가 고정된 아래·오른쪽 끝에 있는지 센다. **창 DC에서 픽셀을 읽는 검사는 이 어긋남을 보지 못한다.** 사람의 입력 데스크톱에 창을 띄우므로 `LUIL_SCREEN_TESTS=1`일 때만 돌고(CTest 레이블 `screen`, 아니면 건너뜀 77), `--drag`를 주면 실제 마우스로 위 테두리를 끄는 길도 잰다. 표준 표시에서 어긋남이 나와야 통과한다 — 어긋남을 못 보는 검사는 레이어드의 0도 증명하지 못한다.

```powershell
$env:LUIL_SCREEN_TESTS = '1'
build\vs2026-tests\tests\Debug\luil_layered_screen_check.exe --drag
```

실기기(3840×2160 화면) 결과다. 크기 변경 하나마다 320ms를 캡처했다(끌기는 12px마다 16ms).

| 길 | 표준: 어긋난 frame / 크기 변경 | 레이어드 |
| --- | --- | --- |
| 다른 thread의 `SetWindowPos` | 0 / 32 | 0 / 32 |
| 앱의 배치 요청 | 6~8 / 32 | 0 / 32 |
| 위 테두리 마우스 끌기 | 8~9 / 60 | 0 / 60 |

다른 thread의 `SetWindowPos`는 표준 표시에서도 어긋나지 않았다. UI thread가 보낸 메시지를 받는 동안 그 thread가 기다리므로 크기 변경과 다시 그리기가 한 합성에 들어간 것으로 보인다. 사람의 끌기와 앱의 요청은 UI thread에서 `SetWindowPos`를 부르는 길이라 어긋난다.

격리 데스크톱에서는 데모(`--presentation=layered --caption=bottom --page=windows`)를 UIA로 몰아 끝 옮기기 네 방향, 최대화·복원, 전체 화면 전환, 보조 창 열기, 배율 변경(`WM_DPICHANGED`)이 창 사각형과 아래 캡션의 자리에 맞게 반영되는 것을 확인했다.

## 반드시 유지할 불변식

- 레이어드 주 창의 크기 변경은 그 크기로 배치한 tree의 frame과 **같은 `UpdateLayeredWindow` 호출**로만 화면에 닿는다.
- tree와 그 표는 한 자물쇠 안에서 게시하고 읽는다.
- 낼 배치는 tree가 반영한 가장 새 배치다. 마지막 배치만 기다리지 않는다.
- 미룬 동안 앱에 알리는 크기는 지금 창이 아니라 가장 새 미룬 배치다.
- 미룬 배치가 남은 동안 배치 보고는 보류한다.
- 레이어드 창에 `SetLayeredWindowAttributes`를 부르지 않는다.

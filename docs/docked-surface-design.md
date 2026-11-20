# 도킹 표면

도킹 표면은 일반 창과 따로 화면에 붙는 작은 판이다. 작업 표시줄 곁의 위젯, 화면 가장자리에 붙는 패널, 알림 띠에 쓴다.
앱은 `ui_frame::docked_surfaces`에 `ui_docked_surface`를 싣는다. popup이나 보조 창처럼 앱 상태라서, 실으면 서고 빼면 사라진다.

공개 계약은 [`include/luil/app/app_host.h`](../include/luil/app/app_host.h)에 있다. Win32 구현은
[`src/win32/docked_surface.h`](../src/win32/docked_surface.h)와 [`src/win32/win32_window.cpp`](../src/win32/win32_window.cpp)의 대조 코드에 있다.
가장자리 자리를 물리 사각형으로 푸는 순수 계산은 [`src/host/docked_placement.h`](../src/host/docked_placement.h)에 있다.

| | 주 창·보조 창 | popup | 도킹 표면 |
| --- | --- | --- | --- |
| 캡션·테두리 | 있다 (custom caption) | 없다 | 없다 |
| 작업 표시줄 단추·Alt+Tab | 주 창·독립 창은 있다 | 없다 | 없다 |
| 맨 위에 뜬다 | 아니다 | 앵커 위에 뜬다 | 언제나 (`WS_EX_TOPMOST`) |
| 누르면 활성화된다 | 그렇다 | 아니다 | 아니다. 앱이 청할 때만 잠깐 된다 |
| 자리 | 처음 한 번, 그 뒤는 사용자의 것 | 앵커 표면 기준, 앱의 것 | 화면 기준, 앱의 것. 매 frame 실은 값이 이긴다 |
| 최소 크기 | `minimum_width`·`minimum_height` | 없다 | 없다 (몇 픽셀 띠도 된다) |
| 닫힘 계기 | 캡션 close·Alt+F4 | 바깥 누름·Esc 등 | 없다. 앱이 목록에서 뺄 때만 사라진다 |
| 키보드·IME | 표면마다 TSF 문서 | 앵커의 문서를 빌린다 | 표면마다 문서. 활성화된 동안만 키가 온다 |
| OS 끌어 놓기 | `accept_file_drop`을 따른다 | 받지 않는다 | `accept_file_drop`을 따른다 |
| 소유자 | 주 창 (독립 창은 없다) | 앵커 표면 | 없다. 주 창이 최소화·숨김이어도 보인다 |

## 만들기

```cpp
luil::docked_edge_placement placement {};
placement.edge = luil::screen_edge::bottom;     // 주 모니터의 아래 가장자리
placement.alignment = luil::edge_alignment::end; // 오른쪽 끝에 맞추고
placement.offset = 16.0f;                         // 끝에서 16만큼 띄운다
placement.length = 320.0f;
placement.thickness = 96.0f;

luil::ui_docked_surface widget {};
widget.id = u8"widget";
widget.title = u8"날씨";                  // 보조 기술과 작업 관리자가 읽는다
widget.placement = placement;
widget.tree = widget_tree;
widget.metrics = [](float width, float height, float scale) {
    return luil::app_message { widget_resized { width, height, scale } };
};
frame.docked_surfaces.push_back(std::move(widget));
frame.displays = [](const std::vector<luil::display_info>& displays) {
    return luil::app_message { displays_changed { displays } };
};
```

`id`는 popup·보조 창과 같은 이름 공간이다. popup의 앵커(`ui_popup::anchor`)와 초점 요청의 표면(`focus_request::surface`)에 쓴다.

## 자리

`placement`는 둘 중 하나다.

- **물리 사각형(`screen_rect`).** 가상 화면의 물리 픽셀이다. 앱이 모니터 목록(`ui_frame::displays`)을 받아 직접 계산한 자리다.
  빈 사각형이면 표면을 숨긴다. 배율은 그 사각형이 놓인 모니터의 것이고 `metrics`의 `scale`로 온다.
- **모니터 가장자리(`docked_edge_placement`).** 앱은 모니터 배치도 배율도 모른 채 "이 모니터의 이 가장자리, 이 끝에서 이만큼"만 말한다.
  길이·거리·두께는 **그 모니터 배율의 논리 픽셀**이다. UI thread가 물리 사각형으로 풀고, 모니터·작업 영역·배율이 바뀌면 같은 값으로 다시 푼다.

가장자리 자리의 규칙은 다음과 같다 ([`docked_placement_tests.cpp`](../tests/docked_placement_tests.cpp)).

- `monitor`가 비면 주 모니터다. 이름은 `display_info::id`다 (Win32는 `\\.\DISPLAY1` 같은 장치 이름).
- `work_area`가 참이면 작업 영역의 가장자리다 (작업 표시줄 안쪽). 거짓이면 모니터 전체의 가장자리라 작업 표시줄 위에 겹친다.
- `alignment`는 가장자리를 따라 어느 끝에 맞출지다. 가로 가장자리의 `start`는 왼쪽, 세로 가장자리의 `start`는 위쪽이다.
  `offset`은 맞춘 끝에서 띄우는 거리이고, `center`면 가운데에서 민다.
- `length`가 0 이하면 가장자리 전체다. 가장자리보다 길면 가장자리 길이로 줄인다.
- `inset`은 가장자리에서 안쪽으로 띄우는 거리다.
- 두께가 0 이하거나 그 모니터가 없으면(빠졌다) 표면을 **숨긴다.** 표면과 tree는 남는다. 앱은 `displays`로 그 사실을 받아
  다른 모니터를 고르고, 모니터가 돌아오면 같은 자리에 다시 보인다.

작업 영역으로 밀어 넣지 않는다. 보조 창은 처음 자리를 작업 영역 안으로 밀지만, 작업 표시줄 곁의 위젯은 작업 영역 밖에 설 수 있어야 한다.

자리는 바뀐 frame에서 `SetWindowPos(HWND_TOPMOST, …, SWP_NOACTIVATE)` 한 번으로 옮긴다. 같은 값이면 OS를 부르지 않는다.
옮길 때마다 맨 위를 다시 세운다. 작업 표시줄처럼 같은 맨 위 창이 앞으로 나서면 그 아래로 밀리기 때문이다.

도킹 표면이 소유한 popup을 일반 창 순서로 내리면 Windows는 도킹 표면도 함께 내린다. 이때 도킹 표면은
`WM_WINDOWPOSCHANGED`를 받아 자기 창에 복구 메시지를 게시한다. 메시지를 처리할 때도 표면이 보이고
`WS_EX_TOPMOST`가 꺼져 있으면, 위치·크기·활성 창을 유지하면서 맨 위 상태를 복구한다. 새 frame이 올 필요는 없다.

### 배율

배율 변화(`WM_DPICHANGED`)에서 OS가 제안하는 사각형은 쓰지 않는다. 그 사각형은 지금 창을 새 배율로 곱한 것이라, 앱이 정한 크기를 OS가
바꾸는 셈이 된다. 대신 배율만 갱신하고 `metrics`로 알린다. 가장자리 자리는 모니터 목록이 바뀐 것으로 보고 새 배율로 다시 푼다.

`metrics`의 크기는 보조 창과 같이 client 물리 픽셀이고 `scale`은 그 모니터의 배율이다. 앱은 그 값으로 tree를 다시 배치한다.
metrics가 오기 전의 frame에서는 크기를 모르므로, 빈 tree를 싣고 기다리는 것이 안전하다.

## 모니터 목록

`ui_frame::displays`는 알림 곳이 처음 실린 frame에서 한 번, 그 뒤로는 모니터 연결·해상도·작업 영역·배율이 바뀔 때마다 새 목록으로 부른다.
값은 `display_info`(id, 모니터 사각형, 작업 영역, 배율, 주 모니터 여부)의 목록이다.

UI thread는 `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`(작업 표시줄 이동·자동 숨김의 `SPI_SETWORKAREA` 포함), `WM_DPICHANGED`에서 목록을 다시 읽고,
지난 목록과 다를 때만 도킹 표면의 자리를 다시 풀고 앱에 알린다. 이 메시지들은 주 창과 도킹 표면이 모두 받는다. 주 창이 숨어 있거나
다른 모니터에 있어도 놓치지 않는다. 설정 변경은 자주 오지만, 목록을 견주어 거르므로 앱에 가는 메시지는 실제 변화 때만이다.

## 활성화와 키보드

도킹 표면은 눌러도 활성화되지 않는다(`WS_EX_NOACTIVATE`, `WM_MOUSEACTIVATE` → `MA_NOACTIVATE`). 다른 앱이 쥔 앞 창과 키보드를 빼앗지 않는다.
누름·휠·hover·커서·보조 기술은 그대로 온다.

키보드가 필요하면(글 칸이 있는 위젯) 앱이 잠깐 활성화를 청한다. 새 API가 아니라 지연 초점 요청이다.

```cpp
frame.focus = luil::focus_request { .id = ++revision, .surface = u8"widget", .target = search_box, .activate = true };
```

- 활성화는 `SetForegroundWindow`다. 사용자가 방금 그 표면을 눌렀다면 마지막 입력을 받은 프로세스라 OS가 허락한다. 아니면 OS가 막는다
  (`focus_request`의 원래 규칙과 같다).
- 사용자가 다른 곳을 누르면 다시 비활성이 된다. 그 순간 `activation_changed`로 popup의 닫힘 계기를 낸다.
- 활성이 **될** 때는 닫힘 계기를 내지 않는다. 메뉴를 열며 활성화를 함께 청한 앱은 메뉴가 서자마자 닫히면 안 된다.
- 비활성인 동안 이 표면에 앵커한 popup은 다른 앱을 눌러도 닫히지 않는다. 활성 전환이 없기 때문이다. 메뉴를 여는 앱은 같은 frame에서
  잠깐 활성화를 함께 청한다 (알림 영역 메뉴가 `SetForegroundWindow`를 먼저 부르는 것과 같은 까닭이다).
- Alt+F4(`WM_CLOSE`)는 무시한다. 표면을 없애는 것은 앱이 목록에서 빼는 것뿐이다.

## popup과의 관계

도킹 표면은 popup의 앵커가 될 수 있다. popup의 자리·소유자·배율이 그 표면을 따른다.
표면이 옮겨지면 붙은 popup은 따라 옮겨진다. 다만 보조 창과 달리 **이동·크기 변경을 닫힘 계기로 내지 않는다.**
도킹 표면의 이동은 사용자의 손이 아니라 앱이 frame에 실은 결정이다. 계기로 내면 위젯을 늘리며 메뉴를 연 앱의 메뉴가 그 자리에서 닫히고,
다른 창의 popup까지 함께 닫힌다. 닫을지는 앱이 같은 frame에서 정한다.

## 주 창과의 관계

- 소유자가 없다. 주 창이 최소화돼도, `main_hidden`으로 숨어도 보인다. 주 창을 숨기고 알림 영역 아이콘과 도킹 표면만으로 도는 위젯 앱이 된다.
- 시간 예고(caret 깜빡임 등)의 사슬은 도킹 표면이 보이는 동안에도 잇는다.
- 앱이 끝나면(주 창 파괴) 함께 거둔다.

## 앱이 HWND를 서브클래싱하지 않는 까닭

같은 동작을 주 창 HWND에 서브클래스로 덧대면 luil 내부와 부딪힌다.

- luil은 같은 창에 1~4번 timer를 건다(화면 갱신·초점 요청 기한·커서 숨김·레이어드 배치). 앱의 timer 번호가 겹치면 서로 덮어쓴다.
- 저장 배치·최소 크기·custom caption 처리와 다투지 않으려면 `WM_WINDOWPOSCHANGING`·`WM_GETMINMAXINFO`·`WM_NCCALCSIZE`·`WM_NCHITTEST`를 덮어써야 한다.

도킹 표면은 처음부터 그런 처리가 없는 별도 창 클래스다. timer도 표면 공통의 것(커서 숨김)뿐이다.

## 플랫폼

- Windows: 위와 같다.
- Android: 지원하지 않는다. 목록을 받고 무시하며 한 번 경고를 남긴다. `displays`도 부르지 않는다.

## 검증

- [`tests/docked_placement_tests.cpp`](../tests/docked_placement_tests.cpp)는 가장자리 자리의 풀이를 확인한다. 끝 맞춤과 띄움, 모니터 배율,
  작업 영역과 모니터 전체, 왼쪽에 놓인 모니터의 음수 좌표, 가운데 맞춤과 안쪽 띄움, 가장자리보다 긴 길이, 빠진 모니터와 두께 0을 다룬다.
- [`tests/docked_surface_tests.cpp`](../tests/docked_surface_tests.cpp)는 실제 창 클래스로 다음을 확인한다. 스타일(캡션·작업 표시줄 단추 없음,
  맨 위·비활성, 소유자 없음), `MA_NOACTIVATE`, Alt+F4 무시, 3px 띠까지 앱이 준 자리, 숨김, 배율 변화에서 제안 사각형을 쓰지 않는 것,
  화면 변경 알림, 활성 전환의 닫힘 계기, 제목·tree 갱신, 연결된 모니터 목록이다.
- 데모 창 페이지의 "도킹 표면" 구역을 격리 데스크톱에서 UI Automation으로 쟀다(2026-11-14). 150% 모니터에서 위젯이 작업 영역 오른쪽 아래에
  480x144(논리 320x96), 끝에서 24px(논리 16) 자리에 섰다. 3px 띠는 5px, 오른쪽 가장자리로 바꾸면 144x480으로 옮겨졌다.
  맨 위·비활성·작업 표시줄 단추 없음·소유자 없음을 창 스타일로 되읽었다. Direct3D(기본)와 CPU 렌더러(`--renderer=cpu`)가 같았다.
  표면 안의 글 두 줄도 UIA tree에 섰다.
- 사람 손으로 남은 확인이 있다. 격리 데스크톱에는 앞 창이 없고(기존 도구 창의 초점 요청도 같다) 진짜 끌기는 사람의 마우스가 해야 한다.
  - 표면이 실제로 그려지는가 (격리 데스크톱에서는 tree와 창 사각형만 읽었다).
  - 표면을 누른 뒤 "도킹 표면 활성화"로 앞 창이 되는가.
  - 작업 표시줄을 누른 뒤에도 표면이 위에 남는가.
  - 누르는 동안과 끌어 놓는 동안 앞 창이 바뀌지 않는가.
  - 브라우저에서 그림을 끌어 오면 끄는 그림이 표면 위에서도 보이고, 놓으면 가상 파일 이름과 URL이 표면에 찍히는가.
  - 도킹 표면에 앵커한 popup은 아직 띄워 본 적이 없다. 위 "popup과의 관계"는 앵커 해석을 보조 창과 같은 길에 이은 것에 기댄 설명이다.

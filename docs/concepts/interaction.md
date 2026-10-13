# 상호작용

input 스레드의 [`interaction_controller`](../../include/luil/ui/ui_interaction.h)는 raw 이벤트를 hover·press·click·drag·초점·메뉴 탐색으로 정규화한다. element kind의 텍스트 대상과 앱별 메시지는 `interaction_policy`가 결정한다. policy의 기본 메서드는 앱 액션을 만들지 않는다. null policy에서도 element의 클릭·초점·값 조절 같은 controller의 기본 경로는 동작하지만 앱의 텍스트 대상 판정과 휠·키 라우팅은 제공되지 않는다.

## 포인터와 표면

클릭은 같은 대상·표면에서 누르고 떼는 동작이다. 비활성 element는 누름부터 제외한다. double-click은 이전 클릭의 대상·표면·시간·거리와 해당 액션을 확인한다. 텍스트 칸의 연속 클릭은 두 번이면 낱말, 세 번 이상이면 전체 선택으로 연결된다.

일반 drag source는 `interaction_config::drag_start_distance`를 넘어서 움직여야 drag가 시작된다. `pointer_drag_target`은 별도로 누르는 순간 `on_press`를 실행하며 포인터가 element 밖에 나가도 `on_move`를 받는다. `start_distance`를 두면 손잡이가 클릭과 함께 산다. 마우스·펜은 누른 자리에서 그 거리를 넘기 전에 떼면 보통 클릭·더블 클릭이고, 넘는 순간 누른 자리의 `on_press`와 넘은 이동의 `on_move`를 잇달아 받는다. 끌기가 선 뒤에는 시작 자리로 돌아와 떼도 클릭이 아니다. 터치는 탭·스크롤·길게 누르기가 먼저이고 스크롤이 될 수 없게 된 뒤에 끌기가 선다 ([터치 제스처와 펜 입력](../touch-pen-input-design.md)). 텍스트 선택 drag와 OS 파일 drag도 별도 경로다. OS 파일 drop의 수락과 실행은 UI가 tree에 동기적으로 묻고 input controller는 target 강조를 게시한다.

포인터 이벤트는 장치(`pointer_device`)를 싣는다. 마우스와 펜은 위 경로를 그대로 쓰고, 펜의 배럴 버튼 접촉은 우클릭이다. 끌어 스크롤하기와 길게 누르기는 터치에만 붙는다. 터치는 한 접촉이 한 조작을 하며, 누른 뒤 시간 창 안의 빠른 쓸기는 그 축으로 흐르는 가장 안쪽 컨테이너(`interaction_policy::pan_target_at`, 기본은 `route_pan`)를 흘리고, 오래 잡았다 옮기면 일반 drag가 된다. 움직이지 않고 오래 눌렀다 떼면 우클릭 액션을 실행한다. 터치 탭의 초점과 caret은 탭이 확정된 뗌에서 옮긴다. 정상적인 뗌 없이 끝난 터치·펜 접촉은 `pointer_cancelled_event`로 액션 없이 거둔다. `interaction_policy::on_press`는 활성 element를 누른 순간의 관찰 hook이다. 세부 계약과 런타임 설정은 [터치 제스처와 펜 입력](../touch-pen-input-design.md)에 있다.

포인터 동작은 그 순간에 눌려 있던 수식 키(`ui_action_context`의 `control`·`shift`·`alt`)를 액션에 싣는다. 앱은 이 값으로 Ctrl+클릭(하나 더 고르기), Shift+클릭(범위 고르기), 놓는 순간의 Ctrl(복사·이동)을 가른다. 값을 읽는 순간은 콜백마다 다음과 같다.

| 콜백 | 수식 키의 순간 |
| --- | --- |
| `left_click`·`double_click`·`right_click` | 뗀 순간 (`pointer_released_event`) |
| 터치 길게 누르기의 `right_click` | 시간이 찬 순간까지의 마지막 이벤트 |
| `pointer_drag_target::on_press` | 누른 순간 |
| `pointer_drag_target::on_move` | 그 이동의 순간 (앞 문맥은 앞 순간) |
| `pointer_drag_target::on_release` | 뗀 순간, 취소면 마지막 이벤트 |
| `drag_source::make_payload` | 끌기가 시작되는 이동의 순간 (자리는 누른 자리) |
| `drop_target::on_drop` | 놓는 순간 |
| Space·Enter의 실행 | 그 키의 수식 키 (Ctrl·Alt는 앱 단축키라 실제로는 Shift뿐) |

플랫폼은 누름·이동·뗌 이벤트에 수식 키를 싣는다. Win32 마우스는 wparam의 `MK_CONTROL`·`MK_SHIFT`와 `GetKeyState(VK_MENU)`, 터치·펜은 `POINTER_MOD_CTRL`·`POINTER_MOD_SHIFT`와 `GetKeyState(VK_MENU)`, Android는 하드웨어 키보드의 meta state를 쓰고 키보드가 없으면 모두 거짓이다. 텍스트 칸의 Shift+클릭 선택 확장은 지금처럼 누른 순간의 `shift`로 정한다. OS 파일 drop의 `on_drop`도 놓는 순간의 키 상태를 받지만 OLE에 답하는 효과는 그대로 복사다.

포인터는 이벤트의 surface id로 tree를 선택한다. key는 논리 초점이 있으면 `focused_surface`를 사용하고, 없으면 키 이벤트가 온 표면을 Tab·기본 버튼·modal dismiss의 시작점으로 사용한다. popup은 OS 초점을 받지 않으므로 anchor 창이 전달한 키가 popup의 논리 초점으로 갈 수 있다.

## 키 라우팅

[`process_key()`](../../src/ui/ui_interaction.cpp)는 열린 메뉴를 먼저 처리한다. 메뉴의 위·아래·Enter·Esc를 처리하고, 나머지는 초점 텍스트 입력에 기회를 준 뒤 소비한다. 열린 메뉴 뒤의 앱 `on_key()`까지는 보내지 않는다.

메뉴가 없을 때의 우선순위는 다음과 같다. 각 단계가 `nullopt`를 반환하면 다음으로 진행하고, 빈 액션 목록은 처리한 키라는 뜻이다.

1. 초점 컨트롤의 Space·Enter 실행
2. Tab·Shift+Tab 초점 이동
3. 초점 텍스트 입력의 편집 키
4. 초점 값 컨트롤의 화살표·Page·Home·End
5. focus group 내부 탐색
6. 진행 중인 drag의 Esc 취소
7. focus trap의 Esc dismiss
8. 기본 버튼의 Enter 실행
9. 앱 정책의 `on_key()`

텍스트 초점에서 Space 키 이벤트는 소비하고 실제 글자는 문자 이벤트로 받는다. Enter는 기본 버튼 경로까지 흐를 수 있다. Tab은 초점 element가 `takes_tab`을 선언하지 않았고 이동할 자리가 있을 때 이동으로 소비한다. 그렇지 않으면 뒤의 라우팅이 이어진다. Shift가 아닌 수정자를 동반한 Enter는 기본 버튼 실행 대상에서 제외한다.

`key_pressed_event`는 물리 수정키(`control`, `alt`, `shift`, `meta`)와 편집 역할(`primary_shortcut`, `word_navigation`)을 구분한다. 역할을 생략하면 기존 Windows 규칙처럼 둘 다 Control을 따른다. Shift를 뺀 수정자가 하나라도 있으면(`shortcut_modifier_down`) 실행 키·기본 버튼·단계 키·묶음 키가 가로채지 않고 앱 단축키로 흐른다. 물리 키로 알리든 역할로 알리든 같은 판정이다. 다른 플랫폼 입력 뒷단은 역할을 명시할 수 있다. 예를 들어 macOS에서는 Command를 복사·붙여넣기의 주 단축키로, Option을 낱말 이동 수정자로 보낸다. 영문자와 숫자는 `key_code::key_*` 이름으로 나르며, `platform_key_code`는 기존 Win32 호출의 호환 경로이자 나머지 플랫폼 고유 키의 대역이다.

## 초점 상태와 tree 교체

`focused`는 키보드 초점 id, `focused_input`은 그 초점이 텍스트 대상일 때 같은 id다. `focus_visible`은 키보드 탐색의 초점 테를 켜고, `menu_highlight`는 일반 초점과 독립인 메뉴 강조다. 이 값들은 각자의 surface 표식과 함께 게시한다.

Tab의 기본 순서는 보이는 tree의 그리기 순서이며 `order_focus()`로 조정한다. `tab_stop`이 false인 element는 순회에서 제외한다. focus group은 Tab에서 한 자리로 접히고 내부 화살표는 초점만 이동한다. 텍스트 caret과 값 조절이 group보다 먼저 키를 처리한다. group의 문자 탐색은 이벤트 시각과 `typeahead_reset_time`으로 연속 질의를 묶는다.

`set_focus_trap()`은 같은 표면의 초점을 trap 안으로 제한하고, 포인터 차단은 scrim의 `hit_opaque`가 맡는다. `focus_entry`를 지정하면 trap 진입 시 초점을 세울 수 있고, 유효하지 않은 entry는 trap의 첫 초점 자리로 물러선다. `focus_return`은 trap이 사라졌을 때 되돌릴 id다. tree 갱신에 따른 진입과 복귀는 활성 표면을 기준으로 판정한다.

상태 기계는 이벤트 timestamp를 사용한다. 새 tree를 받으면 마지막 포인터 좌표로 hover를 재판정하며, 표면이 사라지면 그곳에서 시작한 press·drag를 거둔다. input 큐의 sequence가 건너뛰면 유실된 뗌 이벤트에 대비해 `cancel_dropped_gestures()`로 진행 중인 몸짓과 터치 접촉을 취소한다. 끝을 듣는 손잡이(`pointer_drag_target::on_release`)와 넘겨받은 컨테이너에는 끝을 알리고, 그 액션은 pump가 앱에 보낸다.

drag & drop과 `auto_scroll`을 켠 끌기 손잡이는 흘리는 창의 가장자리에서 그 창을 흘린다. 흐름은 터치 길게 누르기·관성과 같이 `next_deadline`·`advance`로 시간을 받고, 휠과 같은 `scroll_source::scroll` 메시지로 나간다. 계약은 [끄는 동안의 가장자리 자동 스크롤](../drag-auto-scroll-design.md)에 있다.

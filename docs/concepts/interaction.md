# 상호작용

input 스레드의 [`interaction_controller`](../../include/luil/ui/ui_interaction.h)는 raw 이벤트를 hover·press·click·drag·초점·메뉴 탐색으로 정규화한다. element kind의 텍스트 대상과 앱별 메시지는 `interaction_policy`가 결정한다. policy의 기본 메서드는 앱 액션을 만들지 않는다. null policy에서도 element의 클릭·초점·값 조절 같은 controller의 기본 경로는 동작하지만 앱의 텍스트 대상 판정과 휠·키 라우팅은 제공되지 않는다.

## 포인터와 표면

클릭은 같은 대상·표면에서 누르고 떼는 동작이다. 비활성 element는 누름부터 제외한다. double-click은 이전 클릭의 대상·표면·시간·거리와 해당 액션을 확인한다. 텍스트 칸의 연속 클릭은 두 번이면 낱말, 세 번 이상이면 전체 선택으로 연결된다.

일반 drag source는 `interaction_config::drag_start_distance`를 넘어서 움직여야 drag가 시작된다. `pointer_drag_target`은 별도로 누르는 순간 `on_press`를 실행하며 포인터가 element 밖에 나가도 `on_move`를 받는다. 텍스트 선택 drag와 OS 파일 drag도 별도 경로다. OS 파일 drop의 수락과 실행은 UI가 tree에 동기적으로 묻고 input controller는 target 강조를 게시한다.

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

텍스트 초점에서 Space 키 이벤트는 소비하고 실제 글자는 문자 이벤트로 받는다. Enter는 기본 버튼 경로까지 흐를 수 있다. Tab은 초점 element가 `takes_tab`을 선언하지 않았고 이동할 자리가 있을 때 이동으로 소비한다. 그렇지 않으면 뒤의 라우팅이 이어진다. Ctrl·Alt를 동반한 Enter는 기본 버튼 실행 대상에서 제외한다.

## 초점 상태와 tree 교체

`focused`는 키보드 초점 id, `focused_input`은 그 초점이 텍스트 대상일 때 같은 id다. `focus_visible`은 키보드 탐색의 초점 테를 켜고, `menu_highlight`는 일반 초점과 독립인 메뉴 강조다. 이 값들은 각자의 surface 표식과 함께 게시한다.

Tab의 기본 순서는 보이는 tree의 그리기 순서이며 `order_focus()`로 조정한다. `tab_stop`이 false인 element는 순회에서 제외한다. focus group은 Tab에서 한 자리로 접히고 내부 화살표는 초점만 이동한다. 텍스트 caret과 값 조절이 group보다 먼저 키를 처리한다. group의 문자 탐색은 이벤트 시각과 `typeahead_reset_time`으로 연속 질의를 묶는다.

`set_focus_trap()`은 같은 표면의 초점을 trap 안으로 제한하고, 포인터 차단은 scrim의 `hit_opaque`가 맡는다. `focus_entry`를 지정하면 trap 진입 시 초점을 세울 수 있고, 유효하지 않은 entry는 trap의 첫 초점 자리로 물러선다. `focus_return`은 trap이 사라졌을 때 되돌릴 id다. tree 갱신에 따른 진입과 복귀는 활성 표면을 기준으로 판정한다.

상태 기계는 이벤트 timestamp를 사용한다. 새 tree를 받으면 마지막 포인터 좌표로 hover를 재판정하며, 표면이 사라지면 그곳에서 시작한 press·drag를 거둔다. input 큐의 sequence가 건너뛰면 유실된 뗌 이벤트에 대비해 `cancel_dropped_gestures()`로 진행 중인 몸짓을 취소한다.

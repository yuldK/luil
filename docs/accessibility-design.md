# UI Automation 접근성

Luil은 각 Win32 surface를 UI Automation fragment로 공개한다. Element는 `access_info`로 플랫폼 중립적인 의미를 설명하고, Win32 provider가 이를 UIA control type, property, pattern, navigation, hit testing, event로 변환한다.

공개 접근성 모델은 [`include/luil/ui/accessibility.h`](../include/luil/ui/accessibility.h)에 선언되어 있다. Windows provider는 [`src/win32/uia_provider.h`](../src/win32/uia_provider.h)와 [`src/win32/uia_provider.cpp`](../src/win32/uia_provider.cpp)에 구현되어 있다. 실행 가능한 pattern은 [접근성 동작](accessibility-action-design.md)에서 설명한다.

## Element 설명

모든 `ui_element`는 `accessibility()`를 구현하고 `access_info`를 반환한다.

- `role`: element의 의미
- `name`: 접근 가능한 label
- `checked`, `selected`, `expanded`: 선택적 상태
- `range`: 숫자 최솟값, 최댓값, 현재 값
- `value`: 현재 text value

선택적 상태가 없으면 해당 개념과 UIA pattern이 적용되지 않는다. `false`는 상태가 존재하고 현재 값이 false라는 뜻이다. Enabled와 focusable 상태는 `access_info`에 중복 저장하지 않는다. Provider가 `ui_element::enabled()`와 `focusable()`을 직접 읽어 일반 입력과 접근성 응답을 일치시킨다.

Application element는 가장 가까운 semantic role과 실제로 유지할 수 있는 상태만 반환해야 한다. 설명 없는 image와 구조 전용 layout panel은 일반적으로 `access_role::none`을 반환한다.

## Role과 UIA control type

| Luil role | UIA control type |
| --- | --- |
| `button` | Button |
| `link` | Hyperlink |
| `check_box`, `radio_button` | CheckBox, RadioButton |
| `toggle_switch` | Toggle 의미를 가진 Button |
| `slider`, `scroll_bar`, `progress` | Slider, ScrollBar, ProgressBar |
| `handle` | Thumb |
| `image`, `edit`, `combo_box` | Image, Edit, ComboBox |
| `list`, `list_item` | List, ListItem |
| `tab_list`, `tab` | Tab, TabItem |
| `radio_group`, `group` | Group |
| `menu`, `menu_item` | Menu, MenuItem |
| `title_bar`, `dialog` | TitleBar, Window |
| `alert`, `static_text` | Text |

UIA에는 toggle switch, radio group, alert 전용 control type이 없다. 이 구분은 상태와 pattern 동작으로 표현한다. `AutomationId`는 element id에서 `kind:owner` 형식으로 만든다. Automation client, focus, frame 간 identity가 이 id에 의존하므로 application은 안정적이고 고유한 element id를 사용해야 한다.

## 접근성 tree

접근성 tree는 arranged element tree를 다음 규칙으로 투영한다.

- invisible branch는 제외한다.
- role이 `none`인 element는 접고 accessible child를 상위로 올린다.
- accessible element는 publication 순서를 유지한다.
- parent는 가장 가까운 accessible ancestor이며, 없으면 surface fragment root다.

`access_children`, `access_parent`, `access_sibling`이 이 구조를 공개한다. `ui_tree`는 publish 시 index를 만들어 sibling과 parent navigation이 긴 목록을 매번 순회하지 않게 한다.

Selection container는 반드시 element의 직접 parent일 필요가 없다. `access_selection_container_of`는 바깥쪽으로 가장 가까운 accessible list, tab list, radio group을 찾는다. `access_selected_items`는 선택된 accessible child를 publication 순서로 반환한다. Main window, secondary window, popup은 각각 자신의 `HWND`와 surface-local tree에 연결된 fragment root를 가진다.

## Provider identity와 수명

Element provider는 특정 frame의 element pointer 대신 fragment root와 `ui_element_id`를 보관한다. 모든 query는 surface의 현재 tree에서 id를 다시 찾는다. 이 방식은 immutable frame 모델과 맞으며 tree가 교체되어도 provider를 안전하게 유지한다.

Id가 더 이상 없으면 동작 query는 `UIA_E_ELEMENTNOTAVAILABLE`을 반환한다. Surface detach는 root와 client가 보유한 모든 provider를 무효화한다.

Element id가 root에 계속 존재하는 동안 runtime id는 안정적이다. Root map이 커지면 현재 tree에서 사라진 key를 정리해 일시적인 id가 메모리를 계속 차지하지 않게 한다. 사라졌다가 다시 나타난 id는 새 runtime key를 받을 수 있다.

Provider는 `ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading`을 선언한다. UI Automation이 호출을 window STA로 marshal하므로 surface 상태는 UI thread가 소유할 수 있다. 따라서 접근성 runtime에는 `com_sta_scope`가 필요하다.

## Geometry와 hit testing

`BoundingRectangle`은 ancestor clipping이 반영된 `ui_tree::visible_bounds`에서 시작한다. Provider는 physical client-pixel rectangle을 screen coordinate로 변환한다. Arranged bounds가 이미 physical pixel이므로 display scale을 다시 적용하지 않는다.

`ElementProviderFromPoint`는 `access_element_at`을 사용한다. 이 검색은 일반 interaction hit testing과 달리 label이나 progress indicator 같은 읽기 전용 element도 반환할 수 있다. Modal scrim 같은 hit-opaque overlay를 만나면 검색을 중단해 그 아래 content를 해당 지점에 공개하지 않는다.

Clip되거나 hidden인 element의 rectangle은 비어 있다. Scroll viewport 밖의 content는 focus가 화면에 나타낼 수 있다면 keyboard와 automation에서 논리적으로 도달 가능할 수 있다.

## Focus

`HasKeyboardFocus`는 Win32 focus만이 아니라 surface의 Luil logical focus를 보고한다. No-activate popup과 embedded WebView의 유효한 focus 상태는 일반 child `HWND`만으로 표현되지 않기 때문이다.

`SetFocus`는 focusable이고 도달 가능한 element만 허용한다. COM callback 안에서 focus를 직접 바꾸지 않고 `access_focus_event`를 input pump에 post하며, input controller가 keyboard navigation과 같은 규칙을 적용한다. Logical focus가 바뀌면 surface는 새 accessible element에 `UIA_AutomationFocusChangedEventId`를 보낸다. Surface filter는 다른 window의 focus가 잘못된 fragment에 보고되는 것을 막는다.

## Property, text, change event

Provider는 Name, ControlType, AutomationId, IsEnabled, IsKeyboardFocusable, HasKeyboardFocus, IsControlElement, IsContentElement를 제공한다. 상태별 값은 해당 UIA pattern을 통해 제공한다.

Text-input document가 있는 editable text element는 Value와 Text pattern을 제공한다. Text range는 내부에서 UTF-8 byte offset을 쓰고 COM 경계에서 UTF-16으로 변환한다. Document와 selection range, text 읽기, character 또는 document 단위 endpoint 이동, point-to-range 조회, screen rectangle을 지원한다. 단일 행 편집 모델이므로 line, paragraph, page 단위는 document 전체로 축약된다.

Surface는 직전 published tree의 접근성 snapshot을 보관한다. `make_access_snapshot`은 accessible element를 preorder로 기록하고 `diff_access_snapshots`은 다음 변경을 만든다.

- semantic value가 바뀌면 property change
- 항목이 새로 선택되면 selected event 하나
- 구조가 바뀌면 가장 가까운 affected accessible parent의 structure change

Role 변경은 structure event를 통한 교체로 취급한다. Element 추가와 제거도 parent structure event가 된다. 단일 선택의 새 selected item이 이미 상태 전환을 표현하면 별도 deselection property event를 중복 발생시키지 않는다.

Win32 root는 이 변경을 UIA property, element-selected, structure event로 변환한다. Invoke는 snapshot으로 보이는 상태가 아닌 event이므로 해당 동작을 dispatch할 때 발생한다.

## Foreground와 자동화 실행

Automation client가 상태를 바꾸는 pattern을 호출하면 UIA 계층이 대상 window를 foreground로 세울 수 있다. Provider가 막을 수 있는 지점이 아니다. Automation 코드가 전혀 없는 표준 Win32 app에서도 같은 일이 일어나며 더 자주 일어난다. 읽기 전용 조회는 foreground를 바꾸지 않는다.

Library가 보장하는 것은 시작 시점이다. 주 window는 `SW_SHOWDEFAULT`로 표시하므로 표시 방식은 process를 시작한 쪽이 `STARTUPINFO.wShowWindow`로 정한다. Automation harness는 `SW_SHOWNOACTIVATE`나 `SW_SHOWMINNOACTIVE`로 실행해 사람의 focus를 건드리지 않고 app을 띄울 수 있고, 두 경우 모두 접근성 tree를 완전하게 제공한다. 이 계약에 의존하는 호출자가 있으므로 표시 명령을 고정 값으로 바꾸지 않는다.

접근성 tree는 published frame에서 만들어지고 published frame은 surface가 그릴 때 만들어진다. 따라서 **그린 적이 없는 window에는 tree가 없다.** `SW_HIDE`로 시작한 window는 element를 제공하지 않는다. 최소화로 시작한 window는 첫 표시에서 이미 그렸으므로 tree를 그대로 제공한다.

`CreateDesktop`으로 만든 별도 desktop에서는 그리기가 정상으로 돌고 tree도 완전하다. Direct3D와 CPU renderer 모두 동작한다. UIA client는 desktop 단위이므로 같은 desktop에 있어야 조회와 조작이 된다. 그 desktop은 입력 desktop이 아니라 foreground window가 서지 않으며, 사람이 쓰는 desktop의 foreground와 cursor는 영향을 받지 않는다.

Client가 surface window를 고를 때 `Process.MainWindowHandle`이나 "자식이 있는 window"에 기대면 안 된다. App process에는 시스템이 만든 보이는 top-level window(`UAC Input Indicator` 등)가 함께 있고 그것도 자식을 갖는다. Luil surface는 `AutomationId`가 `kind:owner` 꼴인 element를 가진 window다.

## 검증

[`tests/accessibility_tests.cpp`](../tests/accessibility_tests.cpp)는 role과 property mapping, collapsed-tree navigation, visibility, geometry, hit testing, provider 수명, runtime id, focus, selection container, snapshot과 event, 지원 pattern, action, editable value, text range를 검증한다. Platform test는 fake surface host와 event sink를 사용해 screen reader 없이 COM 변환을 반복 검증한다.

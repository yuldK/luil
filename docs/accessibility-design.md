# UI Automation 접근성

Luil은 각 Win32 surface를 UI Automation fragment로 공개한다. Element는 `access_info`로 플랫폼 중립적인 의미를 설명하고, Win32 provider가 이를 UIA control type, property, pattern, navigation, hit testing, event로 변환한다.

공개 접근성 모델은 [`include/luil/ui/accessibility.h`](../include/luil/ui/accessibility.h)에 선언되어 있다. Windows provider는 [`src/win32/uia_provider.h`](../src/win32/uia_provider.h)와 [`src/win32/uia_provider.cpp`](../src/win32/uia_provider.cpp)에 구현되어 있다. 실행 가능한 pattern은 [접근성 동작](accessibility-action-design.md)에서 설명한다.

## Element 설명

모든 `ui_element`는 `accessibility()`를 구현하고 `access_info`를 반환한다.

- `role`: element의 의미
- `name`: 접근 가능한 label
- `checked`, `selected`, `expanded`: 선택적 상태
- `mixed`: 일부만 켜진 체크다. 참이면 `checked`의 값과 무관하게 UIA ToggleState_Indeterminate로 읽힌다 (삼상 체크박스, `check_config::mixed`)
- `range`: 숫자 최솟값, 최댓값, 현재 값
- `value`: 현재 text value

선택적 상태가 없으면 해당 개념과 UIA pattern이 적용되지 않는다. `false`는 상태가 존재하고 현재 값이 false라는 뜻이다. Enabled와 focusable 상태는 `access_info`에 중복 저장하지 않는다. Provider가 `ui_element::enabled()`와 `focusable()`을 직접 읽어 일반 입력과 접근성 응답을 일치시킨다.

Application element는 가장 가까운 semantic role과 실제로 유지할 수 있는 상태만 반환해야 한다. 설명 없는 image와 구조 전용 layout panel은 일반적으로 `access_role::none`을 반환한다.

**이름의 원천은 한 술어다.** 자기 글을 그리는 element(button, label, list row)는 그 글이 곧 이름이다. 글을 세우지 않는 container(list, tab bar, scroll bar)는 `ui_element::access_name()`이 답한다 — `set_access_name` → `search_label` → tooltip 순서다. Tooltip을 이름으로 쓰려면 화면에 글 상자가 떠야 하므로, 화면에 뜨지 않는 이름의 자리를 따로 둔다. Container가 자기 안에 세우는 부품(list와 scroll area의 막대)은 application이 손댈 수 없으므로 container의 이름을 물려받는다.

Modal dialog(`modal_host_config::name`)와 선택 묶음(`choice_group_config::name`)의 이름도 application이 준다. 화면 읽기는 dialog가 뜨는 순간 그 이름을 말하고, 묶음의 이름이 "무엇을 고르는 중인가"를 답한다 — 항목의 "선택됨"만으로는 반쪽이다. Library가 지어낼 수 있는 말이 아니라 비면 이름 없이 선다.

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
| `tree`, `tree_item` | Tree, TreeItem |
| `table`, `data_item`, `header_item` | DataGrid, DataItem, HeaderItem |
| `tool_bar`, `separator`, `status_bar` | ToolBar, Separator, StatusBar |

선택 묶음(`choice_group_element`)은 그리는 style과 무관하게 `radio_group`이다. Radio 행이든 toggle 버튼이든 하는 일은 여럿 가운데 하나를 고르는 것이라, 항목은 `radio_button`으로 서고 묶음이 그 선택 container가 된다. Toggle 항목은 글자 버튼으로 그려지지만 `text_button_config::selected`로 자기 선택을 함께 싣는다 — 강조 채움은 "Enter가 여기로 간다"에도 쓰이는 그림이라 provider가 그림만 보고는 둘을 가를 수 없다. 상태를 실으면 실행 패턴도 함께 갈린다: 상태를 든 것은 그 상태의 패턴으로 실행하므로([접근성 동작](accessibility-action-design.md)의 `invokable`) toggle 항목의 자리는 Invoke가 아니라 Select다.

`tree`와 `table`은 `list`처럼 항목의 선택 container다(`access_selection_container`). 표는 행을 고르는 컨트롤이라 읽기만 하는 정적 Table이 아니라 DataGrid로 선다. Grid·Table 패턴(행·열 위치)은 표 컴포넌트가 들어올 때 함께 붙는다.

UIA에는 toggle switch, radio group, alert 전용 control type이 없다. 이 구분은 상태와 pattern 동작으로 표현한다. `AutomationId`는 element id에서 `kind:owner` 형식으로 만든다. Automation client, focus, frame 간 identity가 이 id에 의존하므로 application은 안정적이고 고유한 element id를 사용해야 한다.

## 접근성 tree

접근성 tree는 arranged element tree를 다음 규칙으로 투영한다.

- invisible branch는 제외한다.
- role이 `none`인 element는 접고 accessible child를 상위로 올린다.
- 같은 element id가 한 tree에 둘 이상 있으면 `ui_tree::find`가 답하는 첫 등록만 sibling 줄에 선다. 나머지는 role이 `none`인 것과 같게 접고 그 accessible child를 상위로 올린다.
- accessible element는 publication 순서를 유지한다.
- parent는 가장 가까운 accessible ancestor이며, 없으면 surface fragment root다.

`access_children`, `access_parent`, `access_sibling`이 이 구조를 공개한다. `ui_tree`는 publish 시 index를 만들어 sibling과 parent navigation이 긴 목록을 매번 순회하지 않게 한다.

중복 id를 접는 것은 진단이 아니라 navigation의 종료 조건이다. Sibling 줄에서의 자리를 id로 되묻으므로 같은 id가 둘 서면 뒤의 element가 앞의 element의 자리를 답하고, `NextSibling`이 이미 지난 자리로 돌아와 client의 child 열거가 끝나지 않는다. Runtime id도 element id에서 나오므로 한 fragment root 안에 같은 runtime id가 둘 생긴다. Element provider의 `FirstChild`와 `LastChild`는 element를 들고 자식을 다시 접지 않고 이 index 줄을 읽는다. 네 방향이 한 줄을 읽어야 앞에서 훑은 답과 뒤에서 훑은 답이 같다.

중복 id 자체는 여전히 application의 결함이다. `ui_tree::duplicate_ids`가 그대로 보고하며, 접기는 화면이 멈추는 대신 뒤쪽 element가 접근성 tree에서 자기 자리를 잃는 형태로 저하시킨다.

Selection container는 반드시 element의 직접 parent일 필요가 없다. `access_selection_container_of`는 바깥쪽으로 가장 가까운 accessible list, tab list, radio group을 찾는다. `access_selected_items`는 선택된 accessible child를 publication 순서로 반환한다.

창에 걸치는 행만 짓는 container(가상 목록과 그 위의 표 본문)는 tree 밖의 항목을 model로 답한다 (`access_item_model`, `ui_element::access_items`). `access_selected_ids`는 그런 container면 model의 선택 전체를, 아니면 `access_selected_items`의 id를 반환한다. `access_item_container_of`는 지어진 행에서 그 model container를 찾는다. 자세한 규칙은 [virtual-list-design.md](virtual-list-design.md#모델로-답하는-접근성)에 있다. Main window, secondary window, popup은 각각 자신의 `HWND`와 surface-local tree에 연결된 fragment root를 가진다.

## Provider identity와 수명

Element provider는 특정 frame의 element pointer 대신 fragment root와 `ui_element_id`를 보관한다. 모든 query는 surface의 현재 tree에서 id를 다시 찾는다. 이 방식은 immutable frame 모델과 맞으며 tree가 교체되어도 provider를 안전하게 유지한다.

Id가 더 이상 없으면 동작 query는 `UIA_E_ELEMENTNOTAVAILABLE`을 반환한다. Surface detach는 root와 client가 보유한 모든 provider를 무효화한다.

Model container의 항목 provider는 그 container의 id를 함께 보관한다. 현재 tree에 항목 id가 없으면 container의 model이 따로 지은 행(`access_item_probe`)으로 Name, ControlType, 선택·펼침 상태, 실행에 답하고, `IsOffscreen`은 참, rectangle은 비어 있으며, navigation은 parent(container)만 있다. 항목이 지어지면 같은 provider가 tree의 행으로 답한다. 항목 id가 행의 element id와 같기 때문이다.

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

Provider는 Name, ControlType, AutomationId, IsEnabled, IsKeyboardFocusable, HasKeyboardFocus, IsControlElement, IsContentElement를 제공한다. Model container의 항목은 PositionInSet, SizeOfSet을, 계층 항목은 Level도 제공한다. 상태별 값은 해당 UIA pattern을 통해 제공한다.

Model container는 ItemContainer pattern을 제공한다. `FindItemByProperty`는 property 없음(다음 항목), Name, SelectionItemIsSelected, AutomationId로 model 전체를 찾고, 그 밖의 property는 `E_INVALIDARG`로 거절한다. 그 항목은 VirtualizedItem pattern을 제공하며 `Realize`는 그 행을 창 안으로 흘리는 scroll 동작을 dispatch한다. Container의 `GetSelection`은 model의 선택 전체를 반환한다. Grid·Table pattern(행·열 좌표와 열 머리글 연결)은 아직 없다.

Text-input document가 있는 editable text element는 Value와 Text pattern을 제공한다. Text range는 내부에서 UTF-8 byte offset을 쓰고 COM 경계에서 UTF-16으로 변환한다. Document와 selection range, text 읽기, character·line·paragraph·document 단위의 넓히기와 이동, point-to-range 조회, screen rectangle을 지원한다. Line은 요소가 내주는 화면 줄이다(`ui_element::text_line_starts`) — 여러 줄 칸은 넘겨 접은 줄도 한 줄로 내주고, 한 줄 칸은 답하지 않아 문서 하나가 한 줄이다. Paragraph는 줄 바꿈 뒤에서 시작한다. 지원하지 않는 단위는 다음 큰 단위로 물러선다: format·word는 line으로, page는 document로 간다. `Move`는 품은 단위의 처음에서 옮기고 그 단위로 넓힌다. 뒤로 옮길 때 단위 가운데에 있었으면 그 처음까지가 한 걸음이다.

Surface는 직전 published tree의 접근성 snapshot을 보관한다. `make_access_snapshot`은 accessible element를 preorder로 기록하고 `diff_access_snapshots`은 다음 변경을 만든다.

- semantic value가 바뀌면 property change
- 항목이 새로 선택되면 selected event 하나
- 다중 선택 container의 항목은 container마다 모아 added-to-selection·removed-from-selection event. 바뀐 끝에 하나만 골라졌으면 그 항목의 selected event 하나, 한꺼번에 `access_selection_burst`(20)보다 많이 바뀌었으면 container의 selection-invalidated event 하나. 가상 목록은 창 밖의 고른 행도 상한(`virtual_list_offscreen_selection_limit`)까지 지어 두므로 흘려도 이 대조가 그 행들을 본다. 선택 읽기는 상한 없이 model이 답한다 ([virtual-list-design.md](virtual-list-design.md))
- 구조가 바뀌면 가장 가까운 affected accessible parent의 structure change

Role 변경은 structure event를 통한 교체로 취급한다. Element 추가와 제거도 parent structure event가 된다. 단일 선택의 새 selected item이 이미 상태 전환을 표현하면 별도 deselection property event를 중복 발생시키지 않는다. 두 발행본에 모두 있는 항목만 선택 event로 알린다 — 새로 지어진 가상 목록 행은 structure가 말한다.

Win32 root는 이 변경을 UIA property, element-selected, `ElementAddedToSelection`·`ElementRemovedFromSelection`·`Selection_Invalidated`, structure event로 변환한다. Invoke는 snapshot으로 보이는 상태가 아닌 event이므로 해당 동작을 dispatch할 때 발생한다.

## Foreground와 자동화 실행

Automation client가 상태를 바꾸는 pattern을 호출하면 UIA 계층이 대상 window를 foreground로 세울 수 있다. Provider가 막을 수 있는 지점이 아니다. Automation 코드가 전혀 없는 표준 Win32 app에서도 같은 일이 일어나며 더 자주 일어난다. 읽기 전용 조회는 foreground를 바꾸지 않는다.

Library가 보장하는 것은 시작 시점이다. 주 window는 초기 배치가 없으면 `SW_SHOWDEFAULT`로 표시하므로 표시 방식은 process를 시작한 쪽이 `STARTUPINFO.wShowWindow`로 정한다. `window_config::initial_placement`가 있으면 저장 상태를 첫 표시부터 유지하면서 `STARTUPINFO`의 비활성·최소화·숨김 정책도 적용한다. Automation harness는 `SW_SHOWNOACTIVATE`나 `SW_SHOWMINNOACTIVE`로 실행해 사람의 focus를 건드리지 않고 app을 띄울 수 있고, 두 경우 모두 접근성 tree를 완전하게 제공한다. 이 계약에 의존하는 호출자가 있으므로 표시 명령을 고정 값으로 바꾸지 않는다.

접근성 tree는 published frame에서 만들어지고 published frame은 surface가 그릴 때 만들어진다. 따라서 **그린 적이 없는 window에는 tree가 없다.** `SW_HIDE`로 시작한 window는 element를 제공하지 않는다. 최소화로 시작한 window는 첫 표시에서 이미 그렸으므로 tree를 그대로 제공한다.

`CreateDesktop`으로 만든 별도 desktop에서는 그리기가 정상으로 돌고 tree도 완전하다. Direct3D와 CPU renderer 모두 동작한다. UIA client는 desktop 단위이므로 같은 desktop에 있어야 조회와 조작이 된다. 그 desktop은 입력 desktop이 아니라 foreground window가 서지 않으며, 사람이 쓰는 desktop의 foreground와 cursor는 영향을 받지 않는다.

Client가 surface window를 고를 때 `Process.MainWindowHandle`이나 "자식이 있는 window"에 기대면 안 된다. App process에는 시스템이 만든 보이는 top-level window(`UAC Input Indicator` 등)가 함께 있고 그것도 자식을 갖는다. Luil surface는 `AutomationId`가 `kind:owner` 꼴인 element를 가진 window다.

## 검증

[`tests/accessibility_tests.cpp`](../tests/accessibility_tests.cpp)는 role과 property mapping, collapsed-tree navigation, visibility, geometry, hit testing, provider 수명, runtime id, focus, selection container, snapshot과 event, 지원 pattern, action, editable value, text range를 검증한다. Platform test는 fake surface host와 event sink를 사용해 screen reader 없이 COM 변환을 반복 검증한다.

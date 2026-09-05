# Modal dialog host

`modal_host_element`는 modal dialog에 필요한 scrim, 포인터 차단, focus trap, Esc action, 가운데 배치를 하나의 element로 제공한다. 앱은 dialog의 열림 상태와 action 결과를 소유하고 host는 frame에 주어진 상태만 표현한다.

공개 API는 [modal_host_element.h](../include/luil/ui/modal_host_element.h)에 있다.

## 기본 구성

```cpp
auto modal = std::make_unique<luil::modal_host_element>(
    luil::modal_host_config {
        .owner = u8"delete-confirmation",
        .scrim_opacity = 0.45f,
        .outside = luil::make_message_action(cancel_delete {}),
        .dismiss = luil::make_message_action(cancel_delete {}),
        .focus_entry = confirm_button_id,
        .focus_return = delete_button_id,
        .content_width = 380.0f,
        .content_height = 190.0f,
    });

modal->set_content(make_delete_dialog());
```

`owner`는 같은 tree의 여러 modal host id를 구분한다. 앱은 dialog가 열려 있을 때만 host를 tree에 포함한다.

## 포인터 차단

Host는 전체 slot을 덮는 scrim과 가운데 content 영역을 만든다. Scrim은 `hit_opaque`라 action이 비어 있어도 뒤의 화면으로 포인터 입력을 통과시키지 않는다.

`scrim_opacity`는 그리기만 제어한다. 값이 `0`이어도 scrim은 입력을 차단한다.

`outside` action이 있으면 scrim click이 해당 action을 실행한다. 비어 있으면 click을 흡수하고 dialog를 유지한다.

Content 영역도 빈 부분의 click을 흡수한다. Dialog 내부의 버튼이나 필드가 hit되지 않은 click이 scrim까지 내려가 outside action을 실행하지 않는다.

## 배치

Content는 host slot의 가운데에 `content_width`와 `content_height` 크기로 배치된다. 단위는 논리 픽셀이고 arrange scale이 적용된다.

`offset_x`와 `offset_y`는 가운데 위치에서 dialog를 이동한다. 끌어서 움직이는 dialog에서는 offset을 앱 상태로 보관하고 다음 frame에 다시 넣는다.

Host는 offset을 화면 경계로 clamp하지 않는다. 앱이 자신의 상태를 `clamp_offset` 같은 정책으로 다듬어야 다음 drag도 실제 표시 위치에서 시작한다.

## Focus trap

`modal_host_element`는 자신을 focus trap으로 설정한다. `ui_tree::focus_order()`와 기존 초점 검증은 trap 자손으로 범위를 제한한다.

Dialog가 나타날 때 바깥 element가 초점을 갖고 있으면 controller가 그 초점을 거둔다. `focus_entry`가 있으면 지정한 내부 element에 자동 진입하고, 없으면 초점 없이 기다렸다가 첫 Tab에서 내부 순회를 시작한다.

`focus_return`은 host가 사라진 뒤 복귀할 바깥 element다. 진입과 복귀의 자세한 수명은 [focus-entry-design.md](focus-entry-design.md)를 따른다.

여러 modal이 겹치면 그리기 순서에서 가장 뒤의 보이는 trap이 유효하다. 위에 그린 dialog가 keyboard를 갖는다.

## Esc 처리

`dismiss` action은 trap이 유효한 동안 Esc로 실행된다. Action이 비어 있으면 Esc를 소비하지 않고 `interaction_policy::on_key()`로 보낸다.

메뉴와 drag 취소가 modal dismiss보다 먼저 처리된다. Dialog 안의 dropdown이 열려 있으면 첫 Esc는 메뉴를 닫고 dialog는 유지한다.

Dismiss action은 보통 outside action과 같은 앱 메시지를 반환하지만 둘을 다르게 설정할 수 있다. Host는 앱 상태를 직접 닫지 않는다. 앱이 메시지를 처리해 다음 frame에서 host를 제거한다.

## 기본 버튼

Dialog의 확인 버튼은 `text_button_config::default_button = true`로 선언할 수 있다. `ui_tree`는 focus trap 안의 기본 버튼만 선택하므로 뒤 화면의 기본 버튼이 Enter를 받지 않는다.

텍스트 입력에서 Enter를 누르면 현재 초점 element가 처리하지 않은 경우 dialog 기본 버튼으로 흐른다. Esc와 Enter 모두 일반 element action 경로를 사용한다.

## 여러 표면

Modal host는 자신이 속한 한 표면의 입력만 가둔다. 활성 보조 창의 host가 keyboard trap이 되고 비활성 창의 host는 해당 창을 활성화할 때까지 진입하지 않는다.

창 비활성화 API로 owner 전체를 막지 않는다. 각 surface의 immutable tree와 활성 표면 상태가 포인터 및 keyboard 범위를 결정한다.

## 반드시 유지할 불변식

- Scrim은 투명해도 포인터를 차단한다.
- Content의 빈 영역은 outside click으로 취급되지 않는다.
- 열림 상태와 offset은 앱 상태다.
- Host는 focus trap, entry, return, dismiss를 함께 선언한다.
- Trap 밖의 초점과 기본 버튼은 modal 범위에서 무효다.
- Dismiss action은 메시지만 내고 앱이 host를 제거한다.
- 가장 뒤에 그린 보이는 trap이 유효하다.

## 검증 지침

[modal_host_element_tests.cpp](../tests/modal_host_element_tests.cpp)는 scrim과 content hit, 가운데 배치, offset, trap 속성을 검증한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 Tab 가둠, 초점 진입과 복귀, Esc 우선순위, 기본 버튼, 여러 표면을 확인한다.

# Enter와 기본 버튼

Enter는 먼저 현재 초점 element를 실행하고, 그 element가 Enter를 처리하지 못할 때만 현재 범위의 기본 버튼을 실행한다. 기본 버튼은 별도의 명령 체계가 아니라 일반 `left_click` 액션을 재사용한다.

이 규칙은 [ui_element.h](../include/luil/ui/ui_element.h), [ui_tree.h](../include/luil/ui/ui_tree.h), [ui_interaction.h](../include/luil/ui/ui_interaction.h)에 정의된다.

## 기본 버튼 선언

```cpp
luil::text_button_config save_config {
    .text = u8"Save",
    .default_button = true,
};

auto save = std::make_unique<luil::text_button_element>(save_id, save_config);
save->set_action(
    luil::ui_trigger::left_click,
    luil::make_message_action(save_requested {}));
```

상자 모양의 `text_button_element`는 이 값을 `default_button()` 속성과 기본 버튼용 강조 채움에 함께 사용한다. `text_button_visual::link`는 채울 상자가 없어 기본 버튼으로 등록되지 않는다. 사용자 정의 element는 `set_default_button(true)`를 호출할 수 있다.

기본 버튼은 보이는 경로에 있고 현재 modal 가둠 안에 있어야 한다. 실행 시점에는 활성 상태와 `left_click` 액션도 필요하다.

## 기본 버튼 선택

`ui_tree`는 게시 시 보이는 element를 그리기 순서로 살펴 기본 버튼을 색인한다. 여러 개가 있으면 가장 뒤에 그려지는 버튼이 선택된다. Modal 가둠이 있으면 가둠 밖의 기본 버튼은 제외된다.

기본 버튼을 하나만 두는 것이 권장된다. 여러 개를 허용하는 선택 규칙은 겹친 UI를 결정적으로 처리하기 위한 안전망이다.

## Enter 처리 순서

`interaction_controller`의 키 처리 순서는 다음과 같다.

1. 열린 메뉴의 Enter
2. 초점 element의 activation
3. 텍스트 편집, 값 step, focus group 이동
4. modal dismiss 처리
5. 기본 버튼
6. `interaction_policy::on_key`

초점 element에 `left_click` 액션이 있으면 Enter와 Space가 그 액션을 실행한다. 키보드 실행의 좌표는 element bounds의 중심이며 `ui_action_context::control`도 전달된다. `interaction_policy::on_click()` 역시 포인터 클릭과 같은 시점에 호출된다.

초점 element가 비활성이거나 실행 액션이 없으면 activation 단계는 키를 소비하지 않는다. Enter는 기본 버튼 단계로 이어진다.

## 텍스트 입력과 수정자

텍스트 입력 element에 초점이 있을 때 Space는 문자 입력이므로 activation 경로가 소비한다. Enter는 텍스트 편집 키로 처리되지 않으면 기본 버튼으로 흐른다. 따라서 dialog의 텍스트 칸에서 Enter를 눌러 확인 버튼을 실행할 수 있다.

Control 또는 Alt와 함께 누른 Enter는 기본 버튼으로 보내지 않는다. 앱 단축키로 해석할 수 있도록 `interaction_policy::on_key()`까지 흐른다. Shift만 누른 Enter는 일반 Enter와 같은 규칙을 따른다.

## Dismiss와의 대칭

- Esc는 현재 가둠의 dismiss 액션을 찾는다.
- Enter는 현재 tree의 기본 버튼을 찾는다.
- 두 경로 모두 일반 element action을 실행하고 `input_action` 목록을 반환한다.
- 처리할 대상이 없으면 앱 키 정책으로 흐른다.

이 구조 덕분에 확인과 취소를 포인터용 액션과 키용 액션으로 중복 선언할 필요가 없다.

## 처리 결과의 의미

| 결과 | 의미 |
| --- | --- |
| `nullopt` | 이 계층의 키가 아니므로 다음 경로로 보낸다. |
| 빈 action 목록 | 키를 처리했지만 앱에 보낼 메시지는 없다. |
| action 목록 | 키를 처리했고 후속 동작을 실행한다. |

기본 버튼이 없거나 실행할 수 없으면 `nullopt`다. 기본 버튼 액션이 빈 목록을 반환하면 Enter는 처리된 것으로 본다.

## 반드시 유지할 불변식

- 초점 element의 실행이 기본 버튼보다 우선한다.
- 기본 버튼은 `left_click` 액션을 그대로 실행한다.
- 텍스트 입력의 Enter는 기본 버튼으로 흐를 수 있다.
- Control·Alt Enter는 기본 버튼을 실행하지 않는다.
- modal 가둠 밖의 기본 버튼은 선택되지 않는다.
- 기본 버튼 표시와 키 동작은 같은 값에서 나온다.

## 검증 지침

[ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)에서 포인터 클릭, Space, Enter가 같은 action을 내는지 검증한다. 텍스트 입력 초점, 비활성 버튼, 여러 기본 버튼, modal 가둠, 수정자 키, 앱 키 정책 fallback도 확인한다.

기본 버튼 색인은 [ui_tree.cpp](../src/ui/ui_tree.cpp), 키 우선순위와 실행은 [ui_interaction.cpp](../src/ui/ui_interaction.cpp)에 구현되어 있다.

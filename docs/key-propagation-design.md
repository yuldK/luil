# 키 조상 전파

키는 가장 작은 단위가 먼저 받는다. 그 단위가 소비하지 않은 키는 감싸는 UI로 차례로 올라가고, 끝까지 남은 키는 앱 정책(`interaction_policy::on_key`)이 받는다.

이 규칙은 [ui_element.h](../include/luil/ui/ui_element.h)의 `key_handler`, [ui_tree.h](../include/luil/ui/ui_tree.h)의 `ancestry_of`, [ui_interaction.cpp](../src/ui/ui_interaction.cpp)의 `process_handler_key`에 정의된다.

## 왜 필요한가

지금까지 키를 받는 단계는 모두 **초점이 선 element 자신**만 봤다. 텍스트 칸, 값 단계(`key_step_target`), 글자 탐색(`key_search_target`)이 그렇다. 그래서 다음 두 경우를 표현할 수 없었다.

- 가상 목록 행 안의 단추에 초점이 있을 때의 Ctrl+A. 단추는 쓰지 않는 키지만 목록은 "전체 선택"으로 쓴다.
- 콤보의 입력 칸에 초점이 있을 때의 ↓·Esc. 칸은 쓰지 않지만 칸을 감싼 콤보가 후보 목록을 옮기거나 닫는다.

앱 정책의 `on_key`로 받을 수는 있다. 그러나 그러면 앱이 "지금 초점이 어느 목록 안인가"를 tree에서 다시 찾아야 하고, 컴포넌트가 자기 키를 스스로 갖지 못한다.

## 처리기

```cpp
// nullopt = 소비하지 않음(위로 흐른다), 빈 목록 = 소비했고 낼 메시지가 없다.
using key_handler = std::function<std::optional<std::vector<input_action>>(const key_pressed_event& event)>;

list->set_key_handler([](const luil::key_pressed_event& event) -> std::optional<std::vector<luil::input_action>> {
    if (event.key == luil::key_code::key_a && event.primary_shortcut_down())
        return std::vector<luil::input_action> { luil::make_app_action(select_all_requested {}) };
    return std::nullopt;
});
```

- 답의 규약은 `key_step_target`·`key_search_target`과 같다. 쓰지 않는 키는 반드시 `std::nullopt`로 돌려준다. 빈 목록은 "삼켰다"는 뜻이라 그 위로 가지 않는다.
- 처리기는 hit 대상을 넓히지 않는다. 키는 포인터가 아니라 초점을 따른다.
- 처리기는 게시된 tree에서 input thread가 부른다. 앱 상태를 바꾸지 않고 메시지만 낸다.

## 전파 순서

`ui_tree::ancestry_of(id)`는 그 element부터 root까지의 경로다(자신이 맨 앞). 컨트롤러는 이 경로를 앞에서부터 걸으며 처리기에 묻고, 처음 답한 처리기에서 멈춘다.

- 보이지 않는 가지는 경로가 없다. 초점이 설 수 없는 자리다.
- 비활성 element는 묻지 않고 건너뛴다. 그 위의 element는 계속 묻는다. 흐리게 그린 행 위의 목록은 살아 있기 때문이다.
- 초점이 없으면 이 단계는 아무것도 하지 않는다.
- 부모를 색인에 담지 않는다. 키 하나에 root에서 한 번 내려가며 찾는다. 키는 드물고, frame마다 모든 element의 부모를 적는 비용이 더 크다.

## 다른 단계와의 자리

`process_key`에서 이 단계는 끌기 취소 Esc 다음, 가둠의 Esc 앞이다.

1. 열린 메뉴
2. 초점 컨트롤의 Space·Enter 실행
3. Tab
4. 초점 텍스트 입력
5. 확대 보기, 값 단계
6. focus group
7. 진행 중인 끌기의 Esc 취소
8. **초점 element부터 root까지의 `key_handler`**
9. 가둠의 Esc dismiss
10. 기본 버튼의 Enter
11. 앱 정책의 `on_key`

- 텍스트 칸·값 단계·묶음은 이 단계보다 앞이다. 그것들이 가장 안쪽의 소비자이기 때문이다. 텍스트 칸에 초점이 있으면 Ctrl+A는 칸의 전체 선택이고 목록에 오지 않는다.
- 가둠의 Esc보다 앞인 이유는 dialog 안의 콤보 때문이다. 콤보가 Esc로 후보 목록만 닫으면 dialog는 남아야 한다. 콤보가 소비하지 않은 Esc만 dialog로 간다.
- 끌기 취소보다 뒤인 이유는 잡고 있는 손이 언제나 먼저이기 때문이다.

## 검증

[key_propagation_tests.cpp](../tests/key_propagation_tests.cpp)가 다음을 잠근다.

- 경로가 자신부터 root까지이고, 숨은 가지에는 경로가 없다.
- 행 안 단추가 쓰지 않는 Ctrl+A를 목록이 받고, 앱 정책까지 가지 않는다.
- 가장 안쪽에서 답한 처리기가 멈추고, 빈 답은 소비, nullopt는 위로 흐른다.
- 아무도 소비하지 않은 키는 앱 정책이 받는다.
- 비활성 조상은 건너뛰고 그 위는 묻는다.
- 초점 텍스트 칸이 Ctrl+A를 목록보다 먼저 가진다.

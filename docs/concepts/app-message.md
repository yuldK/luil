# 앱 메시지와 UI 명령

luil은 앱의 메시지 타입을 알지 못한 채 액션 결과를 스레드 사이로 전달한다. 액션은 상태를 직접 바꾸지 않고 `input_action`을 반환한다. 불투명 메시지는 [`app_message.h`](../../include/luil/ui/app_message.h), 명령과 `input_action`은 [`ui_events.h`](../../include/luil/ui/ui_events.h)에 선언되어 있다.

## 불투명 앱 메시지

`app_message`는 기본 생성 시 비어 있고 `empty()`로 확인한다. 값을 받는 생성자는 `std::make_shared<const T>`로 payload를 만들며 타입 정보도 함께 저장한다.

payload는 `shared_ptr<const void>`로 보관한다. 복사 가능한 envelope를 만들면서 payload를 다시 복사하지 않고, `const`를 통해 게시된 액션의 값을 불변으로 취급한다. `get<T>()`는 저장된 정확한 타입일 때만 포인터를 반환하며 상속 변환은 하지 않는다.

액션은 자신의 메시지를 반환하고, `logic_driver::handle(app_message message)`의 구현은 `get<T>()`로 복원한다. `make_message_action(intent)`는 고정된 메시지 하나를 반환하는 `ui_action`을 만들고, `make_app_action(intent)`는 `input_action` 값 하나를 만든다.

앱의 `handle()`에서 `message.get<MyIntent>()`가 null이 아니면 그 앱 타입의 값을 읽는다. payload의 const 접근만으로 외부 포인터나 mutable 캡처까지 안전해지지는 않으므로, 게시된 액션은 공유되는 앱 상태를 직접 변경하지 않아야 한다.

## UI 스레드 명령

`ui_command`는 프레임워크가 직접 실행하는 창 명령이다.

| 명령 | 뜻 |
| --- | --- |
| `window_minimize` | 주 창을 최소화한다. |
| `window_toggle_maximize` | 최대화와 복원을 오간다. |
| `window_toggle_fullscreen` | 테두리 없는 전체 화면을 오간다 (주 창 전용 — [Win32 창과 표면](window.md)). |
| `window_close` | 닫기를 요청한다. |

앞의 셋은 창을 어떤 모습으로 세울지의 명령이라 **앱 상태가 아니다.** logic thread를 지나지 않고 UI thread가 그 자리에서 실행하며, 앱은 결과를 창 배치 보고(`window_placement`)로 되받는다. 전체 화면은 OS에 넘길 명령이 없어 표면이 스타일·자리·갈무리를 함께 바꾼다.

`app_ui_command`는 앱이 정의한 불투명 정수와 문자열 인자를 담으며, UI 스레드에서 호출되는 앱 handler가 해석한다. 파일 대화 상자나 외부 프로그램 실행처럼 UI 스레드가 필요한 앱 동작에 사용한다.

클립보드 복사·붙여넣기는 모든 텍스트 컨트롤이 공유하는 Win32 동작이므로 `clipboard_copy_request`와 `clipboard_paste_request`로 제공한다.

```cpp
using input_action = std::variant<
    std::monostate, app_message, ui_command, app_ui_command,
    clipboard_copy_request, clipboard_paste_request>;
```

이 variant의 앱별 의미는 `app_message`와 앱 handler에만 있다. 프레임워크는 메시지를 전달하고 실행할 스레드를 선택한다.

# 앱 메시지와 UI 명령

luil은 앱의 메시지 타입을 알지 못한 채 액션 결과를 스레드 사이로 전달한다. 액션은 상태를 직접 바꾸지 않고 `input_action`을 반환한다. 불투명 메시지는 [`app_message.h`](../../include/luil/ui/app_message.h), 명령과 `input_action`은 [`ui_events.h`](../../include/luil/ui/ui_events.h)에 선언되어 있다.

## 불투명 앱 메시지

`app_message`는 기본 생성 시 비어 있고 `empty()`로 확인한다. 값을 받는 생성자는 `std::make_shared<const T>`로 payload를 만들며 타입 정보도 함께 저장한다.

payload는 `shared_ptr<const void>`로 보관한다. 복사 가능한 envelope를 만들면서 payload를 다시 복사하지 않고, `const`를 통해 게시된 액션의 값을 불변으로 취급한다. `get<T>()`는 저장된 정확한 타입일 때만 포인터를 반환하며 상속 변환은 하지 않는다.

액션은 자신의 메시지를 반환하고, `logic_driver::handle(app_message message)`의 구현은 `get<T>()`로 복원한다. `make_message_action(intent)`는 고정된 메시지 하나를 반환하는 `ui_action`을 만들고, `make_app_action(intent)`는 `input_action` 값 하나를 만든다.

앱의 `handle()`에서 `message.get<MyIntent>()`가 null이 아니면 그 앱 타입의 값을 읽는다. payload의 const 접근만으로 외부 포인터나 mutable 캡처까지 안전해지지는 않으므로, 게시된 액션은 공유되는 앱 상태를 직접 변경하지 않아야 한다.

## 되돌림 메시지

앱이 사용자 동작을 되돌리려면 그 동작의 역 메시지를 함께 든다. [`undo_stack`](../../include/luil/ui/undo_stack.h)의 한 걸음은 `app_message` 쌍(되돌림, 다시 실행)이고, stack은 메시지를 열어 보지도 앱 상태를 바꾸지도 않는다. `undo`·`redo`가 걸음 안의 메시지를 `apply`로 넘기면 앱이 자기 `handle`에서 받아 상태를 바꾼다. 상태는 `handle`에서만 바뀐다는 위의 규칙이 그대로 선다.

- **메시지는 절대 값이다.** 연속 동작을 한 걸음으로 합치면 앞 걸음의 되돌림과 뒤 걸음의 다시 실행만 남는다. "다음 값으로 토글"이 아니라 "이 값으로 정함"을 담아야 중간 걸음이 없어도 맞다. 상태 스냅숏도 같은 길이다. `restore_intent { before }`와 `{ after }`를 담으면 되고, payload는 공유라 걸음이 복사본을 따로 쥐지 않는다.
- **처음 실행과 다시 실행이 한 길이다.** 사용자 동작은 걸음을 쌓은 뒤 다시 실행 메시지와 같은 것을 적용한다. 재생 중에는 stack이 쌓지 않으므로 재생 메시지를 기록하는 `handle`에 그대로 흘려도 걸음이 두 번 쌓이지 않는다.
- 역 메시지가 맞는지는 라이브러리가 검증할 수 없다. 앱의 test가 "동작, 되돌림, 처음 상태"를 잠근다.

자세한 규칙은 [실행 취소 stack](../undo-stack-design.md)에 있다.

## UI 스레드 명령

`ui_command`는 프레임워크가 직접 실행하는 창 명령이다.

| 명령 | 뜻 |
| --- | --- |
| `window_minimize` | 주 창을 최소화한다. |
| `window_toggle_maximize` | 최대화와 복원을 오간다. |
| `window_toggle_fullscreen` | 테두리 없는 전체 화면을 오간다 (주 창 전용 — [Win32 창과 표면](window.md)). |
| `window_close` | 주 창 닫기를 요청한다. 앱이 주 창의 닫힘을 받아 가면(`ui_frame::close`) 그 훅으로 간다. |
| `application_exit` | 앱을 끝낸다. 닫힘 훅을 거치지 않는다 ([여러 top-level window](../multi-window-design.md)). |

앞의 셋은 창을 어떤 모습으로 세울지의 명령이라 **앱 상태가 아니다.** logic thread를 지나지 않고 UI thread가 그 자리에서 실행하며, 앱은 결과를 창 배치 보고(`window_placement`)로 되받는다. 전체 화면은 OS에 넘길 명령이 없어 표면이 스타일·자리·갈무리를 함께 바꾼다.

`app_ui_command`는 앱이 정의한 불투명 정수와 문자열 인자를 담으며, UI 스레드에서 호출되는 앱 handler가 해석한다. 파일 대화 상자나 외부 프로그램 실행처럼 UI 스레드가 필요한 앱 동작에 사용한다.

클립보드 복사·붙여넣기는 모든 텍스트 컨트롤이 공유하는 Win32 동작이므로 `clipboard_copy_request`와 `clipboard_paste_request`로 제공한다.

```cpp
using input_action = std::variant<
    std::monostate, app_message, ui_command, app_ui_command,
    clipboard_copy_request, clipboard_paste_request>;
```

이 variant의 앱별 의미는 `app_message`와 앱 handler에만 있다. 프레임워크는 메시지를 전달하고 실행할 스레드를 선택한다.

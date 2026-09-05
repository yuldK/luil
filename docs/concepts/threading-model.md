# 스레드 모델

luil은 [`app_host`](../../include/luil/win32/app_host.h)를 통해 UI, input, logic 세 실행 맥락을 분리한다.

| 스레드 | 소유 |
| --- | --- |
| UI | HWND, renderer, 표면, WebView2, clipboard, TSF COM 호출 |
| input | raw 이벤트, `interaction_controller`, `interaction_snapshot` |
| logic | 앱 상태, `logic_driver`, `ui_frame` 게시 |

UI와 input은 불변 tree를 읽고, logic은 새 frame/tree를 만든다. 앱 상태는 logic에서만 변경하며 액션은 메시지로 전달한다. `channel`은 사건, `latest_slot`은 최신 상태를 운반한다.

## 게시 순서

`logic_driver::make_frame()`이 불변 frame을 반환하면 host는 주 tree, popup·보조 창 tree 목록, frame 순서로 각각의 slot에 게시한다. UI wake callback은 frame slot에 붙어 있으므로 tree 게시가 wake보다 앞선다. 각각 별도 slot이므로 모든 조회가 하나의 원자적인 frame transaction인 것은 아니다. UI는 자신이 본 slot version을 기억한다. input pump가 새 tree를 받기 전에는 이전 tree로 이벤트를 처리하므로 tree와 interaction이 서로 다른 frame일 수 있지만 어느 쪽도 수정되지 않는다.

input은 interaction snapshot을 값으로 게시하고 UI는 자신의 표면 id에 맞게 필터링한다. UI가 앱 메시지를 보내거나 input이 액션을 만들 때 앱 상태를 직접 참조하지 않는다.

## 종료

`app_host::shutdown()`은 UI thread에서 호출하며 취소 알림, 종료 app message, logic의 `shutdown_completed()` 확인, worker 중지, logic join, input join 순서로 진행한다. 종료 메시지는 inbox가 닫히기 전에 전달한다. `shutdown_completed()`는 앱이 종료 신호를 처리하고 종료 저장을 제출했는지 답한다. host는 그 확인 뒤 worker를 중지하고 inbox를 닫아 남은 메시지를 배출한다. 실제 저장 완료를 언제 보장할지는 앱의 종료 처리와 worker 중지 계약으로 정한다. 취소와 종료 처리가 응답하지 않으면 무기한 join하지 않고 fail-fast한다. `stop_workers()`는 logic 종료 처리 뒤, logic join 전에 UI 스레드에서 호출한다. `cancel()`·`make_close_message()`·`shutdown_completed()`도 UI 호출을 고려해 스레드 안전하게 구현해야 한다.

시간에 따른 logic 상태는 `next_tick()`이 다음 시각을 반환하고 `tick(now)`이 상태를 갱신한 뒤 새 frame을 게시한다. worker가 필요하면 앱이 소유하되 종료 시 `stop_workers()`에서 중지한다.

## 창 없는 사용

`app_host`의 wake callback은 비어 있을 수 있다. 따라서 logic/input/channel/slot 조립과 종료·메시지 처리는 HWND 없이도 테스트할 수 있으며, Win32 callback은 host를 사용하는 애플리케이션이 주입한다.

driver와 input 처리에서 예외가 스레드 진입 경계까지 전파되면 host는 fault를 기록하고 처리를 멈춘다. 앱 오류는 메시지나 frame의 상태 값으로 전달하고, UI는 `faulted()`를 확인해 종료할 수 있다.

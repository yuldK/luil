# 입력 pump

[`run_ui_input_pump`](../../include/luil/ui/ui_interaction.h)는 input 스레드에서 raw 입력을 소비하고, 현재 tree로 정규화한 뒤 액션을 목적지별로 보낸다.

```text
input_inbox.receive_wait(min(250ms, next_deadline - 지금))
    → tree_slot / surface_tree_slot.take_newer()
    → interaction_controller.process(event)
    → interaction_controller.advance(지금)
    → app_message       → logic inbox
    → ui_command        → UI callback
    → app_ui_command    → UI callback
    → clipboard request  → UI callback
    → interaction_slot.publish() (변경 시)
```

250ms 대기는 이벤트가 없어도 새 tree를 받게 한다. 마지막 휠 뒤 새 tree가 게시되면 다음 대기에서 hover를 새 tree로 재판정한다.

이벤트 없이 시간이 흘러야 일어나는 판정도 있다. 지금은 터치 길게 누르기 하나다. controller는 시계를 조회하지 않으므로 다음 판정 시각(`next_deadline`)만 알린다. pump는 받기 대기를 그 시각까지로 줄이고, 깰 때마다(이벤트가 왔든 시간이 다 됐든) `advance(지금)`을 불러 그 판정을 실행한다. 그래서 손을 떼기 전에 메뉴가 열린다 ([터치 제스처와 펜 입력](../touch-pen-input-design.md)). test는 `advance`에 시각을 손으로 준다.

앱 메시지는 `reject_newest` 채널이 가득 찰 때 닫힌 채널만 조용히 포기하고, input pump 경로에서는 여유가 생길 때까지 짧게 재시도한다. `app_host::post_app_message`는 반환값을 관찰하지 않으므로 포화 시 게시가 거절될 수 있으며 `app_inbox_statistics()`로 확인한다. 종료 신호는 종료 예산 안에서 재시도한다.

raw 입력은 `drop_oldest` 정책이다. 포인터 이동의 중간 좌표는 버릴 수 있지만 누름·뗌·초점 상실이 포함될 수 있으므로 `envelope.sequence`가 건너뛴 것을 발견하면 `cancel_dropped_gestures()`로 진행 중인 press와 drag를 취소한다. 초점은 유지하고 다음 초점 이벤트가 갱신한다.

pump의 callback 자체는 input 스레드에서 호출된다. `app_host`는 이 callback에서 UI 명령·클립보드 요청을 내부 큐에 넣고 UI를 깨우며, 실제 동작은 UI가 큐를 꺼내 실행한다. 액션의 실행 스레드는 타입으로 나뉜다. 앱 상태는 logic에서, 창 조작·파일 대화 상자·클립보드는 UI에서 실행한다. pump는 Win32를 include하지 않고 callback만 호출한다. 클립보드가 다른 프로세스에 열려 있으면 유한한 재시도 후 실패하며, 텍스트가 없을 때는 `CF_HDROP`의 첫 파일 경로를 붙여넣을 수 있다.

`interaction_snapshot`은 값 타입이다. 이전에 게시한 snapshot과 다를 때만 slot에 게시하므로 같은 element 위에서의 단순한 포인터 이동은 UI를 깨우지 않는다.

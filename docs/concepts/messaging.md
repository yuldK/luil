# 스레드 경계 messaging

luil의 스레드 간 전달은 [`channel`](../../include/luil/messaging/channel.h)과 [`latest_slot`](../../include/luil/messaging/latest_slot.h) 두 장치로 나뉜다.

| | `channel` | `latest_slot` |
| --- | --- | --- |
| 의미 | 클릭·키·앱 메시지 같은 사건 | frame·tree·interaction 같은 상태 |
| 생산자/소비자 | MPSC / 단일 소비자 | 게시자·조회자 다수 가능 |
| 보관 | FIFO, 유한 용량 | 새 값이 이전 값을 대체 |
| 포화 | `reject_newest` 또는 `drop_oldest` | 중간 version을 건너뜀 |

`channel::post()`는 블로킹하지 않는다. 수신은 소비자 스레드 전용이며 debug 빌드에서 첫 수신 스레드를 기억한다. `drain(out, max_count)`는 한 번에 처리할 양을 제한해 계속 들어오는 입력이 렌더링을 굶기지 않게 한다. `close()` 뒤의 게시는 `channel_closed`이고, 이미 들어온 항목은 모두 꺼낸 뒤 `closed`가 된다.

입력에는 최신 좌표가 중요한 `drop_oldest`, 앱 메시지에는 `reject_newest`를 쓴다. 이 정책 자체가 전달을 보장하지는 않으므로 `channel_full`에 대한 재시도 또는 거절 처리가 필요하다. 접수된 항목에만 `sequence`가 붙으므로 `drop_oldest`의 유실은 수신 번호의 건너뜀으로, `reject_newest`의 거절은 반환값과 통계로 확인한다.

`latest_slot::publish()`는 값을 바꾸고 1부터 증가하는 version을 반환한다. 닫힌 slot에는 게시하지 않고 0을 반환한다. 소비자는 `take_newer(last_seen)`으로 자신이 본 version보다 새로울 때만 가져온다. 닫힌 뒤에도 게시된 마지막 값은 읽을 수 있다.

두 장치는 mutex로 내부 저장소를 보호하며 lock-free 자료구조는 아니다. `set_signal_callback()`으로 깨우기 동작을 주입받는다. callback은 짧고 예외가 없어야 하며 channel API를 재진입해서는 안 된다. channel은 비어 있던 큐에 항목이 들어올 때, slot은 아직 가져가지 않은 새 값이 처음 생길 때 신호한다. 둘 다 `close()`에서도 신호한다. 연속 게시는 신호를 병합한다. slot callback이 전달 실패를 보고하면 다음 게시가 다시 신호한다. 조립 순서는 channel/slot 생성, callback 설정, 스레드 시작이다.

messaging은 header-only라 별도 실행 파일도 AddressSanitizer로 전체 구현을 검사할 수 있다. MSVC `/RTC1`과 ASan을 함께 사용하지 않는다.

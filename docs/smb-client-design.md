# SMB client

`luil::net::smb_client`는 Android 앱이 같은 네트워크의 NAS나 Windows 공유를 SMB로 읽게 하는 client다. 공개 API는
[`include/luil/net/smb_client.h`](../include/luil/net/smb_client.h)에 있다.

## 어디서 서나

| 구성 | `create` | 전송 |
| --- | --- | --- |
| Android, `LUIL_ENABLE_SMB=ON` | client를 만든다 | libsmb2 (`libsmb2.so`, LGPL-2.1) |
| Android, 기본(끔) | nullptr, "지원 안 함" | 없음 |
| Windows | nullptr, "지원 안 함" | 운영체제의 SMB를 쓴다 |

Android의 문서 선택기(SAF)는 결과를 받으려면 Java가 있어야 해서 두지 않았다. 사용자가 고른 폴더 대신 앱이 서버와
공유를 알고 SMB로 붙는 길이 이것이다. Windows에서는 쓸 일이 없어 libsmb2를 싣지 않는다. 의존과 고지는
[빌드 체계](concepts/build-system.md)와 [소비자 계약](concepts/consumer-contract.md#smb)에 있다.

## 계약

[`http_client`](http-client-design.md)와 같은 모양이다.

- **표와 답.** `send`가 받은 요청마다 0이 아닌 표 하나와 답 하나가 온다. 0은 받지 않은 것이고 답도 없다 — 규칙에
  맞지 않는 요청, 정지한 client, `max_in_flight` 포화다. 포화는 숨은 줄 없이 바로 거절한다.
- **배달.** 답은 client의 배달 thread 하나에서 차례로 `deliver`에 간다. 안에서 `send`·`cancel`을 불러도 되고
  `stop`은 부르지 않는다. 앱은 `app_host::post_app_message`로 넘긴다.
- **취소와 정지.** `cancel`은 그 요청의 답을 `cancelled`로 하나 낸다. `stop`은 받기를 닫고 진행 중인 요청을 끊고,
  돌아온 뒤에는 `deliver`를 부르지 않는다. `stop_budget`(기본 1초) 안에 worker가 끝나지 않으면 기다리지 않는다 —
  남은 worker는 공유 상태를 쥐고 있어 안전하게 끝나고 그 답은 버린다.
- **시간 상한.** `timeout`(기본 30초)은 연결부터 답까지다. 넘기면 `timed_out`이다.

## 요청

| 갈래 | 하는 일 | 답 |
| --- | --- | --- |
| `list` | 폴더의 항목 (`.`·`..` 뺌) | `entries`: 이름, 폴더 여부, 크기, 수정 시각 |
| `stat` | 항목 하나의 상태 | `entry` (이름은 경로의 마지막 조각) |
| `read` | 파일의 `offset`부터 `length`까지 | `data` (파일 끝이면 짧다) |

- **서버.** IP 주소나 DNS 이름에 포트를 붙일 수 있다 (`192.168.0.10`, `nas.example:4445`, `[fe80::1]:445`). 기본 포트는
  445다. Android의 이름 풀기는 `.local`(mDNS) 이름을 풀지 않으므로 집 안 NAS는 IP 주소로 적는다.
- **계정.** 사용자 이름이 비면 `guest`로 붙는다. NTLM만 쓴다 (Kerberos는 넣지 않았다). 서명은 서버가 요구할 때 따르고,
  `encrypt`를 켜면 SMB3 암호화를 요구한다 — 서버가 못 하면 연결이 실패한다. SMB2.02부터 3.1.1까지 협상한다 (SMB1은
  없다).
- **경로.** 공유 안의 경로를 `/`로 나눈다. 앞뒤와 겹친 `/`는 다듬는다. 비면 공유의 맨 위다.
- **읽기.** 길이는 1 이상 `max_read_bytes`(기본 16 MiB) 이하다. 큰 파일은 구간으로 나눠 읽는다. 서버의 최대 읽기
  크기보다 크면 안에서 여러 번에 나눠 받는다.
- **거절.** 서버·공유가 비었거나 공백·`/`·`\`·제어 문자가 있으면, 공유 이름이 80자를 넘으면, 경로에 `\`나 제어 문자가
  있으면, 읽기 길이가 범위 밖이면 `send`가 거절한다 (`check_smb_request`).

## 오류

`smb_error`의 갈래는 NT 상태를 먼저, 그다음 libsmb2가 돌려준 -errno를 본다 (`smb_error_kind_for`).

| 갈래 | 보는 것 |
| --- | --- |
| `logon_failure` | 틀린 암호, 로그온 실패, 계정 제한·만료·잠김 |
| `access_denied` | 접근 거부, `EACCES`·`EPERM` |
| `not_found` | 없는 공유(`BAD_NETWORK_NAME`), 없는 파일·경로, `ENOENT` |
| `cannot_connect` | 연결 거절·닿지 않음·끊김, 그리고 연결·로그온 단계의 갈래 없는 실패 (이름을 풀지 못함 등) |
| `failed` | 그 밖. `message`에 libsmb2의 글이 그대로 있다 |

`message`는 libsmb2의 `smb2_get_error` 그대로라, 사람이 아니라 개발자가 읽는 글이다. 서버가 없는 개발 환경에서 사용자가
알려 줄 수 있는 단서가 이것뿐이라 고치지 않고 넘긴다. **암호는 오류 글에도 logcat에도 남기지 않는다.**

## 구현

요청마다 worker thread 하나가 libsmb2 문맥 하나를 열고, 연결·로그온·공유 연결 뒤에 요청을 하고, 공유를 놓고 문맥을
거둔다. 연결은 다시 쓰지 않는다 — 같은 서버의 요청이 이어지면 그때마다 연결과 로그온을 다시 한다.

- **비동기 API를 우리가 돌린다.** libsmb2의 동기 API는 막히는 동안 끊을 길이 없다. 비동기 API(`*_async`)를 부르고
  `smb2_get_fds`의 소켓을 poll로 100 ms씩 기다리며, 그 사이마다 취소·정지·시간 상한을 본다. 주소가 여럿인 서버는
  libsmb2가 여러 주소에 함께 연결을 시도한다 (Happy Eyeballs).
- **문맥을 거둘 때.** 문맥을 거두면 libsmb2가 남은 호출의 콜백을 `SMB2_STATUS_SHUTDOWN`으로 부른다. 그래서 콜백이 쓰는
  자리(결과 칸, 상태 칸)는 문맥보다 먼저 세워 나중에 거둔다.
- **경로의 구분자.** libsmb2가 요청을 지을 때 `/`를 `\`로 바꾸므로 `/`로 나눈 경로를 그대로 넘긴다.

## 로컬 네트워크 권한

Android 17부터 targetSdk 37 이상인 앱은 같은 네트워크의 주소로 가는 연결에 `ACCESS_LOCAL_NETWORK`가 있어야 한다. 없으면
연결이 시간 상한까지 기다리다 `timed_out`이나 `cannot_connect`로 끝난다. 앱은 SMB를 쓰기 전에
`app_host::request_permission(app_permission::local_network)`로 묻는다 ([기기 요청](device-request-design.md#권한)).
그 아래 판은 `INTERNET`만 있으면 된다. 두 권한 모두 앱의 매니페스트에 선언한다.

## 검증

| 대상 | 확인 |
| --- | --- |
| 규칙 | 서버·공유·경로·계정의 거절, 포트·IPv6 꼴의 서버, 읽기 길이의 범위와 넘침, 음수 상한, 경로 다듬기와 마지막 조각, NT 상태·errno의 오류 갈래, SMB를 세우지 않은 구성의 "지원 안 함" (core test, Windows와 기기) |
| client | 거절한 요청은 표가 없음, 닫힌 포트는 `cannot_connect`이고 오류 글에 암호가 없음, 답하지 않는 서버의 시간 상한, 기다리는 요청의 취소와 `max_in_flight` 거절, 정지 뒤 배달 없음과 상한 안의 정지 (기기 test, [`smb_client_tests.cpp`](../tests/smb_client_tests.cpp)) |
| 실제 서버 | 서버가 없어 자동 test가 없다. mobile demo의 SMB 페이지로 사람이 NAS에 붙어 열거·읽기·틀린 암호·없는 공유를 확인한다 ([Android 이식 계획](android-port-plan.md)) |
| demo 상태 | 목록 요청 중·실패·거절 시 이전 행을 제거하고, 성공한 목록 응답에서만 폴더를 확정한다. 이전 행의 늦은 클릭과 교체·주소 수정 뒤의 늦은 답은 무시한다 (서버 없는 core test) |

기기 test는 `LUIL_ENABLE_SMB`를 켠 구성에서 `luil_smb_tests`로 돈다. 셸에서 도는 실행 파일이라 JVM이 필요 없고,
[`adb_run.cmake`](../cmake/android/adb_run.cmake)가 실행 파일 옆의 `libsmb2.so`를 함께 올린다 (`LIBRARIES`).

## 남은 것

- 쓰기(만들기·지우기·이름 바꾸기·쓰기), 공유 목록 찾기, 서버 찾기(mDNS)는 없다.
- 연결을 다시 쓰지 않아, 큰 파일을 구간으로 나눠 읽으면 구간마다 연결과 로그온을 다시 한다.
- Kerberos와 SMB1은 없다.

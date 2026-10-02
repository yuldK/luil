# TSF와 IME 입력

Win32 Text Services Framework는 UTF-16 ACP 문서와 COM callback을 요구하고, luil 텍스트 모델은 UTF-8 byte offset을 사용한다. [`win32_tsf_input.cpp`](../../src/win32/win32_tsf_input.cpp)가 이 경계를 하나의 shadow document로 관리한다.

## Shadow document

현재 초점 텍스트 element의 확정 문자열을 shadow document에 복사한다. 문서와 창구(`text_input_host`: 초점 칸, 확정 글, 글자 자리, 조합·편집 게시)는 플랫폼을 가리지 않아 [`text_input_host.h`](../../src/host/text_input_host.h)에 있고 Android IME도 같은 것을 쓴다 ([텍스트 입력](text-input.md#android-ime)). TSF 쪽 창구(`tsf_host`)는 거기에 화면 좌표(`text_screen_rect`)만 더한다. TSF가 조합을 갱신하면 `text_composition_event`로 전체 표시 문자열, caret, composing 범위를 만들고 input policy가 앱 메시지로 변환한다. 조합 중간 결과는 앱의 확정 초안에 쓰지 않으며, 확정 시 `replace_all` 편집 명령 하나가 적용된다.

키보드 focus를 잃으면 먼저 실제 TSF composition을 종료하고 document manager에서 context를 분리한 뒤 `surface_focus_lost_event`를 input으로 보낸다. 이벤트의 surface id가 현재 focused surface와 일치할 때만 텍스트 초점을 거둔다. `WM_SETFOCUS`만으로 이전 element를 복원하지 않으며 텍스트 입력의 논리 초점은 클릭·Tab·명시적인 focus entry 같은 입력 규칙으로 다시 정한다. popup은 keyboard focus를 받지 않으므로 anchor surface의 TSF session이 자기 tree와 연결된 popup tree를 함께 조회한다.

## 좌표 변환

ACP는 UTF-16 code unit 수이고 내부 offset은 UTF-8 byte 수다.

| 문자열 | UTF-8 | ACP |
| --- | ---: | ---: |
| `abc` | 3 | 3 |
| `한글` | 6 | 2 |
| `😀` (U+1F600) | 4 | 2 |

`acp_length`, `acp_from_utf8`, `utf8_from_acp`가 두 좌표계를 변환한다. `utf8_from_acp()`는 surrogate pair 내부의 ACP를 문자 시작으로 되돌리고, 0 이하는 시작, 문서 길이를 넘는 값은 끝으로 자른다. UTF-8에서 ACP로 변환하는 호출자는 문자 경계의 byte offset을 전달한다.

창 하나가 `ITfThreadMgr2 → ITfDocumentMgr → ITfContext → ITextStoreACP2` 한 벌을 소유하고, focus가 바뀔 때 shadow document 내용만 교체한다. `WM_KEYDOWN`/`WM_KEYUP`은 `handle_key()`로 TIP에 먼저 보내며 TIP이 소비한 키는 앱으로 전달하지 않는다. 조합 중 `WM_CHAR`도 버려 중복 입력을 막는다. TSF session callback은 UI thread에서 실행될 수 있으므로 policy의 composition 변환은 순수해야 한다.

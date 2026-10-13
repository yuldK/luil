# 파일 목록 클립보드

파일을 다루는 앱이 OS 파일 관리자(탐색기)와 잘라내기·복사·붙여넣기를 주고받는 계약이다. 글 클립보드(`clipboard_copy_request`·`clipboard_paste_request`)와 같은 길을 타고, 파일은 경로 목록과 뜻(잘라내기·복사)으로만 다닌다. 공개 어휘는 [ui_events.h](../include/luil/ui/ui_events.h)와 [app_delegate.h](../include/luil/app/app_delegate.h)에 있다.

## 어휘

| 값 | 방향 | 뜻 |
| --- | --- | --- |
| `clipboard_files_copy_request { paths, intent }` | 앱 → 클립보드 | UTF-8 절대 경로 목록과 `file_clipboard_intent::copy`·`cut`을 넣는다. 빈 목록은 클립보드를 비운다 |
| `clipboard_files_paste_request { tag }` | 앱 → 클립보드 | 클립보드의 파일 목록을 읽어 달라는 요청이다 |
| `file_clipboard_content { paths, intent, supported, tag }` | 클립보드 → 앱 | 읽은 답이다. `app_delegate::make_file_clipboard_message`가 앱 메시지로 짓는다 |
| `clipboard_change { owned, sequence }` | 클립보드 → 앱 | 클립보드가 바뀌었다. `app_delegate::make_clipboard_changed_message`가 앱 메시지로 짓는다 |

두 요청은 `input_action`이라 element 액션이나 정책(`interaction_policy::on_key`의 Ctrl+X·C·V)이 돌려줄 수 있다. logic thread가 앱 상태에서 정한 순간에는 `app_host::post_clipboard_request`로 넣는다 — 어느 thread에서 불러도 된다. 실제 클립보드 접근은 언제나 UI thread의 몫이다.

읽기의 답은 파일이 없어도 온다. 글만 든 클립보드, 빈 클립보드, 읽지 못한 클립보드는 빈 목록이다. `tag`는 요청에 실은 값이 그대로 돌아와, 앱이 어느 폴더에 붙여 넣으려던 것인지 가른다.

## Win32

넣기는 탐색기와 같은 두 형식을 한 번에 넣는다.

- `CF_HDROP`: `DROPFILES` 머리(`fWide` 참) 뒤에 UTF-16 경로들이 NUL로 나뉘고 NUL 둘로 끝난다. 경로의 `/`는 `\`로 바꾼다.
- `Preferred DropEffect`(`CFSTR_PREFERREDDROPEFFECT`): DWORD 하나. 잘라내기는 `DROPEFFECT_MOVE`, 복사는 `DROPEFFECT_COPY`다.

경로는 드라이브 절대 경로(`C:\`)나 UNC(`\\server\share`, `\\?\`)여야 한다. 하나라도 아니면 아무것도 넣지 않는다 — 탐색기는 상대 경로를 붙여 넣는 사람의 현재 폴더로 읽는다. 둘째 형식을 넣지 못하면 첫째도 거둔다. 뜻이 빠진 잘라내기는 복사로 읽혀 원본이 남는다.

읽기는 `CF_HDROP`의 목록(파일 끌기와 같은 상한 `max_dropped_paths`)과 `Preferred DropEffect`를 읽는다. 값이 없으면 복사다. 이동만 있으면 잘라내기이고, 복사와 함께 있으면(탐색기의 복사는 복사+링크다) 복사다 — 원본을 옮겨 버리는 쪽으로 추측하지 않는다.

주 창은 시작할 때 `AddClipboardFormatListener`로 클립보드를 듣고, `WM_CLIPBOARDUPDATE`마다 `clipboard_change`를 알린다. `owned`는 지금 클립보드의 임자가 이 앱의 주 창인가(`GetClipboardOwner`)이고 `sequence`는 `GetClipboardSequenceNumber`다. 앱이 넣은 직후에도 한 번 온다(`owned` 참). 다른 프로그램이 클립보드를 가져가면 `owned`가 거짓이 되어, 앱은 "잘라 둔 항목"의 흐린 표시를 거둔다.

## 붙여넣기 뒤의 알림 (`Paste Succeeded`·`Performed DropEffect`)

셸의 delete-on-paste 규약은 붙여 넣은 쪽이 원본 쪽 data object에 `Performed DropEffect`·`Paste Succeeded`를 써서 "옮겼으니 원본을 지워라"를 알리는 것이다. luil은 이 규약을 **어느 쪽으로도 쓰지 않는다.** 대신 아래 두 규칙으로 같은 결과를 얻는다.

- **luil 앱이 받는 쪽일 때** (탐색기에서 잘라 luil 앱에 붙임): 옮기기는 앱이 직접 한다(optimized move). 원본은 이미 옮겨졌으므로 원본 쪽이 지울 것이 없다. 옮긴 뒤 앱은 빈 목록의 `clipboard_files_copy_request`로 클립보드를 비운다 — 탐색기도 잘라내기를 붙여 넣으면 클립보드를 비우므로, 같은 항목이 다시 붙지 않는다.
- **luil 앱이 넣는 쪽일 때** (luil 앱에서 잘라 탐색기에 붙임): 탐색기가 옮기기를 직접 하고 클립보드를 비운다. 앱은 그 변화를 `clipboard_change`(`owned` 거짓)로 듣고 잘라 둔 표시를 거둔 뒤 목록을 다시 읽는다.

luil은 클립보드에 `IDataObject`를 올리지 않고(`OleSetClipboard`가 아니라 `SetClipboardData`) 형식 블록만 넣는다. 그래서 받는 쪽이 `Paste Succeeded`를 써도 받을 객체가 없다. 붙여 넣는 쪽이 원본 삭제를 원본 쪽에 맡기는 non-optimized move(다른 볼륨으로의 옮기기 등)를 하는 앱은 드물고, 그런 경우에도 원본은 남을 뿐 잃지 않는다.

## 지원하지 않는 플랫폼

Android는 파일 경로를 클립보드로 주고받는 관례가 없다(content URI를 쓴다). 넣기는 아무 일도 하지 않고, 읽기는 `supported` 거짓과 빈 목록으로 답한다. 답은 그래도 오므로 앱이 기다리다 멈추지 않는다. 클립보드 변화 알림은 보내지 않는다.

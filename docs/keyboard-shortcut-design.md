# 키보드 단축키: 이름 키·문자열 왕복·키 잡기

앱이 단축키를 받고, 저장하고, 사용자가 다시 정하게 하는 데 드는 것을 luil이 맡는다. 앱이 Win32 가상 키와 Android 키 코드를 따로 매핑하거나, "Ctrl+Shift+P" 같은 글과 키 이벤트를 손으로 바꾸거나, 단축키 편집 칸이 다음 키를 받으려고 창 메시지를 먼저 가로채지 않아도 된다.

공개 형식은 [ui_events.h](../include/luil/ui/ui_events.h)의 `key_code`, [commands.h](../include/luil/ui/commands.h), [ui_element.h](../include/luil/ui/ui_element.h)의 `key_capture_target`이다.

## 이름 키

`key_code`에 다음 이름을 더했다. 예전에는 플랫폼 대역(`first_platform_key` 위의 Win32 VK·Android AKEYCODE 값)으로 오던 키다.

| 묶음 | 이름 | 오는 때 |
| --- | --- | --- |
| Backspace | `backspace` | 언제나 (글자 U+0008이면서 누름 — Space와 같다) |
| 구두점 | `minus` `equal` `comma` `period` `slash` `semicolon` `quote` `bracket_left` `bracket_right` `backslash` `grave` | Ctrl·Alt(·Meta)와 함께일 때 |
| 키패드 | `numpad_0`…`numpad_9` `numpad_add` `numpad_subtract` `numpad_multiply` `numpad_divide` `numpad_decimal` | Ctrl·Alt(·Meta)와 함께일 때 |

- 구두점과 키패드는 글자를 만드는 키라 영문자와 같은 규칙이다. 그냥 치면 글자(`character_typed_event`)의 것이고, 단축키 수정자와 함께일 때만 키로 온다. 키 잡기 모드에서는 수정자 없이도 온다.
- 이름은 미국 배열의 자리다. 다른 배열에서 같은 자리의 키가 다른 글자를 만들어도 이름은 같다 — 단축키는 자리의 약속이다.
- NumLock이 꺼진 키패드는 지금처럼 화살표·Home 같은 이동 키로 온다. 키패드 Enter는 Enter다.
- 매핑은 `host/key_mapping`의 두 표(Win32·Android)가 맡는다. 플랫폼 header 없이 값으로 적어 두어 한 test가 두 표를 함께 잠근다.

### 바뀐 기존 동작

- 구두점·키패드 키를 플랫폼 대역(`platform_key_code(VK_OEM_MINUS)` 등)으로 받던 앱은 이제 이름 키를 받는다. 이름이 없는 키(Pause·SysRq·미디어 키)는 그대로 플랫폼 대역이다.
- Backspace가 수정자 없이도 키로 온다. 텍스트 칸에 초점이 있으면 controller가 키 쪽을 칸에서 끝낸다 — 지우기는 글자 경로(U+0008, Ctrl이면 U+007F)가 하고, 글을 지우는 동안 "뒤로" 같은 앱 단축키가 함께 실행되지 않는다. 텍스트 칸 밖의 Backspace는 앱 단축키로 흐른다.

## 문자열 왕복

`shortcut_label(chord)`와 `parse_shortcut(text)`는 같은 이름 표를 쓴다. 그래서 이름 있는 모든 키와 모든 수정자 조합에서 `parse_shortcut(shortcut_label(chord)) == chord`다.

- 글은 "Ctrl+Shift+P", "Ctrl+-", "Ctrl+=", "Alt+Num5", "Ctrl+\\" 꼴이다. 수정자는 언제나 Ctrl·Shift·Alt 차례로 적는다.
- 해석은 대소문자와 수정자 차례를 가리지 않고, "Control"·"Escape"·"Del"·"Return"도 받는다. 모르는 이름, 키가 없는 글, 같은 수정자를 두 번 쓴 글은 없다(nullopt).
- 해석은 수정자를 앞에서 "이름+" 꼴로 벗긴다. '+'로 쪼개면 "Num+"와 "Ctrl++"를 가를 수 없다.
- 플랫폼 대역의 키는 글이 없다. 그 값의 뜻이 플랫폼마다 달라 한 표로 읽으면 다른 키가 적힌다.

## 명령 하나에 여러 단축키

`app_command`는 기본 단축키(`shortcut`) 말고 추가 단축키(`extra_shortcuts`)와 사용자가 다시 정한 단축키(`user_shortcuts`)를 갖는다.

- 실제로 쓰는 단축키(`effective_shortcuts`)는 사용자 값이 있으면 그것뿐이고(빈 목록이면 단축키가 없다), 없으면 기본과 추가 단축키다. 비어 있는 것과 겹친 것은 뺀다.
- `match_command`는 실제로 쓰는 단축키 전부를 본다. 메뉴와 tooltip은 그 첫 단축키를 보인다.
- `find_conflicts`는 같은 범위에서 실제로 쓰는 단축키가 겹치는 명령 쌍을 그 단축키와 함께 돌려준다. 다른 범위(`focused`와 `window`)는 지금처럼 충돌이 아니다 — 초점 범위가 이기는 것이 뜻이다.
- 사용자 값을 어디에 저장할지는 앱의 몫이다. 글로 저장하면 위의 왕복이 맞는다.

## Tooltip 도우미

`label_with_shortcut(label, chord)`는 "닫기 (Ctrl+W)"를, `command_tooltip(command)`는 명령의 실제 첫 단축키를 붙인 글을 돌려준다. 단축키가 없으면 글 그대로다. 앱 화면 규칙의 "단축키는 tooltip 끝에 괄호로" 그대로다.

## 키 잡기 모드

`ui_element::set_key_capture(key_capture_target)`를 단 element가 초점을 가진 동안 **다음 키 입력은 모두 그 element의 것이다.**

- Tab·Esc·Enter·화살표·Alt 조합·앱 단축키보다 먼저 받는다. 초점 이동, 대화 상자의 Esc, 메뉴, 명령이 일어나지 않는다. 빠져나가는 길(포인터로 다른 곳 누르기, 취소 단추)은 앱이 둔다.
- 글자 키도 수정자 없이 키로 온다. 플랫폼이 발행된 상호작용의 `interaction_snapshot::key_capture`를 보고 바꾼다. 그 사이의 글자 입력은 버린다 — 같은 키가 키로 이미 갔다.
- 수정자 키 자체(Ctrl·Shift·Alt만 누름)는 키가 아니라 오지 않는다. 잡는 칸은 수정자를 떼기 전까지 기다리지 않고, 수정자와 함께 온 첫 키를 받는다.
- Windows에서 Alt를 떼면 드는 메뉴 모드(`SC_KEYMENU`)를 막고, Alt+F4·Alt+Space도 잡는 칸에 준다. 단축키를 고르다 창이 닫히거나 시스템 메뉴가 열리면 안 된다.
- **시스템 예약 키는 보여 주기만 한다.** `system_reserved_chord`가 Alt+F4·Alt+Tab·Alt+Esc·Ctrl+Esc·Alt+Space·Ctrl+Alt+Delete를 알아본다. 잡는 칸은 그것을 받아도 단축키로 정하지 말고 예약 키라고 보여 준다. 그 가운데 Alt+Tab·Ctrl+Alt+Delete·Windows 키 조합은 OS가 먼저 가져가 아예 오지 않는다.
- 표식은 초점이 바뀔 때만 달라진다. controller가 이벤트마다 한 곳에서 다시 센다.

## 검증

- `tests/keyboard_shortcut_tests.cpp`: 이름 있는 89개 키 × 수정자 8조합의 왕복, 구두점·키패드·별칭 해석과 거절, Win32·Android 표가 같은 이름으로 모임, 여러 단축키의 맞춤·사용자 재정의·충돌, tooltip 글, 예약 키, 키 잡기 칸이 Tab·Esc·단축키를 먼저 받고 글자를 버림, 잡기를 떠나면 끝남, 텍스트 칸의 Backspace가 앱 단축키로 새지 않음.
- `tests/surface_input_tests.cpp`: Win32 가상 키에서 구두점·키패드·Backspace의 매핑과 키 잡기 모드의 글자 키.
- 데모 "버튼과 컨트롤" 페이지의 "단축키 잡기" 칸.

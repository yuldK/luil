# 텍스트 입력

한 줄 입력은 [`text_input_element`](../../include/luil/ui/dialog_elements.h)가 사용하는 확정된 초안, 입력 초점, 배치 계산을 분리한다.

| 상태 | 소유자 |
| --- | --- |
| `text`, caret, 선택 | 앱 logic의 `text_edit_state` |
| `interaction_snapshot::focused_input` | input 스레드 |
| bounds, 가로 스크롤, 픽셀↔offset 변환 | `text_input_element` |

앱 logic은 편집 가능한 초안을 유지하고 `text_input_element`는 그 초안에서 만든 `text_input_view` 사본을 보관해 그린다. element를 다시 만들어도 logic의 초안이 유지되므로 입력을 이어 갈 수 있다. 초점은 input controller가 별도로 게시한다.

포인터 x를 UTF-8 byte offset으로 바꾸는 `offset_at()`은 배치에 사용한 글꼴과 측정기, 가로 스크롤을 동일하게 사용해야 한다. `text_measurer`만 주입받으므로 입력 controller도 픽셀 계산을 공유할 수 있다. `scroll_for()`는 caret이 보이는 최소 이동량을 순수 함수로 계산한다.

여러 줄을 그리는 커스텀 element는 `offset_at_point(x, y, measurer)`를 재정의해 줄 높이와 세로 스크롤까지 반영한 UTF-8 byte offset을 반환한다. 좌표는 element가 속한 창의 물리 픽셀이다. 클릭·드래그 선택·터치 탭·길게 누르기는 이 함수를 사용하며, 기본 구현은 y를 무시하고 기존 `offset_at(x, measurer)`를 호출한다.

여러 줄 붙여넣기를 허용하는 element는 `text_input()`이 반환하는 `text_input_snapshot::multiline`을 참으로 둔다. Ctrl+V·Shift+Insert와 Android 편집 메뉴는 이 값을 `clipboard_paste_request::multiline`로 전달한다. 직접 붙여넣기 요청을 만드는 앱도 이 플래그를 지정할 수 있다. Windows는 참일 때 줄바꿈과 탭을 보존하고, 기본값인 거짓일 때는 기존처럼 첫 줄바꿈이나 탭 앞까지만 넣는다. Android 클립보드 읽기는 기존처럼 원문을 보존한다. 이 플래그는 IME의 줄바꿈 처리나 기본 `text_input_element`의 그리기를 바꾸지 않는다.

```cpp
struct text_input_view
{
    std::u8string text;
    std::size_t caret, anchor;
    bool composing;
    std::u8string composition_text;
    std::size_t composition_caret, composing_begin, composing_end;
    std::uint64_t applied_sequence;
};
```

IME 조합 중에는 확정 `text`를 바꾸지 않고 화면에 표시할 전체 문자열을 `composition_text`에 둔다. element는 그 값을 그리고 `composing_begin`부터 `composing_end`까지 표시한다. 확정 시 앱은 한 번의 `replace_all`로 초안을 갱신하므로 조합 단계가 undo 기록에 쌓이지 않는다.

앱은 `make_text_input_view(state, composition, target)`로 확정 초안과 해당 target의 조합 표시를 합칠 수 있다. `text_input_view::displayed_text()`는 조합 중 화면에 보이는 전체 문자열을 반환한다. 점진 검색은 `search_query(view, any_match)`를 사용하면 표시 문자열이 아무 결과도 맞히지 못하는 조합 단계에서 비어 있지 않은 확정 문자열로 물러설 수 있다. 확정 문자열이 비어 있으면 표시 문자열을 그대로 쓴다.

## 여러 줄 칸

[`text_area_element`](../../include/luil/ui/text_area_element.h)는 줄 바꿈·세로 스크롤·여러 줄 붙여넣기를 받는 칸이다. 한 줄 칸과 같은 초안(`text_edit_state`), 같은 view(`text_input_view`), 같은 편집 명령을 쓴다. 앱은 `text_target_of`에 그 kind를 이름 대고 `apply_text_edit`로 적용하면 된다. `text_input()`이 `multiline`을 참으로 답하므로 붙여넣기는 줄 바꿈과 탭을 보존한다.

**자동 줄 넘김은 하지 않는다.** 줄은 줄 바꿈(`\n`)으로만 나뉘고, 긴 줄은 caret이 선 줄을 따라 칸 전체를 가로로 민다(한 줄 칸의 `scroll_for`와 같은 식). 이유는 배치 단계에 글꼴이 없다는 것이다(`arrange_context`). 넘겨 접으면 줄 수가 글자 폭에 달려, 배치에서 내용 높이와 세로 스크롤의 범위를 정할 수 없다. 줄 바꿈으로만 나누면 내용 높이는 `text_area_content_height(text)`로 글만의 함수가 된다. 자동 줄 넘김을 하려면 배치에 측정기를 넣는 결정이 먼저 필요하다 ([검토 중인 선택지](../text-area-wrap-design.md)).

키의 뜻은 `text_input_snapshot::multiline`을 보고 controller가 가른다. 한 줄 칸의 경로는 그대로다.

| 키 | 여러 줄 칸 |
| --- | --- |
| Enter, Shift+Enter | 줄 바꿈을 넣는다 (`insert "\n"`). 기본 버튼으로 가지 않는다 |
| Ctrl+Enter, Alt+Enter | 칸이 받지 않는다. 앱 단축키로 흐른다 |
| ↑, ↓ | 같은 가로 자리의 위아래 줄로 간다. 첫 줄의 ↑는 글의 처음, 마지막 줄의 ↓는 글의 끝이다 |
| Page Up, Page Down | 창에 드는 줄 수에서 한 줄을 남긴 만큼 간다 |
| Home, End | caret이 선 줄의 처음과 끝이다 |
| Ctrl+Home, Ctrl+End | 글의 처음과 끝이다 (`move_line_start`·`move_line_end`) |
| Tab | 칸을 떠난다. 탭 문자를 넣지 않는다 |

줄을 오가는 이동은 지금 caret의 자리(`text_span_bounds`)를 칸에 묻고, 위아래로 옮긴 자리를 포인터처럼 다시 묻는다(`offset_at_point`). 그래서 판정이 클릭과 한 길이다. 짧은 줄을 지나도 처음의 가로 자리를 기억하지는 않는다.

세로 스크롤은 앱 상태다(`scroll_offset`·`scroll`·`scroll_to`). 칸 안의 `scroll_area_element`가 막대·휠·범위 다듬기를 맡는다. caret 따라가기는 controller가 한다. 입력 pump는 새 tree를 받을 때마다 `follow_text_caret()`을 부르고, 초점을 가진 여러 줄 칸의 **caret이나 글이 바뀌었을 때만** caret 줄을 창 안으로 들이는 스크롤 메시지를 낸다. 편집은 logic이 적용한 뒤에야 caret 자리가 정해지므로 그 결과가 실린 tree에서 하는 것이다. 휠로 caret에서 멀리 흘린 frame은 caret도 글도 그대로라 되돌아가지 않는다.

IME 후보 창과 보조 기술의 글 자리는 `text_span_bounds`가 답한다. 구간이 시작하는 줄에서의 자리이고, 여러 줄에 걸치면 그 줄의 끝까지다. UIA Text pattern의 줄 단위(`TextUnit_Line`)는 아직 한 줄 칸과 같이 문서 전체로 물러선다.

Android에서는 IME에 넘기는 문서가 여러 줄 칸임을 함께 싣는다(`text_input_document::multiline`). 키보드는 `TYPE_TEXT_FLAG_MULTI_LINE`과 `IME_FLAG_NO_ENTER_ACTION`으로 띄운다. 그래서 Enter 자리가 확정 단추가 아니라 줄 바꿈 단추로 그려지고, IME가 넣은 줄 바꿈은 걷어 내지 않고 글로 둔다. 설정이 같아도 한 줄 칸과 여러 줄 칸 사이를 오가면 키보드를 다시 세운다. Enter 단추의 뜻이 바뀌기 때문이다. 실기기의 Gboard에서 소프트 키보드의 Enter로 줄이 나뉘는 것, 한 줄 칸으로 옮기면 Enter 자리가 다시 "다음" 단추가 되는 것을 확인했다.

## Android IME

IME의 `replace_all`에는 caret과 anchor 및 `text_edit_request::sequence`가 함께 온다. 앱은
`apply_text_edit`로 처리하고 `make_text_input_view`로 snapshot을 만들면 처리 번호가 자동으로
왕복한다. 편집이나 view를 직접 구현하는 앱은 거른 입력을 포함해 처리 완료 번호를
`text_edit_state::applied_sequence`에서 `text_input_view::applied_sequence`로 보존해야 한다.
IME는 최신 번호가 돌아오기 전의 중간 snapshot을 무시하므로 빠른 타이핑이 되돌아가지 않는다.

Android에서는 GameActivity가 주는 GameTextInput이 IME(`InputConnection`)를 맡는다. 앱 쪽 Java 코드는 없다. [`ime_session`](../../src/android/android_text_input.h)이 TSF의 shadow document와 같은 원리로 IME와 초점 칸을 잇는다.

- 초점이 텍스트 칸에 서면 그 칸의 확정 글과 선택을 IME에 넘기고 소프트 키보드를 띄운다. 떠나면 내린다. 사용자가 내린 키보드는 같은 칸을 다시 누르면 다시 뜬다.
- IME가 고친 상태가 오면 조합 범위가 있을 때는 `text_composition_event`로 화면에만 보이고, 조합이 없으면 `replace_all` 하나로 확정한다. 초점이 옮겨 가면 조합 중이던 글을 옛 칸에 확정한다.
- **앱 쪽 글이 바뀌었을 때만** IME에 넘긴다. IME가 고친 글이 logic을 거쳐 돌아오는 동안 앱의 글은 아직 옛것이라, 그 사이에 넘기면 IME가 되돌아간다. 돌아온 글이 IME의 것과 다르면(하드웨어 키, 붙여넣기, 앱의 입력 거르기) 그때 넘긴다.
- GameTextInput의 선택·조합 범위는 **UTF-16 코드 단위**이고 글은 Java의 modified UTF-8이다 (기기에서 `'사'` 3바이트에 선택 1로 확인했다). 둘 다 UTF-8 byte offset으로 옮긴다.
- 하드웨어 키보드의 글자도 IME가 먼저 받는다. Gboard 한국어 모드는 `a`·`b`를 두벌식 `ㅁ`·`ㅠ`로 조합해 IME 상태로 보내므로 키 경로와 겹치지 않는다. IME가 받지 않은 키만 키 이벤트로 온다.
- 키보드의 완료 단추(`IME_ACTION_DONE`)는 Enter 키로 보낸다. 가로 화면에서 전체 화면 편집기로 바뀌지 않게 한다 (`IME_FLAG_NO_EXTRACT_UI`). 이 값은 키보드를 띄울 때마다 정하고 IME 연결을 다시 세운다 — 연결은 창이 초점을 얻을 때 이미 만들어져 있다. Gboard 한국어 자판은 이때도 단추를 "↵"로 그리지만 누르면 완료 동작이 온다. 그래도 IME가 줄바꿈을 글로 넣으면 한 줄 칸은 그것을 걷어 내고 Enter로 보낸다. 여러 줄 칸은 그 줄바꿈이 곧 글이다 (위의 "여러 줄 칸").
- 키보드가 올라오면 그 높이를 안전 영역의 아래 가장자리에 더해 앱이 줄어든 크기로 다시 배치한다. 줄어든 frame이 오면 host가 `focus_reveal_event`를 보내고, controller가 `interaction_policy::on_focus_moved`로 초점 칸을 다시 드러낸다. 그래서 키보드가 칸을 가리지 않는다 (policy가 `route_reveal`을 답해야 한다).
- glue는 IME 상태가 바뀌어도 looper를 깨우지 않는다. host가 GameActivity의 IME 알림을 가로채 UI thread를 깨운다.

클립보드는 UI thread가 JNI로 `ClipboardManager`를 부른다 ([`android_clipboard.h`](../../src/android/android_clipboard.h)). 붙여넣기는 Windows처럼 그 칸의 `insert` 편집으로 앱에 보낸다. 키보드의 복사·붙여넣기·잘라내기 전용 키(`KEYCODE_COPY` 등)는 Ctrl+C·V·X로 옮긴다.

## Android 키보드 종류와 터치 편집

`text_input_config::input`으로 `text`, `number`, `decimal`, `phone`, `email`, `url` 키보드를 고른다. 숫자의 부호는 `signed_number`로 요청한다. 키보드에 주는 힌트이므로 붙여넣기와 하드웨어 키 입력의 검증은 앱이 맡는다. 암호 칸은 이 종류보다 암호 키보드가 우선한다.

`input.action`은 `done`, `search`, `go`, `send`, `next`, `previous`, `none`이다. `submit`이 있으면 소프트 키보드의 확정 단추에서 실행한다. 비어 있으면 앞의 네 동작은 기존 Enter 처리, `next`와 `previous`는 Tab과 Shift+Tab, `none`은 아무 동작도 하지 않는다. IME가 넣은 줄바꿈도 같은 경로로 온다. 종류가 바뀌어도 글과 선택은 유지하며, 조합 중의 종류 변경은 조합이 끝난 뒤 적용한다. Windows의 Enter 동작은 바꾸지 않는다.

Android host는 우클릭 동작이 없는 텍스트 칸을 길게 누르면 낱말을 고르고 C++ 편집 메뉴를 연다. 복사·잘라내기·붙여넣기·전체 선택이 있으며 텍스트 초점과 키보드는 유지한다. 조합 중에 열면 먼저 확정하고 전체를 선택한다. 전체 선택 후 메뉴가 남아 복사나 잘라내기를 이어서 할 수 있다. 암호 칸은 복사와 잘라내기가 비활성이고, 복사 실패 시 잘라내지도 않는다. 앱이 우클릭 동작을 정했으면 그 동작이 우선한다. 밖을 누르거나 뒤로 가기·Esc·초점 상실로 닫는다. 선택 범위를 끄는 손잡이는 아직 없다.

메뉴, 키보드 종류, 클립보드는 새 Java 소스 없이 기존 Android API를 C++ JNI로 호출한다. 후보 창을 caret에 붙이는 `CursorAnchorInfo`와 IME의 `requestCursorUpdates` 처리는 아직 제공하지 않는다. 현재 GameActivity의 InputConnection을 확장하는 Java 연결이 필요해 별도 항목으로 남긴다.

## 암호 칸

`text_input_config::secret`을 켠 칸이다. 글은 앱의 초안에 그대로 있고, 가리는 일은 element와 플랫폼 층이 한다.

| 자리 | 하는 일 |
| --- | --- |
| 그리기 | code point마다 점(●, U+25CF) 하나를 그린다. caret·선택·포인터 위치·후보 창 자리를 모두 점들 위에서 재고 원래 글의 offset으로 돌린다 |
| 보이기 | `reveal`을 켜면 글을 그대로 그린다. 앱이 칸 오른쪽 단추(`clear` 자리, 글리프는 앱이 고른다)로 켜고 끈다. 나머지 규칙은 그대로다 |
| 클립보드 | 복사·잘라내기(Ctrl+C·X, Shift+Delete, Ctrl+Insert, Android 키보드의 복사 키)를 하지 않는다. 보이기를 켜도 같다. 붙여넣기는 된다 — 암호 관리자에서 붙여 넣는다 |
| 낱말 단위 | Ctrl+←·→는 칸 끝으로, Ctrl+Backspace·Delete는 고른 구간 또는 그쪽 끝까지, 더블 클릭은 칸 전체다. 낱말 경계가 공백 자리를 알리기 때문이다 |
| Android 키보드 | `TYPE_TEXT_VARIATION_PASSWORD`와 `IME_FLAG_NO_PERSONALIZED_LEARNING`으로 띄워 키보드가 추천 줄에 글을 띄우지 않고 배우지 않는다. 키보드가 떠 있는 채로 일반 칸과 암호 칸 사이를 옮기면 키보드를 다시 세운다. 보이기는 다시 세우지 않는다 |
| Windows IME | TSF에 문서를 넘기지 않고(초점이 없을 때처럼 빈 document가 초점을 받는다) 창의 IME 문맥도 떼어 낸다 (`ImmAssociateContextEx`). 빈 document만으로는 한글 모드의 IME가 IMM 호환 층으로 자기 조합 창을 띄운다. 그래서 한글 모드여도 IME 창이 뜨지 않고 키는 영문 그대로 `WM_CHAR`로 칸에 간다. 칸을 떠나거나 창이 초점을 잃으면 기본 문맥으로 되돌려 사용자의 한/영 상태가 그대로다 |
| 보조 기술 | 값은 화면과 같은 점이고 UIA `IsPassword`가 참이다. Text pattern은 내주지 않는다 |

- 결합 문자도 점 하나로 세어, 점이 보이는 글자보다 많을 수 있다.
- IME는 진짜 글을 받는다. 편집하려면 있어야 한다.
- 화면 캡처 막기(`FLAG_SECURE`)는 창 전체의 일이라 앱이 정한다.
- 암호를 담은 문자열은 logic의 초안, snapshot, IME 사본으로 여러 thread를 지나므로 메모리에서 지운다고 보장하지 않는다.
- Android의 "비밀번호 표시"(입력한 글자를 잠깐 보이기) 설정은 따르지 않는다. 늘 가린다.

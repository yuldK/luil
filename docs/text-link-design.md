# 텍스트 칸의 하이퍼링크

여러 줄 칸(`text_area_element`)과 한 줄 칸(`text_input_element`)이 글 안의 링크를 그리고 연다. 앱이 구간과 주소를 주면 칸이 강조색과 밑줄로 그리고, 누르면 앱 메시지를 낸다. 주소를 실제로 여는 것은 앱이다. 공개 형식은 [text_document.h](../include/luil/ui/text_document.h)의 `text_link`·`detect_text_links`, 칸 설정의 `links`·`activate_link`·`link_hint`다.

```cpp
luil::text_area_config memo {};
memo.links = std::make_shared<const std::vector<luil::text_link>>(luil::detect_text_links(state.text));
memo.activate_link = [](std::u8string_view destination) { return luil::make_app_action(open_link_intent { std::u8string { destination } }); };
memo.link_hint = u8"Ctrl+클릭하여 열기";

// 앱의 메시지 처리
const luil::platform_result result { luil::open_url(intent.destination) };
```

## 링크는 칸이 아니라 앱의 글 위에 선다

링크는 `spans`와 같은 규칙의 구간이다. 확정 글 기준 UTF-8 byte offset이고, begin 순서이며 겹치지 않는다. 칸은 글이 링크인지 스스로 판단하지 않는다.

- 같은 글이라도 앱마다 링크로 볼 것이 다르다. 채팅은 주소를 찾고, 문서 보기는 원문의 링크 표기를 따르고, 코드 칸은 아무것도 링크로 보지 않는다.
- 보이는 글과 주소가 다를 수 있다(`text_link::destination`). 그래서 머무름 tooltip이 실제 주소를 보인다. 보이는 글만 믿고 누르면 다른 곳으로 가는 링크를 막는 장치다.
- 흔한 경우를 위해 `detect_text_links`가 글에서 `http(s)://`·`mailto:`·낱말 처음의 `www.`·메일 주소를 찾는다. 결과를 그대로 넘기면 된다.

### 주소 찾기의 끝

| 경우 | 규칙 | 이유 |
| --- | --- | --- |
| 공백·제어 문자·`<>"` | 주소가 끝난다 | 글 속 주소의 바깥 경계다 |
| ASCII 밖 글자 | 주소가 끝난다 | 한국어 조사가 주소에 붙어 오는 일이 흔하다 ("https://example.com에서"). 대신 ASCII 밖 글자가 든 IRI는 그 앞에서 끊긴다. 퍼센트 인코딩한 꼴은 끝까지 간다 |
| 끝의 `.,;:!?'*` | 뺀다 | 문장 부호다 |
| 끝의 `)`·`]`·`}` | 주소 안에 짝이 없을 때만 뺀다 | 위키 주소처럼 괄호를 품은 주소가 있다 |
| ASCII 영숫자·`.-_@/` 바로 뒤 | 시작하지 않는다 | "xhttp://", "a.www.b" 같은 낱말 한가운데다 |

`www.`는 `https://`를, 메일 주소는 `mailto:`를 붙인 주소가 된다. `https://user@host`의 `@`는 메일 주소로 다시 잡지 않는다.

## 누름의 임자

링크마다 칸의 자식 element(`text_link` kind)가 선다. 글자와 밑줄은 칸이 그리고, 자식은 그 위의 보이지 않는 자리다. hover·머무름 tooltip·포인터 모양·보조 기술의 Hyperlink가 이 자식의 몫이다. 마크다운 본문의 링크 자식과 같은 구조다.

문제는 편집 칸이다. 링크 자식이 누름을 받으면 링크 위에서 caret을 놓을 수도, 끌어 고를 수도 없다. 링크 안의 글을 고칠 길이 사라진다. 그래서 링크 자식에 `ui_element::inline_link` 표식을 달고 controller가 누름의 임자를 고른다.

| 칸 | 마우스·펜 | 터치 탭 | 길게 누름 |
| --- | --- | --- | --- |
| 편집 칸 | 그냥 누름은 칸(caret·끌어 고르기·낱말 고르기), Ctrl+누름은 링크 | 칸(caret) | 칸의 편집 메뉴 |
| 읽기 전용 칸 | 링크 | 링크 | 칸의 편집 메뉴 |

- 편집 칸의 Ctrl+클릭은 VS Code·Word의 관례다. 그냥 클릭으로 열면 링크 안에 caret을 놓기가 어렵다.
- 한 줄 칸은 읽기 전용이 없어 늘 편집 칸의 규칙이다.
- 터치에는 Ctrl이 없다. 편집 칸의 링크는 길게 눌러 편집 메뉴의 "링크 열기"로 연다(Android).
- 고르는 자리는 하나다(`press_target`). 마우스 누름·뗌, 터치 누름, 살아 있는 터치 대상 확인이 모두 이것을 지난다. 누름에서만 바꾸고 뗌에서 바꾸지 않으면 같은 대상 위의 누름과 뗌이 어긋나 클릭이 서지 않는다.

## Ctrl을 누르면 "지금 누르면 열린다"

편집 칸의 링크는 평소 글 모양(I자) 포인터다. Ctrl을 누른 채 올리면 손 모양과 굵은 밑줄로 바뀐다.

- controller가 마지막 마우스·펜 이동의 Ctrl 상태를 `interaction_snapshot::pointer_control`에 둔다. 링크 자식의 `cursor_at`과 칸의 그리기가 이것을 읽는다.
- 포인터를 움직이지 않고 Ctrl만 누르고 떼도 바뀌어야 한다. Windows 표면은 Ctrl의 누름·뗌(자동 반복 제외)에서 그 자리의 이동을 다시 알린다. 같은 자리라 tooltip 시계는 서지 않는다. 버튼을 누른 채이면(끄는 중) 알리지 않는다.
- 이동을 다시 알려도 `WM_SETCURSOR`는 포인터가 움직여야 온다. 그래서 `pointer_control`이 바뀐 발행본에서 포인터 밑 표면이 커서를 다시 맞춘다(`refresh_hover_cursor`). 마우스를 잡은 창, 크기 조절 테두리·캡션 위는 건드리지 않는다.

## 그리기

링크는 강조색 글자와 1 픽셀 밑줄이다. hover 중이고 지금 누르면 열리면(읽기 전용 칸, 또는 편집 칸에서 Ctrl을 누른 채) 밑줄이 2 픽셀이 된다.

- 굵기는 바꾸지 않는다. 굵기는 advance를 바꿔 줄 접기와 배치 cache(`text_area_wrap_cache`)의 열쇠에 들어간다. 색과 밑줄은 배치와 무관하다.
- 칸의 글을 다 그린 뒤 링크 구간만 잘라 강조색으로 다시 그린다. 검색 결과와 선택은 그 위에 덮는다. 굵은 구간(`spans`)이 링크 안에 있어도 굵기는 남는다.
- 링크 조각(hit 자리)은 배치가 그리기와 같은 줄·같은 자리 계산으로 놓는다. 창 밖으로 흘린 줄의 링크는 조각이 없어 hit하지 않는다. 접힌 링크는 줄마다 조각이 하나씩이다.
- 여러 줄 칸은 조합 중에도 링크를 조합 글만큼 옮겨 그린다(`spans`와 같은 길). 한 줄 칸은 조합 중에 링크 표시를 쉬고 확정되면 다시 선다.
- 암호 칸은 링크를 세우지 않는다. 보조 기술과 편집 메뉴에도 주소를 내지 않는다.

## 머무름 tooltip

링크 자식이 주소를 tooltip으로 단다. 편집 칸이면 `link_hint`를 ` · `로 이어 붙인다("https://example.com · Ctrl+클릭하여 열기"). 안내 글은 앱이 준다. 라이브러리의 다른 tooltip(`clear_tooltip` 등)처럼 언어를 앱이 정한다.

## 편집 메뉴의 링크 줄

caret이 링크 안이거나 링크 바로 뒤면(링크 끝 글자를 누르면 caret이 링크 뒤에 선다) `text_input_snapshot::link`가 그 주소를, `activate_link`가 여는 함수를 가리킨다. 고른 범위가 있으면 한 링크 안일 때만이다.

- 편집 메뉴(Android의 길게 누름)는 그때 "링크 열기"·"주소 복사" 줄을 하나 더 세운다. 여는 함수가 없으면 "주소 복사"만 선다.
- 읽기 전용 칸의 링크를 길게 눌러도 칸의 편집 메뉴다(`long_press_target`). 링크를 그냥 탭하면 열리므로, 주소를 복사할 길이 여기다.
- Windows에는 칸의 편집 메뉴가 없다. 앱이 우클릭 메뉴를 단다면 같은 snapshot을 읽으면 된다.

## 보조 기술

링크 자식은 `access_role::link`(UIA Hyperlink)이고 이름은 보이는 글, 값은 주소다. Invoke는 클릭과 같은 액션을 낸다. 링크는 Tab의 자리가 아니다 — 칸이 Tab의 자리이고, 긴 글의 링크마다 자리를 주면 Tab이 칸을 벗어나지 못한다.

마크다운 본문(`markdown_element`)의 링크는 다르다. 편집 칸이 아니라서 링크가 곧 누를 것이고, 열 수 있으면(`activate_link`가 있으면) Tab의 자리다. 초점 element의 Enter·Space가 클릭과 같은 길로 열고 초점 테는 tree가 두른다. 표시만 하는 링크는 자리가 없다.

## 한계

- 키보드로 편집 칸의 링크를 여는 키는 두지 않았다. Enter는 줄 바꿈이고 Ctrl+Enter·Alt+Enter는 앱 단축키 자리다([text-input.md](concepts/text-input.md)). 앱이 `text_input_snapshot::link`를 읽어 원하는 단축키에 묶는다.
- `detect_text_links`는 ASCII 주소만 끝까지 잡는다(위 표). 국제화 도메인·경로는 퍼센트 인코딩한 꼴이어야 한다.
- 편집 칸에서 Ctrl+누름 뒤 버튼보다 Ctrl을 먼저 떼면 열리지 않는다. 뗌의 임자가 칸으로 바뀌어 누름과 뗌이 다른 대상이 된다 — 끌다 놓은 것과 같은 취소로 본다.
- 링크 자식은 칸의 모든 링크마다 하나씩 선다. 링크가 수만 개인 글은 앱이 보이는 부분의 링크만 넘기는 편이 낫다.

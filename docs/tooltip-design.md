# 문자열·커스텀·마크다운 툴팁

`set_tooltip(text)`는 기존 한 줄 설명이다. `set_tooltip_content(tooltip_config)`는 UI tree를 내용으로 받으며,
`make_markdown_tooltip`은 마크다운을 측정·배치한 내용을 만든다. 앱이 열림 상태나 타이머를 따로 관리할 필요가 없다.

```cpp
button->set_tooltip(u8"변경을 저장합니다");
button->set_tooltip_content(luil::make_markdown_tooltip({
    .text = u8"### 변경 저장\n\n**수정한 내용**을 저장합니다.\n\n"
            u8"```cpp\nsave_document();\n```\n\n[자세히 보기](app:help)",
    .activate_link = [](std::u8string_view destination) -> luil::input_action {
        return luil::make_app_action(open_help_intent { std::u8string { destination } });
    },
}, 320.0f));
```

`open_help_intent`는 앱이 정의한 메시지다. 콜백은 input thread에서 액션만 만들며 URL을 직접 열거나 UI 상태를
고치지 않는다. 앱은 목적지를 보고 문서를 열거나 HTTP 링크를 시스템 브라우저에 보내는 등 자기 정책으로 처리한다.
콜백이 없으면 링크는 표시만 한다. [widgets 예제](../examples/widgets/controls_section.cpp)의 링크는 카운터를 더한다.

## 마크다운

MD4C 0.5.2로 CommonMark를 파싱한다. 제목·문단·강제 줄 바꿈·굵게·기울임·인라인 코드·코드 블록·목록·인용·
구분선·일반/참조/자동 링크와 취소선을 지원한다. 코드 블록은 고정폭 글꼴이며 언어별 구문 강조는 하지 않는다.
HTML은 글로 표시하고 이미지에는 대체 글을 사용한다. 스크립트를 실행하거나 외부 이미지 자원을 가져오지 않는다.

`markdown_element`는 툴팁 밖에서도 쓸 수 있다. `preferred_size`가 주어진 폭에서의 높이를 답하고 `arrange`가
줄 배치와 링크 영역을 확정한다. 링크 안에 강조가 섞여도 하나의 클릭 대상이며, 줄 바꿈 뒤의 실제 글 영역만 맞힌다.
`parse_markdown`은 렌더링 없이 블록·본문·서식·링크를 읽을 때 쓴다.

## 커스텀 내용과 모양

임의의 내용을 넣을 때는 `tooltip_config::content`에 독립 UI tree를 준다. 내용은 배율 **1**의 논리 좌표로 배치하고,
root의 `bounds()`를 내용 크기로 쓴다. 원점은 자유이며 같은 tree를 여러 요소·창·DPI에서 공유할 수 있다.
게시 후 내용도 불변이다. 글꼴이나 스타일 치수를 바꾸어 재측정할 때는 내용을 다시 짓는다.

내용에 `label_element`, `markdown_element`, `image_element`, `stack_element`, `panel_element` 등을 조립할 수 있다.
클릭 액션은 기존 `set_action`을 쓴다. tooltip은 클릭·커서·세로 휠을 처리하며 텍스트 편집이나 키보드 초점을 넘기지는 않는다.
편집기·키보드 탐색이 필요한 독립 판은 [ui_popup](popup-overlay-design.md)을 사용한다.

| `tooltip_config` | 기본값 | 뜻 |
| --- | --- | --- |
| `content` | 없음 | 논리 좌표로 배치한 내용 tree |
| `delay` | 500ms | 포인터 진입부터 표시까지의 시간 |
| `padding` | 8 | 내용 주위 여백 |
| `gap` | 3 | 대상과 상자 사이 거리, 위로 뒤집을 때 포인터와 상자 사이 거리 |
| `corner_radius` | 4 | 모서리 반지름 |
| `shadow` | 0.16 | 그림자 진하기, 0~1 |
| `background` | `tooltip_background` | 팔레트에서 고르는 배경색 |
| `border` | `tooltip_border` | 팔레트에서 고르는 테두리색 |

치수는 논리 픽셀이다. 음수·비정상 실수 치수는 0, 음수 지연은 즉시 표시로 다듬는다.
색 선택자는 그릴 때 호출하므로 밝음·어둠·고대비 전환을 따른다. `make_markdown_tooltip`의 세 번째 인자로 모양을 줄 수 있다.

## Hover와 입력 수명

- 유효한 커스텀 내용이 있으면 문자열 대신 그린다. `set_tooltip_content({})`로 지우면 문자열 표시로 돌아간다.
  빈 tree·배치하지 않은 root·숨은 root·크기가 없는 root는 없는 내용이다. 비활성 요소도 설명을 표시한다.
- 표시된 뒤에는 원래 요소, 툴팁 상자, 둘 사이의 좁은 간격을 건너도 열린 채로 유지한다. 이 영역을 모두 벗어나거나
  표면 이탈·활성 상실·Esc·다른 키 입력이 오면 닫는다. 끌기 중에도 표시하지 않는다.
- 포인터 hover로 뜬 툴팁은 **툴팁이 나타나는 순간의 포인터 자리**에 놓는다. x는 포인터 x이고 y는 커서 그림
  아래(포인터 y + 논리 20px)다. 자리는 hover가 시작될 때 기록하고, 지연이 끝나기 전에 같은 요소 안에서 움직이면
  마지막 자리로 갱신한다. 한 번 뜬 상자는 포인터를 따라 움직이지 않으며, 다른 요소에서 hover가 새로 시작되면 새
  자리에 다시 놓는다. 이 자리는 `interaction_snapshot::hover_point`에 실린다.
- 아래로 넘치면 커서를 가리지 않게 포인터 y보다 `gap`만큼 위로 뒤집는다. 상자는 표면 root 영역 안으로 민다.
- 포인터 자리가 없으면(키보드 초점·보조 기술·터치·캡션 버튼) 대상의 보이는 영역 아래에 놓고, 아래로 넘치면
  대상 위로 뒤집는다.
- 그리기·hit test·간격 건너기·커서는 모두 `ui_tree::place_tooltip`의 같은 답을 쓴다. 포인터 자리가 대상과 상자
  사이를 잇는 간격 안에 있으므로, 포인터를 곧장 상자로 옮겨 링크를 누를 수 있다.
- 표면보다 긴 내용은 세로 휠로 읽는다. 스크롤 범위는 내용 높이와 실제 내용 영역 높이로 계산한다.
- 툴팁 영역의 포인터·클릭·휠은 뒤 화면으로 새지 않는다. 같은 링크에서 눌렀다 떼야 실행하며, 누르는 동안
  대상이 없어지거나 링크 목적지가 바뀌거나 입력이 유실되면 실행하지 않는다. 같은 id의 대상은 새 frame에서도
  클릭을 이어간다. 마크다운 링크 id에는 목적지도 포함한다. 클릭 액션에는 내용 tree 기준 좌표가 전달된다.
- 표시 전에는 지연 끝 시각을 예약하고 표시 중에는 내용의 `next_update`도 예약한다. 숨은 내용은 타이머를 깨우지 않는다.
- 툴팁의 hover·눌림·스크롤은 `tooltip_interaction`으로 분리한다. 내용과 주 화면에 같은 id가 있어도 섞이지 않으며,
  `interaction_for_surface`가 다른 표면의 툴팁을 거른다. 키보드 초점과 접근 tree는 주 화면에 남는다.
- 보조 기술이 읽을 요약은 대상의 `set_tooltip`에 둔다. Windows UIA는 이 문자열을 `HelpText`로 제공한다.
  기존 접근성 이름의 우선순위는 바뀌지 않는다.

## 검증

[markdown_element_tests.cpp](../tests/markdown_element_tests.cpp)가 CommonMark·엔티티·참조 링크·강조가 섞인 링크,
줄 바꿈 hit test, 100%·150%·200% DPI의 간격 통과·링크 클릭·뒤 화면 격리·Esc·입력 취소·내용 교체·스크롤을 확인한다.
[tooltip_tests.cpp](../tests/tooltip_tests.cpp)는 모양·지연·애니메이션·경계 배치·잘라내기와 100%·150%·200%의
포인터 자리 배치·가장자리 밀기·뒤집기·포인터 자리 기록 규칙을,
[uia_provider_tests.cpp](../tests/uia_provider_tests.cpp)는 HelpText 요약을 검증한다.
MD4C 원본·버전·SHA-256은 [third_party/md4c/version.json](../third_party/md4c/version.json)에 고정하고 MIT 고지를 함께 설치한다.

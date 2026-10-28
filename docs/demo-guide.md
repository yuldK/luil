# UI 갤러리와 재사용 안내

luil_demo를 열면 기능 색인에서 각 예제로 바로 이동할 수 있다. 사이드바는 선택 강조와 스크롤이 있는 공용 목록이다.

## 실행

프로젝트 README의 예제 프리셋으로 빌드한 뒤 실행한다.

    build\vs2026-examples\examples\Debug\luil_demo.exe
    build\vs2026-examples\examples\Debug\luil_demo.exe --page=controls
    build\vs2026-examples\examples\Debug\luil_demo.exe --page=theme
    build\vs2026-examples\examples\Debug\luil_demo.exe --page=tabs
    build\vs2026-examples\examples\Debug\luil_demo.exe --page=windows --caption=bottom --presentation=layered
    build\vs2026-examples\examples\Debug\luil_demo.exe --page=theme --backdrop=acrylic

`--presentation=layered`는 주 창을 레이어드 표시로 띄운다([레이어드 표시](layered-presentation-design.md)).
`--caption=bottom`은 캡션을 창 아래에 두고 시작한다. 창 페이지의 끝 옮기기 단추(위로 키우기·위에서 줄이기·
왼쪽으로 키우기·왼쪽에서 줄이기)를 누르거나 위·왼쪽 테두리를 끌어, 표준 표시에서는 크기를 바꾼 직후 한 합성
동안 아래 캡션이 창 맨 위에 보이고 레이어드 표시에서는 그 frame이 없는 것을 견준다.

`--backdrop=none|mica|mica_alt|acrylic`은 주 창 배경 효과의 처음 값이다([창 배경 효과](backdrop-design.md)).
테마 페이지의 "창 배경 효과"에서 바꿀 수 있고, 그 아래 줄이 라이브러리가 알린 상태(섰는지, 서지 않았으면 까닭)를 보인다.
그 아래 "보조 창 · popup 배경 효과"는 견본 보조 창과 견본 메뉴(popup)에 효과를 고른다. 견본 창의 상태는 그 창 id의 보고로 따로 보이고, popup은 상태를 알리지 않는다.

## 바로 가져갈 공용 기능

| 기능 | 공개 API | 예제 |
| --- | --- | --- |
| 독립 토글 칩·스위치 | check_element, check_style::toggle_chip | [컨트롤](../examples/demo/gallery_page.cpp) |
| 시각적 테마 카드 | theme_picker_element | [모양과 색](../examples/demo/theme_page.cpp) |
| 시스템·카탈로그 강조색 | accent_picker_element | [모양과 색](../examples/demo/theme_page.cpp) |
| 마크다운 hover와 클릭 링크 | make_markdown_tooltip, markdown_config::activate_link | [컨트롤](../examples/demo/gallery_page.cpp) |
| 메뉴 팝업의 측정·배치 | make_menu_popup | [메뉴](../examples/demo/popups_page.cpp) |
| 명령과 단축키의 공통 정의 | app_command, match_command, menu_item_from | [탭](../examples/demo/tabs_page.cpp) |
| 앞·뒤 삽입으로 탭 이동 | tab_bar_config::move | [탭](../examples/demo/tabs_page.cpp) |
| 창 밖으로 분리·다시 합치기 | tab_bar_config::unclaimed_drop, receive, ui_window | [탭](../examples/demo/tabs_page.cpp) |
| popup 창의 모서리와 경계 고르기 (Windows 11 둥근 창) | ui_popup::corner, border, popup_corner_radius, menu_popup_config::corner | [메뉴](../examples/demo/popups_page.cpp) |
| 탭 모양·닫기 단추·상태 표시를 앱 스타일로 고르기 | ui_style::tab_bar, arrange_context::style | [탭](../examples/demo/tabs_page.cpp) |
| 견본 막대·가운데 버튼 닫기·줄이기·한 탭씩 넘기기·새 탭 꼬리 | tab_bar_config::preview, middle_click_close, min_tab_width, overflow_mode, trailing | [탭](../examples/demo/tabs_page.cpp) |

공개 헤더는 luil/ui/theme_picker_element.h, luil/ui/check_element.h, luil/app/menu_popup.h다.
소스 포함과 설치 패키지 모두 기존의 luil::luil 링크 대상으로 사용할 수 있다.

토글의 checked, 테마의 selected, 팝업의 열림 상태는 앱이 소유한다.
콜백에서는 input_action을 반환하고, 메시지를 받은 앱이 상태를 갱신한 뒤 tree를 다시 만든다.
컨트롤이 자체적으로 설정 파일을 쓰거나 URL을 실행하지 않는다.

### 테마 카드

    luil::theme_picker_config config {};
    config.owner = u8"appearance";
    config.selected = appearance.theme;
    config.accent_id = appearance.accent_id;
    config.select = [](const luil::theme_preference value) {
        return luil::make_app_action(theme_changed { value });
    };
    const float height { luil::theme_picker_element::height_for(config, content_width) };
    column->add(std::make_unique<luil::theme_picker_element>(std::move(config)), height);

height_for에는 요소가 실제로 받는 논리 폭을 준다. 여백과 스크롤 막대 자리를 먼저 뺀다.
카드는 card_width × preview_height(기본 112×70) 고정 크기라 폭이 넓어도 늘어나지 않고, 세 장이 들지 않으면
다음 줄로 넘어간다. labels와 name으로 표시 언어를 바꿀 수 있다.
강조색 선택기도 같은 방식으로 height_for와 selected, select를 연결한다.

### 메뉴 팝업

    luil::menu_config menu {};
    menu.owner = u8"document";
    menu.items = { { .key = u8"open", .label = u8"열기" } };
    menu.select = [](const std::u8string& key) {
        return luil::make_app_action(document_command { key });
    };

    luil::menu_popup_config popup {};
    popup.id = u8"document-menu";
    popup.x = pointer_x / scale;
    popup.y = pointer_y / scale;
    popup.scale = scale;
    popup.dismiss = [](const luil::popup_dismiss_reason) {
        return luil::make_app_action(close_document_menu {});
    };
    frame.popups.push_back(luil::make_menu_popup(std::move(menu), std::move(popup)));

width를 비우면 글·아이콘·단축키 열로 너비를 정한다. 높이와 분리 tree는 함수가 조립한다.
보조 창에서는 popup.anchor에 그 창의 표면 ID를 넣고 그 창의 배율을 쓴다.
메뉴 키보드 탐색에는 interaction_policy::menu도 연결한다. demo 셸의 구현을 참고한다.

## 조작 흐름

- 컨트롤: 독립 토글과 자동 저장 스위치, 서식 미리보기, 배타 선택, 슬라이더·진행률, 비활성 버튼, 링크가 있는 hover.
- 모양과 색: 밝게·시스템·어둡게 카드, 선택 체크 표시, 시스템 강조색과 색상 카탈로그.
- 메뉴: 표시 방식 변경, 검색과 IME 조합, 문서 미리보기, 즐겨찾기·잠금 체크 메뉴, 경로 복사, 삭제와 복원, popup 모서리(OS 기본·둥글게·작게 둥글게·직각)와 경계 끄기.
- 탭: 새 탭, 닫은 탭 복원, 앞·뒤 삽입, 이웃 이동, 다른 탭·오른쪽 탭·모든 탭 닫기, 넘침 목록, 가운데 버튼 닫기.
- 탭 모양: 기본·이어진 탭·평평한 칸, 닫기 단추를 선택·포인터에서만 보이기, 상태 표시를 아이콘 모서리나 닫기 단추 자리에 두기, 넘칠 때 한 탭씩 넘기기, 자리가 모자라면 줄이기. 세 모양의 견본 막대가 그림으로만 선다.
- 분리 창: 문서 하나를 별도 창으로 띄우고 원래 막대에 끌어 합친다. 창을 닫거나 복귀 버튼을 눌러도 원래 탭으로 돌아온다.
- 기본 입력: 창이 짧아져도 스크롤로 입력·이미지·확인 다이얼로그 예제에 접근할 수 있다.

Ctrl+Tab과 Ctrl+Shift+Tab은 탭 전환, Ctrl+T는 새 탭, Ctrl+W는 닫기,
Ctrl+Shift+T는 닫은 탭 복원이다. 일반 Tab과 Shift+Tab은 초점을 이동한다.
컨텍스트 메뉴가 있는 요소에서 Shift+F10을 누르면 오른쪽 클릭과 같은 메뉴가 열린다.

## 앱에 남는 코드

examples/demo/common의 page_layout은 제목·간격·내용 높이를 모아 공용 scroll_area_element로 감싸는 조립 예다.
프로젝트 고유의 메시지와 창·문서 상태는 demo에 남겼다. 필요한 페이지의 상태 처리와 build를 함께 참고한다.
목록·표·파일·이미지·HTTP·웹뷰 예제는 기능 색인에서 계속 접근할 수 있다.

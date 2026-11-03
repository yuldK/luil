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

창 페이지(`--page=windows`) 맨 아래의 "작업 표시줄" 구역은 주 창 단추에 진행률과 오버레이를 건다([작업 표시줄 진행률과 오버레이](taskbar-design.md)). 상태 단추 다섯으로 없음·불확정·정상·오류·일시정지를 고르고, 막대로 값을 정하며, 오버레이는 실행 중에 지은 작은 원이다. "독립 창 열기"는 작업 표시줄에 따로 서는 창을 띄우고 그 단추에 따로 진행률을 건다. 세 줄이 앱이 바라는 것과 셸에 닿은 정도를 글로 보인다: `작업 표시줄 능력`, `작업 표시줄 요청`, `작업 표시줄 반영`. 창을 최소화해도 단추에 막대가 남는지, 배율을 바꿔도 오버레이가 또렷한지는 사람이 실제 세션에서 본다.

창 페이지의 본문은 통째로 [흘리는 영역](scroll-area-design.md)에 든다. 구역을 모두 쌓으면 기본 창(1280 x 720)보다 길어, 휠이나 막대로 흘려야 맨 아래 점프 목록 줄에 닿는다. 영역 위에 고정한 `페이지 흘림: <흘린 양> / <흘릴 수 있는 최대>` 줄(논리 픽셀)이 위치를 글로 보인다. 구역이 줄을 더해도 영역의 내용 높이는 담긴 줄에서 읽으므로 그대로 흐른다.

창 페이지의 "둘째 인스턴스 인자" 줄은 같은 이름의 둘째 실행이 넘긴 명령 줄 인자와 작업 폴더를 보인다([둘째 인스턴스의 인자 넘기기](single-instance-args-design.md)). 데모를 띄워 둔 채 `luil_demo.exe --send-to-first a.txt "b c"`를 한 번 더 실행하면 둘째는 창을 띄우지 않고 플래그를 뺀 인자를 첫째에 보내고 끝난다. 종료 코드는 받았으면 0, 먼저 뜬 데모가 없으면 3, 그 밖의 실패는 4다. 첫째의 줄은 `둘째 인스턴스 인자: 2개 [a.txt] [b c] · 작업 폴더 <경로>`로 바뀌고 첫째 창이 앞으로 선다. 플래그가 없으면 둘째도 평소처럼 뜬다.

토스트 페이지(`--page=toasts`) 맨 아래의 "OS 알림" 구역은 앱 창 밖 Windows 알림 센터에 알림을 올린다([OS 알림](os-notification-design.md)). `OS 알림 보내기`는 id `demo-1`의 알림을 그림과 함께 올리고, `OS 알림 바꾸기`는 같은 id를 소리 없이 다른 본문으로 바꾸며, `OS 알림 거두기`는 그 알림을 거둔다. 세 줄이 UIA 이름으로 읽힌다: `OS 알림 상태: 사용 가능`, `OS 알림 결과: ok`, `OS 알림 사건: 없음`. 알림을 누르면 사건 줄이 `OS 알림 사건: 눌림 demo-1`로 바뀌고 데모가 앞으로 온다. 데모를 끈 뒤 알림 센터에서 누르면 데모가 새로 뜨고 `눌림 demo-1 · 꺼진 앱에서`다.
데모는 시작할 때마다 앱 식별 `luil.Demo`를 `HKCU\Software\Classes\AppUserModelId`와 활성화기 CLSID 아래에 쓴다. `luil_demo.exe --unregister-app-identity`는 창을 띄우지 않고 그 항목을 지운다. 종료 코드는 지웠으면 0, 없으면 3, 그 밖의 실패는 4다. 알림 센터에 남은 데모 알림은 그 전에 `OS 알림 거두기`로 거둔다.
모바일 데모의 기기 페이지 "알림" 구역은 알림 권한 상태(`알림 권한: ...`), `알림 권한 묻기`(권한이 없을 때)·`알림 보내기` 단추, `알림 결과`와 `알림으로 시작: 아니오`(꺼진 앱을 알림으로 띄웠으면 그 id) 줄을 보인다.

## 바로 가져갈 공용 기능

| 기능 | 공개 API | 예제 |
| --- | --- | --- |
| 독립 토글 칩·스위치 | check_element, check_style::toggle_chip | [컨트롤](../examples/demo/gallery_page.cpp) |
| 시각적 테마 카드 | theme_picker_element | [모양과 색](../examples/demo/theme_page.cpp) |
| 시스템·카탈로그 강조색 | accent_picker_element | [모양과 색](../examples/demo/theme_page.cpp) |
| 마크다운 hover와 클릭 링크 | make_markdown_tooltip, markdown_config::activate_link | [컨트롤](../examples/demo/gallery_page.cpp) |
| 메뉴 팝업의 측정·배치 | make_menu_popup | [메뉴](../examples/demo/popups_page.cpp) |
| 분할 버튼과 드롭다운 단추, 필드 아래에 놓고 위로 뒤집히는 메뉴 | split_button_element, menu_popup_config::below, place_field_popup | [메뉴](../examples/demo/popups_page.cpp) |
| 닫기 전까지 남는 정보 막대, 폭에서 높이를 재서 담기 | info_bar_element, info_bar_element::measure, ui_severity | [토스트](../examples/demo/toasts_page.cpp) |
| 확인 대화 상자, 크기는 내용에서 나오고 닫는 모든 계기가 한 결과를 낸다 | make_message_box, message_box_config, message_box_ok, modal_host_config::fit_height | [기본 입력](../examples/demo/basics_page.cpp) |
| 앱 창 밖 OS 알림 올리기·바꾸기·거두기와 눌림 사건 | show_notification, close_notification, app_delegate::make_notification_message, window_config::identity | [토스트](../examples/demo/toasts_page.cpp) |
| 명령과 단축키의 공통 정의 | app_command, match_command, menu_item_from | [탭](../examples/demo/tabs_page.cpp) |
| 앞·뒤 삽입으로 탭 이동 | tab_bar_config::move | [탭](../examples/demo/tabs_page.cpp) |
| 창 밖으로 분리·다시 합치기 | tab_bar_config::unclaimed_drop, receive, ui_window | [탭](../examples/demo/tabs_page.cpp) |
| popup 창의 모서리와 경계 고르기 (Windows 11 둥근 창) | ui_popup::corner, border, popup_corner_radius, menu_popup_config::corner | [메뉴](../examples/demo/popups_page.cpp) |
| 탭 모양·닫기 단추·상태 표시를 앱 스타일로 고르기 | ui_style::tab_bar, arrange_context::style | [탭](../examples/demo/tabs_page.cpp) |
| 견본 막대·가운데 버튼 닫기·줄이기·한 탭씩 넘기기·새 탭 꼬리 | tab_bar_config::preview, middle_click_close, min_tab_width, overflow_mode, trailing | [탭](../examples/demo/tabs_page.cpp) |
| 실행 중 요소 검사기(Ctrl+Shift+I)와 tree 글 | ui_frame::inspector, inspector_config, describe_element, dump_tree | [검사기](../examples/demo/inspector_page.cpp) |
| 읽기 전용 칸에 초점이 있어도 Ctrl+Z·Ctrl+Y가 앱 on_key에 닿는다 | text_area_config::read_only, interaction_policy::on_key | [기본 입력](../examples/demo/basics_page.cpp) |

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
다음 줄로 넘어간다. labels와 name으로 표시 언어를 바꿀 수 있다. 비면 기본 낱말이다.
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
- 별점: 컨트롤 페이지의 "별점" 카드가 정수 별, 반 별, 표시 전용 평균(4.3)을 한 줄씩 보인다. 별을 누르거나 끌고 ← →·Page·Home·End로 옮기며 같은 별을 다시 누르면 지운다. 마우스를 올리면 채워질 모양을 미리 보인다. 줄마다 "별점: 3 / 5" 꼴의 글이 값을 되읽게 하고 "별점 초기화" 단추가 두 조작 줄을 비운다. 모바일 기본 페이지는 칸 44의 별 다섯 한 줄과, 가로로 흐르는 창 안의 별 열 개 한 줄을 보인다. 창 안에서는 쓸면 창이 흐르고 잠깐 눌러 창이 흐르지 않게 된 뒤 옮기면 별이 따라온다.
- 확인 대화 상자: 기본 페이지의 "카운터 초기화"와 "긴 확인 대화"가 연다. 초기화 대화는 위험한 단추만 있어 취소에 초점이 서고 Enter와 Esc와 바깥 누름이 모두 취소의 결과로 닫는다. 긴 대화는 단추 셋과 확인 칸이 있고 마지막 긍정(저장)이 Enter의 대상이다. 좁은 화면에서는 단추가 위에서 아래로 쌓인다. 끌어서 옮기는 캡션 바는 없다. "대화"와 "마지막 결과" 두 줄이 열림과 결과 key를 글로 보인다.
- 읽기 전용 안내 칸: 칸을 눌러 초점을 두고 Ctrl+Z나 Ctrl+Y(Ctrl+Shift+Z)를 누르면 칸이 삼키지 않고 앱으로 넘겨, 바로 아래 `readonly-undo-keys` 글이 받은 횟수를 센다. 모바일 데모의 기본 페이지에도 같은 칸이 있고 하드웨어 키보드가 있어야 누를 수 있다.

Ctrl+Tab과 Ctrl+Shift+Tab은 탭 전환, Ctrl+T는 새 탭, Ctrl+W는 닫기,
Ctrl+Shift+T는 닫은 탭 복원이다. 일반 Tab과 Shift+Tab은 초점을 이동한다.
컨텍스트 메뉴가 있는 요소에서 Shift+F10을 누르면 오른쪽 클릭과 같은 메뉴가 열린다.

## 앱에 남는 코드

examples/demo/common의 page_layout은 제목·간격·내용 높이를 모아 공용 scroll_area_element로 감싸는 조립 예다.
프로젝트 고유의 메시지와 창·문서 상태는 demo에 남겼다. 필요한 페이지의 상태 처리와 build를 함께 참고한다.
목록·표·파일·이미지·HTTP·웹뷰 예제는 기능 색인에서 계속 접근할 수 있다.

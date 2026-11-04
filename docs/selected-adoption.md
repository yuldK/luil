# 선택 기능 이식 기록

원래 전체 작업은 `codex/fix-r5-review`의 `81243d643e5f117938a1caafd6264fc8e259c8b8`에 보존한다.
현재 이식은 `main`의 `49f46b46c38d923d7bda03c670bd944413953788`에서 시작해, 사용자가 선택한 아래 기능과 해당 보정만 새 변경으로 구성했다.
원래 238개 커밋 전체를 merge하는 작업이 아니다. 이식 전 전체 구현의 파일을 기준으로 필요한 부분을 다시 구성했다.

## main으로 가져온 항목

| 항목 | 기능 | 주요 근거 |
| --- | --- | --- |
| F01 | 앱 수준 실행 취소·다시 실행 | `include/luil/ui/undo_stack.h`, `undo_commands.h` |
| F02 | 두 번째 실행의 인자와 작업 폴더 전달 | `include/luil/app/platform_services.h` |
| F03 | 앱 창 밖 OS 알림과 눌림·활성화 | `include/luil/app/notification.h` |
| F04 | 필압 획·지우개 잉크 캔버스 | `include/luil/ui/ink_canvas_element.h` |
| F05 | 표준 확인 대화 상자 | `include/luil/ui/message_box.h` |
| F07 | 실행 화면의 요소 검사기 | `include/luil/ui/ui_inspect.h` |
| F08 | Windows 작업 표시줄 점프 목록 | `include/luil/app/taskbar.h` |
| F09 | 반별 단위 입력과 읽기 전용 별점 | `include/luil/ui/rating_element.h` |
| F10 | 그림·다중 형식 클립보드 | `include/luil/ui/clipboard_image.h` |
| F11 | 기본 문자 서식 편집 | `include/luil/ui/rich_text.h`, `rich_text_commands.h` |
| F12 | HTML 서식 교환 | `src/ui/rich_text_html.cpp` |
| F13 | 표의 앞쪽 열 고정 | `include/luil/ui/table_element.h` |
| F14 | Alt·F10·접근 키 메뉴 막대 | `include/luil/ui/menu_bar_element.h` |
| F15 | Windows 트레이 아이콘·주 창 숨김 | `include/luil/app/tray_icon.h` |
| F16 | 색 공간 계산·색 선택기·필드 popup | `include/luil/ui/color_picker_element.h` |
| F17 | Windows 전역 단축키 | `include/luil/app/global_hotkey.h` |
| F18 | 이전·다음 화면의 전환 | `include/luil/ui/page_transition_element.h` |
| F19 | 날짜 선택·달력 popup | `include/luil/ui/calendar_element.h` |
| F23 | 대상을 가리키는 안내 풍선 | `include/luil/ui/teaching_tip_element.h` |
| F25 | 공통 기본 문구·한국어/영어 문자열 표 | `include/luil/text/string_table.h`, `assets/strings/` |
| F26 | OS 표시 언어·주의 첫 요일 질의 | `include/luil/app/platform_services.h` |
| S01 | 기존 UI의 이름·키·초점·popup 보정 | 공통 입력·접근성·데모 시험 |
| S02 | Android JNI·클립보드·HTTP 오류 처리 | `tests/android_jni_order_tests.cpp` |

실행 취소 병합 뒤 용량 제한과 Android 알림 ID 충돌의 수정·회귀 테스트는 함께 가져왔다.
차트의 극소값 표시 수정은 아래 차트 구현과 함께 보존한다.

## 구현 완료 후 보존한 항목

아래 항목은 미구현 후보가 아니다. 작업과 시험은 보존 브랜치에 있으며, 이 main 구성의 공개 헤더·구현·CMake target·데모·관련 시험에는 넣지 않았다.

| 항목 | 기능 | 보존 브랜치의 진입점 |
| --- | --- | --- |
| F06 | 실행 중 스타일 JSON 읽기 | `include/luil/theme/ui_style_json.h` |
| F20 | 서명된 새 버전 확인·다운로드 | `include/luil/app/updater.h` |
| F21 | 받은 업데이트 적용·재시작·조건부 복구 | `src/win32/update_apply_win32.cpp` |
| F22 | XML 화면 마크업·바인딩 | `include/luil/ui/ui_markup.h` |
| F24 | 선택 차트 라이브러리 | `chart/` |
| F27 | 스타일·마크업 파일 핫 리로드 | `include/luil/app/hot_reload.h` |

다시 가져올 때는 이 커밋의 경로를 `git show codex/fix-r5-review:<경로>`로 확인하고, 필요한 변경과 시험을 현재 main에 맞게 재구성한다.
특히 핫 리로드의 현재 API는 스타일 JSON과 마크업 두 파서를 직접 참조한다. 차트의 문구 표는 F25 기반을 사용한다.
`81243d6`은 실행 취소·알림·차트의 수정을 함께 담았으므로 일부 기능을 이식할 때 해당 수정만 분리해야 한다.

## 공통 기반과 분리 방식

- R1의 파일 감시·구문 강조·줄 나눔·분할 버튼·정보 막대·스프링·회전·움직임 줄이기·작업 표시줄은 기존 main에 그대로 있다.
- 빌드 시점의 `LUIL_STYLE` JSON 생성은 기존 main 기능이다. 실행 시점 스타일 파서와 그 전용 스키마/시험 자산은 제외했다.
- 선택한 기본 문구·접근 이름·서식·입력 모델에 필요한 공통 필드는 유지했다. 공개 문구 조회와 생성기는 F25의 일부다.
- CMake·우산 헤더·요소 종류·데모 목록·공통 텍스트 대상에서 제외 기능의 연결을 걷었다. 모바일 페이지 목록은 항목 수를 추론해 빈 기본 항목이 남지 않게 했다.
- 두 데모는 선택한 기능을 시연하며, 테마·네트워크 페이지의 기존 기능은 유지한다. 제품 업데이트나 파일 기반 화면 구성을 흉내 내는 대체 기능은 넣지 않았다.

## 지원 경계와 확인

Windows 전용 셸 기능과 Android의 공통 UI를 구분한다. Android 그림 복사와 동등한 접근성 계층, 날짜의 시·분 선택, 문단 목록 편집은 이번 범위 밖이다.
실제 OS 알림 클릭·모든 펜 장치·제품 업데이트 적용까지 전수 확인한 뜻은 아니다. 사용자 클립보드를 지우는 시스템 시험은 실행하지 않는다.

## 검증 기록 (2026-11-05)

- Windows Debug·Release: 라이브러리, core·플랫폼 시험, 데스크톱·모바일 데모 빌드 성공.
- Windows core: 각 구성 1,924개 통과. 외부 Unicode 데이터셋이 없는 1개는 건너뛰었다.
- Windows 플랫폼·데모: 각 구성 572개 통과. 시스템 클립보드를 바꾸는 시험은 제외했다.
- 클립보드 이미지·HTML의 순수 변환 시험 6개, 81개 검증 통과. 시스템 클립보드는 바꾸지 않았다.
- Android arm64: 실제 라이브러리와 core 시험 빌드 성공. Samsung SM-S908N에서 2,000개 통과, 같은 Unicode 시험 1개 건너뜀.
- Debug·Release 설치 패키지를 별도 소비자 프로젝트에서 빌드·실행했다. 보존 기능의 공개 헤더와 차트 target을 설치하지 않음을 확인했다.
- 스타일 생성·우산 헤더·이식성·기본 문구·기본 문구 검사기·JNI 순서의 정적 검사 6개와 전체 소스 스타일 검사를 통과했다.
- UI Automation 격리 실행으로 잉크 견본 획, 검사기 윤곽 표시, 안내 풍선 열림을 확인했다. 테마·네트워크 화면에 제외 기능의 컨트롤이 남지 않음을 확인했다.

홈페이지 `luil-skill-tree`는 구현 상태와 main 반영 상태를 별도로 표시한다. 보존한 6개는 구현 완료 상태를 유지하며 보존 브랜치·커밋·경로를 근거로 제공한다. 이번 작업은 로컬 main 이식이며 저장소 push는 별도 요청 때 수행한다.

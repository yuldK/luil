# luil 문서

앱 통합, 공개 API의 동작 규칙, 내부 구조와 검증 방법을 안내한다.
전체 빌드 명령은 [프로젝트 README](../README.md)에 있다.

## 시작과 빌드

- [시작 가이드](how-to-start.md)
- [UI 갤러리와 재사용 안내](demo-guide.md)
- [Skia 빌드 준비](skia-build.md)
- [컴포넌트 안내](components.md)
- [공용 UI 보강](ui-foundation-design.md)
- [편집형 콤보](combo-box-design.md)
- [파일 대화상자](file-dialog-design.md)
- [명령표](popup-anchor-design.md)
- [공통 UI 텍스트 레이아웃](text-layout-design.md)
- [Codex 트랙 진행·검증](ui-gap-codex-status.md)

## 기본 개념

- [앱 메시지와 UI 명령](concepts/app-message.md)
- [자산 파이프라인](concepts/asset-pipeline.md)
- [빌드 체계](concepts/build-system.md)
- [소비자 계약](concepts/consumer-contract.md)
- [불변 UI tree](concepts/immutable-tree.md)
- [입력 pump](concepts/input-pump.md)
- [상호작용](concepts/interaction.md)
- [스레드 경계 messaging](concepts/messaging.md)
- [렌더링](concepts/rendering.md)
- [텍스트 편집](concepts/text-editing.md)
- [텍스트 입력](concepts/text-input.md)
- [테마와 글꼴](concepts/theming.md)
- [설치 글꼴 목록과 기본 UI 글꼴 API](concepts/fonts.md)
- [글꼴 굵기와 기울임](font-style-design.md)
- [스레드 모델](concepts/threading-model.md)
- [TSF와 IME 입력](concepts/tsf-input.md)
- [UI element](concepts/ui-element.md)
- [Win32 창과 표면](concepts/window.md)

## UI와 입력

- [Tree 배치 진입점과 진단](tree-arrange-design.md)
- [Stack, strip, wrap 배치](stack-expressiveness-design.md)
- [목록과 tree view](list-view-design.md)
- [스크롤 영역과 막대 합성](scroll-area-design.md)
- [확대·축소하는 보기](zoom-view-design.md)
- [가상 목록](virtual-list-design.md)
- [표](table-design.md)
- [고정 열](frozen-columns-design.md): 표의 앞쪽 열을 가로 스크롤에서 제자리에 세운다
- [여러 줄 칸의 자동 줄 넘김](text-area-wrap-design.md)
- [줄 나눔 규칙](line-break-design.md): UAX #14로 접는 자리를 고르고 한국어는 어절에서 바꾼다
- [텍스트 칸의 하이퍼링크](text-link-design.md)
- [구문 강조 도우미](syntax-highlight-design.md): 편집한 줄부터 렉서를 다시 돌려 색 구간을 잇는다
- [끄는 동안의 가장자리 자동 스크롤](drag-auto-scroll-design.md)
- [키보드 초점과 Tab 순회](keyboard-focus-design.md)
- [Focus group과 Tab 순서](focus-group-design.md)
- [초점 진입과 복귀](focus-entry-design.md)
- [키보드 초점 자동 스크롤](focus-reveal-design.md)
- [나중에 서는 초점과 창 활성화](deferred-focus-design.md)
- [커서 감추기와 멈춘 커서 자동 감추기](cursor-idle-design.md)
- [머무름 트리거와 hover 영역](hover-dwell-design.md)
- [여러 줄 라벨](label-wrap-design.md)
- [직접 그리는 글](text-block-design.md)
- [경로 조각 막대와 앞쪽 말줄임](breadcrumb-design.md)
- [가상 목록의 격자 모드와 가운데 맞춤 들이기](virtual-list-grid-design.md)
- [가지 단위 팔레트 덮어쓰기](palette-scope-design.md)
- [숫자 칸 검증](number-input-design.md)
- [장 하나 읽기·고품질 축소·큰 그림·축 맞춤](image-frames-scaling-design.md)
- [명령 팔레트](command-palette-design.md)
- [비밀 저장소](secret-store-design.md)
- [플랫폼 도구](platform-services-design.md)
- [Enter와 기본 버튼](enter-default-design.md)
- [키 조상 전파](key-propagation-design.md)
- [키보드 선택 규칙](keyboard-selection-design.md)
- [항목 모델 도우미](item-model-design.md)
- [키보드로 값 조절하기](value-step-design.md)
- [키보드 단축키: 이름 키·문자열 왕복·키 잡기](keyboard-shortcut-design.md)
- [대소문자 무시 글자 찾기](case-fold-design.md)
- [자석 슬라이더](slider-magnet-design.md)
- [활성 표면과 초점 수명](active-surface-design.md)
- [키 이벤트의 표면 라우팅](key-surface-routing-design.md)
- [터치 제스처와 펜 입력](touch-pen-input-design.md)
- [두 손가락 회전](rotate-gesture-design.md): 확대 보기가 두 접촉이 돌린 각도와 Shift+화살표를 알린다
- [Modal dialog host](modal-dialog-design.md)
- [시트와 서랍](sheet-design.md)
- [스프링](spring-design.md): 손을 뗀 속도에서 이어 목표까지 값을 움직인다
- [움직임 줄이기 연결](reduce-motion-design.md): 시스템 설정을 앱이 든 전환과 Android까지 잇는다
- [화면 전환](page-transition-design.md): 페이지를 바꿀 때 떠나는 화면과 새 화면을 함께 밀거나 옅게 하고 도는 동안 포인터를 막는다
- [문자열·커스텀·마크다운 툴팁](tooltip-design.md)
- [Caption 버튼 구성](caption-button-design.md)
- [탭 막대 스타일과 공용 스타일](tab-bar-style-design.md)
- [모바일 앱 바](app-bar-design.md)
- [분할 버튼](split-button-design.md): 기본 동작 본체와 메뉴 화살표, 필드 popup 열림 규약
- [메뉴 초점 고침](menu-focus-fixes-design.md): 묶음 안 Tab이 다음 자리로 가고, 열린 popup을 연 요소를 다시 누르면 닫힌다
- [메뉴 막대](menu-bar-design.md): 창 위 메뉴 줄을 Alt·F10·접근 키로 세우고 이웃 메뉴로 옮긴다
- [정보 막대](info-bar-design.md): 닫을 때까지 남는 상태 알림과 토스트가 함께 쓰는 심각도
- [라이브러리 기본 문구](default-text-design.md): 요소가 스스로 놓는 한국어 글을 한 헤더에 모으고 앱 글이 비었을 때만 쓴다
- [문자열 표](i18n-design.md): 언어마다 JSON 파일 하나를 빌드 때 헤더로 바꾸고 실행 중에 고른 언어의 문장을 찾는다
- [라이브러리 문구의 표 흡수와 지역 질의](i18n-adoption-design.md): 기본 문구·편집 메뉴·강조색 이름이 고른 언어를 따르고 앱이 OS의 첫 요일과 표시 언어를 읽는다
- [확인 대화 상자](message-box-design.md): 제목·본문·표준 단추를 한 요소로 짓고 좁은 화면에서 단추를 쌓는다
- [별점](rating-design.md): 별 단위·반 별 값과 보조 기술이 읽는 별 하나의 걸음
- [실행 취소 stack](undo-stack-design.md): 앱 수준 되돌리기 걸음과 실행 취소 명령표
- [실행 중 스타일 읽기](style-sheet-design.md): 스타일 JSON을 실행 중에 읽어 빌드 때와 같은 판정으로 받는다
- [줄 나눔 정책 스타일 키](wrap-policy-key-design.md): 스타일 파일 한 줄로 여러 줄 글의 기본 줄 나눔 정책을 고른다
- [잉크 캔버스](ink-design.md): 펜으로 필압 획을 그리고 지우개로 지운다. 획은 앱 상태다
- [잉크 입력](ink-input-design.md): 펜의 압력·기울기·지우개 표식과 합쳐 온 표본을 포인터 이벤트에 싣는다
- [색 고르기](color-picker-design.md): 판·막대·견본·16진 칸으로 색을 고르고 색 칸 popup으로 연다
- [색 공간과 16진 색](color-space-design.md): HSV와 sRGB 변환, 16진 글의 검사·쓰기·거르기
- [서식 있는 글 편집](rich-edit-design.md): 여러 줄 칸에서 굵게·기울임·밑줄·코드·링크를 편집하고 되돌린다
- [서식 글 교환](rich-edit-exchange-design.md): 글과 HTML을 한 클립에 넣고 거른 HTML을 서식째 붙여넣는다
- [검사기](inspector-design.md): 실행 중 화면에 요소의 윤곽과 정보 상자를 얹는 개발 도구
- [날짜 고르기](date-picker-design.md): 닫힌 날짜 칸의 달력 popup이나 인라인 달력에서 날짜 하나를 고른다
- [달력 날짜 계산과 문구](calendar-date-design.md): 42칸 격자, 달 이동의 말일 접기, 범위, ISO 날짜 글
- [안내 풍선](teaching-tip-design.md): 화면의 한 요소를 꼬리로 가리키며 제목·본문·행동 단추를 띄우는 상주 안내
- [화면 마크업](markup-design.md): 실행 중 읽은 엄한 XML 마크업 글을 불변 틀로 두고 frame마다 element tree로 펼친다
- [마크업 바인딩과 오류](markup-binding-design.md): 값·반복·조건·사건을 앱 상태에 잇고 오류를 줄·열과 자리로 알린다
- [핫 리로드](hot-reload-design.md): 스타일 JSON과 화면 마크업 파일을 저장하면 다시 읽어 결과를 앱에 건넨다
- [차트](chart-design.md): 계열을 선·막대·영역으로 그리고 포인터와 키로 점을 읽는다. 10만 점도 픽셀 열로 줄여 그린다
- [차트 라이브러리](chart-library-design.md): core 밖 선택 라이브러리 `luil::luil_chart`를 CMake 옵션으로 켜고 빌드·설치·검사·문구 표를 따로 둔다

## 플랫폼과 콘텐츠

- [Android 이식 계획](android-port-plan.md)
- [기기 요청과 플랫폼 능력](device-request-design.md)
- [Win32 window surface](win32-surface-design.md)
- [레이어드 표시: 창 배치와 frame을 한 합성에](layered-presentation-design.md)
- [창 배경 효과](backdrop-design.md)
- [여러 top-level window](multi-window-design.md)
- [둘째 인스턴스의 인자 넘기기](single-instance-args-design.md): 같은 이름의 두 번째 실행이 첫 실행에 명령 줄 인자와 작업 폴더를 넘긴다
- [작업 표시줄 진행률과 오버레이](taskbar-design.md): Win32 전용, 작업 표시줄 단추의 진행률과 오버레이 아이콘
- [작업 표시줄 점프 목록](jump-list-design.md): Win32 전용, 단추의 점프 목록에 앱이 작업을 싣는다
- [알림 영역 아이콘](tray-icon-design.md): Win32 전용, 트레이에 아이콘을 세우고 누름과 시스템 메뉴를 받는다
- [전역 단축키](global-hotkey-design.md): Win32 전용, 앱이 뒤에 있어도 받는 단축키를 걸고 걸린 결과를 알린다
- [OS 알림](os-notification-design.md): 앱 창 밖 알림을 올리고 바꾸고 거두며, 꺼진 앱도 눌림을 받는다
- [Popup overlay 표면](popup-overlay-design.md)
- [Popup 앵커 표면](popup-anchor-design.md)
- [Popup 안의 텍스트 입력과 IME](popup-ime-design.md)
- [OS 파일 drag & drop](os-dragdrop-design.md)
- [파일 목록 클립보드](file-clipboard-design.md)
- [그림 클립보드](image-clipboard-design.md): 글과 그림을 한 클립에 넣고 다른 앱이 넣은 그림을 읽는다
- [파일 변경 감시](file-watch-design.md): 폴더와 파일의 변경을 OS 알림으로 받아 묶음으로 건넨다
- [UI Automation 접근성](accessibility-design.md)
- [접근성 동작](accessibility-action-design.md)
- [이미지](image-design.md)
- [이미지 디코딩](image-decode-design.md)
- [이미지 인코딩](image-encode-design.md)
- [움직이는 이미지](image-anim-design.md)
- [SVG와 Lottie](vector-image-design.md) — `LUIL_ENABLE_SVG`·`LUIL_ENABLE_LOTTIE`
- [HTTP client](http-client-design.md)
- [자동 업데이트](auto-update-design.md): 서명한 매니페스트로 새 판을 확인하고 덩이로 나눠 받아 해시를 맞춘다
- [자동 업데이트 적용](auto-update-apply-design.md): 받은 새 판을 도우미로 띄워 같은 경로의 실행 파일을 바꾸고 실패하면 되돌린다. Android는 스토어를 연다
- [SMB client](smb-client-design.md) — Android 전용, `LUIL_ENABLE_SMB`
- [WebView2 composition hosting](webview-composition-design.md)
- [Raster 그리기 test](raster-test-design.md)

## 작업 계획

- [UI 공백 보강 구현 인계 (Codex 트랙)](ui-gap-handoff.md)

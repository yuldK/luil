# luil 문서

앱 통합, 공개 API의 동작 규칙, 내부 구조와 검증 방법을 안내한다.
전체 빌드 명령은 [프로젝트 README](../README.md)에 있다.

## 시작과 빌드

- [시작 가이드](how-to-start.md)
- [Skia 빌드 준비](skia-build.md)
- [컴포넌트 안내](components.md)
- [공용 UI 보강](ui-foundation-design.md)
- [편집형 콤보](combo-box-design.md)
- [파일 대화상자](file-dialog-design.md)
- [명령표](popup-anchor-design.md)
- [UI 텍스트 레이아웃 설계안](text-layout-design.md)
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
- [여러 줄 칸의 자동 줄 넘김](text-area-wrap-design.md)
- [끄는 동안의 가장자리 자동 스크롤](drag-auto-scroll-design.md)
- [키보드 초점과 Tab 순회](keyboard-focus-design.md)
- [Focus group과 Tab 순서](focus-group-design.md)
- [초점 진입과 복귀](focus-entry-design.md)
- [키보드 초점 자동 스크롤](focus-reveal-design.md)
- [Enter와 기본 버튼](enter-default-design.md)
- [키보드로 값 조절하기](value-step-design.md)
- [활성 표면과 초점 수명](active-surface-design.md)
- [키 이벤트의 표면 라우팅](key-surface-routing-design.md)
- [터치 제스처와 펜 입력](touch-pen-input-design.md)
- [Modal dialog host](modal-dialog-design.md)
- [시트와 서랍](sheet-design.md)
- [Caption 버튼 구성](caption-button-design.md)
- [모바일 앱 바](app-bar-design.md)

## 플랫폼과 콘텐츠

- [Android 이식 계획](android-port-plan.md)
- [기기 요청과 플랫폼 능력](device-request-design.md)
- [Win32 window surface](win32-surface-design.md)
- [여러 top-level window](multi-window-design.md)
- [Popup overlay 표면](popup-overlay-design.md)
- [Popup 앵커 표면](popup-anchor-design.md)
- [Popup 안의 텍스트 입력과 IME](popup-ime-design.md)
- [OS 파일 drag & drop](os-dragdrop-design.md)
- [파일 목록 클립보드](file-clipboard-design.md)
- [UI Automation 접근성](accessibility-design.md)
- [접근성 동작](accessibility-action-design.md)
- [이미지](image-design.md)
- [이미지 디코딩](image-decode-design.md)
- [이미지 인코딩](image-encode-design.md)
- [움직이는 이미지](image-anim-design.md)
- [SVG와 Lottie](vector-image-design.md) — `LUIL_ENABLE_SVG`·`LUIL_ENABLE_LOTTIE`
- [HTTP client](http-client-design.md)
- [SMB client](smb-client-design.md) — Android 전용, `LUIL_ENABLE_SMB`
- [WebView2 composition hosting](webview-composition-design.md)
- [Raster 그리기 test](raster-test-design.md)

# luil 문서

앱 통합, 공개 API의 동작 규칙, 내부 구조와 검증 방법을 안내한다.
전체 빌드 명령은 [프로젝트 README](../README.md)에 있다.

## 시작과 빌드

- [Skia 빌드 준비](skia-build.md)
- [컴포넌트 안내](components.md)

## 기본 개념

- [앱 메시지와 UI 명령](concepts/app-message.md)
- [자산 파이프라인](concepts/asset-pipeline.md)
- [빌드 체계](concepts/build-system.md)
- [불변 UI tree](concepts/immutable-tree.md)
- [입력 pump](concepts/input-pump.md)
- [상호작용](concepts/interaction.md)
- [스레드 경계 messaging](concepts/messaging.md)
- [렌더링](concepts/rendering.md)
- [텍스트 편집](concepts/text-editing.md)
- [텍스트 입력](concepts/text-input.md)
- [테마와 글꼴](concepts/theming.md)
- [스레드 모델](concepts/threading-model.md)
- [TSF와 IME 입력](concepts/tsf-input.md)
- [UI element](concepts/ui-element.md)
- [Win32 창과 표면](concepts/window.md)

## UI와 입력

- [Tree 배치 진입점과 진단](tree-arrange-design.md)
- [Stack, strip, wrap 배치](stack-expressiveness-design.md)
- [목록과 tree view](list-view-design.md)
- [키보드 초점과 Tab 순회](keyboard-focus-design.md)
- [Focus group과 Tab 순서](focus-group-design.md)
- [초점 진입과 복귀](focus-entry-design.md)
- [키보드 초점 자동 스크롤](focus-reveal-design.md)
- [Enter와 기본 버튼](enter-default-design.md)
- [키보드로 값 조절하기](value-step-design.md)
- [활성 표면과 초점 수명](active-surface-design.md)
- [키 이벤트의 표면 라우팅](key-surface-routing-design.md)
- [Modal dialog host](modal-dialog-design.md)
- [Caption 버튼 구성](caption-button-design.md)

## 플랫폼과 콘텐츠

- [Win32 window surface](win32-surface-design.md)
- [여러 top-level window](multi-window-design.md)
- [Popup overlay 표면](popup-overlay-design.md)
- [Popup 앵커 표면](popup-anchor-design.md)
- [Popup 안의 텍스트 입력과 IME](popup-ime-design.md)
- [OS 파일 drag & drop](os-dragdrop-design.md)
- [UI Automation 접근성](accessibility-design.md)
- [접근성 동작](accessibility-action-design.md)
- [이미지](image-design.md)
- [이미지 디코딩](image-decode-design.md)
- [움직이는 이미지](image-anim-design.md)
- [HTTP client](http-client-design.md)
- [WebView2 composition hosting](webview-composition-design.md)
- [Raster 그리기 test](raster-test-design.md)

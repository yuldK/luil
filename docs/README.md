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
- [텍스트 편집](concepts/text-editing.md)
- [텍스트 입력](concepts/text-input.md)
- [테마와 글꼴](concepts/theming.md)
- [스레드 모델](concepts/threading-model.md)
- [UI element](concepts/ui-element.md)

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

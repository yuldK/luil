# 컴포넌트 안내

기능에 맞는 공개 헤더와 사용 문서를 선택한다.
개별 헤더의 include 경로는 `luil/`로 시작한다.

| 용도 | API | 공개 헤더 | 문서 |
| --- | --- | --- | --- |
| 메시지 큐 | `messaging::channel` | [messaging/channel.h](../include/luil/messaging/channel.h) | [설명](concepts/messaging.md) |
| 최신 상태 게시 | `messaging::latest_slot` | [messaging/latest_slot.h](../include/luil/messaging/latest_slot.h) | [설명](concepts/messaging.md) |
| UTF-8 텍스트 편집 | `text::text_edit_state` | [text/text_edit.h](../include/luil/text/text_edit.h) | [설명](concepts/text-editing.md) |
| 테마·팔레트 | `color_theme`, `ui_color_palette` | [theme/ui_theme.h](../include/luil/theme/ui_theme.h) | [설명](concepts/theming.md) |
| 불변 화면 트리 | `ui_tree` | [ui/ui_tree.h](../include/luil/ui/ui_tree.h) | [설명](concepts/immutable-tree.md) |
| 포인터·키보드 입력 정책 | `interaction_policy` | [ui/ui_interaction.h](../include/luil/ui/ui_interaction.h) | [설명](concepts/interaction.md) |
| 텍스트 표시 | `label_element` | [ui/label_element.h](../include/luil/ui/label_element.h) | [설명](concepts/ui-element.md) |
| 아이콘 버튼 | `button_element` | [ui/button_element.h](../include/luil/ui/button_element.h) | [설명](concepts/ui-element.md) |
| 체크·라디오·스위치 | `check_element` | [ui/check_element.h](../include/luil/ui/check_element.h) | [설명](keyboard-focus-design.md) |
| 진행률 | `progress_element` | [ui/progress_element.h](../include/luil/ui/progress_element.h) | [설명](value-step-design.md) |
| 상태 배지 | `badge_element` | [ui/badge_element.h](../include/luil/ui/badge_element.h) | [설명](concepts/ui-element.md) |
| 값 조절 | `slider_element` | [ui/slider_element.h](../include/luil/ui/slider_element.h) | [설명](value-step-design.md) |

## UI 구성 규칙

- 앱 상태는 logic driver가 소유하고 element 액션은 변경 요청을 메시지로 반환한다.
- 게시한 tree의 구조는 유지한다. 다음 화면은 새 상태를 바탕으로 구성한다.
- 배치 설정에는 논리 픽셀을 사용하고 배율은 배치 문맥에서 적용한다.
- 기본 버튼 표시와 키보드 초점 표시는 구별한다.
- 입력·초점·접근성은 요소의 실제 가시성과 활성 상태를 따른다.

## 예제

- [컨트롤별 사용법](../examples/widgets)
- [통합 예제](../examples/demo)

# 컴포넌트 안내

기능에 맞는 공개 헤더와 사용 문서를 선택한다.
개별 헤더의 include 경로는 `luil/`로 시작한다.

| 용도 | API | 공개 헤더 | 문서 |
| --- | --- | --- | --- |
| 메시지 큐 | `messaging::channel` | [messaging/channel.h](../include/luil/messaging/channel.h) | [설명](concepts/messaging.md) |
| 최신 상태 게시 | `messaging::latest_slot` | [messaging/latest_slot.h](../include/luil/messaging/latest_slot.h) | [설명](concepts/messaging.md) |
| UTF-8 텍스트 편집 | `text::text_edit_state` | [text/text_edit.h](../include/luil/text/text_edit.h) | [설명](concepts/text-editing.md) |
| 테마·팔레트 | `color_theme`, `ui_color_palette` | [theme/ui_theme.h](../include/luil/theme/ui_theme.h) | [설명](concepts/theming.md) |

## 예제

- [통합 예제](../examples/demo)

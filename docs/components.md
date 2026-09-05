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
| 글자 버튼·텍스트 입력 | `text_button_element`, `text_input_element` | [ui/dialog_elements.h](../include/luil/ui/dialog_elements.h) | [설명](concepts/text-input.md) |
| 체크·라디오·스위치 | `check_element` | [ui/check_element.h](../include/luil/ui/check_element.h) | [설명](keyboard-focus-design.md) |
| 진행률 | `progress_element` | [ui/progress_element.h](../include/luil/ui/progress_element.h) | [설명](value-step-design.md) |
| 상태 배지 | `badge_element` | [ui/badge_element.h](../include/luil/ui/badge_element.h) | [설명](concepts/ui-element.md) |
| 값 조절 | `slider_element` | [ui/slider_element.h](../include/luil/ui/slider_element.h) | [설명](value-step-design.md) |
| 세로·가로 배치 | `stack_element` | [ui/stack_element.h](../include/luil/ui/stack_element.h) | [설명](stack-expressiveness-design.md) |
| 줄바꿈 배치 | `wrap_element` | [ui/wrap_element.h](../include/luil/ui/wrap_element.h) | [설명](stack-expressiveness-design.md) |
| 가로 스크롤 배치 | `strip_element` | [ui/strip_element.h](../include/luil/ui/strip_element.h) | [설명](stack-expressiveness-design.md) |
| 최상위 화면 | `root_element` | [ui/root_element.h](../include/luil/ui/root_element.h) | [설명](tree-arrange-design.md) |
| 배경과 자식 컨테이너 | `panel_element` | [ui/panel_element.h](../include/luil/ui/panel_element.h) | [설명](tree-arrange-design.md) |
| 스크롤 영역 | `scroll_view_element` | [ui/scroll_view_element.h](../include/luil/ui/scroll_view_element.h) | [설명](focus-reveal-design.md) |
| 스크롤 막대 | `scrollbar_element` | [ui/scrollbar_element.h](../include/luil/ui/scrollbar_element.h) | [설명](value-step-design.md) |
| 두 영역의 폭·높이 조절 | `split_handle_element` | [ui/split_handle_element.h](../include/luil/ui/split_handle_element.h) | [설명](stack-expressiveness-design.md) |
| 사이드바 | `sidebar_element` | [ui/sidebar_element.h](../include/luil/ui/sidebar_element.h) | [설명](concepts/ui-element.md) |
| 접이식 섹션 | `group_element` | [ui/group_element.h](../include/luil/ui/group_element.h) | [설명](tree-arrange-design.md) |
| 선택·계층·재정렬 목록 | `list_element` | [ui/list_element.h](../include/luil/ui/list_element.h) | [설명](list-view-design.md) |
| 그룹 머리행 목록 | `grouped_list_element` | [ui/grouped_list_element.h](../include/luil/ui/grouped_list_element.h) | [설명](list-view-design.md) |
| 탭 선택·닫기·재정렬 | `tab_bar_element` | [ui/tab_bar_element.h](../include/luil/ui/tab_bar_element.h) | [설명](focus-group-design.md) |
| 선택 묶음 | `choice_group_element` | [ui/choice_group_element.h](../include/luil/ui/choice_group_element.h) | [설명](focus-group-design.md) |
| 메뉴 | `menu_element` | [ui/menu_element.h](../include/luil/ui/menu_element.h) | — |
| 드롭다운 | `dropdown_element` | [ui/dropdown_element.h](../include/luil/ui/dropdown_element.h) | — |
| 모달 입력 범위와 배경 | `modal_host_element` | [ui/modal_host_element.h](../include/luil/ui/modal_host_element.h) | [설명](modal-dialog-design.md) |
| 창 캡션 | `caption_element` | [ui/caption_element.h](../include/luil/ui/caption_element.h) | [설명](caption-button-design.md) |
| 알림과 만료 | `toast_stack_element` | [ui/toast_element.h](../include/luil/ui/toast_element.h) | [설명](concepts/ui-element.md) |
| 정지·애니메이션 이미지 | `image_element` | [ui/image_element.h](../include/luil/ui/image_element.h) | [설명](image-design.md) |
| 이미지 파일·바이트 디코딩 | `load_image_file`, `decode_image_bytes`, `decode_animated_image_bytes` | [ui/image_decode.h](../include/luil/ui/image_decode.h) | [설명](image-decode-design.md) |
| Windows 창과 실행 | `win32::run_application_window` | [win32/win32_window.h](../include/luil/win32/win32_window.h) | [설명](concepts/window.md) |
| 렌더러 선택 | `renderer_mode` | [win32/renderer_policy.h](../include/luil/win32/renderer_policy.h) | [설명](concepts/rendering.md) |

## UI 구성 규칙

- 앱 상태는 logic driver가 소유하고 element 액션은 변경 요청을 메시지로 반환한다.
- 게시한 tree의 구조는 유지한다. 다음 화면은 새 상태를 바탕으로 구성한다.
- 배치 설정에는 논리 픽셀을 사용하고 배율은 배치 문맥에서 적용한다.
- 기본 버튼 표시와 키보드 초점 표시는 구별한다.
- 입력·초점·접근성은 요소의 실제 가시성과 활성 상태를 따른다.

## 예제

- [컨트롤별 사용법](../examples/widgets)
- [통합 예제](../examples/demo)

# 컴포넌트 안내

기능에 맞는 공개 헤더와 사용 문서를 선택한다.
개별 헤더의 include 경로는 `luil/`로 시작한다.

| 용도 | API | 공개 헤더 | 문서 |
| --- | --- | --- | --- |
| 메시지 큐 | `messaging::channel` | [messaging/channel.h](../include/luil/messaging/channel.h) | [설명](concepts/messaging.md) |
| 최신 상태 게시 | `messaging::latest_slot` | [messaging/latest_slot.h](../include/luil/messaging/latest_slot.h) | [설명](concepts/messaging.md) |
| UTF-8 텍스트 편집 | `text::text_edit_state` | [text/text_edit.h](../include/luil/text/text_edit.h) | [설명](concepts/text-editing.md) |
| 테마·팔레트 | `color_theme`, `ui_color_palette`, `compose_palette` | [theme/ui_theme.h](../include/luil/theme/ui_theme.h) | [설명](concepts/theming.md) |
| 앱 스타일(중립 색·tone·치수) | `ui_style`, `default_ui_style` | [theme/ui_style.h](../include/luil/theme/ui_style.h) | [설명](concepts/theming.md) |
| 스타일 JSON 실행 중 읽기 | `parse_ui_style`, `style_parse_result`, `describe` | [theme/ui_style_json.h](../include/luil/theme/ui_style_json.h) | [설명](style-sheet-design.md) |
| 불변 화면 트리 | `ui_tree` | [ui/ui_tree.h](../include/luil/ui/ui_tree.h) | [설명](concepts/immutable-tree.md) |
| 포인터·키보드 입력 정책 | `interaction_policy` | [ui/ui_interaction.h](../include/luil/ui/ui_interaction.h) | [설명](concepts/interaction.md) |
| 문자열·커스텀·마크다운 툴팁 | `set_tooltip`, `set_tooltip_content`, `make_markdown_tooltip` | [ui/ui_element.h](../include/luil/ui/ui_element.h) | [설명](tooltip-design.md) |
| 마크다운 본문·링크 | `markdown_element`, `parse_markdown` | [ui/markdown_element.h](../include/luil/ui/markdown_element.h) | [설명](tooltip-design.md) |
| 텍스트 표시 | `label_element` | [ui/label_element.h](../include/luil/ui/label_element.h) | [설명](concepts/ui-element.md) · [여러 줄](label-wrap-design.md) |
| 경로 조각 막대 | `breadcrumb_element` | [ui/breadcrumb_element.h](../include/luil/ui/breadcrumb_element.h) | [설명](breadcrumb-design.md) |
| 명령 팔레트 | `make_command_palette`, `filter_commands`, `make_command_palette_entry` | [ui/command_palette.h](../include/luil/ui/command_palette.h) | [설명](command-palette-design.md) |
| 아이콘 버튼 | `button_element` | [ui/button_element.h](../include/luil/ui/button_element.h) | [설명](concepts/ui-element.md) |
| 폰트 아이콘 표시 | `glyph_element` | [ui/glyph_element.h](../include/luil/ui/glyph_element.h) | [설명](concepts/fonts.md) |
| 폰트 파일·바이트 로딩 | `load_typeface_file`, `load_typeface_bytes` | [text/fonts.h](../include/luil/text/fonts.h) | [설명](concepts/fonts.md) |
| 글자 버튼·텍스트 입력 | `text_button_element`, `text_input_element` | [ui/dialog_elements.h](../include/luil/ui/dialog_elements.h) | [설명](concepts/text-input.md) · [숫자 칸](number-input-design.md) · [링크](text-link-design.md) |
| 직접 그리는 글(정렬·줄 바꿈·테두리) | `draw_text_block`, `measure_text_block`, `make_text_font` | [ui/text_block.h](../include/luil/ui/text_block.h) | [설명](text-block-design.md) |
| 텍스트 배치·caret·선택 질의 | `text_layout` | [ui/text_layout.h](../include/luil/ui/text_layout.h) | [설명](text-layout-design.md) |
| 여러 줄 텍스트 입력 | `text_area_element` | [ui/text_area_element.h](../include/luil/ui/text_area_element.h) | [설명](concepts/text-input.md) · [링크](text-link-design.md) |
| 구문 강조(편집에 맞춰 줄 단위로 다시 칠하는 구간) | `syntax_highlighter`, `syntax_line_lexer`, `text_span`, `text_color_role` | [ui/syntax_highlighter.h](../include/luil/ui/syntax_highlighter.h) · [ui/text_document.h](../include/luil/ui/text_document.h) | [설명](syntax-highlight-design.md) |
| 서식 있는 글 편집(굵게·기울임·밑줄·코드·링크, 서식 실행 취소) | `text::text_format`, `text::text_mark`, `rich_text_presenter`, `apply_rich_text_command`, `make_rich_text_commands`, `make_rich_text_key_handler` | [text/text_format.h](../include/luil/text/text_format.h) · [ui/rich_text.h](../include/luil/ui/rich_text.h) · [ui/rich_text_commands.h](../include/luil/ui/rich_text_commands.h) | [설명](rich-edit-design.md) |
| 서식 글 교환(복사·붙여넣기의 HTML 조각, 붙인 HTML 거르기, 저장 형식) | `rich_text_copy_request`, `rich_text_paste`, `rich_text_to_html`, `rich_text_from_html`, `clipboard_data_copy_request::html`, `text_area_config::clipboard_keys_to_parent` | [ui/rich_text.h](../include/luil/ui/rich_text.h) · [ui/ui_events.h](../include/luil/ui/ui_events.h) | [설명](rich-edit-exchange-design.md) |
| 두 글의 바뀐 구간 | `text::text_change_between` | [text/text_change.h](../include/luil/text/text_change.h) | [설명](syntax-highlight-design.md) |
| 줄 나눔 자리(UAX #14, 한국어 어절 우선) | `text::line_break_opportunities`, `text::line_break_policy`, `text_layout_request::line_break` | [text/line_break.h](../include/luil/text/line_break.h) | [설명](line-break-design.md) |
| 앱 전체의 여러 줄 정책(스타일 키 `text.wrapPolicy`, config가 비면 따른다) | `text_style::wrap_policy`, `measure_context::line_break_for`, `draw_context::wrap_policy` | [theme/ui_style.h](../include/luil/theme/ui_style.h) | [설명](wrap-policy-key-design.md) |
| 체크(삼상 포함)·라디오·스위치 | `check_element`, `aggregate_check` | [ui/check_element.h](../include/luil/ui/check_element.h) | [설명](keyboard-focus-design.md) |
| 불확정 진행 | `spinner_element` | [ui/spinner_element.h](../include/luil/ui/spinner_element.h) | [설명](concepts/ui-element.md) |
| 진행률 | `progress_element` | [ui/progress_element.h](../include/luil/ui/progress_element.h) | [설명](value-step-design.md) |
| 상태 배지 | `badge_element` | [ui/badge_element.h](../include/luil/ui/badge_element.h) | [설명](concepts/ui-element.md) |
| 값 조절 | `slider_element` | [ui/slider_element.h](../include/luil/ui/slider_element.h) | [설명](value-step-design.md) · [자석](slider-magnet-design.md) |
| 별점(별 몇 개로 점수 매기기, 같은 모양으로 평균 보이기) | `rating_element`, `rating_config`, `rating_precision` | [ui/rating_element.h](../include/luil/ui/rating_element.h) | [설명](rating-design.md) |
| 색 고르기(채도·명도 판, 색상·알파 막대, 견본, 16진 칸, 닫힌 색 칸과 popup) | `color_picker_element`, `color_field_element`, `make_color_picker_popup` | [ui/color_picker_element.h](../include/luil/ui/color_picker_element.h) · [app/color_picker_popup.h](../include/luil/app/color_picker_popup.h) | [설명](color-picker-design.md) |
| HSV 색과 16진 색 글(변환·검사·쓰기·거르기) | `hsv_color`, `color_from_hsv`, `hsv_from_color`, `check_hex_color`, `format_hex_color`, `hex_color_filter` | [ui/color_space.h](../include/luil/ui/color_space.h) | [설명](color-space-design.md) |
| 세로·가로 배치 | `stack_element` | [ui/stack_element.h](../include/luil/ui/stack_element.h) | [설명](stack-expressiveness-design.md) |
| 줄바꿈 배치 | `wrap_element` | [ui/wrap_element.h](../include/luil/ui/wrap_element.h) | [설명](stack-expressiveness-design.md) |
| 행·열 칸 배치 (폼) | `grid_element` | [ui/grid_element.h](../include/luil/ui/grid_element.h) | [설명](stack-expressiveness-design.md#grid) |
| 겹침 배치 | `layer_element` | [ui/layer_element.h](../include/luil/ui/layer_element.h) | [설명](stack-expressiveness-design.md#layer) |
| 가로 스크롤 배치 | `strip_element` | [ui/strip_element.h](../include/luil/ui/strip_element.h) | [설명](stack-expressiveness-design.md) |
| 최상위 화면 | `root_element` | [ui/root_element.h](../include/luil/ui/root_element.h) | [설명](tree-arrange-design.md) |
| 배경과 자식 컨테이너 | `panel_element` | [ui/panel_element.h](../include/luil/ui/panel_element.h) | [설명](tree-arrange-design.md) |
| 스크롤 영역·막대 합성 | `scroll_area_element` | [ui/scroll_area_element.h](../include/luil/ui/scroll_area_element.h) | [설명](scroll-area-design.md) |
| 스크롤 영역 | `scroll_view_element` | [ui/scroll_view_element.h](../include/luil/ui/scroll_view_element.h) | [설명](focus-reveal-design.md) |
| 스크롤 막대 | `scrollbar_element` | [ui/scrollbar_element.h](../include/luil/ui/scrollbar_element.h) | [설명](value-step-design.md) |
| 두 영역의 폭·높이 조절 | `split_handle_element` | [ui/split_handle_element.h](../include/luil/ui/split_handle_element.h) | [설명](stack-expressiveness-design.md) |
| 사이드바 | `sidebar_element` | [ui/sidebar_element.h](../include/luil/ui/sidebar_element.h) | [설명](concepts/ui-element.md) |
| 접이식 섹션 | `group_element` | [ui/group_element.h](../include/luil/ui/group_element.h) | [설명](tree-arrange-design.md) |
| 선택·계층·재정렬·컨텍스트 메뉴 목록 | `list_element` | [ui/list_element.h](../include/luil/ui/list_element.h) | [설명](list-view-design.md) |
| 그룹 머리행 목록 | `grouped_list_element` | [ui/grouped_list_element.h](../include/luil/ui/grouped_list_element.h) | [설명](list-view-design.md) |
| 아주 긴 목록·계층 (가상화) | `virtual_list_element` | [ui/virtual_list_element.h](../include/luil/ui/virtual_list_element.h) | [설명](virtual-list-design.md) |
| 열 머리글이 있는 표 (앞 열 고정 포함) | `table_element`, `table_config::frozen_columns` | [ui/table_element.h](../include/luil/ui/table_element.h) | [설명](table-design.md) · [고정 열](frozen-columns-design.md) |
| 여러 항목 선택 규칙(기준점·범위·토글) | `item_selection`, `selection_pick`, `selection_move` | [ui/selection_model.h](../include/luil/ui/selection_model.h) | [설명](keyboard-selection-design.md) |
| 탭 선택·닫기·재정렬·컨텍스트 메뉴 | `tab_bar_element` | [ui/tab_bar_element.h](../include/luil/ui/tab_bar_element.h) | [설명](focus-group-design.md) |
| 탭 모양·상태 표시·가운데 버튼 닫기·줄이기·한 탭씩 넘기기·견본 | `tab_bar_style`, `tab_indicator`, `tab_bar_preview` | [ui/tab_bar_element.h](../include/luil/ui/tab_bar_element.h) | [설명](tab-bar-style-design.md) |
| 시각적 테마·강조색 선택 | theme_picker_element, accent_picker_element | [ui/theme_picker_element.h](../include/luil/ui/theme_picker_element.h) | [사용 예](demo-guide.md) |
| 독립 토글 버튼·토글 칩 | check_style::toggle_button, check_style::toggle_chip | [ui/check_element.h](../include/luil/ui/check_element.h) | [사용 예](demo-guide.md) |
| 메뉴 팝업 조립(필드 아래 앵커 `below` 포함) | make_menu_popup | [app/menu_popup.h](../include/luil/app/menu_popup.h) | [사용 예](demo-guide.md) |
| 선택 묶음(라디오 행·트랙 위에 판이 뜨는 segmented, 맞춤·채움 폭) | `choice_group_element`, `choice_width` | [ui/choice_group_element.h](../include/luil/ui/choice_group_element.h) | [설명](focus-group-design.md) |
| 명령표·단축키 | `app_command`, `match_command` | [ui/commands.h](../include/luil/ui/commands.h) | [설명](popup-anchor-design.md) |
| 앱 수준 실행 취소·다시 실행 (되돌림 메시지 쌍, 합치기, 묶음, 저장 지점, 명령표) | `undo_stack`, `undo_macro`, `make_undo_commands` | [ui/undo_stack.h](../include/luil/ui/undo_stack.h), [ui/undo_commands.h](../include/luil/ui/undo_commands.h) | [설명](undo-stack-design.md) |
| 메뉴 | `menu_element` | [ui/menu_element.h](../include/luil/ui/menu_element.h) | [설명](popup-overlay-design.md) |
| 창 위 메뉴 막대(Alt·F10·접근 키, 이웃 메뉴로 옮기기) | `menu_bar_element`, `menu_bar_config`, `menu_bar_state`, `make_menu_bar_popup`, `menu_item_config::access_key` | [ui/menu_bar_element.h](../include/luil/ui/menu_bar_element.h) · [app/menu_popup.h](../include/luil/app/menu_popup.h) | [설명](menu-bar-design.md) |
| 회전 진행 표시 | `spinner_element` | [ui/spinner_element.h](../include/luil/ui/spinner_element.h) | [설명](ui-foundation-design.md) |
| 파일·폴더 선택 | `file_dialog_request` | [app/file_dialog.h](../include/luil/app/file_dialog.h) | [설명](file-dialog-design.md) |
| 편집형 콤보 | `combo_box_element` | [ui/combo_box_element.h](../include/luil/ui/combo_box_element.h) | [설명](combo-box-design.md) |
| 드롭다운 | `dropdown_element` | [ui/dropdown_element.h](../include/luil/ui/dropdown_element.h) | [설명](popup-overlay-design.md) |
| 분할 버튼(본체 동작과 메뉴 열기) | `split_button_element`, `split_button_config` | [ui/split_button_element.h](../include/luil/ui/split_button_element.h) | [설명](split-button-design.md) |
| 필드 아래 popup 자리·여는 키 | `place_field_popup`, `field_popup_key_handler` | [ui/field_popup.h](../include/luil/ui/field_popup.h) | [설명](split-button-design.md#필드-popup-열림-규약) |
| 모달 입력 범위와 배경 | `modal_host_element` | [ui/modal_host_element.h](../include/luil/ui/modal_host_element.h) | [설명](modal-dialog-design.md) |
| 확인 대화 상자(제목·본문·단추 행·확인 칸, 크기는 내용에서) | `make_message_box`, `message_box_config`, `message_box_ok` | [ui/message_box.h](../include/luil/ui/message_box.h) | [설명](message-box-design.md) |
| 아래 시트·옆 시트·서랍 | `sheet_host_element` | [ui/sheet_host_element.h](../include/luil/ui/sheet_host_element.h) | [설명](sheet-design.md) |
| 값 하나를 목표로 스프링처럼 움직이기 (놓은 속도에서 이어 출발) | `spring`, `spring_to`, `spring_release` | [ui/spring.h](../include/luil/ui/spring.h) | [설명](spring-design.md) |
| 창 캡션 | `caption_element` | [ui/caption_element.h](../include/luil/ui/caption_element.h) | [설명](caption-button-design.md) |
| 모바일 앱 바 | `app_bar_element` | [ui/app_bar_element.h](../include/luil/ui/app_bar_element.h) | [설명](app-bar-design.md) |
| 플랫폼 성질 | `ui_platform`, `current_ui_platform` | [ui/ui_platform.h](../include/luil/ui/ui_platform.h) | [설명](app-bar-design.md) |
| 알림과 만료 | `toast_stack_element` | [ui/toast_element.h](../include/luil/ui/toast_element.h) | [설명](concepts/ui-element.md) |
| 남아 있는 상태 알림(심각도 글리프·제목·줄이 바뀌는 본문·동작·닫기) | `info_bar_element`, `info_bar_config` | [ui/info_bar_element.h](../include/luil/ui/info_bar_element.h) | [설명](info-bar-design.md) |
| 알림 심각도(토스트·정보 막대 공용) | `ui_severity` | [ui/severity.h](../include/luil/ui/severity.h) | [설명](info-bar-design.md) |
| 정지·애니메이션 이미지 | `image_element` | [ui/image_element.h](../include/luil/ui/image_element.h) | [설명](image-design.md) |
| 펜 잉크 캔버스(필압 획 그리기·지우개·내보내기) | `ink_canvas_element`, `ink_layer`, `make_ink_stroke`, `simplify_ink_points`, `render_ink_image` | [ui/ink_canvas_element.h](../include/luil/ui/ink_canvas_element.h) · [ui/ink.h](../include/luil/ui/ink.h) | [설명](ink-design.md) · [입력](ink-input-design.md) |
| 실행 중 요소 검사기(윤곽·안쪽 여백·정보 상자·팔레트 견본, tree 평탄화와 글) | `ui_frame::inspector`, `inspector_config`, `inspect_nodes`, `inspect_at`, `pick_inspected`, `inspect_step`, `describe_element`, `dump_tree` | [ui/ui_inspect.h](../include/luil/ui/ui_inspect.h) | [설명](inspector-design.md) |
| 이미지 파일·바이트 디코딩 | `load_image_file`, `decode_image_bytes`, `decode_animated_image_bytes`, `read_image_frames_bytes`, `decode_image_frame_bytes`, `downscale_image` | [ui/image_decode.h](../include/luil/ui/image_decode.h) | [설명](image-decode-design.md) · [장 하나·축소](image-frames-scaling-design.md) |
| Windows 창과 실행 | `win32::run_application_window` | [win32/win32_window.h](../include/luil/win32/win32_window.h) | [설명](concepts/window.md) |
| 렌더러 선택 | `renderer_mode` | [app/renderer_policy.h](../include/luil/app/renderer_policy.h) | [설명](concepts/rendering.md) |
| 비동기 HTTP | `net::http_client` | [net/http_client.h](../include/luil/net/http_client.h) | [설명](http-client-design.md) |
| 파일 변경 감시 | `file_watcher` | [app/file_watcher.h](../include/luil/app/file_watcher.h) | [설명](file-watch-design.md) |
| 작업 표시줄 진행률과 오버레이 | `taskbar_request`, `ui_frame::taskbar`, `ui_window::taskbar` | [app/taskbar.h](../include/luil/app/taskbar.h) | [설명](taskbar-design.md) |
| OS 알림(Windows 토스트·Android 알림)과 눌림 사건, 앱 식별 | `show_notification`, `notification_status`, `app_delegate::make_notification_message`, `win32::window_config::identity` | [app/notification.h](../include/luil/app/notification.h) | [설명](os-notification-design.md) |
| 작업 표시줄 점프 목록의 작업 | `taskbar_request::jump_list`, `taskbar_jump_task`, `win32::window_delegate::make_jump_list_message` | [app/taskbar.h](../include/luil/app/taskbar.h) | [설명](jump-list-design.md) |
| 알림 영역(트레이) 아이콘과 그 사건·시스템 메뉴 | `tray_icon`, `ui_frame::tray`, `tray_icon::activate`, `win32::window_delegate::make_tray_message` | [app/tray_icon.h](../include/luil/app/tray_icon.h) | [설명](tray-icon-design.md) |
| 앱이 뒤에 있어도 받는 전역 단축키와 걸린 결과 | `global_hotkey`, `ui_frame::hotkeys`, `global_hotkey::pressed`, `win32::hotkey_report`, `win32::window_delegate::make_hotkey_message` | [app/global_hotkey.h](../include/luil/app/global_hotkey.h) | [설명](global-hotkey-design.md) |
| 둘째 인스턴스의 인자 넘기기 | `single_instance::send_to_primary`, `instance_arguments`, `window_delegate::on_instance_arguments` | [app/platform_services.h](../include/luil/app/platform_services.h) | [설명](single-instance-args-design.md) |
| 자동 업데이트의 확인·서명 확인·덩이 받기 | `updater`, `updater_config`, `update_key`, `update_status` | [app/updater.h](../include/luil/app/updater.h) | [설명](auto-update-design.md) |
| 자동 업데이트 적용(도우미로 실행 파일 바꾸기·되돌리기, Android 스토어 열기) | `updater::apply`, `update_apply_options`, `update_apply_result`, `platform_update_apply_mode`, `run_update_helper` | [app/updater.h](../include/luil/app/updater.h) | [설명](auto-update-apply-design.md) |
| 웹 콘텐츠 자리와 접근성 | `webview_element` | [ui/webview_element.h](../include/luil/ui/webview_element.h) | [설명](webview-composition-design.md) |
| 웹뷰 구성·정책·명령 | `ui_webview`, `webview_policy` | [app/webview.h](../include/luil/app/webview.h) | [설명](webview-composition-design.md) |

## UI 구성 규칙

- 앱 상태는 logic driver가 소유하고 element 액션은 변경 요청을 메시지로 반환한다.
- 게시한 tree의 구조는 유지한다. 다음 화면은 새 상태를 바탕으로 구성한다.
- 배치 설정에는 논리 픽셀을 사용하고 배율은 배치 문맥에서 적용한다.
- 기본 버튼 표시와 키보드 초점 표시는 구별한다.
- 입력·초점·접근성은 요소의 실제 가시성과 활성 상태를 따른다.

## 예제

- [최소 앱](../examples/hello)
- [컨트롤별 사용법](../examples/widgets)
- [통합 예제](../examples/demo)

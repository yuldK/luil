#pragma once

// 라이브러리 공개 API 전체를 한 번에 들이는 umbrella header다.
// 빌드 시간을 아끼려면 필요한 header만 골라 include해도 된다 —
// 어느 쪽이든 경로는 항상 `luil/` 접두로 시작한다.

#include "luil/version.h"

// 스레드 경계 채널이다.
#include "luil/messaging/channel.h"
#include "luil/messaging/envelope.h"
#include "luil/messaging/latest_slot.h"

// 도메인 무관 텍스트 규칙이다 (UTF-8 순회, 한 줄 편집 상태 기계).
#include "luil/text/text_edit.h"
#include "luil/text/utf8_text.h"

// 색·글꼴 선호 값과 팔레트 합성이다.
#include "luil/theme/appearance.h"
#include "luil/theme/ui_theme.h"

// view 무관 UI element 계층이다.
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/badge_element.h"
#include "luil/ui/button_element.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/caption_metrics.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/dropdown_element.h"
#include "luil/ui/group_element.h"
#include "luil/ui/grouped_list_element.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/list_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/modal_host_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/progress_element.h"
#include "luil/ui/root_element.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/sidebar_element.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/split_handle_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/strip_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/toast_element.h"
#include "luil/ui/transition.h"
#include "luil/ui/ui_cursor.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_element_id.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/webview_element.h"
#include "luil/ui/wrap_element.h"

// Win32 platform 조립이다 (창 실행, 앱 계약).
#include "luil/win32/app_host.h"
#include "luil/win32/renderer_policy.h"
#include "luil/win32/webview.h"
#include "luil/win32/win32_window.h"

// HTTP 요청·답이다 (값과 몸 판정, 그리고 WinHTTP 위의 비동기 client).
#include "luil/net/http_body.h"
#include "luil/net/http_client.h"
#include "luil/net/http_media_type.h"
#include "luil/net/http_message.h"

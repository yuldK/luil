#pragma once

// 옛 경로다. 웹뷰를 frame에 싣는 값은 플랫폼을 가리지 않으므로 `luil/app/webview.h`와
// 네임스페이스 `luil`로 옮겼다. 별칭의 뜻과 한계는 `luil/win32/app_host.h`와 같다.
#include "luil/app/webview.h"

namespace luil::win32 {
    using luil::ui_webview;
    using luil::webview_event;
    using luil::webview_event_kind;
    using luil::webview_policy;
} // namespace luil::win32

#pragma once

// 옛 경로다. host API는 플랫폼을 가리지 않으므로 `luil/app/app_host.h`와 네임스페이스
// `luil`로 옮겼다 (docs/android-port-plan.md 결정 1).
//
// 이 헤더는 한 판 동안 옛 이름을 그대로 쓰게 하는 다리다. 아래 별칭은 새 타입 **그 자체**를
// 가리키므로 `luil::win32::logic_driver`를 상속한 구동기, `luil::win32::ui_frame`을 만드는
// 코드가 고치지 않고 빌드된다.
//  - 깨지는 것은 `namespace luil::win32 { class logic_driver; }` 같은 전방 선언뿐이다.
//    별칭은 다시 선언할 수 없어서다. 새 경로의 헤더를 include하면 된다.
//  - 새 코드는 `luil/app/app_host.h`와 `luil::` 이름을 쓴다.
#include "luil/app/app_host.h"
#include "luil/win32/webview.h"

namespace luil::win32 {
    using luil::app_host;
    using luil::logic_driver;
    using luil::popup_dismiss_reason;
    using luil::ui_frame;
    using luil::ui_popup;
    using luil::ui_window;
    using luil::window_placement;
} // namespace luil::win32

#pragma once

#include "luil/ui/image_element.h"

#include <string>

namespace luil {
    // 작업 표시줄 단추의 진행 막대다 (docs/taskbar-design.md).
    enum class taskbar_progress_state
    {
        // 막대 없음 (기본).
        none,
        // 값을 모르는 진행이다.
        indeterminate,
        // `taskbar_progress::value`가 뜻을 갖는다.
        normal,
        // 같은 값, 빨간 막대다.
        error,
        // 같은 값, 노란 막대다.
        paused,
    };

    struct taskbar_progress
    {
        taskbar_progress_state state { taskbar_progress_state::none };
        // 0..1이다. `normal`·`error`·`paused`에서만 읽는다. 범위 밖은 자르고 NaN은 0이다.
        //  - 셸에는 1000단계로 내므로 그보다 작은 변화는 호출이 되지 않는다.
        float value { 0.0f };
    };

    struct taskbar_overlay
    {
        // 단추 모서리에 얹는 작은 그림이다. 비면 오버레이가 없다.
        //  - 같은 그림인지는 이미지의 몸통(`ui_image::backing`)으로 가른다. 앱은 같은 `ui_image` 복사본을 frame마다 실으면
        //    되고, 새로 지으면 새 그림이다.
        ui_image icon {};
        // 보조 기술이 읽는 글이다. 앱이 준다. 그림이 비면 읽지 않는다.
        std::u8string description {};
    };

    // 작업 표시줄 단추 하나의 바라는 상태다. 기본값이면 셸을 만지지 않는다.
    //  - 앱 상태다. 매 frame 지금 바라는 값을 싣고, UI thread가 바뀐 것만 셸에 옮긴다. 마지막 frame의 값이 이긴다.
    //    다른 thread가 바꾸려면 앱 메시지(`app_host::post_app_message`)로 상태를 바꾼다.
    //  - 주 창은 `ui_frame::taskbar`, 독립 창(`ui_window::independent`)은 `ui_window::taskbar`다. 소유된 창은 단추가
    //    없어 읽지 않는다.
    //  - 하지 못하는 플랫폼은 받고 무시한다. 무엇을 하는지는 `ui_platform::capabilities::taskbar`가 말한다.
    struct taskbar_request
    {
        taskbar_progress progress {};
        taskbar_overlay overlay {};
    };
}

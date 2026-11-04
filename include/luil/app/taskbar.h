#pragma once

#include "luil/ui/image_element.h"

#include <optional>
#include <string>
#include <vector>

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

    // 점프 목록의 작업 하나다 (docs/jump-list-design.md). 누르면 셸이 이 앱의 실행 파일을 `arguments`로 새로 띄운다.
    //  - 앱이 떠 있으면 새 프로세스가 둘째다. 앱이 `single_instance::send_to_primary`로 첫째에 넘긴다. 꺼져 있으면 새
    //    프로세스가 첫째라 명령 줄로 같은 인자를 읽는다. 라이브러리는 인자를 해석하지 않는다.
    struct taskbar_jump_task
    {
        // 목록에 보이는 글이다. 비면 안 된다.
        std::u8string title {};
        // 실행 파일 뒤에 붙일 인자다. 따옴표는 라이브러리가 붙인다 (argv 경계가 그대로 산다). 작업 폴더는 정하지 않는다.
        std::vector<std::u8string> arguments {};
        // 마우스를 올리면 보이는 설명이다. 비면 없다.
        std::u8string description {};
        // 아이콘 파일(.ico·.exe·.dll)의 절대 경로와 그 안의 번호다. 경로가 비면 이 실행 파일의 `icon_index`번이다.
        std::u8string icon_path {};
        int icon_index { 0 };

        [[nodiscard]] bool operator==(const taskbar_jump_task&) const = default;
    };

    // 작업 표시줄 단추의 점프 목록이다. 창이 아니라 앱 식별(AUMID)에 묶이고, 셸이 사용자 프로필에 적어 앱이 꺼진 뒤에도 남는다.
    struct taskbar_jump_list
    {
        // 셸의 "작업" 범주에 위에서 아래로 선다. 비면 이 앱의 목록을 지운다.
        std::vector<taskbar_jump_task> tasks {};

        [[nodiscard]] bool operator==(const taskbar_jump_list&) const = default;
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
        // 점프 목록이다. 비어 있으면 셸의 목록을 만지지 않는다 (기본, 앞 실행이 지은 목록도 그대로다). 값이 있으면 마지막으로
        // 낸 것과 다를 때만 그 목록으로 다시 짓고, 작업이 없으면 지운다.
        //  - 주 창(`ui_frame::taskbar`)의 것만 읽는다. 목록은 앱마다 하나라 독립 창(`ui_window::taskbar`)의 것은 읽지 않는다.
        //  - 단추와 무관하다. 주 창이 숨은 동안에도 내고, 단추가 다시 서도 다시 짓지 않는다. 앱이 끝나도 지우지 않는다.
        //  - 결과는 Win32의 `jump_list_report`가 말한다. 진행률·오버레이의 `taskbar_report`에는 섞이지 않는다.
        std::optional<taskbar_jump_list> jump_list {};
    };
}

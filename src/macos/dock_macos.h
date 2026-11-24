#pragma once

#include "luil/app/taskbar.h"
#include "luil/win32/win32_window.h"

#include <functional>
#include <memory>

#ifdef __OBJC__
@class NSMenu;
#else
class NSMenu;
#endif

namespace luil::macos {
    // 작업 표시줄 요청(`taskbar_request`)의 macOS 갈래다 (taskbar-design.md, jump-list-design.md). 앱의 Dock 타일 하나에 옮긴다.
    //  - 진행률과 오버레이는 `NSApp.dockTile`의 content view가 그린다. 앱 아이콘 위에 아래쪽 막대(정상은 강조색, 오류는 빨강,
    //    일시정지는 노랑, 불확정은 빗금 친 꽉 찬 막대)와 오른쪽 아래 오버레이 그림이다. 요청이 기본값이면 view를 떼어 평소
    //    아이콘으로 돌아간다. 오버레이 설명은 view의 접근성 이름으로만 싣는다 (Dock이 읽지는 않는다). 배지 글은 쓰지 않는다.
    //  - 타일은 앱마다 하나다. host는 주 창의 요청(`ui_frame::taskbar`)만 넘긴다. 독립 창의 요청(`ui_window::taskbar`)은 받고
    //    무시한다.
    //  - 점프 목록은 Dock 메뉴(`applicationDockMenu:`)다. 작업마다 항목 하나이고 설명은 툴팁이다. 아이콘 칸은 읽지 않는다.
    //    항목을 고르면 이 실행 파일을 작업의 인자로 새로 띄운다 (번들이면 LaunchServices로 새 인스턴스, 아니면 실행 파일을
    //    바로). Windows에서 셸이 실행 파일을 띄우는 것과 같고, 떠 있는 첫째에 넘기는 일은 앱의 `single_instance`가 한다.
    //  - Dock 메뉴는 앱이 떠 있는 동안만 있다. Windows처럼 앱이 끝난 뒤에 남지 않으므로 시작한 앱이 첫 frame에서 다시 싣는다.
    //  - 어떤 호출을 할지는 core의 `taskbar_sync`·`jump_list_sync`가 고른다. 타일은 늘 있으므로 `waiting`이 없다.
    //  - 모두 메인 thread에서 부른다. 보고 함수도 그 자리에서 곧바로 불린다.
    class dock_host
    {
    public:
        dock_host();
        dock_host(const dock_host&) = delete;
        dock_host(dock_host&&) = delete;
        dock_host& operator=(const dock_host&) = delete;
        dock_host& operator=(dock_host&&) = delete;
        // 걸어 둔 content view를 떼어 평소 아이콘으로 돌린다.
        ~dock_host();

        // 진행률·오버레이의 보고다. 표면은 늘 주 창(빈 id)이고 상태는 `off`·`applied`·`failed`다. 바뀔 때만 불린다.
        void set_report(std::function<void(const win32::taskbar_report&)> report);
        // 점프 목록의 보고다. 목록 요청이 마지막으로 낸 것과 달라질 때만 불린다 (`applied`·`removed`·`failed`).
        void set_jump_list_report(std::function<void(const win32::jump_list_report&)> report);

        // 주 창의 바라는 상태를 마지막으로 낸 것과 견줘 바뀐 것만 타일과 메뉴에 옮긴다. frame이 바뀔 때마다 부른다.
        void update(const taskbar_request& wanted);

        // `applicationDockMenu:`가 돌려줄 메뉴다. 작업이 없으면 nil이다. 목록이 바뀌면 새 메뉴다.
        [[nodiscard]] NSMenu* dock_menu() const noexcept;

    private:
        struct state;
        std::unique_ptr<state> state_;
    };
}

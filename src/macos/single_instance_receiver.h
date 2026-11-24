#pragma once

#include "luil/app/platform_services.h"

#include <functional>

// 첫째 인스턴스가 둘째의 인자를 받는 자리다 (single-instance-args-design.md의 macOS). 앱 host가 단다.
//  - 둘째(`send_to_primary`)는 사용자 임시 폴더의 Unix 소켓 `luil-instance-<이름의 해시>.sock`에 인자를 보내고 답 한 바이트를 기다린다.
//  - 첫째는 배경 thread 하나가 소켓을 받아 읽고 모양을 본 뒤, 받는 함수를 **메인 thread**(dispatch main queue)에서 부른다.
//    그 답이 둘째의 결과다 (참은 `ok`, 거짓은 `failed`).
namespace luil::macos {
    // 둘째가 보낸 인자 하나를 받는다. **메인 thread**에서 불린다. 참이면 앱이 받았다.
    //  - `activate`는 둘째가 첫째의 보이는 주 창을 앞으로 세워 달라고 했는지다 (`instance_send_options::activate`). 세우는 것은
    //    host의 몫이다. 둘째는 macOS 14부터 보내기 전에 앞 창 자리를 첫째에 넘긴다(`yieldActivationToApplication:`).
    //    숨은 주 창(`ui_frame::main_hidden`)은 보이게 하지 않는다. 답이 참이든 거짓이든 같다.
    //  - 둘째는 답을 기다리므로 곧 돌아와야 한다. `host.post_app_message_ticketed`로 게시하고 표가 0이 아닌지를 답한다.
    //    대화 상자나 막는 호출을 하지 않는다.
    //  - 던지면 삼키고 거짓으로 본다.
    using instance_receiver = std::function<bool(const instance_arguments& arguments, bool activate)>;

    // 첫째(`instance.primary()`)의 받는 자리를 열고 받는 함수를 단다. 어느 thread에서나 부른다.
    //  - 처음 부르면 소켓을 열고 배경 thread가 받기 시작한다. 그 전까지 둘째는 시한까지 찾다가 `not_found`다
    //    (Windows에서 주 창이 표식을 걸기 전과 같다). host는 `on_started` 뒤, 창을 보이기 전에 단다.
    //  - 다시 부르면 함수만 바꾼다. 빈 함수는 모두 거절한다 (둘째는 `failed`).
    //  - 받는 함수는 dispatch main queue에서 불리므로 메인 run loop(`NSApplication`의 run)가 돌아야 한다.
    //  - 첫째가 아니거나 옮겨 간 빈 객체면 `not_found`이고 아무 일도 하지 않는다. 소켓을 열지 못하면 `failed`다.
    //  - `instance`가 없어질 때 받는 자리도 닫힌다.
    [[nodiscard]] platform_result set_instance_receiver(const single_instance& instance, instance_receiver receiver);

    // 받는 자리를 닫는다. 소켓을 지우고 배경 thread를 멈춘다(join). 이 뒤의 둘째는 `not_found`다.
    //  - 이미 받아 메인 queue에서 기다리는 것은 거절로 답한다. 단 것이 없으면 아무 일도 하지 않는다.
    //  - host를 거두기 직전(주 창을 닫을 때)에 부른다. Windows가 표식을 먼저 지우는 자리다.
    void clear_instance_receiver(const single_instance& instance) noexcept;
}

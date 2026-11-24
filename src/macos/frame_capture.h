#pragma once

#include <CoreGraphics/CoreGraphics.h>

// 그린 frame의 그림을 개발용 갈무리(`LUIL_MACOS_FRAME_CAPTURE`, 입력 대본의 `snapshot`)에 넘기는 자리다. UI thread에서만 쓴다.
//  - CPU 렌더러는 그리는 그림이 곧 CGImage라 frame마다 넘긴다.
//  - Metal 렌더러는 GPU에서 되읽어야 해서 갈무리를 원할 때만 넘긴다 (`frame_readback_wanted`).
namespace luil::macos {
    // 그린 frame을 넘긴다. 마지막 것을 붙잡아 두고, 환경 변수가 있으면 그 PNG를 덮어쓴다.
    void publish_frame(CGImageRef image);
    // 마지막으로 넘긴 frame이다 (붙잡은 쪽이 놓는다, 없으면 null). `snapshot`이 웹뷰와 합성하는 바탕이다.
    [[nodiscard]] CGImageRef copy_latest_frame();
    // 다음 frame을 되읽어 넘겨 달라고 한다 (`snapshot`). 넘기면 풀린다.
    void request_frame_readback();
    // 되읽어 넘길 frame인가. 환경 변수가 있거나 요청이 있으면 참이다.
    [[nodiscard]] bool frame_readback_wanted();
}

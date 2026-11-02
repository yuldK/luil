#pragma once

namespace luil {
    // 알림의 심각도다. 토스트·정보 막대가 같은 낱말을 쓰고 뒤에 오는 대화 상자도 그렇다.
    //  - 구체 색과 글리프는 이 타입이 아니라 그리는 쪽이 정한다 (비공개 `severity_paint.h`).
    //    색만으로 뜻을 말하지 않는다. 고대비가 info·warning·error의 색을 같게 접으므로 글리프가 뜻을 남긴다.
    enum class ui_severity
    {
        info,
        success,
        warning,
        error,
    };
}

#pragma once

#include "luil/app/app_delegate.h"
#include "luil/app/app_host.h"
#include "luil/app/renderer_policy.h"
#include "luil/ui/ui_interaction.h"

// GameActivity의 native_app_glue가 정의하는 앱 상태다 (game-activity/native_app_glue).
// 앱의 `android_main`이 받은 것을 그대로 넘긴다.
struct android_app;

namespace luil::android {
    // Android 앱 하나의 설정이다.
    struct application_config
    {
        // 렌더러다. Android의 GPU 경로(Vulkan)는 아직 없으므로 CPU로 그린다
        // (docs/android-port-plan.md 4단계). `automatic`이면 GPU 없이 물러선 것으로 기록된다.
        renderer_mode renderer { renderer_mode::cpu };
        // 터치 몸짓의 시작 설정이다 (touch-pen-input-design.md).
        touch_gesture_config touch {};
    };

    // 앱이 꽂는 세 조각이다. 셋 다 `run_application`이 돌아올 때까지 살아 있어야 한다.
    struct application_environment
    {
        logic_driver* driver { nullptr };
        interaction_policy* policy { nullptr };
        app_delegate* delegate { nullptr };
    };

    // GameActivity 앱을 돌린다. 앱의 `android_main`에서 부르고, Activity가 끝날 때 돌아온다.
    //
    // 부른 thread(native_app_glue의 `android_main` thread)가 luil의 UI thread다. input과
    // logic thread는 `app_host`가 세운다 (docs/concepts/threading-model.md).
    //  - 표면은 하나다. 크기와 배율은 시스템 막대·컷아웃을 뺀 안전 영역의 것을
    //    `app_delegate::make_window_metrics_message`로 알리고, 그 자리에서 그린다.
    //    배율은 `density / 160`이라 논리 픽셀 1이 1dp다.
    //  - 보이고 사라질 때 `app_delegate::make_lifecycle_message`를 게시한다. 사라질 때가
    //    저장할 마지막 기회일 수 있다.
    //  - 뒤로 가기는 Activity가 처리해 앱을 끝낸다. 끝날 때 `app_host::shutdown()`이
    //    종료 신호와 종료 저장을 돌린다.
    // 반환값은 0이면 정상 종료, 아니면 시작에 실패한 것이다.
    int run_application(android_app* app, const application_config& config, const application_environment& environment);
} // namespace luil::android

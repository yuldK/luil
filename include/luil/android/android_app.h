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
        // 렌더러다. `automatic`은 Vulkan을 먼저 쓰고, 만들거나 그리다 실패하면 CPU로
        // 물러선다. 한 번 물러서면 그 Activity가 끝날 때까지 Vulkan을 다시 쓰지 않는다
        // (창이 다시 생겨도 CPU다). `gpu`는 Vulkan을 세우지 못하면 Activity를 끝낸다.
        renderer_mode renderer { renderer_mode::automatic };
        // Vulkan 렌더러를 **만들 때** 실패시킨다 (smoke test). Windows `window_config`의
        // `simulate_direct3d_failure`와 같은 자리다.
        bool simulate_gpu_failure { false };
        // Vulkan으로 이만큼 그린 뒤 다음 frame을 실패시킨다 (0이면 하지 않는다).
        // `simulate_direct3d_loss_after_frames`와 같다.
        int simulate_gpu_loss_after_frames { 0 };
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

    // 시스템 속성으로 렌더러 설정을 덮는다. 앱이 명령줄 없이 렌더러 경로를 재현하는 길이다.
    //  - `debug.luil.renderer` — `auto`, `cpu`, `gpu` (`parse_renderer_mode`의 값)
    //  - `debug.luil.simulate_gpu_failure` — `1`이면 Vulkan 생성을 실패시킨다
    //  - `debug.luil.simulate_gpu_loss_after_frames` — 이만큼 그린 뒤 Vulkan 손실을 주입한다
    // `adb shell setprop`으로 정하고 앱을 다시 띄우면 읽힌다. 정하지 않았거나 읽지 못한
    // 값은 그대로 둔다. `debug.` 속성은 셸만 쓸 수 있으므로 배포본에 남겨도 사용자가 바꾸지 못한다.
    void apply_debug_properties(application_config& config);

    // GameActivity 앱을 돌린다. 앱의 `android_main`에서 부르고, Activity가 끝날 때 돌아온다.
    //
    // 부른 thread(native_app_glue의 `android_main` thread)가 luil의 UI thread다. input과
    // logic thread는 `app_host`가 세운다 (docs/concepts/threading-model.md).
    //  - 표면은 하나다. 크기와 배율은 시스템 막대·컷아웃을 뺀 안전 영역의 것을
    //    `app_delegate::make_window_metrics_message`로 알리고, 그 자리에서 그린다.
    //    배율은 `density / 160`이라 논리 픽셀 1이 1dp다.
    //  - 보이고 사라질 때 `app_delegate::make_lifecycle_message`를 게시한다. 사라질 때가
    //    저장할 마지막 기회일 수 있다.
    //  - 터치·펜·마우스·키보드 입력을 옮긴다 (touch-pen-input-design.md). 텍스트 칸에 초점이
    //    서면 IME(GameTextInput)를 붙이고 소프트 키보드를 띄운다. 키보드가 올라오면 안전 영역이
    //    그만큼 줄고, policy의 `on_focus_moved`로 초점 칸을 다시 드러낸다
    //    (docs/concepts/text-input.md). 클립보드는 JNI로 다룬다.
    //  - 시작하자마자 프로세스의 JavaVM을 적어 `net::http_client`가 JNI를 쓸 수 있게 한다. 앱의
    //    매니페스트에 INTERNET 권한이 있어야 한다 (http-client-design.md).
    //  - frame의 popup은 주 표면 위에 겹쳐 그린다 (popup-overlay-design.md). popup이 떠 있는
    //    동안 뒤로 가기는 Esc처럼 popup을 닫는다. 보조 창(`ui_frame::windows`)은 무시한다.
    //  - 뒤로 가기는 Activity가 처리해 앱을 끝낸다. 끝날 때 `app_host::shutdown()`이
    //    종료 신호와 종료 저장을 돌린다.
    // 반환값은 0이면 정상 종료, 아니면 시작에 실패한 것이다.
    int run_application(android_app* app, const application_config& config, const application_environment& environment);
} // namespace luil::android

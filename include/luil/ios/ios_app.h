#pragma once

#include "luil/app/app_delegate.h"
#include "luil/app/app_host.h"
#include "luil/app/renderer_policy.h"
#include "luil/ui/ui_interaction.h"

#include <string>
#include <vector>

namespace luil::ios {
    // iPhone·iPad 앱 하나의 설정이다. 길이는 논리 픽셀(점)이다 — 화면 배율(2배·3배)을 곱한 값이 물리 픽셀이다.
    struct application_config
    {
        // 렌더러다. `automatic`은 Metal(Ganesh)을 먼저 쓰고, 만들거나 그리다 실패하면 CPU로 물러선다. 한 번 물러서면 다시
        // Metal을 쓰지 않는다. `gpu`(`direct3d`도 같은 뜻으로 받는다)는 Metal을 세우지 못하면 그리지 않는다.
        renderer_mode renderer { renderer_mode::automatic };
        // Metal 렌더러를 **만들 때** 실패시킨다 (smoke test). Windows `window_config`의 `simulate_direct3d_failure`와 같은 자리다.
        bool simulate_gpu_failure { false };
        // Metal로 이만큼 그린 뒤 다음 frame을 실패시킨다 (0이면 하지 않는다). `simulate_direct3d_loss_after_frames`와 같다.
        int simulate_gpu_loss_after_frames { 0 };
        // 터치 몸짓의 시작 설정이다 (touch-pen-input-design.md).
        //  - 길게 눌러 메뉴가 열릴 때의 햅틱은 iOS의 관례라 켜서 시작한다 (`long_press_haptic`). 앱이 끄려면 거짓으로 둔다.
        touch_gesture_config touch { .long_press_haptic = true };
    };

    // 앱이 꽂는 세 조각이다. 셋 다 프로세스가 끝날 때까지 살아 있어야 한다 (`run_application`은 돌아오지 않는다).
    //  - delegate는 플랫폼 중립 부분(`app_delegate`)만 불린다. Windows의 `win32::window_delegate`를 구현한 앱 delegate를
    //    그대로 넘겨도 된다 (Android host와 같다).
    struct application_environment
    {
        logic_driver* driver { nullptr };
        interaction_policy* policy { nullptr };
        app_delegate* delegate { nullptr };
    };

    // 명령 줄 인자의 디버그 덮어쓰기를 읽는다. Xcode의 scheme 인자나 `xcrun simctl launch`의 인자로 렌더러 경로를 재현하는 길이다.
    //  - `--renderer=auto|cpu|gpu` (`parse_renderer_mode`의 값)
    //  - `--simulate-gpu-failure` — Metal 생성을 실패시킨다
    //  - `--simulate-gpu-loss-after-frames=N` — 이만큼 그린 뒤 Metal 손실을 주입한다
    // 모르는 인자와 읽지 못한 값은 그대로 둔다.
    void apply_debug_arguments(application_config& config, int argc, const char* const* argv);

    // UIKit 앱을 돌린다. 앱의 `main`에서 부른다. `UIApplicationMain`을 부르므로 **돌아오지 않는다** — 시스템이 프로세스를 거둔다.
    //
    // 부른 thread는 프로세스의 메인 thread여야 하고, 그것이 luil의 UI thread다 (UIKit은 메인 thread에서만 불린다).
    // input과 logic thread는 `app_host`가 세운다 (docs/concepts/threading-model.md).
    //  - 장면(scene)은 하나다. 앱의 Info.plist에 `UIApplicationSceneManifest`가 있어야 한다 (examples/ios.cmake의 틀). iPad의 여러
    //    창(`UIApplicationSupportsMultipleScenes`)은 받지 않는다. 보조 창(`ui_frame::windows`)과 도킹 표면은 무시한다.
    //  - 표면은 장면의 창 하나다. 크기와 배율은 상태 표시줄·노치·홈 표시줄·소프트 키보드를 뺀 안전 영역의 것을
    //    `app_delegate::make_metrics_message`(폭 등급 포함)로 알리고, 그 자리에서 그린다. 안전 영역 밖의 거리는
    //    `window_metrics::outside`다. 회전·iPad 화면 나누기·Stage Manager의 크기 변경도 같은 길로 온다.
    //  - 앞에 서고 뒤로 갈 때 `app_delegate::make_lifecycle_message`를 게시한다. 뒤로 갈 때가 저장할 마지막 기회일 수 있다.
    //    뒤로 갈 때는 배경 작업 시간(`beginBackgroundTask`)을 쥐고, logic이 그 메시지를 처리하면 놓는다 — 그 사이 logic thread의
    //    저장을 iOS가 멈추지 않는다. 그 뒤로 이어지는 저장은 앱이 `app_host::hold_background_work`로 쥔다. 시간이 만료되면 host가
    //    곧바로 돌려주고 `app_delegate::make_background_expired_message`로 알린다 (consumer-contract.md의 "뒤로 갈 때의 저장").
    //  - 터치·Apple Pencil(압력·기울기)·트랙패드와 마우스(iPad)·하드웨어 키보드 입력을 옮긴다 (touch-pen-input-design.md).
    //    Command가 주 단축키(`key_chord::control`의 자리)다. 텍스트 칸에 초점이 서면 입력기(UITextInput)를 붙이고 소프트
    //    키보드를 띄운다. 키보드가 올라오면 안전 영역이 그만큼 줄고 초점 칸을 다시 드러낸다 (docs/concepts/text-input.md).
    //  - frame의 popup은 표면 위에 겹쳐 그린다 (popup-overlay-design.md). 웹뷰(`ui_frame::webviews`)는 WKWebView로 그 자리에 선다.
    //  - 끝내기(`ui_command::application_exit`·`window_close`)는 받지 않는다. iOS 앱은 스스로 끝나지 않는다 (사용자가 앱 전환기에서
    //    거둔다). 앱은 뒤로 갈 때 저장한다.
    //  - 시스템의 밝은·어두운 모양과 강조색(앱의 tint), 대비·움직임·투명도 줄이기와 글자 크기(Dynamic Type) 설정을 따른다.
    //  - frame의 기기 요청(`ui_frame::device`) 가운데 화면 방향과 상태 표시줄·홈 표시줄 숨김(몰입 모드), 노치 밑 그리기를 옮긴다.
    //    볼륨 키는 받을 길이 없다 (`ui_platform_capabilities::volume_keys`가 거짓이다).
    //  - 햅틱(`haptic_request`, `app_host::perform_haptic`)은 `UIFeedbackGenerator`로 낸다. `ui_platform_capabilities::haptics`는
    //    기기에 진동 장치가 있을 때만 참이다 (Core Haptics의 하드웨어 답 — iPhone은 참, iPad와 시뮬레이터는 거짓). 거짓이어도
    //    요청은 받고 조용하다.
    //  - 클립보드는 일반 붙여넣기 판(UIPasteboard)의 글·HTML·그림이다. 파일 대화 상자는 문서 선택기(UIDocumentPicker)다.
    //  - OS 알림은 UserNotifications다. 알림 권한은 `app_permission::notifications`로 묻는다.
    // 시작에 실패하면(logic driver가 없다 등) 1을 돌려준다. 그 밖에는 돌아오지 않는다.
    int run_application(int argc, char** argv, const application_config& config, const application_environment& environment);

    // 명령 줄 인자를 UTF-8 목록으로 옮긴다. Windows의 `win32::command_line_arguments`와 같이 첫 항목이 실행 파일(`argv[0]`)이다.
    [[nodiscard]] std::vector<std::u8string> command_line_arguments(int argc, const char* const* argv);
}

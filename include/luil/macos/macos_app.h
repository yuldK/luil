#pragma once

#include "luil/app/app_delegate.h"
#include "luil/app/app_host.h"
#include "luil/app/platform_services.h"
#include "luil/app/renderer_policy.h"
#include "luil/ui/ui_cursor.h"
#include "luil/ui/ui_interaction.h"

#include <functional>

#include <string>
#include <vector>

namespace luil::macos {
    // macOS 앱 하나의 설정이다. 길이는 논리 픽셀(점)이다 — 화면 배율(레티나 2배)을 곱한 값이 물리 픽셀이다.
    struct application_config
    {
        // 창 제목이다. 앱 메뉴의 "… 종료" 항목에도 쓴다.
        std::u8string title { u8"luil" };
        int initial_width { 1280 };
        int initial_height { 720 };
        // 사용자가 이보다 작게 줄일 수 없는 창 내용 크기다.
        int minimum_width { 480 };
        int minimum_height { 320 };
        // 렌더러다. `automatic`은 Metal(Ganesh)을 먼저 쓰고, 만들거나 그리다 실패하면 CPU로 물러선다. 한 번 물러서면 다시
        // Metal을 쓰지 않는다. `gpu`(`direct3d`도 같은 뜻으로 받는다)는 Metal을 세우지 못하면 시작하지 않고, 그리다 잃으면 끝낸다.
        renderer_mode renderer { renderer_mode::automatic };
        // Metal 렌더러를 **만들 때** 실패시킨다 (smoke test). Windows `window_config`의 `simulate_direct3d_failure`와 같은 자리다.
        bool simulate_gpu_failure { false };
        // Metal로 이만큼 그린 뒤 다음 frame을 실패시킨다 (0이면 하지 않는다). `simulate_direct3d_loss_after_frames`와 같다.
        int simulate_gpu_loss_after_frames { 0 };
        // 한 frame만 그리고 끝낸다 (renderer smoke test). 창은 보이지 않는다.
        bool smoke_test { false };
        // 주 창·보조 창이 OS 파일 끌기를 받는다 (Finder에서 끌어 온 파일, 다른 앱의 주소·글·그림).
        // 놓인 자리의 drop 대상 element가 받고(`ui_action_context::native_drop_data`는 끌기의 `NSPasteboard*`다), 없으면
        // delegate가 `win32::window_delegate`를 구현했을 때 그 `on_file_dropped`가 본다 (os-dragdrop-design.md).
        bool accept_file_drop { false };
        // `ui_cursor`를 실제 커서로 옮긴다. 돌려주는 값은 `NSCursor*`다 (ARC가 쥐지 않은 맨 포인터, 앱이 살려 둔다).
        // 비어 있거나 nullptr를 돌려주면 라이브러리의 기본 매핑(시스템 커서)이다. 앱이 정의한 모양(`application_cursor`)을
        // 여기서 준다 (Windows `window_config::resolve_cursor`의 자리). 시스템 모양은 `system_cursor`로 얻는다.
        std::function<void*(ui_cursor)> resolve_cursor {};
        // 터치 몸짓의 시작 설정이다. 트랙패드는 마우스로 오므로 길게 누르기와 끌어 스크롤하기는 붙지 않는다.
        touch_gesture_config touch {};
    };

    // 앱이 꽂는 세 조각이다. 셋 다 `run_application`이 돌아올 때까지 살아 있어야 한다.
    //  - delegate는 플랫폼 중립 부분(`app_delegate`)만 불린다. Windows의 `win32::window_delegate`를 구현한 앱 delegate를
    //    그대로 넘겨도 된다 (Android host와 같다).
    struct application_environment
    {
        logic_driver* driver { nullptr };
        interaction_policy* policy { nullptr };
        app_delegate* delegate { nullptr };
        // 첫째 인스턴스면 같은 이름의 둘째가 보낸 인자를 받는다 (`single_instance::send_to_primary`). 비어 있으면 받지 않는다.
        //  - 받은 인자는 delegate가 `win32::window_delegate`를 구현했을 때 그 `on_instance_arguments`로 간다 (Windows와 같다).
        //  - 둘째가 앞으로 세워 달라고 했으면 보이는 주 창을 앞으로 세운다. 숨은 주 창은 보이게 하지 않는다.
        const single_instance* instance { nullptr };
    };

    // AppKit 앱을 돌린다. 앱의 `main`에서 부르고, 창이 닫혀 종료가 끝나면 돌아온다.
    //
    // 부른 thread는 프로세스의 메인 thread여야 하고, 그것이 luil의 UI thread다 (AppKit은 메인 thread에서만 불린다).
    // input과 logic thread는 `app_host`가 세운다 (docs/concepts/threading-model.md).
    //  - 제목 막대와 창 단추는 시스템의 것이라 `caption_element`는 자리를 차지하지 않는다
    //    (`ui_platform::window_caption`이 거짓이다). 창 제목은 `caption.title`이다. 앱 메뉴(종료)는 host가 세운다.
    //  - 크기와 배율은 창 내용 영역의 것을 `app_delegate::make_metrics_message`로 알린다. 배율은 창이 놓인 화면의
    //    `backingScaleFactor`다. 다른 배율의 화면으로 옮기면 다시 알린다.
    //  - 보조 창(`ui_frame::windows`)을 창으로 세운다. 소유된 창은 주 창 위에 뜨는 판(`NSPanel`)이고 앱이 뒤로 가면 함께 숨는다.
    //    `independent`는 보통 창이다. 닫기 단추는 `ui_window::close`를, 크기·배율은 `metrics`를, 자리는 `placement`를 낸다.
    //    `main_hidden`은 주 창과 소유된 창을 숨긴다.
    //  - 화면 좌표(`screen_point`, `window_placement`, `window_screen_anchor`)는 주 화면 왼쪽 위가 원점이고 아래로 늘며,
    //    점에 주 화면의 배율을 곱한 픽셀이다.
    //  - frame의 popup은 앵커 사슬이 끝나는 창 위에 겹쳐 그린다 (popup-overlay-design.md).
    //  - frame의 웹뷰(`ui_frame::webviews`)는 WKWebView로 그 앵커 창(주 창·보조 창·도킹 표면)의 자리표 자리에 선다
    //    (webview-composition-design.md의 macOS 갈래).
    //  - 도킹 표면(`ui_frame::docked_surfaces`)은 테두리 없고 활성화되지 않는 판이다. 자리는 모니터 목록으로 푼다.
    //  - 마우스·트랙패드·키보드 입력을 옮긴다. Command가 주 단축키(`key_chord::control`의 자리), Option이 낱말 이동이다.
    //    텍스트 칸에 초점이 서면 입력기(한글 조합 등)가 붙는다.
    //  - 클립보드는 일반 붙여넣기 판(NSPasteboard)의 글·HTML·그림·파일이다. 파일 대화 상자는 sheet, OS 끌어 놓기는 창 view가 받는다.
    //  - delegate가 `win32::window_delegate`도 구현했으면 Windows와 같은 훅을 부른다: 주 창 배치·배경 효과·작업 표시줄(Dock)·
    //    점프 목록(Dock 메뉴)·트레이(메뉴 막대 상태 항목)·전역 단축키의 보고, 파일 놓기의 물러섬, 둘째 인스턴스의 인자,
    //    네이티브 창(`NSWindow*`)의 생성·frame·소멸.
    //  - 시스템의 밝은·어두운 모양과 강조색, 대비·움직임·투명도 줄이기 설정을 따른다.
    //  - 창의 닫기 단추와 Command+Q는 앱이 frame에 실은 닫힘 훅(`ui_frame::close`)을 먼저 부른다. 훅이 없으면
    //    (또는 `ui_command::application_exit`이면) `app_host::shutdown`이 종료 신호를 돌린 뒤 창이 사라지고 이 함수가 돌아온다.
    // 반환값은 0이면 정상 종료, 아니면 시작에 실패한 것이다.
    int run_application(const application_config& config, const application_environment& environment);

    // 기본 매핑의 시스템 커서다 (`NSCursor*`, 맨 포인터). 앱 정의 모양을 시스템 모양으로 보일 때 `resolve_cursor`에서 쓴다.
    [[nodiscard]] void* system_cursor(ui_cursor cursor) noexcept;

    // 명령 줄 인자를 UTF-8 목록으로 옮긴다. Windows의 `win32::command_line_arguments`와 같이 첫 항목이 실행 파일(`argv[0]`)이다.
    [[nodiscard]] std::vector<std::u8string> command_line_arguments(int argc, const char* const* argv);
}

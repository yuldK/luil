// luil 최소 앱이다: 창 하나, custom caption, 라벨 하나, 버튼 하나.
//
// 파일 구성
//   main.cpp       — 이 파일. 조립과 실행뿐이다.
//   hello_app.h    — 앱 쪽 절반의 선언 (kind·메시지·driver·delegate)과 경계 설명.
//   hello_app.cpp  — 구현. 상태 변경(handle)과 화면 만들기(make_frame).
//
// 실행 흐름
//   1. run_application_window가 창을 만들고 스레드 셋을 조립한다.
//      - logic thread: driver가 메시지를 처리하고 frame을 게시한다.
//      - input thread: hit test와 상호작용 상태 기계를 돌린다.
//      - UI thread(이 함수): 창 메시지 루프와 그리기.
//   2. 버튼 클릭 → 액션이 increment_intent를 반환 → logic으로 배달
//      → handle이 상태 변경 → make_frame이 새 tree 게시 → 화면 갱신.
//   3. 창을 닫으면 종료 신호(close_intent)가 돌고 스레드가 순서대로 정리된다.
//
// 자세한 안내는 docs/how-to-start.md에 있다.

#include "hello/hello_app.h"

#include <windows.h>

#include <string>
#include <string_view>

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    // DPI 인지는 창을 만들기 전에 한 번 켠다.
    luil::win32::enable_per_monitor_dpi_awareness();
    // COM STA는 TSF(텍스트 입력)·파일 dialog가 요구한다.
    // 이 예제는 텍스트 입력이 없지만 켜 두는 것이 관례다.
    const luil::win32::com_sta_scope com {};

    // 창의 겉모습과 정책이다. 길이는 전부 논리 96 DPI 기준 px다.
    luil::win32::window_config config {};
    config.class_name = L"Luil.Hello.Window";
    config.caption.title = u8"hello luil";
    config.caption.minimize_tooltip = u8"Minimize";
    config.caption.maximize_tooltip = u8"Maximize or restore";
    config.caption.close_tooltip = u8"Close";
    config.initial_width = 800;
    config.initial_height = 500;

    // --position=x,y 로 창이 뜰 자리를 정한다 (물리 픽셀, 음수도 된다).
    // 읽지 못하면 그냥 두어 OS가 정한다.
    if (const auto arguments { luil::win32::command_line_arguments() }; arguments.has_value())
        for (const std::u8string& argument : *arguments)
            if (argument.starts_with(u8"--position="))
                config.initial_position = luil::win32::parse_window_position(std::u8string_view { argument }.substr(11));

    // 앱의 세 조각을 꽂는다.
    // policy는 텍스트 입력·휠·단축키가 생길 때 구현한다 — 없으면 nullptr로 둔다.
    hello::hello_driver driver {};
    hello::hello_delegate delegate {};
    luil::win32::window_environment environment {};
    environment.driver = &driver;
    environment.delegate = &delegate;

    // 메시지 루프가 끝날 때까지 여기서 돈다. 반환값이 프로세스 종료 코드다.
    return luil::win32::run_application_window(config, environment);
}

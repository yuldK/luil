// widgets 예제다: 흔한 element를 섹션별 파일 하나씩으로 보여 주는 진열장.
//
// 파일 구성
//   main.cpp             — 이 파일. 조립과 실행뿐이다.
//   app.h / app.cpp      — 앱 골격: kind·intent·상태, driver(logic)·policy(input)·delegate(UI).
//   controls_section.cpp — 버튼·라벨·panel
//   inputs_section.cpp   — 한 줄 텍스트 입력 (편집·IME)
//   choices_section.cpp  — 라디오·토글·접이식 그룹
//   toasts_section.cpp   — 토스트 알림과 시간 만료(tick)
//
// 처음이라면 hello 예제(examples/hello)와 docs/how-to-start.md를 먼저 본다.
// 이 예제는 그 골격 위에 element 사용법을 얹은 것이다.

#include "widgets/app.h"

#include <windows.h>

#include <string>
#include <string_view>

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    luil::win32::enable_per_monitor_dpi_awareness();
    // 텍스트 입력(TSF)이 COM STA를 요구한다.
    const luil::win32::com_sta_scope com {};

    luil::win32::window_config config {};
    config.class_name = L"Luil.Widgets.Window";
    config.caption.title = u8"luil widgets";
    config.caption.minimize_tooltip = u8"Minimize";
    config.caption.maximize_tooltip = u8"Maximize or restore";
    config.caption.close_tooltip = u8"Close";
    config.initial_width = 900;
    config.initial_height = 640;
    config.minimum_client_width = 560;
    config.minimum_client_height = 480;

    // --position=x,y 로 창이 뜰 자리를 정한다 (물리 픽셀, 음수도 된다).
    // 읽지 못하면 그냥 두어 OS가 정한다 — 예제라 인자 검사는 데모가 맡는다.
    if (const auto arguments { luil::win32::command_line_arguments() }; arguments.has_value())
        for (const std::u8string& argument : *arguments)
            if (argument.starts_with(u8"--position="))
                config.initial_position = luil::win32::parse_window_position(std::u8string_view { argument }.substr(11));

    widgets::widgets_driver driver {};
    widgets::widgets_policy policy {};
    widgets::widgets_delegate delegate {};
    luil::win32::window_environment environment {};
    environment.driver = &driver;
    environment.policy = &policy;
    environment.delegate = &delegate;

    return luil::win32::run_application_window(config, environment);
}

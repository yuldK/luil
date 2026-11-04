// mobile demo를 데스크톱 창으로 띄운다 (Windows).
//
// 휴대폰·태블릿용 예제지만 페이지는 플랫폼을 가리지 않으므로 Windows에서도 세운다. 창을 좁히면 휴대폰의
// 한 장씩 보기, 넓히면 태블릿의 두 판이 된다. Windows에서는 앱 바의 뒤로 단추가 뒤로 가기다.
//
// 파일 구성
//   main.cpp / android_main.cpp — 플랫폼별 조립과 실행뿐이다.
//   app.h / app.cpp             — 셸: 페이지 목록, 앱 바, 한 장씩 보기와 두 판.
//   common.h / common.cpp       — kind·intent 등록부와 공용 도우미.
//   *_page.h / *_page.cpp       — 페이지 하나씩 (데스크톱 demo에서 옮겼다).

#include "mobile_demo/app.h"
#include "mobile_demo/basics_page.h"
#include "mobile_demo/files_page.h"

#include "luil/win32/win32_window.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {
    // 테마 페이지의 파일 감시 묶음이 볼 폴더다 (hot-reload-design.md 결정 5). 실행 파일 옆이라 사람이 찾는다. 읽지 못하면 빈 경로이고 감시가 서지 않는다.
    [[nodiscard]] std::filesystem::path hot_reload_folder()
    {
        wchar_t buffer[MAX_PATH] {};
        const DWORD length { GetModuleFileNameW(nullptr, buffer, MAX_PATH) };
        if (length == 0 || length >= MAX_PATH)
            return {};
        const std::filesystem::path executable { std::wstring { buffer, length } };
        return executable.parent_path() / L"hot-reload" / executable.stem();
    }

    class desktop_delegate final : public luil::win32::window_delegate
    {
    public:
        // 감시자를 내려 둔 뒤 임시 폴더를 지운다. host와 logic은 이미 멈췄다.
        ~desktop_delegate() override
        {
            if (watcher_ != nullptr)
                watcher_->stop();
            watcher_.reset();
            reloader_.reset();
            std::error_code ignored {};
            std::filesystem::remove_all(folder_, ignored);
        }

        // 파일 감시 페이지가 볼 임시 폴더를 걸어 logic에 넘긴다. 감시자와 핫 리로드 도우미는 logic이 `stop_workers`에서 멈춘다.
        void on_started(luil::app_host& host) override
        {
            folder_ = std::filesystem::temp_directory_path() / (L"luil-mobile-demo-watch-" + std::to_wstring(GetCurrentProcessId()));
            watcher_ = mobile_demo::start_files_watch(host, folder_);
            reloader_ = mobile_demo::start_theme_hot_reload(host, hot_reload_folder());
            host.post_app_message(luil::app_message { mobile_demo::clipboard_host_intent { &host } });
            host.post_app_message(luil::app_message { mobile_demo::device_locale_intent { luil::user_first_weekday(), luil::user_ui_languages() } });
        }

        [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics) override
        {
            return mobile_demo::make_metrics_message(metrics);
        }

        // 기본 페이지의 클립보드 그림 붙여넣기다. 답은 `make_clipboard_data_message`로 온다.
        void execute_app_ui_command(luil::app_host& host, const luil::app_ui_command& command) override
        {
            if (command.command == mobile_demo::command_paste_clipboard_image)
                host.post_clipboard_request(luil::clipboard_data_paste_request {});
        }

        // 서식 칸이 청한 답은 표식으로 가른다.
        [[nodiscard]] luil::app_message make_clipboard_data_message(const luil::clipboard_data& data) override
        {
            if (data.tag == demo::rich_text_clipboard_tag)
                return luil::app_message { demo::rich_text_clipboard_intent { data } };
            return luil::app_message { mobile_demo::clipboard_image_intent { data } };
        }

    private:
        std::filesystem::path folder_ {};
        std::unique_ptr<luil::file_watcher> watcher_ {};
        std::unique_ptr<luil::hot_reloader> reloader_ {};
    };
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    luil::win32::enable_per_monitor_dpi_awareness();
    // 텍스트 입력(TSF)이 COM STA를 요구한다.
    const luil::win32::com_sta_scope com {};

    luil::win32::window_config config {};
    config.class_name = L"Luil.MobileDemo.Window";
    config.caption.title = u8"luil mobile demo";
    config.caption.minimize_tooltip = u8"Minimize";
    config.caption.maximize_tooltip = u8"Maximize or restore";
    config.caption.close_tooltip = u8"Close";
    // 휴대폰 세로에 가까운 창으로 시작한다. 넓히면 두 판이 된다.
    config.initial_width = 420;
    config.initial_height = 860;
    config.minimum_client_width = 360;
    config.minimum_client_height = 480;

    mobile_demo::mobile_driver driver {};
    mobile_demo::mobile_policy policy {};
    desktop_delegate delegate {};
    luil::win32::window_environment environment {};
    environment.driver = &driver;
    environment.policy = &policy;
    environment.delegate = &delegate;
    return luil::win32::run_application_window(config, environment);
}

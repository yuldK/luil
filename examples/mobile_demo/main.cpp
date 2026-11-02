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
#include "mobile_demo/files_page.h"

#include "luil/win32/win32_window.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {
    class desktop_delegate final : public luil::win32::window_delegate
    {
    public:
        // 감시자를 내려 둔 뒤 임시 폴더를 지운다. host와 logic은 이미 멈췄다.
        ~desktop_delegate() override
        {
            if (watcher_ != nullptr)
                watcher_->stop();
            watcher_.reset();
            std::error_code ignored {};
            std::filesystem::remove_all(folder_, ignored);
        }

        // 파일 감시 페이지가 볼 임시 폴더를 걸어 logic에 넘긴다. 감시자는 logic이 `stop_workers`에서 멈춘다.
        void on_started(luil::app_host& host) override
        {
            folder_ = std::filesystem::temp_directory_path() / (L"luil-mobile-demo-watch-" + std::to_wstring(GetCurrentProcessId()));
            watcher_ = mobile_demo::start_files_watch(host, folder_);
        }

        [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics) override
        {
            return mobile_demo::make_metrics_message(metrics);
        }

    private:
        std::filesystem::path folder_ {};
        std::unique_ptr<luil::file_watcher> watcher_ {};
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

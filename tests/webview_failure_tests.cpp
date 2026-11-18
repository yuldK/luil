#include "win32/webview_failure.h"
#include "win32/composition_device.h"
#include "win32/webview_host.h"

#include <chrono>
#include <filesystem>
#include <wrl/client.h>

#include <catch2/catch_test_macros.hpp>

namespace {
    using luil::webview_creation_failure;
    using luil::win32::minimum_webview_runtime_build;
    using luil::win32::webview_environment_failure_for;
    using luil::win32::webview_runtime_build_number;
    using luil::win32::webview_runtime_too_old_message;
}

#if LUIL_TESTS_WEBVIEW
TEST_CASE("A ready webview reports surface transitions even with a zero page event budget", "[.runtime][webview-presentation]")
{
    REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    const HWND window { CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
    REQUIRE(window != nullptr);
    {
        luil::win32::composition_host composition {};
        std::u8string error {};
        auto* const device { composition.acquire(error) };
        REQUIRE(device != nullptr);
        luil::win32::webview_host host {};
        std::vector<bool> reports {};
        std::vector<HWND> children {};
        host.set_structure_reporter([&](const std::u8string& anchor, const std::u8string& id) { children.push_back(host.automation_window(anchor, id)); });
        host.set_deliver([&](luil::app_message message) {
            if (const auto* event { message.get<luil::webview_event>() }; event != nullptr && event->kind == luil::webview_event_kind::presentation_changed)
                reports.push_back(event->presentation_available);
        });
        luil::ui_webview view {};
        view.id = u8"preview";
        view.user_data_folder = (std::filesystem::temp_directory_path() / L"luil-webview-presentation-test").u8string();
        view.policy.maximum_events_per_second = 0;
        view.policy.allowed_schemes = { u8"about" };
        view.navigate_url = u8"about:blank";
        view.navigate_revision = 1;
        view.on_event = [](luil::webview_event event) { return luil::app_message { std::move(event) }; };
        view.report_presentation = true;
        luil::win32::webview_target target { &view, window };
        host.synchronize({ &target, 1 }, device);
        const auto timeout { std::chrono::steady_clock::now() + std::chrono::seconds { 15 } };
        while (reports.empty() && std::chrono::steady_clock::now() < timeout)
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        REQUIRE(reports == std::vector<bool> { false });
        target.has_underlay = true;
        host.synchronize({ &target, 1 }, device);
        host.synchronize({ &target, 1 }, device);
        host.apply_layout(view.id, {}, nullptr);
        host.apply_layout(view.id, { .width = 80, .height = 80 }, nullptr);
        REQUIRE(reports == std::vector<bool> { false, true, false });
        Microsoft::WRL::ComPtr<IDCompositionVisual> underlay {};
        REQUIRE(SUCCEEDED(device->CreateVisual(&underlay)));
        const luil::win32::webview_layout visible { .width = 80, .height = 80, .punch_hole = true };
        const auto navigation_deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 10 } };
        while (children.empty() && std::chrono::steady_clock::now() < navigation_deadline)
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            host.apply_layout(view.id, visible, underlay.Get());
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        REQUIRE(children.size() == 1);
        REQUIRE(children.front() != nullptr);
        host.apply_layout(view.id, visible, underlay.Get());
        host.apply_layout(view.id, {}, underlay.Get());
        host.apply_layout(view.id, {}, underlay.Get());
        REQUIRE(children.size() == 2);
        REQUIRE(children.back() == nullptr);
        host.shutdown();
    }
    DestroyWindow(window);
    CoUninitialize();
}
#endif

TEST_CASE("The loader's file-not-found result means the WebView2 runtime is missing", "[win32][webview]")
{
    REQUIRE(webview_environment_failure_for(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) == webview_creation_failure::runtime_missing);
}

TEST_CASE("Other environment results are environment failures", "[win32][webview]")
{
    // 프로필 자리에 쓸 수 없거나, 같은 자리를 다른 설정의 환경이 쓰고 있는 것이 흔한 까닭이다.
    REQUIRE(webview_environment_failure_for(E_ACCESSDENIED) == webview_creation_failure::environment_failed);
    REQUIRE(webview_environment_failure_for(HRESULT_FROM_WIN32(ERROR_INVALID_STATE)) == webview_creation_failure::environment_failed);
    REQUIRE(webview_environment_failure_for(E_UNEXPECTED) == webview_creation_failure::environment_failed);
}

TEST_CASE("The runtime build number is the third component of the browser version", "[win32][webview]")
{
    REQUIRE(webview_runtime_build_number(L"152.0.4191.62") == 4191);
    REQUIRE(webview_runtime_build_number(L"120.0.2210.55") == 2210);
    REQUIRE(webview_runtime_build_number(L"119.0.2151.97") < minimum_webview_runtime_build);
    // 세 번째 수로 끝나도 읽는다.
    REQUIRE(webview_runtime_build_number(L"120.0.2210") == 2210);
}

TEST_CASE("An unreadable runtime version reads as build zero", "[win32][webview]")
{
    REQUIRE(webview_runtime_build_number(nullptr) == 0);
    REQUIRE(webview_runtime_build_number(L"") == 0);
    REQUIRE(webview_runtime_build_number(L"120.0") == 0);
    REQUIRE(webview_runtime_build_number(L"120-0.2210.55") == 0);
}

TEST_CASE("The environment failure message says when the runtime is missing", "[win32][webview]")
{
    const std::u8string missing { luil::win32::webview_environment_failure_message(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) };
    REQUIRE(missing.find(u8"not installed") != std::u8string::npos);
    REQUIRE(missing.find(u8"80070002") != std::u8string::npos);

    const std::u8string denied { luil::win32::webview_environment_failure_message(E_ACCESSDENIED) };
    REQUIRE(denied.find(u8"not installed") == std::u8string::npos);
    REQUIRE(denied.find(u8"80070005") != std::u8string::npos);
}

TEST_CASE("The too-old message names the found and required builds", "[win32][webview]")
{
    const std::u8string message { webview_runtime_too_old_message(L"119.0.2151.97") };
    REQUIRE(message.find(u8"119.0.2151.97") != std::u8string::npos);
    REQUIRE(message.find(u8"2210") != std::u8string::npos);

    const std::u8string unknown { webview_runtime_too_old_message(nullptr) };
    REQUIRE(unknown.find(u8"unknown version") != std::u8string::npos);
}

#include "win32/webview_failure.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using luil::webview_creation_failure;
    using luil::win32::minimum_webview_runtime_build;
    using luil::win32::webview_environment_failure_for;
    using luil::win32::webview_runtime_build_number;
    using luil::win32::webview_runtime_too_old_message;
} // namespace

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

#pragma once

#include "luil/app/webview.h"

#include <windows.h>

#include <string>

namespace luil::win32 {
    // 이 라이브러리가 기대는 가장 새 인터페이스의 런타임 판번이다 (SDK 1.0.NNNN 축).
    //
    // 정책을 집행하는 인터페이스가 없는 런타임은 **조용히 열린 채**로 돈다 —
    // `ICoreWebView2_4`가 없으면 내려받기가 대화상자와 함께 진행되고,
    // `PermissionRequestedEventArgs3`가 없으면 거절이 프로필에 남고,
    // `NewWindowRequestedEventArgs3`가 없으면 요청이 어느 frame에서 왔는지 모른다.
    // 그래서 그보다 오래된 런타임에서는 웹뷰를 세우지 않는다 (placeholder가 남는다).
    // 2210은 `NewWindowRequestedEventArgs3`(SDK 1.0.2210.55, Chromium 120)이다.
    inline constexpr unsigned minimum_webview_runtime_build { 2210 };

    // 브라우저 판번("152.0.4191.62")의 세 번째 수다 — SDK 판번의 세 번째 수와 같은
    // 축이라 "1.0.NNNN의 인터페이스가 있는가"를 이것으로 판정한다. 읽지 못하면 0이다.
    [[nodiscard]] unsigned webview_runtime_build_number(const wchar_t* version) noexcept;

    // 환경 만들기가 돌려준 HRESULT의 갈래다.
    //  - 런타임이 없으면 로더가 `HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)`를 돌려준다
    //    (`CreateCoreWebView2EnvironmentWithOptions`의 문서). 그 밖은 환경의 실패다.
    [[nodiscard]] webview_creation_failure webview_environment_failure_for(HRESULT result) noexcept;

    // 환경 만들기가 실패했을 때의 설명이다. 런타임이 없으면 그렇다고 말하고 HRESULT를 붙인다.
    [[nodiscard]] std::u8string webview_environment_failure_message(HRESULT result);

    // 판이 낮아 세우지 않을 때의 설명이다. 찾은 판과 필요한 판을 함께 말한다.
    //  - `version`이 nullptr이면 판을 읽지 못한 것이다.
    [[nodiscard]] std::u8string webview_runtime_too_old_message(const wchar_t* version);
} // namespace luil::win32

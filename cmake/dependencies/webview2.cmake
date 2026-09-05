include_guard(GLOBAL)

# WebView2 SDK는 배포된 패키지로 온다 (scripts/fetch_webview2.ps1).
# 패키지에서 우리가 쓰는 것은 넷뿐이다 — 헤더 둘, 정적 로더 하나, 라이선스 하나.
# CMake는 네트워크를 건드리지 않고 이미 존재하는 파일만 검사해 imported target으로
# 노출한다.
#
# **flavor 축이 없다.** Skia와 갈리는 자리다: `WebView2LoaderStatic.lib`는 Chromium이
# `/Zl`(기본 라이브러리 이름 생략)로 낸 **CRT 중립** 아카이브라 CRT DEFAULTLIB 지시자도
# `_ITERATOR_DEBUG_LEVEL` FAILIFMISMATCH도 없고, `/MT`·`/MTd`·`/MD`·`/MDd` 넷 다
# LNK 진단 없이 링크된다 (docs/webview-composition-design.md).
# 그래서 아카이브가 아키텍처당 하나이고, 그것이 옳다 — 여기에
# `IMPORTED_CONFIGURATIONS`와 per-config location을 두면 없는 구분을 지어내는 것이다.
function(luil_find_webview2)
    if(NOT IS_DIRECTORY "${LUIL_WEBVIEW2_ROOT}"
        OR NOT EXISTS "${LUIL_WEBVIEW2_ROOT}/WebView2.h")
        message(FATAL_ERROR
            "The WebView2 SDK package was not found: ${LUIL_WEBVIEW2_ROOT}\n"
            "Fetch it: scripts/fetch_webview2.ps1\n"
            "It downloads the package pinned by third_party/webview2-prep.json - "
            "two headers, one static loader and the licence, nothing else "
            "(13 MB, no NuGet client needed).\n"
            "The Evergreen Runtime that renders pages is a separate install and "
            "is not needed to build.")
    endif()

    foreach(component IN ITEMS
        "WebView2EnvironmentOptions.h"
        "WebView2LoaderStatic.lib"
        "LICENSE.txt")
        if(NOT EXISTS "${LUIL_WEBVIEW2_ROOT}/${component}")
            message(FATAL_ERROR
                "The WebView2 SDK package is incomplete: "
                "${LUIL_WEBVIEW2_ROOT}/${component}\n"
                "Fetch it again: scripts/fetch_webview2.ps1 -Force")
        endif()
    endforeach()

    add_library(luil_webview2_loader STATIC IMPORTED GLOBAL)
    set_target_properties(luil_webview2_loader PROPERTIES
        IMPORTED_LOCATION "${LUIL_WEBVIEW2_ROOT}/WebView2LoaderStatic.lib")

    add_library(luil_webview2 INTERFACE)
    # SYSTEM으로 노출한다. 헤더가 지금은 /W4 /WX에 깨끗하지만, 판번이 오르며 바뀔
    # 것을 우리 /WX가 지고 갈 이유가 없다 (Skia·nlohmann과 같은 판단이다).
    target_include_directories(luil_webview2 SYSTEM INTERFACE "${LUIL_WEBVIEW2_ROOT}")
    target_link_libraries(luil_webview2
        INTERFACE
            luil_webview2_loader
            # 로더가 요구하는 것이다. ole32·uuid는 아카이브의 /DEFAULTLIB이 스스로
            # 끌어오지만 advapi32는 그러지 않는다 — 없으면 레지스트리·ETW·토큰
            # 기호 12개가 LNK2019로 남는다.
            advapi32)
    add_library(luil::webview2 ALIAS luil_webview2)

    message(STATUS "WebView2 SDK: ${LUIL_WEBVIEW2_ROOT}")
endfunction()

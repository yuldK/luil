cmake_minimum_required(VERSION 4.2.0 FATAL_ERROR)

cmake_policy(SET CMP0091 NEW)

# 최상위 빌드에서는 Skia 산출물(/MT·/MTd)과 맞는 정적 CRT를 강제한다.
# 소비자 프로젝트의 하위로 포함될 때는 소비자의 런타임 설정을 그대로 따른다.
if(NOT DEFINED PROJECT_NAME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(luil VERSION 0.1.0 LANGUAGES CXX RC)

# Catch2 등 외부 target을 라이브러리와 같은 CRT로 맞출 때 쓴다.
set(LUIL_MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")

set(LUIL_CXX_STANDARD 20)
set(LUIL_MINIMUM_MSVC_VERSION 1940)
set(LUIL_MINIMUM_WINDOWS_SDK_VERSION "10.0.22621.0")
set(LUIL_TARGET_ARCHITECTURE "x64")
set(LUIL_SUPPORTED_GENERATORS
    "Visual Studio 17 2022"
    "Visual Studio 18 2026"
)

set(LUIL_CMAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/cmake")
set(LUIL_THIRD_PARTY_DIRECTORY "${PROJECT_SOURCE_DIR}/third_party")
# 공개 헤더는 include/luil/ 아래에 있고 소비자는 이 디렉터리 하나만 본다.
# `#include "luil/..."` 접두가 소비자의 include 공간을 오염시키지 않는다.
# src/에는 구현과 비공개 header(win32 내부)만 남는다.
set(LUIL_INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include")
set(LUIL_SOURCE_DIRECTORY "${PROJECT_SOURCE_DIR}/src")
set(LUIL_TEST_DIRECTORY "${PROJECT_SOURCE_DIR}/tests")
set(LUIL_ASSET_DIRECTORY "${PROJECT_SOURCE_DIR}/assets/codicons")
set(LUIL_GENERATED_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
set(LUIL_GENERATED_INCLUDE_DIRECTORY "${LUIL_GENERATED_DIRECTORY}/include")
set(LUIL_GENERATED_CODICONS
    "${LUIL_GENERATED_INCLUDE_DIRECTORY}/luil/generated/codicons.h")
# 키 컬러 정의는 JSON에서 빌드 시점에 만든다.
# 소비자는 자기 카탈로그 JSON으로 바꿔칠 수 있다
# (같은 스키마여야 하며 기본 id `mint`를 담아야 한다).
set(LUIL_ACCENT_CATALOG "${PROJECT_SOURCE_DIR}/assets/accents.json"
    CACHE FILEPATH "Accent catalog JSON embedded at build time.")
set(LUIL_GENERATED_ACCENTS
    "${LUIL_GENERATED_INCLUDE_DIRECTORY}/luil/generated/accents.h")
# 판번 헤더는 project VERSION 하나에서 생성한다.
# version.cpp와 소비자 매크로가 같은 근원을 본다.
configure_file(
    "${LUIL_CMAKE_DIRECTORY}/version.h.in"
    "${LUIL_GENERATED_INCLUDE_DIRECTORY}/luil/generated/version.h"
    @ONLY)
set(LUIL_SOURCE_STYLE_SCRIPT "${PROJECT_SOURCE_DIR}/scripts/check_source_style.ps1")

# Skia 헤더와 라이브러리 위치를 정한다.
# 기본값은 fetch_skia.ps1의 설치 디렉터리이며 직접 빌드한 트리도 지정할 수 있다.
# skia-prep에 헤더가 없고 third_party/skia에 있으면 해당 소스 트리를 사용한다.
# 검사에는 패키지와 소스 트리에 같은 규칙을 적용한다.
if(EXISTS "${LUIL_THIRD_PARTY_DIRECTORY}/skia-prep/include/core/SkCanvas.h"
    OR NOT EXISTS "${LUIL_THIRD_PARTY_DIRECTORY}/skia/include/core/SkCanvas.h")
    set(LUIL_SKIA_DEFAULT_ROOT "${LUIL_THIRD_PARTY_DIRECTORY}/skia-prep")
else()
    set(LUIL_SKIA_DEFAULT_ROOT "${LUIL_THIRD_PARTY_DIRECTORY}/skia")
endif()
set(LUIL_SKIA_ROOT "${LUIL_SKIA_DEFAULT_ROOT}"
    CACHE PATH "Skia package (or source tree) that provides headers and libraries.")
set(LUIL_SKIA_BUILD_DEBUG "${LUIL_SKIA_ROOT}/out/skia-ui-debug"
    CACHE PATH "Skia Debug build directory.")
set(LUIL_SKIA_BUILD_RELEASE "${LUIL_SKIA_ROOT}/out/skia-ui-release"
    CACHE PATH "Skia Release build directory.")
set(LUIL_CATCH2_ROOT "${LUIL_THIRD_PARTY_DIRECTORY}/catch2")
# HTTP 응답의 JSON 본문 타입이다 (header-only submodule, 라이브러리 구성마다 필요).
set(LUIL_NLOHMANN_JSON_ROOT "${LUIL_THIRD_PARTY_DIRECTORY}/nlohmann_json")
# WebView2 SDK는 모든 라이브러리 구성에서 필요하다.
# scripts/fetch_webview2.ps1로 헤더와 정적 로더를 준비한다.
set(LUIL_WEBVIEW2_ROOT "${LUIL_THIRD_PARTY_DIRECTORY}/webview2-prep"
    CACHE PATH "WebView2 SDK package that provides the headers and the static loader.")

# Skia 빌드가 내는 라이브러리다.
# 직접 빌드에서는 external별로 나오므로 모두 링크한다.
# 이름은 파일 이름 그대로다 — png는 rust 코덱으로 세우고 그것은 bazel 산출물(.a)이라
# 확장자가 하나로 정해지지 않는다.
#  - jpeg가 셋인 것은 libjpeg-turbo 3.x가 정밀도(8·12·16bit)별로 같은 소스를
#    다시 컴파일하기 때문이고, webp가 둘인 것은 SSE4.1 조각이 따로 나기 때문이다.
set(LUIL_SKIA_COMPONENTS
    skia.lib
    skcms.lib
    spirv_cross.lib
    d3d12allocator.lib
    libjpeg.lib
    libjpeg12.lib
    libjpeg16.lib
    libwebp.lib
    libwebp_sse41.lib
    wuffs.lib
    librust_png_ffi_rs.a
    libcxx_cc.a)

# Rust PNG 정적 아카이브가 요구하는 Windows 시스템 라이브러리다.
# Rust의 `#[link]` 지시가 CMake의 정적 링크에 전파되지 않아 명시적으로 연결한다.
# 소켓 함수는 ws2_32, GetUserProfileDirectoryW는 userenv,
# NtReadFile·RtlNtStatusToDosError는 ntdll이 제공한다.
set(LUIL_SKIA_RUST_PNG_SYSTEM_LIBRARIES
    ws2_32
    userenv
    ntdll)

# 라이브러리가 요구하는 Skia GN 설정이다. configure에서 일치 여부를 검사한다.
# PNG는 APNG를 지원하는 Rust 코덱을 사용한다.
set(LUIL_SKIA_REQUIRED_ARGUMENTS
    "skia_use_direct3d=true"
    "skia_use_libjpeg_turbo_decode=true"
    "skia_use_libwebp_decode=true"
    "skia_use_wuffs=true"
    "skia_use_rust_png_decode=true")

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD ${LUIL_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# 기본 구성에는 라이브러리 target만 둔다.
# test와 개발 도구 target은 명시적으로 켤 때만 만들어진다.
#   cmake --preset vs2026-tests     → test target과 CTest 등록 포함
#   -DLUIL_BUILD_TOOLING=ON      → clang-format·style 검사 target 포함
option(LUIL_BUILD_TESTS "Build the luil test targets." OFF)
option(LUIL_BUILD_EXAMPLES "Build the luil example targets." OFF)
option(LUIL_BUILD_TOOLING "Build the clang-format and source style targets." OFF)
option(LUIL_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze for library sources." OFF)
# 설치 규칙은 최상위 빌드에서만 기본으로 켠다.
# add_subdirectory 소비자의 install에 끼어들지 않는다.
option(LUIL_INSTALL "Generate install and find_package rules." ${PROJECT_IS_TOP_LEVEL})

include("${LUIL_CMAKE_DIRECTORY}/macro.cmake")
include("${LUIL_CMAKE_DIRECTORY}/compiler/msvc.cmake")
include("${LUIL_CMAKE_DIRECTORY}/platform/windows.cmake")

luil_validate_windows_platform(
    ARCHITECTURE "${LUIL_TARGET_ARCHITECTURE}"
    MINIMUM_SDK_VERSION "${LUIL_MINIMUM_WINDOWS_SDK_VERSION}"
    SUPPORTED_GENERATORS ${LUIL_SUPPORTED_GENERATORS}
)
luil_validate_msvc(MINIMUM_VERSION "${LUIL_MINIMUM_MSVC_VERSION}")
luil_configure_msvc_link_directories()

# 내부 규율(경고 수준·/WX·분석)이다.
# 라이브러리·test·예제가 PRIVATE로 쓰고 소비자에게는 전파하지 않는다 —
# 라이브러리의 경고 정책이 소비자 앱의 빌드를 깨면 안 된다.
add_library(luil_options INTERFACE)
target_compile_features(luil_options INTERFACE cxx_std_${LUIL_CXX_STANDARD})
luil_apply_windows_options(luil_options)
luil_apply_msvc_options(
    luil_options
    ENABLE_ANALYZE "${LUIL_ENABLE_MSVC_ANALYZE}"
)

# 소비자에게 전파되는 사용 요건이다.
# 공개 헤더를 컴파일하는 데 실제로 필요한 것만 담는다:
# C++20과 UTF-8 소스(헤더에 한국어 주석·u8 리터럴)다.
# 공개 헤더는 <windows.h>를 품지 않으므로 매크로 차단도 필요 없다.
add_library(luil_usage INTERFACE)
target_compile_features(luil_usage INTERFACE cxx_std_${LUIL_CXX_STANDARD})
target_compile_options(luil_usage INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)

# 의존성은 submodule과 사용자가 손으로 빌드한 Skia로만 구성한다.
# 취득을 자동화하지 않으므로 configure는 네트워크를 전혀 쓰지 않는다.
include("${LUIL_CMAKE_DIRECTORY}/dependencies.cmake")
luil_find_dependencies(BUILD_TESTS ${LUIL_BUILD_TESTS})

# 고지 생성과 .rc 조각 구성 함수다.
# 소비자 실행 파일과 test가 쓴다.
include("${LUIL_CMAKE_DIRECTORY}/generate_notices.cmake")

# `include(CTest)`는 dashboard target을 함께 만들어 솔루션을 부풀린다.
# 필요한 것은 test 등록뿐이라 enable_testing만 쓴다.
if(LUIL_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)
# smoke test가 예제 실행 파일을 쓰므로 test 구성은 예제를 함께 만든다.
if(LUIL_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# clang-format target은 **있으면 만든다.**
# 그것이 없다고 라이브러리와 test를 세우지 못할 이유가 없는데, test preset이
# 이 옵션을 함께 켜므로 여기서 죽으면 빌드 전체가 막힌다 — 실제로 Visual Studio의
# C++ Clang 도구를 설치하지 않은 머신에서 그렇게 걸렸다.
if(LUIL_BUILD_TOOLING)
    find_program(LUIL_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    # 사람이 -DLUIL_CLANG_FORMAT_EXECUTABLE로 준 것이 있으면 그것을 쓴다.
    if(NOT LUIL_CLANG_FORMAT_EXECUTABLE)
        luil_find_visual_studio_clang_format(LUIL_CLANG_FORMAT_EXECUTABLE)
    endif()
    if(NOT LUIL_CLANG_FORMAT_EXECUTABLE OR NOT LUIL_POWERSHELL_EXECUTABLE)
        set(luil_tooling_missing "")
        if(NOT LUIL_CLANG_FORMAT_EXECUTABLE)
            list(APPEND luil_tooling_missing
                "clang-format (Visual Studio Installer: \"C++ Clang tools for Windows\")")
        endif()
        if(NOT LUIL_POWERSHELL_EXECUTABLE)
            list(APPEND luil_tooling_missing "pwsh or powershell")
        endif()
        list(JOIN luil_tooling_missing " and " luil_tooling_missing_text)
        message(WARNING
            "The formatting targets are not available: "
            "${luil_tooling_missing_text} was not found.\n"
            "luil_format and luil_format_check are skipped. Everything "
            "else - the library, the tests and the examples - still builds.\n"
            "Set LUIL_CLANG_FORMAT_EXECUTABLE to point at one explicitly.")
        unset(luil_tooling_missing)
        unset(luil_tooling_missing_text)
    else()
        luil_add_clang_format_targets(
            CLANG_FORMAT_EXECUTABLE "${LUIL_CLANG_FORMAT_EXECUTABLE}"
            FORMAT_TARGET luil_format
            CHECK_TARGET luil_format_check
            SOURCE_DIRECTORIES
                "${LUIL_INCLUDE_DIRECTORY}"
                "${LUIL_SOURCE_DIRECTORY}"
                "${LUIL_TEST_DIRECTORY}"
            FILE_EXTENSIONS cpp h
            CHECK_COMMAND
                "${LUIL_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${LUIL_SOURCE_STYLE_SCRIPT}"
                -root "${PROJECT_SOURCE_DIR}"
        )
        luil_set_ide_folder("Tooling"
            luil_format
            luil_format_check)
    endif()
endif()

luil_set_ide_folder("Libraries" luil)

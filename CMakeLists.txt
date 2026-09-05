cmake_minimum_required(VERSION 4.2.0 FATAL_ERROR)
cmake_policy(SET CMP0091 NEW)
project(luil VERSION 0.1.0 LANGUAGES CXX RC)

set(LUIL_CXX_STANDARD 20)
set(LUIL_MINIMUM_MSVC_VERSION 1940)
set(LUIL_MINIMUM_WINDOWS_SDK_VERSION "10.0.22621.0")
set(LUIL_TARGET_ARCHITECTURE "x64")
set(LUIL_SUPPORTED_GENERATORS "Visual Studio 17 2022" "Visual Studio 18 2026")
include(cmake/compiler/msvc.cmake)
include(cmake/platform/windows.cmake)
luil_validate_windows_platform(
    ARCHITECTURE "${LUIL_TARGET_ARCHITECTURE}"
    MINIMUM_SDK_VERSION "${LUIL_MINIMUM_WINDOWS_SDK_VERSION}"
    SUPPORTED_GENERATORS ${LUIL_SUPPORTED_GENERATORS})
luil_validate_msvc(MINIMUM_VERSION "${LUIL_MINIMUM_MSVC_VERSION}")

# 환경을 먼저 세우고 다음 기능부터 구현 소스를 더한다.
add_library(luil INTERFACE)
add_library(luil::luil ALIAS luil)
target_compile_features(luil INTERFACE cxx_std_${LUIL_CXX_STANDARD})

include_guard(GLOBAL)

# iOS 구성을 검사한다 (iPhone·iPad, docs/concepts/build-system.md의 "iOS").
# Skia 패키지(ios-arm64·ios-arm64-simulator)가 정한 것과 어긋나면 링크에서야 드러나거나(아키텍처·SDK) 오래된 기기에서
# 실행할 때에야 드러나므로(최소 판) configure에서 원인과 함께 잡는다.
#  - 아키텍처는 arm64 하나다. 기기와 Apple Silicon 시뮬레이터가 모두 arm64이고, x86_64 시뮬레이터는 패키지가 없다.
#  - SDK(`CMAKE_OSX_SYSROOT`)는 iphoneos(기기)나 iphonesimulator(시뮬레이터)다. 어느 쪽인지가 Skia 패키지를 고른다.
#  - 최소 판은 `CMAKE_OSX_DEPLOYMENT_TARGET`으로 정한다. 비워 두면 SDK 판이 그대로 최소 판이 된다.
function(luil_validate_ios_platform)
    set(one_value_arguments ARCHITECTURE MINIMUM_VERSION)
    cmake_parse_arguments(PARSE_ARGV 0 arguments "" "${one_value_arguments}" "")

    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments passed to luil_validate_ios_platform: "
            "${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arguments_ARCHITECTURE OR NOT arguments_MINIMUM_VERSION)
        message(FATAL_ERROR "ARCHITECTURE and MINIMUM_VERSION are required.")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        message(FATAL_ERROR "luil_validate_ios_platform needs an iOS toolchain (CMAKE_SYSTEM_NAME=iOS).")
    endif()

    if(NOT CMAKE_OSX_SYSROOT MATCHES "iphoneos|iPhoneOS|iphonesimulator|iPhoneSimulator")
        message(FATAL_ERROR
            "CMAKE_OSX_SYSROOT must be iphoneos (devices) or iphonesimulator (the simulator). "
            "Current value: ${CMAKE_OSX_SYSROOT}\n"
            "The ios-arm64 and ios-arm64-simulator presets in CMakePresets.json set it.")
    endif()

    if(CMAKE_OSX_ARCHITECTURES)
        set(architectures ${CMAKE_OSX_ARCHITECTURES})
    else()
        set(architectures "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(NOT architectures STREQUAL arguments_ARCHITECTURE)
        message(FATAL_ERROR
            "The iOS architecture must be ${arguments_ARCHITECTURE}. "
            "Current value: ${architectures}\n"
            "The Skia packages are built for that architecture only (no x86_64 simulator). "
            "Set CMAKE_OSX_ARCHITECTURES=${arguments_ARCHITECTURE}.")
    endif()

    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        message(FATAL_ERROR
            "CMAKE_OSX_DEPLOYMENT_TARGET is not set.\n"
            "Set it to ${arguments_MINIMUM_VERSION} or newer before the first configure. "
            "The ios presets in CMakePresets.json set it.")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS arguments_MINIMUM_VERSION)
        message(FATAL_ERROR
            "iOS ${arguments_MINIMUM_VERSION} or newer is required as the deployment target. "
            "Current value: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endfunction()

include_guard(GLOBAL)

# macOS 구성을 검사한다.
# Skia 패키지(mac-arm64)가 정한 것과 어긋나면 링크에서야 드러나거나(아키텍처) 오래된 OS에서
# 실행할 때에야 드러나므로(최소 판) configure에서 원인과 함께 잡는다.
#  - 아키텍처는 하나다. 패키지가 그것만 담는다. universal(`arm64;x86_64`)도 받지 않는다 —
#    한쪽 조각의 아카이브가 없어 링크가 선다.
#  - 최소 판은 `CMAKE_OSX_DEPLOYMENT_TARGET`으로 정한다. 비워 두면 빌드한 기계의 SDK 판이
#    그대로 최소 판이 되어, 그보다 오래된 macOS에서 실행 파일이 뜨지 않는다.
#  - 생성기는 가리지 않는다. preset은 Ninja Multi-Config를 쓴다.
function(luil_validate_macos_platform)
    set(one_value_arguments ARCHITECTURE MINIMUM_VERSION)
    cmake_parse_arguments(PARSE_ARGV 0 arguments "" "${one_value_arguments}" "")

    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments passed to luil_validate_macos_platform: "
            "${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arguments_ARCHITECTURE OR NOT arguments_MINIMUM_VERSION)
        message(FATAL_ERROR "ARCHITECTURE and MINIMUM_VERSION are required.")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(FATAL_ERROR "luil_validate_macos_platform needs a macOS toolchain.")
    endif()

    # `CMAKE_OSX_ARCHITECTURES`를 주지 않으면 컴파일러의 기본 대상(빌드한 기계)으로 세운다.
    if(CMAKE_OSX_ARCHITECTURES)
        set(architectures ${CMAKE_OSX_ARCHITECTURES})
    else()
        set(architectures "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(NOT architectures STREQUAL arguments_ARCHITECTURE)
        message(FATAL_ERROR
            "The macOS architecture must be ${arguments_ARCHITECTURE}. "
            "Current value: ${architectures}\n"
            "The Skia package is built for that architecture only. "
            "Set CMAKE_OSX_ARCHITECTURES=${arguments_ARCHITECTURE}.")
    endif()

    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        message(FATAL_ERROR
            "CMAKE_OSX_DEPLOYMENT_TARGET is not set.\n"
            "Set it to ${arguments_MINIMUM_VERSION} or newer before the first configure. "
            "The macos-arm64 presets in CMakePresets.json set it.")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS arguments_MINIMUM_VERSION)
        message(FATAL_ERROR
            "macOS ${arguments_MINIMUM_VERSION} or newer is required as the deployment target. "
            "Current value: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endfunction()

# Apple 헤더가 식별자를 매크로로 덮지 않게 한다.
#  - `AssertMacros.h`는 `check`·`verify`·`require`를 함수 모양 매크로로 정의한다. luil의 공개 헤더에
#    `check`라는 이름이 있어(메뉴 항목 종류, 메시지 상자의 확인란), Apple 헤더를 들이는 .mm에서 그
#    이름 뒤에 괄호가 오면 매크로로 펼쳐진다. 밑줄 없는 이름을 끄는 것이 Apple이 정한 길이다.
function(luil_apply_macos_options target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Target not found for macOS options: ${target}")
    endif()

    get_target_property(target_type "${target}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(option_scope INTERFACE)
    else()
        set(option_scope PRIVATE)
    endif()

    target_compile_definitions("${target}" ${option_scope}
        __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0)
endfunction()

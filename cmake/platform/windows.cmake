include_guard(GLOBAL)

function(luil_validate_windows_platform)
    set(one_value_arguments ARCHITECTURE MINIMUM_SDK_VERSION)
    set(multi_value_arguments SUPPORTED_GENERATORS)
    cmake_parse_arguments(
        PARSE_ARGV 0
        arguments
        ""
        "${one_value_arguments}"
        "${multi_value_arguments}")

    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments passed to luil_validate_windows_platform: "
            "${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arguments_ARCHITECTURE OR NOT arguments_MINIMUM_SDK_VERSION)
        message(FATAL_ERROR "ARCHITECTURE and MINIMUM_SDK_VERSION are required.")
    endif()
    if(NOT arguments_SUPPORTED_GENERATORS)
        message(FATAL_ERROR "SUPPORTED_GENERATORS is required.")
    endif()
    if(NOT WIN32)
        message(FATAL_ERROR "${PROJECT_NAME} supports only Windows 11 x64.")
    endif()

    list(FIND arguments_SUPPORTED_GENERATORS "${CMAKE_GENERATOR}" generator_index)
    if(generator_index EQUAL -1)
        string(JOIN ", " supported_generators ${arguments_SUPPORTED_GENERATORS})
        message(FATAL_ERROR
            "A supported generator is required: ${supported_generators}. "
            "Current value: ${CMAKE_GENERATOR}")
    endif()
    if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL arguments_ARCHITECTURE)
        message(FATAL_ERROR
            "The generator platform must be ${arguments_ARCHITECTURE}. "
            "Current value: ${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        message(FATAL_ERROR "Could not determine the Windows SDK selected by Visual Studio.")
    endif()
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_LESS arguments_MINIMUM_SDK_VERSION)
        message(FATAL_ERROR
            "Windows SDK ${arguments_MINIMUM_SDK_VERSION} or newer is required. "
            "Current value: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    endif()
endfunction()

function(luil_apply_windows_options target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Target not found for Windows options: ${target}")
    endif()

    get_target_property(target_type "${target}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(option_scope INTERFACE)
    else()
        set(option_scope PRIVATE)
    endif()

    target_compile_definitions("${target}" ${option_scope}
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN)
endfunction()

function(luil_disable_automatic_windows_manifest target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Target not found for the manifest option: ${target}")
    endif()
    target_link_options("${target}" PRIVATE /MANIFEST:NO)
endfunction()

# Visual Studio 설치 디렉터리에서 clang-format을 찾는다.
# VS 2022는 VC/Tools/Llvm/bin 또는 VC/Tools/Llvm/x64/bin을,
# VS 2026은 VC/Tools/Llvm/x64/bin을 사용한다.
# ProgramFiles와 ProgramFiles(x86)의 설치 경로를 모두 확인한다.
# 도구가 없으면 configure를 계속하고 format target만 생략한다.
function(luil_find_visual_studio_clang_format output_variable)
    if(NOT output_variable)
        message(FATAL_ERROR "An output variable for the clang-format path is required.")
    endif()

    set(visual_studio_roots
        "$ENV{ProgramFiles}/Microsoft Visual Studio"
        "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio")
    set(visual_studio_llvm_directories "")
    foreach(root IN LISTS visual_studio_roots)
        if(NOT root OR NOT IS_DIRECTORY "${root}")
            continue()
        endif()
        # `*/*`는 Visual Studio 버전과 에디션에 해당한다.
        # ARM64 실행 파일을 선택하지 않도록 Llvm/bin과 Llvm/x64/bin만 확인한다.
        # 이 프로젝트의 대상 아키텍처는 x64다 (`LUIL_TARGET_ARCHITECTURE`).
        foreach(suffix IN ITEMS "bin" "x64/bin")
            file(GLOB found LIST_DIRECTORIES true
                "${root}/*/*/VC/Tools/Llvm/${suffix}")
            list(APPEND visual_studio_llvm_directories ${found})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES visual_studio_llvm_directories)

    find_program(clang_format_executable
        NAMES clang-format
        HINTS ${visual_studio_llvm_directories}
        NO_CACHE)
    if(NOT clang_format_executable)
        set(clang_format_executable "")
    endif()
    set("${output_variable}" "${clang_format_executable}" PARENT_SCOPE)
endfunction()

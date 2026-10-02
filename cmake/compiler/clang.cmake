include_guard(GLOBAL)

# clang(NDK)으로 luil 자신을 컴파일할 때의 내부 규율이다.
# MSVC의 `/W4 /permissive- /WX`와 같은 무게로 둔다 — 같은 소스가 두 컴파일러에서
# 같은 경고 수준을 지나야, 한쪽에서만 숨는 실수가 생기지 않는다.
function(luil_apply_clang_options target)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "WARNINGS_AS_ERRORS" "")

    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments passed to luil_apply_clang_options: "
            "${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Target not found for clang options: ${target}")
    endif()

    get_target_property(target_type "${target}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(option_scope INTERFACE)
    else()
        set(option_scope PRIVATE)
    endif()

    target_compile_options("${target}" ${option_scope}
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wall>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wextra>)

    # 기본은 켬이다 (MSVC의 /WX와 같은 규칙).
    if(NOT DEFINED arguments_WARNINGS_AS_ERRORS OR arguments_WARNINGS_AS_ERRORS)
        target_compile_options("${target}" ${option_scope}
            $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror>)
    endif()
endfunction()

include_guard(GLOBAL)

# clang(NDK·Xcode)으로 luil 자신을 컴파일할 때의 내부 규율이다.
# MSVC의 `/W4 /permissive- /WX`와 같은 무게로 둔다 — 같은 소스가 두 컴파일러에서
# 같은 경고 수준을 지나야, 한쪽에서만 숨는 실수가 생기지 않는다.
#  - 컴파일러 ID를 둘 다 적는다. Xcode의 clang은 `Clang`이 아니라 `AppleClang`이라, 하나만 적으면
#    macOS에서 경고 옵션과 -Werror가 **오류 없이** 통째로 빠진다.
#  - 언어도 둘 다 적는다. Objective-C++(.mm)는 `CXX`가 아니라 `OBJCXX`로 컴파일된다.
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

    set(is_clang_cxx "$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>")
    set(is_clang_objcxx "$<COMPILE_LANG_AND_ID:OBJCXX,Clang,AppleClang>")
    set(is_clang "$<OR:${is_clang_cxx},${is_clang_objcxx}>")

    target_compile_options("${target}" ${option_scope}
        $<${is_clang}:-Wall>
        $<${is_clang}:-Wextra>)

    # 기본은 켬이다 (MSVC의 /WX와 같은 규칙).
    if(NOT DEFINED arguments_WARNINGS_AS_ERRORS OR arguments_WARNINGS_AS_ERRORS)
        target_compile_options("${target}" ${option_scope}
            $<${is_clang}:-Werror>)
    endif()
endfunction()

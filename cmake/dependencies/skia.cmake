include_guard(GLOBAL)

# Skia 헤더·정적 라이브러리·고지·args.gn을 검사해 imported target으로 노출한다.
# 패키지와 직접 빌드한 Skia 트리에 같은 검사를 적용한다 (docs/skia-build.md).
# CMake는 네트워크나 GN을 실행하지 않는다.
# 검사는 Debug·Release 단위로 독립이며 configure 대상 구성의 산출물만 요구한다.
#
# 검사가 보는 것은 **패키지가 자기 옆에 적어 둔 파일**이다 — args.gn과
# toolchain.json. 컴파일러를 찾지도 부르지도 않으므로, 미리 빌드된 패키지를 쓰는
# 소비자에게 clang 설치를 요구하지 않는다. 소비자의 컴파일러는 그대로 MSVC다.

# 산출물이 실제로 luil와 맞는지 configure 시점에 검사한다.
# 잘못된 옵션으로 빌드한 Skia를 링크 오류가 아니라 원인이 드러나는 메시지로 잡는다.
function(luil_check_skia_arguments build_directory)
    set(arguments_file "${build_directory}/args.gn")
    if(NOT EXISTS "${arguments_file}")
        message(FATAL_ERROR
            "Skia args.gn was not found: ${arguments_file}\n"
            "It is part of the package, so this is not a complete one.\n"
            "Fetch it again: scripts/fetch_skia.ps1 -Force\n"
            "See docs/skia-build.md.")
    endif()

    file(READ "${arguments_file}" arguments_text)
    set(LUIL_SKIA_ARGUMENTS_TEXT "${arguments_text}" PARENT_SCOPE)
    foreach(requirement IN LISTS LUIL_SKIA_REQUIRED_ARGUMENTS)
        string(REPLACE "=" ";" requirement_parts "${requirement}")
        list(GET requirement_parts 0 requirement_name)
        list(GET requirement_parts 1 requirement_value)
        if(NOT arguments_text MATCHES
            "${requirement_name}[ \t]*=[ \t]*${requirement_value}")
            message(FATAL_ERROR
                "Skia was built without ${requirement_name} = ${requirement_value}.\n"
                "Build directory: ${build_directory}\n"
                "Rebuild Skia with the argument file in third_party/skia-args.")
        endif()
    endforeach()
endfunction()

# 무엇이 이 산출물을 컴파일했는지 본다.
# args.gn은 "그렇게 gen했다"는 말이고 CRT 지시문은 두 도구사슬이 똑같이 내므로,
# 컴파일러는 패키지가 따로 적어 둔 toolchain.json이 말한다. 그것이 없는 트리
# (손으로 gn gen한 자리)에서는 args.gn의 clang_win이 근거다.
#
# 이것이 계약인 이유는 어긋나도 링크가 성립하기 때문이다. SkRasterPipeline의
# 벡터 형이 clang·gcc의 확장이라, MSVC로 세운 Skia는 CPU 래스터를 픽셀 하나씩
# 처리한다 — 4000x7000 cubic 축소가 13.6 ms 대신 799.8 ms다
# (skia-prep docs/skia-build.md 5.4의 실측).
function(luil_check_skia_toolchain flavor build_directory arguments_text)
    set(toolchain_file "${build_directory}/toolchain.json")
    set(compiler "")
    if(EXISTS "${toolchain_file}")
        file(READ "${toolchain_file}" toolchain_text)
        string(JSON compiler ERROR_VARIABLE toolchain_error
            GET "${toolchain_text}" compiler)
        if(toolchain_error)
            set(compiler "")
        endif()
    elseif(arguments_text MATCHES "clang_win[ \t]*=[ \t\r\n]*\"[^\"]")
        set(compiler "clang-cl")
    endif()

    if(NOT compiler STREQUAL "${LUIL_SKIA_REQUIRED_TOOLCHAIN}")
        if(compiler)
            set(recorded "${compiler}")
        else()
            set(recorded "not recorded")
        endif()
        message(FATAL_ERROR
            "Skia was not compiled with ${LUIL_SKIA_REQUIRED_TOOLCHAIN} "
            "(${flavor}: ${recorded}).\n"
            "Build directory: ${build_directory}\n"
            "luil needs it for the CPU raster path: SkRasterPipeline only "
            "vectorises under clang or gcc, so a Skia built with anything else "
            "links fine and then rasterises one pixel at a time.\n"
            "With the pinned package: scripts/fetch_skia.ps1 -Force\n"
            "  The package is already compiled - nothing here needs a clang "
            "installation, and your own compiler stays MSVC.\n"
            "With a Skia tree you built by hand: build it with clang-cl and "
            "keep the toolchain.json that records it next to the libraries.\n"
            "See docs/skia-build.md.")
    endif()
endfunction()

# MSVC 소비자와 같은 ABI·CRT로 세운 것인지 본다.
#  - is_trivial_abi가 참이면 sk_sp 같은 형이 clang에서만
#    `[[clang::trivial_abi]]`를 달아 호출 규약이 바뀐다. 그 헤더를 MSVC로
#    컴파일하는 luil와 소비자에게는 그 속성이 없으므로, 같은 형이 서로 다른
#    ABI가 되고 링크가 성립한 채 런타임에 깨진다.
#  - CRT는 Debug가 /MTd, 그 밖의 구성이 /MT다. 링커의 /FAILIFMISMATCH가 결국
#    잡지만 그 메시지는 무엇을 어떻게 고쳐야 하는지 말하지 않는다.
function(luil_check_skia_abi flavor build_directory arguments_text)
    if(NOT arguments_text MATCHES "is_trivial_abi[ \t]*=[ \t]*false")
        message(FATAL_ERROR
            "Skia was not built with is_trivial_abi = false.\n"
            "Build directory: ${build_directory}\n"
            "That argument makes clang give sk_sp and friends "
            "[[clang::trivial_abi]], which changes how they are passed. "
            "luil and its consumers compile the same headers with MSVC, "
            "where the attribute does not exist - the two halves would "
            "disagree at run time while linking cleanly.\n"
            "Fetch the pinned package (scripts/fetch_skia.ps1 -Force), or "
            "rebuild Skia with the argument file in third_party/skia-args.")
    endif()

    # Android에는 MSVC CRT가 없다. 대신 패키지가 기대하는 최저 API 수준을 본다.
    if(NOT LUIL_SKIA_TARGET STREQUAL "win-x64")
        luil_check_skia_android_api("${build_directory}" "${arguments_text}")
        return()
    endif()

    if(flavor STREQUAL "Debug")
        set(expected_runtime "/MTd")
        set(other_runtime "/MT")
    else()
        set(expected_runtime "/MT")
        set(other_runtime "/MTd")
    endif()
    set(runtime_flags "")
    string(REGEX MATCHALL "\"/MTd?\"" runtime_matches "${arguments_text}")
    foreach(match IN LISTS runtime_matches)
        string(REPLACE "\"" "" match "${match}")
        list(APPEND runtime_flags "${match}")
    endforeach()
    if(NOT "${expected_runtime}" IN_LIST runtime_flags
        OR "${other_runtime}" IN_LIST runtime_flags)
        message(FATAL_ERROR
            "Skia's ${flavor} build does not use ${expected_runtime}.\n"
            "Build directory: ${build_directory}\n"
            "extra_cflags says: ${runtime_flags}\n"
            "luil links the static CRT and a ${flavor} configuration needs the "
            "${expected_runtime} flavour of it. Mixing them is what the "
            "linker's /FAILIFMISMATCH would report later, without the "
            "reason.\n"
            "Fetch the pinned package for this configuration: "
            "scripts/fetch_skia.ps1 -Configuration ${flavor} -Force")
    endif()
endfunction()

# Android 패키지가 세워진 API 수준(`ndk_api`)이 이 구성의 API 수준 이하인지 본다.
# 패키지가 쓰는 함수가 그 수준부터 있으므로, 더 낮은 구성은 링크가 되어도 그보다
# 오래된 기기에서 기호를 찾지 못한다 (skia-prep docs/skia-build.md 8.4).
function(luil_check_skia_android_api build_directory arguments_text)
    if(NOT arguments_text MATCHES "ndk_api[ \t]*=[ \t]*([0-9]+)")
        message(FATAL_ERROR
            "Skia's args.gn does not record ndk_api.\n"
            "Build directory: ${build_directory}\n"
            "Fetch the pinned package: scripts/fetch_skia.ps1 -Target ${LUIL_SKIA_TARGET} -Force")
    endif()
    set(package_api "${CMAKE_MATCH_1}")
    if(ANDROID_PLATFORM_LEVEL LESS package_api)
        message(FATAL_ERROR
            "The Skia package needs Android API ${package_api} or newer, "
            "but this configure targets ${ANDROID_PLATFORM_LEVEL}.\n"
            "Raise ANDROID_PLATFORM to android-${package_api}.")
    endif()
endfunction()

# flavor 하나의 산출물 전체를 검사한다.
# Skia 빌드는 skia.lib 하나가 아니라 external별 라이브러리를 함께 낸다.
function(luil_check_skia_build flavor build_directory)
    foreach(component IN LISTS ARGN)
        if(NOT EXISTS "${build_directory}/${component}")
            message(FATAL_ERROR
                "A Skia ${flavor} build output is missing: "
                "${build_directory}/${component}\n"
                "This configure builds a ${flavor}-flavored configuration, "
                "so that Skia flavour is required.\n"
                "Fetch it: scripts/fetch_skia.ps1 -Configuration ${flavor}\n"
                "or point LUIL_SKIA_BUILD_DEBUG/RELEASE at existing builds.")
        endif()
    endforeach()
    luil_check_skia_arguments("${build_directory}")
    # 기능 인자가 맞아도 무엇이 어떻게 컴파일했는가는 따로다.
    # 그쪽이 어긋나면 링크가 성립한 채 조용히 다른 물건이 된다.
    luil_check_skia_toolchain("${flavor}" "${build_directory}"
        "${LUIL_SKIA_ARGUMENTS_TEXT}")
    luil_check_skia_abi("${flavor}" "${build_directory}"
        "${LUIL_SKIA_ARGUMENTS_TEXT}")
endfunction()

# 검증된 flavor의 location만 노출한다.
# per-config location이 독립이라 반대쪽 flavor가 없어도 target은 성립한다.
#  - 인자는 **파일 이름**이다. rust png를 쓰는 Skia는 bazel 산출물(.a)을 내므로
#    확장자가 하나로 정해지지 않는다. target 이름은 그 이름에서 확장자를 뗀다.
function(luil_add_skia_component file_name)
    get_filename_component(name "${file_name}" NAME_WE)
    set(target "luil_skia_${name}")
    add_library("${target}" STATIC IMPORTED GLOBAL)
    set(imported_configurations "")
    if(LUIL_SKIA_NEEDS_DEBUG)
        list(APPEND imported_configurations DEBUG)
        set_target_properties("${target}" PROPERTIES
            IMPORTED_LOCATION_DEBUG "${LUIL_SKIA_BUILD_DEBUG}/${file_name}")
    endif()
    if(LUIL_SKIA_NEEDS_RELEASE)
        list(APPEND imported_configurations RELEASE)
        # 접미사 없는 location은 build type을 정하지 않은 단일 구성 빌드가
        # 결정적으로 Release 산출물을 집게 한다.
        set_target_properties("${target}" PROPERTIES
            IMPORTED_LOCATION_RELEASE "${LUIL_SKIA_BUILD_RELEASE}/${file_name}"
            IMPORTED_LOCATION "${LUIL_SKIA_BUILD_RELEASE}/${file_name}")
    endif()
    set_target_properties("${target}" PROPERTIES
        IMPORTED_CONFIGURATIONS "${imported_configurations}")
    # Debug 외 구성(MinSizeRel·RelWithDebInfo·사용자 정의)은 Release 산출물을 쓴다.
    # 전역 변수는 target 생성 이후라 여기 닿지 않으므로 target 속성으로 직접 건다.
    foreach(configuration IN LISTS LUIL_SKIA_MAPPED_CONFIGURATIONS)
        set_target_properties("${target}" PROPERTIES
            "MAP_IMPORTED_CONFIG_${configuration}" "Release")
    endforeach()
endfunction()

function(luil_find_skia)
    if(NOT IS_DIRECTORY "${LUIL_SKIA_ROOT}"
        OR NOT EXISTS "${LUIL_SKIA_ROOT}/include/core/SkCanvas.h")
        message(FATAL_ERROR
            "The Skia package was not found: ${LUIL_SKIA_ROOT}\n"
            "Fetch it: scripts/fetch_skia.ps1 -Target ${LUIL_SKIA_TARGET}\n"
            "It downloads the package pinned by third_party/skia-prep*.json - "
            "headers, static libraries and notices, nothing else.\n"
            "See docs/skia-build.md. To use a Skia tree you built by hand "
            "instead, point LUIL_SKIA_ROOT at it.")
    endif()

    # 이 configure가 실제로 빌드하는 구성을 모은다.
    # Debug 구성은 /MTd 산출물을, 그 밖의 구성은 /MT 산출물을 쓴다.
    get_property(generator_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(generator_is_multi_config)
        set(build_configurations ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(build_configurations "${CMAKE_BUILD_TYPE}")
    endif()
    # 구성이 비면(단일 구성 + build type 미지정) Release 산출물로 링크한다.
    if(NOT build_configurations)
        set(build_configurations "Release")
    endif()

    set(LUIL_SKIA_NEEDS_DEBUG FALSE)
    set(LUIL_SKIA_NEEDS_RELEASE FALSE)
    set(LUIL_SKIA_MAPPED_CONFIGURATIONS "")
    foreach(configuration IN LISTS build_configurations)
        string(TOUPPER "${configuration}" configuration_name)
        if(configuration_name STREQUAL "DEBUG")
            set(LUIL_SKIA_NEEDS_DEBUG TRUE)
        else()
            set(LUIL_SKIA_NEEDS_RELEASE TRUE)
            if(NOT configuration_name STREQUAL "RELEASE")
                list(APPEND LUIL_SKIA_MAPPED_CONFIGURATIONS
                    "${configuration_name}")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES LUIL_SKIA_MAPPED_CONFIGURATIONS)

    set(skia_components ${LUIL_SKIA_COMPONENTS})

    set(validated_flavors "")
    if(LUIL_SKIA_NEEDS_DEBUG)
        luil_check_skia_build(Debug "${LUIL_SKIA_BUILD_DEBUG}" ${skia_components})
        list(APPEND validated_flavors Debug)
    endif()
    if(LUIL_SKIA_NEEDS_RELEASE)
        luil_check_skia_build(Release "${LUIL_SKIA_BUILD_RELEASE}" ${skia_components})
        list(APPEND validated_flavors Release)
    endif()

    set(component_targets "")
    foreach(component IN LISTS skia_components)
        luil_add_skia_component("${component}")
        get_filename_component(component_stem "${component}" NAME_WE)
        list(APPEND component_targets "luil_skia_${component_stem}")
    endforeach()

    add_library(luil_skia INTERFACE)
    target_include_directories(luil_skia SYSTEM INTERFACE "${LUIL_SKIA_ROOT}")
    if(LUIL_SKIA_TARGET STREQUAL "win-x64")
        set(component_link_items ${component_targets})
    else()
        # Android 소비자의 링크 계약이다 (skia-prep docs/skia-build.md 8.5).
        #  - rust png 아카이브가 Skia 오브젝트를 함께 담아 같은 심볼이 겹친다. lld는
        #    그것을 오류로 보므로 중복 정의를 허용하고, 먼저 나온 정의가 이기도록
        #    libskia.a를 맨 앞에 둔다. 목록의 순서가 곧 그 순서다 (CMakeLists.txt).
        #  - 아카이브끼리 서로 기호를 주고받으므로 한 묶음으로 다시 훑는다. 묶음은
        #    목록 순서를 지킨다 — target_link_libraries의 순서만으로는 CMake가
        #    imported target을 다시 늘어놓을 수 있다.
        string(JOIN "," component_group ${component_targets})
        set(component_link_items "$<LINK_GROUP:RESCAN,${component_group}>")
        target_link_options(luil_skia INTERFACE "LINKER:--allow-multiple-definition")
    endif()
    target_link_libraries(luil_skia
        INTERFACE
            ${component_link_items}
            # Skia가 요구하는 시스템 라이브러리다 (대상마다 CMakeLists.txt에 있다).
            # luil가 직접 쓰는 것은 src/CMakeLists.txt에 따로 있다.
            ${LUIL_SKIA_SYSTEM_LIBRARIES}
            # rust png 코덱이 더 요구하는 것이다 (CMakeLists.txt에 이유가 있다).
            ${LUIL_SKIA_RUST_PNG_SYSTEM_LIBRARIES})
    # APNG(움직이는 png)를 읽는다. rust 코덱을 요구 인자에 두었으므로 늘 그렇다.
    # 매크로를 남겨 두는 것은 소비자가 이미 이것으로 가드한 코드를 쓸 수 있어서다.
    target_compile_definitions(luil_skia INTERFACE LUIL_ANIMATED_PNG=1)
    # 소비자가 쓰는 공개 이름이다.
    # 다른 Skia 패키지의 별칭 (unofficial::skia::skia 등)과 충돌하지 않도록 하나만 정의한다.
    add_library(luil::skia ALIAS luil_skia)

    # luil-targets.cmake가 뒤에 만드는 imported target도 같은 맵핑을 따른다.
    # 앱은 Debug와 Release만 쓰지만 IDE가 다른 구성을 만들 수 있어 매핑을 명시한다.
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "Release;" PARENT_SCOPE)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release;" PARENT_SCOPE)
    # 설치 config가 설치본의 구성 완결성을 같은 flavor 기준으로 검사한다.
    set(LUIL_SKIA_NEEDS_DEBUG "${LUIL_SKIA_NEEDS_DEBUG}" PARENT_SCOPE)
    set(LUIL_SKIA_NEEDS_RELEASE "${LUIL_SKIA_NEEDS_RELEASE}" PARENT_SCOPE)

    string(REPLACE ";" ", " validated_flavors_text "${validated_flavors}")
    message(STATUS
        "Skia: ${LUIL_SKIA_ROOT} "
        "(${LUIL_SKIA_TARGET}; ${validated_flavors_text}; ${LUIL_SKIA_REQUIRED_TOOLCHAIN})")
endfunction()

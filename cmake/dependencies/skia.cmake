include_guard(GLOBAL)

# Skia 헤더·정적 라이브러리·고지·args.gn을 검사해 imported target으로 노출한다.
# 패키지와 직접 빌드한 Skia 트리에 같은 검사를 적용한다 (docs/skia-build.md).
# CMake는 네트워크나 GN을 실행하지 않는다.
# 검사는 Debug·Release 단위로 독립이며 configure 대상 구성의 산출물만 요구한다.

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
            "Fetch it: scripts/fetch_skia.ps1\n"
            "It downloads the package pinned by third_party/skia-prep.json - "
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
    target_link_libraries(luil_skia
        INTERFACE
            ${component_targets}
            # Skia가 요구하는 Windows 시스템 라이브러리다.
            # luil가 직접 쓰는 것은 src/CMakeLists.txt에 따로 있다.
            d3dcompiler
            FontSub
            Usp10
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
    message(STATUS "Skia: ${LUIL_SKIA_ROOT} (${validated_flavors_text})")
endfunction()

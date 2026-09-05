include_guard(GLOBAL)

# 실행 파일 resource에 넣을 제3자 고지를 만든다.
# 원문은 Skia 쪽과 자산 디렉터리에서 직접 읽는다.
#
# Skia 쪽 원문을 읽는 자리는 두 갈래다.
#   1. 패키지  : `NOTICE.md` 한 장이 Skia와 그 external, rust crate까지 전부
#                담고 있다 (skia-prep의 pack_skia.ps1이 만든다). 이쪽을 먼저 본다.
#   2. 소스 트리: `third_party/externals` 아래에서 external마다 읽는다.
#                손으로 빌드한 Skia 트리를 가리킨 소비자를 위한 갈래다.
#
# 소스 트리 갈래는 rust 코덱의 crate 고지를 **채우지 못한다** — bazel이 자기 캐시에
# 받아 두어 트리에 자리가 없다. 그것을 걷는 것은 패키징뿐이므로,
# **배포한다면 패키지 쪽을 써야 한다.** 그 갈래로 떨어지면 configure가 경고한다.
#
# 목록은 luil를 링크한 실행 파일에 실제로 들어가는 것만 담는다.
# Catch2는 test 실행 파일에만 링크되므로 제외한다.
function(luil_generate_third_party_notices output_file)
    # 설치본도 같은 생성기를 쓴다. 소비자가 선택한 Skia·WebView2의 고지를
    # 읽고, 라이브러리에 함께 설치된 폰트·JSON 고지는 패키지에서 읽는다.
    set(installed_resources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/luil")
    if(EXISTS "${installed_resources}/luil_resources.rc.in")
        set(LUIL_ASSET_DIRECTORY "${installed_resources}")
        set(LUIL_NLOHMANN_JSON_ROOT "${installed_resources}")
    endif()
    if(NOT DEFINED LUIL_SKIA_ROOT)
        message(FATAL_ERROR "LUIL_SKIA_ROOT is required to generate notices.")
    endif()


    # luil 자신이 싣는 것이다. 어느 갈래에서나 같다.
    set(luil_notice_entries
        # HTTP 응답의 JSON 본문 타입이다. header-only라 실행 파일에 그대로 실린다.
        "nlohmann/json|${LUIL_NLOHMANN_JSON_ROOT}/LICENSE.MIT"
        # 웹뷰 로더다. 정적으로 링크하므로 그 코드가 실행 파일에 들어가고,
        # 라이선스(BSD 3-Clause 형태)가 바이너리 재배포에 고지 재현을 요구한다.
        #  - 패키지의 NOTICE.txt는 넣지 않는다. 그것은 WinRT 투영 **도구**가 쓰는
        #    Antlr·StringTemplate의 고지라 실행 파일에 들어가지 않는다.
        #  - Evergreen Runtime은 여기 없다. 우리가 배포하지 않고 소비자가
        #    Microsoft에서 직접 받는다 (docs/concepts/consumer-contract.md).
        "Visual Studio Code Icons (Codicons)|${LUIL_ASSET_DIRECTORY}/LICENSE"
        "Visual Studio Code Icons (Codicons) - Code|${LUIL_ASSET_DIRECTORY}/LICENSE-CODE")

    if(EXISTS "${LUIL_SKIA_ROOT}/NOTICE.md")
        set(notice_entries
            "Skia and its bundled components|${LUIL_SKIA_ROOT}/NOTICE.md")
        list(APPEND notice_entries ${luil_notice_entries})
    else()
        set(notice_entries
            "Skia|${LUIL_SKIA_ROOT}/LICENSE"
            "SPIRV-Cross|${LUIL_SKIA_ROOT}/third_party/externals/spirv-cross/LICENSE"
            "SPIRV-Headers|${LUIL_SKIA_ROOT}/third_party/externals/spirv-headers/LICENSE"
            "D3D12 Memory Allocator|${LUIL_SKIA_ROOT}/third_party/externals/d3d12allocator/LICENSE.txt"
            "D3D12 Memory Allocator - Notices|${LUIL_SKIA_ROOT}/third_party/externals/d3d12allocator/NOTICES.txt"
            # 이미지 코덱이다. Skia에 정적으로 들어가므로 실행 파일에 함께 실린다.
            #  - libjpeg-turbo의 LICENSE.md는 IJG 라이선스를 **참조만** 한다.
            #    원문은 README.ijg에 있고 IJG 라이선스가 그 동봉을 요구한다.
            "libjpeg-turbo|${LUIL_SKIA_ROOT}/third_party/externals/libjpeg-turbo/LICENSE.md"
            "libjpeg-turbo - IJG License|${LUIL_SKIA_ROOT}/third_party/externals/libjpeg-turbo/README.ijg"
            "libwebp|${LUIL_SKIA_ROOT}/third_party/externals/libwebp/COPYING"
            "Wuffs|${LUIL_SKIA_ROOT}/third_party/externals/wuffs/LICENSE")
        list(APPEND notice_entries ${luil_notice_entries})

        # 이 갈래는 rust crate 고지를 채우지 못한다 — bazel이 자기 캐시에 받아 두어
        # 트리에 자리가 없다. png를 rust 코덱으로 정했으므로 늘 모자란다.
        message(WARNING
            "The generated notices are incomplete: this is a Skia source tree, "
            "and the rust png crate notices are not in it - Bazel keeps them in "
            "its own cache.\n"
            "Package the Skia build to fill them in - that is what skia-prep's "
            "pack_skia.ps1 does, and the package it makes carries NOTICE.md.")
    endif()

    set(notice_text
        "luil 제3자 소프트웨어 고지\n"
        "================================\n\n"
        "이 파일은 실행 파일에 포함되는 제3자 구성 요소의 라이선스 원문을 모아\n"
        "생성합니다. 원문은 Skia 소스 트리와 자산 디렉터리에서 직접 읽습니다.\n")

    foreach(entry IN LISTS notice_entries)
        string(FIND "${entry}" "|" separator_index)
        if(separator_index EQUAL -1)
            message(FATAL_ERROR "Malformed notice entry: ${entry}")
        endif()
        string(SUBSTRING "${entry}" 0 ${separator_index} component_name)
        math(EXPR path_index "${separator_index} + 1")
        string(SUBSTRING "${entry}" ${path_index} -1 license_file)

        if(NOT EXISTS "${license_file}")
            message(FATAL_ERROR
                "A third-party license file is missing: ${license_file}\n"
                "Component: ${component_name}\n"
                "This Skia tree has no third_party/externals, so its licences "
                "cannot be read. Use a package instead: scripts/fetch_skia.ps1")
        endif()

        file(READ "${license_file}" license_text)
        string(APPEND notice_text
            "\n\n----------------------------------------\n"
            "Component: ${component_name}\n"
            "----------------------------------------\n"
            "${license_text}")
    endforeach()

    get_filename_component(output_directory "${output_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    file(WRITE "${output_file}" "${notice_text}")
endfunction()

# 라이브러리가 요구하는 resource(코디콘 폰트·라이선스·고지)를 담은 .rc 조각을 만든다.
# 소비자는 이 조각을 실행 파일 소스에 넣고, 아이콘·manifest·버전 정보 등 자기 resource는 별도 .rc로 더한다.
function(luil_configure_resource_script output_file notices_file)
    set(resource_template "${LUIL_SOURCE_DIRECTORY}/win32/resources/luil_resources.rc.in")
    file(TO_CMAKE_PATH "${LUIL_SOURCE_DIRECTORY}/win32/resource_ids.h" LUIL_RESOURCE_IDS_PATH)
    get_filename_component(installed_resources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/luil" ABSOLUTE)
    if(EXISTS "${installed_resources}/luil_resources.rc.in")
        set(LUIL_ASSET_DIRECTORY "${installed_resources}")
        set(LUIL_RESOURCE_IDS_PATH "${installed_resources}/resource_ids.h")
        set(resource_template "${installed_resources}/luil_resources.rc.in")
    endif()
    file(TO_CMAKE_PATH "${LUIL_ASSET_DIRECTORY}/codicon.ttf" LUIL_CODICONS_FONT_PATH)
    file(TO_CMAKE_PATH "${LUIL_ASSET_DIRECTORY}/LICENSE" LUIL_CODICONS_LICENSE_PATH)
    file(TO_CMAKE_PATH "${LUIL_ASSET_DIRECTORY}/LICENSE-CODE" LUIL_CODICONS_LICENSE_CODE_PATH)
    file(TO_CMAKE_PATH "${notices_file}" LUIL_THIRD_PARTY_NOTICES_PATH)
    configure_file(
        "${resource_template}"
        "${output_file}"
        @ONLY)
endfunction()

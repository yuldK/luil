include_guard(GLOBAL)

# 실행 파일 resource에 넣을 luil 라이선스와 제3자 고지를 만든다.
# 원문은 luil과 각 의존성의 소스 트리나 패키지에서 직접 읽는다.
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

# 패키지의 `NOTICE.md`에서 켠 기능의 고지만 골라 `output_variable`에 담는다.
#
# 패키지의 `NOTICE.json`이 기능마다 실행 파일에 들어오는 구성요소의 id를
# (`features.<기능>.notices`), id마다 `NOTICE.md`의 절 이름을 적는다 (`notices.<id>`,
# 문자열 하나거나 목록). 실을 것은 core와 켠 기능(`LUIL_SKIA_FEATURES`)의 합이다
# (skia-prep docs/skia-build.md 6.8). 끈 기능의 고지(HarfBuzz·libavif 등)는 그 코드가
# 실행 파일에 없으므로 싣지 않는다.
#
# 머리말(절 앞의 글)은 그대로 둔다. 비공식 빌드라는 것과 rust 표준 라이브러리 고지의
# 자리를 말한다. 절 이름이 `NOTICE.md`에 없으면 멈춘다 — 고지 없이 내보내지 않는다.
function(luil_select_skia_notices notice_markdown notice_json output_variable)
    file(READ "${notice_json}" notice_index)
    set(wanted_ids "")
    foreach(feature IN ITEMS core ${LUIL_SKIA_FEATURES})
        string(JSON feature_notices ERROR_VARIABLE json_error
            GET "${notice_index}" features "${feature}" notices)
        if(json_error)
            message(FATAL_ERROR
                "The Skia package's NOTICE.json does not describe the ${feature} feature: "
                "${notice_json}
${json_error}")
        endif()
        string(JSON id_count LENGTH "${feature_notices}")
        if(id_count GREATER 0)
            math(EXPR last_id "${id_count} - 1")
            foreach(id_index RANGE ${last_id})
                string(JSON notice_id GET "${feature_notices}" ${id_index})
                list(APPEND wanted_ids "${notice_id}")
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES wanted_ids)

    # 절 이름에 `;`가 들 수 있어(`ICU (headers only; ...)`) 목록에 넣기 전에 바꿔 둔다.
    # CMake 목록은 `;`로 갈린다.
    set(wanted_titles "")
    foreach(notice_id IN LISTS wanted_ids)
        string(JSON title_type ERROR_VARIABLE json_error
            TYPE "${notice_index}" notices "${notice_id}")
        if(json_error)
            message(FATAL_ERROR
                "The Skia package's NOTICE.json has no notice named ${notice_id}: "
                "${notice_json}")
        endif()
        if(title_type STREQUAL "ARRAY")
            string(JSON title_count LENGTH "${notice_index}" notices "${notice_id}")
            math(EXPR last_title "${title_count} - 1")
            foreach(title_index RANGE ${last_title})
                string(JSON title GET "${notice_index}" notices "${notice_id}" ${title_index})
                string(REPLACE ";" "<semicolon>" title "${title}")
                list(APPEND wanted_titles "${title}")
            endforeach()
        else()
            string(JSON title GET "${notice_index}" notices "${notice_id}")
            string(REPLACE ";" "<semicolon>" title "${title}")
            list(APPEND wanted_titles "${title}")
        endif()
    endforeach()

    # 절은 `-----\nComponent: <이름>\n-----\n<원문>`이다. 표지 앞까지가 앞 절이다.
    file(READ "${notice_markdown}" markdown)
    set(rule "----------------------------------------")
    set(marker "${rule}\nComponent: ")
    string(LENGTH "${marker}" marker_length)
    string(FIND "${markdown}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The Skia package's NOTICE.md has no component sections: ${notice_markdown}")
    endif()
    string(SUBSTRING "${markdown}" 0 ${position} selected)
    string(SUBSTRING "${markdown}" ${position} -1 rest)
    set(found_titles "")
    while(NOT rest STREQUAL "")
        string(SUBSTRING "${rest}" ${marker_length} -1 body)
        string(FIND "${body}" "\n" title_end)
        string(SUBSTRING "${body}" 0 ${title_end} title)
        string(REPLACE ";" "<semicolon>" title "${title}")
        string(FIND "${body}" "${marker}" next)
        if(next EQUAL -1)
            set(section "${rest}")
            set(rest "")
        else()
            math(EXPR section_length "${next} + ${marker_length}")
            string(SUBSTRING "${rest}" 0 ${section_length} section)
            string(SUBSTRING "${rest}" ${section_length} -1 rest)
        endif()
        if(title IN_LIST wanted_titles)
            string(APPEND selected "${section}")
            list(APPEND found_titles "${title}")
        endif()
    endwhile()

    # 절이 아니라 패키지의 파일을 가리키는 이름도 있다 — rust 표준 라이브러리의
    # 고지(`... (licenses/rust/COPYRIGHT-library.html)`)는 NOTICE.md 밖의 HTML이고,
    # NOTICE.md의 머리말이 그 자리를 말한다. 그 파일이 패키지에 있으면 된 것으로 본다.
    get_filename_component(package_root "${notice_markdown}" DIRECTORY)
    foreach(title IN LISTS wanted_titles)
        # `${CMAKE_MATCH_1}`은 if의 인자를 펼 때 읽히므로 MATCHES와 한 식에 둘 수 없다.
        if(NOT title IN_LIST found_titles AND title MATCHES "\\(([^()]+)\\)$")
            if(EXISTS "${package_root}/${CMAKE_MATCH_1}")
                list(APPEND found_titles "${title}")
            endif()
        endif()
        if(NOT title IN_LIST found_titles)
            string(REPLACE "<semicolon>" ";" title "${title}")
            message(FATAL_ERROR
                "The Skia package's NOTICE.md has no section for \"${title}\": "
                "${notice_markdown}\n"
                "NOTICE.json names it, so the package is incomplete. Fetch it again: "
                "scripts/fetch_skia.ps1 -Force")
        endif()
    endforeach()
    set(${output_variable} "${selected}" PARENT_SCOPE)
endfunction()

function(luil_generate_third_party_notices output_file)
    # 설치본도 같은 생성기를 쓴다. 소비자가 선택한 Skia·WebView2의 고지를
    # 읽고, 라이브러리에 함께 설치된 폰트·JSON 고지는 패키지에서 읽는다.
    set(luil_license_file "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../LICENSE")
    set(installed_resources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/luil")
    if(EXISTS "${installed_resources}/luil_resources.rc.in")
        set(LUIL_ASSET_DIRECTORY "${installed_resources}")
        set(LUIL_NLOHMANN_JSON_ROOT "${installed_resources}")
        set(luil_license_file "${installed_resources}/LICENSE.luil")
    endif()
    if(NOT DEFINED LUIL_SKIA_ROOT)
        message(FATAL_ERROR "LUIL_SKIA_ROOT is required to generate notices.")
    endif()

    # 웹뷰를 끈 구성(LUIL_ENABLE_WEBVIEW=OFF)에는 로더가 링크되지 않으므로 SDK
    # 고지도 싣지 않는다. 변수가 없으면(옵션이 생기기 전 호출자) 켠 것으로 본다.
    if(NOT DEFINED LUIL_ENABLE_WEBVIEW)
        set(LUIL_ENABLE_WEBVIEW ON)
    endif()
    if(LUIL_ENABLE_WEBVIEW AND NOT DEFINED LUIL_WEBVIEW2_ROOT)
        message(FATAL_ERROR "LUIL_WEBVIEW2_ROOT is required to generate notices.")
    endif()

    # luil 자신이 싣는 것이다. 어느 갈래에서나 같다.
    set(luil_notice_entries
        "luil|${luil_license_file}"
        # HTTP 응답의 JSON 본문 타입이다. header-only라 실행 파일에 그대로 실린다.
        "nlohmann/json|${LUIL_NLOHMANN_JSON_ROOT}/LICENSE.MIT")
    if(LUIL_ENABLE_WEBVIEW)
        list(APPEND luil_notice_entries
            # 웹뷰 로더다. 정적으로 링크하므로 그 코드가 실행 파일에 들어가고,
            # 라이선스(BSD 3-Clause 형태)가 바이너리 재배포에 고지 재현을 요구한다.
            #  - 패키지의 NOTICE.txt는 넣지 않는다. 그것은 WinRT 투영 **도구**가 쓰는
            #    Antlr·StringTemplate의 고지라 실행 파일에 들어가지 않는다.
            #  - Evergreen Runtime은 여기 없다. 우리가 배포하지 않고 소비자가
            #    Microsoft에서 직접 받는다 (docs/concepts/consumer-contract.md).
            "Microsoft Edge WebView2 SDK|${LUIL_WEBVIEW2_ROOT}/LICENSE.txt")
    endif()
    list(APPEND luil_notice_entries
        "Visual Studio Code Icons (Codicons)|${LUIL_ASSET_DIRECTORY}/LICENSE"
        "Visual Studio Code Icons (Codicons) - Code|${LUIL_ASSET_DIRECTORY}/LICENSE-CODE")

    # 켠 기능이 있는 패키지는 그 기능의 고지만 골라 싣는다. NOTICE.json이 없는 앞
    # 판번이나 기능 목록을 모르는 호출자(옵션이 생기기 전)는 NOTICE.md 전체다.
    set(skia_notice_file "${LUIL_SKIA_ROOT}/NOTICE.md")
    if(EXISTS "${LUIL_SKIA_ROOT}/NOTICE.md" AND EXISTS "${LUIL_SKIA_ROOT}/NOTICE.json"
        AND DEFINED LUIL_SKIA_FEATURES)
        luil_select_skia_notices("${LUIL_SKIA_ROOT}/NOTICE.md" "${LUIL_SKIA_ROOT}/NOTICE.json"
            selected_skia_notices)
        get_filename_component(notice_directory "${output_file}" DIRECTORY)
        set(skia_notice_file "${notice_directory}/skia_notices.md")
        file(MAKE_DIRECTORY "${notice_directory}")
        file(WRITE "${skia_notice_file}" "${selected_skia_notices}")
    endif()
    if(EXISTS "${LUIL_SKIA_ROOT}/NOTICE.md")
        set(notice_entries
            "Skia and its bundled components|${skia_notice_file}")
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

    # `set`에 인자를 여럿 주면 list가 되어 줄 사이에 `;`가 박혀 파일에 그대로 나간다.
    # 한 문자열로 이어 붙인다.
    string(CONCAT notice_text
        "luil 라이선스 및 제3자 소프트웨어 고지\n"
        "================================\n\n"
        "이 파일은 luil과 실행 파일에 포함되는 제3자 구성 요소의 라이선스 원문을\n"
        "모아 생성합니다. 원문은 각 구성 요소의 소스 트리나 패키지에서 읽습니다.\n")

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
                "A license file is missing: ${license_file}\n"
                "Component: ${component_name}\n"
                "Restore this component's license file before generating notices.")
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

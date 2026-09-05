include(Catch)


# test 실행 파일도 라이브러리 resource(코디콘 폰트·라이선스·고지)를
# 내장해야 embedded assets 경로가 실제와 같은 방식으로 검증된다.

add_executable(luil_tests
    accessibility_tests.cpp
    badge_element_tests.cpp
    check_element_tests.cpp
    group_element_tests.cpp
    messaging_channel_tests.cpp
    messaging_latest_slot_tests.cpp
    progress_element_tests.cpp
    raster_probe.cpp
    scroll_view_element_tests.cpp
    scrollbar_element_tests.cpp
    sidebar_element_tests.cpp
    slider_element_tests.cpp
    split_handle_element_tests.cpp
    stack_element_tests.cpp
    strip_element_tests.cpp
    text_button_element_tests.cpp
    text_edit_tests.cpp
    transition_tests.cpp
    ui_element_tests.cpp
    ui_interaction_tests.cpp
    ui_theme_tests.cpp
    wrap_element_tests.cpp
    utf8_tests.cpp)
target_include_directories(luil_tests
    PRIVATE
        "${LUIL_SOURCE_DIRECTORY}"
        "${LUIL_TEST_DIRECTORY}")
target_link_libraries(luil_tests
    PRIVATE
        luil::luil
        luil_options
        # loopback_http_server가 raw Winsock으로 서므로 test 실행 파일만 이것을 건다.
        ws2_32
        Catch2::Catch2WithMain)
# test는 win32 내부의 순수 계산(caption_layout·popup_reconcile 등)도 검증하므로
# 비공개 header가 있는 src/를 직접 본다. 소비자에게는 열리지 않는 경로다.
target_include_directories(luil_tests PRIVATE "${LUIL_SOURCE_DIRECTORY}")
luil_disable_automatic_windows_manifest(luil_tests)

# messaging의 ASan 검증이다.
# messaging은 header-only라 이 실행 파일만
# /fsanitize=address로 컴파일해도 component 전체가 검사된다.
# Debug 구성의 /RTC1과 ASan이 충돌하므로 Debug가 아닌 구성에서만 ASan을 켠다.
# 검증은 Release로 실행한다.
add_executable(luil_messaging_asan_tests
    asan_guard_tests.cpp
    messaging_channel_tests.cpp
    messaging_latest_slot_tests.cpp)
# messaging은 header-only라 라이브러리를 링크하지 않고 include 경로만 있으면 된다.
target_include_directories(luil_messaging_asan_tests PRIVATE "${LUIL_INCLUDE_DIRECTORY}")
target_link_libraries(luil_messaging_asan_tests
    PRIVATE
        luil_options
        Catch2::Catch2WithMain)
# ASAN 보고에 기호가 필요해 이 target만 Release에서도 debug 정보를 켠다 (C5072).
# Catch2는 ASan 없이 빌드되어 STL 주석이 꺼져 있으므로(LNK2038)
# 이 target도 주석을 끄고 heap 검사만 사용한다.
# `optional`은 나중에 MSVC STL이 더한 세 번째 주석이라 최신 toolset에서 새로 어긋난다.
target_compile_options(luil_messaging_asan_tests PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/fsanitize=address;/Zi>")
target_compile_definitions(luil_messaging_asan_tests PRIVATE "$<$<NOT:$<CONFIG:Debug>>:_DISABLE_STRING_ANNOTATION;_DISABLE_VECTOR_ANNOTATION;_DISABLE_OPTIONAL_ANNOTATION>")
target_link_options(luil_messaging_asan_tests PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO;/DEBUG>")
luil_disable_automatic_windows_manifest(luil_messaging_asan_tests)
# 최신 MSVC의 ASan runtime은 항상 동적 DLL이라 실행 파일 옆에 있어야 한다.
# 경로는 configure 시점의 컴파일러(cl.exe) 디렉터리에서 해석해 따옴표 문제를 피한다.
get_filename_component(LUIL_MSVC_BIN_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
if (EXISTS "${LUIL_MSVC_BIN_DIRECTORY}/clang_rt.asan_dynamic-x86_64.dll")
    add_custom_command(TARGET luil_messaging_asan_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${LUIL_MSVC_BIN_DIRECTORY}/clang_rt.asan_dynamic-x86_64.dll"
            "$<TARGET_FILE_DIR:luil_messaging_asan_tests>"
        VERBATIM)
else ()
    message(WARNING "clang_rt.asan_dynamic-x86_64.dll을 찾지 못했습니다. asan test 실행에 PATH 구성이 필요할 수 있습니다.")
endif ()
# Debug는 ASan이 꺼진 채 빌드되므로 같은 이름의 test가 sanitizer 없이 도는
# 허위 신호가 된다 — Debug 구성에서는 DISABLED로 등록해 아예 돌리지 않는다.
# (POST_BUILD 발견이 구성별로 실행되어 $<CONFIG:...>가 빌드 시점에 풀린다.)
catch_discover_tests(luil_messaging_asan_tests TEST_PREFIX "asan: " PROPERTIES TIMEOUT 120 LABELS "asan" DISABLED "$<CONFIG:Debug>")
# hang 유형의 회귀가 무기한 대기 대신 실패로 드러나도록 모든 Catch2 test에 timeout을 부여한다.
catch_discover_tests(luil_tests PROPERTIES TIMEOUT 120)

add_test(NAME luil_assets_checksum
    COMMAND "${CMAKE_COMMAND}"
        "-DASSET_DIRECTORY=${LUIL_ASSET_DIRECTORY}"
        -P "${LUIL_CMAKE_DIRECTORY}/verify_codicons.cmake")

# umbrella header가 공개 헤더를 하나도 빠뜨리지 않았는지 본다.
# 컴파일러가 잡지 못하는 종류라 관문이 따로 필요하다 (script에 이유가 있다).
add_test(NAME luil_umbrella_header
    COMMAND "${CMAKE_COMMAND}"
        "-DINCLUDE_DIRECTORY=${LUIL_INCLUDE_DIRECTORY}"
        -P "${LUIL_CMAKE_DIRECTORY}/verify_umbrella_header.cmake")

find_program(LUIL_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(NAME luil_source_style
    COMMAND "${CMAKE_COMMAND}"
        "-DPOWERSHELL_EXECUTABLE=${LUIL_POWERSHELL_EXECUTABLE}"
        "-DROOT_DIRECTORY=${PROJECT_SOURCE_DIR}"
        -P "${LUIL_CMAKE_DIRECTORY}/run_style_check.cmake")


luil_set_ide_folder("Tests"
    luil_messaging_asan_tests
    luil_tests)

include(Catch)

# 플랫폼을 모르는 층(luil_core)의 test다.
# core만 링크하므로 Win32 없이도 세울 수 있어야 하고, 다른 플랫폼에서도 이 실행
# 파일을 그대로 돌린다 (docs/android-port-plan.md). Win32 계층의 비공개 header나
# 실행 파일 resource가 필요한 test는 아래 luil_tests에 둔다.
add_executable(luil_core_tests
    accessibility_tests.cpp
    app_bar_element_tests.cpp
    app_host_tests.cpp
    badge_element_tests.cpp
    button_element_tests.cpp
    check_element_tests.cpp
    choice_group_element_tests.cpp
    dialog_elements_tests.cpp
    dropdown_element_tests.cpp
    fence_wait_tests.cpp
    group_element_tests.cpp
    grouped_list_element_tests.cpp
    image_animation_tests.cpp
    image_element_tests.cpp
    list_element_tests.cpp
    menu_element_tests.cpp
    messaging_channel_tests.cpp
    messaging_latest_slot_tests.cpp
    modal_host_element_tests.cpp
    panel_element_tests.cpp
    pointer_sequence_tests.cpp
    popup_reconcile_tests.cpp
    progress_element_tests.cpp
    raster_draw_tests.cpp
    renderer_host_tests.cpp
    renderer_policy_tests.cpp
    scroll_area_element_tests.cpp
    scroll_view_element_tests.cpp
    scrollbar_element_tests.cpp
    sidebar_element_tests.cpp
    slider_element_tests.cpp
    split_handle_element_tests.cpp
    stack_element_tests.cpp
    strip_element_tests.cpp
    style_raster_tests.cpp
    surface_invalidate_tests.cpp
    tab_bar_element_tests.cpp
    text_button_element_tests.cpp
    text_edit_tests.cpp
    toast_element_tests.cpp
    touch_input_tests.cpp
    transition_tests.cpp
    ui_element_tests.cpp
    ui_interaction_tests.cpp
    ui_style_tests.cpp
    ui_theme_tests.cpp
    virtual_list_element_tests.cpp
    webview_element_tests.cpp
    wrap_element_tests.cpp
    zoom_view_tests.cpp
    # core가 링크로 묶어 부르는 플랫폼 hook의 test용 정의다 (파일에 이유가 있다).
    core_platform_stub.cpp
    raster_probe.cpp)
target_include_directories(luil_core_tests
    PRIVATE
        "${LUIL_SOURCE_DIRECTORY}"
        "${LUIL_TEST_DIRECTORY}")
target_link_libraries(luil_core_tests
    PRIVATE
        luil::luil_core
        luil_options
        Catch2::Catch2WithMain)

if(LUIL_PLATFORM STREQUAL "android")
    # 기기에서 돌린다. CTest와 Catch2의 발견이 실행 파일을 부를 때마다 adb로 올리고
    # 기기의 셸에서 실행한다 (cmake/android/adb_run.cmake).
    #  - 발견을 test 시점으로 미룬다. 빌드할 때 기기가 꽂혀 있지 않아도 된다.
    #  - 기기는 adb가 고른다. 여럿이면 ANDROID_SERIAL로 정한다.
    # Android SDK(Android Studio가 까는 자리)의 adb를 PATH보다 먼저 본다. 기기와 붙는
    # adb 서버는 하나라, 버전이 다른 adb가 둘이면 서로의 서버를 끊고 다시 띄운다.
    find_program(LUIL_ADB_EXECUTABLE
        NAMES adb
        HINTS
            "$ENV{ANDROID_HOME}/platform-tools"
            "$ENV{ANDROID_SDK_ROOT}/platform-tools"
            "$ENV{LOCALAPPDATA}/Android/Sdk/platform-tools"
        REQUIRED)
    set_target_properties(luil_core_tests PROPERTIES
        CROSSCOMPILING_EMULATOR
            "${CMAKE_COMMAND};-DADB=${LUIL_ADB_EXECUTABLE};-DDEVICE_DIRECTORY=/data/local/tmp/luil;-P;${LUIL_CMAKE_DIRECTORY}/android/adb_run.cmake")
    # Android 입력 변환은 GameActivity를 모르는 순수 층이라 core test에 함께 넣어 기기에서 돌린다.
    target_sources(luil_core_tests
        PRIVATE
            "${LUIL_SOURCE_DIRECTORY}/android/android_input.cpp"
            android_input_tests.cpp)
    catch_discover_tests(luil_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 120)
    luil_set_ide_folder("Tests" luil_core_tests)
    # 나머지 test는 Win32 계층이나 Windows 도구(rc, pwsh, MSVC ASan)를 쓴다.
    return()
endif()
luil_disable_automatic_windows_manifest(luil_core_tests)

add_test(NAME luil_accent_catalog
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DBINARY_DIR=${PROJECT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/accent_catalog_tests.cmake")
add_test(NAME luil_style
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DBINARY_DIR=${PROJECT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/style_tests.cmake")

if(LUIL_INSTALL)
    add_test(NAME luil_installed_consumer
        COMMAND "${CMAKE_COMMAND}" "-DBINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DCONFIG=$<CONFIG>"
            "-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install_consumer.cmake")
    set_tests_properties(luil_installed_consumer PROPERTIES TIMEOUT 120 LABELS "package")
endif()

# test 실행 파일도 라이브러리 resource(코디콘 폰트·라이선스·고지)를
# 내장해야 embedded assets 경로가 실제와 같은 방식으로 검증된다.
set(luil_tests_notices "${LUIL_GENERATED_DIRECTORY}/third_party_notices.txt")
set(luil_tests_resource "${LUIL_GENERATED_DIRECTORY}/luil_tests.rc")
luil_generate_third_party_notices("${luil_tests_notices}")
luil_configure_resource_script("${luil_tests_resource}" "${luil_tests_notices}")

add_executable(luil_tests
    "${luil_tests_resource}"
    caption_layout_tests.cpp
    dpi_scale_tests.cpp
    embedded_assets_tests.cpp
    font_fallback_tests.cpp
    glyph_element_tests.cpp
    http_body_tests.cpp
    http_client_tests.cpp
    http_media_type_tests.cpp
    image_decode_tests.cpp
    loopback_http_server.cpp
    loopback_http_server_tests.cpp
    popup_dismiss_tests.cpp
    raster_probe.cpp
    secondary_surface_tests.cpp
    surface_input_tests.cpp
    tsf_input_tests.cpp
    uia_provider_tests.cpp
    utf8_tests.cpp
    webview_layout_tests.cpp
    webview_message_gate_tests.cpp
    win32_clipboard_tests.cpp
    win32_drop_tests.cpp
    win32_fonts_tests.cpp
    win32_window_tests.cpp
    window_mode_tests.cpp
    window_position_tests.cpp)
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
# 고지 검사가 웹뷰 구성을 따라간다 — 끈 구성의 실행 파일에는 로더가 없다.
target_compile_definitions(luil_tests PRIVATE "LUIL_TESTS_WEBVIEW=$<BOOL:${LUIL_ENABLE_WEBVIEW}>")
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
catch_discover_tests(luil_core_tests PROPERTIES TIMEOUT 120)
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

# core가 플랫폼 헤더를 들이지 않았는지 본다.
# Windows에서는 들여도 빌드되므로 컴파일러가 잡지 못한다 (script에 이유가 있다).
add_test(NAME luil_core_portability
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIRECTORY=${LUIL_SOURCE_DIRECTORY}"
        "-DINCLUDE_DIRECTORY=${LUIL_INCLUDE_DIRECTORY}"
        -P "${LUIL_CMAKE_DIRECTORY}/verify_core_portability.cmake")

find_program(LUIL_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(NAME luil_source_style
    COMMAND "${CMAKE_COMMAND}"
        "-DPOWERSHELL_EXECUTABLE=${LUIL_POWERSHELL_EXECUTABLE}"
        "-DROOT_DIRECTORY=${PROJECT_SOURCE_DIR}"
        -P "${LUIL_CMAKE_DIRECTORY}/run_style_check.cmake")

add_test(NAME luil_smoke_cpu
    COMMAND "$<TARGET_FILE:luil_demo>" --renderer=cpu --smoke-test)
add_test(NAME luil_smoke_auto
    COMMAND "$<TARGET_FILE:luil_demo>" --renderer=auto --smoke-test)
# Direct3D 렌더러를 뺀 구성에서는 건너뜀(77)으로 끝난다 — 명시한 direct3d가
# 창을 세우지 못하고 그 사실을 종료 코드로 알리는 것까지가 이 구성의 계약이다.
add_test(NAME luil_smoke_direct3d
    COMMAND "$<TARGET_FILE:luil_demo>" --renderer=direct3d --smoke-test)
set_tests_properties(luil_smoke_direct3d PROPERTIES SKIP_RETURN_CODE 77)
set(luil_smoke_tests
    luil_smoke_cpu
    luil_smoke_auto
    luil_smoke_direct3d)
# 두 주입은 Direct3D가 서 있어야 뜻이 있다. 렌더러를 뺀 구성에서는 처음부터
# CPU라 아무것도 주입되지 않은 채 통과하므로 등록하지 않는다.
if(LUIL_ENABLE_DIRECT3D)
    add_test(NAME luil_smoke_auto_fallback
        COMMAND "$<TARGET_FILE:luil_demo>" --renderer=auto --smoke-test
            --simulate-direct3d-failure)
    # 성공적으로 그린 뒤 잃는 경로다. 생성 시점 실패(위)와 달리 창이 이미 제시에
    # 붙어 있으므로, 물러서면서 그것을 놓지 않으면 CPU 픽셀이 화면에 닿지 못한다
    # (docs/webview-composition-design.md).
    add_test(NAME luil_smoke_auto_runtime_loss
        COMMAND "$<TARGET_FILE:luil_demo>" --renderer=auto --smoke-test
            --simulate-direct3d-loss-after-frames=1)
    list(APPEND luil_smoke_tests
        luil_smoke_auto_fallback
        luil_smoke_auto_runtime_loss)
endif()
set_tests_properties(${luil_smoke_tests} PROPERTIES TIMEOUT 30 LABELS "smoke")

luil_set_ide_folder("Tests"
    luil_core_tests
    luil_messaging_asan_tests
    luil_tests)

# 실제 입력 데스크톱을 쓰므로 일반 ctest에 등록하지 않는다.
# --interactive를 준 전용 대화형 검증 환경에서만 입력을 주입한다.
if(LUIL_BUILD_POINTER_INTEGRATION_TESTS)
    add_executable(luil_pointer_integration
        "${luil_tests_resource}"
        pointer_integration_fixture.cpp
        pointer_integration_runner.cpp
        loopback_http_server.cpp)
    target_link_libraries(luil_pointer_integration PRIVATE luil::luil luil_options uiautomationcore comctl32 ws2_32)
    luil_disable_automatic_windows_manifest(luil_pointer_integration)
    target_compile_definitions(luil_pointer_integration PRIVATE LUIL_POINTER_TEST_WEBVIEW=$<BOOL:${LUIL_ENABLE_WEBVIEW}>)
    luil_set_ide_folder("Tests" luil_pointer_integration)
endif()

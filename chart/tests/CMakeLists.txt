include(Catch)

# 차트의 test다. 같은 실행 파일이 Windows와 Android 실기기에서 돈다 (docs/chart-design.md의 확인 절).
#  - core 밖이라 core의 플랫폼 hook 대역과 raster 도우미를 같은 파일 그대로 컴파일한다. 그 대역이 core 비공개 헤더를 들이므로
#    이 test target만 `${LUIL_SOURCE_DIRECTORY}`를 본다. 라이브러리는 그대로 공개 헤더만 쓴다.
#  - 비공개 문구를 재므로 차트의 `src/`도 본다.
add_executable(luil_chart_tests
    "${LUIL_TEST_DIRECTORY}/core_platform_stub.cpp"
    "${LUIL_TEST_DIRECTORY}/raster_probe.cpp"
    chart_element_tests.cpp
    chart_scale_tests.cpp
    chart_text_tests.cpp)
target_include_directories(luil_chart_tests
    PRIVATE
        "${LUIL_SOURCE_DIRECTORY}"
        "${LUIL_TEST_DIRECTORY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(luil_chart_tests
    PRIVATE
        luil::luil_chart
        luil_options
        Catch2::Catch2WithMain)
luil_set_ide_folder("Tests" luil_chart_tests)

if(LUIL_PLATFORM STREQUAL "android")
    # 기기에서 돌린다 (tests/CMakeLists.txt의 Android 절과 같은 길, cmake/android/adb_run.cmake).
    #  - adb 실행기는 tests/가 찾아 둔 캐시 값이다. 루트가 tests/를 먼저 읽는다.
    #  - 건너뜀 글은 tests/의 디렉터리 변수라 여기서 보이지 않아 다시 정한다. adb_run.cmake가 남기는 글과 같다.
    set(luil_chart_device_skip_pattern "skipped its test on the device")
    set_target_properties(luil_chart_tests PROPERTIES
        CROSSCOMPILING_EMULATOR
            "${CMAKE_COMMAND};-DADB=${LUIL_ADB_EXECUTABLE};-DDEVICE_DIRECTORY=/data/local/tmp/luil;-P;${LUIL_CMAKE_DIRECTORY}/android/adb_run.cmake")
    catch_discover_tests(luil_chart_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 120 SKIP_REGULAR_EXPRESSION "${luil_chart_device_skip_pattern}")
else()
    luil_disable_automatic_windows_manifest(luil_chart_tests)
    catch_discover_tests(luil_chart_tests PROPERTIES TIMEOUT 120)
endif()

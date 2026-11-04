# 선택 라이브러리 차트다 (docs/chart-library-design.md). 루트가 LUIL_BUILD_CHART를 켤 때만 이 디렉터리를 읽는다.
#
# core의 공개 헤더만 쓰고 `luil_core`만 잇는다. 플랫폼을 모르는 요소라 Win32·Android 조립(`luil`)이 필요 없고,
# 그래서 Android의 core 프리셋에서도 test가 돈다. core의 비공개 `src/`를 보지 않으므로 설치본만으로 지어지는 꼴이다.
set(luil_chart_include_directory "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(luil_chart_public_headers "${luil_chart_include_directory}/luil")
set(luil_chart_files)

luil_add_source_filter(luil_chart_files "chart"
    "${luil_chart_public_headers}/chart.h"
    src/chart_element.cpp
    "${luil_chart_public_headers}/chart/chart_element.h"
    src/chart_layout.cpp
    src/chart_layout.h
    src/chart_scale.cpp
    "${luil_chart_public_headers}/chart/chart_scale.h"
    src/chart_text.cpp
    src/chart_text.h)

add_library(luil_chart STATIC ${luil_chart_files})
add_library(luil::luil_chart ALIAS luil_chart)
target_include_directories(luil_chart
    PUBLIC
        "$<BUILD_INTERFACE:${luil_chart_include_directory}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(luil_chart
    PUBLIC
        luil_core
        luil_usage
    PRIVATE
        luil_options)
luil_set_ide_folder("Libraries" luil_chart)

# 설치본에서는 구성 요소 `chart`다 (`find_package(luil COMPONENTS chart)`).
#  - 내보내기 묶음이 core와 따로라 차트를 끄고 설치한 패키지에는 이 파일이 없다. `luil_core`는 `luil-targets` 묶음에 있고
#    CMake가 `luil::luil_core`로 참조를 잇는다. 그래서 설치 config가 `luil-targets.cmake`를 먼저 읽는다.
#  - Debug 산출물의 접미사 d는 core와 같은 까닭이다 (cmake/install.cmake).
if(LUIL_INSTALL)
    set_target_properties(luil_chart PROPERTIES DEBUG_POSTFIX "d")
    install(TARGETS luil_chart
        EXPORT luil-chart-targets
        ARCHIVE DESTINATION lib)
    install(DIRECTORY "${luil_chart_include_directory}/" DESTINATION include)
    install(EXPORT luil-chart-targets
        NAMESPACE luil::
        FILE luil-chart-targets.cmake
        DESTINATION lib/cmake/luil)
endif()

if(LUIL_BUILD_TESTS)
    add_subdirectory(tests)
    # core와 같이 스크립트 검사는 Windows의 test 구성에서만 돈다 (tests/CMakeLists.txt는 Android에서 그 앞에 돌아간다).
    if(LUIL_PLATFORM STREQUAL "windows")
        # 차트 소스도 플랫폼 헤더를 들이지 않는다 (script에 이유가 있다). 필수 인자 둘은 위반 경로의 기준이기도 하다.
        add_test(NAME luil_chart_portability
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/src"
                "-DINCLUDE_DIRECTORY=${luil_chart_include_directory}"
                "-DCHECKED_DIRECTORIES=${CMAKE_CURRENT_SOURCE_DIR}/src$<SEMICOLON>${luil_chart_include_directory}"
                -P "${LUIL_CMAKE_DIRECTORY}/verify_core_portability.cmake")
        # 차트의 우산 헤더가 차트 공개 헤더를 하나도 빠뜨리지 않았는지 본다 (script에 이유가 있다).
        add_test(NAME luil_chart_umbrella
            COMMAND "${CMAKE_COMMAND}"
                "-DINCLUDE_DIRECTORY=${luil_chart_include_directory}"
                "-DUMBRELLA_HEADER=luil/chart.h"
                -P "${LUIL_CMAKE_DIRECTORY}/verify_umbrella_header.cmake")
    endif()
endif()

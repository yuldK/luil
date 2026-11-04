cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(luil_package_consumer LANGUAGES CXX RC)
# `LUIL_BUILD_CHART`는 설치 config를 읽은 뒤에야 알려지므로 찾기를 한 번만 하고 차트를 선택으로 묻는다.
# 한 번 더 찾으면 설치 config가 Skia target을 다시 지어 configure 오류가 난다 (docs/chart-library-design.md).
find_package(luil CONFIG REQUIRED OPTIONAL_COMPONENTS chart)
luil_generate_third_party_notices("${CMAKE_CURRENT_BINARY_DIR}/notices.txt")
luil_configure_resource_script("${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
    "${CMAKE_CURRENT_BINARY_DIR}/notices.txt")
add_executable(package_consumer main.cpp "${CMAKE_CURRENT_BINARY_DIR}/resources.rc")
target_link_libraries(package_consumer PRIVATE luil::luil)
target_compile_definitions(package_consumer PRIVATE UNICODE _UNICODE
    "LUIL_CONSUMER_SVG=$<BOOL:${LUIL_ENABLE_SVG}>"
    "LUIL_CONSUMER_LOTTIE=$<BOOL:${LUIL_ENABLE_LOTTIE}>")
luil_add_string_table(package_consumer NAME consumer_strings DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/strings" BASE ko)
# 차트를 켜고 설치한 패키지면 내보내기 파일이 실제로 읽혔는지 보고 잇는다. 끄고 설치한 패키지는 차트 없이 지어진다.
if(LUIL_BUILD_CHART)
    if(NOT luil_chart_FOUND)
        message(FATAL_ERROR "This package was installed with LUIL_BUILD_CHART, but its chart export was not found.")
    endif()
    target_link_libraries(package_consumer PRIVATE luil::luil_chart)
endif()
target_compile_definitions(package_consumer PRIVATE "LUIL_CONSUMER_CHART=$<BOOL:${LUIL_BUILD_CHART}>")

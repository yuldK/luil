# Android 예제의 네이티브 절반이다.
# luil의 CMake가 공유 라이브러리로 세우고, 이 디렉터리의 Gradle 프로젝트가 APK로 싼다.
# Gradle은 <빌드>/jniLibs/<예제>/<구성>/<abi>에서 그 예제의 라이브러리만 집는다
# (app/build.gradle.kts). target 이름은 luil_<예제>이고 Gradle의 flavor 이름이 그 <예제>다.
function(luil_add_android_example example)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "" "SOURCES")
    set(target luil_${example})
    add_library(${target} SHARED ${arguments_SOURCES})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(${target} PRIVATE luil::luil luil_options)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${target}>"
            "${CMAKE_BINARY_DIR}/jniLibs/${example}/$<CONFIG>/${ANDROID_ABI}/$<TARGET_FILE_NAME:${target}>"
        VERBATIM)
endfunction()

# hello — 앱 쪽 절반(hello_app)은 Windows와 같은 파일이다.
luil_add_android_example(hello
    SOURCES
        ../hello/android_main.cpp
        ../hello/hello_app.cpp
        ../hello/hello_app.h)

# widgets — 섹션 파일과 앱 골격은 Windows와 같은 파일이다.
luil_add_android_example(widgets
    SOURCES
        ../widgets/android_main.cpp
        ../widgets/app.cpp
        ../widgets/app.h
        ../widgets/choices_section.cpp
        ../widgets/controls_section.cpp
        ../widgets/inputs_section.cpp
        ../widgets/popups_section.cpp
        ../widgets/status_section.cpp
        ../widgets/toasts_section.cpp)

# mobile demo — 휴대폰·태블릿용 demo다. 페이지와 셸은 Windows와 같은 파일이다.
include("${CMAKE_CURRENT_SOURCE_DIR}/../mobile_demo/mobile_demo.cmake")
luil_mobile_demo_sources(luil_mobile_demo_files)
luil_add_android_example(mobile_demo
    SOURCES
        ../mobile_demo/android_main.cpp
        ${luil_mobile_demo_files})

luil_set_ide_folder("Examples" luil_hello luil_mobile_demo luil_widgets)

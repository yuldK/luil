# Android 예제의 네이티브 절반이다.
# luil의 CMake가 공유 라이브러리로 세우고, 이 디렉터리의 Gradle 프로젝트가 APK로 싼다.
# Gradle은 <빌드>/jniLibs/<구성>/<abi>에서 라이브러리를 집는다 (hello/build.gradle.kts).
function(luil_add_android_example target)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "" "SOURCES")
    add_library(${target} SHARED ${arguments_SOURCES})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(${target} PRIVATE luil::luil luil_options)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${target}>"
            "${CMAKE_BINARY_DIR}/jniLibs/$<CONFIG>/${ANDROID_ABI}/$<TARGET_FILE_NAME:${target}>"
        VERBATIM)
endfunction()

# hello — 앱 쪽 절반(hello_app)은 Windows와 같은 파일이다.
luil_add_android_example(luil_hello
    SOURCES
        ../hello/android_main.cpp
        ../hello/hello_app.cpp
        ../hello/hello_app.h)

luil_set_ide_folder("Examples" luil_hello)

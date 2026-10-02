include_guard(GLOBAL)

# Android 구성을 검사한다.
# Skia 패키지(android-arm64)가 정한 것과 어긋나면 링크는 되어도 기기에서 깨지므로
# configure에서 원인과 함께 잡는다 (docs/android-port-plan.md 2단계).
#  - ABI는 arm64-v8a 하나다. 패키지가 그것만 담는다.
#  - API 수준은 패키지의 ndk_api 이상이어야 한다. 그보다 낮은 기기에는 Skia가 쓰는
#    함수가 없다. 하한은 skia.cmake가 패키지의 args.gn과 맞대 본다.
#  - C++ 런타임은 NDK의 libc++ 정적판이다. skia-prep이 그 조합으로 기기에서 확인했다.
function(luil_validate_android_platform)
    set(one_value_arguments ABI MINIMUM_API_LEVEL STL)
    cmake_parse_arguments(PARSE_ARGV 0 arguments "" "${one_value_arguments}" "")

    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments passed to luil_validate_android_platform: "
            "${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arguments_ABI OR NOT arguments_MINIMUM_API_LEVEL OR NOT arguments_STL)
        message(FATAL_ERROR "ABI, MINIMUM_API_LEVEL and STL are required.")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        message(FATAL_ERROR "luil_validate_android_platform needs an Android toolchain.")
    endif()
    # NDK toolchain 파일(build/cmake/android.toolchain.cmake)이 정하는 값이다.
    # CMake 자체의 Android 지원만으로 구성하면 이 변수들이 없다.
    if(NOT DEFINED ANDROID_ABI OR NOT DEFINED ANDROID_PLATFORM_LEVEL OR NOT DEFINED ANDROID_STL)
        message(FATAL_ERROR
            "Configure Android with the NDK toolchain file:\n"
            "  -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake\n"
            "The android-arm64 presets in CMakePresets.json set it from ANDROID_NDK_HOME.")
    endif()
    if(NOT ANDROID_ABI STREQUAL arguments_ABI)
        message(FATAL_ERROR
            "The Android ABI must be ${arguments_ABI}. Current value: ${ANDROID_ABI}\n"
            "The Skia package is built for that ABI only.")
    endif()
    if(ANDROID_PLATFORM_LEVEL LESS arguments_MINIMUM_API_LEVEL)
        message(FATAL_ERROR
            "Android API level ${arguments_MINIMUM_API_LEVEL} or newer is required. "
            "Current value: ${ANDROID_PLATFORM_LEVEL}")
    endif()
    if(NOT ANDROID_STL STREQUAL arguments_STL)
        message(FATAL_ERROR
            "The Android C++ runtime must be ${arguments_STL}. Current value: ${ANDROID_STL}")
    endif()
endfunction()

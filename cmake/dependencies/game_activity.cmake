include_guard(GLOBAL)

# GameActivity의 네이티브 절반이다 (scripts/fetch_game_activity.ps1).
# Android 앱 host가 쓰는 헤더와 arm64-v8a 정적 라이브러리를 imported target으로 노출한다.
# CMake는 네트워크를 건드리지 않고 이미 존재하는 파일만 검사한다.
#
# AGP의 prefab을 거치지 않고 luil의 CMake가 직접 링크한다. luil 라이브러리는 Gradle
# 없이 CMake만으로 세우기 때문이다 (docs/android-port-plan.md 3단계).
#  - 라이브러리의 abi.json은 c++_shared·NDK 23으로 적혀 있다. prefab이면 c++_static
#    소비자를 거절하는 조합이지만, 직접 링크해 보면 libc++ 기호를 c++_static이 그대로
#    채우고 libc++_shared.so 의존이 생기지 않는다 (2026-10-02, NDK r27d).
#  - 정적 라이브러리의 JNI 진입점은 앱 코드가 부르지 않으므로 링커가 버린다.
#    `-u`로 그 기호를 붙잡아 진입점이 끌어오는 나머지가 함께 남게 한다.
function(luil_find_game_activity)
    set(library "${LUIL_GAME_ACTIVITY_ROOT}/lib/libgame-activity_static.a")
    if(NOT EXISTS "${LUIL_GAME_ACTIVITY_ROOT}/include/game-activity/native_app_glue/android_native_app_glue.h"
        OR NOT EXISTS "${library}")
        message(FATAL_ERROR
            "The GameActivity package was not found: ${LUIL_GAME_ACTIVITY_ROOT}\n"
            "Fetch it: scripts/fetch_game_activity.ps1\n"
            "It downloads the AAR pinned by third_party/game-activity-prep.json and "
            "keeps only the native headers and the arm64-v8a static library.")
    endif()

    add_library(luil_game_activity_static STATIC IMPORTED GLOBAL)
    set_target_properties(luil_game_activity_static PROPERTIES
        IMPORTED_LOCATION "${library}")

    add_library(luil_game_activity INTERFACE)
    target_include_directories(luil_game_activity SYSTEM INTERFACE "${LUIL_GAME_ACTIVITY_ROOT}/include")
    target_link_libraries(luil_game_activity
        INTERFACE
            luil_game_activity_static
            android
            log)
    target_link_options(luil_game_activity INTERFACE
        "LINKER:-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")
    add_library(luil::game_activity ALIAS luil_game_activity)

    message(STATUS "GameActivity: ${LUIL_GAME_ACTIVITY_ROOT}")
endfunction()

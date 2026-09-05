include_guard(GLOBAL)

# 설치와 find_package 지원이다.
# 소비 경로 두 가지가 같은 target 이름을 쓴다:
#   add_subdirectory → luil::luil (alias)
#   find_package     → luil::luil (imported)
# Skia는 imported target이라 export 대상이 될 수 없어, 설치되는 package
# config가 빌드 트리와 같은 검사(dependencies/skia.cmake)로 다시 만든다.
function(luil_install_rules)
    include(CMakePackageConfigHelpers)

    # Debug 산출물에는 접미사 d를 붙인다.
    # 한 설치 prefix의 lib/에 Debug·Release가 서로 덮어쓰지 않고 공존해야
    # 구성별 소비자가 각각 맞는 CRT의 라이브러리에 링크된다.
    set_target_properties(luil PROPERTIES DEBUG_POSTFIX "d")

    install(TARGETS luil luil_usage luil_options
        EXPORT luil-targets
        ARCHIVE DESTINATION lib)
    # 공개 헤더와 생성 헤더(codicons·accents·version)를 한 include 루트로 합친다.
    install(DIRECTORY "${LUIL_INCLUDE_DIRECTORY}/" DESTINATION include)
    install(DIRECTORY "${LUIL_GENERATED_INCLUDE_DIRECTORY}/" DESTINATION include)
    # 공개 헤더가 `<nlohmann/json_fwd.hpp>`를 들이므로 그 헤더들도 같은 include 루트에 둔다.
    # 빌드 트리에서는 $<BUILD_INTERFACE>로 submodule을 보고, 설치본은 이 사본을 본다
    # (Skia와 같은 갈래 — imported target은 export되지 않는다).
    install(DIRECTORY "${LUIL_NLOHMANN_JSON_ROOT}/single_include/" DESTINATION include)

    install(FILES
        "${LUIL_NLOHMANN_JSON_ROOT}/LICENSE.MIT"
        "${LUIL_ASSET_DIRECTORY}/codicon.ttf"
        "${LUIL_ASSET_DIRECTORY}/LICENSE"
        "${LUIL_ASSET_DIRECTORY}/LICENSE-CODE"
        "${LUIL_SOURCE_DIRECTORY}/win32/resource_ids.h"
        "${LUIL_SOURCE_DIRECTORY}/win32/resources/luil_resources.rc.in"
        DESTINATION share/luil)

    install(EXPORT luil-targets
        NAMESPACE luil::
        FILE luil-targets.cmake
        DESTINATION lib/cmake/luil)

    configure_package_config_file(
        "${LUIL_CMAKE_DIRECTORY}/luil-config.cmake.in"
        "${PROJECT_BINARY_DIR}/luil-config.cmake"
        INSTALL_DESTINATION lib/cmake/luil)
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/luil-config-version.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${PROJECT_BINARY_DIR}/luil-config.cmake"
        "${PROJECT_BINARY_DIR}/luil-config-version.cmake"
        "${LUIL_CMAKE_DIRECTORY}/dependencies/skia.cmake"
        "${LUIL_CMAKE_DIRECTORY}/dependencies/webview2.cmake"
        "${LUIL_CMAKE_DIRECTORY}/generate_notices.cmake"
        DESTINATION lib/cmake/luil)
endfunction()

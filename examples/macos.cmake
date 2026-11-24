# macOS 예제다. 앱 쪽 절반은 Windows 예제와 같은 파일이고 진입점(macos_main.cpp)만 따로다.
#  - 예제는 앱 번들(`<이름>.app`)이다. OS 알림(UserNotifications)은 번들 식별자가 있고 서명된 앱에서만 선다.
#    실행 파일은 번들 안 `Contents/MacOS/<이름>`이고 `$<TARGET_FILE:...>`가 그것을 가리켜 smoke test가 그대로 돈다.
#  - Windows의 .rc 조각(codicon·고지)은 없다. codicon은 라이브러리가 싣는다.
set(luil_examples_directory "${CMAKE_CURRENT_LIST_DIR}")

# 대상을 앱 번들로 세운다. Info.plist는 examples/macos/Info.plist.in을 대상마다 채운 것이다.
#  - IDENTIFIER는 번들 식별자(역 도메인), NAME은 Finder·Dock·알림에 보이는 이름이다.
function(luil_add_macos_bundle target)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "IDENTIFIER;NAME" "")
    if(arguments_UNPARSED_ARGUMENTS OR NOT arguments_IDENTIFIER OR NOT arguments_NAME)
        message(FATAL_ERROR "luil_add_macos_bundle needs IDENTIFIER and NAME only.")
    endif()
    set(luil_bundle_identifier "${arguments_IDENTIFIER}")
    set(luil_bundle_name "${arguments_NAME}")
    set(luil_bundle_executable "${target}")
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist")
    configure_file("${luil_examples_directory}/macos/Info.plist.in" "${plist}" @ONLY)
    set_target_properties("${target}" PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${plist}")
endfunction()

# 번들에 ad-hoc 서명(`-`)을 한다. 다른 빌드 뒤 단계(자원 복사)를 다 건 다음에 불러 그 파일까지 봉인한다.
#  - 링커가 실행 파일에만 한 서명으로는 번들의 Info.plist가 봉인되지 않아 UserNotifications가 앱을 받지 않는다.
#  - 배포용 서명(Developer ID, 공증)이 아니다. 이 기계에서 돌리는 개발 빌드용이다.
function(luil_sign_macos_bundle target)
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
        COMMENT "Signing the ${target} app bundle (ad hoc)"
        VERBATIM)
endfunction()

add_executable(luil_hello
    "${luil_examples_directory}/hello/hello_app.cpp"
    "${luil_examples_directory}/hello/hello_app.h"
    "${luil_examples_directory}/hello/macos_main.cpp")
target_include_directories(luil_hello PRIVATE "${luil_examples_directory}")
target_link_libraries(luil_hello PRIVATE luil::luil luil_options)
luil_add_macos_bundle(luil_hello IDENTIFIER "io.github.yuldk.luil.hello" NAME "luil hello")
luil_sign_macos_bundle(luil_hello)

# demo — 데스크톱 demo를 AppKit host에 세운다. 페이지는 Windows와 같은 파일이고, 진입점은 demo_main.cpp의 macOS 갈래다.
#  - 되돌이 HTTP 서버는 test 도우미의 POSIX 소켓 쪽이다 (Android test와 같은 파일).
#  - Windows에만 있는 기능(보조 창·웹뷰·작업 표시줄·알림 영역·OS 알림)은 페이지가 서되 그 자리는 "지원 안 함"으로 답한다.
add_executable(luil_demo
    "${luil_examples_directory}/demo_main.cpp"
    "${luil_examples_directory}/demo/common.cpp"
    "${luil_examples_directory}/demo/gallery_page.cpp"
    "${luil_examples_directory}/demo/basics_page.cpp"
    "${luil_examples_directory}/demo/files_page.cpp"
    "${luil_examples_directory}/demo/groups_page.cpp"
    "${luil_examples_directory}/demo/images_page.cpp"
    "${luil_examples_directory}/demo/inspector_page.cpp"
    "${luil_examples_directory}/demo/lists_page.cpp"
    "${luil_examples_directory}/demo/network_page.cpp"
    "${luil_examples_directory}/demo/palette.cpp"
    "${luil_examples_directory}/demo/popups_page.cpp"
    "${luil_examples_directory}/demo/rich_text_page.cpp"
    "${luil_examples_directory}/demo/shell_menu.cpp"
    "${luil_examples_directory}/demo/tabs_page.cpp"
    "${luil_examples_directory}/demo/zoom_page.cpp"
    "${luil_examples_directory}/demo/ink_page.cpp"
    "${luil_examples_directory}/demo/theme_page.cpp"
    "${luil_examples_directory}/demo/toasts_page.cpp"
    "${luil_examples_directory}/demo/webview_page.cpp"
    "${luil_examples_directory}/demo/windows_page.cpp"
    "${luil_examples_directory}/demo/workbench_page.cpp"
    "${LUIL_TEST_DIRECTORY}/loopback_http_server.cpp"
    "${LUIL_TEST_DIRECTORY}/loopback_socket_posix.cpp")
target_include_directories(luil_demo
    PRIVATE
        "${luil_examples_directory}"
        "${LUIL_TEST_DIRECTORY}")
target_link_libraries(luil_demo PRIVATE luil::luil luil_options)
luil_add_macos_bundle(luil_demo IDENTIFIER "io.github.yuldk.luil.demo" NAME "luil demo")
# 견본 그림은 번들의 자원 폴더 `Contents/Resources/assets`에 둔다 (demo::executable_directory가 번들 안에서 그 자리를 답한다).
#  - Windows처럼 실행 파일 옆(`Contents/MacOS`)에 두면 codesign이 코드가 아닌 파일을 서명되지 않은 하위 코드로 보고 서명을 거절한다.
add_custom_command(TARGET luil_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${luil_examples_directory}/demo/assets/gradient.png"
        "$<TARGET_BUNDLE_CONTENT_DIR:luil_demo>/Resources/assets/gradient.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${luil_examples_directory}/demo/assets/spinner.gif"
        "$<TARGET_BUNDLE_CONTENT_DIR:luil_demo>/Resources/assets/spinner.gif"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${luil_examples_directory}/demo/assets/sweep.webp"
        "$<TARGET_BUNDLE_CONTENT_DIR:luil_demo>/Resources/assets/sweep.webp"
    COMMENT "Copying the demo sample images"
    VERBATIM)
luil_sign_macos_bundle(luil_demo)

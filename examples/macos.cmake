# macOS 예제다. 앱 쪽 절반은 Windows 예제와 같은 파일이고 진입점(macos_main.cpp)만 따로다.
#  - 실행 파일은 번들이 아닌 맨 실행 파일이다. host가 스스로 일반 앱으로 서므로(Dock 아이콘·메뉴 막대) 그대로 뜬다.
#  - Windows의 .rc 조각(codicon·고지)은 없다. codicon은 라이브러리가 싣는다.
set(luil_examples_directory "${CMAKE_CURRENT_LIST_DIR}")

add_executable(luil_hello
    "${luil_examples_directory}/hello/hello_app.cpp"
    "${luil_examples_directory}/hello/hello_app.h"
    "${luil_examples_directory}/hello/macos_main.cpp")
target_include_directories(luil_hello PRIVATE "${luil_examples_directory}")
target_link_libraries(luil_hello PRIVATE luil::luil luil_options)

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
# 견본 그림은 실행 파일 옆 assets에서 읽는다 (Windows와 같은 자리).
add_custom_command(TARGET luil_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${luil_examples_directory}/demo/assets/gradient.png"
        "$<TARGET_FILE_DIR:luil_demo>/assets/gradient.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${luil_examples_directory}/demo/assets/spinner.gif"
        "$<TARGET_FILE_DIR:luil_demo>/assets/spinner.gif"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${luil_examples_directory}/demo/assets/sweep.webp"
        "$<TARGET_FILE_DIR:luil_demo>/assets/sweep.webp"
    COMMENT "Copying the demo sample images"
    VERBATIM)

# 라이브러리 사용 예제들이다.
#   hello        — 최소 앱 (창 + caption + 버튼 하나). docs/how-to-start.md의 본문이다.
#   luil_demo — 전체 element 시연이자 renderer smoke test의 실행 파일이다.
# 실행 파일마다 .rc 조각(내장 codicon 폰트·라이선스·제3자 고지)이 필요하다.
set(luil_demo_notices "${LUIL_GENERATED_DIRECTORY}/third_party_notices.txt")
set(luil_demo_resource "${LUIL_GENERATED_DIRECTORY}/luil_demo.rc")
luil_generate_third_party_notices("${luil_demo_notices}")
luil_configure_resource_script("${luil_demo_resource}" "${luil_demo_notices}")

# 라이브러리와 같은 방식으로 IDE 필터를 짠다 (luil_add_source_filter).
# 필터가 곧 실행 파일의 source 목록이라 두 벌이 어긋날 자리가 없다.
# 앱 뼈대(창 만들기·frame 만들기)와 화면 내용을 가른다 — 예제를 읽는 사람이
# 먼저 볼 것과 하나씩 골라 볼 것이 나뉜다.
#
# .rc 조각은 셋이 함께 쓰므로 묶음도 한 번만 정한다.
luil_add_source_filter(luil_example_resource "resource"
    "${luil_demo_resource}")

luil_add_source_filter(luil_hello_files "app"
    hello/hello_app.cpp
    hello/hello_app.h
    hello/main.cpp)
add_executable(luil_hello WIN32
    ${luil_example_resource}
    ${luil_hello_files})
target_include_directories(luil_hello PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luil_hello PRIVATE luil::luil luil_options)

# widgets — 흔한 element를 섹션별 파일 하나씩으로 보여 주는 진열장이다.
luil_add_source_filter(luil_widgets_files "app"
    widgets/app.cpp
    widgets/app.h
    widgets/main.cpp)
luil_add_source_filter(luil_widgets_files "sections"
    widgets/choices_section.cpp
    widgets/controls_section.cpp
    widgets/inputs_section.cpp
    widgets/popups_section.cpp
    widgets/status_section.cpp
    widgets/toasts_section.cpp)
add_executable(luil_widgets WIN32
    ${luil_example_resource}
    ${luil_widgets_files})
target_include_directories(luil_widgets PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luil_widgets PRIVATE luil::luil luil_options)

# demo — 진입점과 페이지가 함께 쓰는 조각이 앱이고, 시연 내용은 페이지마다 한 벌이다.
luil_add_source_filter(luil_demo_files "app"
    demo_main.cpp
    demo/common.cpp
    demo/common.h)
luil_add_source_filter(luil_demo_files "pages"
    demo/basics_page.cpp
    demo/basics_page.h
    demo/groups_page.cpp
    demo/groups_page.h
    demo/images_page.cpp
    demo/images_page.h
    demo/lists_page.cpp
    demo/lists_page.h
    demo/network_page.cpp
    demo/network_page.h
    demo/popups_page.cpp
    demo/popups_page.h
    demo/tabs_page.cpp
    demo/tabs_page.h
    demo/zoom_page.cpp
    demo/zoom_page.h
    demo/theme_page.cpp
    demo/theme_page.h
    demo/toasts_page.cpp
    demo/toasts_page.h
    demo/webview_page.cpp
    demo/webview_page.h
    demo/windows_page.cpp
    demo/windows_page.h)
# 데모가 자기 안에 세우는 되돌이 HTTP 서버다. 예제가 인터넷·회사 프록시·남의 API
# 사정에 흔들리면 예제가 아니라서 있다.
#  - test의 도우미를 **같은 파일 그대로** 컴파일한다. 데모용으로 한 벌 더 두면
#    언젠가 한쪽만 고쳐지고, 그 순간 예제와 test가 다른 서버를 말하게 된다.
luil_add_source_filter(luil_demo_files "loopback"
    "${LUIL_TEST_DIRECTORY}/loopback_http_server.cpp"
    "${LUIL_TEST_DIRECTORY}/loopback_http_server.h"
    "${LUIL_TEST_DIRECTORY}/loopback_socket.h"
    "${LUIL_TEST_DIRECTORY}/loopback_socket_win32.cpp")
add_executable(luil_demo WIN32
    ${luil_example_resource}
    ${luil_demo_files})
target_include_directories(luil_demo
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        # 되돌이 HTTP 서버 도우미가 저기 산다 (위 "loopback" 묶음).
        "${LUIL_TEST_DIRECTORY}")
# 예제는 저장소의 일부라 내부 규율(luil_options)도 그대로 따른다.
# 소비자 프로젝트는 luil::luil만 링크하면 된다.
#  - ws2_32는 되돌이 서버의 것이다. 라이브러리(winhttp)가 요구하는 것은 저절로 따라온다.
target_link_libraries(luil_demo PRIVATE luil::luil luil_options ws2_32)

# 데모의 견본 그림들이다 (정지 png 하나와 움직이는 둘 — webp·gif).
# 라이브러리 자산(코디콘 폰트·고지)은 실행 파일 resource에 박히지만 이것들은
# **파일에서 읽는 길**을 보이는 것이 목적이라 파일로 남아 실행 파일 옆에 선다.
# 없어도 데모는 뜬다 — 그림 자리에 읽지 못한 이유가 글로 뜬다.
#  - 움직이는 둘은 형식이 갈린다. 한 화면에서 gif와 webp가 함께 돌아야 코덱이
#    우리 것이 된 자리가 눈으로 갈린다.
add_custom_command(TARGET luil_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/gradient.png"
        "$<TARGET_FILE_DIR:luil_demo>/assets/gradient.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/spinner.gif"
        "$<TARGET_FILE_DIR:luil_demo>/assets/spinner.gif"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/sweep.webp"
        "$<TARGET_FILE_DIR:luil_demo>/assets/sweep.webp"
    COMMENT "Copying the demo sample images"
    VERBATIM)

luil_set_ide_folder("Examples" luil_demo luil_hello luil_widgets)

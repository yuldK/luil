# 라이브러리 사용 예제들이다.
#   hello        — 최소 앱 (창 + caption + 버튼 하나). docs/how-to-start.md의 본문이다.
#   — 전체 element 시연이자 renderer smoke test의 실행 파일이다.
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



# widgets — 흔한 element를 섹션별 파일 하나씩으로 보여 주는 진열장이다.
luil_add_source_filter(luil_widgets_files "app"
    widgets/app.cpp
    widgets/app.h
    widgets/main.cpp)
luil_add_source_filter(luil_widgets_files "sections"
    widgets/controls_section.cpp
    widgets/inputs_section.cpp
    widgets/status_section.cpp)
add_executable(luil_widgets WIN32
    ${luil_example_resource}
    ${luil_widgets_files})
target_include_directories(luil_widgets PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(luil_widgets PRIVATE luil::luil luil_options)

# demo — 진입점과 페이지가 함께 쓰는 조각이 앱이고, 시연 내용은 페이지마다 한 벌이다.
luil_add_source_filter(luil_demo_files "app"
    demo/common.cpp
    demo/common.h)
luil_add_source_filter(luil_demo_files "pages"
    demo/basics_page.cpp
    demo/basics_page.h
    demo/theme_page.cpp
    demo/theme_page.h)
# 데모가 자기 안에 세우는 되돌이 HTTP 서버다. 예제가 인터넷·회사 프록시·남의 API
# 사정에 흔들리면 예제가 아니라서 있다.
#  - test의 도우미를 **같은 파일 그대로** 컴파일한다. 데모용으로 한 벌 더 두면
#    언젠가 한쪽만 고쳐지고, 그 순간 예제와 test가 다른 서버를 말하게 된다.


# 데모의 견본 그림들이다 (정지 png 하나와 움직이는 둘 — webp·gif).
# 라이브러리 자산(코디콘 폰트·고지)은 실행 파일 resource에 박히지만 이것들은
# **파일에서 읽는 길**을 보이는 것이 목적이라 파일로 남아 실행 파일 옆에 선다.
# 없어도 데모는 뜬다 — 그림 자리에 읽지 못한 이유가 글로 뜬다.
#  - 움직이는 둘은 형식이 갈린다. 한 화면에서 gif와 webp가 함께 돌아야 코덱이
#    우리 것이 된 자리가 눈으로 갈린다.
add_custom_command(TARGET POST_BUILD
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

luil_set_ide_folder("Examples" luil_widgets)

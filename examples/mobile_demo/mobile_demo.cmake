# mobile demo의 소스다. Windows 예제(examples/CMakeLists.txt)와 Android 예제(examples/android)가
# 함께 쓴다. 진입점(main.cpp, android_main.cpp)은 부르는 쪽이 더한다.
#   luil_mobile_demo_sources(<var>)
#
# 견본 그림은 데스크톱 demo의 assets를 그대로 쓰고, 빌드가 바이트 배열로 옮겨 싣는다
# (cmake/embed_binary.cmake). 그림 파일을 두 벌 두지 않는다.
set(luil_mobile_demo_directory "${CMAKE_CURRENT_LIST_DIR}")

function(luil_mobile_demo_sources out_var)
    set(directory "${luil_mobile_demo_directory}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/mobile_demo_assets")
    set(sources
        "${directory}/app.cpp"
        "${directory}/app.h"
        "${directory}/assets.h"
        "${directory}/basics_page.cpp"
        "${directory}/basics_page.h"
        "${directory}/common.cpp"
        "${directory}/common.h"
        "${directory}/groups_page.cpp"
        "${directory}/groups_page.h"
        "${directory}/lists_page.cpp"
        "${directory}/lists_page.h"
        "${directory}/popups_page.cpp"
        "${directory}/popups_page.h"
        "${directory}/tabs_page.cpp"
        "${directory}/tabs_page.h"
        "${directory}/theme_page.cpp"
        "${directory}/theme_page.h"
        "${directory}/toasts_page.cpp"
        "${directory}/toasts_page.h"
        "${directory}/zoom_page.cpp"
        "${directory}/zoom_page.h")
    foreach(asset IN ITEMS "gradient.png" "sweep.webp" "spinner.gif")
        string(REPLACE "." "_" symbol "mobile_demo_${asset}")
        set(input "${directory}/../demo/assets/${asset}")
        set(output "${generated}/${symbol}.cpp")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}"
                "-DINPUT_FILE=${input}"
                "-DOUTPUT_FILE=${output}"
                "-DSYMBOL=${symbol}"
                -P "${LUIL_CMAKE_DIRECTORY}/embed_binary.cmake"
            DEPENDS "${input}" "${LUIL_CMAKE_DIRECTORY}/embed_binary.cmake"
            COMMENT "Embedding ${asset} for the mobile demo"
            VERBATIM)
        list(APPEND sources "${output}")
    endforeach()
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

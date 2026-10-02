# Codicons C++ 매핑은 파일 하나를 만드는 custom command다.
# 결과 header를 라이브러리 소스에 넣어 두면 빌드 순서가 저절로 맞으므로 별도 target을 두지 않는다.
add_custom_command(
    OUTPUT "${LUIL_GENERATED_CODICONS}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE=${LUIL_ASSET_DIRECTORY}/mapping.json"
        "-DOUTPUT_FILE=${LUIL_GENERATED_CODICONS}"
        -P "${LUIL_CMAKE_DIRECTORY}/generate_codicons.cmake"
    DEPENDS
        "${LUIL_ASSET_DIRECTORY}/mapping.json"
        "${LUIL_CMAKE_DIRECTORY}/generate_codicons.cmake"
    COMMENT "Generating the Codicons C++ mapping"
    VERBATIM)

# 키 컬러 표도 같은 방식이다.
# JSON이 원본이고 형식 검증은 생성 script가 한다.
add_custom_command(
    OUTPUT "${LUIL_GENERATED_ACCENTS}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE=${LUIL_ACCENT_CATALOG}"
        "-DDEFAULT_ACCENT_ID=${LUIL_DEFAULT_ACCENT_ID}"
        "-DOUTPUT_FILE=${LUIL_GENERATED_ACCENTS}"
        -P "${LUIL_CMAKE_DIRECTORY}/generate_accents.cmake"
    DEPENDS
        "${LUIL_ACCENT_CATALOG}"
        "${LUIL_CMAKE_DIRECTORY}/generate_accents.cmake"
    COMMENT "Generating the accent catalog"
    VERBATIM)

# 스타일(중립 색·tone·치수)도 같은 방식이다.
add_custom_command(
    OUTPUT "${LUIL_GENERATED_STYLE}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE=${LUIL_STYLE}"
        "-DOUTPUT_FILE=${LUIL_GENERATED_STYLE}"
        -P "${LUIL_CMAKE_DIRECTORY}/generate_style.cmake"
    DEPENDS
        "${LUIL_STYLE}"
        "${LUIL_CMAKE_DIRECTORY}/generate_style.cmake"
    COMMENT "Generating the style"
    VERBATIM)

# 파일은 IDE 필터 단위로 적고, 그렇게 모인 것이 곧 라이브러리의 source 목록이다
# (luil_add_source_filter, cmake/macro.cmake).
#
# 묶음은 **하는 일** 단위다. 낱개 element마다 나누면 필터가 파일 목록을 한 번 더
# 적은 것에 지나지 않아 아무것도 줄여 주지 않는다.
# 한 묶음 안에는 구현(.cpp)과 그 header(.h)를 함께 둔다 — 공개 헤더가 include/에,
# 구현이 src/에 있어 디렉터리로는 갈라지지만, 고칠 때 여는 파일은 한자리에 모인다.
#
# 소스는 두 target으로 나뉜다. `luil_core`는 플랫폼을 모르는 층(messaging·text·
# theme·ui)이고, `luil`은 그 위의 플랫폼 조립(win32·net)이다. 경계를 target으로
# 긋는 이유는 다른 플랫폼에서 core만 세워 검증하기 위해서다
# (docs/android-port-plan.md). core에 Win32 헤더가 들어오지 않았는지는
# `luil_core_portability` test가 본다.
set(public_headers "${LUIL_INCLUDE_DIRECTORY}/luil")
set(luil_core_files)
set(luil_files)

# 생성 파일이다. source tree 밖에 있어 어느 계층에도 속하지 않는다.
luil_add_source_filter(luil_core_files "generated"
    "${LUIL_GENERATED_CODICONS}"
    "${LUIL_GENERATED_ACCENTS}"
    "${LUIL_GENERATED_STYLE}")

# core: 판번이다.
luil_add_source_filter(luil_core_files "core"
    luil/version.cpp
    "${public_headers}/version.h")

# messaging: thread 사이로 값을 나르는 통로다 (header-only).
luil_add_source_filter(luil_core_files "messaging"
    "${public_headers}/messaging/channel.h"
    "${public_headers}/messaging/envelope.h"
    "${public_headers}/messaging/latest_slot.h")

# text: 도메인 무관 텍스트 규칙이다 (UTF-8 순회, 편집 상태 기계).
luil_add_source_filter(luil_core_files "text"
    text/text_edit.cpp
    "${public_headers}/text/fonts.h"
    "${public_headers}/text/text_edit.h"
    text/utf8_text.cpp
    "${public_headers}/text/utf8_text.h")

# theme: 색·글꼴 선호 값과 팔레트 합성이다.
luil_add_source_filter(luil_core_files "theme"
    theme/appearance.cpp
    "${public_headers}/theme/appearance.h"
    theme/ui_style.cpp
    "${public_headers}/theme/ui_style.h"
    theme/ui_theme.cpp
    "${public_headers}/theme/ui_theme.h")

# ui: view 무관 UI element 계층이다.
# 불변 tree와 그 위에 얹히는 element를, 하는 일에 따라 나눈다.

# tree 자체와 그 정체성이다. 앱으로 나가는 메시지와 보조 기술이 읽는 역할도 여기 있다.
luil_add_source_filter(luil_core_files "ui/foundation"
    ui/accessibility.cpp
    "${public_headers}/ui/accessibility.h"
    "${public_headers}/ui/app_message.h"
    ui/ui_element.cpp
    "${public_headers}/ui/ui_element.h"
    "${public_headers}/ui/ui_element_id.h"
    ui/ui_platform.cpp
    "${public_headers}/ui/ui_platform.h"
    ui/ui_tree.cpp
    "${public_headers}/ui/ui_tree.h")

# 들어온 입력과 그 해석이다 (포인터·키 이벤트, 편집 동작, 커서 모양).
luil_add_source_filter(luil_core_files "ui/interaction"
    "${public_headers}/ui/text_input_state.h"
    "${public_headers}/ui/ui_cursor.h"
    "${public_headers}/ui/ui_events.h"
    ui/ui_interaction.cpp
    "${public_headers}/ui/ui_interaction.h")

# element가 공유하는 그리기 조각과 배치 값이다.
luil_add_source_filter(luil_core_files "ui/drawing"
    ui/draw_primitives.cpp
    "${public_headers}/ui/draw_primitives.h"
    "${public_headers}/ui/layout_metrics.h"
    "${public_headers}/ui/transition.h")

# 자식을 담고 자리를 정하는 element다.
luil_add_source_filter(luil_core_files "ui/layout"
    ui/group_element.cpp
    "${public_headers}/ui/group_element.h"
    ui/panel_element.cpp
    "${public_headers}/ui/panel_element.h"
    ui/root_element.cpp
    "${public_headers}/ui/root_element.h"
    ui/scroll_area_element.cpp
    "${public_headers}/ui/scroll_area_element.h"
    ui/zoom_view_element.cpp
    "${public_headers}/ui/zoom_view_element.h"
    ui/scroll_view_element.cpp
    "${public_headers}/ui/scroll_view_element.h"
    ui/scrollbar_element.cpp
    "${public_headers}/ui/scrollbar_element.h"
    ui/sidebar_element.cpp
    "${public_headers}/ui/sidebar_element.h"
    ui/split_handle_element.cpp
    "${public_headers}/ui/split_handle_element.h"
    ui/stack_element.cpp
    "${public_headers}/ui/stack_element.h"
    ui/strip_element.cpp
    "${public_headers}/ui/strip_element.h"
    ui/wrap_element.cpp
    "${public_headers}/ui/wrap_element.h")

# 사용자가 값을 바꾸는 element다.
luil_add_source_filter(luil_core_files "ui/controls"
    ui/button_element.cpp
    "${public_headers}/ui/button_element.h"
    ui/check_element.cpp
    "${public_headers}/ui/check_element.h"
    ui/choice_group_element.cpp
    "${public_headers}/ui/choice_group_element.h"
    ui/dropdown_element.cpp
    "${public_headers}/ui/dropdown_element.h"
    ui/slider_element.cpp
    "${public_headers}/ui/slider_element.h")

# 보여 주기만 하는 element다 (글·그림·상태 표시).
luil_add_source_filter(luil_core_files "ui/content"
    ui/glyph_element.cpp
    "${public_headers}/ui/glyph_element.h"
    ui/badge_element.cpp
    "${public_headers}/ui/badge_element.h"
    ui/image_element.cpp
    "${public_headers}/ui/image_element.h"
    ui/label_element.cpp
    "${public_headers}/ui/label_element.h"
    ui/webview_element.cpp
    "${public_headers}/ui/webview_element.h"
    ui/progress_element.cpp
    "${public_headers}/ui/progress_element.h")

# 이미지 파일과 바이트를 픽셀로 읽는다 (Skia 코덱 — png·jpeg·webp·gif).
# element가 아니라 값을 만드는 자리라 `ui/content`와 나눈다.
luil_add_source_filter(luil_core_files "ui/image"
    ui/image_decode.cpp
    "${public_headers}/ui/image_decode.h")

# 항목을 늘어놓고 그중 하나를 고르는 element다.
luil_add_source_filter(luil_core_files "ui/lists"
    ui/grouped_list_element.cpp
    "${public_headers}/ui/grouped_list_element.h"
    ui/list_element.cpp
    "${public_headers}/ui/list_element.h"
    ui/reorder_drop.h
    ui/tab_bar_element.cpp
    "${public_headers}/ui/tab_bar_element.h"
    ui/virtual_list_element.cpp
    "${public_headers}/ui/virtual_list_element.h")

# 다른 것 위에 떠서 잠깐 사는 element다.
luil_add_source_filter(luil_core_files "ui/overlays"
    ui/dialog_elements.cpp
    "${public_headers}/ui/dialog_elements.h"
    ui/menu_element.cpp
    "${public_headers}/ui/menu_element.h"
    ui/modal_host_element.cpp
    "${public_headers}/ui/modal_host_element.h"
    ui/toast_element.cpp
    "${public_headers}/ui/toast_element.h")

# 화면 맨 위의 막대다. 데스크톱은 직접 그리는 창 캡션(치수는 win32의 비클라이언트
# hit test와 나눠 쓴다), 모바일은 앱 바다.
luil_add_source_filter(luil_core_files "ui/caption"
    ui/app_bar_element.cpp
    "${public_headers}/ui/app_bar_element.h"
    ui/caption_element.cpp
    "${public_headers}/ui/caption_element.h"
    "${public_headers}/ui/caption_metrics.h")

# app: 플랫폼을 가리지 않는 앱 계약이다 (스레드 조립, frame, 렌더러 정책, 웹뷰 값).
# 회복할 수 없는 종료(`platform_fail_fast`)와 글꼴 자원은 플랫폼 계층이 정의한다.
luil_add_source_filter(luil_core_files "app"
    "${public_headers}/app/app_delegate.h"
    host/app_host.cpp
    "${public_headers}/app/app_host.h"
    host/fail_fast.h
    host/renderer_policy.cpp
    "${public_headers}/app/renderer_policy.h"
    "${public_headers}/app/webview.h")

# host: 플랫폼 host가 함께 쓰는 순수 계산이다.
# 창도 OS 시계도 모르고 값만 받아 값을 돌려주므로 플랫폼마다 다시 쓰지 않는다
# (한 frame 그리기, 렌더러 실패 물러섬, 터치·펜 접촉 추적, popup·웹뷰 자리 대조,
# 다시 그릴 표면과 timer 계획, GPU fence 대기 예산).
# host/fonts: 글꼴 registry다 (설정된 글꼴, 가족 cache, 글자별 대체, 파일·바이트 로딩).
# OS 글꼴 관리자와 사용자 언어는 플랫폼이 `platform_font_source()`로 공급한다.
luil_add_source_filter(luil_core_files "host/fonts"
    host/font_registry.cpp
    host/font_registry.h
    host/typeface_loading.cpp)

luil_add_source_filter(luil_core_files "host"
    host/fence_wait.h
    host/frame_state.cpp
    host/frame_state.h
    host/pointer_sequence.cpp
    host/pointer_sequence.h
    host/popup_reconcile.cpp
    host/popup_reconcile.h
    host/skia_renderer.cpp
    host/skia_renderer.h
    host/surface_invalidate.cpp
    host/surface_invalidate.h
    host/text_input_host.h)

# 우산 헤더는 플랫폼 조립까지 들이므로 core가 아니라 이쪽에 둔다.
luil_add_source_filter(luil_files "umbrella"
    "${public_headers}/luil.h")

# win32: Win32와 Skia 조립이다.
# 여기 남은 header는 비공개 구현 세부이고, 묶음은 계층 안의 관심사로 나눈다.

# 앱 runtime과 창 하나의 수명, 그리고 한 frame이 나르는 것이다.
# 옛 경로의 app_host·renderer_policy·webview 헤더는 `luil/app/`을 가리키는 별칭이다.
luil_add_source_filter(luil_files "win32/host"
    win32/fail_fast.cpp
    "${public_headers}/win32/app_host.h"
    "${public_headers}/win32/renderer_policy.h"
    win32/win32_window.cpp
    "${public_headers}/win32/win32_window.h"
    "${public_headers}/win32/webview.h")

# Direct3D 렌더러다. 끈 구성은 "없음"을 돌려주는 stub을 대신 컴파일한다 —
# Ganesh를 부르는 파일이 이것 하나라, 빠지면 링커가 Skia의 GPU 코드를 걷어 낸다.
if(LUIL_ENABLE_DIRECT3D)
    set(luil_direct3d_renderer_source win32/direct3d_skia_renderer.cpp)
else()
    set(luil_direct3d_renderer_source win32/direct3d_skia_renderer_stub.cpp)
endif()

# Skia 뒷단 선택과 frame 그리기다 (CPU·Direct3D).
luil_add_source_filter(luil_files "win32/renderer"
    win32/composition_device.cpp
    win32/composition_device.h
    win32/cpu_skia_renderer.cpp
    "${luil_direct3d_renderer_source}"
    win32/renderer_factory.cpp
    win32/skia_renderer.h)

# 웹뷰 host다. 웹뷰를 끈 구성은 같은 인터페이스의 stub을 대신 컴파일한다 —
# 창 계층은 어느 쪽인지 모르고, SDK 헤더를 드는 파일은 실제 host 하나뿐이다.
if(LUIL_ENABLE_WEBVIEW)
    set(luil_webview_host_source win32/webview_host.cpp)
else()
    set(luil_webview_host_source win32/webview_host_stub.cpp)
endif()

# 창 하나가 그리는 표면과 다시 그릴 자리 판정이다.
luil_add_source_filter(luil_files "win32/surface"
    win32/dpi_scale.h
    win32/secondary_surface.cpp
    win32/secondary_surface.h
    "${luil_webview_host_source}"
    win32/webview_host.h
    win32/webview_layout.cpp
    win32/webview_layout.h
    win32/webview_message_gate.cpp
    win32/webview_message_gate.h
    win32/window_surface.cpp
    win32/window_surface.h)

# 캡션 표면과 비클라이언트 hit test다.
luil_add_source_filter(luil_files "win32/caption"
    win32/caption_layout.cpp
    win32/caption_layout.h
    win32/caption_surface.cpp
    win32/caption_surface.h
    win32/window_mode.cpp
    win32/window_mode.h)

# 떠 있는 창의 자리·대조·닫힘이다 (메뉴·드롭다운).
luil_add_source_filter(luil_files "win32/popup"
    win32/popup_dismiss.cpp
    win32/popup_dismiss.h
    win32/popup_surface.cpp
    win32/popup_surface.h)

# OS에서 들어오는 것이다 (포인터·휠 번역, IME, 클립보드, 파일 끌어놓기).
luil_add_source_filter(luil_files "win32/input"
    win32/surface_input.cpp
    win32/surface_input.h
    win32/win32_clipboard.cpp
    win32/win32_clipboard.h
    win32/win32_drop.cpp
    win32/win32_drop.h
    win32/win32_tsf_input.cpp
    win32/win32_tsf_input.h)

# 글꼴이다. DirectWrite 글꼴 자원과 실행 파일 resource에 박힌 codicon 폰트·고지다.
# cache·설정·대체 규칙은 core의 host/fonts에 있고, 여기는 OS 자원만 공급한다.
luil_add_source_filter(luil_files "win32/fonts"
    win32/embedded_assets.cpp
    win32/embedded_assets.h
    win32/resource_ids.h
    win32/win32_fonts.cpp)

# 보조 기술이 읽는 UIA provider다.
luil_add_source_filter(luil_files "win32/accessibility"
    win32/uia_provider.cpp
    win32/uia_provider.h)

# 계층 안에서 두루 쓰는 것이다 (UTF-8 변환, HRESULT 오류 글).
luil_add_source_filter(luil_files "win32/support"
    win32/utf8.cpp
    win32/utf8.h
    win32/win32_error.cpp
    win32/win32_error.h)

# net: HTTP 요청과 답이다.
# 어휘와 몸 판정은 창도 소켓도 모르는 값·순수 함수라 test가 서버 없이 규약 전부를
# 잠그고, 그 뒤의 WinHTTP 조립이 같은 묶음에 있다.
#  - `win32` 안이 아닌 이유는 그 계층의 정체가 **창**이어서다 (HWND·메시지 루프·
#    COM STA·렌더러·IME 어느 것도 HTTP에 없다). `win32` **위**인 이유는 의존
#    방향이다 — utf8 변환과 오류 글을 그 계층에서 가져다 쓴다.
luil_add_source_filter(luil_files "net"
    net/http_body.cpp
    "${public_headers}/net/http_body.h"
    net/http_client.cpp
    "${public_headers}/net/http_client.h"
    net/http_media_type.cpp
    "${public_headers}/net/http_media_type.h"
    net/http_message.cpp
    "${public_headers}/net/http_message.h"
    net/http_request_context.cpp
    net/http_request_context.h
    net/http_text_transcode.cpp
    net/http_text_transcode.h
    net/http_url.cpp
    net/http_url.h
    net/winhttp_error.cpp
    net/winhttp_error.h)

# 플랫폼을 모르는 층이다 (messaging → text → theme → ui).
# 층 사이의 방향은 디렉터리 구조와 코드 리뷰로 지키고, 플랫폼과의 경계만
# target으로 긋는다.
add_library(luil_core STATIC ${luil_core_files})
add_library(luil::luil_core ALIAS luil_core)
target_include_directories(luil_core
    PUBLIC
        "$<BUILD_INTERFACE:${LUIL_INCLUDE_DIRECTORY}>"
        "$<BUILD_INTERFACE:${LUIL_GENERATED_INCLUDE_DIRECTORY}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${LUIL_SOURCE_DIRECTORY}")
target_link_libraries(luil_core
    PUBLIC
        # 소비자 요건(C++20·/utf-8)만 전파한다.
        # /W4 /WX 같은 내부 규율은 PRIVATE의 luil_options에 있다.
        luil_usage
        # Skia는 imported target이라 export할 수 없다.
        # 빌드 트리에서만 여기서 잇고, 설치본은 package config가
        # 같은 검사(cmake/dependencies/skia.cmake)로 다시 만들어 잇는다.
        "$<BUILD_INTERFACE:luil_skia>"
    PRIVATE
        luil_options)

# 자산 checksum 검증은 빌드마다 한 번 실행한다.
# 별도 target 대신 라이브러리의 빌드 전 단계에 붙여 솔루션 구성 요소를 늘리지 않는다.
# codicon 매핑을 쓰는 것이 core라 이쪽에 붙인다.
add_custom_command(TARGET luil_core PRE_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DASSET_DIRECTORY=${LUIL_ASSET_DIRECTORY}"
        -P "${LUIL_CMAKE_DIRECTORY}/verify_codicons.cmake"
    COMMENT "Verifying Codicons checksums"
    VERBATIM)

# core 위의 플랫폼 조립이다. 플랫폼마다 소스가 다르고, 소비자가 링크하는 이름은
# 어디서나 `luil::luil`이다.
if(LUIL_PLATFORM STREQUAL "windows")
    # core 위의 플랫폼 조립이다 (win32 → net).
    # 소비자가 링크하는 이름은 지금처럼 `luil::luil` 하나이고, core는 PUBLIC으로 따라온다.
    add_library(luil STATIC ${luil_files})
    add_library(luil::luil ALIAS luil)
    target_include_directories(luil
        PRIVATE
            "${LUIL_SOURCE_DIRECTORY}")
    target_link_libraries(luil
        PUBLIC
            luil_core
            luil_usage
            "$<BUILD_INTERFACE:luil_skia>"
            # JSON 본문 타입이다 (header-only). Skia와 같은 이유로 빌드 트리에서만
            # 여기서 잇고, 설치본은 헤더 사본을 include/에 함께 둔다 (cmake/install.cmake).
            "$<BUILD_INTERFACE:luil_nlohmann_json>"
            # 웹뷰가 붙는 SDK다 (헤더 둘 + 정적 로더). Skia와 같은 이유로 빌드
            # 트리에서만 여기서 잇는다 — 감싸지 않으면 install(EXPORT)가 "export set에
            # 없는 target"으로 세운다. include 경로는 그 target 안에만 있어야 한다:
            # luil에 직접 걸면 **경고 하나 없이** 설치본에 빌드 트리 절대 경로가 박힌다.
            # 웹뷰를 끈 구성에는 그 target이 없다 (LUIL_ENABLE_WEBVIEW).
            "$<$<BOOL:${LUIL_ENABLE_WEBVIEW}>:$<BUILD_INTERFACE:luil_webview2>>"
            # win32 계층이 쓰는 Windows 시스템 라이브러리다.
            # static library라 최종 링크는 소비자 몫이므로 PUBLIC으로 나른다.
            # Skia가 요구하는 것은 cmake/dependencies/skia.cmake에 따로 있다.
            d3d12
            # Direct3D 제시가 창이 아니라 합성에 붙는다
            # (webview-composition-design.md).
            dcomp
            dwrite
            dwmapi
            dxgi
            gdi32
            imm32
            ole32
            oleaut32
            shell32
            uiautomationcore
            user32
            # net 계층의 HTTP client다.
            winhttp
        PRIVATE
            luil_options)
endif()

if(LUIL_PLATFORM STREQUAL "android")
    # codicon 글꼴을 라이브러리 안에 싣는다. Android에는 실행 파일 resource가 없고,
    # 앱이 APK assets에 따로 넣는 단계를 만들지 않으려는 것이다 (docs/android-port-plan.md).
    set(luil_codicon_source "${LUIL_GENERATED_DIRECTORY}/codicon_font.cpp")
    add_custom_command(
        OUTPUT "${luil_codicon_source}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${LUIL_ASSET_DIRECTORY}/codicon.ttf"
            "-DOUTPUT_FILE=${luil_codicon_source}"
            "-DSYMBOL=luil_codicon_font"
            -P "${LUIL_CMAKE_DIRECTORY}/embed_binary.cmake"
        DEPENDS
            "${LUIL_ASSET_DIRECTORY}/codicon.ttf"
            "${LUIL_CMAKE_DIRECTORY}/embed_binary.cmake"
        COMMENT "Embedding the Codicons font"
        VERBATIM)

    # android: GameActivity 위의 앱 host다.
    # 이벤트 루프·수명 주기·안전 영역·한 frame 조립, Vulkan·CPU 렌더러, 글꼴 자원과 내장 codicon이다.
    set(luil_android_files)
    luil_add_source_filter(luil_android_files "android"
        "${public_headers}/android/android_app.h"
        android/android_app.cpp
        android/android_assets.cpp
        android/android_clipboard.cpp
        android/android_clipboard.h
        android/android_fonts.cpp
        android/android_fonts.h
        android/android_input.cpp
        android/android_input.h
        android/android_text_input.cpp
        android/android_text_input.h
        android/cpu_skia_renderer.cpp
        android/cpu_skia_renderer.h
        android/fail_fast.cpp
        android/vulkan_device.cpp
        android/vulkan_device.h
        android/vulkan_memory_allocator.h
        android/vulkan_skia_renderer.cpp
        android/vulkan_skia_renderer.h
        "${luil_codicon_source}")

    add_library(luil STATIC ${luil_android_files})
    add_library(luil::luil ALIAS luil)
    target_include_directories(luil
        PRIVATE
            "${LUIL_SOURCE_DIRECTORY}")
    target_link_libraries(luil
        PUBLIC
            luil_core
            luil_usage
            "$<BUILD_INTERFACE:luil_skia>"
            # 우산 헤더가 HTTP 값 헤더를 들이고 그것이 JSON 타입을 쓴다.
            "$<BUILD_INTERFACE:luil_nlohmann_json>"
            # 앱 host가 GameActivity의 native_app_glue 위에 선다.
            # JNI 진입점을 붙잡는 링크 옵션도 이 target이 함께 나른다.
            "$<BUILD_INTERFACE:luil_game_activity>"
            android
            log
        PRIVATE
            luil_options)
endif()

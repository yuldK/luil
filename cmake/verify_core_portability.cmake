# 플랫폼을 모르는 층(luil_core)이 플랫폼 헤더를 들이지 않았는지 본다.
#
# Windows에서는 core가 Win32 헤더를 include해도 그대로 빌드되고 test도 통과한다 —
# 그 실수는 다른 플랫폼에서 core를 처음 세울 때에야 드러난다. 그래서 include 줄을
# 직접 읽어 Windows에서 막는다 (docs/android-port-plan.md).
#
# 검사 대상은 include 줄뿐이다. 주석에 HWND·WM_CHAR 같은 이름이 나오는 것은 Win32와의
# 관계를 설명하는 정당한 글이라 잡지 않는다.
if(NOT DEFINED SOURCE_DIRECTORY OR NOT DEFINED INCLUDE_DIRECTORY)
    message(FATAL_ERROR "SOURCE_DIRECTORY and INCLUDE_DIRECTORY are required.")
endif()

# 검사하는 디렉터리다. 기본은 core를 이루는 디렉터리이고 src/CMakeLists.txt의 luil_core_files와 같은 범위다.
#  - 다른 라이브러리에도 `CHECKED_DIRECTORIES`로 검사할 디렉터리를 지정할 수 있다.
#    그 라이브러리도 core만 링크하므로 규칙이 같다. 위반 경로는 그때도 SOURCE_DIRECTORY의 부모에서 센다.
if(DEFINED CHECKED_DIRECTORIES)
    set(checked_directories ${CHECKED_DIRECTORIES})
    set(checked_subject "The checked sources include")
else()
    set(checked_directories
        "${SOURCE_DIRECTORY}/host"
        "${SOURCE_DIRECTORY}/luil"
        "${SOURCE_DIRECTORY}/text"
        "${SOURCE_DIRECTORY}/theme"
        "${SOURCE_DIRECTORY}/ui"
        "${INCLUDE_DIRECTORY}/luil/app"
        "${INCLUDE_DIRECTORY}/luil/messaging"
        "${INCLUDE_DIRECTORY}/luil/text"
        "${INCLUDE_DIRECTORY}/luil/theme"
        "${INCLUDE_DIRECTORY}/luil/ui")
    set(checked_subject "luil_core includes")
endif()

# 플랫폼 SDK 헤더와 MSVC 전용 헤더다. 비교 전에 소문자로 바꾸므로 소문자로 적는다.
#  - Apple과 Android의 것은 디렉터리(프레임워크) 이름으로 적는다. 끝의 `/`까지가 이름이라
#    같은 글자로 시작하는 luil 헤더(`foundation_...h`)를 잡지 않는다.
set(windows_header_pattern
    "windows|winuser|wingdi|winbase|winnls|winhttp|winsock2|ws2tcpip|objbase|combaseapi|unknwn|wrl|d3d1[12]|dxgi|dcomp|dwrite|dwmapi|uiautomation|oleacc|ole2|shellapi|shlobj|imm|msctf|commctrl|intrin|webview2")
set(apple_header_pattern
    "cocoa/|appkit/|uikit/|foundation/|corefoundation/|coregraphics/|coretext/|coreservices/|quartzcore/|metal/|metalkit/|webkit/|security/|usernotifications/|avfoundation/|carbon/|objc/|dispatch/|mach/|targetconditionals")
set(android_header_pattern
    "jni\\.h|android/|game-activity/|game-text-input/")
set(platform_header_pattern
    "(${windows_header_pattern}|${apple_header_pattern}|${android_header_pattern})[^>\"]*")
# luil 안에서 core 위의 계층이다.
#  - macOS 계층의 디렉터리 이름은 아직 정하지 않았다. 어느 쪽으로 정해도 잡히게 둘 다 적는다.
set(upper_layer_pattern "(luil/)?(win32|android|mac|macos|ios|net)/")

set(violations "")
set(checked_count 0)
foreach(directory IN LISTS checked_directories)
    if(NOT IS_DIRECTORY "${directory}")
        message(FATAL_ERROR "A checked directory is missing: ${directory}")
    endif()
    file(GLOB_RECURSE files "${directory}/*.h" "${directory}/*.cpp")
    foreach(file IN LISTS files)
        math(EXPR checked_count "${checked_count} + 1")
        # `#import`도 본다. clang은 C++ 소스에서도 그것을 받는다.
        file(STRINGS "${file}" include_lines REGEX "^[ \t]*#[ \t]*(include|import)")
        foreach(line IN LISTS include_lines)
            string(TOLOWER "${line}" lowered)
            if(lowered MATCHES "#[ \t]*(include|import)[ \t]*[<\"]${platform_header_pattern}[>\"]"
                OR line MATCHES "#[ \t]*(include|import)[ \t]*\"${upper_layer_pattern}")
                file(RELATIVE_PATH relative "${SOURCE_DIRECTORY}/.." "${file}")
                string(STRIP "${line}" stripped)
                list(APPEND violations "${relative}: ${stripped}")
            endif()
        endforeach()
    endforeach()
endforeach()

list(LENGTH violations violation_count)
if(violation_count GREATER 0)
    string(REPLACE ";" "\n  " violation_text "${violations}")
    message(FATAL_ERROR
        "${checked_subject} platform or upper-layer headers.\n"
        "Move the dependency behind a platform interface or out of core:\n  ${violation_text}")
endif()

message(STATUS "Core portability check passed: ${checked_count} file(s)")

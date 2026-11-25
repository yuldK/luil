# iOS 예제다 (iPhone·iPad). 앱 쪽 절반은 다른 플랫폼과 같은 파일이고 진입점(ios_main.cpp)만 따로다.
#  - 예제는 앱 번들(`<이름>.app`)이다. 시뮬레이터에는 `xcrun simctl install booted <번들>`로 넣고 `xcrun simctl launch booted <번들 id>`로
#    띄운다. 번들 id는 `LUIL_IOS_BUNDLE_PREFIX`(기본 `io.github.yuldk.luil`) 뒤에 예제 이름을 붙인 것이다.
#  - 서명은 앱이 부르는 것과 같은 `luil_ios_sign_bundle`(cmake/ios/sign.cmake)이다. 시뮬레이터 빌드는 ad-hoc 서명이다. 기기
#    빌드는 서명 신원(`LUIL_IOS_CODE_SIGN_IDENTITY`, 예: "Apple Development: …")과 그 번들 id들을 덮는 provisioning profile
#    (`LUIL_IOS_PROVISIONING_PROFILE`, .mobileprovision 경로)을 주면 서명한다. 주지 않으면 서명하지 않은 번들이라 기기에 설치되지 않는다.
#  - 예제 하나의 번들 id를 통째로 정하려면 `LUIL_IOS_BUNDLE_IDENTIFIER_<대상>`(예: `LUIL_IOS_BUNDLE_IDENTIFIER_luil_mobile_demo`)을 준다.
#    무료 계정의 profile은 앱 id 하나에만 맞으므로, 그 profile로 서명할 예제를 그 id에 맞추고 그 대상만 빌드한다.
#  - 데스크톱 demo(창·트레이·전역 단축키를 보이는 것)는 세우지 않는다. 모바일 demo가 iPhone·iPad의 화면이다.
set(luil_examples_directory "${CMAKE_CURRENT_LIST_DIR}")

set(LUIL_IOS_BUNDLE_PREFIX "io.github.yuldk.luil" CACHE STRING "Bundle identifier prefix of the iOS examples.")
set(LUIL_IOS_CODE_SIGN_IDENTITY "" CACHE STRING "Code signing identity for the iOS device examples (empty: unsigned).")
set(LUIL_IOS_PROVISIONING_PROFILE "" CACHE FILEPATH "Provisioning profile (.mobileprovision) for the iOS device examples.")

# 대상을 앱 번들로 세운다. Info.plist는 examples/ios/Info.plist.in을 대상마다 채운 것이다.
#  - NAME은 홈 화면·알림에 보이는 이름, SUFFIX는 번들 id의 끝이다.
function(luil_add_ios_bundle target)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "SUFFIX;NAME" "")
    if(arguments_UNPARSED_ARGUMENTS OR NOT arguments_SUFFIX OR NOT arguments_NAME)
        message(FATAL_ERROR "luil_add_ios_bundle needs SUFFIX and NAME only.")
    endif()
    # `LUIL_IOS_BUNDLE_IDENTIFIER_<대상>`을 주면 그 값이 번들 id다. 무료 계정의 profile은 앱 id 하나에만 맞아 그 값에 맞춘다.
    if(LUIL_IOS_BUNDLE_IDENTIFIER_${target})
        set(luil_bundle_identifier "${LUIL_IOS_BUNDLE_IDENTIFIER_${target}}")
    else()
        set(luil_bundle_identifier "${LUIL_IOS_BUNDLE_PREFIX}.${arguments_SUFFIX}")
    endif()
    set(luil_bundle_name "${arguments_NAME}")
    set(luil_bundle_executable "${target}")
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist")
    configure_file("${luil_examples_directory}/ios/Info.plist.in" "${plist}" @ONLY)
    set_target_properties("${target}" PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${plist}")
    # SMB를 켠 구성은 luil이 libsmb2.framework를 링크한다. 서명보다 먼저 번들에 넣고 같은 신원으로 서명한다.
    #  - 시뮬레이터는 ad-hoc 서명과 앱 id·Keychain 권한 구역이고, 기기는 신원 + profile이다 (`luil_ios_sign_bundle`). 서명은 번들을
    #    바꾸는 빌드 뒤 단계를 다 건 다음이다.
    set(identity "${LUIL_IOS_CODE_SIGN_IDENTITY}")
    if(LUIL_IOS_SIMULATOR)
        set(identity "-")
    endif()
    luil_ios_embed_smb("${target}" CODE_SIGN_IDENTITY "${identity}")
    luil_ios_sign_bundle("${target}"
        BUNDLE_IDENTIFIER "${luil_bundle_identifier}"
        CODE_SIGN_IDENTITY "${LUIL_IOS_CODE_SIGN_IDENTITY}"
        PROVISIONING_PROFILE "${LUIL_IOS_PROVISIONING_PROFILE}")
endfunction()

add_executable(luil_hello
    "${luil_examples_directory}/hello/hello_app.cpp"
    "${luil_examples_directory}/hello/hello_app.h"
    "${luil_examples_directory}/hello/ios_main.cpp")
target_include_directories(luil_hello PRIVATE "${luil_examples_directory}")
target_link_libraries(luil_hello PRIVATE luil::luil luil_options)
luil_add_ios_bundle(luil_hello SUFFIX "hello" NAME "luil hello")

# mobile demo — Android와 같은 페이지·delegate다. 견본 그림은 빌드가 바이트 배열로 실어 번들 자원이 없다.
#  - SMB 페이지는 LUIL_ENABLE_SMB를 켠 구성에서 libsmb2 위의 client를 쓰고, 끈 구성에서는 "지원 안 함"을 보인다.
#    파일 감시는 앱 컨테이너의 `Library/Application Support` 아래를 본다.
include("${luil_examples_directory}/mobile_demo/mobile_demo.cmake")
luil_mobile_demo_sources(luil_mobile_demo_files)
add_executable(luil_mobile_demo
    "${luil_examples_directory}/mobile_demo/ios_main.cpp"
    ${luil_mobile_demo_files})
target_include_directories(luil_mobile_demo PRIVATE "${luil_examples_directory}")
target_link_libraries(luil_mobile_demo PRIVATE luil::luil luil_options)
luil_add_ios_bundle(luil_mobile_demo SUFFIX "mobile-demo" NAME "luil mobile")

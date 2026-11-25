include_guard(GLOBAL)

# iOS 앱 번들을 서명한다 (docs/concepts/consumer-contract.md의 "iOS 앱 번들").
#
#   luil_ios_sign_bundle(<앱 대상>
#       [BUNDLE_IDENTIFIER <번들 id>]
#       [CODE_SIGN_IDENTITY <신원>]
#       [PROVISIONING_PROFILE <.mobileprovision>])
#
# 앱 대상은 `MACOSX_BUNDLE`인 실행 파일이다. 시뮬레이터 SDK(iphonesimulator)인지 기기 SDK(iphoneos)인지에 따라 하는 일이 다르다.
#  - **시뮬레이터:** 실행 파일의 `__TEXT,__entitlements` 구역에 앱 id(`application-identifier`)와 Keychain 묶음
#    (`keychain-access-groups`)을 싣고, 빌드 뒤에 번들을 ad-hoc(`-`)으로 서명한다. 신원과 profile은 받아도 쓰지 않는다 — 같은
#    호출을 두 구성에 그대로 둘 수 있다.
#      - 권한이 없으면 비밀 저장소(Keychain)가 `errSecMissingEntitlement`(-34018)로 거절한다. 서명에 실으면 프로비저닝 없는 제한
#        권한이라 시뮬레이터가 앱을 띄우지 않으므로 구역에 싣는다 (Xcode의 시뮬레이터 빌드와 같다). 팀 id 자리는 시뮬레이터가 보지
#        않아 `LUILSIMULATOR`다.
#      - 번들 id가 필요하다. `BUNDLE_IDENTIFIER`를 주지 않으면 대상의 `MACOSX_BUNDLE_GUI_IDENTIFIER`를 쓴다. Info.plist의
#        `CFBundleIdentifier`와 같은 값이어야 한다.
#  - **기기:** 신원(예: "Apple Development: …" 또는 그 SHA-1)과 그 번들 id를 덮는 provisioning profile을 주면, 빌드 뒤에 profile을
#    번들의 `embedded.mobileprovision`으로 넣고 그 안의 권한으로 서명한다. 번들 id가 profile의 앱 id(끝이 `*`인 와일드카드 포함)에
#    들지 않으면 그 빌드 단계가 원인과 함께 멈춘다 — 서명은 되어도 기기가 설치를 거절하기 때문이다. 신원을 주지 않으면 서명하지
#    않고 알리기만 한다 (기기에 설치되지 않는다). profile 없이 신원만 주면 권한 없이 서명한다.
#
# 순서: **번들 안의 파일을 바꾸는 빌드 뒤 단계(`luil_ios_embed_smb`, 앱의 자원·프레임워크 복사)를 모두 건 다음에 부른다.** 같은
# 대상의 빌드 뒤 단계는 건 순서대로 돌고, 서명이 그때까지 들어온 파일을 봉인한다. 서명 뒤에 바뀐 파일이 있으면 기기가 설치를
# 거절한다. 한 대상에 한 번만 부른다.
function(luil_ios_sign_bundle target)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "BUNDLE_IDENTIFIER;CODE_SIGN_IDENTITY;PROVISIONING_PROFILE" "")
    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "luil_ios_sign_bundle takes a target and BUNDLE_IDENTIFIER, CODE_SIGN_IDENTITY, PROVISIONING_PROFILE only: "
            "${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "luil_ios_sign_bundle: ${target} is not a target.")
    endif()
    get_target_property(bundle "${target}" MACOSX_BUNDLE)
    if(NOT bundle)
        message(FATAL_ERROR "luil_ios_sign_bundle: ${target} is not an app bundle (set MACOSX_BUNDLE).")
    endif()
    get_target_property(signed "${target}" LUIL_IOS_SIGNED)
    if(signed)
        message(FATAL_ERROR "luil_ios_sign_bundle: ${target} is already signed. Call it once, after the other post-build steps.")
    endif()
    set_target_properties("${target}" PROPERTIES LUIL_IOS_SIGNED TRUE)

    set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/sign_bundle.cmake")
    if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        set(identifier "${arguments_BUNDLE_IDENTIFIER}")
        if(NOT identifier)
            get_target_property(identifier "${target}" MACOSX_BUNDLE_GUI_IDENTIFIER)
        endif()
        if(NOT identifier)
            message(FATAL_ERROR
                "luil_ios_sign_bundle: ${target} needs BUNDLE_IDENTIFIER (or MACOSX_BUNDLE_GUI_IDENTIFIER) for the simulator entitlements.")
        endif()
        set(entitlements "${CMAKE_CURRENT_BINARY_DIR}/${target}-simulator.entitlements")
        file(CONFIGURE OUTPUT "${entitlements}" CONTENT [[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>application-identifier</key>
    <string>LUILSIMULATOR.${identifier}</string>
    <key>keychain-access-groups</key>
    <array>
        <string>LUILSIMULATOR.${identifier}</string>
    </array>
</dict>
</plist>
]])
        target_link_options("${target}" PRIVATE "LINKER:-sectcreate,__TEXT,__entitlements,${entitlements}")
        set_property(TARGET "${target}" APPEND PROPERTY LINK_DEPENDS "${entitlements}")
        set(identity "-")
        set(profile_argument)
    else()
        set(identity "${arguments_CODE_SIGN_IDENTITY}")
        if(NOT identity)
            message(STATUS "luil_ios_sign_bundle: no CODE_SIGN_IDENTITY for ${target}; the bundle is left unsigned and will not install on a device.")
            return()
        endif()
        set(profile_argument)
        if(arguments_PROVISIONING_PROFILE)
            get_filename_component(profile "${arguments_PROVISIONING_PROFILE}" ABSOLUTE)
            if(NOT EXISTS "${profile}")
                message(FATAL_ERROR "luil_ios_sign_bundle: the provisioning profile does not exist: ${profile}")
            endif()
            set(profile_argument "-DPROFILE=${profile}")
        endif()
    endif()

    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DBUNDLE=$<TARGET_BUNDLE_DIR:${target}>"
            "-DIDENTITY=${identity}"
            ${profile_argument}
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${script}"
        COMMENT "Signing the ${target} app bundle"
        VERBATIM)
endfunction()

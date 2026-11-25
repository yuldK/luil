include_guard(GLOBAL)

# iOS 앱 번들에 libsmb2.framework를 싣는다 (docs/concepts/consumer-contract.md의 "SMB").
#
#   luil_ios_embed_smb(<앱 대상> [CODE_SIGN_IDENTITY <신원>])
#
# `LUIL_ENABLE_SMB`를 켠 iOS 구성에서는 luil이 libsmb2를 앱 실행 파일에 섞지 않고 따로 선 동적 프레임워크로 링크한다
# (LGPL-2.1의 바꿔 끼우기). 그 프레임워크가 앱 번들에 없으면 앱이 뜨자마자 dyld가 멈춘다. 이 함수가 하는 것:
#  - 앱 실행 파일의 rpath를 `@executable_path/Frameworks` 하나로 둔다 (빌드 폴더의 절대 경로를 싣지 않는다). 앱이 이미 둔
#    `INSTALL_RPATH`는 남긴다.
#  - 빌드 뒤에 `<앱>.app/Frameworks/libsmb2.framework`로 복사하고 그 자리에서 서명한다. 신원을 주지 않으면 ad-hoc(`-`)이라
#    시뮬레이터에서만 돈다. 기기는 앱과 같은 신원을 준다.
#  - **앱을 서명하는 빌드 뒤 단계보다 먼저 부른다.** 같은 대상의 빌드 뒤 단계는 건 순서대로 돌고, 앱 서명이 안의 프레임워크
#    서명을 봉인한다. 앱을 나중에 다시 서명하는 도구(Xcode 보관·배포)를 쓰면 그 도구가 프레임워크를 다시 서명해도 된다.
# `LUIL_ENABLE_SMB`가 꺼져 있으면 아무것도 하지 않는다 (앱이 구성과 상관없이 부를 수 있다).
function(luil_ios_embed_smb target)
    cmake_parse_arguments(PARSE_ARGV 1 arguments "" "CODE_SIGN_IDENTITY" "")
    if(arguments_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "luil_ios_embed_smb takes a target and CODE_SIGN_IDENTITY only: ${arguments_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "luil_ios_embed_smb: ${target} is not a target.")
    endif()
    if(NOT LUIL_ENABLE_SMB OR NOT TARGET luil_smb2)
        return()
    endif()
    set(identity "${arguments_CODE_SIGN_IDENTITY}")
    if(NOT identity)
        set(identity "-")
    endif()
    find_program(LUIL_CODESIGN_EXECUTABLE codesign REQUIRED)

    set_property(TARGET "${target}" APPEND PROPERTY INSTALL_RPATH "@executable_path/Frameworks")
    set_target_properties("${target}" PROPERTIES BUILD_WITH_INSTALL_RPATH ON)
    add_dependencies("${target}" luil_smb2)

    set(destination "$<TARGET_BUNDLE_DIR:${target}>/Frameworks")
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${destination}/libsmb2.framework"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:luil_smb2>" "${destination}/libsmb2.framework"
        COMMAND "${LUIL_CODESIGN_EXECUTABLE}" --force --sign "${identity}" --timestamp=none "${destination}/libsmb2.framework"
        COMMENT "Embedding libsmb2.framework in the ${target} app bundle"
        VERBATIM)
endfunction()

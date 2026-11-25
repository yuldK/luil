# iOS 앱 번들 하나를 서명한다 (examples/ios.cmake의 luil_sign_ios_bundle이 빌드 뒤에 부른다).
#   cmake -DBUNDLE=<.app> -DIDENTITY=<서명 신원> [-DPROFILE=<.mobileprovision>] [-DWORK=<임시 폴더>] -P sign_bundle.cmake
#  - 신원이 `-`이면 ad-hoc 서명이다. 시뮬레이터는 이것으로 돈다 (번들의 Info.plist까지 봉인해야 UserNotifications가 앱을 받는다).
#    시뮬레이터의 앱 id 권한은 서명이 아니라 실행 파일의 `__TEXT,__entitlements` 구역에 실린다 (examples/ios.cmake). 서명에
#    실으면 프로비저닝이 없는 제한 권한이라 시뮬레이터가 앱을 띄우지 않는다.
#  - 기기는 개발자 신원과 그 앱 id를 덮는 provisioning profile이 있어야 설치된다. profile을 번들에 `embedded.mobileprovision`으로
#    넣고, 그 안의 권한(Entitlements)을 꺼내 서명에 싣는다.
if(NOT BUNDLE OR NOT IDENTITY)
    message(FATAL_ERROR "sign_bundle.cmake needs BUNDLE and IDENTITY.")
endif()
if(NOT IS_DIRECTORY "${BUNDLE}")
    message(FATAL_ERROR "The app bundle does not exist: ${BUNDLE}")
endif()

set(entitlement_arguments)
if(NOT WORK)
    get_filename_component(WORK "${BUNDLE}" DIRECTORY)
endif()
get_filename_component(bundle_name "${BUNDLE}" NAME_WE)
if(PROFILE)
    if(NOT EXISTS "${PROFILE}")
        message(FATAL_ERROR "The provisioning profile does not exist: ${PROFILE}")
    endif()
    set(decoded "${WORK}/${bundle_name}-profile.plist")
    set(entitlements "${WORK}/${bundle_name}.entitlements")
    file(COPY_FILE "${PROFILE}" "${BUNDLE}/embedded.mobileprovision")
    # profile은 CMS로 싼 plist다. 풀어서 권한만 꺼낸다.
    execute_process(
        COMMAND security cms -D -i "${PROFILE}" -o "${decoded}"
        RESULT_VARIABLE decode_result
        ERROR_VARIABLE decode_error)
    if(NOT decode_result EQUAL 0)
        message(FATAL_ERROR "Cannot decode the provisioning profile ${PROFILE}: ${decode_error}")
    endif()
    execute_process(
        COMMAND plutil -extract Entitlements xml1 -o "${entitlements}" "${decoded}"
        RESULT_VARIABLE extract_result
        ERROR_VARIABLE extract_error)
    if(NOT extract_result EQUAL 0)
        message(FATAL_ERROR "The provisioning profile has no entitlements: ${extract_error}")
    endif()
    set(entitlement_arguments --entitlements "${entitlements}")
endif()

execute_process(
    COMMAND codesign --force --sign "${IDENTITY}" ${entitlement_arguments} --timestamp=none --generate-entitlement-der "${BUNDLE}"
    RESULT_VARIABLE sign_result
    ERROR_VARIABLE sign_error)
if(NOT sign_result EQUAL 0)
    message(FATAL_ERROR "Signing ${BUNDLE} failed: ${sign_error}")
endif()

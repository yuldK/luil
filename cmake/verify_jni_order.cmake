# JNI 실패 검사가 nullptr 검사보다 앞에 있는지 본다.
#
# `jni_failed(env)`는 예외를 거두고 참을 돌려준다 (src/android/java_vm.h). `x == nullptr || jni_failed(env)`처럼
# nullptr 검사가 앞이면, x가 null일 때 `||`가 `jni_failed`를 부르지 않고 돌아가서 예외가 남는다. 호출한 쪽의
# 다음 JNI 호출이 예외가 남은 채 불려 깨진다 (android_settings.cpp 머리 주석). 기기 test는 자리의 일부만 치므로,
# 이 검사가 `src` 아래 모든 `.h`와 `.cpp`에서 `||` 뒤에 `jni_failed(`가 오는 꼴을 막는다.
#
# 여러 줄로 나뉜 `||`도 잡도록 파일 글 전체에서 찾는다. 줄 번호 대신 파일 이름과 개수를 준다.
if(NOT DEFINED SOURCE_DIRECTORY)
    message(FATAL_ERROR "SOURCE_DIRECTORY is required.")
endif()

file(GLOB_RECURSE sources "${SOURCE_DIRECTORY}/*.h" "${SOURCE_DIRECTORY}/*.cpp")
list(LENGTH sources source_count)
# 찾을 파일이 없으면 검사가 아무것도 보지 않고 통과한다. 그것을 막는다.
if(source_count EQUAL 0)
    message(FATAL_ERROR "No source files under ${SOURCE_DIRECTORY}.")
endif()

set(violations "")
foreach(source IN LISTS sources)
    file(READ "${source}" text)
    string(REGEX MATCHALL "\\|\\|[ \t\r\n]*jni_failed\\(" hits "${text}")
    list(LENGTH hits hit_count)
    if(hit_count GREATER 0)
        file(RELATIVE_PATH relative "${SOURCE_DIRECTORY}" "${source}")
        list(APPEND violations "${relative} (${hit_count})")
    endif()
endforeach()

if(violations)
    list(JOIN violations "\n  " listed)
    message(FATAL_ERROR "jni_failed must come first in a || chain, before the nullptr checks:\n  ${listed}")
endif()

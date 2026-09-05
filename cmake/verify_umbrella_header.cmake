# umbrella header(`luil/luil.h`)가 공개 헤더를 하나도 빠뜨리지 않았는지 본다.
#
# "라이브러리 공개 API 전체를 한 번에 들인다"가 그 파일이 스스로 적어 둔 계약인데,
# 새 공개 헤더를 더하면서 이 줄을 잊는 것을 컴파일러는 잡지 못한다 — 저장소 안의
# test와 예제는 필요한 헤더를 직접 include하므로 아무 데서도 드러나지 않고,
# umbrella만 include한 소비자에게만 그 기능이 **없는 것**이 된다.
# 실제로 `ui/accessibility.h`와 `ui/image_element.h`가 그렇게 빠져 있었다.
#
# 목록을 여기에 다시 적지 않고 디렉터리에 묻는다. 목록이 두 벌이 되면 언젠가
# 어긋나고, 설치 규칙도 include 디렉터리를 통째로 넣으므로 그쪽이 원본이다.
if(NOT DEFINED INCLUDE_DIRECTORY)
    message(FATAL_ERROR "INCLUDE_DIRECTORY is required.")
endif()

set(umbrella_header "${INCLUDE_DIRECTORY}/luil/luil.h")
if(NOT EXISTS "${umbrella_header}")
    message(FATAL_ERROR "The umbrella header is missing: ${umbrella_header}")
endif()
file(READ "${umbrella_header}" umbrella_text)

file(GLOB_RECURSE public_headers RELATIVE "${INCLUDE_DIRECTORY}" "${INCLUDE_DIRECTORY}/luil/*.h")
set(missing_headers "")
foreach(header IN LISTS public_headers)
    if(header STREQUAL "luil/luil.h")
        continue()
    endif()
    string(FIND "${umbrella_text}" "#include \"${header}\"" include_position)
    if(include_position EQUAL -1)
        list(APPEND missing_headers "${header}")
    endif()
endforeach()

list(LENGTH missing_headers missing_count)
if(missing_count GREATER 0)
    string(REPLACE ";" "\n  " missing_text "${missing_headers}")
    message(FATAL_ERROR
        "The umbrella header does not include every public header.\n"
        "Add these to include/luil/luil.h:\n  ${missing_text}")
endif()

list(LENGTH public_headers header_count)
message(STATUS "Umbrella header check passed: ${header_count} public header(s)")

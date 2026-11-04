include_guard(GLOBAL)

# 문자열 표 원본 폴더를 C++ 헤더로 짓는 CMake 함수다 (docs/i18n-design.md). 설치본에도 실린다.
#  - 생성기(generate_strings.cmake)는 이 파일 곁에 있다. 소스 트리든 설치본이든 같다.

# 생성 명령 하나를 건다. `luil_add_string_table`과 라이브러리 표(src/CMakeLists.txt)가 함께 쓴다.
#  - 언어 파일은 구성할 때 폴더에서 모은다. 파일을 더하면 CONFIGURE_DEPENDS가 다시 구성한다.
#  - custom command는 건 디렉터리의 target만 소비할 수 있다.
function(luil_add_string_table_command)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "OUTPUT;NAMESPACE;DIRECTORY;BASE;REFERENCE_FILE" "")
    file(GLOB language_files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${arg_DIRECTORY}/*.json")
    set(generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_strings.cmake")
    set(reference_argument)
    set(reference_dependency)
    if(DEFINED arg_REFERENCE_FILE)
        set(reference_argument "-DREFERENCE_FILE=${arg_REFERENCE_FILE}")
        set(reference_dependency "${arg_REFERENCE_FILE}")
    endif()
    add_custom_command(
        OUTPUT "${arg_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}"
            "-DDIRECTORY=${arg_DIRECTORY}"
            "-DBASE=${arg_BASE}"
            "-DNAMESPACE=${arg_NAMESPACE}"
            "-DOUTPUT_FILE=${arg_OUTPUT}"
            ${reference_argument}
            -P "${generator}"
        DEPENDS
            ${language_files}
            ${reference_dependency}
            "${generator}"
        COMMENT "Generating the string table ${arg_NAMESPACE}"
        VERBATIM)
endfunction()

# 언어 파일 폴더를 target의 생성 헤더로 잇는다.
#   luil_add_string_table(my_app NAME app_strings DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/strings" BASE en)
#  - 생성 헤더는 `<target 빌드 폴더>/generated/<NAME>.h`이고 이름 공간은 NAME이다. target의 PRIVATE include에 더한다.
#  - target을 정의한 CMakeLists에서 부른다. 다른 디렉터리에서 부르면 custom command가 그 target에 걸리지 않는다.
function(luil_add_string_table target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "NAME;DIRECTORY;BASE" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "luil_add_string_table: unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required_argument IN ITEMS NAME DIRECTORY BASE)
        if(NOT DEFINED arg_${required_argument})
            message(FATAL_ERROR "luil_add_string_table: ${required_argument} is required.")
        endif()
    endforeach()
    if(NOT arg_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "luil_add_string_table: NAME is not a C++ identifier: ${arg_NAME}")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "luil_add_string_table: no such target: ${target}")
    endif()
    get_target_property(target_binary_directory "${target}" BINARY_DIR)
    if(NOT target_binary_directory STREQUAL CMAKE_CURRENT_BINARY_DIR)
        message(FATAL_ERROR "luil_add_string_table: call it from the CMakeLists that defines ${target}.")
    endif()

    get_filename_component(language_directory "${arg_DIRECTORY}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(generated_directory "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(generated_header "${generated_directory}/${arg_NAME}.h")
    luil_add_string_table_command(
        OUTPUT "${generated_header}"
        NAMESPACE "${arg_NAME}"
        DIRECTORY "${language_directory}"
        BASE "${arg_BASE}")
    # 헤더를 소스로 올려 두면 빌드 순서가 저절로 맞는다 (코디콘 매핑과 같다).
    target_sources("${target}" PRIVATE "${generated_header}")
    target_include_directories("${target}" PRIVATE "${generated_directory}")
endfunction()

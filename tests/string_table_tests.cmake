cmake_minimum_required(VERSION 4.2)

# 문자열 표 생성기가 막아야 할 원본과 받아야 할 원본이다 (docs/i18n-design.md의 생성기 검사).
#  - 막는 원본마다 틀린 자리를 알리는 글까지 본다. 다른 까닭으로 멈추면 이 test가 뜻을 잃는다.

set(generator "${SOURCE_DIR}/cmake/generate_strings.cmake")
set(work "${BINARY_DIR}/string-table-test")
file(REMOVE_RECURSE "${work}")

function(write_language case_name tag json)
    file(WRITE "${work}/${case_name}/${tag}.json" "${json}")
endfunction()

# 원본 폴더 하나로 생성기를 돌린다. 뒤 인자는 생성기에 더 줄 `-D` 인자다.
function(run_generator case_name base result_variable error_variable)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DDIRECTORY=${work}/${case_name}"
            "-DBASE=${base}"
            "-DNAMESPACE=sample_strings"
            "-DOUTPUT_FILE=${work}/${case_name}/out/sample_strings.h"
            ${ARGN}
            -P "${generator}"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_VARIABLE errors
    )
    set(${result_variable} "${result}" PARENT_SCOPE)
    set(${error_variable} "${errors}" PARENT_SCOPE)
endfunction()

function(expect_rejected case_name base expected_text)
    run_generator("${case_name}" "${base}" result errors ${ARGN})
    if(result EQUAL 0)
        message(FATAL_ERROR "The generator accepted ${case_name}.")
    endif()
    # 진단은 줄을 접어 찍힌다. 공백을 고르게 한 뒤 견준다.
    string(REGEX REPLACE "[ \t\r\n]+" " " folded_errors "${errors}")
    string(FIND "${folded_errors}" "${expected_text}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "The generator stopped ${case_name} for another reason.\nExpected: ${expected_text}\nGot: ${errors}")
    endif()
endfunction()

function(expect_accepted case_name base)
    run_generator("${case_name}" "${base}" result errors ${ARGN})
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "The generator rejected ${case_name}:\n${errors}")
    endif()
endfunction()

function(expect_header_has case_name expected_text)
    file(READ "${work}/${case_name}/out/sample_strings.h" header)
    string(FIND "${header}" "${expected_text}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "The header of ${case_name} does not have: ${expected_text}\n${header}")
    endif()
endfunction()

# 막는 원본: 언어 파일.
write_language(broken_json en [=[{ "language": "en", "strings": { "save": "Save" ]=])
expect_rejected(broken_json en "must be a JSON object")

write_language(not_object en [=[[ "save" ]]=])
expect_rejected(not_object en "must be a JSON object")

write_language(name_mismatch en [=[{ "language": "ko", "strings": { "save": "Save" } }]=])
expect_rejected(name_mismatch en "does not match the file name")

write_language(no_language en [=[{ "strings": { "save": "Save" } }]=])
expect_rejected(no_language en "has no language")

write_language(bad_tag english [=[{ "language": "english", "strings": { "save": "Save" } }]=])
expect_rejected(bad_tag english "is not a language tag")

write_language(unknown_plural en [=[{ "language": "en", "plural": "two", "strings": { "save": "Save" } }]=])
expect_rejected(unknown_plural en "unknown plural rule: two")

write_language(unknown_member en [=[{ "language": "en", "strings": { "save": "Save" }, "notes": "x" }]=])
expect_rejected(unknown_member en "unknown member")

write_language(no_strings en [=[{ "language": "en" }]=])
expect_rejected(no_strings en "must have a strings object")

write_language(empty_strings en [=[{ "language": "en", "strings": {} }]=])
expect_rejected(empty_strings en "has no strings")

write_language(no_base en [=[{ "language": "en", "strings": { "save": "Save" } }]=])
expect_rejected(no_base ko "base language file is missing")

# 막는 원본: 언어 사이의 키.
write_language(key_missing en [=[{ "language": "en", "strings": { "save": "Save", "open": "Open" } }]=])
write_language(key_missing ko [=[{ "language": "ko", "strings": { "save": "저장" } }]=])
expect_rejected(key_missing en "Language ko is missing key open")

write_language(key_extra en [=[{ "language": "en", "strings": { "save": "Save" } }]=])
write_language(key_extra ko [=[{ "language": "ko", "strings": { "save": "저장", "open": "열기" } }]=])
expect_rejected(key_extra en "Language ko has extra key open")

# 빠진 키와 남는 키를 한 번에 모두 알린다.
write_language(key_both en [=[{ "language": "en", "strings": { "save": "Save", "open": "Open" } }]=])
write_language(key_both ko [=[{ "language": "ko", "strings": { "save": "저장", "close": "닫기" } }]=])
expect_rejected(key_both en "Language ko is missing key open. Language ko has extra key close")

write_language(key_reference en [=[{ "language": "en", "strings": { "save": "Save" } }]=])
file(WRITE "${work}/key_reference.reference.json" [=[{ "language": "en", "strings": { "save": "Save", "open": "Open" } }]=])
expect_rejected(key_reference en "is missing key open of" "-DREFERENCE_FILE=${work}/key_reference.reference.json")

# 막는 원본: 키 이름.
write_language(reserved_key en [=[{ "language": "en", "strings": { "delete": "Delete" } }]=])
expect_rejected(reserved_key en "C++ reserved word as a key: delete")

write_language(bad_key en [=[{ "language": "en", "strings": { "Save-As": "Save as" } }]=])
expect_rejected(bad_key en "invalid key: Save-As")

# 막는 원본: 꼴.
write_language(form_differs en [=[{ "language": "en", "plural": "one", "strings": { "n": { "one": "{count} a", "other": "{count} b" } } }]=])
write_language(form_differs ko [=[{ "language": "ko", "strings": { "n": "{count}" } }]=])
expect_rejected(form_differs en "Language ko key n has a different form")

write_language(not_text_or_object en [=[{ "language": "en", "strings": { "n": 3 } }]=])
expect_rejected(not_text_or_object en "must be text or a plural category object")

write_language(category_missing en [=[{ "language": "en", "plural": "one", "strings": { "n": { "other": "{count} b" } } }]=])
expect_rejected(category_missing en "missing the plural category one")

write_language(category_extra en [=[{ "language": "en", "strings": { "n": { "one": "{count} a", "other": "{count} b" } } }]=])
expect_rejected(category_extra en "plural category one that its rule (other) does not use")

write_language(category_unknown en [=[{ "language": "en", "plural": "one", "strings": { "n": { "one": "{count} a", "few": "{count} c", "other": "{count} b" } } }]=])
expect_rejected(category_unknown en "plural category few")

write_language(category_not_text en [=[{ "language": "en", "strings": { "n": { "other": 3 } } }]=])
expect_rejected(category_not_text en "category other must be text")

write_language(no_count en [=[{ "language": "en", "plural": "one", "strings": { "n": { "one": "a", "other": "b" } } }]=])
expect_rejected(no_count en "does not take {count}")

# 막는 원본: 자리 값.
write_language(open_brace en [=[{ "language": "en", "strings": { "n": "a {name" } }]=])
expect_rejected(open_brace en "unpaired brace or an invalid placeholder name: {")

write_language(close_brace en [=[{ "language": "en", "strings": { "n": "a name}" } }]=])
expect_rejected(close_brace en "unpaired brace or an invalid placeholder name: }")

write_language(nested_brace en [=[{ "language": "en", "strings": { "n": "{a{b}}" } }]=])
expect_rejected(nested_brace en "unpaired brace or an invalid placeholder name")

write_language(bad_name en [=[{ "language": "en", "strings": { "n": "{a b}" } }]=])
expect_rejected(bad_name en "invalid placeholder name: {a b}")

write_language(empty_name en [=[{ "language": "en", "strings": { "n": "{}" } }]=])
expect_rejected(empty_name en "invalid placeholder name: {}")

# `:`는 마크업 후속의 접두 자리라 이름 꼴 밖이다.
write_language(colon_name en [=[{ "language": "en", "strings": { "n": "{t:save}" } }]=])
expect_rejected(colon_name en "invalid placeholder name: {t:save}")

write_language(undeclared_name en [=[{ "language": "en", "strings": { "n": "{name}" } }]=])
write_language(undeclared_name ko [=[{ "language": "ko", "strings": { "n": "{name} {other}" } }]=])
expect_rejected(undeclared_name en "Language ko key n uses the undeclared placeholder {other}")

write_language(declared_not_listed en [=[{ "language": "en", "arguments": { "n": [ "name" ] }, "strings": { "n": "{name} {other}" } }]=])
expect_rejected(declared_not_listed en "Language en key n uses the undeclared placeholder {other}")

write_language(arguments_not_base en [=[{ "language": "en", "strings": { "n": "{name}" } }]=])
write_language(arguments_not_base ko [=[{ "language": "ko", "arguments": { "n": [ "name" ] }, "strings": { "n": "{name}" } }]=])
expect_rejected(arguments_not_base en "Only the base language file may declare arguments")

write_language(arguments_unknown_key en [=[{ "language": "en", "arguments": { "m": [ "name" ] }, "strings": { "n": "x" } }]=])
expect_rejected(arguments_unknown_key en "declares arguments for an unknown key: m")

write_language(arguments_bad_name en [=[{ "language": "en", "arguments": { "n": [ "a b" ] }, "strings": { "n": "x" } }]=])
expect_rejected(arguments_bad_name en "have an invalid name: a b")

# 받는 원본: 글자 표기와 값.
#  - JSON 안의 이스케이프를 지나 C++ 글이 되는 자리라 bracket 인자로 원문 그대로 쓴다.
write_language(escapes en [=[{ "language": "en", "strings": { "quote": "Say \"hi\"", "slash": "C:\\path\\u0041", "lines": "one\ntwo\tthree", "semicolon": "a;b;[c]", "braces": "{{literal}}" } }]=])
expect_accepted(escapes en)
expect_header_has(escapes [=[u8"Say \"hi\""]=])
expect_header_has(escapes [=[u8"C:\\path\\u0041"]=])
expect_header_has(escapes [=[u8"one\ntwo\tthree"]=])
expect_header_has(escapes [=[u8"a;b;[c]"]=])
expect_header_has(escapes [=[u8"{{literal}}"]=])

# 받는 원본: 차례. 언어는 기본 언어 다음에 태그 글자 차례이고 키는 이름 차례다. 파일에 쓴 차례를 따르지 않는다.
set(ordered_en [=[{ "language": "en", "plural": "one", "strings": { "save": "Save", "date": "{month_name} {day}", "files": { "one": "a file", "other": "{count} files" } } }]=])
set(ordered_ko [=[{ "language": "ko", "arguments": { "date": [ "day", "month_name" ] }, "strings": { "files": { "other": "{count}개" }, "date": "{day}일", "save": "저장" } }]=])
set(ordered_fr [=[{ "language": "fr", "plural": "one", "strings": { "date": "{day}", "files": { "one": "{count} f", "other": "{count} fs" }, "save": "S" } }]=])
write_language(ordered en "${ordered_en}")
write_language(ordered ko "${ordered_ko}")
write_language(ordered fr "${ordered_fr}")
expect_accepted(ordered ko)
file(READ "${work}/ordered/out/sample_strings.h" first_header)
string(FIND "${first_header}" [=[{ u8"ko", ::luil::plural_rule::other, messages_0 }]=] ko_position)
string(FIND "${first_header}" [=[{ u8"en", ::luil::plural_rule::one, messages_1 }]=] en_position)
string(FIND "${first_header}" [=[{ u8"fr", ::luil::plural_rule::one, messages_2 }]=] fr_position)
if(ko_position EQUAL -1 OR en_position EQUAL -1 OR fr_position EQUAL -1)
    message(FATAL_ERROR "The languages are not in the base-then-tag order:\n${first_header}")
endif()
string(FIND "${first_header}" "        date,\n        files,\n        save,\n" key_position)
if(key_position EQUAL -1)
    message(FATAL_ERROR "The keys are not in name order:\n${first_header}")
endif()
expect_header_has(ordered "namespace sample_strings {")
# 범주는 칸 차례(zero, one, two, few, many, other)에 든다.
expect_header_has(ordered [=[{ { { {}, u8"a file", {}, {}, {}, u8"{count} files" } } }]=])

# 받는 원본: 같은 원본이면 같은 헤더다. 파일을 다른 차례로 써도 그렇다.
write_language(ordered_again fr "${ordered_fr}")
write_language(ordered_again ko "${ordered_ko}")
write_language(ordered_again en "${ordered_en}")
expect_accepted(ordered_again ko)
file(READ "${work}/ordered_again/out/sample_strings.h" second_header)
if(NOT first_header STREQUAL second_header)
    message(FATAL_ERROR "The same source made a different header.")
endif()

# 받는 원본: 이름 공간 이중 표기와 같은 키 집합의 참조 파일.
write_language(reference_same en [=[{ "language": "en", "strings": { "save": "Save" } }]=])
file(WRITE "${work}/reference_same.reference.json" [=[{ "language": "en", "strings": { "save": "Other text" } }]=])
expect_accepted(reference_same en "-DREFERENCE_FILE=${work}/reference_same.reference.json" "-DNAMESPACE=luil::library_text")
expect_header_has(reference_same "namespace luil::library_text {")

# 저장소의 라이브러리 표도 이 생성기를 지난다.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DDIRECTORY=${SOURCE_DIR}/assets/strings"
        "-DBASE=ko"
        "-DNAMESPACE=luil::library_text"
        "-DOUTPUT_FILE=${work}/library/library_text.h"
        "-DREFERENCE_FILE=${SOURCE_DIR}/assets/strings/ko.json"
        -P "${generator}"
    RESULT_VARIABLE result
    ERROR_VARIABLE errors
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The library string table was rejected:\n${errors}")
endif()

file(REMOVE_RECURSE "${work}")

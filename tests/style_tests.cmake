cmake_minimum_required(VERSION 4.2)

# 스타일 생성 script의 계약이다 — 받아야 할 파일은 받고, 어긋난 파일은 막는다.
# 생성물이 `ui_style`의 지정 초기화라, 여기서 새는 것은 컴파일 오류나 조용한 기본값이 된다.
set(generator "${SOURCE_DIR}/cmake/generate_style.cmake")
file(MAKE_DIRECTORY "${BINARY_DIR}/style-test")
set(input "${BINARY_DIR}/style-test/style.json")
set(output "${BINARY_DIR}/style-test/ui_style.h")

function(luil_expect_generation input_text should_succeed what)
    file(WRITE "${input}" "${input_text}")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${input}" "-DOUTPUT_FILE=${output}"
        -P "${generator}" RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
    if(should_succeed AND NOT result EQUAL 0)
        message(FATAL_ERROR "Generation failed but should succeed: ${what}")
    endif()
    if(NOT should_succeed AND result EQUAL 0)
        message(FATAL_ERROR "Generation succeeded but should fail: ${what}")
    endif()
endfunction()

# 내장 파일은 그대로 지나가고 생성물에 지정 초기화가 선다.
file(READ "${SOURCE_DIR}/assets/style.json" style)
luil_expect_generation("${style}" TRUE "the built-in style")
file(READ "${output}" header)
foreach(needle
    "inline constexpr ui_style default_style"
    ".window_background = 0xFF1E1E1Eu"
    ".secondary_foreground = 0xBFFFFFFFu"
    ".close_button_hover_background = 0xFFC42B1Cu"
    ".body_font_size = 12.0f"
    ".soft_button = 0.25f")
    string(FIND "${header}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The generated header lacks: ${needle}")
    endif()
endforeach()

# 소비자가 바꿀 자리 — 색은 알파를 받고 숫자는 정수·실수 둘 다 받는다.
string(JSON custom SET "${style}" dark windowBackground "\"#1a1b26\"")
string(JSON custom SET "${custom}" light divider "\"#10203080\"")
string(JSON custom SET "${custom}" metrics controlCornerRadius "6.5")
string(JSON custom SET "${custom}" tones pressed "1")
luil_expect_generation("${custom}" TRUE "a customized style")
file(READ "${output}" header)
foreach(needle
    ".window_background = 0xFF1A1B26u"
    ".divider = 0x80102030u"
    ".control_corner_radius = 6.5f"
    ".pressed = 1.0f")
    string(FIND "${header}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The customized header lacks: ${needle}")
    endif()
endforeach()

# 어긋난 파일은 막는다.
luil_expect_generation("[]" FALSE "a non-object root")
luil_expect_generation("{}" FALSE "an empty object")
string(JSON broken SET "${style}" dark windowBackground "\"#12345\"")
luil_expect_generation("${broken}" FALSE "a short color")
string(JSON broken SET "${style}" dark windowBackground "\"1e1e1e\"")
luil_expect_generation("${broken}" FALSE "a color without the hash")
string(JSON broken SET "${style}" dark windowBackgrund "\"#1e1e1e\"")
luil_expect_generation("${broken}" FALSE "an unknown key (typo)")
string(JSON broken REMOVE "${style}" dark noticeBackground)
luil_expect_generation("${broken}" FALSE "a missing neutral key")
string(JSON broken REMOVE "${style}" light caption closeButtonHoverForeground)
luil_expect_generation("${broken}" FALSE "a missing caption key")
string(JSON broken SET "${style}" tones pressed "1.5")
luil_expect_generation("${broken}" FALSE "a tone above 1")
string(JSON broken SET "${style}" tones pressed "\"0.5\"")
luil_expect_generation("${broken}" FALSE "a tone given as a string")
string(JSON broken SET "${style}" metrics bodyFontSize "-1")
luil_expect_generation("${broken}" FALSE "a negative metric")
string(JSON broken REMOVE "${style}" metrics)
luil_expect_generation("${broken}" FALSE "a missing metrics object")
string(JSON broken SET "${style}" extra "{}")
luil_expect_generation("${broken}" FALSE "an unknown root key")

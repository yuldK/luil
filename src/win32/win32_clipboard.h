#pragma once

#include "win32/utf8.h"

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace luil::win32 {
    // 클립보드를 잡아 보는 횟수와 사이의 대기다.
    // 클립보드는 **한 번에 한 프로세스만** 연다. 다른 앱이 잡고 있는 순간에
    // 부딪히면 `OpenClipboard`가 실패하는데, 그 실패는 화면에 아무것도 남기지
    // 않는다 — 복사가 조용히 안 되는 것으로만 보인다. 그래서 몇 번 물러났다
    // 다시 잡는다 (Win32가 권하는 방식이다).
    //  - 무한히 기다리지 않는다. 클립보드를 놓지 않는 앱이 있으면 UI thread가
    //    그만큼 멈추므로, 실패는 실패로 끝내고 앱 상태는 그대로 둔다.
    inline constexpr int clipboard_open_attempts { 5 };
    inline constexpr int clipboard_open_retry_ms { 10 };

    // 한 번에 읽을 클립보드 텍스트의 상한이다 (UTF-16 코드 단위 수).
    // 블록 크기는 다른 프로세스가 정한다 — 상한이 없으면 붙여넣기 한 번이 변환
    // 중간 버퍼로 그 몇 배를 UI thread에서 잡는다. 소비자(한 줄 칸)가 첫 줄만
    // 쓰므로 `max_dropped_paths`처럼 앞에서부터 이만큼만 읽고 나머지는 버린다.
    // 가장 긴 합법 경로(약 32K자)도 여유 있게 들어오는 크기다.
    inline constexpr std::size_t max_clipboard_text_chars { 64u * 1024u };

    // UTF-8 텍스트를 Win32 클립보드에 넣는다.
    // UI thread 전용이며 실패해도 앱 상태를 바꾸지 않으므로 성공 여부만 돌려준다.
    [[nodiscard]] bool copy_text_to_clipboard(HWND owner, std::u8string_view text) noexcept;

    // 한 줄 입력 칸에 넣을 값으로 다듬는다 — 첫 줄만 남긴다.
    // 탭도 줄 구분과 같이 본다 (칸 사이를 옮기는 글자라 한 줄 안에 있을 것이 아니다).
    //  - 순수 계산이라 test가 직접 확인한다.
    [[nodiscard]] std::u8string first_clipboard_line(std::u8string text);

    // `CF_UNICODETEXT` 블록을 UTF-8로 읽는다. `bytes`는 `GlobalSize`가 준 블록 크기다.
    // 다른 프로세스가 만든 블록은 NUL 종단이 보장되지 않는다 — wchar_t 단위로
    // 내림한 범위 안에서 첫 NUL까지, NUL이 없으면 범위 전체를 읽고 경계 밖은
    // 읽지 않는다. 홀수 꼬리 바이트는 글자가 못 되니 버린다.
    // `max_clipboard_text_chars`를 넘으면 앞에서부터 그만큼만 남긴다.
    //  - 자른 자리가 surrogate 쌍의 가운데면 앞쪽 반쪽도 버린다 — 반쪽이 남으면
    //    변환이 블록 전체를 거절해 붙여넣기가 통째로 빈 값이 된다.
    //  - 순수 계산이라 test가 직접 확인한다.
    [[nodiscard]] utf_conversion_result<std::u8string> text_from_utf16_block(const void* data, std::size_t bytes) noexcept;

    // 클립보드의 UTF-8 텍스트다.
    // 텍스트가 없거나 읽지 못하면 빈 값이고, 한 줄 칸에 넣을 값이라 첫 줄만 돌려준다.
    //
    // **글이 없으면 파일 목록(`CF_HDROP`)을 본다.** 탐색기에서 복사한 파일을 칸에
    // 붙여넣으면 그 **경로**가 들어오는 것이 관례이고, 그러지 않으면 붙여넣기가
    // 조용히 아무 일도 하지 않는다.
    //  - 여러 개를 복사했어도 **첫 경로 하나**다. 한 줄 칸이라 나머지를 담을 자리가
    //    없고, 여럿을 어떻게 잇는지(줄바꿈·쉼표·따옴표)는 앱마다 다른 정책이다.
    [[nodiscard]] std::u8string read_text_from_clipboard(HWND owner) noexcept;
} // namespace luil::win32

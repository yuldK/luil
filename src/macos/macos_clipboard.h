#pragma once

#include "luil/ui/ui_events.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// 일반 붙여넣기 판(NSPasteboard)의 파일·글·HTML·그림이다 (file-clipboard-design.md, image-clipboard-design.md의 macOS 갈래).
//  - 모두 UI thread(메인 thread)에서 부른다.
//  - 클립보드 번호는 붙여넣기 판의 `changeCount`다 (Win32 `GetClipboardSequenceNumber`의 자리). 32비트로 자른다.
//  - 파일은 파일 URL 목록이다. 잘라내기의 뜻은 이 라이브러리의 형식(`file_intent_type`)에 "cut"으로 싣는다 — Finder에는 잘라내기
//    클립보드가 없어 Finder는 늘 복사로 붙인다 (옮기기는 Finder의 Command+Option+V다).
//  - 붙여넣기 판은 없는 파일의 URL을 다른 프로세스에 건네지 않는다. 디스크에 있는 경로만 실린다.
//  - 그림은 PNG(앱이 지어 줬으면 그것, 아니면 픽셀을 ImageIO로 지은 것)와 TIFF를 함께 싣는다. 읽기는 PNG를 먼저, 없으면 TIFF를
//    PNG로 옮겨 `encoded`로 준다 (Skia 코덱에는 TIFF가 없다).
namespace luil::macos {
    // 잘라내기·복사의 뜻을 싣는 붙여넣기 판 형식이다.
    inline constexpr char file_intent_type[] { "org.luil.file-clipboard-intent" };

    // 지금의 클립보드 번호다.
    [[nodiscard]] std::uint32_t clipboard_sequence();

    // 파일 목록을 넣는다. 하나라도 절대 경로가 아니면 아무것도 넣지 않는다. 빈 목록은 클립보드를 비운다.
    //  - `expected_sequence`가 있고 지금 번호와 다르면 아무것도 하지 않는다.
    //  - 넣었으면 참이다.
    [[nodiscard]] bool write_clipboard_files(const clipboard_files_copy_request& request);

    // 파일 목록을 읽는다. 파일이 없으면 빈 목록이다.
    [[nodiscard]] file_clipboard_content read_clipboard_files(std::uint32_t tag);

    // 글·HTML·그림을 한 클립에 넣는다. 모두 비면 아무 일도 없이 거짓이다. 그림을 짓지 못하면 아무것도 넣지 않는다.
    [[nodiscard]] bool write_clipboard_data(const clipboard_data_copy_request& request, std::u8string& error);

    // 글·HTML·그림과 번호를 함께 읽는다.
    [[nodiscard]] clipboard_data read_clipboard_data(std::uint32_t tag);

    // 지금 클립보드에 파일·그림이 있는가.
    [[nodiscard]] bool clipboard_has_files();
    [[nodiscard]] bool clipboard_has_image();
}

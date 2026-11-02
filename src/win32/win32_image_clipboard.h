#pragma once

#include "luil/ui/ui_events.h"
#include "luil/ui/ui_image.h"
#include "win32/win32_clipboard.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 그림 클립보드다 (image-clipboard-design.md). 넣기는 "PNG"(앱이 지어 줬을 때), `CF_DIBV5`, `CF_UNICODETEXT`를 한 클립에
// 싣고, 읽기는 한 번 열어 글과 그림과 번호를 함께 읽는다. 창 계층이라 인코더·디코더를 부르지 않는다 — 픽셀만 읽는다.
namespace luil::win32 {
    // 그림 파일 바이트를 싣는 등록 형식의 이름이다. 브라우저·Office·그림 편집기가 같은 이름으로 PNG 파일 내용을 싣는다.
    inline constexpr wchar_t png_clipboard_format[] { L"PNG" };

    // 앞 8바이트가 PNG 서명인가. 아니면 앱이 준 PNG를 버린다 — 받는 앱이 "PNG"를 믿고 풀다 실패한다.
    [[nodiscard]] bool png_signature(std::span<const std::uint8_t> bytes) noexcept;

    // 그림을 `CF_DIBV5` 블록으로 짓는다 (`BITMAPV5HEADER` 124바이트 + 픽셀).
    //  - 32비트 `BI_BITFIELDS`, 마스크 R `0x00FF0000`·G `0x0000FF00`·B `0x000000FF`·A `0xFF000000`, `LCS_sRGB`다.
    //  - 높이는 양수(아래에서 위로)다. 위에서 아래로 쌓은 DIB를 잘못 읽는 앱이 있어서다.
    //  - 색은 알파를 곱하지 않은 값이다 (아이콘 변환과 같은 규칙, icon_from_image.cpp).
    //  - SVG 그림은 자기 크기로 한 번 편다 (`read_image_pixels`). 실패하면 nullopt와 `error`다.
    [[nodiscard]] std::optional<std::vector<std::byte>> dibv5_block(const ui_image& image, std::u8string& error);

    // 요청을 넣을 블록 목록으로 짓는다. 순서는 "PNG", `CF_DIBV5`, `CF_UNICODETEXT`다 — `EnumClipboardFormats`가 이 순서로
    // 내므로 받는 앱이 그림을 먼저 본다.
    //  - 빈 칸은 넣지 않는다. `png`는 `image`가 있고 PNG 서명일 때만 넣는다. `CF_DIB`·`CF_BITMAP`은 OS가 `CF_DIBV5`에서 짓는다.
    //  - 그림이나 글의 블록을 짓지 못하면 nullopt와 `error`다 (반쪽만 넣지 않는다).
    [[nodiscard]] std::optional<std::vector<clipboard_block>> clipboard_data_blocks(const clipboard_data_copy_request& request, std::u8string& error);

    // 요청의 블록을 짓고 한 번 열어 모두 넣는다 (`put_clipboard_blocks`). 모두 비면 아무 일도 없이 거짓이다.
    // UI thread 전용이며 성공 여부만 돌려준다.
    [[nodiscard]] bool copy_data_to_clipboard(HWND owner, const clipboard_data_copy_request& request) noexcept;

    // 한 번 열어 글과 그림과 번호를 함께 읽는다. 번호는 연 채 읽어 내용과 짝이 맞는다.
    //  - 글은 `CF_UNICODETEXT`만 보고 `max_clipboard_text_chars`까지다. 파일 경로로 대신하지 않는다.
    //  - 그림은 "PNG", `CF_DIBV5`, `CF_DIB` 순으로 처음 있는 것 하나다. "PNG"는 `encoded`, 나머지는 `dib`다.
    //  - 블록은 `GlobalSize` 안에서만 읽고, `max_clipboard_image_bytes`(256 MiB)를 넘으면 복사하지 않고 `image_too_large`다.
    //  - 열지 못하면 빈 답이다.
    [[nodiscard]] clipboard_data read_clipboard_data(HWND owner) noexcept;

    // 지금 클립보드에 그림이 있는가 (`clipboard_change::has_image`). 등록 형식 "PNG"나 `CF_DIB`가 있으면 참이다.
    // `CF_BITMAP`만 넣은 앱도 OS가 `CF_DIB`를 지어 주므로 잡힌다. 클립보드를 열지 않는다.
    [[nodiscard]] bool clipboard_has_image() noexcept;
}

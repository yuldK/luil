#pragma once

#include "luil/ui/ui_events.h"

#include <cstdint>
#include <string>

// 일반 붙여넣기 판(UIPasteboard)의 글·HTML·그림이다 (image-clipboard-design.md의 iOS 갈래). macos/macos_clipboard.h와 같은 모양이다.
//  - 모두 UI thread(메인 thread)에서 부른다.
//  - 클립보드 번호는 붙여넣기 판의 `changeCount`다. 32비트로 자른다.
//  - 파일 목록 클립보드는 없다. iOS 앱 사이에는 파일 경로가 통하지 않는다 (샌드박스). 읽기는 "지원 안 함"이다.
//  - iOS 14부터 다른 앱이 넣은 것을 읽으면 시스템이 "붙여넣음" 알림을 띄운다. 그래서 읽기는 앱이 붙여 넣을 때만 하고,
//    바뀜 알림은 내용을 읽지 않는 질문(`hasStrings`·`hasImages`, 형식 목록)으로만 짓는다.
//  - 그림은 PNG(앱이 지어 줬으면 그것, 아니면 픽셀을 ImageIO로 지은 것)로 싣는다. 읽기는 PNG를 먼저, 없으면 다른 그림 형식
//    (JPEG·HEIC 등)을 ImageIO로 PNG로 옮겨 `encoded`로 준다.
namespace luil::ios {
    // 지금의 클립보드 번호다.
    [[nodiscard]] std::uint32_t clipboard_sequence();

    // 글 하나를 넣는다. 빈 글은 클립보드를 비운다.
    void write_clipboard_text(const std::u8string& text);

    // 붙여넣기 판의 평문이다. 없으면 비었다.
    [[nodiscard]] std::u8string read_clipboard_text();

    // 글·HTML·그림을 한 항목에 넣는다. 모두 비면 아무 일도 없이 거짓이다. 그림을 짓지 못하면 아무것도 넣지 않는다.
    [[nodiscard]] bool write_clipboard_data(const clipboard_data_copy_request& request, std::u8string& error);

    // 글·HTML·그림과 번호를 함께 읽는다.
    [[nodiscard]] clipboard_data read_clipboard_data(std::uint32_t tag);

    // 지금 클립보드에 그림이 있는가. 내용을 읽지 않는다 (붙여넣음 알림이 뜨지 않는다).
    [[nodiscard]] bool clipboard_has_image();
}

#pragma once

#include "luil/ui/image_decode.h"
#include "luil/ui/ui_events.h"

#include <cstddef>

namespace luil {
    // 클립보드에서 그림 하나로 읽는 바이트의 상한이다 (256 MiB, image-clipboard-design.md 결정 9).
    // 넘으면 플랫폼이 복사하지 않고 `clipboard_data::image_too_large`로 답한다.
    //  - 디코더의 인코딩 바이트 상한과 같은 값이다 (`image_decode_error_kind::input_too_large`). 넘긴 바이트는 디코더도 받는다.
    //  - 앱이 바꾸는 손잡이가 아니다. 올려도 디코더가 같은 자리에서 거절한다.
    inline constexpr std::size_t max_clipboard_image_bytes { 256u * 1024u * 1024u };

    // 클립보드의 그림 바이트를 `ui_image`로 푼다 (`clipboard_data_paste_request`의 답).
    // 상한·오류·thread 규칙은 `decode_image_bytes`와 같다. 큰 그림은 수십 ms가 걸리므로 logic thread나 worker에서 부른다.
    //  - `encoded`는 `decode_image_bytes`에 그대로 넘긴다.
    //  - `dib`는 머리(`BITMAPINFOHEADER`·V4·V5)를 읽어 24·32비트 `BI_RGB`와 32비트 `BI_BITFIELDS`를 코덱 없이 RGBA로 옮긴다.
    //    팔레트·RLE·16비트 같은 나머지는 BMP 파일 머리를 붙여 BMP 코덱에 넘긴다. 머리가 말하는 크기가 블록보다 크면
    //    `incomplete_input`이고, 픽셀 상한(`max_source_pixels`)은 메모리를 잡기 전에 본다. `max_width`·`max_height`는
    //    다 편 뒤 `downscale_image`로 맞춘다.
    //  - 알파: 알파 자리가 있으면 곱하지 않은 값으로 읽되, 모든 픽셀의 알파가 0이면 불투명으로 본다. 그 바이트를 비워 두는
    //    앱이 흔해 그대로 읽으면 그림이 사라진다.
    //  - 그림이 없으면(`none`이나 빈 바이트) 빈 그림과 `file_empty`, `image_too_large`면 `input_too_large`다.
    //  - 성공하면 `error`를 건드리지 않는다 (`decode_image_bytes`와 같은 규약).
    [[nodiscard]] ui_image decode_clipboard_image(const clipboard_data& data, const image_decode_options& options, image_decode_error& error);
}

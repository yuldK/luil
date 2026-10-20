#pragma once

#include "luil/ui/image_element.h"

#include <cstdint>
#include <string>
#include <vector>

namespace luil {
    // `ui_image`를 파일 바이트로 짓는다 (docs/image-encode-design.md).
    //
    // 형식마다 함수가 따로다. **앱이 부른 형식의 인코더만 실행 파일에 들어온다** — 형식을
    // 값으로 받는 함수 하나로 두면 그 함수가 세 인코더를 모두 참조해, PNG만 쓰는 앱도
    // JPEG·WebP 인코더를 싣는다 (Release 실행 파일 기준 PNG 약 0.13 MB, JPEG 약 0.27 MB).
    //
    // 공통 규칙이다.
    //  - 실패는 예외가 아니라 값이다. 빈 바이트와 `error`(개발자가 읽을 진단 영문)를
    //    돌려준다. 성공하면 `error`를 건드리지 않는다 (`load_image_file`과 같은 규약).
    //  - 입력은 straight alpha RGBA의 `ui_image`다. **SVG에서 온 벡터 그림은 그 크기
    //    (`width`·`height`)로 한 번 펴서** 인코딩한다 — 더 큰 픽셀이 필요하면 상한을 크게
    //    주어 다시 읽거나(`decode_svg_bytes`), 원하는 크기의 칸에 그린다.
    //  - 색은 sRGB로 적는다.
    //  - 동기 CPU 작업이다. 큰 그림은 수십 ms가 걸리므로 logic thread나 worker에서 부른다.
    //    창·GPU·COM을 요구하지 않는다.

    // 무손실 PNG다 (Rust PNG 인코더 — 디코더와 같은 갈래다). 알파를 그대로 담는다.
    [[nodiscard]] std::vector<std::uint8_t> encode_png(const ui_image& image, std::u8string& error);

    // JPEG다 (libjpeg-turbo). `quality`는 0–100이고 범위 밖은 끝으로 자른다.
    //  - **알파가 없는 형식이다.** 투명한 자리는 검정 위에 합성된다. 흰 바탕이 필요하면
    //    그 바탕에 그린 그림을 넘긴다.
    //  - 손실 형식이라 글자 가장자리가 뭉개진다. 화면을 다시 볼 값에는 PNG가 맞다.
    [[nodiscard]] std::vector<std::uint8_t> encode_jpeg(const ui_image& image, int quality, std::u8string& error);

    // 손실 WebP다. `quality`는 0–100이고 범위 밖은 끝으로 자른다. 알파를 담는다.
    [[nodiscard]] std::vector<std::uint8_t> encode_webp(const ui_image& image, int quality, std::u8string& error);

    // 무손실 WebP다. 같은 그림을 PNG보다 대개 작게 담는다 (화면 캡처가 이 형식이다).
    [[nodiscard]] std::vector<std::uint8_t> encode_webp_lossless(const ui_image& image, std::u8string& error);
} // namespace luil

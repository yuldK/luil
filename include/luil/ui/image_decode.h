#pragma once

#include "luil/ui/image_element.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace luil {
    // 디코드 시점에 이미지를 줄일 상한이다 (이미지 자체의 픽셀, 화면 배율과 무관).
    //
    // 4000×3000 사진을 200px 칸에 놓는 것이 흔한 쓰임이다. 원본 그대로 들고 있으면
    // 48 MB이고 mipmap도 그만큼 만들어진다 — 상한을 주면 줄여서 내주므로 메모리와
    // mipmap 부담이 함께 준다.
    //  - 움직이는 그림에서는 그 부담에 장 수가 곱해진다. 상한 하나가 200장짜리
    //    gif에서 200번 값을 낸다.
    //  - **상한은 EXIF로 돌려세운 뒤의 축에 건다.** 앱이 "200px 칸"이라고 할 때
    //    뜻하는 것은 화면에 놓일 크기이지 파일에 적힌 방향이 아니다. 돌리기 전에
    //    걸면 세로로 찍은 사진만 상한이 반대 축에 걸려 엉뚱하게 줄어든다 —
    //    그림은 옳게 뜨므로 눈으로 잘 걸리지 않는다.
    struct image_decode_options
    {
        // 0 이하면 그 축에 상한이 없다. 원본이 이미 상한 안이면 늘리지 않는다
        // (확대는 그릴 때의 일이다 — `image_fit`).
        int max_width { 0 };
        int max_height { 0 };
    };

    struct image_pixel_size
    {
        int width { 0 };
        int height { 0 };

        [[nodiscard]] bool operator==(const image_pixel_size&) const noexcept = default;
    };

    // 상한을 지키며 비율을 유지한 크기다. 줄일 필요가 없으면 원본 그대로다.
    //
    // 두 축 모두 상한이 있으면 더 빡빡한 쪽이 이긴다 (`image_fit::contain`과 같은
    // 규칙이라 담는 쪽이 예측할 수 있다). **원본이 양수인 한** 아주 납작한 그림을
    // 크게 줄여도 어느 변도 0이 되지 않는다 — 0 크기는 빈 이미지가 되어 축소를
    // 청한 대가로 그림이 사라진다. 원본의 어느 변이 0 이하면 상한을 보지 않고
    // `{0, 0}`이다 (`image_destination`이 빈 사각형을 답하는 것과 같은 자리다).
    //  - 판정이 디코딩 안에 숨으면 test가 닿지 못해 여기로 뗀다
    //    (raster-test-design.md가 그리기에 세운 규칙과 같다).
    //  - **넣는 것은 EXIF로 돌려세운 뒤의 크기다.** 이 함수는 방향을 모른다 —
    //    변을 바꿔 넣는 것은 부르는 쪽의 몫이고, 디코딩이 그렇게 부른다.
    [[nodiscard]] image_pixel_size decoded_image_size(int width, int height, const image_decode_options& options) noexcept;

    // 파일을 읽어 ui_image로 만든다. 코덱 지원은 Skia 빌드로 고정된다.
    // PNG·JPEG·WebP·GIF와 Skia 기본 코덱인 BMP·WBMP를 읽는다.
    // Rust PNG 코덱 구성에서는 APNG를 지원하며 ICO·TIFF·DDS·HEIF는 지원하지 않는다.
    // OS에 추가한 코덱은 지원 형식에 영향을 주지 않는다 (skia-build.md).
    //
    // 실패하면 빈 이미지와 error를 반환하며 성공하면 error를 변경하지 않는다.
    // GIF·WebP·APNG도 첫 장만 읽는다. 애니메이션은 load_animated_image_file을 사용한다.
    // 첫 장은 합성이 끝난 논리 화면 크기로 반환한다.
    // EXIF orientation을 적용하고, 축이 바뀌는 방향에서는 width·height도 교환한다.
    // 원본이 64 M 픽셀을 넘으면 디코딩 전에 거절한다. 축소 옵션을 지정해도
    // 코덱이 원본 전체를 펼칠 수 있으므로 이 상한은 원본 크기에 적용한다.
    //
    // 파일 I/O와 디코딩은 호출 스레드를 막으므로 logic 또는 worker 스레드에서 사용한다.
    // 결과는 불변 이미지로 공유하며 frame마다 다시 디코딩하지 않는다.
    // CPU 코덱은 창·GPU·COM apartment를 요구하지 않는다.
    [[nodiscard]] ui_image load_image_file(std::u8string_view path, std::u8string& error);
    [[nodiscard]] ui_image load_image_file(std::u8string_view path, const image_decode_options& options, std::u8string& error);

    // 인코딩된 이미지 바이트를 `ui_image`로 만든다 (실행 파일 resource에 박은
    // 그림, 네트워크에서 받은 바이트처럼 **경로가 없는 그림**의 자리다).
    //
    // 받는 것은 파일 내용 그대로이지 원시 픽셀이 아니다 — 원시 RGBA는
    // `make_rgba_image`가 받는다. 계약·상한·thread 규칙은 `load_image_file`과
    // 같다. 바이트는 이 호출이 끝날 때까지만 살아 있으면 된다 (안에서 복사하지
    // 않는다).
    [[nodiscard]] ui_image decode_image_bytes(std::span<const std::uint8_t> bytes, std::u8string& error);
    [[nodiscard]] ui_image decode_image_bytes(std::span<const std::uint8_t> bytes, const image_decode_options& options, std::u8string& error);

    // 이미지 파일을 읽어 `ui_animated_image`로 만든다 (움직이는 gif·webp·APNG).
    //
    // **움직이지 않는 파일도 성공한다** — 한 장짜리 값이 나오고 `animated()`가
    // 거짓이다. 위의 둘과 짝이 되는 규칙이다: 정지 쪽 문은 움직이는 것을 내놓아
    // 놀래지 않고, 움직이는 쪽 문은 파일이 움직이지 않는다고 실패하지 않는다.
    // 그래서 **무엇이 올지 모르는 파일을 여는 앱은 이 문 하나만 쓰면 되고**,
    // element를 갈라 쥘 일도 없다 (`image_config`가 그 짝이다).
    //  - **APNG도 움직인다.** png 코덱을 rust로 못 박았기 때문이다
    //    (`LUIL_ANIMATED_PNG`은 늘 1이다). 치른 값은 ico다 — 위를 본다.
    //  - **장 하나가 실패하면 통째로 거절한다.** 절반만 돌려주면 화면에서
    //    "원래 그런 그림"과 구별되지 않는다. 잘린 gif라도 첫 장은 필요한 앱은
    //    `load_image_file`로 그 한 장을 받는다 — 고르는 것은 앱이다.
    //  - **장 하나하나가 이미 합성이 끝난 논리 화면 크기의 그림이다.** 형식이
    //    나르는 차이(고치는 사각형·지우기·겹치기)는 여기서 다 풀린다.
    //  - **움직이는 그림은 코덱에게 축소를 청하지 않는다.** Skia 자신이 "원본보다
    //    작은 비트맵으로 읽으면 애니메이션 webp가 옳지 않을 수 있다"고 적어 두었다
    //    (`SkCodec.h`의 `getPixels` 주석). 합성은 원본 크기로 하고, 다 된 장을
    //    우리가 줄인다. 그래서 `image_decode_options`는 여기서 **펴는 동안의**
    //    메모리가 아니라 **들고 있을** 크기를 정하는 값이다.
    //  - 상한이 **둘**이다. 하나는 위와 같은 원본 한 장의 64 M 픽셀이고, 또
    //    하나는 **줄인 뒤의 장을 전부 합한** 64 M 픽셀이다. 뒤엣것이 없으면 작은
    //    gif 파일 하나가 장 수만큼 곱해져 같은 폭탄이 된다. 뒤엣것에 걸리면
    //    `error`가 `max_width`·`max_height`로 줄여 받으라고 적는다 — 그 상한은
    //    **줄인 크기에 걸리므로** 그 말이 실제로 통한다.
    //  - EXIF orientation은 모든 장에 같이 걸린다.
    // 실패·thread 규칙은 `load_image_file`과 같고, 걸리는 시간은 장 수에 비례해
    // **초 단위까지 간다** — UI thread에서 부를 자리가 아니다.
    [[nodiscard]] ui_animated_image load_animated_image_file(std::u8string_view path, std::u8string& error);
    [[nodiscard]] ui_animated_image load_animated_image_file(std::u8string_view path, const image_decode_options& options, std::u8string& error);

    // 인코딩된 이미지 바이트를 `ui_animated_image`로 만든다.
    // 계약은 `load_animated_image_file`과 같고, 바이트의 수명 규칙은
    // `decode_image_bytes`와 같다.
    [[nodiscard]] ui_animated_image decode_animated_image_bytes(std::span<const std::uint8_t> bytes, std::u8string& error);
    [[nodiscard]] ui_animated_image decode_animated_image_bytes(std::span<const std::uint8_t> bytes, const image_decode_options& options, std::u8string& error);
} // namespace luil

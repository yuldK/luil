#pragma once

#include "luil/ui/image_element.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace luil {
    // 자료가 그림보다 먼저 끝난 입력을 어떻게 할 것인가.
    //
    // 잘린 그림은 밖에서 오는 흔한 사고다 (내려받다 끊긴 파일, 쓰다 만 파일, 몸이
    // 잘린 응답). 라이브러리가 한쪽을 고르면 **반드시 다른 쪽 앱이 틀린다** —
    // 사진을 보는 창은 반쪽이라도 띄우는 것이 옳고, 인쇄로 넘길 원본을 고르는
    // 창은 반쪽을 원본으로 알고 넘기면 안 된다. 그래서 고르는 것을 앱에게 준다.
    //  - 고른 뒤에도 **잘렸다는 사실은 사라지지 않는다.** `accept`는 오류를 없애는
    //    스위치가 아니라 값을 함께 받겠다는 뜻이다.
    //  - 그리고 **잘린 입력만 고르는 값이다.** 다른 갈래의 실패(`frame_failed`·
    //    `out_of_memory`·`internal_error`·`cancelled`)는 `accept`라도 통째로
    //    실패다 — 그것들은 자료가 아니라 우리 쪽 사고라, 반쪽 그림으로 덮으면 우리
    //    잘못이 화면에서 "그냥 그런 그림"이 된다.
    enum class image_incomplete_policy
    {
        // 잘린 입력은 통째로 거절한다 — 빈 값과 오류뿐이다.
        //
        // 기본값이고 지금까지의 계약이다. 모르고 반쪽을 받는 앱이 없는 쪽이
        // 기본이어야 한다: 화면에서 반쪽 그림은 "원래 그런 그림"과 구별되지 않아
        // 아무도 실패를 못 본다.
        reject,
        // 푼 만큼을 내주고 잘렸다는 사실을 오류에 함께 남긴다.
        //
        // **`incomplete_input` 하나에만 걸린다.** 자료가 모자란 것은 밖에서 온
        // 사고라 앱이 반쪽을 쓸지 고를 수 있지만, 장을 풀지 못했거나 자리를 잡지
        // 못한 실패는 우리 쪽 사고다 — 그 위에 반쪽을 씌우면 고쳐야 할 우리 잘못이
        // 화면에서 "그냥 그런 그림"이 되어 아무도 보지 못한다. 그래서 그 갈래들은
        // 이 값을 고른 앱에게도 **빈 값과 오류**다.
        accept,
    };

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
    //  - 값 하나에 `std::function`이 들어 상수 평가에 쓰지 못한다. 옵션 묶음이
    //    갈고리를 드는 순간 치르는 값이고, 이 저장소가 이미 치른 자리이기도 하다
    //    (dialog_elements.h:196의 `constexpr`이 아닌 이유와 같다).
    struct image_decode_options
    {
        // 0 이하면 그 축에 상한이 없다. 원본이 이미 상한 안이면 늘리지 않는다
        // (확대는 그릴 때의 일이다 — `image_fit`).
        int max_width { 0 };
        int max_height { 0 };

        // 원본 **한 장**의 픽셀 수 상한이다. 0이면 라이브러리 기본값(64 M 픽셀)이다.
        //
        // **세 상한 중 이것만 앱이 정한다.** 앱은 자기가 무엇을 열려는지 안다 —
        // 목록의 섬네일을 짓는 자리는 4 M 픽셀이면 넉넉하고, 사진을 다루는 앱은
        // 64 M을 넘는 원본을 여는 것이 제 일이다. 반대로 **줄인 장의 합**(움직이는
        // 그림)과 **인코딩 바이트**의 상한은 손잡이로 내지 않았다: 그 둘은 앱의
        // 쓰임이 아니라 프로세스를 지키는 값이라 앱이 올려서 얻는 것이 없고,
        // 잘못 올리면 그 그림 하나가 아니라 프로세스가 죽는다.
        //  - 요청 하나에 붙는 상한이라는 뜻에서 `http_request::max_body_bytes`와
        //    같은 자리다 (http_message.h:88-93). 기본값은 라이브러리가 대고, 이
        //    한 번에 무엇이 옳은지는 부르는 쪽이 안다.
        //  - **내리는 것만이 아니라 올리는 것도 된다.** 올리면 그 그림 하나에
        //    대한 책임이 앱으로 넘어온다. 넘어가지 않는 것은 바이트 상한이라,
        //    올려도 코덱은 여전히 256 MiB보다 큰 파일을 보지 않는다.
        //  - 상한은 **원본**에 건다. `max_width`로 줄여 받아도 우회하지 못한다 —
        //    png는 코덱이 줄여 주지 못해(`SkPngCodecBase`가 `onGetScaledDimensions`를
        //    재정의하지 않는다) 실제로 원본을 펴기 때문이다.
        std::uint64_t max_source_pixels { 0 };

        // 자료가 그림보다 먼저 끝났을 때의 규칙이다.
        //
        // `accept`는 이 API에서 **값과 오류가 함께 오는** 유일한 자리다. 그래서
        // 부르는 쪽은 두 가지를 따로 묻는다 — `valid()`가 "그림이 있는가"이고
        // `error.empty()`가 "그것이 그림 전부인가"다. **둘을 하나로 묻는 코드는
        // 여기서 반드시 틀린다.**
        //  - 이 짝은 이 저장소에 이미 선 모양이다: 깨진 UTF-8을 U+FFFD로 바꿔
        //    담은 글이 값과 이유를 함께 든다 (`http_body::parse_error` —
        //    http_body.h:118-123, "비어 있지 않다고 값이 없는 것은 아니다").
        //  - 정지 그림에서는 코덱이 이미 써 놓은 줄까지가 값이다. 움직이는
        //    그림에서는 **첫 실패 앞까지의 장**이 값이고, 한 장도 풀지 못했으면
        //    `accept`라도 실패다 — 장이 없는 애니메이션은 값이 아니다.
        //  - **값과 함께 오는 갈래는 `incomplete_input` 하나다.** 정지 쪽도
        //    움직이는 쪽도 같다: 깨진 블록에서 멈춘 gif(`frame_failed`)나 자리를
        //    잡지 못한 장(`out_of_memory`)은 `accept`라도 **통째로** 실패이고,
        //    앞서 푼 장은 값이 되지 않는다. 자료의 사고만 앱이 고를 수 있고 우리
        //    쪽 사고는 고를 수 있는 것이 아니다.
        image_incomplete_policy incomplete { image_incomplete_policy::reject };

        // 접을 때가 되었는지를 묻는 갈고리다. **비어 있으면 묻지 않는다** — 옆에
        // `bool`을 두지 않는 것이 이 저장소의 규칙이다 (없음이 곧 스위치다).
        //
        // 디코딩은 파일 하나가 초 단위까지 가는 동기 작업이고(장 수에 비례한다),
        // 그동안 앱이 창을 닫거나 다른 그림을 고르면 남은 일은 전부 버릴 일이다.
        // 갈고리 하나면 그 자리에서 접히므로 200장짜리 gif가 **장 하나를 푸는
        // 시간 안에** 멈춘다. 갈고리가 없으면 창을 닫는 데 그 초가 그대로 든다.
        //  - **디코딩 thread에서** 불린다. 값을 읽어 답하는 것 이상을 하지 않아야
        //    하고(잠그면 그만큼 디코딩이 선다), 던지지 않아야 한다 — 이 API에는
        //    예외로 실패를 나르는 길이 없다.
        //  - 접힌 디코딩은 **빈 값과 `cancelled`**다. 반쪽을 내주지 않는다: 그것은
        //    `incomplete`가 답하는 다른 질문이고, 접었는데 그림이 오면 앱이
        //    "접었다"와 "끝났다"를 다시 갈라 들어야 한다.
        //  - 접힌 것도 알아볼 수 있는 답이다. `http_error_kind::cancelled`가 같은
        //    이유로 답을 그대로 내보낸다 (http_client.h:136-142 — 취소가 답을
        //    지우면 화면에 영원히 도는 표시가 남는다).
        std::function<bool()> cancelled {};
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
    //  - 보는 것은 `max_width`·`max_height`뿐이다. 나머지 셋은 크기가 아니라
    //    무엇을 거절하고 언제 접을지를 정하는 값이라 여기 걸리지 않는다.
    [[nodiscard]] image_pixel_size decoded_image_size(int width, int height, const image_decode_options& options) noexcept;

    // 디코딩이 실패한 갈래다.
    //
    // `http_error_kind`와 같은 모양이고 같은 이유다 (http_message.h:123-154) —
    // 글은 사람이 읽고 갈래는 코드가 읽는다. 글로 갈래를 가리려고 문장을 뒤지면
    // 문장을 다듬는 날 앱이 조용히 틀린다.
    enum class image_decode_error_kind
    {
        // 오류가 없다. **`incomplete == accept`로 받은 반쪽 그림은 여기가 아니다** —
        // 값이 섰어도 원본과 다르면 그 사실이 남는다.
        none,
        // 경로가 비었다 (파일을 열어 보지 않았다).
        path_empty,
        // 경로에 NUL이 들었다. 잘린 채로 열면 **다른 파일**이 열리고, 그것은
        // 오류로 보이지 않고 엉뚱한 그림으로 보인다.
        path_invalid,
        // 파일을 읽지 못했다 (없는 경로·권한·폴더·읽다 끊김).
        file_unreadable,
        // 입력이 비었다 (0 byte 파일이거나 빈 바이트 span이다).
        file_empty,
        // 인코딩 바이트가 상한(256 MiB)을 넘었다. 코덱에 물리기 전에 걸린다.
        input_too_large,
        // 이 빌드가 아는 형식이 아니다 (skia-build.md).
        unsupported_format,
        // 자료가 그림보다 먼저 끝났다. `incomplete == accept`면 이 갈래는 **값과
        // 함께** 온다.
        incomplete_input,
        // 그림에 픽셀이 없다 (변이 0이거나 장이 하나도 없다).
        no_pixels,
        // 원본 한 장이 `max_source_pixels`(기본 64 M 픽셀)를 넘었다.
        too_many_pixels,
        // **줄인 뒤의** 장을 전부 합한 것이 64 M 픽셀을 넘었다.
        // `max_width`·`max_height`를 낮추면 통과한다 — 상한이 줄인 크기에 걸리므로
        // 그 조언이 실제로 통한다.
        animation_too_large,
        // 장 하나가 코덱에서 실패했다. 몇 번째인지는 `frame`에 있다.
        frame_failed,
        // 앱이 `image_decode_options::cancelled`로 접었다.
        cancelled,
        // 픽셀을 담을 자리를 잡지 못했다.
        out_of_memory,
        // 그 밖의 우리 쪽 사고다 (`message`가 자세한 이유를 든다).
        internal_error,
    };

    // 실패는 예외가 아니라 값이다 (`http_error`·`utf_conversion_error`와 같은 성격이다).
    struct image_decode_error
    {
        image_decode_error_kind kind { image_decode_error_kind::none };
        // 사람이 아니라 개발자가 읽을 진단 영문이다. 사람에게 보일 문장은 앱이
        // 짓는다 (`http_error::message`와 같은 규약이다 — http_message.h:160-163).
        std::u8string message {};
        // 걸린 장의 번호다 (`frame_durations()`와 같은 0부터의 색인이다). 장을
        // 가리지 않는 갈래에서는 0이고, **정지 그림은 언제나 0번 장이다.**
        //  - 움직이는 그림에서 장을 푸는 도중에 난 실패는 **갈래를 가리지 않고**
        //    그 장의 번호를 든다 (자리를 잡지 못한 것도 그렇다). 40번째 장에서
        //    끝난 것을 0으로 적으면 `accept`로 마흔 장을 받은 앱이 "0번에서
        //    끊겼다"를 화면에 적는다 — 값이 거짓이면 그것을 읽는 화면도 거짓이다.
        //  - 장에 들어가기 전의 실패(경로·파일·머리·상한)는 0이다. 그때는 걸린
        //    장이라는 것이 없다.
        std::size_t frame { 0 };

        [[nodiscard]] bool empty() const noexcept
        {
            return kind == image_decode_error_kind::none;
        }
    };

    // 파일을 읽어 ui_image로 만든다. 코덱 지원은 Skia 빌드로 고정된다.
    // PNG·JPEG·WebP·GIF와 Skia 기본 코덱인 BMP·WBMP를 읽는다.
    // Rust PNG 코덱 구성에서는 APNG를 지원하며 ICO·TIFF·DDS·HEIF는 지원하지 않는다.
    // OS에 추가한 코덱은 지원 형식에 영향을 주지 않는다 (skia-build.md).
    //
    // 실패하면 빈 이미지와 error를 반환하며 성공하면 error를 변경하지 않는다.
    // 그래서 성공은 앞선 오류 값이 아니라 `valid()`로 묻는다.
    //  - **예외는 `incomplete == accept` 하나다.** 잘린 파일에서 그림과 오류가
    //    함께 온다 — `valid()`가 "그림이 있는가"이고 `error.empty()`가 "그것이
    //    전부인가"다.
    //  - 접힌 디코딩(`cancelled`)은 값이 없는 쪽이다. 빈 이미지와
    //    `image_decode_error_kind::cancelled`가 온다.
    // GIF·WebP·APNG도 첫 장만 읽는다. 애니메이션은 load_animated_image_file을 사용한다.
    // 첫 장은 합성이 끝난 논리 화면 크기로 반환한다.
    // EXIF orientation을 적용하고, 축이 바뀌는 방향에서는 width·height도 교환한다.
    // 원본이 `max_source_pixels`(기본 64 M 픽셀)를 넘으면 디코딩 전에 거절한다.
    // 축소 옵션을 지정해도 코덱이 원본 전체를 펼칠 수 있으므로 이 상한은 원본
    // 크기에 적용한다.
    //
    // 파일 I/O와 디코딩은 호출 스레드를 막으므로 logic 또는 worker 스레드에서 사용한다.
    // 막힌 동안 접는 길은 `image_decode_options::cancelled` 하나다.
    // 결과는 불변 이미지로 공유하며 frame마다 다시 디코딩하지 않는다.
    // CPU 코덱은 창·GPU·COM apartment를 요구하지 않는다.
    [[nodiscard]] ui_image load_image_file(std::u8string_view path, image_decode_error& error);
    [[nodiscard]] ui_image load_image_file(std::u8string_view path, const image_decode_options& options, image_decode_error& error);

    // 인코딩된 이미지 바이트를 `ui_image`로 만든다 (실행 파일 resource에 박은
    // 그림, 네트워크에서 받은 바이트처럼 **경로가 없는 그림**의 자리다).
    //
    // 받는 것은 파일 내용 그대로이지 원시 픽셀이 아니다 — 원시 RGBA는
    // `make_rgba_image`가 받는다. 계약·상한·thread 규칙은 `load_image_file`과
    // 같다. 바이트는 이 호출이 끝날 때까지만 살아 있으면 된다 (안에서 복사하지
    // 않는다).
    [[nodiscard]] ui_image decode_image_bytes(std::span<const std::uint8_t> bytes, image_decode_error& error);
    [[nodiscard]] ui_image decode_image_bytes(std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error);

    // 이미지 파일을 읽어 `ui_animated_image`로 만든다 (움직이는 gif·webp·APNG).
    //
    // **움직이지 않는 파일도 성공한다** — 한 장짜리 값이 나오고 `animated()`가
    // 거짓이다. 위의 둘과 짝이 되는 규칙이다: 정지 쪽 문은 움직이는 것을 내놓아
    // 놀래지 않고, 움직이는 쪽 문은 파일이 움직이지 않는다고 실패하지 않는다.
    // 그래서 **무엇이 올지 모르는 파일을 여는 앱은 이 문 하나만 쓰면 되고**,
    // element를 갈라 쥘 일도 없다 (`image_config`가 그 짝이다).
    //  - **APNG도 움직인다.** png 코덱을 rust로 못 박았기 때문이다
    //    (`LUIL_ANIMATED_PNG`은 늘 1이다). 치른 값은 ico다 — 위를 본다.
    //  - **장 하나가 실패하면 통째로 거절한다** (`incomplete::reject`, 기본값).
    //    절반만 돌려주면 화면에서 "원래 그런 그림"과 구별되지 않는다. 잘린 gif라도
    //    앞의 장은 필요한 앱은 `incomplete::accept`로 **푼 만큼과 이유를 함께**
    //    받거나, `load_image_file`로 첫 장 하나를 받는다 — 고르는 것은 앱이다.
    //    다만 `accept`가 여는 것은 **잘린 파일** 하나다: 장을 풀지 못했거나
    //    (`frame_failed`) 자리를 잡지 못한(`out_of_memory`) 실패는 그 값에서도
    //    통째로 거절이고, `error.frame`이 어느 장이었는지를 든다.
    //  - **장 하나하나가 이미 합성이 끝난 논리 화면 크기의 그림이다.** 형식이
    //    나르는 차이(고치는 사각형·지우기·겹치기)는 여기서 다 풀린다.
    //  - **움직이는 그림은 코덱에게 축소를 청하지 않는다.** Skia 자신이 "원본보다
    //    작은 비트맵으로 읽으면 애니메이션 webp가 옳지 않을 수 있다"고 적어 두었다
    //    (`SkCodec.h`의 `getPixels` 주석). 합성은 원본 크기로 하고, 다 된 장을
    //    우리가 줄인다. 그래서 `image_decode_options`는 여기서 **펴는 동안의**
    //    메모리가 아니라 **들고 있을** 크기를 정하는 값이다.
    //  - 상한이 **둘**이다. 하나는 위와 같은 원본 한 장의 `max_source_pixels`이고,
    //    또 하나는 **줄인 뒤의 장을 전부 합한** 64 M 픽셀이다. 뒤엣것이 없으면
    //    작은 gif 파일 하나가 장 수만큼 곱해져 같은 폭탄이 된다. 뒤엣것에 걸리면
    //    `error`가 `max_width`·`max_height`로 줄여 받으라고 적는다 — 그 상한은
    //    **줄인 크기에 걸리므로** 그 말이 실제로 통한다. 뒤엣것만은 앱이 바꾸지
    //    못한다 (`max_source_pixels`가 그 이유를 든다).
    //  - EXIF orientation은 모든 장에 같이 걸린다.
    //  - **접는 값이 가장 큰 자리가 여기다.** 장마다 한 번씩 갈고리를 물으므로
    //    200장짜리도 장 하나를 푸는 시간 안에 멈춘다.
    // 실패·thread 규칙은 `load_image_file`과 같고, 걸리는 시간은 장 수에 비례해
    // **초 단위까지 간다** — UI thread에서 부를 자리가 아니다.
    [[nodiscard]] ui_animated_image load_animated_image_file(std::u8string_view path, image_decode_error& error);
    [[nodiscard]] ui_animated_image load_animated_image_file(std::u8string_view path, const image_decode_options& options, image_decode_error& error);

    // 인코딩된 이미지 바이트를 `ui_animated_image`로 만든다.
    // 계약은 `load_animated_image_file`과 같고, 바이트의 수명 규칙은
    // `decode_image_bytes`와 같다.
    [[nodiscard]] ui_animated_image decode_animated_image_bytes(std::span<const std::uint8_t> bytes, image_decode_error& error);
    [[nodiscard]] ui_animated_image decode_animated_image_bytes(std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error);
} // namespace luil

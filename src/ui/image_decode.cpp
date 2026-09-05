#include "luil/ui/image_decode.h"

#include "include/codec/SkCodec.h"
#include "include/codec/SkEncodedOrigin.h"
#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSize.h"
#include "include/core/SkStream.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 원본 한 장의 픽셀 수 상한이다 (64 M 픽셀 = RGBA로 256 MiB).
        //
        // 파일은 밖에서 오는 값이라 상한이 필요하다 — 100 KB짜리 png가
        // 40000×40000으로 펴지면 6 GB를 잡으려 든다(압축 폭탄). 축소를 청해도
        // 상한은 **원본**에 건다: png는 코덱이 줄여 주지 못해(`SkPngCodecBase`가
        // `onGetScaledDimensions`를 재정의하지 않는다) 실제로 원본을 펴기 때문이다.
        //  - 앱이 바꾸는 값으로 열지 않는다. `max_dropped_paths`와 같은 성격의
        //    정책값이다.
        constexpr std::uint64_t maximum_image_pixels { 64u * 1024u * 1024u };

        // **들고 있을** 픽셀의 합 상한이다 (같은 64 M 픽셀).
        //
        // 한 장에만 걸면 300장짜리 gif가 상한을 하나도 넘기지 않고 몇 GB를 잡는다.
        //  - 이 상한은 **줄인 뒤의** 크기에 건다. 그래야 `max_width`·`max_height`로
        //    줄여 받으라는 오류 글이 실제로 통한다 — 원본에 걸면 앱이 손에 쥔
        //    손잡이로 할 수 있는 일이 없어진다.
        constexpr std::uint64_t maximum_animation_pixels { 64u * 1024u * 1024u };

        // 인코딩 바이트의 상한이다 (256 MiB).
        // 픽셀 상한과 별개다 — 파일을 통째로 메모리에 올려 코덱에 물리기 때문이다.
        constexpr std::uint64_t maximum_encoded_bytes { 256u * 1024u * 1024u };

        // 코덱이 말한 이유를 오류 글에 그대로 붙인다. Skia가 내주는 것도, 장
        // 번호도 ASCII라 한 바이트씩 옮기면 그만이다 (`make_hresult_error`가 이미
        // 선 그 자리다 — src/win32/win32_error.cpp:18-19).
        void append_ascii(std::u8string& output, const std::string_view text)
        {
            for (const char character : text)
                output.push_back(static_cast<char8_t>(character));
        }

        void append_number(std::u8string& output, const int value)
        {
            std::array<char, 16> buffer {};
            const std::to_chars_result conversion { std::to_chars(buffer.data(), buffer.data() + buffer.size(), value) };
            if (conversion.ec == std::errc {})
                append_ascii(output, std::string_view { buffer.data(), static_cast<std::size_t>(conversion.ptr - buffer.data()) });
        }

        [[nodiscard]] std::string_view result_reason(const SkCodec::Result result) noexcept
        {
            const char* const text { SkCodec::ResultToString(result) };
            return text != nullptr ? std::string_view { text } : std::string_view { "internal error" };
        }

        // 장 하나가 실패하면 통째로 거절한다 — 절반만 돌려주면 화면에서 "원래
        // 그런 그림"과 구별되지 않는다. 그래서 어느 장이었는지를 글에 남긴다
        // (번호는 `frame_durations()`와 같은 0부터의 색인이다).
        [[nodiscard]] std::u8string frame_failure(const int index, const std::string_view reason)
        {
            std::u8string output { u8"Failed to decode image frame " };
            append_number(output, index);
            output += u8" (";
            append_ascii(output, reason);
            output += u8").";
            return output;
        }

        // 코덱을 세우지 못한 이유다.
        // 두 갈래는 사람이 고칠 수 있는 것이라 이름을 붙여 준다 — "아는 형식이
        // 아니다"와 "파일이 도중에 끝났다"는 앱이 사람에게 그대로 전할 말이고,
        // 나머지는 우리가 볼 진단이라 코덱의 말을 그대로 싣는다.
        [[nodiscard]] std::u8string header_failure(const SkCodec::Result result)
        {
            switch (result)
            {
            case SkCodec::kUnimplemented:
                return u8"The image is not in a format this build can decode (png, jpeg, webp, gif, bmp and ico).";
            case SkCodec::kIncompleteInput:
                return u8"The image data ends before the image does.";
            default:
                break;
            }
            std::u8string output { u8"Failed to read the image header (" };
            append_ascii(output, result_reason(result));
            output += u8").";
            return output;
        }

        // 바이트 한 덩이를 코덱으로 세운다 (파일 쪽도 바이트 쪽도 여기로 모인다).
        //
        // **`SelectionPolicy`를 넘길 수 있는 것은 이 낡은 오버로드뿐이다.** 새
        // 쪽은 `SkCodecs::Decoder` 목록을 받는데 그 목록을 짓는 `get_decoders()`가
        // `src/codec/SkCodecPriv.h`에 갇혀 있어 소비자가 부를 수 없다
        // (SkCodec.h:190-202). 낡은 쪽은 Skia가 빌드 시점에 스스로 등록해 둔
        // 목록을 쓴다 — 우리 빌드에서는 png·jpeg·webp·gif가 거기 있다
        // (`src/codec/SkCodec.cpp`의 legacy init).
        [[nodiscard]] std::unique_ptr<SkCodec> make_codec(sk_sp<SkData> data, const SkCodec::SelectionPolicy policy, std::u8string& error)
        {
            SkCodec::Result result { SkCodec::kInternalError };
            std::unique_ptr<SkCodec> codec { SkCodec::MakeFromStream(SkMemoryStream::Make(std::move(data)), &result, nullptr, policy) };
            if (codec == nullptr)
            {
                error = header_failure(result);
                return nullptr;
            }
            return codec;
        }

        // 파일을 통째로 읽어 코덱에 물릴 바이트로 만든다.
        //
        // 경로 변환에 `utf8_to_utf16`이 필요 없는 자리다 — `std::filesystem::path`가
        // UTF-8을 네이티브 와이드로 옮긴다. 그 대가로 "UTF-8이 아니다"라는 갈래가
        // 사라지고 잘못된 경로는 "열지 못했다"로 떨어진다.
        [[nodiscard]] sk_sp<SkData> read_image_file(const std::u8string_view path, std::u8string& error)
        {
            if (path.empty())
            {
                error = u8"The image path is empty.";
                return nullptr;
            }
            // NUL이 든 경로는 거절한다. 네이티브 경로가 거기서 잘려 **다른 파일**이
            // 열리는데, 그것은 오류로 보이지 않고 엉뚱한 그림으로 보인다.
            if (path.find(u8'\0') != std::u8string_view::npos)
            {
                error = u8"The image path contains a null character.";
                return nullptr;
            }

            std::filesystem::path native {};
            try
            {
                // 짓기만 하는 자리라 지금 표준 라이브러리에서는 던지지 않지만,
                // **이 API가 던지지 않는다고 적어 두었다** — 계약을 구현 세부에
                // 기대지 않는다 (`std::bad_alloc`도 여기서 멈춘다).
                native = std::filesystem::path { std::u8string { path } };
            }
            catch (...)
            {
                error = u8"Failed to open the image file.";
                return nullptr;
            }

            // 던지지 않는 오버로드들이다 — 없는 파일도 권한 없는 파일도 여기서 갈린다.
            //  - **폴더를 먼저 거른다.** `file_size`는 폴더에 0을 답하고 오류를
            //    적지 않아, 그냥 물으면 폴더 경로가 "파일이 비었다"로 보고된다 —
            //    사람이 읽고 고칠 수 있는 말이 아니다.
            std::error_code code {};
            const std::filesystem::file_status status { std::filesystem::status(native, code) };
            if (code || std::filesystem::is_regular_file(status) == false)
            {
                error = u8"Failed to open the image file.";
                return nullptr;
            }
            const std::uintmax_t size { std::filesystem::file_size(native, code) };
            if (code)
            {
                error = u8"Failed to open the image file.";
                return nullptr;
            }
            if (size == 0)
            {
                error = u8"The image file is empty.";
                return nullptr;
            }
            if (size > maximum_encoded_bytes)
            {
                error = u8"The image file is larger than the 256 mebibyte decode limit.";
                return nullptr;
            }

            std::ifstream file { native, std::ios::binary };
            if (file.is_open() == false)
            {
                error = u8"Failed to open the image file.";
                return nullptr;
            }
            // 상한이 앞에서 걸러 주므로 이 자리 잡기는 256 MiB를 넘지 않는다.
            // `MakeUninitialized`는 실패하면 null이 아니라 그 자리에서 죽는
            // Skia의 자리 잡기라 살펴볼 것이 없다 (`sk_malloc_throw`).
            sk_sp<SkData> data { SkData::MakeUninitialized(static_cast<std::size_t>(size)) };
            file.read(reinterpret_cast<char*>(data->writable_data()), static_cast<std::streamsize>(size));
            // 크기를 물은 뒤 파일이 줄어들 수 있다 — 덜 읽은 바이트를 코덱에
            // 물리면 "잘린 그림"으로 보여, 실패를 그 자리에서 잡는다.
            if (file.gcount() != static_cast<std::streamsize>(size))
            {
                error = u8"Failed to open the image file.";
                return nullptr;
            }
            return data;
        }

        // EXIF 방향과 목표 크기를 함께 잰 값이다. 정지와 움직이는 그림이 같은
        // 답을 써야 해서 한 자리에서 만든다.
        struct oriented_target
        {
            SkEncodedOrigin origin { kTopLeft_SkEncodedOrigin };
            // 파일에 적힌 축의 원본 크기다.
            SkISize native {};
            // 파일 축으로 본 목표 크기다 (돌려세우기 **전**).
            SkISize file {};
            // 화면에 놓일 크기다 (돌려세운 **뒤**) — `ui_image`가 답하는 값이다.
            image_pixel_size display {};
        };

        // **상한은 돌려세운 뒤의 축에 건다.** 앱이 "200px 칸"이라고 할 때 뜻하는
        // 것은 화면에 놓일 크기이지 파일에 적힌 방향이 아니다 — 파일 축에 걸면
        // 세로로 찍은 사진만 상한이 반대 축에 걸려 엉뚱하게 줄어들고, 그림은
        // 옳게 뜨므로 눈으로 잘 걸리지 않는다.
        [[nodiscard]] oriented_target measure_target(const SkCodec& codec, const image_decode_options& options)
        {
            oriented_target target {};
            target.origin = codec.getOrigin();
            target.native = codec.dimensions();
            // origin 5~8이 가로세로를 맞바꾼다.
            const bool swaps { SkEncodedOriginSwapsWidthHeight(target.origin) };
            const SkISize display { swaps ? SkISize { target.native.height(), target.native.width() } : target.native };
            target.display = decoded_image_size(display.width(), display.height(), options);
            target.file = swaps ? SkISize { target.display.height, target.display.width } : SkISize { target.display.width, target.display.height };
            return target;
        }

        [[nodiscard]] bool check_native_size(const SkISize native, std::u8string& error)
        {
            if (native.width() <= 0 || native.height() <= 0)
            {
                error = u8"The image has no pixels.";
                return false;
            }
            if (static_cast<std::uint64_t>(native.width()) * static_cast<std::uint64_t>(native.height()) > maximum_image_pixels)
            {
                error = u8"The image is larger than the 64 megapixel decode limit.";
                return false;
            }
            return true;
        }

        [[nodiscard]] SkImageInfo premultiplied_info(const SkISize size)
        {
            return SkImageInfo::Make(size, kN32_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
        }

        // premul로 편 한 장을 EXIF 방향과 목표 크기에 맞춰 straight alpha RGBA로
        // 옮겨 `ui_image`를 짓는다.
        //
        // 정지 경로와 움직이는 경로가 이 한 함수를 부른다 — 돌려세우기와 축소가
        // 두 자리에 갈라져 서면 한쪽만 고쳐졌을 때 정지 그림과 움직이는 그림이
        // 다른 방향으로 서고, 그것은 눈으로만 보인다.
        //
        // `straight`는 **바깥에서 한 번 잡아 장마다 다시 쓰는** 스크래치다. 장마다
        // 새로 잡으면 200장짜리 gif에서 자리 잡기가 200번 돈다.
        [[nodiscard]] ui_image finish_frame(const SkBitmap& decoded, const oriented_target& target, std::uint8_t* const straight, const std::size_t straight_bytes, std::u8string& error)
        {
            // 방향도 크기도 그대로면 옮겨 그릴 것이 없다 — 흔한 자리(EXIF 없는
            // png를 상한 없이 읽기)가 한 번의 복사도 하지 않는다.
            const SkBitmap* finished { &decoded };
            SkBitmap oriented {};
            if (target.origin != kTopLeft_SkEncodedOrigin || decoded.dimensions() != target.file)
            {
                if (oriented.tryAllocPixels(premultiplied_info(SkISize { target.display.width, target.display.height })) == false)
                {
                    error = u8"Failed to allocate the decoded image pixels.";
                    return {};
                }
                const sk_sp<SkImage> source { decoded.asImage() };
                if (source == nullptr)
                {
                    error = u8"Failed to allocate the decoded image pixels.";
                    return {};
                }

                SkCanvas canvas { oriented };
                canvas.clear(SK_ColorTRANSPARENT);
                // `SkEncodedOriginToMatrix`가 받는 두 수는 **돌린 뒤(표시)의**
                // 크기다 (SkEncodedOrigin.h:27-31 — Skia 자신의 소비자도 그렇게
                // 부른다, SkAnimCodecPlayer.cpp:80-82). 파일 축의 크기를 넣으면
                // 90° 도는 그림만 화면 밖으로 밀려난다.
                canvas.concat(SkEncodedOriginToMatrix(target.origin, target.display.width, target.display.height));
                SkPaint paint {};
                paint.setAntiAlias(true);
                // 방금 지운 투명 위라 섞을 밑그림이 없다. 그런데도 섞으면 반투명한
                // 가장자리가 한 번 더 묽어지므로 덮어 쓴다.
                paint.setBlendMode(SkBlendMode::kSrc);
                // 그리기와 같은 표본이다 (src/ui/image_element.cpp:120-121) —
                // 디코딩에서 줄인 그림과 그리기에서 줄인 그림이 같아 보인다.
                //  - 목적 사각형이 **파일 축**의 목표 크기인 것이 요점이다. 위의
                //    행렬이 그 사각형을 표시 축으로 옮겨 정확히 칸을 채운다.
                canvas.drawImageRect(source.get(), SkRect::MakeWH(static_cast<float>(target.file.width()), static_cast<float>(target.file.height())),
                    SkSamplingOptions { SkFilterMode::kLinear, SkMipmapMode::kLinear }, &paint);
                finished = &oriented;
            }

            // **premul ↔ straight 왕복은 여기 한 번뿐이다.** 합성·축소·돌려세우기가
            // 전부 premul에서 끝난 뒤라 채널당 1/255의 오차가 쌓이지 않는다
            // (image-decode-design.md). straight alpha RGBA가 `make_rgba_image`의
            // 계약이고(image-design.md), 그것이 `ui_image`로 가는 유일한 문이다.
            const SkImageInfo straight_info { SkImageInfo::Make(target.display.width, target.display.height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB()) };
            if (finished->readPixels(straight_info, straight, static_cast<std::size_t>(target.display.width) * 4, 0, 0) == false)
            {
                error = u8"Failed to read the decoded image pixels.";
                return {};
            }

            ui_image image { make_rgba_image(target.display.width, target.display.height, std::span<const std::uint8_t> { straight, straight_bytes }) };
            if (image.valid() == false)
            {
                // 크기는 방금 우리가 맞췄으므로 남는 실패는 Skia 쪽 자리 잡기다.
                // 빈 이미지를 이유 없이 돌려주면 "왜 안 뜨는가"가 앱에게 보이지 않는다.
                error = u8"Failed to wrap the decoded image pixels for drawing.";
                return {};
            }
            return image;
        }

        // 다 된 한 장을 담을 straight alpha 스크래치다.
        //
        // 상한 안이어도 256 MiB까지 가는 자리라 **던지지 않고 물어본다.** 이
        // 라이브러리에는 예외로 실패를 나르는 API가 없고, 크기를 정하는 것은
        // 밖에서 온 파일이다 — 잡을 자리 없는 `std::bad_alloc`이 앱을 끝내면
        // 그것이야말로 이 API가 하지 않기로 한 일이다.
        [[nodiscard]] std::unique_ptr<std::uint8_t[]> allocate_straight_pixels(const image_pixel_size size, std::u8string& error)
        {
            const std::size_t bytes { static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4 };
            std::unique_ptr<std::uint8_t[]> pixels { new (std::nothrow) std::uint8_t[bytes] };
            if (pixels == nullptr)
                error = u8"Failed to allocate the decoded image pixels.";
            return pixels;
        }

        [[nodiscard]] ui_image decode_still(sk_sp<SkData> data, const image_decode_options& options, std::u8string& error)
        {
            const std::unique_ptr<SkCodec> codec { make_codec(std::move(data), SkCodec::SelectionPolicy::kPreferStillImage, error) };
            if (codec == nullptr)
                return {};
            // GIF·WebP·APNG에서도 첫 장만 읽는다.
            // 부분 frame인 GIF도 합성이 끝난 논리 화면 크기로 반환한다.
            const SkISize native { codec->dimensions() };
            if (check_native_size(native, error) == false)
                return {};
            const oriented_target target { measure_target(*codec, options) };

            // 코덱이 줄여 줄 수 있는 만큼만 맡기고 나머지는 우리가 마무리한다.
            //  - png는 아예 줄이지 못한다. `SkPngCodecBase`가 `onGetScaledDimensions`를
            //    재정의하지 않아 기본이 원본을 답하고, 더 작은 크기를 청하면
            //    `getPixels`가 `kInvalidScale`이다 (SkCodec.cpp:527).
            //  - jpeg는 libjpeg의 1/8 눈금까지, 임의 축소는 `SkScalingCodec` 파생
            //    (webp·wuffs gif)뿐이다.
            //  - **후보가 목표보다 작으면 쓰지 않는다.** 마무리 축소는 늘릴 수
            //    없어, 모자란 후보를 받으면 청한 크기가 흐릿해진다.
            SkISize decode_size { native };
            if (target.file != native)
            {
                const float width_scale { static_cast<float>(target.file.width()) / static_cast<float>(native.width()) };
                const float height_scale { static_cast<float>(target.file.height()) / static_cast<float>(native.height()) };
                const SkISize candidate { codec->getScaledDimensions(width_scale > height_scale ? width_scale : height_scale) };
                if (candidate != native && candidate.width() >= target.file.width() && candidate.height() >= target.file.height())
                    decode_size = candidate;
            }

            // **premul로 편다.** 줄이기와 돌려세우기가 전부 이 공간에서 돈다 —
            // straight를 그대로 섞으면 완전히 투명한 픽셀의 색이 이웃과 평균되어
            // 가장자리에 테가 생긴다 (image-decode-design.md).
            SkImageInfo work { premultiplied_info(decode_size) };
            SkBitmap decoded {};
            if (decoded.tryAllocPixels(work) == false)
            {
                error = u8"Failed to allocate the decoded image pixels.";
                return {};
            }
            SkCodec::Result result { codec->getPixels(work, decoded.getPixels(), decoded.rowBytes()) };
            if (result == SkCodec::kInvalidScale && decode_size != native)
            {
                // 코덱이 스스로 답한 후보인데도 거절하는 자리가 있다 (형식마다
                // `onDimensionsSupported`가 다르다). 그때는 원본으로 물러선다 —
                // 축소는 어차피 우리가 마무리하므로 **결과는 같고 시간만 더 든다.**
                // 여기서 실패로 끝내면 멀쩡한 그림이 상한 하나 때문에 사라진다.
                decode_size = native;
                work = premultiplied_info(native);
                decoded.reset();
                if (decoded.tryAllocPixels(work) == false)
                {
                    error = u8"Failed to allocate the decoded image pixels.";
                    return {};
                }
                result = codec->getPixels(work, decoded.getPixels(), decoded.rowBytes());
            }
            if (result != SkCodec::kSuccess)
            {
                // `kIncompleteInput`도 실패로 본다 — 잘린 파일을 반만 내주지 않는
                // 것이 지금 계약이다.
                std::u8string failure { u8"Failed to decode the image pixels (" };
                append_ascii(failure, result_reason(result));
                failure += u8").";
                error = std::move(failure);
                return {};
            }

            const std::unique_ptr<std::uint8_t[]> straight { allocate_straight_pixels(target.display, error) };
            if (straight == nullptr)
                return {};
            const std::size_t straight_bytes { static_cast<std::size_t>(target.display.width) * static_cast<std::size_t>(target.display.height) * 4 };
            return finish_frame(decoded, target, straight.get(), straight_bytes, error);
        }

        [[nodiscard]] ui_animated_image decode_animation(sk_sp<SkData> data, const image_decode_options& options, std::u8string& error)
        {
            const std::unique_ptr<SkCodec> codec { make_codec(std::move(data), SkCodec::SelectionPolicy::kPreferAnimation, error) };
            if (codec == nullptr)
                return {};
            const SkISize native { codec->dimensions() };
            if (check_native_size(native, error) == false)
                return {};

            // `getFrameCount()`를 먼저 부르는 차례를 지킨다 — 그것만이 stream을
            // 읽어 목록을 짓고, `getFrameInfo(int, *)`는 이미 지어진 것을 볼 뿐이다
            // (SkCodec.h:747-748).
            const int count { codec->getFrameCount() };
            if (count <= 0)
            {
                error = u8"The image has no frames.";
                return {};
            }
            std::vector<SkCodec::FrameInfo> infos(static_cast<std::size_t>(count));
            bool has_info { true };
            for (int index = 0; index < count; ++index)
                if (codec->getFrameInfo(index, &infos[static_cast<std::size_t>(index)]) == false)
                {
                    has_info = false;
                    break;
                }
            // **정지 파일이 여기서 거짓이다.** 한 장이고 목록이 없으면 한 장짜리
            // 값으로 내려간다 — 실패가 아니다. 움직이는 쪽 문이 "이 파일은 움직이지
            // 않는다"로 실패하지 않는 것이 계약이고, 그래야 무엇이 올지 모르는 앱이
            // 이 문 하나만 쓴다. 여러 장이라고 해 놓고 목록을 못 주면 그때는 실패다.
            if (has_info == false && count > 1)
            {
                error = u8"Failed to read the image frame list.";
                return {};
            }

            const oriented_target target { measure_target(*codec, options) };
            // **줄인 뒤의 장을 전부 합한** 상한이다. 없으면 작은 gif 파일 하나가
            // 장 수만큼 곱해져 같은 폭탄이 된다.
            if (static_cast<std::uint64_t>(target.display.width) * static_cast<std::uint64_t>(target.display.height) * static_cast<std::uint64_t>(count) > maximum_animation_pixels)
            {
                error = u8"The animation needs more than the 64 megapixel decode limit; ask for a smaller size with max_width or max_height.";
                return {};
            }

            // 누가 뒤에서 필요한가를 먼저 센다 — 이 표가 있어야 다 쓴 사슬 버퍼를
            // 곧바로 놓을 수 있고, 합성 중의 peak가 장 수에 비례하지 않는다.
            std::vector<int> needed_until(static_cast<std::size_t>(count), -1);
            if (has_info)
                for (int index = 1; index < count; ++index)
                {
                    const int required { infos[static_cast<std::size_t>(index)].fRequiredFrame };
                    if (required != SkCodec::kNoFrame && required >= 0 && required < index)
                        needed_until[static_cast<std::size_t>(required)] = index;
                }

            // **native 크기로만 편다.** 코덱에게 줄여 달라고 청하지 않는다: Skia
            // 자신이 "원본보다 작은 비트맵으로 읽으면 애니메이션 webp가 옳지 않을
            // 수 있다"고 적어 두었다 (SkCodec.h의 `getPixels` 주석). 합성은 원본
            // 크기로 하고, 다 된 장을 우리가 줄인다.
            const SkImageInfo work { premultiplied_info(native) };
            const std::unique_ptr<std::uint8_t[]> straight { allocate_straight_pixels(target.display, error) };
            if (straight == nullptr)
                return {};
            const std::size_t straight_bytes { static_cast<std::size_t>(target.display.width) * static_cast<std::size_t>(target.display.height) * 4 };

            std::vector<SkBitmap> chain(static_cast<std::size_t>(count));
            std::vector<int> release_head(static_cast<std::size_t>(count), -1);
            std::vector<int> release_next(static_cast<std::size_t>(count), -1);
            for (int index = 0; index < count; ++index)
            {
                const int last { needed_until[static_cast<std::size_t>(index)] };
                if (last >= 0)
                {
                    release_next[static_cast<std::size_t>(index)] = release_head[static_cast<std::size_t>(last)];
                    release_head[static_cast<std::size_t>(last)] = index;
                }
            }
            std::vector<ui_image> frames {};
            frames.reserve(static_cast<std::size_t>(count));
            std::vector<std::chrono::milliseconds> durations {};
            durations.reserve(static_cast<std::size_t>(count));

            for (int index = 0; index < count; ++index)
            {
                // **매 바퀴 새로 잡는다.** `SkBitmap` 복사는 픽셀 참조 공유라, 한
                // 장을 다시 쓰면 사슬에 붙들어 둔 앞 장이 그 자리에서 덮인다.
                SkBitmap canvas_bitmap {};
                if (canvas_bitmap.tryAllocPixels(work) == false)
                {
                    error = u8"Failed to allocate the decoded image pixels.";
                    return {};
                }

                SkCodec::Options decode_options {};
                decode_options.fFrameIndex = index;
                const int required { has_info ? infos[static_cast<std::size_t>(index)].fRequiredFrame : SkCodec::kNoFrame };
                if (required != SkCodec::kNoFrame)
                {
                    // **`index - 1`이 아니라 `fRequiredFrame`을 준다.**
                    //  - `index - 1`은 그 장의 disposal이 `kRestorePrevious`면
                    //    `kInvalidParameters`다 (SkCodec.cpp:449-451) — 멀쩡한 gif가
                    //    통째로 거절되는 자리다.
                    //  - `fRequiredFrame`은 결코 `kRestorePrevious`가 아니다.
                    //    그것을 정하는 `setAlphaAndRequiredFrame`이 그런 장을
                    //    while 루프로 건너뛴다 (SkCodec.cpp:1027-1035).
                    //  - 그리고 `fPriorFrame == fRequiredFrame`일 때만
                    //    `kRestoreBGColor`의 지우기가 걸린다 (SkCodec.cpp:455-458의
                    //    `frameId() == requiredFrame`). 곧 이 선택만이 disposal을
                    //    옳게 만든다.
                    //  - Skia 자신의 정본 소비자가 같은 선택을 한다
                    //    (modules/skresources/src/SkAnimCodecPlayer.cpp:101-105).
                    if (required < 0 || required >= index || chain[static_cast<std::size_t>(required)].isNull())
                    {
                        error = frame_failure(index, "internal error");
                        return {};
                    }
                    // 코덱은 **합성이 끝난** 앞 장이 목적 버퍼에 이미 들어 있기를
                    // 요구한다. 같은 `work`라 형식 변환 없이 옮겨 담기다.
                    if (chain[static_cast<std::size_t>(required)].readPixels(work, canvas_bitmap.getPixels(), canvas_bitmap.rowBytes(), 0, 0) == false)
                    {
                        error = frame_failure(index, "internal error");
                        return {};
                    }
                    decode_options.fPriorFrame = required;
                }
                else
                {
                    canvas_bitmap.eraseColor(SK_ColorTRANSPARENT);
                }

                if (const SkCodec::Result result { codec->getPixels(work, canvas_bitmap.getPixels(), canvas_bitmap.rowBytes(), &decode_options) }; result != SkCodec::kSuccess)
                {
                    error = frame_failure(index, result_reason(result));
                    return {};
                }

                // 뒤에서 필요하면 붙들고(픽셀 참조 공유라 복사가 아니다), 아무도
                // 찾지 않게 된 것은 그 자리에서 놓는다.
                if (needed_until[static_cast<std::size_t>(index)] >= 0)
                    chain[static_cast<std::size_t>(index)] = canvas_bitmap;
                for (int older = release_head[static_cast<std::size_t>(index)]; older >= 0; older = release_next[static_cast<std::size_t>(older)])
                    chain[static_cast<std::size_t>(older)].reset();

                frames.push_back(finish_frame(canvas_bitmap, target, straight.get(), straight_bytes, error));
                if (frames.back().valid() == false)
                    return {};
                durations.push_back(std::chrono::milliseconds { has_info ? infos[static_cast<std::size_t>(index)].fDuration : 0 });
            }

            // Skia의 되풀이 수는 **첫 바퀴를 빼고** 센다 (SkCodec.h:785-805) — 0이
            // "한 번 돈다"이고 음수가 "끝없이"다. 그대로 내보내면 0을 "돌지
            // 않는다"로 읽는 자리가 반드시 생겨, 바퀴 수로 옮기는 것을 여기서
            // 한 번 하고 만다 (`image_plays_forever`).
            //  - wuffs는 넘치는 되풀이 수를 `INT_MAX`로 잘라 준다
            //    (SkWuffsCodec.cpp:881) — 그것도 끝없다로 본다. 잘리지 않았다면
            //    `repetition + 1`이 넘쳐 음수가 될 자리이기도 하다.
            const int repetition { codec->getRepetitionCount() };
            const int play_count { repetition < 0 || repetition >= std::numeric_limits<int>::max() ? image_plays_forever : repetition + 1 };

            ui_animated_image image { make_animated_image(frames, durations, play_count) };
            if (image.valid() == false)
            {
                // 크기도 길이도 방금 우리가 맞췄으므로 남는 실패는 만드는 자리의
                // 자리 잡기다. 이유 없이 빈 값을 돌려주지 않는다.
                error = u8"Failed to build the animated image from its frames.";
                return {};
            }
            return image;
        }

        [[nodiscard]] sk_sp<SkData> borrow_image_bytes(const std::span<const std::uint8_t> bytes, std::u8string& error)
        {
            if (bytes.empty())
            {
                error = u8"The image bytes are empty.";
                return nullptr;
            }
            if (bytes.size() > maximum_encoded_bytes)
            {
                error = u8"The image bytes are larger than the 256 mebibyte decode limit.";
                return nullptr;
            }
            // 복사하지 않는다 — 코덱은 이 호출이 끝나기 전에 죽으므로 빌린 버퍼가
            // 그보다 오래 살 필요가 없다.
            return SkData::MakeWithoutCopy(bytes.data(), bytes.size());
        }
    } // namespace

    image_pixel_size decoded_image_size(const int width, const int height, const image_decode_options& options) noexcept
    {
        if (width <= 0 || height <= 0)
            return {};

        // 상한이 없거나 원본이 이미 그 안이면 배율은 1이다 — 늘리지 않는다.
        double scale { 1.0 };
        if (options.max_width > 0 && options.max_width < width)
            scale = static_cast<double>(options.max_width) / static_cast<double>(width);
        if (options.max_height > 0 && options.max_height < height)
        {
            const double vertical { static_cast<double>(options.max_height) / static_cast<double>(height) };
            // 두 축 모두 상한이 있으면 더 빡빡한 쪽이 이긴다.
            if (vertical < scale)
                scale = vertical;
        }
        if (scale >= 1.0)
            return { width, height };

        // 반올림이 0을 만들면 1로 올린다 — 0 크기는 빈 이미지가 되어, 축소를 청한
        // 대가로 그림이 사라진다.
        const int scaled_width { static_cast<int>(std::lround(static_cast<double>(width) * scale)) };
        const int scaled_height { static_cast<int>(std::lround(static_cast<double>(height) * scale)) };
        return { scaled_width > 0 ? scaled_width : 1, scaled_height > 0 ? scaled_height : 1 };
    }

    ui_image load_image_file(const std::u8string_view path, std::u8string& error)
    {
        return load_image_file(path, image_decode_options {}, error);
    }

    ui_image load_image_file(const std::u8string_view path, const image_decode_options& options, std::u8string& error)
    {
        sk_sp<SkData> data { read_image_file(path, error) };
        if (data == nullptr)
            return {};
        return decode_still(std::move(data), options, error);
    }

    ui_image decode_image_bytes(const std::span<const std::uint8_t> bytes, std::u8string& error)
    {
        return decode_image_bytes(bytes, image_decode_options {}, error);
    }

    ui_image decode_image_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, std::u8string& error)
    {
        sk_sp<SkData> data { borrow_image_bytes(bytes, error) };
        if (data == nullptr)
            return {};
        return decode_still(std::move(data), options, error);
    }

    ui_animated_image load_animated_image_file(const std::u8string_view path, std::u8string& error)
    {
        return load_animated_image_file(path, image_decode_options {}, error);
    }

    ui_animated_image load_animated_image_file(const std::u8string_view path, const image_decode_options& options, std::u8string& error)
    {
        sk_sp<SkData> data { read_image_file(path, error) };
        if (data == nullptr)
            return {};
        return decode_animation(std::move(data), options, error);
    }

    ui_animated_image decode_animated_image_bytes(const std::span<const std::uint8_t> bytes, std::u8string& error)
    {
        return decode_animated_image_bytes(bytes, image_decode_options {}, error);
    }

    ui_animated_image decode_animated_image_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, std::u8string& error)
    {
        sk_sp<SkData> data { borrow_image_bytes(bytes, error) };
        if (data == nullptr)
            return {};
        return decode_animation(std::move(data), options, error);
    }
} // namespace luil

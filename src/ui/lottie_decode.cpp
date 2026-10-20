#include "luil/ui/image_decode.h"

#include "host/font_registry.h"
#include "ui/image_backing.h"
#include "ui/image_codecs.h"
#include "ui/image_source.h"
#include "ui/text_shaping.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "modules/skottie/include/Skottie.h"
#include "modules/skresources/include/SkResources.h"
#include "modules/skshaper/include/SkShaper_factory.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <utility>

// Lottie를 켠 구성이다 (`LUIL_ENABLE_LOTTIE`). 이 파일이 Skottie를 끌어온다 — 그리기도 이 파일의
// `lottie_backing` 구현을 거치므로, 앱이 Lottie 진입점을 부르지 않으면 Skottie는 실행 파일에
// 들어오지 않는다 (docs/vector-image-design.md).
namespace luil {
    namespace {
        using image_input::borrow_image_bytes;
        using image_input::cancelled_error;
        using image_input::decode_cancelled;
        using image_input::make_error;
        using image_input::read_image_file;

        // 한 변의 상한이다 (svg_decode.cpp와 같은 값, 같은 이유).
        constexpr float maximum_lottie_extent { 1'000'000.0f };

        // Skottie 장면 하나다.
        //
        // 장면은 시각으로 옮길 때마다 바뀐다(`seekFrameTime`). 그래서 옮기고 그리는 두 걸음을
        // 자물쇠 하나로 묶는다 — 같은 값을 두 창이 서로 다른 시각으로 함께 그릴 수 있고,
        // 그 사이에 다른 시각이 끼면 엉뚱한 장면이 그려진다.
        class skottie_scene final : public lottie_backing
        {
        public:
            explicit skottie_scene(sk_sp<skottie::Animation> animation)
                : animation_ { std::move(animation) }
            {}

            void draw(SkCanvas& canvas, const double seconds, const SkRect& bounds) const override
            {
                const std::scoped_lock lock { mutex_ };
                animation_->seekFrameTime(seconds);
                animation_->render(&canvas, &bounds);
            }

        private:
            sk_sp<skottie::Animation> animation_ {};
            mutable std::mutex mutex_ {};
        };

        [[nodiscard]] bool usable_extent(const float extent) noexcept
        {
            return std::isfinite(extent) && extent > 0.0f && extent <= maximum_lottie_extent;
        }

        // 읽은 바이트를 Lottie 장면으로 세운다.
        //  - 밖의 자원은 열지 않는다. 이미지 레이어의 파일·네트워크 경로는 `data:` URI만 풀고,
        //    그것도 지금 미리 푼다 (svg_decode.cpp와 같은 규칙이다).
        //  - 글자 레이어는 시스템 글꼴로 그리고, 모양 잡기는 텍스트 기능을 켠 구성만
        //    HarfBuzz다 (`text_shaping.h`).
        [[nodiscard]] ui_lottie decode_lottie(const sk_sp<SkData>& data, const image_decode_options& options, image_decode_error& error)
        {
            register_optional_image_decoders();
            const sk_sp<SkFontMgr> fonts { platform_font_source().make_uncached_manager() };
            skottie::Animation::Builder builder {};
            builder.setFontManager(fonts);
            builder.setResourceProvider(skresources::DataURIResourceProviderProxy::Make(nullptr, skresources::ImageDecodeStrategy::kPreDecode, fonts));
            if (sk_sp<SkShapers::Factory> shaping { text_shaping_factory() }; shaping != nullptr)
                builder.setTextShapingFactory(std::move(shaping));

            sk_sp<skottie::Animation> animation { builder.make(static_cast<const char*>(data->data()), data->size()) };
            if (animation == nullptr)
            {
                error = make_error(image_decode_error_kind::unsupported_format, u8"The data is not a Lottie animation.");
                return {};
            }
            // JSON을 풀고 장면을 다 지은 자리다.
            if (decode_cancelled(options))
            {
                error = cancelled_error();
                return {};
            }

            const SkSize intrinsic { animation->size() };
            if (usable_extent(intrinsic.width()) == false || usable_extent(intrinsic.height()) == false)
            {
                error = make_error(image_decode_error_kind::no_pixels, u8"The Lottie animation has no usable size.");
                return {};
            }
            const double seconds { animation->duration() };
            const double frame_rate { animation->fps() };
            const double maximum_seconds { std::chrono::duration<double> { std::chrono::nanoseconds::max() }.count() };
            if (std::isfinite(seconds) == false || seconds <= 0.0 || seconds >= maximum_seconds || std::isfinite(frame_rate) == false || frame_rate <= 0.0)
            {
                error = make_error(image_decode_error_kind::no_pixels, u8"The Lottie animation has no frames.");
                return {};
            }

            // 상한(`max_width`·`max_height`)은 그림 자신의 크기를 정한다 (svg_decode.cpp와 같다).
            // 장면은 그릴 때 그 크기로 늘이거나 줄여 그린다.
            const int width { static_cast<int>(std::lround(intrinsic.width())) };
            const int height { static_cast<int>(std::lround(intrinsic.height())) };
            const image_pixel_size size { decoded_image_size(width > 0 ? width : 1, height > 0 ? height : 1, options) };
            const std::chrono::nanoseconds duration { std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double> { seconds }) };

            ui_lottie result { image_access::lottie(std::make_shared<const skottie_scene>(std::move(animation)), size.width, size.height, duration, frame_rate) };
            if (result.valid() == false)
                error = make_error(image_decode_error_kind::internal_error, u8"Failed to build the Lottie animation.");
            return result;
        }
    } // namespace

    ui_lottie load_lottie_file(const std::u8string_view path, image_decode_error& error)
    {
        return load_lottie_file(path, image_decode_options {}, error);
    }

    ui_lottie load_lottie_file(const std::u8string_view path, const image_decode_options& options, image_decode_error& error)
    {
        const sk_sp<SkData> data { read_image_file(path, options, error) };
        if (data == nullptr)
            return {};
        return decode_lottie(data, options, error);
    }

    ui_lottie decode_lottie_bytes(const std::span<const std::uint8_t> bytes, image_decode_error& error)
    {
        return decode_lottie_bytes(bytes, image_decode_options {}, error);
    }

    ui_lottie decode_lottie_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error)
    {
        const sk_sp<SkData> data { borrow_image_bytes(bytes, options, error) };
        if (data == nullptr)
            return {};
        return decode_lottie(data, options, error);
    }
} // namespace luil

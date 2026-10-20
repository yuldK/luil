#include "luil/ui/image_decode.h"

#include "host/font_registry.h"
#include "ui/image_backing.h"
#include "ui/image_source.h"
#include "ui/text_shaping.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/core/SkStream.h"
#include "modules/skresources/include/SkResources.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

// SVG를 켠 구성이다 (`LUIL_ENABLE_SVG`). 이 파일이 Skia SVG 모듈을 끌어온다 — 앱이 SVG 진입점을
// 부르지 않으면 이 파일도, 모듈도 실행 파일에 들어오지 않는다 (docs/vector-image-design.md).
namespace luil {
    namespace {
        using image_input::borrow_image_bytes;
        using image_input::cancelled_error;
        using image_input::decode_cancelled;
        using image_input::make_error;
        using image_input::read_image_file;

        // 한 변의 상한이다. 벡터라 픽셀을 들지 않지만, 크기는 칸 맞추기와 조각의 좌표로
        // `int`에 담긴다. 이보다 큰 SVG는 단위를 잘못 적은 파일이다.
        constexpr float maximum_svg_extent { 1'000'000.0f };

        // 뿌리 `<svg>`의 고유 크기다 (그림 자신의 좌표계 — CSS 픽셀).
        //
        // `width`·`height`가 절대 단위면 그것이다. 퍼센트이거나 없으면(기본값이 100%다)
        // 그 변은 0으로 오고, 그때 `viewBox`의 비율로 채운다 — 아이콘 SVG는 대개 `viewBox`만
        // 적는다. 둘 다 없으면 크기가 없다.
        //  - 컨테이너 크기를 0으로 두고 재는 것이 요점이다. 그래야 퍼센트가 0이 되어 "적지
        //    않았다"와 같은 갈래로 온다.
        [[nodiscard]] SkSize intrinsic_svg_size(const SkSVGSVG& root)
        {
            const SkSVGLengthContext lengths { SkSize::Make(0.0f, 0.0f) };
            SkSize size { root.intrinsicSize(lengths) };
            const std::optional<SkRect>& view_box { root.getViewBox() };
            if (view_box.has_value() == false || view_box->width() <= 0.0f || view_box->height() <= 0.0f)
                return size;
            const float aspect { view_box->width() / view_box->height() };
            if (size.width() <= 0.0f && size.height() > 0.0f)
                size.fWidth = size.height() * aspect;
            else if (size.height() <= 0.0f && size.width() > 0.0f)
                size.fHeight = size.width() / aspect;
            else if (size.width() <= 0.0f && size.height() <= 0.0f)
                size = SkSize::Make(view_box->width(), view_box->height());
            return size;
        }

        [[nodiscard]] bool usable_extent(const float extent) noexcept
        {
            return std::isfinite(extent) && extent > 0.0f && extent <= maximum_svg_extent;
        }

        // 읽은 바이트를 SVG로 세워 한 번 기록한다.
        //
        // DOM이 아니라 기록한 그림(`SkPicture`)을 내준다. DOM은 그릴 때마다 상태를 바꾸지만
        // 그림은 만들어진 뒤 바뀌지 않아 thread 사이로 나눠 그려도 된다 (image_backing.h).
        //  - 밖의 자원은 열지 않는다. `<image href>`의 파일·네트워크 경로는 SVG 파일 하나를
        //    여는 일이 디스크와 네트워크를 건드리는 길이 된다 — 그래서 `data:` URI만 푼다.
        //    그것도 지금 미리 풀어(`kPreDecode`) 그림에 픽셀로 담는다. 그릴 때 풀면 UI
        //    thread가 코덱을 돈다.
        //  - 글자는 시스템 글꼴로 그린다. 모양 잡기는 텍스트 기능을 켠 구성만 HarfBuzz이고,
        //    끈 구성은 모듈의 기본(primitive)이다 (`text_shaping.h`).
        [[nodiscard]] ui_image decode_svg(sk_sp<SkData> data, const image_decode_options& options, image_decode_error& error)
        {
            const sk_sp<SkFontMgr> fonts { platform_font_source().make_uncached_manager() };
            SkSVGDOM::Builder builder {};
            builder.setFontManager(fonts);
            builder.setResourceProvider(skresources::DataURIResourceProviderProxy::Make(nullptr, skresources::ImageDecodeStrategy::kPreDecode, fonts));
            if (sk_sp<SkShapers::Factory> shaping { text_shaping_factory() }; shaping != nullptr)
                builder.setTextShapingFactory(std::move(shaping));

            SkMemoryStream stream { std::move(data) };
            const sk_sp<SkSVGDOM> document { builder.make(stream) };
            if (document == nullptr || document->getRoot() == nullptr)
            {
                error = make_error(image_decode_error_kind::unsupported_format, u8"The data is not an SVG document.");
                return {};
            }
            // XML을 다 푼 자리다. 큰 문서는 여기까지가 시간의 대부분이다.
            if (decode_cancelled(options))
            {
                error = cancelled_error();
                return {};
            }

            const SkSize intrinsic { intrinsic_svg_size(*document->getRoot()) };
            if (usable_extent(intrinsic.width()) == false || usable_extent(intrinsic.height()) == false)
            {
                error = make_error(image_decode_error_kind::no_pixels, u8"The SVG has no usable size: its root needs width and height or a viewBox.");
                return {};
            }

            // 고유 크기를 정수로 옮긴 뒤 상한(`max_width`·`max_height`)을 건다. 벡터라 픽셀을
            // 아끼는 값은 아니고, 그림 자신의 좌표계 크기를 정한다 — 칸 맞추기의 비율과
            // 조각의 좌표가 이 크기다. 기록은 이 크기로 늘이거나 줄여 그린다.
            const int width { static_cast<int>(std::lround(intrinsic.width())) };
            const int height { static_cast<int>(std::lround(intrinsic.height())) };
            const image_pixel_size size { decoded_image_size(width > 0 ? width : 1, height > 0 ? height : 1, options) };

            document->setContainerSize(intrinsic);
            SkPictureRecorder recorder {};
            SkCanvas* const canvas { recorder.beginRecording(SkRect::MakeIWH(size.width, size.height)) };
            canvas->scale(static_cast<float>(size.width) / intrinsic.width(), static_cast<float>(size.height) / intrinsic.height());
            document->render(canvas);
            sk_sp<SkPicture> picture { recorder.finishRecordingAsPicture() };
            ui_image image { image_access::picture_image(std::move(picture), size.width, size.height) };
            if (image.valid() == false)
                error = make_error(image_decode_error_kind::internal_error, u8"Failed to record the SVG.");
            return image;
        }
    } // namespace

    ui_image load_svg_file(const std::u8string_view path, image_decode_error& error)
    {
        return load_svg_file(path, image_decode_options {}, error);
    }

    ui_image load_svg_file(const std::u8string_view path, const image_decode_options& options, image_decode_error& error)
    {
        sk_sp<SkData> data { read_image_file(path, options, error) };
        if (data == nullptr)
            return {};
        return decode_svg(std::move(data), options, error);
    }

    ui_image decode_svg_bytes(const std::span<const std::uint8_t> bytes, image_decode_error& error)
    {
        return decode_svg_bytes(bytes, image_decode_options {}, error);
    }

    ui_image decode_svg_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error)
    {
        sk_sp<SkData> data { borrow_image_bytes(bytes, options, error) };
        if (data == nullptr)
            return {};
        return decode_svg(std::move(data), options, error);
    }
} // namespace luil

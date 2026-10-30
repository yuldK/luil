#include "ui/image_pixels.h"

#include "ui/image_backing.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPicture.h"

namespace luil {
    bool read_image_pixels(const ui_image& image, SkBitmap& pixels, std::u8string& error)
    {
        const image_backing* const backing { image.backing() };
        if (backing == nullptr || image.width() <= 0 || image.height() <= 0)
        {
            error = u8"The image is empty.";
            return false;
        }
        if (pixels.tryAllocPixels(SkImageInfo::MakeN32Premul(image.width(), image.height(), SkColorSpace::MakeSRGB())) == false)
        {
            error = u8"Failed to allocate the pixels to encode.";
            return false;
        }
        // 벡터 그림이다. 그림의 좌표가 곧 `{0, 0, width, height}`라 그대로 그린다.
        if (backing->picture != nullptr)
        {
            pixels.eraseColor(SK_ColorTRANSPARENT);
            SkCanvas canvas { pixels };
            canvas.drawPicture(backing->picture.get());
            return true;
        }
        if (backing->image == nullptr || backing->image->readPixels(nullptr, pixels.pixmap(), 0, 0) == false)
        {
            error = u8"Failed to read the image pixels.";
            return false;
        }
        return true;
    }

    std::vector<std::uint8_t> encoded_bytes(const SkData* const data, std::u8string& error)
    {
        if (data == nullptr)
        {
            error = u8"Failed to encode the image.";
            return {};
        }
        const auto* const begin { static_cast<const std::uint8_t*>(data->data()) };
        return std::vector<std::uint8_t> { begin, begin + data->size() };
    }
}

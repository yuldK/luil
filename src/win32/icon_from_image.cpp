#include "win32/icon_from_image.h"

#include "ui/image_pixels.h"
#include "win32/win32_error.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace luil::win32 {
    namespace {
        // GDI 개체 하나를 쥐고 없어질 때 지운다. 아이콘을 만드는 동안의 중간 개체만 맡는다.
        class owned_bitmap
        {
        public:
            explicit owned_bitmap(const HBITMAP bitmap) noexcept
                : bitmap_ { bitmap }
            {
            }
            owned_bitmap(const owned_bitmap&) = delete;
            owned_bitmap& operator=(const owned_bitmap&) = delete;
            ~owned_bitmap()
            {
                if (bitmap_ != nullptr)
                    static_cast<void>(DeleteObject(bitmap_));
            }

            [[nodiscard]] HBITMAP get() const noexcept
            {
                return bitmap_;
            }

        private:
            HBITMAP bitmap_ { nullptr };
        };
    }

    owned_icon make_icon(const ui_image& image, const int size, std::u8string& error)
    {
        if (size <= 0 || image.valid() == false)
        {
            error = u8"The taskbar icon needs a valid image and a positive size.";
            return {};
        }
        SkBitmap source {};
        if (read_image_pixels(image, source, error) == false)
            return {};

        // 정사각형 안에 비율을 지켜 가운데에 맞춘다. 작은 그림을 크게 늘리면 흐린 것은 막을 수 없다.
        SkBitmap scaled {};
        if (scaled.tryAllocN32Pixels(size, size) == false)
        {
            error = u8"Cannot allocate the pixels of the taskbar icon.";
            return {};
        }
        scaled.eraseColor(SK_ColorTRANSPARENT);
        {
            const float fit { std::min(static_cast<float>(size) / static_cast<float>(source.width()), static_cast<float>(size) / static_cast<float>(source.height())) };
            const float width { static_cast<float>(source.width()) * fit };
            const float height { static_cast<float>(source.height()) * fit };
            const SkRect target { SkRect::MakeXYWH((static_cast<float>(size) - width) / 2.0f, (static_cast<float>(size) - height) / 2.0f, width, height) };
            SkCanvas canvas { scaled };
            SkPaint paint {};
            paint.setAntiAlias(true);
            // 그리기와 같은 표본이다 (src/ui/image_element.cpp) — 오버레이와 창 안의 같은 그림이 같아 보인다.
            canvas.drawImageRect(source.asImage().get(), target, SkSamplingOptions { SkFilterMode::kLinear, SkMipmapMode::kLinear }, &paint);
        }

        BITMAPINFO information {};
        information.bmiHeader.biSize = sizeof(information.bmiHeader);
        information.bmiHeader.biWidth = size;
        // 음수 높이는 위에서 아래로 쌓는 DIB다. Skia 픽셀의 행 순서와 같다.
        information.bmiHeader.biHeight = -size;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* bits { nullptr };
        const HDC screen { GetDC(nullptr) };
        const owned_bitmap color { CreateDIBSection(screen, &information, DIB_RGB_COLORS, &bits, nullptr, 0) };
        if (screen != nullptr)
            ReleaseDC(nullptr, screen);
        if (color.get() == nullptr || bits == nullptr)
        {
            error = make_last_error(u8"Cannot create the color bitmap of the taskbar icon");
            return {};
        }
        // 아이콘의 알파 비트맵은 알파를 곱하지 않은 색을 쥔다 (ICO 파일과 같다). 곱한 색을 주면 알파가 있는 가장자리가
        // 한 번 더 곱해져 어두워진다.
        const SkImageInfo pixel_information { SkImageInfo::Make(size, size, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType) };
        if (scaled.readPixels(pixel_information, bits, static_cast<std::size_t>(size) * 4, 0, 0) == false)
        {
            error = u8"Cannot copy the pixels of the taskbar icon.";
            return {};
        }

        // 마스크는 알파가 0인 자리만 투명(1)으로 둔다. 알파가 하나라도 있는 아이콘은 모양을 알파가 정하고 마스크를 보지
        // 않지만, 알파가 모두 0인 아이콘은 알파 없는 아이콘으로 읽혀 마스크가 모양을 정한다 — 마스크가 0이면 검은 사각형이다.
        //  - 1비트 행은 WORD 정렬이고 왼쪽 픽셀이 바이트의 최상위 비트다.
        const std::size_t mask_stride { static_cast<std::size_t>((size + 15) / 16) * 2 };
        std::vector<std::uint8_t> mask_bits(mask_stride * static_cast<std::size_t>(size), 0);
        const auto* const color_bits { static_cast<const std::uint8_t*>(bits) };
        for (int row = 0; row < size; ++row)
        {
            for (int column = 0; column < size; ++column)
            {
                if (color_bits[(static_cast<std::size_t>(row) * static_cast<std::size_t>(size) + static_cast<std::size_t>(column)) * 4 + 3] == 0)
                    mask_bits[static_cast<std::size_t>(row) * mask_stride + static_cast<std::size_t>(column / 8)] |= static_cast<std::uint8_t>(0x80 >> (column % 8));
            }
        }
        const owned_bitmap mask { CreateBitmap(size, size, 1, 1, mask_bits.data()) };
        if (mask.get() == nullptr)
        {
            error = make_last_error(u8"Cannot create the mask bitmap of the taskbar icon");
            return {};
        }
        ICONINFO icon_information {};
        icon_information.fIcon = TRUE;
        icon_information.hbmMask = mask.get();
        icon_information.hbmColor = color.get();
        owned_icon icon { CreateIconIndirect(&icon_information) };
        if (icon.valid() == false)
            error = make_last_error(u8"Cannot create the taskbar icon");
        return icon;
    }
}

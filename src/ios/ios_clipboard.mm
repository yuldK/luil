#include "ios/ios_clipboard.h"

#include "luil/ui/clipboard_image.h"
#include "macos/macos_text.h"
#include "ui/image_pixels.h"

#include "include/core/SkImageInfo.h"

#import <ImageIO/ImageIO.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace luil::ios {
    namespace {
        [[nodiscard]] UIPasteboard* board()
        {
            return [UIPasteboard generalPasteboard];
        }

        [[nodiscard]] bool png_signature(const std::vector<std::uint8_t>& bytes)
        {
            constexpr std::array<std::uint8_t, 8> signature { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
            return bytes.size() >= signature.size() && std::equal(signature.begin(), signature.end(), bytes.begin());
        }

        // CGImage를 PNG로 짓는다 (ImageIO). 실패하면 nil이다.
        [[nodiscard]] NSData* png_of_image(CGImageRef const picture)
        {
            if (picture == nullptr)
                return nil;
            NSMutableData* const png { [NSMutableData data] };
            bool written { false };
            if (CGImageDestinationRef const destination { CGImageDestinationCreateWithData((__bridge CFMutableDataRef)png, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr) };
                destination != nullptr)
            {
                CGImageDestinationAddImage(destination, picture, nullptr);
                written = CGImageDestinationFinalize(destination);
                CFRelease(destination);
            }
            return written ? png : nil;
        }

        // 그림을 곱하지 않은 RGBA로 펴서 PNG로 짓는다. 창 계층이라 Skia 인코더를 부르지 않는다 (macOS와 같다).
        [[nodiscard]] NSData* encode_png(const ui_image& image, std::u8string& error)
        {
            SkBitmap pixels {};
            if (read_image_pixels(image, pixels, error) == false)
                return nil;
            const int width { pixels.width() };
            const int height { pixels.height() };
            if (width <= 0 || height <= 0)
            {
                error = u8"The clipboard image is empty.";
                return nil;
            }
            const std::size_t stride { static_cast<std::size_t>(width) * 4 };
            NSMutableData* const bytes { [NSMutableData dataWithLength:stride * static_cast<std::size_t>(height)] };
            const SkImageInfo information { SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType) };
            if (pixels.readPixels(information, bytes.mutableBytes, stride, 0, 0) == false)
            {
                error = u8"Cannot copy the pixels of the clipboard image.";
                return nil;
            }
            CGDataProviderRef const provider { CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes) };
            CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
            CGImageRef const picture { CGImageCreate(static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, 32, stride, space,
                kCGBitmapByteOrderDefault | static_cast<CGBitmapInfo>(kCGImageAlphaLast), provider, nullptr, false, kCGRenderingIntentDefault) };
            CGColorSpaceRelease(space);
            CGDataProviderRelease(provider);
            if (picture == nullptr)
            {
                error = u8"Cannot build the clipboard image.";
                return nil;
            }
            NSData* const png { png_of_image(picture) };
            CGImageRelease(picture);
            if (png == nil)
                error = u8"Cannot encode the clipboard image as PNG.";
            return png;
        }

        // PNG가 아닌 그림(JPEG·HEIC·TIFF 등)을 PNG로 옮긴다 (Skia 코덱이 모든 형식을 읽지는 않는다).
        [[nodiscard]] NSData* png_of_data(NSData* const encoded)
        {
            CGImageSourceRef const source { CGImageSourceCreateWithData((__bridge CFDataRef)encoded, nullptr) };
            if (source == nullptr)
                return nil;
            CGImageRef const picture { CGImageSourceCreateImageAtIndex(source, 0, nullptr) };
            CFRelease(source);
            NSData* const png { png_of_image(picture) };
            if (picture != nullptr)
                CGImageRelease(picture);
            return png;
        }
    }

    std::uint32_t clipboard_sequence()
    {
        return static_cast<std::uint32_t>(board().changeCount);
    }

    void write_clipboard_text(const std::u8string& text)
    {
        NSString* const value { macos::make_ns_string(text) };
        board().items = value != nil && value.length > 0 ? @[ @{ UTTypeUTF8PlainText.identifier : value } ] : @[];
    }

    std::u8string read_clipboard_text()
    {
        return macos::from_ns_string(board().string);
    }

    bool write_clipboard_data(const clipboard_data_copy_request& request, std::u8string& error)
    {
        NSData* png { nil };
        if (request.image.valid())
        {
            if (png_signature(request.png))
                png = [NSData dataWithBytes:request.png.data() length:request.png.size()];
            else
                png = encode_png(request.image, error);
            if (png == nil)
                return false;
        }
        NSString* const text { request.text.empty() ? nil : macos::make_ns_string(request.text) };
        NSString* const html { request.html.empty() ? nil : macos::make_ns_string(request.html) };
        if (png == nil && text == nil && html == nil)
            return false;
        // 한 항목에 형식마다 값을 싣는다. 받는 앱이 고른다.
        NSMutableDictionary<NSString*, id>* const item { [NSMutableDictionary dictionary] };
        if (png != nil)
            item[UTTypePNG.identifier] = png;
        if (html != nil)
            item[UTTypeHTML.identifier] = html;
        if (text != nil)
            item[UTTypeUTF8PlainText.identifier] = text;
        board().items = @[ item ];
        return true;
    }

    clipboard_data read_clipboard_data(const std::uint32_t tag)
    {
        UIPasteboard* const source { board() };
        clipboard_data data {};
        data.tag = tag;
        data.sequence = clipboard_sequence();
        data.text = macos::from_ns_string(source.string);
        if (NSData* const html { [source dataForPasteboardType:UTTypeHTML.identifier] }; html != nil)
            data.html = macos::from_ns_string([[NSString alloc] initWithData:html encoding:NSUTF8StringEncoding]);
        NSData* image { [source dataForPasteboardType:UTTypePNG.identifier] };
        if (image == nil && source.hasImages)
        {
            // PNG가 없으면 실린 그림 형식 가운데 첫 것을 옮긴다.
            for (NSString* const type in source.pasteboardTypes)
            {
                UTType* const kind { [UTType typeWithIdentifier:type] };
                if (kind == nil || [kind conformsToType:UTTypeImage] == NO)
                    continue;
                if (NSData* const encoded { [source dataForPasteboardType:type] }; encoded != nil)
                {
                    image = encoded.length <= max_clipboard_image_bytes ? png_of_data(encoded) : encoded;
                    break;
                }
            }
        }
        if (image != nil && image.length > 0)
        {
            data.image_format = clipboard_image_format::encoded;
            if (image.length > max_clipboard_image_bytes)
                data.image_too_large = true;
            else
            {
                const auto* const first { static_cast<const std::uint8_t*>(image.bytes) };
                data.image = std::make_shared<const std::vector<std::uint8_t>>(first, first + image.length);
            }
        }
        return data;
    }

    bool clipboard_has_image()
    {
        return board().hasImages;
    }
}

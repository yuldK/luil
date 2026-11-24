#include "macos/macos_clipboard.h"

#include "luil/ui/clipboard_image.h"
#include "macos/macos_text.h"
#include "ui/image_pixels.h"

#include "include/core/SkImageInfo.h"

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace luil::macos {
    namespace {
        [[nodiscard]] NSPasteboard* board()
        {
            return [NSPasteboard generalPasteboard];
        }

        [[nodiscard]] NSPasteboardType intent_type()
        {
            return [NSString stringWithUTF8String:file_intent_type];
        }

        [[nodiscard]] bool png_signature(const std::vector<std::uint8_t>& bytes)
        {
            constexpr std::array<std::uint8_t, 8> signature { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
            return bytes.size() >= signature.size() && std::equal(signature.begin(), signature.end(), bytes.begin());
        }

        // 그림을 곱하지 않은 RGBA로 펴서 PNG로 짓는다 (ImageIO). 창 계층이라 Skia 인코더를 부르지 않는다.
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
            NSMutableData* const png { [NSMutableData data] };
            bool written { false };
            if (CGImageDestinationRef const destination { CGImageDestinationCreateWithData((__bridge CFMutableDataRef)png, CFSTR("public.png"), 1, nullptr) }; destination != nullptr)
            {
                CGImageDestinationAddImage(destination, picture, nullptr);
                written = CGImageDestinationFinalize(destination);
                CFRelease(destination);
            }
            CGImageRelease(picture);
            if (written == false)
            {
                error = u8"Cannot encode the clipboard image as PNG.";
                return nil;
            }
            return png;
        }

        // PNG를 TIFF로 옮긴다. TIFF만 읽는 옛 앱을 위해서다. 실패하면 nil이고 PNG만 남는다.
        [[nodiscard]] NSData* tiff_of(NSData* const png)
        {
            NSBitmapImageRep* const representation { [NSBitmapImageRep imageRepWithData:png] };
            return representation != nil ? representation.TIFFRepresentation : nil;
        }

        // TIFF를 PNG로 옮긴다 (Skia 코덱에는 TIFF가 없다).
        [[nodiscard]] NSData* png_of(NSData* const tiff)
        {
            NSBitmapImageRep* const representation { [NSBitmapImageRep imageRepWithData:tiff] };
            return representation != nil ? [representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}] : nil;
        }

        [[nodiscard]] bool absolute(const std::u8string& path)
        {
            return path.empty() == false && path.front() == u8'/';
        }
    }

    std::uint32_t clipboard_sequence()
    {
        return static_cast<std::uint32_t>(board().changeCount);
    }

    bool write_clipboard_files(const clipboard_files_copy_request& request)
    {
        if (request.expected_sequence.has_value() && *request.expected_sequence != clipboard_sequence())
            return false;
        if (std::ranges::all_of(request.paths, absolute) == false)
            return false;
        NSPasteboard* const target { board() };
        [target clearContents];
        if (request.paths.empty())
            return true;
        // 경로마다 항목 하나다 (Finder가 여러 파일을 싣는 꼴). 잘라내기의 뜻은 첫 항목에만 싣는다 — 없으면 받는 쪽이 복사로 본다.
        NSMutableArray<NSPasteboardItem*>* const items { [NSMutableArray arrayWithCapacity:request.paths.size()] };
        for (const std::u8string& path : request.paths)
        {
            NSString* const text { make_ns_string(path) };
            if (text == nil)
                return false;
            NSPasteboardItem* const item { [NSPasteboardItem new] };
            [item setString:[NSURL fileURLWithPath:text].absoluteString forType:NSPasteboardTypeFileURL];
            if (items.count == 0 && request.intent == file_clipboard_intent::cut)
                [item setString:@"cut" forType:intent_type()];
            [items addObject:item];
        }
        return [target writeObjects:items];
    }

    file_clipboard_content read_clipboard_files(const std::uint32_t tag)
    {
        NSPasteboard* const source { board() };
        file_clipboard_content content {};
        content.tag = tag;
        content.sequence = clipboard_sequence();
        NSArray<NSURL*>* const urls { [source readObjectsForClasses:@[ [NSURL class] ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }] };
        for (NSURL* const url in urls)
            if (url.fileURL && url.path != nil)
                content.paths.push_back(from_ns_string(url.path));
        if (content.paths.empty() == false && [[source stringForType:intent_type()] isEqualToString:@"cut"])
            content.intent = file_clipboard_intent::cut;
        return content;
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
        NSString* const text { request.text.empty() ? nil : make_ns_string(request.text) };
        NSString* const html { request.html.empty() ? nil : make_ns_string(request.html) };
        if (png == nil && text == nil && html == nil)
            return false;
        NSPasteboard* const target { board() };
        [target clearContents];
        // 받는 앱이 그림을 먼저, 서식 있는 글을 평문보다 먼저 본다 (Win32와 같은 순서).
        NSPasteboardItem* const item { [NSPasteboardItem new] };
        if (png != nil)
        {
            [item setData:png forType:NSPasteboardTypePNG];
            if (NSData* const tiff { tiff_of(png) }; tiff != nil)
                [item setData:tiff forType:NSPasteboardTypeTIFF];
        }
        if (html != nil)
            [item setString:html forType:NSPasteboardTypeHTML];
        if (text != nil)
            [item setString:text forType:NSPasteboardTypeString];
        return [target writeObjects:@[ item ]];
    }

    clipboard_data read_clipboard_data(const std::uint32_t tag)
    {
        NSPasteboard* const source { board() };
        clipboard_data data {};
        data.tag = tag;
        data.sequence = clipboard_sequence();
        data.text = from_ns_string([source stringForType:NSPasteboardTypeString]);
        data.html = from_ns_string([source stringForType:NSPasteboardTypeHTML]);
        NSData* image { [source dataForType:NSPasteboardTypePNG] };
        if (image == nil)
            if (NSData* const tiff { [source dataForType:NSPasteboardTypeTIFF] }; tiff != nil)
                image = tiff.length <= max_clipboard_image_bytes ? png_of(tiff) : tiff;
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

    bool clipboard_has_files()
    {
        return [board() canReadObjectForClasses:@[ [NSURL class] ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
    }

    bool clipboard_has_image()
    {
        return [board() availableTypeFromArray:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ]] != nil;
    }
}

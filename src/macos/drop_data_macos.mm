#include "luil/win32/drop_data.h"

#include "macos/macos_text.h"

#import <AppKit/AppKit.h>

#include <cstring>
#include <string>

// OS 끌어 놓기의 원시 데이터(`ui_action_context::native_drop_data`)를 읽는 도우미의 macOS 갈래다 (os-dragdrop-design.md).
//  - macOS에서 원시 데이터는 끌기의 `NSPasteboard*`다. `on_drop` 안의 메인 thread에서만 읽는다.
//  - 형식 이름은 붙여넣기 판의 형식(UTI, `drag_payload::formats`에 실린 것)이다. 여러 플랫폼에서 같은 코드가 돌도록 흔한 Win32
//    이름도 받아 같은 뜻의 형식으로 옮긴다: `CF_UNICODETEXT`·`CF_TEXT` → 글, `UniformResourceLocatorW`·`UniformResourceLocator`
//    → 주소, `HTML Format` → HTML, `PNG` → PNG.
//  - 가상 파일은 파일 약속(`NSFilePromiseReceiver`)이다. 이름만 읽는다. 내용은 약속을 받아 폴더에 쓰는 비동기 일이라
//    `FileContents`를 바이트로 읽지 못한다 (빈 값이다).
namespace luil::win32 {
    namespace {
        [[nodiscard]] NSPasteboard* pasteboard_of(void* const data)
        {
            return data != nullptr ? (__bridge NSPasteboard*)data : nil;
        }

        [[nodiscard]] NSPasteboardType type_of(const std::u8string_view format)
        {
            if (format == u8"CF_UNICODETEXT" || format == u8"CF_TEXT" || format == u8"CF_OEMTEXT")
                return NSPasteboardTypeString;
            if (format == u8"UniformResourceLocatorW" || format == u8"UniformResourceLocator")
                return NSPasteboardTypeURL;
            if (format == u8"HTML Format")
                return NSPasteboardTypeHTML;
            if (format == u8"PNG")
                return NSPasteboardTypePNG;
            return luil::macos::make_ns_string(format);
        }
    }

    std::vector<dropped_virtual_file> drop_virtual_files(void* const data)
    {
        std::vector<dropped_virtual_file> files {};
        NSPasteboard* const board { pasteboard_of(data) };
        if (board == nil)
            return files;
        NSArray* const receivers { [board readObjectsForClasses:@[ [NSFilePromiseReceiver class] ] options:@{}] };
        for (NSFilePromiseReceiver* const receiver in receivers)
            for (NSString* const name in receiver.fileNames)
            {
                dropped_virtual_file file {};
                file.name = luil::macos::from_ns_string(name);
                files.push_back(std::move(file));
            }
        return files;
    }

    std::optional<std::vector<std::byte>> drop_data_bytes(void* const data, const std::u8string_view format, const int index, const std::size_t limit)
    {
        NSPasteboard* const board { pasteboard_of(data) };
        NSPasteboardType const type { type_of(format) };
        // 파일 약속의 내용은 바이트로 오지 않는다. 다른 형식은 첫 항목의 것이다 (`index`는 가상 파일의 색인이다).
        if (board == nil || type == nil || index >= 0)
            return std::nullopt;
        NSData* const bytes { [board dataForType:type] };
        if (bytes == nil || bytes.length > limit)
            return std::nullopt;
        std::vector<std::byte> result(bytes.length);
        if (bytes.length > 0)
            std::memcpy(result.data(), bytes.bytes, bytes.length);
        return result;
    }

    std::optional<std::u8string> drop_data_text(void* const data, const std::u8string_view format, const std::size_t limit)
    {
        NSPasteboard* const board { pasteboard_of(data) };
        NSPasteboardType const type { type_of(format) };
        if (board == nil || type == nil)
            return std::nullopt;
        NSString* const text { [board stringForType:type] };
        if (text == nil)
            return std::nullopt;
        std::u8string result { luil::macos::from_ns_string(text) };
        // 첫 NUL에서 끝난다 (Win32 도우미와 같다).
        if (const std::size_t end { result.find(u8'\0') }; end != std::u8string::npos)
            result.resize(end);
        if (result.size() > limit)
            return std::nullopt;
        return result;
    }
}

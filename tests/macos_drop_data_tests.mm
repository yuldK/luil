#include "luil/win32/drop_data.h"

#include <catch2/catch_test_macros.hpp>

#import <AppKit/AppKit.h>

// macOS의 끌어 놓기 원시 데이터는 끌기의 붙여넣기 판이다. 도우미가 그 판의 형식을 읽고 흔한 Win32 이름을 옮기는지 본다.
TEST_CASE("macOS drop data helpers read the drag pasteboard and map Win32 format names", "[macos][dragdrop]")
{
    NSPasteboard* const board { [NSPasteboard pasteboardWithUniqueName] };
    [board clearContents];
    NSPasteboardItem* const item { [NSPasteboardItem new] };
    [item setString:@"https://example.com/a" forType:NSPasteboardTypeURL];
    [item setString:@"plain text" forType:NSPasteboardTypeString];
    [item setData:[NSData dataWithBytes:"\x89PNG" length:4] forType:NSPasteboardTypePNG];
    REQUIRE([board writeObjects:@[ item ]]);
    void* const data { (__bridge void*)board };

    REQUIRE(luil::win32::drop_data_text(data, u8"UniformResourceLocatorW") == u8"https://example.com/a");
    REQUIRE(luil::win32::drop_data_text(data, u8"public.url") == u8"https://example.com/a");
    REQUIRE(luil::win32::drop_data_text(data, u8"CF_UNICODETEXT") == u8"plain text");
    const auto png { luil::win32::drop_data_bytes(data, u8"PNG") };
    REQUIRE(png.has_value());
    REQUIRE(png->size() == 4);
    // 상한을 넘으면 잘린 값을 주지 않는다.
    REQUIRE(luil::win32::drop_data_bytes(data, u8"PNG", -1, 3).has_value() == false);
    // 가상 파일의 내용(색인이 있는 읽기)은 바이트로 오지 않는다.
    REQUIRE(luil::win32::drop_data_bytes(data, u8"FileContents", 0).has_value() == false);
    REQUIRE(luil::win32::drop_virtual_files(data).empty());
    REQUIRE(luil::win32::drop_data_text(nullptr, u8"CF_UNICODETEXT").has_value() == false);
    [board releaseGlobally];
}

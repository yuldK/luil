#include "win32/toast_payload.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// 토스트 XML을 OS 없이 본다 (os-notification-design.md의 "토스트 XML"). 짓기만 하고 토스트를 올리지 않는다.

TEST_CASE("Toast text escapes the five XML characters and drops forbidden controls", "[win32][notification][toast]")
{
    REQUIRE(luil::win32::escape_toast_text(u8"a & b < c > d \" e ' f") == u8"a &amp; b &lt; c &gt; d &quot; e &apos; f");
    // 탭·줄바꿈·복귀는 남고, 그 밖의 C0 제어 문자와 U+FFFE·U+FFFF는 빠진다.
    const std::u8string controls { u8"a\tb\nc\rd" + std::u8string { char8_t { 0x01 } } + u8"e" + std::u8string { char8_t { 0x1F } } + u8"f" };
    REQUIRE(luil::win32::escape_toast_text(controls) == u8"a\tb\nc\rdef");
    REQUIRE(luil::win32::escape_toast_text(u8"x\uFFFEy\uFFFFz") == u8"xyz");
    // 여러 바이트 글자는 그대로다.
    REQUIRE(luil::win32::escape_toast_text(u8"\uC54C\uB9BC \U0001F600") == u8"\uC54C\uB9BC \U0001F600");
}

TEST_CASE("The launch argument carries only the id", "[win32][notification][toast]")
{
    REQUIRE(luil::win32::toast_launch_argument(u8"demo-1") == u8"luil:demo-1");
    REQUIRE(luil::win32::toast_id_from_launch_argument(u8"luil:demo-1") == std::u8string { u8"demo-1" });
    REQUIRE(luil::win32::toast_id_from_launch_argument(u8"demo-1").has_value() == false);
    REQUIRE(luil::win32::toast_id_from_launch_argument(u8"luil:").has_value() == false);
    REQUIRE(luil::win32::toast_id_from_launch_argument(u8"luil:two words").has_value() == false);
    REQUIRE(luil::win32::toast_id_from_launch_argument(u8"luil:" + std::u8string(65, u8'x')).has_value() == false);

    const std::u8string xml { luil::win32::make_toast_xml({ .id = u8"demo-1", .title = u8"Title & more", .body = u8"<body>" }) };
    REQUIRE(xml.starts_with(u8"<toast launch=\"luil:demo-1\">"));
    // 제목과 본문은 `launch`에 가지 않고 이스케이프되어 `<text>` 안에 있다.
    REQUIRE(xml.find(u8"<text>Title &amp; more</text><text>&lt;body&gt;</text>") != std::u8string::npos);
    REQUIRE(xml.find(u8"template=\"ToastGeneric\"") != std::u8string::npos);
    REQUIRE(xml.find(u8"<audio") == std::u8string::npos);
    REQUIRE(xml.find(u8"<image") == std::u8string::npos);
    REQUIRE(xml.ends_with(u8"</toast>"));
}

TEST_CASE("A silent toast carries a silent audio element and an empty body has no second text", "[win32][notification][toast]")
{
    const std::u8string xml { luil::win32::make_toast_xml({ .id = u8"quiet", .title = u8"Only a title", .silent = true }) };
    REQUIRE(xml.find(u8"<audio silent=\"true\"/>") != std::u8string::npos);
    REQUIRE(xml.find(u8"<text>Only a title</text></binding>") != std::u8string::npos);
}

TEST_CASE("The image is a file URL with spaces, hashes and percents encoded", "[win32][notification][toast]")
{
    REQUIRE(luil::win32::toast_image_url(u8"C:\\Users\\a b\\#1 100%.png") == u8"file:///C:/Users/a%20b/%231%20100%25.png");
    // 다른 글자(한글 포함)는 그대로 둔다. URL 해석은 OS가 UTF-8로 한다.
    REQUIRE(luil::win32::toast_image_url(u8"D:\\\uADF8\uB9BC.png") == u8"file:///D:/\uADF8\uB9BC.png");

    const std::u8string xml { luil::win32::make_toast_xml({ .id = u8"picture", .title = u8"t", .image_path = u8"C:\\tmp\\a&b.png" }) };
    REQUIRE(xml.find(u8"<image placement=\"appLogoOverride\" src=\"file:///C:/tmp/a&amp;b.png\"/>") != std::u8string::npos);
}

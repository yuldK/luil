#include "luil/text/line_break.h"
#include "luil/text/utf8_text.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using luil::text::line_break_policy;

    std::vector<std::size_t> offsets_of(const std::u8string_view text, const line_break_policy policy = line_break_policy::hangul_words)
    {
        std::vector<std::size_t> offsets {};
        for (const auto& opportunity : luil::text::line_break_opportunities(text, policy))
            offsets.push_back(opportunity.offset);
        return offsets;
    }

    std::vector<std::size_t> offsets_unicode(const std::u8string_view text)
    {
        return offsets_of(text, line_break_policy::unicode);
    }

    // 빈 값과 없는 변수를 같게 본다. MSVC는 `std::getenv`를 경고로 막아 `_dupenv_s`로 읽는다.
    std::string environment_value(const char* const name)
    {
#if defined(_MSC_VER)
        char* buffer { nullptr };
        std::size_t size { 0 };
        if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
            return {};
        std::string value { buffer };
        std::free(buffer);
        return value;
#else
        const char* const value { std::getenv(name) };
        return value != nullptr ? std::string { value } : std::string {};
#endif
    }
}

TEST_CASE("Line breaks follow hyphens and slashes but not closing punctuation", "[domain][line-break]")
{
    // 하이픈 뒤에서 바꾼다. 숫자 앞의 하이픈(`-5`, `123-456`)은 LB25가 붙여 둔다.
    REQUIRE(offsets_of(u8"alpha-beta-gamma") == std::vector<std::size_t> { 6, 11, 16 });
    REQUIRE(offsets_of(u8"ABC-123-456") == std::vector<std::size_t> { 11 });
    // 슬래시 뒤에서 바꾸고 `:`·`.` 뒤의 글자는 붙인다 (LB13·LB29).
    REQUIRE(offsets_of(u8"https://example.com/a/b") == std::vector<std::size_t> { 8, 20, 22, 23 });
    // 닫는 부호 앞과 여는 부호 뒤에서는 바꾸지 않는다.
    REQUIRE(offsets_of(u8"(a) b") == std::vector<std::size_t> { 4, 5 });
    REQUIRE(offsets_of(u8"「東京」。") == std::vector<std::size_t> { 6, 15 });
    REQUIRE(offsets_of(u8"東京。大阪") == std::vector<std::size_t> { 3, 9, 12, 15 });
}

TEST_CASE("Line breaks keep glued spaces and numbers together", "[domain][line-break]")
{
    // NBSP(GL)·WJ는 붙이고 ZW 뒤에서는 바꾼다.
    REQUIRE(offsets_of(u8"100 km") == std::vector<std::size_t> { 7 });
    REQUIRE(offsets_of(u8"a⁠b") == std::vector<std::size_t> { 5 });
    REQUIRE(offsets_of(u8"a​b") == std::vector<std::size_t> { 4, 5 });
    // LB25: 숫자 표현 안은 붙인다.
    REQUIRE(offsets_of(u8"1,234.5") == std::vector<std::size_t> { 7 });
    REQUIRE(offsets_of(u8"$100 (12)%") == std::vector<std::size_t> { 5, 10 });
}

TEST_CASE("Line breaks keep emoji, flags, small kana and marks whole", "[domain][line-break]")
{
    // 작은 가나(CJ)는 NS라 앞에서 바꾸지 않는다.
    REQUIRE(offsets_of(u8"カァカ") == std::vector<std::size_t> { 6, 9 });
    // 피부색 수식(LB30b)과 국기 쌍(LB30a)은 한 덩어리다.
    REQUIRE(offsets_of(u8"\U0001F44D\U0001F3FD\U0001F44D") == std::vector<std::size_t> { 8, 12 });
    REQUIRE(offsets_of(u8"\U0001F1F0\U0001F1F7\U0001F1EF\U0001F1F5") == std::vector<std::size_t> { 8, 16 });
    // 결합 부호는 앞 글자에 붙는다 (LB9).
    REQUIRE(offsets_of(u8"é x") == std::vector<std::size_t> { 4, 5 });
    // 공백 뒤 결합 부호는 AL이다 (LB10). 공백 뒤에서 바꾼다.
    REQUIRE(offsets_of(u8"a ́b") == std::vector<std::size_t> { 2, 5 });
}

TEST_CASE("Hangul words keep syllables together unless the unicode policy is chosen", "[domain][line-break]")
{
    REQUIRE(offsets_of(u8"한국어 문장") == std::vector<std::size_t> { 10, 16 });
    REQUIRE(offsets_unicode(u8"한국어 문장") == std::vector<std::size_t> { 3, 6, 10, 13, 16 });
    // 한자·가나는 두 정책에서 같다.
    REQUIRE(offsets_of(u8"漢字かな") == std::vector<std::size_t> { 3, 6, 9, 12 });
    REQUIRE(offsets_unicode(u8"漢字かな") == offsets_of(u8"漢字かな"));
    // 하이픈 뒤는 한국어에서도 바꾼다. 한글과 라틴·숫자 사이는 붙인다.
    REQUIRE(offsets_of(u8"서울-부산") == std::vector<std::size_t> { 7, 13 });
    REQUIRE(offsets_of(u8"v2버전") == std::vector<std::size_t> { 8 });
    // 조합형 한글(자모)도 같은 정책을 따른다.
    REQUIRE(offsets_of(u8"가가") == std::vector<std::size_t> { 12 });
    REQUIRE(offsets_unicode(u8"가가") == std::vector<std::size_t> { 6, 12 });
}

TEST_CASE("Mandatory breaks are reported but text end is always last", "[domain][line-break]")
{
    const auto breaks { luil::text::line_break_opportunities(u8"a\nb c") };
    REQUIRE(breaks == std::vector<luil::text::line_break_opportunity> { { 2, true }, { 6, true }, { 7, true } });
    REQUIRE(luil::text::line_break_opportunities(u8"a b") == std::vector<luil::text::line_break_opportunity> { { 2, false }, { 3, true } });
    REQUIRE(luil::text::line_break_opportunities(u8"\r\n") == std::vector<luil::text::line_break_opportunity> { { 2, true } });
}

TEST_CASE("Line breaks stay inside broken UTF-8 and empty text", "[domain][line-break]")
{
    REQUIRE(luil::text::line_break_opportunities(u8"").empty());
    std::u8string broken { u8"ab" };
    broken.push_back(static_cast<char8_t>(0xE3));
    broken.push_back(static_cast<char8_t>(0x81));
    broken += u8" c";
    broken.push_back(static_cast<char8_t>(0xFF));
    const auto breaks { offsets_of(broken) };
    REQUIRE(breaks.empty() == false);
    REQUIRE(breaks.back() == broken.size());
    for (const auto offset : breaks)
        REQUIRE(offset <= broken.size());
}

TEST_CASE("The ASCII fast path matches the decoding path", "[domain][line-break]")
{
    // 끝에 ASCII 밖의 글자를 붙이면 해독 길을 탄다. 앞부분의 자리가 같아야 한다.
    constexpr std::u8string_view sample { u8"say \"hi\" (now)! a-b, c/d 1,000.5% $3 x_y" };
    const auto fast { offsets_of(sample) };
    const std::u8string slow_text { std::u8string { sample } + u8" é" };
    auto slow { offsets_of(slow_text) };
    slow.resize(fast.size() - 1);
    std::vector<std::size_t> expected { fast };
    expected.pop_back();
    REQUIRE(slow == expected);
}

TEST_CASE("Hanging spaces are the wide non-glue space separators", "[domain][line-break]")
{
    REQUIRE(luil::text::line_break_hangs(U' '));
    REQUIRE(luil::text::line_break_hangs(U'　'));
    REQUIRE(luil::text::line_break_hangs(U' '));
    REQUIRE(luil::text::line_break_hangs(U' ') == false);
    REQUIRE(luil::text::line_break_hangs(U' ') == false);
    REQUIRE(luil::text::line_break_hangs(U' ') == false);
    REQUIRE(luil::text::line_break_hangs(U'​') == false);
    REQUIRE(luil::text::line_break_hangs(U'\t') == false);
    REQUIRE(luil::text::line_break_hangs(U'a') == false);
}

TEST_CASE("The unicode policy passes LineBreakTest.txt from LUIL_UCD_DIRECTORY", "[domain][line-break][ucd]")
{
    // 원본은 저장소에 넣지 않는다. 변수가 가리키는 UCD 압축 배치에서 읽는다.
    const std::string directory { environment_value("LUIL_UCD_DIRECTORY") };
    if (directory.empty())
        SKIP("LUIL_UCD_DIRECTORY is not set.");
    const std::filesystem::path path { std::filesystem::path { directory } / "auxiliary" / "LineBreakTest.txt" };
    std::ifstream file { path, std::ios::binary };
    REQUIRE(file.good());
    std::string line {};
    REQUIRE(std::getline(file, line));
    if (line.empty() == false && line.back() == '\r')
        line.pop_back();
    REQUIRE(line == "# LineBreakTest-16.0.0.txt");
    std::size_t number { 1 };
    std::size_t cases { 0 };
    std::size_t failures { 0 };
    std::string report {};
    while (std::getline(file, line))
    {
        ++number;
        const auto hash { line.find('#') };
        const std::string body { line.substr(0, hash) };
        std::istringstream tokens { body };
        std::u8string text {};
        std::vector<std::size_t> expected {};
        std::string token {};
        bool first { true };
        while (tokens >> token)
        {
            // `÷`·`×`는 UTF-8 두 byte다. 첫 자리(늘 ×)는 0이라 넣지 않는다.
            if (token == "\xC3\xB7")
            {
                if (first == false)
                    expected.push_back(text.size());
            }
            else if (token != "\xC3\x97")
                text += luil::text::utf8_encode(static_cast<char32_t>(std::stoul(token, nullptr, 16)));
            first = false;
        }
        if (text.empty())
            continue;
        ++cases;
        const auto actual { offsets_unicode(text) };
        if (actual != expected)
        {
            ++failures;
            if (failures <= 30)
            {
                report += std::to_string(number) + ": " + body + " got";
                for (const auto offset : actual)
                    report += " " + std::to_string(offset);
                report += " #" + (hash == std::string::npos ? std::string {} : line.substr(hash + 1, 400)) + "\n";
            }
        }
    }
    INFO(report);
    CAPTURE(cases);
    REQUIRE(cases > 10000);
    REQUIRE(failures == 0);
}

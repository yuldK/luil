#include "win32/instance_message.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// 둘째 인스턴스가 첫째에 보내는 WM_COPYDATA의 운반 형식을 OS 호출 없이 본다 (single-instance-args-design.md).

namespace {
    using luil::win32::instance_message_error;

    [[nodiscard]] std::span<const std::byte> bytes_of(const std::wstring& units) noexcept
    {
        return std::as_bytes(std::span<const wchar_t> { units });
    }

    // 짓고 바로 읽는다. 짓기가 실패하면 test가 멈춘다.
    [[nodiscard]] luil::win32::instance_decode_result round_trip(const std::vector<std::u8string>& arguments, const std::u8string& working_directory, const bool activate)
    {
        const luil::win32::instance_encode_result encoded { luil::win32::encode_instance_message(arguments, working_directory, activate) };
        REQUIRE(encoded.ok());
        return luil::win32::decode_instance_message(luil::win32::instance_message_tag, bytes_of(encoded.units));
    }

    [[nodiscard]] luil::win32::instance_decode_result decode(const std::wstring& units)
    {
        return luil::win32::decode_instance_message(luil::win32::instance_message_tag, bytes_of(units));
    }

    // 깨진 UTF-8 글이다 (연속 바이트가 모자란 선두 바이트).
    [[nodiscard]] std::u8string broken_utf8()
    {
        return std::u8string { static_cast<char8_t>(0xC3), u8'(' };
    }
}

TEST_CASE("Arguments and the working directory survive a round trip", "[win32][instance]")
{
    const std::vector<std::u8string> arguments {
        u8"a.txt",
        u8"b c",
        u8"\"quoted\" and \\ back",
        u8"한글 인자.txt",
        // BMP 밖 글자는 surrogate 쌍으로 간다.
        u8"emoji \U0001F600 end",
        u8"",
        u8"--flag=value",
    };
    const auto result { round_trip(arguments, u8"C:\\작업 폴더\\sub", false) };
    REQUIRE(result.ok());
    REQUIRE(result.value.arguments == arguments);
    REQUIRE(result.value.working_directory == u8"C:\\작업 폴더\\sub");
    REQUIRE(result.activate == false);
}

TEST_CASE("An empty list, an empty argument and an empty folder are told apart", "[win32][instance]")
{
    const auto none { round_trip({}, u8"C:\\work", false) };
    REQUIRE(none.ok());
    REQUIRE(none.value.arguments.empty());
    REQUIRE(none.value.working_directory == u8"C:\\work");

    // 인자 하나가 빈 글인 목록은 인자가 없는 목록과 다르다.
    const auto one_empty { round_trip({ u8"" }, u8"C:\\work", false) };
    REQUIRE(one_empty.ok());
    REQUIRE(one_empty.value.arguments.size() == 1);
    REQUIRE(one_empty.value.arguments.front().empty());

    const auto no_folder { round_trip({ u8"x" }, u8"", false) };
    REQUIRE(no_folder.ok());
    REQUIRE(no_folder.value.working_directory.empty());
    REQUIRE(no_folder.value.arguments.size() == 1);
}

TEST_CASE("The activate flag is the first bit of the first code unit", "[win32][instance]")
{
    const std::vector<std::u8string> arguments { u8"x" };
    const auto on { luil::win32::encode_instance_message(arguments, u8"w", true) };
    const auto off { luil::win32::encode_instance_message(arguments, u8"w", false) };
    REQUIRE(on.ok());
    REQUIRE(off.ok());
    REQUIRE(on.units.front() == luil::win32::instance_flag_activate);
    REQUIRE(off.units.front() == L'\0');
    // 작업 폴더는 표지 바로 뒤에 NUL로 끝나고, 인자가 이어진다.
    REQUIRE(on.units == std::wstring { L"\u0001w\0x\0", 5 });

    REQUIRE(decode(on.units).activate);
    REQUIRE(decode(off.units).activate == false);
}

TEST_CASE("The size and the count limits hold on both sides", "[win32][instance]")
{
    // 표지 하나, 작업 폴더의 NUL 하나, 인자의 NUL 하나를 뺀 만큼이 한 인자의 길이다.
    constexpr std::size_t units_limit { luil::win32::instance_message_max_bytes / sizeof(wchar_t) };
    const std::u8string fills_exactly(units_limit - 3, u8'a');
    const std::u8string one_over(units_limit - 2, u8'a');

    const auto exact { luil::win32::encode_instance_message(std::vector<std::u8string> { fills_exactly }, u8"", false) };
    REQUIRE(exact.ok());
    REQUIRE(exact.units.size() * sizeof(wchar_t) == luil::win32::instance_message_max_bytes);
    REQUIRE(decode(exact.units).ok());

    const auto over { luil::win32::encode_instance_message(std::vector<std::u8string> { one_over }, u8"", false) };
    REQUIRE(over.ok() == false);
    REQUIRE(over.error == instance_message_error::too_large);
    REQUIRE(over.units.empty());
    // 받는 쪽도 같은 상한으로 거절한다. 짓는 쪽을 거치지 않은 바이트가 와도 그렇다.
    std::wstring too_big(units_limit + 1, L'a');
    too_big.front() = L'\0';
    too_big[1] = L'\0';
    too_big.back() = L'\0';
    REQUIRE(decode(too_big).error == instance_message_error::too_large);

    // 작업 폴더도 같은 크기에 든다. 인자가 없으면 표지와 폴더의 NUL 둘을 뺀 만큼까지 된다.
    REQUIRE(luil::win32::encode_instance_message({}, std::u8string(units_limit - 2, u8'a'), false).ok());
    REQUIRE(luil::win32::encode_instance_message({}, std::u8string(units_limit - 1, u8'a'), false).error == instance_message_error::too_large);

    const std::vector<std::u8string> most(luil::win32::instance_message_max_arguments, u8"a");
    const auto full { round_trip(most, u8"w", false) };
    REQUIRE(full.ok());
    REQUIRE(full.value.arguments.size() == luil::win32::instance_message_max_arguments);

    const std::vector<std::u8string> too_many(luil::win32::instance_message_max_arguments + 1, u8"a");
    REQUIRE(luil::win32::encode_instance_message(too_many, u8"w", false).error == instance_message_error::too_many_arguments);
    // 받는 쪽: 4097개를 손으로 지어 보낸다.
    std::wstring units { L"\0w\0", 3 };
    for (std::size_t index = 0; index < luil::win32::instance_message_max_arguments + 1; ++index)
        units += std::wstring { L"a\0", 2 };
    REQUIRE(decode(units).error == instance_message_error::too_many_arguments);
}

TEST_CASE("Text that cannot travel is refused before anything is sent", "[win32][instance]")
{
    const std::u8string with_nul { u8'a', u8'\0', u8'b' };
    REQUIRE(luil::win32::encode_instance_message(std::vector<std::u8string> { with_nul }, u8"w", false).error == instance_message_error::embedded_nul);
    REQUIRE(luil::win32::encode_instance_message(std::vector<std::u8string> { u8"ok", with_nul }, u8"w", false).error == instance_message_error::embedded_nul);
    REQUIRE(luil::win32::encode_instance_message({}, with_nul, false).error == instance_message_error::embedded_nul);

    REQUIRE(luil::win32::encode_instance_message(std::vector<std::u8string> { broken_utf8() }, u8"w", false).error == instance_message_error::invalid_text);
    REQUIRE(luil::win32::encode_instance_message({}, broken_utf8(), false).error == instance_message_error::invalid_text);
    for (const auto error : { instance_message_error::embedded_nul, instance_message_error::invalid_text, instance_message_error::too_large })
        REQUIRE(luil::win32::instance_message_error_text(error).empty() == false);
    REQUIRE(luil::win32::instance_message_error_text(instance_message_error::none).empty());
}

TEST_CASE("A message with a broken shape is refused", "[win32][instance]")
{
    // 홀수 바이트
    const std::vector<std::byte> odd(5, std::byte { 0 });
    REQUIRE(luil::win32::decode_instance_message(luil::win32::instance_message_tag, odd).error == instance_message_error::misaligned);

    // 빈 바이트열과 표지만 있는 바이트열
    REQUIRE(luil::win32::decode_instance_message(luil::win32::instance_message_tag, {}).error == instance_message_error::too_short);
    REQUIRE(decode(std::wstring { L"\0", 1 }).error == instance_message_error::too_short);

    // 작업 폴더가 NUL로 끝나지 않는다.
    REQUIRE(decode(std::wstring { L"\0abc", 4 }).error == instance_message_error::missing_terminator);
    // 마지막 인자가 NUL로 끝나지 않는다.
    REQUIRE(decode(std::wstring { L"\0w\0abc", 6 }).error == instance_message_error::missing_terminator);

    // 짝 없는 surrogate는 작업 폴더에서도 인자에서도 거절한다.
    REQUIRE(decode(std::wstring { L"\0w\xD800\0", 4 }).error == instance_message_error::invalid_text);
    REQUIRE(decode(std::wstring { L"\0\xD800\0", 3 }).error == instance_message_error::invalid_text);
    REQUIRE(decode(std::wstring { L"\0w\0\xDC00x\0", 6 }).error == instance_message_error::invalid_text);

    // 모르는 표지 비트
    REQUIRE(decode(std::wstring { L"\x0002w\0", 3 }).error == instance_message_error::unknown_flags);
    REQUIRE(decode(std::wstring { L"\x0003w\0", 3 }).error == instance_message_error::unknown_flags);
    REQUIRE(decode(std::wstring { L"\x0001w\0", 3 }).ok());
}

TEST_CASE("Only the tag of this format version is read", "[win32][instance]")
{
    const auto encoded { luil::win32::encode_instance_message(std::vector<std::u8string> { u8"x" }, u8"w", false) };
    REQUIRE(encoded.ok());
    REQUIRE(luil::win32::decode_instance_message(luil::win32::instance_message_tag, bytes_of(encoded.units)).ok());
    REQUIRE(luil::win32::decode_instance_message(luil::win32::instance_message_tag + 1, bytes_of(encoded.units)).error == instance_message_error::wrong_tag);
    REQUIRE(luil::win32::decode_instance_message(luil::win32::instance_message_tag - 1, bytes_of(encoded.units)).error == instance_message_error::wrong_tag);
    REQUIRE(luil::win32::decode_instance_message(0, bytes_of(encoded.units)).error == instance_message_error::wrong_tag);
    // 판은 값에 담긴다.
    REQUIRE((luil::win32::instance_message_tag & 0xFFu) == luil::win32::instance_message_version);
}

TEST_CASE("The window property is named after the instance and bounded at 241 code units", "[win32][instance]")
{
    using luil::win32::instance_property_name;

    REQUIRE(instance_property_name(u8"my-app") == std::wstring { L"luil-instance:my-app" });
    REQUIRE(instance_property_name(u8"한글") == std::wstring { L"luil-instance:\uD55C\uAE00" });

    // 속성 이름은 255자까지다: `luil-instance:`이 14자이므로 이름은 241 코드 단위까지 된다.
    const auto longest { instance_property_name(std::u8string(luil::win32::instance_name_max_code_units, u8'n')) };
    REQUIRE(longest.has_value());
    REQUIRE(longest->size() == 255);
    REQUIRE(instance_property_name(std::u8string(luil::win32::instance_name_max_code_units + 1, u8'n')).has_value() == false);

    // 글자 수가 아니라 UTF-16 코드 단위로 센다. BMP 밖 글자는 둘이다.
    std::u8string emoji_at_limit {};
    for (std::size_t unit = 0; unit + 2 <= luil::win32::instance_name_max_code_units; unit += 2)
        emoji_at_limit += u8"\U0001F600";
    emoji_at_limit += u8"n";
    REQUIRE(instance_property_name(emoji_at_limit).has_value());
    REQUIRE(instance_property_name(emoji_at_limit + u8"n").has_value() == false);

    REQUIRE(instance_property_name(u8"").has_value() == false);
    REQUIRE(instance_property_name(broken_utf8()).has_value() == false);
    REQUIRE(instance_property_name(std::u8string { u8'a', u8'\0' }).has_value() == false);
}

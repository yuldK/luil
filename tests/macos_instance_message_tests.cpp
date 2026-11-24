#include "macos/instance_message.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// 둘째 인스턴스가 첫째의 Unix 소켓에 보내는 글의 운반 형식을 OS 호출 없이 본다 (single-instance-args-design.md의 macOS).
// Windows의 instance_message_tests.cpp와 같은 경우를 UTF-8 형식으로 본다.

namespace {
    using luil::macos::instance_message_error;

    [[nodiscard]] std::vector<std::byte> bytes_of(const std::u8string_view text)
    {
        std::vector<std::byte> bytes {};
        for (const char8_t unit : text)
            bytes.push_back(static_cast<std::byte>(unit));
        return bytes;
    }

    // 짓고 머리와 몸을 나눠 읽는다. 짓기가 실패하면 test가 멈춘다.
    [[nodiscard]] luil::macos::instance_decode_result round_trip(const std::vector<std::u8string>& arguments, const std::u8string& working_directory, const bool activate)
    {
        const luil::macos::instance_encode_result encoded { luil::macos::encode_instance_message(arguments, working_directory, activate) };
        REQUIRE(encoded.ok());
        const std::span<const std::byte> all { encoded.bytes };
        const luil::macos::instance_header_result header { luil::macos::decode_instance_header(all.first(luil::macos::instance_header_bytes)) };
        REQUIRE(header.ok());
        REQUIRE(header.body_bytes == all.size() - luil::macos::instance_header_bytes);
        return luil::macos::decode_instance_body(all.subspan(luil::macos::instance_header_bytes));
    }

    [[nodiscard]] luil::macos::instance_decode_result decode(const std::u8string_view body)
    {
        return luil::macos::decode_instance_body(bytes_of(body));
    }

    [[nodiscard]] std::vector<std::byte> header(const std::u8string_view tag, const std::uint32_t body)
    {
        std::vector<std::byte> bytes { bytes_of(tag) };
        for (std::size_t index { 0 }; index < 4; ++index)
            bytes.push_back(static_cast<std::byte>((body >> (8u * index)) & 0xFFu));
        return bytes;
    }

    [[nodiscard]] std::u8string broken_utf8()
    {
        return std::u8string { static_cast<char8_t>(0xC3), u8'(' };
    }
}

TEST_CASE("macOS instance arguments and the working directory survive a round trip", "[macos][instance]")
{
    const std::vector<std::u8string> arguments {
        u8"a.txt",
        u8"b c",
        u8"\"quoted\" and \\ back",
        u8"한글 인자.txt",
        u8"emoji \U0001F600 end",
        u8"",
        u8"--flag=value",
    };
    const auto result { round_trip(arguments, u8"/Users/me/작업 폴더", false) };
    REQUIRE(result.ok());
    REQUIRE(result.value.arguments == arguments);
    REQUIRE(result.value.working_directory == u8"/Users/me/작업 폴더");
    REQUIRE(result.activate == false);
}

TEST_CASE("macOS instance messages tell an empty list, an empty argument and an empty folder apart", "[macos][instance]")
{
    const auto none { round_trip({}, u8"/work", false) };
    REQUIRE(none.ok());
    REQUIRE(none.value.arguments.empty());
    REQUIRE(none.value.working_directory == u8"/work");

    const auto one_empty { round_trip({ u8"" }, u8"/work", false) };
    REQUIRE(one_empty.ok());
    REQUIRE(one_empty.value.arguments.size() == 1);
    REQUIRE(one_empty.value.arguments.front().empty());

    const auto no_folder { round_trip({ u8"x" }, u8"", false) };
    REQUIRE(no_folder.ok());
    REQUIRE(no_folder.value.working_directory.empty());
    REQUIRE(no_folder.value.arguments.size() == 1);
}

TEST_CASE("The macOS instance header carries the tag, the version and the body size", "[macos][instance]")
{
    const auto on { luil::macos::encode_instance_message(std::vector<std::u8string> { u8"x" }, u8"w", true) };
    const auto off { luil::macos::encode_instance_message(std::vector<std::u8string> { u8"x" }, u8"w", false) };
    REQUIRE(on.ok());
    REQUIRE(off.ok());
    // 머리 다음이 표지 바이트이고 작업 폴더와 인자가 NUL로 끝나 이어진다.
    std::vector<std::byte> expected { header(u8"LIN\x01", 5) };
    for (const std::byte value : bytes_of(std::u8string_view { u8"\x01w\0x\0", 5 }))
        expected.push_back(value);
    REQUIRE(on.bytes == expected);
    REQUIRE(std::to_integer<std::uint8_t>(off.bytes[luil::macos::instance_header_bytes]) == 0);
    REQUIRE(round_trip({}, u8"w", true).activate);

    // 다른 표식·판은 이 메시지가 아니다. 머리가 모자라도 읽지 않는다.
    REQUIRE(luil::macos::decode_instance_header(header(u8"LIN\x02", 5)).error == instance_message_error::wrong_tag);
    REQUIRE(luil::macos::decode_instance_header(header(u8"XIN\x01", 5)).error == instance_message_error::wrong_tag);
    REQUIRE(luil::macos::decode_instance_header(std::span { expected }.first(7)).error == instance_message_error::too_short);
    // 몸은 표지와 작업 폴더의 끝 NUL보다 짧을 수 없고 상한을 넘을 수 없다.
    REQUIRE(luil::macos::decode_instance_header(header(u8"LIN\x01", 1)).error == instance_message_error::too_short);
    REQUIRE(luil::macos::decode_instance_header(header(u8"LIN\x01", 2)).ok());
    REQUIRE(luil::macos::decode_instance_header(header(u8"LIN\x01", luil::macos::instance_message_max_bytes)).ok());
    REQUIRE(luil::macos::decode_instance_header(header(u8"LIN\x01", luil::macos::instance_message_max_bytes + 1)).error == instance_message_error::too_large);
}

TEST_CASE("The macOS instance size and count limits hold on both sides", "[macos][instance]")
{
    // 표지 하나, 작업 폴더의 NUL 하나, 인자의 NUL 하나를 뺀 만큼이 한 인자의 길이다.
    constexpr std::size_t limit { luil::macos::instance_message_max_bytes };
    const auto exact { luil::macos::encode_instance_message(std::vector<std::u8string> { std::u8string(limit - 3, u8'a') }, u8"", false) };
    REQUIRE(exact.ok());
    REQUIRE(exact.bytes.size() == luil::macos::instance_header_bytes + limit);
    const auto over { luil::macos::encode_instance_message(std::vector<std::u8string> { std::u8string(limit - 2, u8'a') }, u8"", false) };
    REQUIRE(over.error == instance_message_error::too_large);
    REQUIRE(over.bytes.empty());
    REQUIRE(luil::macos::encode_instance_message({}, std::u8string(limit - 2, u8'a'), false).ok());
    REQUIRE(luil::macos::encode_instance_message({}, std::u8string(limit - 1, u8'a'), false).error == instance_message_error::too_large);
    // 받는 쪽도 같은 상한으로 거절한다.
    std::u8string too_big(limit + 1, u8'a');
    too_big.front() = u8'\0';
    too_big.back() = u8'\0';
    REQUIRE(decode(too_big).error == instance_message_error::too_large);

    const std::vector<std::u8string> most(luil::macos::instance_message_max_arguments, u8"a");
    const auto full { round_trip(most, u8"w", false) };
    REQUIRE(full.ok());
    REQUIRE(full.value.arguments.size() == luil::macos::instance_message_max_arguments);
    const std::vector<std::u8string> too_many(luil::macos::instance_message_max_arguments + 1, u8"a");
    REQUIRE(luil::macos::encode_instance_message(too_many, u8"w", false).error == instance_message_error::too_many_arguments);
    std::u8string many { u8"\0w\0", 3 };
    for (std::size_t index { 0 }; index < luil::macos::instance_message_max_arguments + 1; ++index)
        many += std::u8string_view { u8"a\0", 2 };
    REQUIRE(decode(many).error == instance_message_error::too_many_arguments);
}

TEST_CASE("macOS instance text that cannot travel is refused before anything is sent", "[macos][instance]")
{
    const std::u8string with_nul { u8'a', u8'\0', u8'b' };
    REQUIRE(luil::macos::encode_instance_message(std::vector<std::u8string> { with_nul }, u8"w", false).error == instance_message_error::embedded_nul);
    REQUIRE(luil::macos::encode_instance_message({}, with_nul, false).error == instance_message_error::embedded_nul);
    REQUIRE(luil::macos::encode_instance_message(std::vector<std::u8string> { broken_utf8() }, u8"w", false).error == instance_message_error::invalid_text);
    REQUIRE(luil::macos::encode_instance_message({}, broken_utf8(), false).error == instance_message_error::invalid_text);
    for (const auto error : { instance_message_error::embedded_nul, instance_message_error::invalid_text, instance_message_error::too_large })
        REQUIRE(luil::macos::instance_message_error_text(error).empty() == false);
    REQUIRE(luil::macos::instance_message_error_text(instance_message_error::none).empty());
}

TEST_CASE("A macOS instance body with a broken shape is refused", "[macos][instance]")
{
    REQUIRE(luil::macos::decode_instance_body({}).error == instance_message_error::too_short);
    REQUIRE(decode(std::u8string_view { u8"\0", 1 }).error == instance_message_error::too_short);
    REQUIRE(decode(std::u8string_view { u8"\0abc", 4 }).error == instance_message_error::missing_terminator);
    REQUIRE(decode(std::u8string_view { u8"\0w\0abc", 6 }).error == instance_message_error::missing_terminator);
    std::u8string broken_folder { u8'\0' };
    broken_folder += broken_utf8();
    broken_folder.push_back(u8'\0');
    REQUIRE(decode(broken_folder).error == instance_message_error::invalid_text);
    std::u8string broken_argument { u8"\0w\0", 3 };
    broken_argument += broken_utf8();
    broken_argument.push_back(u8'\0');
    REQUIRE(decode(broken_argument).error == instance_message_error::invalid_text);
    REQUIRE(decode(std::u8string_view { u8"\x02w\0", 3 }).error == instance_message_error::unknown_flags);
    REQUIRE(decode(std::u8string_view { u8"\x03w\0", 3 }).error == instance_message_error::unknown_flags);
    REQUIRE(decode(std::u8string_view { u8"\x01w\0", 3 }).ok());
}

TEST_CASE("A macOS instance name is bounded like the Windows window mark", "[macos][instance]")
{
    using luil::macos::instance_name_valid;
    REQUIRE(instance_name_valid(u8"my-app"));
    REQUIRE(instance_name_valid(u8"한글"));
    REQUIRE(instance_name_valid(u8"") == false);
    REQUIRE(instance_name_valid(std::u8string { u8'a', u8'\0', u8'b' }) == false);
    REQUIRE(instance_name_valid(broken_utf8()) == false);
    REQUIRE(instance_name_valid(std::u8string(luil::macos::instance_name_max_code_units, u8'n')));
    REQUIRE(instance_name_valid(std::u8string(luil::macos::instance_name_max_code_units + 1, u8'n')) == false);
    // 글자 수가 아니라 UTF-16 코드 단위로 센다. BMP 밖 글자는 둘이다.
    std::u8string wide(luil::macos::instance_name_max_code_units - 2, u8'n');
    wide += u8"\U0001F600";
    REQUIRE(instance_name_valid(wide));
    wide += u8"n";
    REQUIRE(instance_name_valid(wide) == false);
}

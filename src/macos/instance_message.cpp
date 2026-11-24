#include "macos/instance_message.h"

#include "luil/text/utf8_text.h"

#include <array>
#include <utility>

namespace luil::macos {
    namespace {
        constexpr std::array<std::byte, 4> header_tag {
            std::byte { 'L' },
            std::byte { 'I' },
            std::byte { 'N' },
            std::byte { instance_message_version },
        };

        [[nodiscard]] instance_decode_result decode_failure(const instance_message_error error)
        {
            instance_decode_result result {};
            result.error = error;
            return result;
        }
    }

    std::u8string_view instance_message_error_text(const instance_message_error error) noexcept
    {
        switch (error)
        {
        case instance_message_error::invalid_text:
            return u8"A text is not valid UTF-8.";
        case instance_message_error::embedded_nul:
            return u8"An argument or the working directory holds a NUL character.";
        case instance_message_error::too_many_arguments:
            return u8"There are more than 4096 arguments.";
        case instance_message_error::too_large:
            return u8"The arguments are larger than 256 KiB.";
        case instance_message_error::wrong_tag:
            return u8"The message tag is not the one of this format version.";
        case instance_message_error::too_short:
            return u8"The message is too short to hold the flags and the working directory.";
        case instance_message_error::unknown_flags:
            return u8"The message sets a flag this format version does not know.";
        case instance_message_error::missing_terminator:
            return u8"A text of the message does not end with a NUL character.";
        case instance_message_error::none:
        default:
            return {};
        }
    }

    bool instance_name_valid(const std::u8string_view name) noexcept
    {
        if (name.empty() || name.find(u8'\0') != std::u8string_view::npos || text::utf8_is_valid(name) == false)
            return false;
        // BMP 밖 글자는 UTF-16으로 둘이다.
        std::size_t units { 0 };
        for (std::size_t offset { 0 }; offset < name.size();)
        {
            const text::utf8_decoded decoded { text::utf8_decode(name, offset) };
            units += decoded.codepoint >= 0x10000u ? 2u : 1u;
            offset += decoded.length;
        }
        return units <= instance_name_max_code_units;
    }

    instance_encode_result encode_instance_message(const std::span<const std::u8string> arguments, const std::u8string_view working_directory, const bool activate)
    {
        instance_encode_result result {};
        if (arguments.size() > instance_message_max_arguments)
        {
            result.error = instance_message_error::too_many_arguments;
            return result;
        }
        result.bytes.assign(header_tag.begin(), header_tag.end());
        result.bytes.resize(instance_header_bytes);
        result.bytes.push_back(std::byte { activate ? instance_flag_activate : std::uint8_t { 0 } });

        // 글 하나를 NUL로 끝내 붙인다. 위반이면 까닭을 돌려준다.
        const auto append = [&result](const std::u8string_view text) -> instance_message_error {
            if (text.find(u8'\0') != std::u8string_view::npos)
                return instance_message_error::embedded_nul;
            if (text.size() > instance_message_max_bytes || result.bytes.size() - instance_header_bytes + text.size() + 1 > instance_message_max_bytes)
                return instance_message_error::too_large;
            if (text::utf8_is_valid(text) == false)
                return instance_message_error::invalid_text;
            for (const char8_t unit : text)
                result.bytes.push_back(static_cast<std::byte>(unit));
            result.bytes.push_back(std::byte { 0 });
            return instance_message_error::none;
        };

        result.error = append(working_directory);
        for (std::size_t index { 0 }; result.error == instance_message_error::none && index < arguments.size(); ++index)
            result.error = append(arguments[index]);
        if (result.ok() == false)
        {
            result.bytes.clear();
            return result;
        }
        const auto body { static_cast<std::uint32_t>(result.bytes.size() - instance_header_bytes) };
        for (std::size_t index { 0 }; index < 4; ++index)
            result.bytes[4 + index] = static_cast<std::byte>((body >> (8u * index)) & 0xFFu);
        return result;
    }

    instance_header_result decode_instance_header(const std::span<const std::byte> header) noexcept
    {
        instance_header_result result {};
        if (header.size() < instance_header_bytes)
        {
            result.error = instance_message_error::too_short;
            return result;
        }
        for (std::size_t index { 0 }; index < header_tag.size(); ++index)
        {
            if (header[index] != header_tag[index])
            {
                result.error = instance_message_error::wrong_tag;
                return result;
            }
        }
        std::uint32_t body { 0 };
        for (std::size_t index { 0 }; index < 4; ++index)
            body |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(header[4 + index])) << (8u * index);
        if (body > instance_message_max_bytes)
            result.error = instance_message_error::too_large;
        // 표지 하나와 작업 폴더의 끝 NUL 하나는 있어야 한다.
        else if (body < 2)
            result.error = instance_message_error::too_short;
        else
            result.body_bytes = body;
        return result;
    }

    instance_decode_result decode_instance_body(const std::span<const std::byte> body)
    {
        if (body.size() > instance_message_max_bytes)
            return decode_failure(instance_message_error::too_large);
        if (body.size() < 2)
            return decode_failure(instance_message_error::too_short);
        const auto flags { std::to_integer<std::uint8_t>(body.front()) };
        if ((flags & ~instance_flag_activate) != 0)
            return decode_failure(instance_message_error::unknown_flags);

        instance_decode_result result {};
        result.activate = (flags & instance_flag_activate) != 0;
        const std::u8string_view units { reinterpret_cast<const char8_t*>(body.data()), body.size() };
        std::size_t position { 1 };
        // 다음 NUL까지를 글로 읽는다.
        const auto read_text = [&units, &position](std::u8string& text) -> instance_message_error {
            const std::size_t end { units.find(u8'\0', position) };
            if (end == std::u8string_view::npos)
                return instance_message_error::missing_terminator;
            const std::u8string_view value { units.substr(position, end - position) };
            if (text::utf8_is_valid(value) == false)
                return instance_message_error::invalid_text;
            text = value;
            position = end + 1;
            return instance_message_error::none;
        };

        if (const instance_message_error error { read_text(result.value.working_directory) }; error != instance_message_error::none)
            return decode_failure(error);
        while (position < units.size())
        {
            if (result.value.arguments.size() >= instance_message_max_arguments)
                return decode_failure(instance_message_error::too_many_arguments);
            std::u8string argument {};
            if (const instance_message_error error { read_text(argument) }; error != instance_message_error::none)
                return decode_failure(error);
            result.value.arguments.push_back(std::move(argument));
        }
        return result;
    }
}

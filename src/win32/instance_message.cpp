#include "win32/instance_message.h"

#include "win32/utf8.h"

#include <cstring>
#include <utility>

namespace luil::win32 {
    namespace {
        static_assert(sizeof(wchar_t) == 2, "The instance message is a UTF-16 array.");

        constexpr std::size_t max_code_units { instance_message_max_bytes / sizeof(wchar_t) };
        // UTF-8 글자 하나는 UTF-16으로 코드 단위 하나 이상이고 바이트 셋 이하다. 이보다 긴 글은 옮기지 않고 거절한다.
        constexpr std::size_t max_text_bytes { max_code_units * 3 };
        constexpr std::wstring_view property_prefix { L"luil-instance:" };

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
            return u8"A text is not valid UTF-8 or has an unpaired surrogate.";
        case instance_message_error::embedded_nul:
            return u8"An argument or the working directory holds a NUL character.";
        case instance_message_error::too_many_arguments:
            return u8"There are more than 4096 arguments.";
        case instance_message_error::too_large:
            return u8"The arguments are larger than 256 KiB.";
        case instance_message_error::wrong_tag:
            return u8"The message tag is not the one of this format version.";
        case instance_message_error::misaligned:
            return u8"The message size is not a whole number of UTF-16 code units.";
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

    std::optional<std::wstring> instance_property_name(const std::u8string_view name)
    {
        if (name.empty() || name.find(u8'\0') != std::u8string_view::npos)
            return std::nullopt;
        const auto wide { utf8_to_utf16(name) };
        if (wide.value.has_value() == false || wide.value->size() > instance_name_max_code_units)
            return std::nullopt;
        std::wstring property { property_prefix };
        property += *wide.value;
        return property;
    }

    instance_encode_result encode_instance_message(const std::span<const std::u8string> arguments, const std::u8string_view working_directory, const bool activate)
    {
        instance_encode_result result {};
        if (arguments.size() > instance_message_max_arguments)
        {
            result.error = instance_message_error::too_many_arguments;
            return result;
        }
        result.units.push_back(activate ? instance_flag_activate : L'\0');

        // 글 하나를 NUL로 끝내 붙인다. 위반이면 까닭을 돌려준다.
        const auto append = [&result](const std::u8string_view text) -> instance_message_error {
            if (text.find(u8'\0') != std::u8string_view::npos)
                return instance_message_error::embedded_nul;
            if (text.size() > max_text_bytes)
                return instance_message_error::too_large;
            const auto wide { utf8_to_utf16(text) };
            if (wide.value.has_value() == false)
                return instance_message_error::invalid_text;
            if (result.units.size() + wide.value->size() + 1 > max_code_units)
                return instance_message_error::too_large;
            result.units += *wide.value;
            result.units.push_back(L'\0');
            return instance_message_error::none;
        };

        result.error = append(working_directory);
        for (std::size_t index = 0; result.error == instance_message_error::none && index < arguments.size(); ++index)
            result.error = append(arguments[index]);
        if (result.ok() == false)
            result.units.clear();
        return result;
    }

    instance_decode_result decode_instance_message(const std::uintptr_t tag, const std::span<const std::byte> bytes)
    {
        if (tag != instance_message_tag)
            return decode_failure(instance_message_error::wrong_tag);
        if (bytes.size() > instance_message_max_bytes)
            return decode_failure(instance_message_error::too_large);
        if (bytes.size() % sizeof(wchar_t) != 0)
            return decode_failure(instance_message_error::misaligned);
        // 표지 하나와 작업 폴더의 끝 NUL 하나는 있어야 한다.
        if (bytes.size() < 2 * sizeof(wchar_t))
            return decode_failure(instance_message_error::too_short);

        // `lpData`는 정렬이 보장되지 않아 코드 단위 배열로 복사해 읽는다.
        std::wstring units(bytes.size() / sizeof(wchar_t), L'\0');
        std::memcpy(units.data(), bytes.data(), bytes.size());
        if ((units.front() & ~static_cast<int>(instance_flag_activate)) != 0)
            return decode_failure(instance_message_error::unknown_flags);

        instance_decode_result result {};
        result.activate = (units.front() & instance_flag_activate) != 0;
        std::size_t position { 1 };
        // 다음 NUL까지를 UTF-8 글로 옮긴다.
        const auto read_text = [&units, &position](std::u8string& text) -> instance_message_error {
            const std::size_t end { units.find(L'\0', position) };
            if (end == std::wstring::npos)
                return instance_message_error::missing_terminator;
            auto converted { utf16_to_utf8(std::wstring_view { units }.substr(position, end - position)) };
            if (converted.value.has_value() == false)
                return instance_message_error::invalid_text;
            text = std::move(*converted.value);
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

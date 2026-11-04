#include "luil/text/string_table.h"

#include "text/library_text.h"

#include <atomic>
#include <charconv>
#include <iterator>
#include <mutex>
#include <utility>

namespace luil {
    namespace {
        constexpr std::size_t other_form { static_cast<std::size_t>(plural_category::other) };

        // 앱이 정한 목록이다. 읽는 쪽은 드물어 잠금으로 지킨다.
        std::mutex languages_mutex {};
        std::optional<std::vector<std::u8string>> chosen_languages {};
        // 라이브러리 표에서 고른 언어 차례다. 요소가 어느 thread에서든 잠금 없이 읽는다 (`library_strings`).
        std::atomic<std::size_t> library_language { 0 };

        [[nodiscard]] char8_t folded_tag_unit(const char8_t unit) noexcept
        {
            if (unit == u8'_')
                return u8'-';
            return unit >= u8'A' && unit <= u8'Z' ? static_cast<char8_t>(unit - u8'A' + u8'a') : unit;
        }

        [[nodiscard]] bool same_tag(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index { 0 }; index < left.size(); ++index)
            {
                if (folded_tag_unit(left[index]) != folded_tag_unit(right[index]))
                    return false;
            }
            return true;
        }

        // 태그와 끝 부분을 하나씩 뗀 태그를 차례로 찾는다 (`zh-Hant-TW`, `zh-Hant`, `zh`).
        [[nodiscard]] std::optional<std::size_t> find_language(const string_table& table, std::u8string_view tag) noexcept
        {
            while (tag.empty() == false)
            {
                for (std::size_t index { 0 }; index < table.languages.size(); ++index)
                {
                    if (same_tag(table.languages[index].tag, tag))
                        return index;
                }

                const std::size_t cut { tag.find_last_of(u8"-_") };
                if (cut == std::u8string_view::npos)
                    break;
                tag = tag.substr(0, cut);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::size_t select_language(const string_table& table, const std::span<const std::u8string> preferences) noexcept
        {
            for (const std::u8string& preference : preferences)
            {
                if (const std::optional<std::size_t> found { find_language(table, preference) }; found.has_value())
                    return *found;
            }
            return 0;
        }

        [[nodiscard]] const string_argument* find_argument(const std::span<const string_argument> arguments, const std::u8string_view name) noexcept
        {
            for (const string_argument& argument : arguments)
            {
                if (argument.name == name)
                    return &argument;
            }
            return nullptr;
        }

        void append_number(std::u8string& text, const std::int64_t number)
        {
            // 부호 포함 19자리면 INT64_MIN까지 든다. `to_chars`는 로케일을 읽지 않아 자릿점이 끼지 않는다.
            char digits[24] {};
            const std::to_chars_result written { std::to_chars(std::begin(digits), std::end(digits), number) };
            for (const char* digit { digits }; digit != written.ptr; ++digit)
                text += static_cast<char8_t>(*digit);
        }

        void append_argument(std::u8string& text, const string_argument& argument)
        {
            if (const auto* words { std::get_if<std::u8string_view>(&argument.value) }; words != nullptr)
                text += *words;
            else
                append_number(text, std::get<std::int64_t>(argument.value));
        }

        // 원본은 생성기가 검사했으므로 틀린 괄호는 손으로 지은 표에서만 온다. 지우지 않고 글자 그대로 둔다.
        [[nodiscard]] std::u8string fill_arguments(const std::u8string_view form, const std::span<const string_argument> arguments)
        {
            std::u8string text {};
            text.reserve(form.size());
            std::size_t index { 0 };
            while (index < form.size())
            {
                const char8_t unit { form[index] };
                const bool doubled { index + 1u < form.size() && form[index + 1u] == unit };
                if ((unit == u8'{' || unit == u8'}') && doubled)
                {
                    text += unit;
                    index += 2u;
                    continue;
                }

                if (unit == u8'{')
                {
                    const std::size_t close { form.find_first_of(u8"{}", index + 1u) };
                    if (close != std::u8string_view::npos && form[close] == u8'}')
                    {
                        const std::u8string_view placeholder { form.substr(index, close + 1u - index) };
                        if (const string_argument* argument { find_argument(arguments, placeholder.substr(1u, placeholder.size() - 2u)) }; argument != nullptr)
                            append_argument(text, *argument);
                        else
                            text += placeholder;
                        index = close + 1u;
                        continue;
                    }
                }

                text += unit;
                ++index;
            }
            return text;
        }
    }

    plural_category plural_category_for(const plural_rule rule, const std::int64_t count) noexcept
    {
        switch (rule)
        {
        case plural_rule::one:
            // 절댓값을 짓지 않는다. INT64_MIN의 부호를 바꾸면 넘친다.
            return count == 1 || count == -1 ? plural_category::one : plural_category::other;
        case plural_rule::other:
            break;
        }
        return plural_category::other;
    }

    string_lookup::string_lookup(const string_table& table, const std::size_t language) noexcept
        : table_ { &table }
        , language_ { language < table.languages.size() ? language : 0u }
    {
    }

    std::u8string_view string_lookup::tag() const noexcept
    {
        if (table_ == nullptr || language_ >= table_->languages.size())
            return {};
        return table_->languages[language_].tag;
    }

    std::u8string_view string_lookup::text(const std::size_t key) const noexcept
    {
        if (table_ == nullptr || language_ >= table_->languages.size())
            return {};

        const std::span<const string_message> messages { table_->languages[language_].messages };
        return key < messages.size() ? messages[key].forms[other_form] : std::u8string_view {};
    }

    std::u8string string_lookup::format(const std::size_t key, const std::span<const string_argument> arguments) const
    {
        if (table_ == nullptr || language_ >= table_->languages.size())
            return {};

        const string_table_language& language { table_->languages[language_] };
        if (key >= language.messages.size())
            return {};

        const string_message& message { language.messages[key] };
        std::size_t form_index { other_form };
        if (const string_argument* count { find_argument(arguments, u8"count") }; count != nullptr)
        {
            if (const auto* number { std::get_if<std::int64_t>(&count->value) }; number != nullptr)
                form_index = static_cast<std::size_t>(plural_category_for(language.plural, *number));
        }

        const std::u8string_view form { message.forms[form_index] };
        return fill_arguments(form.empty() ? message.forms[other_form] : form, arguments);
    }

    std::optional<std::size_t> string_lookup::find(const std::u8string_view name) const noexcept
    {
        if (table_ == nullptr)
            return std::nullopt;

        for (std::size_t index { 0 }; index < table_->keys.size(); ++index)
        {
            if (table_->keys[index] == name)
                return index;
        }
        return std::nullopt;
    }

    string_lookup select_strings(const string_table& table, const std::span<const std::u8string> preferences) noexcept
    {
        return string_lookup { table, select_language(table, preferences) };
    }

    void set_text_languages(const std::span<const std::u8string> preferences)
    {
        std::vector<std::u8string> copy(preferences.begin(), preferences.end());
        const std::size_t language { select_language(library_text::table, copy) };
        const std::lock_guard lock { languages_mutex };
        chosen_languages = std::move(copy);
        library_language.store(language);
    }

    std::optional<std::vector<std::u8string>> text_languages()
    {
        const std::lock_guard lock { languages_mutex };
        return chosen_languages;
    }

    string_lookup library_strings() noexcept
    {
        return string_lookup { library_text::table, library_language.load() };
    }

    void reset_text_languages() noexcept
    {
        const std::lock_guard lock { languages_mutex };
        chosen_languages.reset();
        library_language.store(0u);
    }
}

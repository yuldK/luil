#include "luil/theme/ui_style_json.h"

#include "luil/generated/ui_style_keys.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace luil {
    namespace {
        namespace keys = generated::style_keys;
        // 낯선 키를 JSON에 적힌 순서로 알리려면 키 순서를 지키는 객체여야 한다 (기본 `json`은 키를 정렬한다).
        using json = nlohmann::ordered_json;

        constexpr std::size_t max_depth { 8 };
        constexpr std::size_t max_value_bytes { 64 };

        [[nodiscard]] std::u8string to_u8(const std::string_view text)
        {
            return { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        // 값 글을 64바이트에서 자른다. 여러 바이트 글자의 가운데에서 자르지 않는다.
        [[nodiscard]] std::u8string clip_value(const std::string_view text)
        {
            if (text.size() <= max_value_bytes)
                return to_u8(text);
            std::size_t end { max_value_bytes };
            while (end > 0
                && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
                --end;
            return to_u8(text.substr(0, end));
        }

        // 생성기(CMake의 `string(JSON TYPE)`)가 쓰는 형 이름이다. 오류 글이 두 쪽에서 같다.
        [[nodiscard]] std::string_view type_name(const json& value) noexcept
        {
            switch (value.type())
            {
            case json::value_t::object:
                return "OBJECT";
            case json::value_t::array:
                return "ARRAY";
            case json::value_t::string:
                return "STRING";
            case json::value_t::boolean:
                return "BOOLEAN";
            case json::value_t::number_integer:
            case json::value_t::number_unsigned:
            case json::value_t::number_float:
                return "NUMBER";
            default:
                return "NULL";
            }
        }

        // 숫자를 생성기가 보는 글로 적는다. CMake는 읽은 숫자를 다시 적어 넘긴다 — 정수는 그대로, 실수는 `%.17g`이고
        // 점도 지수도 없으면 `.0`을 붙인다. 오류 글의 값이 두 쪽에서 같다.
        [[nodiscard]] std::string number_text(const json& value)
        {
            std::array<char, 40> buffer {};
            std::to_chars_result result {};
            if (value.is_number_unsigned())
                result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.get<std::uint64_t>());
            else if (value.is_number_integer())
                result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.get<std::int64_t>());
            else
                result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.get<double>(), std::chars_format::general, 17);
            std::string text { buffer.data(), result.ptr };
            if (value.is_number_float()
                && text.find_first_of(".e") == std::string::npos)
                text += ".0";
            return text;
        }

        // 범위의 끝을 생성기처럼 적는다 (`0..1`, `0..1000`).
        [[nodiscard]] std::string bound_text(const float bound)
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), bound) };
            return { buffer.data(), result.ptr };
        }

        // 문법·중첩·겹친 키를 값을 짓기 전에 본다 (SAX).
        //  - nlohmann의 값 객체는 겹친 키를 뒤의 것으로 덮어 흔적을 남기지 않으므로 키를 여기서 센다.
        //  - 중첩이 깊으면 그 자리에서 멈춘다. 값 객체를 짓지 않아 깊은 글이 메모리를 끌어 쓰지 않는다.
        class structure_check
        {
        public:
            using number_integer_t = json::number_integer_t;
            using number_unsigned_t = json::number_unsigned_t;
            using number_float_t = json::number_float_t;
            using string_t = json::string_t;
            using binary_t = json::binary_t;

            explicit structure_check(std::vector<style_error>& errors) noexcept
                : errors_ { errors }
            {
            }

            bool null()
            {
                return true;
            }

            bool boolean(bool)
            {
                return true;
            }

            bool number_integer(number_integer_t)
            {
                return true;
            }

            bool number_unsigned(number_unsigned_t)
            {
                return true;
            }

            bool number_float(number_float_t, const string_t&)
            {
                return true;
            }

            bool string(string_t&)
            {
                return true;
            }

            bool binary(binary_t&)
            {
                return true;
            }

            bool start_object(std::size_t)
            {
                return enter(true);
            }

            bool key(string_t& name)
            {
                frame& current { frames_.back() };
                if (std::find(current.keys.begin(), current.keys.end(), name) != current.keys.end())
                    errors_.push_back({ .kind = style_error_kind::duplicate_key, .path = to_u8(current.path), .key = to_u8(name) });
                else
                    current.keys.push_back(name);
                pending_key_ = name;
                return true;
            }

            bool end_object()
            {
                frames_.pop_back();
                return true;
            }

            bool start_array(std::size_t)
            {
                return enter(false);
            }

            bool end_array()
            {
                frames_.pop_back();
                return true;
            }

            // 문법 오류는 그 하나만 남긴다. 앞에서 센 겹친 키는 깨진 글의 일부라 고칠 곳을 흐린다.
            bool parse_error(const std::size_t position, const std::string&, const nlohmann::json::exception&)
            {
                // `position`은 마지막으로 읽은 바이트를 1부터 센 자리다(`parse_error::byte`와 같다). 0부터 세면 그 바이트의
                // 자리이고, 글이 도중에 끝났으면 글 길이다.
                errors_.clear();
                errors_.push_back({ .kind = style_error_kind::syntax, .offset = position > 0 ? position - 1 : 0 });
                return false;
            }

        private:
            struct frame
            {
                bool object { true };
                std::string path {};
                std::vector<std::string> keys {};
            };

            bool enter(const bool object)
            {
                if (frames_.size() >= max_depth)
                {
                    errors_.clear();
                    errors_.push_back({ .kind = style_error_kind::too_deep });
                    return false;
                }
                std::string path { "root" };
                if (frames_.empty() == false)
                {
                    const frame& parent { frames_.back() };
                    if (parent.object == false)
                        path = parent.path + "[]";
                    else if (parent.path == "root")
                        path = pending_key_;
                    else
                        path = parent.path + "." + pending_key_;
                }
                frames_.push_back({ .object = object, .path = std::move(path) });
                return true;
            }

            std::vector<style_error>& errors_;
            std::vector<frame> frames_ {};
            std::string pending_key_ {};
        };

        // 값 객체를 표대로 돌며 `ui_style`을 채운다. 오류는 끝까지 모은다.
        class style_reader
        {
        public:
            explicit style_reader(std::vector<style_error>& errors) noexcept
                : errors_ { errors }
            {
            }

            void read(const json& root, ui_style& style)
            {
                if (root.is_object() == false)
                {
                    not_object("root", root);
                    return;
                }
                std::vector<std::string_view> known { std::begin(keys::root_required), std::end(keys::root_required) };
                std::apply([&known](const auto&... sections) { (known.push_back(sections.key), ...); }, keys::components);
                expect_keys(root, "root", known);
                for (const std::string_view key : keys::root_required)
                    if (root.contains(key) == false)
                        missing("root", key);

                read_theme(root, "dark", style.dark);
                read_theme(root, "light", style.light);
                read_numbers(root, "tones", keys::tones, keys::tone_minimum, keys::tone_maximum, style.tones);
                read_numbers(root, "metrics", keys::metrics, keys::metric_minimum, keys::metric_maximum, style.metrics);
                if (const json* const metrics { find(root, "metrics") }; metrics != nullptr && metrics->is_object())
                    read_enum_section(*metrics, keys::font_weights_key, "metrics.fontWeights", keys::font_weights, style.metrics.font_weights);
                std::apply([&](const auto&... sections) { (read_enum_section(root, sections.key, sections.key, sections.rows, style.*sections.field), ...); }, keys::components);
            }

        private:
            [[nodiscard]] static const json* find(const json& object, const std::string_view key)
            {
                const auto found { object.find(key) };
                return found != object.end() ? &*found : nullptr;
            }

            void add(const style_error_kind kind, const std::string_view path, const std::string_view key, const std::string_view value)
            {
                errors_.push_back({ .kind = kind, .path = to_u8(path), .key = to_u8(key), .value = clip_value(value) });
            }

            void not_object(const std::string_view path, const json& value)
            {
                add(style_error_kind::not_object, path, {}, type_name(value));
            }

            void missing(const std::string_view path, const std::string_view key)
            {
                add(style_error_kind::missing_key, path, key, {});
            }

            // 낯선 키를 JSON에 적힌 순서로 알린다.
            void expect_keys(const json& object, const std::string_view path, const std::span<const std::string_view> known)
            {
                for (const auto& [key, value] : object.items())
                {
                    static_cast<void>(value);
                    if (std::find(known.begin(), known.end(), key) == known.end())
                        add(style_error_kind::unknown_key, path, key, {});
                }
            }

            // 있으면 객체로 돌려준다. 객체가 아니면 알리고 비운다.
            [[nodiscard]] const json* object_at(const json& parent, const std::string_view key, const std::string_view path)
            {
                const json* const value { find(parent, key) };
                if (value == nullptr)
                    return nullptr;
                if (value->is_object() == false)
                {
                    not_object(path, *value);
                    return nullptr;
                }
                return value;
            }

            [[nodiscard]] std::optional<ui_color> read_color(const json& object, const std::string_view path, const std::string_view key)
            {
                const json& value { object.at(key) };
                if (value.is_string() == false)
                {
                    add(style_error_kind::wrong_type, path, key, type_name(value));
                    return std::nullopt;
                }
                const std::string& text { value.get_ref<const std::string&>() };
                const auto is_hex = [](const char digit) {
                    return (digit >= '0' && digit <= '9')
                        || (digit >= 'a' && digit <= 'f')
                        || (digit >= 'A' && digit <= 'F');
                };
                const bool hex_digits { std::all_of(text.begin() + std::min<std::size_t>(text.size(), 1), text.end(), is_hex) };
                if ((text.size() != 7 && text.size() != 9)
                    || text.front() != '#'
                    || hex_digits == false)
                {
                    add(style_error_kind::invalid_color, path, key, text);
                    return std::nullopt;
                }
                std::uint32_t rgb { 0 };
                std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
                std::uint32_t alpha { 0xFFu };
                if (text.size() == 9)
                    std::from_chars(text.data() + 7, text.data() + 9, alpha, 16);
                return static_cast<ui_color>((alpha << 24) | rgb);
            }

            void read_theme(const json& root, const std::string_view theme_key, neutral_color_palette& palette)
            {
                const json* const theme { object_at(root, theme_key, theme_key) };
                if (theme == nullptr)
                    return;
                std::vector<std::string_view> known {};
                for (const keys::color_row& row : keys::neutral)
                    known.push_back(row.key);
                known.push_back(keys::caption_key);
                expect_keys(*theme, theme_key, known);
                for (const keys::color_row& row : keys::neutral)
                    if (row.fallback == nullptr && theme->contains(row.key) == false)
                        missing(theme_key, row.key);
                if (theme->contains(keys::caption_key) == false)
                    missing(theme_key, keys::caption_key);

                for (const keys::color_row& row : keys::neutral)
                {
                    if (theme->contains(row.key) == false)
                        continue;
                    if (const std::optional<ui_color> color { read_color(*theme, theme_key, row.key) }; color.has_value())
                        palette.*row.field = *color;
                }
                // 대신할 키는 꼭 있는 키라 위에서 이미 채워졌다 (생성기가 데이터 파일을 검사한다).
                for (const keys::color_row& row : keys::neutral)
                    if (row.fallback != nullptr && theme->contains(row.key) == false)
                        palette.*row.field = palette.*row.fallback;

                const std::string caption_path { std::string { theme_key } + "." + std::string { keys::caption_key } };
                const json* const caption { object_at(*theme, keys::caption_key, caption_path) };
                if (caption == nullptr)
                    return;
                std::vector<std::string_view> caption_keys {};
                for (const auto& row : keys::caption)
                    caption_keys.push_back(row.key);
                expect_keys(*caption, caption_path, caption_keys);
                for (const auto& row : keys::caption)
                    if (caption->contains(row.key) == false)
                        missing(caption_path, row.key);
                for (const auto& row : keys::caption)
                {
                    if (caption->contains(row.key) == false)
                        continue;
                    if (const std::optional<ui_color> color { read_color(*caption, caption_path, row.key) }; color.has_value())
                        palette.caption.*row.field = *color;
                }
            }

            template <class Section, std::size_t Count>
            void read_numbers(
                const json& root, const std::string_view section_key, const keys::field_row<Section, float> (&rows)[Count], const float minimum, const float maximum
                , Section& section)
            {
                const json* const object { object_at(root, section_key, section_key) };
                if (object == nullptr)
                    return;
                std::vector<std::string_view> known {};
                for (const auto& row : rows)
                    known.push_back(row.key);
                // metrics의 굵기 묶음은 숫자가 아닌, 빠져도 되는 칸이다. 따로 읽는다.
                if (section_key == "metrics")
                    known.push_back(keys::font_weights_key);
                expect_keys(*object, section_key, known);
                for (const auto& row : rows)
                    if (object->contains(row.key) == false)
                        missing(section_key, row.key);
                for (const auto& row : rows)
                {
                    const json* const value { find(*object, row.key) };
                    if (value == nullptr)
                        continue;
                    if (value->is_number() == false)
                    {
                        add(style_error_kind::wrong_type, section_key, row.key, type_name(*value));
                        continue;
                    }
                    const double number { value->get<double>() };
                    // 부호로 본다. `-0.0`은 0과 같지만 생성기가 부호 붙은 글로 받아 막는다 (`-0` 정수는 0으로 온다).
                    if (std::signbit(number)
                        || number < minimum
                        || number > maximum)
                    {
                        add(style_error_kind::out_of_range, section_key, row.key, number_text(*value));
                        continue;
                    }
                    section.*row.field = static_cast<float>(number);
                }
            }

            template <class Section, class Enum>
            void read_enum(const json& object, const std::string_view path, const keys::enum_row<Section, Enum>& row, Section& section)
            {
                const json* const value { find(object, row.key) };
                if (value == nullptr)
                    return;
                if (value->is_string() == false)
                {
                    add(style_error_kind::wrong_type, path, row.key, type_name(*value));
                    return;
                }
                const std::string& name { value->get_ref<const std::string&>() };
                for (const keys::enum_name<Enum>& entry : row.names)
                {
                    if (entry.name == name)
                    {
                        section.*row.field = entry.value;
                        return;
                    }
                }
                add(style_error_kind::invalid_value, path, row.key, name);
            }

            // 빠져도 되는 enum 칸(컴포넌트 칸·굵기 묶음)이다. 빠지면 struct의 기본값을 그대로 둔다.
            template <class Section, class... Rows>
            void read_enum_section(const json& parent, const std::string_view key, const std::string_view path, const std::tuple<Rows...>& rows, Section& section)
            {
                const json* const object { object_at(parent, key, path) };
                if (object == nullptr)
                    return;
                std::vector<std::string_view> known {};
                std::apply([&known](const auto&... row) { (known.push_back(row.key), ...); }, rows);
                expect_keys(*object, path, known);
                std::apply([&](const auto&... row) { (read_enum(*object, path, row, section), ...); }, rows);
            }

            std::vector<style_error>& errors_;
        };

        // 이름 칸의 허용 값을 생성기처럼 `, `로 이어 적는다. 표에 없는 자리면 비어 있다.
        [[nodiscard]] std::string allowed_names(const std::string_view path, const std::string_view key)
        {
            std::string text {};
            const auto collect = [&text, key](const auto& row) {
                if (row.key != key || text.empty() == false)
                    return;
                for (const auto& entry : row.names)
                {
                    if (text.empty() == false)
                        text += ", ";
                    text += entry.name;
                }
            };
            if (path == "metrics.fontWeights")
                std::apply([&collect](const auto&... row) { (collect(row), ...); }, keys::font_weights);
            std::apply(
                [&](const auto&... sections) {
                    const auto visit = [&](const auto& section) {
                        if (section.key == path)
                            std::apply([&collect](const auto&... row) { (collect(row), ...); }, section.rows);
                    };
                    (visit(sections), ...);
                },
                keys::components);
            return text;
        }
    }

    std::u8string describe(const style_error& error)
    {
        const std::u8string at { error.path + u8"." + error.key };
        switch (error.kind)
        {
        case style_error_kind::syntax:
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), error.offset) };
            return u8"Invalid JSON at byte " + to_u8({ buffer.data(), result.ptr });
        }
        case style_error_kind::not_object:
            return error.path + u8" must be an object, got " + error.value;
        case style_error_kind::missing_key:
            return u8"Missing key in " + error.path + u8": " + error.key;
        case style_error_kind::unknown_key:
            return u8"Unknown key in " + error.path + u8": " + error.key;
        case style_error_kind::duplicate_key:
            return u8"Duplicate key in " + error.path + u8": " + error.key;
        case style_error_kind::wrong_type:
        {
            // 숫자 칸은 tones·metrics의 행뿐이다. 그 밖의 값 자리는 모두 글(색·이름)이다.
            const bool number { error.path == u8"tones" || error.path == u8"metrics" };
            return u8"Expected a " + std::u8string { number ? u8"number" : u8"string" } + u8" at " + at + u8", got " + error.value;
        }
        case style_error_kind::invalid_color:
            return u8"Invalid color at " + at + u8": " + error.value + u8" (expected #rrggbb or #rrggbbaa)";
        case style_error_kind::out_of_range:
        {
            const bool tone { error.path == u8"tones" };
            const float minimum { tone ? keys::tone_minimum : keys::metric_minimum };
            const float maximum { tone ? keys::tone_maximum : keys::metric_maximum };
            return u8"Number out of range at " + at + u8": " + error.value + u8" (expected " + to_u8(bound_text(minimum)) + u8".." + to_u8(bound_text(maximum)) + u8")";
        }
        case style_error_kind::invalid_value:
        {
            const std::string_view path { reinterpret_cast<const char*>(error.path.data()), error.path.size() };
            const std::string_view key { reinterpret_cast<const char*>(error.key.data()), error.key.size() };
            const std::string allowed { allowed_names(path, key) };
            std::u8string text { u8"Invalid value at " + at + u8": " + error.value };
            if (allowed.empty() == false)
                text += u8" (expected one of " + to_u8(allowed) + u8")";
            return text;
        }
        case style_error_kind::too_deep:
            return u8"JSON nested deeper than 8 levels";
        }
        return {};
    }

    style_parse_result parse_ui_style(const std::u8string_view text)
    {
        style_parse_result result {};
        const auto* const first { reinterpret_cast<const char*>(text.data()) };
        const auto* const last { first + text.size() };

        structure_check check { result.errors };
        // 예외를 값으로 바꾸는 자리다. SAX는 문법 오류를 `parse_error`로 알려 예외 없이 끝나고, 값 짓기는 그 뒤라
        // 던지지 않지만 nlohmann이 던지는 길이 남지 않게 둘 다 여기서 막는다 (http_body.cpp와 같은 경계).
        try
        {
            if (json::sax_parse(first, last, &check) == false)
                return result;
            // 문법은 맞지만 겹친 키가 있다. 값은 계속 읽어 남은 오류도 함께 알린다.
            const json root { json::parse(first, last) };
            ui_style style {};
            style_reader { result.errors }.read(root, style);
            if (result.errors.empty())
                result.style = style;
        }
        catch (const nlohmann::json::exception&)
        {
            result.errors.clear();
            result.errors.push_back({ .kind = style_error_kind::syntax });
            result.style.reset();
        }
        return result;
    }
}

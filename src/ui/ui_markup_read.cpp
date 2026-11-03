#include "ui/ui_markup_document.h"

#include "luil/text/utf8_text.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <utility>

// 화면 마크업의 엄한 XML 읽기다 (docs/markup-design.md 규칙 절).
//  - 받는 글은 모두 바른 XML 1.0이다. XML이 막는 것(주석 속 `--`, 글 속 `]]>`, 속성 값의 `<`, XML 글자 밖의 글자)은 여기서도
//    막는다. 받지 않는 구문(DTD·CDATA·처리 지시·이름 공간)은 `unsupported`다.
//  - 재귀하지 않는다. 열린 요소를 목록으로 쥐고, 깊이와 요소 수는 여는 태그를 읽는 자리에서 센다. 신뢰하지 않는 글이
//    호출 깊이를 정하지 못한다.
namespace luil {
    namespace {
        [[nodiscard]] bool is_space(const char8_t c) noexcept
        {
            return c == u8' ' || c == u8'\t' || c == u8'\r' || c == u8'\n';
        }

        // XML 1.0의 Char다. 받는 글과 숫자 참조가 같은 범위를 쓴다.
        [[nodiscard]] bool is_xml_char(const char32_t c) noexcept
        {
            return c == 0x9u || c == 0xAu || c == 0xDu
                || (c >= 0x20u && c <= 0xD7FFu)
                || (c >= 0xE000u && c <= 0xFFFDu)
                || (c >= 0x10000u && c <= 0x10FFFFu)
            ;
        }

        // XML 1.0 5판의 NameStartChar·NameChar다. 어휘는 ASCII지만 이름을 끝까지 읽어야 모르는 이름이 문법 오류가 아니라
        // `unknown_element`로 선다.
        [[nodiscard]] bool is_name_start(const char32_t c) noexcept
        {
            return c == u8':' || c == u8'_'
                || (c >= u8'A' && c <= u8'Z') || (c >= u8'a' && c <= u8'z')
                || (c >= 0xC0u && c <= 0xD6u) || (c >= 0xD8u && c <= 0xF6u) || (c >= 0xF8u && c <= 0x2FFu)
                || (c >= 0x370u && c <= 0x37Du) || (c >= 0x37Fu && c <= 0x1FFFu) || (c >= 0x200Cu && c <= 0x200Du)
                || (c >= 0x2070u && c <= 0x218Fu) || (c >= 0x2C00u && c <= 0x2FEFu) || (c >= 0x3001u && c <= 0xD7FFu)
                || (c >= 0xF900u && c <= 0xFDCFu) || (c >= 0xFDF0u && c <= 0xFFFDu) || (c >= 0x10000u && c <= 0xEFFFFu)
            ;
        }

        [[nodiscard]] bool is_name_char(const char32_t c) noexcept
        {
            return is_name_start(c)
                || c == u8'-' || c == u8'.' || (c >= u8'0' && c <= u8'9') || c == 0xB7u
                || (c >= 0x300u && c <= 0x36Fu) || (c >= 0x203Fu && c <= 0x2040u)
            ;
        }

        [[nodiscard]] std::u8string hex_text(const std::uint32_t value, const int digits)
        {
            constexpr char8_t hex[] { u8"0123456789ABCDEF" };
            std::u8string text {};
            for (int shift { (digits - 1) * 4 }; shift >= 0; shift -= 4)
                text.push_back(hex[(value >> shift) & 0xFu]);
            return text;
        }

        // 막는 글자를 `U+0001` 꼴로 적는다 (오류의 value). 글자 자체는 화면에 찍히지 않는 것이 많다.
        [[nodiscard]] std::u8string code_point_text(const char32_t c)
        {
            return u8"U+" + hex_text(static_cast<std::uint32_t>(c), c > 0xFFFFu ? 6 : 4);
        }

        class xml_reader
        {
        public:
            xml_reader(const std::u8string_view text, const ui_markup_limits& limits)
                : text_ { text }
                , limits_ { limits }
            {
            }

            [[nodiscard]] ui_markup_xml_result read()
            {
                if (text_.size() > limits_.max_bytes)
                    return failed(ui_markup_error_kind::too_large, {}, u8"max_bytes", ui_markup_decimal(limits_.max_bytes));
                // BOM은 건너뛰고 열로 세지 않는다. offset은 BOM을 포함한 바이트 자리다.
                if (text_.starts_with(u8"﻿"))
                {
                    at_ = 3;
                    position_.offset = 3;
                }
                if (validate() == false
                    || read_declaration() == false
                    || read_misc() == false)
                    return failed();
                if (done() || peek() != u8'<')
                {
                    fail(ui_markup_error_kind::syntax, position_, {}, found());
                    return failed();
                }
                if (read_elements() == false || read_misc() == false)
                    return failed();
                // 뿌리 뒤에는 공백과 주석만 온다. 둘째 뿌리와 글은 여기서 막는다.
                if (done() == false)
                {
                    fail(ui_markup_error_kind::syntax, position_, {}, found());
                    return failed();
                }
                return { .nodes = std::move(nodes_) };
            }

        private:
            [[nodiscard]] bool done() const noexcept
            {
                return at_ >= text_.size();
            }

            [[nodiscard]] char8_t peek(const std::size_t ahead = 0) const noexcept
            {
                return at_ + ahead < text_.size() ? text_[at_ + ahead] : char8_t { 0 };
            }

            [[nodiscard]] bool looking_at(const std::u8string_view word) const noexcept
            {
                return text_.substr(at_).starts_with(word);
            }

            // 한 바이트 나아간다. 줄은 LF·CR LF·CR을 한 번씩 세고, 열은 코드 포인트의 첫 바이트에서만 센다.
            void step() noexcept
            {
                const char8_t c { text_[at_] };
                ++at_;
                position_.offset = at_;
                const bool after_cr { after_cr_ };
                after_cr_ = c == u8'\r';
                if (c == u8'\r' || (c == u8'\n' && after_cr == false))
                {
                    ++position_.line;
                    position_.column = 1;
                }
                else if (c != u8'\n' && (c & 0xC0u) != 0x80u)
                {
                    ++position_.column;
                }
            }

            void skip(const std::size_t bytes) noexcept
            {
                for (std::size_t index { 0 }; index < bytes && done() == false; ++index)
                    step();
            }

            // 공백을 건너뛰고 하나라도 건넜는지 답한다.
            bool skip_spaces() noexcept
            {
                const std::size_t start { at_ };
                while (done() == false && is_space(peek()))
                    step();
                return at_ != start;
            }

            [[nodiscard]] text::utf8_decoded decoded() const noexcept
            {
                return text::utf8_decode(text_, at_);
            }

            // 지금 자리의 글자 하나다 (오류의 value). 끝이면 비어 있다.
            [[nodiscard]] std::u8string found() const
            {
                if (done())
                    return {};
                const text::utf8_decoded value { decoded() };
                if (value.codepoint < 0x20u)
                    return code_point_text(value.codepoint);
                return std::u8string { text_.substr(at_, value.length) };
            }

            void fail(const ui_markup_error_kind kind, const ui_markup_position& at, const std::u8string_view name, const std::u8string_view value)
            {
                if (error_.has_value() == false)
                    error_ = ui_markup_error_at(kind, at, {}, name, value);
            }

            [[nodiscard]] ui_markup_xml_result failed()
            {
                return { .error = error_ };
            }

            [[nodiscard]] ui_markup_xml_result failed(const ui_markup_error_kind kind, const ui_markup_position& at, const std::u8string_view name, const std::u8string_view value)
            {
                fail(kind, at, name, value);
                return failed();
            }

            // 글 전체가 바른 UTF-8이고 XML 글자뿐인지 먼저 본다. 뒤의 읽기는 바른 글만 만난다.
            [[nodiscard]] bool validate()
            {
                const std::size_t start { at_ };
                const ui_markup_position start_position { position_ };
                while (done() == false)
                {
                    const text::utf8_decoded value { decoded() };
                    if (value.codepoint == text::utf8_replacement_character && value.length == 1)
                    {
                        fail(ui_markup_error_kind::syntax, position_, {}, u8"0x" + hex_text(text_[at_], 2));
                        return false;
                    }
                    if (is_xml_char(value.codepoint) == false)
                    {
                        fail(ui_markup_error_kind::syntax, position_, {}, code_point_text(value.codepoint));
                        return false;
                    }
                    skip(value.length);
                }
                at_ = start;
                position_ = start_position;
                after_cr_ = false;
                return true;
            }

            // XML 이름 하나를 읽는다. 이름이 아니면 비어 있고 자리는 그대로다.
            [[nodiscard]] std::u8string read_name()
            {
                const std::size_t start { at_ };
                if (done() || is_name_start(decoded().codepoint) == false)
                    return {};
                skip(decoded().length);
                while (done() == false && is_name_char(decoded().codepoint))
                    skip(decoded().length);
                return std::u8string { text_.substr(start, at_ - start) };
            }

            // `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>`다. 맨 앞에만 오고 셋의 차례는 이대로다.
            //  - version은 꼭 있고 1.0만 받는다. encoding은 UTF-8(대소문자 무시)만, standalone은 yes·no다.
            [[nodiscard]] bool read_declaration()
            {
                if (looking_at(u8"<?xml") == false || (is_space(peek(5)) == false && peek(5) != u8'?'))
                    return true;
                const ui_markup_position start { position_ };
                skip(5);
                constexpr std::u8string_view names[] { u8"version", u8"encoding", u8"standalone" };
                std::size_t next { 0 };
                while (true)
                {
                    const bool spaced { skip_spaces() };
                    if (looking_at(u8"?>"))
                    {
                        skip(2);
                        break;
                    }
                    if (done())
                    {
                        fail(ui_markup_error_kind::syntax, start, u8"xml", {});
                        return false;
                    }
                    const ui_markup_position name_at { position_ };
                    const std::u8string name { read_name() };
                    std::size_t index { next };
                    while (index < std::size(names) && names[index] != name)
                        ++index;
                    // version이 먼저 오고, 차례를 건너뛸 수는 있어도 되돌아가지 않는다.
                    if (spaced == false
                        || index == std::size(names)
                        || (next == 0 && index != 0))
                    {
                        fail(ui_markup_error_kind::syntax, name_at, u8"xml", name.empty() ? found() : name);
                        return false;
                    }
                    next = index + 1;
                    std::u8string value {};
                    if (read_plain_value(value) == false)
                    {
                        fail(ui_markup_error_kind::syntax, name_at, name, found());
                        return false;
                    }
                    if (index == 0 && value != u8"1.0")
                    {
                        fail(ui_markup_error_kind::unsupported, name_at, name, value);
                        return false;
                    }
                    if (index == 1 && equals_ascii_case(value, u8"UTF-8") == false)
                    {
                        fail(ui_markup_error_kind::unsupported, name_at, name, value);
                        return false;
                    }
                    if (index == 2 && value != u8"yes" && value != u8"no")
                    {
                        fail(ui_markup_error_kind::syntax, name_at, name, value);
                        return false;
                    }
                }
                if (next == 0)
                {
                    fail(ui_markup_error_kind::syntax, start, u8"xml", {});
                    return false;
                }
                return true;
            }

            [[nodiscard]] static bool equals_ascii_case(const std::u8string_view text, const std::u8string_view expected) noexcept
            {
                if (text.size() != expected.size())
                    return false;
                for (std::size_t index { 0 }; index < text.size(); ++index)
                {
                    const auto lower = [](const char8_t c) {
                        return c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c;
                    };
                    if (lower(text[index]) != lower(expected[index]))
                        return false;
                }
                return true;
            }

            // 선언의 `= "값"`이다. 엔티티를 받지 않는다 (선언의 값은 이름 글자뿐이다).
            [[nodiscard]] bool read_plain_value(std::u8string& value)
            {
                skip_spaces();
                if (peek() != u8'=')
                    return false;
                step();
                skip_spaces();
                const char8_t quote { peek() };
                if (quote != u8'"' && quote != u8'\'')
                    return false;
                step();
                const std::size_t start { at_ };
                while (done() == false && peek() != quote)
                {
                    if (peek() == u8'<' || peek() == u8'&')
                        return false;
                    step();
                }
                if (done())
                    return false;
                value.assign(text_.substr(start, at_ - start));
                step();
                return true;
            }

            // 요소 밖(뿌리 앞뒤)의 공백·주석이다. DTD·CDATA·처리 지시는 여기서 막는다.
            [[nodiscard]] bool read_misc()
            {
                while (true)
                {
                    skip_spaces();
                    if (done())
                        return true;
                    if (looking_at(u8"<!--"))
                    {
                        if (read_comment() == false)
                            return false;
                        continue;
                    }
                    if (looking_at(u8"<!") || looking_at(u8"<?"))
                        return read_markup_declaration();
                    return true;
                }
            }

            // `<!`·`<?`로 여는 것 중 주석이 아닌 것이다. 모두 받지 않는다.
            [[nodiscard]] bool read_markup_declaration()
            {
                const ui_markup_position start { position_ };
                if (looking_at(u8"<!DOCTYPE"))
                {
                    fail(ui_markup_error_kind::unsupported, start, u8"DOCTYPE", {});
                    return false;
                }
                if (looking_at(u8"<![CDATA["))
                {
                    fail(ui_markup_error_kind::unsupported, start, u8"CDATA", {});
                    return false;
                }
                if (looking_at(u8"<?"))
                {
                    skip(2);
                    const std::u8string target { read_name() };
                    // `xml` 선언은 맨 앞에만 온다. 다른 자리에서는 문법 오류다 (XML의 예약 이름).
                    if (equals_ascii_case(target, u8"xml"))
                        fail(ui_markup_error_kind::syntax, start, u8"xml", {});
                    else
                        fail(ui_markup_error_kind::unsupported, start, target, {});
                    return false;
                }
                fail(ui_markup_error_kind::syntax, start, {}, u8"<!");
                return false;
            }

            // `<!-- … -->`다. 안의 `--`는 XML이 막는다.
            [[nodiscard]] bool read_comment()
            {
                const ui_markup_position start { position_ };
                skip(4);
                while (done() == false)
                {
                    if (looking_at(u8"--"))
                    {
                        if (peek(2) == u8'>')
                        {
                            skip(3);
                            return true;
                        }
                        fail(ui_markup_error_kind::syntax, position_, {}, u8"--");
                        return false;
                    }
                    step();
                }
                fail(ui_markup_error_kind::syntax, start, {}, u8"<!--");
                return false;
            }

            // `&…;` 하나를 풀어 `out`에 붙인다.
            [[nodiscard]] bool read_reference(std::u8string& out)
            {
                const ui_markup_position start { position_ };
                const std::size_t start_offset { at_ };
                step();
                if (peek() == u8'#')
                {
                    step();
                    const bool hex { peek() == u8'x' };
                    if (hex)
                        step();
                    std::uint32_t value { 0 };
                    bool overflow { false };
                    std::size_t digits { 0 };
                    while (done() == false)
                    {
                        const char8_t c { peek() };
                        std::uint32_t digit { 0 };
                        if (c >= u8'0' && c <= u8'9')
                            digit = static_cast<std::uint32_t>(c - u8'0');
                        else if (hex && c >= u8'a' && c <= u8'f')
                            digit = static_cast<std::uint32_t>(c - u8'a' + 10);
                        else if (hex && c >= u8'A' && c <= u8'F')
                            digit = static_cast<std::uint32_t>(c - u8'A' + 10);
                        else
                            break;
                        // 넘쳐도 끝까지 읽어 자리를 맞춘다. 넘친 값은 XML 글자가 아니다.
                        if (value > 0x10FFFFu)
                            overflow = true;
                        else
                            value = value * (hex ? 16u : 10u) + digit;
                        ++digits;
                        step();
                    }
                    if (digits == 0 || peek() != u8';')
                    {
                        fail(ui_markup_error_kind::syntax, start, {}, text_.substr(start_offset, at_ - start_offset));
                        return false;
                    }
                    step();
                    if (overflow || is_xml_char(static_cast<char32_t>(value)) == false)
                    {
                        fail(ui_markup_error_kind::syntax, start, {}, text_.substr(start_offset, at_ - start_offset));
                        return false;
                    }
                    out += text::utf8_encode(static_cast<char32_t>(value));
                    return true;
                }
                const std::u8string name { read_name() };
                if (name.empty() || peek() != u8';')
                {
                    fail(ui_markup_error_kind::syntax, start, name, text_.substr(start_offset, at_ - start_offset));
                    return false;
                }
                step();
                constexpr std::pair<std::u8string_view, char8_t> entities[] {
                    { u8"amp", u8'&' },
                    { u8"lt", u8'<' },
                    { u8"gt", u8'>' },
                    { u8"quot", u8'"' },
                    { u8"apos", u8'\'' },
                };
                for (const auto& [entity, character] : entities)
                {
                    if (entity == name)
                    {
                        out.push_back(character);
                        return true;
                    }
                }
                fail(ui_markup_error_kind::syntax, start, name, text_.substr(start_offset, at_ - start_offset));
                return false;
            }

            // 여는 태그 하나다. `<`에 서 있다. 빈 요소(`/>`)면 `closed`가 참이다.
            [[nodiscard]] bool read_start_tag(const std::size_t parent, bool& closed)
            {
                const ui_markup_position start { position_ };
                step();
                std::u8string name { read_name() };
                if (name.empty())
                {
                    fail(ui_markup_error_kind::syntax, position_, {}, found());
                    return false;
                }
                if (name.find(u8':') != std::u8string::npos)
                {
                    fail(ui_markup_error_kind::unsupported, start, name, {});
                    return false;
                }
                if (open_.size() + 1 > limits_.max_depth)
                {
                    fail(ui_markup_error_kind::too_large, start, u8"max_depth", ui_markup_decimal(limits_.max_depth));
                    return false;
                }
                if (elements_ + 1 > limits_.max_nodes)
                {
                    fail(ui_markup_error_kind::too_large, start, u8"max_nodes", ui_markup_decimal(limits_.max_nodes));
                    return false;
                }
                ++elements_;
                const std::size_t index { nodes_.size() };
                nodes_.push_back({ .name = std::move(name), .at = start, .parent = parent });
                if (parent != ui_markup_no_parent)
                    nodes_[parent].children.push_back(index);
                while (true)
                {
                    const bool spaced { skip_spaces() };
                    if (done())
                    {
                        fail(ui_markup_error_kind::syntax, start, nodes_[index].name, {});
                        return false;
                    }
                    if (peek() == u8'>')
                    {
                        step();
                        closed = false;
                        open_.push_back(index);
                        return true;
                    }
                    if (looking_at(u8"/>"))
                    {
                        skip(2);
                        closed = true;
                        return true;
                    }
                    if (spaced == false)
                    {
                        fail(ui_markup_error_kind::syntax, position_, nodes_[index].name, found());
                        return false;
                    }
                    if (read_attribute(index) == false)
                        return false;
                }
            }

            // 속성 하나다. 값의 공백 글자(탭·CR·LF)는 공백 하나가 되고 CR LF는 한 번이다. `&#10;`은 줄 바꿈으로 남는다.
            [[nodiscard]] bool read_attribute(const std::size_t element)
            {
                ui_markup_xml_attribute attribute { .at = position_ };
                attribute.name = read_name();
                if (attribute.name.empty())
                {
                    fail(ui_markup_error_kind::syntax, position_, nodes_[element].name, found());
                    return false;
                }
                if (attribute.name.find(u8':') != std::u8string::npos || attribute.name == u8"xmlns")
                {
                    fail(ui_markup_error_kind::unsupported, attribute.at, attribute.name, {});
                    return false;
                }
                skip_spaces();
                if (peek() != u8'=')
                {
                    fail(ui_markup_error_kind::syntax, position_, attribute.name, found());
                    return false;
                }
                step();
                skip_spaces();
                const char8_t quote { peek() };
                if (quote != u8'"' && quote != u8'\'')
                {
                    fail(ui_markup_error_kind::syntax, position_, attribute.name, found());
                    return false;
                }
                step();
                while (true)
                {
                    if (done())
                    {
                        fail(ui_markup_error_kind::syntax, attribute.at, attribute.name, {});
                        return false;
                    }
                    const char8_t c { peek() };
                    if (c == quote)
                    {
                        step();
                        break;
                    }
                    if (c == u8'<')
                    {
                        fail(ui_markup_error_kind::syntax, position_, attribute.name, u8"<");
                        return false;
                    }
                    if (c == u8'&')
                    {
                        if (read_reference(attribute.value) == false)
                            return false;
                        continue;
                    }
                    if (c == u8'\r' && peek(1) == u8'\n')
                        step();
                    if (is_space(c))
                        attribute.value.push_back(u8' ');
                    else
                        attribute.value.push_back(c);
                    step();
                }
                nodes_[element].attributes.push_back(std::move(attribute));
                return true;
            }

            // `</이름 >`다. 열린 맨 안쪽 요소와 이름이 같아야 한다.
            [[nodiscard]] bool read_end_tag()
            {
                const ui_markup_position start { position_ };
                skip(2);
                const std::u8string name { read_name() };
                const std::size_t element { open_.back() };
                if (name.empty())
                {
                    fail(ui_markup_error_kind::syntax, position_, nodes_[element].name, found());
                    return false;
                }
                if (name != nodes_[element].name)
                {
                    fail(ui_markup_error_kind::syntax, start, nodes_[element].name, name);
                    return false;
                }
                skip_spaces();
                if (peek() != u8'>')
                {
                    fail(ui_markup_error_kind::syntax, position_, nodes_[element].name, found());
                    return false;
                }
                step();
                open_.pop_back();
                return true;
            }

            // 열린 요소 안의 글 한 토막을 다음 `<`까지 읽는다. 공백이 아닌 글자나 참조가 있으면 글 조각으로 남긴다.
            [[nodiscard]] bool read_text()
            {
                std::u8string text {};
                bool content { false };
                ui_markup_position at {};
                while (done() == false && peek() != u8'<')
                {
                    const char8_t c { peek() };
                    if (content == false && (c == u8'&' || is_space(c) == false))
                    {
                        content = true;
                        at = position_;
                    }
                    if (c == u8'&')
                    {
                        if (read_reference(text) == false)
                            return false;
                        continue;
                    }
                    if (c == u8']' && looking_at(u8"]]>"))
                    {
                        fail(ui_markup_error_kind::syntax, position_, {}, u8"]]>");
                        return false;
                    }
                    if (c == u8'\r')
                    {
                        step();
                        if (peek() == u8'\n')
                            step();
                        text.push_back(u8'\n');
                        continue;
                    }
                    text.push_back(c);
                    step();
                }
                if (content)
                {
                    const std::size_t parent { open_.back() };
                    const std::size_t index { nodes_.size() };
                    nodes_.push_back({ .text = std::move(text), .is_text = true, .at = at, .parent = parent });
                    nodes_[parent].children.push_back(index);
                }
                return true;
            }

            // 뿌리 요소와 그 안 전부다. `<`에 서 있다.
            [[nodiscard]] bool read_elements()
            {
                bool closed { false };
                if (read_start_tag(ui_markup_no_parent, closed) == false)
                    return false;
                while (open_.empty() == false)
                {
                    if (read_text() == false)
                        return false;
                    if (done())
                    {
                        const ui_markup_xml_node& unclosed { nodes_[open_.back()] };
                        fail(ui_markup_error_kind::syntax, unclosed.at, unclosed.name, {});
                        return false;
                    }
                    if (looking_at(u8"</"))
                    {
                        if (read_end_tag() == false)
                            return false;
                    }
                    else if (looking_at(u8"<!--"))
                    {
                        if (read_comment() == false)
                            return false;
                    }
                    else if (looking_at(u8"<!") || looking_at(u8"<?"))
                    {
                        static_cast<void>(read_markup_declaration());
                        return false;
                    }
                    else if (read_start_tag(open_.back(), closed) == false)
                    {
                        return false;
                    }
                }
                return true;
            }

            std::u8string_view text_ {};
            const ui_markup_limits& limits_;
            std::size_t at_ { 0 };
            ui_markup_position position_ {};
            bool after_cr_ { false };
            std::vector<ui_markup_xml_node> nodes_ {};
            std::vector<std::size_t> open_ {};
            std::size_t elements_ { 0 };
            std::optional<ui_markup_error> error_ {};
        };

        [[nodiscard]] std::u8string number_text(const std::size_t value)
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result written { std::to_chars(buffer.data(), buffer.data() + buffer.size(), value) };
            return { reinterpret_cast<const char8_t*>(buffer.data()), static_cast<std::size_t>(written.ptr - buffer.data()) };
        }

        // 자리의 마지막 요소 이름이다 (`[n]`을 뗀다). 자리가 비면 뿌리 밖이다.
        [[nodiscard]] std::u8string element_of(const std::u8string_view path)
        {
            const std::size_t slash { path.rfind(u8'/') };
            std::u8string_view last { slash == std::u8string_view::npos ? path : path.substr(slash + 1) };
            if (const std::size_t bracket { last.find(u8'[') }; bracket != std::u8string_view::npos)
                last = last.substr(0, bracket);
            return std::u8string { last.empty() ? std::u8string_view { u8"markup" } : last };
        }
    }

    ui_markup_xml_result read_ui_markup_xml(const std::u8string_view text, const ui_markup_limits& limits)
    {
        return xml_reader { text, limits }.read();
    }

    std::u8string ui_markup_clip(const std::u8string_view text)
    {
        constexpr std::size_t limit { 64 };
        if (text.size() <= limit)
            return std::u8string { text };
        std::size_t end { limit };
        while (end > 0 && (static_cast<unsigned>(text[end]) & 0xC0u) == 0x80u)
            --end;
        return std::u8string { text.substr(0, end) };
    }

    std::u8string ui_markup_decimal(const std::size_t value)
    {
        return number_text(value);
    }

    ui_markup_error ui_markup_error_at(const ui_markup_error_kind kind, const ui_markup_position& at, std::u8string path, const std::u8string_view name, const std::u8string_view value)
    {
        return {
            .kind = kind,
            .line = at.line,
            .column = at.column,
            .offset = at.offset,
            .path = std::move(path),
            .name = std::u8string { name },
            .value = ui_markup_clip(value),
        };
    }

    ui_markup_parse_result parse_ui_markup(const std::u8string_view text, const ui_markup_limits& limits)
    {
        ui_markup_xml_result read { read_ui_markup_xml(text, limits) };
        if (read.error.has_value())
            return { .errors = { std::move(*read.error) } };
        ui_markup_check_result checked { check_ui_markup_vocabulary(read.nodes, limits) };
        return { .document = std::move(checked.document), .errors = std::move(checked.errors) };
    }

    std::u8string describe(const ui_markup_error& error)
    {
        std::u8string text { number_text(error.line) + u8":" + number_text(error.column) + u8" " };
        const std::u8string element { element_of(error.path) };
        switch (error.kind)
        {
        case ui_markup_error_kind::syntax:
            text += u8"Syntax error";
            if (error.name.empty() == false)
                text += u8" in " + error.name;
            if (error.value.empty() == false)
                text += u8": " + error.value;
            return text;
        case ui_markup_error_kind::unsupported:
            text += u8"Unsupported markup: " + error.name;
            if (error.value.empty() == false)
                text += u8"=" + error.value;
            return text;
        case ui_markup_error_kind::too_large:
            return text + u8"Markup exceeds " + error.name + u8" (" + error.value + u8")";
        case ui_markup_error_kind::unknown_element:
            return text + u8"Unknown element: " + error.name;
        case ui_markup_error_kind::unknown_attribute:
            return text + u8"Unknown attribute on " + element + u8": " + error.name;
        case ui_markup_error_kind::duplicate_attribute:
            return text + u8"Duplicate attribute on " + element + u8": " + error.name;
        case ui_markup_error_kind::missing_attribute:
            return text + u8"Missing attribute on " + element + u8": " + error.name;
        case ui_markup_error_kind::invalid_value:
            return text + u8"Invalid value for " + error.name + u8" on " + element + u8": " + error.value;
        case ui_markup_error_kind::invalid_child:
            if (error.path.empty())
                return text + u8"Invalid root element: " + error.name;
            if (error.name.empty())
                return text + u8"Text is not allowed in " + element + u8": " + error.value;
            return text + u8"Invalid child in " + element + u8": " + error.name;
        case ui_markup_error_kind::missing_length:
            return text + u8"Missing length on " + element + u8" (set length, weight or fit)";
        case ui_markup_error_kind::duplicate_id:
            return text + u8"Duplicate id: " + error.name;
        case ui_markup_error_kind::unknown_value:
            return text + u8"Unknown value: " + error.name;
        case ui_markup_error_kind::wrong_value_type:
            return text + u8"Wrong value type for " + error.name + u8" on " + element + u8": " + error.value;
        case ui_markup_error_kind::unknown_action:
            return text + u8"Unknown action: " + error.name;
        case ui_markup_error_kind::unknown_slot:
            return text + u8"Unknown slot: " + error.name;
        case ui_markup_error_kind::unknown_kind:
            return text + u8"Unknown kind: " + error.name;
        case ui_markup_error_kind::duplicate_key:
            return text + u8"Duplicate key in " + element + u8": " + error.value;
        case ui_markup_error_kind::too_many_built:
            return text + u8"Built elements exceed " + error.name + u8" (" + error.value + u8")";
        }
        return text;
    }
}

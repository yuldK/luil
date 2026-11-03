#include "luil/ui/rich_text.h"

#include "luil/text/utf8_text.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 서식 글의 HTML 쓰기와 거르는 읽기다 (docs/rich-edit-exchange-design.md).
//  - 읽기는 HTML 표준 파서가 아니다. 허용한 서식과 글만 꺼내는 데 필요한 만큼만 토큰을 읽고, 모르는 꼴은 글이나 버림으로 떨어뜨린다.
//  - 바깥 자원(그림·스타일 시트·주소)을 풀거나 가져오지 않는다. 읽는 것은 넘겨받은 바이트뿐이다.
namespace luil {
    namespace {
        using text::text_mark;

        [[nodiscard]] text_mark with_mark(const text_mark marks, const text_mark mark, const bool on) noexcept
        {
            const auto bits { static_cast<std::uint8_t>(marks) };
            const auto bit { static_cast<std::uint8_t>(mark) };
            return static_cast<text_mark>(on ? bits | bit : bits & static_cast<std::uint8_t>(~bit));
        }

        // ---- 쓰기 ----

        // 글을 HTML 글로 옮긴다. 줄 바꿈은 `<br>`, CR은 숫자 참조라 읽기가 글자 그대로 되돌린다.
        void append_text(std::u8string& html, const std::u8string_view text)
        {
            for (const char8_t unit : text)
            {
                switch (unit)
                {
                case u8'&':
                    html += u8"&amp;";
                    break;
                case u8'<':
                    html += u8"&lt;";
                    break;
                case u8'>':
                    html += u8"&gt;";
                    break;
                case u8'\n':
                    html += u8"<br>";
                    break;
                case u8'\r':
                    html += u8"&#13;";
                    break;
                default:
                    html += unit;
                    break;
                }
            }
        }

        void append_attribute(std::u8string& html, const std::u8string_view value)
        {
            for (const char8_t unit : value)
            {
                switch (unit)
                {
                case u8'&':
                    html += u8"&amp;";
                    break;
                case u8'<':
                    html += u8"&lt;";
                    break;
                case u8'>':
                    html += u8"&gt;";
                    break;
                case u8'"':
                    html += u8"&quot;";
                    break;
                default:
                    html += unit;
                    break;
                }
            }
        }

        // 서식 비트와 태그의 짝이다. 여는 차례(바깥부터)다. 링크는 이보다 바깥이다.
        struct mark_tag
        {
            text_mark mark { text_mark::none };
            std::u8string_view open {};
            std::u8string_view close {};
        };

        constexpr std::array<mark_tag, 4> mark_tags {
            mark_tag { text_mark::code, u8"<code>", u8"</code>" },
            mark_tag { text_mark::bold, u8"<b>", u8"</b>" },
            mark_tag { text_mark::italic, u8"<i>", u8"</i>" },
            mark_tag { text_mark::underline, u8"<u>", u8"</u>" },
        };

        // ---- 읽기 ----

        [[nodiscard]] constexpr bool ascii_space(const char8_t unit) noexcept
        {
            return unit == u8' ' || unit == u8'\t' || unit == u8'\n' || unit == u8'\r' || unit == u8'\f';
        }

        [[nodiscard]] constexpr bool ascii_letter(const char8_t unit) noexcept
        {
            return (unit >= u8'a' && unit <= u8'z') || (unit >= u8'A' && unit <= u8'Z');
        }

        [[nodiscard]] constexpr bool ascii_digit(const char8_t unit) noexcept
        {
            return unit >= u8'0' && unit <= u8'9';
        }

        [[nodiscard]] constexpr char8_t ascii_lower(const char8_t unit) noexcept
        {
            return unit >= u8'A' && unit <= u8'Z' ? static_cast<char8_t>(unit - u8'A' + u8'a') : unit;
        }

        [[nodiscard]] std::u8string lowered(const std::u8string_view value)
        {
            std::u8string result { value };
            std::ranges::transform(result, result.begin(), ascii_lower);
            return result;
        }

        [[nodiscard]] std::u8string_view trimmed(std::u8string_view value) noexcept
        {
            while (value.empty() == false && ascii_space(value.front()))
                value.remove_prefix(1);
            while (value.empty() == false && ascii_space(value.back()))
                value.remove_suffix(1);
            return value;
        }

        [[nodiscard]] bool starts_with_ignoring_case(const std::u8string_view value, const std::u8string_view prefix) noexcept
        {
            if (value.size() < prefix.size())
                return false;
            for (std::size_t index = 0; index < prefix.size(); ++index)
            {
                if (ascii_lower(value[index]) != ascii_lower(prefix[index]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool one_of(const std::u8string_view value, const std::initializer_list<std::u8string_view> names) noexcept
        {
            return std::ranges::find(names, value) != names.end();
        }

        // 서식이 없는 요소다. 이름만 있어 스택에 올리지 않는다.
        [[nodiscard]] bool void_element(const std::u8string_view name) noexcept
        {
            return one_of(name, { u8"area", u8"base", u8"br", u8"col", u8"embed", u8"hr", u8"img", u8"input", u8"link", u8"meta", u8"param", u8"source", u8"track", u8"wbr" });
        }

        // 내용이 markup이 아닌 글(raw text)인 요소다. 닫는 태그까지 글자 그대로 건너뛴다. 모두 내용째 버린다.
        [[nodiscard]] bool raw_text_element(const std::u8string_view name) noexcept
        {
            return one_of(name, { u8"script", u8"style", u8"title", u8"iframe", u8"noscript", u8"xmp", u8"noembed", u8"noframes" });
        }

        // 내용이 markup이지만 내용째 버리는 요소다. 스택에 숨김으로 올려 짝을 맞춘다.
        [[nodiscard]] bool hidden_element(const std::u8string_view name) noexcept
        {
            return one_of(name, { u8"head", u8"template", u8"svg", u8"math", u8"object" });
        }

        // 앞뒤에 줄 바꿈 하나를 두는 요소다. 모양(제목 크기, 목록 기호)은 1단계에 버린다.
        [[nodiscard]] bool block_element(const std::u8string_view name) noexcept
        {
            return one_of(name, { u8"p", u8"div", u8"li", u8"h1", u8"h2", u8"h3", u8"h4", u8"h5", u8"h6", u8"blockquote", u8"pre", u8"tr", u8"ul", u8"ol", u8"table", u8"dl", u8"dt", u8"dd", u8"section", u8"article", u8"header", u8"footer" });
        }

        // 허용한 scheme의 주소다. 앞뒤 공백을 버리고 scheme은 대소문자를 가리지 않는다. 아니면 빈 값이다.
        [[nodiscard]] std::u8string allowed_link(const std::u8string_view href)
        {
            const std::u8string_view destination { trimmed(href) };
            if (starts_with_ignoring_case(destination, u8"http:") || starts_with_ignoring_case(destination, u8"https:") || starts_with_ignoring_case(destination, u8"mailto:"))
                return std::u8string { destination };
            return {};
        }

        // 서식 비트 하나를 켜고 끄는 값이다. 비면 손대지 않는다.
        struct mark_override
        {
            text_mark mark { text_mark::none };
            bool on { false };
        };

        // `style` 속성에서 읽은 값이다. 서식 셋과 공백 규칙만 본다.
        struct style_values
        {
            std::vector<mark_override> marks {};
            std::optional<bool> preserve {};
        };

        [[nodiscard]] std::optional<int> parse_weight(const std::u8string_view value) noexcept
        {
            if (value.empty() || value.size() > 4)
                return std::nullopt;
            int weight { 0 };
            for (const char8_t unit : value)
            {
                if (ascii_digit(unit) == false)
                    return std::nullopt;
                weight = weight * 10 + (unit - u8'0');
            }
            return weight;
        }

        [[nodiscard]] style_values parse_style(const std::u8string_view style)
        {
            style_values values {};
            std::size_t begin { 0 };
            while (begin <= style.size())
            {
                const std::size_t end { std::min(style.find(u8';', begin), style.size()) };
                const std::u8string_view declaration { style.substr(begin, end - begin) };
                begin = end + 1;
                const std::size_t colon { declaration.find(u8':') };
                if (colon == std::u8string_view::npos)
                    continue;
                const std::u8string property { lowered(trimmed(declaration.substr(0, colon))) };
                std::u8string value { lowered(trimmed(declaration.substr(colon + 1))) };
                if (const std::size_t important { value.find(u8"!important") }; important != std::u8string::npos)
                    value = std::u8string { trimmed(std::u8string_view { value }.substr(0, important)) };

                if (property == u8"font-weight")
                {
                    if (value == u8"bold" || value == u8"bolder")
                        values.marks.push_back({ text_mark::bold, true });
                    else if (value == u8"normal" || value == u8"lighter")
                        values.marks.push_back({ text_mark::bold, false });
                    else if (const std::optional<int> weight { parse_weight(value) }; weight.has_value())
                        values.marks.push_back({ text_mark::bold, *weight >= 600 });
                }
                else if (property == u8"font-style")
                {
                    if (value == u8"italic" || value.starts_with(u8"oblique"))
                        values.marks.push_back({ text_mark::italic, true });
                    else if (value == u8"normal")
                        values.marks.push_back({ text_mark::italic, false });
                }
                else if (property == u8"text-decoration" || property == u8"text-decoration-line")
                {
                    bool underline { false };
                    bool none { false };
                    std::size_t at { 0 };
                    while (at < value.size())
                    {
                        while (at < value.size() && ascii_space(value[at]))
                            ++at;
                        std::size_t stop { at };
                        while (stop < value.size() && ascii_space(value[stop]) == false)
                            ++stop;
                        const std::u8string_view token { std::u8string_view { value }.substr(at, stop - at) };
                        underline = underline || token == u8"underline";
                        none = none || token == u8"none";
                        at = stop;
                    }
                    if (underline)
                        values.marks.push_back({ text_mark::underline, true });
                    else if (none)
                        values.marks.push_back({ text_mark::underline, false });
                }
                else if (property == u8"white-space")
                {
                    if (value == u8"pre" || value == u8"pre-wrap" || value == u8"break-spaces")
                        values.preserve = true;
                    else if (value == u8"normal" || value == u8"nowrap")
                        values.preserve = false;
                }
            }
            return values;
        }

        // 문자 참조 하나를 푼다. `at`은 `&` 자리이고, 풀었으면 길이와 글을 돌려준다.
        //  - 숫자 꼴(`&#65;`, `&#x41;`, `;`는 없어도 된다)과 이름 여섯(amp, lt, gt, quot, apos, nbsp, `;`가 있어야 한다)만 푼다.
        //  - 0·surrogate·`U+10FFFF` 초과는 `U+FFFD`다. 모르는 이름은 풀지 않는다 (글 그대로다).
        struct decoded_reference
        {
            std::size_t length { 0 };
            std::u8string text {};
        };

        [[nodiscard]] std::optional<decoded_reference> decode_reference(const std::u8string_view source, const std::size_t at)
        {
            std::size_t cursor { at + 1 };
            if (cursor < source.size() && source[cursor] == u8'#')
            {
                ++cursor;
                const bool hex { cursor < source.size() && (source[cursor] == u8'x' || source[cursor] == u8'X') };
                if (hex)
                    ++cursor;
                const std::size_t digits_begin { cursor };
                std::uint32_t value { 0 };
                bool overflow { false };
                while (cursor < source.size())
                {
                    const char8_t unit { source[cursor] };
                    std::uint32_t digit { 0 };
                    if (ascii_digit(unit))
                        digit = static_cast<std::uint32_t>(unit - u8'0');
                    else if (hex && unit >= u8'a' && unit <= u8'f')
                        digit = static_cast<std::uint32_t>(unit - u8'a' + 10);
                    else if (hex && unit >= u8'A' && unit <= u8'F')
                        digit = static_cast<std::uint32_t>(unit - u8'A' + 10);
                    else
                        break;
                    // 넘쳐도 끝까지 읽어 길이를 맞춘다. 값은 범위 밖이라 `U+FFFD`다.
                    if (value > 0x10FFFFu)
                        overflow = true;
                    else
                        value = value * (hex ? 16u : 10u) + digit;
                    ++cursor;
                }
                if (cursor == digits_begin)
                    return std::nullopt;
                if (cursor < source.size() && source[cursor] == u8';')
                    ++cursor;
                const bool invalid { overflow || value == 0 || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu) };
                return decoded_reference { cursor - at, text::utf8_encode(invalid ? text::utf8_replacement_character : static_cast<char32_t>(value)) };
            }
            const std::size_t name_begin { cursor };
            while (cursor < source.size() && cursor - name_begin < 8 && (ascii_letter(source[cursor]) || ascii_digit(source[cursor])))
                ++cursor;
            if (cursor >= source.size() || source[cursor] != u8';')
                return std::nullopt;
            const std::u8string_view name { source.substr(name_begin, cursor - name_begin) };
            std::u8string_view value {};
            if (name == u8"amp")
                value = u8"&";
            else if (name == u8"lt")
                value = u8"<";
            else if (name == u8"gt")
                value = u8">";
            else if (name == u8"quot")
                value = u8"\"";
            else if (name == u8"apos")
                value = u8"'";
            else if (name == u8"nbsp")
                value = u8" ";
            else
                return std::nullopt;
            return decoded_reference { cursor + 1 - at, std::u8string { value } };
        }

        // 속성 값의 문자 참조를 푼다.
        [[nodiscard]] std::u8string decode_attribute(const std::u8string_view value)
        {
            std::u8string result {};
            result.reserve(value.size());
            std::size_t at { 0 };
            while (at < value.size())
            {
                if (value[at] == u8'&')
                    if (std::optional<decoded_reference> reference { decode_reference(value, at) }; reference.has_value())
                    {
                        result += reference->text;
                        at += reference->length;
                        continue;
                    }
                result += value[at];
                ++at;
            }
            return result;
        }

        // 읽은 시작 태그다. 속성은 쓰는 둘(`href`, `style`)만 남긴다.
        struct start_tag
        {
            std::u8string name {};
            std::optional<std::u8string> href {};
            std::u8string style {};
            bool self_closing { false };
        };

        // 스택에 오른 요소 하나와 그 안의 실효 상태다.
        struct open_element
        {
            std::u8string name {};
            text_mark marks { text_mark::none };
            // 실효 링크다. 비면 링크가 아니다.
            std::u8string link {};
            bool preserve { false };
            bool hidden { false };
            bool block { false };
        };

        class html_reader
        {
        public:
            html_reader(const std::u8string_view source, const rich_text_html_limits& limits)
                : source_ { source }
                , limits_ { limits }
            {
            }

            [[nodiscard]] rich_text_fragment read()
            {
                while (at_ < source_.size())
                {
                    if (source_[at_] == u8'<')
                    {
                        if (read_markup() == false)
                            break;
                        continue;
                    }
                    read_text();
                }
                return std::move(fragment_);
            }

        private:
            [[nodiscard]] const open_element& top() const noexcept
            {
                static const open_element root {};
                return stack_.empty() ? root : stack_.back();
            }

            // ---- 글 내보내기 ----

            [[nodiscard]] bool at_line_start() const noexcept
            {
                return fragment_.text.empty() || fragment_.text.back() == u8'\n';
            }

            void append(const std::u8string_view piece, const text_mark marks, const std::u8string_view link)
            {
                if (piece.empty())
                    return;
                const std::size_t begin { fragment_.text.size() };
                fragment_.text += piece;
                const std::size_t end { fragment_.text.size() };
                // 맞닿은 같은 서식과 같은 주소는 합친다 (`text_format`의 불변식).
                if (marks != text_mark::none)
                {
                    if (fragment_.format.runs.empty() == false && fragment_.format.runs.back().end == begin && fragment_.format.runs.back().marks == marks)
                        fragment_.format.runs.back().end = end;
                    else
                        fragment_.format.runs.push_back({ begin, end, marks });
                }
                if (link.empty() == false)
                {
                    if (fragment_.format.links.empty() == false && fragment_.format.links.back().end == begin && fragment_.format.links.back().destination == link)
                        fragment_.format.links.back().end = end;
                    else
                        fragment_.format.links.push_back({ begin, end, std::u8string { link } });
                }
            }

            // 미뤄 둔 블록 줄 바꿈과 공백을 내보낸다. 글이 이어질 때만 나오므로 끝의 블록·공백은 남지 않는다.
            void flush_pending()
            {
                if (break_pending_)
                {
                    append(u8"\n", text_mark::none, {});
                    break_pending_ = false;
                    space_pending_ = false;
                    return;
                }
                if (space_pending_)
                {
                    // 줄 처음의 공백은 접어 없앤다.
                    if (at_line_start() == false)
                        append(u8" ", space_marks_, space_link_);
                    space_pending_ = false;
                }
            }

            void emit(const std::u8string_view piece)
            {
                const open_element& state { top() };
                if (state.hidden || piece.empty())
                    return;
                flush_pending();
                append(piece, state.marks, state.link);
            }

            // 공백을 지키지 않는 자리의 공백이다. 여럿을 하나로 접고, 이어지는 글이 있을 때만 그 자리의 서식으로 낸다.
            void collapsible_space()
            {
                const open_element& state { top() };
                if (state.hidden || space_pending_)
                    return;
                space_pending_ = true;
                space_marks_ = state.marks;
                space_link_ = state.link;
            }

            // 블록 경계다. 앞뒤에 줄 바꿈 하나를 둔다 (겹치지 않는다). 가장자리의 공백은 버린다.
            void block_boundary()
            {
                space_pending_ = false;
                if (at_line_start() == false)
                    break_pending_ = true;
            }

            void line_break()
            {
                const open_element& state { top() };
                if (state.hidden)
                    return;
                if (break_pending_)
                {
                    append(u8"\n", text_mark::none, {});
                    break_pending_ = false;
                }
                // 줄 끝의 공백은 접혀 사라진다.
                space_pending_ = false;
                append(u8"\n", state.marks, state.link);
            }

            // 표 칸 사이의 탭이다. 줄의 첫 칸에는 두지 않는다.
            void cell_boundary()
            {
                space_pending_ = false;
                if (break_pending_ || at_line_start())
                    return;
                append(u8"\t", text_mark::none, {});
            }

            // ---- 글 ----

            void read_text()
            {
                const bool preserve { top().preserve };
                const std::size_t end { std::min(source_.find(u8'<', at_), source_.size()) };
                std::size_t run { at_ };
                const auto flush_run = [&](const std::size_t stop) {
                    if (stop > run)
                        emit(source_.substr(run, stop - run));
                };
                while (at_ < end)
                {
                    const char8_t unit { source_[at_] };
                    if (unit == u8'&')
                    {
                        flush_run(at_);
                        // 문자 참조가 낸 글자는 글 그대로다. 공백 접기를 받지 않는다.
                        if (std::optional<decoded_reference> reference { decode_reference(source_.substr(0, end), at_) }; reference.has_value())
                        {
                            emit(reference->text);
                            at_ += reference->length;
                        }
                        else
                        {
                            emit(u8"&");
                            ++at_;
                        }
                        run = at_;
                        continue;
                    }
                    if (ascii_space(unit))
                    {
                        flush_run(at_);
                        if (preserve == false)
                            collapsible_space();
                        else if (unit == u8'\r' || unit == u8'\n')
                        {
                            // 지키는 공백의 CR LF와 CR은 줄 바꿈 하나다.
                            if (unit == u8'\r' && at_ + 1 < end && source_[at_ + 1] == u8'\n')
                                ++at_;
                            emit(u8"\n");
                        }
                        else
                            emit(source_.substr(at_, 1));
                        ++at_;
                        run = at_;
                        continue;
                    }
                    ++at_;
                }
                flush_run(at_);
            }

            // ---- markup ----

            // `<`에서 시작하는 markup 하나를 읽는다. 끝나지 않은 태그를 만나 더 읽을 것이 없으면 거짓이다.
            [[nodiscard]] bool read_markup()
            {
                const std::u8string_view rest { source_.substr(at_) };
                if (rest.starts_with(u8"<!--"))
                    return skip_past(at_ + 4, u8"-->");
                if (rest.starts_with(u8"<![CDATA["))
                    return skip_past(at_ + 9, u8"]]>");
                if (rest.starts_with(u8"<!") || rest.starts_with(u8"<?"))
                    return skip_past(at_ + 2, u8">");
                if (rest.size() >= 3 && rest[1] == u8'/' && ascii_letter(rest[2]))
                    return read_end_tag();
                if (rest.size() >= 2 && ascii_letter(rest[1]))
                    return read_start_tag();
                // 태그가 아닌 `<`는 글이다.
                emit(u8"<");
                ++at_;
                return true;
            }

            // `from`부터 `terminator`를 찾아 그 뒤로 간다. 없으면 끝까지 버린다.
            [[nodiscard]] bool skip_past(const std::size_t from, const std::u8string_view terminator)
            {
                const std::size_t found { source_.find(terminator, from) };
                if (found == std::u8string_view::npos)
                {
                    at_ = source_.size();
                    return false;
                }
                at_ = found + terminator.size();
                return true;
            }

            [[nodiscard]] std::u8string read_name(std::size_t& cursor) const
            {
                const std::size_t begin { cursor };
                while (cursor < source_.size() && ascii_space(source_[cursor]) == false && source_[cursor] != u8'/' && source_[cursor] != u8'>')
                    ++cursor;
                return lowered(source_.substr(begin, cursor - begin));
            }

            [[nodiscard]] bool read_end_tag()
            {
                std::size_t cursor { at_ + 2 };
                const std::u8string name { read_name(cursor) };
                if (skip_past(cursor, u8">") == false)
                    return false;
                close_element(name);
                return true;
            }

            [[nodiscard]] bool read_start_tag()
            {
                std::size_t cursor { at_ + 1 };
                start_tag tag {};
                tag.name = read_name(cursor);
                while (true)
                {
                    while (cursor < source_.size() && ascii_space(source_[cursor]))
                        ++cursor;
                    if (cursor >= source_.size())
                    {
                        // 끝나지 않은 태그는 버린다.
                        at_ = source_.size();
                        return false;
                    }
                    const char8_t unit { source_[cursor] };
                    if (unit == u8'>')
                    {
                        ++cursor;
                        break;
                    }
                    if (unit == u8'/')
                    {
                        ++cursor;
                        if (cursor < source_.size() && source_[cursor] == u8'>')
                        {
                            tag.self_closing = true;
                            ++cursor;
                            break;
                        }
                        continue;
                    }
                    const std::size_t name_begin { cursor };
                    while (cursor < source_.size() && ascii_space(source_[cursor]) == false && source_[cursor] != u8'=' && source_[cursor] != u8'>' && source_[cursor] != u8'/')
                        ++cursor;
                    // `=`로 시작하는 이상한 이름도 한 글자는 먹어 제자리에 머물지 않는다.
                    if (cursor == name_begin)
                        ++cursor;
                    const std::u8string attribute { lowered(source_.substr(name_begin, cursor - name_begin)) };
                    while (cursor < source_.size() && ascii_space(source_[cursor]))
                        ++cursor;
                    std::u8string_view value {};
                    if (cursor < source_.size() && source_[cursor] == u8'=')
                    {
                        ++cursor;
                        while (cursor < source_.size() && ascii_space(source_[cursor]))
                            ++cursor;
                        if (cursor < source_.size() && (source_[cursor] == u8'"' || source_[cursor] == u8'\''))
                        {
                            const char8_t quote { source_[cursor] };
                            const std::size_t close { source_.find(quote, cursor + 1) };
                            if (close == std::u8string_view::npos)
                            {
                                at_ = source_.size();
                                return false;
                            }
                            value = source_.substr(cursor + 1, close - cursor - 1);
                            cursor = close + 1;
                        }
                        else
                        {
                            const std::size_t value_begin { cursor };
                            while (cursor < source_.size() && ascii_space(source_[cursor]) == false && source_[cursor] != u8'>')
                                ++cursor;
                            value = source_.substr(value_begin, cursor - value_begin);
                        }
                    }
                    if (attribute == u8"href" && tag.href.has_value() == false)
                        tag.href = decode_attribute(value);
                    else if (attribute == u8"style" && tag.style.empty())
                        tag.style = decode_attribute(value);
                }
                at_ = cursor;
                open_element_of(tag);
                return true;
            }

            // raw text 요소의 내용을 닫는 태그까지 건너뛴다. 닫는 태그가 없으면 끝까지 버린다.
            void skip_raw_text(const std::u8string_view name)
            {
                std::size_t cursor { at_ };
                while (true)
                {
                    const std::size_t found { source_.find(u8"</", cursor) };
                    if (found == std::u8string_view::npos)
                    {
                        at_ = source_.size();
                        return;
                    }
                    const std::size_t after { found + 2 + name.size() };
                    if (starts_with_ignoring_case(source_.substr(found + 2), name)
                        && (after >= source_.size() || ascii_space(source_[after]) || source_[after] == u8'>' || source_[after] == u8'/'))
                    {
                        const std::size_t close { source_.find(u8'>', after) };
                        at_ = close == std::u8string_view::npos ? source_.size() : close + 1;
                        return;
                    }
                    cursor = found + 2;
                }
            }

            void open_element_of(const start_tag& tag)
            {
                const std::u8string_view name { tag.name };
                if (raw_text_element(name))
                {
                    skip_raw_text(name);
                    return;
                }
                if (name == u8"br")
                {
                    line_break();
                    return;
                }
                if (name == u8"hr")
                {
                    if (top().hidden == false)
                        block_boundary();
                    return;
                }
                if (void_element(name))
                    return;
                // 본문이 시작되면 닫지 않은 문서 머리를 닫는다. 아니면 본문이 통째로 숨는다.
                if (name == u8"body")
                    pop_to(u8"head");

                const bool block { block_element(name) };
                if (top().hidden == false)
                {
                    if (block)
                        block_boundary();
                    else if (name == u8"td" || name == u8"th")
                        cell_boundary();
                }
                if (tag.self_closing)
                    return;
                if (stack_.size() >= limits_.max_depth)
                {
                    // 너무 깊은 요소는 서식 없이 글만 읽는다. 숨길 요소는 닫는 태그까지 건너뛴다.
                    if (hidden_element(name))
                        skip_raw_text(name);
                    else
                        ++overflow_;
                    return;
                }

                const open_element& parent { top() };
                open_element element {};
                element.name = tag.name;
                element.marks = parent.marks;
                element.link = parent.link;
                element.preserve = parent.preserve;
                element.hidden = parent.hidden || hidden_element(name);
                element.block = block;
                if (name == u8"b" || name == u8"strong")
                    element.marks = with_mark(element.marks, text_mark::bold, true);
                else if (name == u8"i" || name == u8"em")
                    element.marks = with_mark(element.marks, text_mark::italic, true);
                else if (name == u8"u" || name == u8"ins")
                    element.marks = with_mark(element.marks, text_mark::underline, true);
                else if (name == u8"code" || name == u8"kbd" || name == u8"samp" || name == u8"tt")
                    element.marks = with_mark(element.marks, text_mark::code, true);
                else if (name == u8"pre")
                    element.preserve = true;
                else if (name == u8"a")
                    element.link = tag.href.has_value() ? allowed_link(*tag.href) : std::u8string {};
                // `style`이 태그보다 앞선다. 문서 편집기는 굵게를 `style`로만 싣거나 감싸는 `<b>`를 `font-weight: normal`로 끈다.
                if (tag.style.empty() == false)
                {
                    const style_values style { parse_style(tag.style) };
                    for (const mark_override& value : style.marks)
                        element.marks = with_mark(element.marks, value.mark, value.on);
                    if (style.preserve.has_value())
                        element.preserve = *style.preserve;
                }
                stack_.push_back(std::move(element));
            }

            void close_element(const std::u8string_view name)
            {
                if (overflow_ > 0)
                {
                    --overflow_;
                    return;
                }
                if (name == u8"br")
                {
                    line_break();
                    return;
                }
                pop_to(name);
            }

            // 그 이름의 가장 안쪽 요소까지 닫는다. 그 사이의 닫지 않은 요소도 함께 닫는다. 없으면 아무 일도 없다.
            void pop_to(const std::u8string_view name)
            {
                const auto found { std::find_if(stack_.rbegin(), stack_.rend(), [name](const open_element& element) { return element.name == name; }) };
                if (found == stack_.rend())
                    return;
                const std::size_t keep { static_cast<std::size_t>(stack_.rend() - found) - 1 };
                while (stack_.size() > keep)
                {
                    const bool boundary { stack_.back().block && stack_.back().hidden == false };
                    stack_.pop_back();
                    if (boundary)
                        block_boundary();
                }
            }

            std::u8string_view source_ {};
            rich_text_html_limits limits_ {};
            std::size_t at_ { 0 };
            std::vector<open_element> stack_ {};
            // 깊이 한도 밖에서 열린 요소의 수다. 스택에 올리지 않고 세기만 한다.
            std::size_t overflow_ { 0 };
            rich_text_fragment fragment_ {};
            bool break_pending_ { false };
            bool space_pending_ { false };
            text_mark space_marks_ { text_mark::none };
            std::u8string space_link_ {};
        };
    }

    std::u8string rich_text_to_html(const std::u8string_view text, const text::text_format& format)
    {
        if (text.empty())
            return {};
        // 서식이 바뀌는 자리마다 조각을 끊는다.
        std::vector<std::size_t> cuts { 0, text.size() };
        for (const text::text_mark_run& run : format.runs)
        {
            cuts.push_back(std::min(run.begin, text.size()));
            cuts.push_back(std::min(run.end, text.size()));
        }
        for (const text::text_format_link& link : format.links)
        {
            cuts.push_back(std::min(link.begin, text.size()));
            cuts.push_back(std::min(link.end, text.size()));
        }
        std::ranges::sort(cuts);
        const auto unique { std::ranges::unique(cuts) };
        cuts.erase(unique.begin(), unique.end());

        std::u8string html { u8"<span style=\"white-space: pre-wrap;\">" };
        html.reserve(html.size() + text.size() + text.size() / 4 + 16);
        std::size_t run_index { 0 };
        std::size_t link_index { 0 };
        for (std::size_t index = 0; index + 1 < cuts.size(); ++index)
        {
            const std::size_t begin { cuts[index] };
            const std::size_t end { cuts[index + 1] };
            while (run_index < format.runs.size() && format.runs[run_index].end <= begin)
                ++run_index;
            while (link_index < format.links.size() && format.links[link_index].end <= begin)
                ++link_index;
            const text_mark marks { run_index < format.runs.size() && format.runs[run_index].begin <= begin ? format.runs[run_index].marks : text_mark::none };
            const text::text_format_link* const link { link_index < format.links.size() && format.links[link_index].begin <= begin ? &format.links[link_index] : nullptr };

            if (link != nullptr)
            {
                html += u8"<a href=\"";
                append_attribute(html, link->destination);
                html += u8"\">";
            }
            for (const mark_tag& tag : mark_tags)
            {
                if (text::has_mark(marks, tag.mark))
                    html += tag.open;
            }
            append_text(html, text.substr(begin, end - begin));
            for (auto tag = mark_tags.rbegin(); tag != mark_tags.rend(); ++tag)
            {
                if (text::has_mark(marks, tag->mark))
                    html += tag->close;
            }
            if (link != nullptr)
                html += u8"</a>";
        }
        html += u8"</span>";
        return html;
    }

    rich_text_fragment rich_text_from_html(const std::u8string_view html, const rich_text_html_limits& limits)
    {
        // 한도를 넘거나 깨진 글이면 HTML을 버린다. 부르는 쪽이 글로 물러선다.
        if (html.size() > limits.max_bytes || text::utf8_is_valid(html) == false)
            return {};
        html_reader reader { html, limits };
        return reader.read();
    }

    clipboard_data_copy_request rich_text_copy_request(const text::text_edit_state& state)
    {
        if (text::text_edit_has_selection(state) == false)
            return {};
        const std::size_t begin { text::text_edit_selection_begin(state) };
        const std::size_t end { text::text_edit_selection_end(state) };
        clipboard_data_copy_request request {};
        request.text = std::u8string { text::text_edit_selected_text(state) };
        const text::text_format slice { state.format != nullptr ? text::text_format_slice(*state.format, begin, end) : text::text_format {} };
        request.html = rich_text_to_html(request.text, slice);
        return request;
    }

    bool rich_text_paste(text::text_edit_state& state, const clipboard_data& data, const bool plain)
    {
        if (plain == false && data.html.empty() == false)
        {
            const rich_text_fragment fragment { rich_text_from_html(data.html) };
            if (fragment.text.empty() == false)
            {
                text::text_edit_insert_formatted(state, fragment.text, fragment.format);
                return true;
            }
        }
        // HTML이 없거나 거른 조각이 비었다 (한도, 깨진 UTF-8, 그림만 든 HTML). 글로 물러선다.
        if (data.text.empty())
            return false;
        text::text_edit_insert(state, data.text);
        return true;
    }
}

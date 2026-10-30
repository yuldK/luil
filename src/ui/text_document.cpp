#include "luil/ui/text_document.h"

#include <algorithm>
#include <iterator>

namespace luil {
    namespace {
        template<typename range>
        std::span<const range> visible_ranges(const std::span<const range> spans, const std::size_t begin, const std::size_t end) noexcept
        {
            if (begin >= end)
                return {};
            const auto first { std::lower_bound(spans.begin(), spans.end(), begin, [](const range& item, const auto offset) { return item.end <= offset; }) };
            const auto last { std::lower_bound(first, spans.end(), end, [](const range& item, const auto offset) { return item.begin < offset; }) };
            return { first, last };
        }

        [[nodiscard]] bool ascii_alphanumeric(const char8_t character) noexcept
        {
            return (character >= u8'0' && character <= u8'9') || (character >= u8'a' && character <= u8'z') || (character >= u8'A' && character <= u8'Z');
        }

        [[nodiscard]] bool starts_with_folded(const std::u8string_view text, const std::size_t at, const std::u8string_view prefix) noexcept
        {
            if (text.size() - at < prefix.size())
                return false;
            for (std::size_t index { 0 }; index < prefix.size(); ++index)
            {
                const char8_t character { text[at + index] };
                const char8_t folded { character >= u8'A' && character <= u8'Z' ? static_cast<char8_t>(character - u8'A' + u8'a') : character };
                if (folded != prefix[index])
                    return false;
            }
            return true;
        }

        // 주소에 드는 글자다. ASCII 밖 글자를 받지 않는 이유는 `detect_text_links`의 설명에 있다.
        [[nodiscard]] bool url_character(const char8_t character) noexcept
        {
            return character > 0x20 && character < 0x7F && character != u8'<' && character != u8'>' && character != u8'"';
        }

        // 주소 뒤에 붙은 문장 부호와 짝 없는 닫는 괄호를 뺀 끝이다.
        [[nodiscard]] std::size_t trim_url_end(const std::u8string_view text, const std::size_t begin, std::size_t end)
        {
            const auto unmatched = [&](const char8_t open, const char8_t close) {
                if (text[end - 1] != close)
                    return false;
                const auto body { text.substr(begin, end - begin) };
                return std::count(body.begin(), body.end(), close) > std::count(body.begin(), body.end(), open);
            };
            while (end > begin)
                if (std::u8string_view { u8".,;:!?'*" }.find(text[end - 1]) != std::u8string_view::npos || unmatched(u8'(', u8')') || unmatched(u8'[', u8']') || unmatched(u8'{', u8'}'))
                    --end;
                else
                    break;
            return end;
        }

        [[nodiscard]] bool mail_local_character(const char8_t character) noexcept
        {
            return ascii_alphanumeric(character) || std::u8string_view { u8"._%+-" }.find(character) != std::u8string_view::npos;
        }

        [[nodiscard]] bool domain_character(const char8_t character) noexcept
        {
            return ascii_alphanumeric(character) || character == u8'.' || character == u8'-';
        }

        // 주소 앞에 올 수 없는 글자다. 낱말이나 다른 주소의 한가운데("xhttp://", "a.www.b")에서 시작하지 않는다.
        [[nodiscard]] bool joins_word(const char8_t character) noexcept
        {
            return ascii_alphanumeric(character) || std::u8string_view { u8".-_@/" }.find(character) != std::u8string_view::npos;
        }

        [[nodiscard]] std::vector<text_link> detect_addresses(const std::u8string_view text)
        {
            struct prefix
            {
                std::u8string_view text {};
                // 주소 앞에 붙일 scheme이다. 비면 보이는 글이 곧 주소다.
                std::u8string_view scheme {};
                // 이 뒤에 점이 있어야 주소로 본다 ("www.만"은 아니다).
                bool needs_dot { false };
            };
            static constexpr prefix prefixes[] {
                { u8"https://" },
                { u8"http://" },
                { u8"mailto:" },
                { u8"www.", u8"https://", true },
            };
            std::vector<text_link> links {};
            std::size_t at { 0 };
            while (at < text.size())
            {
                bool matched { false };
                if (at == 0 || joins_word(text[at - 1]) == false)
                    for (const prefix& candidate : prefixes)
                    {
                        if (starts_with_folded(text, at, candidate.text) == false)
                            continue;
                        std::size_t end { at + candidate.text.size() };
                        while (end < text.size() && url_character(text[end]))
                            ++end;
                        end = trim_url_end(text, at, end);
                        const std::size_t body { at + candidate.text.size() };
                        const auto rest { text.substr(body, end > body ? end - body : 0) };
                        if (std::none_of(rest.begin(), rest.end(), ascii_alphanumeric) || (candidate.needs_dot && rest.find(u8'.') == std::u8string_view::npos))
                            continue;
                        links.push_back({ at, end, std::u8string { candidate.scheme } + std::u8string { text.substr(at, end - at) } });
                        at = end;
                        matched = true;
                        break;
                    }
                if (matched == false)
                    ++at;
            }
            return links;
        }

        // `이름@도메인.최상위` 꼴이다. 이미 주소인 자리(`mailto:`, `https://user@host`)의 `@`는 건너뛴다.
        [[nodiscard]] std::vector<text_link> detect_mail(const std::u8string_view text, const std::vector<text_link>& addresses)
        {
            std::vector<text_link> mail {};
            for (std::size_t at { text.find(u8'@') }; at != std::u8string_view::npos; at = text.find(u8'@', at + 1))
            {
                if (text_link_at(addresses, at) != nullptr)
                    continue;
                std::size_t begin { at };
                while (begin > 0 && mail_local_character(text[begin - 1]))
                    --begin;
                while (begin < at && text[begin] == u8'.')
                    ++begin;
                std::size_t end { at + 1 };
                while (end < text.size() && domain_character(text[end]))
                    ++end;
                while (end > at + 1 && (text[end - 1] == u8'.' || text[end - 1] == u8'-'))
                    --end;
                const auto domain { text.substr(at + 1, end - at - 1) };
                const std::size_t dot { domain.rfind(u8'.') };
                if (begin == at || dot == std::u8string_view::npos || dot == 0)
                    continue;
                const auto top { domain.substr(dot + 1) };
                const auto alphabetic = [](const char8_t character) { return (character >= u8'a' && character <= u8'z') || (character >= u8'A' && character <= u8'Z'); };
                if (top.size() < 2 || std::all_of(top.begin(), top.end(), alphabetic) == false)
                    continue;
                mail.push_back({ begin, end, u8"mailto:" + std::u8string { text.substr(begin, end - begin) } });
            }
            return mail;
        }
    }

    std::span<const text_span> text_spans_in_range(const std::span<const text_span> spans, const std::size_t begin, const std::size_t end) noexcept
    {
        return visible_ranges(spans, begin, end);
    }

    std::span<const text_highlight> text_highlights_in_range(const std::span<const text_highlight> spans, const std::size_t begin, const std::size_t end) noexcept
    {
        return visible_ranges(spans, begin, end);
    }

    std::span<const text_link> text_links_in_range(const std::span<const text_link> links, const std::size_t begin, const std::size_t end) noexcept
    {
        return visible_ranges(links, begin, end);
    }

    const text_link* text_link_at(const std::span<const text_link> links, const std::size_t offset) noexcept
    {
        const auto found { visible_ranges(links, offset, offset + 1) };
        return found.empty() ? nullptr : &found.front();
    }

    std::vector<text_link> detect_text_links(const std::u8string_view text)
    {
        std::vector<text_link> links { detect_addresses(text) };
        const std::vector<text_link> mail { detect_mail(text, links) };
        if (mail.empty())
            return links;
        // 메일 주소의 앞부분은 `@`에서 거꾸로 넓혔으므로 앞 주소에 닿는 것은 버린다.
        std::vector<text_link> merged {};
        merged.reserve(links.size() + mail.size());
        std::merge(std::make_move_iterator(links.begin()), std::make_move_iterator(links.end()), mail.begin(), mail.end(), std::back_inserter(merged),
            [](const text_link& left, const text_link& right) { return left.begin < right.begin; });
        std::vector<text_link> result {};
        result.reserve(merged.size());
        for (text_link& link : merged)
            if (result.empty() || result.back().end <= link.begin)
                result.push_back(std::move(link));
        return result;
    }
}

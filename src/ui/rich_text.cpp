#include "luil/ui/rich_text.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace luil {
    namespace {
        constexpr text::text_mark all_marks { text::text_mark::bold | text::text_mark::italic | text::text_mark::underline | text::text_mark::code };

        [[nodiscard]] std::optional<text::text_mark> mark_of(const rich_text_command command) noexcept
        {
            switch (command)
            {
            case rich_text_command::bold:
                return text::text_mark::bold;
            case rich_text_command::italic:
                return text::text_mark::italic;
            case rich_text_command::underline:
                return text::text_mark::underline;
            case rich_text_command::code:
                return text::text_mark::code;
            case rich_text_command::clear_format:
                return all_marks;
            case rich_text_command::link:
            case rich_text_command::copy:
            case rich_text_command::cut:
            case rich_text_command::paste:
            case rich_text_command::paste_plain:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] text::text_mark toggled(const text::text_mark marks, const text::text_mark mark) noexcept
        {
            return static_cast<text::text_mark>(static_cast<std::uint8_t>(marks) ^ static_cast<std::uint8_t>(mark));
        }

        [[nodiscard]] const text::text_format& format_of(const text::text_edit_state& state) noexcept
        {
            static const text::text_format empty {};
            return state.format != nullptr ? *state.format : empty;
        }

        // caret이 선 링크다. 링크 안이거나 바로 뒤다 (링크 끝 글자를 누르면 caret이 링크 뒤에 선다).
        [[nodiscard]] const text::text_format_link* link_at_caret(const text::text_format& format, const std::size_t caret) noexcept
        {
            for (const text::text_format_link& link : format.links)
            {
                if (link.begin >= caret)
                    break;
                if (caret <= link.end)
                    return &link;
            }
            return nullptr;
        }

        // [begin, end)를 품은 링크 하나다.
        [[nodiscard]] const text::text_format_link* link_covering(const text::text_format& format, const std::size_t begin, const std::size_t end) noexcept
        {
            for (const text::text_format_link& link : format.links)
            {
                if (link.begin > begin)
                    break;
                if (end <= link.end)
                    return &link;
            }
            return nullptr;
        }
    }

    rich_text_state rich_text_state_of(const text::text_edit_state& state)
    {
        const text::text_format& format { format_of(state) };
        rich_text_state result {};
        if (text::text_edit_has_selection(state))
        {
            const std::size_t begin { text::text_edit_selection_begin(state) };
            const std::size_t end { text::text_edit_selection_end(state) };
            result.bold = text::text_marks_cover(format, begin, end, text::text_mark::bold);
            result.italic = text::text_marks_cover(format, begin, end, text::text_mark::italic);
            result.underline = text::text_marks_cover(format, begin, end, text::text_mark::underline);
            result.code = text::text_marks_cover(format, begin, end, text::text_mark::code);
            result.link = link_covering(format, begin, end) != nullptr;
            return result;
        }
        const text::text_mark marks { text::text_edit_typing_marks(state) };
        result.bold = text::has_mark(marks, text::text_mark::bold);
        result.italic = text::has_mark(marks, text::text_mark::italic);
        result.underline = text::has_mark(marks, text::text_mark::underline);
        result.code = text::has_mark(marks, text::text_mark::code);
        result.link = link_at_caret(format, state.caret) != nullptr;
        return result;
    }

    bool apply_rich_text_command(text::text_edit_state& state, const rich_text_command command)
    {
        const std::optional<text::text_mark> mark { mark_of(command) };
        if (mark.has_value() == false)
            return false;
        const bool clear { command == rich_text_command::clear_format };
        if (text::text_edit_has_selection(state) == false)
        {
            // 고른 구간이 없으면 다음 글자의 서식만 바꾼다. 앞 글자에서 이어받을 서식에서 그 비트를 뒤집는다.
            state.typing_marks = clear ? text::text_mark::none : toggled(text::text_edit_typing_marks(state), *mark);
            return true;
        }
        const text::text_format& format { format_of(state) };
        const std::size_t begin { text::text_edit_selection_begin(state) };
        const std::size_t end { text::text_edit_selection_end(state) };
        const bool on { clear == false && text::text_marks_cover(format, begin, end, *mark) == false };
        text::text_format next { text::text_format_with_mark(format, begin, end, *mark, on) };
        if (next == format)
            return false;
        text::text_edit_set_format(state, std::make_shared<const text::text_format>(std::move(next)));
        return true;
    }

    bool set_rich_text_link(text::text_edit_state& state, const std::u8string_view destination)
    {
        const text::text_format& format { format_of(state) };
        std::size_t begin { text::text_edit_selection_begin(state) };
        std::size_t end { text::text_edit_selection_end(state) };
        if (begin == end)
        {
            const text::text_format_link* const link { link_at_caret(format, state.caret) };
            if (link == nullptr)
                return false;
            begin = link->begin;
            end = link->end;
        }
        text::text_format next { text::text_format_with_link(format, begin, end, destination) };
        if (next == format)
            return false;
        text::text_edit_set_format(state, std::make_shared<const text::text_format>(std::move(next)));
        return true;
    }

    void rich_text_presenter::update(const text::text_edit_state& state)
    {
        if (updated_ && state.format == format_)
            return;
        updated_ = true;
        format_ = state.format;
        if (format_ == nullptr)
        {
            spans_.reset();
            links_.reset();
            return;
        }
        std::vector<text_span> spans {};
        spans.reserve(format_->runs.size());
        for (const text::text_mark_run& run : format_->runs)
        {
            text_span span {};
            span.begin = run.begin;
            span.end = run.end;
            span.bold = text::has_mark(run.marks, text::text_mark::bold);
            span.italic = text::has_mark(run.marks, text::text_mark::italic);
            span.underline = text::has_mark(run.marks, text::text_mark::underline);
            span.code = text::has_mark(run.marks, text::text_mark::code);
            spans.push_back(span);
        }
        std::vector<text_link> links {};
        links.reserve(format_->links.size());
        for (const text::text_format_link& link : format_->links)
            links.push_back({ link.begin, link.end, link.destination });
        spans_ = std::make_shared<const std::vector<text_span>>(std::move(spans));
        links_ = std::make_shared<const std::vector<text_link>>(std::move(links));
    }

    std::shared_ptr<const std::vector<text_span>> rich_text_presenter::spans() const noexcept
    {
        return spans_;
    }

    std::shared_ptr<const std::vector<text_link>> rich_text_presenter::links() const noexcept
    {
        return links_;
    }
}

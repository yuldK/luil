#include "luil/ui/syntax_highlighter.h"

#include "luil/text/grapheme.h"
#include "luil/text/text_change.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace luil {
    namespace {
        using time_source = std::chrono::steady_clock;

        struct line_end
        {
            std::size_t end { 0 };
            bool terminated { false };
        };

        // `from`에서 시작하는 줄의 끝(줄 바꿈 바이트 다음)이다. [from, limit) 안에 줄 바꿈이 없으면 limit이다.
        [[nodiscard]] line_end find_line_end(const std::u8string_view text, const std::size_t from, const std::size_t limit) noexcept
        {
            for (std::size_t at { from }; at < limit; ++at)
            {
                if (text[at] == u8'\n')
                    return { at + 1, true };
                if (text[at] == u8'\r')
                    return { at + 1 < text.size() && text[at + 1] == u8'\n' ? at + 2 : at + 1, true };
            }
            return { limit, false };
        }

        // 줄 바꿈을 뺀 줄 글의 길이다. 줄 글에는 CR도 LF도 들지 않으므로 끝 바이트만 보면 된다.
        [[nodiscard]] std::size_t content_size(const std::u8string_view text, const std::size_t start, const std::size_t length) noexcept
        {
            if (length == 0)
                return 0;
            const char8_t last { text[start + length - 1] };
            if (last == u8'\r')
                return length - 1;
            if (last != u8'\n')
                return length;
            return length >= 2 && text[start + length - 2] == u8'\r' ? length - 2 : length - 1;
        }

        [[nodiscard]] bool same_span(const text_span& first, const text_span& second) noexcept
        {
            return first.begin == second.begin
                && first.end == second.end
                && first.color == second.color
                && first.bold == second.bold
                && first.background == second.background
                && first.italic == second.italic;
        }

        [[nodiscard]] bool same_spans(const std::vector<text_span>& first, const std::vector<text_span>& second) noexcept
        {
            return first.size() == second.size()
                && std::equal(first.begin(), first.end(), second.begin(), same_span);
        }

        [[nodiscard]] bool is_ascii(const std::u8string_view text) noexcept
        {
            return std::ranges::all_of(text, [](const char8_t byte) { return byte < 0x80; });
        }

        // 렉서가 낸 구간을 다듬어 문서 offset으로 `out` 뒤에 붙이고, 붙인 수를 돌려준다.
        //  - 줄 글 밖과 빈 구간을 버리고, begin 순으로 정렬한다 (같은 begin은 낸 순서를 지킨다).
        //  - 양끝을 글자소 경계로 맞춘다 (begin은 앞으로, end는 뒤로). ASCII뿐인 줄은 글자소 계산을 건너뛴다.
        //  - 겹친 몫은 앞 구간이 갖는다. 뒤 구간의 begin을 앞 구간의 끝으로 밀고, 비면 버린다.
        std::size_t tidy_spans(const std::u8string_view line, const std::size_t line_offset, std::vector<text_span>& raw, std::vector<text_span>& out)
        {
            std::size_t kept { 0 };
            for (const text_span& span : raw)
            {
                if (span.begin >= span.end
                    || span.begin >= line.size())
                    continue;
                raw[kept] = span;
                raw[kept].end = std::min(span.end, line.size());
                ++kept;
            }
            raw.resize(kept);
            if (std::ranges::is_sorted(raw, {}, &text_span::begin) == false)
                std::ranges::stable_sort(raw, {}, &text_span::begin);

            std::vector<std::size_t> boundaries {};
            if (is_ascii(line) == false)
                boundaries = text::grapheme_boundaries(line);

            const std::size_t before { out.size() };
            std::size_t cursor { 0 };
            for (text_span span : raw)
            {
                if (boundaries.empty() == false)
                {
                    span.begin = *std::prev(std::upper_bound(boundaries.begin(), boundaries.end(), span.begin));
                    span.end = *std::lower_bound(boundaries.begin(), boundaries.end(), span.end);
                }
                span.begin = std::max(span.begin, cursor);
                if (span.begin >= span.end)
                    continue;
                cursor = span.end;
                span.begin += line_offset;
                span.end += line_offset;
                out.push_back(span);
            }
            return out.size() - before;
        }

        // 줄 기록으로 쪼갠 [begin, end)의 길이들이다. `include_final`이면 글 끝의 줄(줄 바꿈이 없는 마지막 줄)이 이
        // 구간에 든다. 글이 줄 바꿈으로 끝나면 그 뒤에 빈 줄이 하나 있다.
        [[nodiscard]] std::vector<std::size_t> split_lengths(const std::u8string_view text, const std::size_t begin, const std::size_t end, const bool include_final)
        {
            std::vector<std::size_t> lengths {};
            bool terminated { true };
            for (std::size_t at { begin }; at < end;)
            {
                const line_end found { find_line_end(text, at, end) };
                lengths.push_back(found.end - at);
                terminated = found.terminated;
                at = found.end;
            }
            if (include_final
                && terminated)
                lengths.push_back(0);
            return lengths;
        }

        // 이어서 칠하는 줄들이 만든 구간을 평면 vector의 한 자리에 한 번에 바꿔 넣기 위한 묶음이다.
        // 줄마다 바꿔 넣으면 구간이 많은 글에서 줄 수 곱하기 구간 수만큼 옮겨 쓴다.
        struct span_run
        {
            bool active { false };
            // 칠하기 전 배치 기준의 시작 자리와 그 줄들이 쥐고 있던 구간 수다.
            std::size_t begin { 0 };
            std::size_t old_count { 0 };
            std::vector<text_span> fresh {};
        };
    }

    syntax_highlighter::syntax_highlighter(syntax_highlighter_config config)
        : config_ { std::move(config) }
    {
    }

    bool syntax_highlighter::pending() const noexcept
    {
        return pending_lines_.empty() == false;
    }

    const syntax_highlight_stats& syntax_highlighter::stats() const noexcept
    {
        return stats_;
    }

    void syntax_highlighter::reset()
    {
        text_.clear();
        lines_.clear();
        spans_.clear();
        pending_lines_.clear();
        resume_known_ = false;
        loaded_ = false;
        touched_ = true;
        stats_ = {};
    }

    std::shared_ptr<const std::vector<text_span>> syntax_highlighter::spans()
    {
        if (published_ == nullptr
            || (touched_ && same_spans(*published_, spans_) == false))
            published_ = std::make_shared<const std::vector<text_span>>(spans_);
        touched_ = false;
        return published_;
    }

    void syntax_highlighter::update(const std::u8string_view text, const std::chrono::microseconds budget)
    {
        stats_.relexed = 0;
        std::size_t forced_end { 0 };
        if (loaded_ == false)
            load(text);
        else if (const text::text_change change { text::text_change_between(text_, text) }; change.empty() == false)
            forced_end = apply_change(change.offset, change.removed, change.inserted, text);
        work(forced_end, budget);
        stats_.lines = lines_.size();
        stats_.spans = spans_.size();
    }

    void syntax_highlighter::advance(const std::chrono::microseconds budget)
    {
        stats_.relexed = 0;
        // 예산이 아무리 작아도 대기 중인 첫 줄은 칠한다. 호출이 제자리걸음하지 않는다.
        if (pending_lines_.empty() == false)
            work(pending_lines_.front() + 1, budget);
        stats_.lines = lines_.size();
        stats_.spans = spans_.size();
    }

    void syntax_highlighter::load(const std::u8string_view text)
    {
        text_.assign(text);
        spans_.clear();
        lines_.clear();
        for (const std::size_t length : split_lengths(text_, 0, text_.size(), true))
            lines_.push_back({ .length = length });
        pending_lines_.assign(1, 0);
        resume_known_ = false;
        loaded_ = true;
        touched_ = true;
    }

    std::size_t syntax_highlighter::apply_change(const std::size_t offset, const std::size_t removed, const std::size_t inserted, const std::u8string_view text)
    {
        // 바뀐 구간이 시작하는 줄이다. 줄 처음에서 시작하는 편집은 그 줄 것이다.
        std::size_t first { 0 };
        std::size_t first_offset { 0 };
        std::size_t first_span { 0 };
        while (first + 1 < lines_.size()
            && first_offset + lines_[first].length <= offset)
        {
            first_offset += lines_[first].length;
            first_span += lines_[first].span_count;
            ++first;
        }
        // 윗줄이 CR로 끝나면 편집이 그 CR을 CRLF로 잇거나 가른다. 윗줄 글은 그대로여도 줄 바꿈의 길이가 바뀐다.
        if (first > 0
            && first_offset == offset
            && text_[offset - 1] == u8'\r')
        {
            --first;
            first_offset -= lines_[first].length;
            first_span -= lines_[first].span_count;
        }

        // 바뀐 구간의 끝이 든 줄이다. 그 줄 뒤는 글이 그대로이고 옮겨 읽기만 한다.
        const std::size_t removed_end { offset + removed };
        std::size_t last { first };
        std::size_t last_end { first_offset + lines_[first].length };
        std::size_t region_spans { lines_[first].span_count };
        while (last + 1 < lines_.size()
            && last_end <= removed_end)
        {
            ++last;
            last_end += lines_[last].length;
            region_spans += lines_[last].span_count;
        }

        const bool include_final { last + 1 == lines_.size() };
        const std::size_t new_end { last_end - removed + inserted };
        text_.replace(offset, removed, text.substr(offset, inserted));

        std::vector<line_record> fresh {};
        for (const std::size_t length : split_lengths(text_, first_offset, new_end, include_final))
            fresh.push_back({ .length = length });
        // 바뀐 구간 안의 옛 구간은 칠할 때 한 번에 바꿔 넣는다. 그때까지 맨 앞 줄이 그 수를 대신 쥔다.
        fresh.front().span_count = static_cast<std::uint32_t>(region_spans);

        // 바뀐 구간 뒤의 구간은 글이 밀린 만큼 옮긴다. 덧셈이 되감겨도 뺄셈이 되는 unsigned 연산이다.
        const std::size_t shift { inserted - removed };
        if (shift != 0)
        {
            const auto tail { spans_.begin() + static_cast<std::ptrdiff_t>(first_span + region_spans) };
            for (auto span { tail }; span != spans_.end(); ++span)
            {
                span->begin += shift;
                span->end += shift;
            }
        }

        const std::size_t replaced { last - first + 1 };
        if (fresh.size() == replaced)
            std::ranges::copy(fresh, lines_.begin() + static_cast<std::ptrdiff_t>(first));
        else
        {
            const auto position { lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(first), lines_.begin() + static_cast<std::ptrdiff_t>(last + 1)) };
            lines_.insert(position, fresh.begin(), fresh.end());
        }

        // 바뀐 구간 안의 대기 줄은 이번에 칠하니 버리고, 뒤의 대기 줄은 줄 수가 바뀐 만큼 옮긴다.
        std::vector<std::size_t> pending {};
        for (const std::size_t line : pending_lines_)
            if (line < first)
                pending.push_back(line);
        pending.push_back(first);
        for (const std::size_t line : pending_lines_)
            if (line > last)
                pending.push_back(line - replaced + fresh.size());
        pending_lines_ = std::move(pending);
        resume_known_ = false;
        touched_ = true;
        return first + fresh.size();
    }

    void syntax_highlighter::work(const std::size_t forced_end, const std::chrono::microseconds budget)
    {
        if (pending_lines_.empty())
            return;
        const bool timed { budget != syntax_unlimited_budget };
        const time_source::time_point started { timed ? time_source::now() : time_source::time_point {} };
        const auto exhausted = [&] { return timed && std::chrono::duration_cast<std::chrono::microseconds>(time_source::now() - started) >= budget; };

        std::size_t line_index { pending_lines_.front() };
        std::size_t offset { 0 };
        std::size_t span_cursor { 0 };
        if (resume_known_
            && resume_line_ == line_index)
        {
            offset = resume_offset_;
            span_cursor = resume_span_;
        }
        else
        {
            for (std::size_t index { 0 }; index < line_index; ++index)
            {
                offset += lines_[index].length;
                span_cursor += lines_[index].span_count;
            }
        }

        std::size_t next_pending { 0 };
        // 앞서 끝난 묶음이 구간 수를 바꾼 만큼, 칠하기 전 배치의 자리가 밀린다.
        std::ptrdiff_t span_shift { 0 };
        span_run run {};
        const auto commit = [&] {
            if (run.active == false)
                return;
            run.active = false;
            const auto position { spans_.begin() + static_cast<std::ptrdiff_t>(run.begin) + span_shift };
            const std::size_t common { std::min(run.old_count, run.fresh.size()) };
            std::copy_n(run.fresh.begin(), common, position);
            if (run.fresh.size() > run.old_count)
                spans_.insert(position + static_cast<std::ptrdiff_t>(common), run.fresh.begin() + static_cast<std::ptrdiff_t>(common), run.fresh.end());
            else if (run.fresh.size() < run.old_count)
                spans_.erase(position + static_cast<std::ptrdiff_t>(common), position + static_cast<std::ptrdiff_t>(run.old_count));
            span_shift += static_cast<std::ptrdiff_t>(run.fresh.size()) - static_cast<std::ptrdiff_t>(run.old_count);
            touched_ = true;
        };

        const std::u8string_view text { text_ };
        while (true)
        {
            while (next_pending < pending_lines_.size()
                && pending_lines_[next_pending] <= line_index)
                ++next_pending;
            if (line_index >= lines_.size())
                break;

            line_record& line { lines_[line_index] };
            const int input { line_index == 0 ? config_.initial_state : lines_[line_index - 1].end_state };
            if (line.lexed
                && line.in_state == input)
            {
                // 지난번과 같은 글에 같은 입력 상태라 지난 결과가 그대로 맞다. 다음 대기 줄로 건넌다.
                commit();
                if (next_pending >= pending_lines_.size())
                {
                    line_index = lines_.size();
                    break;
                }
                for (const std::size_t target { pending_lines_[next_pending] }; line_index < target; ++line_index)
                {
                    offset += lines_[line_index].length;
                    span_cursor += lines_[line_index].span_count;
                }
                continue;
            }
            if (line_index >= forced_end
                && exhausted())
                break;

            if (run.active == false)
            {
                run.active = true;
                run.begin = span_cursor;
                run.old_count = 0;
                run.fresh.clear();
            }
            const std::size_t size { content_size(text_, offset, line.length) };
            int end_state { input };
            std::size_t added { 0 };
            if (size <= config_.max_line_size
                && config_.lexer)
            {
                const std::u8string_view content { text.substr(offset, size) };
                raw_.clear();
                end_state = config_.lexer(content, input, raw_);
                added = tidy_spans(content, offset, raw_, run.fresh);
                ++stats_.relexed;
            }
            run.old_count += line.span_count;
            span_cursor += line.span_count;
            line.span_count = static_cast<std::uint32_t>(added);
            line.in_state = input;
            line.end_state = end_state;
            line.lexed = true;
            offset += line.length;
            ++line_index;
        }
        commit();

        // 멈춘 자리부터는 아직 칠하지 않은 줄이다. 그 뒤의 대기 줄은 그대로 이어 간다.
        std::vector<std::size_t> rest {};
        if (line_index < lines_.size())
        {
            rest.push_back(line_index);
            for (; next_pending < pending_lines_.size(); ++next_pending)
                if (pending_lines_[next_pending] > line_index)
                    rest.push_back(pending_lines_[next_pending]);
        }
        pending_lines_ = std::move(rest);
        // 칠한 묶음을 다 반영한 뒤의 자리다. 구간 자리는 묶음이 바꾼 수만큼 밀려 있다.
        resume_known_ = pending_lines_.empty() == false;
        resume_line_ = line_index;
        resume_offset_ = offset;
        resume_span_ = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(span_cursor) + span_shift);
        stats_.spans = spans_.size();
    }
}

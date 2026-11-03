#include "luil/text/text_edit.h"

#include "luil/text/text_change.h"
#include "luil/text/utf8_text.h"
#include "luil/text/grapheme.h"
#include "text/grapheme_internal.h"
#include "text/text_format_internal.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace luil::text {
    namespace {
        bool is_continuation(const char8_t value) noexcept
        {
            return (static_cast<unsigned int>(value) & 0xC0u) == 0x80u;
        }

        // 낱말 문자다.
        // 비 ASCII는 모두 낱말로 본다.
        //  - 한글 경로 조각이 통째로 움직인다.
        bool is_word_byte(const char8_t value) noexcept
        {
            if (value >= 0x80u)
                return true;
            return (value >= u8'a' && value <= u8'z') || (value >= u8'A' && value <= u8'Z') || (value >= u8'0' && value <= u8'9');
        }

        std::size_t previous_boundary(const std::u8string_view text, std::size_t offset) noexcept
        {
            return grapheme_previous(text, offset);
        }

        std::size_t next_boundary(const std::u8string_view text, const std::size_t offset) noexcept
        {
            return grapheme_next(text, offset);
        }

        bool is_line_break(const char8_t value) noexcept
        {
            return value == u8'\r' || value == u8'\n';
        }

        // 줄 하나의 글자소 경계를 한 번 모아 두고 그 안에서 걷는다.
        //  - 낱말 단위 명령은 글자소마다 경계를 묻는다. `grapheme_previous`·`grapheme_next`는 부를 때마다 offset을
        //    감싼 줄을 다시 훑으므로, 긴 줄에서는 낱말 하나가 줄 길이 × 글자 수가 된다.
        //  - 경계는 CR·LF를 넘지 않는다 (GB4·GB5). 줄 바꿈 둘레는 그 자리에서 답하고, 다른 줄로 넘어가면 그 줄을 모은다.
        class grapheme_walker
        {
        public:
            explicit grapheme_walker(const std::u8string_view text) noexcept
                : text_ { text }
            {
            }

            std::size_t previous(const std::size_t offset)
            {
                if (offset == 0 || is_line_break(text_[offset - 1]))
                    return previous_boundary(text_, offset);
                load(offset - 1);
                const auto found { std::lower_bound(boundaries_.begin(), boundaries_.end(), offset - begin_) };
                return begin_ + *(found - 1);
            }

            std::size_t next(const std::size_t offset)
            {
                if (offset >= text_.size() || is_line_break(text_[offset]))
                    return next_boundary(text_, offset);
                load(offset);
                const auto found { std::upper_bound(boundaries_.begin(), boundaries_.end(), offset - begin_) };
                return begin_ + *found;
            }

        private:
            // 줄 바꿈이 아닌 `at` 바이트가 든 줄을 모은다.
            void load(const std::size_t at)
            {
                if (boundaries_.empty() == false && at >= begin_ && at < end_)
                    return;
                begin_ = at;
                while (begin_ > 0 && is_line_break(text_[begin_ - 1]) == false)
                    --begin_;
                end_ = at;
                while (end_ < text_.size() && is_line_break(text_[end_]) == false)
                    ++end_;
                boundaries_ = grapheme_boundaries(text_.substr(begin_, end_ - begin_));
            }

            std::u8string_view text_ {};
            std::size_t begin_ { 0 };
            std::size_t end_ { 0 };
            std::vector<std::size_t> boundaries_ {};
        };

        // 왼쪽으로: 구분자를 먼저 지나고 그다음 낱말을 지난다.
        std::size_t word_left(const std::u8string_view text, std::size_t offset)
        {
            grapheme_walker walker { text };
            while (offset > 0 && is_word_byte(text[walker.previous(offset)]) == false)
                offset = walker.previous(offset);
            while (offset > 0 && is_word_byte(text[walker.previous(offset)]))
                offset = walker.previous(offset);
            return offset;
        }

        // 오른쪽으로: 낱말을 먼저 지나고 그다음 구분자를 지나 다음 낱말의 시작에 선다.
        std::size_t word_right(const std::u8string_view text, std::size_t offset)
        {
            grapheme_walker walker { text };
            while (offset < text.size() && is_word_byte(text[offset]))
                offset = walker.next(offset);
            while (offset < text.size() && is_word_byte(text[offset]) == false)
                offset = walker.next(offset);
            return offset;
        }
        // 선택 범위만 지운다.
        // 기록은 호출한 쪽이 연다. 서식은 여기서 함께 옮긴다 (`shift_format`은 아래에 있어 앞에 선언한다).
        void shift_format(text_edit_state& state, const text_change& change, text_mark inserted);

        void erase_selection_only(text_edit_state& state)
        {
            if (text_edit_has_selection(state) == false)
                return;
            const std::size_t begin { text_edit_selection_begin(state) };
            const std::size_t length { text_edit_selection_end(state) - begin };
            state.text.erase(begin, length);
            state.caret = begin;
            state.anchor = begin;
            shift_format(state, { begin, length, 0 }, text_mark::none);
        }

        // 시점 하나가 쥔 바이트다. 나눠 쥔 서식도 시점마다 센다 (실제보다 크게 세어 상한을 넘지 않는 쪽이다).
        std::size_t revision_bytes(const text_edit_revision& revision) noexcept
        {
            return revision.text.size() + (revision.format != nullptr ? text_format_bytes(*revision.format) : 0);
        }

        std::size_t history_bytes(const std::vector<text_edit_revision>& revisions) noexcept
        {
            std::size_t bytes { 0 };
            for (const text_edit_revision& revision : revisions)
                bytes += revision_bytes(revision);
            return bytes;
        }

        // 앞(가장 먼 시점)부터 버려 `bytes`를 상한 안으로 줄인다.
        // 마지막 하나(바로 이웃한 시점)는 상한을 넘어도 남긴다.
        void drop_oldest(std::vector<text_edit_revision>& revisions, std::size_t& bytes)
        {
            std::size_t count { 0 };
            while (bytes > text_edit_history_byte_limit && count + 1 < revisions.size())
            {
                bytes -= revision_bytes(revisions[count]);
                ++count;
            }
            revisions.erase(revisions.begin(), revisions.begin() + static_cast<std::ptrdiff_t>(count));
        }

        // 기록을 개수와 바이트 상한 안으로 줄인다.
        // 기록이 늘어나는 모든 자리(새 덩어리, 실행 취소, 다시 실행)가 부른다.
        //  - 가장 먼 과거(undo 앞)를 먼저 버린다. 그래도 넘치면 가장 먼 미래(redo 앞)를 버린다.
        //    redo의 뒤가 다음에 다시 실행할 시점이라 앞이 가장 멀다.
        //  - 바이트는 `size()`로 센다. 시점은 복사로 만들어 용량이 길이와 거의 같다.
        void trim_history(text_edit_state& state)
        {
            if (state.undo.size() > text_edit_history_limit)
                state.undo.erase(state.undo.begin(), state.undo.end() - static_cast<std::ptrdiff_t>(text_edit_history_limit));
            std::size_t bytes { history_bytes(state.undo) + history_bytes(state.redo) };
            drop_oldest(state.undo, bytes);
            drop_oldest(state.redo, bytes);
        }

        // 새 편집 덩어리를 연다.
        // 종류가 같고 caret이 직전 편집이 끝난 자리에서 이어질 때만 묶어 기록을 남기지 않는다.
        void begin_edit(text_edit_state& state, const text_edit_group group)
        {
            const bool joins { group != text_edit_group::replace && group == state.group && state.caret == state.group_caret && state.undo.empty() == false };
            state.redo.clear();
            if (joins == false)
            {
                state.undo.push_back({ state.text, state.caret, state.anchor, state.format });
                trim_history(state);
            }
            state.group = group;
        }

        // 편집이 끝난 자리를 기억한다.
        // 다음 같은 종류의 편집이 여기서 이어지면 묶인다.
        // 다음 글자 서식은 넣은 글이 받았거나(넣기) 그 자리가 바뀌어(지우기) 비운다.
        void end_edit(text_edit_state& state)
        {
            state.group_caret = state.caret;
            state.typing_marks.reset();
        }

        // caret 이동·선택은 다음 편집을 새 덩어리로 만든다.
        // 다음 글자 서식도 그 자리의 것이 아니게 되어 비운다.
        void break_group(text_edit_state& state)
        {
            state.group = text_edit_group::none;
            state.typing_marks.reset();
        }

        // [begin, end)를 대신해 넣는 글이 이어받는 서식이다 (`typing_marks`가 없을 때).
        //  - 고른 구간을 대체하면 고른 첫 글자의 서식이다.
        //  - 아니면 앞 글자의 서식이고, 글 처음이나 줄 처음이면 뒤 글자의 서식이다.
        text_mark inherited_marks(const text_format* const format, const std::u8string_view text, const std::size_t begin, const std::size_t end) noexcept
        {
            if (format == nullptr)
                return text_mark::none;
            if (begin < end)
                return text_marks_at(*format, begin + 1);
            if (begin == 0 || (begin <= text.size() && is_line_break(text[begin - 1])))
                return begin < text.size() ? text_marks_at(*format, begin + 1) : text_mark::none;
            return text_marks_at(*format, begin);
        }

        // 글이 `change`만큼 바뀐 뒤 서식을 옮기고 바뀐 자리의 경계를 글자소 경계로 맞춘다.
        //  - 서식이 없는 칸은 넣은 글에 서식이 없는 한 아무것도 하지 않는다. 한 줄 칸과 서식을 쓰지 않는 칸은 비용이 없다.
        //  - 서식이 없던 칸도 다음 글자 서식(`typing_marks`)을 받은 글을 넣으면 그때 서식이 생긴다.
        void shift_format(text_edit_state& state, const text_change& change, const text_mark inserted)
        {
            if (change.empty())
                return;
            if (state.format == nullptr
                && (inserted == text_mark::none || change.inserted == 0))
                return;
            static const text_format empty {};
            text_format shifted { text_format_shifted(state.format != nullptr ? *state.format : empty, change, inserted) };
            snap_text_format(shifted, state.text, change.offset, change.offset + change.inserted);
            state.format = std::make_shared<const text_format>(std::move(shifted));
        }

        [[nodiscard]] bool codepoint_start(const std::u8string_view text, const std::size_t offset) noexcept
        {
            return offset >= text.size() || is_continuation(text[offset]) == false;
        }

        // `text_change_between`은 접두를 먼저 가져가 바뀐 구간을 가장 뒤에 둔다. 같은 글자가 이어진 자리에서는 바뀐
        // 구간이 여럿일 수 있어(`aa`에서 `a` 하나를 지움), 변경의 끝이 새 caret에 닿도록 그 범위 안에서 앞으로 옮긴다.
        //  - caret은 편집이 끝난 자리다. 첫 `a` 앞에서 Delete를 누른 글은 첫 `a`를 지운 것이고, 남은 `a`가 둘째 `a`의 서식을 갖는다.
        //  - 한 바이트씩 민다. 창 바로 앞 바이트가 창의 마지막 바이트와 같으면 두 글 모두에서 같은 변경이다.
        //    코드포인트 경계에 선 자리만 답으로 고른다.
        [[nodiscard]] text_change change_toward_caret(const std::u8string_view before, const std::u8string_view after, text_change change, const std::size_t caret) noexcept
        {
            text_change chosen { change };
            while (change.offset > 0 && change.offset + change.inserted > caret)
            {
                const std::size_t last { change.offset - 1 };
                if (change.removed > 0 && before[last] != before[last + change.removed])
                    break;
                if (change.inserted > 0 && after[last] != after[last + change.inserted])
                    break;
                --change.offset;
                if (codepoint_start(before, change.offset)
                    && codepoint_start(after, change.offset)
                    && codepoint_start(before, change.offset + change.removed)
                    && codepoint_start(after, change.offset + change.inserted))
                    chosen = change;
            }
            return chosen;
        }

        [[nodiscard]] bool same_format(const std::shared_ptr<const text_format>& left, const std::shared_ptr<const text_format>& right) noexcept
        {
            static const text_format empty {};
            return left == right || (left != nullptr ? *left : empty) == (right != nullptr ? *right : empty);
        }
    }

    std::u8string text_edit_encode_utf8(const char32_t character)
    {
        return utf8_encode(character);
    }

    std::size_t text_edit_selection_begin(const text_edit_state& state) noexcept
    {
        return state.caret < state.anchor ? state.caret : state.anchor;
    }

    std::size_t text_edit_selection_end(const text_edit_state& state) noexcept
    {
        return state.caret < state.anchor ? state.anchor : state.caret;
    }

    bool text_edit_has_selection(const text_edit_state& state) noexcept
    {
        return state.caret != state.anchor;
    }

    std::u8string_view text_edit_selected_text(const text_edit_state& state) noexcept
    {
        const std::size_t begin { text_edit_selection_begin(state) };
        return std::u8string_view { state.text }.substr(begin, text_edit_selection_end(state) - begin);
    }

    std::size_t text_edit_clamp_offset(const std::u8string_view text, std::size_t offset) noexcept
    {
        if (offset >= text.size())
            return text.size();
        // 이어지는 바이트 한가운데를 가리키면 그 문자의 시작으로 되돌린다.
        while (offset > 0 && is_continuation(text[offset]))
            --offset;
        const std::size_t previous { grapheme_previous(text, offset) };
        return grapheme_next(text, previous) == offset ? offset : previous;
    }

    bool text_edit_erase_selection(text_edit_state& state)
    {
        if (text_edit_has_selection(state) == false)
            return false;
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        end_edit(state);
        return true;
    }

    void text_edit_insert(text_edit_state& state, const std::u8string_view text)
    {
        if (text.empty())
            return;
        // 선택을 덮어쓰거나 코드포인트 하나보다 긴 조각을 넣는 것은 묶지 않는다.
        //  - 붙여넣기와 대체는 한 번에 되돌아가야 한다.
        //  - 친 글자는 바이트가 아니라 코드포인트로 센다. 4바이트 글자(이모지, BMP 밖 한자)도 친 글자로 이어 묶인다.
        const bool replaces { text_edit_has_selection(state) || utf8_decode(text, 0).length < text.size() };
        // 넣은 글의 서식은 고른 글을 지우기 전에 묻는다. 고른 첫 글자의 서식이 지운 뒤에는 없다.
        const text_mark marks { text_edit_typing_marks(state) };
        begin_edit(state, replaces ? text_edit_group::replace : text_edit_group::insert);
        erase_selection_only(state);
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        const std::size_t at { state.caret };
        state.text.insert(at, text);
        state.caret += text.size();
        state.anchor = state.caret;
        shift_format(state, { at, 0, text.size() }, marks);
        end_edit(state);
    }

    void text_edit_backspace(text_edit_state& state)
    {
        if (text_edit_has_selection(state))
        {
            begin_edit(state, text_edit_group::replace);
            erase_selection_only(state);
            end_edit(state);
            return;
        }
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        if (state.caret == 0)
            return;
        begin_edit(state, text_edit_group::erase);
        const std::size_t begin { grapheme_backspace_offset(state.text, state.caret) };
        const std::size_t length { state.caret - begin };
        state.text.erase(begin, length);
        state.caret = begin;
        state.anchor = begin;
        shift_format(state, { begin, length, 0 }, text_mark::none);
        end_edit(state);
    }

    void text_edit_delete_forward(text_edit_state& state)
    {
        if (text_edit_has_selection(state))
        {
            begin_edit(state, text_edit_group::replace);
            erase_selection_only(state);
            end_edit(state);
            return;
        }
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        if (state.caret >= state.text.size())
            return;
        begin_edit(state, text_edit_group::erase);
        const std::size_t end { next_boundary(state.text, state.caret) };
        state.text.erase(state.caret, end - state.caret);
        state.anchor = state.caret;
        shift_format(state, { state.caret, end - state.caret, 0 }, text_mark::none);
        end_edit(state);
    }

    void text_edit_delete_word_left(text_edit_state& state)
    {
        if (text_edit_has_selection(state) == false)
        {
            state.caret = text_edit_clamp_offset(state.text, state.caret);
            state.anchor = word_left(state.text, state.caret);
        }
        if (text_edit_has_selection(state) == false)
            return;
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        end_edit(state);
    }

    void text_edit_delete_word_right(text_edit_state& state)
    {
        if (text_edit_has_selection(state) == false)
        {
            state.caret = text_edit_clamp_offset(state.text, state.caret);
            state.anchor = word_right(state.text, state.caret);
        }
        if (text_edit_has_selection(state) == false)
            return;
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        end_edit(state);
    }

    bool text_edit_undo(text_edit_state& state)
    {
        if (state.undo.empty())
            return false;
        state.redo.push_back({ state.text, state.caret, state.anchor, state.format });
        const text_edit_revision previous { std::move(state.undo.back()) };
        state.undo.pop_back();
        state.text = previous.text;
        state.format = previous.format;
        state.caret = text_edit_clamp_offset(state.text, previous.caret);
        state.anchor = text_edit_clamp_offset(state.text, previous.anchor);
        trim_history(state);
        break_group(state);
        return true;
    }

    bool text_edit_redo(text_edit_state& state)
    {
        if (state.redo.empty())
            return false;
        state.undo.push_back({ state.text, state.caret, state.anchor, state.format });
        const text_edit_revision next { std::move(state.redo.back()) };
        state.redo.pop_back();
        state.text = next.text;
        state.format = next.format;
        state.caret = text_edit_clamp_offset(state.text, next.caret);
        state.anchor = text_edit_clamp_offset(state.text, next.anchor);
        trim_history(state);
        break_group(state);
        return true;
    }

    void text_edit_move(text_edit_state& state, const text_edit_motion motion, const bool extend)
    {
        const std::u8string_view text { state.text };
        state.caret = text_edit_clamp_offset(text, state.caret);
        state.anchor = text_edit_clamp_offset(text, state.anchor);

        // 선택이 있을 때 늘리지 않는 ←/→는 선택의 끝으로 접힌다 (편집기 관례).
        if (extend == false && text_edit_has_selection(state) && (motion == text_edit_motion::left || motion == text_edit_motion::right))
        {
            state.caret = motion == text_edit_motion::left ? text_edit_selection_begin(state) : text_edit_selection_end(state);
            state.anchor = state.caret;
            break_group(state);
            return;
        }

        switch (motion)
        {
        case text_edit_motion::left:
            state.caret = previous_boundary(text, state.caret);
            break;
        case text_edit_motion::right:
            state.caret = next_boundary(text, state.caret);
            break;
        case text_edit_motion::word_left:
            state.caret = word_left(text, state.caret);
            break;
        case text_edit_motion::word_right:
            state.caret = word_right(text, state.caret);
            break;
        case text_edit_motion::line_start:
            state.caret = 0;
            break;
        case text_edit_motion::line_end:
            state.caret = text.size();
            break;
        }
        if (extend == false)
            state.anchor = state.caret;
        break_group(state);
    }

    void text_edit_select_all(text_edit_state& state)
    {
        state.anchor = 0;
        state.caret = state.text.size();
        break_group(state);
    }

    void text_edit_select_word(text_edit_state& state, const std::size_t offset)
    {
        const std::u8string_view text { state.text };
        const std::size_t at { text_edit_clamp_offset(text, offset) };
        if (text.empty())
        {
            state.caret = 0;
            state.anchor = 0;
            return;
        }

        // 끝에 섰으면 마지막 문자가 속한 덩어리를 고른다.
        grapheme_walker walker { text };
        const std::size_t sample { at < text.size() ? at : walker.previous(at) };
        const bool word { is_word_byte(text[sample]) };
        std::size_t begin { sample };
        while (begin > 0 && is_word_byte(text[walker.previous(begin)]) == word)
            begin = walker.previous(begin);
        std::size_t end { sample };
        while (end < text.size() && is_word_byte(text[end]) == word)
            end = walker.next(end);
        state.anchor = begin;
        state.caret = end;
        break_group(state);
    }

    void text_edit_place_caret(text_edit_state& state, const std::size_t offset, const bool extend)
    {
        state.caret = text_edit_clamp_offset(state.text, offset);
        if (extend == false)
            state.anchor = state.caret;
        else
            state.anchor = text_edit_clamp_offset(state.text, state.anchor);
        break_group(state);
    }

    void text_edit_replace_all(text_edit_state& state, const std::u8string_view text, const std::size_t caret)
    {
        if (state.text == text)
        {
            // 글이 같으면 caret 놓기다. caret이 움직였을 때만 다음 글자 서식을 비운다. IME가 같은 글을 다시 알려도
            // 조합 중에 누른 서식 명령이 살아 있어야 확정 글이 받는다.
            const std::size_t placed { text_edit_clamp_offset(state.text, caret) };
            if (placed != state.caret || state.anchor != placed)
                state.typing_marks.reset();
            state.caret = placed;
            state.anchor = state.caret;
            return;
        }

        // 바뀐 구간은 견주어 찾는다. IME는 글 전체를 보낸다. 넣은 글의 서식은 바꾸기 전에 묻는다.
        const std::size_t placed { std::min(caret, text.size()) };
        const text_change change { change_toward_caret(state.text, text, text_change_between(state.text, text), placed) };
        const text_mark marks { state.typing_marks.value_or(inherited_marks(state.format.get(), state.text, change.offset, change.offset + change.removed)) };

        // 조합 한 번이 실행 취소 한 덩어리다.
        // `replace`라 절대 묶이지 않는다.
        begin_edit(state, text_edit_group::replace);
        state.text = text;
        state.caret = text_edit_clamp_offset(state.text, caret);
        state.anchor = state.caret;
        shift_format(state, change, marks);
        end_edit(state);
        state.group = text_edit_group::none;
    }

    void text_edit_set_text(text_edit_state& state, const std::u8string_view text, const bool caret_to_end)
    {
        // 사용자의 편집이 아니라 밖에서 온 값이다.
        // 되돌릴 것이 없다.
        state.undo.clear();
        state.redo.clear();
        break_group(state);
        state.text = text;
        state.format.reset();
        if (caret_to_end)
        {
            state.caret = state.text.size();
            state.anchor = state.caret;
            return;
        }
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        state.anchor = text_edit_clamp_offset(state.text, state.anchor);
        state.group_caret = state.caret;
    }

    text_mark text_edit_typing_marks(const text_edit_state& state) noexcept
    {
        if (state.typing_marks.has_value())
            return *state.typing_marks;
        return inherited_marks(state.format.get(), state.text, text_edit_selection_begin(state), text_edit_selection_end(state));
    }

    void text_edit_set_format(text_edit_state& state, std::shared_ptr<const text_format> format)
    {
        if (same_format(state.format, format))
            return;
        // 서식만 바꾼 걸음이다. 글 편집과 같은 기록에 들어 Ctrl+Z가 글과 서식을 차례대로 되돌린다.
        begin_edit(state, text_edit_group::replace);
        state.format = std::move(format);
        end_edit(state);
        state.group = text_edit_group::none;
    }

    void text_edit_insert_formatted(text_edit_state& state, const std::u8string_view text, const text_format& fragment)
    {
        if (text.empty())
            return;
        // 붙여넣기다. 묶지 않는다.
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        const std::size_t at { state.caret };
        state.text.insert(at, text);
        state.caret += text.size();
        state.anchor = state.caret;
        if (state.format != nullptr
            || fragment.runs.empty() == false
            || fragment.links.empty() == false)
        {
            static const text_format empty {};
            // 넣은 자리는 서식 없이 비워 두고 조각의 서식을 옮겨 놓는다. 링크 한가운데에 넣었으면 늘어난 링크를 조각
            // 둘레에서 끊는다. 조각은 주변 서식과 섞지 않는다.
            text_format result { text_format_shifted(state.format != nullptr ? *state.format : empty, { at, 0, text.size() }, text_mark::none) };
            result = text_format_with_link(result, at, at + text.size(), {});
            for (const text_mark_run& run : fragment.runs)
                if (run.begin < run.end && run.begin < text.size())
                    result.runs.push_back({ at + run.begin, at + std::min(run.end, text.size()), run.marks });
            for (const text_format_link& link : fragment.links)
                if (link.begin < link.end && link.begin < text.size())
                    result.links.push_back({ at + link.begin, at + std::min(link.end, text.size()), link.destination });
            snap_text_format(result, state.text, at, at + text.size());
            state.format = std::make_shared<const text_format>(std::move(result));
        }
        end_edit(state);
        state.group = text_edit_group::none;
    }

    void text_edit_set_formatted_text(text_edit_state& state, const std::u8string_view text, std::shared_ptr<const text_format> format)
    {
        text_edit_set_text(state, text, false);
        if (format == nullptr)
            return;
        // 밖에서 온 값이라 글 크기로 자르고 불변식과 글자소 경계로 다듬는다. 부르는 일이 드물어 글 전체를 본다.
        text_format tidy { text_format_slice(*format, 0, text.size()) };
        snap_text_format(tidy, state.text, 0, state.text.size());
        state.format = std::make_shared<const text_format>(std::move(tidy));
    }
}

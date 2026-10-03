#include "android/android_text_input.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace luil::android {
    namespace {
        // 초점이나 Activity를 다시 붙여도 옛 편집의 완료와 겹치지 않는다.
        std::atomic<std::uint64_t> edit_sequence { 0 };
        // UTF-8 글자 하나의 길이다 (첫 바이트로 안다). 잘못된 바이트는 한 바이트로 넘긴다.
        [[nodiscard]] std::size_t sequence_length(const unsigned char lead) noexcept
        {
            if (lead < 0x80u)
                return 1;
            if ((lead & 0xE0u) == 0xC0u)
                return 2;
            if ((lead & 0xF0u) == 0xE0u)
                return 3;
            if ((lead & 0xF8u) == 0xF0u)
                return 4;
            return 1;
        }

        void append_utf8(std::u8string& output, const char32_t codepoint)
        {
            if (codepoint < 0x80u)
                output.push_back(static_cast<char8_t>(codepoint));
            else if (codepoint < 0x800u)
            {
                output.push_back(static_cast<char8_t>(0xC0u | (codepoint >> 6)));
                output.push_back(static_cast<char8_t>(0x80u | (codepoint & 0x3Fu)));
            }
            else if (codepoint < 0x10000u)
            {
                output.push_back(static_cast<char8_t>(0xE0u | (codepoint >> 12)));
                output.push_back(static_cast<char8_t>(0x80u | ((codepoint >> 6) & 0x3Fu)));
                output.push_back(static_cast<char8_t>(0x80u | (codepoint & 0x3Fu)));
            }
            else
            {
                output.push_back(static_cast<char8_t>(0xF0u | (codepoint >> 18)));
                output.push_back(static_cast<char8_t>(0x80u | ((codepoint >> 12) & 0x3Fu)));
                output.push_back(static_cast<char8_t>(0x80u | ((codepoint >> 6) & 0x3Fu)));
                output.push_back(static_cast<char8_t>(0x80u | (codepoint & 0x3Fu)));
            }
        }

        // modified UTF-8의 3바이트 글자 하나를 읽는다 (surrogate 반쪽도 그대로 값이 된다).
        [[nodiscard]] char32_t three_byte_value(const std::string_view text, const std::size_t index) noexcept
        {
            const auto byte = [&](const std::size_t offset) { return static_cast<char32_t>(static_cast<unsigned char>(text[index + offset])); };
            return ((byte(0) & 0x0Fu) << 12) | ((byte(1) & 0x3Fu) << 6) | (byte(2) & 0x3Fu);
        }

        [[nodiscard]] bool is_high_surrogate(const char32_t value) noexcept
        {
            return value >= 0xD800u && value <= 0xDBFFu;
        }

        [[nodiscard]] bool is_low_surrogate(const char32_t value) noexcept
        {
            return value >= 0xDC00u && value <= 0xDFFFu;
        }
    } // namespace

    std::u8string utf8_from_modified_utf8(const std::string_view text)
    {
        std::u8string output {};
        output.reserve(text.size());
        for (std::size_t index { 0 }; index < text.size();)
        {
            const auto lead { static_cast<unsigned char>(text[index]) };
            // U+0000은 `C0 80`으로 온다.
            if (lead == 0xC0u && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0x80u)
            {
                output.push_back(char8_t { 0 });
                index += 2;
                continue;
            }
            // BMP 밖 글자는 surrogate 쌍이 3바이트씩 둘로 온다. 하나로 합쳐 4바이트로 쓴다.
            if ((lead & 0xF0u) == 0xE0u && index + 6 <= text.size())
            {
                const char32_t high { three_byte_value(text, index) };
                if (is_high_surrogate(high) && (static_cast<unsigned char>(text[index + 3]) & 0xF0u) == 0xE0u)
                    if (const char32_t low { three_byte_value(text, index + 3) }; is_low_surrogate(low))
                    {
                        append_utf8(output, 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u));
                        index += 6;
                        continue;
                    }
            }
            const std::size_t length { std::min(sequence_length(lead), text.size() - index) };
            for (std::size_t offset { 0 }; offset < length; ++offset)
                output.push_back(static_cast<char8_t>(text[index + offset]));
            index += length;
        }
        return output;
    }

    std::string modified_utf8_from_utf8(const std::u8string_view text)
    {
        std::string output {};
        output.reserve(text.size());
        for (std::size_t index { 0 }; index < text.size();)
        {
            const auto lead { static_cast<unsigned char>(text[index]) };
            if (lead == 0u)
            {
                output.push_back(static_cast<char>(0xC0));
                output.push_back(static_cast<char>(0x80));
                ++index;
                continue;
            }
            const std::size_t length { std::min(sequence_length(lead), text.size() - index) };
            if (length == 4)
            {
                const auto byte = [&](const std::size_t offset) { return static_cast<char32_t>(static_cast<unsigned char>(text[index + offset])); };
                const char32_t codepoint { ((byte(0) & 0x07u) << 18) | ((byte(1) & 0x3Fu) << 12) | ((byte(2) & 0x3Fu) << 6) | (byte(3) & 0x3Fu) };
                const char32_t value { codepoint - 0x10000u };
                for (const char32_t surrogate : { 0xD800u + (value >> 10), 0xDC00u + (value & 0x3FFu) })
                {
                    output.push_back(static_cast<char>(0xE0u | (surrogate >> 12)));
                    output.push_back(static_cast<char>(0x80u | ((surrogate >> 6) & 0x3Fu)));
                    output.push_back(static_cast<char>(0x80u | (surrogate & 0x3Fu)));
                }
                index += 4;
                continue;
            }
            for (std::size_t offset { 0 }; offset < length; ++offset)
                output.push_back(static_cast<char>(text[index + offset]));
            index += length;
        }
        return output;
    }

    std::size_t utf8_offset_from_utf16(const std::u8string_view text, const std::int32_t index) noexcept
    {
        if (index <= 0)
            return 0;
        std::int32_t units { 0 };
        for (std::size_t offset { 0 }; offset < text.size();)
        {
            const std::size_t length { std::min(sequence_length(static_cast<unsigned char>(text[offset])), text.size() - offset) };
            // BMP 밖 글자는 UTF-16에서 둘이다. 가운데를 가리키면 글자 시작으로 돌린다.
            const std::int32_t width { length == 4 ? 2 : 1 };
            if (units + width > index)
                return offset;
            units += width;
            offset += length;
            if (units == index)
                return offset;
        }
        return text.size();
    }

    std::int32_t utf16_offset_from_utf8(const std::u8string_view text, const std::size_t offset) noexcept
    {
        std::int32_t units { 0 };
        for (std::size_t index { 0 }; index < text.size() && index < offset;)
        {
            const std::size_t length { std::min(sequence_length(static_cast<unsigned char>(text[index])), text.size() - index) };
            units += length == 4 ? 2 : 1;
            index += length;
        }
        return units;
    }

    ime_session::ime_session(text_input_host& host, platform platform)
        : host_ { &host }
        , platform_ { std::move(platform) }
    {}

    void ime_session::synchronize()
    {
        const std::optional<text_input_target> target { host_->focused_text_target() };
        if (target != target_)
        {
            // 초점이 옮겨 간다. 조합 중이었으면 옛 칸에 확정한다.
            finish_composition();
            target_ = target;
            pending_sequence_ = 0;
            if (target_.has_value())
            {
                observed_ = host_->committed_document();
                document_ = observed_;
                send(document_);
            }
            if (target_.has_value() != keyboard_shown_)
            {
                keyboard_shown_ = target_.has_value();
                if (platform_.show_keyboard)
                    platform_.show_keyboard(keyboard_shown_);
            }
            return;
        }
        if (target_.has_value() == false || composing_)
            return;
        // **앱 쪽 글이 바뀌었을 때만** IME에 넘긴다. IME가 고친 글은 logic을 거쳐 돌아오는
        // 동안 앱의 글이 아직 옛것이다 — 그 사이에 옛 글을 넘기면 IME가 되돌아간다.
        //  - 돌아온 글이 IME의 것과 같으면 넘길 것이 없다. 다르면 앱이 따로 바꾼 것이다
        //    (하드웨어 키, 붙여넣기, 앱의 입력 거르기).
        const text_input_document committed { host_->committed_document() };
        if (pending_sequence_ != 0 && committed.applied_sequence < pending_sequence_)
            return;
        const bool acknowledged { pending_sequence_ != 0 };
        pending_sequence_ = 0;
        if (committed == observed_ && acknowledged == false)
            return;
        observed_ = committed;
        if (committed == document_)
            return;
        document_ = committed;
        send(document_);
    }

    void ime_session::accept(const ime_state& original)
    {
        if (target_.has_value() == false)
            return;
        // 한 줄 칸이다. IME가 완료 동작 대신 줄바꿈을 글로 넣으면(키보드의 Enter) 그 글자를
        // 걷어 내고 Enter로 보낸다. 안드로이드의 한 줄 EditText가 하는 일과 같다.
        ime_state state { original };
        const bool newline { strip_newlines(state) };
        accept_clean(state);
        if (newline)
        {
            // IME의 글에서도 걷어 낸다. 다음 동기화가 같은 글이라 넘기지 않으므로 여기서 넘긴다.
            send(composing_ ? composed_ : document_);
            if (platform_.submit)
                platform_.submit();
        }
    }

    void ime_session::accept_clean(const ime_state& state)
    {
        const std::size_t caret { utf8_offset_from_utf16(state.text, state.selection.end) };
        const std::size_t anchor { utf8_offset_from_utf16(state.text, state.selection.start) };
        if (state.composing.defined() && state.composing.start != state.composing.end)
        {
            composing_ = true;
            composed_ = text_input_document { state.text, caret, anchor };
            text_composition_event event {};
            event.target = *target_;
            event.text = state.text;
            event.caret = caret;
            event.composing_begin = utf8_offset_from_utf16(state.text, state.composing.start);
            event.composing_end = utf8_offset_from_utf16(state.text, state.composing.end);
            event.composing = true;
            host_->post_composition(std::move(event));
            return;
        }

        const bool was_composing { composing_ };
        composing_ = false;
        const text_input_document document { state.text, caret, anchor };
        if (document != document_)
        {
            document_ = document;
            // 조합 없이 IME가 곧바로 쓴 글이거나 조합이 끝난 글이다. 확정된 글이므로 초안으로 보낸다.
            post_document(document);
        }
        if (was_composing)
        {
            text_composition_event clear {};
            clear.target = *target_;
            clear.composing = false;
            host_->post_composition(std::move(clear));
        }
    }

    bool ime_session::strip_newlines(ime_state& state)
    {
        if (state.text.find(u8'\n') == std::u8string::npos && state.text.find(u8'\r') == std::u8string::npos)
            return false;
        // 범위는 UTF-16 단위다. 줄바꿈 글자는 하나가 한 단위라, 그 앞에서 걷어 낸 수만큼 당긴다.
        const auto shift = [&](const std::int32_t index) {
            if (index < 0)
                return index;
            std::int32_t removed { 0 };
            std::int32_t units { 0 };
            for (std::size_t offset { 0 }; offset < state.text.size() && units < index; ++offset)
            {
                const auto byte { static_cast<unsigned char>(state.text[offset]) };
                if ((byte & 0xC0u) == 0x80u)
                    continue;
                if (byte == '\n' || byte == '\r')
                    ++removed;
                units += byte >= 0xF0u ? 2 : 1;
            }
            return index - removed;
        };
        state.selection = { shift(state.selection.start), shift(state.selection.end) };
        state.composing = state.composing.defined() ? ime_span { shift(state.composing.start), shift(state.composing.end) } : ime_span {};
        std::erase_if(state.text, [](const char8_t value) { return value == u8'\n' || value == u8'\r'; });
        return true;
    }

    void ime_session::detach()
    {
        finish_composition();
        target_.reset();
        if (keyboard_shown_)
        {
            keyboard_shown_ = false;
            if (platform_.show_keyboard)
                platform_.show_keyboard(false);
        }
    }

    void ime_session::set_keyboard_visible(const bool visible) noexcept
    {
        keyboard_shown_ = visible;
    }

    void ime_session::request_keyboard()
    {
        if (target_.has_value() == false || keyboard_shown_)
            return;
        keyboard_shown_ = true;
        if (platform_.show_keyboard)
            platform_.show_keyboard(true);
    }

    bool ime_session::composing() const noexcept
    {
        return composing_;
    }

    std::optional<text_input_target> ime_session::target() const noexcept
    {
        return target_;
    }

    void ime_session::finish_composition()
    {
        if (composing_ == false || target_.has_value() == false)
            return;
        composing_ = false;
        // 조합 중이던 글을 그대로 확정한다 (TSF가 초점 상실에서 하는 것과 같다).
        document_ = composed_;
        post_document(composed_);
        text_composition_event clear {};
        clear.target = *target_;
        clear.composing = false;
        host_->post_composition(std::move(clear));
    }

    void ime_session::post_document(const text_input_document& document)
    {
        text_edit_request edit {};
        edit.target = *target_;
        edit.command = text::text_edit_command::replace_all;
        edit.text = document.text;
        edit.offset = document.caret;
        edit.anchor = document.anchor;
        edit.sequence = edit_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        pending_sequence_ = edit.sequence;
        host_->post_edit(std::move(edit));
    }

    void ime_session::send(const text_input_document& document)
    {
        if (platform_.set_state == nullptr)
            return;
        ime_state state {};
        state.text = document.text;
        state.selection = { utf16_offset_from_utf8(document.text, document.anchor), utf16_offset_from_utf8(document.text, document.caret) };
        platform_.set_state(state);
    }
} // namespace luil::android

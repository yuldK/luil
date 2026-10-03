#pragma once

#include "host/text_input_host.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace luil::android {
    // IME가 보는 글의 한 구간이다. 단위는 **UTF-16 코드 단위**다 (Java `Editable`의 index).
    // 정해지지 않은 구간은 둘 다 -1이다.
    struct ime_span
    {
        std::int32_t start { -1 };
        std::int32_t end { -1 };

        [[nodiscard]] bool defined() const noexcept
        {
            return start >= 0 && end >= 0;
        }

        [[nodiscard]] bool operator==(const ime_span&) const noexcept = default;
    };

    // IME 편집 상태다 (GameTextInput의 `GameTextInputState`에서 옮긴 값).
    struct ime_state
    {
        std::u8string text {};
        ime_span selection {};
        ime_span composing {};

        [[nodiscard]] bool operator==(const ime_state&) const = default;
    };

    // Java가 쓰는 modified UTF-8과 표준 UTF-8을 오간다.
    // BMP 밖 글자는 modified UTF-8에서 surrogate 쌍(3바이트씩)으로, U+0000은 `C0 80`으로 온다.
    [[nodiscard]] std::u8string utf8_from_modified_utf8(std::string_view text);
    [[nodiscard]] std::string modified_utf8_from_utf8(std::u8string_view text);
    // UTF-16 코드 단위 index와 UTF-8 byte offset을 오간다. 범위 밖은 끝으로 자른다.
    // surrogate 쌍 가운데를 가리키는 index는 글자 시작으로 돌린다.
    [[nodiscard]] std::size_t utf8_offset_from_utf16(std::u8string_view text, std::int32_t index) noexcept;
    [[nodiscard]] std::int32_t utf16_offset_from_utf8(std::u8string_view text, std::size_t offset) noexcept;

    // Android IME(GameTextInput) 하나와 luil 텍스트 칸을 잇는다.
    //
    // TSF의 그림자 문서와 같은 원리다. 초점을 가진 칸의 확정 글을 IME에 넘기고, IME가 고친
    // 상태가 오면 조합 중이면 `post_composition`(화면에만), 조합이 없으면 `post_edit`의
    // `replace_all` 하나로 확정한다. 앱이 글을 바꾸면(하드웨어 키, 붙여넣기) 다음
    // `synchronize`가 IME에 다시 넘긴다.
    //  - GameActivity를 모른다. 상태를 넘기고 키보드를 띄우는 일은 `platform`이 한다.
    //  - UI thread에서만 쓴다. GameTextInput의 상태도 그 thread에서 읽는다.
    class ime_session
    {
    public:
        struct platform
        {
            // IME의 편집 상태를 이 값으로 바꾼다.
            std::function<void(const ime_state&)> set_state {};
            // 소프트 키보드를 띄우거나 내린다.
            std::function<void(bool)> show_keyboard {};
            // 한 줄 칸에서 IME가 줄을 바꿨다 (Enter). 칸은 줄바꿈을 받지 않고 이 동작으로 바꾼다.
            std::function<void()> submit {};
        };

        ime_session(text_input_host& host, platform platform);

        // 초점과 확정 글이 바뀌었을 수 있다. 바뀌었으면 IME에 넘긴다.
        //  - 조합 중이면 글을 넘기지 않는다. 조합 도중 글이 밑에서 바뀌면 IME가 혼란에 빠진다.
        //  - 초점이 텍스트 칸에 서면 키보드를 띄우고, 떠나면 내린다.
        void synchronize();
        // IME가 고친 상태다.
        void accept(const ime_state& state);
        // 창이 사라진다. 조합 중이면 확정하고 키보드를 내린다.
        void detach();
        // 시스템이 알린 키보드 표시 여부다. 사용자가 키보드를 내렸을 수 있다.
        void set_keyboard_visible(bool visible) noexcept;
        // 초점을 가진 칸을 다시 눌렀다. 키보드가 내려가 있으면 다시 띄운다.
        void request_keyboard();

        [[nodiscard]] bool composing() const noexcept;
        [[nodiscard]] std::optional<text_input_target> target() const noexcept;

    private:
        void accept_clean(const ime_state& state);
        // 줄바꿈을 글과 범위에서 걷어 낸다. 걷어 낸 것이 있으면 참이다.
        [[nodiscard]] static bool strip_newlines(ime_state& state);
        // 조합을 지금 글로 확정하고 조합 표시를 거둔다.
        void finish_composition();
        void send(const text_input_document& document);
        void post_document(const text_input_document& document);

        text_input_host* host_;
        platform platform_ {};
        std::optional<text_input_target> target_ {};
        // IME가 지금 가진 확정 글이다 (마지막으로 넘겼거나 IME에서 받은 것).
        text_input_document document_ {};
        // 앱에서 마지막으로 본 확정 글이다. 이것이 바뀌어야 IME에 넘긴다.
        text_input_document observed_ {};
        // 조합 중인 글 전체다. 초점이 옮겨 가면 이것으로 확정한다.
        text_input_document composed_ {};
        // 이 번호가 돌아오기 전의 snapshot은 이전 편집의 중간 결과다.
        std::uint64_t pending_sequence_ { 0 };
        bool composing_ { false };
        bool keyboard_shown_ { false };
    };
} // namespace luil::android

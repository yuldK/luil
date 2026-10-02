#include "android/android_text_input.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

namespace {
    constexpr luil::text_input_target note { 7 };

    // 앱의 초점과 확정 글을 손으로 정하고, 앱으로 간 것을 모은다.
    class fake_host final : public luil::text_input_host
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> focused_text_target() const override
        {
            return target;
        }

        [[nodiscard]] luil::text_input_document committed_document() const override
        {
            return committed;
        }

        [[nodiscard]] std::optional<luil::rect_f> text_rect(const luil::text_input_document&, std::size_t, std::size_t) const override
        {
            return std::nullopt;
        }

        void post_composition(luil::text_composition_event event) override
        {
            compositions.push_back(std::move(event));
        }

        void post_edit(luil::text_edit_request request) override
        {
            edits.push_back(std::move(request));
        }

        std::optional<luil::text_input_target> target {};
        luil::text_input_document committed {};
        std::vector<luil::text_composition_event> compositions {};
        std::vector<luil::text_edit_request> edits {};
    };

    struct fake_platform
    {
        [[nodiscard]] luil::android::ime_session::platform hooks()
        {
            return { .set_state = [this](const luil::android::ime_state& state) { sent.push_back(state); }, .show_keyboard = [this](const bool show) { keyboard.push_back(show); } };
        }

        std::vector<luil::android::ime_state> sent {};
        std::vector<bool> keyboard {};
    };
} // namespace

TEST_CASE("UTF-16 indexes and UTF-8 offsets meet at character boundaries", "[android][ime]")
{
    // 한글은 UTF-16에서 하나, UTF-8에서 셋이다. 이모지는 둘과 넷이다.
    const std::u8string text { u8"a한😀b" };
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 0) == 0u);
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 1) == 1u);
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 2) == 4u);
    // surrogate 쌍 가운데는 글자 시작으로 돌린다.
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 3) == 4u);
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 4) == 8u);
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 5) == 9u);
    REQUIRE(luil::android::utf8_offset_from_utf16(text, 99) == 9u);
    REQUIRE(luil::android::utf16_offset_from_utf8(text, 4) == 2);
    REQUIRE(luil::android::utf16_offset_from_utf8(text, 8) == 4);
    REQUIRE(luil::android::utf16_offset_from_utf8(text, 9) == 5);
}

TEST_CASE("Modified UTF-8 round trips emoji and nul", "[android][ime]")
{
    const std::u8string text { u8"가😀" };
    const std::string modified { luil::android::modified_utf8_from_utf8(text) };
    // 이모지는 surrogate 쌍이 3바이트씩 둘이다.
    REQUIRE(modified.size() == 3u + 6u);
    REQUIRE(luil::android::utf8_from_modified_utf8(modified) == text);

    // U+0000은 `C0 80`이다. 16진 이스케이프가 뒤 글자를 먹지 않게 글자 b를 따로 붙인다.
    const std::u8string with_nul { u8"a\0b", 3 };
    const std::string encoded { std::string { "a\xC0\x80" } + 'b' };
    REQUIRE(luil::android::modified_utf8_from_utf8(with_nul) == encoded);
    REQUIRE(luil::android::utf8_from_modified_utf8(encoded) == with_nul);
}

TEST_CASE("Focusing a text box hands its text to the IME and shows the keyboard", "[android][ime]")
{
    fake_host host {};
    fake_platform platform {};
    luil::android::ime_session session { host, platform.hooks() };

    host.target = note;
    host.committed = { u8"한글", 6, 3 };
    session.synchronize();
    REQUIRE(platform.keyboard == std::vector<bool> { true });
    REQUIRE(platform.sent.size() == 1u);
    REQUIRE(platform.sent[0].text == u8"한글");
    // 선택은 UTF-16 단위다 (anchor 1, caret 2).
    REQUIRE(platform.sent[0].selection == luil::android::ime_span { 1, 2 });
    REQUIRE(platform.sent[0].composing.defined() == false);

    // 같으면 다시 넘기지 않는다.
    session.synchronize();
    REQUIRE(platform.sent.size() == 1u);

    // 초점이 떠나면 키보드를 내린다.
    host.target.reset();
    session.synchronize();
    REQUIRE(platform.keyboard == std::vector<bool> { true, false });
}

TEST_CASE("Composition shows on screen and commits as one replacement", "[android][ime]")
{
    fake_host host {};
    fake_platform platform {};
    luil::android::ime_session session { host, platform.hooks() };
    host.target = note;
    session.synchronize();

    // 조합 중인 글은 화면에만 간다.
    session.accept({ u8"ㅎ", { 1, 1 }, { 0, 1 } });
    session.accept({ u8"하", { 1, 1 }, { 0, 1 } });
    REQUIRE(session.composing());
    REQUIRE(host.edits.empty());
    REQUIRE(host.compositions.size() == 2u);
    REQUIRE(host.compositions[1].text == u8"하");
    REQUIRE(host.compositions[1].composing);
    REQUIRE(host.compositions[1].caret == 3u);
    REQUIRE(host.compositions[1].composing_end == 3u);

    // 조합 중에 앱 글이 바뀌어도 IME에 넘기지 않는다.
    host.committed = { u8"x", 1, 1 };
    session.synchronize();
    REQUIRE(platform.sent.size() == 1u);

    // 조합이 끝나면 replace_all 하나로 확정하고 조합 표시를 거둔다.
    session.accept({ u8"한", { 1, 1 }, {} });
    REQUIRE(session.composing() == false);
    REQUIRE(host.edits.size() == 1u);
    REQUIRE(host.edits[0].command == luil::text::text_edit_command::replace_all);
    REQUIRE(host.edits[0].text == u8"한");
    REQUIRE(host.edits[0].offset == 3u);
    REQUIRE(host.compositions.back().composing == false);
}

TEST_CASE("The IME is not rewound while its edit travels through logic", "[android][ime]")
{
    fake_host host {};
    fake_platform platform {};
    luil::android::ime_session session { host, platform.hooks() };
    host.target = note;
    host.committed = { u8"ab", 2, 2 };
    session.synchronize();
    REQUIRE(platform.sent.size() == 1u);

    // IME가 글자를 더했다. 앱은 아직 옛 글이다 — 그 사이에 옛 글을 넘기면 IME가 되돌아간다.
    session.accept({ u8"abc", { 3, 3 }, {} });
    REQUIRE(host.edits.size() == 1u);
    session.synchronize();
    REQUIRE(platform.sent.size() == 1u);

    // 앱이 받아들인 글이 돌아오면 넘길 것이 없다.
    host.committed = { u8"abc", 3, 3 };
    session.synchronize();
    REQUIRE(platform.sent.size() == 1u);

    // 앱이 따로 바꾼 글(하드웨어 키, 붙여넣기)은 IME에 넘긴다.
    host.committed = { u8"abcd", 4, 4 };
    session.synchronize();
    REQUIRE(platform.sent.size() == 2u);
    REQUIRE(platform.sent[1].text == u8"abcd");
    REQUIRE(platform.sent[1].selection == luil::android::ime_span { 4, 4 });
}

TEST_CASE("Moving the focus mid composition commits it to the old box", "[android][ime]")
{
    fake_host host {};
    fake_platform platform {};
    luil::android::ime_session session { host, platform.hooks() };
    host.target = note;
    session.synchronize();
    session.accept({ u8"가", { 1, 1 }, { 0, 1 } });

    host.target = luil::text_input_target { 8 };
    host.committed = { u8"other", 5, 5 };
    session.synchronize();
    REQUIRE(host.edits.size() == 1u);
    REQUIRE(host.edits[0].target == note);
    REQUIRE(host.edits[0].text == u8"가");
    REQUIRE(host.compositions.back().target == note);
    REQUIRE(host.compositions.back().composing == false);
    // 새 칸의 글을 IME에 넘긴다. 키보드는 이미 떠 있다.
    REQUIRE(platform.sent.back().text == u8"other");
    REQUIRE(platform.keyboard == std::vector<bool> { true });
}

TEST_CASE("A newline from the IME becomes Enter in a single line box", "[android][ime]")
{
    fake_host host {};
    fake_platform platform {};
    int submitted { 0 };
    luil::android::ime_session::platform hooks { platform.hooks() };
    hooks.submit = [&submitted] { ++submitted; };
    luil::android::ime_session session { host, std::move(hooks) };
    host.target = note;
    host.committed = { u8"한글", 6, 6 };
    session.synchronize();

    // 키보드의 Enter가 줄바꿈을 글로 넣었다. 칸에는 들이지 않고 Enter로 보낸다.
    session.accept({ u8"한글\n", { 3, 3 }, {} });
    REQUIRE(submitted == 1);
    REQUIRE(host.edits.empty());
    // IME의 글에서도 걷어 낸다.
    REQUIRE(platform.sent.back().text == u8"한글");
    REQUIRE(platform.sent.back().selection == luil::android::ime_span { 2, 2 });
}

TEST_CASE("A dismissed keyboard comes back when the box is pressed again", "[android][ime]")
{
    fake_host host {};
    fake_platform platform {};
    luil::android::ime_session session { host, platform.hooks() };
    host.target = note;
    session.synchronize();

    session.set_keyboard_visible(false);
    session.request_keyboard();
    REQUIRE(platform.keyboard == std::vector<bool> { true, true });
    // 이미 떠 있으면 다시 부르지 않는다.
    session.request_keyboard();
    REQUIRE(platform.keyboard.size() == 2u);

    // 창이 사라지면 키보드를 내리고, 다시 생기면 다시 붙인다.
    session.detach();
    REQUIRE(platform.keyboard.back() == false);
    session.synchronize();
    REQUIRE(platform.keyboard.back());
}

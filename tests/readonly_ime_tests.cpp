#include "android/android_text_input.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    class readonly_ime_host final : public luil::text_input_host
    {
    public:
        std::optional<luil::text_input_target> focused_text_target() const override { return luil::text_input_target { 1 }; }
        luil::text_input_document committed_document() const override { return document; }
        std::optional<luil::rect_f> text_rect(const luil::text_input_document&, std::size_t, std::size_t) const override { return std::nullopt; }
        void post_edit(luil::text_edit_request) override { ++edits; }
        void post_composition(luil::text_composition_event event) override { if (event.composing == false) ++clears; }
        luil::text_input_document document { u8"원문", 6, 6 };
        int edits { 0 };
        int clears { 0 };
    };
}

TEST_CASE("Read only IME documents reject edits and end an existing composition without committing", "[android][ime][read_only]")
{
    readonly_ime_host host {};
    bool keyboard { false };
    std::u8string shown {};
    luil::android::ime_session session { host, {
        .set_state = [&shown](const luil::android::ime_state& state) { shown = state.text; },
        .show_keyboard = [&keyboard](const bool show, bool) { keyboard = show; },
    }
    };
    session.synchronize();
    REQUIRE(keyboard);
    session.accept({ u8"원문가", { 3, 3 }, { 2, 3 } });
    REQUIRE(session.composing());
    host.document.read_only = true;
    session.synchronize();
    REQUIRE_FALSE(session.composing());
    REQUIRE_FALSE(keyboard);
    REQUIRE(host.edits == 0);
    REQUIRE(host.clears == 1);
    REQUIRE(shown == u8"원문");
    session.accept({ u8"바뀐 글", { 0, 0 }, {} });
    session.request_keyboard();
    REQUIRE(host.edits == 0);
    REQUIRE_FALSE(keyboard);
    host.document.read_only = false;
    session.synchronize();
    session.request_keyboard();
    REQUIRE(keyboard);
    session.accept({ u8"새 글", { 3, 3 }, {} });
    REQUIRE(host.edits == 1);
}

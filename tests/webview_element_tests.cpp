#include "luil/ui/webview_element.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using luil::access_role;
    using luil::arrange_context;
    using luil::rect_f;
    using luil::ui_element_id;
    using luil::ui_element_kind;
    using luil::webview_config;
    using luil::webview_element;

    [[nodiscard]] ui_element_id placeholder_id(std::u8string owner)
    {
        return ui_element_id { ui_element_kind::webview, std::move(owner) };
    }
} // namespace

TEST_CASE("A webview placeholder takes the slot it is given", "[ui][webview]")
{
    webview_element element { placeholder_id(u8"docs"), webview_config { .webview = u8"docs" } };
    element.arrange(arrange_context { .slot = rect_f { 40.0f, 24.0f, 300.0f, 200.0f }, .scale = 1.0f });

    REQUIRE(element.bounds().x == 40.0f);
    REQUIRE(element.bounds().y == 24.0f);
    REQUIRE(element.bounds().width == 300.0f);
    REQUIRE(element.bounds().height == 200.0f);
}

TEST_CASE("A webview placeholder remembers the scale it was arranged at", "[ui][webview]")
{
    // 웹뷰의 래스터 배율이 이 값을 따른다 — 자리와 같은 tree의 것이라야 배율이
    // 바뀐 직후의 frame에서도 자리와 배율이 어긋난 쌍이 되지 않는다.
    webview_element element { placeholder_id(u8"docs"), webview_config { .webview = u8"docs" } };
    REQUIRE(element.scale() == 1.0f);

    element.arrange(arrange_context { .slot = rect_f { 0.0f, 0.0f, 300.0f, 200.0f }, .scale = 2.0f });
    REQUIRE(element.scale() == 2.0f);

    // 0 이하는 1로 본다 (다른 element와 같은 규칙).
    element.arrange(arrange_context { .slot = rect_f { 0.0f, 0.0f, 300.0f, 200.0f }, .scale = 0.0f });
    REQUIRE(element.scale() == 1.0f);
}

TEST_CASE("A webview placeholder absorbs hits and takes a tab stop", "[ui][webview]")
{
    // 이 자리의 포인터는 웹 콘텐츠의 것이다 — 뒤에 있는 것이 받으면 페이지 위를
    // 눌렀는데 그 아래 목록이 반응한다.
    // Tab은 여기 닿을 수 있어야 한다. 닿으면 초점을 페이지로 넘긴다.
    const webview_element element { placeholder_id(u8"docs"), webview_config { .webview = u8"docs" } };
    REQUIRE(element.hit_opaque());
    REQUIRE(element.tab_stop());
}

TEST_CASE("A webview placeholder is not a focus trap", "[ui][webview]")
{
    // tree의 가둠은 **우리** Tab 순회를 그 subtree에 가두는 것인데 이 자리표는
    // 자식이 없다. 걸면 Tab이 여기 닿는 순간 영영 빠져나가지 못한다 — 웹뷰에
    // 들어가기도 전에. 페이지 안의 가둠은 WebView2의 MoveFocusRequested가 맡는다
    // (webview-composition-design.md).
    const webview_element element { placeholder_id(u8"docs"), webview_config { .webview = u8"docs" } };
    REQUIRE(element.focus_trap() == false);
}

TEST_CASE("A webview placeholder reads as a named group", "[ui][webview]")
{
    // 페이지 내용은 창의 UIA tree에 저절로 들어온다. 여기서 흉내 내지 않는다.
    const webview_element element {
        placeholder_id(u8"docs"),
        webview_config { .webview = u8"docs", .name = u8"문서 미리 보기" },
    };
    const auto access { element.accessibility() };
    REQUIRE(access.role == access_role::group);
    REQUIRE(access.name == u8"문서 미리 보기");
}

TEST_CASE("A webview placeholder keeps the id its owner names", "[ui][webview]")
{
    // 자리표와 수명이 만나는 값 하나다 — owner가 곧 ui_webview::id다.
    const webview_element element { placeholder_id(u8"docs"), webview_config { .webview = u8"docs" } };
    REQUIRE(element.id().kind == ui_element_kind::webview);
    REQUIRE(element.id().owner == u8"docs");
    REQUIRE(element.config().webview == u8"docs");
}

#include "luil/ui/dialog_elements.h"

#include "luil/generated/codicons.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(0) };
} // namespace

TEST_CASE("A text button without a glyph keeps the text-only layout", "[ui][text_button]")
{
    // 아이콘이 없으면 칸을 아예 만들지 않는다 — 지금까지의 글자 버튼이
    // 이 설정의 특수 경우다.
    STATIC_REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config {}) == 0.0f);
    STATIC_REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config { .text = u8"확인" }) == 0.0f);
}

TEST_CASE("A text button with a glyph reserves the icon column", "[ui][text_button]")
{
    // 버튼 폭은 담는 쪽이 정하므로(측정 단계가 없다) 그 쪽이 이 값을 더해 잡는다.
    //  - 설정을 `constexpr` 변수로 두지는 못한다. `std::u8string`을 담고 있어
    //    상수 평가 밖으로 나갈 수 없기 때문이다. 임시로 넘기면 그대로 잠긴다.
    STATIC_REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config { .text = u8"열기", .glyph = luil::codicons::icon_add })
        == luil::text_button_icon_size + luil::text_button_icon_gap);
    STATIC_REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config { .text = u8"열기", .glyph = luil::codicons::icon_add }) == 20.0f);
}

TEST_CASE("A text button is interactive only once it has an action", "[ui][text_button]")
{
    // 없는 것은 두지 않는다: 액션이 없으면 hit의 임자도 Tab의 자리도 아니다.
    const luil::text_button_element quiet { luil::ui_element_id { kind_button }, luil::text_button_config { .text = u8"열기" } };
    REQUIRE(quiet.interactive() == false);
    REQUIRE(quiet.tab_stop() == false);

    luil::text_button_element live { luil::ui_element_id { kind_button }, luil::text_button_config { .text = u8"열기" } };
    live.set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    REQUIRE(live.interactive());
    // 누를 수 있으면 Tab의 자리다 (기본 규칙 — 거절은 set_tab_stop이 한다).
    REQUIRE(live.tab_stop());
}

TEST_CASE("A default button says Enter with a fill, never with a ring", "[ui][text_button]")
{
    using luil::text_button_config;
    using luil::text_button_fill;
    using luil::text_button_fill_for;
    using luil::text_button_visual;

    // 이 판정이 그리기 안에 숨어 있던 동안 조용히 어긋나 있었다 — 기본 버튼의
    // 테가 `ui_tree`의 초점 테와 색·굵기·여백이 같아, 확인 dialog에서 초점이
    // 둘로 보였다 (enter-default-design.md).
    SECTION("기본 버튼은 강조색으로 채운다")
    {
        const text_button_config confirm { .text = u8"초기화", .visual = text_button_visual::accent, .default_button = true };
        REQUIRE(text_button_fill_for(confirm, true) == text_button_fill::solid_accent);
        // 모양이 accent가 아니어도 기본 버튼이면 채운다 — 주 동작이라는 말이 먼저다.
        const text_button_config plain_default { .text = u8"확인", .default_button = true };
        REQUIRE(text_button_fill_for(plain_default, true) == text_button_fill::solid_accent);
    }

    SECTION("기본 버튼이 아니면 자기 모양대로다")
    {
        REQUIRE(text_button_fill_for(text_button_config { .text = u8"취소" }, true) == text_button_fill::plain);
        REQUIRE(text_button_fill_for(text_button_config { .text = u8"확인", .visual = text_button_visual::accent }, true) == text_button_fill::soft_accent);
    }

    SECTION("꺼진 기본 버튼은 채우지 않는다")
    {
        // 채움은 주 동작으로의 권유다. 누를 수 없는 자리에 세우면 거짓말이 된다.
        const text_button_config confirm { .text = u8"초기화", .visual = text_button_visual::accent, .default_button = true };
        REQUIRE(text_button_fill_for(confirm, false) == text_button_fill::soft_accent);
    }

    SECTION("link는 채울 상자가 없다")
    {
        // 상자가 없으니 표시가 서지 않고, 그래서 Enter도 데려가지 않는다.
        const text_button_config link { .text = u8"자세히", .visual = text_button_visual::link, .default_button = true };
        REQUIRE(text_button_fill_for(link, true) == text_button_fill::none);
        const luil::text_button_element element { luil::ui_element_id { kind_button }, link };
        REQUIRE(element.default_button() == false);
    }
}

#include "luil/ui/dialog_elements.h"

#include "luil/generated/codicons.h"
#include "luil/theme/ui_theme.h"
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
    REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config {}) == 0.0f);
    REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config { .text = u8"확인" }) == 0.0f);
}

TEST_CASE("A text button with a glyph reserves the icon column", "[ui][text_button]")
{
    // 버튼 폭은 담는 쪽이 정하므로(측정 단계가 없다) 그 쪽이 이 값을 더해 잡는다.
    //  - 이 판정은 한동안 `STATIC_REQUIRE`로 잠겨 있었다. 설정이 팔레트 선택자
    //    (`std::function`)를 담으면서 상수 평가 안에서 지을 수 없게 되어 값으로 잠근다 —
    //    `text_input_config`가 액션을 담아 `leading_width_for`를 밖으로 낸 것과 같은 자리다.
    //    판정 자체는 그대로라 잠기는 것도 그대로다.
    REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config { .text = u8"열기", .glyph = luil::codicons::icon_add })
        == luil::text_button_icon_size + luil::text_button_icon_gap);
    REQUIRE(luil::text_button_element::icon_width_for(luil::text_button_config { .text = u8"열기", .glyph = luil::codicons::icon_add }) == 20.0f);
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

TEST_CASE("A destructive text button fills with the error role", "[ui][text_button]")
{
    using luil::text_button_config;
    using luil::text_button_fill;
    using luil::text_button_fill_for;
    using luil::text_button_visual;

    SECTION("되돌릴 수 없는 동작은 자기 색으로 채운다")
    {
        // 아이콘 버튼의 `button_visual_role::danger`와 같은 뜻이고 같은 팔레트 역할을 쓴다.
        // 한 dialog에 두 붉은색이 갈리면 그것은 두 element가 각자 색을 정했다는 뜻이다.
        const text_button_config remove { .text = u8"지우기", .visual = text_button_visual::danger };
        REQUIRE(text_button_fill_for(remove, true) == text_button_fill::soft_danger);
        // 꺼져도 모양은 그대로다. 물러서는 채움은 "주 동작으로의 권유"인 기본 버튼뿐이다.
        REQUIRE(text_button_fill_for(remove, false) == text_button_fill::soft_danger);
    }

    SECTION("danger도 기본 버튼이면 강조색으로 채운다")
    {
        // Enter가 어디로 가는지가 그 버튼이 무엇을 뜻하는지보다 급한 정보다.
        // 채운 상자가 둘이면 어느 쪽이 Enter의 자리인지 화면에서 사라진다.
        const text_button_config confirm { .text = u8"지우기", .visual = text_button_visual::danger, .default_button = true };
        REQUIRE(text_button_fill_for(confirm, true) == text_button_fill::solid_accent);
        // 꺼지면 채움이 물러서고 자기 모양으로 돌아간다.
        REQUIRE(text_button_fill_for(confirm, false) == text_button_fill::soft_danger);
    }

    SECTION("danger는 글 흐름이 아니라 상자다")
    {
        // 역할은 여전히 버튼이다 — 갈리는 것은 link 하나뿐이다.
        const luil::text_button_element element { luil::ui_element_id { kind_button }, text_button_config { .text = u8"지우기", .visual = text_button_visual::danger } };
        REQUIRE(element.accessibility().role == luil::access_role::button);
        REQUIRE(element.accessibility().name == u8"지우기");
    }
}

TEST_CASE("A palette selector on a text button answers the colour, never the shape", "[ui][text_button]")
{
    using luil::text_button_config;
    using luil::text_button_fill;
    using luil::text_button_fill_for;
    using luil::text_button_visual;

    // 선택자가 답하는 것은 색이지 모양이 아니다. 상자가 서는지는 여전히 `visual`과
    // `default_button`이 정한다 — 색을 고른다고 없던 상자가 서면, 앱이 색만 바꾸려 할 때마다
    // 배치가 함께 움직인다.
    SECTION("채움 선택자는 채움의 종류를 바꾸지 않는다")
    {
        text_button_config tinted { .text = u8"확인" };
        tinted.fill = [](const luil::ui_color_palette& palette) { return palette.notice_background; };
        REQUIRE(text_button_fill_for(tinted, true) == text_button_fill::plain);

        tinted.visual = text_button_visual::accent;
        REQUIRE(text_button_fill_for(tinted, true) == text_button_fill::soft_accent);
    }

    SECTION("link는 선택자를 주어도 칠할 상자가 없다")
    {
        // 상자가 없어야 글 흐름 안에 놓을 수 있고, 그것이 link가 버튼과 갈리는 전부다.
        text_button_config link { .text = u8"자세히", .visual = text_button_visual::link };
        link.fill = [](const luil::ui_color_palette& palette) { return palette.notice_background; };
        link.label_color = [](const luil::ui_color_palette& palette) { return palette.warning_accent; };
        REQUIRE(text_button_fill_for(link, true) == text_button_fill::none);
        // 글자 색만 앱의 것이 되고 역할은 그대로다.
        const luil::text_button_element element { luil::ui_element_id { kind_button }, link };
        REQUIRE(element.accessibility().role == luil::access_role::link);
    }

    SECTION("선택자는 기본 버튼의 채움도 밀어내지 않는다")
    {
        // "Enter가 여기로 간다"는 색이 아니라 채움의 종류가 말한다.
        text_button_config confirm { .text = u8"확인", .default_button = true };
        confirm.label_color = [](const luil::ui_color_palette& palette) { return palette.error_accent; };
        REQUIRE(text_button_fill_for(confirm, true) == text_button_fill::solid_accent);
    }
}

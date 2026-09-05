#include "luil/ui/wrap_element.h"

#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_wrap { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_item { luil::application_element_kind(1) };

    // 항목 20×10, 항목 사이 4, 줄 사이 6이다.
    // 네 값을 모두 다르게 두어 축이나 간격이 뒤바뀌면 test가 잡는다.
    constexpr luil::wrap_config sample { 20.0f, 10.0f, 4.0f, 6.0f };

    class slot_probe final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::vector<luil::ui_element*> fill(luil::wrap_element& wrap, const std::size_t count)
    {
        std::vector<luil::ui_element*> probes {};
        for (std::size_t index = 0; index < count; ++index)
        {
            auto item { std::make_unique<slot_probe>(luil::ui_element_id { kind_item, u8"item-" + std::u8string { static_cast<char8_t>(u8'a' + index) } }) };
            probes.push_back(item.get());
            wrap.add(std::move(item));
        }
        return probes;
    }
} // namespace

TEST_CASE("Wrap sizers count the columns and rows that fit", "[ui][wrap]")
{
    // 넷은 20×4 + 4×3 = 92라 딱 들어가고, 다섯은 116이라 못 들어간다.
    STATIC_REQUIRE(luil::wrap_columns_for(sample, 100.0f) == 4u);
    STATIC_REQUIRE(luil::wrap_columns_for(sample, 92.0f) == 4u);
    // 한 칸 모자라면 셋이다 — 마지막 항목 뒤에는 간격이 붙지 않는다.
    STATIC_REQUIRE(luil::wrap_columns_for(sample, 91.0f) == 3u);

    // 아홉 개를 네 열에 담으면 세 줄이고, 줄 사이 간격은 줄 수보다 하나 적다.
    STATIC_REQUIRE(luil::wrap_height_for(sample, 100.0f, 9) == 42.0f);
    STATIC_REQUIRE(luil::wrap_height_for(sample, 100.0f, 4) == 10.0f);
    STATIC_REQUIRE(luil::wrap_height_for(sample, 100.0f, 5) == 26.0f);
}

TEST_CASE("Wrap sizers answer at the boundaries without dividing", "[ui][wrap]")
{
    // 담을 것이 없으면 높이도 0이다.
    STATIC_REQUIRE(luil::wrap_height_for(sample, 100.0f, 0) == 0.0f);

    // 크기를 주지 않으면 한 열이다 — 나누지 않는다.
    constexpr luil::wrap_config sizeless { 0.0f, 10.0f, 4.0f, 6.0f };
    STATIC_REQUIRE(luil::wrap_columns_for(sizeless, 100.0f) == 1u);
    STATIC_REQUIRE(luil::wrap_height_for(sizeless, 100.0f, 3) == 42.0f);

    // 항목이 slot보다 넓어도 한 줄에 하나다 (그만큼 넘친다).
    constexpr luil::wrap_config wide { 200.0f, 10.0f, 4.0f, 6.0f };
    STATIC_REQUIRE(luil::wrap_columns_for(wide, 100.0f) == 1u);
    STATIC_REQUIRE(luil::wrap_columns_for(wide, 0.0f) == 1u);
}

TEST_CASE("A wrap fills a line before moving to the next", "[ui][wrap]")
{
    luil::wrap_element wrap { luil::ui_element_id { kind_wrap }, sample };
    const auto probes { fill(wrap, 6) };
    wrap.arrange({ { 10.0f, 20.0f, 100.0f, 100.0f }, 1.0f });

    // 한 줄에 넷이 들어간다.
    REQUIRE(probes[0]->bounds().x == 10.0f);
    REQUIRE(probes[0]->bounds().y == 20.0f);
    REQUIRE(probes[0]->bounds().width == 20.0f);
    REQUIRE(probes[0]->bounds().height == 10.0f);
    REQUIRE(probes[1]->bounds().x == 34.0f);
    REQUIRE(probes[3]->bounds().x == 82.0f);
    REQUIRE(probes[3]->bounds().y == 20.0f);

    // 다섯째부터 다음 줄이고, 마지막 줄은 덜 차 있어도 된다.
    REQUIRE(probes[4]->bounds().x == 10.0f);
    REQUIRE(probes[4]->bounds().y == 36.0f);
    REQUIRE(probes[5]->bounds().x == 34.0f);
    REQUIRE(probes[5]->bounds().y == 36.0f);
}

TEST_CASE("A wrap uses the same column count that its sizer reports", "[ui][wrap]")
{
    // 두 답이 갈리면 담는 쪽이 잡은 높이와 실제 줄 수가 어긋나 마지막 줄이 잘린다.
    for (const float scale : { 1.0f, 1.25f, 2.0f })
    {
        luil::wrap_element wrap { luil::ui_element_id { kind_wrap }, sample };
        const auto probes { fill(wrap, 9) };
        wrap.arrange({ { 0.0f, 0.0f, 100.0f * scale, 100.0f * scale }, scale });

        const std::size_t columns { luil::wrap_columns_for(sample, 100.0f) };
        REQUIRE(columns == 4u);
        // 마지막 열은 아직 첫 줄이고 그다음이 둘째 줄이다.
        REQUIRE(probes[columns - 1]->bounds().y == 0.0f);
        REQUIRE(probes[columns]->bounds().y == 16.0f * scale);
        REQUIRE(probes[columns]->bounds().x == 0.0f);
        // 정적 사이저가 답한 높이 안에 마지막 줄이 들어온다.
        const float height { luil::wrap_height_for(sample, 100.0f, probes.size()) * scale };
        REQUIRE(probes[8]->bounds().y + probes[8]->bounds().height == height);
    }
}

TEST_CASE("A hidden child keeps its place in the wrap", "[ui][wrap]")
{
    luil::wrap_element wrap { luil::ui_element_id { kind_wrap }, sample };
    const auto probes { fill(wrap, 6) };
    probes[1]->set_visible(false);
    wrap.arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });

    // 숨긴 자식이 자리를 비우지 않으므로 뒤 항목이 앞으로 당겨지지 않는다
    REQUIRE(probes[1]->bounds().x == 24.0f);
    REQUIRE(probes[2]->bounds().x == 48.0f);
    REQUIRE(probes[4]->bounds().y == 16.0f);
}

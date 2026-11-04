#include "mobile_demo/page_list.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/label_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 400dp 폭에서 짜서 배치한 페이지 목록이다. 높이는 내용이 정한다.
    [[nodiscard]] luil::ui_tree list_tree(const std::u8string_view selected, const bool chevron)
    {
        auto content { mobile_demo::make_page_list(400.0f, selected, chevron) };
        const float height { content.height };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f);
    }

    // 보조 기술의 Invoke가 칸에서 받는 메시지가 여는 페이지다. 메시지가 없으면 빈 글이다.
    [[nodiscard]] std::u8string invoked_page(const luil::ui_element& item)
    {
        const std::optional<std::vector<luil::input_action>> planned { luil::plan_access_request(item, luil::access_request { luil::access_command::invoke }) };
        REQUIRE(planned.has_value());
        REQUIRE(planned->size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&planned->front()) };
        REQUIRE(message != nullptr);
        const auto* const navigate { message->get<mobile_demo::navigate_intent>() };
        REQUIRE(navigate != nullptr);
        return navigate->page;
    }

    // 글꼴이 넓은 쪽을 본뜬 측정기다: 로마자는 글자 크기의 0.6배, 그 밖(한글)은 1배. 설치 글꼴에 기대지 않아 어느 기기에서나 같다.
    [[nodiscard]] float wide_sans_width(const std::u8string_view text, const float pixel_size)
    {
        float em { 0.0f };
        for (const char8_t byte : text)
        {
            if ((byte & 0xC0u) == 0x80u)
                continue;
            em += byte < 0x80u ? 0.6f : 1.0f;
        }
        return em * pixel_size;
    }

    const luil::text_measurer wide_sans_measurer { wide_sans_width };

    // 시스템 글자 크기를 1.1배로 둔다. 끝나면 기본 배율로 되돌린다.
    class font_scale_one_point_one
    {
    public:
        font_scale_one_point_one()
        {
            std::vector<float> sizes {};
            for (int size { 0 }; size <= 40; ++size)
                sizes.push_back(static_cast<float>(size) * 1.1f);
            REQUIRE(luil::set_system_font_sizes(std::move(sizes)));
        }
        font_scale_one_point_one(const font_scale_one_point_one&) = delete;
        font_scale_one_point_one& operator=(const font_scale_one_point_one&) = delete;
        ~font_scale_one_point_one()
        {
            static_cast<void>(luil::set_system_font_sizes({}));
        }
    };

    [[nodiscard]] luil::ui_tree measured_list_tree(const float width, const std::u8string_view selected, const bool chevron)
    {
        auto content { mobile_demo::make_page_list(width, selected, chevron, 1.0f, &wide_sans_measurer) };
        const float height { content.height };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, width, height }, 1.0f);
    }

    [[nodiscard]] const luil::ui_element& page_summary(const luil::ui_tree& tree, const std::u8string_view key)
    {
        const luil::ui_element* const summary { tree.find({ mobile_demo::kind_text, u8"entry-summary-" + std::u8string { key } }) };
        REQUIRE(summary != nullptr);
        return *summary;
    }

    [[nodiscard]] const luil::ui_element& page_cell(const luil::ui_tree& tree, const std::u8string_view key)
    {
        const luil::ui_element* const cell { tree.find({ mobile_demo::kind_page_entry, std::u8string { key } }) };
        REQUIRE(cell != nullptr);
        return *cell;
    }
}

TEST_CASE("Each page entry of the mobile list is a button named after its page and opens it on Invoke", "[mobile-demo][navigation][accessibility]")
{
    // 좁은 화면(다음 장 표시가 있다)과 넓은 화면(고른 칸이 칠해진다) 둘 다 같은 칸이다.
    for (const bool wide : { false, true })
    {
        const luil::ui_tree tree { list_tree(wide ? mobile_demo::page_basics : std::u8string_view {}, wide == false) };
        REQUIRE(mobile_demo::page_entries().size() == 15u);
        for (const mobile_demo::page_entry& entry : mobile_demo::page_entries())
        {
            const luil::ui_element_id id { mobile_demo::kind_page_entry, std::u8string { entry.key } };
            const luil::ui_element* const item { tree.find(id) };
            REQUIRE(item != nullptr);
            const luil::access_info info { item->accessibility() };
            REQUIRE(info.role == luil::access_role::button);
            REQUIRE(info.name == entry.title);
            REQUIRE(luil::access_reachable(tree, id));
            REQUIRE(invoked_page(*item) == entry.key);
        }
    }
}

TEST_CASE("The page entry names of the mobile list are unique so a client can open a page by name", "[mobile-demo][navigation][accessibility]")
{
    std::set<std::u8string> names {};
    for (const mobile_demo::page_entry& entry : mobile_demo::page_entries())
        names.insert(std::u8string { entry.title });
    REQUIRE(names.size() == mobile_demo::page_entries().size());
}

TEST_CASE("The left pane of the wide layout breaks a long page summary instead of ending it in an ellipsis", "[mobile-demo][navigation]")
{
    // 왼쪽 판의 폭(260dp)에 글꼴 배율 1.1이면 "드롭다운, 분할 버튼, 길게 눌…"처럼 설명 끝이 잘렸다.
    const font_scale_one_point_one font_scale {};
    const luil::ui_tree tree { measured_list_tree(260.0f, mobile_demo::page_basics, false) };
    const luil::measure_context measure { .scale = 1.0f, .measurer = &wide_sans_measurer };
    for (const mobile_demo::page_entry& entry : mobile_demo::page_entries())
    {
        CAPTURE(entry.title);
        const luil::rect_f box { page_summary(tree, entry.key).bounds() };
        const std::optional<luil::size_f> wanted { page_summary(tree, entry.key).preferred_size(measure, box.width) };
        REQUIRE(wanted.has_value());
        // 글이 칸의 폭과 높이 안에 다 들어간다. 한 줄 label로 되돌아가면 잰 폭이 칸을 넘는다.
        REQUIRE(wanted->width <= box.width);
        REQUIRE(wanted->height <= box.height);
        // 늘어난 줄만큼 칸도 커서 글이 칸 밖으로 나가지 않는다.
        const luil::rect_f cell { page_cell(tree, entry.key).bounds() };
        REQUIRE(box.y + box.height <= cell.y + cell.height);
    }

    // 가장 긴 글은 이 폭에서 두 줄 이상이고, 짧은 글의 칸은 그대로 한 줄 높이다.
    const float one_line_height { page_cell(tree, mobile_demo::page_toasts).bounds().height };
    REQUIRE(page_cell(tree, mobile_demo::page_popups).bounds().height > one_line_height);
    const luil::ui_element& longest { page_summary(tree, mobile_demo::page_popups) };
    REQUIRE(longest.preferred_size(measure, longest.bounds().width)->height >= 2.0f * luil::label_element::height_for({ .font_size = mobile_demo::note_text_size }));
}

TEST_CASE("The portrait page list keeps one summary line and one cell height at a 1.1 font scale", "[mobile-demo][navigation]")
{
    // 세로 목록은 폭이 넉넉해 글이 들어간다. 줄 바꿈이 칸의 자리를 옮기지 않는다.
    const font_scale_one_point_one font_scale {};
    const luil::ui_tree tree { measured_list_tree(400.0f, {}, true) };
    const float cell_height { page_cell(tree, mobile_demo::page_basics).bounds().height };
    const float summary_height { page_summary(tree, mobile_demo::page_basics).bounds().height };
    for (const mobile_demo::page_entry& entry : mobile_demo::page_entries())
    {
        CAPTURE(entry.title);
        REQUIRE(page_cell(tree, entry.key).bounds().height == cell_height);
        REQUIRE(page_summary(tree, entry.key).bounds().height == summary_height);
    }
    // 한 줄 설명의 높이 그대로다. 위아래 간격이 바뀌지 않는다.
    REQUIRE(summary_height == luil::text_line_height(mobile_demo::note_text_size, mobile_demo::note_line_height - mobile_demo::note_text_size));
}

TEST_CASE("The page title falls back to the app name for a key the list does not hold", "[mobile-demo][navigation]")
{
    REQUIRE(mobile_demo::page_title(mobile_demo::page_zoom) == u8"확대 보기");
    REQUIRE(mobile_demo::page_title(u8"") == u8"luil mobile demo");
}

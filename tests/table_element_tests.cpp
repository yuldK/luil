#include "luil/ui/table_element.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "raster_probe.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_cell { luil::application_element_kind(0) };

    struct sort_intent
    {
        std::u8string column {};
    };

    struct resize_intent
    {
        std::u8string column {};
        float delta { 0.0f };
    };

    struct resize_end_intent
    {
        std::u8string column {};
    };

    struct scroll_x_intent
    {
        float delta { 0.0f };
    };

    struct scroll_x_to_intent
    {
        float offset { 0.0f };
    };

    struct scroll_y_intent
    {
        float delta { 0.0f };
    };

    struct move_intent
    {
        std::u8string key {};
    };

    // 앱이 지은 칸이다. 자리표에 행과 열을 함께 담는다.
    class cell_probe final : public luil::ui_element
    {
    public:
        explicit cell_probe(std::u8string owner)
            : ui_element { luil::ui_element_id { kind_cell, std::move(owner) } }
        {
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] std::u8string part(const std::u8string& key)
    {
        return u8"files" + std::u8string { u8"\u001f" } + key;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    [[nodiscard]] std::vector<luil::table_column> make_columns()
    {
        luil::table_column name {};
        name.key = u8"name";
        name.header = u8"이름";
        name.width = 160.0f;
        name.min_width = 80.0f;
        luil::table_column author {};
        author.key = u8"author";
        author.header = u8"작성자";
        author.width = 90.0f;
        luil::table_column revision {};
        revision.key = u8"revision";
        revision.header = u8"리비전";
        revision.width = 70.0f;
        revision.align = luil::column_align::end;
        revision.sort = luil::sort_direction::descending;
        revision.sortable = false;
        return { name, author, revision };
    }

    // 열 폭의 합은 320이고 표는 200 폭이다. 스무 행(400)이 넘쳐 세로 막대(16)가 서므로 가로 창은 184다 —
    // 가로로 136만큼 흐를 수 있다.
    [[nodiscard]] luil::table_config make_config(const bool custom_cells = false)
    {
        luil::table_config config {};
        config.owner = u8"files";
        config.columns = make_columns();
        for (std::size_t index = 0; index < 20u; ++index)
        {
            luil::virtual_list_item item {};
            item.key = u8"row-" + std::u8string(1u, static_cast<char8_t>(u8'a' + index));
            item.label = item.key;
            config.rows.items.push_back(std::move(item));
        }
        config.rows.row_height = 20.0f;
        config.rows.move = [](const std::u8string& key) { return luil::make_app_action(move_intent { key }); };
        config.rows.scroll = [](const float delta) { return luil::make_app_action(scroll_y_intent { delta }); };
        config.cell_text = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column) { return row.key + u8"." + column.key; };
        if (custom_cells)
            config.build_cell = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column, bool) -> std::unique_ptr<luil::ui_element> {
                return std::make_unique<cell_probe>(row.key + u8"/" + column.key);
            };
        config.scroll_horizontal = [](const float delta) { return luil::make_app_action(scroll_x_intent { delta }); };
        config.sort_request = [](const std::u8string& column) { return luil::make_app_action(sort_intent { column }); };
        config.resize_column = [](const std::u8string& column, const float delta) { return luil::make_app_action(resize_intent { column, delta }); };
        config.resize_column_end = [](const std::u8string& column) { return luil::make_app_action(resize_end_intent { column }); };
        return config;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> published(luil::table_config config, const float scale = 1.0f)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::table_element>(std::move(config)), { 0.0f, 0.0f, 200.0f * scale, 300.0f * scale }, scale));
    }

    constexpr luil::ui_element_kind kind_editor { luil::application_element_kind(1) };

    struct cell_intent
    {
        std::u8string verb {};
        luil::table_cell_ref cell {};
    };

    struct column_intent
    {
        std::u8string column {};
    };

    // 칸 편집기다. 앱 정책이 텍스트 칸으로 알아본다.
    class test_editor final : public luil::ui_element
    {
    public:
        explicit test_editor(std::u8string owner)
            : ui_element { luil::ui_element_id { kind_editor, std::move(owner) } }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }

        [[nodiscard]] std::optional<luil::text_input_snapshot> text_input() const override
        {
            return luil::text_input_snapshot { u8"row-b.name", 0u, 0u };
        }
    };

    class editor_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == kind_editor ? std::optional<luil::text_input_target> { luil::text_input_target { 1 } } : std::nullopt;
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return {};
        }

        int keys { 0 };
    };

    // 셀 탐색과 편집을 켠 표다. 이름·작성자 열을 편집할 수 있다.
    [[nodiscard]] luil::table_config editable_config(const std::u8string& cell_column)
    {
        luil::table_config config { make_config() };
        config.columns[0].editable = true;
        config.columns[1].editable = true;
        config.rows.cursor = u8"row-b";
        config.cell_column = cell_column;
        config.move_column = [](const std::u8string& column) { return luil::make_app_action(column_intent { column }); };
        config.begin_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"begin", cell }); };
        config.commit_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"commit", cell }); };
        config.cancel_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"cancel", cell }); };
        config.build_editor = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column) -> std::unique_ptr<luil::ui_element> {
            return std::make_unique<test_editor>(row.key + u8"/" + column.key);
        };
        return config;
    }

    void focus_body(luil::interaction_controller& controller)
    {
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
    }

    [[nodiscard]] luil::key_pressed_event press(const luil::key_code key, const int milliseconds)
    {
        return luil::key_pressed_event { key, false, false, false, false, at(milliseconds) };
    }

    [[nodiscard]] const luil::ui_element& find(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        return *element;
    }

    constexpr luil::ui_color fixed_color { luil::make_ui_color(255, 0, 0) };
    constexpr luil::ui_color flowing_color { luil::make_ui_color(0, 255, 0) };

    // 누를 수 있고 이름이 있는 칸이다 (고정 구역의 hit와 접근 질의가 이 칸에 닿는지 본다).
    // 색을 주면 자리를 그 색으로 채운다 — 흐르는 칸이 고정 구역에 비치는지를 픽셀로 본다.
    class swatch_probe final : public luil::ui_element
    {
    public:
        swatch_probe(std::u8string owner, const luil::ui_color fill)
            : ui_element { luil::ui_element_id { kind_cell, owner } }
            , fill_ { fill }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
            set_access_name(std::move(owner));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot&) const override
        {
            if (fill_ == 0u)
                return;
            const luil::rect_f box { bounds() };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), luil::solid_paint(fill_));
        }

    private:
        luil::ui_color fill_ { 0u };
    };

    // 열 폭의 합이 280이다. 이름이 100이라 앞 한 열을 고정하면 흐르는 열은 180이다.
    [[nodiscard]] std::vector<luil::table_column> frozen_layout_columns()
    {
        luil::table_column name {};
        name.key = u8"name";
        name.header = u8"이름";
        name.width = 100.0f;
        name.min_width = 60.0f;
        luil::table_column alpha {};
        alpha.key = u8"alpha";
        alpha.header = u8"알파";
        alpha.width = 60.0f;
        luil::table_column beta {};
        beta.key = u8"beta";
        beta.header = u8"베타";
        beta.width = 60.0f;
        luil::table_column gamma {};
        gamma.key = u8"gamma";
        gamma.header = u8"감마";
        gamma.width = 60.0f;
        gamma.align = luil::column_align::end;
        return { name, alpha, beta, gamma };
    }

    // 앞 열 `frozen`개를 고정한 표다. 표는 200 폭이고 스무 행이 넘쳐 세로 막대(16)가 서므로 가로 창은 184다 —
    // 가로로 96까지 흐르고, 고정 100을 두고도 흐르는 창이 84 남는다. 칸은 누를 수 있는 probe다.
    [[nodiscard]] luil::table_config frozen_config(const std::size_t frozen = 1u, const bool paint = false)
    {
        luil::table_config config { make_config() };
        config.columns = frozen_layout_columns();
        config.frozen_columns = frozen;
        config.build_cell = [paint](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column, bool) -> std::unique_ptr<luil::ui_element> {
            const luil::ui_color fill { paint ? (column.key == u8"name" ? fixed_color : flowing_color) : 0u };
            return std::make_unique<swatch_probe>(row.key + u8"/" + column.key, fill);
        };
        return config;
    }

    struct table_view
    {
        std::shared_ptr<const luil::ui_tree> tree {};
        const luil::table_element* table { nullptr };
    };

    [[nodiscard]] table_view arrange_table(luil::table_config config, const float width = 200.0f, const float scale = 1.0f)
    {
        auto owned { std::make_unique<luil::table_element>(std::move(config)) };
        const luil::table_element* const table { owned.get() };
        return { std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, width * scale, 300.0f * scale }, scale)), table };
    }

    [[nodiscard]] std::vector<luil::ui_element_id> child_ids(const luil::ui_element& element)
    {
        std::vector<luil::ui_element_id> ids {};
        for (const std::unique_ptr<luil::ui_element>& child : element.children())
            ids.push_back(child->id());
        return ids;
    }

    // 반투명한 색을 불투명한 바탕 위에 얹은 색이다. 경계선의 divider는 알파가 있어 바탕과 섞여 그려진다.
    [[nodiscard]] luil::ui_color over(const luil::ui_color foreground, const luil::ui_color background) noexcept
    {
        const std::uint32_t alpha { foreground >> 24U };
        const auto channel = [&](const std::uint32_t shift) {
            const std::uint32_t top { (foreground >> shift) & 0xFFU };
            const std::uint32_t bottom { (background >> shift) & 0xFFU };
            return (top * alpha + bottom * (255U - alpha) + 127U) / 255U;
        };
        return 0xFF000000U | (channel(16U) << 16U) | (channel(8U) << 8U) | channel(0U);
    }

    // 반올림 오차(채널당 둘)까지는 같은 색이다.
    [[nodiscard]] bool near_color(const luil::ui_color actual, const luil::ui_color expected) noexcept
    {
        for (const std::uint32_t shift : { 0U, 8U, 16U })
        {
            const int difference { static_cast<int>((actual >> shift) & 0xFFU) - static_cast<int>((expected >> shift) & 0xFFU) };
            if (difference > 2 || difference < -2)
                return false;
        }
        return true;
    }

    // 화면 전체 픽셀의 지문이다. 두 그림이 한 픽셀도 다르지 않은지 견준다.
    [[nodiscard]] std::uint64_t fingerprint(const luil::testing::raster_frame& frame, const int width, const int height)
    {
        std::uint64_t hash { 14695981039346656037ull };
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                hash ^= frame.pixel_at(x, y);
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    // 머리글 칸(HeaderItem)의 이름을 보조 기술이 읽는 순서대로 모은다.
    [[nodiscard]] std::vector<std::u8string> header_item_names(const luil::ui_tree& tree)
    {
        std::vector<std::u8string> names {};
        for (const luil::ui_element* const item : tree.access_children_of(find(tree, { luil::ui_element_kind::table_header, u8"files" })))
            if (item->accessibility().role == luil::access_role::header_item)
                names.push_back(item->accessibility().name);
        return names;
    }

    // 고정 열이 있는 표의 셀 탐색과 편집이다. 칸은 글로만 그려 Tab의 자리를 만들지 않는다.
    [[nodiscard]] luil::table_config frozen_nav_config(const std::size_t frozen, const std::u8string& cell_column, const float scroll_x)
    {
        luil::table_config config { frozen_config(frozen) };
        config.build_cell = {};
        config.scroll_x = scroll_x;
        for (luil::table_column& column : config.columns)
            column.editable = true;
        config.rows.cursor = u8"row-b";
        config.cell_column = cell_column;
        config.move_column = [](const std::u8string& column) { return luil::make_app_action(column_intent { column }); };
        config.begin_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"begin", cell }); };
        return config;
    }
}

TEST_CASE("Column geometry is a running sum and clamps stay in range", "[ui][table]")
{
    const std::vector<luil::table_column> columns { make_columns() };
    const std::vector<luil::table_column_span> spans { luil::table_column_spans(columns) };
    REQUIRE(spans == std::vector<luil::table_column_span> { { 0.0f, 160.0f }, { 160.0f, 90.0f }, { 250.0f, 70.0f } });
    REQUIRE(luil::table_content_width(columns) == 320.0f);

    REQUIRE(luil::table_clamp_scroll(500.0f, 320.0f, 200.0f) == 120.0f);
    REQUIRE(luil::table_clamp_scroll(-10.0f, 320.0f, 200.0f) == 0.0f);
    // 내용이 창보다 좁으면 흐르지 않는다.
    REQUIRE(luil::table_clamp_scroll(30.0f, 150.0f, 200.0f) == 0.0f);

    // 이름 열은 160에서 최소 80까지 줄 수 있다.
    REQUIRE(luil::table_resize_delta(columns[0], -50.0f) == -50.0f);
    REQUIRE(luil::table_resize_delta(columns[0], -120.0f) == -80.0f);
    REQUIRE(luil::table_resize_delta(columns[0], 30.0f) == 30.0f);
}

TEST_CASE("Header cells and body cells share column boundaries at every scale", "[ui][table]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        luil::table_config config { make_config(true) };
        config.scroll_x = 40.0f;
        const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config), scale) };
        REQUIRE(tree->duplicate_ids().empty());
        REQUIRE(tree->unarranged().empty());
        for (const std::u8string column : { u8"name", u8"author", u8"revision" })
        {
            const luil::rect_f header { find(*tree, { luil::ui_element_kind::table_header_cell, part(column) }).bounds() };
            const luil::rect_f cell { find(*tree, { kind_cell, u8"row-a/" + column }).bounds() };
            REQUIRE(header.x == cell.x);
            REQUIRE(header.width == cell.width);
        }
        // 가로 스크롤만큼 왼쪽으로 밀렸다.
        REQUIRE(find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }).bounds().x == -40.0f * scale);
    }
}

TEST_CASE("A table clamps its horizontal scroll and only realises the visible rows", "[ui][table]")
{
    luil::table_config config { make_config() };
    config.scroll_x = 1000.0f;
    auto owned { std::make_unique<luil::table_element>(std::move(config)) };
    const luil::table_element* const table { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 120.0f }, 1.0f) };
    REQUIRE(table->content_width() == 320.0f);
    // 가로 창은 세로 막대를 뺀 184다. 끝까지 흘리면 마지막 열의 끝이 세로 막대 앞에 선다.
    REQUIRE(table->scroll_x() == 136.0f);
    // 머리글 28과 가로 막대 16을 뺀 76 높이에 20짜리 행 넷과 overscan이다. 모델 스무 줄을 다 짓지 않는다.
    REQUIRE(table->body().realized().end - table->body().realized().begin < 10u);
}

TEST_CASE("Pressing a header asks the app to sort and keeps focus on the body", "[ui][table]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(make_config()));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 14.0f, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { 100.0f, 14.0f, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<sort_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<sort_intent>(clicked, 0)->column == u8"name");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });

    // 정렬할 수 없는 열의 머리글은 누를 것이 없다.
    const luil::table_config config { make_config() };
    const std::shared_ptr<const luil::ui_tree> tree { published(config) };
    REQUIRE(find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"revision") }).action(luil::ui_trigger::left_click) == nullptr);
}

TEST_CASE("Dragging a column boundary reports width changes without going below the minimum", "[ui][table]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(make_config()));

    // 이름 열의 오른쪽 경계는 x = 160이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 160.0f, 14.0f, luil::pointer_button::left, at(0) }));
    const auto widened { controller.process(luil::pointer_moved_event { 175.0f, 14.0f, at(10) }) };
    REQUIRE(message_at<resize_intent>(widened, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(widened, 0)->column == u8"name");
    REQUIRE(message_at<resize_intent>(widened, 0)->delta == 15.0f);

    // 폭은 앱 상태라 이 tree에서는 여전히 160이다. 160에서 최소 80까지만 줄 수 있다.
    const auto narrowed { controller.process(luil::pointer_moved_event { 10.0f, 14.0f, at(20) }) };
    REQUIRE(message_at<resize_intent>(narrowed, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(narrowed, 0)->delta == -80.0f);

    const auto released { controller.process(luil::pointer_released_event { 10.0f, 14.0f, luil::pointer_button::left, at(30) }) };
    REQUIRE(message_at<resize_end_intent>(released, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(released, 0)->column == u8"name");
}

TEST_CASE("A horizontal wheel scrolls the table and a vertical wheel scrolls its body", "[ui][table][wheel]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config()) };
    const luil::mouse_wheel_event right { .x = 100.0f, .y = 100.0f, .delta = 120.0f, .horizontal = true };
    const auto sideways { luil::route_wheel(*tree, right, -(right.delta / 120.0f) * luil::input_wheel_scroll_step) };
    REQUIRE(message_at<scroll_x_intent>(sideways, 0) != nullptr);
    REQUIRE(message_at<scroll_x_intent>(sideways, 0)->delta == luil::input_wheel_scroll_step);

    const luil::mouse_wheel_event down { .x = 100.0f, .y = 100.0f, .delta = -120.0f };
    const auto downward { luil::route_wheel(*tree, down, -(down.delta / 120.0f) * luil::input_wheel_scroll_step) };
    REQUIRE(message_at<scroll_y_intent>(downward, 0) != nullptr);
}

TEST_CASE("Left and right scroll the table sideways once the body leaves them", "[ui][table][keys]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(make_config()));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });

    const auto right { controller.process(luil::key_pressed_event { luil::key_code::arrow_right, false, false, false, false, at(10) }) };
    REQUIRE(message_at<scroll_x_intent>(right, 0) != nullptr);
    REQUIRE(message_at<scroll_x_intent>(right, 0)->delta == luil::table_key_scroll_step);

    // 이미 왼쪽 끝이라 ←는 키만 삼킨다.
    REQUIRE(controller.process(luil::key_pressed_event { luil::key_code::arrow_left, false, false, false, false, at(20) }).empty());

    // ↓는 여전히 본문의 것이다.
    const auto down { controller.process(luil::key_pressed_event { luil::key_code::arrow_down, false, false, false, false, at(30) }) };
    REQUIRE(message_at<move_intent>(down, 0) != nullptr);
}

TEST_CASE("Assistive technology reads a data grid with header items and data items", "[ui][table][accessibility]")
{
    luil::table_config config { make_config() };
    config.rows.selected = u8"row-b";
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    REQUIRE(find(*tree, { luil::ui_element_kind::table, u8"files" }).accessibility().role == luil::access_role::none);
    REQUIRE(find(*tree, { luil::ui_element_kind::table_header, u8"files" }).accessibility().role == luil::access_role::header);
    const luil::access_info header { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"author") }).accessibility() };
    REQUIRE(header.role == luil::access_role::header_item);
    REQUIRE(header.name == u8"작성자");
    REQUIRE(find(*tree, { luil::ui_element_kind::virtual_list, u8"files" }).accessibility().role == luil::access_role::table);
    const luil::access_info row { find(*tree, luil::virtual_list_row_id(u8"files", u8"row-b")).accessibility() };
    REQUIRE(row.role == luil::access_role::data_item);
    REQUIRE(row.selected == true);
}

TEST_CASE("The tree column indents its cells and holds the triangles", "[ui][table][tree]")
{
    luil::table_config config { make_config(true) };
    config.tree_column = u8"name";
    config.rows.items[0].expansion = luil::list_expansion::expanded;
    config.rows.items[1].depth = 1;
    config.rows.items[1].expansion = luil::list_expansion::collapsed;
    config.rows.items[2].depth = 2;
    config.rows.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(sort_intent { key + (expanded ? u8"+" : u8"-") }); };
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    REQUIRE(tree->duplicate_ids().empty());

    // 삼각형은 트리 열 안, 깊이만큼 들여 선다. 행 앞의 가상 목록 삼각형은 서지 않는다.
    const luil::ui_element& top { find(*tree, { luil::ui_element_kind::table_expander, part(u8"row-a") }) };
    const luil::ui_element& child { find(*tree, { luil::ui_element_kind::table_expander, part(u8"row-b") }) };
    REQUIRE(top.bounds().x == luil::table_cell_padding);
    REQUIRE(child.bounds().x - top.bounds().x == luil::list_indent_step);
    REQUIRE(tree->find(luil::virtual_list_expander_id(u8"files", u8"row-a")) == nullptr);
    REQUIRE(tree->find({ luil::ui_element_kind::table_expander, part(u8"row-c") }) == nullptr);

    // 앱의 칸은 들여쓰기와 삼각형 뒤에 선다. 다른 열은 밀리지 않는다.
    const float lead { 2.0f * luil::list_indent_step + luil::list_expander_width };
    REQUIRE(find(*tree, { kind_cell, u8"row-c/name" }).bounds().x == lead);
    REQUIRE(find(*tree, { kind_cell, u8"row-c/author" }).bounds().x == 160.0f);

    // 펼침 상태는 행(DataItem)이 알린다.
    REQUIRE(find(*tree, luil::virtual_list_row_id(u8"files", u8"row-b")).accessibility().expanded == false);
}

TEST_CASE("The check column shows each row's state and toggles without stealing focus", "[ui][table][check]")
{
    luil::table_config config { make_config() };
    config.check_column = u8"name";
    config.check_state_of = [](const luil::virtual_list_item& row, std::size_t) {
        return row.key == u8"row-a" ? luil::check_state::mixed : row.key == u8"row-b" ? luil::check_state::checked : luil::check_state::unchecked;
    };
    config.toggle_check = [](const std::u8string& row) { return luil::make_app_action(resize_end_intent { row }); };
    const std::shared_ptr<const luil::ui_tree> tree { published(config) };
    REQUIRE(tree->duplicate_ids().empty());

    const luil::ui_element& mixed { find(*tree, { luil::ui_element_kind::check, part(u8"row-a") }) };
    REQUIRE(mixed.accessibility().mixed);
    REQUIRE(mixed.accessibility().name == u8"row-a");
    REQUIRE(find(*tree, { luil::ui_element_kind::check, part(u8"row-b") }).accessibility().checked == true);
    REQUIRE(mixed.tab_stop() == false);

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const luil::rect_f box { mixed.bounds() };
    static_cast<void>(controller.process(luil::pointer_pressed_event { box.x + 10.0f, box.y + box.height / 2.0f, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { box.x + 10.0f, box.y + box.height / 2.0f, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<resize_end_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(clicked, 0)->column == u8"row-a");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });

    // 본문이 Space를 실행 키로 쓰지 않으므로 Space는 커서 행의 체크다.
    config.rows.cursor = u8"row-c";
    controller.set_tree(published(config));
    const auto spaced { controller.process(luil::key_pressed_event { luil::key_code::space, false, false, false, false, at(20) }) };
    REQUIRE(message_at<resize_end_intent>(spaced, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(spaced, 0)->column == u8"row-c");
}

TEST_CASE("With cell navigation left and right move the cell cursor and reveal it", "[ui][table][cells]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(editable_config(u8"author")));
    focus_body(controller);

    const auto right { controller.process(press(luil::key_code::arrow_right, 10)) };
    REQUIRE(message_at<column_intent>(right, 0) != nullptr);
    REQUIRE(message_at<column_intent>(right, 0)->column == u8"revision");
    // 리비전 열(250~320)은 창(세로 막대를 뺀 0~184) 밖이라 끝을 맞춰 들인다.
    REQUIRE(message_at<scroll_x_intent>(right, 1) != nullptr);
    REQUIRE(message_at<scroll_x_intent>(right, 1)->delta == 136.0f);

    const auto left { controller.process(press(luil::key_code::arrow_left, 20)) };
    REQUIRE(message_at<column_intent>(left, 0) != nullptr);
    REQUIRE(message_at<column_intent>(left, 0)->column == u8"name");
    // 이미 보이는 열은 흘리지 않는다.
    REQUIRE(left.size() == 1u);

    // 끝 열에서 →는 키만 삼킨다.
    controller.set_tree(published(editable_config(u8"revision")));
    REQUIRE(controller.process(press(luil::key_code::arrow_right, 30)).empty());
}

TEST_CASE("A cell cursor in the tree column walks the hierarchy before it leaves the column", "[ui][table][cells][tree]")
{
    luil::table_config config { editable_config(u8"name") };
    config.tree_column = u8"name";
    config.rows.items[1].expansion = luil::list_expansion::expanded;
    config.rows.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(cell_intent { expanded ? u8"expand" : u8"collapse", { key, {} } }); };
    luil::interaction_controller controller {};
    controller.set_tree(published(std::move(config)));
    focus_body(controller);

    const auto collapsed { controller.process(press(luil::key_code::arrow_left, 10)) };
    REQUIRE(message_at<cell_intent>(collapsed, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(collapsed, 0)->verb == u8"collapse");
    // 펼친 가지 위의 →는 첫 자식으로 가려 하지만 자식이 없어 키만 삼킨다 (계층 키가 이긴다).
    REQUIRE(controller.process(press(luil::key_code::arrow_right, 20)).empty());
}

TEST_CASE("F2 starts editing an editable cell and nothing else", "[ui][table][edit]")
{
    editor_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(editable_config(u8"author")));
    focus_body(controller);
    const auto started { controller.process(press(luil::key_code::f2, 10)) };
    REQUIRE(message_at<cell_intent>(started, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(started, 0)->verb == u8"begin");
    REQUIRE(message_at<cell_intent>(started, 0)->cell == luil::table_cell_ref { u8"row-b", u8"author" });

    // 편집할 수 없는 열에서 F2는 앱으로 흐른다.
    controller.set_tree(published(editable_config(u8"revision")));
    static_cast<void>(controller.process(press(luil::key_code::f2, 20)));
    REQUIRE(policy.keys == 1);
}

TEST_CASE("An editor takes focus, commits on Enter, cancels on Escape and hands focus back", "[ui][table][edit]")
{
    editor_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(editable_config(u8"name")));
    focus_body(controller);

    luil::table_config editing { editable_config(u8"name") };
    editing.editing = luil::table_cell_ref { u8"row-b", u8"name" };
    const std::shared_ptr<const luil::ui_tree> with_editor { published(editing) };
    REQUIRE(with_editor->duplicate_ids().empty());
    controller.set_tree(with_editor);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_editor, u8"row-b/name" });
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_editor, u8"row-b/name" });
    // 편집기는 그 칸 자리에 선다.
    REQUIRE(find(*with_editor, { kind_editor, u8"row-b/name" }).bounds().x == 0.0f);

    const auto committed { controller.process(press(luil::key_code::enter, 10)) };
    REQUIRE(message_at<cell_intent>(committed, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(committed, 0)->verb == u8"commit");
    REQUIRE(message_at<cell_intent>(committed, 0)->cell == luil::table_cell_ref { u8"row-b", u8"name" });

    const auto cancelled { controller.process(press(luil::key_code::escape, 20)) };
    REQUIRE(cancelled.size() == 1u);
    REQUIRE(message_at<cell_intent>(cancelled, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(cancelled, 0)->verb == u8"cancel");

    // 앱이 편집을 끝내면 편집기가 사라지고 초점이 본문으로 돌아온다.
    controller.set_tree(published(editable_config(u8"name")));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
    REQUIRE(policy.keys == 0);
}

TEST_CASE("Dragging a header onto another asks the app to reorder the columns", "[ui][table][columns]")
{
    luil::table_config config { make_config() };
    config.reorder_column = [](const std::u8string& moved, const std::u8string& target) { return luil::make_app_action(resize_intent { moved + u8">" + target, 0.0f }); };
    config.column_menu = [](const std::u8string& column, const luil::ui_action_context&) { return luil::make_app_action(column_intent { column }); };
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    const luil::ui_element& name { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }) };
    const luil::ui_element& author { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"author") }) };

    REQUIRE(author.drag() != nullptr);
    const luil::drag_payload payload { author.drag()->make_payload({ author.id(), 170.0f, 10.0f }) };
    REQUIRE(payload.dragged_owner == u8"author");
    REQUIRE(payload.grab_offset_x == 10.0f);
    // 자기 자리에는 놓지 않는다. 다른 표의 머리글 칸은 받지 않는다.
    REQUIRE(author.drop()->accepts(payload) == false);
    REQUIRE(name.drop()->accepts(payload));
    luil::drag_payload foreign { payload };
    foreign.container.owner = u8"other";
    REQUIRE(name.drop()->accepts(foreign) == false);
    const auto dropped { name.drop()->on_drop(payload, { name.id(), 0.0f, 0.0f }) };
    REQUIRE(message_at<resize_intent>(dropped, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(dropped, 0)->column == u8"author>name");

    // 정렬할 수 없는 열의 머리글도 끌고 메뉴를 열 수 있다.
    const luil::ui_element& revision { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"revision") }) };
    REQUIRE(revision.drag() != nullptr);
    const luil::ui_action* const menu { revision.action(luil::ui_trigger::right_click) };
    REQUIRE(menu != nullptr);
    const auto opened { (*menu)({ revision.id(), 260.0f, 10.0f }) };
    REQUIRE(message_at<column_intent>(opened, 0) != nullptr);
    REQUIRE(message_at<column_intent>(opened, 0)->column == u8"revision");
}

TEST_CASE("A horizontal bar stands under the rows only while the columns overflow", "[ui][table][scrollbar]")
{
    luil::table_config config { make_config() };
    config.scroll_x = 50.0f;
    config.scroll_horizontal_to = [](const float offset) { return luil::make_app_action(scroll_x_to_intent { offset }); };
    auto owned { std::make_unique<luil::table_element>(std::move(config)) };
    const luil::table_element* const table { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };
    REQUIRE(tree.duplicate_ids().empty());
    REQUIRE(tree.unarranged().empty());

    // 열(320)이 창(세로 막대를 뺀 184)보다 넓어 막대가 선다. 범위와 값이 표의 가로 스크롤 그대로다.
    const luil::scrollbar_element* const bar { table->horizontal_bar() };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->visible());
    REQUIRE(bar->tab_stop() == false);
    REQUIRE(bar->pointer_focus_target() == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
    const luil::access_info info { bar->accessibility() };
    REQUIRE(info.role == luil::access_role::scroll_bar);
    REQUIRE(info.orientation == luil::access_orientation::horizontal);
    REQUIRE(table->scroll_x() == 50.0f);
    REQUIRE(info.range == luil::access_range { 0.0f, 136.0f, table->scroll_x() });

    // 막대는 본문 아래에 서고, 세로 막대의 폭만큼 짧아 구석을 비운다.
    const luil::rect_f box { bar->bounds() };
    REQUIRE(box.x == 0.0f);
    REQUIRE(box.y == 300.0f - luil::scroll_area_bar_width);
    REQUIRE(box.width == 200.0f - luil::scroll_area_bar_width);
    REQUIRE(box.height == luil::scroll_area_bar_width);
    // 본문 창은 머리글과 막대를 뺀 높이다. 마지막 행이 막대 밑에 숨지 않는다.
    const float rows { 300.0f - luil::table_header_height - luil::scroll_area_bar_width };
    REQUIRE(table->body().bounds().height == rows);
    REQUIRE(table->body().metrics().viewport_height == rows);

    // 보조 기술의 값 설정은 절대 메시지다. 범위 밖 값은 막대가 다듬는다.
    const std::optional<std::vector<luil::input_action>> set { bar->access_actions({ luil::access_command::set_value, 500.0f }) };
    REQUIRE(set.has_value());
    REQUIRE(message_at<scroll_x_to_intent>(*set, 0) != nullptr);
    REQUIRE(message_at<scroll_x_to_intent>(*set, 0)->offset == 136.0f);

    // 열이 창 안에 들어가면 막대가 숨고 본문이 그 자리를 되찾는다.
    luil::table_config narrow_config { make_config() };
    narrow_config.columns[0].width = 60.0f;
    narrow_config.columns[1].width = 50.0f;
    narrow_config.columns[2].width = 40.0f;
    auto narrow_owned { std::make_unique<luil::table_element>(std::move(narrow_config)) };
    const luil::table_element* const narrow { narrow_owned.get() };
    const luil::ui_tree narrow_tree { luil::make_arranged_tree(std::move(narrow_owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };
    REQUIRE(narrow_tree.unarranged().empty());
    REQUIRE(narrow->horizontal_bar() != nullptr);
    REQUIRE(narrow->horizontal_bar()->visible() == false);
    REQUIRE(narrow->body().bounds().height == 300.0f - luil::table_header_height);

    // 가로로 흐르지 않는 표와 막대를 끈 표에는 막대가 없다.
    luil::table_config fixed { make_config() };
    fixed.scroll_horizontal = {};
    REQUIRE(luil::table_element { std::move(fixed) }.horizontal_bar() == nullptr);
    luil::table_config hidden { make_config() };
    hidden.horizontal_bar = luil::scrollbar_visibility::never;
    REQUIRE(luil::table_element { std::move(hidden) }.horizontal_bar() == nullptr);
}

TEST_CASE("A horizontal bar that pushes the rows into overflow still leaves the corner", "[ui][table][scrollbar]")
{
    // 열세 행(260)은 머리글만 뺀 창(272)에 들어가지만, 가로 막대가 선 창(256)에는 넘친다.
    luil::table_config config { make_config() };
    config.rows.items.resize(13u);
    config.scroll_x = 1000.0f;
    auto owned { std::make_unique<luil::table_element>(std::move(config)) };
    const luil::table_element* const table { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f) };
    REQUIRE(tree.unarranged().empty());
    REQUIRE(table->body().metrics().overflowing());
    REQUIRE(find(tree, { luil::ui_element_kind::scroll_area_bar, u8"files" }).visible());
    // 세로 막대가 서므로 가로 창도 그만큼 준다. 막대와 다듬기가 같은 창을 본다.
    REQUIRE(table->horizontal_bar()->bounds().width == 200.0f - luil::scroll_area_bar_width);
    REQUIRE(table->scroll_x() == 320.0f - (200.0f - luil::scroll_area_bar_width));
}

TEST_CASE("A column can shorten its text in the middle", "[ui][table][elide]")
{
    // 기본은 지금처럼 뒤를 줄인다.
    REQUIRE(luil::table_column {}.elide == luil::text_elide::end);

    // 이 test 실행 파일은 글꼴 관리자가 비어 있어(`core_platform_stub.cpp`) 글 폭을 잴 수 없다. 줄인 글의 앞뒤는
    // `elide_text_middle`이 맡고, 여기서는 열의 값이 그리기까지 닿아 그 길이 끝까지 도는지를 본다.
    luil::table_config config { make_config() };
    config.columns[0].elide = luil::text_elide::middle;
    config.columns[2].elide = luil::text_elide::middle;
    config.cell_text = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column) {
        return u8"trunk/source/very/deep/folder/" + row.key + u8"." + column.key;
    };
    auto owned { std::make_unique<luil::table_element>(std::move(config)) };
    const luil::table_element* const table { owned.get() };
    const auto tree { std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 400.0f, 600.0f }, 2.0f)) };
    REQUIRE(table->config().columns[0].elide == luil::text_elide::middle);
    REQUIRE(table->config().columns[1].elide == luil::text_elide::end);

    luil::testing::raster_frame frame { 400, 600, luil::color_palette_for(luil::color_theme::dark), 2.0f };
    // 가운데 말줄임 길을 지나 끝까지 그린다. 픽셀은 글꼴에 매이므로 보지 않는다.
    frame.draw(*tree, {});
    REQUIRE(tree->unarranged().empty());
}

TEST_CASE("Frozen width, fit and reveal are plain functions of the columns", "[ui][table][frozen]")
{
    const std::vector<luil::table_column> columns { frozen_layout_columns() };
    REQUIRE(luil::table_frozen_width(columns, 0u) == 0.0f);
    REQUIRE(luil::table_frozen_width(columns, 1u) == 100.0f);
    REQUIRE(luil::table_frozen_width(columns, 3u) == 220.0f);
    // 열 수보다 크면 열 수로 다듬는다.
    REQUIRE(luil::table_frozen_width(columns, 99u) == 280.0f);
    // 폭이 음수인 열은 0으로 본다.
    std::vector<luil::table_column> negative { columns };
    negative[0].width = -30.0f;
    REQUIRE(luil::table_frozen_width(negative, 2u) == 60.0f);
    // 앞 N열의 폭 합은 N번째 열의 시작이다 (같은 누적이라 경계가 어긋나지 않는다).
    const std::vector<luil::table_column_span> spans { luil::table_column_spans(columns) };
    for (std::size_t count = 0; count < columns.size(); ++count)
        REQUIRE(luil::table_frozen_width(columns, count) == spans[count].begin);

    // 고정 폭을 두고도 흐르는 창이 한 걸음(48) 이상 남아야 고정이 선다.
    REQUIRE(luil::table_frozen_fits(100.0f, 148.0f));
    REQUIRE(luil::table_frozen_fits(100.0f, 147.5f) == false);
    REQUIRE(luil::table_frozen_fits(0.0f, luil::table_key_scroll_step));
    REQUIRE(luil::table_frozen_fits(300.0f, 200.0f) == false);

    // 창은 [고정 폭 + 스크롤, 스크롤 + 창 폭]이다. 고정 100, 창 184.
    const luil::table_column_span alpha { 100.0f, 60.0f };
    const luil::table_column_span gamma { 220.0f, 60.0f };
    // 이미 보인다.
    REQUIRE(luil::table_reveal_delta(alpha, 0.0f, 100.0f, 184.0f) == 0.0f);
    // 오른쪽 밖은 끝을 창 끝에 맞춘다.
    REQUIRE(luil::table_reveal_delta(gamma, 0.0f, 100.0f, 184.0f) == 96.0f);
    // 고정 구역 아래로 간 칸은 창 시작(고정 폭 뒤)에 맞춘다.
    REQUIRE(luil::table_reveal_delta(alpha, 96.0f, 100.0f, 184.0f) == -96.0f);
    // 창보다 넓은 열은 시작을 맞춘다.
    REQUIRE(luil::table_reveal_delta({ 120.0f, 200.0f }, 0.0f, 100.0f, 184.0f) == 20.0f);

    // 고정 폭이 0이면 고정 없는 표의 들이기와 같은 값이다.
    const auto without_freeze = [](const luil::table_column_span& span, const float scroll, const float viewport) {
        const float left { span.begin };
        const float right { left + span.width };
        if (left < scroll)
            return left - scroll;
        if (right > scroll + viewport)
            return std::min(left, right - viewport) - scroll;
        return 0.0f;
    };
    const luil::table_column_span spans_to_try[] { { 0.0f, 100.0f }, { 100.0f, 60.0f }, { 220.0f, 60.0f }, { 50.0f, 400.0f } };
    for (const float scroll : { 0.0f, 40.0f, 96.0f })
        for (const luil::table_column_span& span : spans_to_try)
            REQUIRE(luil::table_reveal_delta(span, scroll, 0.0f, 184.0f) == without_freeze(span, scroll, 184.0f));
}

TEST_CASE("A frozen column stays put while the others flow and every part shares one edge", "[ui][table][frozen]")
{
    const std::u8string keys[] { u8"name", u8"alpha", u8"beta", u8"gamma" };
    const float begins[] { 0.0f, 100.0f, 160.0f, 220.0f };
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        luil::table_config config { frozen_config() };
        config.scroll_x = 40.0f;
        const table_view view { arrange_table(std::move(config), 200.0f, scale) };
        const luil::ui_tree& tree { *view.tree };
        REQUIRE(tree.duplicate_ids().empty());
        REQUIRE(tree.unarranged().empty());
        REQUIRE(view.table->frozen_columns() == 1u);
        REQUIRE(view.table->frozen_width() == 100.0f);
        REQUIRE(view.table->scroll_x() == 40.0f);
        // 끌기 자동 스크롤의 왼쪽 띠가 고정 경계에 서도록 가로 창이 고정 폭(논리)을 싣는다.
        REQUIRE(view.table->scroll() != nullptr);
        REQUIRE(view.table->scroll()->leading_inset == 100.0f);
        // 오른쪽 띠는 세로 막대 앞에 서도록 막대 폭을 싣는다. 물리 폭을 그대로 실으면 배율마다 값이 달라진다.
        REQUIRE(view.table->scroll()->trailing_inset == luil::scroll_area_bar_width);

        for (std::size_t index = 0; index < 4u; ++index)
        {
            const luil::rect_f header { find(tree, { luil::ui_element_kind::table_header_cell, part(keys[index]) }).bounds() };
            const luil::rect_f cell { find(tree, { kind_cell, u8"row-a/" + keys[index] }).bounds() };
            // 머리글 칸과 행 칸의 bounds가 같다.
            REQUIRE(header.x == cell.x);
            REQUIRE(header.width == cell.width);
            // 고정 열은 제자리에 서고 흐르는 열만 스크롤만큼 밀린다.
            const float shift { index == 0u ? 0.0f : 40.0f };
            REQUIRE(header.x == (begins[index] - shift) * scale);
        }

        // 레인의 왼쪽은 마지막 고정 칸의 오른쪽이고 오른쪽은 머리글·행의 오른쪽 끝이다.
        const luil::rect_f last_fixed { find(tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }).bounds() };
        const luil::rect_f header_lane { find(tree, { luil::ui_element_kind::table_header_lane, part(u8"lane") }).bounds() };
        const luil::rect_f row { find(tree, { luil::ui_element_kind::table_row, part(u8"row-a") }).bounds() };
        const luil::rect_f row_lane { find(tree, { luil::ui_element_kind::table_row_lane, part(u8"row-a") }).bounds() };
        REQUIRE(header_lane.x == last_fixed.x + last_fixed.width);
        REQUIRE(row_lane.x == header_lane.x);
        REQUIRE(header_lane.x + header_lane.width == 200.0f * scale);
        REQUIRE(row_lane.x + row_lane.width == row.x + row.width);
        REQUIRE(row_lane.y == row.y);
        REQUIRE(row_lane.height == row.height);
    }
}

TEST_CASE("The horizontal scroll source carries the vertical bar width only while that bar stands", "[ui][table][auto-scroll]")
{
    // 세로 막대가 서면 가로 창이 그만큼 좁다. 싣는 폭과 가로 창이 같은 막대를 본다.
    for (const float scale : { 1.0f, 2.0f })
    {
        const table_view barred { arrange_table(frozen_config(0u), 200.0f, scale) };
        REQUIRE(barred.table->scroll()->trailing_inset == luil::scroll_area_bar_width);
        REQUIRE(barred.table->scroll()->leading_inset == 0.0f);

        // 막대가 서지 않는 표는 창 끝까지 내용이 보이므로 0이다.
        luil::table_config plain { frozen_config(0u) };
        plain.rows.bar = luil::scrollbar_visibility::never;
        const table_view unbarred { arrange_table(std::move(plain), 200.0f, scale) };
        REQUIRE(unbarred.table->scroll()->trailing_inset == 0.0f);
    }
}

TEST_CASE("A freeze lets go when the window leaves less than one key step and returns when it widens", "[ui][table][frozen]")
{
    luil::table_config config { frozen_config() };
    config.scroll_x = 20.0f;

    // 창 140에서 가로 창은 124이고 흐르는 몫이 24라 고정이 풀린다. 스크롤 값은 앱 상태 그대로다.
    const table_view narrow { arrange_table(config, 140.0f) };
    REQUIRE(narrow.tree->unarranged().empty());
    REQUIRE(narrow.table->frozen_columns() == 0u);
    REQUIRE(narrow.table->frozen_width() == 0.0f);
    REQUIRE(narrow.table->scroll_x() == 20.0f);
    REQUIRE(narrow.table->scroll()->leading_inset == 0.0f);
    // 머리글이 스크롤을 따라 움직인다. 레인은 서 있되 머리글과 행 전체를 덮는다 (구조는 창 폭과 무관하다).
    REQUIRE(find(*narrow.tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }).bounds().x == -20.0f);
    REQUIRE(find(*narrow.tree, { kind_cell, u8"row-a/name" }).bounds().x == -20.0f);
    const luil::rect_f lane { find(*narrow.tree, { luil::ui_element_kind::table_header_lane, part(u8"lane") }).bounds() };
    REQUIRE(lane.x == 0.0f);
    REQUIRE(lane.width == 140.0f);
    const luil::rect_f row_lane { find(*narrow.tree, { luil::ui_element_kind::table_row_lane, part(u8"row-a") }).bounds() };
    REQUIRE(row_lane.x == find(*narrow.tree, { luil::ui_element_kind::table_row, part(u8"row-a") }).bounds().x);

    // 넓히면 고정이 돌아온다.
    const table_view wide { arrange_table(config, 200.0f) };
    REQUIRE(wide.table->frozen_columns() == 1u);
    REQUIRE(wide.table->scroll()->leading_inset == 100.0f);
    REQUIRE(find(*wide.tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }).bounds().x == 0.0f);

    // 가로 막대의 판정과 값은 고정 열이 없을 때와 같다 (열 폭의 합으로 정해져 고정과 맞물려 돌지 않는다).
    luil::table_config plain_config { frozen_config(0u) };
    plain_config.scroll_x = 20.0f;
    const table_view plain { arrange_table(std::move(plain_config), 200.0f) };
    REQUIRE(plain.table->scroll()->leading_inset == 0.0f);
    REQUIRE(wide.table->content_width() == plain.table->content_width());
    REQUIRE(wide.table->scroll_x() == plain.table->scroll_x());
    REQUIRE(wide.table->horizontal_bar() != nullptr);
    REQUIRE(wide.table->horizontal_bar()->visible());
    REQUIRE(wide.table->horizontal_bar()->accessibility().range == plain.table->horizontal_bar()->accessibility().range);
    REQUIRE(wide.table->horizontal_bar()->bounds().x == plain.table->horizontal_bar()->bounds().x);
    REQUIRE(wide.table->horizontal_bar()->bounds().width == plain.table->horizontal_bar()->bounds().width);
}

TEST_CASE("With no frozen columns the tree has no lanes and a released freeze draws exactly like none", "[ui][table][frozen]")
{
    const std::u8string keys[] { u8"name", u8"alpha", u8"beta", u8"gamma" };

    // 고정 수가 0이면 tree는 지금과 같다: 머리글은 칸들 뒤에 손잡이들이고 행은 칸들이다. 레인이 없다.
    const table_view plain { arrange_table(frozen_config(0u)) };
    REQUIRE(plain.tree->find({ luil::ui_element_kind::table_header_lane, part(u8"lane") }) == nullptr);
    REQUIRE(plain.tree->find({ luil::ui_element_kind::table_row_lane, part(u8"row-a") }) == nullptr);
    std::vector<luil::ui_element_id> header_children {};
    for (const std::u8string& key : keys)
        header_children.push_back({ luil::ui_element_kind::table_header_cell, part(key) });
    for (const std::u8string& key : keys)
        header_children.push_back({ luil::ui_element_kind::table_column_grip, part(key) });
    REQUIRE(child_ids(find(*plain.tree, { luil::ui_element_kind::table_header, u8"files" })) == header_children);
    std::vector<luil::ui_element_id> row_children {};
    for (const std::u8string& key : keys)
        row_children.push_back({ kind_cell, u8"row-a/" + key });
    REQUIRE(child_ids(find(*plain.tree, { luil::ui_element_kind::table_row, part(u8"row-a") })) == row_children);
    // 열 수 이상으로 고정해도 흐르는 열이 없어 레인이 서지 않는다. 열 수보다 큰 수는 열 수로 본다.
    for (const std::size_t frozen : { std::size_t { 4u }, std::size_t { 99u } })
    {
        const table_view all { arrange_table(frozen_config(frozen)) };
        REQUIRE(all.tree->find({ luil::ui_element_kind::table_header_lane, part(u8"lane") }) == nullptr);
        REQUIRE(all.tree->find({ luil::ui_element_kind::table_row_lane, part(u8"row-a") }) == nullptr);
        REQUIRE(child_ids(find(*all.tree, { luil::ui_element_kind::table_header, u8"files" })) == header_children);
        REQUIRE(all.table->frozen_columns() == 0u);
    }

    // 창이 좁아 고정이 풀린 배치는 고정이 없는 표와 한 픽셀도 다르지 않다 (레인은 서 있되 행 전체를 덮는다).
    luil::table_config frozen { frozen_config(1u, true) };
    frozen.scroll_x = 30.0f;
    luil::table_config none { frozen_config(0u, true) };
    none.scroll_x = 30.0f;
    const table_view released { arrange_table(std::move(frozen), 140.0f, 2.0f) };
    const table_view free_view { arrange_table(std::move(none), 140.0f, 2.0f) };
    REQUIRE(released.table->frozen_columns() == 0u);
    for (const std::u8string& key : keys)
    {
        REQUIRE(find(*released.tree, { luil::ui_element_kind::table_header_cell, part(key) }).bounds().x == find(*free_view.tree, { luil::ui_element_kind::table_header_cell, part(key) }).bounds().x);
        REQUIRE(find(*released.tree, { kind_cell, u8"row-a/" + key }).bounds().x == find(*free_view.tree, { kind_cell, u8"row-a/" + key }).bounds().x);
    }
    luil::testing::raster_frame released_frame { 280, 600, luil::color_palette_for(luil::color_theme::dark), 2.0f };
    released_frame.draw(*released.tree, {});
    luil::testing::raster_frame free_frame { 280, 600, luil::color_palette_for(luil::color_theme::dark), 2.0f };
    free_frame.draw(*free_view.tree, {});
    REQUIRE(fingerprint(released_frame, 280, 600) == fingerprint(free_frame, 280, 600));
}

TEST_CASE("A frozen table keeps the fixed parts first and the flowing ones in a lane", "[ui][table][frozen]")
{
    const table_view view { arrange_table(frozen_config(1u)) };
    const luil::ui_tree& tree { *view.tree };
    REQUIRE(tree.duplicate_ids().empty());
    const auto header_cell = [](const std::u8string& key) { return luil::ui_element_id { luil::ui_element_kind::table_header_cell, part(key) }; };
    const auto grip = [](const std::u8string& key) { return luil::ui_element_id { luil::ui_element_kind::table_column_grip, part(key) }; };
    const luil::ui_element_id header_lane { luil::ui_element_kind::table_header_lane, part(u8"lane") };

    // 머리글: 고정 칸, 레인(흐르는 칸, 흐르는 손잡이), 고정 손잡이. 뒤 자식이 이기므로 경계에 걸친 마지막 고정 열의
    // 손잡이가 흐르는 칸을 이긴다. 칸(HeaderItem)은 어느 쪽이든 열 순서다.
    REQUIRE(child_ids(find(tree, { luil::ui_element_kind::table_header, u8"files" }))
        == std::vector<luil::ui_element_id> { header_cell(u8"name"), header_lane, grip(u8"name") });
    REQUIRE(child_ids(find(tree, header_lane))
        == std::vector<luil::ui_element_id> { header_cell(u8"alpha"), header_cell(u8"beta"), header_cell(u8"gamma"), grip(u8"alpha"), grip(u8"beta"), grip(u8"gamma") });

    // 행: 고정 부품, 레인. Tab 순서(tree의 앞에서 뒤)가 열 순서와 맞는다.
    const luil::ui_element_id row_lane { luil::ui_element_kind::table_row_lane, part(u8"row-a") };
    REQUIRE(child_ids(find(tree, { luil::ui_element_kind::table_row, part(u8"row-a") }))
        == std::vector<luil::ui_element_id> { { kind_cell, u8"row-a/name" }, row_lane });
    REQUIRE(child_ids(find(tree, row_lane))
        == std::vector<luil::ui_element_id> { { kind_cell, u8"row-a/alpha" }, { kind_cell, u8"row-a/beta" }, { kind_cell, u8"row-a/gamma" } });
    REQUIRE(find(tree, row_lane).clip_children());
    REQUIRE(find(tree, header_lane).clip_children());
}

TEST_CASE("Hit and assistive queries stop at the frozen edge and skip the flowing cells beneath it", "[ui][table][frozen][accessibility]")
{
    luil::table_config config { frozen_config() };
    config.scroll_x = 96.0f;
    const table_view view { arrange_table(std::move(config)) };
    const luil::ui_tree& tree { *view.tree };
    REQUIRE(tree.unarranged().empty());
    REQUIRE(view.table->scroll_x() == 96.0f);

    const luil::ui_element_id name_cell { kind_cell, u8"row-a/name" };
    const luil::ui_element_id alpha_cell { kind_cell, u8"row-a/alpha" };
    const luil::ui_element_id beta_cell { kind_cell, u8"row-a/beta" };
    const luil::ui_element_id gamma_cell { kind_cell, u8"row-a/gamma" };

    // 알파 칸은 [4, 64]에서 고정 구역 아래로 통째로 숨었다. 그 자리의 hit와 접근 질의는 고정 칸이 답한다.
    REQUIRE(find(tree, alpha_cell).bounds().x == 4.0f);
    REQUIRE(tree.hit_test(30.0f, 38.0f) != nullptr);
    REQUIRE(tree.hit_test(30.0f, 38.0f)->id() == name_cell);
    REQUIRE(luil::access_element_at(tree, 30.0f, 38.0f) != nullptr);
    REQUIRE(luil::access_element_at(tree, 30.0f, 38.0f)->id() == name_cell);
    // 베타 칸은 [64, 124]에서 일부만 가렸다. 경계 오른쪽 조각은 베타 칸이 답한다.
    REQUIRE(tree.hit_test(110.0f, 38.0f) != nullptr);
    REQUIRE(tree.hit_test(110.0f, 38.0f)->id() == beta_cell);

    // 가려진 칸은 화면 밖으로 읽힌다. 일부만 가리면 교집합이다.
    REQUIRE(tree.visible_bounds(find(tree, alpha_cell)).has_value() == false);
    const std::optional<luil::rect_f> beta_visible { tree.visible_bounds(find(tree, beta_cell)) };
    REQUIRE(beta_visible.has_value());
    REQUIRE(beta_visible->x == 100.0f);
    REQUIRE(beta_visible->width == 24.0f);
    const std::optional<luil::rect_f> gamma_visible { tree.visible_bounds(find(tree, gamma_cell)) };
    REQUIRE(gamma_visible.has_value());
    REQUIRE(gamma_visible->x == find(tree, gamma_cell).bounds().x);
    REQUIRE(gamma_visible->width == find(tree, gamma_cell).bounds().width);

    // 머리글도 같다. 가려진 흐르는 칸의 손잡이는 잡히지 않고, 마지막 고정 열 손잡이는 흐르는 쪽 절반도 잡힌다.
    const auto header_cell = [](const std::u8string& key) { return luil::ui_element_id { luil::ui_element_kind::table_header_cell, part(key) }; };
    const auto grip = [](const std::u8string& key) { return luil::ui_element_id { luil::ui_element_kind::table_column_grip, part(key) }; };
    REQUIRE(tree.hit_test(30.0f, 14.0f)->id() == header_cell(u8"name"));
    REQUIRE(tree.hit_test(64.0f, 14.0f)->id() == header_cell(u8"name"));
    REQUIRE(tree.hit_test(97.0f, 14.0f)->id() == grip(u8"name"));
    REQUIRE(tree.hit_test(103.0f, 14.0f)->id() == grip(u8"name"));
    REQUIRE(tree.hit_test(110.0f, 14.0f)->id() == header_cell(u8"beta"));
    REQUIRE(tree.hit_test(124.0f, 14.0f)->id() == grip(u8"beta"));
    REQUIRE(luil::access_element_at(tree, 30.0f, 14.0f)->id() == header_cell(u8"name"));
    REQUIRE(luil::access_element_at(tree, 110.0f, 14.0f)->id() == header_cell(u8"beta"));
    REQUIRE(luil::access_element_at(tree, 103.0f, 14.0f)->id() == grip(u8"name"));

    // 레인은 구조다. DataGrid·Header·HeaderItem·DataItem의 역할과 이름, 머리글 칸의 순서는 고정이 없을 때와 같다.
    REQUIRE(find(tree, { luil::ui_element_kind::table_header_lane, part(u8"lane") }).accessibility().role == luil::access_role::none);
    REQUIRE(find(tree, { luil::ui_element_kind::table_row_lane, part(u8"row-a") }).accessibility().role == luil::access_role::none);
    REQUIRE(find(tree, { luil::ui_element_kind::virtual_list, u8"files" }).accessibility().role == luil::access_role::table);
    REQUIRE(find(tree, { luil::ui_element_kind::table_header, u8"files" }).accessibility().role == luil::access_role::header);
    REQUIRE(find(tree, luil::virtual_list_row_id(u8"files", u8"row-a")).accessibility().role == luil::access_role::data_item);

    luil::table_config plain_config { frozen_config(0u) };
    plain_config.scroll_x = 96.0f;
    const table_view plain { arrange_table(std::move(plain_config)) };
    REQUIRE(header_item_names(tree) == std::vector<std::u8string> { u8"이름", u8"알파", u8"베타", u8"감마" });
    REQUIRE(header_item_names(tree) == header_item_names(*plain.tree));
    const luil::ui_element& row { find(tree, luil::virtual_list_row_id(u8"files", u8"row-a")) };
    const luil::ui_element& plain_row { find(*plain.tree, luil::virtual_list_row_id(u8"files", u8"row-a")) };
    REQUIRE(tree.access_children_of(row).size() == 4u);
    for (std::size_t index = 0; index < 4u; ++index)
    {
        REQUIRE(tree.access_children_of(row)[index]->id() == plain.tree->access_children_of(plain_row)[index]->id());
        REQUIRE(tree.access_parent_of(tree.access_children_of(row)[index]->id()) == &row);
    }
}

TEST_CASE("Frozen columns follow the same pointer paths as any other column", "[ui][table][frozen]")
{
    luil::table_config config { frozen_config() };
    config.scroll_x = 40.0f;
    luil::interaction_controller controller {};
    controller.set_tree(published(config));

    // 가로 휠은 고정 칸 위에서 시작해도 표가 받는다.
    const std::shared_ptr<const luil::ui_tree> tree { published(config) };
    const luil::mouse_wheel_event sideways { .x = 30.0f, .y = 100.0f, .delta = 120.0f, .horizontal = true };
    const auto wheeled { luil::route_wheel(*tree, sideways, -(sideways.delta / 120.0f) * luil::input_wheel_scroll_step) };
    REQUIRE(message_at<scroll_x_intent>(wheeled, 0) != nullptr);

    // 고정 칸의 머리글을 눌러도 정렬을 청한다. 초점은 본문에 선다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, 14.0f, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { 50.0f, 14.0f, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<sort_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<sort_intent>(clicked, 0)->column == u8"name");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
}

TEST_CASE("Widening a frozen column stops where the flowing window keeps one key step", "[ui][table][frozen][columns]")
{
    // 이름 열 오른쪽 경계는 x = 100이다. 창 184에서 고정을 세운 채 넓힐 수 있는 한도는 184 - 48 - 100 = 36이다.
    luil::interaction_controller controller {};
    controller.set_tree(published(frozen_config()));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 14.0f, luil::pointer_button::left, at(0) }));
    const auto widened { controller.process(luil::pointer_moved_event { 150.0f, 14.0f, at(10) }) };
    REQUIRE(message_at<resize_intent>(widened, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(widened, 0)->column == u8"name");
    REQUIRE(message_at<resize_intent>(widened, 0)->delta == 36.0f);
    // 줄이는 몫은 최소 폭(60)까지다.
    const auto narrowed { controller.process(luil::pointer_moved_event { 0.0f, 14.0f, at(20) }) };
    REQUIRE(message_at<resize_intent>(narrowed, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(narrowed, 0)->delta == -40.0f);

    // 이미 한도(136)면 더 넓히는 끌기는 메시지가 없고, 줄이는 끌기는 나간다. 끝 알림은 끌기마다 한 번이다.
    luil::table_config at_limit { frozen_config() };
    at_limit.columns[0].width = 136.0f;
    controller.set_tree(published(std::move(at_limit)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 136.0f, 14.0f, luil::pointer_button::left, at(30) }));
    REQUIRE(controller.process(luil::pointer_moved_event { 170.0f, 14.0f, at(40) }).empty());
    const auto shrunk { controller.process(luil::pointer_moved_event { 100.0f, 14.0f, at(50) }) };
    REQUIRE(message_at<resize_intent>(shrunk, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(shrunk, 0)->delta == -70.0f);
    const auto finished { controller.process(luil::pointer_released_event { 100.0f, 14.0f, luil::pointer_button::left, at(60) }) };
    REQUIRE(message_at<resize_end_intent>(finished, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(finished, 0)->column == u8"name");
    REQUIRE(finished.size() == 1u);

    // 흐르는 열은 한도가 없다. 알파 열의 오른쪽 경계는 x = 160이다.
    controller.set_tree(published(frozen_config()));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 160.0f, 14.0f, luil::pointer_button::left, at(70) }));
    const auto flowing { controller.process(luil::pointer_moved_event { 260.0f, 14.0f, at(80) }) };
    REQUIRE(message_at<resize_intent>(flowing, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(flowing, 0)->column == u8"alpha");
    REQUIRE(message_at<resize_intent>(flowing, 0)->delta == 100.0f);
}

TEST_CASE("The cell cursor reveals into the flowing window and never scrolls for a frozen column", "[ui][table][frozen][cells]")
{
    // 알파에서 →: 베타(160~220)는 흐르는 창(100~184) 밖이라 끝을 맞춰 36 들인다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(frozen_nav_config(1u, u8"alpha", 0.0f)));
        focus_body(controller);
        const auto right { controller.process(press(luil::key_code::arrow_right, 10)) };
        REQUIRE(message_at<column_intent>(right, 0) != nullptr);
        REQUIRE(message_at<column_intent>(right, 0)->column == u8"beta");
        REQUIRE(message_at<scroll_x_intent>(right, 1) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(right, 1)->delta == 36.0f);
    }
    // 이름(고정)에서 →: 알파(100~160)는 이미 보인다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(frozen_nav_config(1u, u8"name", 0.0f)));
        focus_body(controller);
        const auto right { controller.process(press(luil::key_code::arrow_right, 10)) };
        REQUIRE(message_at<column_intent>(right, 0) != nullptr);
        REQUIRE(right.size() == 1u);
    }
    // 흘러간 뒤 알파에서 ←: 고정 열이 목표면 스크롤 메시지가 없다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(frozen_nav_config(1u, u8"alpha", 96.0f)));
        focus_body(controller);
        const auto left { controller.process(press(luil::key_code::arrow_left, 10)) };
        REQUIRE(message_at<column_intent>(left, 0) != nullptr);
        REQUIRE(message_at<column_intent>(left, 0)->column == u8"name");
        REQUIRE(left.size() == 1u);
    }
    // 흘러간 뒤 베타에서 ←: 알파가 고정 구역 아래에 있으니 흐르는 창의 시작에 맞춰 96 되돌린다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(frozen_nav_config(1u, u8"beta", 96.0f)));
        focus_body(controller);
        const auto left { controller.process(press(luil::key_code::arrow_left, 10)) };
        REQUIRE(message_at<column_intent>(left, 0) != nullptr);
        REQUIRE(message_at<column_intent>(left, 0)->column == u8"alpha");
        REQUIRE(message_at<scroll_x_intent>(left, 1) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(left, 1)->delta == -96.0f);
    }
    // F2도 같다: 흐르는 열의 편집은 들이고 고정 열의 편집은 들이지 않는다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(frozen_nav_config(1u, u8"alpha", 96.0f)));
        focus_body(controller);
        const auto started { controller.process(press(luil::key_code::f2, 10)) };
        REQUIRE(message_at<cell_intent>(started, 0) != nullptr);
        REQUIRE(message_at<cell_intent>(started, 0)->cell == luil::table_cell_ref { u8"row-b", u8"alpha" });
        REQUIRE(message_at<scroll_x_intent>(started, 1) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(started, 1)->delta == -96.0f);
    }
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(frozen_nav_config(1u, u8"name", 96.0f)));
        focus_body(controller);
        const auto started { controller.process(press(luil::key_code::f2, 10)) };
        REQUIRE(message_at<cell_intent>(started, 0) != nullptr);
        REQUIRE(started.size() == 1u);
    }
}

TEST_CASE("A header cell only accepts a drop from its own side of the frozen edge", "[ui][table][frozen][columns]")
{
    const auto drop_checks = [](const std::size_t frozen, const std::vector<std::pair<std::u8string, std::u8string>>& accepted, const std::vector<std::pair<std::u8string, std::u8string>>& refused) {
        luil::table_config config { frozen_config(frozen) };
        config.reorder_column = [](const std::u8string& moved, const std::u8string& target) { return luil::make_app_action(resize_intent { moved + u8">" + target, 0.0f }); };
        const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
        const auto accepts = [&tree](const std::u8string& moved, const std::u8string& target) {
            const luil::ui_element& source { find(*tree, { luil::ui_element_kind::table_header_cell, part(moved) }) };
            const luil::ui_element& destination { find(*tree, { luil::ui_element_kind::table_header_cell, part(target) }) };
            REQUIRE(source.drag() != nullptr);
            REQUIRE(destination.drop() != nullptr);
            return destination.drop()->accepts(source.drag()->make_payload({ source.id(), 10.0f, 10.0f }));
        };
        for (const auto& [moved, target] : accepted)
            REQUIRE(accepts(moved, target));
        for (const auto& [moved, target] : refused)
            REQUIRE(accepts(moved, target) == false);
    };
    // 앞 한 열 고정: 이름은 고정이고 나머지는 흐른다.
    drop_checks(1u, { { u8"alpha", u8"beta" }, { u8"gamma", u8"alpha" }, { u8"beta", u8"gamma" } }, { { u8"alpha", u8"name" }, { u8"name", u8"alpha" }, { u8"gamma", u8"name" } });
    // 앞 두 열 고정: 이름과 알파가 서로 놓을 수 있고 흐르는 열로는 놓지 못한다.
    drop_checks(2u, { { u8"name", u8"alpha" }, { u8"alpha", u8"name" }, { u8"beta", u8"gamma" } }, { { u8"beta", u8"alpha" }, { u8"name", u8"beta" }, { u8"alpha", u8"gamma" } });
    // 고정이 없으면 지금처럼 같은 표 안이면 어디든 받는다.
    drop_checks(0u, { { u8"alpha", u8"name" }, { u8"name", u8"gamma" } }, {});
}

TEST_CASE("Focus on a control inside a flowing cell reveals it in the flowing window only", "[ui][table][frozen][reveal]")
{
    const luil::ui_element_id table_id { luil::ui_element_kind::table, u8"files" };
    luil::scroll_route route {};
    route.id = table_id;
    route.scroll = [](const float delta) { return luil::make_app_action(scroll_x_intent { delta }); };
    const std::vector<luil::scroll_route> routes { route };
    const luil::ui_element_id body { luil::ui_element_kind::virtual_list, u8"files" };
    const luil::ui_element_id header { luil::ui_element_kind::table_header, u8"files" };
    const luil::ui_element_id row { luil::virtual_list_row_id(u8"files", u8"row-a") };
    const luil::ui_element_id name_cell { kind_cell, u8"row-a/name" };
    const luil::ui_element_id alpha_cell { kind_cell, u8"row-a/alpha" };
    const luil::ui_element_id beta_cell { kind_cell, u8"row-a/beta" };
    const luil::ui_element_id gamma_cell { kind_cell, u8"row-a/gamma" };

    // 고정이 서 있고 흘러가지 않은 표: 오른쪽 밖의 감마는 흐르는 창(100~184)에 들인다.
    {
        const table_view view { arrange_table(frozen_config()) };
        const auto revealed { luil::route_reveal(*view.tree, gamma_cell, routes) };
        REQUIRE(message_at<scroll_x_intent>(revealed, 0) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(revealed, 0)->delta == 96.0f);
        REQUIRE(luil::route_reveal(*view.tree, alpha_cell, routes).empty());
        REQUIRE(luil::route_reveal(*view.tree, name_cell, routes).empty());
    }
    // 흘러간 표: 본문·행·머리글·고정 칸·고정 구역에 걸친 칸·이미 보이는 칸은 모두 빈 목록이다.
    // 본문의 왼쪽 끝을 경계에 맞춰 답하면 -고정 폭이 나와 Tab마다 표가 되돌아간다.
    {
        luil::table_config config { frozen_config() };
        config.scroll_x = 40.0f;
        const table_view view { arrange_table(std::move(config)) };
        REQUIRE(view.table->frozen_columns() == 1u);
        for (const luil::ui_element_id& target : { body, header, row, name_cell, alpha_cell, beta_cell })
            REQUIRE(luil::route_reveal(*view.tree, target, routes).empty());
        REQUIRE(view.table->scroll_delta_to_reveal(find(*view.tree, body).bounds()) == 0.0f);
        const auto revealed { luil::route_reveal(*view.tree, gamma_cell, routes) };
        REQUIRE(message_at<scroll_x_intent>(revealed, 0) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(revealed, 0)->delta == 56.0f);
    }
    // 고정이 없는 표(결정 8): 오른쪽 밖에 숨은 칸은 이제 흐른다. 변화량은 고정 없는 표의 들이기와 같고 본문은 빈 목록이다.
    {
        luil::table_config config { frozen_config(0u) };
        config.scroll_x = 40.0f;
        const table_view view { arrange_table(std::move(config)) };
        for (const luil::ui_element_id& target : { body, header, row, name_cell, alpha_cell, beta_cell })
            REQUIRE(luil::route_reveal(*view.tree, target, routes).empty());
        const auto revealed { luil::route_reveal(*view.tree, gamma_cell, routes) };
        REQUIRE(message_at<scroll_x_intent>(revealed, 0) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(revealed, 0)->delta == luil::table_reveal_delta({ 220.0f, 60.0f }, 40.0f, 0.0f, 184.0f));
        REQUIRE(luil::table_reveal_delta({ 220.0f, 60.0f }, 40.0f, 0.0f, 184.0f) == 56.0f);
    }
    // 배율이 있어도 같은 논리 변화량이다.
    {
        luil::table_config config { frozen_config() };
        const table_view view { arrange_table(std::move(config), 200.0f, 1.5f) };
        const auto revealed { luil::route_reveal(*view.tree, gamma_cell, routes) };
        REQUIRE(message_at<scroll_x_intent>(revealed, 0) != nullptr);
        REQUIRE(message_at<scroll_x_intent>(revealed, 0)->delta == 96.0f);
    }
}

TEST_CASE("The frozen zone shows no flowing cell and the edge line stands", "[ui][table][frozen][raster]")
{
    luil::table_config config { frozen_config(1u, true) };
    config.scroll_x = 96.0f;
    const table_view view { arrange_table(std::move(config), 200.0f, 2.0f) };
    REQUIRE(view.tree->unarranged().empty());
    luil::testing::raster_frame frame { 400, 600, luil::color_palette_for(luil::color_theme::dark), 2.0f };
    frame.draw(*view.tree, {});

    // 본문은 머리글(56) 아래 가로 막대(32) 위다. 고정 구역은 물리 [0, 200)이고 경계선은 [198, 200)이다.
    const luil::rect_f fixed_zone { 0.0f, 56.0f, 198.0f, 512.0f };
    const luil::rect_f flowing_zone { 200.0f, 56.0f, 168.0f, 512.0f };
    REQUIRE(frame.count_color(fixed_zone, flowing_color) == 0);
    REQUIRE(frame.count_color(fixed_zone, fixed_color) > 0);
    REQUIRE(frame.count_color(flowing_zone, flowing_color) > 0);
    REQUIRE(frame.count_color(flowing_zone, fixed_color) == 0);
    // 경계선은 마지막 고정 칸의 오른쪽 끝 한 획(물리 2픽셀)이고 본문 높이만큼 이어진다.
    const luil::rect_f line { 198.0f, 56.0f, 2.0f, 512.0f };
    const luil::ui_color drawn_line { over(frame.palette().divider, fixed_color) };
    int line_pixels { 0 };
    for (int y = 56; y < 568; ++y)
        for (int x = 198; x < 200; ++x)
            if (near_color(frame.pixel_at(x, y), drawn_line))
                ++line_pixels;
    REQUIRE(line_pixels == 2 * 512);
    REQUIRE(frame.count_color(line, fixed_color) == 0);

    // 고정이 없으면 같은 자리에 선이 없다.
    luil::table_config plain_config { frozen_config(0u, true) };
    plain_config.scroll_x = 96.0f;
    const table_view plain { arrange_table(std::move(plain_config), 200.0f, 2.0f) };
    luil::testing::raster_frame plain_frame { 400, 600, luil::color_palette_for(luil::color_theme::dark), 2.0f };
    plain_frame.draw(*plain.tree, {});
    REQUIRE(plain_frame.count_color(line, flowing_color) == 2 * 512);
    // 고정 없는 표에서는 흐르는 칸이 같은 자리에 비친다 (레인이 자르는 것이다).
    REQUIRE(plain_frame.count_color(fixed_zone, flowing_color) > 0);
}

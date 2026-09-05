#include "demo/theme_page.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/wrap_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include "luil/ui/draw_primitives.h"

#include <optional>
#include <utility>
#include <vector>

namespace demo {
    namespace {
        // 키 컬러 하나를 고르는 색 동그라미다.
        // 대표색은 테마와 무관한 값이라 팔레트가 아니라 카탈로그에서 온다.
        class swatch_element final : public luil::ui_element
        {
        public:
            swatch_element(const luil::ui_element_id id, const luil::ui_color color, const bool selected) noexcept
                : ui_element { id }
                , color_ { color }
                , selected_ { selected }
            {}

            void arrange(const luil::arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const luil::rect_f box { bounds() };
                const float radius { box.width * 0.5f };
                const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), luil::solid_paint(color_));

                // 고른 것과 포인터가 올라간 것을 테두리 굵기로 구분한다.
                if (selected_ == false && interaction.hovered != id())
                    return;
                SkPaint ring { luil::solid_paint(context.palette.primary_foreground) };
                ring.setStyle(SkPaint::kStroke_Style);
                ring.setStrokeWidth((selected_ ? 2.0f : 1.0f) * scale);
                const float outset { 3.0f * scale };
                const SkRect outline { SkRect::MakeXYWH(box.x - outset, box.y - outset, box.width + 2.0f * outset, box.height + 2.0f * outset) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(outline, radius + outset, radius + outset), ring);
            }

        private:
            luil::ui_color color_ { 0 };
            bool selected_ { false };
        };
    } // namespace

    bool theme_page::handle(const luil::app_message& message)
    {
        if (const auto* const theme { message.get<theme_intent>() }; theme != nullptr)
        {
            appearance_.theme = theme->theme;
            return true;
        }
        if (const auto* const accent { message.get<accent_intent>() }; accent != nullptr)
        {
            appearance_.accent_id = accent->id;
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> theme_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(height);
        static_cast<void>(scale);

        // 테마 선호는 배타 선택이라 토글 묶음으로 그린다.
        luil::choice_group_config themes {};
        themes.owner = u8"theme";
        themes.style = luil::choice_style::toggle;
        const auto add_theme = [&themes](const luil::theme_preference value, std::u8string label) {
            themes.items.push_back({ std::u8string { luil::theme_preference_name(value) }, std::move(label) });
        };
        add_theme(luil::theme_preference::system, u8"시스템");
        add_theme(luil::theme_preference::light, u8"밝게");
        add_theme(luil::theme_preference::dark, u8"어둡게");
        themes.selected = std::u8string { luil::theme_preference_name(appearance_.theme) };
        themes.select = [](const std::u8string& value) {
            for (const luil::theme_preference preference : { luil::theme_preference::system, luil::theme_preference::light, luil::theme_preference::dark })
                if (value == luil::theme_preference_name(preference))
                    return luil::make_app_action(theme_intent { preference });
            return luil::input_action {};
        };
        const float themes_height { luil::choice_group_element::height_for(themes) };

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"theme" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"theme-hint" }, u8"테마 선호", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(6.0f);
        column->add(std::make_unique<luil::choice_group_element>(std::move(themes)), { .length = themes_height, .cross_length = 240.0f });
        column->add_gap(20.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"accent-hint" }, u8"키 컬러", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(8.0f);

        // 실행 시점 항목(시스템)이 먼저, 그 뒤가 정적 카탈로그다 —
        // 위의 테마 선호에서 "시스템"이 앞서는 것과 같은 자리다.
        // OS accent를 읽지 못한 실행에서는 항목이 서지 않고 지금까지의 화면 그대로다.
        std::vector<luil::accent_definition> accents {};
        if (const std::optional<luil::accent_definition> system { luil::system_accent() }; system.has_value())
            accents.push_back(*system);
        for (const luil::accent_definition& accent : luil::accent_catalog())
            accents.push_back(accent);

        // 카탈로그는 빌드 시점에 JSON에서 만들어진 표다.
        // 한 줄에 안 들어가면 다음 줄로 넘긴다 — 전에는 들어가는 만큼만 담고
        // 나머지 색을 **버렸다.**
        luil::wrap_config swatches {};
        swatches.item_width = 16.0f;
        swatches.item_height = 16.0f;
        swatches.spacing = 6.0f;
        swatches.line_spacing = 6.0f;
        auto grid { std::make_unique<luil::wrap_element>(luil::ui_element_id { kind_layout, u8"accents" }, swatches) };
        for (const luil::accent_definition& accent : accents)
        {
            std::u8string id { accent.id };
            auto swatch { std::make_unique<swatch_element>(luil::ui_element_id { kind_accent, id }, accent.swatch, appearance_.accent_id == accent.id) };
            swatch->set_tooltip(std::u8string { accent.label });
            swatch->set_cursor(luil::ui_cursor::hand);
            swatch->set_action(
                luil::ui_trigger::left_click, [id](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(accent_intent { id }) }; });
            grid->add(std::move(swatch));
        }
        // 줄 수는 폭에서 나오는 산수라 tree를 짓기 전에 높이를 알 수 있다.
        // 여기 쓰는 폭은 이 column의 좌우 여백을 뺀 값이라 wrap이 받을 slot과 같다.
        column->add(std::move(grid), luil::wrap_height_for(swatches, width - 48.0f, accents.size()));
        return column;
    }
} // namespace demo

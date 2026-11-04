#include "mobile_demo/theme_page.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/theme_picker_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/wrap_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include "luil/ui/draw_primitives.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace mobile_demo {
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
            {
            }

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
    }

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

    page_content theme_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);

        // 테마 선호는 라이브러리의 테마 카드로 그린다. 글을 주지 않아 기기 페이지에서 고른 언어의 기본 문구를 따른다.
        const float themes_width { width - 2.0f * page_padding };
        luil::theme_picker_config themes {};
        themes.owner = u8"theme";
        themes.selected = appearance_.theme;
        themes.accent_id = appearance_.accent_id;
        // 폰 폭에서도 세 장이 한 줄에 들게 줄인다. 다음 줄로 넘기면 왼쪽에 두 장만 서서 화면을 많이 먹는다.
        themes.card_width = std::min(themes.card_width, (themes_width - 2.0f * themes.gap) / 3.0f);
        themes.select = [](const luil::theme_preference value) { return luil::make_app_action(theme_intent { value }); };
        const float themes_height { luil::theme_picker_element::height_for(themes, themes_width) };

        page_column column { u8"theme" };
        column.note(u8"theme-hint", u8"테마 선호");
        column.gap(6.0f);
        column.add(std::make_unique<luil::theme_picker_element>(std::move(themes)), themes_height, themes_width);
        column.gap(8.0f);
        column.note(u8"theme-bars", u8"상태 표시줄 아이콘도 바탕을 따른다.");
        column.gap(20.0f);

        // 실행 시점 항목(시스템)이 먼저, 그 뒤가 정적 카탈로그다. Android 12부터는 배경화면에서 뽑은
        // 동적 색이 "시스템" 항목이 된다. 읽지 못한 기기에서는 항목이 서지 않는다.
        std::vector<luil::accent_definition> accents {};
        if (const std::optional<luil::accent_definition> system { luil::system_accent() }; system.has_value())
            accents.push_back(*system);
        for (const luil::accent_definition& accent : luil::accent_catalog())
            accents.push_back(accent);

        // 고른 색의 이름이다. 견본의 이름은 데스크톱에서 tooltip으로 보이지만 손가락에는 hover가 없다.
        std::u8string chosen { u8"키 컬러" };
        for (const luil::accent_definition& accent : accents)
            if (accent.id == appearance_.accent_id)
                chosen = u8"키 컬러 — " + std::u8string { accent.label };
        column.note(u8"accent-hint", std::move(chosen));
        column.gap(8.0f);

        // 손가락으로 누를 견본이라 데스크톱(16)보다 크게 둔다. 한 줄에 안 들어가면 다음 줄로 넘긴다.
        luil::wrap_config swatches {};
        swatches.item_width = 40.0f;
        swatches.item_height = 40.0f;
        swatches.spacing = 10.0f;
        swatches.line_spacing = 10.0f;
        auto grid { std::make_unique<luil::wrap_element>(luil::ui_element_id { kind_layout, u8"accents" }, swatches) };
        for (const luil::accent_definition& accent : accents)
        {
            std::u8string id { accent.id };
            auto swatch { std::make_unique<swatch_element>(luil::ui_element_id { kind_accent, id }, accent.swatch, appearance_.accent_id == accent.id) };
            swatch->set_tooltip(std::u8string { accent.label });
            swatch->set_cursor(luil::ui_cursor::hand);
            swatch->set_action(luil::ui_trigger::left_click, [id](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(accent_intent { id }) }; });
            grid->add(std::move(swatch));
        }
        // 줄 수는 폭에서 나오는 산수라 tree를 짓기 전에 높이를 알 수 있다.
        column.add(std::move(grid), luil::wrap_height_for(swatches, width - 2.0f * page_padding, accents.size()));
        return column.finish();
    }
}

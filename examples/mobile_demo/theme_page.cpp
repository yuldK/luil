#include "mobile_demo/theme_page.h"

#include "demo/line_break_sample.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/label_element.h"
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

    theme_page::theme_page(std::u8string style_source, std::u8string markup_source)
        : style_sheet_ { std::move(style_source) }
        , markup_ { std::move(markup_source) }
    {
    }

    bool theme_page::handle(const luil::app_message& message)
    {
        if (const auto* const sheet { message.get<demo::style_sheet_intent>() }; sheet != nullptr)
        {
            // 견본은 곧바로 읽는다. 막히면 쥔 스타일이 그대로라 화면은 이전 스타일로 남는다.
            if (sheet->action == demo::style_sheet_action::reset)
                style_sheet_.reset();
            else
                style_sheet_.apply(demo::style_sheet_sample(style_sheet_.source(), sheet->action));
            return true;
        }
        if (markup_.handle(message))
            return true;
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
        // 테마 선호는 배타 선택이라 선택 묶음으로 그린다. 폰 화면 폭에 채운다.
        luil::choice_group_config themes {};
        themes.owner = u8"theme";
        themes.style = luil::choice_style::toggle;
        themes.width = luil::choice_width::fill;
        const auto add_theme
            = [&themes](const luil::theme_preference value, std::u8string label) { themes.items.push_back({ std::u8string { luil::theme_preference_name(value) }, std::move(label) }); };
        add_theme(luil::theme_preference::system, u8"시스템");
        add_theme(luil::theme_preference::light, u8"밝게");
        add_theme(luil::theme_preference::dark, u8"어둡게");
        themes.selected = std::u8string { luil::theme_preference_name(appearance_.theme) };
        themes.name = u8"테마";
        themes.select = [](const std::u8string& value) {
            for (const luil::theme_preference preference : { luil::theme_preference::system, luil::theme_preference::light, luil::theme_preference::dark })
                if (value == luil::theme_preference_name(preference))
                    return luil::make_app_action(theme_intent { preference });
            return luil::input_action {};
        };
        const float themes_height { luil::choice_group_element::height_for(themes) };

        page_column column { u8"theme" };
        column.note(u8"theme-hint", u8"테마 선호");
        column.gap(6.0f);
        column.add(std::make_unique<luil::choice_group_element>(std::move(themes)), themes_height, std::min(width - 2.0f * page_padding, 320.0f));
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
        column.gap(20.0f);

        // 스타일 파일이다. 견본을 실행 중에 읽어, 받으면 셸이 frame과 본문 배치에 같은 포인터로 싣고 막히면 이전 스타일이 남는다.
        column.note(u8"theme-style-hint", u8"스타일 파일 (견본을 실행 중에 읽는다)");
        column.gap(8.0f);
        struct style_button
        {
            std::u8string_view owner {};
            std::u8string_view text {};
            demo::style_sheet_action action { demo::style_sheet_action::reset };
        };
        constexpr style_button buttons[] {
            { u8"theme-style-sample-background", u8"견본: 창 바탕 바꿈", demo::style_sheet_action::sample_background },
            { u8"theme-style-sample-unknown", u8"견본: 낯선 키", demo::style_sheet_action::sample_unknown_key },
            { u8"theme-style-sample-broken", u8"견본: 괄호 없음", demo::style_sheet_action::sample_broken },
            { u8"theme-style-sample-wrap", u8"견본: 줄 나눔 unicode", demo::style_sheet_action::sample_wrap },
            { u8"theme-style-reset", u8"내장값으로", demo::style_sheet_action::reset },
        };
        // 손가락으로 누를 단추라 높이를 넉넉히 둔다.
        constexpr float style_button_height { 44.0f };
        const float button_width { std::min(width - 2.0f * page_padding, 320.0f) };
        for (const style_button& button : buttons)
        {
            const luil::ui_element_id id { kind_style_sheet_button, std::u8string { button.owner } };
            auto element { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .text = std::u8string { button.text } }) };
            element->set_action(luil::ui_trigger::left_click, luil::make_message_action(demo::style_sheet_intent { button.action }));
            column.add(std::move(element), style_button_height, button_width);
            column.gap(8.0f);
        }
        // 결과 줄이다. 거부의 오류 글이 길어 폭에서 줄을 바꾼다.
        luil::label_config status {};
        status.text = style_sheet_.status();
        status.font_size = note_text_size;
        status.color = luil::label_color_role::dim;
        status.wrap = true;
        status.max_lines = 3;
        column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"theme-style-status" }, std::move(status)),
            3.0f * luil::text_line_height(note_text_size, note_line_height - note_text_size));
        column.gap(12.0f);

        // 정책 칸이 빈 여러 줄 label이다. 본문 배치가 지금 스타일을 실어 그 정책으로 접고, 상태 줄은 같은 스타일로 잰 줄 수와
        // 첫 줄 끝을 말한다 (docs/wrap-policy-key-design.md의 확인). 견본 글과 폭은 기본 페이지의 줄 나눔 칸과 같다.
        const luil::ui_style* const style { style_sheet_.style().get() };
        luil::label_config wrap_label { demo::style_wrap_label_config() };
        const float wrap_height { demo::measure_style_wrap(wrap_label, style, scale).height };
        luil::label_config wrap_status {};
        wrap_status.text = demo::measure_style_wrap_status(wrap_label, style, scale);
        wrap_status.font_size = note_text_size;
        wrap_status.color = luil::label_color_role::dim;
        // 첫 줄 끝 글이 붙어 폰 폭을 넘을 수 있어 폭에서 줄을 바꾼다.
        wrap_status.wrap = true;
        wrap_status.max_lines = 2;
        column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"theme-wrap-label" }, std::move(wrap_label)), wrap_height, demo::line_break_cell_width);
        column.gap(6.0f);
        column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"theme-wrap-status" }, std::move(wrap_status)),
            2.0f * luil::text_line_height(note_text_size, note_line_height - note_text_size));
        column.gap(20.0f);

        // 화면 마크업이다. 견본 글을 실행 중에 읽어 frame마다 이 페이지의 상태로 펼친다. 읽기가 막히면 쥔 틀이 남는다.
        //  - 모바일에는 글 칸이 없어 slot에 C++가 지은 label을 담는다. 견본 단추만 있다 (markup-design.md 확인 절).
        column.note(u8"theme-markup-hint", u8"마크업 (견본을 실행 중에 읽는다)");
        column.gap(8.0f);
        const std::vector<luil::inspector_kind_name> kinds { { kind_layout, u8"layout" }, { kind_text, u8"text" } };
        const auto make_slot = [](const std::u8string_view name, const luil::ui_markup_scope&) -> std::unique_ptr<luil::ui_element> {
            if (name != u8"search")
                return nullptr;
            luil::label_config slot {};
            slot.text = u8"slot: C++가 지은 칸";
            slot.font_size = note_text_size;
            slot.color = luil::label_color_role::dim;
            return std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"markup-search" }, std::move(slot));
        };
        demo::markup_sample_state::built_branch branch { markup_.build(kinds, make_slot) };
        if (branch.element != nullptr)
        {
            // 가지의 높이는 펼친 뿌리에게 묻는다. 반복 단추 줄이 나고 사라지면 페이지도 따라 줄어든다.
            const luil::measure_context measure { .scale = scale, .style = style };
            const std::optional<luil::size_f> size { branch.element->preferred_size(measure, width - 2.0f * page_padding) };
            column.add(std::move(branch.element), size.has_value() ? size->height : 0.0f);
            column.gap(12.0f);
        }
        struct markup_button
        {
            std::u8string_view owner {};
            std::u8string_view text {};
            demo::markup_sample_action action { demo::markup_sample_action::reload };
        };
        constexpr markup_button markup_buttons[] {
            { u8"markup-reload", u8"다시 읽기", demo::markup_sample_action::reload },
            { u8"markup-sample-unknown", u8"견본: 모르는 속성", demo::markup_sample_action::sample_unknown_attribute },
            { u8"markup-sample-broken", u8"견본: 닫지 않은 태그", demo::markup_sample_action::sample_broken },
            { u8"markup-sample-value", u8"견본: 없는 값", demo::markup_sample_action::sample_missing_value },
        };
        for (const markup_button& button : markup_buttons)
        {
            const luil::ui_element_id id { kind_markup_button, std::u8string { button.owner } };
            auto element { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .text = std::u8string { button.text } }) };
            element->set_action(luil::ui_trigger::left_click, luil::make_message_action(demo::markup_sample_intent { button.action }));
            column.add(std::move(element), style_button_height, button_width);
            column.gap(8.0f);
        }
        // 결과 줄이다. 거부의 오류 글이 길어 폭에서 줄을 바꾼다.
        luil::label_config markup_status {};
        markup_status.text = std::move(branch.status);
        markup_status.font_size = note_text_size;
        markup_status.color = luil::label_color_role::dim;
        markup_status.wrap = true;
        markup_status.max_lines = 3;
        column.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"markup-status" }, std::move(markup_status)),
            3.0f * luil::text_line_height(note_text_size, note_line_height - note_text_size));
        return column.finish();
    }
}

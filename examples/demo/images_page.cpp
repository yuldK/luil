#include "demo/images_page.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/image_decode.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkDashPathEffect.h"

#include "luil/generated/codicons.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace demo {
    namespace {
        // 두 칸은 클라이언트 영역에 **비례한다** — 창을 키우면 함께 큰다.
        // 비례만 두면 좁은 창에서 아무것도 알아볼 수 없고 아주 넓은 창에서는
        // 드롭 영역만 커지므로 위아래를 함께 묶는다 (stack의 `minimum`이 유연
        // 항목에 하는 일과 같은 뜻이다 — 어느 쪽이 나은지는 앱이 정한다).
        constexpr float drop_column_ratio { 0.34f };
        constexpr float drop_column_minimum_width { 220.0f };
        constexpr float drop_column_maximum_width { 380.0f };

        // 디코드 시점에 그림을 줄일 상한이다 (그림 자체의 픽셀).
        //
        // 미리 보기 칸이 커질 수 있는 만큼 넉넉히 잡은 상수 하나다. 칸 크기에
        // 맞춰 매번 다시 읽는 것이 더 정확하지만, 그러면 창을 끄는 동안 디스크와
        // 디코딩이 계속 돈다 — 이 데모가 보일 것이 아니다.
        constexpr int preview_pixel_limit { 1600 };

        [[nodiscard]] float clamp_width(const float value) noexcept
        {
            if (value < drop_column_minimum_width)
                return drop_column_minimum_width;
            if (value > drop_column_maximum_width)
                return drop_column_maximum_width;
            return value;
        }

        [[nodiscard]] std::u8string file_name_of(const std::u8string_view path)
        {
            const std::size_t separator { path.find_last_of(u8"/\\") };
            return std::u8string { separator == std::u8string_view::npos ? path : path.substr(separator + 1) };
        }

        [[nodiscard]] std::u8string extension_of(const std::u8string_view path)
        {
            const std::size_t dot { path.find_last_of(u8'.') };
            if (dot == std::u8string_view::npos)
                return {};
            // 디렉터리 이름의 점은 확장자가 아니다.
            const std::size_t separator { path.find_last_of(u8"/\\") };
            if (separator != std::u8string_view::npos && dot < separator)
                return {};

            std::u8string extension {};
            for (const char8_t character : path.substr(dot + 1))
                extension.push_back(character >= u8'A' && character <= u8'Z' ? static_cast<char8_t>(character - u8'A' + u8'a') : character);
            return extension;
        }

        // 확장자 목록의 한 구간을 한 줄로 잇는다.
        // 여럿을 한 줄에 넣으면 좁은 칸에서 잘려 안내가 거짓이 되므로 두 줄로 나눈다 —
        // 나누는 것은 **표시**이고 목록 자체는 여전히 한 벌이다.
        [[nodiscard]] std::u8string join_extensions(const std::size_t begin, const std::size_t end)
        {
            std::u8string joined {};
            for (std::size_t index = begin; index < end && index < image_extensions.size(); ++index)
            {
                if (joined.empty() == false)
                    joined += u8" · ";
                joined += image_extensions[index];
            }
            return joined;
        }

        // 칸 가운데에 한 줄을 그린다.
        // 좁은 칸에서 잘린 글은 **자른 폭으로** 가운데 둔다 — 원문 폭으로 재면
        // 잘린 글이 왼쪽으로 밀린다.
        void draw_centered_line(luil::draw_context& context, const std::u8string_view text, const luil::rect_f& box, const float font_size, const SkPaint& paint)
        {
            if (text.empty() || box.width <= 0.0f)
                return;
            const SkFont font { sk_ref_sp(context.ui_typeface), font_size };
            const std::u8string fitted { luil::elide_text(text, box.width, font) };
            const float width { luil::measure_text(fitted, font) };
            luil::draw_text(context.canvas, fitted, box.x + (box.width - width) * 0.5f, box.y + luil::centered_text_baseline(font, box.height), font, paint);
        }

        // 파일을 받는 자리다.
        //
        // 대시 테두리는 "여기는 놓는 자리"라는 관례라 이 element가 직접 그린다.
        // 라이브러리로 올리지 않은 이유는 소비자가 이 데모 하나여서다 — 잊으면
        // 조용히 틀리는 것이 없고(수락 판정·강조 대상은 이미 라이브러리가 답한다)
        // 남는 것은 그림 하나뿐이다.
        class drop_zone_element final : public luil::ui_element
        {
        public:
            drop_zone_element(luil::ui_element_id id, std::u8string title, std::u8string hint)
                : ui_element { std::move(id) }
                , title_ { std::move(title) }
                , hint_ { std::move(hint) }
            {}

            void arrange(const luil::arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const luil::rect_f box { bounds() };
                // 지금 이 자리 위에 놓을 수 있는가.
                // 판정은 라이브러리가 이미 했다 — 수락한 대상의 id가 실려 온다.
                const bool receiving { interaction.drag.has_value() && interaction.drag->hovered_drop_target == id() };
                const float radius { 6.0f * scale };
                const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

                // 받는 동안의 바탕은 강조색이고, 평소의 hover·눌림은 공용 도우미가
                // 그린다 — 앱 element도 라이브러리와 같은 겉모습을 쓴다.
                if (receiving)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), luil::solid_paint(luil::with_alpha(context.palette.accent_soft, 0.25f)));
                else
                    luil::draw_hover_fill(context, box, id(), interaction, enabled(), 6.0f);

                SkPaint border { luil::solid_paint(receiving ? context.palette.accent : context.palette.input_border) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth((receiving ? 2.0f : 1.0f) * scale);
                const SkScalar intervals[2] { 6.0f * scale, 4.0f * scale };
                border.setPathEffect(SkDashPathEffect::Make({ intervals, 2 }, 0.0f));
                // 획은 경로 위에 절반씩 걸치므로 그만큼 안으로 들인다.
                // 그러지 않으면 테가 칸 밖으로 반 걸쳐 옆 자리를 침범한다.
                const float inset { border.getStrokeWidth() * 0.5f };
                context.canvas.drawRRect(SkRRect::MakeRectXY(shape.makeInset(inset, inset), radius, radius), border);

                // 글리프 한 줄 + 글 두 줄을 칸 가운데에 쌓는다.
                const float glyph_height { 30.0f * scale };
                const float title_height { 20.0f * scale };
                const float hint_height { 18.0f * scale };
                const float spacing { 6.0f * scale };
                const float total { glyph_height + spacing + title_height + hint_height };
                float top { box.y + (box.height - total) * 0.5f };
                if (top < box.y)
                    top = box.y;

                const SkPaint accent_text { luil::solid_paint(receiving ? context.palette.accent : context.palette.secondary_foreground) };
                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 24.0f * scale };
                luil::draw_centered_glyph(context.canvas, receiving ? luil::codicons::icon_cloud_upload : luil::codicons::icon_file_media,
                    { box.x, top, box.width, glyph_height }, glyph_font, accent_text);
                top += glyph_height + spacing;

                const float padding { 12.0f * scale };
                const float text_x { box.x + padding };
                const float text_width { box.width - padding * 2.0f };
                draw_centered_line(context, title_, { text_x, top, text_width, title_height }, 12.0f * scale, luil::solid_paint(context.palette.primary_foreground));
                top += title_height;
                draw_centered_line(context, hint_, { text_x, top, text_width, hint_height }, 11.0f * scale, accent_text);
            }

        private:
            std::u8string title_ {};
            std::u8string hint_ {};
        };
    } // namespace

    bool is_supported_image(const std::u8string_view path)
    {
        const std::u8string extension { extension_of(path) };
        for (const std::u8string_view supported : image_extensions)
            if (extension == supported)
                return true;
        return false;
    }

    bool images_page::handle(const luil::app_message& message)
    {
        const auto* const open { message.get<image_open_intent>() };
        if (open == nullptr)
            return false;

        // 디코딩은 logic thread의 몫이다 — UI thread에서 부르면 그만큼 창이 멈춘다
        // (image_decode.h의 thread 규칙). 만들어진 그림은 불변이라 UI thread가
        // 그려도 새 동기화가 없다.
        picture_name_ = file_name_of(open->path);
        picture_error_.clear();
        animation_ = luil::load_animated_image_file(open->path, { .max_width = preview_pixel_limit, .max_height = preview_pixel_limit }, picture_error_);
        playback_ = animation_.valid() ? luil::image_playback { .started = std::chrono::steady_clock::now() } : luil::image_playback {};
        return true;
    }

    std::unique_ptr<luil::ui_element> images_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(height);
        static_cast<void>(scale);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 24.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"images-row" }, row_config) };
        row->add(make_drop_column(width), clamp_width(width * drop_column_ratio));
        // 미리 보기는 남는 자리를 다 갖는다 — 가로도 세로도 창을 따라 큰다.
        row->add_flexible(make_preview());

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"images" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"images-hint" }, u8"그림 파일을 왼쪽 영역에 끌어다 놓거나 눌러서 고르면 오른쪽에 미리 보기가 선다.", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(12.0f);
        column->add_flexible(std::move(row));
        column->add_gap(10.0f);
        column->add(make_status(), 16.0f);
        return column;
    }

    std::unique_ptr<luil::ui_element> images_page::make_drop_column(const float width) const
    {
        static_cast<void>(width);

        auto zone { std::make_unique<drop_zone_element>(luil::ui_element_id { kind_image_drop }, u8"여기에 그림을 끌어다 놓는다", u8"또는 눌러서 파일을 고른다") };
        zone->set_tooltip(u8"그림 파일을 고른다");
        zone->set_cursor(luil::ui_cursor::hand);
        // 파일 dialog는 **창을 가진 thread**의 것이라 앱 메시지가 아니라
        // `app_ui_command`로 낸다. 라이브러리는 이 번호를 해석하지 않고 셸의
        // delegate에 그대로 넘긴다 (ui_events.h).
        zone->set_action(luil::ui_trigger::left_click,
            [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::app_ui_command { app_command_open_image } }; });

        // 밖에서 온 끌기 중 **읽을 수 있는 것만** 받는다.
        // 거절한 파일은 강조도 뜨지 않고 셸의 물러섬(토스트)이 받는다 —
        // 목록 재정렬의 `accepts`가 파일 끌기를 저절로 거절하는 것과 같은 자리다.
        luil::drop_target drop {};
        drop.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false && is_supported_image(payload.files.front()); };
        drop.on_drop = [](const luil::drag_payload& payload, const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return { luil::make_app_action(image_open_intent { payload.files.front() }) };
        };
        zone->set_drop_target(std::move(drop));

        luil::stack_config config {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"images-drop" }, config) };
        // 영역이 남는 높이를 다 갖고, 안내는 그 아래에 고정 두 줄이다.
        column->add_flexible(std::move(zone));
        column->add_gap(8.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"images-extensions-1" }, u8"지원 형식: " + join_extensions(0, 4), 11.0f, luil::label_color_role::dim), 16.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"images-extensions-2" }, join_extensions(4, image_extensions.size()), 11.0f, luil::label_color_role::dim), 16.0f);
        return column;
    }

    std::unique_ptr<luil::ui_element> images_page::make_preview() const
    {
        luil::panel_config config {};
        config.corner_radius = 6.0f;
        config.background = [](const luil::ui_color_palette& palette) { return palette.input_background; };
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_image_preview, u8"panel" }, config) };

        luil::stack_config inner {};
        inner.padding = luil::edge_insets::all(12.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"images-preview" }, inner) };
        // 비율을 지켜 칸 안에 전부 들어온다 (contain). 칸이 창을 따라 커지므로
        // 미리 보기 크기도 클라이언트 영역에 비례한다.
        //  - 아직 고른 그림이 없으면 빈 이미지라 아무것도 그리지 않는다. 자리
        //    표시를 그리면 "그림이 있다"는 거짓말이 되고, 무엇을 해야 하는지는
        //    드롭 영역과 상태 줄이 이미 말한다.
        column->add_flexible(std::make_unique<luil::image_element>(luil::ui_element_id { kind_image_preview, u8"image" },
            luil::image_config { .fit = luil::image_fit::contain, .description = picture_name_, .animation = animation_, .playback = playback_ }));
        panel->set_content(std::move(column));
        return panel;
    }

    std::unique_ptr<luil::label_element> images_page::make_status() const
    {
        const luil::ui_element_id id { kind_text, u8"images-status" };
        // 읽지 못한 파일은 조용히 비지 않는다 — 이유가 화면에 남는다.
        // 라이브러리가 내는 글은 진단용 영문이고, 사람에게 보일 문장은 앱이 짓는다.
        if (picture_error_.empty() == false)
            return make_label(id, u8"그림을 읽지 못했다 — " + picture_name_ + u8": " + picture_error_, 11.0f, luil::label_color_role::primary);
        if (animation_.valid() == false)
            return make_label(id, u8"아직 고른 그림이 없다.", 11.0f, luil::label_color_role::dim);
        const std::u8string playback { animation_.animated() ? u8" frame 애니메이션으로 읽었다." : u8" frame으로 읽었다." };
        return make_label(id, picture_name_ + u8" — " + to_u8(animation_.width()) + u8"×" + to_u8(animation_.height()) + u8" 픽셀, " + to_u8(static_cast<int>(animation_.frame_count())) + playback,
            11.0f, luil::label_color_role::dim);
    }
} // namespace demo

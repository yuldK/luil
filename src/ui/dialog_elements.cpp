#include "luil/ui/dialog_elements.h"
#include "luil/text/grapheme.h"

#include "luil/generated/codicons.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"
#include "ui/text_layout_internal.h"
#include "ui/text_link_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 꺼진 글자가 이 바탕 위에서 읽히지 않는가. 알파로 얹은 바탕은 표면이 비쳐 판정하지 않는다.
        //  - 3:1은 WCAG의 큰 글자·그래픽 기준이다. 꺼진 글자는 대비 기준에서 빠지지만 읽혀야 한다.
        [[nodiscard]] bool disabled_label_unreadable_on(const ui_color background, const ui_color label) noexcept
        {
            if ((background >> 24U) != 0xFFU)
                return false;
            const float first { relative_luminance(background) };
            const float second { relative_luminance(label) };
            return (std::max(first, second) + 0.05f) / (std::min(first, second) + 0.05f) < 3.0f;
        }

        // 암호 칸이 글자 대신 그리는 점이다 (U+25CF). Windows 암호 칸의 점이고, 작은 글자 크기에서도 •(U+2022)보다
        // 잘 보인다. 글꼴에 없으면 대체 글꼴이 그리고, 재는 쪽도 같은 길을 탄다.
        constexpr std::u8string_view secret_mark { u8"\u25CF" };

        [[nodiscard]] bool utf8_lead(const char8_t value) noexcept
        {
            return (static_cast<unsigned int>(value) & 0xC0u) != 0x80u;
        }

        // offset 앞의 code point 수다. 가린 글에서는 그만큼의 점이 그 앞에 선다.
        [[nodiscard]] std::size_t code_points_before(const std::u8string_view text, const std::size_t offset) noexcept
        {
            const std::size_t end { offset < text.size() ? offset : text.size() };
            return static_cast<std::size_t>(std::count_if(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(end), utf8_lead));
        }

        // code point마다 점 하나인 가린 글이다. 결합 문자도 하나로 세어, 점이 보이는 글자보다 많을 수 있다.
        [[nodiscard]] std::u8string masked_text(const std::u8string_view text)
        {
            const std::size_t count { code_points_before(text, text.size()) };
            std::u8string masked {};
            masked.reserve(count * secret_mark.size());
            for (std::size_t index { 0 }; index < count; ++index)
                masked.append(secret_mark);
            return masked;
        }

        // 가린 글의 offset(점 경계)을 원래 글의 offset으로 돌린다.
        [[nodiscard]] std::size_t unmasked_offset(const std::u8string_view text, const std::size_t masked_offset) noexcept
        {
            std::size_t remaining { masked_offset / secret_mark.size() };
            for (std::size_t offset { 0 }; offset < text.size(); ++offset)
            {
                if (utf8_lead(text[offset]) == false)
                    continue;
                if (remaining == 0)
                    return offset;
                --remaining;
            }
            return text.size();
        }
    }

    dialog_caption_element::dialog_caption_element(dialog_caption_config config)
        : ui_element { ui_element_id { ui_element_kind::dialog_caption, config.owner } }
        , config_ { std::move(config) }
    {
        // 잡아 끄는 동안 좌표 변화량만 메시지로 바꾼다.
        // 나르는 payload가 없으므로 drag & drop이 아니라 pointer drag다.
        if (config_.move)
        {
            pointer_drag_target target {};
            target.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
                const float dx { (current.x - previous.x) / scale_ };
                const float dy { (current.y - previous.y) / scale_ };
                if (dx == 0.0f && dy == 0.0f)
                    return {};
                return { config_.move(dx, dy) };
            };
            set_pointer_drag_target(std::move(target));
            // 잡을 수 있는 자리라는 것을 포인터로 알린다.
            set_cursor(ui_cursor::grab);
            set_active_cursor(ui_cursor::grabbing);
        }

        if (config_.close == nullptr)
            return;

        const button_config close_button {
            .glyph = codicons::icon_close,
            .icon_size = 12.0f,
            .corner_radius = 3.0f,
            .role = button_visual_role::caption_close,
        };
        auto close { std::make_unique<button_element>(ui_element_id { ui_element_kind::dialog_caption_close, config_.owner }, close_button) };
        // 창 캡션의 닫기와 같은 chrome이다 — Tab의 자리가 아니다.
        // dialog 안에서 Tab이 도는 것은 내용의 컨트롤들이다.
        close->set_tab_stop(false);
        close->set_tooltip(config_.close_tooltip);
        close->set_action(ui_trigger::left_click, config_.close);
        // 자식이라 hit test에서 캡션보다 먼저 걸린다.
        //  - 닫기 버튼 위에서 눌러도 이동이 시작되지 않는다.
        close_ = close.get();
        add_child(std::move(close));
    }

    void dialog_caption_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        scale_ = scale;
        const float caption_height { height_for(config_) * scale };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, caption_height });
        if (close_ == nullptr)
            return;

        const float button_size { 24.0f * scale };
        const float inset { 8.0f * scale };
        close_->arrange(context.for_child({ context.slot.x + context.slot.width - button_size - inset, context.slot.y + (caption_height - button_size) / 2.0f, button_size, button_size }));
    }

    void dialog_caption_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const float inset { 14.0f * scale };
        if (config_.title.empty() == false)
        {
            // 닫기 버튼과 겹치지 않도록 오른쪽을 비운다.
            const float reserved { close_ != nullptr ? 40.0f * scale : inset };
            const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(config_.title_font_size, scale) };
            const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
            static_cast<void>(draw_text_within(context.canvas, config_.title, box.x + inset, box.y + centered_text_baseline(font, box.height), box.width - inset - reserved, font, foreground));
        }

        // 본문과의 경계다.
        const SkRect divider { SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale) };
        context.canvas.drawRect(divider, solid_paint(context.palette.divider));
        draw_children(context, interaction);
    }

    float dialog_caption_element::height_for(const dialog_caption_config& config) noexcept
    {
        return config.height;
    }

    access_info dialog_caption_element::accessibility() const
    {
        return { .role = access_role::title_bar, .name = config_.title };
    }

    text_button_fill text_button_fill_for(const text_button_config& config, const bool enabled) noexcept
    {
        // link는 두를 상자가 없다 — 채움도 테도 설 자리가 없다.
        if (config.visual == text_button_visual::link)
            return text_button_fill::none;
        // **기본 버튼은 채움으로 말한다.** 테로 말하면 `ui_tree`가 얹는 초점 테와
        // 색·굵기·여백이 같아져 한 화면에서 둘이 구별되지 않는다 — 확인 dialog에서
        // 초점 테(취소)와 기본 버튼 테(초기화)가 실제로 그렇게 읽혔다.
        // 채움은 "Enter가 여기로 간다", 테는 "지금 여기를 보고 있다"로 갈린다.
        //  - 꺼진 버튼은 채우지 않는다. 채움은 주 동작으로의 권유인데 누를 수 없는
        //    자리에 그것을 세우면 거짓말이 된다.
        if (config.default_button && enabled)
            return text_button_fill::solid_accent;
        // 되돌릴 수 없는 동작은 자기 색으로 채운다.
        // 기본 버튼 판정보다 뒤인 것은 Enter의 자리가 먼저 보여야 하기 때문이다.
        if (config.visual == text_button_visual::danger)
            return text_button_fill::soft_danger;
        return config.visual == text_button_visual::accent ? text_button_fill::soft_accent : text_button_fill::plain;
    }

    float text_button_element::icon_width_for(const text_button_config& config) noexcept
    {
        return config.glyph != 0 ? text_button_icon_size + text_button_icon_gap : 0.0f;
    }

    text_button_element::text_button_element(const ui_element_id id, text_button_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        // 강조색 채움과 Enter 대상 지정을 같은 설정으로 연결한다.
        // link는 채울 상자가 없으므로 기본 버튼에서 제외한다.
        if (config_.default_button && config_.visual != text_button_visual::link)
            set_default_button(true);
    }

    void text_button_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void text_button_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const bool accent { config_.visual == text_button_visual::accent };
        const bool danger { config_.visual == text_button_visual::danger };
        const bool link { config_.visual == text_button_visual::link };
        const bool hot { enabled() && (interaction.pressed == id() || interaction.hovered == id()) };
        const bool down { enabled() && interaction.pressed == id() };
        const bool over { enabled() && interaction.hovered == id() };
        const text_button_fill fill { text_button_fill_for(config_, enabled()) };
        const bool solid { fill == text_button_fill::solid_accent };

        // link는 바탕을 칠하지 않는다.
        // 상자가 없어야 글 흐름 안에 놓을 수 있고, 그것이 link가 버튼과 갈리는 전부다.
        if (fill != text_button_fill::none)
        {
            const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            const float radius { context.metrics.control_corner_radius * scale };
            // 채운 버튼의 hover·눌림은 **자기 색 안에서** 움직인다.
            // 중립색으로 갈아 끼우면 누르는 동안만 주 동작이 아닌 것처럼 보인다.
            ui_color background { context.palette.input_background };
            if (solid)
                background = down ? context.palette.accent_pressed : (over ? context.palette.accent_hover : context.palette.accent);
            else if (fill == text_button_fill::soft_accent)
            {
                background = down ? context.palette.button_pressed_background : (over ? context.palette.soft_button_hover_background : context.palette.soft_button_background);
                // 꺼진 글자(`disabled_foreground`)는 옅은 강조 바탕을 위해 만든 색이 아니다. 고대비는 이 바탕을
                // 불투명한 highlight로 접어 GrayText가 사라지므로, 꺼진 글자와 짝인 입력 표면에 깐다.
                if (enabled() == false && disabled_label_unreadable_on(background, context.palette.disabled_foreground))
                    background = context.palette.input_background;
            }
            else if (fill == text_button_fill::soft_danger)
                // 오류색에는 강조색과 달리 hover·soft 짝이 없다.
                // 팔레트가 역할 하나에 tone을 층 지어 같은 자리를 만든다 — 색을 새로 정하지 않는다.
                background = down ? context.palette.danger_button_pressed_background : (over ? context.palette.danger_button_hover_background : context.palette.danger_button_background);
            else if (down)
                background = context.palette.button_pressed_background;
            else if (over)
                background = context.palette.button_hover_background;
            // 선택자는 마지막에 얹고, 그 색은 hover·눌림에도 그대로 남는다.
            if (config_.fill)
                background = config_.fill(context.palette);
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));
        }

        const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
        ui_color label_color { context.palette.primary_foreground };
        // 채운 바탕 위의 글자는 그 바탕을 위해 만든 색이어야 읽힌다. 채운 강조와 옅은 강조는 바탕의 밝기가
        // 달라 글자 역할도 다르다.
        if (solid)
            label_color = context.palette.accent_foreground;
        else if (accent)
            label_color = context.palette.accent_emphasis_foreground;
        else if (danger)
            // 옅은 바탕이라 뜻을 나르는 것은 글자다 — 오류색 그대로 쓴다.
            label_color = context.palette.error_accent;
        else if (link)
            // 바탕이 없으니 hover를 글자 색이 말한다.
            label_color = hot ? context.palette.accent_hover : context.palette.accent;
        if (config_.label_color)
            label_color = config_.label_color(context.palette);
        const SkPaint foreground { solid_paint(enabled() ? label_color : context.palette.disabled_foreground) };

        // 아이콘과 글자를 **한 덩어리로** 가운데에 둔다.
        // 아이콘만 왼쪽 끝에 붙이면 글이 짧을 때 버튼이 한쪽으로 쏠려 보인다.
        const float icon { icon_width_for(config_) * scale };
        const float text_width { measure_text(config_.text, font) };
        float left { box.x + (box.width - icon - text_width) / 2.0f };
        // 덩어리가 버튼보다 넓으면 왼쪽 끝에서 시작해 글을 잘라 낸다.
        // 가운데 정렬을 그대로 두면 아이콘이 버튼 밖으로 나간다.
        if (left < box.x)
            left = box.x;

        if (config_.glyph != 0)
        {
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), text_button_icon_size * scale };
            draw_centered_glyph(context.canvas, config_.glyph, { left, box.y, text_button_icon_size * scale, box.height }, glyph_font, foreground);
            left += icon;
        }
        const float baseline { box.y + centered_text_baseline(font, box.height) };
        const float drawn { draw_text_within(context.canvas, config_.text, left, baseline, box.x + box.width - left, font, foreground) };

        // 밑줄은 **실제로 그려진 폭**만큼이다. 잘린 글 뒤로 밑줄만 뻗지 않는다.
        if (link && drawn > 0.0f)
            context.canvas.drawRect(SkRect::MakeXYWH(left, baseline + 2.0f * scale, drawn, scale), foreground);
    }

    text_input_element::text_input_element(const ui_element_id id, text_input_view value, text_input_config config)
        : ui_element { id }
        , value_ { std::move(value) }
        , config_ { std::move(config) }
    {
        if (masked())
            masked_ = masked_text(value_.displayed_text());

        // 누름·끌기·더블 클릭은 interaction controller가 직접 다룬다.
        //  - 포인터 x를 offset으로 옮기려면 측정 함수가 필요한데 그것은 입력 thread가 가진다.
        //    여기서는 클릭을 흡수해 hit test 대상이 되게만 한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });

        if (config_.clear != nullptr)
        {
            const button_config clear_button {
                .glyph = config_.clear_glyph != 0 ? config_.clear_glyph : codicons::icon_close,
                .icon_size = 10.0f,
                .corner_radius = 3.0f,
            };
            auto clear { std::make_unique<button_element>(ui_element_id { ui_element_kind::text_input_clear, id.owner }, clear_button) };
            // 칸 **안의** 보조 버튼이다. Tab의 자리는 칸이고, 이 버튼까지 자리를 가지면
            // Tab이 칸과 지우기를 번갈아 지난다 — 탭의 닫기 버튼과 같은 판정이다.
            clear->set_tab_stop(false);
            clear->set_tooltip(config_.clear_tooltip);
            clear->set_action(ui_trigger::left_click, config_.clear);
            clear_ = clear.get();
            add_child(std::move(clear));
        }

        if (config_.links != nullptr && config_.secret == false)
            make_text_link_elements(id, value_.text, *config_.links, true, config_.activate_link, config_.link_hint, [this](const std::size_t index, std::unique_ptr<text_link_element> element) {
                link_elements_.emplace_back(index, element.get());
                add_child(std::move(element));
            });

        if (config_.number.has_value() == false || config_.number->change_to == nullptr)
            return;
        // 한 줄 칸의 텍스트 경로는 ↑↓·PageUp/PageDown을 쓰지 않으므로 그 넷이 여기로 흐른다.
        // ←→·Home/End는 그대로 caret의 것이다.
        key_step_target steps {};
        steps.axis = focus_axis::vertical;
        steps.up_increases = true;
        steps.on_step = [this](const value_step step) -> std::optional<std::vector<input_action>> { return step_number(step); };
        set_key_step_target(std::move(steps));
        if (config_.number->spin_buttons == false)
            return;

        // 지우기 단추와 같은 칸 안의 보조 단추다 — Tab 자리를 갖지 않고, 키보드로는 ↑↓가 같은 일을 한다.
        const auto spin_button = [this, &id](const ui_element_kind kind, const char32_t glyph, const std::u8string& tooltip, const value_step step) {
            const button_config look { .glyph = glyph, .icon_size = 8.0f, .corner_radius = 2.0f };
            auto button { std::make_unique<button_element>(ui_element_id { kind, id.owner }, look) };
            button->set_tab_stop(false);
            button->set_tooltip(tooltip);
            button->set_action(ui_trigger::left_click, [this, step](const ui_action_context&) { return step_number(step); });
            ui_element* const raw { button.get() };
            add_child(std::move(button));
            return raw;
        };
        increase_ = spin_button(ui_element_kind::text_input_increase, codicons::icon_chevron_up, config_.number->increase_tooltip, value_step::increase);
        decrease_ = spin_button(ui_element_kind::text_input_decrease, codicons::icon_chevron_down, config_.number->decrease_tooltip, value_step::decrease);
    }

    std::vector<input_action> text_input_element::step_number(const value_step step) const
    {
        // 조합 중의 걸음은 삼킨다 — 확정되지 않은 글을 바꾸면 IME와 초안이 어긋난다.
        // 끝에 닿은 걸음도 삼킨다. 흘려보내면 PageDown이 바깥 스크롤로 새어 화면이 함께 뛴다.
        if (value_.composing || config_.number.has_value() == false || config_.number->change_to == nullptr)
            return {};
        std::optional<std::u8string> next { stepped_number_input(value_.text, config_.number->range, step) };
        if (next.has_value() == false)
            return {};
        return { config_.number->change_to(std::move(*next)) };
    }

    access_info text_button_element::accessibility() const
    {
        // 고른 값을 싣고 있으면 라디오 항목이다 — 그리는 모양이 글자 버튼일 뿐
        // 하는 일은 여럿 가운데 하나를 고르는 것이다 (`choice_group`의 토글 스타일).
        // 이름은 적힌 글이다. 아이콘만 있는 단추는 글이 없으므로 앱이 단 이름(없으면 툴팁)으로 읽힌다.
        const std::u8string& name { config_.text.empty() ? access_name() : config_.text };
        if (config_.selected.has_value())
            return { .role = access_role::radio_button, .name = name, .selected = config_.selected };
        // link는 글 흐름 안의 자리라 역할도 갈린다.
        return { .role = config_.visual == text_button_visual::link ? access_role::link : access_role::button, .name = name };
    }

    float text_input_element::leading_width_for(const text_input_config& config) noexcept
    {
        return config.leading_glyph != 0 ? text_input_glyph_size + text_input_icon_gap : 0.0f;
    }

    float text_input_element::trailing_width_for(const text_input_config& config) noexcept
    {
        const bool spin { config.number.has_value() && config.number->spin_buttons && config.number->change_to != nullptr };
        return (config.clear != nullptr ? text_input_clear_size + text_input_icon_gap : 0.0f) + (spin ? text_input_spin_width + text_input_icon_gap : 0.0f);
    }

    float text_input_element::error_height_for(const text_input_config& config) noexcept
    {
        return config.error.empty() ? 0.0f : text_input_error_height;
    }

    bool text_input_element::invalid() const noexcept
    {
        return config_.invalid || config_.error.empty() == false;
    }

    rect_f text_input_element::box() const noexcept
    {
        const rect_f slot { bounds() };
        const float error { error_height_for(config_) * scale_ };
        return { slot.x, slot.y, slot.width, std::max(0.0f, slot.height - error) };
    }

    float text_input_element::font_size() const noexcept
    {
        return font_size_;
    }

    float text_input_element::text_left() const noexcept
    {
        return bounds().x + (7.0f + leading_width_for(config_)) * scale_;
    }

    float text_input_element::inner_width() const noexcept
    {
        // 그리기·offset_at·IME 질의가 전부 이 하나를 쓴다.
        // 아이콘이 먹은 폭을 여기서 빼야 셋이 함께 맞는다.
        const float width { bounds().width - (14.0f + leading_width_for(config_) + trailing_width_for(config_)) * scale_ };
        return width > 0.0f ? width : 0.0f;
    }

    bool text_input_element::masked() const noexcept
    {
        return config_.secret && config_.reveal == false;
    }

    std::size_t text_input_element::shown_offset(const std::u8string_view text, const std::size_t offset) const noexcept
    {
        return masked() ? code_points_before(text, offset) * secret_mark.size() : offset;
    }

    std::u8string_view text_input_element::displayed_text() const noexcept
    {
        // 조합 중에는 조합 글이 섞인 쪽을 그린다.
        // 확정된 글은 그 동안 logic의 초안에 그대로 남아 있다.
        // 규칙은 view가 갖는다 — 앱이 검색에 쓰는 글과 그리는 글이 같아야 한다.
        // 가린 칸은 그 글을 가린 점들이다.
        if (masked())
            return masked_;
        return value_.displayed_text();
    }

    std::size_t text_input_element::displayed_caret() const noexcept
    {
        const std::u8string_view text { value_.displayed_text() };
        const std::size_t caret { value_.composing ? value_.composition_caret : value_.caret };
        return shown_offset(text, caret < text.size() ? caret : text.size());
    }

    float text_input_element::scroll_for(const std::function<float(std::u8string_view)>& measure) const
    {
        return scroll_for(measure, displayed_text(), displayed_caret());
    }

    float text_input_element::scroll_for(const std::function<float(std::u8string_view)>& measure, const std::u8string_view text, const std::size_t caret) const
    {
        const float total { measure(text) };
        const float visible { inner_width() };
        float maximum { total - visible };
        if (maximum < 0.0f)
            maximum = 0.0f;

        // caret이 칸 밖으로 나가지 않을 만큼만 민다.
        // 값은 글·caret·폭만으로 정해져 그리기와 hit test가 같은 결과를 낸다.
        const float caret_x { measure(text.substr(0, caret < text.size() ? caret : text.size())) };
        float scroll { caret_x - visible };
        if (scroll < 0.0f)
            scroll = 0.0f;
        if (scroll > caret_x)
            scroll = caret_x;
        if (scroll > maximum)
            scroll = maximum;
        return scroll;
    }

    text_layout text_input_element::layout_for(const std::u8string_view text) const
    {
        if (layout_.valid() && layout_.text() == text)
            return layout_;
        if (layout_.valid() == false)
            return {};
        return remake_text_layout(layout_, { .text = std::u8string { text }, .font_size = font_size(), .line_height = box().height });
    }

    float text_input_element::scroll_for(const text_layout& layout, const std::size_t caret) const
    {
        const float maximum { std::max(0.0f, layout.size().width - inner_width()) };
        return std::clamp(layout.caret_rect(caret).x - inner_width(), 0.0f, maximum);
    }

    void text_input_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        font_size_ = scaled_font_size(11.0f, scale_);
        layout_ = {};
        const rect_f area { box() };
        if (ui_text_shaping_enabled() && context.measurer == nullptr)
        {
            auto layout { make_text_layout({ .text = std::u8string { displayed_text() }, .font_size = font_size(), .line_height = area.height }) };
            if (layout.backend() != text_layout_backend::unavailable)
                layout_ = std::move(layout);
        }

        // 오른쪽 안쪽 가장자리부터 스핀 단추, 지우기 단추 차례로 붙인다. 글이 차지하는 폭은 inner_width가 이미 뺐다.
        const float inset { 4.0f * scale_ };
        float right { area.x + area.width - inset };
        if (increase_ != nullptr && decrease_ != nullptr)
        {
            // 위·아래 단추가 상자 높이를 반씩 나눈다. 테두리 안쪽 한 픽셀씩은 비운다.
            const float width { text_input_spin_width * scale_ };
            const float half { std::max(0.0f, area.height - 2.0f * scale_) / 2.0f };
            increase_->arrange(context.for_child({ right - width, area.y + scale_, width, half }));
            decrease_->arrange(context.for_child({ right - width, area.y + scale_ + half, width, half }));
            right -= width + text_input_icon_gap * scale_;
        }
        if (clear_ != nullptr)
        {
            const float size { text_input_clear_size * scale_ };
            clear_->arrange(context.for_child({ right - size, area.y + (area.height - size) / 2.0f, size, size }));
        }
        if (link_elements_.empty())
            return;
        // 링크 조각은 그리기와 같은 가로 스크롤로 놓고 글이 보이는 안쪽 폭으로 자른다.
        const rect_f clip { text_left(), area.y, inner_width(), area.height };
        const std::u8string_view text { displayed_text() };
        const float size { font_size() };
        const text_measurer& measurer { context.measure_text() };
        const auto measure = [&measurer, size](const std::u8string_view value) { return measurer != nullptr ? measurer(value, size) : 0.0f; };
        const auto layout { layout_for(text) };
        const auto position = [&](const std::size_t at) { return layout.valid() ? layout.caret_rect(at).x : measure(text.substr(0, at)); };
        const float left { text_left() - (layout.valid() ? scroll_for(layout, displayed_caret()) : scroll_for(measure)) };
        for (const auto& [index, element] : link_elements_)
        {
            const text_link& link { (*config_.links)[index] };
            std::vector<rect_f> fragments {};
            if (value_.composing == false && link.end <= text.size())
                fragments.push_back({ left + position(link.begin), area.y, position(link.end) - position(link.begin), area.height });
            element->set_enabled(enabled());
            element->place(std::move(fragments), clip);
        }
    }

    std::optional<text_input_snapshot> text_input_element::text_input() const
    {
        text_input_snapshot snapshot {
            value_.text,
            value_.caret,
            value_.anchor,
            value_.composing,
            value_.composition_text,
            value_.composition_caret,
            value_.applied_sequence,
            config_.secret,
            config_.input,
        };
        snapshot.navigation_keys_to_parent = config_.navigation_keys_to_parent;
        // caret이 링크 안이거나 바로 뒤면 그 링크다 (`text_area_element`와 같은 규칙).
        if (config_.links != nullptr && config_.secret == false && value_.composing == false)
        {
            const std::size_t begin { std::min(value_.caret, value_.anchor) };
            const std::size_t end { std::max(value_.caret, value_.anchor) };
            const text_link* link { text_link_at(*config_.links, begin) };
            if (link == nullptr && begin == end && begin > 0)
                link = text_link_at(*config_.links, begin - 1);
            if (link != nullptr && end <= link->end)
            {
                snapshot.link = link->destination;
                snapshot.activate_link = config_.activate_link != nullptr ? &config_.activate_link : nullptr;
            }
        }
        // 숫자 칸은 앱이 키보드 종류를 따로 고르지 않아도 숫자 키보드를 띄운다.
        if (config_.number.has_value() && snapshot.options.kind == text_input_kind::text)
            snapshot.options.kind = config_.number->range.decimals > 0 ? text_input_kind::decimal : text_input_kind::number;
        return snapshot;
    }

    std::optional<rect_f> text_input_element::text_span_bounds(const text_span_query& query, const text_measurer& measurer) const
    {
        if (measurer == nullptr)
            return std::nullopt;

        // 질의한 문서(조합 글 포함)를 그대로 잰다.
        //  - 확정 글로 재면 조합이 시작되는 순간 offset이 문서 길이를 넘어
        //    구간이 칸 왼쪽 끝으로 무너진다.
        // 스크롤도 같은 문서로 계산해 화면에 그려진 글자와 어긋나지 않는다.
        // 가린 칸은 그 문서를 가린 점들로 잰다.
        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        const std::u8string dots { masked() ? masked_text(query.text) : std::u8string {} };
        const std::u8string_view text { masked() ? std::u8string_view { dots } : query.text };
        const std::size_t shown_begin { shown_offset(query.text, query.begin) };
        const std::size_t shown_end { shown_offset(query.text, query.end) };
        const std::size_t begin { shown_begin < text.size() ? shown_begin : text.size() };
        const std::size_t clamped_end { shown_end < text.size() ? shown_end : text.size() };
        const std::size_t end { clamped_end > begin ? clamped_end : begin };

        if (const auto layout { layout_for(text) }; layout.valid())
        {
            const float left { text_left() - scroll_for(layout, shown_offset(query.text, query.caret)) };
            const float from { left + layout.caret_rect(begin).x };
            const float to { left + layout.caret_rect(end).x };
            return rect_f { std::min(from, to), box().y, std::abs(to - from), box().height };
        }
        const float left { text_left() - scroll_for(measure, text, shown_offset(query.text, query.caret)) };
        const float from { left + measure(text.substr(0, begin)) };
        const float to { left + measure(text.substr(0, end)) };
        return rect_f { from, box().y, to - from, box().height };
    }

    std::optional<std::size_t> text_input_element::offset_at(const float x, const text_measurer& measurer) const
    {
        // 조합 중에도 그려진 글(조합 글 포함)에서 찾는다.
        //  - 누름은 host가 조합을 먼저 확정한 뒤에 오고, 확정된 글이 곧 이 글이다.
        // 가린 칸은 그려진 점들 위에서 경계를 찾고 원래 글의 경계로 돌린다 (가린 칸은 조합하지 않는다).
        const std::u8string dots { masked() ? masked_text(value_.text) : std::u8string {} };
        const std::u8string_view text { masked() ? std::u8string_view { dots } : value_.displayed_text() };
        const auto original = [this](const std::size_t offset) { return masked() ? unmasked_offset(value_.text, offset) : offset; };
        if (measurer == nullptr)
            return original(text.size());

        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        if (const auto layout { layout_for(text) }; layout.valid())
        {
            const auto caret { layout.hit_test({ x - text_left() + scroll_for(layout, displayed_caret()), 0.0f }) };
            return original(caret.offset);
        }
        const float target { x - text_left() + scroll_for(measure) };
        if (target <= 0.0f)
            return std::size_t { 0 };

        // 글자소 경계를 한 번에 모은 뒤 폭을 이분 탐색한다. 긴 입력에서도
        // 모든 접두사를 반복 측정하지 않고, 경계마다 글을 다시 훑지 않는다.
        const std::vector<std::size_t> boundaries { text::grapheme_boundaries(text) };
        const auto after { std::upper_bound(boundaries.begin(), boundaries.end(), target, [&](const float position, const std::size_t offset) { return position < measure(text.substr(0, offset)); }) };
        if (after == boundaries.end())
            return original(text.size());
        if (after == boundaries.begin())
            return original(*after);
        const std::size_t before { *(after - 1) };
        const float after_width { measure(text.substr(0, *after)) };
        if (target - measure(text.substr(0, before)) < after_width - target)
            return original(before);
        // 폭이 없는 결합 문자도 포함해, 같은 거리에 있는 마지막 경계를 고른다.
        const auto narrower = [&](const float position, const std::size_t offset) { return position < measure(text.substr(0, offset)); };
        const auto last_equal { std::upper_bound(after + 1, boundaries.end(), after_width, narrower) };
        return original(*(last_equal - 1));
    }

    void text_input_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool focused { interaction.focused_input == id() };
        const rect_f box { this->box() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { context.metrics.control_corner_radius * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.input_background));
        // 초점을 받은 칸은 테두리를 강조해 입력이 이곳으로 간다는 것을 보인다.
        // 오류 상태는 초점을 받아도 오류색을 지키고 두께만 늘린다 — 고치는 동안 오류 표시가 사라지지 않는다.
        const bool invalid { this->invalid() };
        SkPaint border { solid_paint(invalid ? context.palette.error_accent : (focused ? context.palette.accent : context.palette.input_border)) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth((invalid && focused ? 2.0f : 1.0f) * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        // 앞 글리프는 글의 일부가 아니라 칸의 표식이라 흐린 보조색이다.
        if (config_.leading_glyph != 0)
        {
            const float glyph_size { text_input_glyph_size * scale };
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph_size };
            const SkPaint dim { solid_paint(context.palette.secondary_foreground) };
            draw_centered_glyph(context.canvas, config_.leading_glyph, { box.x + 7.0f * scale, box.y, glyph_size, box.height }, glyph_font, dim);
        }

        const SkFont font { sk_ref_sp(context.ui_typeface), font_size() };
        const float left { text_left() };
        const float baseline { box.y + (layout_.valid() ? layout_.lines().front().baseline : centered_text_baseline(font, box.height)) };
        const float visible { inner_width() };

        const std::u8string_view text { displayed_text() };
        if (text.empty())
        {
            // 빈 값의 뜻은 안내가 설명한다.
            // 초점이 없을 때만 흐리게 그린다.
            if (focused == false && config_.placeholder.empty() == false)
            {
                SkPaint placeholder { solid_paint(context.palette.primary_foreground) };
                placeholder.setAlphaf(0.5f);
                static_cast<void>(draw_text_within(context.canvas, config_.placeholder, left, baseline, visible, font, placeholder));
            }
        }

        // 글이 칸보다 길면 잘라 `…`를 붙이는 대신 clip + 가로 스크롤로 그린다.
        // 편집 중에는 잘린 표시보다 caret 주변이 보이는 것이 중요하다.
        const auto measure { [&font](const std::u8string_view value) { return measure_text(value, font); } };
        const auto layout { layout_for(text) };
        const auto position { [&](const std::size_t at) { return layout.valid() ? layout.caret_rect(at).x : measure(text.substr(0, at)); } };
        const float scroll { layout.valid() ? scroll_for(layout, displayed_caret()) : scroll_for(measure) };
        // 조합 중에는 선택 대신 조합 구간에 밑줄을 긋는다.
        //  - 그 동안 선택은 IME의 것이다.
        const std::size_t selection_begin { value_.composing ? 0u : shown_offset(value_.text, value_.caret < value_.anchor ? value_.caret : value_.anchor) };
        const std::size_t selection_end { value_.composing ? 0u : shown_offset(value_.text, value_.caret < value_.anchor ? value_.anchor : value_.caret) };
        const float caret_x { left - scroll + position(displayed_caret()) };

        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(left, box.y, visible, box.height));
        std::optional<SkRect> selection {};
        if (selection_begin != selection_end)
        {
            const float from { left - scroll + position(selection_begin) };
            const float to { left - scroll + position(selection_end) };
            selection = SkRect::MakeXYWH(from, box.y + 3.0f * scale, to - from, box.height - 6.0f * scale);
            context.canvas.drawRect(*selection, solid_paint(context.palette.selection_background));
        }
        if (text.empty() == false)
        {
            const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
            if (layout.valid())
                draw_text_layout(context.canvas, layout, { left - scroll, box.y }, foreground);
            else
                draw_text(context.canvas, text, left - scroll, baseline, font, foreground);
            // 링크는 강조색 글자와 밑줄이다. 조각은 배치가 링크 자식에 놓은 그대로다.
            for (const auto& [index, element] : link_elements_)
            {
                const rect_f part { element->bounds() };
                if (part.width <= 0.0f)
                    continue;
                if (enabled())
                {
                    context.canvas.save();
                    context.canvas.clipRect(SkRect::MakeXYWH(part.x, part.y, part.width, part.height));
                    if (layout.valid())
                        draw_text_layout(context.canvas, layout, { left - scroll, box.y }, solid_paint(context.palette.accent));
                    else
                        draw_text(context.canvas, text, left - scroll, baseline, font, solid_paint(context.palette.accent));
                    context.canvas.restore();
                }
                context.canvas.drawRect(SkRect::MakeXYWH(part.x, baseline + 1.0f * scale, part.width, (element->emphasized(interaction) ? 2.0f : 1.0f) * scale),
                    solid_paint(enabled() ? context.palette.accent : context.palette.disabled_foreground));
            }
            // 고른 구간의 글자는 그 바탕의 짝이다. 옅은 바탕에서는 본문 글자와 같은 값이라
            // 겹쳐 그려도 같은 그림이고, 고대비에서만 highlight 글자로 바뀐다.
            if (selection.has_value() && enabled() && context.palette.selection_foreground != context.palette.primary_foreground)
            {
                context.canvas.save();
                context.canvas.clipRect(*selection);
                if (layout.valid())
                    draw_text_layout(context.canvas, layout, { left - scroll, box.y }, solid_paint(context.palette.selection_foreground));
                else
                    draw_text(context.canvas, text, left - scroll, baseline, font, solid_paint(context.palette.selection_foreground));
                context.canvas.restore();
            }
        }

        // 조합 구간은 밑줄로 표시한다.
        // 아직 확정되지 않은 글이라는 뜻이다.
        if (value_.composing && value_.composing_end > value_.composing_begin)
        {
            const std::size_t shown_begin { shown_offset(value_.composition_text, value_.composing_begin) };
            const std::size_t shown_end { shown_offset(value_.composition_text, value_.composing_end) };
            const std::size_t underline_begin { shown_begin < text.size() ? shown_begin : text.size() };
            const std::size_t underline_end { shown_end < text.size() ? shown_end : text.size() };
            const float from { left - scroll + position(underline_begin) };
            const float to { left - scroll + position(underline_end) };
            const float underline_top { baseline + 2.0f * scale };
            context.canvas.drawRect(SkRect::MakeXYWH(from, underline_top, to - from, 1.0f * scale), solid_paint(context.palette.accent));
        }

        // caret은 초점을 받은 동안 caret 자리에서 깜빡인다.
        // 초점을 받은 시각이 위상 기준이라 받는 순간에는 항상 켜져 있다.
        // 다시 그리기는 next_update가 예고한 시각에 platform이 일으킨다.
        const bool caret_on { focused && interaction.focus_started_at.has_value() && ((context.now - *interaction.focus_started_at) / caret_blink_interval) % 2 == 0 };
        if (caret_on)
        {
            const float caret_top { box.y + 4.0f * scale };
            context.canvas.drawRect(SkRect::MakeXYWH(caret_x, caret_top, 1.0f * scale, box.height - 8.0f * scale), solid_paint(context.palette.primary_foreground));
        }
        context.canvas.restore();

        // 오류 글은 상자 아래 줄이다. 한 줄이라 길면 끝을 줄이고, 보조 기술은 전체 글을 읽는다.
        if (config_.error.empty() == false)
        {
            const SkFont error_font { sk_ref_sp(context.ui_typeface), scaled_font_size(10.0f, scale) };
            const float line { bounds().y + bounds().height - box.y - box.height };
            const float error_baseline { box.y + box.height + centered_text_baseline(error_font, line) };
            const SkPaint error_paint { solid_paint(context.palette.error_accent) };
            static_cast<void>(draw_text_within(context.canvas, config_.error, box.x + 2.0f * scale, error_baseline, box.width - 2.0f * scale, error_font, error_paint));
        }

        // 지우기 버튼은 글의 clip 밖이다 — 글이 길어도 버튼이 잘리지 않는다.
        draw_children(context, interaction);
    }

    std::optional<std::chrono::steady_clock::time_point> text_input_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        // caret이 깜빡이는 것은 초점을 받은 박스뿐이다.
        if (interaction.focused_input != id() || interaction.focus_started_at.has_value() == false)
            return std::nullopt;
        // 다음 반주기 경계다 (draw의 켜짐 판정과 같은 위상).
        const auto phase { (context.now - *interaction.focus_started_at) % caret_blink_interval };
        return context.now + (caret_blink_interval - phase);
    }

    std::optional<rect_f> text_input_element::focus_ring_bounds() const
    {
        return box();
    }

    access_info text_input_element::accessibility() const
    {
        // 값은 확정 글이다 — 조합 글은 아직 사용자의 글이 아니라 싣지 않는다
        // (`text_input_view::text`가 같은 판단으로 조합 글을 밖에 둔다).
        // 가린 칸은 화면처럼 점이다. 보조 기술이 글자 수는 알되 글은 읽지 못한다.
        // 이름은 앱이 단 것이 먼저다. 안내 글은 글을 쓰면 사라지는 것이라 이름이 없을 때의 대신이다.
        access_info info {
            .role = access_role::edit, .name = access_name().empty() ? config_.placeholder : access_name(), .value = masked() ? masked_text(value_.text) : value_.text, .secret = config_.secret
        };
        info.invalid = invalid();
        info.error = config_.error;
        return info;
    }

    std::optional<size_f> text_button_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(available_width);
        const float content { icon_width_for(config_) + std::ceil(context.text_width(config_.text, context.style_metrics().body_font_size)) };
        if (config_.visual == text_button_visual::link)
            return size_f { content, text_button_height };
        return size_f { std::max(text_button_minimum_width, content + text_button_padding * 2.0f), text_button_height };
    }
}

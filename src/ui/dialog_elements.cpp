#include "luil/ui/dialog_elements.h"

#include "luil/generated/codicons.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
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
    } // namespace

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
            const SkFont font { sk_ref_sp(context.ui_typeface), config_.title_font_size * scale };
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

        const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.body_font_size * scale };
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
        // 누름·끌기·더블 클릭은 interaction controller가 직접 다룬다.
        //  - 포인터 x를 offset으로 옮기려면 측정 함수가 필요한데 그것은 입력 thread가 가진다.
        //    여기서는 클릭을 흡수해 hit test 대상이 되게만 한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });

        if (config_.clear == nullptr)
            return;

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

    access_info text_button_element::accessibility() const
    {
        // 고른 값을 싣고 있으면 라디오 항목이다 — 그리는 모양이 글자 버튼일 뿐
        // 하는 일은 여럿 가운데 하나를 고르는 것이다 (`choice_group`의 토글 스타일).
        if (config_.selected.has_value())
            return { .role = access_role::radio_button, .name = config_.text, .selected = config_.selected };
        // link는 글 흐름 안의 자리라 역할도 갈린다. 이름은 둘 다 적힌 글이다.
        return { .role = config_.visual == text_button_visual::link ? access_role::link : access_role::button, .name = config_.text };
    }

    float text_input_element::leading_width_for(const text_input_config& config) noexcept
    {
        return config.leading_glyph != 0 ? text_input_glyph_size + text_input_icon_gap : 0.0f;
    }

    float text_input_element::trailing_width_for(const text_input_config& config) noexcept
    {
        return config.clear != nullptr ? text_input_clear_size + text_input_icon_gap : 0.0f;
    }

    float text_input_element::font_size() const noexcept
    {
        return 11.0f * scale_;
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

    std::u8string_view text_input_element::displayed_text() const noexcept
    {
        // 조합 중에는 조합 글이 섞인 쪽을 그린다.
        // 확정된 글은 그 동안 logic의 초안에 그대로 남아 있다.
        // 규칙은 view가 갖는다 — 앱이 검색에 쓰는 글과 그리는 글이 같아야 한다.
        return value_.displayed_text();
    }

    std::size_t text_input_element::displayed_caret() const noexcept
    {
        const std::u8string_view text { displayed_text() };
        const std::size_t caret { value_.composing ? value_.composition_caret : value_.caret };
        return caret < text.size() ? caret : text.size();
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

    void text_input_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        if (clear_ == nullptr)
            return;

        // 오른쪽 안쪽 가장자리에 붙인다. 글이 차지하는 폭은 inner_width가 이미 뺐다.
        const float size { text_input_clear_size * scale_ };
        const float inset { 4.0f * scale_ };
        clear_->arrange(context.for_child({ context.slot.x + context.slot.width - size - inset, context.slot.y + (context.slot.height - size) / 2.0f, size, size }));
    }

    std::optional<text_input_snapshot> text_input_element::text_input() const
    {
        return text_input_snapshot { value_.text, value_.caret, value_.anchor, value_.composing, value_.composition_text, value_.composition_caret, value_.applied_sequence };
    }

    std::optional<rect_f> text_input_element::text_span_bounds(const text_span_query& query, const text_measurer& measurer) const
    {
        if (measurer == nullptr)
            return std::nullopt;

        // 질의한 문서(조합 글 포함)를 그대로 잰다.
        //  - 확정 글로 재면 조합이 시작되는 순간 offset이 문서 길이를 넘어
        //    구간이 칸 왼쪽 끝으로 무너진다.
        // 스크롤도 같은 문서로 계산해 화면에 그려진 글자와 어긋나지 않는다.
        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        const std::u8string_view text { query.text };
        const std::size_t begin { query.begin < text.size() ? query.begin : text.size() };
        const std::size_t clamped_end { query.end < text.size() ? query.end : text.size() };
        const std::size_t end { clamped_end > begin ? clamped_end : begin };

        const float left { text_left() - scroll_for(measure, text, query.caret) };
        const float from { left + measure(text.substr(0, begin)) };
        const float to { left + measure(text.substr(0, end)) };
        return rect_f { from, bounds().y, to - from, bounds().height };
    }

    std::optional<std::size_t> text_input_element::offset_at(const float x, const text_measurer& measurer) const
    {
        // 조합 중에는 포인터로 caret을 옮기지 않는다.
        //  - 조합은 IME가 소유한다.
        const std::u8string_view text { value_.text };
        if (measurer == nullptr)
            return text.size();

        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        const float target { x - text_left() + scroll_for(measure) };
        if (target <= 0.0f)
            return std::size_t { 0 };

        // UTF-8 경계를 한 번 모은 뒤 폭을 이분 탐색한다. 긴 입력에서도
        // 모든 접두사를 반복 측정하지 않는다.
        std::vector<std::size_t> boundaries { 0 };
        for (std::size_t offset = 1; offset < text.size(); ++offset)
            if ((static_cast<unsigned int>(text[offset]) & 0xC0u) != 0x80u)
                boundaries.push_back(offset);
        if (text.empty() == false)
            boundaries.push_back(text.size());
        const auto after { std::upper_bound(boundaries.begin(), boundaries.end(), target, [&](const float position, const std::size_t offset) { return position < measure(text.substr(0, offset)); }) };
        if (after == boundaries.end())
            return text.size();
        if (after == boundaries.begin())
            return *after;
        const std::size_t before { *(after - 1) };
        const float after_width { measure(text.substr(0, *after)) };
        if (target - measure(text.substr(0, before)) < after_width - target)
            return before;
        // 폭이 없는 결합 문자도 포함해, 같은 거리에 있는 마지막 경계를 고른다.
        const auto narrower = [&](const float position, const std::size_t offset) { return position < measure(text.substr(0, offset)); };
        const auto last_equal { std::upper_bound(after + 1, boundaries.end(), after_width, narrower) };
        return *(last_equal - 1);
    }

    void text_input_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool focused { interaction.focused_input == id() };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { context.metrics.control_corner_radius * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.input_background));
        // 초점을 받은 칸은 테두리를 강조해 입력이 이곳으로 간다는 것을 보인다.
        SkPaint border { solid_paint(focused ? context.palette.accent : context.palette.input_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
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
        const float baseline { box.y + centered_text_baseline(font, box.height) };
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
        const float scroll { scroll_for(measure) };
        // 조합 중에는 선택 대신 조합 구간에 밑줄을 긋는다.
        //  - 그 동안 선택은 IME의 것이다.
        const std::size_t selection_begin { value_.composing ? 0u : (value_.caret < value_.anchor ? value_.caret : value_.anchor) };
        const std::size_t selection_end { value_.composing ? 0u : (value_.caret < value_.anchor ? value_.anchor : value_.caret) };
        const float caret_x { left - scroll + measure(text.substr(0, displayed_caret())) };

        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(left, box.y, visible, box.height));
        std::optional<SkRect> selection {};
        if (selection_begin != selection_end)
        {
            const float from { left - scroll + measure(text.substr(0, selection_begin)) };
            const float to { left - scroll + measure(text.substr(0, selection_end)) };
            selection = SkRect::MakeXYWH(from, box.y + 3.0f * scale, to - from, box.height - 6.0f * scale);
            context.canvas.drawRect(*selection, solid_paint(context.palette.selection_background));
        }
        if (text.empty() == false)
        {
            const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
            draw_text(context.canvas, text, left - scroll, baseline, font, foreground);
            // 고른 구간의 글자는 그 바탕의 짝이다. 옅은 바탕에서는 본문 글자와 같은 값이라
            // 겹쳐 그려도 같은 그림이고, 고대비에서만 highlight 글자로 바뀐다.
            if (selection.has_value() && enabled() && context.palette.selection_foreground != context.palette.primary_foreground)
            {
                context.canvas.save();
                context.canvas.clipRect(*selection);
                draw_text(context.canvas, text, left - scroll, baseline, font, solid_paint(context.palette.selection_foreground));
                context.canvas.restore();
            }
        }

        // 조합 구간은 밑줄로 표시한다.
        // 아직 확정되지 않은 글이라는 뜻이다.
        if (value_.composing && value_.composing_end > value_.composing_begin)
        {
            const std::size_t underline_begin { value_.composing_begin < text.size() ? value_.composing_begin : text.size() };
            const std::size_t underline_end { value_.composing_end < text.size() ? value_.composing_end : text.size() };
            const float from { left - scroll + measure(text.substr(0, underline_begin)) };
            const float to { left - scroll + measure(text.substr(0, underline_end)) };
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

    access_info text_input_element::accessibility() const
    {
        // 값은 확정 글이다 — 조합 글은 아직 사용자의 글이 아니라 싣지 않는다
        // (`text_input_view::text`가 같은 판단으로 조합 글을 밖에 둔다).
        return { .role = access_role::edit, .name = config_.placeholder, .value = value_.text };
    }
} // namespace luil

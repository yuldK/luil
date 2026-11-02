#include "ui/message_box_element.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "ui/severity_paint.h"

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 치수는 논리 픽셀이고 그릴 때 배율을 곱한다.
        // 안쪽 여백은 사방 같고, 단추 영역 앞뒤의 간격도 그 값이다.
        constexpr float padding { 20.0f };
        // 글리프 칸과 크기, 글리프와 글 사이다. 글리프는 `severity`가 있을 때만 서고 칸도 그때만 잡는다.
        constexpr float glyph_cell { 24.0f };
        constexpr float glyph_size { 20.0f };
        constexpr float glyph_gap { 12.0f };
        constexpr float title_body_gap { 8.0f };
        constexpr float check_gap { 12.0f };
        constexpr float button_gap { 8.0f };
        // 제목은 본문보다 이만큼 크다.
        constexpr float title_extra_size { 2.0f };
        // 배율을 곱했다 나눈 폭이 한 부동소수점 단위 어긋나도 "같으면 한 행"의 경계가 뒤집히지 않게 하는 여유다 (논리 픽셀).
        constexpr float fit_tolerance { 0.01f };

        // 0이거나 잘못된 값은 "정하지 않음"이다.
        [[nodiscard]] float control_size_of(const message_box_config& config) noexcept
        {
            return std::isfinite(config.control_size) && config.control_size > 0.0f ? config.control_size : 0.0f;
        }

        // 제목은 굵기가 스타일의 `title`이다. 줄을 바꾸고 위에서부터 쌓는다.
        [[nodiscard]] text_block_style title_style(const ui_metrics& metrics) noexcept
        {
            return text_block_style {
                .font = { .size = metrics.body_font_size + title_extra_size, .weight = metrics.font_weights.title },
                .vertical = text_vertical_align::top,
                .wrap = true,
            };
        }

        [[nodiscard]] text_block_style body_style(const ui_metrics& metrics, const std::size_t max_lines) noexcept
        {
            return text_block_style {
                .font = { .size = metrics.body_font_size, .weight = metrics.font_weights.body_text },
                .vertical = text_vertical_align::top,
                .wrap = true,
                .max_lines = max_lines,
            };
        }

        // 한 폭에서 상자의 높이와 부분의 자리를 정한 결과다 (논리 픽셀, 상자 왼쪽 위 기준). `preferred_size`와 arrange가 이 식 하나를 쓴다.
        struct box_plan
        {
            bool has_glyph { false };
            float width { 0.0f };
            float text_left { 0.0f };
            float text_width { 0.0f };
            text_block_layout title {};
            text_block_layout body {};
            std::size_t body_max_lines { 0 };
            float title_top { 0.0f };
            float body_top { 0.0f };
            float check_top { 0.0f };
            // 확인 칸이 차지하는 줄의 높이와 칸 자신의 높이다. 칸은 줄 안에서 세로 가운데에 선다.
            float check_height { 0.0f };
            float check_own_height { 0.0f };
            bool stacked { false };
            float buttons_top { 0.0f };
            float button_height { 0.0f };
            // 놓을 단추 폭이다. 쌓이면 모두 안쪽 폭이다.
            std::vector<float> button_widths {};
            float height { 0.0f };
        };

        [[nodiscard]] box_plan plan_box(const message_box_config& config, const measure_context& context, const std::span<const float> natural_widths, const float width, const float max_height)
        {
            const ui_metrics& metrics { context.style_metrics() };
            const float control { control_size_of(config) };

            box_plan plan {};
            plan.has_glyph = config.severity.has_value();
            plan.width = std::isfinite(width) ? std::max(0.0f, width) : 0.0f;
            const float inner_width { std::max(0.0f, plan.width - 2.0f * padding) };
            plan.text_left = padding + (plan.has_glyph ? glyph_cell + glyph_gap : 0.0f);
            plan.text_width = std::max(0.0f, plan.width - plan.text_left - padding);

            // 글이 설 폭이 없으면 재지 않는다. 한 글자 폭으로 접어 상자가 끝없이 길어지는 것을 막는다.
            const auto measure_body = [&](const std::size_t max_lines) {
                if (config.body.empty() || plan.text_width <= 0.0f)
                    return text_block_layout {};
                return measure_text_block(context, config.body, body_style(metrics, max_lines), plan.text_width);
            };
            if (config.title.empty() == false && plan.text_width > 0.0f)
                plan.title = measure_text_block(context, config.title, title_style(metrics), plan.text_width);
            plan.body = measure_body(0);

            // 단추: 폭의 합이 안쪽 폭을 넘으면 모두 안쪽 폭 전체로 쌓는다.
            plan.button_height = control > 0.0f ? control : text_button_height;
            plan.stacked = natural_widths.empty() == false && message_box_stacks_buttons(natural_widths, inner_width);
            if (plan.stacked)
                plan.button_widths.assign(natural_widths.size(), inner_width);
            else
                plan.button_widths.assign(natural_widths.begin(), natural_widths.end());
            const std::size_t button_count { plan.button_widths.size() };
            float button_area { 0.0f };
            if (button_count > 0u)
                button_area = plan.stacked ? static_cast<float>(button_count) * plan.button_height + static_cast<float>(button_count - 1u) * button_gap : plan.button_height;

            if (config.check.has_value())
            {
                plan.check_own_height = check_element::height_for(check_config { .label = config.check->label });
                plan.check_height = std::max(plan.check_own_height, control);
            }

            // 위에서 아래로 쌓는다. 머리는 글리프 칸과 글 높이 중 큰 쪽이다.
            const auto lay_out = [&] {
                const float title_height { plan.title.height };
                const float gap { title_height > 0.0f && plan.body.height > 0.0f ? title_body_gap : 0.0f };
                const float head { std::max(plan.has_glyph ? glyph_cell : 0.0f, title_height + gap + plan.body.height) };
                plan.title_top = padding;
                plan.body_top = padding + title_height + gap;
                float content_end { padding + head };
                if (config.check.has_value())
                {
                    plan.check_top = content_end + (head > 0.0f ? check_gap : 0.0f);
                    content_end = plan.check_top + plan.check_height;
                }
                plan.buttons_top = content_end + (button_count > 0u ? padding : 0.0f);
                plan.height = plan.buttons_top + button_area + padding;
            };
            lay_out();

            // 자리가 모자라면 본문의 줄 수를 줄이고 마지막 줄을 `…`로 줄인다. 제목, 확인 칸, 단추는 줄이지 않는다.
            //  - 한 줄로도 모자라면 아래가 넘쳐 그려진다. 본문 스크롤은 없다.
            if (std::isfinite(max_height) && plan.height > max_height + fit_tolerance && plan.body.lines.empty() == false)
            {
                const float line_height { text_block_line_height(body_style(metrics, 0)) };
                const std::size_t lines { plan.body.lines.size() };
                if (line_height > 0.0f)
                {
                    const std::size_t excess { static_cast<std::size_t>(std::ceil((plan.height - max_height) / line_height)) };
                    const std::size_t keep { excess >= lines ? 1u : lines - excess };
                    if (keep < lines)
                    {
                        plan.body_max_lines = keep;
                        plan.body = measure_body(keep);
                        lay_out();
                    }
                }
            }
            return plan;
        }
    }

    std::optional<std::size_t> message_box_default_index(const std::span<const message_box_button> buttons) noexcept
    {
        std::optional<std::size_t> found {};
        for (std::size_t index { 0 }; index < buttons.size(); ++index)
            if (buttons[index].role == message_box_role::accept)
                found = index;
        return found;
    }

    std::optional<std::size_t> message_box_entry_index(const std::span<const message_box_button> buttons) noexcept
    {
        if (const std::optional<std::size_t> accept { message_box_default_index(buttons) }; accept.has_value())
            return accept;
        std::optional<std::size_t> reject {};
        for (std::size_t index { 0 }; index < buttons.size(); ++index)
            if (buttons[index].role == message_box_role::reject)
                reject = index;
        if (reject.has_value())
            return reject;
        // 역할로 가를 수 없으면 첫 단추다. 단추가 없으면 서지 않는다.
        if (buttons.empty())
            return std::nullopt;
        return 0u;
    }

    std::optional<std::size_t> message_box_dismiss_index(const std::span<const message_box_button> buttons) noexcept
    {
        std::optional<std::size_t> reject {};
        for (std::size_t index { 0 }; index < buttons.size(); ++index)
            if (buttons[index].role == message_box_role::reject)
                reject = index;
        if (reject.has_value())
            return reject;
        // 거절이 없어도 단추가 하나면 그 단추가 닫는 길이다 (안내만 하는 대화의 확인).
        if (buttons.size() == 1u)
            return 0u;
        return std::nullopt;
    }

    bool message_box_stacks_buttons(const std::span<const float> widths, const float inner_width) noexcept
    {
        if (widths.empty())
            return false;
        float sum { static_cast<float>(widths.size() - 1u) * button_gap };
        for (const float width : widths)
            sum += width;
        return sum > inner_width + fit_tolerance;
    }

    message_box_element::message_box_element(message_box_config config)
        : ui_element { ui_element_id { ui_element_kind::message_box, config.owner } }
        , config_ { std::move(config) }
    {
        // 상자는 내용 위에 놓이는 표면이다. 단추가 없는 자리를 눌러도 뒤로 새지 않는다 (host도 세운다).
        set_hit_opaque(true);

        if (config_.check.has_value())
        {
            check_config check {};
            check.owner = message_box_check_id(config_.owner).owner;
            check.label = config_.check->label;
            check.checked = config_.check->checked;
            check.toggle = config_.check->toggle;
            auto element { std::make_unique<check_element>(std::move(check)) };
            check_ = element.get();
            add_child(std::move(element));
        }

        const std::optional<std::size_t> default_index { message_box_default_index(config_.buttons) };
        for (std::size_t index { 0 }; index < config_.buttons.size(); ++index)
        {
            const message_box_button& button { config_.buttons[index] };
            text_button_config look {};
            look.text = button.text;
            if (button.role == message_box_role::accept)
                look.visual = text_button_visual::accent;
            else if (button.role == message_box_role::destructive)
                look.visual = text_button_visual::danger;
            // 마지막 accept만 Enter의 대상이다. 채운 상자가 둘이면 어느 쪽이 Enter의 자리인지 사라진다.
            look.default_button = default_index.has_value() && *default_index == index;

            auto element { std::make_unique<text_button_element>(message_box_button_id(config_.owner, button.key), std::move(look)) };
            element->set_cursor(ui_cursor::hand);
            // 닫는 모든 계기가 같은 `result`를 부른다. 비면 눌러도 메시지가 없다.
            element->set_action(ui_trigger::left_click, [result = config_.result, key = button.key](const ui_action_context&) -> std::vector<input_action> {
                if (result == nullptr)
                    return {};
                return { result(key) };
            });
            buttons_.push_back(element.get());
            add_child(std::move(element));
        }
    }

    std::vector<float> message_box_element::button_widths(const measure_context& context) const
    {
        std::vector<float> widths {};
        widths.reserve(buttons_.size());
        for (const ui_element* const button : buttons_)
        {
            // 단추의 식을 옮겨 적지 않고 단추에게 묻는다.
            const std::optional<size_f> size { button->preferred_size(context, std::numeric_limits<float>::infinity()) };
            widths.push_back(std::max(message_box_button_minimum_width, size.has_value() ? size->width : 0.0f));
        }
        return widths;
    }

    void message_box_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const measure_context measure { context.measure() };
        const std::vector<float> natural { button_widths(measure) };
        // 높이가 정해지지 않은 자리(0)는 자르지 않는다. 자를 근거가 없다.
        const float max_height { context.slot.height > 0.0f ? context.slot.height / scale : std::numeric_limits<float>::infinity() };
        const box_plan plan { plan_box(config_, measure, natural, context.slot.width / scale, max_height) };

        placement_.text_left = plan.text_left;
        placement_.text_width = plan.text_width;
        placement_.title_top = plan.title_top;
        placement_.title_height = plan.title.height;
        placement_.body_top = plan.body_top;
        placement_.body_max_lines = plan.body_max_lines;
        placement_.title = plan.title;
        placement_.body = plan.body;

        const rect_f slot { context.slot };
        if (check_ != nullptr)
            check_->arrange(context.for_child({ slot.x + plan.text_left * scale, slot.y + (plan.check_top + (plan.check_height - plan.check_own_height) / 2.0f) * scale, plan.text_width * scale, plan.check_own_height * scale }));

        const float height { plan.button_height * scale };
        if (plan.stacked)
        {
            for (std::size_t index { 0 }; index < buttons_.size(); ++index)
            {
                const float top { plan.buttons_top + static_cast<float>(index) * (plan.button_height + button_gap) };
                buttons_[index]->arrange(context.for_child({ slot.x + padding * scale, slot.y + top * scale, plan.button_widths[index] * scale, height }));
            }
            return;
        }

        // 한 행은 오른쪽 끝에 붙인다.
        float total { static_cast<float>(buttons_.size() > 0u ? buttons_.size() - 1u : 0u) * button_gap };
        for (const float width : plan.button_widths)
            total += width;
        float left { plan.width - padding - total };
        for (std::size_t index { 0 }; index < buttons_.size(); ++index)
        {
            buttons_[index]->arrange(context.for_child({ slot.x + left * scale, slot.y + plan.buttons_top * scale, plan.button_widths[index] * scale, height }));
            left += plan.button_widths[index] + button_gap;
        }
    }

    void message_box_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;

        // 글리프는 늘 그린다. 색만으로 뜻을 말하지 않는다 (고대비는 info·warning·error의 색을 접는다).
        if (config_.severity.has_value() && context.codicon_typeface != nullptr)
        {
            const ui_severity severity { *config_.severity };
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph_size * scale };
            draw_centered_glyph(context.canvas, severity_glyph(severity), { box.x + padding * scale, box.y + padding * scale, glyph_cell * scale, glyph_cell * scale }, glyph_font,
                solid_paint(severity_color(context.palette, severity)));
        }

        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        const float text_left { box.x + placement_.text_left * scale };
        const float text_width { placement_.text_width * scale };
        if (text_width > 0.0f)
        {
            if (placement_.title.lines.empty() == false)
            {
                const rect_f title_box { text_left, box.y + placement_.title_top * scale, text_width, placement_.title.height * scale };
                static_cast<void>(draw_text_block(context, config_.title, title_box, title_style(context.metrics), foreground, placement_.title));
            }
            if (placement_.body.lines.empty() == false)
            {
                const rect_f body_box { text_left, box.y + placement_.body_top * scale, text_width, placement_.body.height * scale };
                static_cast<void>(draw_text_block(context, config_.body, body_box, body_style(context.metrics, placement_.body_max_lines), foreground, placement_.body));
            }
        }

        draw_children(context, interaction);
    }

    access_info message_box_element::accessibility() const
    {
        if (config_.body.empty())
            return { .role = access_role::none };
        return { .role = access_role::static_text, .name = config_.body };
    }

    std::optional<size_f> message_box_element::preferred_size(const measure_context& context, const float available_width) const
    {
        // 줄바꿈과 쌓임은 폭이 있어야 높이가 나온다.
        if (std::isfinite(available_width) == false)
            return std::nullopt;
        const std::vector<float> natural { button_widths(context) };
        return size_f { available_width, plan_box(config_, context, natural, available_width, std::numeric_limits<float>::infinity()).height };
    }
}

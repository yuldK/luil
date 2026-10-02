#include "luil/ui/zoom_view_element.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/label_element.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] bool finite_point(const zoom_point point) noexcept
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

        [[nodiscard]] bool valid_config(const zoom_view_config& config) noexcept
        {
            if (std::isfinite(config.zoom) == false || config.zoom <= 0.0f || finite_point(config.origin) == false || std::isfinite(config.minimum_zoom) == false || config.minimum_zoom <= 0.0f
                || std::isfinite(config.maximum_zoom) == false || config.maximum_zoom < config.minimum_zoom)
                return false;
            if (config.content_bounds.has_value() == false)
                return true;
            const rect_f& box { *config.content_bounds };
            return finite_point({ box.x, box.y }) && std::isfinite(box.width) && std::isfinite(box.height) && box.width > 0.0f && box.height > 0.0f && std::isfinite(box.x + box.width)
                && std::isfinite(box.y + box.height);
        }

        [[nodiscard]] zoom_view_state clamp_state(const zoom_view_config& config, const zoom_point viewport, zoom_view_state value) noexcept
        {
            if (valid_config(config) == false)
                return {};
            value.zoom = std::clamp(value.zoom, config.minimum_zoom, config.maximum_zoom);
            if (finite_point(value.origin) == false)
                value.origin = config.origin;
            if (config.content_bounds.has_value() == false || finite_point(viewport) == false || viewport.x <= 0.0f || viewport.y <= 0.0f)
                return value;
            const rect_f& box { *config.content_bounds };
            const auto clamp_axis = [zoom = value.zoom](const float origin, const float extent, const float start, const float length) {
                // 1 논리 픽셀을 남긴다. 아주 작은 내용·창에서는 그 작은 쪽 전부다.
                const double overlap { std::min({ 1.0, static_cast<double>(extent), static_cast<double>(length) * zoom }) };
                const double low { -extent / 2.0 - (static_cast<double>(start) + length) * zoom + overlap };
                const double high { extent / 2.0 - static_cast<double>(start) * zoom - overlap };
                return static_cast<float>(std::clamp(static_cast<double>(origin), low, high));
            };
            value.origin.x = clamp_axis(value.origin.x, viewport.x, box.x, box.width);
            value.origin.y = clamp_axis(value.origin.y, viewport.y, box.y, box.height);
            return value;
        }

        [[nodiscard]] std::vector<input_action> fit_actions(const zoom_view_config& config, const zoom_point viewport, const float margin = 16.0f, const float maximum = 1.0f)
        {
            if (config.content_bounds.has_value() == false || config.zoom_by == nullptr || config.pan_by == nullptr)
                return {};
            const zoom_view_state current { zoom_about(config, viewport, 1.0f, {}) };
            const zoom_view_state next { zoom_to_fit(config, viewport, margin, maximum) };
            // 먼저 중앙에서 확대하면 원점도 비율만큼 움직인다. 이동은 그 뒤 원점과의 차이다.
            const float ratio { next.zoom / current.zoom };
            std::vector<input_action> result {};
            if (ratio != 1.0f)
                result.push_back(config.zoom_by(ratio, {}));
            // 확대의 경계 다듬기도 같은 식으로 반영한다.
            const zoom_view_state scaled { zoom_about(config, viewport, ratio, {}) };
            const zoom_point delta { next.origin.x - scaled.origin.x, next.origin.y - scaled.origin.y };
            if (delta != zoom_point {})
                result.push_back(config.pan_by(delta));
            return result;
        }
    } // namespace

    zoom_view_state zoom_about(const zoom_view_config& config, const zoom_point viewport, const float factor, const zoom_point anchor) noexcept
    {
        zoom_view_state current { clamp_state(config, viewport, { config.zoom, config.origin }) };
        if (valid_config(config) == false || std::isfinite(factor) == false || factor <= 0.0f || finite_point(anchor) == false)
            return current;
        const float next { static_cast<float>(std::clamp(static_cast<double>(current.zoom) * factor, static_cast<double>(config.minimum_zoom), static_cast<double>(config.maximum_zoom))) };
        if (next == current.zoom)
            return current;
        const double ratio { static_cast<double>(next) / current.zoom };
        const zoom_point origin {
            static_cast<float>(anchor.x - (anchor.x - static_cast<double>(current.origin.x)) * ratio),
            static_cast<float>(anchor.y - (anchor.y - static_cast<double>(current.origin.y)) * ratio),
        };
        return clamp_state(config, viewport, { next, origin });
    }

    zoom_view_state pan_by(const zoom_view_config& config, const zoom_point viewport, const zoom_point delta) noexcept
    {
        zoom_view_state current { clamp_state(config, viewport, { config.zoom, config.origin }) };
        if (finite_point(delta) == false)
            return current;
        current.origin.x += delta.x;
        current.origin.y += delta.y;
        return clamp_state(config, viewport, current);
    }

    zoom_view_state zoom_to_fit(const zoom_view_config& config, const zoom_point viewport, const float margin, const float maximum_fit_zoom) noexcept
    {
        const zoom_view_state current { clamp_state(config, viewport, { config.zoom, config.origin }) };
        if (valid_config(config) == false || config.content_bounds.has_value() == false || finite_point(viewport) == false || std::isfinite(margin) == false || margin < 0.0f
            || std::isfinite(maximum_fit_zoom) == false || maximum_fit_zoom <= 0.0f || viewport.x <= margin * 2.0f || viewport.y <= margin * 2.0f)
            return current;
        const rect_f& box { *config.content_bounds };
        const float zoom { std::clamp(std::min({ (viewport.x - margin * 2.0f) / box.width, (viewport.y - margin * 2.0f) / box.height, maximum_fit_zoom }), config.minimum_zoom, config.maximum_zoom) };
        return clamp_state(config, viewport, { zoom, { -(box.x + box.width / 2.0f) * zoom, -(box.y + box.height / 2.0f) * zoom } });
    }

    zoom_view_element::zoom_view_element(zoom_view_config config)
        : ui_element { { ui_element_kind::zoom_view, config.owner } }
        , config_ { std::move(config) }
    {
        if (valid_config(config_) == false)
            throw std::invalid_argument { "Invalid zoom view configuration" };
        set_clip_children(true);
        set_tab_stop(true);
        set_hit_opaque(true);
        set_zoom_source(zoom_source { config_.zoom_by, config_.pan_by, 1.0f, config_.wheel, [this](const key_pressed_event& event) { return keys(event); } });
    }

    void zoom_view_element::set_content(std::unique_ptr<ui_element> content)
    {
        if (content_ != nullptr)
            throw std::logic_error { "Zoom view accepts one content element" };
        if (content == nullptr)
            return;
        content_ = content.get();
        add_child(std::move(content));
    }

    void zoom_view_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = std::isfinite(context.scale) && context.scale > 0.0f ? context.scale : 1.0f;
        const zoom_point viewport { bounds().width / scale_, bounds().height / scale_ };
        const zoom_view_state value { zoom_about(config_, viewport, 1.0f, {}) };
        config_.zoom = value.zoom;
        config_.origin = value.origin;
        set_zoom_source(zoom_source { config_.zoom_by, config_.pan_by, scale_, config_.wheel, [this](const key_pressed_event& event) { return keys(event); } });
        if (content_ == nullptr)
            return;
        const rect_f content { config_.content_bounds.value_or(rect_f { 0.0f, 0.0f, viewport.x / value.zoom, viewport.y / value.zoom }) };
        const float scale { scale_ * value.zoom };
        const rect_f slot {
            bounds().x + bounds().width / 2.0f + value.origin.x * scale_ + content.x * scale,
            bounds().y + bounds().height / 2.0f + value.origin.y * scale_ + content.y * scale,
            content.width * scale,
            content.height * scale,
        };
        arrange_context child { context.for_child(slot) };
        child.scale = scale;
        child.scroll_offset = 0.0f;
        content_->arrange(child);
    }

    void zoom_view_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_context child {
            .canvas = context.canvas,
            .codicon_typeface = context.codicon_typeface,
            .ui_typeface = context.ui_typeface,
            .code_typeface = context.code_typeface,
            .fonts = context.fonts,
            .palette = context.palette,
            .scale = scale_ * config_.zoom,
            .now = context.now,
            .maximized = context.maximized,
            .fullscreen = context.fullscreen,
            .metrics = context.metrics,
        };
        draw_children(child, interaction);
    }

    zoom_view_state zoom_view_element::state() const noexcept
    {
        return { config_.zoom, config_.origin };
    }

    access_info zoom_view_element::accessibility() const
    {
        return { .role = access_role::pane, .name = access_name(), .range = access_range { config_.minimum_zoom, config_.maximum_zoom, config_.zoom } };
    }

    std::optional<std::vector<input_action>> zoom_view_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::set_value || config_.zoom_to == nullptr || std::isfinite(request.value) == false)
            return std::nullopt;
        const float value { std::clamp(request.value, config_.minimum_zoom, config_.maximum_zoom) };
        // 절대 명령은 낡은 frame에서도 같은 목표를 보낸다.
        return std::vector<input_action> { config_.zoom_to(value) };
    }

    std::optional<std::vector<input_action>> zoom_view_element::keys(const key_pressed_event& event) const
    {
        if (event.shortcut_modifier_down())
            return std::nullopt;
        if (event.key == key_code::zoom_in || event.key == key_code::zoom_out)
            return config_.zoom_by ? std::vector<input_action> { config_.zoom_by(event.key == key_code::zoom_in ? 1.2f : 1.0f / 1.2f, {}) } : std::vector<input_action> {};
        const zoom_point viewport { bounds().width / scale_, bounds().height / scale_ };
        if (event.key == key_code::key_0)
        {
            if (config_.content_bounds.has_value())
                return fit_actions(config_, viewport);
            return config_.zoom_by ? std::vector<input_action> { config_.zoom_by(1.0f / config_.zoom, {}) } : std::vector<input_action> {};
        }
        zoom_point delta {};
        switch (event.key)
        {
        case key_code::arrow_left:
            delta.x = 32.0f;
            break;
        case key_code::arrow_right:
            delta.x = -32.0f;
            break;
        case key_code::arrow_up:
            delta.y = 32.0f;
            break;
        case key_code::arrow_down:
            delta.y = -32.0f;
            break;
        case key_code::page_up:
            delta.y = viewport.y * 0.9f;
            break;
        case key_code::page_down:
            delta.y = -viewport.y * 0.9f;
            break;
        default:
            return std::nullopt;
        }
        return config_.pan_by ? std::vector<input_action> { config_.pan_by(delta) } : std::vector<input_action> {};
    }

    zoom_controls_element::zoom_controls_element(zoom_controls_config config)
        : ui_element { { ui_element_kind::zoom_controls, config.view.owner } }
    {
        if (valid_config(config.view) == false)
            throw std::invalid_argument { "Invalid zoom controls configuration" };
        set_clip_children(true);
        const auto button = [this, &config](const ui_element_kind kind, const std::u8string text, ui_action action, const bool enabled) {
            auto element { std::make_unique<text_button_element>(ui_element_id { kind, config.view.owner }, text_button_config { .text = text }) };
            element->set_access_name(text);
            element->set_action(ui_trigger::left_click, std::move(action));
            element->set_enabled(enabled);
            add_child(std::move(element));
        };
        const auto zoom_action = [change = config.view.zoom_by](const float factor) -> ui_action {
            if (change == nullptr)
                return {};
            return [change, factor](const ui_action_context&) -> std::vector<input_action> { return { change(factor, {}) }; };
        };
        button(ui_element_kind::zoom_decrease, u8"축소", zoom_action(1.0f / 1.2f), config.view.zoom_by != nullptr && config.view.zoom > config.view.minimum_zoom);
        std::string percent { std::to_string(std::round(static_cast<double>(std::clamp(config.view.zoom, config.view.minimum_zoom, config.view.maximum_zoom)) * 100.0)) };
        percent.resize(percent.find('.'));
        percent += "%";
        add_child(std::make_unique<label_element>(ui_element_id { ui_element_kind::zoom_label, config.view.owner }, label_config { .text = std::u8string { percent.begin(), percent.end() } }));
        button(ui_element_kind::zoom_increase, u8"확대", zoom_action(1.2f), config.view.zoom_by != nullptr && config.view.zoom < config.view.maximum_zoom);
        if (config.view.content_bounds.has_value())
            button(
                ui_element_kind::zoom_fit, u8"맞춤", [config](const ui_action_context&) { return fit_actions(config.view, config.viewport, config.fit_margin, config.maximum_fit_zoom); },
                config.view.zoom_by != nullptr && config.view.pan_by != nullptr);
    }

    void zoom_controls_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { std::isfinite(context.scale) && context.scale > 0.0f ? context.scale : 1.0f };
        const float gaps { static_cast<float>(children().size() - 1u) };
        const float natural { (64.0f + gaps * 60.0f) * scale };
        const float fit { std::clamp(context.slot.width / natural, 0.0f, 1.0f) };
        float x { context.slot.x };
        for (const std::unique_ptr<ui_element>& child : children())
        {
            const float width { (child->id().kind == ui_element_kind::zoom_label ? 64.0f : 56.0f) * scale * fit };
            child->arrange(context.for_child({ x, context.slot.y, width, context.slot.height }));
            x += width + 4.0f * scale * fit;
        }
    }

    void zoom_controls_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
} // namespace luil

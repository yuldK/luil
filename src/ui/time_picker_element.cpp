#include "luil/ui/time_picker_element.h"

#include "luil/ui/label_element.h"
#include "luil/ui/number_input.h"
#include "luil/ui/stack_element.h"
#include "ui/default_text.h"

#include <utility>

namespace luil {
    std::optional<time_of_day> read_time_picker(const std::u8string_view hour, const std::u8string_view minute, const bool twelve_hour, const bool pm)
    {
        const auto hours { check_number_input(hour, { .minimum = twelve_hour ? 1.0 : 0.0, .maximum = twelve_hour ? 12.0 : 23.0 }) };
        const auto minutes { check_number_input(minute, { .minimum = 0.0, .maximum = 59.0 }) };
        if (hours.valid() == false || minutes.valid() == false)
            return std::nullopt;
        const int value { static_cast<int>(*hours.value) };
        return time_of_day { twelve_hour ? value % 12 + (pm ? 12 : 0) : value, static_cast<int>(*minutes.value) };
    }

    std::u8string format_time_part(const int value)
    {
        if (value < 0 || value > 59)
            return {};
        return { static_cast<char8_t>(u8'0' + value / 10), static_cast<char8_t>(u8'0' + value % 10) };
    }

    time_picker_element::time_picker_element(time_picker_config config)
        : ui_element { { ui_element_kind::time_picker, config.owner } }
        , twelve_hour_ { config.twelve_hour }
    {
        if (config.hour_name.empty()) config.hour_name = default_text::time_hour();
        if (config.minute_name.empty()) config.minute_name = default_text::time_minute();
        if (config.am_label.empty()) config.am_label = default_text::time_am();
        if (config.pm_label.empty()) config.pm_label = default_text::time_pm();
        auto row { std::make_unique<stack_element>(ui_element_id {}, stack_config { .direction = stack_direction::row, .spacing = 4.0f }) };
        const auto field = [&](const bool hour) {
            text_input_config input {};
            input.invalid = config.invalid;
            text_input_number number {};
            number.range = { .minimum = hour && config.twelve_hour ? 1.0 : 0.0, .maximum = hour ? (config.twelve_hour ? 12.0 : 23.0) : 59.0 };
            number.spin_buttons = true;
            number.increase_tooltip = hour ? default_text::time_hour_increase() : default_text::time_minute_increase();
            number.decrease_tooltip = hour ? default_text::time_hour_decrease() : default_text::time_minute_decrease();
            const auto change { hour ? config.hour_to : config.minute_to };
            if (change != nullptr)
                number.change_to = [change, range = number.range](std::u8string text) {
                    const auto checked { check_number_input(text, range) };
                    return change(checked.valid() ? format_time_part(static_cast<int>(*checked.value)) : std::move(text));
                };
            input.number = std::move(number);
            auto element { std::make_unique<text_input_element>(ui_element_id { hour ? ui_element_kind::time_picker_hour : ui_element_kind::time_picker_minute, config.owner }, hour ? std::move(config.hour) : std::move(config.minute), std::move(input)) };
            element->set_access_name(hour ? config.hour_name : config.minute_name);
            return element;
        };
        row->add(field(true), 64.0f);
        row->add(std::make_unique<label_element>(ui_element_id {}, label_config { .text = u8":" }), 12.0f);
        row->add(field(false), 64.0f);
        if (config.twelve_hour)
        {
            auto period { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::time_picker_period, config.owner }, text_button_config { .text = config.pm ? config.pm_label : config.am_label }) };
            period->set_action(ui_trigger::left_click, std::move(config.toggle_period));
            row->add(std::move(period), 64.0f);
        }
        row_ = row.get();
        add_child(std::move(row));
    }

    void time_picker_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        row_->arrange(context);
    }

    void time_picker_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    std::optional<size_f> time_picker_element::preferred_size(const measure_context&, float) const
    {
        return size_f { twelve_hour_ ? 216.0f : 148.0f, 28.0f };
    }
}

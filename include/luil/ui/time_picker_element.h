#pragma once

#include "luil/ui/dialog_elements.h"

namespace luil {
    struct time_of_day
    {
        int hour { 0 };
        int minute { 0 };
        [[nodiscard]] bool operator==(const time_of_day&) const = default;
    };

    // 24시간제 값이다. 빈 글·범위 밖·소수는 nullopt다. 12시간제의 12 AM은 0시, 12 PM은 12시다.
    [[nodiscard]] std::optional<time_of_day> read_time_picker(std::u8string_view hour, std::u8string_view minute, bool twelve_hour = false, bool pm = false);
    [[nodiscard]] std::u8string format_time_part(int value);

    struct time_picker_config
    {
        std::u8string owner {};
        text_input_view hour {};
        text_input_view minute {};
        bool twelve_hour { false };
        bool pm { false };
        bool invalid { false };
        std::function<input_action(std::u8string)> hour_to {};
        std::function<input_action(std::u8string)> minute_to {};
        ui_action toggle_period {};
        std::u8string hour_name {};
        std::u8string minute_name {};
        std::u8string am_label {};
        std::u8string pm_label {};
    };

    // 두 숫자 칸은 time_picker_hour/minute + owner다. 타자·IME는 일반 text_input과 같은 편집 경로로 보낸다.
    class time_picker_element final : public ui_element
    {
    public:
        explicit time_picker_element(time_picker_config config);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        ui_element* row_ { nullptr };
        bool twelve_hour_ { false };
    };
}

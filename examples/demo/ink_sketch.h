#pragma once

// 두 데모의 잉크 페이지가 함께 쓰는 상태다 (ink-design.md의 데모 절). 획은 앱 상태이고 캔버스는 그 획을 그리며
// 메시지를 낸다. 화면 배치만 데모마다 다르다.

#include "luil/ui/app_message.h"
#include "luil/ui/ink_canvas_element.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace demo {
    // 캔버스가 낸 획 입력이다. `ink-sample` 단추도 같은 메시지를 낸다.
    struct ink_stroke_intent
    {
        luil::ink_stroke_input input {};
    };

    // 지우개가 닿은 획 id다.
    struct ink_erase_intent
    {
        std::vector<std::uint64_t> ids {};
    };

    enum class ink_command
    {
        toggle_tool,
        clear,
        toggle_mouse,
        toggle_touch,
    };

    struct ink_command_intent
    {
        ink_command command { ink_command::toggle_tool };
    };

    class ink_sketch
    {
    public:
        bool handle(const luil::app_message& message)
        {
            if (const auto* const stroke { message.get<ink_stroke_intent>() }; stroke != nullptr)
                apply(stroke->input);
            else if (const auto* const erase { message.get<ink_erase_intent>() }; erase != nullptr)
            {
                // 같은 id가 앱이 지우기 전에 다시 온다. 실제로 지운 수만 센다.
                const std::size_t before { layer_.size() };
                layer_ = layer_.without(erase->ids);
                if (layer_.size() < before)
                    last_ = u8"지운 획 " + number(before - layer_.size());
            }
            else if (const auto* const command { message.get<ink_command_intent>() }; command != nullptr)
            {
                if (command->command == ink_command::toggle_tool)
                    tool_ = tool_ == luil::ink_tool::pen ? luil::ink_tool::eraser : luil::ink_tool::pen;
                else if (command->command == ink_command::clear)
                {
                    layer_ = {};
                    points_.clear();
                    last_ = u8"마지막 획: 없음";
                }
                else if (command->command == ink_command::toggle_mouse)
                    mouse_ = mouse_ == false;
                else
                    touch_ = touch_ == false;
            }
            else
                return false;
            return true;
        }

        // 상태 글이다. 화면의 label과 UIA Name이 같은 글이다.
        [[nodiscard]] std::u8string count_text() const
        {
            return u8"획 " + number(layer_.size()) + u8"개";
        }

        [[nodiscard]] std::u8string tool_text() const
        {
            return tool_ == luil::ink_tool::pen ? u8"도구: 펜" : u8"도구: 지우개";
        }

        [[nodiscard]] const std::u8string& last_text() const noexcept
        {
            return last_;
        }

        [[nodiscard]] bool mouse() const noexcept
        {
            return mouse_;
        }

        [[nodiscard]] bool touch() const noexcept
        {
            return touch_;
        }

        [[nodiscard]] std::size_t stroke_count() const noexcept
        {
            return layer_.size();
        }

        // 캔버스 설정이다. 캔버스의 Value(획 수 요약)는 `ink-count`와 같은 글이다.
        [[nodiscard]] luil::ink_canvas_config canvas_config() const
        {
            return {
                .owner = u8"ink-canvas",
                .strokes = layer_,
                .wet = luil::make_ink_stroke(0, points_, style()),
                .tool = tool_,
                .mouse = mouse_,
                .touch = touch_,
                .eraser_radius = 10.0f,
                .on_stroke = [](const luil::ink_stroke_input& input) { return luil::make_app_action(ink_stroke_intent { input }); },
                .on_erase = [](std::vector<std::uint64_t> ids) { return luil::make_app_action(ink_erase_intent { std::move(ids) }); },
                .access_name = u8"잉크 캔버스",
                .access_summary = [](const std::size_t strokes) { return u8"획 " + number(strokes) + u8"개"; },
            };
        }

        // 정해 둔 물결 획 하나를 캔버스가 내는 것과 같은 메시지로 낸다 (`ink-sample`). 펜이 압력을 실은 획이다.
        //  - UIA는 그리기를 만들지 못하므로 이 단추가 획 수와 요약 값을 되읽는 길이다.
        [[nodiscard]] std::vector<luil::input_action> sample_actions() const
        {
            std::vector<luil::input_action> actions {};
            const float shift { static_cast<float>(layer_.size() % 6) * 24.0f };
            constexpr int count { 16 };
            for (int index { 0 }; index < count; ++index)
            {
                const float along { static_cast<float>(index) / static_cast<float>(count - 1) };
                luil::ink_stroke_input input {};
                input.phase = index == 0 ? luil::ink_phase::begin : luil::ink_phase::extend;
                input.device = luil::pointer_device::pen;
                input.point = { 24.0f + along * 220.0f, 40.0f + shift + std::sin(along * 6.2831853f) * 16.0f, 0.2f + along * 0.8f };
                input.pressure_reported = true;
                actions.push_back(luil::make_app_action(ink_stroke_intent { input }));
                if (index + 1 == count)
                {
                    input.phase = luil::ink_phase::end;
                    actions.push_back(luil::make_app_action(ink_stroke_intent { input }));
                }
            }
            return actions;
        }

    private:
        [[nodiscard]] static std::u8string number(const std::size_t value)
        {
            const std::string text { std::to_string(value) };
            return { text.begin(), text.end() };
        }

        [[nodiscard]] static luil::ink_style style()
        {
            return { .width = 4.0f, .minimum_ratio = 0.25f };
        }

        [[nodiscard]] static std::u8string device_name(const luil::pointer_device device)
        {
            switch (device)
            {
            case luil::pointer_device::mouse:
                return u8"마우스";
            case luil::pointer_device::touch:
                return u8"손가락";
            case luil::pointer_device::pen:
                return u8"펜";
            }
            return u8"펜";
        }

        // 점을 모으다가 끝에 굳힌다. 끊긴 끝(`interrupted`)도 받은 점까지가 획이다.
        void apply(const luil::ink_stroke_input& input)
        {
            if (input.phase == luil::ink_phase::begin)
            {
                points_ = { input.point };
                device_ = input.device;
                pressure_ = input.pressure_reported;
            }
            else if (input.phase == luil::ink_phase::extend)
                points_.push_back(input.point);
            else if (input.phase == luil::ink_phase::end)
            {
                if (points_.empty())
                    return;
                layer_ = layer_.with(luil::make_ink_stroke(next_id_++, luil::simplify_ink_points(std::exchange(points_, {}), 0.25f, 0.05f), style()));
                last_ = u8"마지막 획: " + device_name(device_) + (pressure_ ? u8", 압력 있음" : u8", 압력 없음");
            }
            else
                points_.clear();
        }

        luil::ink_layer layer_ {};
        std::vector<luil::ink_point> points_ {};
        std::uint64_t next_id_ { 1 };
        luil::ink_tool tool_ { luil::ink_tool::pen };
        bool mouse_ { false };
        bool touch_ { false };
        luil::pointer_device device_ { luil::pointer_device::pen };
        bool pressure_ { false };
        std::u8string last_ { u8"마지막 획: 없음" };
    };
}

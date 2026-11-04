#include "luil/ui/color_picker_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/field_popup.h"
#include "luil/ui/text_block.h"
#include "ui/default_text.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace luil {
    namespace {
        // 판 손잡이 원의 반지름이다 (논리 픽셀).
        constexpr float plane_handle_radius { 7.0f };
        // 막대 손잡이의 폭이다 (논리 픽셀).
        constexpr float bar_handle_width { 6.0f };
        // 손잡이 안쪽 테와 바깥 테의 굵기다 (논리 픽셀).
        constexpr float handle_inner_width { 2.0f };
        constexpr float handle_outer_width { 1.0f };
        // 투명을 보이는 체커 한 칸이다 (논리 픽셀).
        constexpr float checker_size { 4.0f };
        constexpr ui_color checker_light { make_ui_color(255, 255, 255) };
        constexpr ui_color checker_dark { make_ui_color(0xCC, 0xCC, 0xCC) };
        // 비활성의 내용 불투명도다.
        constexpr float disabled_content_alpha { 0.4f };
        // 색 칸 안 견본의 한 변과 왼쪽 여백이다 (논리 픽셀).
        constexpr float field_swatch_size { 18.0f };
        constexpr float field_inset { 6.0f };
        // 색 칸의 펼침 화살표 자리 폭이다 (논리 픽셀). 드롭다운과 같다.
        constexpr float field_arrow_width { 18.0f };
        // 걸음 격자에 놓였다고 보는 오차다 (걸음에 대한 비율). 부동소수 오차가 한 걸음을 건너뛰게 하지 않는다.
        constexpr float grid_tolerance { 0.001f };
        // 화살표 한 번과 Page 한 번의 걸음 수다. 채도·명도·알파는 1%, 색상은 1도가 한 걸음이다.
        constexpr float page_steps { 10.0f };

        [[nodiscard]] std::u8string part_owner(const std::u8string& owner, const std::u8string_view part)
        {
            std::u8string result { owner };
            result += u8'\u001f';
            result += part;
            return result;
        }

        [[nodiscard]] std::u8string preset_owner(const std::u8string& owner, const std::size_t number)
        {
            std::u8string digits {};
            for (const char digit : std::to_string(number))
                digits += static_cast<char8_t>(digit);
            return part_owner(owner, digits);
        }

        [[nodiscard]] float unit_of(const float value) noexcept
        {
            // NaN은 "0보다 크다"가 거짓이라 0이다.
            return value > 0.0f ? std::min(value, 1.0f) : 0.0f;
        }

        [[nodiscard]] float plane_size_of(const color_picker_config& config) noexcept
        {
            return config.plane_size > 0.0f && std::isfinite(config.plane_size) ? config.plane_size : color_picker_plane_size;
        }

        [[nodiscard]] float bar_height_of(const color_picker_config& config) noexcept
        {
            return config.bar_height > 0.0f && std::isfinite(config.bar_height) ? config.bar_height : color_picker_bar_height;
        }

        // 폭 `width`에 한 줄로 드는 견본 칸 수다. 같은 식을 논리 크기(`size_for`)와 배치가 함께 쓴다.
        [[nodiscard]] std::size_t presets_per_row(const float width, const float cell, const float gap) noexcept
        {
            if (cell <= 0.0f || (width > 0.0f) == false)
                return 1u;
            const float fit { std::floor((width + gap) / (cell + gap)) };
            return fit >= 1.0f ? static_cast<std::size_t>(fit) : 1u;
        }

        [[nodiscard]] float presets_height(const std::size_t count, const float width, const float cell, const float gap) noexcept
        {
            if (count == 0u)
                return 0.0f;
            const std::size_t per_row { presets_per_row(width, cell, gap) };
            const std::size_t rows { (count + per_row - 1u) / per_row };
            return static_cast<float>(rows) * cell + static_cast<float>(rows - 1u) * gap;
        }

        [[nodiscard]] SkRect sk_rect(const rect_f& box) noexcept
        {
            return SkRect::MakeXYWH(box.x, box.y, box.width, box.height);
        }

        [[nodiscard]] SkColor4f sk_color(const ui_color color) noexcept
        {
            return SkColor4f::FromColor(static_cast<SkColor>(color));
        }

        // 가로 선형 그라디언트로 `box`를 칠한다. 정지점은 고르게 놓는다. 보간은 그리는 표면의 공간에서 미리 곱하지 않은 값이다.
        void fill_gradient(SkCanvas& canvas, const rect_f& box, const SkPoint (&points)[2], const std::span<const SkColor4f> colors)
        {
            SkPaint paint {};
            paint.setShader(SkShaders::LinearGradient(points, SkGradient { SkGradient::Colors { SkSpan<const SkColor4f> { colors.data(), colors.size() }, SkTileMode::kClamp }, SkGradient::Interpolation {} }));
            canvas.drawRect(sk_rect(box), paint);
        }

        // 투명을 보이는 체커다. 흰색 바탕에 회색 칸을 엇갈려 놓는다.
        void draw_checker(SkCanvas& canvas, const rect_f& box, const float scale)
        {
            const float size { checker_size * scale };
            if (size <= 0.0f || box.width <= 0.0f || box.height <= 0.0f)
                return;
            const SkAutoCanvasRestore restore { &canvas, true };
            canvas.clipRect(sk_rect(box));
            canvas.drawRect(sk_rect(box), solid_paint(checker_light));
            const SkPaint dark { solid_paint(checker_dark) };
            int row { 0 };
            for (float y { box.y }; y < box.y + box.height; y += size, ++row)
            {
                int column { 0 };
                for (float x { box.x }; x < box.x + box.width; x += size, ++column)
                    if ((row + column) % 2 == 1)
                        canvas.drawRect(SkRect::MakeXYWH(x, y, size, size), dark);
            }
        }

        // 1 논리 픽셀 둘레다. 안쪽으로 그어 칸 밖으로 번지지 않는다.
        void draw_outline(SkCanvas& canvas, const rect_f& box, const ui_color color, const float scale)
        {
            SkPaint line { solid_paint(color) };
            line.setStyle(SkPaint::kStroke_Style);
            line.setStrokeWidth(scale);
            canvas.drawRect(SkRect::MakeXYWH(box.x + scale / 2.0f, box.y + scale / 2.0f, box.width - scale, box.height - scale), line);
        }

        // 손잡이 테의 두 색이다. 안쪽은 아래 색이 어두우면 흰색, 밝으면 검정이고 바깥은 그 반대다 (강조색 견본의 체크 판정).
        struct handle_colors
        {
            ui_color inner { 0 };
            ui_color outer { 0 };
        };

        [[nodiscard]] handle_colors handle_colors_over(const ui_color under) noexcept
        {
            const ui_color white { make_ui_color(255, 255, 255) };
            const ui_color black { make_ui_color(0, 0, 0) };
            return is_dark_background(under) ? handle_colors { white, black } : handle_colors { black, white };
        }

        // 체커의 평균 회색 위에 알파 색을 얹은 색이다. 알파 막대 손잡이의 테 색을 고를 때 쓴다.
        [[nodiscard]] ui_color over_checker(const ui_color color) noexcept
        {
            const float alpha { static_cast<float>((color >> 24u) & 0xFFu) / 255.0f };
            const float base { 230.0f };
            const auto mix = [alpha, base](const std::uint32_t channel) { return static_cast<std::uint8_t>(std::lround(static_cast<float>(channel) * alpha + base * (1.0f - alpha))); };
            return make_ui_color(mix((color >> 16u) & 0xFFu), mix((color >> 8u) & 0xFFu), mix(color & 0xFFu));
        }

        // 고르기의 한 부분이다. 그리기·접근·접근 실행은 root가 준 함수로 하고, 이 element는 자리와 hit만 든다.
        //  - 부분이 root의 값과 자리를 읽어야 해서 root가 `this`를 붙잡은 함수를 준다. 부분은 root의 자식이라 root보다 먼저 죽는다.
        class picker_part final : public ui_element
        {
        public:
            struct behavior
            {
                std::function<void(draw_context&, const interaction_snapshot&, const ui_element&)> paint {};
                std::function<access_info()> access {};
                // 값 정하기만 받는다. 비어 있거나 nullopt면 할 수 없는 일이다.
                std::function<std::optional<std::vector<input_action>>(float value)> set_value {};
            };

            picker_part(const ui_element_id id, behavior behavior)
                : ui_element { id }
                , behavior_ { std::move(behavior) }
            {
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                if (behavior_.paint != nullptr)
                    behavior_.paint(context, interaction, *this);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return behavior_.access != nullptr ? behavior_.access() : ui_element::accessibility();
            }

            [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override
            {
                if (request.command != access_command::set_value)
                    return ui_element::access_actions(request);
                if (behavior_.set_value == nullptr)
                    return std::nullopt;
                return behavior_.set_value(request.value);
            }

        private:
            behavior behavior_ {};
        };

        // 견본 칸 줄이다. Tab의 한 자리(가로 묶음)이고 칸을 폭에 맞춰 다음 줄로 넘긴다. 구조라 접근 역할이 없다.
        class preset_row final : public ui_element
        {
        public:
            using ui_element::ui_element;

            void add(std::unique_ptr<ui_element> cell)
            {
                add_child(std::move(cell));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const float cell { color_picker_preset_size * scale };
                const float gap { color_picker_gap * scale };
                const std::size_t per_row { presets_per_row(context.slot.width, cell, gap) };
                const std::span<const std::unique_ptr<ui_element>> cells { children() };
                for (std::size_t index { 0 }; index < cells.size(); ++index)
                {
                    const float x { context.slot.x + static_cast<float>(index % per_row) * (cell + gap) };
                    const float y { context.slot.y + static_cast<float>(index / per_row) * (cell + gap) };
                    cells[index]->set_enabled(enabled());
                    cells[index]->arrange(context.for_child({ x, y, cell, cell }));
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }
        };
    }

    color_picker_element::color_picker_element(color_picker_config config)
        : ui_element { ui_element_id { ui_element_kind::color_picker, config.owner } }
        , config_ { std::move(config) }
        , value_ { clamp_hsv(config_.value) }
    {
        const bool editable { config_.change_to != nullptr };
        const auto make_part = [this, editable](const ui_element_kind kind, const std::u8string_view part_name, const channel part) {
            picker_part::behavior behavior {};
            behavior.paint = [this, part](draw_context& context, const interaction_snapshot&, const ui_element& self) { draw_part(context, part, self.bounds()); };
            behavior.access = [this, part]() { return part_accessibility(part); };
            if (part != channel::plane)
                behavior.set_value = [this, part](const float value) { return set_bar_value(part, value); };
            auto element { std::make_unique<picker_part>(ui_element_id { kind, part_owner(config_.owner, part_name) }, std::move(behavior)) };
            if (editable == false)
            {
                element->set_tab_stop(false);
                return element;
            }

            // 클릭과 탭은 그 자리의 값이다. 시작 거리를 넘기면 끌기이고, 넘는 순간 누른 자리의 값과 이동한 자리의 값을 낸다.
            //  - 끌기는 지금 값의 자기 채널을 읽지 않는다. 앞뒤 포인터의 값이 같으면 메시지가 없고 다르면 뒤의 값이다.
            //  - 터치는 시작 거리 대신 터치 몸짓의 규칙이다. 축에 맞는 빠른 쓸기는 감싼 창의 스크롤이고 잠깐 눌러 옮기면 끌기다.
            element->set_action(ui_trigger::left_click, [this, part](const ui_action_context& context) { return emit(value_at(part, context.x, context.y)); });
            pointer_drag_target drag {};
            drag.start_distance = color_picker_drag_start_distance;
            drag.on_press = [this, part](const ui_action_context& context) { return emit(value_at(part, context.x, context.y)); };
            drag.on_move = [this, part](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
                const hsv_color target { value_at(part, current.x, current.y) };
                if (target == value_at(part, previous.x, previous.y))
                    return {};
                return emit(target);
            };
            element->set_pointer_drag_target(std::move(drag));
            element->set_cursor(ui_cursor::hand);
            element->set_tab_stop(true);

            // 판은 ↑↓·Page·Home·End가 명도(세로 걸음, ↑가 늘린다)이고 ←→가 채도(키 처리기)다. 가상 목록 격자의 조합이다.
            // 막대는 가로 걸음이다. Ctrl·Alt를 든 키는 앱 단축키로 흐른다.
            key_step_target steps {};
            steps.axis = part == channel::plane ? focus_axis::vertical : focus_axis::horizontal;
            steps.up_increases = part == channel::plane;
            steps.on_step = [this, part](const value_step step) -> std::optional<std::vector<input_action>> { return step_by_key(part, step, part != channel::plane); };
            element->set_key_step_target(std::move(steps));
            if (part == channel::plane)
                element->set_key_handler([this](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
                    if (event.shortcut_modifier_down() || (event.key != key_code::arrow_left && event.key != key_code::arrow_right))
                        return std::nullopt;
                    return step_by_key(channel::plane, event.key == key_code::arrow_left ? value_step::decrease : value_step::increase, true);
                });
            return element;
        };

        auto plane { make_part(ui_element_kind::color_picker_plane, u8"plane", channel::plane) };
        plane_ = plane.get();
        add_child(std::move(plane));
        auto hue { make_part(ui_element_kind::color_picker_bar, u8"hue", channel::hue) };
        hue_ = hue.get();
        add_child(std::move(hue));
        if (config_.alpha)
        {
            auto alpha { make_part(ui_element_kind::color_picker_bar, u8"alpha", channel::alpha) };
            alpha_bar_ = alpha.get();
            add_child(std::move(alpha));
        }

        // 16진 칸은 앱 kind의 텍스트 칸이다 (콤보가 안쪽 칸을 품는 길과 같다). 글 편집과 확정은 앱이 든다.
        if (config_.hex.has_value())
        {
            const color_picker_hex& hex { *config_.hex };
            text_input_config input {};
            input.invalid = hex.invalid;
            // Android 키보드의 확정 단추도 확정이다. 비어 있으면 Enter로 흐른다.
            input.submit = hex.commit;
            auto field { std::make_unique<text_input_element>(hex.id, hex.value, std::move(input)) };
            field->set_access_name(std::u8string { default_text::text_or_default(hex.name, default_text::color_picker_hex_name()) });
            if (hex.commit != nullptr)
            {
                // 같은 표면 안에서 칸을 떠나면 확정한다. 다른 표면(popup 밖)으로 옮긴 초점은 떠남이 아니다.
                field->set_focus_leave_action(hex.commit);
                // 한 줄 칸이 쓰지 않은 Enter가 조상 전파로 이 칸에 먼저 온다. 수식 키를 든 Enter는 앱 단축키다.
                field->set_key_handler([commit = hex.commit, id = hex.id](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
                    if (event.key != key_code::enter || event.shortcut_modifier_down())
                        return std::nullopt;
                    const ui_action_context context { id };
                    return commit(context);
                });
            }
            hex_ = field.get();
            add_child(std::move(field));
        }

        if (config_.presets.empty() == false)
        {
            auto row { std::make_unique<preset_row>(ui_element_id { ui_element_kind::none, part_owner(config_.owner, u8"presets") }) };
            for (std::size_t index { 0 }; index < config_.presets.size(); ++index)
            {
                const color_picker_preset& preset { config_.presets[index] };
                const std::u8string name { preset.name.empty() ? format_hex_color(preset.color, config_.alpha) : preset.name };
                picker_part::behavior behavior {};
                behavior.paint = [color = preset.color, alpha = config_.alpha](draw_context& context, const interaction_snapshot& interaction, const ui_element& self) {
                    const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                    const rect_f box { self.bounds() };
                    const bool dim { self.enabled() == false };
                    if (dim)
                        context.canvas.saveLayerAlphaf(nullptr, disabled_content_alpha);
                    if (alpha && ((color >> 24u) & 0xFFu) != 0xFFu)
                        draw_checker(context.canvas, box, scale);
                    context.canvas.drawRect(sk_rect(box), solid_paint(alpha ? color : (color | 0xFF000000u)));
                    if (dim)
                        context.canvas.restore();
                    // 포인터가 올라가면 둘레를 2 논리 픽셀로 굵게 긋는다. 내용의 색이 무엇이든 둘레는 팔레트 색이다.
                    const bool hovered { dim == false && interaction.hovered == self.id() };
                    draw_outline(context.canvas, box, context.palette.control_border, scale);
                    if (hovered)
                        draw_outline(context.canvas, { box.x + scale, box.y + scale, box.width - 2.0f * scale, box.height - 2.0f * scale }, context.palette.control_border, scale);
                };
                behavior.access = [name]() { return access_info { .role = access_role::button, .name = name }; };
                auto cell { std::make_unique<picker_part>(ui_element_id { ui_element_kind::color_picker_preset, preset_owner(config_.owner, index + 1u) }, std::move(behavior)) };
                cell->set_tooltip(name);
                if (editable)
                {
                    // 견본은 그 색이다. 회색·검정 견본에서도 지금 색상·채도를 지킨다.
                    const ui_color color { preset.color };
                    cell->set_action(ui_trigger::left_click, [this, color](const ui_action_context&) { return emit(hsv_from_color(config_.alpha ? color : (color | 0xFF000000u), value_)); });
                    cell->set_cursor(ui_cursor::hand);
                }
                else
                    cell->set_tab_stop(false);
                row->add(std::move(cell));
            }
            // 줄 하나가 Tab의 한 자리이고 ←→가 칸 사이를 옮긴다. 들어올 때 첫 칸에 선다.
            if (editable)
            {
                row->set_focus_group(focus_axis::horizontal);
                row->set_focus_entry(row->children().front()->id());
            }
            presets_ = row.get();
            add_child(std::move(row));
        }
    }

    size_f color_picker_element::size_for(const color_picker_config& config) noexcept
    {
        const float width { plane_size_of(config) };
        const float bar { bar_height_of(config) };
        float height { width + color_picker_gap + bar };
        if (config.alpha)
            height += color_picker_gap + bar;
        height += color_picker_gap + color_picker_swatch_size;
        if (config.presets.empty() == false)
            height += color_picker_gap + presets_height(config.presets.size(), width, color_picker_preset_size, color_picker_gap);
        return { width, height };
    }

    ui_element_id color_picker_element::plane_id(const std::u8string_view owner)
    {
        return { ui_element_kind::color_picker_plane, part_owner(std::u8string { owner }, u8"plane") };
    }

    hsv_color color_picker_element::value_at_plane(const float x, const float y) const noexcept
    {
        hsv_color result { value_ };
        result.saturation = plane_box_.width > 0.0f ? unit_of((x - plane_box_.x) / plane_box_.width) : 0.0f;
        result.value = plane_box_.height > 0.0f ? 1.0f - unit_of((y - plane_box_.y) / plane_box_.height) : 1.0f;
        return result;
    }

    float color_picker_element::hue_at(const float x) const noexcept
    {
        return hue_box_.width > 0.0f ? unit_of((x - hue_box_.x) / hue_box_.width) * 360.0f : 0.0f;
    }

    float color_picker_element::alpha_at(const float x) const noexcept
    {
        return alpha_box_.width > 0.0f ? unit_of((x - alpha_box_.x) / alpha_box_.width) : 0.0f;
    }

    hsv_color color_picker_element::value_at(const channel part, const float x, const float y) const noexcept
    {
        hsv_color result { value_ };
        switch (part)
        {
        case channel::plane:
            return value_at_plane(x, y);
        case channel::hue:
            result.hue = hue_at(x);
            break;
        case channel::alpha:
            result.alpha = alpha_at(x);
            break;
        }
        return result;
    }

    std::vector<input_action> color_picker_element::emit(const hsv_color& target) const
    {
        if (config_.change_to == nullptr || enabled() == false)
            return {};
        return { config_.change_to(clamp_hsv(target)) };
    }

    std::vector<input_action> color_picker_element::step_by_key(const channel part, const value_step step, const bool horizontal) const
    {
        // 채널을 걸음 단위로 옮긴다. 색상은 1도, 나머지는 1%가 한 걸음이다. 지금 값이 격자 사이면 이웃 격자점으로 간다(별점과 같다).
        const bool hue { part == channel::hue };
        const float units { hue ? 1.0f : 100.0f };
        const float limit { hue ? 360.0f : 100.0f };
        float* channel_value { nullptr };
        hsv_color target { value_ };
        switch (part)
        {
        case channel::plane:
            channel_value = horizontal ? &target.saturation : &target.value;
            break;
        case channel::hue:
            channel_value = &target.hue;
            break;
        case channel::alpha:
            channel_value = &target.alpha;
            break;
        }
        const float current { *channel_value * units };
        float next { current };
        switch (step)
        {
        case value_step::decrease:
            next = std::ceil(current - grid_tolerance) - 1.0f;
            break;
        case value_step::increase:
            next = std::floor(current + grid_tolerance) + 1.0f;
            break;
        case value_step::decrease_page:
            next = std::ceil(current - grid_tolerance) - page_steps;
            break;
        case value_step::increase_page:
            next = std::floor(current + grid_tolerance) + page_steps;
            break;
        case value_step::minimum:
            next = 0.0f;
            break;
        case value_step::maximum:
            next = limit;
            break;
        }
        next = std::clamp(next, 0.0f, limit);
        // 이미 끝에 닿았으면 **삼킨다.** 흘려보내면 명도 100%인 판의 End가 앱 단축키로 새어 화면이 함께 맨 아래로 뛴다.
        if (std::abs(next - current) <= grid_tolerance)
            return {};
        *channel_value = next / units;
        return emit(target);
    }

    std::optional<std::vector<input_action>> color_picker_element::set_bar_value(const channel part, const float value) const
    {
        // 값 정하기는 그 채널만 바꾼 절대 메시지다. 표시 전용은 값을 받지 않는다.
        if (config_.change_to == nullptr)
            return std::nullopt;
        if (std::isnan(value))
            return std::vector<input_action> {};
        hsv_color target { value_ };
        if (part == channel::hue)
            target.hue = std::clamp(value, 0.0f, 360.0f);
        else
            target.alpha = std::clamp(value / 100.0f, 0.0f, 1.0f);
        if (target == value_)
            return std::vector<input_action> {};
        return emit(target);
    }

    access_info color_picker_element::part_accessibility(const channel part) const
    {
        access_info info { .role = access_role::slider };
        info.orientation = access_orientation::horizontal;
        switch (part)
        {
        case channel::plane:
            // 값이 둘이라 범위 하나로 말할 수 없다. 두 값을 글로 알리고 범위는 두지 않는다 (결정 4). 값을 정하는 보조 기술 길은
            // 막대와 16진 칸이다.
            info.orientation.reset();
            info.name = std::u8string { default_text::text_or_default(config_.plane_name, default_text::color_picker_plane_name()) };
            info.value = default_text::color_picker_plane_value(value_.saturation, value_.value);
            break;
        case channel::hue:
            info.name = std::u8string { default_text::text_or_default(config_.hue_name, default_text::color_picker_hue_name()) };
            info.range = access_range { 0.0f, 360.0f, value_.hue, 1.0f, page_steps };
            break;
        case channel::alpha:
            info.name = std::u8string { default_text::text_or_default(config_.alpha_name, default_text::color_picker_alpha_name()) };
            info.range = access_range { 0.0f, 100.0f, value_.alpha * 100.0f, 1.0f, page_steps };
            break;
        }
        return info;
    }

    void color_picker_element::arrange(const arrange_context& context)
    {
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        const rect_f slot { context.slot };
        const float requested { plane_size_of(config_) * scale_ };
        const float width { slot.width > 0.0f ? std::min(requested, slot.width) : requested };
        const float gap { color_picker_gap * scale_ };
        const float bar { bar_height_of(config_) * scale_ };
        float y { slot.y };
        plane_box_ = { slot.x, y, width, width };
        y += width + gap;
        hue_box_ = { slot.x, y, width, bar };
        y += bar + gap;
        alpha_box_ = {};
        if (alpha_bar_ != nullptr)
        {
            alpha_box_ = { slot.x, y, width, bar };
            y += bar + gap;
        }
        const float swatch { color_picker_swatch_size * scale_ };
        swatch_box_ = { slot.x, y, std::min(swatch, width), swatch };
        y += swatch;

        // 비활성은 root의 것이므로 부분이 따라간다.
        const auto place = [this, &context](ui_element* const part, const rect_f& box) {
            if (part == nullptr)
                return;
            part->set_enabled(enabled());
            part->arrange(context.for_child(box));
        };
        place(plane_, plane_box_);
        place(hue_, hue_box_);
        place(alpha_bar_, alpha_box_);
        if (hex_ != nullptr)
        {
            const float hex_height { color_picker_hex_height * scale_ };
            const float hex_width { std::min(color_picker_hex_width * scale_, std::max(0.0f, width - swatch - gap)) };
            place(hex_, { slot.x + swatch + gap, swatch_box_.y + (swatch - hex_height) / 2.0f, hex_width, hex_height });
        }
        if (presets_ != nullptr)
        {
            y += gap;
            const float height { presets_height(presets_->children().size(), width, color_picker_preset_size * scale_, gap) };
            place(presets_, { slot.x, y, width, height });
            y += height;
        }
        set_bounds({ slot.x, slot.y, width, y - slot.y });
    }

    void color_picker_element::draw_part(draw_context& context, const channel part, const rect_f& box) const
    {
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const float scale { scale_ };
        SkCanvas& canvas { context.canvas };
        const bool dim { enabled() == false };
        const ui_color hue_color { color_from_hsv({ value_.hue, 1.0f, 1.0f, 1.0f }) };
        const ui_color opaque { color_from_hsv({ value_.hue, value_.saturation, value_.value, 1.0f }) };
        // 판·막대는 내용의 색이라 모든 테마와 고대비에서 실제 색이다. 비활성은 내용을 옅게 그리고 손잡이를 회색으로 그린다.
        if (dim)
            canvas.saveLayerAlphaf(nullptr, disabled_content_alpha);
        handle_colors ring {};
        float handle_x { box.x };
        switch (part)
        {
        case channel::plane: {
            // 순색 위에 가로로 흰색에서 투명한 흰색, 세로로 투명한 검정에서 검정을 얹는다. 결과가 v((1 - s) 흰색 + s 순색)이다.
            //  - 투명 정지점은 흰색·검정의 알파 0이다. 투명 검정을 흰색 쪽에 쓰면 미리 곱하지 않은 보간이 가운데를 회색으로 끈다.
            canvas.drawRect(sk_rect(box), solid_paint(hue_color));
            const SkPoint across[2] { { box.x, box.y }, { box.x + box.width, box.y } };
            const std::array<SkColor4f, 2> whites { SkColor4f { 1.0f, 1.0f, 1.0f, 1.0f }, SkColor4f { 1.0f, 1.0f, 1.0f, 0.0f } };
            fill_gradient(canvas, box, across, whites);
            const SkPoint down[2] { { box.x, box.y }, { box.x, box.y + box.height } };
            const std::array<SkColor4f, 2> blacks { SkColor4f { 0.0f, 0.0f, 0.0f, 0.0f }, SkColor4f { 0.0f, 0.0f, 0.0f, 1.0f } };
            fill_gradient(canvas, box, down, blacks);
            ring = handle_colors_over(opaque);
            break;
        }
        case channel::hue: {
            const SkPoint across[2] { { box.x, box.y }, { box.x + box.width, box.y } };
            const std::array<SkColor4f, 7> stops {
                sk_color(make_ui_color(255, 0, 0)), sk_color(make_ui_color(255, 255, 0)), sk_color(make_ui_color(0, 255, 0)), sk_color(make_ui_color(0, 255, 255)),
                sk_color(make_ui_color(0, 0, 255)), sk_color(make_ui_color(255, 0, 255)), sk_color(make_ui_color(255, 0, 0))
            };
            fill_gradient(canvas, box, across, stops);
            ring = handle_colors_over(hue_color);
            handle_x = box.x + value_.hue / 360.0f * box.width;
            break;
        }
        case channel::alpha: {
            draw_checker(canvas, box, scale);
            const SkPoint across[2] { { box.x, box.y }, { box.x + box.width, box.y } };
            const std::array<SkColor4f, 2> fade { sk_color(opaque & 0x00FFFFFFu), sk_color(opaque) };
            fill_gradient(canvas, box, across, fade);
            ring = handle_colors_over(over_checker(with_alpha(opaque, value_.alpha)));
            handle_x = box.x + value_.alpha * box.width;
            break;
        }
        }
        if (dim)
        {
            canvas.restore();
            ring = { context.palette.disabled_foreground, context.palette.disabled_foreground };
        }
        // 둘레는 팔레트의 윤곽이다 (고대비에서 `window_foreground`).
        draw_outline(canvas, box, context.palette.control_border, scale);

        SkPaint inner { solid_paint(ring.inner) };
        inner.setStyle(SkPaint::kStroke_Style);
        inner.setStrokeWidth(handle_inner_width * scale);
        inner.setAntiAlias(true);
        SkPaint outer { solid_paint(ring.outer) };
        outer.setStyle(SkPaint::kStroke_Style);
        outer.setStrokeWidth(handle_outer_width * scale);
        outer.setAntiAlias(true);
        if (part == channel::plane)
        {
            // 원 손잡이다. 안쪽 2 논리 픽셀 테와 그 바깥 1 논리 픽셀 테다.
            const float cx { box.x + value_.saturation * box.width };
            const float cy { box.y + (1.0f - value_.value) * box.height };
            const float radius { plane_handle_radius * scale };
            canvas.drawCircle(cx, cy, radius - handle_inner_width * scale / 2.0f, inner);
            canvas.drawCircle(cx, cy, radius + handle_outer_width * scale / 2.0f, outer);
            return;
        }
        // 세로 막대 손잡이다. 막대 높이를 다 덮고 값의 자리에 가운데가 선다.
        const float width { bar_handle_width * scale };
        const SkRect handle { SkRect::MakeXYWH(handle_x - width / 2.0f, box.y, width, box.height) };
        canvas.drawRect(handle.makeInset(handle_inner_width * scale / 2.0f, handle_inner_width * scale / 2.0f), inner);
        canvas.drawRect(handle.makeOutset(handle_outer_width * scale / 2.0f, handle_outer_width * scale / 2.0f), outer);
    }

    void color_picker_element::draw_swatch(draw_context& context) const
    {
        const rect_f box { swatch_box_ };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const ui_color color { color_from_hsv(config_.alpha ? value_ : hsv_color { value_.hue, value_.saturation, value_.value, 1.0f }) };
        const bool dim { enabled() == false };
        if (dim)
            context.canvas.saveLayerAlphaf(nullptr, disabled_content_alpha);
        if (config_.alpha)
            draw_checker(context.canvas, box, scale_);
        context.canvas.drawRect(sk_rect(box), solid_paint(color));
        if (dim)
            context.canvas.restore();
        draw_outline(context.canvas, box, context.palette.control_border, scale_);
    }

    void color_picker_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_swatch(context);
        draw_children(context, interaction);
    }

    access_info color_picker_element::accessibility() const
    {
        // 값 글이 16진 글이라 UIA Value가 읽기 전용으로 선다. 견본은 root가 그리고 노드가 아니다.
        access_info info { .role = access_role::group };
        info.name = std::u8string { default_text::text_or_default(config_.name, default_text::color_picker_name()) };
        info.value = format_hex_color(color_from_hsv(config_.alpha ? value_ : hsv_color { value_.hue, value_.saturation, value_.value, 1.0f }), config_.alpha);
        return info;
    }

    color_field_element::color_field_element(color_field_config config)
        : ui_element { ui_element_id { ui_element_kind::color_field, config.owner } }
        , config_ { std::move(config) }
    {
        if (config_.set_open == nullptr)
            return;
        // 누름은 build 때의 상태 반대를 담아 절대 메시지 factory를 부른다. 보조 기술의 펼치기·접기도 같은 factory다.
        set_action(ui_trigger::left_click, [set_open = config_.set_open, target = config_.open == false](const ui_action_context&) -> std::vector<input_action> { return { set_open(target) }; });
        set_cursor(ui_cursor::hand);
        // 칸에서는 ↓가 달리 쓰이지 않는다. 드롭다운 단추의 관례대로 ↓도 연다.
        set_key_handler(field_popup_key_handler(config_.open, config_.set_open, true));
        // 열린 동안 키보드 초점이 같은 표면 안에서 칸을 떠나면 닫는다. popup의 판으로 옮긴 초점은 떠남이 아니다.
        if (config_.open)
            set_focus_leave_action([close = config_.set_open](const ui_action_context&) { return std::vector<input_action> { close(false) }; });
    }

    void color_field_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void color_field_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const SkRect body { sk_rect(box) };
        const float radius { context.metrics.control_corner_radius * scale };

        // 드롭다운과 같은 표면·테두리 규칙이다. popup이 떠 있으면 테두리를 강조한다.
        ui_color background { context.palette.input_background };
        if (enabled() && interaction.pressed == id())
            background = context.palette.button_pressed_background;
        else if (enabled() && interaction.hovered == id())
            background = context.palette.button_hover_background;
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));
        SkPaint border { solid_paint(config_.open ? context.palette.accent : context.palette.input_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        const float arrow_width { field_arrow_width * scale };
        const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 12.0f * scale };
        const SkPaint glyph { solid_paint(enabled() ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
        draw_centered_glyph(context.canvas, config_.open ? codicons::icon_chevron_up : codicons::icon_chevron_down, { box.x + box.width - arrow_width - 4.0f * scale, box.y, arrow_width, box.height }, glyph_font,
            glyph
        );

        // 견본은 내용의 색이다. 알파가 있으면 체커 위에 얹는다.
        const hsv_color value { clamp_hsv(config_.value) };
        const ui_color color { color_from_hsv(config_.alpha ? value : hsv_color { value.hue, value.saturation, value.value, 1.0f }) };
        const float inset { field_inset * scale };
        const float side { std::min(field_swatch_size * scale, std::max(0.0f, box.height - 2.0f * inset)) };
        const rect_f swatch { box.x + inset, box.y + (box.height - side) / 2.0f, side, side };
        const bool dim { enabled() == false };
        if (dim)
            context.canvas.saveLayerAlphaf(nullptr, disabled_content_alpha);
        if (config_.alpha)
            draw_checker(context.canvas, swatch, scale);
        context.canvas.drawRect(sk_rect(swatch), solid_paint(color));
        if (dim)
            context.canvas.restore();
        draw_outline(context.canvas, swatch, context.palette.control_border, scale);

        const float text_x { swatch.x + swatch.width + inset };
        const float text_width { box.x + box.width - arrow_width - 8.0f * scale - text_x };
        if (text_width <= 0.0f)
            return;
        const SkFont font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
        const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
        static_cast<void>(draw_text_within(context.canvas, format_hex_color(color, config_.alpha), text_x, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
    }

    access_info color_field_element::accessibility() const
    {
        const hsv_color value { clamp_hsv(config_.value) };
        access_info info { .role = access_role::combo_box, .expanded = config_.open };
        info.name = std::u8string { default_text::text_or_default(config_.name, default_text::color_picker_name()) };
        info.value = format_hex_color(color_from_hsv(config_.alpha ? value : hsv_color { value.hue, value.saturation, value.value, 1.0f }), config_.alpha);
        return info;
    }

    std::optional<std::vector<input_action>> color_field_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::expand && request.command != access_command::collapse)
            return ui_element::access_actions(request);
        // 펼치기·접기는 절대 메시지만 탄다. 클릭(뒤집기)으로 흘리면 오래된 발행본을 본 같은 명령 둘이 열었다 도로 닫는다.
        if (config_.set_open == nullptr)
            return std::nullopt;
        return std::vector<input_action> { config_.set_open(request.command == access_command::expand) };
    }
}

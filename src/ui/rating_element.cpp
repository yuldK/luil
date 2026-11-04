#include "luil/ui/rating_element.h"

#include "luil/ui/draw_primitives.h"
#include "ui/default_text.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>

namespace luil {
    namespace {
        // 별의 안쪽 꼭짓점 반지름이 바깥 꼭짓점 반지름의 몇 배인가다. 정오각 별은 0.382이고, 그보다 통통해야 작은 별에서도
        // 선이 뭉개지지 않으면서 면이 또렷하다.
        constexpr float star_inner_ratio { 0.45f };
        // 바깥 꼭짓점 반지름 곱하기 이 값이 별의 가로 폭의 절반이다 (sin 72도). 가로 폭이 별 한 변이다.
        constexpr float star_half_width_ratio { 0.9510565f };
        // 윗꼭짓점이 아랫꼭짓점보다 더 오른다. 경계 상자의 세로 가운데는 바깥 반지름의 (1 - cos 36도) / 2배만큼 외접원의 중심보다 위다.
        constexpr float star_center_drop_ratio { 0.0954915f };
        // 빈 별의 선 굵기다 (논리 픽셀).
        constexpr float outline_width { 1.5f };
        // 칸이 이 비율의 두 배보다 좁으면 안쪽 여백을 칸에 비례해 줄인다.
        constexpr float inset_limit_ratio { 0.25f };
        // 격자에 놓였다고 보는 오차다 (걸음에 대한 비율). 부동소수 오차가 한 걸음을 건너뛰게 하지 않는다.
        constexpr float grid_tolerance { 0.001f };

        // 걸음 하나의 hit 칸이다. 그리지 않고 hit와 hover와 끌기만 맡는다.
        // 칸은 자기 id와 모양만 갖고, 값을 읽거나 정하는 일은 전부 root가 한다.
        class rating_cell final : public ui_element
        {
        public:
            using ui_element::ui_element;

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {
            }
        };

        [[nodiscard]] std::u8string cell_owner(const std::u8string& owner, const std::size_t number)
        {
            std::u8string result { owner };
            result += u8'\u001f';
            for (const char digit : std::to_string(number))
                result += static_cast<char8_t>(digit);
            return result;
        }

        [[nodiscard]] bool same_on_grid(const float first, const float second, const float step) noexcept
        {
            return std::abs(first - second) <= step * grid_tolerance;
        }

        // 윗꼭짓점이 위를 보는 오각 별이다. `outer`는 외접원의 반지름이고 중심은 외접원의 중심이다.
        [[nodiscard]] SkPath star_path(const float center_x, const float center_y, const float outer)
        {
            SkPathBuilder path {};
            for (int point = 0; point < 10; ++point)
            {
                const float radius { point % 2 == 0 ? outer : outer * star_inner_ratio };
                const float angle { (static_cast<float>(point) * 36.0f - 90.0f) * std::numbers::pi_v<float> / 180.0f };
                const float x { center_x + radius * std::cos(angle) };
                const float y { center_y + radius * std::sin(angle) };
                if (point == 0)
                    path.moveTo(x, y);
                else
                    path.lineTo(x, y);
            }
            path.close();
            return path.detach();
        }
    }

    rating_element::rating_element(rating_config config)
        : ui_element { ui_element_id { ui_element_kind::rating, config.owner } }
        , config_ { std::move(config) }
    {
        // 표시 전용에는 칸도 커서도 Tab 자리도 키 역할도 없다. 없는 것은 두지 않는다.
        if (config_.change_to == nullptr)
            return;

        const float step { step_of(config_) };
        const std::size_t cells { count_of(config_) * (config_.precision == rating_precision::half ? 2u : 1u) };
        for (std::size_t number { 1 }; number <= cells; ++number)
        {
            const float cell_value { static_cast<float>(number) * step };
            auto cell { std::make_unique<rating_cell>(ui_element_id { ui_element_kind::rating_cell, cell_owner(config_.owner, number) }) };
            cell->set_action(ui_trigger::left_click, [this, cell_value](const ui_action_context&) -> std::vector<input_action> { return click(cell_value); });

            // 시작 거리를 둔 손잡이다. 누르고 떼면 클릭(칸의 값)이고, 거리를 넘기면 끌기다. 터치는 탭이 클릭이고
            // 축에 맞는 빠른 쓸기는 감싼 창의 스크롤이라 별 줄 위에서 시작한 쓸기가 창을 흘린다.
            //  - 끌기는 지금 값을 읽지 않는다. 앞뒤 포인터의 값이 같으면 메시지가 없고 다르면 뒤의 값이다.
            //    절대 메시지라 이동이 겹쳐도 쌓이지 않는다.
            pointer_drag_target drag {};
            drag.start_distance = rating_drag_start_distance;
            drag.on_press = [this](const ui_action_context& context) -> std::vector<input_action> { return emit(value_at(context.x)); };
            drag.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
                const float target { value_at(current.x) };
                if (target == value_at(previous.x))
                    return {};
                return emit(target);
            };
            cell->set_pointer_drag_target(std::move(drag));

            cell->set_cursor(ui_cursor::hand);
            // 칸은 누를 수 있어서 기본이 Tab 자리다. 자리는 root 하나이고, 눌러서 얻는 초점도 root로 보낸다.
            cell->set_tab_stop(false);
            cell->set_pointer_focus_target(id());
            add_child(std::move(cell));
        }

        // 초점은 root에 선다. 키는 초점을 따르므로 hit 대상을 넓히지 않는다.
        set_tab_stop(true);
        key_step_target steps {};
        steps.axis = focus_axis::horizontal;
        steps.on_step = [this](const value_step step_kind) -> std::optional<std::vector<input_action>> { return step_by_key(step_kind); };
        set_key_step_target(std::move(steps));
    }

    float rating_element::clamp_value(const rating_config& config) noexcept
    {
        // NaN은 "0보다 크다"가 거짓이라 0으로 떨어진다.
        if ((config.value > 0.0f) == false)
            return 0.0f;
        const float limit { static_cast<float>(count_of(config)) };
        return config.value < limit ? config.value : limit;
    }

    float rating_element::minimum_for(const rating_config& config) noexcept
    {
        if (config.clearable || clamp_value(config) <= 0.0f)
            return 0.0f;
        return step_of(config);
    }

    float rating_element::value_at(const float x) const noexcept
    {
        const float step { step_of(config_) };
        const float cell_width { cell_ * step };
        const float offset { x - bounds().x };
        if (cell_width <= 0.0f || offset < 0.0f)
            return config_.clearable ? 0.0f : step;
        const std::size_t cells { count_of(config_) * (config_.precision == rating_precision::half ? 2u : 1u) };
        // 오른쪽 밖(과 NaN)은 마지막 칸이다. 부동소수를 정수로 바꾸기 전에 거른다.
        const float number { std::floor(offset / cell_width) };
        const std::size_t index { number < static_cast<float>(cells) ? static_cast<std::size_t>(number) : cells - 1u };
        return static_cast<float>(index + 1u) * step;
    }

    float rating_element::grid_above(const float value) const noexcept
    {
        const float step { step_of(config_) };
        return (std::floor(value / step + grid_tolerance) + 1.0f) * step;
    }

    float rating_element::grid_below(const float value) const noexcept
    {
        const float step { step_of(config_) };
        return (std::ceil(value / step - grid_tolerance) - 1.0f) * step;
    }

    std::vector<input_action> rating_element::emit(const float value) const
    {
        if (config_.change_to == nullptr || enabled() == false)
            return {};
        return { config_.change_to(value) };
    }

    std::vector<input_action> rating_element::click(const float cell_value) const
    {
        // 지우기는 그 frame의 값으로 판정한다. 같은 칸을 다시 눌러야 0이 되므로 값이 칸과 같을 때만 본다.
        if (config_.clearable && same_on_grid(cell_value, clamp_value(config_), step_of(config_)))
            return emit(0.0f);
        return emit(cell_value);
    }

    std::vector<input_action> rating_element::step_by_key(const value_step step) const
    {
        const float current { clamp_value(config_) };
        const float low { minimum_for(config_) };
        const float high { static_cast<float>(count_of(config_)) };
        // 한 별은 정수 별에서 걸음 하나, 반 별에서 걸음 둘이다.
        const int stars { config_.precision == rating_precision::half ? 2 : 1 };
        float target { current };
        switch (step)
        {
        case value_step::decrease:
            target = grid_below(current);
            break;
        case value_step::increase:
            target = grid_above(current);
            break;
        case value_step::decrease_page:
            for (int index = 0; index < stars; ++index)
                target = std::max(grid_below(target), low);
            break;
        case value_step::increase_page:
            for (int index = 0; index < stars; ++index)
                target = std::min(grid_above(target), high);
            break;
        case value_step::minimum:
            target = low;
            break;
        case value_step::maximum:
            target = high;
            break;
        }
        target = std::clamp(target, low, high);
        // 이미 끝에 닿았으면 **삼킨다.** 흘려보내면 최소인 별점의 Home이 앱 단축키로 새어 화면이 함께 맨 위로 뛴다.
        if (same_on_grid(target, current, step_of(config_)))
            return {};
        return emit(target);
    }

    std::optional<float> rating_element::preview_value(const interaction_snapshot& interaction) const
    {
        if (enabled() == false || config_.change_to == nullptr)
            return std::nullopt;
        const std::span<const std::unique_ptr<ui_element>> cells { children() };
        // 누른 동안은 미리 보기를 끈다. 끌기가 서면 controller가 hover를 갱신하지 않아 누른 칸에 머무는데,
        // 값은 절대 메시지가 이동마다 나가 포인터의 칸을 따르기 때문이다.
        if (std::ranges::any_of(cells, [&interaction](const std::unique_ptr<ui_element>& cell) { return cell->id() == interaction.pressed; }))
            return std::nullopt;
        for (std::size_t index { 0 }; index < cells.size(); ++index)
        {
            if (cells[index]->id() == interaction.hovered)
                return static_cast<float>(index + 1u) * step_of(config_);
        }
        return std::nullopt;
    }

    void rating_element::arrange(const arrange_context& context)
    {
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        const rect_f slot { context.slot };
        const std::size_t count { count_of(config_) };
        // 자리 폭을 별 수로 나눈 것보다 크면 그 값으로 줄인다.
        const float room { slot.width > 0.0f ? slot.width / static_cast<float>(count) : 0.0f };
        cell_ = std::min(height_for(config_) * scale_, room);
        set_bounds({ slot.x, slot.y, cell_ * static_cast<float>(count), slot.height });

        // 칸은 걸음 하나의 폭 곱하기 자리 높이이고 빈틈없이 이어진다. 비활성은 root의 것이므로 칸이 따라간다.
        const float cell_width { cell_ * step_of(config_) };
        const std::span<const std::unique_ptr<ui_element>> cells { children() };
        for (std::size_t index { 0 }; index < cells.size(); ++index)
        {
            ui_element& cell { *cells[index] };
            cell.set_enabled(enabled());
            cell.arrange(context.for_child({ slot.x + static_cast<float>(index) * cell_width, slot.y, cell_width, slot.height }));
        }
    }

    void rating_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        if (cell_ <= 0.0f || box.height <= 0.0f)
            return;

        // 미리 보기는 값을 그 칸의 값으로 바꿔 그린다. 칸의 값이 현재 값보다 작으면 사이의 별이 빈 별로 돌아간다.
        const std::optional<float> preview { preview_value(interaction) };
        const float shown { preview.value_or(clamp_value(config_)) };
        const ui_color fill_color { enabled() == false ? context.palette.disabled_foreground : (preview.has_value() ? context.palette.accent_hover : context.palette.accent) };
        const ui_color outline_color { enabled() == false ? context.palette.disabled_foreground : context.palette.secondary_foreground };

        SkPaint fill { solid_paint(fill_color) };
        fill.setAntiAlias(true);
        SkPaint outline { solid_paint(outline_color) };
        outline.setStyle(SkPaint::kStroke_Style);
        outline.setStrokeWidth(outline_width * scale_);
        outline.setStrokeJoin(SkPaint::kRound_Join);
        outline.setAntiAlias(true);

        // 별은 칸 안 가운데이고 한 변(가로 폭)은 칸보다 여백의 두 배만큼 작다. 세로 가운데는 별의 경계 상자로 맞춘다.
        const float inset { std::min(rating_star_inset * scale_, cell_ * inset_limit_ratio) };
        const float star_side { cell_ - 2.0f * inset };
        const float outer { star_side / (2.0f * star_half_width_ratio) };
        const float center_y { box.y + box.height / 2.0f + outer * star_center_drop_ratio };
        const std::size_t count { count_of(config_) };
        for (std::size_t index { 0 }; index < count; ++index)
        {
            const float center_x { box.x + (static_cast<float>(index) + 0.5f) * cell_ };
            const SkPath star { star_path(center_x, center_y, outer) };
            const float ratio { std::clamp(shown - static_cast<float>(index), 0.0f, 1.0f) };
            if (ratio <= 0.0f)
            {
                context.canvas.drawPath(star, outline);
                continue;
            }
            if (ratio >= 1.0f)
            {
                context.canvas.drawPath(star, fill);
                continue;
            }

            // 일부만 채운 별은 가로의 그 비율 지점에서 자리를 둘로 나눠 왼쪽은 채움, 오른쪽은 빈 별로 그린다.
            const float split { center_x - star_side / 2.0f + ratio * star_side };
            const SkRect left { SkRect::MakeLTRB(center_x - cell_, center_y - cell_, split, center_y + cell_) };
            const SkRect right { SkRect::MakeLTRB(split, center_y - cell_, center_x + cell_, center_y + cell_) };
            {
                const SkAutoCanvasRestore restore { &context.canvas, true };
                context.canvas.clipRect(left);
                context.canvas.drawPath(star, fill);
            }
            {
                const SkAutoCanvasRestore restore { &context.canvas, true };
                context.canvas.clipRect(right);
                context.canvas.drawPath(star, outline);
            }
        }
        draw_children(context, interaction);
    }

    access_info rating_element::accessibility() const
    {
        const float value { clamp_value(config_) };
        const std::size_t count { count_of(config_) };
        access_info info { .role = access_role::slider };
        info.name = config_.name.empty() ? std::u8string { default_text::rating_name() } : config_.name;
        // 범위가 값을 품어야 한다. 표시 전용에서 지울 수 없는 소수 값(한 걸음보다 작은 값)은 최소보다 작을 수 있다.
        info.range = access_range { std::min(minimum_for(config_), value), static_cast<float>(count), value, step_of(config_), 1.0f };
        // 값 글이 비지 않아 UIA Value 패턴도 함께 선다.
        info.value = config_.value_name != nullptr ? config_.value_name(value, count) : default_text::rating_value_name(value, count);
        return info;
    }

    std::optional<std::vector<input_action>> rating_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::set_value)
            return ui_element::access_actions(request);
        // 값 정하기는 **절대 메시지**만 탄다. 표시 전용은 값을 받지 않는다.
        if (config_.change_to == nullptr)
            return std::nullopt;
        if (std::isnan(request.value))
            return std::vector<input_action> {};
        // 가장 가까운 격자점이고 같은 거리면 큰 쪽이다. 범위 밖은 최소·최대로 자른다. 지울 수 없는 별점이 0 요청을 받으면
        // 한 걸음이 된다 (provider가 범위 밖 요청을 먼저 거절하므로 이 줄에는 반올림이 범위 가장자리에 닿는 요청만 온다).
        const float step { step_of(config_) };
        const float target { std::clamp(std::floor(request.value / step + 0.5f) * step, minimum_for(config_), static_cast<float>(count_of(config_))) };
        if (same_on_grid(target, clamp_value(config_), step))
            return std::vector<input_action> {};
        return emit(target);
    }
}

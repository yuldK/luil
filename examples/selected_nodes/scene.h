#pragma once

#include "luil/luil.h"

namespace selected_nodes {
    inline constexpr auto target_hour { static_cast<luil::text_input_target>(500) };
    inline constexpr auto target_minute { static_cast<luil::text_input_target>(501) };
    inline constexpr auto target_sample { static_cast<luil::text_input_target>(502) };
    inline constexpr auto kind_sample { luil::application_element_kind(500) };

    [[nodiscard]] inline luil::dock_layout initial_layout()
    {
        luil::dock_layout layout {};
        layout.root.panels = { u8"editor", u8"inspector", u8"console", u8"notes" };
        layout.root.selected = u8"editor";
        (void)luil::apply_dock_command(layout, { .panel = u8"inspector", .target = u8"root", .placement = luil::dock_placement::right });
        (void)luil::apply_dock_command(layout, { .panel = u8"console", .target = u8"root", .placement = luil::dock_placement::bottom });
        (void)luil::apply_dock_command(layout, { .operation = luil::dock_operation::detach, .panel = u8"notes", .bounds = { 40.0f, 48.0f, 280.0f, 180.0f } });
        return layout;
    }

    struct scene_state
    {
        luil::dock_layout dock { initial_layout() };
        float page { 0.0f };
        luil::text::text_edit_state hour { .text = u8"23" };
        luil::text::text_edit_state minute { .text = u8"45" };
        luil::text::text_edit_state sample { .text = u8"office affine ffi — caret" };
        std::optional<luil::text_composition_event> composition {};
    };

    [[nodiscard]] std::unique_ptr<luil::ui_element> make_scene(const scene_state& state);
}

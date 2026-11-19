#pragma once

#include "luil/luil.h"
#include "video_strings.h"

namespace video_demo {
    enum class command
    {
        play,
        stop,
        next_sample,
        change_theme,
        close,
    };

    struct sample_choice
    {
        std::u8string extension {};
    };

    struct player_view
    {
        luil::string_lookup strings { video_strings::table, 0 };
        luil::ui_image image {};
        std::u8string title {};
        std::u8string detail {};
        std::u8string format {};
        std::u8string status {};
        std::u8string subtitle {};
        std::u8string subtitle_file {};
        std::u8string clock {};
        luil::win32::media_state state { luil::win32::media_state::empty };
        bool translucent_overlays { true };
        luil::theme_preference theme { luil::theme_preference::system };

        [[nodiscard]] std::u8string text(const video_strings::key key) const
        {
            return std::u8string { video_strings::text(strings, key) };
        }
    };

    [[nodiscard]] std::unique_ptr<luil::ui_element> build_player(const player_view& view, const luil::window_metrics& metrics);
}

#include "video/scene.h"

#include "luil/generated/codicons.h"

#include <algorithm>
#include <cmath>

namespace video_demo {
    namespace {
        constexpr auto kind { luil::application_element_kind(1) };
        using luil::layout_alignment;
        using video_strings::key;
        using luil::win32::media_state;

        std::unique_ptr<luil::label_element> label(std::u8string id, luil::label_config config)
        {
            auto result { std::make_unique<luil::label_element>(luil::ui_element_id { kind, std::move(id) }, std::move(config)) };
            result->set_hit_opaque(false);
            return result;
        }

        std::unique_ptr<luil::button_element> icon_button(std::u8string_view owner, std::u8string name, std::u8string hint, char32_t glyph, command action)
        {
            auto result { std::make_unique<luil::button_element>(luil::ui_element_id { kind, std::u8string { owner } }
                , luil::button_config { .glyph = glyph, .icon_size = 16.0f }
            )
            };
            result->set_access_name(std::move(name));
            result->set_tooltip(std::move(hint));
            result->set_cursor(luil::ui_cursor::hand);
            result->set_action(luil::ui_trigger::left_click, luil::make_message_action(action));
            return result;
        }

        std::unique_ptr<luil::ui_element> heading(const player_view& view, bool narrow)
        {
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id {}
                , luil::stack_config { .direction = luil::stack_direction::row, .spacing = narrow ? 6.0f : 12.0f
                    , .cross_alignment = luil::stack_alignment::center
                }
            )
            };
            auto names { std::make_unique<luil::stack_element>(luil::ui_element_id {}
                , luil::stack_config { .spacing = 1.0f, .main_alignment = luil::stack_main_alignment::center }
            )
            };
            names->add(label(u8"title", { .text = view.title, .font_size = narrow ? 18.0f : 22.0f
                , .weight = luil::font_weight::semibold, .color = luil::label_color_role::primary
            }), 29.0f);
            names->add(label(u8"file", { .text = view.detail, .font_size = 12.0f }), 20.0f);
            row->add_flexible(std::move(names));

            luil::choice_group_config choices {};
            choices.owner = u8"sample-format";
            choices.name = view.text(key::sample_format);
            choices.style = luil::choice_style::toggle;
            choices.items = { { u8".mp4", u8"MP4" }, { u8".avi", u8"AVI" }, { u8".ts", u8"TS" } };
            choices.selected = view.format;
            choices.select = [](const std::u8string& value) { return luil::make_app_action(sample_choice { value }); };
            row->add_fit(std::make_unique<luil::choice_group_element>(std::move(choices)));
            const auto hint { view.theme == luil::theme_preference::system ? view.text(key::theme_system_hint)
                : view.theme == luil::theme_preference::dark ? view.text(key::theme_dark_hint) : view.text(key::theme_light_hint)
            };
            row->add(icon_button(u8"theme", view.text(key::change_theme), hint, luil::codicons::icon_color_mode, command::change_theme)
                , { .length = 30.0f, .cross_length = 30.0f }
            );
            return row;
        }

        // 여러 줄 SRT도 가장 긴 표시 줄에만 바탕을 깐다. 실제 배치와 같은 글꼴·줄바꿈 측정기를 쓴다.
        class subtitle_box final : public luil::ui_element
        {
        public:
            subtitle_box(std::u8string text, bool translucent)
                : luil::ui_element { {} }
                , config_ { .text = std::move(text), .font_size = 16.0f, .weight = luil::font_weight::medium
                    , .color = luil::label_color_role::primary, .background = luil::label_background_role::raised
                    , .padding = 10.0f, .align = luil::text_align::center, .wrap = true
                }
            {
                auto scope { std::make_unique<luil::palette_scope_element>(luil::ui_element_id {}
                    , luil::palette_scope_config { .palette = [translucent](luil::ui_color_palette palette, luil::color_theme) {
                        static const auto dark { luil::color_palette_for(luil::color_theme::dark) };
                        palette.primary_foreground = dark.primary_foreground;
                        palette.raised_background = translucent ? luil::with_alpha(dark.window_background, 0.82f) : dark.window_background;
                        return palette;
                    }
                    }
                )
                };
                scope->set_content(label(u8"subtitle", config_));
                add_child(std::move(scope));
                set_hit_opaque(false);
            }

            std::optional<luil::size_f> preferred_size(const luil::measure_context& context, float width) const override
            {
                const auto lines { luil::label_element::measure_wrap(config_, context, width) };
                return luil::size_f { std::min(width, std::ceil(lines.width) + 2.0f * config_.padding + 1.0f)
                    , std::max(lines.height, luil::label_element::height_for(config_)) + 4.0f
                };
            }

            void arrange(const luil::arrange_context& context) override
            {
                set_bounds(context.slot);
                children().front()->arrange(context);
            }

            void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

        private:
            luil::label_config config_ {};
        };

        std::unique_ptr<luil::ui_element> overlay(const player_view& view, bool compact)
        {
            auto layers { std::make_unique<luil::layer_element>(luil::ui_element_id {}) };
            if (view.subtitle.empty() == false)
                layers->add(std::make_unique<subtitle_box>(view.subtitle, view.translucent_overlays)
                    , { .horizontal = layout_alignment::center, .vertical = layout_alignment::end, .margin = { 8, 0, 8, 58 } }
                );

            auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id {}
                , luil::stack_config { .direction = luil::stack_direction::row, .spacing = 3.0f, .padding = luil::edge_insets::all(4) }
            )
            };
            const bool playing { view.state == media_state::playing };
            const bool enabled { playing || view.state == media_state::ready || view.state == media_state::stopped || view.state == media_state::ended };
            const auto name { playing ? view.text(key::stop) : view.text(key::play) };
            const auto glyph { playing ? luil::codicons::icon_debug_stop : luil::codicons::icon_play };
            const auto action { playing ? command::stop : command::play };
            if (compact)
            {
                auto primary { icon_button(u8"transport", name, name, glyph, action) };
                primary->set_enabled(enabled);
                controls->add(std::move(primary), 30.0f);
            }
            else
            {
                auto primary { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind, u8"transport" }
                    , luil::text_button_config { .text = name, .visual = luil::text_button_visual::accent, .glyph = glyph }
                )
                };
                primary->set_cursor(luil::ui_cursor::hand);
                primary->set_action(luil::ui_trigger::left_click, luil::make_message_action(action));
                primary->set_enabled(enabled);
                controls->add(std::move(primary), 66.0f);
            }
            if (view.state == media_state::ended)
                controls->add(icon_button(u8"rewind", view.text(key::stop), view.text(key::rewind), luil::codicons::icon_debug_stop, command::stop), 30.0f);
            controls->add(icon_button(u8"next-sample", view.text(key::next_sample), view.text(key::next_sample), luil::codicons::icon_chevron_right, command::next_sample), 30.0f);
            if (compact == false)
                controls->add_flexible(label(u8"clock", { .text = view.clock, .font_size = 12.0f, .align = luil::text_align::center }));

            auto surface { std::make_unique<luil::panel_element>(luil::ui_element_id {}
                , luil::panel_config { .background = [translucent = view.translucent_overlays](const auto& palette) {
                        return translucent ? luil::with_alpha(palette.surface_background, 0.94f) : palette.surface_background;
                    }
                    , .corner_radius = 6, .border = [](const auto& palette) { return palette.divider; }
                }
            )
            };
            surface->set_content(std::move(controls));
            layers->add(std::move(surface)
                , { .horizontal = layout_alignment::start, .vertical = layout_alignment::end
                    , .width = (compact ? 72.0f : 184.0f) + (view.state == media_state::ended ? 33.0f : 0.0f), .height = 38.0f, .margin = { 12, 0, 8, 12 }
                }
            );
            return layers;
        }
    }

    std::unique_ptr<luil::ui_element> build_player(const player_view& view, const luil::window_metrics& metrics)
    {
        const luil::caption_config caption { .title = view.text(key::window_title) };
        const float scale { metrics.scale > 0.0f ? metrics.scale : 1.0f };
        const bool narrow { metrics.width / scale < 620.0f };
        const float inset { narrow ? 12.0f : 24.0f };
        auto root { std::make_unique<luil::stack_element>(luil::ui_element_id {}, luil::stack_config {}) };
        root->add(std::make_unique<luil::caption_element>(caption), luil::caption_element::height_for(caption));
        const luil::stack_config layout { .spacing = 8.0f, .padding = { inset, 8, inset, 8 } };
        auto body { std::make_unique<luil::stack_element>(luil::ui_element_id {}, layout) };
        body->add(heading(view, narrow), 52.0f);
        const luil::rect_f slot { 0, 0, metrics.width / scale - 2 * inset
            , metrics.height / scale - luil::caption_element::height_for(caption) - 106.0f
        };
        const auto film { view.image.valid() ? luil::image_destination(slot, view.image.width(), view.image.height(), luil::image_fit::contain) : slot };
        // 레터박스를 창 바탕에 맞춰 화면 비율이 바뀌어도 영상 둘레의 여백이 이어진다.
        luil::palette_scope_config stage_colors {};
        stage_colors.palette = [](luil::ui_color_palette palette, luil::color_theme)
        {
            palette.surface_background = palette.window_background;
            return palette;
        };
        auto stage { std::make_unique<luil::palette_scope_element>(luil::ui_element_id {}, std::move(stage_colors)) };
        stage->set_content(std::make_unique<luil::video_element>(luil::ui_element_id { kind, u8"video" }
            , luil::video_config { .name = view.text(key::video), .frame = view.image }, overlay(view, film.width < 300.0f)
        ));

        body->add_flexible(std::move(stage));

        auto footer { std::make_unique<luil::stack_element>(luil::ui_element_id {}
            , luil::stack_config { .direction = luil::stack_direction::row, .spacing = 12.0f }
        )
        };
        footer->add_flexible(label(u8"status", { .text = view.status, .font_size = 12.0f
            , .color = view.state == media_state::failed ? luil::label_color_role::error : luil::label_color_role::primary
        }));
        footer->add(label(u8"subtitle-file", { .text = view.subtitle_file, .font_size = 12.0f, .align = luil::text_align::end }), narrow ? 150.0f : 220.0f);
        body->add(std::move(footer), 22.0f);
        root->add_flexible(std::move(body));
        return root;
    }
}

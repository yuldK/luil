#include "selected_nodes/scene.h"

#include <windows.h>
#include <atomic>

namespace selected_nodes {
    namespace {
        struct close_request
        {
        };
        class driver final : public luil::logic_driver
        {
        public:
            void handle(luil::app_message message) override
            {
                if (message.get<close_request>() != nullptr)
                    closed_ = true;
                if (const auto* metrics { message.get<luil::window_metrics>() }; metrics != nullptr)
                    metrics_ = *metrics;
                if (const auto* command { message.get<luil::dock_command>() }; command != nullptr)
                    (void)luil::apply_dock_command(state_.dock, *command);
                if (const auto* command { message.get<luil::pager_command>() }; command != nullptr)
                    state_.page = luil::apply_pager_command(state_.page, 3, *command);
                if (const auto* composition { message.get<luil::text_composition_event>() }; composition != nullptr)
                    state_.composition = *composition;
                if (const auto* edit { message.get<luil::text_edit_request>() }; edit != nullptr)
                {
                    if (edit->target == target_sample)
                        luil::apply_text_edit(state_.sample, *edit);
                    else
                    {
                        const bool hour { edit->target == target_hour };
                        luil::apply_text_edit(hour ? state_.hour : state_.minute, *edit, luil::number_input_filter({ .minimum = 0.0, .maximum = hour ? 23.0 : 59.0 }));
                    }
                }
            }
            std::shared_ptr<const luil::ui_frame> make_frame() override
            {
                const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
                const float width { metrics_.width > 0.0f ? metrics_.width : 1200.0f };
                const float height { metrics_.height > 0.0f ? metrics_.height : 820.0f };
                auto root { std::make_unique<luil::stack_element>(luil::ui_element_id {}, luil::stack_config {}) };
                luil::caption_config caption { .title = u8"luil selected nodes" };
                root->add(std::make_unique<luil::caption_element>(caption), luil::caption_element::height_for(caption));
                root->add_flexible(make_scene(state_));
                auto frame { std::make_shared<luil::ui_frame>() };
                frame->tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, scale));
                return frame;
            }
            luil::app_message make_close_message() override { return luil::app_message { close_request {} }; }
            bool shutdown_completed() const override { return closed_; }

        private:
            scene_state state_ {};
            luil::window_metrics metrics_ {};
            std::atomic<bool> closed_ { false };
        };

        class policy final : public luil::interaction_policy
        {
        public:
            std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
            {
                if (kind == luil::ui_element_kind::time_picker_hour) return target_hour;
                if (kind == luil::ui_element_kind::time_picker_minute) return target_minute;
                if (kind == kind_sample) return target_sample;
                return std::nullopt;
            }
            luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override { return luil::make_app_action(request); }
            luil::input_action make_text_composition_action(const luil::text_composition_event& event) const override { return luil::make_app_action(event); }
        };

        class delegate final : public luil::win32::window_delegate
        {
        public:
            luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
            {
                return luil::app_message { luil::make_window_metrics(width, height, scale) };
            }
        };
    }
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    luil::win32::enable_per_monitor_dpi_awareness();
    const luil::win32::com_sta_scope com {};
    selected_nodes::driver driver {};
    selected_nodes::policy policy {};
    selected_nodes::delegate delegate {};
    luil::win32::window_config config {};
    config.class_name = L"Luil.SelectedNodes";
    config.caption.title = u8"luil selected nodes";
    config.initial_width = 1200;
    config.initial_height = 820;
    luil::win32::window_environment environment {};
    environment.driver = &driver;
    environment.delegate = &delegate;
    environment.policy = &policy;
    return luil::win32::run_application_window(config, environment);
}

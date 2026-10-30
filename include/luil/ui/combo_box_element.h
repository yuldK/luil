#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/dialog_elements.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    struct combo_suggestion
    {
        std::u8string key {};
        std::u8string label {};
        bool enabled { true };
    };

    struct combo_suggestion_state
    {
        std::uint64_t token { 0 };
        std::u8string query {};
        std::shared_ptr<const std::vector<combo_suggestion>> items {};
        bool loading { false };
    };

    [[nodiscard]] std::uint64_t begin_combo_query(combo_suggestion_state& state, std::u8string query);
    [[nodiscard]] bool accept_combo_suggestions(combo_suggestion_state& state, std::uint64_t token, std::shared_ptr<const std::vector<combo_suggestion>> items);

    struct combo_box_config
    {
        text_input_config input {};
        combo_suggestion_state suggestions {};
        std::u8string highlighted {};
        std::u8string validation_message {};
        bool open { false };
        bool allow_custom { true };
        float popup_height { 240.0f };
        std::function<input_action(bool)> set_open {};
        std::function<input_action(const std::u8string&)> move {};
        std::function<input_action(const std::u8string&)> select {};
        std::function<input_action(const std::u8string&)> commit {};
        std::function<input_action(const std::u8string&)> invalid {};
        std::function<input_action(std::uint64_t, const std::u8string&)> request_suggestions {};
        ui_action restore {};
        float scroll_offset { 0.0f };
        std::function<input_action(float delta)> scroll {};
        std::function<input_action(float offset)> scroll_to {};
        std::function<bool(const combo_suggestion&, std::u8string_view)> matches {};
    };

    class combo_box_element final : public ui_element
    {
    public:
        // 입력 칸은 같은 kind와 다른 owner다. 기존 text_target_of를 그대로 쓴다.
        combo_box_element(ui_element_id id, text_input_view value, combo_box_config config);
        [[nodiscard]] bool ancestor_keys_connected() const noexcept;
        [[nodiscard]] std::optional<std::vector<input_action>> on_key(const key_pressed_event& event) const;
        // 앱이 편집·IME 표시를 반영한 뒤 그 token의 후보 요청을 만든다.
        [[nodiscard]] std::optional<input_action> suggestion_request(std::uint64_t token) const;
        [[nodiscard]] ui_popup suggestion_popup(std::u8string anchor = {}) const;
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        [[nodiscard]] std::optional<text_input_snapshot> text_input() const override;
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        text_input_view value_ {};
        combo_box_config config_ {};
        text_input_element* input_ { nullptr };
        ui_element* validation_ { nullptr };
        float scale_ { 1.0f };
        bool keys_connected_ { false };
        std::vector<combo_suggestion> visible_items_ {};
    };
}

#include "luil/ui/message_box.h"

#include "luil/ui/panel_element.h"
#include "ui/default_text.h"
#include "ui/message_box_element.h"

#include <cmath>
#include <utility>

namespace luil {
    namespace {
        // 표준 단추 하나다. 글이 비면 기본 문구이고 앱이 준 글은 그대로 쓴다.
        // 기본 문구는 늘 `default_text::`로 한정해 부른다. 같은 이름의 공개 helper가 이 namespace에 있다.
        [[nodiscard]] message_box_button standard_button(const std::u8string_view key, std::u8string text, const std::u8string_view fallback, const message_box_role role)
        {
            // 돌려받은 view는 `text`가 가리키던 글자일 수 있다. 옮겨 담기 전에 `text`를 버리지 않는다.
            std::u8string label { default_text::text_or_default(text, fallback) };
            return message_box_button { .key = std::u8string { key }, .text = std::move(label), .role = role };
        }

        // 접근 이름이다. 심각도가 있으면 낱말을 제목 앞에 붙인다. 화면 읽기가 뜨는 순간 말하는 이름이다.
        [[nodiscard]] std::u8string host_name_of(const message_box_config& config)
        {
            if (config.severity.has_value() == false)
                return config.title;
            std::u8string name { default_text::text_or_default(config.severity_name, default_text::severity_name(*config.severity)) };
            if (config.title.empty() == false)
            {
                name += u8' ';
                name += config.title;
            }
            return name;
        }

        // Esc와 바깥 누름이 낼 액션이다. 닫는 길이 없으면 비어 있고, 빈 액션이 "이 계기로는 닫지 않는다"다.
        [[nodiscard]] ui_action dismiss_action_of(const message_box_config& config)
        {
            const std::optional<std::size_t> index { message_box_dismiss_index(config.buttons) };
            if (config.result == nullptr || index.has_value() == false)
                return {};
            return [result = config.result, key = config.buttons[*index].key](const ui_action_context&) -> std::vector<input_action> { return { result(key) }; };
        }
    }

    std::unique_ptr<modal_host_element> make_message_box(message_box_config config)
    {
        modal_host_config host {};
        host.owner = config.owner;
        host.name = host_name_of(config);
        host.focus_return = config.focus_return;
        // 뜨는 순간 초점이 서는 단추가 Enter의 대상과 같다. 초점이 이기므로 어긋나면 채움이 Enter의 자리를 잘못 말한다.
        if (const std::optional<std::size_t> entry { message_box_entry_index(config.buttons) }; entry.has_value())
            host.focus_entry = message_box_button_id(config.owner, config.buttons[*entry].key);
        if (ui_action dismiss { dismiss_action_of(config) })
        {
            host.dismiss = dismiss;
            if (config.outside_dismisses)
                host.outside = std::move(dismiss);
        }

        // 표면은 팔레트 역할뿐이다. 둘레의 1px 테가 고대비에서 경계를 남긴다.
        panel_config surface {};
        surface.background = [](const ui_color_palette& palette) { return palette.surface_background; };
        surface.corner_radius = 8.0f;
        surface.border = [](const ui_color_palette& palette) { return palette.divider; };
        host.surface = std::move(surface);

        // 폭은 설정이고 높이는 배치할 때 내용에게 묻는다. 자리 가장자리에서 여백을 남긴다.
        host.content_width = std::isfinite(config.width) && config.width > 0.0f ? config.width : message_box_default_width;
        host.fit_height = true;
        host.edge_margin = message_box_screen_margin;

        auto result { std::make_unique<modal_host_element>(std::move(host)) };
        result->set_content(std::make_unique<message_box_element>(std::move(config)));
        return result;
    }

    std::optional<input_action> message_box_dismiss_message(const message_box_config& config)
    {
        const std::optional<std::size_t> index { message_box_dismiss_index(config.buttons) };
        if (config.result == nullptr || index.has_value() == false)
            return std::nullopt;
        return config.result(config.buttons[*index].key);
    }

    ui_element_id message_box_button_id(const std::u8string_view owner, const std::u8string_view key)
    {
        std::u8string part { owner };
        part += u8'\u001f';
        part += key;
        return { ui_element_kind::message_box_button, std::move(part) };
    }

    ui_element_id message_box_check_id(const std::u8string_view owner)
    {
        std::u8string part { owner };
        part += u8'\u001f';
        part += u8"check";
        return { ui_element_kind::check, std::move(part) };
    }

    message_box_button message_box_ok(std::u8string text)
    {
        return standard_button(message_box_ok_key, std::move(text), default_text::message_box_ok_label(), message_box_role::accept);
    }

    message_box_button message_box_cancel(std::u8string text)
    {
        return standard_button(message_box_cancel_key, std::move(text), default_text::message_box_cancel_label(), message_box_role::reject);
    }

    message_box_button message_box_yes(std::u8string text)
    {
        return standard_button(message_box_yes_key, std::move(text), default_text::message_box_yes_label(), message_box_role::accept);
    }

    message_box_button message_box_no(std::u8string text)
    {
        return standard_button(message_box_no_key, std::move(text), default_text::message_box_no_label(), message_box_role::reject);
    }
}

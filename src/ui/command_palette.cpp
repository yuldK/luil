#include "luil/ui/command_palette.h"

#include "luil/generated/codicons.h"
#include "luil/text/case_fold.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_interaction.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace luil {
    namespace {
        // 거르기의 단계다. 작을수록 앞이다.
        enum class match_rank
        {
            label_start,
            word_start,
            label_contains,
            id_contains,
        };

        // 낱말을 가르는 글자인가. 접은 글의 ASCII 바이트만 본다 — 한글 낱말은 공백으로 갈린다.
        [[nodiscard]] bool separates_words(const char8_t character) noexcept
        {
            return character < 0x80u
                && (character == u8' ' || character == u8'-' || character == u8'_' || character == u8'.' || character == u8'/' || character == u8'(' || character == u8'[' || character == u8':');
        }

        [[nodiscard]] std::optional<match_rank> rank_of(const app_command& command, const std::u8string_view needle)
        {
            const std::u8string label { text::fold_case(command.label) };
            if (label.starts_with(needle))
                return match_rank::label_start;
            bool contains { false };
            for (std::size_t at { label.find(needle) }; at != std::u8string::npos; at = label.find(needle, at + 1))
            {
                contains = true;
                if (separates_words(label[at - 1]))
                    return match_rank::word_start;
            }
            if (contains)
                return match_rank::label_contains;
            if (text::fold_case(command.id).find(needle) != std::u8string::npos)
                return match_rank::id_contains;
            return std::nullopt;
        }

        [[nodiscard]] std::u8string_view trimmed(std::u8string_view text) noexcept
        {
            while (text.empty() == false && (text.front() == u8' ' || text.front() == u8'\t'))
                text.remove_prefix(1);
            while (text.empty() == false && (text.back() == u8' ' || text.back() == u8'\t'))
                text.remove_suffix(1);
            return text;
        }

        // 팔레트의 메뉴 설정이다. 줄·기본 항목·고른 뒤 닫기를 한 자리에서 짓는다.
        [[nodiscard]] menu_config palette_menu(const command_palette_config& config)
        {
            menu_config menu {};
            menu.owner = config.owner;
            menu.items = command_palette_items(config);
            if (config.run != nullptr)
                menu.select = [run = config.run](const std::u8string& key) { return run(key); };
            menu.after_select = config.close;
            // Enter가 강조 없이 고르는 것은 고를 수 있는 첫 결과다. 결과 없음 줄은 고를 수 없어 기본이 되지 않는다.
            for (const menu_item_config& item : menu.items)
                if (item.enabled)
                {
                    menu.default_key = item.key;
                    break;
                }
            return menu;
        }
    }

    std::vector<std::size_t> filter_commands(const std::span<const app_command> commands, const std::u8string_view query)
    {
        const std::u8string needle { text::fold_case(trimmed(query)) };
        std::vector<std::pair<match_rank, std::size_t>> ranked {};
        for (std::size_t index { 0 }; index < commands.size(); ++index)
        {
            if (needle.empty())
            {
                ranked.emplace_back(match_rank::label_start, index);
                continue;
            }
            if (const std::optional<match_rank> rank { rank_of(commands[index], needle) }; rank.has_value())
                ranked.emplace_back(*rank, index);
        }
        // 같은 단계 안에서는 명령표의 차례다.
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
        std::vector<std::size_t> result {};
        result.reserve(ranked.size());
        for (const auto& [rank, index] : ranked)
            result.push_back(index);
        return result;
    }

    std::vector<menu_item_config> command_palette_items(const command_palette_config& config)
    {
        // 조합 중인 글자가 아무것도 맞히지 못하면 확정 글로 물러선다 — 한글을 치는 동안 목록이 비었다 살아나지 않는다.
        const auto any_match = [&config](const std::u8string_view candidate) { return filter_commands(config.commands, candidate).empty() == false; };
        const std::u8string_view query { search_query(config.query, any_match) };
        std::vector<menu_item_config> items {};
        for (const std::size_t index : filter_commands(config.commands, query))
        {
            if (items.size() >= config.maximum_rows)
                break;
            items.push_back(menu_item_from(config.commands[index]));
        }
        if (items.empty())
        {
            menu_item_config empty {};
            empty.label = config.empty_label;
            empty.enabled = false;
            items.push_back(std::move(empty));
        }
        return items;
    }

    float command_palette_height(const command_palette_config& config)
    {
        return 3.0f * command_palette_padding + command_palette_field_height + menu_element::height_for(palette_menu(config));
    }

    std::unique_ptr<ui_element> make_command_palette(command_palette_config config)
    {
        menu_config menu { palette_menu(config) };
        const float menu_height { menu_element::height_for(menu) };

        stack_config column_config {};
        column_config.padding = edge_insets::all(command_palette_padding);
        column_config.spacing = command_palette_padding;
        auto column { std::make_unique<stack_element>(ui_element_id { ui_element_kind::command_palette, config.owner + u8"/column" }, column_config) };
        text_input_config search {};
        search.placeholder = std::move(config.placeholder);
        search.leading_glyph = codicons::icon_search;
        auto field { std::make_unique<text_input_element>(ui_element_id { ui_element_kind::command_palette_search, config.owner }, std::move(config.query), std::move(search)) };
        field->set_access_name(field->config().placeholder);
        column->add(std::move(field), command_palette_field_height);
        column->add(std::make_unique<menu_element>(std::move(menu)), menu_height);

        // popup 창 전체를 칠한다. 메뉴는 자기 바탕과 테두리를 그 위에 그린다. 여백을 눌러도 아래로 새지 않는다.
        auto panel { std::make_unique<panel_element>(ui_element_id { ui_element_kind::command_palette, std::move(config.owner) }, panel_config {}) };
        panel->set_hit_opaque(true);
        panel->set_content(std::move(column));
        return panel;
    }

    std::unique_ptr<text_button_element> make_command_palette_entry(command_palette_entry_config config)
    {
        text_button_config look {};
        look.text = std::move(config.text);
        look.glyph = codicons::icon_search;
        auto button { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::command_palette_entry, std::move(config.owner) }, std::move(look)) };
        // 제목 표시줄 위에 겹쳐 세워도 끌기 띠가 아니라 단추가 누름을 받는다.
        button->set_caption_client(true);
        button->set_cursor(ui_cursor::hand);
        button->set_tooltip(label_with_shortcut(config.tooltip, config.shortcut));
        if (config.open != nullptr)
            button->set_action(ui_trigger::left_click, std::move(config.open));
        return button;
    }
}

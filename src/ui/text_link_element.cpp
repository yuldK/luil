#include "ui/text_link_element.h"

#include <algorithm>
#include <string>
#include <utility>

namespace luil {
    text_link_element::text_link_element(ui_element_id id, const text_link& link, std::u8string name, const bool editable, const text_link_activation& activate, const std::u8string_view hint)
        : ui_element { std::move(id) }
        , name_ { std::move(name) }
        , destination_ { link.destination }
        , editable_ { editable }
        , active_ { activate != nullptr }
    {
        set_inline_link(true);
        // 칸이 Tab의 자리다. 링크마다 자리를 주면 긴 글에서 Tab이 칸을 벗어나지 못한다.
        set_tab_stop(false);
        if (active_)
            set_action(ui_trigger::left_click, [activate, destination = destination_](const ui_action_context&) { return std::vector<input_action> { activate(destination) }; });
        // 보이는 글과 주소가 다를 수 있다. 누르기 전에 갈 곳을 보인다.
        set_tooltip(editable_ && active_ && hint.empty() == false ? destination_ + u8" · " + std::u8string { hint } : destination_);
    }

    void text_link_element::place(std::vector<rect_f> fragments, const rect_f& clip)
    {
        fragments_.clear();
        for (const rect_f& part : fragments)
        {
            const float left { std::max(part.x, clip.x) };
            const float top { std::max(part.y, clip.y) };
            const float right { std::min(part.x + part.width, clip.x + clip.width) };
            const float bottom { std::min(part.y + part.height, clip.y + clip.height) };
            if (right > left && bottom > top)
                fragments_.push_back({ left, top, right - left, bottom - top });
        }
        rect_f box {};
        if (fragments_.empty() == false)
        {
            box = fragments_.front();
            for (const rect_f& part : fragments_)
            {
                const float right { std::max(box.x + box.width, part.x + part.width) };
                const float bottom { std::max(box.y + box.height, part.y + part.height) };
                box.x = std::min(box.x, part.x);
                box.y = std::min(box.y, part.y);
                box.width = right - box.x;
                box.height = bottom - box.y;
            }
        }
        set_bounds(box);
    }

    bool text_link_element::emphasized(const interaction_snapshot& interaction) const noexcept
    {
        return active_ && interaction.hovered == id() && (editable_ == false || interaction.pointer_control);
    }

    void text_link_element::arrange(const arrange_context&)
    {
    }

    void text_link_element::draw(draw_context&, const interaction_snapshot&) const
    {
    }

    const ui_element* text_link_element::hit_test(const float x, const float y) const
    {
        if (visible() && interactive())
            for (const rect_f& part : fragments_)
                if (part.contains(x, y))
                    return this;
        return nullptr;
    }

    ui_cursor text_link_element::cursor_at(const float, const float, const interaction_snapshot& interaction) const
    {
        // 편집 칸에서는 누름이 caret을 놓으므로 글 모양이다. Ctrl을 누르면 그때 누름이 링크의 것이 된다.
        if (active_ == false || (editable_ && interaction.pointer_control == false))
            return ui_cursor::text;
        return ui_cursor::hand;
    }

    access_info text_link_element::accessibility() const
    {
        return { .role = access_role::link, .name = name_, .value = destination_ };
    }

    ui_element_id text_link_id(const ui_element_id& owner, const std::size_t index)
    {
        const std::string kind { std::to_string(static_cast<std::uint32_t>(owner.kind)) };
        const std::string number { std::to_string(index) };
        return { ui_element_kind::text_link, std::u8string { kind.begin(), kind.end() } + u8":" + owner.owner + u8"/" + std::u8string { number.begin(), number.end() } };
    }

    void make_text_link_elements(const ui_element_id& owner, const std::u8string_view text, const std::span<const text_link> links, const bool editable, const text_link_activation& activate,
        const std::u8string_view hint, const std::function<void(std::size_t index, std::unique_ptr<text_link_element> element)>& add)
    {
        for (std::size_t index { 0 }; index < links.size(); ++index)
        {
            const text_link& link { links[index] };
            if (link.begin >= link.end || link.end > text.size())
                continue;
            add(index, std::make_unique<text_link_element>(text_link_id(owner, index), link, std::u8string { text.substr(link.begin, link.end - link.begin) }, editable, activate, hint));
        }
    }
}

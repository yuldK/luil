#include "luil/ui/markdown_element.h"

#include "luil/text/utf8_text.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <md4c.h>
extern "C"
{
#include <entity.h>
}

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] std::u8string decode_entity(const std::string_view value)
        {
            if (value.size() > 3 && value[1] == '#')
            {
                unsigned codepoint { 0 };
                const bool hex { value[2] == 'x' || value[2] == 'X' };
                const auto result { std::from_chars(value.data() + (hex ? 3 : 2), value.data() + value.size() - 1, codepoint, hex ? 16 : 10) };
                if (result.ec != std::errc {} || codepoint == 0 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    codepoint = 0xfffd;
                return text::utf8_encode(static_cast<char32_t>(codepoint));
            }
            if (const ENTITY* const entity { entity_lookup(value.data(), value.size()) }; entity != nullptr)
            {
                auto result { text::utf8_encode(static_cast<char32_t>(entity->codepoints[0])) };
                if (entity->codepoints[1] != 0)
                    result += text::utf8_encode(static_cast<char32_t>(entity->codepoints[1]));
                return result;
            }
            return { reinterpret_cast<const char8_t*>(value.data()), value.size() };
        }

        [[nodiscard]] std::u8string attribute_text(const MD_ATTRIBUTE& attribute)
        {
            std::u8string result {};
            for (std::size_t index { 0 }; attribute.size > 0 && attribute.substr_offsets[index] < attribute.size; ++index)
            {
                const auto start { attribute.substr_offsets[index] };
                const std::string_view part { attribute.text + start, attribute.substr_offsets[index + 1] - start };
                if (attribute.substr_types[index] == MD_TEXT_ENTITY)
                    result += decode_entity(part);
                else if (attribute.substr_types[index] == MD_TEXT_NULLCHAR)
                    result += u8"\ufffd";
                else
                    result.append(reinterpret_cast<const char8_t*>(part.data()), part.size());
            }
            return result;
        }

        struct document_builder
        {
            struct list_state
            {
                bool ordered { false };
                unsigned next { 1 };
            };
            std::vector<markdown_block> blocks {};
            std::optional<markdown_block> current {};
            std::vector<list_state> lists {};
            unsigned quotes { 0 };
            std::size_t next_link { 1 };
            std::u8string prefix {};
            markdown_span style {};
            std::vector<markdown_span> styles {};
            std::exception_ptr error {};

            void finish()
            {
                if (current.has_value() && (current->text.empty() == false || current->rule))
                {
                    for (auto& span : current->spans)
                        span.end = std::min(span.end, current->text.size());
                    std::erase_if(current->spans, [](const auto& span) { return span.begin >= span.end; });
                    blocks.push_back(std::move(*current));
                }
                current.reset();
            }

            void start()
            {
                if (current.has_value() == false)
                {
                    current = markdown_block { .indent = static_cast<unsigned>(lists.empty() ? 0 : lists.size() - 1), .quote = quotes > 0 };
                    current->text = std::move(prefix);
                    prefix.clear();
                }
            }

            void append(const std::u8string_view value)
            {
                start();
                markdown_span span { style };
                span.begin = current->text.size();
                current->text += value;
                span.end = current->text.size();
                if (span.end > span.begin)
                    current->spans.push_back(std::move(span));
            }

            template<typename operation>
            int guard(operation&& work) noexcept
            {
                try
                {
                    work();
                    return 0;
                }
                catch (...)
                {
                    error = std::current_exception();
                    return -1;
                }
            }
        };

        int enter_block(const MD_BLOCKTYPE type, void* detail, void* data)
        {
            auto& builder { *static_cast<document_builder*>(data) };
            return builder.guard([&] {
                switch (type)
                {
                case MD_BLOCK_UL:
                case MD_BLOCK_OL:
                    builder.finish();
                    builder.lists.push_back({ type == MD_BLOCK_OL, type == MD_BLOCK_OL ? static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start : 1 });
                    break;
                case MD_BLOCK_LI: {
                    builder.finish();
                    auto& list { builder.lists.back() };
                    const auto number { std::to_string(list.next++) };
                    builder.prefix = list.ordered ? std::u8string { number.begin(), number.end() } + u8". " : u8"• ";
                    break;
                }
                case MD_BLOCK_QUOTE:
                    builder.finish();
                    ++builder.quotes;
                    break;
                case MD_BLOCK_H:
                case MD_BLOCK_P:
                case MD_BLOCK_CODE:
                case MD_BLOCK_HR:
                    builder.finish();
                    builder.start();
                    builder.current->heading = type == MD_BLOCK_H ? static_cast<MD_BLOCK_H_DETAIL*>(detail)->level : 0;
                    builder.current->code = type == MD_BLOCK_CODE;
                    builder.current->rule = type == MD_BLOCK_HR;
                    break;
                default:
                    break;
                }
            });
        }

        int leave_block(const MD_BLOCKTYPE type, void*, void* data)
        {
            auto& builder { *static_cast<document_builder*>(data) };
            return builder.guard([&] {
                if (type == MD_BLOCK_CODE && builder.current.has_value() && builder.current->text.ends_with(u8"\n"))
                    builder.current->text.pop_back();
                if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE || type == MD_BLOCK_HR || type == MD_BLOCK_LI || type == MD_BLOCK_DOC)
                    builder.finish();
                if (type == MD_BLOCK_UL || type == MD_BLOCK_OL)
                {
                    builder.finish();
                    builder.lists.pop_back();
                }
                if (type == MD_BLOCK_QUOTE)
                {
                    builder.finish();
                    --builder.quotes;
                }
            });
        }

        int enter_span(const MD_SPANTYPE type, void* detail, void* data)
        {
            auto& builder { *static_cast<document_builder*>(data) };
            return builder.guard([&] {
                builder.styles.push_back(builder.style);
                if (type == MD_SPAN_STRONG)
                    builder.style.bold = true;
                if (type == MD_SPAN_EM)
                    builder.style.italic = true;
                if (type == MD_SPAN_CODE)
                    builder.style.code = true;
                if (type == MD_SPAN_DEL)
                    builder.style.strike = true;
                if (type == MD_SPAN_A)
                {
                    builder.style.link = attribute_text(static_cast<MD_SPAN_A_DETAIL*>(detail)->href);
                    builder.style.link_id = builder.next_link++;
                }
            });
        }

        int leave_span(MD_SPANTYPE, void*, void* data)
        {
            auto& builder { *static_cast<document_builder*>(data) };
            return builder.guard([&] {
                builder.style = std::move(builder.styles.back());
                builder.styles.pop_back();
            });
        }

        int append_text(const MD_TEXTTYPE type, const MD_CHAR* text, const MD_SIZE length, void* data)
        {
            auto& builder { *static_cast<document_builder*>(data) };
            return builder.guard([&] {
                if (type == MD_TEXT_ENTITY)
                    builder.append(decode_entity({ text, length }));
                else if (type == MD_TEXT_NULLCHAR)
                    builder.append(u8"\ufffd");
                else if (type == MD_TEXT_BR)
                    builder.append(u8"\n");
                else if (type == MD_TEXT_SOFTBR)
                    builder.append(u8" ");
                else
                    builder.append({ reinterpret_cast<const char8_t*>(text), length });
            });
        }

        class markdown_link final : public ui_element
        {
        public:
            markdown_link(ui_element_id id, std::u8string name, const std::u8string& destination, const markdown_config& config)
                : ui_element { std::move(id) }
                , name_ { std::move(name) }
            {
                if (config.activate_link)
                {
                    set_cursor(ui_cursor::hand);
                    set_action(ui_trigger::left_click, [activate = config.activate_link, destination](const ui_action_context&) { return std::vector<input_action> { activate(destination) }; });
                }
                // 열 수 있는 링크는 Tab의 자리다. 초점 element의 Enter·Space가 클릭과 같은 길로 열고, 초점 테는 tree가 두른다.
                // 표시만 하는 링크는 누를 것이 없어 자리도 없다.
                set_tab_stop(config.activate_link != nullptr);
            }
            void place(std::vector<rect_f> fragments)
            {
                fragments_ = std::move(fragments);
                rect_f box {};
                if (fragments_.empty() == false)
                {
                    box = fragments_.front();
                    for (const auto& part : fragments_)
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
            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }
            void draw(draw_context&, const interaction_snapshot&) const override
            {}
            [[nodiscard]] const ui_element* hit_test(const float x, const float y) const override
            {
                if (visible() && interactive())
                    for (const auto& part : fragments_)
                        if (part.contains(x, y))
                            return this;
                return nullptr;
            }
            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::link, .name = name_ };
            }

        private:
            std::u8string name_ {};
            std::vector<rect_f> fragments_ {};
        };

        [[nodiscard]] float positive(const float value, const float fallback)
        {
            return std::isfinite(value) && value > 0.0f ? value : fallback;
        }
    } // namespace

    std::vector<markdown_block> parse_markdown(const std::u8string_view text)
    {
        if (text.size() > std::numeric_limits<MD_SIZE>::max())
            throw std::length_error { "Markdown is too large" };
        document_builder builder {};
        const MD_PARSER parser { 0, MD_FLAG_NOHTML | MD_FLAG_STRIKETHROUGH, enter_block, leave_block, enter_span, leave_span, append_text, nullptr, nullptr };
        const int result { md_parse(reinterpret_cast<const char*>(text.data()), static_cast<MD_SIZE>(text.size()), &parser, &builder) };
        if (builder.error != nullptr)
            std::rethrow_exception(builder.error);
        if (result != 0)
            throw std::runtime_error { "Markdown parsing failed" };
        return std::move(builder.blocks);
    }

    markdown_element::markdown_element(ui_element_id id, markdown_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
        , blocks_ { parse_markdown(config_.text) }
    {
        config_.font_size = positive(config_.font_size, 12.0f);
        config_.line_height = positive(config_.line_height, 18.0f);
        config_.block_spacing = positive(config_.block_spacing, 8.0f);
        for (const auto& block : blocks_)
            for (const auto& span : block.spans)
                if (span.link.empty() == false && std::find(link_ids_.begin(), link_ids_.end(), span.link_id) == link_ids_.end())
                {
                    std::u8string name {};
                    for (const auto& part : block.spans)
                        if (part.link_id == span.link_id)
                            name += block.text.substr(part.begin, part.end - part.begin);
                    const auto number { std::to_string(link_ids_.size()) };
                    link_ids_.push_back(span.link_id);
                    add_child(std::make_unique<markdown_link>(
                        ui_element_id { this->id().kind, this->id().owner + u8"/link/" + std::u8string { number.begin(), number.end() } + u8":" + span.link }, std::move(name), span.link, config_));
                }
    }

    std::vector<markdown_element::block_layout> markdown_element::layout(const measure_context& context, const float width) const
    {
        const float scale { positive(context.scale, 1.0f) };
        std::vector<block_layout> result {};
        float top { 0.0f };
        for (const auto& block : blocks_)
        {
            const float indent { (static_cast<float>(block.indent) * 14.0f + (block.quote ? 10.0f : 0.0f)) * scale };
            const float padding { block.code ? 8.0f * scale : 0.0f };
            const float size { (config_.font_size + (block.heading > 0 ? static_cast<float>(7 - block.heading) : 0.0f)) * scale };
            text_layout_request request {
                .text = block.text,
                .font_size = size,
                .face = block.code ? text_layout_face::code : text_layout_face::ui,
                .width = std::max(1.0f, width - indent - padding * 2.0f),
                .wrap = true,
                .line_height = std::max(config_.line_height * scale, size * 1.5f),
            };
            for (const auto& span : block.spans)
                request.styles.push_back({ span.begin, span.end, span.bold || block.heading > 0, span.italic, span.code });
            auto text { context.measurer != nullptr ? make_text_layout(std::move(request), *context.measurer) : make_text_layout(std::move(request)) };
            const float height { block.rule ? 4.0f * scale : text.size().height + padding * 2.0f };
            result.push_back({ std::move(text), { indent, top, std::max(0.0f, width - indent), height }, { indent + padding, top + padding } });
            top += height + config_.block_spacing * scale;
        }
        return result;
    }

    std::optional<size_f> markdown_element::preferred_size(const measure_context& context, const float available_width) const
    {
        const float scale { positive(context.scale, 1.0f) };
        const auto measured { layout(context, std::max(1.0f, available_width) * scale) };
        const float height { measured.empty() ? 1.0f : measured.back().box.y + measured.back().box.height };
        return size_f { available_width, height / scale };
    }

    void markdown_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        layouts_ = layout(context.measure(), context.slot.width);
        std::vector<std::vector<rect_f>> fragments(children().size());
        for (std::size_t index { 0 }; index < blocks_.size(); ++index)
            for (const auto& span : blocks_[index].spans)
                if (span.link.empty() == false)
                {
                    const auto link { static_cast<std::size_t>(std::find(link_ids_.begin(), link_ids_.end(), span.link_id) - link_ids_.begin()) };
                    for (auto part : layouts_[index].text.selection_rects(span.begin, span.end))
                    {
                        part.x += context.slot.x + layouts_[index].origin.x;
                        part.y += context.slot.y + layouts_[index].origin.y;
                        fragments[link].push_back(part);
                    }
                }
        for (std::size_t index { 0 }; index < children().size(); ++index)
            static_cast<markdown_link*>(children()[index].get())->place(std::move(fragments[index]));
    }
    void markdown_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const SkAutoCanvasRestore restore { &context.canvas, true };
        context.canvas.clipRect(SkRect::MakeXYWH(bounds().x, bounds().y, bounds().width, bounds().height));
        const float scale { positive(context.scale, 1.0f) };
        for (std::size_t index { 0 }; index < layouts_.size(); ++index)
        {
            const auto& block { blocks_[index] };
            const auto& placed { layouts_[index] };
            const zoom_point origin { bounds().x + placed.origin.x, bounds().y + placed.origin.y };
            const rect_f box { bounds().x + placed.box.x, bounds().y + placed.box.y, placed.box.width, placed.box.height };
            if (block.code)
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.input_background));
            if (block.rule || block.quote)
                context.canvas.drawRect(SkRect::MakeXYWH(block.quote ? box.x - 7.0f * scale : box.x, box.y, block.quote ? 2.0f * scale : box.width, block.rule ? scale : box.height),
                    solid_paint(context.palette.tooltip_border));
            draw_text_layout(context.canvas, placed.text, origin, solid_paint(context.palette.primary_foreground));
            for (const auto& span : block.spans)
            {
                const bool linked { span.link.empty() == false };
                const auto link { static_cast<std::size_t>(std::find(link_ids_.begin(), link_ids_.end(), span.link_id) - link_ids_.begin()) };
                const bool hovered { linked && children()[link]->id() == interaction.hovered };
                if (span.code || span.strike || linked)
                {
                    const auto foreground { linked ? context.palette.accent : context.palette.primary_foreground };
                    for (const auto& part : placed.text.selection_rects(span.begin, span.end))
                    {
                        if (span.code && block.code == false)
                            context.canvas.drawRect(SkRect::MakeXYWH(origin.x + part.x, origin.y + part.y, part.width, part.height), solid_paint(context.palette.input_background));
                        if (span.strike || linked)
                        {
                            const float y { origin.y + part.y + part.height * (span.strike ? 0.5f : 0.88f) };
                            context.canvas.drawRect(SkRect::MakeXYWH(origin.x + part.x, y, part.width, (hovered ? 1.5f : 1.0f) * scale), solid_paint(foreground));
                        }
                    }
                    draw_text_layout(context.canvas, placed.text, origin, solid_paint(foreground), span.begin, span.end);
                }
            }
        }
    }

    access_info markdown_element::accessibility() const
    {
        std::u8string text {};
        for (const auto& block : blocks_)
        {
            if (text.empty() == false)
                text += u8"\n";
            text += block.text;
        }
        return { .role = access_role::static_text, .name = std::move(text) };
    }

    tooltip_config make_markdown_tooltip(markdown_config config, const float width, tooltip_config appearance)
    {
        const float measured_width { positive(width, 320.0f) };
        auto content { std::make_unique<markdown_element>(ui_element_id { ui_element_kind::root, u8"markdown-tooltip" }, std::move(config)) };
        const auto size { *content->preferred_size({}, measured_width) };
        appearance.content = std::make_shared<const ui_tree>(make_arranged_tree(std::move(content), { 0.0f, 0.0f, size.width, size.height }, 1.0f));
        return appearance;
    }
} // namespace luil

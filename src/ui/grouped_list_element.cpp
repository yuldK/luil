#include "luil/ui/grouped_list_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 그룹의 머리행이다.
        // 자리는 부모가 고정(sticky) 계산으로 정하고, 여기는 받은 slot에 그리기만 한다.
        // 바탕을 불투명하게 칠해 아래로 지나가는 행을 가린다.
        class list_header_element final : public ui_element
        {
        public:
            list_header_element(std::u8string key, std::u8string title, const list_group_message_factory& activate)
                : ui_element { ui_element_id { ui_element_kind::list_header, std::move(key) } }
                , title_ { std::move(title) }
            {
                if (activate == nullptr)
                    return;
                const std::u8string value { id().owner };
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, [value, &activate](const ui_action_context&) -> std::vector<input_action> { return { activate(value) }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                // 배너 바탕은 항상 불투명하게 칠한다.
                // hover 색은 반투명이라 바탕을 대체하면 고정된 머리행 아래를
                // 지나가는 행이 비쳐 글자가 겹쳐 보인다. 위에 겹쳐 칠한다.
                context.canvas.drawRect(body, solid_paint(context.palette.notice_background));
                draw_hover_fill(context, box, id(), interaction, enabled());
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale), solid_paint(context.palette.divider));

                const float inset { 8.0f * scale };
                const float text_width { box.width - 2.0f * inset };
                if (text_width > 0.0f && title_.empty() == false)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.small_font_size * scale };
                    const SkPaint foreground { solid_paint(context.palette.secondary_foreground) };
                    static_cast<void>(draw_text_within(context.canvas, title_, box.x + inset, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::header, .name = title_ };
            }

        private:
            std::u8string title_ {};
        };
    } // namespace

    grouped_list_element::grouped_list_element(grouped_list_config config, std::vector<list_group> groups)
        : ui_element { ui_element_id { ui_element_kind::grouped_list, config.owner } }
        , config_ { std::move(config) }
    {
        // 목록은 Tab에서 한 자리다. 그 안(머리행과 앱이 담은 행)은 ↑/↓가 돈다.
        //  - 들어오는 자리를 이름 붙이지 않는다. 무엇이 "지금 항목"인지는 목록마다
        //    다르고(선택·마지막으로 본 것) 그것은 앱 상태다 — 이름이 없으면 첫
        //    항목이다.
        set_focus_group(focus_axis::vertical);
        for (list_group& group : groups)
        {
            entry item {};
            item.begin = content_height_;
            item.content_height = group.content_height > 0.0f ? group.content_height : 0.0f;

            // 내용을 먼저, 머리행을 나중에 담는다.
            // 뒤에 담긴 자식이 위에 그려지므로 고정된 머리행이 지나가는 행을 덮는다.
            if (group.content != nullptr)
            {
                item.content = group.content.get();
                add_child(std::move(group.content));
            }
            auto header { std::make_unique<list_header_element>(std::move(group.key), std::move(group.title), config_.activate) };
            item.header = header.get();
            add_child(std::move(header));

            entries_.push_back(item);
            content_height_ += list_header_height + item.content_height;
        }
    }

    float grouped_list_element::content_height() const noexcept
    {
        return content_height_;
    }

    void grouped_list_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, content_height_ * scale });

        // slot은 흘러간 만큼 위로 올라가 있으므로 창의 윗변은 여기서 되짚을 수 있다.
        const float viewport_top { context.slot.y + context.scroll_offset * scale };
        const float header_height { list_header_height * scale };
        for (const entry& item : entries_)
        {
            const float begin_y { context.slot.y + item.begin * scale };
            const float end_y { begin_y + header_height + item.content_height * scale };
            if (item.content != nullptr)
                item.content->arrange(context.for_child({ context.slot.x, begin_y + header_height, context.slot.width, item.content_height * scale }));

            // 머리행은 제자리보다 아래로는 창 위에 붙고, 그룹 끝을 넘지 못해
            // 다음 머리행이 다가오면 밀려 올라간다.
            float header_y { begin_y };
            if (header_y < viewport_top)
                header_y = viewport_top;
            if (header_y > end_y - header_height)
                header_y = end_y - header_height;
            item.header->arrange(context.for_child({ context.slot.x, header_y, context.slot.width, header_height }));
        }
    }

    void grouped_list_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
    access_info grouped_list_element::accessibility() const
    {
        return { .role = access_role::list, .name = tooltip() };
    }
} // namespace luil

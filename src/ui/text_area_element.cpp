#include "luil/ui/text_area_element.h"
#include "luil/text/grapheme.h"
#include "luil/text/text_edit.h"
#include "ui/text_layout_internal.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/font_scale.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        using text_line = text_area_element::line;
        using width_of = std::function<float(std::u8string_view)>;

        ui_color span_foreground(const text_color_role role, const ui_color_palette& palette) noexcept
        {
            switch (role)
            {
            case text_color_role::secondary: return palette.secondary_foreground;
            case text_color_role::accent: return palette.accent;
            case text_color_role::warning: return palette.warning_accent;
            case text_color_role::error: return palette.error_accent;
            default: return palette.primary_foreground;
            }
        }

        ui_color span_background(const text_background_role role, const ui_color_palette& palette) noexcept
        {
            switch (role)
            {
            case text_background_role::added: return palette.document_added_background;
            case text_background_role::removed: return palette.document_removed_background;
            case text_background_role::changed: return palette.document_changed_background;
            default: return 0;
            }
        }

        std::u8string line_number_text(std::size_t number)
        {
            std::u8string result {};
            do
            {
                result.push_back(static_cast<char8_t>(u8'0' + number % 10));
                number /= 10;
            } while (number != 0);
            std::reverse(result.begin(), result.end());
            return result;
        }

        // 줄 넘김과 hit test가 같은 글자소 경계를 쓴다.
        [[nodiscard]] std::vector<std::size_t> boundaries_of(const std::u8string_view line)
        {
            std::vector<std::size_t> boundaries { 0 };
            for (std::size_t offset { text::grapheme_next(line, 0) }; offset < line.size(); offset = text::grapheme_next(line, offset))
                boundaries.push_back(offset);
            if (line.empty() == false)
                boundaries.push_back(line.size());
            return boundaries;
        }

        // 문단 하나(줄 바꿈이 없는 글)를 폭으로 접은 자리다. 문단 처음 기준 offset이고, 문단의 끝은 넣지 않는다.
        //  - 줄마다 폭에 드는 가장 먼 문자 경계를 찾는다. 앞 줄의 글자 수에서 시작해 걸음을 두 배로 늘려 넘치는 자리를
        //    잡고 그 사이를 이분 탐색한다 — 남은 문단 전체를 재지 않으므로 줄 바꿈 없는 긴 문단에서도 한 줄에 그 줄
        //    길이의 몇 배만 잰다. 줄 길이는 대개 비슷해 첫 걸음이 거의 맞는다.
        //  - 그 안의 마지막 공백 뒤에서 접는다. 넘치는 자리가 공백이면 이어진 공백을 모두 윗줄에 매단다. 공백만 남은
        //    꼬리는 접지 않는다 — 빈 화면 줄이 생기지 않는다.
        //  - 공백이 없으면(칸보다 긴 낱말, 띄어 쓰지 않는 글) 글자 사이에서 접는다. 한 글자도 안 들면 한 글자는 둔다.
        [[nodiscard]] std::vector<std::size_t> wrap_paragraph(const std::u8string_view paragraph, const float width, const width_of& measure)
        {
            std::vector<std::size_t> cuts {};
            if (width <= 0.0f || paragraph.empty())
                return cuts;
            const std::vector<std::size_t> boundaries { boundaries_of(paragraph) };
            const std::size_t last { boundaries.size() - 1 };
            std::size_t first { 0 };
            std::size_t span { 1 };
            while (first < last)
            {
                const std::size_t start { boundaries[first] };
                const auto fits = [&](const std::size_t index) { return measure(paragraph.substr(start, boundaries[index] - start)) <= width; };
                std::size_t good { first };
                std::size_t bad { last };
                for (std::size_t step = span;; step *= 2)
                {
                    const std::size_t probe { std::min(first + step, last) };
                    if (fits(probe) == false)
                    {
                        bad = probe;
                        break;
                    }
                    good = probe;
                    if (probe == last)
                        return cuts;
                }
                while (bad - good > 1)
                {
                    const std::size_t middle { good + (bad - good) / 2 };
                    if (fits(middle))
                        good = middle;
                    else
                        bad = middle;
                }
                span = std::max<std::size_t>(good - first, 1);
                const std::size_t fit { boundaries[std::max(good, first + 1)] };
                std::size_t cut { fit };
                while (cut < paragraph.size() && paragraph[cut] == u8' ')
                    ++cut;
                if (cut == fit)
                {
                    const std::size_t space { paragraph.substr(start, fit - start).find_last_of(u8' ') };
                    if (space != std::u8string_view::npos)
                        cut = start + space + 1;
                }
                if (cut >= paragraph.size())
                    return cuts;
                cuts.push_back(cut);
                first = static_cast<std::size_t>(std::lower_bound(boundaries.begin(), boundaries.end(), cut) - boundaries.begin());
            }
            return cuts;
        }

        // 글을 화면의 줄로 나눈다. 줄 바꿈에서 나누고, 문단마다 `cuts_of`가 답한 자리에서 넘겨 접는다.
        template<typename cuts_function>
        [[nodiscard]] std::vector<text_line> split_screen_lines(const std::u8string_view text, cuts_function&& cuts_of, const bool crlf = false)
        {
            std::vector<text_line> lines {};
            std::size_t begin { 0 };
            while (true)
            {
                const std::size_t newline { crlf ? text.find_first_of(u8"\r\n", begin) : text.find(u8'\n', begin) };
                const std::size_t end { newline != std::u8string_view::npos ? newline : text.size() };
                std::size_t start { begin };
                for (const std::size_t cut : cuts_of(text.substr(begin, end - begin)))
                {
                    lines.push_back({ start, begin + cut, true });
                    start = begin + cut;
                }
                lines.push_back({ start, end, false });
                if (newline == std::u8string_view::npos)
                    return lines;
                begin = newline + 1;
                if (crlf && text[newline] == u8'\r' && begin < text.size() && text[begin] == u8'\n')
                    ++begin;
            }
        }

        [[nodiscard]] std::vector<text_line> wrap_lines(const std::u8string_view text, const float width, const width_of& measure)
        {
            return split_screen_lines(text, [width, &measure](const std::u8string_view paragraph) { return wrap_paragraph(paragraph, width, measure); });
        }

        // offset이 선 화면 줄이다. 줄 바꿈 바로 뒤와 넘겨 접은 자리는 다음 줄의 처음이다.
        //  - 줄의 끝은 오름차순이라 이분 탐색한다. 큰 글에서 caret을 물을 때마다 줄 전체를 훑지 않는다.
        [[nodiscard]] std::size_t line_of(const std::vector<text_line>& lines, const std::size_t offset) noexcept
        {
            const auto before = [offset](const text_line& row) { return offset > row.end || (offset == row.end && row.wrapped); };
            const auto found { std::partition_point(lines.begin(), lines.end(), before) };
            return found != lines.end() ? static_cast<std::size_t>(found - lines.begin()) : lines.size() - 1;
        }
        // caret이 서는 화면 줄이다. 앞 글자 쪽에 붙은 caret(affinity)이 넘겨 접은 자리에 있으면 윗줄이다.
        [[nodiscard]] std::size_t caret_line_of(const std::vector<text_line>& lines, const std::size_t offset, const bool upstream) noexcept
        {
            const std::size_t index { line_of(lines, offset) };
            if (upstream && index > 0 && lines[index - 1].wrapped && lines[index - 1].end == offset)
                return index - 1;
            return index;
        }

        // 줄 안에서 `target`(그 줄의 왼쪽 끝 기준 픽셀)에 가장 가까운 문자 경계다 (줄 안의 offset).
        //  - 한 줄 칸(`text_input_element::offset_at`)과 같은 규칙이다: 경계를 모아 폭으로 이분 탐색하고,
        //    가운데에서 가까운 쪽을 고른다.
        [[nodiscard]] std::size_t nearest_boundary(const std::u8string_view line, const float target, const width_of& measure)
        {
            if (target <= 0.0f || line.empty())
                return 0;
            const std::vector<std::size_t> boundaries { boundaries_of(line) };
            const auto wider = [&line, &measure](const float position, const std::size_t offset) { return position < measure(line.substr(0, offset)); };
            const auto after { std::upper_bound(boundaries.begin(), boundaries.end(), target, wider) };
            if (after == boundaries.end())
                return line.size();
            if (after == boundaries.begin())
                return *after;
            const std::size_t before { *(after - 1) };
            const float after_width { measure(line.substr(0, *after)) };
            return target - measure(line.substr(0, before)) < after_width - target ? before : *after;
        }

        // 줄을 그리는 몸통이다. 그리는 것은 칸이 하고, 이것은 흘린 만큼 밀린 자리만 갖는다 —
        // 흘리는 창(`scroll_area_element`)이 내용을 그 자리에 배치하고 자른다.
        class text_area_body final : public ui_element
        {
        public:
            using ui_element::ui_element;

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {}
        };
    } // namespace

    text_area_element::text_area_element(const ui_element_id id, text_input_view value, text_area_config config)
        : ui_element { id }
        , value_ { std::move(value) }
        , config_ { std::move(config) }
    {
        // 누름·끌기·더블 클릭은 interaction controller가 직접 다룬다 (`text_input_element`와 같다).
        // 여기서는 클릭을 흡수해 hit test의 대상이 되게만 한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });

        // 내용 높이는 접은 줄 수의 함수라 `arrange`가 정한다.
        scroll_area_config area {};
        area.owner = u8"text-area:" + id.owner;
        area.scroll_offset = config_.scroll_offset;
        area.scroll = config_.scroll;
        area.scroll_to = config_.scroll_to;
        // 칸이 세로 키를 가진다 — 막대가 Tab의 자리를 하나 더 갖지 않는다 (목록과 같은 판정).
        area.bar_tab_stop = false;
        auto scroll_area { std::make_unique<scroll_area_element>(std::move(area)) };
        auto body { std::make_unique<text_area_body>(ui_element_id { ui_element_kind::none }) };
        body_ = body.get();
        scroll_area->set_content(std::move(body));
        area_ = scroll_area.get();
        add_child(std::move(scroll_area));
        if (config_.wrap == false && config_.scroll_horizontal != nullptr)
        {
            scrollbar_config bar {};
            bar.axis = scrollbar_axis::horizontal;
            bar.scroll = config_.scroll_horizontal;
            bar.scroll_to = config_.scroll_horizontal_to;
            auto element { std::make_unique<scrollbar_element>(ui_element_id { ui_element_kind::scroll_area_bar, u8"text-area-x:" + id.owner }, std::move(bar)) };
            element->set_tab_stop(false);
            horizontal_bar_ = element.get();
            add_child(std::move(element));
        }
    }

    float text_area_element::scale() const noexcept
    {
        return scale_;
    }

    float text_area_element::font_size() const noexcept
    {
        return scaled_font_size(config_.font_size > 0.0f ? config_.font_size : text_area_font_size, scale_);
    }

    float text_area_element::line_height_logical() const noexcept
    {
        return text_area_line_height * (config_.font_size > 0.0f ? config_.font_size / text_area_font_size : 1.0f);
    }

    const text_measurer& text_area_element::measure_with(const text_measurer& fallback) const
    {
        return config_.face == text_face::code ? code_measurer_ : fallback;
    }

    float text_area_element::content_width() const noexcept
    {
        return content_width_;
    }

    float text_area_element::scroll_delta_to_reveal(const rect_f& target) const
    {
        const rect_f viewport { area_->viewport() };
        const float gutter { gutter_width_ * scale_ };
        return luil::scroll_delta_to_reveal(target.x, target.width, viewport.x + gutter, std::max(0.0f, viewport.width - gutter)) / scale_;
    }

    std::optional<rect_f> text_area_element::line_rect(const std::size_t logical_line) const
    {
        const auto first { std::lower_bound(logical_lines_.begin(), logical_lines_.end(), logical_line) };
        if (first == logical_lines_.end() || *first != logical_line)
            return std::nullopt;
        const auto last { std::upper_bound(first, logical_lines_.end(), logical_line) };
        const float height { line_height_logical() * scale_ };
        return rect_f { text_left(), text_top() + static_cast<float>(first - logical_lines_.begin()) * height,
            std::max(0.0f, area_->viewport().width - gutter_width_ * scale_), static_cast<float>(last - first) * height
        };
    }

    std::optional<std::size_t> text_area_element::offset_at(const zoom_point point) const
    {
        return layout_measurer_ ? offset_at_point(point.x, point.y, layout_measurer_) : std::nullopt;
    }

    std::vector<input_action> text_area_element::scroll_to_offset(const std::size_t offset) const
    {
        std::vector<input_action> actions {};
        if (layout_measurer_ == nullptr)
            return actions;
        const auto caret { text::text_edit_clamp_offset(value_.text, offset) };
        const auto bounds { text_span_bounds({ value_.text, caret, caret, caret }, layout_measurer_) };
        if (bounds.has_value() == false)
            return actions;
        if (config_.scroll != nullptr)
        {
            const float delta { area_->scroll_delta_to_reveal(*bounds) };
            if (delta != 0.0f)
                actions.push_back(config_.scroll(delta));
        }
        if (config_.wrap == false && config_.scroll_horizontal != nullptr)
        {
            const float delta { scroll_delta_to_reveal(*bounds) };
            if (delta != 0.0f)
                actions.push_back(config_.scroll_horizontal(delta));
        }
        return actions;
    }

    std::u8string_view text_area_element::displayed_text() const noexcept
    {
        return value_.displayed_text();
    }

    std::size_t text_area_element::displayed_caret() const noexcept
    {
        const std::u8string_view text { displayed_text() };
        const std::size_t caret { value_.composing ? value_.composition_caret : value_.caret };
        return caret < text.size() ? caret : text.size();
    }

    std::size_t text_area_wrap_cache::size() const
    {
        const std::scoped_lock lock { mutex_ };
        return entries_.size();
    }

    std::size_t text_area_wrap_cache::key_hash::operator()(const std::u8string_view key) const noexcept
    {
        return std::hash<std::u8string_view> {}(key);
    }

    const std::vector<text_area_element::line>& text_area_element::lines_for(const std::u8string_view text, const width_of& measure, std::vector<line>& storage) const
    {
        if (text == displayed_text())
            return lines_;
        if (native_layout_)
        {
            const auto layout { remake_text_layout(font_layout_, layout_request(text, 0, config_.wrap)) };
            for (const auto& row : layout.lines())
                storage.push_back({ row.begin, row.end, row.wrapped });
            return storage;
        }
        storage = wrap_lines(text, wrap_width_, measure);
        return storage;
    }

    std::vector<text_layout_style> text_area_element::metric_styles(const std::size_t begin, const std::size_t end) const
    {
        std::vector<text_layout_style> styles {};
        if (config_.spans != nullptr)
            for (const auto& span : text_spans_in_range(*config_.spans, begin, end))
                if (span.bold)
                {
                    const auto from { std::max(span.begin, begin) - begin };
                    const auto to { std::min(span.end, end) - begin };
                    if (styles.empty() == false && styles.back().end == from)
                        styles.back().end = to;
                    else
                        styles.push_back({ from, to, true });
                }
        return styles;
    }

    text_layout_request text_area_element::layout_request(const std::u8string_view text, const std::size_t begin, const bool wrap) const
    {
        text_layout_request request {};
        request.text = text;
        request.font_size = font_size();
        request.face = config_.face == text_face::code ? text_layout_face::code : text_layout_face::ui;
        request.width = wrap ? wrap_width_ : 0.0f;
        request.wrap = wrap;
        request.line_height = line_height_logical() * scale_;
        request.styles = metric_styles(begin, begin + text.size());
        return request;
    }

    text_layout text_area_element::row_layout(const std::u8string_view text, const line& row) const
    {
        if (native_layout_ == false)
            return {};
        const auto displayed { displayed_text() };
        if ((text.data() == displayed.data() && text.size() == displayed.size()) || text == displayed)
        {
            const auto found { std::lower_bound(row_layouts_.begin(), row_layouts_.end(), row.begin,
                [](const auto& item, const auto at) { return item.first < at; })
            };
            if (found != row_layouts_.end() && found->first == row.begin)
                return found->second;
        }
        return remake_text_layout(font_layout_, layout_request(text.substr(row.begin, row.end - row.begin), row.begin, false));
    }

    std::vector<text_area_element::line> text_area_element::wrap_displayed(const width_of& measure)
    {
        const std::u8string_view text { displayed_text() };
        const auto entry_for = [&](const std::u8string_view paragraph) {
            text_area_wrap_cache::entry entry {};
            if (native_layout_)
            {
                const auto begin { static_cast<std::size_t>(paragraph.data() - text.data()) };
                auto request { layout_request(paragraph, begin, config_.wrap) };
                entry.styles = request.styles;
                const auto layout { remake_text_layout(font_layout_, std::move(request)) };
                for (const auto& row : layout.lines())
                    if (row.wrapped)
                        entry.cuts.push_back(row.end);
                entry.width = layout.size().width / scale_;
            }
            else
            {
                entry.cuts = wrap_paragraph(paragraph, wrap_width_, measure);
                entry.width = config_.wrap ? 0.0f : measure(paragraph) / scale_;
            }
            return entry;
        };
        if (config_.wrap_cache == nullptr)
        {
            return split_screen_lines(text, [&](const std::u8string_view paragraph) {
                auto entry { entry_for(paragraph) };
                content_width_ = std::max(content_width_, entry.width);
                return entry.cuts;
            }, native_layout_);
        }
        text_area_wrap_cache& cache { *config_.wrap_cache };
        const std::scoped_lock lock { cache.mutex_ };
        const float sample { measure(u8"Wg 0가") };
        const auto face { config_.face == text_face::code ? text_layout_face::code : text_layout_face::ui };
        const auto revision { native_layout_ ? text_layout_revision() : 0 };
        if (cache.width_ != wrap_width_ || cache.font_size_ != font_size() || cache.sample_width_ != sample || cache.wrap_ != config_.wrap
            || cache.native_ != native_layout_ || cache.face_ != face || cache.font_revision_ != revision)
        {
            cache.entries_.clear();
            cache.width_ = wrap_width_;
            cache.wrap_ = config_.wrap;
            cache.font_size_ = font_size();
            cache.sample_width_ = sample;
            cache.native_ = native_layout_;
            cache.face_ = face;
            cache.font_revision_ = revision;
        }
        const std::uint64_t generation { ++cache.generation_ };
        const auto cached_cuts = [&](const std::u8string_view paragraph) -> const std::vector<std::size_t>& {
            auto found { cache.entries_.find(paragraph) };
            if (found == cache.entries_.end())
                found = cache.entries_.emplace(std::u8string { paragraph }, entry_for(paragraph)).first;
            else if (native_layout_)
            {
                const auto begin { static_cast<std::size_t>(paragraph.data() - text.data()) };
                if (found->second.styles != metric_styles(begin, begin + paragraph.size()))
                    found->second = entry_for(paragraph);
            }
            content_width_ = std::max(content_width_, found->second.width);
            found->second.used = generation;
            return found->second.cuts;
        };
        std::vector<line> lines { split_screen_lines(text, cached_cuts, native_layout_) };
        std::erase_if(cache.entries_, [generation](const auto& item) { return item.second.used != generation; });
        return lines;
    }

    float text_area_element::text_left() const noexcept
    {
        return area_->viewport().x + (text_area_padding_x + gutter_width_ - horizontal_offset_) * scale_;
    }

    float text_area_element::text_top() const noexcept
    {
        return body_->bounds().y + text_area_padding_y * scale_;
    }

    void text_area_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;

        // 줄은 여기서 접는다 — 접은 줄 수가 내용 높이고, 내용 높이가 흘리는 범위와 막대다.
        //  - 흘리는 칸은 막대 자리를 늘 뺀 폭으로 접는다. 막대는 넘칠 때만 서는데, 그 자리를 그때만 빼면 줄 하나가
        //    넘치는 순간 글 전체가 좁은 폭으로 다시 접히고, 지우면 다시 넓게 펴진다.
        row_layouts_.clear();
        native_layout_ = false;
        font_layout_ = {};
        if (ui_text_shaping_enabled() && context.measurer == nullptr)
        {
            font_layout_ = make_text_layout(layout_request({}, 0, false));
            native_layout_ = font_layout_.backend() != text_layout_backend::unavailable;
        }
        code_measurer_ = context.measurer != nullptr ? *context.measurer : default_code_text_measurer();
        const text_measurer& measurer { measure_with(context.measure_text()) };
        const float size { font_size() };
        const auto measure { [this, &measurer, size](const std::u8string_view value) { return measure_with(measurer)(value, size); } };
        layout_measurer_ = measurer;
        gutter_widths_.clear();
        gutter_width_ = 0.0f;
        std::size_t logical_count { 0 };
        if (config_.gutters.empty() == false)
        {
            logical_count = 1;
            const auto text { displayed_text() };
            for (std::size_t index { 0 }; index < text.size(); ++index)
                if (text[index] == u8'\n' || (native_layout_ && text[index] == u8'\r'))
                {
                    ++logical_count;
                    if (native_layout_ && text[index] == u8'\r' && index + 1 < text.size() && text[index + 1] == u8'\n')
                        ++index;
                }
        }
        for (const auto& column : config_.gutters)
        {
            const float width { column.width > 0.0f ? column.width : (measure(line_number_text(logical_count)) / scale_ + 12.0f) };
            gutter_widths_.push_back(width);
            gutter_width_ += width;
        }
        const float bar { config_.scroll != nullptr ? std::min(scroll_area_bar_width * scale_, context.slot.width) : 0.0f };
        wrap_width_ = config_.wrap ? context.slot.width - bar - (2.0f * text_area_padding_x + gutter_width_) * scale_ : 0.0f;
        content_width_ = 0.0f;
        lines_ = wrap_displayed(measure);
        logical_lines_.clear();
        std::size_t logical { 0 };
        for (const auto& row : lines_)
        {
            logical_lines_.push_back(logical);
            if (row.wrapped == false)
                ++logical;
        }
        content_width_ += 2.0f * text_area_padding_x;
        area_->set_content_height(static_cast<float>(lines_.size()) * line_height_logical() + 2.0f * text_area_padding_y);
        const float horizontal_height { horizontal_bar_ != nullptr ? std::min(scrollbar_visual_width * scale_ + 8.0f * scale_, context.slot.height) : 0.0f };
        rect_f area_slot { context.slot };
        area_slot.height -= horizontal_height;
        area_->arrange(context.for_child(area_slot));
        const float viewport_width { std::max(0.0f, area_->viewport().width / scale_ - gutter_width_) };
        horizontal_offset_ = config_.wrap ? 0.0f : clamp_scroll(content_width_, viewport_width, config_.scroll_x);
        if (horizontal_bar_ != nullptr)
        {
            horizontal_bar_->set_metrics(content_width_, viewport_width, horizontal_offset_);
            horizontal_bar_->arrange(context.for_child({ area_slot.x, area_slot.y + area_slot.height, area_->viewport().width, horizontal_height }));
        }
        if (config_.wrap == false && config_.scroll_horizontal != nullptr && content_width_ > viewport_width)
            set_scroll_source(scroll_source { config_.scroll_horizontal, scale_, scroll_axis::horizontal, horizontal_offset_ <= 0.0f, horizontal_offset_ >= content_width_ - viewport_width });
        else
            set_scroll_source(std::nullopt);
        if (native_layout_)
        {
            const float height { line_height_logical() * scale_ };
            const float first { std::floor((area_->viewport().y - text_top()) / height) };
            const float last { std::ceil((area_->viewport().y + area_->viewport().height - text_top()) / height) };
            const auto from { std::min(lines_.size(), first > 0.0f ? static_cast<std::size_t>(first) : 0u) };
            const auto to { std::min(lines_.size(), last > 0.0f ? static_cast<std::size_t>(last) + 1 : 0u) };
            for (std::size_t index { from }; index < to; ++index)
            {
                const auto row { lines_[index] };
                row_layouts_.emplace_back(row.begin, remake_text_layout(font_layout_, layout_request(displayed_text().substr(row.begin, row.end - row.begin), row.begin, false)));
            }
        }
    }

    std::optional<text_input_snapshot> text_area_element::text_input() const
    {
        text_input_snapshot snapshot {
            value_.text,
            value_.caret,
            value_.anchor,
            value_.composing,
            value_.composition_text,
            value_.composition_caret,
            value_.applied_sequence,
        };
        snapshot.multiline = true;
        snapshot.read_only = config_.read_only;
        snapshot.caret_upstream = value_.caret_upstream;
        return snapshot;
    }

    std::optional<rect_f> text_area_element::text_span_bounds(const text_span_query& query, const text_measurer& measurer) const
    {
        if (measurer == nullptr)
            return std::nullopt;
        // 질의한 문서(조합 글 포함)를 그대로 잰다 — 확정 글로 재면 조합이 시작되는 순간 offset이 문서를 넘는다.
        const float size { font_size() };
        const auto measure { [this, &measurer, size](const std::u8string_view value) { return measure_with(measurer)(value, size); } };
        const std::u8string_view text { query.text };
        const std::size_t begin { std::min(query.begin, text.size()) };
        const std::size_t end { std::max(std::min(query.end, text.size()), begin) };
        std::vector<line> storage {};
        const std::vector<line>& lines { lines_for(text, measure, storage) };
        // 발행된 caret 자리를 묻는 질의(빈 구간, 같은 글·같은 caret)에만 affinity를 쓴다 — IME 후보 창과 보조 기술이
        // 그려진 caret과 같은 줄을 가리킨다. 조합 중의 문서는 확정 글과 달라 여기에 걸리지 않는다.
        const bool published_caret { begin == end && begin == query.caret && query.caret == value_.caret && text == std::u8string_view { value_.text } && value_.composing == false };
        const std::size_t index { caret_line_of(lines, begin, published_caret && value_.caret_upstream) };
        const line row { lines[index] };
        const float line_height { line_height_logical() * scale_ };
        const float top { text_top() + static_cast<float>(index) * line_height };
        const std::u8string_view content { text.substr(row.begin, row.end - row.begin) };
        const auto layout { row_layout(text, row) };
        const auto position { [&](const std::size_t at) { return layout.valid() ? layout.caret_rect(at).x : measure(content.substr(0, at)); } };
        const float from { text_left() + position(begin - row.begin) };
        const float to { text_left() + position(std::min(end, row.end) - row.begin) };
        return rect_f { from, top, to - from, line_height };
    }

    std::vector<std::size_t> text_area_element::text_line_starts() const
    {
        std::vector<std::size_t> starts {};
        starts.reserve(lines_.size());
        for (const line& row : lines_)
            starts.push_back(row.begin);
        return starts;
    }

    std::optional<std::size_t> text_area_element::offset_at_point(const float x, const float y, const text_measurer& measurer) const
    {
        const std::optional<text_caret_position> position { caret_at_point(x, y, measurer) };
        return position.has_value() ? std::optional<std::size_t> { position->offset } : std::nullopt;
    }

    std::optional<text_caret_position> text_area_element::caret_at_point(const float x, const float y, const text_measurer& measurer) const
    {
        const std::u8string_view text { value_.text };
        if (measurer == nullptr)
            return text_caret_position { text.size(), false };
        const float size { font_size() };
        const auto measure { [this, &measurer, size](const std::u8string_view value) { return measure_with(measurer)(value, size); } };
        std::vector<line> storage {};
        const std::vector<line>& lines { lines_for(text, measure, storage) };
        const float line_height { line_height_logical() * scale_ };
        const float top { text_top() };
        // 첫 줄 위는 글의 처음, 마지막 줄 아래는 글의 끝이다 — 첫 줄에서 ↑, 마지막 줄에서 ↓가 그 끝으로 간다.
        if (y < top)
            return text_caret_position { 0, false };
        const float index { std::floor((y - top) / line_height) };
        if (index >= static_cast<float>(lines.size()))
            return text_caret_position { text.size(), false };
        const line row { lines[static_cast<std::size_t>(index)] };
        const std::u8string_view content { text.substr(row.begin, row.end - row.begin) };
        const auto layout { row_layout(text, row) };
        const std::size_t offset { row.begin + (layout.valid() ? layout.hit_test({ x - text_left(), 0.0f }).offset : nearest_boundary(content, x - text_left(), measure)) };
        // 접은 줄의 끝은 아랫줄의 처음과 offset이 같다. 누른 줄이 윗줄이므로 caret을 그 줄에 붙인다.
        return text_caret_position { offset, row.wrapped && offset == row.end };
    }

    std::optional<std::size_t> text_area_element::offset_at(const float x, const text_measurer& measurer) const
    {
        // y가 없으면 caret이 선 줄에서 판정한다.
        if (measurer == nullptr)
            return value_.text.size();
        const float size { font_size() };
        const auto measure { [this, &measurer, size](const std::u8string_view value) { return measure_with(measurer)(value, size); } };
        std::vector<line> storage {};
        const std::size_t index { caret_line_of(lines_for(value_.text, measure, storage), std::min(value_.caret, value_.text.size()), value_.caret_upstream) };
        const float line_height { line_height_logical() * scale_ };
        return offset_at_point(x, text_top() + (static_cast<float>(index) + 0.5f) * line_height, measurer);
    }

    void text_area_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool focused { interaction.focused_input == id() };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { context.metrics.control_corner_radius * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.input_background));
        // 초점을 받은 칸은 테두리를 강조한다 (한 줄 칸과 같은 표시).
        SkPaint border { solid_paint(focused ? context.palette.accent : context.palette.input_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        // 줄은 배치가 접은 그대로다. 접을 때 잰 글꼴과 여기서 그리는 글꼴은 같은 UI 글꼴이다.
        const SkFont font { sk_ref_sp(config_.face == text_face::code ? context.code_typeface : context.ui_typeface), font_size() };
        const auto measure { [&font](const std::u8string_view value) { return measure_text(value, font); } };
        const std::u8string_view text { displayed_text() };
        const std::vector<line>& lines { lines_ };
        const rect_f viewport { area_->viewport() };
        const float line_height { line_height_logical() * scale };
        const float top { text_top() };
        const float left { text_left() };
        const auto baseline_of = [this, &font, &text, &lines, top, line_height](const std::size_t index) {
            const auto layout { row_layout(text, lines[index]) };
            return top + static_cast<float>(index) * line_height + (layout.valid() ? layout.lines().front().baseline : centered_text_baseline(font, line_height));
        };

        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(viewport.x + gutter_width_ * scale, viewport.y, std::max(0.0f, viewport.width - gutter_width_ * scale), viewport.height));

        if (text.empty() && focused == false && config_.placeholder.empty() == false)
        {
            SkPaint placeholder { solid_paint(context.palette.primary_foreground) };
            placeholder.setAlphaf(0.5f);
            static_cast<void>(draw_text_within(context.canvas, config_.placeholder, left, baseline_of(0), viewport.width - 2.0f * text_area_padding_x * scale, font, placeholder));
        }

        // 창에 걸치는 줄만 그린다. 긴 글에서도 그리기가 창 크기에 머문다.
        const float first_row { std::floor((viewport.y - top) / line_height) };
        const std::size_t first { first_row > 0.0f ? static_cast<std::size_t>(first_row) : 0u };
        const float last_row { std::ceil((viewport.y + viewport.height - top) / line_height) };
        const std::size_t last { std::min(lines.size(), last_row > 0.0f ? static_cast<std::size_t>(last_row) + 1u : 0u) };

        // 조합 중에는 선택 대신 조합 구간에 밑줄을 긋는다 — 그동안 선택은 IME의 것이다.
        const std::size_t selection_begin { value_.composing ? 0u : std::min(value_.caret, value_.anchor) };
        const std::size_t selection_end { value_.composing ? 0u : std::max(value_.caret, value_.anchor) };
        const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
        // 고른 줄 바꿈을 보이는 폭이다. 빈 줄을 골라도 고른 줄이 보인다.
        const float newline_width { 4.0f * scale };
        // 구간이 그 화면 줄에 걸치는가. 접은 자리에서 시작하는 구간은 아랫줄의 것이다.
        const auto touches = [](const line& row, const std::size_t from, const std::size_t to) { return to > row.begin && (from < row.end || (from == row.end && row.wrapped == false)); };
        for (std::size_t index = first; index < last; ++index)
        {
            const line row { lines[index] };
            const std::u8string_view content { text.substr(row.begin, row.end - row.begin) };
            const auto layout { row_layout(text, row) };
            const auto position { [&](const std::size_t at) { return layout.valid() ? layout.caret_rect(at).x : measure(content.substr(0, at)); } };
            const float row_top { top + static_cast<float>(index) * line_height };
            const auto logical { logical_lines_[index] };
            if (config_.lines != nullptr)
            {
                const auto found { std::lower_bound(config_.lines->begin(), config_.lines->end(), logical,
                    [](const auto& item, const auto value) { return item.line < value; })
                };
                if (found != config_.lines->end() && found->line == logical)
                    context.canvas.drawRect(SkRect::MakeXYWH(viewport.x + gutter_width_ * scale, row_top, viewport.width - gutter_width_ * scale, line_height),
                        solid_paint(span_background(static_cast<text_background_role>(found->background), context.palette)));
            }
            const std::span<const text_span> styled { config_.spans != nullptr ? text_spans_in_range(*config_.spans, row.begin, row.end) : std::span<const text_span> {} };
            const auto range_rect = [&](const std::size_t begin, const std::size_t end) {
                const auto from { std::max(begin, row.begin) };
                const auto to { std::min(end, row.end) };
                const float x { left + position(from - row.begin) };
                return SkRect::MakeXYWH(x, row_top, position(to - row.begin) - position(from - row.begin), line_height);
            };
            for (const auto& span : styled)
                if (span.background != text_background_role::none)
                    context.canvas.drawRect(range_rect(span.begin, span.end), solid_paint(span_background(span.background, context.palette)));
            const auto highlights { text_highlights_in_range(config_.highlights, row.begin, row.end) };
            for (const auto& match : highlights)
            {
                const auto match_box { range_rect(match.begin, match.end) };
                context.canvas.drawRect(match_box, solid_paint(context.palette.document_search_background));
                if (match.current)
                {
                    SkPaint match_border { solid_paint(context.palette.accent) };
                    match_border.setStyle(SkPaint::kStroke_Style);
                    context.canvas.drawRect(match_box, match_border);
                }
            }
            std::optional<SkRect> selection {};
            if (selection_begin != selection_end && touches(row, selection_begin, selection_end))
            {
                const std::size_t from_offset { std::max(selection_begin, row.begin) - row.begin };
                const std::size_t to_offset { std::min(selection_end, row.end) - row.begin };
                const float from { left + position(from_offset) };
                float to { left + position(to_offset) };
                if (selection_end > row.end && row.wrapped == false)
                    to += newline_width;
                selection = SkRect::MakeXYWH(from, row_top, to - from, line_height);
                context.canvas.drawRect(*selection, solid_paint(context.palette.selection_background));
            }
            if (content.empty() == false)
            {
                const auto draw_content = [&](const std::optional<ui_color> override_color) {
                    const auto draw_piece = [&](const std::size_t from, const std::size_t to, const SkFont& piece_font, const ui_color color) {
                        if (from >= to)
                            return;
                        if (layout.valid())
                            draw_text_layout(context.canvas, layout, { left, row_top }, solid_paint(override_color.value_or(color)), from - row.begin, to - row.begin);
                        else
                            draw_text(context.canvas, text.substr(from, to - from), left + measure(text.substr(row.begin, from - row.begin)),
                                baseline_of(index), piece_font, solid_paint(override_color.value_or(color)));
                    };
                    std::size_t cursor { row.begin };
                    for (const auto& span : styled)
                    {
                        const auto from { std::max(span.begin, row.begin) };
                        const auto to { std::min(span.end, row.end) };
                        draw_piece(cursor, from, font, foreground.getColor());
                        SkFont styled_font { font };
                        styled_font.setEmbolden(span.bold);
                        draw_piece(from, to, styled_font, enabled() ? span_foreground(span.color, context.palette) : context.palette.disabled_foreground);
                        cursor = to;
                    }
                    draw_piece(cursor, row.end, font, foreground.getColor());
                };
                draw_content(std::nullopt);
                for (const auto& match : highlights)
                {
                    context.canvas.save();
                    context.canvas.clipRect(range_rect(match.begin, match.end));
                    draw_content(context.palette.document_search_foreground);
                    context.canvas.restore();
                }
                // 고른 구간의 글자는 그 바탕의 짝이다 (한 줄 칸과 같은 규칙 — 고대비에서만 바뀐다).
                if (selection.has_value() && enabled() && (styled.empty() == false || highlights.empty() == false || context.palette.selection_foreground != context.palette.primary_foreground))
                {
                    context.canvas.save();
                    context.canvas.clipRect(*selection);
                    draw_content(context.palette.selection_foreground);
                    context.canvas.restore();
                }
            }
        }

        // 조합 구간은 밑줄이다. 여러 줄에 걸친 조합은 줄마다 긋는다.
        if (value_.composing && value_.composing_end > value_.composing_begin)
        {
            const std::size_t underline_begin { std::min(value_.composing_begin, text.size()) };
            const std::size_t underline_end { std::min(value_.composing_end, text.size()) };
            for (std::size_t index = first; index < last; ++index)
            {
                const line row { lines[index] };
                if (touches(row, underline_begin, underline_end) == false)
                    continue;
                const std::u8string_view content { text.substr(row.begin, row.end - row.begin) };
                const auto layout { row_layout(text, row) };
                const auto position { [&](const std::size_t at) { return layout.valid() ? layout.caret_rect(at).x : measure(content.substr(0, at)); } };
                const float from { left + position(std::max(underline_begin, row.begin) - row.begin) };
                const float to { left + position(std::min(underline_end, row.end) - row.begin) };
                context.canvas.drawRect(SkRect::MakeXYWH(from, baseline_of(index) + 2.0f * scale, to - from, 1.0f * scale), solid_paint(context.palette.accent));
            }
        }

        // caret은 초점을 받은 동안 깜빡인다. 위상은 초점을 받은 시각이다 (한 줄 칸과 같다).
        const bool caret_on { focused && interaction.focus_started_at.has_value() && ((context.now - *interaction.focus_started_at) / caret_blink_interval) % 2 == 0 };
        if (caret_on)
        {
            const std::size_t caret { displayed_caret() };
            const std::size_t index { caret_line_of(lines, caret, value_.composing == false && value_.caret_upstream) };
            const line row { lines[index] };
            const auto layout { row_layout(text, row) };
            const float caret_x { left + (layout.valid() ? layout.caret_rect(caret - row.begin).x : measure(text.substr(row.begin, caret - row.begin))) };
            const float row_top { top + static_cast<float>(index) * line_height };
            context.canvas.drawRect(SkRect::MakeXYWH(caret_x, row_top + 2.0f * scale, 1.0f * scale, line_height - 4.0f * scale), solid_paint(context.palette.primary_foreground));
        }
        context.canvas.restore();

        if (gutter_width_ > 0.0f)
        {
            context.canvas.save();
            context.canvas.clipRect(SkRect::MakeXYWH(viewport.x, viewport.y, std::min(viewport.width, gutter_width_ * scale), viewport.height));
            context.canvas.drawRect(SkRect::MakeXYWH(viewport.x, viewport.y, gutter_width_ * scale, viewport.height), solid_paint(context.palette.input_background));
            for (std::size_t index { first }; index < last; ++index)
            {
                const auto logical { logical_lines_[index] };
                if (index > 0 && logical_lines_[index - 1] == logical)
                    continue;
                float x { viewport.x };
                for (std::size_t column { 0 }; column < config_.gutters.size(); ++column)
                {
                    const auto& item { config_.gutters[column] };
                    const std::u8string label { item.kind == gutter_kind::line_number ? line_number_text(logical + 1) : (item.text ? item.text(logical) : std::u8string {}) };
                    const float width { gutter_widths_[column] * scale };
                    draw_text(context.canvas, label, x + width - 6.0f * scale - measure(label), baseline_of(index), font, solid_paint(context.palette.secondary_foreground));
                    x += width;
                }
                if (config_.lines != nullptr)
                {
                    const auto found { std::lower_bound(config_.lines->begin(), config_.lines->end(), logical, [](const auto& item, const auto value) { return item.line < value; }) };
                    if (found != config_.lines->end() && found->line == logical && found->marker != 0)
                        draw_centered_glyph(context.canvas, found->marker, { viewport.x, top + static_cast<float>(index) * line_height, 12.0f * scale, line_height },
                            SkFont { sk_ref_sp(context.codicon_typeface), font_size() }, solid_paint(context.palette.accent));
                }
            }
            context.canvas.restore();
        }
        // 막대는 글 위에 선다.
        draw_children(context, interaction);
    }

    std::optional<std::chrono::steady_clock::time_point> text_area_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        if (interaction.focused_input != id() || interaction.focus_started_at.has_value() == false)
            return std::nullopt;
        const auto phase { (context.now - *interaction.focus_started_at) % caret_blink_interval };
        return context.now + (caret_blink_interval - phase);
    }

    access_info text_area_element::accessibility() const
    {
        // 값은 확정 글이다 — 조합 글은 아직 사용자의 글이 아니다 (`text_input_element`와 같은 판단).
        return { .role = access_role::edit, .name = config_.placeholder, .value = value_.text };
    }
} // namespace luil

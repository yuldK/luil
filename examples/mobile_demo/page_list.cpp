#include "mobile_demo/page_list.h"

#include "luil/generated/codicons.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/glyph_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/stack_element.h"
#include "mobile_demo/chart_page.h"
#include "mobile_demo/files_page.h"

#include <algorithm>
#include <array>
#include <utility>

namespace mobile_demo {
    namespace {
        // 목록 한 칸의 높이다. 제목과 설명 두 줄에 손가락이 넉넉히 닿는다.
        constexpr float entry_height { 64.0f };
        // 칸 안쪽의 여백, 아이콘과 다음 장 표시의 폭, 그 사이 간격이다.
        constexpr float entry_padding_x { 16.0f };
        constexpr float entry_padding_y { 10.0f };
        constexpr float entry_icon_width { 24.0f };
        constexpr float entry_chevron_width { 20.0f };
        constexpr float entry_spacing { 16.0f };

        [[nodiscard]] float entry_line_height(const float font_size, const float height)
        {
            return luil::text_line_height(font_size, height - font_size);
        }

        struct summary_line
        {
            luil::label_config config {};
            float height { 0.0f };
        };

        // 칸의 설명 줄이다. 들어가면 한 줄 label 그대로이고, 넘치면 끝을 줄이지 않고 줄을 바꾼다.
        //  - 폭이 좁은 왼쪽 판(260dp)은 글꼴 배율 1.1부터 긴 설명이 넘친다. 글을 줄여도 큰 글꼴에서 다시 잘리고 기기 글꼴마다 폭이 다르다.
        //  - 한 줄일 때를 그대로 두어 폭이 넉넉한 세로 목록의 글 자리와 칸 높이가 바뀌지 않는다. 넘친 칸만 그 줄만큼 커진다.
        [[nodiscard]] summary_line make_summary(const page_entry& entry, const float width, const float scale, const luil::text_measurer* const measurer)
        {
            summary_line line {};
            line.config.text = std::u8string { entry.summary };
            line.config.font_size = note_text_size;
            line.config.color = luil::label_color_role::dim;
            line.config.wrap = true;
            // 줄 나눔 규칙을 스타일에 맡기면 여기서 잴 때와 배치할 때의 규칙이 갈려 줄 수가 달라질 수 있다.
            line.config.line_break = luil::text::line_break_policy::hangul_words;
            const luil::label_wrap measured { luil::label_element::measure_wrap(line.config, luil::measure_context { .scale = scale, .measurer = measurer }, width) };
            line.config.wrap = measured.lines.size() > 1;
            const float one_line { entry_line_height(note_text_size, note_line_height) };
            line.height = line.config.wrap ? std::max(one_line, measured.height) : one_line;
            return line;
        }

        constexpr std::array<page_entry, 15> entries {
            page_entry { page_basics, u8"기본", u8"단추, 글 칸, 그림, dialog", luil::codicons::icon_home },
            page_entry { page_lists, u8"목록", u8"순서 바꾸기, tree, 가상 목록", luil::codicons::icon_list_unordered },
            page_entry { page_tabs, u8"탭", u8"쓸어 넘기기, 넘침 메뉴", luil::codicons::icon_files },
            page_entry { page_groups, u8"그룹", u8"접는 섹션, 선택 묶음", luil::codicons::icon_group_by_ref_type },
            page_entry { page_toasts, u8"토스트", u8"알림과 실행 취소", luil::codicons::icon_bell },
            page_entry { page_popups, u8"메뉴·팝업", u8"드롭다운, 분할 버튼, 길게 눌러 메뉴", luil::codicons::icon_menu },
            page_entry { page_zoom, u8"확대 보기", u8"두 손가락 확대와 이동", luil::codicons::icon_zoom_in },
            page_entry { page_ink, u8"잉크", u8"스타일러스 필압 그리기와 지우개", luil::codicons::icon_pencil },
            page_entry { page_inspector, u8"검사기", u8"탭한 요소의 윤곽과 정보", luil::codicons::icon_inspect },
            page_entry { page_theme, u8"테마", u8"밝기, 키 컬러, 시스템 색", luil::codicons::icon_symbol_color },
            page_entry { page_sheets, u8"시트", u8"아래 시트, 옆 시트, 서랍", luil::codicons::icon_layout_panel },
            page_entry { page_device, u8"기기", u8"방향, 막대, 볼륨 키, 햅틱, 창 폭", luil::codicons::icon_device_mobile },
            page_entry { page_smb, u8"SMB", u8"NAS 폴더 열거와 파일 읽기", luil::codicons::icon_server },
            page_entry { page_files, u8"파일 감시", u8"파일 변경 묶음과 끝 묶음", luil::codicons::icon_eye },
            page_entry { page_chart, u8"차트", u8"선, 막대, 영역과 값 상자", luil::codicons::icon_graph },
        };
    }

    std::span<const page_entry> page_entries() noexcept
    {
        return entries;
    }

    std::u8string page_title(const std::u8string_view page)
    {
        for (const page_entry& entry : entries)
            if (entry.key == page)
                return std::u8string { entry.title };
        return std::u8string { u8"luil mobile demo" };
    }

    page_content make_page_entry(const page_entry& entry, const bool selected, const bool chevron, const float width, const float scale, const luil::text_measurer* const measurer)
    {
        luil::panel_config panel_config {};
        if (selected)
            panel_config.background = [](const luil::ui_color_palette& palette) { return palette.row_selection_background; };
        else
            panel_config.background = [](const luil::ui_color_palette& palette) { return palette.window_background; };
        panel_config.corner_radius = selected ? 8.0f : 0.0f;
        const std::u8string key { entry.key };
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_page_entry, key }, std::move(panel_config)) };
        panel->set_cursor(luil::ui_cursor::hand);
        panel->set_action(luil::ui_trigger::left_click, luil::make_message_action(navigate_intent { key }));
        // 이름이 없으면 누를 수 있어도 구조로 접혀 보조 기술이 칸을 이름으로 찾지 못한다. 기본 접근 규칙이 이름 있는 눌림을 단추로
        // 읽고 Invoke를 왼쪽 클릭 액션으로 잇는다. 이름은 화면의 제목이다.
        panel->set_access_name(std::u8string { entry.title });

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = entry_spacing;
        row_config.padding = luil::edge_insets::symmetric(entry_padding_x, entry_padding_y);
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"entry-" + key }, row_config) };
        row->add(std::make_unique<luil::glyph_element>(luil::ui_element_id { kind_text, u8"entry-icon-" + key }, luil::glyph_config { .glyph = entry.icon, .font_size = 22.0f }), entry_icon_width);

        const float chevron_width { chevron ? entry_spacing + entry_chevron_width : 0.0f };
        summary_line summary { make_summary(entry, width - 2.0f * entry_padding_x - entry_icon_width - entry_spacing - chevron_width, scale, measurer) };

        luil::stack_config text_config {};
        auto text { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"entry-text-" + key }, text_config) };
        text->add(make_label(luil::ui_element_id { kind_text, u8"entry-title-" + key }, std::u8string { entry.title }, body_text_size, luil::label_color_role::primary),
            entry_line_height(body_text_size, body_line_height));
        text->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"entry-summary-" + key }, std::move(summary.config)), summary.height);
        row->add_flexible(std::move(text));
        if (chevron)
            row->add(std::make_unique<luil::glyph_element>(
                         luil::ui_element_id { kind_text, u8"entry-chevron-" + key }, luil::glyph_config { .glyph = luil::codicons::icon_chevron_right, .font_size = 18.0f }),
                entry_chevron_width);
        panel->set_content(std::move(row));
        return { std::move(panel), entry_height + entry_line_height(body_text_size, body_line_height) - body_line_height + summary.height - note_line_height };
    }

    page_content make_page_list(const float width, const std::u8string_view selected, const bool chevron, const float scale, const luil::text_measurer* const measurer)
    {
        page_column column { u8"navigation", 0.0f };
        column.gap(8.0f);
        for (const page_entry& entry : entries)
        {
            page_content cell { make_page_entry(entry, entry.key == selected, chevron, width, scale, measurer) };
            column.add(std::move(cell.element), cell.height, width);
        }
        column.gap(8.0f);
        return column.finish();
    }
}

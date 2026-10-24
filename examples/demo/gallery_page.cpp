#include "demo/gallery_page.h"
#include "demo/files_page.h"

#include "luil/ui/badge_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/grid_element.h"
#include "luil/ui/markdown_element.h"
#include "luil/ui/progress_element.h"
#include "luil/ui/slider_element.h"

#include <algorithm>
#include <array>
#include <utility>

namespace demo {
    namespace {
        struct feature
        {
            std::u8string_view page {};
            std::u8string_view title {};
            std::u8string_view description {};
        };

        constexpr std::array features {
            feature { page_controls, u8"버튼과 컨트롤", u8"토글 · 스위치 · 툴팁 · 슬라이더" },
            feature { page_theme, u8"모양과 색", u8"시각적 테마 카드 · 시스템 강조색" },
            feature { page_tabs, u8"문서와 탭", u8"순서 이동 · 키보드 전환 · 메뉴" },
            feature { page_popups, u8"메뉴와 팝업", u8"검색 · 명령 · 컨텍스트 메뉴" },
            feature { page_basics, u8"입력과 다이얼로그", u8"IME · 여러 줄 편집 · 기본 버튼" },
            feature { page_lists, u8"목록과 트리", u8"가상 목록 · 드래그 · sticky 머리행" },
            feature { page_workbench, u8"변경 작업대", u8"비동기 트리 · 표 정렬 · 편집 폼" },
            feature { page_files, u8"파일 작업", u8"여럿 선택 · 끌어 놓기 · 클립보드" },
            feature { page_groups, u8"그룹과 선택", u8"접이식 섹션 · 배타 선택 · 체크" },
            feature { page_toasts, u8"알림", u8"심각도 · 실행 취소 · 만료" },
            feature { page_images, u8"이미지", u8"가져오기 · 애니메이션 · 미리보기" },
            feature { page_zoom, u8"확대 보기", u8"확대 · 축소 · 화면 맞춤" },
            feature { page_windows, u8"여러 창", u8"보조 창 · 전체 화면 · 창 명령" },
            feature { page_network, u8"네트워크", u8"요청 · 취소 · 스트리밍 · 재시도" },
            feature { page_webview, u8"웹 콘텐츠", u8"내장 웹뷰 · 로컬 문서" },
        };

        [[nodiscard]] luil::input_action follow_link(const std::u8string_view destination)
        {
            if (destination == u8"app:theme")
                return luil::make_app_action(navigate_intent { std::u8string { page_theme } });
            if (destination == u8"app:tabs")
                return luil::make_app_action(navigate_intent { std::u8string { page_tabs } });
            return luil::make_app_action(toast_request_intent { u8"마크다운 링크를 실행했습니다.", luil::toast_severity::success });
        }

        [[nodiscard]] std::unique_ptr<luil::text_button_element> button(std::u8string key, std::u8string text, const luil::text_button_visual visual, luil::ui_action action)
        {
            luil::text_button_config config {};
            config.text = std::move(text);
            config.visual = visual;
            auto element { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(key) }, std::move(config)) };
            element->set_cursor(luil::ui_cursor::hand);
            element->set_action(luil::ui_trigger::left_click, std::move(action));
            return element;
        }

        [[nodiscard]] std::unique_ptr<luil::stack_element> row(std::u8string owner)
        {
            luil::stack_config config {};
            config.direction = luil::stack_direction::row;
            config.spacing = 8.0f;
            return std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, std::move(owner) }, config);
        }

        [[nodiscard]] std::unique_ptr<luil::check_element> toggle(std::u8string key, std::u8string label, const bool checked, const luil::check_style style)
        {
            luil::check_config config {};
            config.owner = u8"gallery/" + key;
            config.style = style;
            config.label = std::move(label);
            config.checked = checked;
            config.toggle = luil::make_message_action(gallery_toggle_intent { std::move(key) });
            return std::make_unique<luil::check_element>(std::move(config));
        }
    } // namespace

    bool gallery_page::handle(const luil::app_message& message)
    {
        if (const auto* const toggle { message.get<gallery_toggle_intent>() }; toggle != nullptr)
        {
            if (toggle->key == u8"autosave")
                autosave_ = !autosave_;
            if (toggle->key == u8"bold")
                bold_ = !bold_;
            if (toggle->key == u8"italic")
                italic_ = !italic_;
            if (toggle->key == u8"pinned")
                pinned_ = !pinned_;
            return true;
        }
        if (const auto* const value { message.get<gallery_value_intent>() }; value != nullptr)
        {
            value_ = std::clamp(value->relative ? value_ + value->value : value->value, 0.0f, 100.0f);
            return true;
        }
        if (const auto* const scroll { message.get<gallery_scroll_intent>() }; scroll != nullptr)
        {
            float& offset { scroll->controls ? controls_scroll_ : overview_scroll_ };
            offset = std::max(0.0f, offset + scroll->delta);
            return true;
        }
        if (const auto* const choice { message.get<gallery_choice_intent>() }; choice != nullptr)
        {
            choice_ = choice->value;
            return true;
        }
        if (message.get<gallery_reset_intent>() != nullptr)
        {
            autosave_ = true;
            bold_ = false;
            italic_ = false;
            pinned_ = true;
            value_ = 65.0f;
            choice_ = u8"comfortable";
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> gallery_page::build_overview(const float width)
    {
        page_layout page { u8"overview" };
        page.heading(u8"luil UI 갤러리", u8"기능을 직접 조작하고, 필요한 페이지부터 프로젝트에 가져가세요.");
        auto actions { row(u8"overview/actions") };
        actions->add_fit(button(u8"overview/start", u8"컨트롤 둘러보기", luil::text_button_visual::accent, luil::make_message_action(navigate_intent { std::u8string { page_controls } })));
        actions->add_fit(button(u8"overview/theme", u8"모양 설정", luil::text_button_visual::normal, luil::make_message_action(navigate_intent { std::u8string { page_theme } })));
        page.add(std::move(actions), luil::text_button_height);
        page.gap(24.0f);

        const std::size_t columns { width >= 1000.0f ? 3u : (width >= 600.0f ? 2u : 1u) };
        const std::size_t rows { (features.size() + columns - 1) / columns };
        luil::grid_config config {};
        config.column_spacing = 12.0f;
        config.row_spacing = 12.0f;
        config.columns.assign(columns, luil::grid_track::flexible());
        config.rows.assign(rows, luil::grid_track::fixed(94.0f));
        auto grid { std::make_unique<luil::grid_element>(luil::ui_element_id { kind_layout, u8"feature-index" }, config) };
        for (std::size_t index = 0; index < features.size(); ++index)
        {
            const feature& item { features[index] };
            const std::u8string key { item.page };
            luil::panel_config card {};
            card.corner_radius = 8.0f;
            card.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
            auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"feature/" + key }, std::move(card)) };
            panel->set_access_name(std::u8string { item.title });
            panel->set_search_label(std::u8string { item.title });
            panel->set_cursor(luil::ui_cursor::hand);
            panel->set_tooltip(std::u8string { item.description });
            panel->set_action(luil::ui_trigger::left_click, luil::make_message_action(navigate_intent { key }));
            luil::stack_config body_config {};
            body_config.padding = luil::edge_insets::all(16.0f);
            auto body { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"feature-body/" + key }, body_config) };
            body->add(make_label({ kind_text, key + u8"/title" }, std::u8string { item.title }, 15.0f, luil::label_color_role::primary), 26.0f);
            body->add(make_label({ kind_text, key + u8"/description" }, std::u8string { item.description }, 11.0f, luil::label_color_role::dim), 24.0f);
            panel->set_content(std::move(body));
            grid->add(std::move(panel), { .row = index / columns, .column = index % columns });
        }
        page.add(std::move(grid), static_cast<float>(rows) * 106.0f - 12.0f);
        page.gap(16.0f);
        page.add(make_label({ kind_text, u8"overview/keyboard" }, u8"Tab으로 이동 · 방향키로 그룹 선택 · Enter/Space로 실행 · Esc로 닫기", 12.0f, luil::label_color_role::dim), 24.0f);
        return page.finish(overview_scroll_, [](const float delta) { return luil::make_app_action(gallery_scroll_intent { false, delta }); });
    }

    std::unique_ptr<luil::ui_element> gallery_page::build_controls()
    {
        page_layout page { u8"controls" };
        page.heading(u8"버튼과 컨트롤", u8"상태가 실제로 바뀌는 공용 컨트롤과 마크다운 hover를 체험하세요.");

        auto buttons { row(u8"gallery/buttons") };
        buttons->add_fit(button(u8"gallery/notify", u8"알림 보내기", luil::text_button_visual::accent,
            luil::make_message_action(toast_request_intent { u8"공용 버튼에서 보낸 알림입니다.", luil::toast_severity::success })));
        buttons->add_fit(button(u8"gallery/reset", u8"상태 초기화", luil::text_button_visual::danger, luil::make_message_action(gallery_reset_intent {})));
        buttons->add_fit(button(u8"gallery/theme", u8"모양 설정", luil::text_button_visual::link, luil::make_message_action(navigate_intent { std::u8string { page_theme } })));
        auto disabled { button(u8"gallery/disabled", u8"비활성 버튼", luil::text_button_visual::normal, {}) };
        disabled->set_enabled(false);
        buttons->add_fit(std::move(disabled));
        page.add(std::move(buttons), luil::text_button_height);
        page.gap(24.0f);
        page.add(make_label({ kind_text, u8"gallery/toggles-title" }, u8"독립 토글과 즉시 적용 스위치", 15.0f, luil::label_color_role::primary), 28.0f);

        auto toggles { row(u8"gallery/toggles") };
        toggles->add_fit(toggle(u8"bold", u8"굵게", bold_, luil::check_style::toggle_button));
        toggles->add_fit(toggle(u8"italic", u8"기울임", italic_, luil::check_style::toggle_button));
        toggles->add_fit(toggle(u8"pinned", u8"상단 고정", pinned_, luil::check_style::toggle_button));
        page.add(std::move(toggles), luil::text_button_height);
        page.gap();
        page.add(toggle(u8"autosave", u8"자동 저장", autosave_, luil::check_style::toggle_switch), luil::check_row_height);
        page.add(make_label({ kind_text, u8"gallery/autosave-status" }, autosave_ ? u8"변경 사항을 자동으로 저장합니다." : u8"수동 저장 모드입니다.", 12.0f, luil::label_color_role::dim), 24.0f);

        luil::markdown_config preview {};
        preview.text = u8"선택한 서식: ";
        if (bold_)
            preview.text += u8"**굵은 글씨** ";
        if (italic_)
            preview.text += u8"*기울임 글씨* ";
        if (bold_ == false && italic_ == false)
            preview.text += u8"기본 글씨";
        preview.text += pinned_ ? u8" · 상단 고정 켜짐" : u8" · 상단 고정 꺼짐";
        page.add(std::make_unique<luil::markdown_element>(luil::ui_element_id { kind_text, u8"gallery/format-preview" }, std::move(preview)), 32.0f);
        page.gap(20.0f);

        luil::choice_group_config choices {};
        choices.owner = u8"gallery/density";
        choices.name = u8"표시 밀도";
        choices.style = luil::choice_style::toggle;
        choices.items = { { u8"compact", u8"간결하게" }, { u8"comfortable", u8"여유롭게" }, { u8"wide", u8"넓게" } };
        choices.selected = choice_;
        choices.select = [](const std::u8string& value) { return luil::make_app_action(gallery_choice_intent { value }); };
        page.add(make_label({ kind_text, u8"gallery/choices-title" }, u8"배타 선택", 15.0f, luil::label_color_role::primary), 28.0f);
        page.add(std::make_unique<luil::choice_group_element>(std::move(choices)), luil::choice_toggle_height);
        page.gap(24.0f);

        page.add(make_label({ kind_text, u8"gallery/value" }, u8"진행률  " + to_u8(static_cast<int>(value_)) + u8"%", 15.0f, luil::label_color_role::primary), 28.0f);
        luil::slider_config slider {};
        slider.maximum = 100.0f;
        slider.value = value_;
        slider.change = [](const float delta) { return luil::make_app_action(gallery_value_intent { delta, true }); };
        slider.change_to = [](const float value) { return luil::make_app_action(gallery_value_intent { value, false }); };
        page.add(std::make_unique<luil::slider_element>(luil::ui_element_id { kind_layout, u8"gallery/slider" }, std::move(slider)), luil::slider_row_height);
        page.gap();
        page.add(std::make_unique<luil::progress_element>(luil::ui_element_id { kind_layout, u8"gallery/progress" }, luil::progress_config { .value = value_ / 100.0f }), 8.0f);
        page.gap(24.0f);

        luil::ui_action hover_action { luil::make_message_action(toast_request_intent { u8"툴팁 안의 링크도 눌러 보세요." }) };
        auto hover { button(u8"gallery/hover", u8"여기에 마우스를 올려 보세요", luil::text_button_visual::normal, std::move(hover_action)) };
        hover->set_tooltip(u8"마크다운 도움말: 팝업 안으로 마우스를 옮겨 링크를 누를 수 있습니다.");
        luil::markdown_config markdown {};
        markdown.text = u8"### 마크다운 hover\n**굵게**, *기울임*, 인라인 코드와 링크를 지원합니다.\n\n- 팝업 위에서도 열린 상태 유지\n- 긴 내용은 휠로 스크롤\n\n[테마 설정](app:theme) · [탭 "
                        u8"예제](app:tabs) · [링크 실행](app:sample)";
        markdown.activate_link = follow_link;
        hover->set_tooltip_content(luil::make_markdown_tooltip(std::move(markdown), 340.0f));
        page.add(std::move(hover), luil::text_button_height);
        page.gap();
        page.add(make_label({ kind_text, u8"gallery/hover-help" }, u8"도움말 안으로 마우스를 옮겨도 유지됩니다. 링크를 누르거나 Esc로 닫아 보세요.", 12.0f, luil::label_color_role::dim), 24.0f);
        return page.finish(controls_scroll_, [](const float delta) { return luil::make_app_action(gallery_scroll_intent { true, delta }); });
    }
} // namespace demo

#include "demo/gallery_page.h"
#include "demo/files_page.h"

#include "luil/ui/badge_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/grid_element.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/markdown_element.h"
#include "luil/ui/progress_element.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/spinner_element.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace demo {
    namespace {
        struct feature
        {
            std::u8string_view page {};
            std::u8string_view title {};
            std::u8string_view description {};
        };

        constexpr std::array features {
            feature { page_controls, u8"버튼과 컨트롤", u8"토글 · 스위치 · 툴팁 · 슬라이더 · 스피너" },
            feature { page_theme, u8"모양과 색", u8"시각적 테마 카드 · 시스템 강조색" },
            feature { page_tabs, u8"문서와 탭", u8"순서 이동 · 키보드 전환 · 메뉴" },
            feature { page_popups, u8"메뉴와 팝업", u8"검색 · 명령 · 컨텍스트 메뉴" },
            feature { page_basics, u8"입력과 다이얼로그", u8"IME · 여러 줄 편집 · 기본 버튼" },
            feature { page_lists, u8"목록과 트리", u8"가상 목록 · 드래그 · sticky 머리행" },
            feature { page_workbench, u8"변경 작업대", u8"비동기 트리 · 표 정렬 · 차이 보기 · 편집형 콤보" },
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
            // 범위 다듬기는 값을 가진 앱의 몫이다 (slider_config).
            const auto apply = [value](float& target, const float minimum, const float maximum) { target = std::clamp(value->relative ? target + value->value : value->value, minimum, maximum); };
            if (value->key == u8"volume")
                apply(volume_, 0.0f, 150.0f);
            else if (value->key == u8"balance")
                apply(balance_, -100.0f, 100.0f);
            else if (value->key == u8"position")
                apply(position_, 0.0f, 1440.0f);
            else
                apply(value_, 0.0f, 100.0f);
            return true;
        }
        if (const auto* const shortcut { message.get<gallery_shortcut_intent>() }; shortcut != nullptr)
        {
            // 예약 키는 정하지 않고 그렇다고 보여 준다. 저장할 때는 글로 왕복한다 (shortcut_label·parse_shortcut).
            if (luil::system_reserved_chord(shortcut->chord))
            {
                shortcut_note_ = u8"시스템 예약 키라 정할 수 없습니다 — " + luil::shortcut_label(shortcut->chord);
                return true;
            }
            shortcut_ = luil::parse_shortcut(luil::shortcut_label(shortcut->chord)).value_or(luil::key_chord {});
            shortcut_note_.clear();
            return true;
        }
        if (const auto* const preview { message.get<gallery_preview_intent>() }; preview != nullptr)
        {
            preview_open_ = preview->open;
            return true;
        }
        if (const auto* const toolbar { message.get<gallery_toolbar_intent>() }; toolbar != nullptr)
        {
            toolbar_shown_ = toolbar->shown;
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
            volume_ = 72.0f;
            balance_ = -18.0f;
            position_ = 400.0f;
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
        page.gap();
        // 끝을 모르는 진행은 값 없이 도는 스피너다. 움직임 줄이기 설정에서는 멈춘 모양으로 그린다.
        auto busy { row(u8"gallery/busy") };
        busy->add(std::make_unique<luil::spinner_element>(luil::ui_element_id { kind_layout, u8"gallery/spinner" }, luil::spinner_config { .name = u8"동기화 중" }),
            luil::stack_item { .length = 20.0f, .cross_length = 20.0f });
        busy->add_fit(make_label({ kind_text, u8"gallery/busy-text" }, u8"동기화 중… 끝을 모르는 작업", 12.0f, luil::label_color_role::dim));
        page.add(std::move(busy), 24.0f);
        page.gap(24.0f);

        // 자석 막대: 앱이 정한 지점 근처에서 손잡이가 붙고, Ctrl을 누른 채 끌면 자석 없이 놓는다.
        page.add(make_label({ kind_text, u8"gallery/magnet-title" }, u8"자석 막대", 15.0f, luil::label_color_role::primary), 28.0f);
        page.add(make_label({ kind_text, u8"gallery/magnet-help" }, u8"표식 근처에서 손잡이가 붙습니다. Ctrl을 누른 채 끌면 자석 없이 놓습니다.", 12.0f, luil::label_color_role::dim), 24.0f);
        const auto magnet_slider = [&page](std::u8string key, std::u8string name, std::u8string caption, const float minimum, const float maximum, const float value, std::vector<float> magnets) {
            page.add(make_label({ kind_text, u8"gallery/" + key + u8"-value" }, std::move(caption), 12.0f, luil::label_color_role::primary), 24.0f);
            luil::slider_config config {};
            config.minimum = minimum;
            config.maximum = maximum;
            config.value = value;
            config.magnets = std::move(magnets);
            config.change = [key](const float delta) { return luil::make_app_action(gallery_value_intent { delta, true, key }); };
            config.change_to = [key](const float target) { return luil::make_app_action(gallery_value_intent { target, false, key }); };
            auto element { std::make_unique<luil::slider_element>(luil::ui_element_id { kind_layout, u8"gallery/" + key }, std::move(config)) };
            element->set_tooltip(std::move(name));
            page.add(std::move(element), luil::slider_row_height);
            page.gap();
        };
        const int balance { static_cast<int>(std::lround(balance_)) };
        const std::u8string balance_text { balance == 0 ? std::u8string { u8"가운데" } : (balance < 0 ? u8"왼쪽 " + to_u8(-balance) : u8"오른쪽 " + to_u8(balance)) };
        const int position { static_cast<int>(std::lround(position_)) };
        const int seconds { position % 60 };
        const std::u8string position_text { to_u8(position / 60) + u8":" + (seconds < 10 ? u8"0" : u8"") + to_u8(seconds) + u8" / 24:00" };
        magnet_slider(u8"volume", u8"볼륨", u8"볼륨  " + to_u8(static_cast<int>(std::lround(volume_))) + u8"% — 100%에 붙습니다", 0.0f, 150.0f, volume_, { 100.0f });
        magnet_slider(u8"balance", u8"좌우 균형", u8"좌우 균형  " + balance_text + u8" — 가운데에 붙습니다", -100.0f, 100.0f, balance_, { 0.0f });
        magnet_slider(u8"position", u8"재생 위치", u8"재생 위치  " + position_text + u8" — 장이 시작하는 자리에 붙습니다", 0.0f, 1440.0f, position_, { 192.0f, 580.0f, 905.0f, 1290.0f });
        page.gap(24.0f);

        // 단축키 잡기 칸: 초점을 가진 동안 Tab·Esc·Alt 조합까지 모든 키가 이 칸의 것이다.
        page.add(make_label({ kind_text, u8"gallery/shortcut-title" }, u8"단축키 잡기", 15.0f, luil::label_color_role::primary), 28.0f);
        page.add(make_label({ kind_text, u8"gallery/shortcut-help" }, u8"칸을 누르고 키를 누르세요. Tab·Esc도 잡힙니다. 빠져나가려면 다른 곳을 누르세요.", 12.0f, luil::label_color_role::dim), 24.0f);
        auto shortcut_row { row(u8"gallery/shortcut-row") };
        const std::u8string current { luil::shortcut_label(shortcut_) };
        // 눌러서 초점을 받는 자리라 누름 자체는 아무 일도 하지 않는다.
        const luil::ui_action focus_only { [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; } };
        auto capture { button(u8"gallery/shortcut", current.empty() ? u8"(없음)" : current, luil::text_button_visual::normal, focus_only) };
        capture->set_tooltip(luil::label_with_shortcut(u8"명령 팔레트 열기", shortcut_));
        capture->set_key_capture(luil::key_capture_target {
            .on_key = [](const luil::key_pressed_event& event) -> std::vector<luil::input_action> { return { luil::make_app_action(gallery_shortcut_intent { luil::chord_of(event) }) }; },
        });
        shortcut_row->add(std::move(capture), luil::stack_item { .length = 160.0f, .cross_length = luil::text_button_height });
        shortcut_row->add(button(u8"gallery/shortcut-clear", u8"지우기", luil::text_button_visual::normal, luil::make_message_action(gallery_shortcut_intent {})),
            luil::stack_item { .length = 72.0f, .cross_length = luil::text_button_height });
        page.add(std::move(shortcut_row), luil::text_button_height);
        if (shortcut_note_.empty() == false)
            page.add(make_label({ kind_text, u8"gallery/shortcut-note" }, shortcut_note_, 12.0f, luil::label_color_role::dim), 24.0f);
        page.gap(24.0f);

        // 여러 줄 라벨: 폭에서 줄을 바꾸고 두 줄을 넘으면 마지막 줄 끝을 줄인다. 창 폭을 줄여 보세요.
        luil::label_config description {};
        description.text = u8"설명 글은 칸의 폭에서 줄을 바꿉니다. 공백에서 먼저 바꾸고, 공백이 없는 긴 낱말은 글자 경계에서 바꿉니다. "
                           u8"두 줄을 넘으면 마지막 줄의 끝을 줄여 아래 내용이 밀리지 않습니다.";
        description.font_size = 12.0f;
        description.wrap = true;
        description.max_lines = 2;
        page.add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"gallery/wrapped-description" }, description), 2.0f * luil::label_element::height_for(description));
        page.gap(24.0f);

        // 변을 고른 테두리: 위·아래만 그어 목록 띠처럼 앞뒤 내용과 가른다.
        luil::panel_config strip {};
        strip.background = [](const luil::ui_color_palette& palette) { return palette.window_background; };
        strip.border = [](const luil::ui_color_palette& palette) { return palette.divider; };
        strip.border_sides = { .top = true, .right = false, .bottom = true, .left = false };
        auto strip_panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"gallery/border-strip" }, std::move(strip)) };
        strip_panel->set_content(make_label({ kind_text, u8"gallery/border-strip-text" }, u8"위·아래 변만 긋는 판입니다.", 12.0f, luil::label_color_role::primary));
        page.add(std::move(strip_panel), 32.0f);
        page.gap(24.0f);

        // 머무름: 카드 위에 0.35초 머무르면 미리보기가 열리고, 떠나거나 누르면 닫힌다.
        luil::panel_config card_config {};
        card_config.corner_radius = 6.0f;
        card_config.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto dwell_card { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"gallery/dwell-card" }, std::move(card_config)) };
        dwell_card->set_content(make_label({ kind_text, u8"gallery/dwell-card-text" }, u8"여기에 포인터를 잠시 머무르세요", 12.0f, luil::label_color_role::primary));
        dwell_card->set_hover_dwell(luil::hover_dwell_target {
            .delay = std::chrono::milliseconds { 350 },
            .on_dwell = luil::make_message_action(gallery_preview_intent { true }),
            .on_end = luil::make_message_action(gallery_preview_intent { false }),
        });
        page.add(std::move(dwell_card), 36.0f);
        page.add(make_label({ kind_text, u8"gallery/dwell-preview" }, preview_open_ ? u8"미리보기가 열렸습니다 — 떠나거나 누르면 닫힙니다." : u8"(미리보기 닫힘)", 12.0f,
                     preview_open_ ? luil::label_color_role::primary : luil::label_color_role::dim),
            24.0f);
        page.gap();

        // hover 영역: hit를 받지 않는 띠에 포인터가 들면 도구 막대가 드러난다. 띠 아래의 글은 그대로 고를 수 있다.
        auto reveal_layer { std::make_unique<luil::layer_element>(luil::ui_element_id { kind_layout, u8"gallery/reveal-layer" }) };
        auto reveal_row { row(u8"gallery/reveal-row") };
        if (toolbar_shown_)
        {
            reveal_row->add_fit(button(u8"gallery/reveal-cut", u8"잘라내기", luil::text_button_visual::normal, luil::make_message_action(toast_request_intent { u8"잘라내기" })));
            reveal_row->add_fit(button(u8"gallery/reveal-copy", u8"복사", luil::text_button_visual::normal, luil::make_message_action(toast_request_intent { u8"복사" })));
        }
        else
            reveal_row->add_fit(make_label({ kind_text, u8"gallery/reveal-hint" }, u8"이 띠 위로 포인터를 올리면 도구 막대가 드러납니다.", 12.0f, luil::label_color_role::dim));
        reveal_layer->add(std::move(reveal_row));
        auto reveal_region { std::make_unique<luil::layer_element>(luil::ui_element_id { kind_layout, u8"gallery/reveal-region" }) };
        reveal_region->set_hover_region(luil::hover_region_target {
            .on_enter = [] { return luil::make_app_action(gallery_toolbar_intent { true }); },
            .on_leave = [] { return luil::make_app_action(gallery_toolbar_intent { false }); },
        });
        reveal_layer->add(std::move(reveal_region));
        page.add(std::move(reveal_layer), luil::text_button_height);
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

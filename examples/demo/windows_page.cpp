#include "demo/windows_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"

#include <utility>

namespace demo {
    namespace {
        // 도구 창의 초기 client 크기와 자리다 (논리 픽셀, 자리는 주 창 client 기준).
        // 만들 때 한 번만 쓰고 이후의 이동·크기 조절은 사용자의 것이다.
        constexpr float tool_initial_width { 340.0f };
        constexpr float tool_initial_height { 240.0f };
        constexpr float tool_initial_x { 90.0f };
        constexpr float tool_initial_y { 90.0f };
        // 도구 창 안 메뉴의 너비다 (논리 픽셀).
        constexpr float tool_menu_width { 180.0f };

        // 창 caption과 tree의 caption element가 같은 설정을 쓴다.
        // 비클라이언트 hit test·그리기·창 스타일이 같은 metrics와 버튼 집합으로 맞물린다.
        [[nodiscard]] luil::caption_config make_tool_caption()
        {
            luil::caption_config caption {};
            caption.title = u8"도구 창";
            caption.application_icon = luil::codicons::icon_tools;
            // 도구 창은 닫기만 둔다.
            // 최소화·최대화 버튼이 사라질 뿐 아니라 그 창 스타일도 함께 빠져,
            // 캡션 더블클릭·Win+↑·시스템 메뉴로도 최대화되지 않는다.
            caption.buttons = { .minimize = false, .maximize = false, .close = true };
            caption.close_tooltip = u8"Close";
            return caption;
        }

        // 도구 창 메뉴의 항목이다 (검색 칸이 좁히기 전의 전부).
        [[nodiscard]] std::vector<luil::menu_item_config> tool_menu_items()
        {
            std::vector<luil::menu_item_config> items {};
            items.push_back({ u8"toast", u8"토스트 보내기", luil::codicons::icon_bell });
            items.push_back({ u8"close", u8"도구 창 닫기", luil::codicons::icon_close });
            return items;
        }
    } // namespace

    void windows_page::close_tool_menu() noexcept
    {
        tool_menu_open_ = false;
        menu_search_ = {};
    }

    bool windows_page::handle(const luil::app_message& message)
    {
        if (const auto* const toggle { message.get<tool_window_toggle_intent>() }; toggle != nullptr)
        {
            // 다시 열면 요청 크기에서 시작한다.
            // 실제 크기는 창이 만들어지자마자 metrics 메시지로 돌아온다.
            if (toggle->open && tool_open_ == false)
                tool_metrics_ = {};
            tool_open_ = toggle->open;
            // 창이 닫히면 그 안의 메뉴도 없다.
            if (tool_open_ == false)
                close_tool_menu();
            return true;
        }
        if (const auto* const menu { message.get<tool_menu_toggle_intent>() }; menu != nullptr)
        {
            if (menu->open)
                tool_menu_open_ = true;
            else
                close_tool_menu();
            return true;
        }
        if (const auto* const select { message.get<tool_menu_select_intent>() }; select != nullptr)
        {
            close_tool_menu();
            // 창을 닫는 것도 앱 상태다 — 토스트는 셸이 잇는다.
            if (select->key == u8"close")
                tool_open_ = false;
            return true;
        }
        if (const auto* const metrics { message.get<tool_window_metrics_intent>() }; metrics != nullptr)
        {
            tool_metrics_ = *metrics;
            return true;
        }
        // 창 안 메모 칸과 메뉴 안 검색 칸이 같은 메시지 타입을 나눠 쓴다.
        // 어느 칸인지는 target이 정한다 — popup 안이라고 다른 경로가 아니다.
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            if (edit->request.target == target_tool_menu_search)
            {
                luil::apply_text_edit(menu_search_, edit->request);
                return true;
            }
            if (edit->request.target != target_tool_note)
                return false;
            luil::apply_text_edit(note_, edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_tool_note && composition->event.target != target_tool_menu_search)
                return false;
            if (composition->event.composing == false)
                composition_.reset();
            else
                composition_ = composition->event;
            return true;
        }
        if (const auto* const file { message.get<tool_note_file_intent>() }; file != nullptr)
        {
            // 기본 페이지의 노트 칸과 같은 규칙이다 — 모두 고르고 넣어 통째로 바꾼다.
            luil::text_edit_request request {};
            request.target = target_tool_note;
            request.command = luil::text::text_edit_command::select_all;
            luil::apply_text_edit(note_, request);
            request.command = luil::text::text_edit_command::insert;
            request.text = file->path;
            luil::apply_text_edit(note_, request);
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> windows_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows" }, config) };
        column->add(
            make_label(luil::ui_element_id { kind_text, u8"windows-hint" }, u8"보조 창은 자기 캡션·테두리·DPI를 갖는 진짜 창이다. 열림·닫힘은 앱 상태다.", 11.0f, luil::label_color_role::dim),
            18.0f);
        column->add_gap(4.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"windows-resize-hint" }, u8"창을 끌어 옮기고 가장자리로 크기를 바꿔 본다 — 내용은 metrics 메시지로 다시 배치된다.", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(14.0f);

        luil::text_button_config toggle_config {};
        toggle_config.text = tool_open_ ? u8"도구 창 닫기" : u8"도구 창 열기";
        auto toggle { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle }, std::move(toggle_config)) };
        toggle->set_cursor(luil::ui_cursor::hand);
        const bool open { tool_open_ };
        toggle->set_action(luil::ui_trigger::left_click,
            [open](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(tool_window_toggle_intent { open == false }) }; });
        column->add(std::move(toggle), { .length = 26.0f, .cross_length = 140.0f });
        return column;
    }

    std::vector<luil::win32::ui_window> windows_page::make_windows()
    {
        tool_menu_button_ = nullptr;
        std::vector<luil::win32::ui_window> windows {};
        if (tool_open_ == false)
            return windows;

        luil::win32::ui_window tool {};
        tool.id = u8"tool";
        tool.caption = make_tool_caption();
        tool.x = tool_initial_x;
        tool.y = tool_initial_y;
        tool.width = tool_initial_width;
        tool.height = tool_initial_height;
        tool.minimum_width = 260.0f;
        tool.minimum_height = 160.0f;
        tool.tree = make_tool_tree();
        tool.metrics = [](const float width, const float height, const float scale) { return luil::app_message { tool_window_metrics_intent { width, height, scale } }; };
        tool.close = [] { return luil::make_app_action(tool_window_toggle_intent { false }); };
        windows.push_back(std::move(tool));
        return windows;
    }

    std::vector<luil::win32::ui_popup> windows_page::make_popups() const
    {
        std::vector<luil::win32::ui_popup> popups {};
        if (tool_menu_open_ == false || tool_menu_button_ == nullptr)
            return popups;

        // 배율은 도구 창의 것이다 (주 창과 다른 모니터에 있을 수 있다).
        const float scale { tool_metrics_.scale > 0.0f ? tool_metrics_.scale : 1.0f };
        const luil::text_input_view search { luil::make_text_input_view(menu_search_, composition_, target_tool_menu_search) };
        luil::menu_config menu {};
        menu.owner = u8"tool";
        menu.items = narrow_menu_items(tool_menu_items(), search);
        menu.select = [](const std::u8string& key) { return luil::make_app_action(tool_menu_select_intent { key }); };

        // 메뉴 버튼 바로 아래에 붙인다.
        // 자리는 **도구 창 client** 기준 논리 픽셀이다 — 앵커가 그 창이기 때문이다.
        const luil::rect_f button { tool_menu_button_->bounds() };
        luil::win32::ui_popup popup {};
        popup.id = u8"tool-menu";
        popup.anchor = u8"tool";
        popup.width = tool_menu_width;
        popup.height = search_menu_height(menu);
        popup.x = button.x / scale;
        popup.y = (button.y + button.height) / scale + 2.0f;
        // 검색 칸의 조합은 **도구 창**의 IME session이 본다 — 앵커가 그 창이다.
        popup.tree = make_search_menu_tree(std::move(menu), kind_tool_menu_search_input, search, u8"검색", popup.width, scale);
        popup.dismiss = [](const luil::win32::popup_dismiss_reason reason) {
            // 도구 창을 끌고 다녀도, 주 창을 잠깐 봐도 검색은 남는다.
            if (search_popup_stays(reason))
                return luil::input_action {};
            return luil::make_app_action(tool_menu_toggle_intent { false });
        };
        popups.push_back(std::move(popup));
        return popups;
    }

    std::shared_ptr<const luil::ui_tree> windows_page::make_tool_tree()
    {
        // metrics가 오기 전(창이 만들어지는 그 frame)에는 요청 크기로 배치한다.
        const float scale { tool_metrics_.scale > 0.0f ? tool_metrics_.scale : 1.0f };
        const float width { tool_metrics_.width > 0.0f ? tool_metrics_.width : tool_initial_width * scale };
        const float height { tool_metrics_.height > 0.0f ? tool_metrics_.height : tool_initial_height * scale };

        auto root { std::make_unique<luil::root_element>(u8"tool") };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });

        // 주 창과 같은 custom caption이다.
        // 버튼 실행은 비클라이언트 경로가 이 창에 직접 하므로 여기서는 그리기와 tooltip만 맡는다.
        // 높이는 height_for가 미리 알려 준다 (미리 arrange해 보는 우회가 필요 없다).
        const luil::caption_config tool_caption { make_tool_caption() };
        const float caption_height { luil::caption_element::height_for(tool_caption) * scale };
        auto caption { std::make_unique<luil::caption_element>(tool_caption) };
        caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
        root->add(std::move(caption));

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(16.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tool" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"tool-hint" }, u8"frame의 windows 목록에 실리면 생기고 빠지면 사라진다.", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(2.0f);
        column->add(
            make_label(luil::ui_element_id { kind_text, u8"tool-caption-hint" }, u8"캡션에 닫기만 둔 창이다 — 더블클릭·Win+↑로도 최대화되지 않는다.", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(12.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"tool-input-label" }, u8"텍스트 박스 — 주 창과 같은 입력 경로다 (IME 포함)", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(4.0f);
        luil::text_input_config note_config {};
        note_config.placeholder = u8"메모";
        auto note_input { std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_tool_note_input }, luil::make_text_input_view(note_, composition_, target_tool_note), std::move(note_config)) };
        // 보조 창의 칸도 파일을 받는다 — 표면마다 IDropTarget이 선 것을 눈으로 보는 자리다.
        luil::drop_target note_drop {};
        note_drop.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
        note_drop.on_drop = [](const luil::drag_payload& payload, const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return { luil::make_app_action(tool_note_file_intent { payload.files.front() }) };
        };
        note_input->set_drop_target(std::move(note_drop));
        column->add(std::move(note_input), 28.0f);
        column->add_gap(14.0f);

        // 페이지를 넘던 토스트 요청이 창도 넘는다.
        // 토스트 오버레이는 주 창 tree에 있으므로 주 창 오른쪽 아래에 뜬다.
        auto toast { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toast }, luil::text_button_config { .text = u8"토스트 보내기" }) };
        toast->set_cursor(luil::ui_cursor::hand);
        toast->set_action(luil::ui_trigger::left_click, luil::make_message_action(toast_request_intent { u8"도구 창에서 온 토스트: " + note_.text, luil::toast_severity::success }));
        column->add(std::move(toast), { .length = 26.0f, .cross_length = 140.0f });
        column->add_gap(10.0f);

        // 이 창에 앵커를 둔 메뉴다.
        // popup은 이 창 기준 자리에 이 창 위로 뜨고, 창을 옮기면 따라온다.
        luil::text_button_config menu_config {};
        menu_config.text = u8"메뉴 열기";
        menu_config.visual = tool_menu_open_ ? luil::text_button_visual::accent : luil::text_button_visual::normal;
        auto menu_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_menu }, std::move(menu_config)) };
        menu_button->set_cursor(luil::ui_cursor::hand);
        const bool menu_open { tool_menu_open_ };
        menu_button->set_action(luil::ui_trigger::left_click,
            [menu_open](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(tool_menu_toggle_intent { menu_open == false }) }; });
        tool_menu_button_ = menu_button.get();
        column->add(std::move(menu_button), { .length = 26.0f, .cross_length = 140.0f });
        column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
        root->add(std::move(column));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace demo

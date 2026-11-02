#include "demo/windows_page.h"

#include "luil/app/platform_services.h"
#include "luil/generated/codicons.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_platform.h"
#include "luil/ui/ui_tree.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

        // 작업 표시줄 구역이다 (논리 픽셀).
        // 독립 창은 만들 때 한 번만 이 크기와 자리를 쓴다.
        constexpr float taskbar_window_initial_width { 320.0f };
        constexpr float taskbar_window_initial_height { 160.0f };
        constexpr float taskbar_window_initial_x { 140.0f };
        constexpr float taskbar_window_initial_y { 140.0f };
        constexpr float taskbar_slider_width { 280.0f };
        // 독립 창 단추에 거는 값이다. 상태만 돌리고 값은 고정한다.
        constexpr float taskbar_window_percent { 60.0f };
        // 독립 창의 표면 id다. 보고가 이 id로 온다.
        constexpr std::u8string_view taskbar_window_id { u8"taskbar-window" };
        // 독립 창 단추의 진행 상태가 도는 순서의 길이다. 0은 없음이다.
        constexpr int taskbar_window_steps { 5 };

        // 오버레이에 얹는 작은 원이다. 실행 중에 지은 그림이라 실행 파일 리소스에 기대지 않는다.
        //  - 가장자리는 알파로 부드럽게 한다. 오버레이는 작업 표시줄 아이콘 크기(배율에 따라 16~32)로 맞춘다.
        [[nodiscard]] luil::ui_image make_taskbar_icon()
        {
            constexpr int size { 32 };
            constexpr float radius { 13.0f };
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const float dx { static_cast<float>(x) + 0.5f - static_cast<float>(size) / 2.0f };
                    const float dy { static_cast<float>(y) + 0.5f - static_cast<float>(size) / 2.0f };
                    const float coverage { std::clamp(radius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f) };
                    std::uint8_t* const pixel { pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4 };
                    pixel[0] = 232;
                    pixel[1] = 72;
                    pixel[2] = 60;
                    pixel[3] = static_cast<std::uint8_t>(std::lround(coverage * 255.0f));
                }
            }
            return luil::make_rgba_image(size, size, pixels);
        }

        // 독립 창 단추의 상태가 도는 순서다: 없음 → 정상 → 오류 → 일시정지 → 불확정 → 없음.
        [[nodiscard]] luil::taskbar_progress_state taskbar_window_state(const int step) noexcept
        {
            switch (step)
            {
            case 1:
                return luil::taskbar_progress_state::normal;
            case 2:
                return luil::taskbar_progress_state::error;
            case 3:
                return luil::taskbar_progress_state::paused;
            case 4:
                return luil::taskbar_progress_state::indeterminate;
            default:
                return luil::taskbar_progress_state::none;
            }
        }

        // 상태를 화면 글로 옮긴다. 값이 있는 상태는 퍼센트가 뒤따른다.
        [[nodiscard]] std::u8string taskbar_state_text(const luil::taskbar_progress_state state, const float percent)
        {
            const std::u8string value { to_u8(static_cast<int>(std::lround(percent))) + u8"%" };
            switch (state)
            {
            case luil::taskbar_progress_state::indeterminate:
                return u8"불확정";
            case luil::taskbar_progress_state::normal:
                return u8"정상 " + value;
            case luil::taskbar_progress_state::error:
                return u8"오류 " + value;
            case luil::taskbar_progress_state::paused:
                return u8"일시정지 " + value;
            case luil::taskbar_progress_state::none:
            default:
                return u8"없음";
            }
        }

        [[nodiscard]] std::u8string taskbar_status_text(const luil::win32::taskbar_status status)
        {
            switch (status)
            {
            case luil::win32::taskbar_status::waiting:
                return u8"대기 중";
            case luil::win32::taskbar_status::applied:
                return u8"적용됨";
            case luil::win32::taskbar_status::unavailable:
                return u8"쓸 수 없음";
            case luil::win32::taskbar_status::failed:
                return u8"실패";
            case luil::win32::taskbar_status::off:
            default:
                return u8"꺼짐";
            }
        }

        // 보고를 한 줄로 옮긴다. 요청이 기본값이던 단추는 보고가 없다.
        [[nodiscard]] std::u8string taskbar_report_text(const std::optional<luil::win32::taskbar_report>& report)
        {
            if (report.has_value() == false)
                return u8"아직 보고 없음";
            std::u8string text { taskbar_status_text(report->status) };
            if (report->error.empty() == false)
                text += u8" (" + report->error + u8")";
            return text;
        }

        // 고른 쪽을 강조하는 작은 단추다.
        [[nodiscard]] std::unique_ptr<luil::text_button_element> make_taskbar_button(std::u8string key, std::u8string text, const bool selected, luil::ui_action action)
        {
            luil::text_button_config config {};
            config.text = std::move(text);
            config.visual = selected ? luil::text_button_visual::accent : luil::text_button_visual::normal;
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, std::move(key) }, std::move(config)) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, std::move(action));
            return button;
        }

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

        // 작업 표시줄 독립 창의 캡션이다. 최소화·최대화·닫기를 모두 둔다 — 독립 창은 작업 표시줄에서 따로 최소화된다.
        [[nodiscard]] luil::caption_config make_taskbar_window_caption()
        {
            luil::caption_config caption {};
            caption.title = u8"작업 표시줄 창";
            caption.application_icon = luil::codicons::icon_window;
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
    }

    void windows_page::close_tool_menu() noexcept
    {
        tool_menu_open_ = false;
        menu_search_ = {};
    }

    std::optional<luil::focus_request> windows_page::make_focus_request() const
    {
        if (focus_revision_ == 0)
            return std::nullopt;
        return luil::focus_request {
            .id = focus_revision_,
            .surface = u8"tool",
            .target = luil::ui_element_id { kind_tool_note_input },
            .activate = true,
        };
    }

    bool windows_page::handle(const luil::app_message& message)
    {
        // 플랫폼 도구는 logic thread에서 부른다. 탐색기와 메모장은 따로 뜨는 창이라 이쪽은 결과만 적는다.
        if (message.get<reveal_executable_intent>() != nullptr)
        {
            wchar_t module[MAX_PATH] {};
            const DWORD length { GetModuleFileNameW(nullptr, module, MAX_PATH) };
            const std::filesystem::path executable { std::wstring { module, length } };
            const luil::platform_result result { luil::reveal_in_file_manager(executable.u8string()) };
            platform_report_ = result.ok() ? u8"탐색기에서 실행 파일을 골라 두었다." : u8"탐색기를 열지 못했다 — " + result.error;
            return true;
        }
        if (message.get<launch_notepad_intent>() != nullptr)
        {
            const luil::platform_result result { luil::launch_program(u8"notepad.exe", {}) };
            platform_report_ = result.ok() ? u8"메모장을 띄웠다." : u8"메모장을 띄우지 못했다 — " + result.error;
            return true;
        }
        if (message.get<tool_focus_intent>() != nullptr)
        {
            if (tool_open_ == false)
                tool_metrics_ = {};
            tool_open_ = true;
            ++focus_revision_;
            return true;
        }
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
        if (const auto* const position { message.get<caption_position_intent>() }; position != nullptr)
        {
            caption_bottom_ = position->bottom;
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
        if (const auto* const state { message.get<taskbar_state_intent>() }; state != nullptr)
        {
            taskbar_state_ = state->state;
            return true;
        }
        if (const auto* const value { message.get<taskbar_value_intent>() }; value != nullptr)
        {
            // 막대를 끌 때는 변화량이, 보조 기술이 값을 정할 때는 목표 값이 온다.
            taskbar_percent_ = std::clamp(value->relative ? taskbar_percent_ + value->value : value->value, 0.0f, 100.0f);
            return true;
        }
        if (const auto* const overlay { message.get<taskbar_overlay_intent>() }; overlay != nullptr)
        {
            // 그림은 처음 켤 때 지어 둔다. 같은 `ui_image` 복사본을 frame마다 실어야 같은 그림으로 읽힌다.
            if (overlay->on && taskbar_icon_.valid() == false)
                taskbar_icon_ = make_taskbar_icon();
            taskbar_overlay_on_ = overlay->on;
            return true;
        }
        if (const auto* const toggle { message.get<taskbar_window_toggle_intent>() }; toggle != nullptr)
        {
            // 다시 열면 요청 크기에서 시작하고 보고도 새로 받는다 (창을 다시 만들면 처음부터 다시 알린다).
            if (toggle->open && taskbar_window_open_ == false)
            {
                taskbar_window_metrics_ = {};
                taskbar_window_step_ = 1;
            }
            if (toggle->open == false)
                taskbar_window_report_.reset();
            taskbar_window_open_ = toggle->open;
            return true;
        }
        if (message.get<taskbar_window_step_intent>() != nullptr)
        {
            taskbar_window_step_ = (taskbar_window_step_ + 1) % taskbar_window_steps;
            return true;
        }
        if (const auto* const metrics { message.get<taskbar_window_metrics_intent>() }; metrics != nullptr)
        {
            taskbar_window_metrics_ = *metrics;
            return true;
        }
        if (const auto* const report { message.get<taskbar_report_intent>() }; report != nullptr)
        {
            // 보고는 단추가 있는 창만 낸다: 주 창은 빈 id, 독립 창은 `ui_window::id`다.
            if (report->report.surface.empty())
                taskbar_report_ = report->report;
            else if (report->report.surface == taskbar_window_id && taskbar_window_open_)
                taskbar_window_report_ = report->report;
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
        column->add_gap(8.0f);
        // 아직 없는 창의 칸에 초점을 청한다. luil이 칸이 설 때까지 요청을 들고 있다가 한 번 이행한다.
        auto focus { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, u8"focus" }, luil::text_button_config { .text = u8"도구 창 메모 칸에 초점" }) };
        focus->set_cursor(luil::ui_cursor::hand);
        focus->set_action(luil::ui_trigger::left_click, luil::make_message_action(tool_focus_intent {}));
        column->add(std::move(focus), { .length = 26.0f, .cross_length = 180.0f });
        column->add_gap(18.0f);

        // 주 창의 전체 화면 전환이다.
        //
        // **이 단추는 앱 메시지를 내지 않는다.** 창을 어떤 모습으로 세울지는 앱 상태가
        // 아니라 UI thread의 것이라, 액션이 `ui_command` 하나를 그대로 돌려주고
        // logic thread는 지나지도 않는다 (ui_events.h의 `ui_command`). 그래서 이
        // 페이지는 지금 전체 화면인지 **모르고** 단추 글도 상태를 따라 바뀌지 않는다 —
        // 아는 것은 배치 보고를 받는 셸이고, 캡션 줄을 접는 것도 그쪽이다
        // (demo_main.cpp의 make_frame).
        column->add(make_label(luil::ui_element_id { kind_text, u8"windows-fullscreen-hint" }, u8"전체 화면은 테두리 없는 창이다 — 모니터를 덮고 캡션 줄은 접힌다.", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(2.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"windows-fullscreen-restore-hint" }, u8"다시 누르면 들어가기 전 자리로 돌아오고, 그 자리는 종료할 때 함께 저장된다.",
                        11.0f, luil::label_color_role::dim),
            18.0f);
        column->add_gap(4.0f);
        // id는 이 페이지의 전환 단추 종류를 이름으로 갈라 쓴다.
        auto fullscreen { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, u8"fullscreen" }, luil::text_button_config { .text = u8"전체 화면 전환" }) };
        fullscreen->set_cursor(luil::ui_cursor::hand);
        fullscreen->set_action(luil::ui_trigger::left_click,
            [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::ui_command::window_toggle_fullscreen } }; });
        column->add(std::move(fullscreen), { .length = 26.0f, .cross_length = 140.0f });

        // 주 창 캡션의 자리다. 그리기는 셸의 배치를 따르고, 끌기·창 버튼·Alt+Space 메뉴는 게시된 tree에서
        // 캡션이 선 자리를 읽어 따라간다 — 라이브러리에 앵커를 따로 알리지 않는다.
        column->add_gap(18.0f);
        const bool bottom { caption_bottom_ };
        column->add(make_label(luil::ui_element_id { kind_text, u8"windows-caption-position" }, bottom ? u8"캡션 위치: 아래" : u8"캡션 위치: 위", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(4.0f);
        luil::text_button_config position_config {};
        position_config.text = bottom ? u8"캡션을 위로" : u8"캡션을 아래로";
        auto position { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, u8"caption-position" }, std::move(position_config)) };
        position->set_cursor(luil::ui_cursor::hand);
        position->set_action(luil::ui_trigger::left_click, luil::make_message_action(caption_position_intent { bottom == false }));
        column->add(std::move(position), { .length = 26.0f, .cross_length = 140.0f });

        // 위·왼쪽 끝을 옮기는 크기 변경이다. 캡션을 아래에 두고 누르면, 표준 표시에서는 크기가 바뀐 직후 한 합성 동안
        // 캡션이 창 맨 위에 보이고, 레이어드 표시에서는 그 frame이 없다.
        column->add_gap(18.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"windows-presentation" },
                        layered_ ? u8"표시 방식: 레이어드 — 배치와 frame을 한 합성에 낸다" : u8"표시 방식: 표준 — --presentation=layered로 켠다", 11.0f, luil::label_color_role::dim),
            18.0f);
        column->add_gap(4.0f);
        luil::stack_config edges_config {};
        edges_config.direction = luil::stack_direction::row;
        edges_config.spacing = 8.0f;
        auto edges { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-edges" }, edges_config) };
        const auto add_edge = [&edges](const std::u8string_view key, const std::u8string_view text, const edge_resize_intent intent) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, std::u8string { key } }, luil::text_button_config { .text = std::u8string { text } }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(intent));
            edges->add(std::move(button), 96.0f);
        };
        add_edge(u8"edge-top-grow", u8"위로 키우기", edge_resize_intent { .top = 80.0f });
        add_edge(u8"edge-top-shrink", u8"위에서 줄이기", edge_resize_intent { .top = -80.0f });
        add_edge(u8"edge-left-grow", u8"왼쪽으로 키우기", edge_resize_intent { .left = 80.0f });
        add_edge(u8"edge-left-shrink", u8"왼쪽에서 줄이기", edge_resize_intent { .left = -80.0f });
        column->add(std::move(edges), 26.0f);

        // 플랫폼 도구다. 경로 따옴표·COM·핸들 정리는 라이브러리가 맡는다.
        column->add_gap(18.0f);
        luil::stack_config tools_config {};
        tools_config.direction = luil::stack_direction::row;
        tools_config.spacing = 8.0f;
        auto tools { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-platform" }, tools_config) };
        auto reveal { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, u8"reveal" }, luil::text_button_config { .text = u8"탐색기에서 실행 파일 보기" }) };
        reveal->set_action(luil::ui_trigger::left_click, luil::make_message_action(reveal_executable_intent {}));
        tools->add(std::move(reveal), 180.0f);
        auto notepad { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, u8"notepad" }, luil::text_button_config { .text = u8"메모장 열기" }) };
        notepad->set_action(luil::ui_trigger::left_click, luil::make_message_action(launch_notepad_intent {}));
        tools->add(std::move(notepad), 100.0f);
        column->add(std::move(tools), 26.0f);
        if (platform_report_.empty() == false)
        {
            column->add_gap(4.0f);
            column->add(make_label(luil::ui_element_id { kind_text, u8"windows-platform-report" }, platform_report_, 11.0f, luil::label_color_role::dim), 18.0f);
        }
        add_taskbar_section(*column);
        return column;
    }

    luil::taskbar_request windows_page::make_taskbar_request() const
    {
        luil::taskbar_request request {};
        request.progress.state = taskbar_state_;
        request.progress.value = taskbar_percent_ / 100.0f;
        if (taskbar_overlay_on_)
        {
            request.overlay.icon = taskbar_icon_;
            request.overlay.description = u8"새 소식 있음";
        }
        return request;
    }

    luil::taskbar_request windows_page::make_taskbar_window_request() const
    {
        luil::taskbar_request request {};
        request.progress.state = taskbar_window_state(taskbar_window_step_);
        request.progress.value = taskbar_window_percent / 100.0f;
        return request;
    }

    void windows_page::add_taskbar_section(luil::stack_element& column)
    {
        column.add_gap(18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-taskbar-hint" }, u8"작업 표시줄 단추에 진행률과 오버레이를 건다 — 창을 최소화해도 단추에 보인다.", 11.0f,
                       luil::label_color_role::dim),
            18.0f);
        column.add_gap(4.0f);
        // 세 줄이 UIA로 읽힌다: 이 프로세스가 셸에 낼 수 있는가, 앱이 바라는 것, 셸에 닿은 정도.
        const bool capable { luil::current_ui_platform().capabilities.taskbar };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-taskbar-capability" }, capable ? u8"작업 표시줄 능력: 있음" : u8"작업 표시줄 능력: 없음", 12.0f, luil::label_color_role::primary),
            18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-taskbar-request" },
                       u8"작업 표시줄 요청: " + taskbar_state_text(taskbar_state_, taskbar_percent_) + u8" · 오버레이 " + (taskbar_overlay_on_ ? u8"켬" : u8"끔"), 12.0f, luil::label_color_role::primary),
            18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-taskbar-report" }, u8"작업 표시줄 반영: " + taskbar_report_text(taskbar_report_), 12.0f, luil::label_color_role::primary), 18.0f);
        column.add_gap(6.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto states { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-taskbar-states" }, row_config) };
        const auto add_state = [&states, this](const std::u8string_view key, const std::u8string_view text, const luil::taskbar_progress_state state) {
            states->add(make_taskbar_button(u8"taskbar-" + std::u8string { key }, std::u8string { text }, taskbar_state_ == state, luil::make_message_action(taskbar_state_intent { state })), 92.0f);
        };
        add_state(u8"none", u8"진행 없음", luil::taskbar_progress_state::none);
        add_state(u8"indeterminate", u8"진행 불확정", luil::taskbar_progress_state::indeterminate);
        add_state(u8"normal", u8"진행 정상", luil::taskbar_progress_state::normal);
        add_state(u8"error", u8"진행 오류", luil::taskbar_progress_state::error);
        add_state(u8"paused", u8"진행 일시정지", luil::taskbar_progress_state::paused);
        column.add(std::move(states), 26.0f);
        column.add_gap(6.0f);

        // 값은 정상·오류·일시정지에서만 단추에 닿는다. 셸에는 1000단계로 간다.
        luil::slider_config slider {};
        slider.maximum = 100.0f;
        slider.value = taskbar_percent_;
        slider.change = [](const float delta) { return luil::make_app_action(taskbar_value_intent { delta, true }); };
        slider.change_to = [](const float value) { return luil::make_app_action(taskbar_value_intent { value, false }); };
        auto value { std::make_unique<luil::slider_element>(luil::ui_element_id { kind_layout, u8"windows-taskbar-value" }, std::move(slider)) };
        value->set_tooltip(u8"작업 표시줄 진행 값");
        column.add(std::move(value), { .length = luil::slider_row_height, .cross_length = taskbar_slider_width });
        column.add_gap(6.0f);

        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-taskbar-controls" }, row_config) };
        const auto add_control = [&controls](std::u8string key, std::u8string text, const bool selected, luil::ui_action action, const float width) {
            controls->add(make_taskbar_button(std::move(key), std::move(text), selected, std::move(action)), width);
        };
        add_control(u8"taskbar-overlay", taskbar_overlay_on_ ? u8"오버레이 끄기" : u8"오버레이 켜기", taskbar_overlay_on_, luil::make_message_action(taskbar_overlay_intent { taskbar_overlay_on_ == false }), 120.0f);
        add_control(u8"taskbar-window-toggle", taskbar_window_open_ ? u8"독립 창 닫기" : u8"독립 창 열기", taskbar_window_open_, luil::make_message_action(taskbar_window_toggle_intent { taskbar_window_open_ == false }),
            120.0f);
        add_control(u8"taskbar-window-step", u8"독립 창 진행 바꾸기", false, luil::make_message_action(taskbar_window_step_intent {}), 140.0f);
        column.add(std::move(controls), 26.0f);
        column.add_gap(4.0f);
        std::u8string window_text { u8"작업 표시줄 독립 창: 닫힘" };
        if (taskbar_window_open_)
            window_text = u8"작업 표시줄 독립 창: 요청 " + taskbar_state_text(taskbar_window_state(taskbar_window_step_), taskbar_window_percent) + u8" · 반영 " + taskbar_report_text(taskbar_window_report_);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-taskbar-window" }, std::move(window_text), 12.0f, luil::label_color_role::primary), 18.0f);
    }

    std::vector<luil::win32::ui_window> windows_page::make_windows()
    {
        tool_menu_button_ = nullptr;
        std::vector<luil::win32::ui_window> windows {};
        if (tool_open_)
        {
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
        }
        if (taskbar_window_open_)
        {
            // 독립 창이다. 주 창에 매이지 않고 작업 표시줄에 따로 서므로 단추도 따로 있다.
            luil::win32::ui_window taskbar {};
            taskbar.id = std::u8string { taskbar_window_id };
            taskbar.caption = make_taskbar_window_caption();
            taskbar.x = taskbar_window_initial_x;
            taskbar.y = taskbar_window_initial_y;
            taskbar.width = taskbar_window_initial_width;
            taskbar.height = taskbar_window_initial_height;
            taskbar.minimum_width = 240.0f;
            taskbar.minimum_height = 120.0f;
            taskbar.independent = true;
            taskbar.tree = make_taskbar_window_tree();
            taskbar.metrics = [](const float width, const float height, const float scale) { return luil::app_message { taskbar_window_metrics_intent { width, height, scale } }; };
            taskbar.close = [] { return luil::make_app_action(taskbar_window_toggle_intent { false }); };
            taskbar.taskbar = make_taskbar_window_request();
            windows.push_back(std::move(taskbar));
        }
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
        // OS 기본 모서리의 popup이다. 둥근 창이면 메뉴의 사각 선은 끄고 경계를 OS 테두리에 맡긴다.
        menu.border = luil::popup_corner_radius(luil::popup_corner::system) <= 0.0f;

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

    std::shared_ptr<const luil::ui_tree> windows_page::make_taskbar_window_tree()
    {
        // metrics가 오기 전(창이 만들어지는 그 frame)에는 요청 크기로 배치한다.
        const float scale { taskbar_window_metrics_.scale > 0.0f ? taskbar_window_metrics_.scale : 1.0f };
        const float width { taskbar_window_metrics_.width > 0.0f ? taskbar_window_metrics_.width : taskbar_window_initial_width * scale };
        const float height { taskbar_window_metrics_.height > 0.0f ? taskbar_window_metrics_.height : taskbar_window_initial_height * scale };

        auto root { std::make_unique<luil::root_element>(std::u8string { taskbar_window_id }) };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });

        const luil::caption_config caption_config { make_taskbar_window_caption() };
        const float caption_height { luil::caption_element::height_for(caption_config) * scale };
        auto caption { std::make_unique<luil::caption_element>(caption_config) };
        caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
        root->add(std::move(caption));

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(16.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"taskbar-window" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"taskbar-window-hint" }, u8"독립 창이다 — 작업 표시줄에 따로 서고 자기 단추에 진행률이 걸린다.", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(4.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"taskbar-window-state" }, u8"이 창의 진행: " + taskbar_state_text(taskbar_window_state(taskbar_window_step_), taskbar_window_percent), 12.0f,
                        luil::label_color_role::primary),
            18.0f);
        column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
        root->add(std::move(column));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
}

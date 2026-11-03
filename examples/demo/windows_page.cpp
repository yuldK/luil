#include "demo/windows_page.h"

#include "luil/app/platform_services.h"
#include "luil/generated/codicons.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/commands.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/scroll_area_element.h"
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

        // 페이지 위쪽에 고정한 흘림 상태 줄이다 (논리 픽셀).
        // 본문은 이 줄 아래의 영역에서 흐르므로 영역이 받는 높이는 페이지 높이에서 이 줄을 뺀 값이다.
        constexpr luil::edge_insets scroll_status_padding { 24.0f, 8.0f, 24.0f, 4.0f };
        constexpr float scroll_status_line_height { 18.0f };
        constexpr float scroll_status_height { scroll_status_padding.top + scroll_status_line_height + scroll_status_padding.bottom };

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

        // 점프 목록의 작업 둘이다. 데모를 `--send-to-first`로 띄워, 떠 있는 데모가 있으면 그 창 페이지의 둘째 인스턴스 줄에
        // 인자가 닿는다. 아이콘은 실행 파일의 것이다.
        [[nodiscard]] luil::taskbar_jump_list make_demo_jump_list()
        {
            luil::taskbar_jump_list list {};
            list.tasks.push_back(luil::taskbar_jump_task {
                .title = u8"메모로 보내기"
                , .arguments = { u8"--send-to-first", u8"jump:note" }
                , .description = u8"떠 있는 데모의 창 페이지에 jump:note를 넘긴다"
            });
            list.tasks.push_back(luil::taskbar_jump_task {
                .title = u8"그림으로 보내기"
                , .arguments = { u8"--send-to-first", u8"jump:image" }
                , .description = u8"떠 있는 데모의 창 페이지에 jump:image를 넘긴다"
            });
            return list;
        }

        [[nodiscard]] std::u8string jump_list_request_text(const std::optional<bool>& filled)
        {
            if (filled.has_value() == false)
                return u8"손대지 않음";
            if (*filled)
                return u8"작업 " + to_u8(static_cast<int>(make_demo_jump_list().tasks.size())) + u8"개";
            return u8"비움";
        }

        // 보고를 한 줄로 옮긴다. 셸에 낸 일이 없으면 보고가 없다.
        [[nodiscard]] std::u8string jump_list_report_text(const std::optional<luil::win32::jump_list_report>& report)
        {
            if (report.has_value() == false)
                return u8"아직 보고 없음";
            std::u8string text {};
            switch (report->status)
            {
            case luil::win32::jump_list_status::applied:
                text = u8"적용됨 · 작업 " + to_u8(static_cast<int>(report->tasks)) + u8"개";
                break;
            case luil::win32::jump_list_status::removed:
                text = u8"지움";
                break;
            case luil::win32::jump_list_status::unavailable:
                text = u8"쓸 수 없음";
                break;
            case luil::win32::jump_list_status::failed:
            default:
                text = u8"실패";
                break;
            }
            if (report->error.empty() == false)
                text += u8" (" + report->error + u8")";
            return text;
        }

        // 둘째 인스턴스가 보낸 인자를 한 줄로 옮긴다: `2개 [a.txt] [b c] · 작업 폴더 <경로>`. 받은 적이 없으면 `없음`이다.
        [[nodiscard]] std::u8string instance_arguments_text(const std::optional<luil::instance_arguments>& received)
        {
            if (received.has_value() == false)
                return u8"없음";
            std::u8string text { to_u8(static_cast<int>(received->arguments.size())) + u8"개" };
            for (const std::u8string& argument : received->arguments)
                text += u8" [" + argument + u8"]";
            text += u8" · 작업 폴더 " + received->working_directory;
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

        // 알림 영역 아이콘의 원이다. 두 그림이 색으로 갈린다. 실행 중에 지은 그림이라 실행 파일 리소스에 기대지 않는다.
        //  - 셸이 알림 영역 크기(배율에 따라 16~32)로 맞춘다.
        [[nodiscard]] luil::ui_image make_tray_picture(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
        {
            constexpr int size { 32 };
            constexpr float radius { 14.0f };
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const float dx { static_cast<float>(x) + 0.5f - static_cast<float>(size) / 2.0f };
                    const float dy { static_cast<float>(y) + 0.5f - static_cast<float>(size) / 2.0f };
                    const float coverage { std::clamp(radius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f) };
                    std::uint8_t* const pixel { pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4 };
                    pixel[0] = red;
                    pixel[1] = green;
                    pixel[2] = blue;
                    pixel[3] = static_cast<std::uint8_t>(std::lround(coverage * 255.0f));
                }
            }
            return luil::make_rgba_image(size, size, pixels);
        }

        [[nodiscard]] std::u8string tray_report_text(const std::optional<luil::win32::tray_report>& report)
        {
            if (report.has_value() == false)
                return u8"아직 보고 없음";
            std::u8string text {};
            switch (report->status)
            {
            case luil::win32::tray_status::waiting:
                text = u8"대기 중";
                break;
            case luil::win32::tray_status::shown:
                text = u8"보임";
                break;
            case luil::win32::tray_status::failed:
                text = u8"실패";
                break;
            case luil::win32::tray_status::off:
            default:
                text = u8"끔";
                break;
            }
            if (report->error.empty() == false)
                text += u8" (" + report->error + u8")";
            return text;
        }

        // 데모가 거는 전역 단축키의 조합이다. 세 수정자와 F11이라 다른 프로그램과 겹치기 어렵다.
        [[nodiscard]] luil::key_chord hotkey_chord()
        {
            return luil::key_chord { luil::key_code::f11, true, true, true };
        }

        // 걸려는 id다. 둘째는 같은 조합이라 같은 목록의 앞 항목(첫째)이 이긴다.
        constexpr std::u8string_view hotkey_main_id { u8"demo" };
        constexpr std::u8string_view hotkey_extra_id { u8"demo-extra" };

        // 보고 하나의 상태를 한마디로 옮긴다. 실패의 영문 진단은 부르는 쪽이 덧붙인다.
        [[nodiscard]] std::u8string hotkey_status_text(const luil::win32::hotkey_status status)
        {
            switch (status)
            {
            case luil::win32::hotkey_status::registered:
                return u8"등록됨";
            case luil::win32::hotkey_status::conflict:
                return u8"충돌";
            case luil::win32::hotkey_status::reserved:
                return u8"예약된 조합";
            case luil::win32::hotkey_status::needs_modifier:
                return u8"수정자 필요";
            case luil::win32::hotkey_status::invalid_key:
                return u8"쓸 수 없는 키";
            case luil::win32::hotkey_status::invalid_id:
                return u8"쓸 수 없는 id";
            case luil::win32::hotkey_status::failed:
            default:
                return u8"실패";
            }
        }

        [[nodiscard]] const luil::win32::hotkey_report* hotkey_report_of(const std::vector<luil::win32::hotkey_report>& reports, const std::u8string_view id)
        {
            for (const luil::win32::hotkey_report& report : reports)
            {
                if (report.id == id)
                    return &report;
            }
            return nullptr;
        }

        // 반영 줄의 글이다: `등록됨`. 충돌 시험 중이고 둘째 id의 보고가 있으면 ` · 충돌 (demo-extra)`가 이어진다. 끈 동안은 `끔`이다.
        //  - 보고는 끌 때 지우지 않아 지난 충돌 시험의 둘째 id 보고가 남을 수 있다. 시험이 꺼져 있으면 그 보고는 그리지 않는다.
        [[nodiscard]] std::u8string hotkey_report_text(const bool on, const bool conflict, const std::vector<luil::win32::hotkey_report>& reports)
        {
            if (on == false)
                return u8"끔";
            const luil::win32::hotkey_report* const main_report { hotkey_report_of(reports, hotkey_main_id) };
            if (main_report == nullptr)
                return u8"아직 보고 없음";
            std::u8string text { hotkey_status_text(main_report->status) };
            if (main_report->error.empty() == false)
                text += u8" (" + main_report->error + u8")";
            if (const luil::win32::hotkey_report* const extra { conflict ? hotkey_report_of(reports, hotkey_extra_id) : nullptr }; extra != nullptr)
                text += u8" · " + hotkey_status_text(extra->status) + u8" (" + std::u8string { hotkey_extra_id } + u8")";
            return text;
        }

        [[nodiscard]] std::u8string tray_event_text(const luil::tray_icon_event& event)
        {
            switch (event.action)
            {
            case luil::tray_icon_action::double_click:
                return u8"두 번 클릭";
            case luil::tray_icon_action::context:
                return u8"오른쪽 클릭";
            case luil::tray_icon_action::select:
            default:
                return event.keyboard ? u8"고름 · 키보드" : u8"고름";
            }
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
        // 트레이에서 되살린 주 창을 앞으로 세운다. 라이브러리는 사건에서 창을 세우지 않는다 (tray-icon-design.md).
        if (focus_main_)
            return luil::focus_request { .id = focus_revision_, .activate = true };
        return luil::focus_request {
            .id = focus_revision_,
            .surface = u8"tool",
            .target = luil::ui_element_id { kind_tool_note_input },
            .activate = true,
        };
    }

    bool windows_page::handle(const luil::app_message& message)
    {
        // 위치는 0 아래로 내려가지 않는다. 위쪽 끝은 여기서, 아래쪽 끝은 페이지를 지을 때 다듬는다
        // (창 높이를 아는 것이 그때뿐이다).
        if (const auto* const scroll { message.get<windows_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const scroll_to { message.get<windows_scroll_to_intent>() }; scroll_to != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_to->offset);
            return true;
        }
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
            focus_main_ = false;
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
        if (const auto* const fill { message.get<jump_list_fill_intent>() }; fill != nullptr)
        {
            jump_list_filled_ = fill->fill;
            return true;
        }
        if (const auto* const report { message.get<jump_list_report_intent>() }; report != nullptr)
        {
            jump_list_report_ = report->report;
            return true;
        }
        if (const auto* const received { message.get<instance_arguments_intent>() }; received != nullptr)
        {
            instance_arguments_ = received->arguments;
            return true;
        }
        if (const auto* const toggle { message.get<tray_toggle_intent>() }; toggle != nullptr)
        {
            if (toggle->on && tray_pictures_[0].valid() == false)
                tray_pictures_ = { make_tray_picture(52, 120, 220), make_tray_picture(40, 170, 90) };
            tray_on_ = toggle->on;
            // 아이콘이 없으면 숨은 주 창을 되살릴 길이 없다.
            if (tray_on_ == false)
                tray_hidden_ = false;
            return true;
        }
        if (message.get<tray_picture_intent>() != nullptr)
        {
            tray_picture_ = 1 - tray_picture_;
            return true;
        }
        if (const auto* const event { message.get<tray_event_intent>() }; event != nullptr)
        {
            tray_event_ = tray_event_text(event->event);
            return true;
        }
        if (const auto* const menu { message.get<tray_menu_intent>() }; menu != nullptr)
        {
            if (menu->key == u8"show")
            {
                tray_hidden_ = false;
                focus_main_ = true;
                ++focus_revision_;
                tray_event_ = u8"메뉴: 창 보이기";
            }
            else if (menu->key == u8"hide")
            {
                // 아이콘이 서 있을 때만 숨긴다. 메뉴 항목도 그때만 켜져 있다.
                if (tray_report_.has_value() && tray_report_->status == luil::win32::tray_status::shown)
                    tray_hidden_ = true;
                tray_event_ = u8"메뉴: 주 창 숨기기";
            }
            return true;
        }
        if (const auto* const report { message.get<tray_report_intent>() }; report != nullptr)
        {
            tray_report_ = report->report;
            // 아이콘이 내려가면 숨은 주 창을 되살린다.
            if (report->report.status != luil::win32::tray_status::shown)
                tray_hidden_ = false;
            return true;
        }
        if (const auto* const toggle { message.get<hotkey_toggle_intent>() }; toggle != nullptr)
        {
            hotkey_on_ = toggle->on;
            // 끄면 충돌 시험을 끈다. 지난 보고는 지우지 않고 반영 줄이 `끔`으로만 그린다. 빠르게 끄고 켜면 두 번의 바뀜이 한
            // wake에 묶여 목록이 그대로라, 라이브러리가 새 보고를 내지 않는다. 지웠다면 반영 줄이 `아직 보고 없음`에 머문다.
            if (toggle->on == false)
                hotkey_conflict_ = false;
            return true;
        }
        if (message.get<hotkey_conflict_intent>() != nullptr)
        {
            if (hotkey_on_)
                hotkey_conflict_ = true;
            return true;
        }
        if (const auto* const front { message.get<hotkey_front_intent>() }; front != nullptr)
        {
            hotkey_front_ = front->on;
            return true;
        }
        if (message.get<hotkey_pressed_intent>() != nullptr)
        {
            ++hotkey_presses_;
            // 라이브러리는 눌림에서 창을 앞으로 세우지 않는다. 앱이 숨김을 끄고 활성화를 청한다 (트레이 메뉴의 `창 보이기`와 같은 길).
            if (hotkey_front_)
            {
                tray_hidden_ = false;
                focus_main_ = true;
                ++focus_revision_;
            }
            return true;
        }
        if (const auto* const report { message.get<hotkey_report_intent>() }; report != nullptr)
        {
            // 끈 뒤에 늦게 온 보고는 버린다. 항목마다 마지막 보고만 둔다.
            if (hotkey_on_ == false)
                return true;
            const auto found { std::find_if(hotkey_reports_.begin(), hotkey_reports_.end(), [report](const luil::win32::hotkey_report& item) { return item.id == report->report.id; }) };
            if (found != hotkey_reports_.end())
                *found = report->report;
            else
                hotkey_reports_.push_back(report->report);
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
        return make_scroll_page(build_content(), width, height, scale);
    }

    std::unique_ptr<luil::ui_element> windows_page::make_scroll_page(std::unique_ptr<luil::stack_element> content, const float width, const float height, const float scale)
    {
        // 내용 높이는 담긴 줄에서 읽는다. 구역이 줄을 더해도 손으로 센 합이 어긋날 일이 없다.
        //  - 줄은 모두 고정 길이라 글 측정기는 이 높이에 닿지 않는다. 맞춤 줄을 더해도 같은 식이 센다.
        const luil::measure_context measure { .scale = scale };
        const float content_height { content->preferred_size(measure, std::max(0.0f, width - luil::scroll_area_bar_width)).value_or(luil::size_f {}).height };
        const float viewport_height { std::max(0.0f, height - scroll_status_height) };
        // 창이 커져 흘릴 것이 줄었으면 쌓인 위치도 되돌린다. 그대로 두면 끝에서 돌린 휠이 보이지 않는 곳에 쌓여
        // 되돌릴 때 아무 일도 없는 것처럼 보인다.
        scroll_ = luil::clamp_scroll(content_height, viewport_height, scroll_);

        luil::scroll_area_config area_config {};
        area_config.owner = u8"windows";
        area_config.content_height = content_height;
        area_config.scroll_offset = scroll_;
        area_config.scroll = [](const float delta) { return luil::make_app_action(windows_scroll_intent { delta }); };
        area_config.scroll_to = [](const float offset) { return luil::make_app_action(windows_scroll_to_intent { offset }); };
        // 막대 자리를 늘 지킨다. 구역이 늘고 줄어도 글의 폭이 흔들리지 않는다.
        area_config.bar = luil::scrollbar_visibility::always;
        auto area { std::make_unique<luil::scroll_area_element>(std::move(area_config)) };
        area->set_content(std::move(content));

        // 상태 줄은 영역 밖에 고정한다. 흘려도 읽히고, 화면 밖으로 간 구역을 UIA가 찾을 때도 같은 이름이다.
        const float maximum_scroll { std::max(0.0f, content_height - viewport_height) };
        std::u8string status_text { u8"페이지 흘림: " + to_u8(static_cast<int>(std::lround(scroll_))) + u8" / " + to_u8(static_cast<int>(std::lround(maximum_scroll))) };
        luil::stack_config status_config {};
        status_config.padding = scroll_status_padding;
        auto status { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-scroll-status" }, status_config) };
        status->add(make_label(luil::ui_element_id { kind_text, u8"windows-scroll" }, std::move(status_text), 11.0f, luil::label_color_role::dim), scroll_status_line_height);

        luil::stack_config page_config {};
        auto page { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-page" }, page_config) };
        page->add(std::move(status), scroll_status_height);
        page->add_flexible(std::move(area));
        return page;
    }

    std::unique_ptr<luil::stack_element> windows_page::build_content()
    {
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
        add_instance_section(*column);
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
        // 누르기 전에는 손대지 않는다. 앞 실행이 남긴 목록은 비우기를 눌러야 지워진다.
        if (jump_list_filled_.has_value())
            request.jump_list = *jump_list_filled_ ? make_demo_jump_list() : luil::taskbar_jump_list {};
        return request;
    }

    luil::taskbar_request windows_page::make_taskbar_window_request() const
    {
        luil::taskbar_request request {};
        request.progress.state = taskbar_window_state(taskbar_window_step_);
        request.progress.value = taskbar_window_percent / 100.0f;
        return request;
    }

    void windows_page::add_instance_section(luil::stack_element& column)
    {
        column.add_gap(18.0f);
        const std::u8string hint { u8"같은 이름의 둘째 실행이 인자와 작업 폴더를 넘기면 아래 줄에 보인다. 둘째는 luil_demo.exe --send-to-first 뒤에 인자를 적어 띄운다." };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-instance-hint" }, hint, 11.0f, luil::label_color_role::dim), 18.0f);
        column.add_gap(4.0f);
        std::u8string received { u8"둘째 인스턴스 인자: " + instance_arguments_text(instance_arguments_) };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-instance-arguments" }, std::move(received), 12.0f, luil::label_color_role::primary), 18.0f);
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
        add_jump_list_section(column);
    }

    void windows_page::add_jump_list_section(luil::stack_element& column)
    {
        column.add_gap(12.0f);
        const std::u8string hint { u8"점프 목록은 작업 표시줄 단추를 오른쪽 클릭하면 보인다. 작업을 누르면 둘째 실행이 떠 위의 둘째 인스턴스 인자 줄에 닿는다." };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-jump-list-hint" }, hint, 11.0f, luil::label_color_role::dim), 18.0f);
        column.add_gap(4.0f);
        // 두 줄이 UIA로 읽힌다: 앱이 바라는 것, 셸에 닿은 정도.
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-jump-list-request" }, u8"점프 목록 요청: " + jump_list_request_text(jump_list_filled_), 12.0f, luil::label_color_role::primary), 18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-jump-list-report" }, u8"점프 목록 반영: " + jump_list_report_text(jump_list_report_), 12.0f, luil::label_color_role::primary), 18.0f);
        column.add_gap(6.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-jump-list-controls" }, row_config) };
        const bool filled { jump_list_filled_.value_or(false) };
        const bool cleared { jump_list_filled_.has_value() && filled == false };
        controls->add(make_taskbar_button(u8"jump-list-fill", u8"점프 목록 채우기", filled, luil::make_message_action(jump_list_fill_intent { true })), 130.0f);
        controls->add(make_taskbar_button(u8"jump-list-clear", u8"점프 목록 비우기", cleared, luil::make_message_action(jump_list_fill_intent { false })), 130.0f);
        column.add(std::move(controls), 26.0f);
        add_tray_section(column);
    }

    std::optional<luil::tray_icon> windows_page::make_tray_request() const
    {
        if (tray_on_ == false || tray_pictures_[0].valid() == false)
            return std::nullopt;
        luil::tray_icon icon {};
        icon.icon = tray_pictures_[static_cast<std::size_t>(tray_picture_)];
        icon.tooltip = u8"luil 데모 · 그림 " + to_u8(tray_picture_ + 1);
        const bool shown { tray_report_.has_value() && tray_report_->status == luil::win32::tray_status::shown };
        luil::menu_config menu {};
        menu.owner = u8"tray";
        menu.items.push_back({ .key = u8"show", .label = u8"창 보이기" });
        menu.items.push_back({ .key = u8"hide", .label = u8"주 창 숨기기", .enabled = shown && tray_hidden_ == false });
        menu.items.push_back({ .key = u8"exit", .label = u8"끝내기", .separator_above = true });
        // 끝내기는 창 명령이라 logic thread를 지나지 않는다. 닫힘 훅이 있어도 비켜 간다.
        menu.select = [](const std::u8string& key) {
            if (key == u8"exit")
                return luil::input_action { luil::ui_command::application_exit };
            return luil::make_app_action(tray_menu_intent { key });
        };
        icon.menu = std::move(menu);
        icon.activate = [](const luil::tray_icon_event& event) { return luil::make_app_action(tray_event_intent { event }); };
        return icon;
    }

    void windows_page::add_tray_section(luil::stack_element& column)
    {
        column.add_gap(18.0f);
        const std::u8string hint { u8"알림 영역에 데모 아이콘을 세운다. 오른쪽 클릭하면 시스템 메뉴가 뜬다. Windows 11은 넘침 칸에 둘 수 있다." };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-tray-hint" }, hint, 11.0f, luil::label_color_role::dim), 18.0f);
        column.add_gap(4.0f);
        // 네 줄이 UIA로 읽힌다: 이 프로세스가 아이콘을 세울 수 있는가, 앱이 바라는 것, 셸에 닿은 정도, 마지막 사건.
        const bool capable { luil::current_ui_platform().capabilities.tray };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-tray-capability" }, capable ? u8"트레이 능력: 있음" : u8"트레이 능력: 없음", 12.0f, luil::label_color_role::primary), 18.0f);
        const std::u8string request { tray_on_ ? u8"켬 · 그림 " + to_u8(tray_picture_ + 1) + u8" · 메뉴 있음" : u8"끔" };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-tray-request" }, u8"트레이 요청: " + request, 12.0f, luil::label_color_role::primary), 18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-tray-report" }, u8"트레이 반영: " + tray_report_text(tray_report_), 12.0f, luil::label_color_role::primary), 18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-tray-event" }, u8"트레이 사건: " + (tray_event_.empty() ? std::u8string { u8"없음" } : tray_event_), 12.0f,
                       luil::label_color_role::primary),
            18.0f);
        column.add_gap(6.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        // 셋을 한 줄에 두면 기본 창의 200% 배율에서 오른쪽이 잘린다. 그림 바꾸기는 다음 줄에 둔다.
        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-tray-controls" }, row_config) };
        controls->add(make_taskbar_button(u8"tray-on", u8"트레이 아이콘 켜기", tray_on_, luil::make_message_action(tray_toggle_intent { true })), 140.0f);
        controls->add(make_taskbar_button(u8"tray-off", u8"트레이 아이콘 끄기", false, luil::make_message_action(tray_toggle_intent { false })), 140.0f);
        column.add(std::move(controls), 26.0f);
        column.add_gap(6.0f);
        column.add(make_taskbar_button(u8"tray-picture", u8"트레이 그림 바꾸기", false, luil::make_message_action(tray_picture_intent {})), { .length = 26.0f, .cross_length = 140.0f });
        add_hotkey_section(column);
    }

    std::vector<luil::global_hotkey> windows_page::make_hotkeys_request() const
    {
        std::vector<luil::global_hotkey> hotkeys {};
        if (hotkey_on_ == false)
            return hotkeys;
        const auto make_hotkey = [](const std::u8string_view id) {
            luil::global_hotkey hotkey {};
            hotkey.id = std::u8string { id };
            hotkey.chord = hotkey_chord();
            hotkey.pressed = [id = std::u8string { id }] { return luil::make_app_action(hotkey_pressed_intent { id }); };
            return hotkey;
        };
        hotkeys.push_back(make_hotkey(hotkey_main_id));
        // 같은 조합의 둘째 id다. 목록에서 앞 항목이 이기므로 OS를 부르지 않고 충돌로 보고된다.
        if (hotkey_conflict_)
            hotkeys.push_back(make_hotkey(hotkey_extra_id));
        return hotkeys;
    }

    void windows_page::add_hotkey_section(luil::stack_element& column)
    {
        column.add_gap(18.0f);
        const std::u8string hint { u8"앱이 뒤에 있거나 창이 숨어 있어도 Ctrl+Shift+Alt+F11을 받는다. 다른 프로그램이 쓰는 조합은 걸리지 않고 충돌로 보인다." };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-hotkey-hint" }, hint, 11.0f, luil::label_color_role::dim), 18.0f);
        column.add_gap(4.0f);
        // 네 줄이 UIA로 읽힌다: 이 프로세스가 걸 수 있는가, 앱이 바라는 것, OS에 닿은 정도, 눌린 횟수.
        const bool capable { luil::current_ui_platform().capabilities.global_hotkeys };
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-hotkey-capability" }, capable ? u8"전역 단축키 능력: 있음" : u8"전역 단축키 능력: 없음", 12.0f, luil::label_color_role::primary),
            18.0f);
        std::u8string request { u8"끔" };
        if (hotkey_on_)
        {
            request = u8"켬 · " + luil::shortcut_label(hotkey_chord());
            if (hotkey_conflict_)
                request += u8" · 충돌 시험";
        }
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-hotkey-request" }, u8"전역 단축키 요청: " + request, 12.0f, luil::label_color_role::primary), 18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-hotkey-report" }, u8"전역 단축키 반영: " + hotkey_report_text(hotkey_on_, hotkey_conflict_, hotkey_reports_), 12.0f, luil::label_color_role::primary),
            18.0f);
        column.add(make_label(luil::ui_element_id { kind_text, u8"windows-hotkey-pressed" }, u8"전역 단축키 눌림: " + to_u8(hotkey_presses_) + u8"번", 12.0f, luil::label_color_role::primary), 18.0f);
        column.add_gap(6.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"windows-hotkey-controls" }, row_config) };
        controls->add(make_taskbar_button(u8"hotkey-on", u8"전역 단축키 켜기", hotkey_on_, luil::make_message_action(hotkey_toggle_intent { true })), 140.0f);
        controls->add(make_taskbar_button(u8"hotkey-off", u8"전역 단축키 끄기", false, luil::make_message_action(hotkey_toggle_intent { false })), 140.0f);
        column.add(std::move(controls), 26.0f);
        column.add_gap(6.0f);

        // 확인란은 스스로 바뀌지 않는다. 바뀐 뒤의 상태를 실은 액션을 매 frame 새로 싣는다.
        luil::check_config front {};
        front.owner = u8"hotkey-front";
        front.style = luil::check_style::checkbox;
        front.label = u8"눌리면 창 앞으로";
        front.checked = hotkey_front_;
        front.tooltip = u8"켜면 눌릴 때 숨은 주 창을 되살려 앞으로 세운다. 라이브러리는 창을 세우지 않는다";
        front.toggle = luil::make_message_action(hotkey_front_intent { hotkey_front_ == false });
        column.add(std::make_unique<luil::check_element>(std::move(front)), luil::check_row_height);
        column.add_gap(6.0f);
        // 마지막 단추다. 같은 조합의 둘째 id를 더한다. 끄기가 되돌린다.
        column.add(make_taskbar_button(u8"hotkey-conflict", u8"충돌 시험 켜기", hotkey_conflict_, luil::make_message_action(hotkey_conflict_intent {})), { .length = 26.0f, .cross_length = 140.0f });
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

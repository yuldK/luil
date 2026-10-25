#include "demo/palette.h"

#include "demo/files_page.h"
#include "luil/ui/command_palette.h"
#include "luil/ui/ui_tree.h"

#include <array>
#include <memory>
#include <string_view>
#include <utility>

namespace demo {
    namespace {
        constexpr float palette_width { 460.0f };

        struct page_entry
        {
            std::u8string_view key {};
            std::u8string_view label {};
        };

        // 사이드바와 같은 차례·이름이다.
        constexpr std::array<page_entry, 16> pages {
            page_entry { page_overview, u8"시작 / 기능 색인" },
            page_entry { page_controls, u8"버튼과 컨트롤" },
            page_entry { page_basics, u8"입력과 다이얼로그" },
            page_entry { page_lists, u8"목록" },
            page_entry { page_files, u8"파일" },
            page_entry { page_tabs, u8"탭" },
            page_entry { page_groups, u8"그룹" },
            page_entry { page_toasts, u8"토스트" },
            page_entry { page_popups, u8"메뉴·팝업" },
            page_entry { page_windows, u8"창" },
            page_entry { page_images, u8"이미지" },
            page_entry { page_network, u8"네트워크" },
            page_entry { page_webview, u8"웹뷰" },
            page_entry { page_zoom, u8"확대 보기" },
            page_entry { page_theme, u8"모양과 색" },
            page_entry { page_workbench, u8"변경 작업대" },
        };
    } // namespace

    std::vector<luil::app_command> palette_commands()
    {
        std::vector<luil::app_command> commands {};
        for (const page_entry& page : pages)
            commands.push_back({ .id = u8"page:" + std::u8string { page.key }, .label = u8"페이지 열기: " + std::u8string { page.label } });
        commands.push_back({ .id = u8"view.sidebar", .label = u8"사이드바 접기·펴기", .shortcut = { luil::key_code::key_b, true } });
        return commands;
    }

    bool command_palette_host::handle(const luil::app_message& message)
    {
        if (message.get<palette_open_intent>() != nullptr)
        {
            // 열 때마다 빈 질의에서 시작한다 — 지난 질의가 남으면 첫 결과가 엉뚱한 명령이다.
            open_ = true;
            ++focus_revision_;
            query_ = {};
            composition_.reset();
            return true;
        }
        if (message.get<palette_close_intent>() != nullptr)
        {
            open_ = false;
            return true;
        }
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            if (edit->request.target != target_palette)
                return false;
            luil::apply_text_edit(query_, edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_palette)
                return false;
            if (composition->event.composing)
                composition_ = composition->event;
            else
                composition_.reset();
            return true;
        }
        return false;
    }

    std::optional<luil::focus_request> command_palette_host::make_focus_request() const
    {
        if (open_ == false || focus_revision_ == 0)
            return std::nullopt;
        // popup은 활성화를 받지 않는 표면이라 초점만 옮긴다. popup이 서는 frame을 기다린다 (timeout).
        return luil::focus_request { .id = focus_revision_, .surface = u8"palette", .target = luil::ui_element_id { luil::ui_element_kind::command_palette_search, u8"palette" } };
    }

    std::vector<luil::win32::ui_popup> command_palette_host::make_popups(const float window_width, const float top, const float scale) const
    {
        std::vector<luil::win32::ui_popup> popups {};
        if (open_ == false)
            return popups;

        luil::command_palette_config config {};
        config.owner = u8"palette";
        config.commands = palette_commands();
        config.query = luil::make_text_input_view(query_, composition_, target_palette);
        config.placeholder = u8"명령 찾기";
        config.empty_label = u8"일치하는 명령 없음";
        config.run = [](const std::u8string& id) { return luil::make_app_action(palette_run_intent { id }); };
        config.close = luil::make_message_action(palette_close_intent {});

        luil::win32::ui_popup popup {};
        popup.id = u8"palette";
        popup.width = palette_width;
        popup.height = luil::command_palette_height(config);
        popup.x = (window_width - palette_width) / 2.0f;
        popup.y = top;
        popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(luil::make_command_palette(std::move(config)), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        popup.dismiss = [](const luil::win32::popup_dismiss_reason reason) {
            // 검색 중이던 팔레트다 — 창을 옮기거나 잠깐 다른 창을 봐도 남는다.
            if (search_popup_stays(reason))
                return luil::input_action {};
            return luil::make_app_action(palette_close_intent {});
        };
        popups.push_back(std::move(popup));
        return popups;
    }
} // namespace demo

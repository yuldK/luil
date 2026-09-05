// luil 사용 예제이자 smoke test 실행 파일이다.
// 라이브러리 소비자가 구현해야 하는 세 조각(logic_driver, interaction_policy,
// window_delegate)의 실제 형태를 보여 준다.
//
// 화면은 접이식 사이드바로 오가는 페이지들이고, 각 페이지가 element 하나씩을 다룬다.
//  - 기본: 버튼·카운터, 텍스트 입력, 확인 dialog          (demo/basics_page)
//  - 목록: drag 순서 바꾸기, 스크롤 막대, sticky 그룹 머리행 (demo/lists_page)
//  - 탭: 선택·닫기·순서·가로 스크롤·넘침 메뉴 popup        (demo/tabs_page)
//  - 그룹: 접이식 섹션, 라디오·토글 묶음                   (demo/groups_page)
//  - 토스트: 심각도별 알림과 실행 취소 토스트               (demo/toasts_page)
//  - 창: 보조 top-level 창(도구 창) 여닫기와 그 안의 입력   (demo/windows_page)
//  - 이미지: 파일을 끌어다 놓거나 골라 그림 미리 보기       (demo/images_page)
//  - 테마: 테마 선호와 키 컬러                             (demo/theme_page)
// 이 파일은 셸이다: 사이드바 내비게이션, 페이지 전환, driver·policy·delegate 조립.
//
// 인자: --renderer=auto|cpu|direct3d, --smoke-test, --simulate-direct3d-failure,
//       --simulate-direct3d-loss-after-frames=N (N frame을 그린 뒤 Direct3D를 잃는다 —
//                       생성 시점 실패와 달리 **제시에 붙은 창을 놓고** CPU로 물러선다)
//       --position=x,y (창이 뜰 화면 자리 — 물리 픽셀, 가상 화면 좌표라 음수도 된다.
//                       지난 실행이 저장한 배치보다 세다)

#include "demo/basics_page.h"
#include "demo/common.h"
#include "demo/groups_page.h"
#include "demo/images_page.h"
#include "demo/lists_page.h"
#include "demo/tabs_page.h"
#include "demo/theme_page.h"
#include "demo/toasts_page.h"
#include "demo/windows_page.h"

#include "luil/ui/caption_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/sidebar_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/transition.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/win32_window.h"

#include "luil/generated/codicons.h"

#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace demo {
    namespace {
        // 사이드바의 처음 폭과 접힌 폭이다 (논리 픽셀).
        constexpr float sidebar_default_width { 200.0f };
        constexpr float sidebar_collapsed_width { 44.0f };

        // --- 창 배치의 저장·복원 ---
        // 라이브러리는 배치를 보고하고(placement_intent) 요청을 적용할 뿐
        // (`window_placement_request` — 너무 작은 값·화면 밖은 그쪽이 거른다),
        // 어디에 어떻게 적는지는 앱의 몫이다 (multi-window-design.md).

        // 저장 위치다.
        // 환경 변수를 읽지 못하면 빈 경로이고 저장·복원이 그대로 빠진다.
        [[nodiscard]] std::filesystem::path placement_file_path()
        {
            wchar_t buffer[MAX_PATH] {};
            const DWORD length { GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) };
            if (length == 0 || length >= MAX_PATH)
                return {};
            return std::filesystem::path { buffer } / L"luil-demo" / L"placement.txt";
        }

        // "x,y,width,height,max"를 배치로 읽는다 (십진 정수 다섯, max는 0·1).
        // 형식이 다르면 빈 값이다 — 앞부분만 조용히 읽지 않는다
        // (parse_window_position과 같은 규칙이다).
        [[nodiscard]] std::optional<luil::win32::window_placement> parse_placement(const std::u8string_view text) noexcept
        {
            std::array<int, 5> values {};
            const char* cursor { reinterpret_cast<const char*>(text.data()) };
            const char* const end { cursor + text.size() };
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (index > 0)
                {
                    if (cursor == end || *cursor != ',')
                        return std::nullopt;
                    ++cursor;
                }
                const auto [next, error] { std::from_chars(cursor, end, values[index]) };
                if (error != std::errc {} || next == cursor)
                    return std::nullopt;
                cursor = next;
            }
            if (cursor != end || (values[4] != 0 && values[4] != 1))
                return std::nullopt;
            const luil::win32::window_placement placement { values[0], values[1], values[2], values[3], values[4] != 0 };
            if (placement.valid() == false)
                return std::nullopt;
            return placement;
        }

        [[nodiscard]] std::string format_placement(const luil::win32::window_placement& placement)
        {
            return std::to_string(placement.x) + "," + std::to_string(placement.y) + "," + std::to_string(placement.width) + "," + std::to_string(placement.height) + ","
                + (placement.maximized ? "1" : "0");
        }

        [[nodiscard]] std::optional<luil::win32::window_placement> load_placement()
        {
            const std::filesystem::path path { placement_file_path() };
            if (path.empty())
                return std::nullopt;
            std::ifstream file { path, std::ios::binary };
            if (file.is_open() == false)
                return std::nullopt;
            std::string line {};
            std::getline(file, line);
            if (line.empty() == false && line.back() == '\r')
                line.pop_back();
            return parse_placement(std::u8string_view { reinterpret_cast<const char8_t*>(line.data()), line.size() });
        }

        void save_placement(const luil::win32::window_placement& placement)
        {
            const std::filesystem::path path { placement_file_path() };
            if (path.empty())
                return;
            std::error_code error {};
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
                return;
            std::ofstream file { path, std::ios::binary | std::ios::trunc };
            if (file.is_open() == false)
                return;
            const std::string text { format_placement(placement) };
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        [[nodiscard]] luil::caption_config make_caption_config()
        {
            luil::caption_config caption {};
            caption.title = u8"luil demo";
            caption.application_icon = luil::codicons::icon_rocket;
            caption.minimize_tooltip = u8"Minimize";
            caption.maximize_tooltip = u8"Maximize or restore";
            caption.close_tooltip = u8"Close";
            return caption;
        }

        // logic thread가 소유하는 앱 상태다.
        // 페이지가 자기 상태·메시지·화면을 맡고, 셸은 내비게이션과 조립만 한다.
        class demo_driver final : public luil::win32::logic_driver
        {
        public:
            // 지난 실행이 갈무리한 배치다.
            // 비어 있으면 복원하지 않는다 (첫 실행·--position이 이긴 실행).
            explicit demo_driver(const std::optional<luil::win32::window_placement> restore) noexcept
                : restore_ { restore }
            {}

            void handle(luil::app_message message) override
            {
                if (message.get<close_intent>() != nullptr)
                {
                    // 종료 저장이다.
                    // 배치 보고는 종료 신호보다 먼저 게시되므로(같은 채널의 순서)
                    // placement_는 닫히던 순간의 값이다.
                    if (placement_.valid())
                        save_placement(placement_);
                    closed_.store(true);
                    return;
                }
                if (const auto* const placement { message.get<placement_intent>() }; placement != nullptr)
                {
                    placement_ = placement->placement;
                    return;
                }
                if (const auto* const metrics { message.get<window_metrics_intent>() }; metrics != nullptr)
                {
                    metrics_ = *metrics;
                    return;
                }
                if (const auto* const navigate { message.get<navigate_intent>() }; navigate != nullptr)
                {
                    page_ = navigate->page;
                    // 페이지를 떠나면 그 페이지의 popup도 닫는다.
                    tabs_.close_popups();
                    return;
                }
                if (message.get<sidebar_toggle_intent>() != nullptr)
                {
                    sidebar_collapsed_ = sidebar_collapsed_ == false;
                    // **지금 폭에서** 새 목표로 이어 출발한다. 반쯤 접힌 채 다시
                    // 눌러도 끝값으로 튀었다 오지 않는다 (transition_to의 몫이다).
                    //  - 시계를 읽는 것은 앱이다. 라이브러리는 시각을 인자로 받을 뿐이다.
                    sidebar_motion_ = luil::transition_to(sidebar_motion_, target_sidebar_width(), std::chrono::steady_clock::now());
                    return;
                }
                if (const auto* const resize { message.get<sidebar_resize_intent>() }; resize != nullptr)
                {
                    sidebar_width_ += resize->delta;
                    if (sidebar_width_ < 150.0f)
                        sidebar_width_ = 150.0f;
                    if (sidebar_width_ > 360.0f)
                        sidebar_width_ = 360.0f;
                    // 손으로 끄는 동안은 전환하지 않는다 — 손이 이미 그 자리를 정하고
                    // 있어서, 뒤따라오는 애니메이션은 지연으로만 느껴진다.
                    sidebar_motion_ = luil::transition { sidebar_width_, sidebar_width_, {}, std::chrono::milliseconds { 0 } };
                    return;
                }
                if (message.get<popup_close_intent>() != nullptr)
                {
                    tabs_.close_popups();
                    windows_.close_popups();
                    return;
                }
                // 컨텍스트 메뉴 선택은 페이지가 메뉴를 닫고 셸이 토스트로 잇는다.

                // 도구 창 메뉴의 선택도 같은 규칙이다: 페이지가 메뉴(와 필요하면 창)를 닫고 셸이 잇는다.
                if (const auto* const tool_select { message.get<tool_menu_select_intent>() }; tool_select != nullptr)
                {
                    static_cast<void>(windows_.handle(message));
                    if (tool_select->key == u8"toast")
                        static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { u8"도구 창 메뉴에서 보낸 토스트", luil::toast_severity::success } }));
                    return;
                }

                // 페이지들의 메시지다. 타입이 겹치지 않아 처음 받는 쪽이 임자다.
                // 편집 메시지(edit_intent)만 타입이 같고 target으로 나뉜다 — 남의 target이면 handle이 거짓을 돌려준다.
                if (basics_.handle(message) || lists_.handle(message) || tabs_.handle(message) || groups_.handle(message) || toasts_.handle(message)
                    || windows_.handle(message) || images_.handle(message) || theme_.handle(message))
                    return;
            }

            [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
            {
                const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
                const float width { metrics_.width > 0.0f ? metrics_.width : 1280.0f };
                const float height { metrics_.height > 0.0f ? metrics_.height : 720.0f };

                auto root { std::make_unique<luil::root_element>() };
                root->arrange({ { 0.0f, 0.0f, width, height }, scale });

                // 높이는 height_for가 미리 알려 준다 (미리 arrange해 보는 우회가 필요 없다).
                const luil::caption_config caption_config { make_caption_config() };
                const float caption_height { luil::caption_element::height_for(caption_config) * scale };
                auto caption { std::make_unique<luil::caption_element>(caption_config) };
                caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
                root->add(std::move(caption));

                // 캡션 아래는 사이드바 + 현재 페이지의 가로 stack이다.
                const luil::sidebar_config sidebar_config { make_sidebar_config() };
                const float sidebar_width { luil::sidebar_element::width_for(sidebar_config) };
                const float content_width { width / scale - sidebar_width };
                const float content_height { (height - caption_height) / scale };

                luil::stack_config shell_config {};
                shell_config.direction = luil::stack_direction::row;
                auto shell { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"shell" }, shell_config) };
                shell->add(std::make_unique<luil::sidebar_element>(sidebar_config, sidebar_collapsed_ ? nullptr : make_navigation()), sidebar_width);
                shell->add_flexible(build_page(content_width, content_height, scale));
                shell->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
                root->add(std::move(shell));

                // 오버레이들: dialog는 화면을 덮고 토스트는 오른쪽 아래에 쌓인다.
                basics_.add_overlay(*root, width, height, scale);
                if (auto toasts { toasts_.make_overlay() }; toasts != nullptr)
                {
                    toasts->arrange({ { 0.0f, 0.0f, width, height }, scale });
                    root->add(std::move(toasts));
                }

                auto frame { std::make_shared<luil::win32::ui_frame>() };
                frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
                frame->appearance = theme_.appearance();
                // 크기·최대화의 복원이다. 자리는 창을 만들 때 이미 잡았다 (wWinMain).
                // 요청은 revision이 바뀔 때만 적용되므로 매 frame 같은 값을 실어도
                // 창은 한 번만 움직인다.
                if (restore_.has_value())
                {
                    frame->window_placement_revision = 1;
                    frame->window_placement_request = restore_;
                }
                // popup은 이번 frame에서 배치된 element의 자리를 쓰므로 tree를 만든 뒤 단다.
                // 현재 페이지의 것만 뜬다 (다른 페이지의 열림 상태는 이동 때 닫았다).
                for (luil::win32::ui_popup& popup : tabs_.make_popups(scale))
                    frame->popups.push_back(std::move(popup));
                // 도구 창은 페이지의 일부가 아니라 어느 페이지에서든 열려 있다.
                for (luil::win32::ui_window& window : windows_.make_windows())
                    frame->windows.push_back(std::move(window));
                // 도구 창 안의 메뉴다. 앵커가 그 창이라 자리도 그 창 기준이고,
                // 창 tree를 만든 뒤라야 메뉴 버튼의 자리를 안다.
                for (luil::win32::ui_popup& popup : windows_.make_popups())
                    frame->popups.push_back(std::move(popup));
                return frame;
            }

            [[nodiscard]] luil::app_message make_close_message() override
            {
                return luil::app_message { close_intent {} };
            }

            [[nodiscard]] bool shutdown_completed() const override
            {
                return closed_.load();
            }

            // 시간이 지나야 바뀌는 것 둘이다: 토스트 만료와 사이드바 접힘 전환.
            // 더 이른 쪽을 예고하면 메시지가 없어도 그 시각에 tick이 온다.
            //  - 전환이 끝나면 그쪽이 nullopt를 답해 시간 경로가 다시 잠잔다.
            [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override
            {
                std::optional<std::chrono::steady_clock::time_point> next { toasts_.next_expiry() };
                const std::optional<std::chrono::steady_clock::time_point> motion { luil::transition_next_update(sidebar_motion_, std::chrono::steady_clock::now()) };
                if (motion.has_value() && (next.has_value() == false || *motion < *next))
                    next = motion;
                return next;
            }

            void tick(std::chrono::steady_clock::time_point) override
            {
                toasts_.prune();
            }

        private:
            // 접힘이 끝났을 때의 폭이다 (전환의 목표).
            [[nodiscard]] float target_sidebar_width() const
            {
                return sidebar_collapsed_ ? sidebar_collapsed_width : sidebar_width_;
            }

            [[nodiscard]] luil::sidebar_config make_sidebar_config() const
            {
                luil::sidebar_config config {};
                config.owner = u8"navigation";
                config.expanded_width = sidebar_width_;
                config.collapsed_width = sidebar_collapsed_width;
                config.collapsed = sidebar_collapsed_;
                // 지금 폭은 전환이 답한다. 끝나 있으면 목표와 같은 값이다.
                config.width = luil::transition_value(sidebar_motion_, std::chrono::steady_clock::now());
                config.toggle_tooltip = sidebar_collapsed_ ? u8"펼친다" : u8"접는다";
                config.toggle = luil::make_message_action(sidebar_toggle_intent {});
                config.resize = [](const float delta) { return luil::make_app_action(sidebar_resize_intent { delta }); };
                return config;
            }

            // 사이드바 내용: 페이지를 오가는 배타 선택이다.
            [[nodiscard]] std::unique_ptr<luil::ui_element> make_navigation() const
            {
                luil::choice_group_config navigation {};
                navigation.owner = u8"navigation";
                navigation.style = luil::choice_style::radio;
                const auto add_page = [&navigation](const std::u8string_view key, std::u8string label) { navigation.items.push_back({ std::u8string { key }, std::move(label) }); };
                add_page(page_basics, u8"기본");
                add_page(page_lists, u8"목록");
                add_page(page_tabs, u8"탭");
                add_page(page_groups, u8"그룹");
                add_page(page_toasts, u8"토스트");
                add_page(page_windows, u8"창");
                add_page(page_images, u8"이미지");
                add_page(page_theme, u8"테마");
                navigation.selected = page_;
                navigation.select = [](const std::u8string& value) { return luil::make_app_action(navigate_intent { value }); };
                const float navigation_height { luil::choice_group_element::height_for(navigation) };

                luil::stack_config config {};
                config.padding = luil::edge_insets::symmetric(8.0f, 4.0f);
                auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"navigation" }, config) };
                column->add(std::make_unique<luil::choice_group_element>(std::move(navigation)), navigation_height);
                return column;
            }

            [[nodiscard]] std::unique_ptr<luil::ui_element> build_page(const float width, const float height, const float scale)
            {
                if (page_ == page_lists)
                    return lists_.build(width, height, scale);
                if (page_ == page_tabs)
                    return tabs_.build(width, height, scale);
                if (page_ == page_groups)
                    return groups_.build(width, height, scale);
                if (page_ == page_toasts)
                    return toasts_.build(width, height, scale);
                if (page_ == page_windows)
                    return windows_.build(width, height, scale);
                if (page_ == page_images)
                    return images_.build(width, height, scale);
                if (page_ == page_theme)
                    return theme_.build(width, height, scale);
                return basics_.build(width, height, scale);
            }

            basics_page basics_ {};
            lists_page lists_ {};
            tabs_page tabs_ {};
            groups_page groups_ {};
            toasts_page toasts_ {};
            windows_page windows_ {};
            images_page images_ {};
            theme_page theme_ {};

            // 복원할 배치와 마지막으로 보고된 배치다.
            // 하나는 시작에 쓰고 하나는 종료 저장에 쓴다.
            std::optional<luil::win32::window_placement> restore_ {};
            luil::win32::window_placement placement_ {};

            std::u8string page_ { page_basics };
            bool sidebar_collapsed_ { false };
            float sidebar_width_ { sidebar_default_width };
            // 접힘 전환이다 (앱 상태).
            // 라이브러리는 이것을 들고 있지 않고, 지금 폭만 물어 사이드바에 넘긴다.
            luil::transition sidebar_motion_ { sidebar_default_width, sidebar_default_width, {}, std::chrono::milliseconds { 0 } };
            window_metrics_intent metrics_ {};
            std::atomic<bool> closed_ { false };
        };

        // 상태 기계가 모르는 앱 정책이다.
        // 어느 element가 텍스트 칸인지, 휠이 무엇을 스크롤하는지,
        // 메뉴 kind 짝이 무엇인지, dialog가 열린 동안 Esc·Enter가 무엇을 뜻하는지.
        class demo_policy final : public luil::interaction_policy
        {
        public:
            [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
            {
                if (kind == kind_number_input)
                    return target_number;
                if (kind == kind_note_input)
                    return target_note;
                if (kind == kind_tool_note_input)
                    return target_tool_note;
                // popup 안의 검색 칸도 여느 텍스트 칸과 같이 등록한다.
                // popup이라고 다른 경로가 아니다 (popup-ime-design.md).
                if (kind == kind_dropdown_search_input)
                    return target_dropdown_search;
                if (kind == kind_tool_menu_search_input)
                    return target_tool_menu_search;
                return std::nullopt;
            }

            [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override
            {
                return luil::make_app_action(edit_intent { request });
            }

            [[nodiscard]] luil::input_action make_text_composition_action(const luil::text_composition_event& event) const override
            {
                return luil::make_app_action(composition_intent { event });
            }

            // popup의 메뉴가 이 kind 짝을 쓴다.
            // 열려 있으면 ↑/↓/Enter/Esc가 메뉴 탐색이 된다.
            [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
            {
                return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
            }

            [[nodiscard]] std::vector<luil::input_action> close_menu() const override
            {
                return { luil::make_app_action(popup_close_intent {}) };
            }

            [[nodiscard]] std::vector<luil::input_action> on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float scroll_delta) override
            {
                // 페이지마다 차례로 물어본다.
                // 지금 tree에 없는 페이지의 대상은 find가 걸러 준다.
                if (auto actions { lists_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
                    return actions;
                return tabs_page::route_wheel(tree, event, scroll_delta);
            }

            [[nodiscard]] std::vector<luil::input_action> on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused) override
            {
                // 휠과 같은 모양으로 페이지마다 차례로 물어본다.
                // 지금 tree에 없는 페이지의 창은 `route_reveal`이 걸러 준다.
                if (auto actions { lists_page::route_reveal(tree, focused) }; actions.empty() == false)
                    return actions;
                return tabs_page::route_reveal(tree, focused);
            }

            // 이 셸에는 앱이 가로챌 키가 없다.
            // Esc는 modal host의 dismiss 액션이 처리하고 Enter는 default button이 처리한다.
            // 나머지 키는 앱 단축키를 위해 policy hook으로 전달할 수 있다.
        };


        // 그림 하나를 고르는 파일 dialog다.
        //
        // **UI thread 전용이라 앱 메시지로는 열 수 없다.** modal dialog는 창을
        // 가진 thread에서만 뜨므로, 이미지 페이지의 클릭은 `app_ui_command`를
        // 내고(라이브러리는 그 번호를 해석하지 않는다) 여기서 열린다. 고른 경로는
        // 여느 앱 메시지처럼 logic으로 가서 그쪽이 디코딩한다.
        //  - 고르지 않고 닫으면 빈 값이다. 취소는 오류가 아니라 아무 일도 없는 것이다.
        [[nodiscard]] std::optional<std::u8string> pick_image_file()
        {
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog {};
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
                return std::nullopt;

            // 필터는 화면의 안내 줄·드롭 수락과 **같은 목록**에서 만든다
            // (images_page.h). 확장자는 전부 ASCII라 글자를 그대로 넓힌다.
            std::wstring pattern {};
            for (const std::u8string_view extension : image_extensions)
            {
                if (pattern.empty() == false)
                    pattern += L';';
                pattern += L"*.";
                for (const char8_t character : extension)
                    pattern += static_cast<wchar_t>(character);
            }
            const COMDLG_FILTERSPEC filter { L"이미지 파일", pattern.c_str() };
            if (FAILED(dialog->SetFileTypes(1, &filter)))
                return std::nullopt;

            // 소유자를 주지 않으면 dialog가 창 뒤로 숨을 수 있다.
            // 이 함수는 UI thread에서만 불리므로 이 thread의 활성 창이 곧 우리
            // 창이다 — 공개 API는 HWND를 드러내지 않는다.
            if (dialog->Show(GetActiveWindow()) != S_OK)
                return std::nullopt;

            Microsoft::WRL::ComPtr<IShellItem> item {};
            if (FAILED(dialog->GetResult(&item)))
                return std::nullopt;
            PWSTR selected { nullptr };
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected)))
                return std::nullopt;
            const std::u8string path { std::filesystem::path { selected }.u8string() };
            CoTaskMemFree(selected);
            return path;
        }

        class demo_delegate final : public luil::win32::window_delegate
        {
        public:
            // 파일 dialog는 UI thread의 일이고, 고른 경로는 여느 앱 메시지다 —
            // 드롭이 내는 것과 **같은 메시지**라 logic 쪽에 갈래가 늘지 않는다.
            void execute_app_ui_command(luil::win32::app_host& host, const luil::app_ui_command& command) override
            {
                if (command.command != app_command_open_image)
                    return;
                if (const std::optional<std::u8string> path { pick_image_file() }; path.has_value())
                    host.post_app_message(luil::app_message { image_open_intent { *path } });
            }

            [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
            {
                return luil::app_message { window_metrics_intent { width, height, scale } };
            }

            [[nodiscard]] luil::app_message make_window_placement_message(const luil::win32::window_placement& placement) override
            {
                return luil::app_message { placement_intent { placement } };
            }

            // 받는 칸이 아닌 곳에 놓인 파일의 물러섬이다.
            // 데모는 토스트로 알린다 — 참을 돌려주므로 여러 개를 놓아도 하나다.
            [[nodiscard]] bool on_file_dropped(luil::win32::app_host& host, const std::u8string& path) override
            {
                host.post_app_message(luil::app_message { toast_request_intent { u8"받은 파일: " + path, luil::toast_severity::info } });
                return true;
            }
        };
    } // namespace
} // namespace demo

namespace {
    [[nodiscard]] bool parse_arguments(const std::vector<std::u8string>& arguments, luil::win32::window_config& config)
    {
        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            const std::u8string_view argument { arguments[index] };
            if (argument == u8"--smoke-test")
                config.smoke_test = true;
            else if (argument == u8"--simulate-direct3d-failure")
                config.simulate_direct3d_failure = true;
            else if (argument.starts_with(u8"--simulate-direct3d-loss-after-frames="))
            {
                const std::u8string_view value { argument.substr(38) };
                int frames { 0 };
                const auto* const first { reinterpret_cast<const char*>(value.data()) };
                const auto [parsed, code] { std::from_chars(first, first + value.size(), frames) };
                if (code != std::errc {} || parsed != first + value.size() || frames < 0)
                    return false;
                config.simulate_direct3d_loss_after_frames = frames;
            }
            else if (argument.starts_with(u8"--renderer="))
            {
                const auto mode { luil::parse_renderer_mode(argument.substr(11)) };
                if (mode.has_value() == false)
                    return false;
                config.renderer = *mode;
            }
            else if (argument.starts_with(u8"--position="))
            {
                // 자리를 정하지 않으면 OS가 정한다 — 이 인자가 그 기본을 대신한다.
                const auto position { luil::win32::parse_window_position(argument.substr(11)) };
                if (position.has_value() == false)
                    return false;
                config.initial_position = position;
            }
            else
                return false;
        }
        return true;
    }
} // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    luil::win32::enable_per_monitor_dpi_awareness();
    const luil::win32::com_sta_scope com {};

    const auto arguments { luil::win32::command_line_arguments() };
    if (arguments.has_value() == false)
        return 1;

    luil::win32::window_config config {};
    config.class_name = L"Luil.Demo.Window";
    config.caption = demo::make_caption_config();
    // 파일을 받는다 — 노트 칸(기본 페이지·도구 창)이 대상이고 나머지는 토스트다.
    config.accept_file_drop = true;
    // 앱이 정의한 모양만 앱이 해석하고 나머지는 라이브러리 기본 매핑에 맡긴다.
    //  - 빈 핸들을 돌려주면 그 값은 기본 매핑으로 물러선다.
    config.resolve_cursor = [](const luil::ui_cursor cursor) -> luil::win32::cursor_handle {
        if (cursor == demo::cursor_reorder)
            return luil::win32::load_system_cursor(luil::win32::system_cursor::resize_vertical);
        return {};
    };
    if (parse_arguments(*arguments, config) == false)
        return 2;

    // 지난 실행이 갈무리한 배치를 되돌린다.
    // --position이 있으면 그쪽이 이긴다 — 명시한 자리를 저장된 자리가 덮지 않는다.
    // 자리는 창을 만들 때 잡고(물리 픽셀 그대로), 크기·최대화는 첫 frame의
    // 요청이 잇는다 — 크기는 논리 픽셀이라 만들기 전에는 배율을 모른다.
    std::optional<luil::win32::window_placement> restore {};
    if (config.smoke_test == false && config.initial_position.has_value() == false)
    {
        restore = demo::load_placement();
        if (restore.has_value())
            config.initial_position = luil::win32::window_position { restore->x, restore->y };
    }

    demo::demo_driver driver { restore };
    demo::demo_policy policy {};
    demo::demo_delegate delegate {};

    luil::win32::window_environment environment {};
    if (config.smoke_test == false)
    {
        environment.driver = &driver;
        environment.policy = &policy;
        environment.delegate = &delegate;
    }
    return luil::win32::run_application_window(config, environment);
}

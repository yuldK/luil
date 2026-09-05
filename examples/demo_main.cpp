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
//  - 메뉴·팝업: 드롭다운과 컨텍스트 메뉴 (popup 창)         (demo/popups_page)
//  - 창: 보조 top-level 창(도구 창) 여닫기와 그 안의 입력   (demo/windows_page)
//  - 이미지: 파일을 끌어다 놓거나 골라 그림 미리 보기       (demo/images_page)
//  - 네트워크: 되돌이 서버에 던진 요청과 갈래별 답·심장 박동 (demo/network_page)
//  - 웹뷰: 되돌이 서버의 문서를 창 안에 띄운다             (demo/webview_page)
//  - 테마: 테마 선호와 키 컬러                             (demo/theme_page)
// 이 파일은 셸이다: 사이드바 내비게이션, 페이지 전환, driver·policy·delegate 조립.
//
// 인자: --renderer=auto|cpu|direct3d, --smoke-test, --simulate-direct3d-failure,
//       --simulate-direct3d-loss-after-frames=N (N frame을 그린 뒤 Direct3D를 잃는다 —
//                       생성 시점 실패와 달리 **제시에 붙은 창을 놓고** CPU로 물러선다)
//       --page=basics|lists|tabs|groups|toasts|popups|windows|images|network|webview|theme
//                      (열고 시작할 페이지 — 눈으로 확인할 때 사이드바를 누르지 않아도 된다)
//       --position=x,y (창이 뜰 화면 자리 — 물리 픽셀, 가상 화면 좌표라 음수도 된다.
//                       지난 실행이 저장한 배치보다 세다)

#include "demo/basics_page.h"
#include "demo/common.h"
#include "demo/groups_page.h"
#include "demo/images_page.h"
#include "demo/lists_page.h"
#include "demo/network_page.h"
#include "demo/popups_page.h"
#include "demo/tabs_page.h"
#include "demo/theme_page.h"
#include "demo/toasts_page.h"
#include "demo/webview_page.h"
#include "demo/windows_page.h"

#include "luil/net/http_client.h"
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
            // `page`가 비어 있지 않으면 그 페이지로 시작한다 (--page=).
            demo_driver(const std::optional<luil::win32::window_placement> restore, std::u8string page)
                : restore_ { restore }
                , page_ { page.empty() ? std::u8string { page_basics } : std::move(page) }
            {}

            // 창과 host가 준비된 직후 UI thread가 알려 주는 자리다.
            // 배출구(`deliver`)가 답을 logic thread로 나르려면 이 포인터가 필요하다.
            //  - `on_started`는 메시지 루프 **전**에 불리고 첫 요청은 사람이 눌러야
            //    나가므로, 붙기 전에 답이 올 창은 없다.
            void bind_host(luil::win32::app_host& host) noexcept
            {
                host_.store(&host);
            }

            // logic thread가 시작한 직후 한 번이다.
            //
            // **서버도 client도 여기서 선다.** driver는 smoke 모드에서도 만들어지지만
            // (wWinMain) host를 조립하지 않는 그 모드에서는 `start()`가 불리지 않는다 —
            // 한 frame만 그리는 길에 소켓도 thread도 서지 않는 것이 그래서다.
            void start() override
            {
                server_ = std::make_unique<luil::testing::loopback_http_server>();
                server_->set_handler(make_network_handler());
                if (server_->port() != 0)
                {
                    network_.set_base_url(server_->url(""));
                    webview_.set_base_url(server_->url(""));
                }
                webview_.load_assets();
                live_server_.store(server_.get());

                luil::net::http_client_config config {};
                config.user_agent = u8"luil-demo";
                // 되돌이 주소를 두드리므로 시스템 proxy를 타지 않는다 — proxy가
                // 127.0.0.1을 가로채면 예제의 성패를 남의 설정이 정한다.
                config.use_system_proxy = false;
                // **배출구는 client가 소유한 thread에서 불린다.** 여기서 하는 일은
                // 값을 logic thread로 나르는 것 하나다 (http_client.h의 계약).
                config.deliver = [this](luil::net::http_response response) {
                    if (luil::win32::app_host* const host { host_.load() }; host != nullptr)
                        host->post_app_message(luil::app_message { network_response_intent { std::move(response) } });
                };
                client_ = luil::net::http_client::create(std::move(config), client_error_);
                live_client_.store(client_.get());
            }

            void handle(luil::app_message message) override
            {
                // 네트워크 메시지는 셸이 먼저 받는다 — client를 든 것이 셸이고
                // 페이지는 그 답만 본다 (컨텍스트 메뉴 선택과 같은 자리다).
                if (const auto* const send { message.get<network_send_intent>() }; send != nullptr)
                {
                    send_network_request(*send);
                    return;
                }
                if (message.get<network_cancel_intent>() != nullptr)
                {
                    cancel_network_request();
                    return;
                }
                if (const auto* const beat { message.get<network_heartbeat_intent>() }; beat != nullptr)
                {
                    toggle_network_heartbeat(beat->on);
                    return;
                }
                if (const auto* const answer { message.get<network_response_intent>() }; answer != nullptr)
                {
                    // **답이 온 흐름은 표를 잊는다.** 잊지 않으면 취소가 이미 끝난
                    // 표를 겨누고, `cancel`은 모르는 표에 아무 일도 하지 않으므로
                    // (http_client.h) 단추가 눌리는데 아무 일도 없는 자리가 생긴다.
                    if (answer->response.ticket == network_pending_)
                        network_pending_ = {};
                    if (answer->response.last && answer->response.ticket == network_heartbeat_)
                        network_heartbeat_ = {};
                    static_cast<void>(network_.handle(message));
                    return;
                }

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
                    popups_.close_popups();
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
                    popups_.close_popups();
                    windows_.close_popups();
                    return;
                }
                // 컨텍스트 메뉴 선택은 페이지가 메뉴를 닫고 셸이 토스트로 잇는다.
                if (const auto* const select { message.get<card_menu_select_intent>() }; select != nullptr)
                {
                    static_cast<void>(popups_.handle(message));
                    static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { u8"컨텍스트 메뉴: " + select->key, luil::toast_severity::success } }));
                    return;
                }

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
                if (basics_.handle(message) || lists_.handle(message) || tabs_.handle(message) || groups_.handle(message) || toasts_.handle(message) || popups_.handle(message)
                    || windows_.handle(message) || images_.handle(message) || network_.handle(message) || webview_.handle(message) || theme_.handle(message))
                    return;
            }

            // 종료 0단계다. 여기서 접어 두면 3단계의 `stop()` 기다림이 수 ms로 줄어든다
            // (http_client.h의 `stop` 주석과 짝이다).
            //  - **UI thread에서 불린다.** 그래서 손잡이를 원자적으로 든다 —
            //    소유는 logic thread의 `client_`이고 이쪽은 볼 뿐이다.
            void cancel() noexcept override
            {
                if (luil::net::http_client* const client { live_client_.load() }; client != nullptr)
                    client->cancel_all();
            }

            // 종료 3단계다 (threading-model.md). 이 줄 뒤로는 배출구가 다시
            // 불리지 않으므로, logic thread가 빠져나가는 동안 답이 들어올 자리가 없다.
            void stop_workers() override
            {
                if (luil::net::http_client* const client { live_client_.load() }; client != nullptr)
                    client->stop();
                if (luil::testing::loopback_http_server* const server { live_server_.load() }; server != nullptr)
                    server->stop();
            }

            [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
            {
                // 네트워크 페이지는 client를 모르므로 "지금 몇 개가 날아가 있는가"도
                // 셸이 물어 넣는다 (취소 단추가 잠기는 자리다).
                network_.set_in_flight(client_ != nullptr ? client_->in_flight() : 0);

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
                for (luil::win32::ui_popup& popup : popups_.make_popups(scale))
                    frame->popups.push_back(std::move(popup));
                // 도구 창은 페이지의 일부가 아니라 어느 페이지에서든 열려 있다.
                for (luil::win32::ui_window& window : windows_.make_windows())
                    frame->windows.push_back(std::move(window));
                // 웹뷰도 어느 페이지에서든 실려 있다. 페이지를 떠났다고 목록에서
                // 빼면 웹뷰가 사라지고 읽던 문서도 함께 사라진다 — 자리표가 없는
                // frame에서는 그 자리가 없어 저절로 감춰질 뿐이다.
                if (std::optional<luil::win32::ui_webview> view { webview_.webview() }; view.has_value())
                    frame->webviews.push_back(std::move(*view));
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
            // 주소 하나를 보낸다. 표를 페이지에게 되돌려 주는 것까지가 이 함수다 —
            // 페이지는 그 표로 자기 흐름을 알아본다 (http-client-design.md).
            void send_network_request(const network_send_intent& request)
            {
                if (client_ == nullptr)
                {
                    network_.note_sent(request, {}, client_error_.empty() ? std::u8string { u8"The HTTP client is not available." } : client_error_);
                    return;
                }

                luil::net::http_request outgoing {};
                outgoing.url = request.url;
                outgoing.method = request.post ? luil::net::http_method::post : luil::net::http_method::get;
                if (request.post)
                {
                    outgoing.body = luil::net::http_text_body(u8"데모가 보낸 몸이다.");
                    outgoing.content_type = u8"text/plain; charset=utf-8";
                }
                // **밖에서 오는 그림에는 상한을 건다** (http_body.h의 그 문단).
                // 미리 보기 칸이 커질 수 있는 만큼만 잡으면 장 수가 곱해져도 안전하다.
                outgoing.parse.image.decode = { .max_width = 1600, .max_height = 1600 };

                std::u8string error {};
                const luil::net::http_ticket ticket { client_->send(std::move(outgoing), error) };
                network_pending_ = ticket;
                network_.note_sent(request, ticket, error);
            }

            void cancel_network_request()
            {
                if (client_ == nullptr)
                    return;
                // 취소는 **본문 흐름 하나**를 겨눈다. `cancel_all()`은 되풀이표까지
                // 비우므로 심장 박동이 도는 중에 느린 요청 하나를 접는 자리에서는
                // 지나치다 — 겨눌 것이 없으면 아무 일도 하지 않는다 (단추도 잠겨 있다).
                if (network_pending_)
                    client_->cancel(network_pending_);
            }

            void toggle_network_heartbeat(const bool on)
            {
                if (on == false)
                {
                    // 멈추는 것도 한 번짜리와 같은 `cancel` 하나다 (3.12).
                    if (client_ != nullptr && network_heartbeat_)
                        client_->cancel(network_heartbeat_);
                    network_heartbeat_ = {};
                    network_.note_heartbeat_stopped();
                    return;
                }

                const std::u8string url { network_.heartbeat_url() };
                if (client_ == nullptr || url.empty())
                {
                    network_.note_heartbeat({}, client_ == nullptr ? client_error_ : std::u8string { u8"The loopback server is not listening." });
                    return;
                }

                luil::net::http_request request {};
                request.url = url;
                std::u8string error {};
                const luil::net::http_heartbeat schedule { .interval = std::chrono::milliseconds { 1000 }, .immediate = true, .max_rounds = 0 };
                const luil::net::http_ticket ticket { client_->start_heartbeat(std::move(request), schedule, error) };
                network_heartbeat_ = ticket;
                network_.note_heartbeat(ticket, error);
            }

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
                add_page(page_popups, u8"메뉴·팝업");
                add_page(page_windows, u8"창");
                add_page(page_images, u8"이미지");
                add_page(page_network, u8"네트워크");
                add_page(page_webview, u8"웹뷰");
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
                if (page_ == page_popups)
                    return popups_.build(width, height, scale);
                if (page_ == page_windows)
                    return windows_.build(width, height, scale);
                if (page_ == page_images)
                    return images_.build(width, height, scale);
                if (page_ == page_network)
                    return network_.build(width, height, scale);
                if (page_ == page_webview)
                    return webview_.build(width, height, scale);
                if (page_ == page_theme)
                    return theme_.build(width, height, scale);
                return basics_.build(width, height, scale);
            }

            basics_page basics_ {};
            lists_page lists_ {};
            tabs_page tabs_ {};
            groups_page groups_ {};
            toasts_page toasts_ {};
            popups_page popups_ {};
            windows_page windows_ {};
            images_page images_ {};
            network_page network_ {};
            theme_page theme_ {};
            webview_page webview_ {};

            // --- 네트워크 페이지가 기대는 조각들 ---
            // 되돌이 서버와 client는 **셸이 든다.** 페이지는 메시지만 주고받는다.
            //  - 둘 다 `start()`(logic thread)에서 서고 소멸자(UI thread, join 뒤)에서
            //    사라진다. 그 사이 UI thread가 만지는 자리가 둘 있어(`cancel`·
            //    `stop_workers`) 그쪽만 원자적 손잡이로 본다 — 소유는 여전히 아래 둘이다.
            std::unique_ptr<luil::testing::loopback_http_server> server_ {};
            std::unique_ptr<luil::net::http_client> client_ {};
            std::atomic<luil::testing::loopback_http_server*> live_server_ { nullptr };
            std::atomic<luil::net::http_client*> live_client_ { nullptr };
            // client가 서지 못한 이유다 (`create`의 규약 — 성공하면 건드리지 않는다).
            std::u8string client_error_ {};
            // 배출구가 답을 나를 자리다. `demo_delegate::on_started`가 채운다.
            std::atomic<luil::win32::app_host*> host_ { nullptr };
            // 셸이 아는 표 둘이다 (취소가 겨눌 자리 — 페이지의 것과 같은 값이다).
            luil::net::http_ticket network_pending_ {};
            luil::net::http_ticket network_heartbeat_ {};

            // 복원할 배치와 마지막으로 보고된 배치다.
            // 하나는 시작에 쓰고 하나는 종료 저장에 쓴다.
            std::optional<luil::win32::window_placement> restore_ {};
            luil::win32::window_placement placement_ {};

            std::u8string page_ {};
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
                if (kind == kind_network_url_input)
                    return target_network_url;
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
                if (auto actions { tabs_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
                    return actions;
                return network_page::route_wheel(tree, event, scroll_delta);
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
            // driver를 아는 유일한 이유는 배출구가 쓸 host를 건네주기 위해서다.
            // 둘 다 wWinMain의 지역이라 수명이 같다.
            explicit demo_delegate(demo_driver& driver) noexcept
                : driver_ { driver }
            {}

            // 창과 host가 준비된 직후다 (메시지 루프 전).
            // HTTP client의 배출구가 답을 넣을 곳이 이 host다.
            void on_started(luil::win32::app_host& host) override
            {
                driver_.bind_host(host);
            }

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

        private:
            demo_driver& driver_;
        };
    } // namespace
} // namespace demo

namespace {
    [[nodiscard]] bool parse_arguments(const std::vector<std::u8string>& arguments, luil::win32::window_config& config, std::u8string& start_page)
    {
        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            const std::u8string_view argument { arguments[index] };
            if (argument == u8"--smoke-test")
                config.smoke_test = true;
            else if (argument.starts_with(u8"--page="))
                start_page = argument.substr(7);
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
    std::u8string start_page {};
    if (parse_arguments(*arguments, config, start_page) == false)
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

    demo::demo_driver driver { restore, start_page };
    demo::demo_policy policy {};
    demo::demo_delegate delegate { driver };

    luil::win32::window_environment environment {};
    if (config.smoke_test == false)
    {
        environment.driver = &driver;
        environment.policy = &policy;
        environment.delegate = &delegate;
    }
    return luil::win32::run_application_window(config, environment);
}

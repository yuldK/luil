// luil 사용 예제이자 smoke test 실행 파일이다.
// 라이브러리 소비자가 구현해야 하는 세 조각(logic_driver, interaction_policy,
// window_delegate)의 실제 형태를 보여 준다.
//
// 화면은 접이식 사이드바로 오가는 페이지들이고, 각 페이지가 element 하나씩을 다룬다.
//  - 기본: 버튼·카운터, 텍스트 입력, 확인 dialog          (demo/basics_page)
//  - 목록: drag 순서 바꾸기, tree, 만 줄짜리 가상 목록, sticky 머리행 (demo/lists_page)
//  - 파일: 여럿 고르기, 고른 것 끌어 폴더에 놓기, 끌어서 범위 고르기, 파일 클립보드 (demo/files_page)
//  - 탭: 선택·닫기·순서·가로 스크롤·넘침 메뉴 popup        (demo/tabs_page)
//  - 그룹: 접이식 섹션, 라디오·토글 묶음                   (demo/groups_page)
//  - 토스트: 심각도별 알림, 실행 취소 토스트, 정보 막대      (demo/toasts_page)
//  - 메뉴·팝업: 드롭다운·컨텍스트 메뉴·분할 버튼 (popup 창) (demo/popups_page)
//  - 창: 보조 top-level 창(도구 창) 여닫기, 전체 화면 전환, 작업 표시줄 진행률·오버레이, 둘째 인스턴스가 넘긴 인자 (demo/windows_page)
//  - 이미지: 파일을 끌어다 놓거나 골라 그림 미리 보기       (demo/images_page)
//  - 네트워크: 되돌이 서버에 던진 요청과 갈래별 답·심장 박동 (demo/network_page)
//  - 웹뷰: 되돌이 서버의 문서를 창 안에 띄운다             (demo/webview_page)
//  - 테마: 테마 선호와 키 컬러                             (demo/theme_page)
//  - 변경 작업대: 지연 로딩 계층·하위 메뉴가 있는 표·읽기 전용 차이 보기·편집형 콤보·패치 불러오기, 커스텀 element 없이 (demo/workbench_page)
// 이 파일은 셸이다: 사이드바 내비게이션, 페이지 전환, driver·policy·delegate 조립.
//
// 인자: --renderer=auto|cpu|direct3d, --smoke-test, --simulate-direct3d-failure,
//       --simulate-direct3d-loss-after-frames=N (N frame을 그린 뒤 Direct3D를 잃는다 —
//                       생성 시점 실패와 달리 **제시에 붙은 창을 놓고** CPU로 물러선다)
//       --page=basics|lists|files|tabs|groups|toasts|popups|windows|images|network|webview|zoom|ink|inspector|theme|workbench|rich-text
//                      (열고 시작할 페이지 — 눈으로 확인할 때 사이드바를 누르지 않아도 된다)
//       --position=x,y (창이 뜰 화면 자리 — 물리 픽셀, 가상 화면 좌표라 음수도 된다.
//                       지난 실행이 저장한 배치보다 세다)
//       --presentation=standard|layered (주 창의 표시 방식 — layered는 CPU 렌더러로 배치와 frame을 한 합성에 낸다)
//       --caption=top|bottom (주 창 캡션의 처음 자리 — 창 페이지의 전환과 같다)
//       --backdrop=none|mica|mica_alt|acrylic (주 창 배경 효과의 처음 값 — 테마 페이지의 고르기와 같다)
//       --page-transition=none|slide|fade|zoom|spring (페이지를 바꿀 때의 전환 — 보기 메뉴의 "페이지 전환"과 같다. 기본은 slide)
//       --page-transition-ms=N (전환의 길이 — 스프링이면 response다. UIA가 도는 중을 읽도록 길게 잡는다)
//       --send-to-first (둘째 실행이 창을 띄우지 않고, 이 플래그를 뺀 나머지 인자와 작업 폴더를 먼저 뜬 데모에 보내고 끝낸다.
//                       종료 코드는 받았으면 0, 먼저 뜬 데모가 없으면 3, 그 밖의 실패는 4다. 받은 인자는 창 페이지의 "둘째 인스턴스 인자" 줄에 보인다)
//       --unregister-app-identity (창을 띄우지 않고 데모의 앱 식별 `luil.Demo`의 HKCU 항목을 지우고 끝낸다. 지웠으면 0, 없으면 3, 실패는 4다)
//  - 데모는 시작할 때마다 앱 식별 `luil.Demo`를 HKCU에 쓴다 (토스트 페이지의 OS 알림 구역, os-notification-design.md).
//    꺼진 데모의 알림을 누르면 COM이 `-luil-notification`(과 `-Embedding`)을 붙여 데모를 띄운다. 그 표식은 인자 읽기에서 거른다.

#include "demo/basics_page.h"
#include "demo/common.h"
#include "demo/files_page.h"
#include "demo/gallery_page.h"
#include "demo/groups_page.h"
#include "demo/ink_page.h"
#include "demo/images_page.h"
#include "demo/inspector_page.h"
#include "demo/lists_page.h"
#include "demo/network_page.h"
#include "demo/palette.h"
#include "demo/popups_page.h"
#include "demo/readonly_undo_keys.h"
#include "demo/shell_menu.h"
#include "demo/tabs_page.h"
#include "demo/text_targets.h"
#include "demo/theme_page.h"
#include "demo/toasts_page.h"
#include "demo/webview_page.h"
#include "demo/windows_page.h"
#include "demo/workbench_page.h"
#include "demo/zoom_page.h"
#include "demo/rich_text_page.h"

#include "luil/app/notification.h"
#include "luil/app/platform_services.h"
#include "luil/net/http_client.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/command_palette.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/list_element.h"
#include "luil/ui/page_transition_element.h"
#include "luil/ui/sidebar_element.h"
#include "luil/ui/spring.h"
#include "luil/ui/stack_element.h"
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
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// 데모 빌드가 실은 스타일 파일 글이다 (LUIL_STYLE, cmake/embed_binary.cmake). 테마 페이지의 스타일 파일 묶음이 처음 싣는다.
extern const std::uint8_t luil_demo_style_json[];
extern const std::size_t luil_demo_style_json_size;

namespace demo {
    namespace {
        // 사이드바의 처음 폭과 접힌 폭이다 (논리 픽셀).
        constexpr float sidebar_default_width { 200.0f };
        constexpr float sidebar_collapsed_width { 44.0f };

        // 사이드바 목록의 페이지와 그 순서다. 페이지 전환의 쪽도 이 순서로 정한다 (목록에서 아래 페이지로 가면 forward).
        struct navigation_page
        {
            std::u8string_view key {};
            std::u8string_view label {};
        };

        constexpr navigation_page navigation_pages[] {
            { page_overview, u8"시작 / 기능 색인" },
            { page_controls, u8"버튼과 컨트롤" },
            { page_basics, u8"입력과 다이얼로그" },
            { page_lists, u8"목록" },
            { page_files, u8"파일" },
            { page_tabs, u8"탭" },
            { page_groups, u8"그룹" },
            { page_toasts, u8"토스트" },
            { page_popups, u8"메뉴·팝업" },
            { page_windows, u8"창" },
            { page_images, u8"이미지" },
            { page_network, u8"네트워크" },
            { page_webview, u8"웹뷰" },
            { page_zoom, u8"확대 보기" },
            { page_ink, u8"잉크" },
            { page_inspector, u8"검사기" },
            { page_theme, u8"모양과 색" },
            { page_workbench, u8"변경 작업대" },
            { page_rich_text, u8"서식 있는 글" },
        };

        [[nodiscard]] std::size_t navigation_index(const std::u8string_view key) noexcept
        {
            for (std::size_t index = 0; index < std::size(navigation_pages); ++index)
                if (navigation_pages[index].key == key)
                    return index;
            return 0;
        }

        // 페이지 전환 label에 쓰는 이름이다. 보기 메뉴의 라디오와 같다. 꺼져 있으면 빈 글이다.
        [[nodiscard]] std::u8string_view page_transition_name(const std::u8string_view mode) noexcept
        {
            if (mode == shell_menu_page_transition_slide)
                return u8"밀기";
            if (mode == shell_menu_page_transition_fade)
                return u8"옅어짐";
            if (mode == shell_menu_page_transition_zoom)
                return u8"확대";
            if (mode == shell_menu_page_transition_spring)
                return u8"스프링 밀기";
            return {};
        }

        // 데모의 앱 식별이다 (`window_config::identity`). `--unregister-app-identity`가 같은 id를 지운다.
        constexpr std::u8string_view demo_app_identity { u8"luil.Demo" };

        // 실행 파일이 있는 폴더다. 읽지 못하면 빈 경로다.
        [[nodiscard]] std::filesystem::path executable_directory()
        {
            wchar_t buffer[MAX_PATH] {};
            const DWORD length { GetModuleFileNameW(nullptr, buffer, MAX_PATH) };
            if (length == 0 || length >= MAX_PATH)
                return {};
            return std::filesystem::path { std::wstring { buffer, length } }.parent_path();
        }

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

        // "x,y,width,height,max,full"을 배치로 읽는다 (십진 정수 여섯, max·full은 0·1).
        // 형식이 다르면 빈 값이다 — 앞부분만 조용히 읽지 않는다
        // (parse_window_position과 같은 규칙이다).
        //  - **다섯 개짜리도 받는다.** 전체 화면 칸이 생기기 전에 저장된 파일이 그것이고,
        //    그때의 창은 전체 화면이 아니었으므로 뜻이 분명하다. 지난 설정 하나 때문에
        //    사용자가 창 자리를 잃는 것이 형식을 엄히 지키는 것보다 나쁘다.
        //  - 전체 화면이면 앞의 넷은 **돌아갈 자리**다 (app_host.h의 `window_placement`).
        //    한 줄에 둘이 함께 실려 있어 따로 저장할 것이 없다.
        [[nodiscard]] std::optional<luil::win32::window_placement> parse_placement(const std::u8string_view text) noexcept
        {
            std::array<int, 6> values {};
            const char* cursor { reinterpret_cast<const char*>(text.data()) };
            const char* const end { cursor + text.size() };
            std::size_t count { 0 };
            for (; count < values.size(); ++count)
            {
                if (count > 0)
                {
                    // 값 뒤에서 글이 끝나면 거기까지가 이 파일의 전부다.
                    if (cursor == end)
                        break;
                    if (*cursor != ',')
                        return std::nullopt;
                    ++cursor;
                }
                const auto [next, error] { std::from_chars(cursor, end, values[count]) };
                if (error != std::errc {} || next == cursor)
                    return std::nullopt;
                cursor = next;
            }
            if (cursor != end || count < values.size() - 1)
                return std::nullopt;
            if ((values[4] != 0 && values[4] != 1) || (values[5] != 0 && values[5] != 1))
                return std::nullopt;
            const luil::win32::window_placement placement { values[0], values[1], values[2], values[3], values[4] != 0, values[5] != 0 };
            if (placement.valid() == false)
                return std::nullopt;
            return placement;
        }

        [[nodiscard]] std::string format_placement(const luil::win32::window_placement& placement)
        {
            return std::to_string(placement.x) + "," + std::to_string(placement.y) + "," + std::to_string(placement.width) + "," + std::to_string(placement.height) + ","
                + (placement.maximized ? "1" : "0") + "," + (placement.fullscreen ? "1" : "0");
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
            // 통째로 바꾼다 — 종료하다 끊겨도 지난 배치 파일이 반쪽으로 남지 않는다 (platform-services-design.md).
            const std::string text { format_placement(placement) };
            static_cast<void>(luil::replace_file_atomically(path.u8string(), { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() }));
        }

        // 같은 사용자 세션에서 먼저 뜬 데모가 있으면 제목에 그 사실을 적는다 (`single_instance`).
        [[nodiscard]] luil::caption_config make_caption_config(const bool primary_instance = true)
        {
            luil::caption_config caption {};
            caption.title = primary_instance ? u8"luil demo" : u8"luil demo — 둘째 인스턴스";
            caption.application_icon = luil::codicons::icon_rocket;
            caption.minimize_tooltip = u8"Minimize";
            caption.maximize_tooltip = u8"Maximize or restore";
            caption.close_tooltip = u8"Close";
            // 창 전체의 명령을 caption에 둔다. 그 자리는 창을 끄는 자리가 아니라 눌리는 단추다.
            caption.app_buttons.push_back(luil::caption_app_button {
                .key = u8"settings",
                .glyph = luil::codicons::icon_settings_gear,
                .tooltip = u8"테마 설정",
                .action = luil::make_message_action(navigate_intent { std::u8string { page_theme } }),
            });
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
            demo_driver(const std::optional<luil::win32::window_placement> restore, std::u8string page, const bool primary_instance)
                : restore_ { restore }
                , placement_request_ { restore }
                , placement_revision_ { restore.has_value() ? 1u : 0u }
                , page_ { page.empty() ? std::u8string { page_overview } : std::move(page) }
                , primary_instance_ { primary_instance }
            {
            }

            // 페이지 전환의 시작 상태다 (`--page-transition=`, `--page-transition-ms=`). `mode`는 보기 메뉴의 라디오 키다.
            void set_page_transition(std::u8string mode, const std::optional<std::chrono::milliseconds> length)
            {
                page_transition_mode_ = std::move(mode);
                page_transition_length_ = length;
            }

            // 창 페이지·테마 페이지의 시작 상태다 (`--caption=bottom`, `--presentation=`, `--backdrop=`).
            void set_window_options(const bool caption_bottom, const bool layered, const luil::window_backdrop backdrop) noexcept
            {
                windows_.set_caption_at_bottom(caption_bottom);
                windows_.set_layered_presentation(layered);
                theme_.set_backdrop(backdrop);
            }

            // 창과 host가 준비된 직후 UI thread가 알려 주는 자리다.
            // 배출구(`deliver`)가 답을 logic thread로 나르려면 이 포인터가 필요하다.
            //  - `on_started`는 메시지 루프 **전**에 불리고 첫 요청은 사람이 눌러야
            //    나가므로, 붙기 전에 답이 올 창은 없다.
            void bind_host(luil::win32::app_host& host) noexcept
            {
                host_.store(&host);
                files_.bind_host(host);
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

            // OS 알림 단추 하나를 OS에 옮기고 답과 지금 상태를 토스트 페이지에 넘긴다. logic thread에서 돈다.
            //  - 보내기와 바꾸기는 같은 id라 바꾸기가 알림 센터의 그 알림을 바꾼다. 보내기만 그림을 싣는다.
            void run_os_notification(const os_notification_action action)
            {
                luil::platform_result result {};
                if (action == os_notification_action::close)
                    result = luil::close_notification(os_notification_id);
                else
                {
                    luil::notification value {};
                    value.id = std::u8string { os_notification_id };
                    value.title = u8"luil 데모";
                    value.body = action == os_notification_action::send ? u8"알림 센터에 올린 알림입니다. 누르면 데모가 앞으로 옵니다." : u8"같은 id로 바꾼 알림입니다. 소리 없이 올렸습니다.";
                    value.silent = action == os_notification_action::replace;
                    if (action == os_notification_action::send)
                    {
                        luil::image_decode_error error {};
                        value.image = luil::load_image_file((executable_directory() / L"assets" / L"gradient.png").u8string(), error);
                    }
                    result = luil::show_notification(value);
                }
                static_cast<void>(toasts_.handle(luil::app_message { os_notification_result_intent { std::move(result) } }));
                static_cast<void>(toasts_.handle(luil::app_message { os_notification_status_intent { luil::notification_status() } }));
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
                // OS 알림은 셸이 부른다. 토스트 페이지는 답과 지금 상태만 받아 그린다.
                if (const auto* const request { message.get<os_notification_request_intent>() }; request != nullptr)
                {
                    run_os_notification(request->action);
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

                // 파일 대화상자는 한 번에 하나다. 누가 물었는지 기억해 결과를 그 페이지에 돌려준다.
                if (message.get<image_pick_intent>() != nullptr)
                {
                    luil::file_dialog_request request {};
                    request.id = ++file_dialog_revision_;
                    request.title = u8"이미지 파일";
                    luil::file_filter filter { u8"이미지 파일" };
                    for (const auto extension : image_extensions)
                        filter.extensions.emplace_back(extension);
                    request.filters.push_back(std::move(filter));
                    file_dialog_ = std::move(request);
                    file_dialog_purpose_ = file_dialog_purpose::image;
                    return;
                }
                if (message.get<workbench_import_intent>() != nullptr)
                {
                    file_dialog_ = workbench_page::patch_dialog_request(++file_dialog_revision_);
                    file_dialog_purpose_ = file_dialog_purpose::patch;
                    return;
                }
                if (const auto* result { message.get<luil::file_dialog_result>() }; result != nullptr)
                {
                    // 지금 실은 요청의 답만 받는다. 목적은 그 요청을 실을 때 정했다.
                    if (file_dialog_.has_value() == false || file_dialog_->id != result->id)
                        return;
                    file_dialog_.reset();
                    if (result->status == luil::file_dialog_status::chosen && result->paths.empty() == false)
                    {
                        if (file_dialog_purpose_ == file_dialog_purpose::patch)
                            static_cast<void>(workbench_.handle(luil::app_message { workbench_patch_intent { result->paths.front() } }));
                        else
                            static_cast<void>(images_.handle(luil::app_message { image_open_intent { result->paths.front() } }));
                    }
                    if (result->status == luil::file_dialog_status::failed)
                        static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { result->error, luil::toast_severity::error } }));
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
                if (const auto* const edge { message.get<edge_resize_intent>() }; edge != nullptr)
                {
                    // 마지막 배치 보고에서 위·왼쪽 끝만 옮긴다. 최대화·전체 화면 창은 끝이 화면에 붙어 있어 두지 않는다.
                    //  - 라이브러리는 앱의 배치 요청을 되울려 보고하지 않으므로 낸 값을 그대로 지금 배치로 삼는다.
                    if (placement_.valid() == false || placement_.maximized || placement_.fullscreen)
                        return;
                    const int left { static_cast<int>(edge->left * metrics_.scale) };
                    const int top { static_cast<int>(edge->top * metrics_.scale) };
                    luil::win32::window_placement next { placement_ };
                    next.x -= left;
                    next.width += left;
                    next.y -= top;
                    next.height += top;
                    placement_ = next;
                    placement_request_ = next;
                    ++placement_revision_;
                    return;
                }
                if (const auto* const metrics { message.get<window_metrics_intent>() }; metrics != nullptr)
                {
                    // 도는 중에 움직임 줄이기가 켜지면 끝 폭에 붙인다. 두면 남은 시간만큼 더 돌고 끝난다.
                    if (metrics->reduce_motion && metrics_.reduce_motion == false)
                        sidebar_motion_ = luil::spring_at_rest(target_sidebar_width());
                    // 페이지 전환은 움직임 줄이기가 켜지거나 창 크기·배율이 바뀌면 놓는다. 요소도 옛 크기 그림을 그리지 않지만
                    // 놓아야 덮개가 곧 걷혀 포인터가 풀린다 (page-transition-design.md 결정 5).
                    if ((metrics->reduce_motion && metrics_.reduce_motion == false) || metrics->width != metrics_.width || metrics->height != metrics_.height
                        || metrics->scale != metrics_.scale)
                        page_motion_ = {};
                    metrics_ = *metrics;
                    return;
                }
                if (const auto* const navigate { message.get<navigate_intent>() }; navigate != nullptr)
                {
                    // 떠나는 화면은 `page_`가 아직 옛 값일 때 짓는다. 도는 중에 또 바뀌면 들어오던 화면을 다시 지어 새 전환이다.
                    if (navigate->page != page_)
                        page_motion_ = make_page_motion(navigate->page);
                    page_ = navigate->page;
                    // 페이지를 떠나면 그 페이지의 popup도 닫는다.
                    tabs_.close_popups();
                    popups_.close_popups();
                    workbench_.close_popups();
                    theme_.close_popups();
                    return;
                }
                if (const auto* const scroll { message.get<navigation_scroll_intent>() }; scroll != nullptr)
                {
                    navigation_scroll_ = std::max(0.0f, navigation_scroll_ + scroll->delta);
                    return;
                }
                if (message.get<sidebar_toggle_intent>() != nullptr)
                {
                    sidebar_collapsed_ = sidebar_collapsed_ == false;
                    // **지금 폭과 속도에서** 새 목표로 이어 출발한다. 반쯤 접힌 채 다시
                    // 눌러도 끝값으로 튀거나 멈칫하지 않는다 (spring_to의 몫이다).
                    //  - 시계를 읽는 것은 앱이다. 라이브러리는 시각을 인자로 받을 뿐이다.
                    sidebar_motion_ = luil::spring_to(sidebar_motion_, target_sidebar_width(), std::chrono::steady_clock::now(), luil::spring_smooth, metrics_.reduce_motion);
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
                    sidebar_motion_ = luil::spring_at_rest(sidebar_width_);
                    return;
                }
                if (message.get<sidebar_resize_finish_intent>() != nullptr)
                {
                    // 끄는 동안은 저장하지 않는다 — 끝났다는 알림이 올 때 한 번이다.
                    saved_sidebar_width_ = sidebar_width_;
                    ++sidebar_resize_finishes_;
                    return;
                }
                if (message.get<popup_close_intent>() != nullptr)
                {
                    tabs_.close_popups();
                    popups_.close_popups();
                    windows_.close_popups();
                    workbench_.close_popups();
                    theme_.close_popups();
                    return;
                }
                // 컨텍스트 메뉴 선택은 페이지가 메뉴를 닫고 셸이 토스트로 잇는다.
                if (const auto* const select { message.get<card_menu_select_intent>() }; select != nullptr)
                {
                    static_cast<void>(popups_.handle(message));
                    if (select->key == u8"복사")
                        if (luil::app_host* const host { host_.load() }; host != nullptr)
                            host->post_clipboard_request(luil::clipboard_copy_request { u8"문서/보고서.md" });
                    static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { u8"컨텍스트 메뉴: " + select->key, luil::toast_severity::success } }));
                    return;
                }

                // 초점 요청의 결과는 셸이 토스트로 알린다. 덮어쓴 요청은 새 요청이 이어받아 말하지 않는다.
                if (const auto* const focus_report { message.get<tool_focus_report_intent>() }; focus_report != nullptr)
                {
                    // 팔레트와 색 칸 popup의 초점 요청은 알리지 않는다 — 열면 곧바로 칸·판에 초점이 서는 것이 보인다.
                    if (focus_report->id == palette_focus_id_ || focus_report->id == popups_focus_id_)
                        return;
                    if (focus_report->outcome == luil::focus_request_outcome::fulfilled)
                        static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { u8"도구 창 메모 칸으로 초점을 옮겼습니다.", luil::toast_severity::success } }));
                    else if (focus_report->outcome == luil::focus_request_outcome::expired)
                        static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { u8"메모 칸이 제때 서지 않아 초점을 옮기지 못했습니다.", luil::toast_severity::warning } }));
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

                // 명령 팔레트에서 고른 명령이다. 팔레트는 무엇을 골랐는지만 알리고 실행은 셸이 한다 — 단축키(Ctrl+B)와 같은
                // 메시지라 두 길이 갈라지지 않는다.
                if (const auto* const run { message.get<palette_run_intent>() }; run != nullptr)
                {
                    if (run->id.starts_with(u8"page:"))
                        handle(luil::app_message { navigate_intent { run->id.substr(5) } });
                    else if (run->id == u8"view.sidebar")
                        handle(luil::app_message { sidebar_toggle_intent {} });
                    else if (run->id == luil::undo_command_id && page_ == page_workbench)
                        static_cast<void>(workbench_.handle(luil::app_message { workbench_undo_intent {} }));
                    else if (run->id == luil::redo_command_id && page_ == page_workbench)
                        static_cast<void>(workbench_.handle(luil::app_message { workbench_redo_intent {} }));
                    return;
                }
                if (palette_.handle(message))
                    return;

                // 메뉴 막대다. 막대가 고른 셸 명령(사이드바·테마)은 단축키·팔레트와 같은 메시지로 잇는다.
                if (shell_menu_.handle(message))
                {
                    if (const std::optional<std::u8string> command { shell_menu_.take_command() }; command.has_value())
                    {
                        if (*command == shell_menu_sidebar)
                            handle(luil::app_message { sidebar_toggle_intent {} });
                        else if (*command == shell_menu_theme_light)
                            static_cast<void>(theme_.handle(luil::app_message { theme_intent { luil::theme_preference::light } }));
                        else if (*command == shell_menu_theme_system)
                            static_cast<void>(theme_.handle(luil::app_message { theme_intent { luil::theme_preference::system } }));
                        else if (*command == shell_menu_theme_dark)
                            static_cast<void>(theme_.handle(luil::app_message { theme_intent { luil::theme_preference::dark } }));
                        else if (command->starts_with(shell_menu_page_transition_prefix))
                        {
                            page_transition_mode_ = *command;
                            page_motion_ = {};
                        }
                    }
                    return;
                }

                // 그림 클립보드다 (image-clipboard-design.md). 클립보드는 UI thread의 몫이라 요청은 host에 넣고, 그림과 PNG는
                // 앱 상태라 여기(logic thread)서 짓는다. 페이지는 host를 모른다.
                if (message.get<image_clipboard_copy_intent>() != nullptr)
                {
                    if (luil::app_host* const host { host_.load() }; host != nullptr)
                        if (std::optional<luil::clipboard_data_copy_request> request { images_.make_copy_request() }; request.has_value())
                            host->post_clipboard_request(std::move(*request));
                    return;
                }
                if (message.get<image_clipboard_paste_intent>() != nullptr)
                {
                    if (luil::app_host* const host { host_.load() }; host != nullptr)
                        host->post_clipboard_request(luil::clipboard_data_paste_request {});
                    return;
                }
                // 클립보드 변화는 파일 페이지가 임자다. 이미지 페이지는 그림이 있는지만 함께 본다.
                if (const auto* const changed { message.get<files_clipboard_changed_intent>() }; changed != nullptr)
                    images_.set_clipboard_has_image(changed->change.has_image);

                // 서식 있는 글 페이지다 (rich-edit-exchange-design.md). 복사·붙여넣기는 페이지가 클립보드 요청을 지어 쥐고, 셸이
                // 여기(logic thread)서 가져가 host에 넣는다. 편집 메시지는 target이 다르면 거짓이라 다음 페이지가 받는다.
                if (rich_text_.handle(message))
                {
                    if (std::optional<luil::clipboard_request> request { rich_text_.take_clipboard_request() }; request.has_value())
                        if (luil::app_host* const host { host_.load() }; host != nullptr)
                            host->post_clipboard_request(std::move(*request));
                    return;
                }

                // 페이지들의 메시지다. 타입이 겹치지 않아 처음 받는 쪽이 임자다.
                // 편집 메시지(edit_intent)만 타입이 같고 target으로 나뉜다 — 남의 target이면 handle이 거짓을 돌려준다.
                if (gallery_.handle(message) || basics_.handle(message) || lists_.handle(message) || files_.handle(message) || tabs_.handle(message) || groups_.handle(message)
                    || toasts_.handle(message) || popups_.handle(message) || windows_.handle(message) || images_.handle(message) || network_.handle(message) || webview_.handle(message)
                    || zoom_.handle(message) || ink_.handle(message) || inspector_.handle(message) || theme_.handle(message) || workbench_.handle(message))
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
                files_.stop_watcher();
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
                // 테마 페이지가 읽은 스타일이다 (비면 내장 스타일). 캡션과 본문을 이 스타일로 배치하고 frame에도 같은 포인터를 싣는다.
                const luil::ui_style* const style { theme_.style().get() };

                // 높이는 height_for가 미리 알려 준다 (미리 arrange해 보는 우회가 필요 없다).
                //
                // **전체 화면에서는 캡션 줄을 아예 접는다.** 그동안 창에는 비클라이언트가
                // 없어(가장자리도 끌기 띠도 시스템 메뉴도 없다 — docs/concepts/window.md)
                // 창 chrome을 그려 둘 자리가 아니고, 화면을 덮는다는 뜻도 그것이다.
                //  - 그 사실은 배치 메시지가 나른다 (`window_placement::fullscreen`).
                //    창을 어떤 모습으로 세울지는 앱의 상태가 아니지만, 무엇을 그릴지는
                //    앱의 것이다 — 라이브러리는 알리기만 하고 tree는 우리가 짓는다.
                const luil::caption_config caption_config { make_caption_config(primary_instance_) };
                const float caption_height { placement_.fullscreen ? 0.0f : luil::caption_element::height_for(caption_config) * scale };
                // 캡션은 창 위나 아래에 선다 (창 페이지의 전환). 놓는 자리가 곧 끌기 띠의 자리다 —
                // 라이브러리는 게시된 tree에서 캡션이 선 자리를 읽어 판정한다 (caption-button-design.md).
                const bool caption_bottom { windows_.caption_at_bottom() };
                const float caption_top { caption_bottom ? height - caption_height : 0.0f };
                // 메뉴 막대는 caption 아래 별도 줄이다 (caption이 아래면 창 위). 본문을 그만큼 내린다 (docs/menu-bar-design.md).
                //  - 막대는 앱 내용이라 전체 화면에서도 남는다. caption만 접힌다.
                const float menu_bar_row { luil::menu_bar_height * scale };
                const float menu_bar_top { caption_bottom ? 0.0f : caption_height };
                const float content_top { menu_bar_top + menu_bar_row };
                if (placement_.fullscreen == false)
                {
                    auto caption { std::make_unique<luil::caption_element>(caption_config) };
                    caption->arrange({ .slot = { 0.0f, caption_top, width, caption_height }, .scale = scale, .style = style });
                    root->add(std::move(caption));

                    // 제목 표시줄 가운데의 명령 찾기 단추다. caption 위에 겹쳐 세우고 client 표시를 달면
                    // 끌기 띠 위에서도 눌린다 (caption-button-design.md).
                    constexpr float search_width { 240.0f };
                    constexpr float search_inset { 4.0f };
                    // 명령 팔레트의 진입 단추다 (command-palette-design.md). caption client 표시와 툴팁의 단축키는
                    // 라이브러리가 단다.
                    luil::command_palette_entry_config search_config {
                        .owner = u8"caption/search",
                        .text = u8"명령 찾기…",
                        .tooltip = u8"명령 팔레트를 엽니다",
                        .shortcut = palette_shortcut,
                        .open = luil::make_message_action(palette_open_intent {}),
                    };
                    auto search { luil::make_command_palette_entry(std::move(search_config)) };
                    search->arrange(
                        { { (width - search_width * scale) / 2.0f, caption_top + search_inset * scale, search_width * scale, caption_height - search_inset * 2.0f * scale }, scale });
                    root->add(std::move(search));
                }

                // 캡션 아래는 사이드바 + 현재 페이지의 가로 stack이다.
                //  - 사이드바 폭은 이 frame을 지은 시각의 스프링 값이다. 다음 예고(`next_tick`)도 이 시각에서 센다.
                frame_time_ = std::chrono::steady_clock::now();
                const luil::sidebar_config sidebar_config { make_sidebar_config(frame_time_) };
                const float sidebar_width { luil::sidebar_element::width_for(sidebar_config) };
                const float content_width { width / scale - sidebar_width };
                const float content_height { (height - caption_height - menu_bar_row) / scale };

                luil::stack_config shell_config {};
                shell_config.direction = luil::stack_direction::row;
                auto shell { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"shell" }, shell_config) };
                shell->add(std::make_unique<luil::sidebar_element>(sidebar_config, sidebar_collapsed_ ? nullptr : make_navigation()), sidebar_width);
                // 페이지는 늘 화면 전환 요소로 감싼다. 떠나는 화면이 없으면 새 화면만 그리는 껍질이다.
                auto page { std::make_unique<luil::page_transition_element>(luil::page_transition_config { u8"page", page_motion_ }, build_page(content_width, content_height, scale)) };
                const luil::page_transition_element* const page_slot { page.get() };
                shell->add_flexible(std::move(page));
                shell->arrange({ .slot = { 0.0f, content_top, width, height - caption_height - menu_bar_row }, .scale = scale, .style = style });
                // 다음 이동에서 떠나는 화면을 이 자리와 배율로 짓는다.
                content_slot_ = page_slot->bounds();
                content_scale_ = scale;
                root->add(std::move(shell));

                // 막대와 그 오른쪽 상태 label 둘이다. 막대는 메뉴 popup의 자리를 정하므로 아래에서 다시 읽는다.
                const luil::menu_bar_element* menu_bar { nullptr };
                {
                    constexpr float state_label_width { 150.0f };
                    constexpr float last_label_width { 220.0f };
                    constexpr float label_gap { 12.0f };
                    const float labels_width { (state_label_width + last_label_width + label_gap * 2.0f) * scale };
                    auto bar { shell_menu_.make_bar() };
                    bar->arrange({ .slot = { 0.0f, menu_bar_top, std::max(0.0f, width - labels_width), menu_bar_row }, .scale = scale, .style = style });
                    menu_bar = bar.get();
                    root->add(std::move(bar));
                    auto state { shell_menu_.make_state_label() };
                    state->arrange({ { width - labels_width + label_gap * scale, menu_bar_top, state_label_width * scale, menu_bar_row }, scale });
                    root->add(std::move(state));
                    auto last { shell_menu_.make_last_label() };
                    last->arrange({ { width - (last_label_width + label_gap) * scale, menu_bar_top, last_label_width * scale, menu_bar_row }, scale });
                    root->add(std::move(last));
                }

                // 사이드바 접힘 스프링의 상태다. 접힌 동안은 사이드바 내용이 없어서 이 label이 폭을 말한다.
                {
                    constexpr float motion_label_width { 160.0f };
                    constexpr float motion_label_height { 18.0f };
                    const bool moving { luil::spring_finished(sidebar_motion_, frame_time_) == false };
                    std::u8string motion_text { u8"도는 중" };
                    if (moving == false)
                        motion_text = u8"사이드바 " + to_u8(static_cast<int>(std::lround(sidebar_width))) + u8" · 멈춤";
                    auto motion { make_label(luil::ui_element_id { kind_text, u8"sidebar-motion" }, std::move(motion_text), 11.0f, luil::label_color_role::dim) };
                    motion->arrange({ { 6.0f * scale, content_top + (content_height - motion_label_height - 4.0f) * scale, motion_label_width * scale, motion_label_height * scale }, scale });
                    root->add(std::move(motion));

                    // 페이지 전환의 상태다. 도는 동안은 tree를 다시 짓지 않으므로 시작 frame의 "도는 중"이 끝 tick까지 남는다.
                    constexpr float transition_label_width { 190.0f };
                    const std::u8string_view name { page_transition_name(page_transition_mode_) };
                    std::u8string transition_text { u8"페이지 전환: 꺼짐" };
                    if (name.empty() == false)
                    {
                        const bool turning { page_motion_.prior != nullptr && luil::page_transition_finished(page_motion_, frame_time_) == false };
                        transition_text = u8"페이지 전환: " + std::u8string { name } + (turning ? u8" · 도는 중" : u8" · 멈춤");
                    }
                    auto transition { make_label(luil::ui_element_id { kind_text, u8"page-transition-state" }, std::move(transition_text), 11.0f, luil::label_color_role::dim) };
                    transition->arrange({ { 6.0f * scale, content_top + (content_height - motion_label_height * 2.0f - 4.0f) * scale, transition_label_width * scale, motion_label_height * scale }, scale });
                    root->add(std::move(transition));
                }

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
                frame->backdrop = theme_.backdrop();
                // 테마 페이지가 실행 중에 읽은 스타일이다. 비어 있으면 내장 스타일이다. 위의 캡션·본문 배치에 준 것과 같은 포인터다 —
                // 팔레트·그리기 치수는 이쪽에서, 컴포넌트 칸·글 크기는 배치에서 읽어 한쪽만 주면 잰 폭과 그린 글이 어긋난다.
                frame->style = theme_.style();
                // 검사기는 페이지의 상태 하나로 켠다. 어느 페이지를 보든 켠 채 다른 페이지를 들여다볼 수 있다.
                frame->inspector = inspector_.config();
                // 코드 글꼴은 이름을 줘야 고정폭이 된다. 비워 두면 내장 UI 글꼴이다 (appearance.h).
                // 변경 작업대의 차이 보기가 이 글꼴로 열을 맞춘다. Windows에 늘 있는 Consolas다.
                frame->fonts.code_family = u8"Consolas";
                // 크기·최대화의 복원과 창 페이지의 끝 옮기기다. 복원의 자리는 창을 만들 때 이미 잡았다 (wWinMain).
                // 요청은 revision이 바뀔 때만 적용되므로 매 frame 같은 값을 실어도
                // 창은 한 번만 움직인다.
                if (placement_request_.has_value())
                {
                    frame->window_placement_revision = placement_revision_;
                    frame->window_placement_request = placement_request_;
                }
                // popup은 이번 frame에서 배치된 element의 자리를 쓰므로 tree를 만든 뒤 단다.
                // 현재 페이지의 것만 뜬다 (다른 페이지의 열림 상태는 이동 때 닫았다).
                for (luil::win32::ui_popup& popup : tabs_.make_popups(scale))
                    frame->popups.push_back(std::move(popup));
                // 필드 메뉴는 창 client 안에서 아래가 모자라면 위로 뒤집힌다.
                for (luil::win32::ui_popup& popup : popups_.make_popups(scale, { 0.0f, 0.0f, width, height }))
                    frame->popups.push_back(std::move(popup));
                for (luil::win32::ui_popup& popup : workbench_.make_popups(scale))
                    frame->popups.push_back(std::move(popup));
                for (luil::win32::ui_popup& popup : theme_.make_popups(scale))
                    frame->popups.push_back(std::move(popup));
                // 메뉴 막대의 메뉴는 이번 frame에 배치한 항목 아래에 선다. 보기 메뉴는 사이드바와 테마의 지금 상태를 그린다.
                if (menu_bar != nullptr)
                    for (luil::win32::ui_popup& popup : shell_menu_.make_popups(*menu_bar, scale, { 0.0f, 0.0f, width, height }, sidebar_collapsed_ == false, theme_.appearance().theme, page_transition_mode_))
                        frame->popups.push_back(std::move(popup));
                // 도구 창은 페이지의 일부가 아니라 어느 페이지에서든 열려 있다.
                for (luil::ui_window& window : tabs_.make_windows())
                    frame->windows.push_back(std::move(window));
                for (luil::win32::ui_window& window : windows_.make_windows())
                    frame->windows.push_back(std::move(window));
                // 테마 페이지의 배경 효과 견본 창이다.
                for (luil::win32::ui_window& window : theme_.make_windows())
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
                // 명령 팔레트는 어느 페이지에서든 내용 영역 맨 위 가운데에 뜬다 (캡션이 위면 제목 표시줄 바로 아래).
                for (luil::win32::ui_popup& popup : palette_.make_popups(width / scale, content_top / scale + 8.0f, scale, page_ == page_workbench ? workbench_.undo_state() : luil::undo_command_state { u8"실행 취소", u8"다시 실행", false, false }))
                    frame->popups.push_back(std::move(popup));
                frame->file_dialog = file_dialog_;
                // 주 창 작업 표시줄 단추의 진행률과 오버레이다. 독립 창의 것은 그 창(`ui_window::taskbar`)에 실린다.
                frame->taskbar = windows_.make_taskbar_request();
                // 알림 영역 아이콘과, 그 메뉴로 숨긴 주 창이다. 숨기는 것은 앱 상태라 페이지가 쥔다.
                frame->tray = windows_.make_tray_request();
                // 앱이 뒤에 있어도 받는 전역 단축키다. 눌림은 항목의 `pressed`가 앱 메시지로 돌려준다.
                frame->hotkeys = windows_.make_hotkeys_request();
                frame->main_hidden = windows_.main_hidden();
                // 초점 요청은 세 곳(도구 창 페이지·명령 팔레트·색 칸 popup)이 낸다. 요청은 번호가 바뀔 때만 새것이라, 마지막으로 바뀐
                // 쪽의 요청을 셸 번호로 다시 매겨 싣는다 — 팔레트를 닫았다고 도구 창의 지난 요청이 되살아나지 않는다.
                if (const auto request { windows_.make_focus_request() }; request.has_value() && request->id != seen_tool_focus_)
                {
                    seen_tool_focus_ = request->id;
                    focus_ = *request;
                    focus_->id = ++focus_revision_;
                }
                if (const auto request { palette_.make_focus_request() }; request.has_value() && request->id != seen_palette_focus_)
                {
                    seen_palette_focus_ = request->id;
                    focus_ = *request;
                    focus_->id = ++focus_revision_;
                    palette_focus_id_ = focus_->id;
                }
                if (const auto request { popups_.make_focus_request() }; request.has_value() && request->id != seen_popups_focus_)
                {
                    seen_popups_focus_ = request->id;
                    focus_ = *request;
                    focus_->id = ++focus_revision_;
                    popups_focus_id_ = focus_->id;
                }
                frame->focus = focus_;
                frame->focus_report = [](const std::uint64_t id, const luil::focus_request_outcome outcome) { return luil::app_message { tool_focus_report_intent { id, outcome } }; };
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

            // 시간이 지나야 바뀌는 것 셋이다: 토스트 만료, 사이드바 접힘 전환, 변경 작업대의 흉내 낸 불러오기.
            // 가장 이른 쪽을 예고하면 메시지가 없어도 그 시각에 tick이 온다.
            //  - 전환이 끝나면 그쪽이 nullopt를 답해 시간 경로가 다시 잠잔다.
            [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override
            {
                std::optional<std::chrono::steady_clock::time_point> next { toasts_.next_expiry() };
                // 접힘 스프링은 마지막으로 지은 frame의 시각에서 센다. 그 frame이 도는 중이었다면 한 frame 뒤에 하나를 더 지어
                // 멈춘 모습으로 끝낸다 (지금 시각에서 세면 그 사이에 끝난 스프링이 "도는 중" 표시를 남긴 채 잠든다).
                //  - 페이지 전환은 끝 시각 하나를 싣는다. 도는 동안의 frame은 요소의 예고가 다시 그리고, tree는 끝에 한 번 짓는다.
                for (const std::optional<std::chrono::steady_clock::time_point> other :
                    { luil::spring_next_update(sidebar_motion_, frame_time_), workbench_.next_due(), luil::page_transition_end(page_motion_) })
                    if (other.has_value() && (next.has_value() == false || *other < *next))
                        next = other;
                return next;
            }

            void tick(const std::chrono::steady_clock::time_point now) override
            {
                toasts_.prune();
                workbench_.tick(now);
                // 끝난 전환은 놓는다. 다음 frame은 prior 없는 tree라 덮개가 걷혀 포인터가 풀린다.
                if (page_motion_.prior != nullptr && luil::page_transition_finished(page_motion_, now))
                    page_motion_ = {};
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

            [[nodiscard]] luil::sidebar_config make_sidebar_config(const std::chrono::steady_clock::time_point now) const
            {
                luil::sidebar_config config {};
                config.owner = u8"navigation";
                config.expanded_width = sidebar_width_;
                config.collapsed_width = sidebar_collapsed_width;
                config.collapsed = sidebar_collapsed_;
                // 지금 폭은 스프링이 답한다. 끝나 있으면 목표와 같은 값이다.
                config.width = luil::spring_value(sidebar_motion_, now);
                config.toggle_tooltip = sidebar_collapsed_ ? u8"펼친다" : u8"접는다";
                config.toggle = luil::make_message_action(sidebar_toggle_intent {});
                config.resize = [](const float delta) { return luil::make_app_action(sidebar_resize_intent { delta }); };
                config.resize_finish = []() { return luil::make_app_action(sidebar_resize_finish_intent {}); };
                return config;
            }

            // 사이드바 내용: 페이지를 오가는 배타 선택이다.
            [[nodiscard]] std::unique_ptr<luil::ui_element> make_navigation() const
            {
                luil::list_config navigation {};
                navigation.owner = u8"navigation";
                navigation.row_height = 32.0f;
                navigation.scroll_offset = navigation_scroll_;
                navigation.scroll = [](const float delta) { return luil::make_app_action(navigation_scroll_intent { delta }); };
                const auto add_page = [&navigation](const std::u8string_view key, std::u8string label) {
                    const char32_t icon { key == page_overview ? luil::codicons::icon_rocket : (key == page_theme ? luil::codicons::icon_settings_gear : luil::codicons::icon_file) };
                    navigation.items.push_back({ std::u8string { key }, std::move(label), icon });
                };
                for (const navigation_page& page : navigation_pages)
                    add_page(page.key, std::u8string { page.label });
                navigation.selected = page_;
                navigation.select = [](const std::u8string& value) { return luil::make_app_action(navigate_intent { value }); };

                luil::stack_config config {};
                config.padding = luil::edge_insets::symmetric(8.0f, 4.0f);
                auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"navigation" }, config) };
                auto pages { std::make_unique<luil::list_element>(std::move(navigation)) };
                pages->set_access_name(u8"페이지");
                column->add_flexible(std::move(pages));
                // 손잡이를 놓을 때 저장한 폭이다. 끄는 동안에는 바뀌지 않고 놓아야 따라온다.
                if (sidebar_resize_finishes_ > 0)
                {
                    std::u8string saved { u8"놓은 폭 " };
                    for (const char digit : std::to_string(static_cast<int>(std::lround(saved_sidebar_width_))))
                        saved.push_back(static_cast<char8_t>(digit));
                    saved += u8" · ";
                    for (const char digit : std::to_string(sidebar_resize_finishes_))
                        saved.push_back(static_cast<char8_t>(digit));
                    saved += u8"번";
                    column->add_gap(8.0f);
                    column->add(make_label(luil::ui_element_id { kind_text, u8"sidebar-saved-width" }, std::move(saved), 11.0f, luil::label_color_role::dim), 16.0f);
                }
                return column;
            }

            // 지금 페이지(`page_`)에서 `next`로 가는 전환이다. 떠나는 화면을 지난 frame의 본문 자리와 배율로 따로 짓는다.
            //  - 꺼져 있거나 움직임 줄이기면 짓지도 않는다. 웹뷰는 창 위의 자식 창이라 함께 움직이지 않아 오가는 바꿈은 전환하지 않는다.
            [[nodiscard]] luil::page_transition make_page_motion(const std::u8string& next)
            {
                if (page_transition_name(page_transition_mode_).empty() || metrics_.reduce_motion || page_ == page_webview || next == page_webview
                    || content_slot_.width <= 0.0f || content_slot_.height <= 0.0f)
                    return {};
                const float scale { content_scale_ };
                auto prior { build_page(content_slot_.width / scale, content_slot_.height / scale, scale) };
                const luil::ui_style* const style { theme_.style().get() };
                auto tree { std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(prior), content_slot_, scale, {}, nullptr, style)) };
                return luil::start_page_transition(std::move(tree), page_style_for(next), std::chrono::steady_clock::now(), metrics_.reduce_motion);
            }

            // 고른 전환의 모양이다. 사이드바 목록이 세로라 밀기는 세로 축이다.
            [[nodiscard]] luil::page_transition_style page_style_for(const std::u8string& next) const
            {
                luil::page_transition_style style {};
                style.axis = luil::page_transition_axis::vertical;
                style.direction = navigation_index(next) >= navigation_index(page_) ? luil::page_transition_direction::forward : luil::page_transition_direction::backward;
                style.curve = luil::page_transition_timing { page_transition_length_.value_or(luil::page_transition_duration) };
                if (page_transition_mode_ == shell_menu_page_transition_fade)
                    style.effect = luil::page_transition_effect::fade;
                else if (page_transition_mode_ == shell_menu_page_transition_zoom)
                    style.effect = luil::page_transition_effect::zoom;
                else if (page_transition_mode_ == shell_menu_page_transition_spring)
                {
                    luil::spring_config config { luil::spring_smooth };
                    if (page_transition_length_.has_value())
                        config.response = *page_transition_length_;
                    style.curve = luil::page_transition_spring { config, 0.0f };
                }
                return style;
            }

            [[nodiscard]] std::unique_ptr<luil::ui_element> build_page(const float width, const float height, const float scale)
            {
                if (page_ == page_overview)
                    return gallery_.build_overview(width);
                if (page_ == page_controls)
                    return gallery_.build_controls();
                if (page_ == page_lists)
                    return lists_.build(width, height, scale);
                if (page_ == page_files)
                    return files_.build(width, height, scale);
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
                if (page_ == page_zoom)
                    return zoom_.build(width, height, scale);
                if (page_ == page_ink)
                    return ink_.build(width, height, scale);
                if (page_ == page_inspector)
                    return inspector_.build(width, height, scale);
                if (page_ == page_theme)
                    return theme_.build(width, height, scale);
                if (page_ == page_workbench)
                    return workbench_.build(width, height, scale);
                if (page_ == page_rich_text)
                    return rich_text_.build(width, height, scale);
                return basics_.build(width, height, scale);
            }

            gallery_page gallery_ {};
            basics_page basics_ {};
            lists_page lists_ {};
            files_page files_ {};
            tabs_page tabs_ {};
            groups_page groups_ {};
            toasts_page toasts_ {};
            popups_page popups_ {};
            windows_page windows_ {};
            images_page images_ {};
            std::optional<luil::file_dialog_request> file_dialog_ {};
            std::uint64_t file_dialog_revision_ { 0 };
            enum class file_dialog_purpose { image, patch };
            file_dialog_purpose file_dialog_purpose_ { file_dialog_purpose::image };
            network_page network_ {};
            zoom_page zoom_ {};
            ink_page ink_ {};
            inspector_page inspector_ {};
            command_palette_host palette_ {};
            shell_menu shell_menu_ {};
            // 같은 세션에서 먼저 뜬 데모가 없었는가 (wWinMain의 `single_instance`).
            bool primary_instance_ { true };
            // 셸이 실은 마지막 초점 요청과, 세 요청원의 마지막으로 본 번호다.
            std::optional<luil::focus_request> focus_ {};
            std::uint64_t focus_revision_ { 0 };
            std::uint64_t seen_tool_focus_ { 0 };
            std::uint64_t seen_palette_focus_ { 0 };
            std::uint64_t palette_focus_id_ { 0 };
            std::uint64_t seen_popups_focus_ { 0 };
            std::uint64_t popups_focus_id_ { 0 };
            theme_page theme_ { std::u8string { reinterpret_cast<const char8_t*>(luil_demo_style_json), luil_demo_style_json_size } };
            webview_page webview_ {};
            workbench_page workbench_ {};
            rich_text_page rich_text_ {};

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
            // 지금 싣는 배치 요청과 그 번호다. 복원이 있으면 1번이 복원이다.
            std::optional<luil::win32::window_placement> placement_request_ {};
            std::uint64_t placement_revision_ { 0 };

            std::u8string page_ {};
            bool sidebar_collapsed_ { false };
            float navigation_scroll_ { 0.0f };
            float sidebar_width_ { sidebar_default_width };
            // 손잡이를 놓았을 때 저장한 폭과 그 횟수다 (`sidebar_config::resize_finish`의 시연).
            float saved_sidebar_width_ { sidebar_default_width };
            int sidebar_resize_finishes_ { 0 };
            // 접힘 스프링이다 (앱 상태).
            // 라이브러리는 이것을 들고 있지 않고, 지금 폭만 물어 사이드바에 넘긴다.
            luil::spring sidebar_motion_ { luil::spring_at_rest(sidebar_default_width) };
            // 마지막으로 지은 frame의 시각이다. 시간이 흘러야 바뀌는 상태의 다음 예고를 이 시각에서 센다.
            std::chrono::steady_clock::time_point frame_time_ {};
            // 페이지 전환이다 (앱 상태). 고른 모양은 보기 메뉴의 라디오 키이고, 길이는 인자로 바꿀 수 있다.
            std::u8string page_transition_mode_ { shell_menu_page_transition_slide };
            std::optional<std::chrono::milliseconds> page_transition_length_ {};
            luil::page_transition page_motion_ {};
            // 지난 frame의 본문 자리(물리 픽셀)와 배율이다. 떠나는 화면을 같은 크기·배율로 짓는다.
            luil::rect_f content_slot_ {};
            float content_scale_ { 1.0f };
            window_metrics_intent metrics_ {};
            std::atomic<bool> closed_ { false };
        };

        // 상태 기계가 모르는 앱 정책이다.
        // 어느 element가 텍스트 칸인지, 휠이 무엇을 스크롤하는지,
        // 메뉴 kind 짝이 무엇인지, dialog가 열린 동안 Esc·Enter가 무엇을 뜻하는지.
        class demo_policy final : public luil::interaction_policy
        {
        public:
            // 표는 page test가 함께 보도록 `text_targets.h`에 둔다.
            [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
            {
                return text_target_of_kind(kind);
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
                // **표를 든 페이지가 먼저다.**
                //
                // 표는 특정 id를 이름 대므로 그 자리에서만 답하고, 아래의 표 없는
                // 짝은 좌표를 덮는 **아무** 흘리는 컨테이너나 답한다. 순서를 뒤집으면
                // 표가 이름 댄 것을 감싼 영역이 휠을 먼저 삼켜, 그 표는 영영 불리지
                // 않는다 — 탭 막대처럼 휠과 되살리기의 임자가 진짜로 갈리는 자리가
                // 그때 조용히 망가진다.
                //  - 지금 tree에 없는 페이지의 대상은 find가 걸러 준다.
                if (auto actions { lists_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
                    return actions;
                if (auto actions { tabs_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
                    return actions;
                if (auto actions { network_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
                    return actions;
                // 표에 없는 것은 자기 메시지를 든 컨테이너다 (`scroll_source`).
                // 네트워크 페이지의 결과 칸과 목록 페이지의 가상 목록이 여기서 잡힌다 —
                // 페이지가 늘어도 이 줄은 그대로다.
                return luil::route_wheel(tree, event, scroll_delta);
            }

            [[nodiscard]] std::vector<luil::input_action> on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused) override
            {
                // 휠과 같은 순서다 — 이름 댄 표가 먼저고 표 없는 짝이 나중이다.
                // 지금 tree에 없는 페이지의 창은 `route_reveal`이 걸러 준다.
                if (auto actions { lists_page::route_reveal(tree, focused) }; actions.empty() == false)
                    return actions;
                if (auto actions { tabs_page::route_reveal(tree, focused) }; actions.empty() == false)
                    return actions;
                // 네트워크 페이지에는 되살리기 표가 **아예 없었다.** 결과 칸을
                // `scroll_area_element`로 바꾼 것만으로 이 한 줄이 그 페이지의
                // 되살리기까지 세운다 — 표를 새로 적지 않아도, 자기 메시지를 든
                // 컨테이너면 여기서 찾는다. 이것이 표 없는 짝의 값이다.
                return luil::route_reveal(tree, focused);
            }

            [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree* tree, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot) override
            {
                // 읽기 전용 안내 칸에 초점이 있는 채 닿은 Ctrl+Z·Ctrl+Y다. 칸이 삼키지 않고 넘긴 키를 기본 페이지가 센다.
                if (const auto undo { readonly_undo_key_of(event, snapshot, kind_link_view_input) }; undo.has_value())
                    return { luil::make_app_action(*undo) };
                // 팔레트를 여는 단축키와 팔레트 명령의 단축키(Ctrl+B)다. 팔레트에 보이는 단축키 열과 같은 명령표로 맞춘다.
                if (luil::chord_of(event) == palette_shortcut)
                    return { luil::make_app_action(palette_open_intent {}) };
                // 검사기를 켜고 끈다. 검사기 페이지의 단추와 같은 메시지다.
                if (luil::chord_of(event) == inspector_shortcut)
                    return { luil::make_app_action(inspector_command_intent { inspector_command::toggle }) };
                if (const auto id { luil::match_command(palette_commands(), event, luil::command_scope::window) }; id.has_value())
                    return { luil::make_app_action(palette_run_intent { *id }) };
                return tabs_page::route_key(tree, event);
            }
        };

        class demo_delegate final : public luil::win32::window_delegate
        {
        public:
            // driver를 아는 유일한 이유는 배출구가 쓸 host를 건네주기 위해서다.
            // 둘 다 wWinMain의 지역이라 수명이 같다.
            explicit demo_delegate(demo_driver& driver) noexcept
                : driver_ { driver }
            {
            }

            // 창과 host가 준비된 직후다 (메시지 루프 전).
            // HTTP client의 배출구가 답을 넣을 곳이 이 host다.
            void on_started(luil::win32::app_host& host) override
            {
                driver_.bind_host(host);
                // 앱 식별은 창보다 먼저 섰다. 토스트 페이지의 상태 줄이 처음부터 이 값을 보인다.
                host.post_app_message(luil::app_message { os_notification_status_intent { luil::notification_status() } });
            }

            // 사용자가 OS 알림을 누르거나 닫았다. 토스트 페이지가 사건 줄로 보인다.
            [[nodiscard]] luil::app_message make_notification_message(const luil::notification_event& event) override
            {
                return luil::app_message { os_notification_event_intent { event } };
            }

            [[nodiscard]] luil::app_message make_file_dialog_message(const luil::file_dialog_result& result) override
            {
                return luil::app_message { result };
            }

            [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics) override
            {
                return luil::app_message { window_metrics_intent { metrics.width, metrics.height, metrics.scale, metrics.reduce_motion } };
            }

            [[nodiscard]] luil::app_message make_backdrop_message(const luil::win32::backdrop_report& report) override
            {
                return luil::app_message { backdrop_report_intent { report } };
            }

            // 작업 표시줄 단추에 요청이 닿은 정도다. 창 페이지가 받아 글로 보인다.
            [[nodiscard]] luil::app_message make_taskbar_message(const luil::win32::taskbar_report& report) override
            {
                return luil::app_message { taskbar_report_intent { report } };
            }

            // 점프 목록을 셸에 낸 결과는 창 페이지의 반영 줄이 읽는다.
            [[nodiscard]] luil::app_message make_jump_list_message(const luil::win32::jump_list_report& report) override
            {
                return luil::app_message { jump_list_report_intent { report } };
            }

            // 알림 영역 아이콘이 셸에 닿은 정도도 창 페이지의 반영 줄이 읽는다.
            [[nodiscard]] luil::app_message make_tray_message(const luil::win32::tray_report& report) override
            {
                return luil::app_message { tray_report_intent { report } };
            }

            // 전역 단축키를 걸어 본 결과도 창 페이지의 반영 줄이 읽는다.
            [[nodiscard]] luil::app_message make_hotkey_message(const luil::win32::hotkey_report& report) override
            {
                return luil::app_message { hotkey_report_intent { report } };
            }

            [[nodiscard]] luil::app_message make_window_placement_message(const luil::win32::window_placement& placement) override
            {
                return luil::app_message { placement_intent { placement } };
            }

            // 파일 클립보드의 답과 변화다. 파일 페이지가 받는다.
            [[nodiscard]] luil::app_message make_file_clipboard_message(const luil::file_clipboard_content& content) override
            {
                return luil::app_message { files_clipboard_read_intent { content } };
            }

            [[nodiscard]] luil::app_message make_clipboard_changed_message(const luil::clipboard_change& change) override
            {
                return luil::app_message { files_clipboard_changed_intent { change } };
            }

            // 클립보드의 글과 그림이다. 이미지 페이지가 받아 logic thread에서 푼다. 서식 글 페이지가 청한 답은 표식으로 가른다.
            [[nodiscard]] luil::app_message make_clipboard_data_message(const luil::clipboard_data& data) override
            {
                if (data.tag == rich_text_clipboard_tag)
                    return luil::app_message { rich_text_clipboard_intent { data } };
                return luil::app_message { image_clipboard_data_intent { data } };
            }

            // 받는 칸이 아닌 곳에 놓인 파일의 물러섬이다.
            // 데모는 토스트로 알린다 — 참을 돌려주므로 여러 개를 놓아도 하나다.
            [[nodiscard]] bool on_file_dropped(luil::win32::app_host& host, const std::u8string& path) override
            {
                host.post_app_message(luil::app_message { toast_request_intent { u8"받은 파일: " + path, luil::toast_severity::info } });
                return true;
            }

            // 같은 이름의 둘째 인스턴스가 보낸 인자와 작업 폴더다. 창 페이지가 받아 한 줄로 보인다.
            //  - 보낸 쪽이 끝나기를 기다리므로 게시만 하고, 표가 0이 아닌지를 답한다 (file-watch-design.md의 `deliver`와 같다).
            //    앞으로 세우기는 라이브러리가 콜백 뒤에 한다.
            [[nodiscard]] bool on_instance_arguments(luil::win32::app_host& host, const luil::instance_arguments& arguments) override
            {
                // 이 데모가 끝나는 중에 COM이 띄운 둘째가 알림 표식을 넘길 수 있다. 파일 인자가 아니라 받기만 하고 보이지 않는다.
                if (luil::notification_launch(arguments.arguments))
                    return true;
                return host.post_app_message_ticketed(luil::app_message { instance_arguments_intent { arguments } }) != 0;
            }

        private:
            demo_driver& driver_;
        };
    }
}

namespace {
    // `--send-to-first`가 있으면 그 플래그를 뺀 나머지 인자를 첫째 인스턴스에 보내고 종료 코드를 돌려준다. 없으면 빈 값이다.
    //  - 종료 코드: 첫째가 받았으면 0, 첫째의 주 창이 시한 안에 없거나 이 프로세스가 첫째면 3, 그 밖의 실패는 4다.
    //  - `parse_arguments`가 모르는 인자를 거절하므로 이 분기가 그 앞에 선다. 플래그가 없으면 둘째도 평소처럼 뜬다.
    [[nodiscard]] std::optional<int> send_to_first_instance(const luil::single_instance& instance, const std::vector<std::u8string>& arguments)
    {
        std::vector<std::u8string> forwarded {};
        bool requested { false };
        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            if (const std::u8string_view argument { arguments[index] }; argument == u8"--send-to-first")
                requested = true;
            else
                forwarded.push_back(arguments[index]);
        }
        if (requested == false)
            return std::nullopt;
        const luil::platform_result result { instance.send_to_primary(forwarded) };
        if (result.ok())
            return 0;
        return result.status == luil::platform_status::not_found ? 3 : 4;
    }

    // `--unregister-app-identity`가 있으면 데모의 앱 식별을 지우고 종료 코드를 돌려준다. 없으면 빈 값이다.
    //  - 종료 코드: 지웠으면 0, 지울 항목이 없으면 3, 그 밖의 실패는 4다. 알림 센터에 남은 데모 알림은 거두지 않는다.
    [[nodiscard]] std::optional<int> unregister_identity(const std::vector<std::u8string>& arguments)
    {
        bool requested { false };
        for (std::size_t index = 1; index < arguments.size(); ++index)
            requested = requested || arguments[index] == u8"--unregister-app-identity";
        if (requested == false)
            return std::nullopt;
        const luil::platform_result result { luil::win32::unregister_app_identity(demo::demo_app_identity) };
        if (result.ok())
            return 0;
        return result.status == luil::platform_status::not_found ? 3 : 4;
    }

    // 알림 클릭으로 뜬 실행의 표식(`-luil-notification`·`-Embedding`)을 뺀 인자다. `parse_arguments`는 모르는 인자를 거절한다.
    [[nodiscard]] std::vector<std::u8string> without_notification_flags(const std::vector<std::u8string>& arguments)
    {
        std::vector<std::u8string> kept {};
        for (const std::u8string& argument : arguments)
        {
            if (luil::notification_launch(std::span<const std::u8string> { &argument, 1 }) == false)
                kept.push_back(argument);
        }
        return kept;
    }

    [[nodiscard]] bool parse_arguments(
        const std::vector<std::u8string>& arguments, luil::win32::window_config& config, std::u8string& start_page, bool& caption_bottom, luil::window_backdrop& backdrop
        , std::u8string& page_transition, std::optional<std::chrono::milliseconds>& page_transition_length)
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
            else if (argument == u8"--presentation=standard")
                config.presentation = luil::win32::window_presentation::standard;
            else if (argument == u8"--presentation=layered")
                config.presentation = luil::win32::window_presentation::layered;
            else if (argument == u8"--caption=top" || argument == u8"--caption=bottom")
                caption_bottom = argument == u8"--caption=bottom";
            else if (argument == u8"--backdrop=none")
                backdrop = luil::window_backdrop::none;
            else if (argument == u8"--backdrop=mica")
                backdrop = luil::window_backdrop::mica;
            else if (argument == u8"--backdrop=mica_alt")
                backdrop = luil::window_backdrop::mica_alt;
            else if (argument == u8"--backdrop=acrylic")
                backdrop = luil::window_backdrop::acrylic;
            else if (argument.starts_with(u8"--page-transition="))
            {
                const std::u8string mode { std::u8string { demo::shell_menu_page_transition_prefix } + std::u8string { argument.substr(18) } };
                const bool known { mode == demo::shell_menu_page_transition_none || mode == demo::shell_menu_page_transition_slide || mode == demo::shell_menu_page_transition_fade
                    || mode == demo::shell_menu_page_transition_zoom || mode == demo::shell_menu_page_transition_spring
                };

                if (known == false)
                    return false;
                page_transition = mode;
            }
            else if (argument.starts_with(u8"--page-transition-ms="))
            {
                const std::u8string_view value { argument.substr(21) };
                int milliseconds { 0 };
                const auto* const first { reinterpret_cast<const char*>(value.data()) };
                const auto [parsed, code] { std::from_chars(first, first + value.size(), milliseconds) };
                if (code != std::errc {} || parsed != first + value.size() || milliseconds <= 0)
                    return false;
                page_transition_length = std::chrono::milliseconds { milliseconds };
            }
            else
                return false;
        }
        return true;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    luil::win32::enable_per_monitor_dpi_awareness();
    const luil::win32::com_sta_scope com {};

    const auto arguments { luil::win32::command_line_arguments() };
    if (arguments.has_value() == false)
        return 1;

    // 같은 세션에서 둘째로 뜬 데모도 막지 않고 띄운다 — 데모는 창을 여럿 띄워 보는 자리이기도 하다. 제목만 다르다.
    //  - 프로세스가 끝날 때까지 쥔다. wWinMain의 지역이라 메시지 루프가 끝난 뒤에 놓인다.
    const luil::single_instance instance { u8"luil-demo" };
    // 인자를 첫째에 넘기는 일만 하는 둘째는 창을 띄우지 않고 끝낸다.
    if (const std::optional<int> sent { send_to_first_instance(instance, *arguments) }; sent.has_value())
        return *sent;
    // 앱 식별을 지우는 일만 하는 실행도 창을 띄우지 않는다.
    if (const std::optional<int> removed { unregister_identity(*arguments) }; removed.has_value())
        return *removed;
    luil::win32::window_config config {};
    config.class_name = L"Luil.Demo.Window";
    config.caption = demo::make_caption_config(instance.primary());
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
    bool caption_bottom { false };
    luil::window_backdrop backdrop { luil::window_backdrop::none };
    std::u8string page_transition { demo::shell_menu_page_transition_slide };
    std::optional<std::chrono::milliseconds> page_transition_length {};
    if (parse_arguments(without_notification_flags(*arguments), config, start_page, caption_bottom, backdrop, page_transition, page_transition_length) == false)
        return 2;
    // OS 알림이 쓰는 앱 식별이다. 아이콘은 실행 파일 옆 assets의 PNG다 (IconUri는 실행 파일 리소스를 가리키지 못한다).
    config.identity.id = std::u8string { demo::demo_app_identity };
    config.identity.display_name = u8"luil 데모";
    if (const std::filesystem::path directory { demo::executable_directory() }; directory.empty() == false)
        config.identity.icon_path = (directory / L"assets" / L"gradient.png").u8string();

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

    demo::demo_driver driver { restore, start_page, instance.primary() };
    driver.set_window_options(caption_bottom, config.presentation == luil::win32::window_presentation::layered, backdrop);
    driver.set_page_transition(std::move(page_transition), page_transition_length);
    demo::demo_policy policy {};
    demo::demo_delegate delegate { driver };

    luil::win32::window_environment environment {};
    if (config.smoke_test == false)
    {
        environment.driver = &driver;
        environment.policy = &policy;
        environment.delegate = &delegate;
        // 첫째이면 같은 이름의 둘째가 보낸 인자를 주 창이 받는다. instance는 wWinMain의 지역이라 창보다 오래 산다.
        environment.instance = &instance;
    }
    return luil::win32::run_application_window(config, environment);
}

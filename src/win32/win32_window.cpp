#include "luil/win32/win32_window.h"

#include "host/popup_reconcile.h"
#include "host/surface_invalidate.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/webview_element.h"
#include "win32/caption_surface.h"
#include "win32/composition_device.h"
#include "win32/dpi_scale.h"
#include "win32/embedded_assets.h"
#include "win32/popup_surface.h"
#include "win32/secondary_surface.h"
#include "win32/skia_renderer.h"
#include "win32/surface_input.h"
#include "win32/utf8.h"
#include "win32/webview_host.h"
#include "win32/webview_layout.h"
#include "win32/win32_clipboard.h"
#include "win32/win32_drop.h"
#include "win32/win32_error.h"
#include "win32/win32_fonts.h"
#include "win32/window_mode.h"
#include "win32/window_surface.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <windows.h>

#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace luil::win32 {
    namespace {
        // 십진 정수 하나다. 부호와 숫자만 받고 남는 글자가 있으면 실패다.
        // 화면 좌표라 백만을 넘는 값은 오타로 본다.
        [[nodiscard]] std::optional<int> parse_screen_coordinate(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return std::nullopt;
            const bool negative { text.front() == u8'-' };
            const std::u8string_view digits { negative || text.front() == u8'+' ? text.substr(1) : text };
            if (digits.empty())
                return std::nullopt;
            long long value { 0 };
            for (const char8_t character : digits)
            {
                if (character < u8'0' || character > u8'9')
                    return std::nullopt;
                value = value * 10 + (character - u8'0');
                if (value > 1'000'000)
                    return std::nullopt;
            }
            return static_cast<int>(negative ? -value : value);
        }
    } // namespace

    std::optional<window_position> parse_window_position(const std::u8string_view text) noexcept
    {
        const std::size_t comma { text.find(u8',') };
        if (comma == std::u8string_view::npos)
            return std::nullopt;
        const std::optional<int> x { parse_screen_coordinate(text.substr(0, comma)) };
        const std::optional<int> y { parse_screen_coordinate(text.substr(comma + 1)) };
        if (x.has_value() == false || y.has_value() == false)
            return std::nullopt;
        return window_position { *x, *y };
    }

    bool placement_in_screen_range(const window_placement& placement) noexcept
    {
        // Win32 화면 좌표(LONG)와 int는 같은 32-bit라 상·하한 하나면 된다.
        constexpr long long lowest { std::numeric_limits<LONG>::min() };
        constexpr long long highest { std::numeric_limits<LONG>::max() };
        const long long right { static_cast<long long>(placement.x) + placement.width };
        const long long bottom { static_cast<long long>(placement.y) + placement.height };
        return right >= lowest && right <= highest && bottom >= lowest && bottom <= highest;
    }

    namespace {
        constexpr int direct3d_unavailable_exit_code { 77 };
        // wake 신호다.
        // frame slot과 interaction slot의 signal callback이 게시한다.
        constexpr UINT snapshot_wake_message { WM_APP + 1 };
        // input thread가 큐에 넣은 창 명령(`ui_command`)의 신호다.
        // 내용은 host의 큐가 나른다 — 창 메시지는 신호만 싣는다.
        constexpr UINT ui_command_request_message { WM_APP + 2 };
        // input thread가 큐에 넣은 앱 UI 명령의 신호다.
        constexpr UINT app_ui_command_request_message { WM_APP + 3 };
        // 클립보드 요청(복사·붙여넣기)을 UI thread로 넘기는 신호다.
        constexpr UINT clipboard_request_message { WM_APP + 4 };
        // tree가 next_update로 예고한 "다음에 그림이 달라지는 시각"에 다시 그리기 위한 timer다.
        // 매 render가 남은 시간을 다시 계산해 하나만 걸므로 반복 timer가 아니어도 이어진다.
        // 예고가 없으면 timer도 없다.
        //  - 평소에는 이벤트가 있을 때만 그린다.
        constexpr UINT_PTR update_timer_id { 1 };
        // 연속 애니메이션(회전 표시처럼 next_update가 "지금"을 답하는 것)의 다시 그리기 간격이다.
        // 30fps면 눈에 매끄럽다.
        constexpr std::chrono::milliseconds continuous_repaint_interval { 33 };

        // OS의 앱 모드가 밝은지다.
        // 값을 읽지 못하면 어두운 쪽으로 본다.
        [[nodiscard]] bool read_system_prefers_light_theme() noexcept
        {
            DWORD value { 0 };
            DWORD size { sizeof(value) };
            const LSTATUS status {
                RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size),
            };
            return status == ERROR_SUCCESS && value != 0;
        }

        [[nodiscard]] bool read_high_contrast_enabled() noexcept
        {
            HIGHCONTRASTW high_contrast {};
            high_contrast.cbSize = sizeof(high_contrast);
            return FALSE != SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0) && (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        }

        // OS의 글꼴 다듬기 설정이다.
        // ClearType이면 LCD 서브픽셀, 아니면(접근성으로 껐거나 표준 다듬기) 회색조다.
        [[nodiscard]] text_render_quality read_text_render_quality() noexcept
        {
            BOOL smoothing { FALSE };
            if (SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0) == FALSE || smoothing == FALSE)
                return text_render_quality::grayscale;
            UINT type { FE_FONTSMOOTHINGSTANDARD };
            if (SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0) == FALSE)
                return text_render_quality::grayscale;
            return type == FE_FONTSMOOTHINGCLEARTYPE ? text_render_quality::subpixel_lcd : text_render_quality::grayscale;
        }

        [[nodiscard]] ui_color read_system_color(const int index) noexcept
        {
            const DWORD value { GetSysColor(index) };
            return make_ui_color(GetRValue(value), GetGValue(value), GetBValue(value));
        }

        // OS가 지금 쓰는 accent다 (DWM 창 색소, 0xAARRGGBB).
        // 읽지 못하면 빈 값이고 시스템 accent 항목이 서지 않는다 —
        // `u8"system"`을 고른 앱은 기본색으로 물러선다.
        [[nodiscard]] std::optional<ui_color> read_system_accent_color() noexcept
        {
            DWORD color { 0 };
            BOOL opaque_blend { FALSE };
            if (FAILED(DwmGetColorizationColor(&color, &opaque_blend)))
                return std::nullopt;
            // 알파는 합성이 무시하므로 그대로 넘긴다.
            return static_cast<ui_color>(color);
        }

        // 고대비 테마가 실제로 고른 색이다.
        // 고대비는 검정 바탕만이 아니라 흰 바탕·사용자 지정 색이 있어
        // 하드코딩하면 "고대비 흰색" 사용자에게 정반대 화면이 나온다.
        [[nodiscard]] high_contrast_colors read_high_contrast_colors() noexcept
        {
            return high_contrast_colors {
                .window_background = read_system_color(COLOR_WINDOW),
                .window_foreground = read_system_color(COLOR_WINDOWTEXT),
                .highlight_background = read_system_color(COLOR_HIGHLIGHT),
                .highlight_foreground = read_system_color(COLOR_HIGHLIGHTTEXT),
                .emphasis = read_system_color(COLOR_HOTLIGHT),
                .button_background = read_system_color(COLOR_BTNFACE),
                .button_foreground = read_system_color(COLOR_BTNTEXT),
                .disabled_foreground = read_system_color(COLOR_GRAYTEXT),
            };
        }

        // 주 창의 표면이다.
        // caption chrome 위에 셋을 더한다: host가 없을 때의 대체 화면(smoke),
        // tree에 등록된 caption 버튼 액션, 시간 예고 걸기.
        class main_surface final : public caption_surface
        {
        public:
            explicit main_surface(surface_context& context) noexcept
                : caption_surface { context }
            {}

        private:
            void prepare_frame(frame_state& state) override
            {
                if (app_host* const host { context_.host() }; host != nullptr)
                {
                    // 주 tree는 그릴 때마다 게시본을 집는다.
                    // 다른 표면은 frame의 목록이 실어 준다.
                    static_cast<void>(set_tree(host->acquire_ui_tree()));
                }
                else
                {
                    // host가 없으면(smoke) caption만 있는 화면을 그린다.
                    // 렌더러와 글꼴·글리프 경로가 실제와 같은 코드로 검증된다.
                    static_cast<void>(set_tree(std::make_shared<const ui_tree>(make_caption_tree(static_cast<float>(state.width), state.dpi_scale, context_.config().caption))));
                }
                state.tree = tree_.get();
                caption_surface::prepare_frame(state);
                context_.schedule_update_repaint(id_, state.interaction);
            }

            // 주 창의 그리기 실패는 프로세스의 실패다.
            // 다른 표면은 알리기만 하고 계속 산다.
            void on_render_failed(const std::u8string& error) override
            {
                context_.report_error(error);
                PostQuitMessage(1);
            }

            // 비클라이언트 클릭을 tree 안 caption 버튼에 등록된 액션으로 실행한다.
            // tree가 아직 없으면(host 없음, 첫 frame 이전) 같은 의미의 창 명령을 직접 낸다.
            [[nodiscard]] bool execute_caption_button(const WPARAM hit) override
            {
                const ui_element_id id { caption_button_element_id(caption_hover_from_hit(hit)) };
                if (id == ui_element_id {})
                    return false;

                const std::shared_ptr<const ui_tree> tree { context_.host() != nullptr ? context_.host()->acquire_ui_tree() : nullptr };
                if (tree != nullptr)
                {
                    const ui_element* const button { tree->find(id) };
                    const ui_action* const action { button != nullptr ? button->action(ui_trigger::left_click) : nullptr };
                    if (action != nullptr)
                    {
                        const rect_f bounds { button->bounds() };
                        for (input_action& result : (*action)(ui_action_context { id, bounds.x, bounds.y, false }))
                            context_.dispatch_action(std::move(result));
                        return true;
                    }
                }

                switch (hit)
                {
                case HTMINBUTTON:
                    context_.dispatch_action(input_action { ui_command::window_minimize });
                    return true;
                case HTMAXBUTTON:
                    context_.dispatch_action(input_action { ui_command::window_toggle_maximize });
                    return true;
                case HTCLOSE:
                    context_.dispatch_action(input_action { ui_command::window_close });
                    return true;
                default:
                    return false;
                }
            }
        };

        // 주 창의 런타임이다.
        // 창 클래스 등록·조립·표면 목록 대조·프로세스 수명을 맡고,
        // 표면들이 함께 보는 것은 `surface_context`로 연다
        // (win32-surface-design.md).
        class application_window final : public surface_context
        {
        public:
            application_window(const HINSTANCE instance, window_config config, const window_environment environment) noexcept
                : instance_ { instance }
                , config_ { std::move(config) }
                , environment_ { environment }
            {
                // 고른 글꼴에 없는 글자를 대신 그릴 해석기를 그리기 계층에 알린다.
                // 한 번 등록하고 이 창이 사라질 때 되돌린다.
                set_font_fallback(&font_resolver_);
                // 글자 그리기 품질은 OS의 글꼴 다듬기 설정을 따른다.
                // 바뀌면 WM_SETTINGCHANGE가 다시 읽는다.
                set_text_render_quality(read_text_render_quality());
                // OS accent로 시스템 키 컬러 항목을 세운다.
                // 바뀌면 WM_DWMCOLORIZATIONCOLORCHANGED가 다시 세운다.
                //  - 스레드들이 돌기 전이라 첫 게시부터 목록에 서 있다.
                set_system_accent(read_system_accent_color());
            }

            application_window(const application_window&) = delete;
            application_window(application_window&&) = delete;
            application_window& operator=(const application_window&) = delete;
            application_window& operator=(application_window&&) = delete;

            ~application_window() override
            {
                // TSF는 창보다 먼저 정리한다.
                // 조합 중이었다면 확정된 글이 logic으로 간다.
                main_.reset_text_input();
                // 메시지 루프를 돌지 않고 끝나는 길의 안전망이다 (smoke의 한 frame,
                // `run`의 실패 반환). 없으면 숨은 창과 그 창이 쥔 OLE drop 등록이
                // 프로세스에 남는다.
                //  - 이중 파괴가 되지 않는 근거는 `WM_NCDESTROY`가 `window_`를
                //    비운다는 것 하나다. 정상 종료는 이미 그 길을 지나왔다.
                if (window_ != nullptr)
                    DestroyWindow(window_);
                set_font_fallback(nullptr);
                // 이 창이 세운 것은 이 창이 거둔다 (글꼴 해석기와 같은 규칙).
                set_system_accent(std::nullopt);
            }

            // --- surface_context ---

            [[nodiscard]] app_host* host() noexcept override
            {
                return host_.get();
            }

            [[nodiscard]] const window_config& config() const noexcept override
            {
                return config_;
            }

            [[nodiscard]] interaction_policy* policy() noexcept override
            {
                return environment_.policy;
            }

            [[nodiscard]] IDCompositionDevice* composition_device(std::u8string& error) noexcept override
            {
                return composition_.acquire(error);
            }

            // 웹뷰가 알려 온 초점 신호를 우리 초점 기계로 옮긴다.
            //
            // **가둠에서 나가는 것은 그냥 Tab이다.** `Ctrl+Tab`을 우리 세계의 Tab으로
            // 옮기면 초점 순서·묶음·가둠이 이미 아는 길을 그대로 탄다 — 여기에
            // "다음 자리 찾기"를 새로 쓰지 않는다 (keyboard-focus-design.md).
            void report_webview_focus(const std::u8string& id, const std::u8string& anchor, const webview_focus_signal signal)
            {
                app_host* const host { host_.get() };
                if (host == nullptr)
                    return;
                const auto now { std::chrono::steady_clock::now() };
                switch (signal)
                {
                case webview_focus_signal::entered:
                    // 논리 초점을 자리표로 옮긴다. 클릭 진입은 raw input이 우리에게
                    // 오지 않아, 이것이 없으면 초점 테가 이전 자리에 남는다.
                    host->post_raw_input(access_focus_event { ui_element_id { ui_element_kind::webview, id }, anchor, now });
                    return;
                case webview_focus_signal::left:
                    // 거두지 않는다. 초점이 어디로 갔는지는 우리 쪽 사건이 말한다 —
                    // 여기서 지우면 우리 창을 눌러 옮긴 초점을 도로 지운다.
                    return;
                case webview_focus_signal::leave_forward:
                case webview_focus_signal::leave_backward:
                    // 창의 초점을 먼저 되찾는다. 그러지 않으면 자식 창이 초점을 쥔
                    // 채라 우리가 옮긴 논리 초점에 키가 오지 않는다.
                    if (window_ != nullptr)
                        static_cast<void>(SetFocus(window_));
                    host->post_raw_input(key_pressed_event {
                        .key = key_code::tab,
                        .shift = signal == webview_focus_signal::leave_backward,
                        .time = now,
                        .surface = anchor,
                    });
                    return;
                case webview_focus_signal::dismiss:
                    if (window_ != nullptr)
                        static_cast<void>(SetFocus(window_));
                    host->post_raw_input(key_pressed_event { .key = key_code::escape, .time = now, .surface = anchor });
                    return;
                }
            }

            [[nodiscard]] bool relay_webview_pointer(const std::u8string& surface, const UINT message, const WPARAM word_parameter, const int client_x, const int client_y) override
            {
                return webviews_.relay_pointer(surface, message, word_parameter, client_x, client_y);
            }

            void webview_pointer_left(const std::u8string& surface) override
            {
                webviews_.relay_pointer_left(surface);
            }

            void cancel_webview_pointer(const std::u8string& surface) override
            {
                webviews_.cancel_pointer(surface);
            }

            [[nodiscard]] bool relay_webview_pointer_input(const std::u8string& surface, const webview_pointer_input& input) override
            {
                return webviews_.relay_pointer_input(surface, input);
            }

            void cancel_webview_pointer_input(const std::u8string& surface, const std::uint32_t pointer_id) override
            {
                webviews_.cancel_pointer_input(surface, pointer_id);
            }

            [[nodiscard]] std::span<const pixel_rect> apply_webviews(
                const std::u8string& surface, const ui_tree* const tree, IDCompositionVisual* const underlay, const int client_width, const int client_height) override
            {
                webview_holes_.clear();
                if (tree == nullptr || last_frame_ == nullptr)
                    return webview_holes_;

                // 이 표면의 배율이다. 다른 배율로 배치된 tree는 아래에서 자리를 주지
                // 않는다 (표면의 배율과 tree의 배율은 같은 식 `dpi / 96`으로 난다).
                const window_surface* const anchor { anchor_surface(surface) };
                const float surface_scale { anchor != nullptr ? dpi_scale(anchor->dpi()) : 1.0f };

                // 지금 논리 초점이 어디인가. 자리표에 **닿는 순간**에만 페이지로
                // 넘기려면 직전 값과 견줘야 한다.
                ui_element_id state_focus {};
                if (app_host* const focus_host { host_.get() }; focus_host != nullptr)
                    state_focus = interaction_for_surface(focus_host->acquire_interaction(), surface).focused;

                for (const ui_webview& want : last_frame_->webviews)
                {
                    if (want.anchor != surface || want.id.empty())
                        continue;
                    const ui_element_id placeholder_id { ui_element_kind::webview, want.id };
                    // **자리표가 아니면 자리도 없다.** id만 같은 다른 element는 자리를
                    // 말하지 못한다 — 자리·가림·배율을 전부 자리표에서만 읽는다.
                    const auto* const placeholder { dynamic_cast<const webview_element*>(tree->find(placeholder_id)) };
                    const std::optional<rect_f> visible { placeholder != nullptr ? tree->visible_bounds(*placeholder) : std::nullopt };
                    // 배율은 **자리표가 배치된 값**이다. 표면의 `dpi()`가 아니다 —
                    // 자리와 배율은 같은 tree에서 나온 한 쌍이라야 한다 (webview_layout.h).
                    const webview_layout layout {
                        plan_webview_layout(visible, client_width, client_height, occluded(*tree, placeholder, visible), placeholder != nullptr ? placeholder->scale() : 1.0f),
                    };
                    // **이 표면의 배율로 배치된 tree만 자리를 준다.** `WM_DPICHANGED`
                    // 직후 아직 옛 배율의 tree를 **새 크기의 client**에 그리는 frame이
                    // 하나 있다. 그 tree의 자리는 client에 잘려 크기가 바뀌므로 그대로
                    // 주면 옛 배율의 좁아진 자리로 한 번, 새 tree가 오면 다시 한 번
                    // 리플로한다. 그 frame에는 자리를 건드리지 않는다 — 웹뷰는 옛 자리·
                    // 옛 배율 그대로이고, 아래의 구멍은 그 자리를 client에 자른 것이라
                    // 웹뷰 밖으로 나가지 않는다. 자리표가 없는 frame(감추기)은 배율과
                    // 무관하게 그대로 준다.
                    const float tree_scale { placeholder != nullptr ? placeholder->scale() : surface_scale };
                    const bool arranged_for_this_surface { tree_scale - surface_scale < 0.001f && surface_scale - tree_scale < 0.001f };
                    if (arranged_for_this_surface)
                        webviews_.apply_layout(want.id, layout, underlay);
                    // **초점이 자리표에 닿으면 페이지로 넘긴다.**
                    //
                    // Tab이 자리표를 하나의 자리로 보고 거기 서면, 그 자리는 그릴
                    // 것이 없는 빈 상자다 — 사용자가 보기에 초점이 사라진다. 그때
                    // 페이지 안으로 들여보내야 그 다음 Tab이 페이지 안을 돈다.
                    //  - 웹뷰가 이미 쥐고 있으면 다시 넘기지 않는다. 매 frame 부르면
                    //    페이지 안에서 옮긴 초점이 첫 자리로 되돌아간다.
                    if (last_focus_ != placeholder_id && placeholder_id == state_focus)
                        static_cast<void>(webviews_.move_focus_in(want.id, false));
                    // **웹뷰가 실제로 서 있을 때만 비운다.** 서지 않은 자리를 비우면
                    // 그 아래에 아무것도 없어 바탕 화면이 비친다 — 자리표의
                    // placeholder가 보여야 하는 자리다 (webview_element.h).
                    if (layout.punch_hole && webviews_.standing(want.id))
                        webview_holes_.push_back(pixel_rect { layout.x, layout.y, layout.width, layout.height });
                }
                last_focus_ = state_focus;
                return webview_holes_;
            }

            [[nodiscard]] bool pointer_capture_active() const noexcept override
            {
                return pointer_capture_active_;
            }

            void set_pointer_capture_active(const bool active) noexcept override
            {
                pointer_capture_active_ = active;
            }

            void report_error(const std::u8string& error) noexcept override
            {
                report_runtime_error(error);
            }

            // dismiss_popups는 popup 구역에, dispatch_action과
            // schedule_update_repaint는 아래 구현부에 있다.

            // 주 창의 배율이다.
            // 표면과 popup 자리 계산이 같은 값을 봐야 하므로 한 곳에서 바꾼다.
            void set_dpi(const std::uint32_t dpi) noexcept
            {
                dpi_ = dpi;
                main_.set_dpi(dpi);
            }

            [[nodiscard]] bool create(std::u8string& error)
            {
                // 잘못된 터치 설정은 시작 실패다. 조용히 기본값으로 물러서면 앱이
                // 준 값이 쓰이는 줄 안다.
                if (valid_touch_gesture_config(config_.touch) == false)
                {
                    error = u8"The touch gesture configuration is invalid.";
                    return false;
                }
                WNDCLASSEXW window_class {};
                window_class.cbSize = sizeof(window_class);
                window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
                window_class.lpfnWndProc = &application_window::static_window_procedure;
                window_class.hInstance = instance_;
                if (config_.icon_resource != 0)
                    window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(config_.icon_resource));
                if (window_class.hIcon == nullptr)
                    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
                window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                window_class.lpszClassName = config_.class_name.c_str();
                window_class.hIconSm = window_class.hIcon;
                if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                {
                    error = u8"Failed to register the Win32 window class.";
                    return false;
                }

                std::wstring title { L"luil" };
                if (const auto converted { utf8_to_utf16(config_.caption.title) }; converted.value.has_value() && converted.value->empty() == false)
                    title = *converted.value;

                // 파일 드롭은 표면의 IDropTarget이 받는다 — 확장 스타일의 몫이 아니다.
                main_.set_extended_style(WS_EX_APPWINDOW);
                main_.set_caption(config_.caption);
                main_.set_minimum_client_size(static_cast<float>(config_.minimum_client_width), static_cast<float>(config_.minimum_client_height));
                // 자리를 정하지 않았으면 OS에 맡긴다.
                // 정했으면 그 값은 이미 물리 픽셀이라 배율을 곱하지 않는다.
                const int initial_x { config_.initial_position.has_value() ? config_.initial_position->x : CW_USEDEFAULT };
                const int initial_y { config_.initial_position.has_value() ? config_.initial_position->y : CW_USEDEFAULT };
                // 창은 통상 모습으로 선다.
                // 저장된 배치가 전체 화면이더라도 그것은 창이 보인 **뒤에** 들어가는
                // 상태다 (`apply_requested_window_placement`).
                window_ = CreateWindowExW(WS_EX_APPWINDOW, config_.class_name.c_str(), title.c_str(), window_style_for(config_.caption.buttons, window_display_mode::normal), initial_x, initial_y,
                    config_.initial_width, config_.initial_height, nullptr, nullptr, instance_, this);

                if (window_ == nullptr)
                {
                    error = u8"Failed to create the Win32 window.";
                    return false;
                }

                if (caption_surface::remove_system_caption(window_, error) == false)
                {
                    DestroyWindow(window_);
                    window_ = nullptr;
                    return false;
                }
                caption_surface::apply_dwm_frame(window_, window_display_mode::normal);
                set_dpi(GetDpiForWindow(window_));
                // 요청 크기는 논리 96 DPI 기준이다.
                // 창이 어느 모니터에 뜰지는 만들어 봐야 알므로, 만든 뒤 그 모니터의
                // 배율을 곱해 다시 잡는다 — 안 하면 150% 모니터에서 창이 2/3 크기로 뜬다.
                // client == window rect(WM_NCCALCSIZE가 0을 돌려준다)라 프레임 보정은 없다.
                if (dpi_ != 96)
                    SetWindowPos(window_, nullptr, 0, 0, scale_for_dpi(config_.initial_width), scale_for_dpi(config_.initial_height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                RECT client {};
                GetClientRect(window_, &client);
                const renderer_fault_injection fault {
                    .at_creation = config_.simulate_direct3d_failure,
                    .after_frames = config_.simulate_direct3d_loss_after_frames,
                };
                if (main_.create_renderer(config_.renderer, fault, client.right - client.left, client.bottom - client.top, error) == false)
                {
                    // 캡션 실패와 같은 정리다. 그냥 돌아가면 숨은 창이 살아남고
                    // `GWLP_USERDATA`가 곧 사라질 이 객체를 계속 가리킨다.
                    //  - `DestroyWindow`가 태우는 `WM_DESTROY`에서 `main_.detach_window()`가
                    //    돌아 표면이 등록한 OLE drop 대상까지 함께 풀린다.
                    DestroyWindow(window_);
                    window_ = nullptr;
                    return false;
                }

                // smoke test는 스레드 없이 한 frame만 그린다.
                // driver가 있을 때만 host(스레드와 채널)를 조립한다.
                if (config_.smoke_test == false && environment_.driver != nullptr)
                {
                    app_host::config host_config {};
                    // wake는 창 메시지 게시로 구현한다 (WM_APP 계열).
                    // host는 창을 모르고 이 불투명 함수만 받는다.
                    // 내용(명령·요청)은 host의 큐가 나르고 창 메시지는 신호만 싣는다.
                    // snapshot은 게시 성공 여부를 돌려준다 — 큐 포화로 실패한
                    // 신호는 slot이 되돌려 다음 게시가 다시 신호한다.
                    const HWND wake_window { window_ };
                    host_config.wake.snapshot = [wake_window] { return PostMessageW(wake_window, snapshot_wake_message, 0, 0) != FALSE; };
                    host_config.wake.ui_command = [wake_window] { PostMessageW(wake_window, ui_command_request_message, 0, 0); };
                    host_config.wake.app_ui_command = [wake_window] { PostMessageW(wake_window, app_ui_command_request_message, 0, 0); };
                    host_config.wake.clipboard = [wake_window] { PostMessageW(wake_window, clipboard_request_message, 0, 0); };
                    // 더블 클릭 임계는 사용자의 시스템 설정을 따른다.
                    host_config.interaction.double_click_time = std::chrono::milliseconds { GetDoubleClickTime() };
                    host_config.interaction.touch = config_.touch;
                    // 포인터 x를 글 안의 offset으로 옮기려면 글자 폭을 재야 한다.
                    // input thread에서 불리지만 글꼴 registry는 그리기와 같은 값을 주므로
                    // caret이 꽂히는 자리와 화면의 글자가 어긋나지 않는다.
                    host_config.interaction.measure_text = [](const std::u8string_view text, const float pixel_size) {
                        const SkFont font { configured_ui_typeface(), pixel_size };
                        return measure_text(text, font);
                    };
                    host_ = std::make_unique<app_host>(std::move(host_config), *environment_.driver, environment_.policy);

                    // IME 조합을 박스 안에서 하려면 문서를 OS에 열어 줘야 한다.
                    // 만들지 못하면 조용히 없이 간다.
                    //  - 조합은 시스템 창이 그리고 확정 글만 WM_CHAR로 온다.
                    main_.create_text_input();
                }
                return true;
            }

            [[nodiscard]] int run()
            {
                if (host_ == nullptr)
                {
                    // 통상은 한 frame이다. 런타임 손실을 주입했으면 그 손실이
                    // 실제로 오는 frame까지 그린다 — 손실 **뒤의** frame이 화면에
                    // 닿는지가 이 주입점이 묻는 전부이므로, 거기까지 가지 않으면
                    // 아무것도 재지 못한다.
                    const int frames { config_.simulate_direct3d_loss_after_frames > 0 ? config_.simulate_direct3d_loss_after_frames + 1 : 1 };
                    for (int frame = 0; frame < frames; ++frame)
                    {
                        std::u8string error {};
                        if (main_.render(error) == false)
                        {
                            report_runtime_error(error);
                            return 1;
                        }
                    }
                    return 0;
                }

                post_window_metrics();
                if (environment_.delegate != nullptr)
                    environment_.delegate->on_started(*host_);

                // 어떻게 보일지는 **띄우는 쪽이** 정한다.
                // `SW_SHOWDEFAULT`는 STARTUPINFO의 `wShowWindow`를 그대로 따르므로,
                // 자동화가 `SW_SHOWNOACTIVATE`로 실행하면 창이 사람의 포커스를 뺏지
                // 않고 뜬다 (accessibility-design.md). 여기를 고정 값으로 바꾸면
                // 그 길이 조용히 막힌다 — 창은 그대로 뜨고 포커스만 넘어간다.
                ShowWindow(window_, SW_SHOWDEFAULT);
                UpdateWindow(window_);
                MSG message {};
                BOOL received {};
                while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0)
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                // -1은 오류다. 이 호출부(hWnd 없음, 유효한 MSG)에서는 문서화된
                // 조건이 성립하지 않는 방어 보강이다 — 그래도 WM_QUIT의 종료
                // 코드와 섞이면 오류가 성공 종료로 위장되므로 갈라 둔다.
                if (received == -1)
                {
                    report_runtime_error(make_hresult_error(u8"Failed to retrieve a message from the message loop.", HRESULT_FROM_WIN32(GetLastError())));
                    return 1;
                }
                return static_cast<int>(message.wParam);
            }

        private:
            static LRESULT CALLBACK static_window_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
            {
                application_window* self {
                    reinterpret_cast<application_window*>(GetWindowLongPtrW(window, GWLP_USERDATA)),
                };

                if (message == WM_NCCREATE)
                {
                    const auto* creation { reinterpret_cast<const CREATESTRUCTW*>(long_parameter) };
                    self = static_cast<application_window*>(creation->lpCreateParams);
                    self->window_ = window;
                    self->main_.attach_window(window);
                    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }

                if (self != nullptr)
                    return self->window_procedure(message, word_parameter, long_parameter);
                return DefWindowProcW(window, message, word_parameter, long_parameter);
            }

            // 주 창의 메시지다.
            // 예외는 User32 경계를 넘지 못한다 — 사용자 delegate와 할당이 던진
            // 것은 여기서 보고하고 안전한 기본값으로 물러선다
            // (`deliver_file_drop`이 세운 불변식을 경계 전체로 넓힌 것이다).
            //  - 커널 콜백 스택에서 배달되는 중첩 메시지는 경계를 되감을 수조차
            //    없다. MSVC에서 try 진입 비용은 사실상 0이다.
            LRESULT window_procedure(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
            {
                try
                {
                    return handle_window_message(message, word_parameter, long_parameter);
                }
                catch (...)
                {
                    report_runtime_error(u8"An exception escaped a window message handler.");
                    // 앱 신호 메시지(WM_APP 계열)는 기본 처리가 없어 0으로 답한다.
                    // 나머지는 OS 기본 처리가 창을 일관된 상태로 이끈다.
                    if (message >= WM_APP)
                        return 0;
                    return DefWindowProcW(window_, message, word_parameter, long_parameter);
                }
            }

            // 주 창의 메시지 처리 본체다.
            // 런타임(조립·스레드·프로세스 수명)만 여기서 보고, 나머지는 표면 계층이
            // 이어 본다 — caption chrome → 표면 공통 (win32-surface-design.md).
            LRESULT handle_window_message(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
            {
                switch (message)
                {
                case snapshot_wake_message: {
                    apply_requested_window_placement();
                    // 창을 먼저 맞춘다.
                    // popup의 앵커 표면이 있어야 그 위에 배치할 수 있다.
                    // 대조 결과는 표면별 현재 콘텐츠와 게시된 콘텐츠의 차이를 반영한다.
                    std::vector<surface_content> contents { synchronize_windows() };
                    synchronize_webviews();
                    for (surface_content& popup_content : synchronize_popups())
                        contents.push_back(std::move(popup_content));
                    contents.push_back(main_surface_content());
                    // 무엇을 다시 그릴지 고르는 규칙은 한 곳에 있다.
                    // 상호작용·외양 변화는 tree 밖에서 오므로 전부를 깨운다 (5.4).
                    //  - **대조를 마친 뒤에 묻는다.** 이 판정도 발행본을 집으므로
                    //    인자 자리에 두면 (인자의 평가 순서는 정해져 있지 않다)
                    //    대조가 본 frame보다 앞선 것을 보고 마지막 외양을 갱신할 수
                    //    있다. 그러면 이번 게시의 테마 변화를 이 wake가 놓친다.
                    const bool every_surface { posting_changes_every_surface() };
                    invalidate_surfaces(surfaces_to_repaint(contents, every_surface));
                    // **예고가 꺼져 있으면 여기서 켠다.** 표면을 골라 깨우는 순간
                    // "게시마다 주 창이 그려지며 다시 건다"는 전제가 무너졌다 —
                    // popup은 자기 render에서 예고를 걸지 않으므로(5.2), popup의
                    // tree만 바뀐 게시는 timer가 꺼져 있으면 아무도 켜지 않아
                    // 그 popup의 caret·회전 표시가 첫 그리기 뒤 그대로 멈춘다.
                    //  - 이미 걸린 timer는 **건드리지 않는다.** 여기서 다시 걸면
                    //    게시가 잦을 때(포인터가 움직이는 동안 발행본이 계속 온다)
                    //    tick이 매번 뒤로 밀려 애니메이션이 굶는다. 걸려 있는
                    //    동안에는 WM_TIMER의 되걸기가 새 tree의 예고를 집으므로
                    //    늦어도 다음 tick에 사슬에 붙는다.
                    if (update_timer_armed_ == false)
                        arm_update_timer(nullptr, nullptr);
                    return 0;
                }
                case ui_command_request_message:
                    // 명령은 큐에서 넣은 순서대로 꺼내 실행한다.
                    // 신호 하나가 유실돼도 내용은 큐에 남아 다음 신호가 함께 실행한다.
                    if (host_ != nullptr)
                        for (const ui_command command : host_->take_ui_commands())
                            execute_ui_command(command);
                    return 0;
                case app_ui_command_request_message:
                    if (host_ != nullptr && environment_.delegate != nullptr)
                        for (const app_ui_command& command : host_->take_app_ui_commands())
                            environment_.delegate->execute_app_ui_command(*host_, command);
                    return 0;
                case clipboard_request_message:
                    if (host_ != nullptr)
                        for (const clipboard_request& request : host_->take_clipboard_requests())
                            execute_clipboard_request(request);
                    return 0;
                case WM_TIMER:
                    if (word_parameter == update_timer_id)
                    {
                        KillTimer(window_, word_parameter);
                        update_timer_armed_ = false;
                        // 이 시각을 답한 표면만 깨운다.
                        // 보조 창 하나의 caret 깜빡임이나 회전 표시가 모든 창을
                        // 33ms 주기로 끌고 가던 자리다 (rendering.md).
                        invalidate_surfaces(timer_wake_);
                        // 다음 예고는 **여기서** 다시 건다. 깨운 표면이 실제로
                        // 그려지는지에 기대지 않는다 — 최소화된 창은
                        // `InvalidateRect`에도 `WM_PAINT`가 오지 않고 popup은 자기
                        // render에서 예고를 걸지 않으므로, 그쪽의 render를 기다리면
                        // 예고의 사슬이 통째로 끊겨 **다른 창의 caret이 멈춘다.**
                        //  - 주 창이 최소화면 소유된 보조 창·popup도 함께 감춰져
                        //    보이는 표면이 없다. 그때만 사슬을 놓고, 복원의
                        //    `WM_SIZE`가 다시 그리며 그 render가 이어 붙인다.
                        if (IsIconic(window_) == FALSE)
                            arm_update_timer(nullptr, nullptr);
                        return 0;
                    }
                    break;
                case WM_CLOSE:
                    // 스레드를 모두 정리한 뒤에 창을 파괴한다.
                    // 창 배치는 종료 신호보다 먼저 게시해야 같은 채널의 순서로 반영된다.
                    if (host_ != nullptr)
                    {
                        post_window_placement();
                        host_->shutdown();
                        host_.reset();
                    }
                    break;
                case WM_QUERYENDSESSION:
                    // 로그오프·재시작·업데이트 재부팅은 WM_CLOSE 없이 창을 거둔다.
                    // 저장은 WM_ENDSESSION에서 하고 여기서는 종료에 동의만 한다.
                    return TRUE;
                case WM_ENDSESSION:
                    // wParam FALSE는 다른 앱이 거부해 종료가 취소된 것이다.
                    // TRUE면 WM_CLOSE와 같은 저장·정리 경로를 밟는다 —
                    // 안 하면 배치 저장·종료 저장 설계가 가장 흔한 강제 종료에서 통째로 건너뛰어진다.
                    // shutdown의 3초 상한은 세션 종료 유예(기본 5초) 안이다.
                    if (word_parameter != FALSE && host_ != nullptr)
                    {
                        post_window_placement();
                        host_->shutdown();
                        host_.reset();
                    }
                    return 0;
                case WM_ACTIVATE:
                    if (LOWORD(word_parameter) == WA_INACTIVE)
                    {
                        main_.update_caption_hover(caption_button_hover::none);
                        // 다른 창으로 넘어간 것도 popup 밖 클릭과 같은 뜻이다.
                        static_cast<void>(dismiss_popups(popup_dismiss_reason::activation_changed));
                    }
                    break;
                case WM_SIZE:
                    main_.update_caption_hover(caption_button_hover::none);
                    // 최대화 ↔ 복원 전환만 배치로 알린다.
                    // 끌어서 크기를 바꾸는 동안 에도 SIZE_RESTORED가
                    // 연속으로 오므로 상태가 바뀐 경우로 한정 한다.
                    //  - **전체 화면으로 들어가는 길은 여기서 세지 않는다.** 그
                    //    `SetWindowPos`도 `SIZE_RESTORED`로 오므로, 최대화 창에서
                    //    들어가면 이 판정이 "최대화가 풀렸다"고 보아 갈무리해 둔 것과
                    //    다른 배치를 알린다. 그 전환의 보고는 명령이 직접 한다.
                    if (main_.fullscreen() == false && (word_parameter == SIZE_MAXIMIZED || word_parameter == SIZE_RESTORED))
                    {
                        const bool maximized { word_parameter == SIZE_MAXIMIZED };
                        if (maximized != posted_maximized_)
                        {
                            posted_maximized_ = maximized;
                            post_window_placement();
                        }
                    }
                    if (word_parameter != SIZE_MINIMIZED)
                    {
                        // 크기 변경도 popup의 닫힘 계기다.
                        static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_resized));
                        std::u8string error {};
                        if (main_.resize_renderer(LOWORD(long_parameter), HIWORD(long_parameter), error) == false)
                        {
                            report_runtime_error(error);
                            PostQuitMessage(1);
                        }
                        post_window_metrics();
                        InvalidateRect(window_, nullptr, FALSE);
                    }
                    return 0;
                case WM_EXITSIZEMOVE:
                    // 이동·크기 조절이 끝난 시점의 배치다.
                    // 앱이 저장할 값을 알고 있어야 한다.
                    post_window_placement();
                    return 0;
                case WM_DPICHANGED: {
                    set_dpi(HIWORD(word_parameter));
                    // **전체 화면 창에는 제안 사각형을 쓰지 않는다.** OS가 주는 것은
                    // 지금 창 사각형에 배율 비를 곱한 값이라, 1920x1080 모니터가
                    // 100%에서 150%로 바뀌면 모니터는 그대로인데 창만 2880x1620이
                    // 된다. 그 창은 여전히 전체 화면이라 판정이 어디나 `HTCLIENT`고
                    // `WS_THICKFRAME`도 없어, 사용자가 넘친 창을 되돌릴 길이 전체
                    // 화면 해제밖에 없다. 덮을 자리를 정하는 것은 배율이 아니라
                    // 모니터이므로 들어갈 때와 같은 유도로 다시 구한다
                    // (`fullscreen_bounds_for`).
                    //  - 배율 값 갱신·metrics 게시·popup 다시 맞추기는 두 길이
                    //    똑같이 지난다. 달라지는 것은 사각형을 어디서 얻는가뿐이다.
                    if (main_.fullscreen())
                        static_cast<void>(main_.reapply_fullscreen_bounds());
                    else
                    {
                        const auto* suggested_rectangle { reinterpret_cast<const RECT*>(long_parameter) };
                        SetWindowPos(window_, nullptr, suggested_rectangle->left, suggested_rectangle->top, suggested_rectangle->right - suggested_rectangle->left,
                            suggested_rectangle->bottom - suggested_rectangle->top, SWP_NOACTIVATE | SWP_NOZORDER);
                    }
                    post_window_metrics();
                    // 배율이 바뀌면 popup의 물리 자리·크기도 다시 계산해야 한다.
                    //  - 이 대조도 새 tree를 표면에 옮겨 담으므로 **결과를 버리면
                    //    안 된다.** 게시의 wake가 아직 큐에 있는 동안 이 메시지가
                    //    끼어들면, 여기서 삼킨 tree를 그 wake는 "이미 같다"고 보아
                    //    popup이 낡은 채로 남는다.
                    invalidate_surfaces(surfaces_to_repaint(synchronize_popups(), false));
                    InvalidateRect(window_, nullptr, FALSE);
                    return 0;
                }
                case WM_DISPLAYCHANGE:
                    // 해상도나 모니터 구성이 바뀌었다.
                    //
                    // **전체 화면 창만 다시 앉힌다.** 그 사각형은 들어갈 때 한 번
                    // 잰 모니터의 것이라, 그 뒤에 화면이 1920x1080에서 1280x720으로
                    // 줄거나 덮고 있던 모니터가 빠지면 창만 옛 사각형에 남는다 —
                    // 여전히 테두리가 없고 판정은 어디나 `HTCLIENT`라 크기를 되돌릴
                    // 길이 전체 화면 해제밖에 없고, "전체 화면은 rcMonitor를 덮는
                    // 창"이라는 말도 들어선 순간에만 참이 된다
                    // (docs/concepts/window.md).
                    //  - 전체 화면이 아니면 **아무것도 하지 않는다.** 통상 창을
                    //    화면이 바뀔 때마다 옮기는 것은 사용자의 몫이고, 화면 밖으로
                    //    나간 창을 다듬는 것은 이미 OS가 한다.
                    if (main_.fullscreen())
                        static_cast<void>(main_.reapply_fullscreen_bounds());
                    return 0;
                case WM_FONTCHANGE:
                    // 글꼴이 설치·삭제되면 캐시의 해석 결과를 비운다.
                    // 조회 결과에 소유권이 포함되므로 이미 사용하는 typeface는 살아 있다.
                    clear_font_caches();
                    invalidate_all_surfaces();
                    return 0;
                case WM_SETTINGCHANGE:
                case WM_THEMECHANGED:
                    // OS 테마가 바뀌면 `system` 선호가 가리키는 팔레트도 바뀐다.
                    // popup·보조 창 표면도 새 팔레트로 다시 칠한다.
                    system_prefers_light_ = read_system_prefers_light_theme();
                    high_contrast_ = read_high_contrast_enabled();
                    set_text_render_quality(read_text_render_quality());
                    invalidate_all_surfaces();
                    return 0;
                case WM_DWMCOLORIZATIONCOLORCHANGED:
                    // OS accent가 바뀌었다.
                    // wParam에도 새 색이 실리지만 읽는 길은 시작과 같은 함수 하나로 둔다.
                    // `u8"system"`을 고른 화면은 다음 그리기부터 새 색이다.
                    set_system_accent(read_system_accent_color());
                    invalidate_all_surfaces();
                    return 0;
                case WM_DESTROY:
                    // **웹뷰가 가장 먼저다.** 웹뷰는 표면 렌더러가 소유한 visual에
                    // 붙어 있고 배출구가 `app_host`를 잡고 있어, 그 둘보다 늦게
                    // 정리되면 이미 놓인 것을 만진다.
                    webviews_.shutdown();
                    // WM_CLOSE를 거치지 않은 파괴 경로에서도 스레드를 먼저 정리한다.
                    if (host_ != nullptr)
                    {
                        host_->shutdown();
                        host_.reset();
                    }
                    destroy_all_popups();
                    destroy_all_secondary_windows();
                    main_.detach_window();
                    PostQuitMessage(0);
                    return 0;
                case WM_NCDESTROY: {
                    // 창의 마지막 메시지다.
                    // 역참조 고리를 끊어 뒤에 오는 메시지가 곧 사라질 이 객체를
                    // 다시 찾지 않게 하고, 소멸자의 안전망이 이미 파괴된 창을
                    // 두 번 파괴하지 않도록 `window_`도 함께 비운다.
                    //  - 삼키지 않는다. 창의 마지막 정리는 기본 처리의 몫이고,
                    //    caption·표면 계층은 이 메시지를 보지 않으므로 곧장 넘긴다.
                    const HWND dying { window_ };
                    window_ = nullptr;
                    SetWindowLongPtrW(dying, GWLP_USERDATA, 0);
                    return DefWindowProcW(dying, message, word_parameter, long_parameter);
                }
                default:
                    break;
                }

                if (const std::optional<LRESULT> handled { main_.handle_caption_message(message, word_parameter, long_parameter) }; handled.has_value())
                    return *handled;
                if (const std::optional<LRESULT> handled { main_.handle_surface_message(message, word_parameter, long_parameter) }; handled.has_value())
                    return *handled;
                return DefWindowProcW(window_, message, word_parameter, long_parameter);
            }

            // 수락한 element가 없는 파일 드롭은 경로 순서대로 delegate에 전달한다.
            // delegate가 true를 반환하면 나머지 경로는 전달하지 않는다.
            void deliver_file_drop(const std::vector<std::u8string>& paths) override
            {
                if (host_ == nullptr || environment_.delegate == nullptr)
                    return;
                try
                {
                    for (const std::u8string& path : paths)
                        if (environment_.delegate->on_file_dropped(*host_, path))
                            return;
                }
                catch (...)
                {
                    // WndProc 경계를 예외가 넘어가지 않게 한다.
                }
            }

            // 액션 목록을 통상 규칙대로 배분한다 (caption 버튼·TSF 경로).
            void dispatch_action(input_action action) override
            {
                if (auto* const message { std::get_if<app_message>(&action) }; message != nullptr)
                {
                    if (host_ != nullptr && message->empty() == false)
                        host_->post_app_message(std::move(*message));
                    return;
                }
                if (const auto* const command { std::get_if<ui_command>(&action) }; command != nullptr)
                {
                    execute_ui_command(*command);
                    return;
                }
                if (const auto* const app_command { std::get_if<app_ui_command>(&action) }; app_command != nullptr)
                {
                    if (host_ != nullptr && environment_.delegate != nullptr)
                        environment_.delegate->execute_app_ui_command(*host_, *app_command);
                    return;
                }
                if (auto* const copy { std::get_if<clipboard_copy_request>(&action) }; copy != nullptr)
                {
                    execute_clipboard_request(clipboard_request { std::move(*copy) });
                    return;
                }
                if (const auto* const paste { std::get_if<clipboard_paste_request>(&action) }; paste != nullptr)
                {
                    execute_clipboard_request(clipboard_request { *paste });
                    return;
                }
                if (const auto* const shot { std::get_if<capture_request>(&action) }; shot != nullptr)
                {
                    execute_capture(*shot);
                    return;
                }
                if (const auto* const record { std::get_if<record_request>(&action) }; record != nullptr)
                    execute_record(*record);
            }

            // 찍기와 같은 자리다 — 실패해도 창을 끝내지 않고 알리기만 한다.
            void execute_record(const record_request& request)
            {
                window_surface* const target { surface_for_capture(request.surface) };
                if (target == nullptr)
                {
                    report_runtime_error(u8"The recording target surface is gone.");
                    return;
                }
                std::u8string error {};
                const bool done { request.command == record_command::start ? target->start_recording(request.path, error) : target->stop_recording(error) };
                if (done == false)
                    report_runtime_error(error);
            }

            // 찍기는 실패해도 창을 끝내지 않는다. 화면을 남기려던 것이지
            // 화면을 세우려던 것이 아니라, 못 남겼다고 앱이 죽으면 손해가 크다.
            void execute_capture(const capture_request& request)
            {
                window_surface* const target { surface_for_capture(request.surface) };
                if (target == nullptr)
                {
                    report_runtime_error(u8"The capture target surface is gone.");
                    return;
                }
                if (std::u8string error {}; target->capture(request.path, error) == false)
                    report_runtime_error(error);
            }

            // 빈 표식은 주 창이다. 보조 창과 popup은 자기 id로 찾는다.
            [[nodiscard]] window_surface* surface_for_capture(const std::u8string& surface)
            {
                if (surface.empty() || surface == main_.id())
                    return &main_;
                for (const std::unique_ptr<secondary_surface>& window : windows_)
                {
                    if (window->id() == surface)
                        return window.get();
                }
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                {
                    if (popup->id() == surface)
                        return popup.get();
                }
                return nullptr;
            }

            void execute_ui_command(const ui_command command)
            {
                switch (command)
                {
                case ui_command::window_minimize:
                    PostMessageW(window_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    return;
                case ui_command::window_toggle_maximize:
                    PostMessageW(window_, WM_SYSCOMMAND, IsZoomed(window_) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    return;
                case ui_command::window_toggle_fullscreen:
                    // 최소화·최대화·닫기와 달리 OS에 맡길 명령이 없다. 스타일·자리·
                    // 갈무리를 우리가 함께 바꾸는 상태 전환이라 표면이 그 자리에서 한다.
                    //
                    // 배치를 알리는 것도 여기다. 들어가고 나오는 것 자체가 **저장할
                    // 값의 변화**이고(전체 화면 여부는 배치에 실려 저장된다), 이 전환의
                    // `WM_SIZE`는 최대화 전환으로 세지 않아 아무도 알리지 않는다.
                    if (main_.set_fullscreen(main_.fullscreen() == false))
                        post_window_placement();
                    return;
                case ui_command::window_close:
                    PostMessageW(window_, WM_CLOSE, 0, 0);
                    return;
                }
            }

            // 종료 직전·크기 조절 뒤·전체 화면 전환의 창 배치를 앱에 알린다.
            // 최대화·최소화 상태 에서도 `rcNormalPosition`이 복원 크기를 담으므로 그대로 보낸다.
            //  - **전체 화면인 동안은 관측값을 쓸 수 없다.** 들어갈 때의
            //    `SetWindowPos`가 `rcNormalPosition`을 모니터 사각형으로 덮어썼기
            //    때문이다. 그때 알릴 것은 표면이 갈무리해 둔 "돌아갈 자리"이고,
            //    무엇을 고를지는 `placement_to_report`가 정한다 (window_mode.h).
            void post_window_placement() noexcept
            {
                if (host_ == nullptr || environment_.delegate == nullptr)
                    return;
                WINDOWPLACEMENT placement {};
                placement.length = sizeof(placement);
                if (GetWindowPlacement(window_, &placement) == FALSE)
                    return;

                window_placement observed {};
                observed.x = placement.rcNormalPosition.left;
                observed.y = placement.rcNormalPosition.top;
                observed.width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
                observed.height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
                observed.maximized = IsZoomed(window_) != FALSE;
                const window_placement value { placement_to_report(observed, main_.placement_before_fullscreen()) };
                if (value.valid() == false)
                    return;
                // delegate는 noexcept가 아니다.
                // 여기서 삼키지 않으면 noexcept 경계가 예외를 즉사로 만든다.
                try
                {
                    if (app_message message { environment_.delegate->make_window_placement_message(value) }; message.empty() == false)
                        host_->post_app_message(std::move(message));
                }
                catch (...)
                {
                    report_runtime_error(u8"The window placement delegate threw an exception.");
                }
            }

            // 앱이 요청한 배치를 한 번만 적용한다.
            // frame마다 창을 옮기지 않도록 게시 번호가 바뀐 경우에만 움직인다.
            void apply_requested_window_placement()
            {
                if (host_ == nullptr)
                    return;
                // **창이 보인 뒤에만 적용한다.** 전체 화면 요청은 스타일과 자리를 함께
                // 바꾸는데, 아직 `ShowWindow`를 지나지 않은 창에 그것을 하면 뒤따르는
                // `ShowWindow(SW_SHOWDEFAULT)`가 그 자리를 다시 잡는다. 게시 번호를
                // 아직 소비하지 않았으므로 다음 게시가 그대로 다시 시도한다.
                if (IsWindowVisible(window_) == FALSE)
                    return;
                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                if (frame == nullptr || frame->window_placement_revision == applied_window_placement_revision_)
                    return;

                applied_window_placement_revision_ = frame->window_placement_revision;
                if (frame->window_placement_request.has_value() == false)
                    return;

                const window_placement& requested { *frame->window_placement_request };
                // 너무 작은 값은 창을 사실상 못 쓰게 만든다.
                // 저장이 깨진 경우의 방어다.
                if (requested.valid() == false || requested.width < scale_for_dpi(config_.minimum_client_width) || requested.height < scale_for_dpi(config_.minimum_client_height))
                    return;
                // 좌표에 크기를 더한 끝이 화면 좌표 범위를 넘는 값도 깨진 저장이다.
                // 아래 int 덧셈이 넘치면 화면 밖 검사에 닿기 전에 미정의 동작이라
                // 그 전에 거른다 (작은 값과 같은 방식으로 요청을 무시한다).
                if (placement_in_screen_range(requested) == false)
                    return;

                RECT bounds {
                    requested.x,
                    requested.y,
                    requested.x + requested.width,
                    requested.y + requested.height,
                };
                // 모니터 구성이 바뀌어 저장된 위치가 화면 밖이면 크기만 적용한다.
                if (MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL) == nullptr)
                {
                    RECT current {};
                    if (GetWindowRect(window_, &current) == FALSE)
                        return;
                    // 물러선 자리에도 같은 덧셈이 있다 — 지금 자리 기준으로 다시 거른다.
                    if (placement_in_screen_range(window_placement { current.left, current.top, requested.width, requested.height }) == false)
                        return;
                    bounds = { current.left, current.top, current.left + requested.width, current.top + requested.height };
                }

                WINDOWPLACEMENT placement {};
                placement.length = sizeof(placement);
                if (GetWindowPlacement(window_, &placement) == FALSE)
                    return;

                // 전체 화면 요청도 **정상 배치를 먼저 놓고** 들어간다.
                // 요청의 자리·크기·최대화는 전체 화면 창의 사각형이 아니라 나올 때
                // 돌아갈 배치이므로(app_host.h), 순서를 뒤집으면 돌아갈 자리로
                // 갈무리되는 것이 지금 창의 자리가 된다.
                //  - 이미 전체 화면인 창이면 먼저 빠져나온다. 그러지 않으면 갈무리해
                //    둔 옛 배치가 아래의 `SetWindowPlacement`를 이겨 요청이 묻힌다.
                const bool was_fullscreen { main_.fullscreen() };
                if (was_fullscreen)
                    static_cast<void>(main_.set_fullscreen(false));
                placement.rcNormalPosition = bounds;
                placement.showCmd = requested.maximized ? static_cast<UINT>(SW_SHOWMAXIMIZED) : static_cast<UINT>(SW_SHOWNORMAL);
                static_cast<void>(SetWindowPlacement(window_, &placement));
                if (requested.fullscreen)
                    static_cast<void>(main_.set_fullscreen(true));
                // 전체 화면 여부가 바뀌었으면 알린다.
                // **요청을 그대로 되울리는 것이 아니다.** 앱은 이 사실로 tree를 다시
                // 짓는데(캡션 줄을 접는다), 이 전환의 `WM_SIZE`는 최대화 전환으로 세지
                // 않으므로 알리지 않으면 화면을 덮은 창이 캡션을 그린 채로 남는다.
                if (main_.fullscreen() != was_fullscreen)
                    post_window_placement();
            }

            void post_window_metrics() noexcept
            {
                if (host_ == nullptr || environment_.delegate == nullptr)
                    return;
                RECT client_rectangle {};
                if (GetClientRect(window_, &client_rectangle) == FALSE)
                    return;
                const float width { static_cast<float>(client_rectangle.right - client_rectangle.left) };
                const float height { static_cast<float>(client_rectangle.bottom - client_rectangle.top) };
                const float scale { static_cast<float>(dpi_) / 96.0f };
                // delegate는 noexcept가 아니다 (`post_window_placement`와 같은 이유).
                try
                {
                    if (app_message message { environment_.delegate->make_window_metrics_message(width, height, scale) }; message.empty() == false)
                        host_->post_app_message(std::move(message));
                }
                catch (...)
                {
                    report_runtime_error(u8"The window metrics delegate threw an exception.");
                }
            }

            // 클립보드 요청을 실행한다.
            // 클립보드는 Win32 자원이라 UI thread 전용이다.
            // 붙여넣기는 읽은 글을 그 텍스트 박스의 insert 편집으로 바꿔 logic에 보낸다.
            void execute_clipboard_request(const clipboard_request& request) noexcept
            {
                if (const auto* const copy { std::get_if<clipboard_copy_request>(&request) }; copy != nullptr)
                {
                    static_cast<void>(copy_text_to_clipboard(window_, copy->text));
                    return;
                }
                const auto* const paste { std::get_if<clipboard_paste_request>(&request) };
                if (paste == nullptr || host_ == nullptr || environment_.policy == nullptr)
                    return;
                std::u8string text { read_text_from_clipboard(window_) };
                if (text.empty())
                    return;

                text_edit_request edit {};
                edit.target = paste->target;
                edit.command = text::text_edit_command::insert;
                edit.text = std::move(text);
                // policy는 noexcept가 아니다 (`post_window_placement`와 같은 이유).
                try
                {
                    dispatch_action(environment_.policy->make_text_edit_action(edit));
                }
                catch (...)
                {
                    report_runtime_error(u8"The text edit policy threw an exception.");
                }
            }

            // --- 다시 그리기 ---
            // 무엇을 다시 그릴지 고르는 규칙은 `surface_invalidate.h`의 순수 함수
            // 둘이 정하고, 여기 남는 것은 그 답을 창에 옮기는 일이다 (rendering.md).

            // 표면 id 목록을 다시 그릴 대상으로 표시한다.
            // 빈 id는 주 창이고, 이미 사라진 표면의 id는 조용히 지나간다 —
            // timer가 기억해 둔 목록은 그 사이의 대조보다 낡을 수 있다.
            void invalidate_surfaces(const std::vector<std::u8string>& surfaces) noexcept
            {
                for (const std::u8string& id : surfaces)
                {
                    if (id.empty())
                    {
                        InvalidateRect(window_, nullptr, FALSE);
                        continue;
                    }
                    if (const popup_surface* const popup { find_popup(id) }; popup != nullptr)
                    {
                        InvalidateRect(popup->window(), nullptr, FALSE);
                        continue;
                    }
                    if (const secondary_surface* const secondary { find_secondary(id) }; secondary != nullptr)
                        InvalidateRect(secondary->window(), nullptr, FALSE);
                }
            }

            // 살아 있는 표면을 모두 다시 그린다.
            // OS가 팔레트·글꼴 해석을 통째로 바꿨을 때의 길이다.
            void invalidate_all_surfaces() const noexcept
            {
                InvalidateRect(window_, nullptr, FALSE);
                invalidate_popups();
                invalidate_windows();
            }

            // 주 창이 지금 그리는 tree와 이 게시가 실은 tree다.
            // 주 tree는 표면이 그릴 때 집으므로(`main_surface::prepare_frame`)
            // 목록 대조가 없다 — 다른 표면과 **같은 규칙**으로 견주기만 한다.
            [[nodiscard]] surface_content main_surface_content()
            {
                const std::shared_ptr<const ui_tree> posted { host_ != nullptr ? host_->acquire_ui_tree() : nullptr };
                return surface_content { main_.id(), main_.tree(), posted.get() };
            }

            // 이 게시가 모든 표면의 그림을 바꾸는지 판정한다.
            // frame과 interaction slot은 같은 wake 신호를 사용하므로 마지막 게시본과 비교한다.
            // interaction 또는 appearance가 바뀌면 모든 표면을 무효화한다.
            // 두 비교는 마지막 값을 각각 갱신하므로 단축 평가 없이 모두 실행해야 한다.
            // 발행본이 같으면 같은 element 안의 포인터 이동만으로는 다시 그리지 않는다.
            [[nodiscard]] bool posting_changes_every_surface()
            {
                if (host_ == nullptr)
                    return false;
                bool changed { false };
                if (interaction_snapshot interaction { host_->acquire_interaction() }; interaction != last_interaction_)
                {
                    last_interaction_ = std::move(interaction);
                    changed = true;
                }
                const std::shared_ptr<const ui_frame> frame { host_->acquire_frame() };
                if (frame != nullptr && (frame->appearance != last_appearance_ || frame->fonts != last_fonts_))
                {
                    last_appearance_ = frame->appearance;
                    last_fonts_ = frame->fonts;
                    changed = true;
                }
                return changed;
            }

            // 주 tree와 popup·보조 창 tree들이 next_update로 예고한 시각 중 가장 이른 것에 timer 하나를 건다.
            // caret 깜빡임·tooltip 지연·토스트 흐려짐·회전 표시가 전부 이 경로 하나로 모인다.
            //  - 위상이 각 element의 기준 시각(초점·hover·표시 시각)에서 나오므로
            //    render가 몇 번 겹쳐도 뒤집힘·넘어감 시점은 같다.
            // 예고가 없으면 timer를 꺼 평소 재그리기 비용을 0으로 되돌린다.
            void schedule_update_repaint(const std::u8string& surface, const interaction_snapshot& interaction) noexcept override
            {
                arm_update_timer(&surface, &interaction);
            }

            // 예고를 모아 timer 하나를 걸고 **그때 깨울 표면을 기억한다**.
            // 그 기억이 이 묶음의 핵심이다 — 없으면 보조 창 하나의 회전 표시가
            // 모든 창을 33ms 주기로 끌고 간다 (rendering.md).
            //  - `caller`가 nullptr이면 부르는 표면이 없다 (WM_TIMER의 되걸기).
            //    그때는 모든 표면이 발행본을 자기 것으로 거른 값으로 답한다.
            void arm_update_timer(const std::u8string* const caller, const interaction_snapshot* const interaction) noexcept
            {
                const update_context context { std::chrono::steady_clock::now() };
                std::vector<surface_update_deadline> deadlines {};
                deadlines.reserve(1 + popups_.size() + windows_.size());
                // 부르는 표면의 것은 이미 손에 있다 (거기에만 caption hover가 얹혀
                // 있어 caption tooltip의 지연 시각이 여기서 나온다). 나머지 표면에는
                // 거르기 전 발행본을 그 표면의 것으로 걸러 준다 — 부르는 표면의
                // 것을 그대로 주면 그 표면에서 난 초점·hover가 이미 지워져 있어
                // popup 안 caret이 깜빡일 시각을 아무도 예고하지 않는다
                // (multi-window-design.md).
                //  - 발행본은 **남의 표면이 실제로 있을 때만** 집는다. 이 함수는
                //    표면을 그릴 때마다 돌고 스냅샷 하나는 문자열 넷과 끌기 payload
                //    (파일 목록)를 실은 값이라, 창이 하나뿐인 앱에서 매 frame 한 벌을
                //    더 복사하게 둘 이유가 없다. `noexcept` 안의 할당도 그만큼 준다.
                //    되걸기(`caller`가 nullptr)에는 부르는 표면이 없어 첫 표면부터
                //    발행본을 집는다 — 그 길은 render마다가 아니라 tick마다다.
                std::optional<interaction_snapshot> published {};
                const auto ask = [&](const ui_tree* const tree, const std::u8string& id) {
                    if (tree == nullptr)
                        return;
                    if (caller != nullptr && id == *caller)
                    {
                        deadlines.push_back(surface_update_deadline { id, tree->next_update(context, *interaction) });
                        return;
                    }
                    if (published.has_value() == false)
                        published = host_ != nullptr ? host_->acquire_interaction() : (interaction != nullptr ? *interaction : interaction_snapshot {});
                    std::optional<std::chrono::steady_clock::time_point> next { tree->next_update(context, interaction_for_surface(*published, id)) };
                    // 부르는 표면이 아닌 것에는 **그 표면만 아는 상태가 빠져 있다.**
                    // caption hover는 비클라이언트 메시지로만 와 발행본에 실리지
                    // 않으므로 여기서는 되살릴 수 없다. 그 표면이 스스로 답했던 시각을
                    // 잊으면, 다른 표면이 애니메이션 중인 동안 그 표면은 다시 그려지지
                    // 않아 예고를 다시 낼 기회조차 없다 — **caption tooltip이 영영 뜨지
                    // 않는 길**이 그것이다. 아직 지나지 않은 이전 예고를 잇는다.
                    //  - 틀려도 방향은 더 그리는 쪽이고, 그 표면이 그려지는 순간
                    //    자기 상호작용으로 답한 값이 이것을 덮는다.
                    for (const surface_update_deadline& remembered : remembered_deadlines_)
                    {
                        if (remembered.surface != id || remembered.next.has_value() == false || *remembered.next <= context.now)
                            continue;
                        if (next.has_value() == false || *remembered.next < *next)
                            next = remembered.next;
                    }
                    deadlines.push_back(surface_update_deadline { id, next });
                };
                ask(main_.tree(), main_.id());
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                    ask(popup->tree(), popup->id());
                for (const std::unique_ptr<secondary_surface>& secondary : windows_)
                    ask(secondary->tree(), secondary->id());

                update_timer_plan plan { plan_update_timer(deadlines, context.now, continuous_repaint_interval) };
                timer_wake_ = std::move(plan.wake);
                // 다음 되걸기가 이을 수 있게 이번 답을 남긴다.
                // 사라진 표면의 것은 목록이 살아 있는 표면으로 다시 세워지며 빠진다.
                remembered_deadlines_ = std::move(deadlines);
                if (plan.armed == false)
                {
                    update_timer_armed_ = false;
                    KillTimer(window_, update_timer_id);
                    return;
                }
                // armed는 SetTimer가 실제로 선 뒤에만 참이다.
                // 실패하면 거짓으로 남아 snapshot wake의 가드가 다음 게시에서
                // 다시 시도한다 — 실패를 참으로 적으면 그 가드가 재장전을 건너뛴다.
                update_timer_armed_ = SetTimer(window_, update_timer_id, plan.delay_milliseconds, nullptr) != 0;
            }

            // 테마·키 컬러·글꼴 선호를 state에 옮긴다.
            // frame을 아직 받지 못했으면 기본값(system·mint)이다.
            // 반환된 typeface는 render 호출 동안 살아 있어야 한다.
            [[nodiscard]] sk_sp<SkTypeface> apply_frame_appearance(frame_state& state, const ui_frame* const frame) override
            {
                const appearance_settings appearance { frame != nullptr ? frame->appearance : appearance_settings {} };
                // 고대비 여부는 밝은 모드처럼 캐시를 읽는다.
                // 매 frame·표면마다 시스템 호출을 반복하지 않고 WM_SETTINGCHANGE가 갱신한다.
                state.theme = resolve_color_theme(appearance.theme, high_contrast_, system_prefers_light_);
                state.accent_id = appearance.accent_id;
                // 앱이 실은 스타일이다. frame은 이 렌더 동안 살아 있으므로 포인터로 족하다.
                state.style = frame != nullptr ? frame->style.get() : nullptr;
                if (state.theme == color_theme::high_contrast)
                    state.high_contrast = read_high_contrast_colors();
                // 그리기와 창 테두리·웹뷰 바닥이 **같은 팔레트**를 본다.
                const ui_color_palette palette { frame_palette(state) };
                apply_frame_theme(state.theme, palette);
                // 페이지가 배경을 정하기 전의 바닥은 창 바탕이다 — 흰색을 깔면 어두운
                // 화면에서 페이지가 뜨는 동안 흰 판이 번쩍인다.
                webviews_.set_default_background(palette.window_background);

                // 글꼴은 앱 선호가 정한다.
                // registry가 이름이 그대로면 아무 일도 하지 않으므로 매 frame 갱신해도 된다.
                const font_settings fonts { frame != nullptr ? frame->fonts : font_settings {} };
                set_configured_fonts(fonts.ui_family, fonts.code_family);
                sk_sp<SkTypeface> code { configured_code_typeface() };
                state.code_typeface = code.get();
                state.fonts = &font_resolver_;
                return code;
            }

            // 창 테두리를 팔레트에 맞춘다.
            // `USE_IMMERSIVE_DARK_MODE`는 OS가 그리는 창 요소의 명암을 설정한다.
            // `BORDER_COLOR`는 캔버스 바깥 DWM 테두리 색을 설정하며 팔레트의 divider를 사용한다.
            // 같은 답에는 다시 호출하지 않고 high contrast에서는 OS 색을 보존한다.
            void apply_frame_theme(const color_theme theme, const ui_color_palette& palette) noexcept
            {
                if (window_ == nullptr || theme == color_theme::high_contrast)
                    return;

                // 명암은 테마의 이름이 아니라 **바탕의 밝기**가 정한다.
                // 앱이 세운 스타일은 "dark"에 밝은 바탕을 둘 수 있고, 그때 OS 그림이
                // 이름을 따라가면 밝은 창에 어두운 모드의 요소가 선다.
                const BOOL dark { is_dark_background(palette.window_background) ? TRUE : FALSE };

                // **구분선은 알파가 실린 역할색이다.** 그대로 COLORREF로 보내면
                // 어두운 테마에서 흰 테두리가 나온다 (띄워 재 보고 알았다) — DWM은
                // 알파를 받지 않으므로 **창 바탕 위에 얹은 불투명 결과**를 보낸다.
                const auto channel = [](const ui_color color, const int shift) { return static_cast<float>((color >> shift) & 0xFFu); };
                const float alpha { channel(palette.divider, 24) / 255.0f };
                const auto blend = [&](const int shift) {
                    const float value { channel(palette.window_background, shift) * (1.0f - alpha) + channel(palette.divider, shift) * alpha };
                    return static_cast<BYTE>(value + 0.5f);
                };
                // ui_color는 0xAARRGGBB, COLORREF는 0x00BBGGRR다.
                const COLORREF border { RGB(blend(16), blend(8), blend(0)) };

                // 답이 같으면 DWM에 다시 말하지 않는다 — 키 컬러가 바뀌어도 테두리는
                // 중립 색에서 나오므로 답이 같고, 스타일이 바뀌면 답이 달라진다.
                if (frame_theme_.has_value() && frame_theme_->first == dark && frame_theme_->second == border)
                    return;
                frame_theme_ = std::pair { dark, border };
                static_cast<void>(DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
                // `DWMWA_BORDER_COLOR`는 지원되는 Windows에서 적용되며, 미지원 환경에서는 기본 테두리를 유지한다.
                static_cast<void>(DwmSetWindowAttribute(window_, DWMWA_BORDER_COLOR, &border, sizeof(border)));
            }

            // --- popup 오버레이 ---
            // frame의 popup 목록이 곧 상태다: 실리면 창이 생기고 빠지면 사라진다.
            // 표면 자체(입력·커서·그리기)는 `popup_surface`가 맡고,
            // 여기 남는 것은 목록 대조와 창 수명이다 (win32-surface-design.md).

            void invalidate_popups() const noexcept
            {
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                    InvalidateRect(popup->window(), nullptr, FALSE);
            }

            [[nodiscard]] bool ensure_popup_class()
            {
                if (popup_class_registered_)
                    return true;
                popup_class_name_ = config_.class_name + L".Popup";
                WNDCLASSEXW popup_class {};
                popup_class.cbSize = sizeof(popup_class);
                // 그림자는 OS의 것이다 (메뉴·tooltip이 쓰는 그 그림자).
                // popup은 자기 창이라 우리가 그린 것은 창 밖으로 드리울 수 없고, 창 클래스의
                // drop shadow가 창 둘레에 그것을 얹는다 (popup-overlay-design.md).
                popup_class.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
                popup_class.lpfnWndProc = &popup_surface::static_procedure;
                popup_class.hInstance = instance_;
                popup_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                popup_class.lpszClassName = popup_class_name_.c_str();
                popup_class_registered_ = RegisterClassExW(&popup_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
                return popup_class_registered_;
            }

            // frame의 popup 목록과 살아 있는 창을 대조해 만들고·옮기고·없앤다.
            // 대조를 마친 표면들이 지금 무엇을 그리고 있고 이 frame이 무엇을 실었는지
            // 함께 돌려준다 — 다시 그릴 대상을 고르는 것은 부른 쪽이다 (rendering.md).
            // frame의 웹뷰 목록과 살아 있는 것을 대조한다.
            //
            // **창 대조 뒤다.** 앵커 표면이 이미 있어야 그 창을 컨트롤러의
            // parentWindow로 줄 수 있다 (popup과 같은 순서다).
            void synchronize_webviews()
            {
                if (host_ == nullptr)
                    return;
                last_frame_ = host_->acquire_frame();
                if (last_frame_ == nullptr)
                    return;
                // 배출구는 한 번만 꽂는다. 사건은 입력이 아니라 앱 메시지이고
                // 받는 곳은 logic thread다 (http_client_config::deliver와 같은 자리).
                if (webview_deliver_bound_ == false)
                {
                    app_host* const target { host_.get() };
                    webviews_.set_deliver([target](app_message message) { target->post_app_message(std::move(message)); });
                    // 초점 신호는 **입력**이다. 앱 메시지가 아니라 raw input으로 가야
                    // input thread의 초점 상태 기계가 그것을 본다.
                    webviews_.set_focus_reporter([this](const std::u8string& id, const std::u8string& anchor, const webview_focus_signal signal) { report_webview_focus(id, anchor, signal); });
                    webview_deliver_bound_ = true;
                }

                std::vector<webview_target> targets {};
                targets.reserve(last_frame_->webviews.size());
                for (const ui_webview& want : last_frame_->webviews)
                {
                    const window_surface* const anchor { anchor_surface(want.anchor) };
                    targets.push_back(webview_target { &want, anchor != nullptr ? anchor->window() : nullptr });
                }
                std::u8string composition_error {};
                webviews_.synchronize(targets, composition_.acquire(composition_error));
            }

            [[nodiscard]] std::vector<surface_content> synchronize_popups()
            {
                // 주 창과 함께 파괴된 표면을 먼저 걷어낸다.
                // 표면은 자기 procedure 안에서 자기를 지울 수 없어 창만 놓고 남아 있다.
                std::erase_if(popups_, [](const std::unique_ptr<popup_surface>& popup) { return popup->attached() == false; });

                std::shared_ptr<const ui_frame> frame {};
                if (host_ != nullptr)
                    frame = host_->acquire_frame();

                std::vector<popup_placement> alive {};
                alive.reserve(popups_.size());
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                    alive.push_back(popup->placement());

                // 원하는 목록은 frame의 popup을 물리 픽셀로 옮긴 것이다.
                // 배율은 앵커 표면의 것이고, 앵커가 아직 없는 popup은 뺀다.
                std::vector<popup_placement> wanted {};
                std::vector<const ui_popup*> sources {};
                if (frame != nullptr)
                {
                    wanted.reserve(frame->popups.size());
                    for (const ui_popup& popup : frame->popups)
                    {
                        const window_surface* const anchor { anchor_surface(popup.anchor) };
                        if (anchor == nullptr)
                            continue;
                        const float scale { dpi_scale(anchor->dpi()) };
                        popup_placement placement { popup.id };
                        placement.anchor = popup.anchor;
                        placement.x = scaled_pixels(popup.x, scale);
                        placement.y = scaled_pixels(popup.y, scale);
                        placement.width = scaled_pixels(popup.width, scale);
                        placement.height = scaled_pixels(popup.height, scale);
                        wanted.push_back(std::move(placement));
                        sources.push_back(&popup);
                    }
                }

                // frame이 더 싣지 않는 실패 기록은 잊는다.
                std::erase_if(failed_popups_, [&wanted](const std::u8string& id) {
                    for (const popup_placement& placement : wanted)
                        if (placement.id == id)
                            return false;
                    return true;
                });

                const popup_reconcile_result plan { reconcile_popups(alive, wanted) };
                for (const std::u8string& id : plan.destroy)
                    destroy_popup(id);
                for (const std::size_t index : plan.move)
                    move_popup(wanted[index]);
                // 이번 대조에서 만들어진 popup이다.
                // 그릴 것이 통째로 새것이라 아래에서 `current`를 비워 반드시 목록에
                // 들게 한다 — `ShowWindow`의 첫 그리기에 기대지 않는다.
                std::vector<std::u8string> created {};
                for (const std::size_t index : plan.create)
                {
                    // 만들다 실패한 popup은 다시 시도하지 않는다.
                    if (std::find(failed_popups_.begin(), failed_popups_.end(), wanted[index].id) != failed_popups_.end())
                        continue;
                    create_popup(wanted[index], *sources[index]);
                    if (find_popup(wanted[index].id) != nullptr)
                        created.push_back(wanted[index].id);
                }

                std::vector<surface_content> contents {};
                if (frame == nullptr)
                    return contents;
                contents.reserve(frame->popups.size());
                for (const ui_popup& popup : frame->popups)
                {
                    popup_surface* const alive_popup { find_popup(popup.id) };
                    if (alive_popup == nullptr)
                        continue;
                    const bool fresh { std::find(created.begin(), created.end(), popup.id) != created.end() };
                    // **adopt보다 먼저 견준다.** adopt가 새 tree를 표면에 옮겨 담고
                    // 나면 무엇이 달라졌는지 견줄 것이 남지 않는다.
                    contents.push_back(surface_content { popup.id, fresh ? nullptr : alive_popup->tree(), popup.tree.get() });
                    // 새 frame이 popup을 계속 실었다는 것은 앱이 상태를 유지하기로 했다는 뜻이다.
                    // adopt는 tree 말고도 닫힘 factory와 빗장을 갱신하므로 판정과 무관하게 부른다.
                    static_cast<void>(alive_popup->adopt(popup));
                }
                return contents;
            }

            // popup이 붙는 표면이다.
            // 앵커 id가 비어 있으면 주 창이고, 가리키는 보조 창이 없으면 nullptr다
            // (아직 만들어지지 않았거나 이미 닫힌 창이다).
            [[nodiscard]] window_surface* anchor_surface(const std::u8string& anchor)
            {
                if (anchor.empty())
                    return &main_;
                return find_secondary(anchor);
            }

            [[nodiscard]] popup_surface* find_popup(const std::u8string& id)
            {
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                    if (popup->id() == id)
                        return popup.get();
                return nullptr;
            }

            // 표면 하나가 keyboard focus를 잃었다.
            // 그 표면과 **그 표면에 붙은 popup들**의 텍스트 초점을 거둔다.
            // 초점이 popup에 있으면 앵커 창의 이벤트만으로는 id가 달라 거둬지지
            // 않고, 그대로 두면 다음에 focus를 얻은 창에서 친 글자가 popup의
            // 박스로 간다 (popup-ime-design.md).
            //  - popup을 닫지는 않는다. 텍스트 칸이 있는 popup은 명시적인
            //    취소로만 닫혀야 한다.
            void surface_focus_lost(const std::u8string& surface) override
            {
                if (host_ == nullptr)
                    return;
                host_->post_raw_input(surface_focus_lost_event { surface });
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                    if (popup->anchor() == surface)
                        host_->post_raw_input(surface_focus_lost_event { popup->id() });
            }

            // 표면 하나가 keyboard focus를 얻었다.
            // 상실과 달리 붙은 popup에 퍼뜨리지 않는다 — 이것이 답하는 질문은
            // "어느 **창**이 활성인가"이고 popup은 창 초점을 받지 못한다
            // (active-surface-design.md).
            void surface_focus_gained(const std::u8string& surface) override
            {
                if (host_ == nullptr)
                    return;
                host_->post_raw_input(surface_focus_gained_event { surface });
            }

            // 이 앵커에 붙은 popup 표면 중 id가 같은 것이다.
            // popup은 keyboard focus를 받지 못해 자기 TSF session이 없다 —
            // 앵커 표면의 session이 이것으로 popup의 tree와 창을 함께 본다
            // (popup-ime-design.md).
            //  - 빈 id는 주 창을 가리키므로 popup일 수 없다.
            const window_surface* anchored_popup(const std::u8string& anchor, const std::u8string& id) const noexcept override
            {
                if (id.empty())
                    return nullptr;
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                    if (popup->id() == id && popup->anchor() == anchor)
                        return popup.get();
                return nullptr;
            }

            // 앵커 표면의 client 자리를 화면 자리로 옮기고 모니터 작업 영역 안으로 다듬는다.
            // 앵커가 사라진 뒤 한 frame 동안은 주 창을 기준으로 물러선다.
            [[nodiscard]] POINT popup_screen_position(const popup_placement& placement)
            {
                const window_surface* const anchor { anchor_surface(placement.anchor) };
                POINT origin { 0, 0 };
                ClientToScreen(anchor != nullptr ? anchor->window() : window_, &origin);
                int x { origin.x + placement.x };
                int y { origin.y + placement.y };

                RECT desired { x, y, x + placement.width, y + placement.height };
                const HMONITOR monitor { MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST) };
                MONITORINFO information {};
                information.cbSize = sizeof(information);
                if (GetMonitorInfoW(monitor, &information) != FALSE)
                {
                    const screen_area work { information.rcWork.left, information.rcWork.top, information.rcWork.right, information.rcWork.bottom };
                    const auto [clamped_x, clamped_y] { clamp_popup_position(x, y, placement.width, placement.height, work) };
                    x = clamped_x;
                    y = clamped_y;
                }
                return { x, y };
            }

            void create_popup(const popup_placement& placement, const ui_popup& source)
            {
                if (ensure_popup_class() == false)
                {
                    failed_popups_.push_back(placement.id);
                    return;
                }

                // 표면을 먼저 만들어 생성 인자로 넘긴다.
                // WM_NCCREATE가 창을 이 표면에 묶는다.
                auto popup { std::make_unique<popup_surface>(*this, placement.id, placement.anchor) };
                popup->set_placement(placement);
                const window_surface* const anchor { anchor_surface(placement.anchor) };
                // popup은 앵커 표면의 배율을 그대로 쓴다 (popup-overlay-design.md).
                popup->set_dpi(anchor != nullptr ? anchor->dpi() : dpi_);
                const POINT screen { popup_screen_position(placement) };
                // 앵커 창이 소유해 언제나 그 위에 있고, 초점·작업 표시줄에는 나타나지 않는다.
                // 그 창이 사라지면 popup도 함께 사라진다.
                const HWND owner { anchor != nullptr ? anchor->window() : window_ };
                const HWND handle {
                    CreateWindowExW(
                        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, popup_class_name_.c_str(), L"", WS_POPUP, screen.x, screen.y, placement.width, placement.height, owner, nullptr, instance_, popup.get()),
                };
                if (handle == nullptr)
                {
                    failed_popups_.push_back(placement.id);
                    return;
                }

                // popup 표면은 CPU 렌더러로 고정한다 (popup-overlay-design.md).
                std::u8string error {};
                if (popup->create_renderer(renderer_mode::cpu, {}, placement.width, placement.height, error) == false)
                {
                    report_runtime_error(error);
                    DestroyWindow(handle);
                    failed_popups_.push_back(placement.id);
                    return;
                }
                static_cast<void>(popup->adopt(source));
                popups_.push_back(std::move(popup));
                ShowWindow(handle, SW_SHOWNOACTIVATE);
            }

            void move_popup(const popup_placement& placement)
            {
                popup_surface* const popup { find_popup(placement.id) };
                if (popup == nullptr)
                    return;
                const popup_placement previous { popup->placement() };
                const bool resized { previous.width != placement.width || previous.height != placement.height };
                popup->set_placement(placement);
                const POINT screen { popup_screen_position(placement) };
                SetWindowPos(popup->window(), nullptr, screen.x, screen.y, placement.width, placement.height, SWP_NOACTIVATE | SWP_NOZORDER);
                if (resized)
                {
                    std::u8string error {};
                    if (popup->resize_renderer(placement.width, placement.height, error) == false)
                        report_runtime_error(error);
                    InvalidateRect(popup->window(), nullptr, FALSE);
                }
            }

            // 창 하나가 움직였다.
            // 어느 창을 움직이든 popup 밖 상호작용이라 닫힘 계기는 전부에게 가고,
            // 닫히지 않는 popup 중 **그 표면에 붙은 것**만 따라 옮긴다.
            void surface_moved(const std::u8string& anchor) override
            {
                if (popups_.empty())
                    return;
                static_cast<void>(dismiss_popups(popup_dismiss_reason::surface_moved));
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                {
                    if (popup->anchor() != anchor)
                        continue;
                    const POINT screen { popup_screen_position(popup->placement()) };
                    SetWindowPos(popup->window(), nullptr, screen.x, screen.y, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
                }
            }

            void destroy_popup(const std::u8string& id)
            {
                for (auto position = popups_.begin(); position != popups_.end(); ++position)
                {
                    if ((*position)->id() != id)
                        continue;
                    // 항목을 먼저 꺼내야 WM_DESTROY 중에도 목록이 일관된다.
                    const std::unique_ptr<popup_surface> taken { std::move(*position) };
                    popups_.erase(position);
                    DestroyWindow(taken->window());
                    return;
                }
            }

            void destroy_all_popups()
            {
                while (popups_.empty() == false)
                    destroy_popup(popups_.back()->id());
            }

            // 닫힘 계기 하나를 모든 popup에 알린다.
            // 닫자는 메시지만 내고 창을 없애는 것은 다음 frame의 대조다.
            // 무엇 하나라도 냈으면 참이다.
            bool dismiss_popups(const popup_dismiss_reason reason) override
            {
                bool requested { false };
                for (const std::unique_ptr<popup_surface>& popup : popups_)
                {
                    if (std::optional<input_action> action { popup->take_dismiss_action(reason) }; action.has_value())
                    {
                        dispatch_action(std::move(*action));
                        requested = true;
                    }
                }
                return requested;
            }

            // --- 보조 top-level 창 ---
            // frame의 windows 목록이 곧 상태다: 실리면 창이 생기고 빠지면 사라진다.
            // 표면 자체(입력·키·IME·caption chrome·그리기)는 `secondary_surface`가
            // 맡고, 여기 남는 것은 목록 대조와 창 수명이다.

            void invalidate_windows() const noexcept
            {
                for (const std::unique_ptr<secondary_surface>& secondary : windows_)
                    InvalidateRect(secondary->window(), nullptr, FALSE);
            }

            [[nodiscard]] bool ensure_secondary_class()
            {
                if (secondary_class_registered_)
                    return true;
                secondary_class_name_ = config_.class_name + L".Secondary";
                WNDCLASSEXW secondary_class {};
                secondary_class.cbSize = sizeof(secondary_class);
                secondary_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
                secondary_class.lpfnWndProc = &secondary_surface::static_procedure;
                secondary_class.hInstance = instance_;
                secondary_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                secondary_class.lpszClassName = secondary_class_name_.c_str();
                secondary_class_registered_ = RegisterClassExW(&secondary_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
                return secondary_class_registered_;
            }

            // frame의 windows 목록과 살아 있는 보조 창을 id로 대조해 만들고 없앤다.
            // 배치는 만들 때 한 번이라 popup과 달리 옮기는 일이 없다.
            // 돌려주는 것은 popup 쪽과 같다 (rendering.md).
            [[nodiscard]] std::vector<surface_content> synchronize_windows()
            {
                // 주 창과 함께 파괴된 표면을 먼저 걷어낸다 (popup과 같은 규칙).
                std::erase_if(windows_, [](const std::unique_ptr<secondary_surface>& secondary) { return secondary->attached() == false; });

                std::shared_ptr<const ui_frame> frame {};
                if (host_ != nullptr)
                    frame = host_->acquire_frame();

                std::vector<std::u8string> alive {};
                alive.reserve(windows_.size());
                for (const std::unique_ptr<secondary_surface>& secondary : windows_)
                    alive.push_back(secondary->id());

                // 크기 없는 창은 없는 것으로 본다 (popup과 같은 규칙).
                std::vector<std::u8string> wanted {};
                std::vector<const ui_window*> sources {};
                if (frame != nullptr)
                    for (const ui_window& source : frame->windows)
                        if (source.width > 0.0f && source.height > 0.0f)
                        {
                            wanted.push_back(source.id);
                            sources.push_back(&source);
                        }

                // frame이 더 싣지 않는 실패 기록은 잊는다.
                std::erase_if(failed_windows_, [&wanted](const std::u8string& id) { return std::find(wanted.begin(), wanted.end(), id) == wanted.end(); });

                const window_reconcile_result plan { reconcile_windows(alive, wanted) };
                for (const std::u8string& id : plan.destroy)
                    destroy_secondary(id);
                // popup과 같은 이유로 이번에 만들어진 창을 적어 둔다.
                std::vector<std::u8string> created {};
                for (const std::size_t index : plan.create)
                {
                    // 만들다 실패한 창은 다시 시도하지 않는다.
                    if (std::find(failed_windows_.begin(), failed_windows_.end(), wanted[index]) != failed_windows_.end())
                        continue;
                    create_secondary(*sources[index]);
                    if (find_secondary(wanted[index]) != nullptr)
                        created.push_back(wanted[index]);
                }

                std::vector<surface_content> contents {};
                if (frame == nullptr)
                    return contents;
                contents.reserve(frame->windows.size());
                for (const ui_window& source : frame->windows)
                {
                    secondary_surface* const alive_window { find_secondary(source.id) };
                    if (alive_window == nullptr)
                        continue;
                    const bool fresh { std::find(created.begin(), created.end(), source.id) != created.end() };
                    // popup과 같이 **adopt보다 먼저** 견준다.
                    contents.push_back(surface_content { source.id, fresh ? nullptr : alive_window->tree(), source.tree.get() });
                    // 새 frame이 창을 계속 실었으면 닫힘 요청을 다시 낼 수 있게 풀린다.
                    // adopt는 tree 말고도 metrics·close factory와 빗장을 갱신하므로
                    // 판정과 무관하게 부른다.
                    static_cast<void>(alive_window->adopt(source));
                }
                return contents;
            }

            [[nodiscard]] secondary_surface* find_secondary(const std::u8string& id)
            {
                for (const std::unique_ptr<secondary_surface>& secondary : windows_)
                    if (secondary->id() == id)
                        return secondary.get();
                return nullptr;
            }

            void create_secondary(const ui_window& source)
            {
                if (ensure_secondary_class() == false)
                {
                    failed_windows_.push_back(source.id);
                    return;
                }

                // 초기 배치는 주 창 client 기준 논리 픽셀을 주 창 배율로 계산한다.
                // 생성 뒤에는 자기 모니터의 DPI를 따른다.
                const float scale { dpi_scale(dpi_) };
                POINT origin { 0, 0 };
                ClientToScreen(window_, &origin);
                RECT bounds {};
                bounds.left = origin.x + scaled_pixels(source.x, scale);
                bounds.top = origin.y + scaled_pixels(source.y, scale);
                bounds.right = bounds.left + scaled_pixels(source.width, scale);
                bounds.bottom = bounds.top + scaled_pixels(source.height, scale);
                // client 크기를 창 크기로 바꾸고 작업 영역 안으로 다듬는다.
                // 보조 창은 전체 화면이 되지 않는다 — 배치와 마찬가지로 주 창의 것이다.
                static_cast<void>(AdjustWindowRectExForDpi(&bounds, custom_window_style_for(source.caption.buttons, window_display_mode::normal), FALSE, 0, dpi_));
                int x { bounds.left };
                int y { bounds.top };
                const int width { bounds.right - bounds.left };
                const int height { bounds.bottom - bounds.top };
                const HMONITOR monitor { MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST) };
                MONITORINFO information {};
                information.cbSize = sizeof(information);
                if (GetMonitorInfoW(monitor, &information) != FALSE)
                {
                    const screen_area work { information.rcWork.left, information.rcWork.top, information.rcWork.right, information.rcWork.bottom };
                    const auto [clamped_x, clamped_y] { clamp_popup_position(x, y, width, height, work) };
                    x = clamped_x;
                    y = clamped_y;
                }

                std::wstring title {};
                if (const auto converted { utf8_to_utf16(source.caption.title) }; converted.value.has_value())
                    title = *converted.value;

                // 표면을 먼저 만들어 생성 인자로 넘긴다 (popup과 같은 규칙).
                auto secondary { std::make_unique<secondary_surface>(*this, source.id) };
                secondary->set_caption(source.caption);
                secondary->set_minimum_client_size(source.minimum_width, source.minimum_height);
                // 주 창이 소유해 언제나 그 위에 있다.
                // 스타일은 이 창의 caption 버튼 집합에서 나오고 시스템 캡션만 뗀다.
                const DWORD style { window_style_for(source.caption.buttons, window_display_mode::normal) };
                const HWND handle { CreateWindowExW(0, secondary_class_name_.c_str(), title.c_str(), style, x, y, width, height, window_, nullptr, instance_, secondary.get()) };
                if (handle == nullptr)
                {
                    failed_windows_.push_back(source.id);
                    return;
                }
                std::u8string error {};
                if (caption_surface::remove_system_caption(handle, error) == false)
                {
                    report_runtime_error(error);
                    DestroyWindow(handle);
                    failed_windows_.push_back(source.id);
                    return;
                }
                caption_surface::apply_dwm_frame(handle, window_display_mode::normal);
                secondary->set_dpi(GetDpiForWindow(handle));

                // 렌더러는 주 창 정책 그대로다.
                // 실패 물러섬은 renderer_host의 몫이다.
                RECT client {};
                GetClientRect(handle, &client);
                if (secondary->create_renderer(config_.renderer, {}, client.right - client.left, client.bottom - client.top, error) == false)
                {
                    report_runtime_error(error);
                    DestroyWindow(handle);
                    failed_windows_.push_back(source.id);
                    return;
                }
                // IME 조합을 이 창에서 하려면 창마다 문서를 열어 줘야 한다.
                if (host_ != nullptr)
                    secondary->create_text_input();

                static_cast<void>(secondary->adopt(source));
                // 앱은 이 메시지의 실제 크기·배율로 tree를 배치해 다음 frame에 싣는다.
                secondary->post_metrics();
                windows_.push_back(std::move(secondary));
                ShowWindow(handle, SW_SHOWNOACTIVATE);
            }

            void destroy_secondary(const std::u8string& id)
            {
                for (auto position = windows_.begin(); position != windows_.end(); ++position)
                {
                    if ((*position)->id() != id)
                        continue;
                    // 항목을 먼저 꺼내야 WM_DESTROY 중에도 목록이 일관된다.
                    const std::unique_ptr<secondary_surface> taken { std::move(*position) };
                    windows_.erase(position);
                    DestroyWindow(taken->window());
                    return;
                }
            }

            void destroy_all_secondary_windows()
            {
                while (windows_.empty() == false)
                    destroy_secondary(windows_.back()->id());
            }

            int scale_for_dpi(const int value) const noexcept
            {
                return MulDiv(value, static_cast<int>(dpi_), 96);
            }

            void report_runtime_error(const std::u8string& error) const noexcept
            {
                const auto wide_error { utf8_to_utf16(error) };
                if (wide_error.value.has_value())
                {
                    OutputDebugStringW(wide_error.value->c_str());
                    OutputDebugStringW(L"\n");
                }
            }

            HINSTANCE instance_ { nullptr };
            window_config config_ {};
            window_environment environment_ {};
            HWND window_ { nullptr };
            std::uint32_t dpi_ { 96 };
            // 마지막으로 적용한 창 배치 게시 번호다.
            // 0은 아직 적용한 적이 없다는 뜻이다.
            std::uint64_t applied_window_placement_revision_ { 0 };
            // 마지막으로 배치를 알린 최대화 상태다.
            // WM_SIZE 폭주 중에 같은 상태를 반복해서 보내지 않는다.
            bool posted_maximized_ { false };
            // OS의 "앱 모드" 설정이다.
            // 레지스트리 조회는 프레임마다 하지 않고 시작 시 한 번,
            // 이후 WM_SETTINGCHANGE·WM_THEMECHANGED에서만 갱신한다.
            bool system_prefers_light_ { read_system_prefers_light_theme() };
            // DWM에 마지막으로 말한 명암과 테두리 색이다 (아직 말하지 않았으면 비어 있다).
            // 테마 이름이 아니라 **보낸 답**을 기억한다 — 스타일이 바뀌어 같은 테마의 답이
            // 달라지는 경우를 이름으로는 잡을 수 없다.
            std::optional<std::pair<BOOL, COLORREF>> frame_theme_ {};
            // 고대비 여부의 캐시다 (WM_SETTINGCHANGE가 갱신).
            bool high_contrast_ { read_high_contrast_enabled() };
            // update timer가 깨울 표면들이다.
            // "그 시각을 답한 표면"을 timer와 함께 들고 있는 것이 표면별 다시 그리기의
            // 절반이다 — 나머지 절반은 frame 대조가 낸다 (rendering.md).
            std::vector<std::u8string> timer_wake_ {};
            // 표면마다 마지막으로 답한 예고다.
            // 되걸 때는 부르는 표면이 없어 caption hover 같은 UI thread만의 상태를
            // 되살릴 수 없으므로, 아직 지나지 않은 이전 답을 잇는 근거로 쓴다.
            std::vector<surface_update_deadline> remembered_deadlines_ {};
            // update timer가 지금 걸려 있는가다.
            // 게시가 예고의 사슬을 **켤 때만** 손대고 이미 도는 사슬은 두기 위한
            // 값이다 — 걸린 timer를 다시 걸면 tick이 뒤로 밀린다.
            bool update_timer_armed_ { false };
            // 다시 그릴 대상을 고를 때 마지막으로 본 발행본·외양이다.
            // frame slot과 interaction slot이 같은 wake를 쓰므로 이것과 견주는 것이
            // "상호작용이 바뀌었나"를 아는 유일한 길이다 (rendering.md).
            interaction_snapshot last_interaction_ {};
            appearance_settings last_appearance_ {};
            font_settings last_fonts_ {};
            // 버튼을 눌러 캡처를 잡고 있는 중이다.
            // 자기 ReleaseCapture가 낳는 WM_CAPTURECHANGED와 진짜 캡처 상실을
            // 구분하는 근거다 — 뗌 처리기가 ReleaseCapture 직전에 내린다.
            bool pointer_capture_active_ { false };
            // 가족 이름 → typeface(글꼴 미리 보기)와 글자 → 대체 typeface를 함께 맡는다.
            // 둘 다 win32_fonts의 같은 cache를 보므로 나눌 이유가 없다.
            struct registry_font_resolver final : font_resolver, font_fallback
            {
                [[nodiscard]] SkTypeface* family(const std::u8string_view name) const override
                {
                    return family_typeface(name).get();
                }

                [[nodiscard]] sk_sp<SkTypeface> for_codepoint(const char32_t codepoint) const override
                {
                    return fallback_typeface(codepoint);
                }
            };

            registry_font_resolver font_resolver_ {};
            // 프로세스에 하나뿐인 DirectComposition device다.
            // 표면들이 `surface_context`로 빌려 가고, 창별 target·visual은 그
            // 표면의 렌더러가 든다 (webview-composition-design.md).
            //  - **표면들보다 오래 살아야 한다.** 렌더러 소멸자가 이 device로
            //    `Commit`을 부르므로 선언 순서가 곧 파괴 순서의 계약이다.
            // 가림 판정이다. 자리표의 사각형 다섯 지점에서 hit test가 자기 자신을
            // 돌려주면 아무것도 덮지 않은 것이다.
            //
            // 다섯 점은 **전부 아니면 전무**의 값싼 근사다. 걸치는 것을 rect
            // 차집합으로 빼는 것은 오버레이가 여럿이면 layer가 되므로 짓지 않았다
            // (webview-composition-design.md).
            [[nodiscard]] static bool occluded(const ui_tree& tree, const ui_element* const placeholder, const std::optional<rect_f>& visible) noexcept
            {
                if (placeholder == nullptr || visible.has_value() == false)
                    return true;
                const float inset { 1.0f };
                const float left { visible->x + inset };
                const float top { visible->y + inset };
                const float right { visible->x + visible->width - inset };
                const float bottom { visible->y + visible->height - inset };
                const float center_x { visible->x + visible->width / 2.0f };
                const float center_y { visible->y + visible->height / 2.0f };
                const std::array<std::pair<float, float>, 5> probes {
                    std::pair { center_x, center_y },
                    std::pair { left, top },
                    std::pair { right, top },
                    std::pair { left, bottom },
                    std::pair { right, bottom },
                };
                for (const auto& [x, y] : probes)
                    if (tree.hit_test(x, y) != placeholder)
                        return true;
                return false;
            }

            composition_host composition_ {};
            // 살아 있는 웹뷰들이다. 창 하나에 하나이고 표면들이 나눠 쓴다.
            webview_host webviews_ {};
            bool webview_deliver_bound_ { false };
            // 이번 표면에서 비울 자리들이다. 표면 하나를 그리는 동안만 유효하다.
            std::vector<pixel_rect> webview_holes_ {};
            // 직전 frame의 논리 초점이다. 자리표에 **닿는 순간**을 가리기 위한 것이고,
            // 매 frame 넘기면 페이지 안에서 옮긴 초점이 첫 자리로 되돌아간다.
            ui_element_id last_focus_ {};
            // 마지막으로 본 게시본이다. 자리 계산이 그리기 시점에 목록을 다시 본다.
            std::shared_ptr<const ui_frame> last_frame_ {};
            std::unique_ptr<app_host> host_ {};
            // 주 창의 표면이다.
            // 자기 renderer와 TSF 연결을 들고 있다 (win32-surface-design.md).
            main_surface main_ { *this };
            // 살아 있는 popup 표면들이다.
            // frame의 popup 목록과 대조해 유지한다.
            std::vector<std::unique_ptr<popup_surface>> popups_ {};
            std::wstring popup_class_name_ {};
            bool popup_class_registered_ { false };
            // 만들다 실패한 popup·보조 창의 id다.
            // frame 목록에 남아 있는 한 매 frame 생성→실패가 반복되므로
            // 실패를 기억해 건너뛰고, 앱이 목록에서 빼면 잊는다
            // (뺐다가 다시 실으면 그때 새로 시도한다).
            std::vector<std::u8string> failed_popups_ {};
            std::vector<std::u8string> failed_windows_ {};
            // 살아 있는 보조 창 표면들이다.
            // frame의 windows 목록과 대조해 유지한다.
            std::vector<std::unique_ptr<secondary_surface>> windows_ {};
            std::wstring secondary_class_name_ {};
            bool secondary_class_registered_ { false };
        };

        void show_startup_error(const std::u8string& error, const window_config& config)
        {
            const auto wide_error { utf8_to_utf16(error) };
            if (wide_error.value.has_value() == false)
                return;
            OutputDebugStringW(wide_error.value->c_str());
            OutputDebugStringW(L"\n");
            if (config.smoke_test)
                return;

            // 제목·앞글은 앱이 준 것을 쓴다 (`startup_error_config`).
            // 변환에 실패한 값은 없는 것으로 본다 — 시작 실패를 알리는 자리에서
            // 설정 문자열 하나 때문에 상자를 못 띄우면 안 된다.
            std::wstring title { L"luil startup error" };
            if (const auto wide_title { utf8_to_utf16(config.startup_error.title) }; config.startup_error.title.empty() == false && wide_title.value.has_value())
                title = *wide_title.value;
            std::wstring text { *wide_error.value };
            if (const auto wide_preface { utf8_to_utf16(config.startup_error.preface) }; config.startup_error.preface.empty() == false && wide_preface.value.has_value())
                text = *wide_preface.value + L"\n\n" + text;
            MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        }
    } // namespace

    cursor_handle load_system_cursor(const system_cursor cursor) noexcept
    {
        const wchar_t* name { IDC_ARROW };
        switch (cursor)
        {
        case system_cursor::hand:
            name = IDC_HAND;
            break;
        case system_cursor::text:
            name = IDC_IBEAM;
            break;
        case system_cursor::wait:
            name = IDC_WAIT;
            break;
        case system_cursor::not_allowed:
            name = IDC_NO;
            break;
        case system_cursor::resize_horizontal:
            name = IDC_SIZEWE;
            break;
        case system_cursor::resize_vertical:
            name = IDC_SIZENS;
            break;
        case system_cursor::resize_all:
            name = IDC_SIZEALL;
            break;
        case system_cursor::cross:
            name = IDC_CROSS;
            break;
        case system_cursor::help:
            name = IDC_HELP;
            break;
        case system_cursor::arrow:
        default:
            break;
        }
        return cursor_handle { LoadCursorW(nullptr, name) };
    }

    cursor_handle load_cursor_resource(const int resource_id) noexcept
    {
        return cursor_handle { LoadCursorW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource_id)) };
    }

    int run_application_window(const window_config& config, const window_environment& environment)
    {
        std::u8string error {};
        if (verify_embedded_resources(error) == false)
        {
            show_startup_error(error, config);
            return 1;
        }

        // 실행 module은 여기서 스스로 얻는다.
        // 소비자의 wWinMain이 Win32 타입을 라이브러리에 나를 필요가 없다.
        application_window window { GetModuleHandleW(nullptr), config, environment };
        if (window.create(error) == false)
        {
            show_startup_error(error, config);
            if (config.smoke_test && config.renderer == renderer_mode::direct3d)
                return direct3d_unavailable_exit_code;
            return 1;
        }
        return window.run();
    }

    void enable_per_monitor_dpi_awareness() noexcept
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    com_sta_scope::com_sta_scope() noexcept
        // OleInitialize가 같은 STA를 함께 세우고 OLE 서비스(drag & drop)를 더한다.
        // CoInitializeEx만으로는 RegisterDragDrop이 서지 않는다.
        : succeeded_ { SUCCEEDED(OleInitialize(nullptr)) }
    {}

    com_sta_scope::~com_sta_scope()
    {
        if (succeeded_)
            OleUninitialize();
    }

    bool com_sta_scope::succeeded() const noexcept
    {
        return succeeded_;
    }

    std::optional<std::vector<std::u8string>> command_line_arguments()
    {
        int argument_count { 0 };
        LPWSTR* wide_arguments { CommandLineToArgvW(GetCommandLineW(), &argument_count) };
        if (wide_arguments == nullptr)
            return std::nullopt;

        std::vector<std::u8string> arguments {};
        arguments.reserve(static_cast<std::size_t>(argument_count));
        for (int index = 0; index < argument_count; ++index)
        {
            auto conversion { utf16_to_utf8(wide_arguments[index]) };
            if (conversion.value.has_value() == false)
            {
                LocalFree(wide_arguments);
                return std::nullopt;
            }
            arguments.push_back(std::move(*conversion.value));
        }
        LocalFree(wide_arguments);
        return arguments;
    }
} // namespace luil::win32

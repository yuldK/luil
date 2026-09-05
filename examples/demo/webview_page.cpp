#include "demo/webview_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/webview_element.h"

#include "luil/generated/codicons.h"

#include <windows.h>

#include <array>
#include <string>
#include <utility>

namespace demo {
    namespace {
        // 이 웹뷰의 브라우저 프로필(쿠키·저장소·캐시)이 사는 자리다.
        //
        // 라이브러리가 **필수로 요구하는 값**이라 앱이 정한다. 기본값(실행 파일 옆)은
        // 설치 프로그램이 보호된 자리에 놓은 앱에서 반드시 실패하므로 라이브러리가
        // 조용히 물러서지 않는다 (luil/win32/webview.h).
        [[nodiscard]] std::u8string profile_directory()
        {
            wchar_t buffer[MAX_PATH] {};
            const DWORD length { GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) };
            if (length == 0 || length >= MAX_PATH)
                return {};
            const std::filesystem::path folder { std::filesystem::path { buffer } / L"luil-demo" / L"webview" };
            return folder.u8string();
        }

        // 네이티브로 그리는 글 문서다.
        // 웹뷰가 푸는 HTML과 나란히 두어 무엇이 무엇을 그리는지 보이게 한다.
        constexpr std::u8string_view text_document {
            u8"이 탭은 웹뷰가 아니다. 글은 label_element가 그린다.\n"
            u8"탭 막대도, 이 아래 알림 줄도 우리 tree의 것이다.\n"
            u8"\n"
            u8"탭을 오갈 때마다 웹뷰는 자리표가 tree에서 빠져 감춰지고 돌아오면 다시 선다.\n"
            u8"그 왕복이 화면에 구멍을 남기지 않는 것이 이 페이지가 지키는 계약이다.",
        };

        // 사건의 이름이다 (상태 줄에 찍힌다).
        [[nodiscard]] std::u8string_view kind_name(const luil::win32::webview_event_kind kind)
        {
            using luil::win32::webview_event_kind;
            switch (kind)
            {
            case webview_event_kind::navigation_started:
                return u8"항해 시작";
            case webview_event_kind::navigation_completed:
                return u8"항해 끝";
            case webview_event_kind::navigation_failed:
                return u8"항해 실패";
            case webview_event_kind::navigation_blocked:
                return u8"막힘";
            case webview_event_kind::web_message_received:
                return u8"메시지";
            case webview_event_kind::events_dropped:
                return u8"버림";
            case webview_event_kind::permission_denied:
                return u8"권한 거절";
            case webview_event_kind::download_blocked:
                return u8"내려받기 막힘";
            case webview_event_kind::render_process_failed:
                return u8"렌더러 죽음";
            case webview_event_kind::browser_process_failed:
                return u8"브라우저 죽음";
            }
            return u8"?";
        }

        // 사건 한 줄이다: 이름 · 주소 · 까닭 · 메시지(앞부분만).
        [[nodiscard]] std::u8string describe(const luil::win32::webview_event& event)
        {
            std::u8string line { kind_name(event.kind) };
            if (event.url.empty() == false)
                line += u8" · " + event.url;
            if (event.error.empty() == false)
                line += u8" · " + event.error;
            if (event.message.empty() == false)
            {
                // 상태 줄 한 칸이다. ASCII만 자른다 — JSON의 한글을 중간에서 자르면
                // 깨진 바이트가 남는다.
                constexpr std::size_t preview { 72 };
                std::size_t cut { std::min(preview, event.message.size()) };
                while (cut < event.message.size() && (static_cast<unsigned char>(event.message[cut]) & 0xC0U) == 0x80U)
                    ++cut;
                line += u8" · " + event.message.substr(0, cut) + (cut < event.message.size() ? u8"…" : u8"");
            }
            return line;
        }
    } // namespace

    bool webview_page::handle(const luil::app_message& message)
    {
        if (const auto* const select { message.get<webview_tab_select_intent>() }; select != nullptr)
        {
            selected_ = select->key;
            // 웹뷰 탭으로 왔으면 그 문서를 연다.
            // **revision을 올리는 것이 곧 명령이다** — 주소만 바꾸면 frame이 다시
            // 게시될 때마다 같은 주소를 다시 읽는다 (webview.h).
            //  - 같은 주소여도 다시 연다. 페이지가 스스로 다른 곳으로 갔을 수 있고
            //    (새 창 요청을 여기서 열면 그렇다) 탭이 말하는 문서가 그 자리에 있어야 한다.
            if (const tab* const current { selected() }; current != nullptr && current->path.empty() == false && base_url_.empty() == false)
            {
                requested_url_ = base_url_ + current->path;
                ++navigate_revision_;
            }
            return true;
        }
        if (const auto* const report { message.get<webview_report_intent>() }; report != nullptr)
        {
            recent_events_.insert(recent_events_.begin(), describe(report->event));
            if (recent_events_.size() > 4)
                recent_events_.resize(4);
            // 브라우저 프로세스가 죽으면 그 웹뷰는 목록에서 사라진 것과 같다.
            // 다른 id로 다시 실으면 라이브러리가 새로 세운다 (webview.h) — 같은 id를
            // 한 frame 뺐다가 다시 싣는 것과 같은 일을 한 frame에 한다.
            if (report->event.kind == luil::win32::webview_event_kind::browser_process_failed)
            {
                ++generation_;
                ++navigate_revision_;
            }
            return true;
        }
        if (const auto* const toggle { message.get<webview_new_window_toggle_intent>() }; toggle != nullptr)
        {
            open_new_windows_here_ = toggle->open;
            return true;
        }
        return false;
    }

    void webview_page::set_base_url(std::u8string url)
    {
        base_url_ = std::move(url);
        user_data_folder_ = profile_directory();
        if (tabs_.empty())
        {
            tabs_.push_back({ u8"doc", u8"문서 (HTML)", u8"/page" });
            tabs_.push_back({ u8"text", u8"글 (우리 element)", {} });
            tabs_.push_back({ u8"image", u8"그림 (우리 element)", {} });
            tabs_.push_back({ u8"notes", u8"메모 (HTML)", u8"/page?tab=notes" });
        }
        if (base_url_.empty() == false && requested_url_.empty())
        {
            requested_url_ = base_url_ + u8"/page";
            ++navigate_revision_;
        }
    }

    void webview_page::load_assets()
    {
        // 자리는 실행 파일 옆이다 — 작업 디렉터리로 찾으면 무엇으로 띄웠느냐에 따라
        // 달라진다 (basics_page와 같은 규칙).
        std::array<wchar_t, MAX_PATH> buffer {};
        const DWORD length { GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())) };
        if (length == 0 || length >= buffer.size())
            return;
        const std::filesystem::path directory { std::filesystem::path { std::wstring { buffer.data(), length } }.parent_path() };
        const std::filesystem::path path { directory / L"assets" / L"gradient.png" };
        picture_ = luil::load_image_file(path.u8string(), { .max_width = 880, .max_height = 880 }, picture_error_);
    }

    const webview_page::tab* webview_page::selected() const
    {
        for (const tab& candidate : tabs_)
            if (candidate.key == selected_)
                return &candidate;
        return nullptr;
    }

    std::u8string webview_page::webview_id() const
    {
        std::u8string id { u8"demo" };
        if (generation_ > 0)
        {
            id += u8'-';
            for (const char digit : std::to_string(generation_))
                id.push_back(static_cast<char8_t>(digit));
        }
        return id;
    }

    std::optional<luil::win32::ui_webview> webview_page::webview() const
    {
        if (user_data_folder_.empty())
            return std::nullopt;

        luil::win32::ui_webview view {};
        view.id = webview_id();
        view.user_data_folder = user_data_folder_;
        // 되돌이 서버는 http다. 기본값(https만)으로는 열리지 않으므로 이 예제가
        // 무엇을 여는지를 명시한다 — 목록이 곧 그 웹뷰가 갈 수 있는 곳의 전부다.
        view.policy.allowed_schemes = { u8"http", u8"https" };
        view.policy.open_new_windows_here = open_new_windows_here_;
        view.navigate_revision = navigate_revision_;
        view.navigate_url = requested_url_;
        view.on_event = [](luil::win32::webview_event event) { return luil::app_message { webview_report_intent { std::move(event) } }; };
        return view;
    }

    std::unique_ptr<luil::ui_element> webview_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(scale);

        luil::tab_bar_config bar {};
        bar.owner = u8"webview";
        for (const tab& entry : tabs_)
        {
            luil::tab_item item {};
            item.key = entry.key;
            item.label = entry.label;
            item.icon = entry.path.empty() ? luil::codicons::icon_file : luil::codicons::icon_globe;
            bar.items.push_back(std::move(item));
        }
        bar.selected = selected_;
        bar.tab_width = 170.0f;
        bar.select = [](const std::u8string& key) { return luil::make_app_action(webview_tab_select_intent { key }); };

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"webview" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"webview-hint" }, u8"탭마다 그리는 쪽이 다르다 — HTML은 웹뷰가, 글과 그림은 우리 element가 그린다.", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(8.0f);
        column->add(std::make_unique<luil::tab_bar_element>(std::move(bar)), 34.0f);
        column->add_gap(6.0f);

        // 정책 스위치 하나다. 확인란은 스스로 바뀌지 않는다 — **바뀐 뒤의 상태**를
        // 실은 액션을 매 frame 새로 싣는다 (network_page의 심장 박동과 같다).
        luil::check_config windows {};
        windows.owner = u8"webview-new-window";
        windows.style = luil::check_style::checkbox;
        windows.label = u8"새 창 요청(target=_blank)을 이 웹뷰에서 연다";
        windows.checked = open_new_windows_here_;
        windows.tooltip = u8"꺼져 있으면 막고 상태 줄에 알린다. 어느 쪽이든 두 번째 창은 뜨지 않는다";
        windows.toggle = luil::make_message_action(webview_new_window_toggle_intent { open_new_windows_here_ == false });
        column->add(std::make_unique<luil::check_element>(std::move(windows)), luil::check_row_height);
        column->add_gap(6.0f);

        const tab* const current { selected() };
        const float body { height > 0.0f ? height - 246.0f : 320.0f };
        const float body_height { body > 80.0f ? body : 80.0f };

        if (current != nullptr && current->path.empty() == false)
        {
            // 웹뷰 탭이다. 자리표가 자리를 잡고, 웹뷰가 서면 그 위가 구멍으로
            // 비워져 페이지가 대신 보인다.
            luil::webview_config placeholder {};
            placeholder.webview = webview_id();
            placeholder.name = u8"문서 미리 보기";
            placeholder.placeholder = base_url_.empty() ? u8"되돌이 서버를 기다린다" : u8"웹뷰가 서지 않았다 — 런타임이 없거나 CPU 렌더러다";
            column->add(std::make_unique<luil::webview_element>(luil::ui_element_id { luil::ui_element_kind::webview, webview_id() }, std::move(placeholder)),
                { .length = body_height });
        }
        else if (current != nullptr && current->key == u8"image")
        {
            luil::image_config picture {};
            picture.image = picture_;
            picture.fit = luil::image_fit::contain;
            column->add(std::make_unique<luil::image_element>(luil::ui_element_id { kind_image_preview, u8"webview-image" }, std::move(picture)), { .length = body_height });
        }
        else
        {
            column->add(make_label(luil::ui_element_id { kind_text, u8"webview-text" }, std::u8string { text_document }, 13.0f, luil::label_color_role::primary),
                { .length = body_height });
        }

        column->add_gap(8.0f);
        // 사건 한 줄에 label 하나다 (label은 줄바꿈을 그리지 않는다).
        for (std::size_t index = 0; index < 4; ++index)
        {
            std::u8string id { u8"webview-event-" };
            id.push_back(static_cast<char8_t>('0' + index));
            const std::u8string text { index < recent_events_.size() ? recent_events_[index] : (index == 0 ? std::u8string { u8"아직 알려 온 것이 없다" } : std::u8string {}) };
            column->add(make_label(luil::ui_element_id { kind_text, id }, text, 11.0f, luil::label_color_role::dim), 16.0f);
        }
        return column;
    }
} // namespace demo

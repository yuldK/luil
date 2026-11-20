#include "demo/webview_page.h"
#include "demo/basics_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/ui_tree.h"
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
        // 떠 있는 단추의 크기와 자리표 오른쪽 아래 모서리에서 떨어진 거리다 (논리 픽셀).
        //  - 거리를 둔 자리는 가림 표본(가운데와 네 모서리)에 닿지 않는다. 닿게 두면 페이지가 통째로 사라진다.
        constexpr float floating_width { 132.0f };
        constexpr float floating_height { 30.0f };
        constexpr float floating_margin { 16.0f };

        // 두 방식이 같은 단추를 쓴다. 달라지는 것은 그 단추가 서는 표면뿐이다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_floating_button()
        {
            luil::text_button_config config {};
            config.text = u8"떠 있는 단추";
            config.glyph = luil::codicons::icon_globe;
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_webview_floating, u8"webview-floating" }, std::move(config)) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(webview_floating_press_intent {}));
            return button;
        }

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
            case webview_event_kind::presentation_changed:
                return u8"표면 표시";
            case webview_event_kind::creation_failed:
                return u8"세우지 못함";
            }
            return u8"?";
        }

        // 사건 한 줄이다: 이름 · 주소 · 까닭 · 메시지(앞부분만).
        [[nodiscard]] std::u8string describe(const luil::win32::webview_event& event)
        {
            std::u8string line { kind_name(event.kind) };
            if (event.kind == luil::win32::webview_event_kind::presentation_changed)
                line += event.presentation_available ? u8" 가능" : u8" 불가";
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
    }

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
        if (const auto* const layer { message.get<webview_layer_toggle_intent>() }; layer != nullptr)
        {
            overlay_ = layer->overlay;
            return true;
        }
        if (const auto* const floating { message.get<webview_floating_intent>() }; floating != nullptr)
        {
            floating_ = floating->mode;
            return true;
        }
        if (message.get<webview_floating_press_intent>() != nullptr)
        {
            ++floating_presses_;
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
        view.layer = overlay_ ? luil::webview_layer::overlay : luil::webview_layer::underlay;
        view.user_data_folder = user_data_folder_;
        view.report_presentation = true;
        // 되돌이 서버는 http다. 기본값(https만)으로는 열리지 않으므로 이 예제가
        // 무엇을 여는지를 명시한다 — 목록이 곧 그 웹뷰가 갈 수 있는 곳의 전부다.
        view.policy.allowed_schemes = { u8"http", u8"https" };
        view.policy.open_new_windows_here = open_new_windows_here_;
        view.navigate_revision = navigate_revision_;
        view.navigate_url = requested_url_;
        view.on_event = [](luil::win32::webview_event event) { return luil::app_message { webview_report_intent { std::move(event) } }; };
        return view;
    }

    std::vector<luil::win32::ui_popup> webview_page::make_popups(const float scale) const
    {
        if (floating_ != webview_floating::popup || slot_ == nullptr || scale <= 0.0f)
            return {};
        // 자리표의 오른쪽 아래 모서리 안쪽이다. 자리는 물리 픽셀로 배치돼 있고 popup의 자리는 논리 픽셀이다.
        const luil::rect_f slot { slot_->bounds() };
        luil::win32::ui_popup popup {};
        popup.id = u8"webview-floating";
        popup.width = floating_width;
        popup.height = floating_height;
        popup.x = (slot.x + slot.width) / scale - floating_width - floating_margin;
        popup.y = (slot.y + slot.height) / scale - floating_height - floating_margin;
        popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(make_floating_button(), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        // `dismiss`를 비워 둔다. 바깥을 누르거나 창이 움직여도 남아 있는 도구 단추라, 닫는 것은 확인란뿐이다.
        return { std::move(popup) };
    }

    std::unique_ptr<luil::ui_element> webview_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(scale);
        slot_ = nullptr;

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

        // 두 합성 층을 같은 화면에서 오간다. 기본값인 아래층도 여기서 확인한다.
        luil::check_config layer {};
        layer.owner = u8"webview-layer";
        layer.style = luil::check_style::checkbox;
        layer.label = u8"페이지를 자리표 위에 얹는다 (위층)";
        layer.checked = overlay_;
        layer.tooltip = u8"꺼져 있으면 자리표를 투명하게 비우고 그 아래에 깐다 (아래층, 기본값). 같은 웹뷰가 층만 옮긴다";
        layer.toggle = luil::make_message_action(webview_layer_toggle_intent { overlay_ == false });
        column->add(std::make_unique<luil::check_element>(std::move(layer)), luil::check_row_height);
        column->add_gap(6.0f);

        auto dialog { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_dialog_details_open }
            , luil::text_button_config { .text = u8"웹뷰 위에 대화 열기" })
        };
        dialog->set_action(luil::ui_trigger::left_click, luil::make_message_action(dialog_intent { dialog_kind::details }));
        column->add(std::move(dialog), { .length = 26.0f, .cross_length = 150.0f });
        column->add_gap(6.0f);

        // 웹뷰 위에 떠 있는 단추다. 두 확인란은 서로를 끈다 — 고른 쪽의 반대를 누르면 그쪽으로 옮긴다.
        luil::check_config floating_popup {};
        floating_popup.owner = u8"webview-floating-popup";
        floating_popup.style = luil::check_style::checkbox;
        floating_popup.label = u8"떠 있는 단추를 popup 표면으로 띄운다";
        floating_popup.checked = floating_ == webview_floating::popup;
        floating_popup.tooltip = u8"popup은 자기 창이라 위층과 아래층 모두에서 페이지 위에 보인다. 웹뷰 위에 띄울 UI는 이 길로 둔다";
        floating_popup.toggle = luil::make_message_action(webview_floating_intent { floating_ == webview_floating::popup ? webview_floating::none : webview_floating::popup });
        column->add(std::make_unique<luil::check_element>(std::move(floating_popup)), luil::check_row_height);
        column->add_gap(6.0f);
        luil::check_config floating_tree {};
        floating_tree.owner = u8"webview-floating-tree";
        floating_tree.style = luil::check_style::checkbox;
        floating_tree.label = u8"떠 있는 단추를 자리표와 같은 tree에 겹친다";
        floating_tree.checked = floating_ == webview_floating::tree;
        floating_tree.tooltip = u8"보이지 않는다. 아래층은 자리를 비우는 구멍이 단추까지 지우고, 위층은 페이지가 단추 위에 얹힌다";
        floating_tree.toggle = luil::make_message_action(webview_floating_intent { floating_ == webview_floating::tree ? webview_floating::none : webview_floating::tree });
        column->add(std::make_unique<luil::check_element>(std::move(floating_tree)), luil::check_row_height);
        column->add_gap(4.0f);
        std::u8string presses { u8"떠 있는 단추 눌림: " };
        for (const char digit : std::to_string(floating_presses_))
            presses.push_back(static_cast<char8_t>(digit));
        presses += u8"번";
        column->add(make_label(luil::ui_element_id { kind_text, u8"webview-floating-presses" }, std::move(presses), 11.0f, luil::label_color_role::dim), 16.0f);
        column->add_gap(6.0f);

        const tab* const current { selected() };
        const float body { height > 0.0f ? height - 390.0f : 320.0f };
        const float body_height { body > 80.0f ? body : 80.0f };

        if (current != nullptr && current->path.empty() == false)
        {
            // 위층이면 첫 페이지 픽셀이 자리표를 덮고, 아래층이면 웹뷰가 선 뒤 이 자리가 구멍으로 비워진다.
            luil::webview_config placeholder {};
            placeholder.webview = webview_id();
            placeholder.name = u8"문서 미리 보기";
            placeholder.placeholder = base_url_.empty() ? u8"되돌이 서버를 기다린다" : u8"웹뷰가 서지 않았다 — 런타임이 없거나 CPU 렌더러다";
            auto slot { std::make_unique<luil::webview_element>(luil::ui_element_id { luil::ui_element_kind::webview, webview_id() }, std::move(placeholder)) };
            slot_ = slot.get();
            if (floating_ == webview_floating::tree)
            {
                // 자리표 위에 같은 tree의 단추를 겹친다. **이렇게 두면 보이지 않는다** — 그것을 확인하는 견본이다.
                // 자리표가 먼저, 단추가 나중이라 그리는 순서로는 단추가 위다. 그래도 웹뷰는 이 tree의 그림과 다른
                // 층에서 합성되므로 순서가 뜻을 갖지 못한다 (webview_floating의 주석).
                auto layers { std::make_unique<luil::layer_element>(luil::ui_element_id { kind_layout, u8"webview-floating-layers" }) };
                layers->add(std::move(slot));
                luil::layer_item corner {};
                corner.horizontal = luil::layout_alignment::end;
                corner.vertical = luil::layout_alignment::end;
                corner.width = floating_width;
                corner.height = floating_height;
                corner.margin = luil::edge_insets::all(floating_margin);
                layers->add(make_floating_button(), corner);
                column->add(std::move(layers), { .length = body_height });
            }
            else
                column->add(std::move(slot), { .length = body_height });
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
}

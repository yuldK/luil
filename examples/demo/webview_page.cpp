#include "demo/webview_page.h"
#include "demo/basics_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/webview_element.h"

#include "luil/generated/codicons.h"

#include <windows.h>

#include <algorithm>
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
        // 떠 있는 판의 크기와 본문 가장자리에서 떨어진 거리다 (논리 픽셀). 본문이 낮으면 판도 그만큼 낮아진다.
        constexpr float floating_width { 380.0f };
        constexpr float floating_height { 240.0f };
        constexpr float floating_margin { 12.0f };
        constexpr float floating_title_height { 24.0f };

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
        if (const auto* const floating { message.get<webview_floating_toggle_intent>() }; floating != nullptr)
        {
            floating_ = floating->floating;
            return true;
        }
        if (const auto* const corner { message.get<webview_floating_corner_intent>() }; corner != nullptr)
        {
            floating_left_ = corner->left;
            floating_top_ = corner->top;
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

        // 웹뷰를 본문 위에 떠 있는 판에 붙인다. 모서리는 목표 상태로 말하는 확인란 둘이 고른다.
        luil::check_config floating {};
        floating.owner = u8"webview-floating";
        floating.style = luil::check_style::checkbox;
        floating.label = u8"웹뷰를 본문 위에 떠 있는 판에 붙인다";
        floating.checked = floating_;
        floating.tooltip = u8"판은 같은 tree의 element라 창 밖으로 나가지 못한다. 웹뷰는 판 안의 자리표를 따라가고, 어느 탭에서든 본문 위에 떠 있다";
        floating.toggle = luil::make_message_action(webview_floating_toggle_intent { floating_ == false });
        column->add(std::make_unique<luil::check_element>(std::move(floating)), luil::check_row_height);
        column->add_gap(6.0f);
        luil::stack_config corner_row {};
        corner_row.direction = luil::stack_direction::row;
        auto corners { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"webview-floating-corner" }, corner_row) };
        luil::check_config left {};
        left.owner = u8"webview-floating-left";
        left.style = luil::check_style::checkbox;
        left.label = u8"판을 왼쪽에 둔다";
        left.checked = floating_left_;
        left.toggle = luil::make_message_action(webview_floating_corner_intent { floating_left_ == false, floating_top_ });
        corners->add(std::make_unique<luil::check_element>(std::move(left)), 200.0f);
        luil::check_config top {};
        top.owner = u8"webview-floating-top";
        top.style = luil::check_style::checkbox;
        top.label = u8"판을 위에 둔다";
        top.checked = floating_top_;
        top.toggle = luil::make_message_action(webview_floating_corner_intent { floating_left_, floating_top_ == false });
        corners->add(std::make_unique<luil::check_element>(std::move(top)), 200.0f);
        column->add(std::move(corners), luil::check_row_height);
        column->add_gap(6.0f);

        const tab* const current { selected() };
        const float body { height > 0.0f ? height - 368.0f : 320.0f };
        const float body_height { body > 80.0f ? body : 80.0f };

        // 웹뷰의 자리표다. 본문에 서든 떠 있는 판 안에 서든 같은 element고, 웹뷰는 이것이 배치된 자리를 따라간다.
        //  - 위층이면 첫 페이지 픽셀이 자리표를 덮고, 아래층이면 웹뷰가 선 뒤 이 자리가 구멍으로 비워진다.
        const auto make_slot = [this] {
            luil::webview_config placeholder {};
            placeholder.webview = webview_id();
            placeholder.name = u8"문서 미리 보기";
            placeholder.placeholder = base_url_.empty() ? u8"되돌이 서버를 기다린다" : u8"웹뷰가 서지 않았다 — 런타임이 없거나 CPU 렌더러다";
            return std::make_unique<luil::webview_element>(luil::ui_element_id { luil::ui_element_kind::webview, webview_id() }, std::move(placeholder));
        };

        std::unique_ptr<luil::ui_element> content {};
        if (current != nullptr && current->path.empty() == false && floating_ == false)
            content = make_slot();
        else if (current != nullptr && current->key == u8"image")
        {
            luil::image_config picture {};
            picture.image = picture_;
            picture.fit = luil::image_fit::contain;
            content = std::make_unique<luil::image_element>(luil::ui_element_id { kind_image_preview, u8"webview-image" }, std::move(picture));
        }
        else
        {
            // 웹뷰가 판으로 옮겨 간 HTML 탭의 본문에도 이 글이 선다. 판 아래에 우리 element가 그대로 있음을 보인다.
            content = make_label(luil::ui_element_id { kind_text, u8"webview-text" }, std::u8string { text_document }, 13.0f, luil::label_color_role::primary);
        }

        if (floating_)
        {
            // **겹침 배치 하나면 된다.** 본문을 먼저, 판을 나중에 담아 판이 위에 선다. 판의 자리는 정렬과 여백이
            // 정하므로 본문 영역을 벗어나지 않는다 — 창 밖으로 나가는 popup 표면과 다른 점이다.
            auto layers { std::make_unique<luil::layer_element>(luil::ui_element_id { kind_layout, u8"webview-floating-layers" }) };
            layers->add(std::move(content));

            // 판의 테두리 안쪽으로 한 칸 들여 자리표를 앉힌다. 자리표가 테두리까지 덮으면 아래층의 구멍이 테두리를 지운다.
            luil::stack_config inner {};
            inner.padding = luil::edge_insets::all(1.0f);
            auto card { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"webview-floating-card" }, inner) };
            luil::stack_config title_row {};
            title_row.direction = luil::stack_direction::row;
            title_row.padding = luil::edge_insets::symmetric(8.0f, 0.0f);
            auto title { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"webview-floating-title-row" }, title_row) };
            title->add_flexible(make_label(luil::ui_element_id { kind_text, u8"webview-floating-title" }, u8"떠 있는 웹뷰", 11.0f, luil::label_color_role::dim));
            card->add(std::move(title), floating_title_height);
            card->add_flexible(make_slot());
            luil::panel_config surface {};
            surface.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
            surface.shadow = 0.5f;
            auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_card, u8"webview-floating" }, std::move(surface)) };
            panel->set_content(std::move(card));

            luil::layer_item place {};
            place.horizontal = floating_left_ ? luil::layout_alignment::start : luil::layout_alignment::end;
            place.vertical = floating_top_ ? luil::layout_alignment::start : luil::layout_alignment::end;
            place.width = floating_width;
            place.height = std::min(floating_height, body_height - floating_margin * 2.0f);
            place.margin = luil::edge_insets::all(floating_margin);
            layers->add(std::move(panel), place);
            column->add(std::move(layers), { .length = body_height });
        }
        else
            column->add(std::move(content), { .length = body_height });

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

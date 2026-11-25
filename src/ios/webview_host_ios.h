#pragma once

#include "host/webview_layout.h"
#include "luil/app/webview.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifdef __OBJC__
@class UIView;
#else
class UIView;
#endif

namespace luil::ios {
    // 주 창의 웹뷰들이다 (WKWebView). UI thread(메인 thread)에서만 산다 (webview-composition-design.md의 iOS 갈래).
    // macOS host(`macos/webview_host.h`)를 UIKit으로 옮긴 것이고, 부르는 꼴이 같다.
    //
    // 웹뷰는 Skia가 그리는 view의 **형제 UIView**다. 층마다 자리가 다르다.
    //  - 위층(`overlay`)은 Skia view 위에 서서 자리표의 픽셀을 덮는다.
    //  - 아래층(`underlay`)은 Skia view 아래에 서고, 그 자리를 Skia가 알파 0으로 비운 구멍(`frame_state::holes`)으로 보인다.
    //    손가락은 구멍 자리에서 Skia view를 지나 웹뷰로 간다 — Skia view의 `hitTest:withEvent:`가 `passes_through`이면 nil을 돌려준다.
    // popup은 별도 창이 아니라 같은 Skia view에 겹쳐 그려진다. 그래서 popup이 자리와 겹치면 그것도 가림이다 (host가 판정한다).
    //
    // 초점은 첫 응답자다. 페이지를 누르는 손가락(트랙패드 누름도)이 닿는 순간 논리 초점을 그 자리표로 옮기라고 알린다.
    //  - WKWebView의 첫 응답자는 웹뷰 자신이 아니라 그 안의 내용 view라 `becomeFirstResponder`를 가로채서는 누름을 놓친다.
    //    그래서 웹뷰에 손가락만 지켜보는 제스처 인식기를 단다 (닿으면 알리고 곧바로 실패한다 — 페이지의 손짓을 막지 않는다).
    //
    // 페이지와 앱의 다리는 WebView2와 같은 이름이다. 페이지에 `window.chrome.webview`(`postMessage`, `message` 사건)를 세우는
    // 스크립트를 넣는다 — WebView2용으로 쓴 페이지가 고치지 않고 돈다.
    class webview_host
    {
    public:
        // `container`는 창의 root view controller가 가진 그릇 view이고 `skia_view`는 그 자식인 Skia view다. 둘 다 host보다 오래 산다.
        // `surface`는 이 host가 맡는 창의 표면 id다 (주 창은 빈 값, 보조 창은 `ui_window::id`). 앵커가 같은 웹뷰만 세운다.
        webview_host(UIView* container, UIView* skia_view, std::u8string surface = {});
        webview_host(const webview_host&) = delete;
        webview_host(webview_host&&) = delete;
        webview_host& operator=(const webview_host&) = delete;
        webview_host& operator=(webview_host&&) = delete;
        ~webview_host();

        // 웹뷰 사건을 앱에 넘기는 길이다 (`app_host::post_app_message`).
        void set_deliver(std::function<void(app_message)> deliver);
        // 사용자가 페이지를 눌렀다. 논리 초점을 그 자리표로 옮기는 길이다.
        void set_focus_reporter(std::function<void(const std::u8string& id)> reporter);

        // frame의 웹뷰 목록과 맞춘다. 새 id는 세우고, 빠진 id는 거두고, 있는 id에는 정책과 새 revision의 명령을 낸다.
        //  - 앵커가 이 host의 표면 id인 웹뷰만 선다 (창마다 host 하나다).
        void synchronize(std::span<const ui_webview> wanted);
        // 자리를 앉힌다 (Skia view의 물리 픽셀). `surface_scale`은 Skia view의 배율이라 점(point)으로 옮기는 데 쓴다.
        void apply_layout(const std::u8string& id, const webview_layout& layout, float surface_scale);
        // 그 웹뷰가 아래층 구멍을 열어도 되는가 (섰고 첫 문서를 다 읽었다). 열지 않으면 자리표의 placeholder가 보인다.
        [[nodiscard]] bool opens_hole(const std::u8string& id) const noexcept;
        // Skia view의 점(물리 픽셀)이 보이는 아래층 웹뷰의 구멍 안인가. 참이면 손가락이 Skia view를 지나 웹뷰로 간다.
        [[nodiscard]] bool passes_through(float x, float y) const noexcept;
        // 그 점이 보이는 웹뷰(어느 층이든) 안인가. 참이면 Skia view의 hover(iPad 포인터)를 거둔다.
        [[nodiscard]] bool covers(float x, float y) const noexcept;
        // 키보드 초점(첫 응답자)을 페이지로 넘긴다. 서 있지 않으면 거짓이다. 편집 칸이 아니면 화면 키보드는 뜨지 않는다.
        bool move_focus_in(const std::u8string& id);
        // 보이는 웹뷰의 그림을 찍는다 (개발용 갈무리). 다 찍으면 `done`이 층과 자리와 그림(CGImage, 넘겨받은 쪽이 놓는다)을
        // 받는다. 자리는 Skia view의 물리 픽셀이다.
        struct snapshot_layer
        {
            webview_layer layer { webview_layer::underlay };
            int x { 0 };
            int y { 0 };
            int width { 0 };
            int height { 0 };
            void* image { nullptr };
        };
        void snapshot(std::function<void(std::vector<snapshot_layer>)> done) const;

        // 사건 배달을 끊고 웹뷰를 모두 거둔다. 멱등이다.
        void shutdown() noexcept;

        struct entry;

    private:
        void create(entry& target);
        void apply_policy(entry& target);
        void flush_commands(entry& target);
        void place(entry& target);
        void tear_down(entry& target) noexcept;
        [[nodiscard]] entry* find(const std::u8string& id) noexcept;
        [[nodiscard]] const entry* find(const std::u8string& id) const noexcept;

        UIView* container_ { nullptr };
        UIView* skia_view_ { nullptr };
        std::shared_ptr<std::function<void(app_message)>> deliver_ { std::make_shared<std::function<void(app_message)>>() };
        std::shared_ptr<std::function<void(const std::u8string&)>> focus_reporter_ { std::make_shared<std::function<void(const std::u8string&)>>() };
        std::u8string surface_ {};
        std::vector<std::unique_ptr<entry>> entries_ {};
    };
}

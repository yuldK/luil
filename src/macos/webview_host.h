#pragma once

#include "host/webview_layout.h"
#include "luil/app/webview.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifdef __OBJC__
@class NSView;
#else
class NSView;
#endif

namespace luil::macos {
    // 주 창의 웹뷰들이다 (WKWebView). UI thread(메인 thread)에서만 산다 (webview-composition-design.md의 macOS 갈래).
    //
    // 웹뷰는 Skia가 그리는 view의 **형제 NSView**다. 층마다 자리가 다르다.
    //  - 위층(`overlay`)은 Skia view 위에 서서 자리표의 픽셀을 덮는다.
    //  - 아래층(`underlay`)은 Skia view 아래에 서고, 그 자리를 Skia가 알파 0으로 비운 구멍(`frame_state::holes`)으로 보인다.
    //    포인터는 구멍 자리에서 Skia view를 지나 웹뷰로 간다 (`passes_through`).
    // popup은 별도 창이 아니라 같은 Skia view에 겹쳐 그려진다. 그래서 popup이 자리와 겹치면 그것도 가림이다 (host가 판정한다).
    //
    // 페이지와 앱의 다리는 WebView2와 같은 이름이다. 페이지에 `window.chrome.webview`(`postMessage`, `message` 사건)를 세우는
    // 스크립트를 넣는다 — WebView2용으로 쓴 페이지가 고치지 않고 돈다.
    class webview_host
    {
    public:
        // `container`는 창의 내용 view이고 `skia_view`는 그 자식인 Skia view다. 둘 다 host보다 오래 산다.
        webview_host(NSView* container, NSView* skia_view);
        webview_host(const webview_host&) = delete;
        webview_host(webview_host&&) = delete;
        webview_host& operator=(const webview_host&) = delete;
        webview_host& operator=(webview_host&&) = delete;
        ~webview_host();

        // 웹뷰 사건을 앱에 넘기는 길이다 (`app_host::post_app_message`).
        void set_deliver(std::function<void(app_message)> deliver);
        // 사용자가 페이지를 눌러 키보드 초점이 웹뷰로 갔다. 논리 초점을 그 자리표로 옮기는 길이다.
        void set_focus_reporter(std::function<void(const std::u8string& id)> reporter);

        // frame의 웹뷰 목록과 맞춘다. 새 id는 세우고, 빠진 id는 거두고, 있는 id에는 정책과 새 revision의 명령을 낸다.
        //  - 주 창(`anchor`가 빈 것)의 웹뷰만 선다. 보조 창이 없는 플랫폼이다.
        void synchronize(std::span<const ui_webview> wanted);
        // 자리를 앉힌다 (물리 픽셀). `surface_scale`은 Skia view의 배율이라 점(point)으로 옮기는 데 쓴다.
        void apply_layout(const std::u8string& id, const webview_layout& layout, float surface_scale);
        // 그 웹뷰가 아래층 구멍을 열어도 되는가 (섰고 첫 문서를 다 읽었다). 열지 않으면 자리표의 placeholder가 보인다.
        [[nodiscard]] bool opens_hole(const std::u8string& id) const noexcept;
        // Skia view의 점(물리 픽셀)이 보이는 아래층 웹뷰의 구멍 안인가. 참이면 포인터가 Skia view를 지나 웹뷰로 간다.
        [[nodiscard]] bool passes_through(float x, float y) const noexcept;
        // 그 점이 보이는 웹뷰(어느 층이든) 안인가. 참이면 Skia view의 hover·커서를 거둔다.
        [[nodiscard]] bool covers(float x, float y) const noexcept;
        // 키보드 초점을 페이지로 넘긴다. 서 있지 않으면 거짓이다.
        bool move_focus_in(const std::u8string& id);
        // 보이는 웹뷰의 그림을 찍는다 (개발용 갈무리, `LUIL_MACOS_INPUT_SCRIPT`의 `snapshot`). 다 찍으면 `done`이 층과 자리와
        // 그림(CGImage, 넘겨받은 쪽이 놓는다)을 받는다. 자리는 Skia view의 물리 픽셀이다.
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

        NSView* container_ { nullptr };
        NSView* skia_view_ { nullptr };
        std::shared_ptr<std::function<void(app_message)>> deliver_ { std::make_shared<std::function<void(app_message)>>() };
        std::shared_ptr<std::function<void(const std::u8string&)>> focus_reporter_ { std::make_shared<std::function<void(const std::u8string&)>>() };
        std::vector<std::unique_ptr<entry>> entries_ {};
    };
}

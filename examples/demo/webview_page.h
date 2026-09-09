#pragma once

// 웹뷰 페이지다: 탭 하나에 문서 하나씩, 갈래가 다른 문서들을 나란히 놓는다.
//
// **탭마다 그리는 쪽이 다르다.** HTML은 웹뷰가 풀고, 글과 그림은 우리 element가
// 그린다 — 그것이 이 페이지가 보이려는 것이다. 웹뷰 탭을 떠나면 웹뷰는 tree에서
// 자리표가 빠져 저절로 감춰지고, 돌아오면 읽던 문서가 그대로 있다.
//
// 여는 것을 **우리 서버**로 두는 이유는 예제가 인터넷·회사 프록시·남의 API 사정에
// 흔들리면 예제가 아니어서다 (network_page와 같은 판단이다).

#include "demo/common.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"
#include "luil/win32/webview.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct webview_tab_select_intent
    {
        std::u8string key {};
    };

    // 웹뷰가 낸 일이다 (라이브러리가 `on_event`로 만들어 준다).
    struct webview_report_intent
    {
        luil::win32::webview_event event {};
    };

    // 새 창 요청을 이 웹뷰에서 열지를 바꾼다 (`webview_policy::open_new_windows_here`).
    struct webview_new_window_toggle_intent
    {
        bool open { false };
    };

    class webview_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 되돌이 서버의 뿌리다. 서버가 서기 전에는 비어 있다.
        void set_base_url(std::u8string url);
        // 실행 파일 옆 `assets/`에서 그림 문서를 읽는다 (logic thread에서 한 번).
        void load_assets();

        // 이 frame에 실을 웹뷰다.
        //  - 웹뷰 탭이 아니어도 **싣고 있는다.** 목록에서 빼면 웹뷰가 사라지고 읽던
        //    문서도 함께 사라진다. 자리표가 없는 frame에서는 자리가 없어 저절로
        //    감춰질 뿐이다.
        //  - 브라우저 프로세스가 죽은 뒤에는 **다른 id로** 싣는다. 목록에서 뺐다가
        //    다시 싣는 것과 같은 일이고, 그것이 라이브러리가 새로 세우는 계기다
        //    (webview.h의 `browser_process_failed`).
        [[nodiscard]] std::optional<luil::win32::ui_webview> webview() const;

    private:
        // 탭 하나다.
        struct tab
        {
            std::u8string key {};
            std::u8string label {};
            // 비어 있지 않으면 웹뷰 탭이고, 이 경로를 연다 (뿌리에 붙는다).
            std::u8string path {};
        };

        [[nodiscard]] const tab* selected() const;

        std::vector<tab> tabs_ {};
        std::u8string selected_ { u8"doc" };
        std::u8string base_url_ {};
        std::u8string user_data_folder_ {};
        std::u8string requested_url_ {};
        std::uint64_t navigate_revision_ { 0 };
        // 마지막 사건 넷이다 (새것이 위). 하나만 두면 항해 끝이 그 앞의 권한 거절을
        // 곧바로 덮어 무엇이 있었는지 보이지 않는다.
        std::vector<std::u8string> recent_events_ {};
        bool open_new_windows_here_ { false };
        // 브라우저 프로세스가 죽은 횟수다. 웹뷰의 id에 붙여 **다른 웹뷰로** 다시 싣는다 —
        // 목록에서 뺐다가 다시 싣는 것과 같은 일을 한 frame에 한다 (webview.h).
        unsigned generation_ { 0 };
        [[nodiscard]] std::u8string webview_id() const;
        luil::ui_image picture_ {};
        // 읽지 못한 이유다 (값이지 글이 아니다 — `luil::image_decode_error`).
        // 이 페이지는 그림이 서지 않으면 빈 칸을 보이므로 화면에 적지는 않지만,
        // 이유를 버리면 "왜 안 뜨는가"가 디버거에서도 사라진다.
        luil::image_decode_error picture_error_ {};
    };
} // namespace demo

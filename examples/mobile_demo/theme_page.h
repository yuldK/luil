#pragma once

// 테마 페이지다: 테마 선호 셋과 키 컬러 카탈로그, 실행 중에 읽는 스타일 파일과 화면 마크업, 저장하면 다시 읽는 파일 감시.
// 고른 값은 frame에 실려 가고 실제 팔레트 선택은 UI thread가 한다.

#include "demo/hot_reload_sample.h"
#include "demo/markup_sample.h"
#include "demo/style_sheet_sample.h"
#include "luil/theme/appearance.h"
#include "mobile_demo/assets.h"
#include "mobile_demo/common.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    struct theme_intent
    {
        luil::theme_preference theme { luil::theme_preference::system };
    };

    struct accent_intent
    {
        std::u8string id {};
    };

    class theme_page
    {
    public:
        // `style_source`는 스타일 파일 견본의 바탕 글이다. 앱이 실은 LUIL_STYLE 글이다 (assets.h).
        // `markup_source`는 마크업 묶음이 처음 읽는 글이다. 앱이 실은 마크업 견본이다 (assets.h).
        explicit theme_page(std::u8string style_source = {}, std::u8string markup_source = {});

        // 스타일 파일 묶음의 메시지(`demo::style_sheet_intent`)도 받는다. 모바일에는 글 칸 대신 견본 단추만 있다.
        // 마크업 묶음의 견본 단추와 가지의 사건(`demo::markup_sample_intent` 등)도 받는다.
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 셸이 frame에 싣는 유효 선호다.
        [[nodiscard]] const luil::appearance_settings& appearance() const noexcept
        {
            return appearance_;
        }

        // 파일 감시 묶음의 단추를 셸이 받아 쓰는 자리다. 쓰기와 도우미는 core test에 들지 않는 셸이 맡는다 (hot_reload_sample.h).
        [[nodiscard]] const std::u8string& hot_reload_folder() const noexcept
        {
            return hot_reload_.folder();
        }

        void note_hot_reload_write_failure(const std::u8string_view error)
        {
            hot_reload_.note_write_failure(error);
        }

        // 도우미를 멈춘다. 결과가 앱 host를 거쳐 오므로 host보다 먼저 부른다 (`stop_workers`).
        void stop_hot_reload()
        {
            hot_reload_.stop();
        }

        // 셸이 frame(`ui_frame::style`)과 본문 배치에 같은 포인터로 싣는 스타일이다. 비어 있으면 내장 스타일이다.
        [[nodiscard]] const std::shared_ptr<const luil::ui_style>& style() const noexcept
        {
            return style_sheet_.style();
        }

    private:
        luil::appearance_settings appearance_ {};
        demo::style_sheet_state style_sheet_ {};
        demo::markup_sample_state markup_ {};
        demo::hot_reload_state hot_reload_ {};
    };

    // delegate가 `on_started`에서 부른다. 앱이 실은 글로 없는 파일을 만들고 도우미를 세운다. 도우미는 delegate가 host보다 오래 쥔다.
    //  - inline인 것은 페이지 소스만 묶는 core test가 도우미 구현 없이 링크되게 하려는 것이다 (`start_files_watch`와 같다).
    [[nodiscard]] inline std::unique_ptr<luil::hot_reloader> start_theme_hot_reload(luil::app_host& host, const std::filesystem::path& folder)
    {
        return demo::start_hot_reload(host, folder, style_json(), markup_xml());
    }
}

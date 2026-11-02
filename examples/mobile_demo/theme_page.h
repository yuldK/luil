#pragma once

// 테마 페이지다: 테마 선호 셋과 키 컬러 카탈로그, 실행 중에 읽는 스타일 파일.
// 고른 값은 frame에 실려 가고 실제 팔레트 선택은 UI thread가 한다.

#include "demo/style_sheet_sample.h"
#include "luil/theme/appearance.h"
#include "mobile_demo/common.h"

#include <memory>
#include <string>

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
        explicit theme_page(std::u8string style_source = {});

        // 스타일 파일 묶음의 메시지(`demo::style_sheet_intent`)도 받는다. 모바일에는 글 칸 대신 견본 단추만 있다.
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 셸이 frame에 싣는 유효 선호다.
        [[nodiscard]] const luil::appearance_settings& appearance() const noexcept
        {
            return appearance_;
        }

        // 셸이 frame(`ui_frame::style`)과 본문 배치에 같은 포인터로 싣는 스타일이다. 비어 있으면 내장 스타일이다.
        [[nodiscard]] const std::shared_ptr<const luil::ui_style>& style() const noexcept
        {
            return style_sheet_.style();
        }

    private:
        luil::appearance_settings appearance_ {};
        demo::style_sheet_state style_sheet_ {};
    };
}

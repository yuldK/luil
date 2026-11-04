#pragma once

// 테마 페이지다: 테마 선호 셋과 키 컬러 카탈로그, 실행 중에 읽는 스타일 파일과 화면 마크업, 저장하면 다시 읽는 파일 감시.
// 고른 값은 frame에 실려 가고 실제 팔레트 선택은 UI thread가 한다.

#include "demo/common.h"
#include "demo/hot_reload_sample.h"
#include "demo/markup_sample.h"
#include "demo/style_sheet_sample.h"
#include "luil/text/text_edit.h"
#include "luil/theme/appearance.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/win32/win32_window.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct theme_scroll_intent
    {
        float delta { 0.0f };
    };

    struct theme_intent
    {
        luil::theme_preference theme { luil::theme_preference::system };
    };

    struct accent_intent
    {
        std::u8string id {};
    };

    // 주 창의 배경 효과를 고른다. 셸이 frame에 싣는다 (`ui_frame::backdrop`).
    struct backdrop_intent
    {
        luil::window_backdrop backdrop { luil::window_backdrop::none };
    };

    // 라이브러리가 알린 배경 효과의 상태다 (`window_delegate::make_backdrop_message`).
    // 주 창과 견본 창이 같은 메시지로 온다. 어느 표면인지는 `report.surface`다.
    struct backdrop_report_intent
    {
        luil::win32::backdrop_report report {};
    };

    // 견본 보조 창·견본 메뉴의 배경 효과를 고른다 (`ui_window::backdrop`·`ui_popup::backdrop`).
    struct sample_window_backdrop_intent
    {
        luil::window_backdrop backdrop { luil::window_backdrop::none };
    };

    struct sample_popup_backdrop_intent
    {
        luil::window_backdrop backdrop { luil::window_backdrop::none };
    };

    struct sample_window_toggle_intent
    {
        bool open { false };
    };

    struct sample_window_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct sample_menu_toggle_intent
    {
        bool open { false };
    };

    // 스타일 JSON 칸의 흘림이다 (논리 픽셀).
    struct style_json_scroll_intent
    {
        float delta { 0.0f };
    };

    struct style_json_scroll_to_intent
    {
        float offset { 0.0f };
    };

    class theme_page
    {
    public:
        // `style_source`는 스타일 파일 묶음이 처음 싣는 글이다. 데모 빌드가 실은 LUIL_STYLE 글이고, 견본의 바탕이다.
        // `markup_source`는 마크업 묶음이 처음 읽는 글이다. 데모 빌드가 실은 assets/markup_sample.xml이다.
        explicit theme_page(std::u8string style_source = {}, std::u8string markup_source = {});

        // 편집 메시지(edit_intent·composition_intent)는 스타일 JSON 칸(`target_style_json`)과 마크업 slot 칸(`target_markup_search`)의
        // 것만 받는다.
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 셸이 frame에 싣는 유효 선호다.
        [[nodiscard]] const luil::appearance_settings& appearance() const noexcept
        {
            return appearance_;
        }

        // 셸이 frame에 싣는 배경 효과다.
        [[nodiscard]] luil::window_backdrop backdrop() const noexcept
        {
            return backdrop_;
        }

        // 셸이 frame(`ui_frame::style`)과 주 tree 배치에 같은 포인터로 싣는 스타일이다. 비어 있으면 내장 스타일이다.
        [[nodiscard]] const std::shared_ptr<const luil::ui_style>& style() const noexcept
        {
            return style_sheet_.style();
        }

        // 파일 감시 묶음의 단추를 셸이 받아 쓰는 자리다. 쓰기와 도우미는 core test에 들지 않는 셸이 맡는다 (hot_reload_sample.h).
        //  - 내장 글은 처음에 정해 바뀌지 않아 도우미를 세우는 UI thread도 읽는다.
        [[nodiscard]] const std::u8string& style_source() const noexcept
        {
            return style_sheet_.source();
        }

        [[nodiscard]] const std::u8string& markup_source() const noexcept
        {
            return markup_.source();
        }

        [[nodiscard]] const std::u8string& hot_reload_folder() const noexcept
        {
            return hot_reload_.folder();
        }

        void note_hot_reload_write_failure(const std::u8string_view error)
        {
            hot_reload_.note_write_failure(error);
        }

        // 배경 효과를 고르고 시작한다 (`--backdrop=`).
        void set_backdrop(const luil::window_backdrop backdrop) noexcept
        {
            backdrop_ = backdrop;
        }

        // 효과를 건 견본 보조 창이다. 셸이 frame의 windows 목록에 싣는다.
        [[nodiscard]] std::vector<luil::win32::ui_window> make_windows() const;
        // 효과를 건 견본 메뉴다. 이 페이지의 tree를 만든 뒤에 단다 — 메뉴 단추의 자리를 쓴다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale) const;
        // 페이지를 떠날 때 견본 메뉴를 닫는다.
        void close_popups() noexcept
        {
            sample_menu_open_ = false;
        }

    private:
        [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_sample_tree() const;
        // 스타일 파일 묶음이다. 칸·단추 줄·상태 줄과 스타일 정책을 따르는 여러 줄 label을 카드에 담는다.
        //  - `scale`은 그 label을 재는 배율이다. 상태 줄이 build에서 잰 줄 수를 말한다.
        void add_style_sheet(page_layout& page, float scale) const;
        void handle_style_sheet(style_sheet_action action);
        // 칸의 글을 통째로 바꾼다. 실행 취소 한 덩어리다.
        void set_style_text(std::u8string_view text);
        // 마크업 묶음이다. 견본 글을 펼친 가지, 견본 단추 줄, 상태 줄을 카드에 담는다.
        void add_markup(page_layout& page, float width, float scale) const;
        // 파일 감시 묶음이다. 감시하는 폴더, 파일에 쓰는 단추 넷, 상태 줄을 카드에 담는다.
        void add_hot_reload(page_layout& page, float width, float scale) const;
        // 도우미의 결과를 받는다. 받은 값은 위의 두 묶음으로 가고, 값이 없는 결과는 상태 줄만 바꾼다.
        void apply_hot_reload(const luil::hot_reload_update& update);

        hot_reload_state hot_reload_ {};
        style_sheet_state style_sheet_ {};
        luil::text::text_edit_state style_json_ {};
        std::optional<luil::text_composition_event> style_composition_ {};
        float style_json_scroll_ { 0.0f };
        std::shared_ptr<luil::text_area_wrap_cache> style_json_wrap_ { std::make_shared<luil::text_area_wrap_cache>() };

        markup_sample_state markup_ {};
        // 마크업 slot에 C++가 짓는 한 줄 칸의 글이다.
        luil::text::text_edit_state markup_search_ {};
        std::optional<luil::text_composition_event> markup_search_composition_ {};

        luil::appearance_settings appearance_ {};
        luil::window_backdrop backdrop_ { luil::window_backdrop::none };
        // 마지막으로 알린 상태다. 알림이 오기 전에는 비어 있다.
        std::optional<luil::win32::backdrop_report> report_ {};
        // 견본 창·견본 메뉴다. 견본 창의 상태 보고도 따로 든다 (popup은 알리지 않는다).
        luil::window_backdrop sample_window_backdrop_ { luil::window_backdrop::acrylic };
        luil::window_backdrop sample_popup_backdrop_ { luil::window_backdrop::acrylic };
        bool sample_window_open_ { false };
        bool sample_menu_open_ { false };
        sample_window_metrics_intent sample_metrics_ {};
        std::optional<luil::win32::backdrop_report> sample_report_ {};
        // 견본 메뉴가 붙는 단추다. 이번 frame의 tree가 소유하며 `build`가 다시 잡는다.
        const luil::ui_element* sample_menu_button_ { nullptr };
        float scroll_ { 0.0f };
    };
}

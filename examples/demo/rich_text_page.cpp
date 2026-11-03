#include "demo/rich_text_page.h"

#include "luil/ui/dialog_elements.h"

#include <algorithm>
#include <utility>

namespace demo {
    namespace {
        // 서식 칸의 높이와 토글 사이 간격이다 (논리 픽셀). 토글 줄은 글자 버튼 높이다.
        constexpr float editor_height { 160.0f };
        constexpr float toolbar_spacing { 6.0f };
    }

    bool rich_text_page::handle(const luil::app_message& message)
    {
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            // 편집 메시지는 target으로 나눠 갖는다. 남의 칸이면 다음 페이지가 받는다.
            if (edit->request.target != target_rich_editor)
                return false;
            sample_.apply_edit(edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_rich_editor)
                return false;
            // 조합이 끝나면 표시 상태를 버린다. 확정된 글은 별도의 편집 요청으로 이미 들어왔다.
            if (composition->event.composing)
                composition_ = composition->event;
            else
                composition_.reset();
            return true;
        }
        if (const auto* const scroll { message.get<rich_text_page_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_ + scroll->delta);
            return true;
        }
        return sample_.handle(message);
    }

    std::unique_ptr<luil::ui_element> rich_text_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        page_layout page { u8"rich-text" };
        page.heading(u8"서식 있는 글", u8"굵게 · 기울임 · 밑줄 · 코드 · 링크 · 서식 실행 취소");
        page.add(make_label(luil::ui_element_id { kind_text, u8"rich-help" }, u8"범위를 골라 토글이나 Ctrl+B·I·U·K를 누른다. 고른 곳 없이 누르면 다음에 칠 글자의 서식이다.", 11.0f, luil::label_color_role::dim),
            18.0f);
        page.gap(6.0f);
        page.add(sample_.make_toolbar(kind_layout, toolbar_spacing), luil::text_button_height);
        page.gap(6.0f);
        page.add(sample_.make_editor(kind_rich_editor_input, luil::make_text_input_view(sample_.state(), composition_, target_rich_editor)), editor_height);
        page.gap(6.0f);
        page.add(make_label(luil::ui_element_id { kind_text, u8"rich-state" }, sample_.status_text(), 11.0f, luil::label_color_role::dim), 16.0f);
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(rich_text_page_scroll_intent { delta }); });
    }
}

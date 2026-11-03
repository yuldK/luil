#pragma once

// 두 데모가 함께 쓰는 서식 있는 글 칸이다 (docs/rich-edit-design.md). 편집 상태와 서식 presenter를 들고, 칸·도구 막대·상태 글을
// 짓는다. 페이지는 kind와 편집 대상, 조합 상태만 넘긴다.
//  - 도구 막대 토글과 칸의 Ctrl+B·I·U·K가 같은 메시지(`rich_text_command_intent`)를 낸다.
//  - 상태 글은 label로 그려 UIA Name과 화면으로 되읽는다.

#include "luil/text/text_edit.h"
#include "luil/ui/app_message.h"
#include "luil/ui/check_element.h"
#include "luil/ui/rich_text.h"
#include "luil/ui/rich_text_commands.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demo {
    // 서식 명령 하나다. 도구 막대 토글과 칸의 키 처리기가 낸다.
    struct rich_text_command_intent
    {
        luil::rich_text_command command { luil::rich_text_command::bold };
    };

    // 서식 칸의 세로 스크롤이다 (논리 픽셀). 위치는 앱 상태다.
    struct rich_text_scroll_intent
    {
        float delta { 0.0f };
    };

    struct rich_text_scroll_to_intent
    {
        float offset { 0.0f };
    };

    // 링크 토글이 거는 고정 주소다. 데모는 주소를 받는 대화 상자 없이 이 주소를 걸고, 켜져 있으면 걷는다.
    inline constexpr std::u8string_view rich_text_link_address { u8"https://example.com/" };

    class rich_text_sample
    {
    public:
        rich_text_sample()
        {
            // 서식 넷과 링크 하나가 걸린 견본 글이다. 처음 화면에서 구간과 링크가 보이고 상태 글이 그것을 센다.
            const std::u8string text { u8"굵게 기울임 밑줄 코드 링크\n범위를 골라 도구 막대나 Ctrl+B·I·U·K로 서식을 켜고 끈다." };
            luil::text::text_format format {};
            const auto mark = [&](const std::u8string_view word, const luil::text::text_mark marks) {
                const std::size_t at { text.find(word) };
                format.runs.push_back({ at, at + word.size(), marks });
            };
            mark(u8"굵게", luil::text::text_mark::bold);
            mark(u8"기울임", luil::text::text_mark::italic);
            mark(u8"밑줄", luil::text::text_mark::underline);
            mark(u8"코드", luil::text::text_mark::code);
            const std::size_t link { text.find(u8"링크") };
            format.links.push_back({ link, link + std::u8string_view { u8"링크" }.size(), std::u8string { rich_text_link_address } });
            luil::text::text_edit_set_formatted_text(state_, text, std::make_shared<const luil::text::text_format>(std::move(format)));
            presenter_.update(state_);
        }

        // 이 칸의 편집 요청을 적용한다. 같은 logic turn에서 구간을 다시 옮긴다. 늦으면 칸이 한 frame 낡은 offset으로 그린다.
        void apply_edit(const luil::text_edit_request& request)
        {
            luil::apply_text_edit(state_, request);
            presenter_.update(state_);
        }

        // 서식 명령과 스크롤 메시지를 받는다. 받았으면 참이다.
        bool handle(const luil::app_message& message)
        {
            if (const auto* const command { message.get<rich_text_command_intent>() }; command != nullptr)
            {
                // 링크는 대화 상자 대신 고정 주소다. 켜져 있으면 걷는다.
                if (command->command == luil::rich_text_command::link)
                    static_cast<void>(luil::set_rich_text_link(state_, luil::rich_text_state_of(state_).link ? std::u8string_view {} : rich_text_link_address));
                else
                    static_cast<void>(luil::apply_rich_text_command(state_, command->command));
                presenter_.update(state_);
                return true;
            }
            if (const auto* const scroll { message.get<rich_text_scroll_intent>() }; scroll != nullptr)
            {
                // 범위 다듬기는 칸이 한다. 아래로 넘친 값만 0으로 막는다.
                scroll_ = std::max(0.0f, scroll_ + scroll->delta);
                return true;
            }
            if (const auto* const scroll_to { message.get<rich_text_scroll_to_intent>() }; scroll_to != nullptr)
            {
                scroll_ = std::max(0.0f, scroll_to->offset);
                return true;
            }
            return false;
        }

        [[nodiscard]] const luil::text::text_edit_state& state() const noexcept
        {
            return state_;
        }

        // 서식 칸이다. 칸에 키 처리기를 달아 Ctrl+B·I·U·K를 서식 명령으로 바꾼다. 판정용 표는 정적이다.
        [[nodiscard]] std::unique_ptr<luil::text_area_element> make_editor(const luil::ui_element_kind kind, luil::text_input_view view) const
        {
            luil::text_area_config config {};
            config.placeholder = u8"서식 있는 글";
            config.scroll_offset = scroll_;
            config.scroll = [](const float delta) { return luil::make_app_action(rich_text_scroll_intent { delta }); };
            config.scroll_to = [](const float offset) { return luil::make_app_action(rich_text_scroll_to_intent { offset }); };
            config.wrap_cache = wrap_;
            config.spans = presenter_.spans();
            config.links = presenter_.links();
            auto editor { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind, u8"rich-editor" }, std::move(view), std::move(config)) };
            editor->set_key_handler(luil::make_rich_text_key_handler(luil::make_rich_text_commands({}),
                [](const luil::rich_text_command command) { return luil::make_app_action(rich_text_command_intent { command }); }));
            return editor;
        }

        // 서식 토글 다섯 줄이다. 누르는 동안 초점이 칸에 남아 IME 조합이 끊기지 않는다 (`set_preserve_pointer_focus`).
        [[nodiscard]] std::unique_ptr<luil::stack_element> make_toolbar(const luil::ui_element_kind layout_kind, const float spacing) const
        {
            const luil::rich_text_state on { luil::rich_text_state_of(state_) };
            luil::stack_config config {};
            config.direction = luil::stack_direction::row;
            config.spacing = spacing;
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { layout_kind, u8"rich-toolbar" }, config) };
            const auto add = [&row](std::u8string owner, std::u8string label, const bool checked, const luil::rich_text_command command) {
                luil::check_config toggle {};
                toggle.owner = std::move(owner);
                toggle.style = luil::check_style::toggle_button;
                toggle.label = std::move(label);
                toggle.checked = checked;
                toggle.toggle = luil::make_message_action(rich_text_command_intent { command });
                auto element { std::make_unique<luil::check_element>(std::move(toggle)) };
                element->set_preserve_pointer_focus(true);
                row->add_fit(std::move(element));
            };
            add(u8"rich-bold", u8"굵게", on.bold, luil::rich_text_command::bold);
            add(u8"rich-italic", u8"기울임", on.italic, luil::rich_text_command::italic);
            add(u8"rich-underline", u8"밑줄", on.underline, luil::rich_text_command::underline);
            add(u8"rich-code", u8"코드", on.code, luil::rich_text_command::code);
            add(u8"rich-link", u8"링크", on.link, luil::rich_text_command::link);
            return row;
        }

        // label `rich-state`가 그리는 글이다. 앞의 다섯은 지금 켜짐, 뒤의 셋은 서식 구간 수·링크 수·칸 기록의 되돌리기 수다.
        //  - 좁은 화면(모바일)은 한 줄 label에 다 들지 않아 두 조각(`marks_text`·`counts_text`)을 두 줄에 나눠 그린다.
        [[nodiscard]] std::u8string status_text() const
        {
            return marks_text() + u8" · " + counts_text();
        }

        // "굵게 켜짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐"
        [[nodiscard]] std::u8string marks_text() const
        {
            const luil::rich_text_state on { luil::rich_text_state_of(state_) };
            const auto word = [](const std::u8string_view name, const bool value) { return std::u8string { name } + (value ? u8" 켜짐" : u8" 꺼짐"); };
            return word(u8"굵게", on.bold) + u8" · " + word(u8"기울임", on.italic) + u8" · " + word(u8"밑줄", on.underline) + u8" · " + word(u8"코드", on.code)
                + u8" · " + word(u8"링크", on.link);
        }

        // "구간 4 · 링크 1 · 되돌리기 0"
        [[nodiscard]] std::u8string counts_text() const
        {
            const std::size_t runs { state_.format != nullptr ? state_.format->runs.size() : 0 };
            const std::size_t links { state_.format != nullptr ? state_.format->links.size() : 0 };
            return u8"구간 " + number(runs) + u8" · 링크 " + number(links) + u8" · 되돌리기 " + number(state_.undo.size());
        }

    private:
        [[nodiscard]] static std::u8string number(const std::size_t value)
        {
            const std::string text { std::to_string(value) };
            return { text.begin(), text.end() };
        }

        luil::text::text_edit_state state_ {};
        luil::rich_text_presenter presenter_ {};
        float scroll_ { 0.0f };
        std::shared_ptr<luil::text_area_wrap_cache> wrap_ { std::make_shared<luil::text_area_wrap_cache>() };
    };
}

#pragma once

// 데모의 "마크업" 묶음이다 (docs/markup-design.md·docs/markup-binding-design.md의 확인).
//  - 앱이 실행 중에 마크업 글을 읽어(`parse_ui_markup`) 받으면 틀을 쥐고, 막히면 쥔 틀을 그대로 둔다. frame마다 그 틀을
//    앱 상태로 펼친다(`build_ui_markup`). 데스크톱 demo와 모바일 demo, page test가 이 파일 하나를 함께 쓴다
//    (style_sheet_sample.h처럼).
//  - 상태 줄(`markup-status`)이 읽기 거부와 펼침 오류를 갈라 말한다. UIA Name과 화면으로 되읽는다.
//  - 견본의 바탕은 데모 빌드가 실은 견본 글(assets/markup_sample.xml)이다. 글을 고쳐 짓고 파일을 읽지 않는다. 파일을 읽는 길은 파일 감시 묶음이다
//    (hot_reload_sample.h).

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_markup.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demo {
    // 마크업 묶음의 단추가 싣는 조작이다.
    enum class markup_sample_action
    {
        // 견본 글을 처음부터 다시 읽는다.
        reload,
        // label의 `color`를 `colr`로 바꾼 견본이다. 읽기가 막혀 이전 틀이 남는다.
        sample_unknown_attribute,
        // 뿌리의 닫는 태그를 지운 견본이다. 문법 오류로 막힌다.
        sample_broken,
        // `{countText}`를 `{cuont}`로 바꾼 견본이다. 읽기는 받고 펼칠 때 `unknown_value`다. 가지는 선다.
        sample_missing_value,
    };

    struct markup_sample_intent
    {
        markup_sample_action action { markup_sample_action::reload };
    };

    // 마크업 가지의 사건이다 (`action` 속성의 이름을 앱이 이 메시지로 바꾼다).
    struct markup_increment_intent
    {
    };

    struct markup_toggle_list_intent
    {
    };

    struct markup_select_intent
    {
        std::u8string key {};
    };

    // 견본 글이다. `reload`는 바탕 글 그대로다.
    [[nodiscard]] inline std::u8string markup_sample(const std::u8string_view source, const markup_sample_action action)
    {
        std::u8string text { source };
        const auto replace_first = [&text](const std::u8string_view from, const std::u8string_view to) {
            if (const std::size_t at { text.find(from) }; at != std::u8string::npos)
                text.replace(at, from.size(), to);
        };
        switch (action)
        {
        case markup_sample_action::sample_unknown_attribute:
            replace_first(u8" color=\"primary\"", u8" colr=\"primary\"");
            break;
        case markup_sample_action::sample_broken:
            if (const std::size_t close { text.rfind(u8"</stack>") }; close != std::u8string::npos)
                text.erase(close);
            break;
        case markup_sample_action::sample_missing_value:
            replace_first(u8"{countText}", u8"{cuont}");
            break;
        case markup_sample_action::reload:
            break;
        }
        return text;
    }

    // 마크업 묶음의 상태다. 쥔 틀과 가지가 읽는 앱 상태(누른 횟수·목록 보기·고른 항목)를 든다.
    class markup_sample_state
    {
    public:
        // 가지 하나다. 펼친 뿌리(배치 전)와 상태 줄의 글이다. 틀이 없으면 뿌리가 비어 있다.
        struct built_branch
        {
            std::unique_ptr<luil::ui_element> element {};
            std::u8string status {};
        };

        // slot을 짓는 앱 함수다 (`ui_markup_context::make_slot`). 데스크톱은 한 줄 칸, 모바일은 label을 짓는다.
        using slot_factory = std::function<std::unique_ptr<luil::ui_element>(std::u8string_view name, const luil::ui_markup_scope& scope)>;

        explicit markup_sample_state(std::u8string source = {})
            : source_ { std::move(source) }
        {
            read(source_);
        }

        // 묶음의 단추와 가지의 사건을 받는다.
        bool handle(const luil::app_message& message)
        {
            if (const auto* const sample { message.get<markup_sample_intent>() }; sample != nullptr)
            {
                read(markup_sample(source_, sample->action));
                return true;
            }
            if (message.get<markup_increment_intent>() != nullptr)
            {
                ++clicks_;
                return true;
            }
            if (message.get<markup_toggle_list_intent>() != nullptr)
            {
                show_list_ = show_list_ == false;
                return true;
            }
            if (const auto* const select { message.get<markup_select_intent>() }; select != nullptr)
            {
                selected_ = select->key;
                return true;
            }
            return false;
        }

        // 다시 읽은 틀을 받는다 (`hot_reload_update`). 해석은 도우미가 마쳤다. 다음 펼치기가 새 틀로 서고 초점·값은 id로 이어진다.
        void accept(std::shared_ptr<const luil::ui_markup_document> document)
        {
            document_ = std::move(document);
            refused_.clear();
        }

        // 쥔 틀을 이번 상태로 펼친다. 앱 함수를 부르므로 logic thread(페이지의 build)에서 부른다.
        //  - `kinds`는 검사기에 주는 것과 같은 이름표다. 견본의 `kind="layout"`이 그 앱 kind가 된다.
        [[nodiscard]] built_branch build(const std::span<const luil::inspector_kind_name> kinds, slot_factory make_slot) const
        {
            built_branch branch {};
            if (document_ == nullptr)
            {
                branch.status = refused_;
                return branch;
            }
            luil::ui_markup_scope values {};
            values.set(u8"countText", u8"누른 횟수 " + count_text(clicks_)).set(u8"showList", show_list_).set(u8"items", items());
            const luil::ui_markup_context context {
                .values = &values,
                .make_action = [](const luil::ui_markup_action& action) -> luil::input_action {
                    if (action.name == u8"increment")
                        return luil::make_app_action(markup_increment_intent {});
                    if (action.name == u8"toggleList")
                        return luil::make_app_action(markup_toggle_list_intent {});
                    if (action.name == u8"select")
                        return luil::make_app_action(markup_select_intent { std::u8string { action.argument } });
                    return {};
                },
                .make_slot = std::move(make_slot),
                .kinds = kinds,
            };
            luil::ui_markup_build_result built { luil::build_ui_markup(*document_, context) };
            branch.element = std::move(built.element);
            if (refused_.empty() == false)
            {
                branch.status = refused_;
                return branch;
            }
            branch.status = u8"펼침 · 요소 " + count_text(built.built) + u8" · 오류 " + count_text(built.errors.size());
            if (built.errors.empty() == false)
                branch.status += u8" · " + luil::describe(built.errors.front());
            return branch;
        }

        // 견본 글의 바탕이다 (데모 빌드가 실은 글).
        [[nodiscard]] const std::u8string& source() const noexcept
        {
            return source_;
        }

        [[nodiscard]] std::size_t clicks() const noexcept
        {
            return clicks_;
        }

        [[nodiscard]] bool show_list() const noexcept
        {
            return show_list_;
        }

        [[nodiscard]] const std::u8string& selected() const noexcept
        {
            return selected_;
        }

        // 반복 단추의 key다. page test가 owner를 짓는다.
        [[nodiscard]] static constexpr std::array<std::u8string_view, 3> item_keys() noexcept
        {
            return { u8"apple", u8"pear", u8"plum" };
        }

    private:
        // 글을 읽는다. 받으면 새 틀을 쥐고, 막히면 쥔 틀을 그대로 둔다. 화면은 이전 틀로 남는다.
        void read(const std::u8string_view text)
        {
            const luil::ui_markup_parse_result parsed { luil::parse_ui_markup(text) };
            if (parsed.ok())
            {
                document_ = parsed.document;
                refused_.clear();
                return;
            }
            refused_ = u8"거부 · 오류 " + count_text(parsed.errors.size()) + u8" · " + luil::describe(parsed.errors.front()) + u8" · 이전 틀 유지";
        }

        [[nodiscard]] luil::ui_markup_list items() const
        {
            constexpr std::array<std::u8string_view, 3> titles { u8"사과", u8"배", u8"자두" };
            std::vector<luil::ui_markup_scope> rows {};
            for (std::size_t index { 0 }; index < titles.size(); ++index)
            {
                const std::u8string key { item_keys()[index] };
                luil::ui_markup_scope row {};
                row.set(u8"key", key).set(u8"title", std::u8string { titles[index] }).set(u8"selected", key == selected_);
                rows.push_back(std::move(row));
            }
            return std::make_shared<const std::vector<luil::ui_markup_scope>>(std::move(rows));
        }

        [[nodiscard]] static std::u8string count_text(const std::size_t count)
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), count) };
            return { reinterpret_cast<const char8_t*>(buffer.data()), static_cast<std::size_t>(result.ptr - buffer.data()) };
        }

        std::u8string source_ {};
        std::shared_ptr<const luil::ui_markup_document> document_ {};
        // 마지막 읽기가 막혔을 때의 상태 줄이다. 받으면 비운다.
        std::u8string refused_ {};
        std::size_t clicks_ { 0 };
        bool show_list_ { false };
        std::u8string selected_ {};
    };
}

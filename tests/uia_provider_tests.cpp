#include "win32/uia_provider.h"

#include "luil/ui/app_message.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/group_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/list_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <uiautomation.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 자식 전부에게 자기 slot을 주는 최소 컨테이너다 (accessibility_tests와 같다).
    class test_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // 창 없이 만든다 — 화면 좌표 변환이 빠질 뿐 경로는 같다
    // (win32_drop_tests의 surface_drop_target과 같은 갈래).
    class fake_uia_host final : public luil::win32::uia_surface_host
    {
    public:
        std::shared_ptr<const luil::ui_tree> tree {};
        luil::ui_element_id focus {};

        [[nodiscard]] const luil::ui_tree* accessibility_tree() const noexcept override
        {
            return tree.get();
        }

        [[nodiscard]] HWND accessibility_window() const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] luil::ui_element_id accessibility_focus() const override
        {
            return focus;
        }

        // 실제 표면은 창의 다음 차례로 미뤄 배분한다. test는 받은 것만 적어 둔다.
        void accessibility_dispatch(std::vector<luil::input_action> actions) override
        {
            dispatched.insert(dispatched.end(), std::make_move_iterator(actions.begin()), std::make_move_iterator(actions.end()));
        }

        void accessibility_request_focus(const luil::ui_element_id& id) override
        {
            focus_requested = id;
        }

        // 텍스트 칸의 편집 파이프라인이다 — test가 값을 미리 꽂는다.
        [[nodiscard]] std::optional<luil::text_input_target> accessibility_text_target(const luil::ui_element_kind kind) const override
        {
            static_cast<void>(kind);
            return text_target;
        }

        void accessibility_text_edit(luil::text_edit_request request) override
        {
            edits.push_back(std::move(request));
        }

        [[nodiscard]] std::optional<RECT> accessibility_text_span(const luil::ui_element_id&, const std::u8string_view document, std::size_t, const std::size_t begin,
            const std::size_t end) const override
        {
            spanned_document = std::u8string { document };
            spanned_begin = begin;
            spanned_end = end;
            return text_span;
        }

        [[nodiscard]] std::optional<std::size_t> accessibility_text_offset(const luil::ui_element_id&, float, float) const override
        {
            return text_offset;
        }

        std::vector<luil::input_action> dispatched {};
        luil::ui_element_id focus_requested {};
        std::optional<luil::text_input_target> text_target {};
        std::vector<luil::text_edit_request> edits {};
        std::optional<RECT> text_span {};
        std::optional<std::size_t> text_offset {};
        // 구간 질의가 받은 인자다 (같은 문서·같은 구간이 넘어오는지 본다).
        mutable std::u8string spanned_document {};
        mutable std::size_t spanned_begin { 0 };
        mutable std::size_t spanned_end { 0 };
    };

    // 배분이 던지는 표면이다 — COM 경계 가드가 예외를 HRESULT로 바꾸는지 본다.
    class throwing_uia_host final : public luil::win32::uia_surface_host
    {
    public:
        std::shared_ptr<const luil::ui_tree> tree {};
        bool out_of_memory { true };

        [[nodiscard]] const luil::ui_tree* accessibility_tree() const noexcept override
        {
            return tree.get();
        }

        [[nodiscard]] HWND accessibility_window() const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] luil::ui_element_id accessibility_focus() const override
        {
            return {};
        }

        void accessibility_dispatch(std::vector<luil::input_action>) override
        {
            if (out_of_memory)
                throw std::bad_alloc {};
            throw std::runtime_error { "dispatch" };
        }

        void accessibility_request_focus(const luil::ui_element_id&) override
        {}

        [[nodiscard]] std::optional<luil::text_input_target> accessibility_text_target(luil::ui_element_kind) const override
        {
            return std::nullopt;
        }

        void accessibility_text_edit(luil::text_edit_request) override
        {}

        [[nodiscard]] std::optional<RECT> accessibility_text_span(const luil::ui_element_id&, std::u8string_view, std::size_t, std::size_t, std::size_t) const override
        {
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::size_t> accessibility_text_offset(const luil::ui_element_id&, float, float) const override
        {
            return std::nullopt;
        }
    };

    // 실행이 실제로 나갔는지 알아보는 메시지다.
    struct probe_intent
    {
        std::u8string tag {};
    };

    struct probe_delta_intent
    {
        float delta { 0.0f };
    };

    // 절대 값·절대 상태를 나르는 메시지다.
    struct probe_value_intent
    {
        float value { 0.0f };
    };

    struct probe_state_intent
    {
        bool state { false };
    };

    [[nodiscard]] luil::ui_action tagged_action(std::u8string tag)
    {
        return [tag = std::move(tag)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(probe_intent { tag }) }; };
    }

    // 표면이 받은 액션 하나의 표식이다.
    [[nodiscard]] std::u8string dispatched_tag(const fake_uia_host& host)
    {
        REQUIRE(host.dispatched.size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&host.dispatched.front()) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<probe_intent>() };
        REQUIRE(intent != nullptr);
        return intent->tag;
    }

    [[nodiscard]] float dispatched_value(const fake_uia_host& host)
    {
        REQUIRE(host.dispatched.size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&host.dispatched.front()) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<probe_value_intent>() };
        REQUIRE(intent != nullptr);
        return intent->value;
    }

    constexpr luil::ui_element_kind kind_panel { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_item { luil::application_element_kind(1) };

    // 라벨·막대·글자 버튼이 든 화면 하나다.
    // root panel은 구조라 접히고 셋이 표면 root의 자식으로 승격된다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_page_tree(const float slider_value = 3.0f)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        luil::label_config note {};
        note.text = u8"안내";
        root->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, u8"hint" }, std::move(note)));
        root->add(std::make_unique<luil::slider_element>(luil::ui_element_id { kind_item, u8"volume" },
            luil::slider_config { .minimum = 0.0f, .maximum = 10.0f, .value = slider_value }));
        root->add(std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"confirm" }, luil::text_button_config { .text = u8"확인" }));
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    }

    // 부모 panel 아래 버튼 하나다. 숨길 때도 **부모만** 숨긴다 — 자식 플래그는
    // 참으로 남는다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_section_tree(const bool section_hidden)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        auto section { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"section" }) };
        auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"child" }, luil::text_button_config { .text = u8"자식" }) };
        button->set_action(luil::ui_trigger::left_click, tagged_action(u8"child"));
        section->add(std::move(button));
        section->set_visible(section_hidden == false);
        root->add(std::move(section));
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    }

    [[nodiscard]] std::wstring read_text_property(luil::win32::uia_element_provider& provider, const PROPERTYID property)
    {
        VARIANT value {};
        REQUIRE(provider.GetPropertyValue(property, &value) == S_OK);
        REQUIRE(value.vt == VT_BSTR);
        std::wstring text { value.bstrVal, SysStringLen(value.bstrVal) };
        static_cast<void>(VariantClear(&value));
        return text;
    }

    // "stem" 뒤에 번호를 붙인 owner다 (긴 목록·동적 id test가 쓴다).
    [[nodiscard]] std::u8string numbered_owner(const std::u8string_view stem, const std::size_t number)
    {
        const std::string digits { std::to_string(number) };
        std::u8string owner { stem };
        owner.append(digits.begin(), digits.end());
        return owner;
    }

    [[nodiscard]] int runtime_key_of(luil::win32::uia_element_provider& provider)
    {
        SAFEARRAY* array { nullptr };
        REQUIRE(provider.GetRuntimeId(&array) == S_OK);
        REQUIRE(array != nullptr);
        LONG index { 0 };
        int marker { 0 };
        REQUIRE(SafeArrayGetElement(array, &index, &marker) == S_OK);
        REQUIRE(marker == UiaAppendRuntimeId);
        index = 1;
        int key { 0 };
        REQUIRE(SafeArrayGetElement(array, &index, &key) == S_OK);
        static_cast<void>(SafeArrayDestroy(array));
        return key;
    }

    // 나간 이벤트를 그대로 적는 mock 창구다.
    // "각 변화에 정확히 한 번, 옳은 낱말로"를 이 기록으로 잰다.
    class recording_event_sink final : public luil::win32::uia_event_sink
    {
    public:
        struct record
        {
            std::u8string kind {};
            // 요소의 owner다. 표면 root면 비어 있다.
            std::u8string owner {};
            PROPERTYID property { 0 };
            double old_number { 0.0 };
            double new_number { 0.0 };
            std::wstring old_text {};
            std::wstring new_text {};
        };

        // 표면 root와 요소 provider를 가른다 (요소 provider만 owner를 안다).
        IRawElementProviderSimple* root { nullptr };
        std::vector<record> records {};

        void focus_changed(luil::win32::uia_element_provider& provider) override
        {
            records.push_back({ .kind = u8"focus", .owner = provider.element_id().owner });
        }

        void invoked(luil::win32::uia_element_provider& provider) override
        {
            records.push_back({ .kind = u8"invoked", .owner = provider.element_id().owner });
        }

        void property_changed(luil::win32::uia_element_provider& provider, const PROPERTYID property, const VARIANT& old_value, const VARIANT& new_value) override
        {
            record entry { .kind = u8"property", .owner = provider.element_id().owner, .property = property };
            const auto capture = [](const VARIANT& value, double& number, std::wstring& text) {
                if (value.vt == VT_I4)
                    number = static_cast<double>(value.lVal);
                else if (value.vt == VT_R8)
                    number = value.dblVal;
                else if (value.vt == VT_BSTR)
                    text.assign(value.bstrVal, SysStringLen(value.bstrVal));
            };
            capture(old_value, entry.old_number, entry.old_text);
            capture(new_value, entry.new_number, entry.new_text);
            records.push_back(std::move(entry));
        }

        void element_selected(luil::win32::uia_element_provider& provider) override
        {
            records.push_back({ .kind = u8"selected", .owner = provider.element_id().owner });
        }

        void structure_changed(IRawElementProviderSimple& provider) override
        {
            if (&provider == root)
            {
                records.push_back({ .kind = u8"structure" });
                return;
            }
            records.push_back({ .kind = u8"structure", .owner = static_cast<luil::win32::uia_element_provider&>(provider).element_id().owner });
        }
    };
} // namespace

TEST_CASE("A UIA provider answers name and control type from the current tree", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree();
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const button { root->make_element_provider({ kind_item, u8"confirm" }) };

    REQUIRE(read_text_property(*button, UIA_NamePropertyId) == L"확인");
    VARIANT type {};
    REQUIRE(button->GetPropertyValue(UIA_ControlTypePropertyId, &type) == S_OK);
    REQUIRE(type.vt == VT_I4);
    REQUIRE(type.lVal == UIA_ButtonControlTypeId);

    // 자동화 id는 kind:owner다.
    REQUIRE(read_text_property(*button, UIA_AutomationIdPropertyId) == L"65:confirm");

    button->Release();
    root->Release();
}

TEST_CASE("UIA navigation walks the collapsed access tree", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree();
    auto* const root { new luil::win32::uia_root_provider { host } };

    // 구조인 root panel은 접혀 라벨이 첫 자식으로 승격된다.
    IRawElementProviderFragment* first { nullptr };
    REQUIRE(root->Navigate(NavigateDirection_FirstChild, &first) == S_OK);
    REQUIRE(first != nullptr);
    auto* const label { static_cast<luil::win32::uia_element_provider*>(first) };
    REQUIRE(label->element_id().owner == u8"hint");

    IRawElementProviderFragment* second { nullptr };
    REQUIRE(label->Navigate(NavigateDirection_NextSibling, &second) == S_OK);
    REQUIRE(second != nullptr);
    auto* const slider { static_cast<luil::win32::uia_element_provider*>(second) };
    REQUIRE(slider->element_id().owner == u8"volume");

    // 감싸는 접근 요소가 없으면 부모는 표면 root다.
    IRawElementProviderFragment* parent { nullptr };
    REQUIRE(label->Navigate(NavigateDirection_Parent, &parent) == S_OK);
    REQUIRE(parent == static_cast<IRawElementProviderFragment*>(root));
    parent->Release();

    IRawElementProviderFragment* last { nullptr };
    REQUIRE(root->Navigate(NavigateDirection_LastChild, &last) == S_OK);
    REQUIRE(last != nullptr);
    REQUIRE(static_cast<luil::win32::uia_element_provider*>(last)->element_id().owner == u8"confirm");

    last->Release();
    slider->Release();
    label->Release();
    root->Release();
}

TEST_CASE("A display-only slider serves the range value pattern read-only", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree(3.0f);
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const slider { root->make_element_provider({ kind_item, u8"volume" }) };

    IUnknown* pattern { nullptr };
    REQUIRE(slider->GetPatternProvider(UIA_RangeValuePatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    IRangeValueProvider* range { nullptr };
    REQUIRE(pattern->QueryInterface(__uuidof(IRangeValueProvider), reinterpret_cast<void**>(&range)) == S_OK);

    double value { -1.0 };
    REQUIRE(range->get_Value(&value) == S_OK);
    REQUIRE(value == 3.0);
    REQUIRE(range->get_Minimum(&value) == S_OK);
    REQUIRE(value == 0.0);
    REQUIRE(range->get_Maximum(&value) == S_OK);
    REQUIRE(value == 10.0);
    BOOL read_only { FALSE };
    REQUIRE(range->get_IsReadOnly(&read_only) == S_OK);
    REQUIRE(read_only == TRUE);
    // 값을 받을 factory가 없는 막대다 — UIA의 낱말로는 접근 거부다.
    REQUIRE(range->SetValue(5.0) == E_ACCESSDENIED);
    range->Release();
    pattern->Release();

    // 범위가 없는 요소에는 패턴도 없다.
    luil::win32::uia_element_provider* const label { root->make_element_provider({ kind_item, u8"hint" }) };
    IUnknown* none { nullptr };
    REQUIRE(label->GetPatternProvider(UIA_RangeValuePatternId, &none) == S_OK);
    REQUIRE(none == nullptr);

    label->Release();
    slider->Release();
    root->Release();
}

TEST_CASE("A toggle control serves its state", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    page->add(std::make_unique<luil::check_element>(luil::check_config {
        .owner = u8"alerts",
        .label = u8"알림",
        .checked = true,
        .toggle = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; },
    }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const box { root->make_element_provider({ luil::ui_element_kind::check, u8"alerts" }) };

    IUnknown* pattern { nullptr };
    REQUIRE(box->GetPatternProvider(UIA_TogglePatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    IToggleProvider* toggle { nullptr };
    REQUIRE(pattern->QueryInterface(__uuidof(IToggleProvider), reinterpret_cast<void**>(&toggle)) == S_OK);
    ToggleState state { ToggleState_Indeterminate };
    REQUIRE(toggle->get_ToggleState(&state) == S_OK);
    REQUIRE(state == ToggleState_On);

    toggle->Release();
    pattern->Release();
    box->Release();
    root->Release();
}

TEST_CASE("Invoking through UIA runs the element's click", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"confirm" }, luil::text_button_config { .text = u8"확인" }) };
    button->set_action(luil::ui_trigger::left_click, tagged_action(u8"confirm"));
    page->add(std::move(button));
    auto quiet { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"off" }, luil::text_button_config { .text = u8"꺼짐" }) };
    quiet->set_action(luil::ui_trigger::left_click, tagged_action(u8"off"));
    quiet->set_enabled(false);
    page->add(std::move(quiet));
    page->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, u8"hint" }, luil::label_config { .text = u8"안내" }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    luil::win32::uia_element_provider* const confirm { root->make_element_provider({ kind_item, u8"confirm" }) };
    IUnknown* pattern { nullptr };
    REQUIRE(confirm->GetPatternProvider(UIA_InvokePatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    IInvokeProvider* invoke { nullptr };
    REQUIRE(pattern->QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(&invoke)) == S_OK);
    REQUIRE(invoke->Invoke() == S_OK);
    // 실행은 tree에 이미 있던 그 액션이다.
    REQUIRE(dispatched_tag(host) == u8"confirm");
    invoke->Release();
    pattern->Release();

    // 비활성은 지금 할 수 없는 일이다.
    host.dispatched.clear();
    luil::win32::uia_element_provider* const off { root->make_element_provider({ kind_item, u8"off" }) };
    REQUIRE(off->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));
    REQUIRE(host.dispatched.empty());

    // 누를 자리가 아니면 패턴도 실행도 없다.
    luil::win32::uia_element_provider* const hint { root->make_element_provider({ kind_item, u8"hint" }) };
    IUnknown* none { nullptr };
    REQUIRE(hint->GetPatternProvider(UIA_InvokePatternId, &none) == S_OK);
    REQUIRE(none == nullptr);
    REQUIRE(hint->Invoke() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));

    hint->Release();
    off->Release();
    confirm->Release();
    root->Release();
}

TEST_CASE("A control with state serves that state's pattern, not Invoke", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    page->add(std::make_unique<luil::check_element>(luil::check_config { .owner = u8"alerts", .label = u8"알림", .toggle = tagged_action(u8"alerts") }));
    page->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_item, u8"note" }, luil::text_input_view {}, luil::text_input_config { .placeholder = u8"메모" }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    // 체크박스가 Invoke와 Toggle을 함께 내걸면 어느 쪽으로 눌러야 하는지 모른다.
    luil::win32::uia_element_provider* const box { root->make_element_provider({ luil::ui_element_kind::check, u8"alerts" }) };
    IUnknown* none { nullptr };
    REQUIRE(box->GetPatternProvider(UIA_InvokePatternId, &none) == S_OK);
    REQUIRE(none == nullptr);
    REQUIRE(box->Toggle() == S_OK);
    REQUIRE(dispatched_tag(host) == u8"alerts");

    // 텍스트 칸은 **hit를 흡수하려고** 빈 클릭 액션을 든다 — 누를 수 있다고
    // 내걸면 S_OK로 아무 일도 하지 않는 거짓말이 된다.
    luil::win32::uia_element_provider* const note { root->make_element_provider({ kind_item, u8"note" }) };
    REQUIRE(note->GetPatternProvider(UIA_InvokePatternId, &none) == S_OK);
    REQUIRE(none == nullptr);
    // 편집 대상이 이어지지 않은 칸은 읽기 전용이다 (이 host는 대상을 답하지 않는다).
    BOOL read_only { FALSE };
    REQUIRE(note->get_IsReadOnly(&read_only) == S_OK);
    REQUIRE(read_only == TRUE);
    REQUIRE(note->SetValue(L"메모") == E_ACCESSDENIED);

    note->Release();
    box->Release();
    root->Release();
}

TEST_CASE("Selecting through UIA leaves an already selected item alone", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    page->add(std::make_unique<luil::check_element>(
        luil::check_config { .owner = u8"latte", .style = luil::check_style::radio, .label = u8"라떼", .checked = true, .toggle = tagged_action(u8"latte") }));
    page->add(std::make_unique<luil::check_element>(
        luil::check_config { .owner = u8"mocha", .style = luil::check_style::radio, .label = u8"모카", .toggle = tagged_action(u8"mocha") }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    // 라디오는 Toggle이 아니라 SelectionItem이다.
    luil::win32::uia_element_provider* const latte { root->make_element_provider({ luil::ui_element_kind::check, u8"latte" }) };
    IUnknown* none { nullptr };
    REQUIRE(latte->GetPatternProvider(UIA_TogglePatternId, &none) == S_OK);
    REQUIRE(none == nullptr);
    IUnknown* pattern { nullptr };
    REQUIRE(latte->GetPatternProvider(UIA_SelectionItemPatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    pattern->Release();

    // 이미 골라진 것을 다시 고르면 아무 메시지도 나가지 않는다 (클릭은 뒤집기다).
    REQUIRE(latte->Select() == S_OK);
    REQUIRE(host.dispatched.empty());
    // 하나뿐인 선택이라 더하기는 이미 골라진 것에만 뜻이 통하고 빼기는 무효다.
    REQUIRE(latte->AddToSelection() == S_OK);
    REQUIRE(latte->RemoveFromSelection() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));

    luil::win32::uia_element_provider* const mocha { root->make_element_provider({ luil::ui_element_kind::check, u8"mocha" }) };
    REQUIRE(mocha->Select() == S_OK);
    REQUIRE(dispatched_tag(host) == u8"mocha");
    REQUIRE(mocha->AddToSelection() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));

    mocha->Release();
    latte->Release();
    root->Release();
}

TEST_CASE("A selection container completes the SelectionItem contract", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    page->add(std::make_unique<luil::choice_group_element>(luil::choice_group_config {
        .owner = u8"coffee",
        .items = { { u8"latte", u8"라떼" }, { u8"mocha", u8"모카" } },
        .selected = u8"latte",
        .select = [](const std::u8string&) { return luil::input_action {}; },
    }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    // 선택 항목이 반환한 container는 Selection 패턴을 제공해야 한다.
    // 그래야 클라이언트가 선택 목록과 선택 정책을 조회할 수 있다.
    luil::win32::uia_element_provider* const latte { root->make_element_provider({ luil::ui_element_kind::choice, u8"latte" }) };
    IRawElementProviderSimple* container_simple { nullptr };
    REQUIRE(latte->get_SelectionContainer(&container_simple) == S_OK);
    REQUIRE(container_simple != nullptr);
    IUnknown* pattern { nullptr };
    REQUIRE(container_simple->GetPatternProvider(UIA_SelectionPatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    ISelectionProvider* selection { nullptr };
    REQUIRE(pattern->QueryInterface(__uuidof(ISelectionProvider), reinterpret_cast<void**>(&selection)) == S_OK);

    // 선택 목록은 골라진 항목 하나다.
    SAFEARRAY* array { nullptr };
    REQUIRE(selection->GetSelection(&array) == S_OK);
    REQUIRE(array != nullptr);
    LONG lower { 0 };
    LONG upper { -1 };
    REQUIRE(SafeArrayGetLBound(array, 1, &lower) == S_OK);
    REQUIRE(SafeArrayGetUBound(array, 1, &upper) == S_OK);
    REQUIRE(upper - lower + 1 == 1);
    IUnknown* item { nullptr };
    REQUIRE(SafeArrayGetElement(array, &lower, &item) == S_OK);
    REQUIRE(item != nullptr);
    // 항목 provider로 돌아온다 — 골라진 그 라디오다.
    REQUIRE(static_cast<luil::win32::uia_element_provider*>(static_cast<IRawElementProviderSimple*>(item))->element_id().owner == u8"latte");
    item->Release();
    static_cast<void>(SafeArrayDestroy(array));

    // 이 저장소의 선택은 하나뿐이고, 하나가 골라져 있으면 비울 수 없다.
    BOOL multiple { TRUE };
    REQUIRE(selection->get_CanSelectMultiple(&multiple) == S_OK);
    REQUIRE(multiple == FALSE);
    BOOL required { FALSE };
    REQUIRE(selection->get_IsSelectionRequired(&required) == S_OK);
    REQUIRE(required == TRUE);

    selection->Release();
    pattern->Release();
    container_simple->Release();
    latte->Release();
    root->Release();
}

TEST_CASE("A lone radio has no selection container and a list starts unselected", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    page->add(std::make_unique<luil::check_element>(
        luil::check_config { .owner = u8"pick", .style = luil::check_style::radio, .label = u8"하나", .checked = true, .toggle = tagged_action(u8"pick") }));
    luil::list_config files {};
    files.owner = u8"files";
    files.items = { { .key = u8"a", .label = u8"가" } };
    files.select = [](const std::u8string&) { return luil::input_action {}; };
    page->add(std::make_unique<luil::list_element>(std::move(files)));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    // 묶음 밖의 라디오는 container가 없다 (빈 답 — 패턴 없는 부모를 지어내지 않는다).
    luil::win32::uia_element_provider* const lone { root->make_element_provider({ luil::ui_element_kind::check, u8"pick" }) };
    IRawElementProviderSimple* container_simple { nullptr };
    REQUIRE(lone->get_SelectionContainer(&container_simple) == S_OK);
    REQUIRE(container_simple == nullptr);

    // 아직 아무것도 고르지 않은 목록은 선택이 비어 있고, 비어 있는 동안은
    // 골라야 하는 묶음이 아니다.
    luil::win32::uia_element_provider* const list { root->make_element_provider({ luil::ui_element_kind::list, u8"files" }) };
    SAFEARRAY* array { nullptr };
    REQUIRE(list->GetSelection(&array) == S_OK);
    REQUIRE(array != nullptr);
    LONG lower { 0 };
    LONG upper { -1 };
    REQUIRE(SafeArrayGetLBound(array, 1, &lower) == S_OK);
    REQUIRE(SafeArrayGetUBound(array, 1, &upper) == S_OK);
    REQUIRE(upper - lower + 1 == 0);
    static_cast<void>(SafeArrayDestroy(array));
    BOOL required { TRUE };
    REQUIRE(list->get_IsSelectionRequired(&required) == S_OK);
    REQUIRE(required == FALSE);

    // 항목이 아닌 요소는 Selection 패턴도 조회도 서지 않는다.
    luil::win32::uia_element_provider* const row { root->make_element_provider({ luil::ui_element_kind::list_row, u8"a" }) };
    IUnknown* none { nullptr };
    REQUIRE(row->GetPatternProvider(UIA_SelectionPatternId, &none) == S_OK);
    REQUIRE(none == nullptr);
    REQUIRE(row->GetSelection(&array) == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));

    row->Release();
    list->Release();
    lone->Release();
    root->Release();
}

TEST_CASE("Expanding through UIA carries the target state, and the open group stays put", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    luil::group_config advanced { .owner = u8"advanced", .title = u8"고급", .toggle = tagged_action(u8"advanced") };
    advanced.set_collapsed = [](const bool collapsed) { return luil::make_app_action(probe_state_intent { collapsed }); };
    page->add(std::make_unique<luil::group_element>(std::move(advanced)));
    page->add(std::make_unique<luil::group_element>(luil::group_config { .owner = u8"legacy", .title = u8"옛", .toggle = tagged_action(u8"legacy") }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const group { root->make_element_provider({ luil::ui_element_kind::group, u8"advanced" }) };

    ExpandCollapseState state { ExpandCollapseState_Collapsed };
    REQUIRE(group->get_ExpandCollapseState(&state) == S_OK);
    REQUIRE(state == ExpandCollapseState_Expanded);
    // Expand는 "펼쳐진 상태로 만들라"이지 "펼침을 뒤집으라"가 아니다.
    REQUIRE(group->Expand() == S_OK);
    REQUIRE(host.dispatched.empty());
    // 접기는 목표 상태를 실은 절대 메시지다. 다음 frame 전에 같은 명령이 또 와도
    // 같은 상태가 실릴 뿐, 토글이 겹쳐 도로 펼쳐지지 않는다.
    REQUIRE(group->Collapse() == S_OK);
    REQUIRE(group->Collapse() == S_OK);
    REQUIRE(host.dispatched.size() == 2u);
    for (const luil::input_action& action : host.dispatched)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<probe_state_intent>() };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->state == true);
    }

    // 절대 메시지가 없는 그룹은 상태 읽기만 서고 쓰기는 거절된다 — 토글 클릭으로
    // 물러서면 그것이 곧 겹쳐 뒤집히는 길이다.
    host.dispatched.clear();
    luil::win32::uia_element_provider* const legacy { root->make_element_provider({ luil::ui_element_kind::group, u8"legacy" }) };
    REQUIRE(legacy->get_ExpandCollapseState(&state) == S_OK);
    REQUIRE(legacy->Collapse() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));
    REQUIRE(host.dispatched.empty());

    legacy->Release();
    group->Release();
    root->Release();
}

TEST_CASE("Setting a slider value through UIA carries the absolute target", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    luil::slider_config volume {};
    volume.maximum = 10.0f;
    volume.value = 3.0f;
    volume.change = [](const float delta) { return luil::make_app_action(probe_delta_intent { delta }); };
    volume.change_to = [](const float value) { return luil::make_app_action(probe_value_intent { value }); };
    page->add(std::make_unique<luil::slider_element>(luil::ui_element_id { kind_item, u8"volume" }, std::move(volume)));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const slider { root->make_element_provider({ kind_item, u8"volume" }) };

    // 조작할 수 있는 막대는 읽기 전용이 아니다.
    BOOL read_only { TRUE };
    REQUIRE(slider->get_IsReadOnly(&read_only) == S_OK);
    REQUIRE(read_only == FALSE);

    // 목표 값이 절대 메시지로 나간다.
    REQUIRE(slider->SetValue(7.5) == S_OK);
    REQUIRE(dispatched_value(host) == 7.5f);

    // 다음 frame 전에 같은 목표가 또 와도 같은 절대 값이다 — 오래된 발행본에서
    // 잰 델타가 겹쳐 두 배로 움직이는 자리가 없다.
    host.dispatched.clear();
    REQUIRE(slider->SetValue(7.5) == S_OK);
    REQUIRE(dispatched_value(host) == 7.5f);

    // 범위 밖은 자르지 않고 인자 오류다 — 무엇을 잘못 말했는지 알아야 한다.
    host.dispatched.clear();
    REQUIRE(slider->SetValue(11.0) == E_INVALIDARG);
    REQUIRE(slider->SetValue(-1.0) == E_INVALIDARG);
    REQUIRE(host.dispatched.empty());

    // 같은 값이면 할 일이 없다 (거절이 아니다).
    REQUIRE(slider->SetValue(3.0) == S_OK);
    REQUIRE(host.dispatched.empty());

    slider->Release();
    root->Release();
}

TEST_CASE("A slider without an absolute factory is read-only through UIA", "[win32][uia]")
{
    // 델타 factory만으로 절대 목표를 적용하면 중복 요청이 누적될 수 있다.
    // 절대값 액션이 없는 요소는 UIA에서 읽기 전용으로 제공한다.
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    luil::slider_config volume {};
    volume.maximum = 10.0f;
    volume.value = 3.0f;
    volume.change = [](const float delta) { return luil::make_app_action(probe_delta_intent { delta }); };
    page->add(std::make_unique<luil::slider_element>(luil::ui_element_id { kind_item, u8"volume" }, std::move(volume)));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const slider { root->make_element_provider({ kind_item, u8"volume" }) };

    BOOL read_only { FALSE };
    REQUIRE(slider->get_IsReadOnly(&read_only) == S_OK);
    REQUIRE(read_only == TRUE);
    REQUIRE(slider->SetValue(7.5) == E_ACCESSDENIED);
    REQUIRE(host.dispatched.empty());

    slider->Release();
    root->Release();
}

TEST_CASE("A UIA write cannot reach behind a modal", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto behind { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"behind" }, luil::text_button_config { .text = u8"뒤" }) };
    behind->set_action(luil::ui_trigger::left_click, tagged_action(u8"behind"));
    page->add(std::move(behind));
    auto dialog { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"dialog" }) };
    dialog->set_focus_trap(true);
    auto inside { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"inside" }, luil::text_button_config { .text = u8"안" }) };
    inside->set_action(luil::ui_trigger::left_click, tagged_action(u8"inside"));
    dialog->add(std::move(inside));
    page->add(std::move(dialog));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    // scrim이 포인터를 막고 가둠이 키보드를 막는 그 자리를 보조 기술도 지난다.
    luil::win32::uia_element_provider* const behind_provider { root->make_element_provider({ kind_item, u8"behind" }) };
    REQUIRE(behind_provider->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));
    REQUIRE(behind_provider->SetFocus() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));
    REQUIRE(host.dispatched.empty());
    REQUIRE(host.focus_requested == luil::ui_element_id {});

    luil::win32::uia_element_provider* const inside_provider { root->make_element_provider({ kind_item, u8"inside" }) };
    REQUIRE(inside_provider->Invoke() == S_OK);
    REQUIRE(dispatched_tag(host) == u8"inside");

    inside_provider->Release();
    behind_provider->Release();
    root->Release();
}

TEST_CASE("UIA SetFocus asks the input thread for the logical focus", "[win32][uia]")
{
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto stop { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"confirm" }, luil::text_button_config { .text = u8"확인" }) };
    stop->set_action(luil::ui_trigger::left_click, tagged_action(u8"confirm"));
    page->add(std::move(stop));
    page->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, u8"hint" }, luil::label_config { .text = u8"안내" }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    // 초점은 tree가 아니라 입력 상태라 여기서 세우지 않고 청한다.
    luil::win32::uia_element_provider* const button { root->make_element_provider({ kind_item, u8"confirm" }) };
    REQUIRE(button->SetFocus() == S_OK);
    REQUIRE(host.focus_requested == luil::ui_element_id { kind_item, u8"confirm" });

    // 자리가 아닌 것은 청하지 않는다 — 청해도 서지 않을 것을 S_OK로 답하면 거짓말이다.
    host.focus_requested = {};
    luil::win32::uia_element_provider* const label { root->make_element_provider({ kind_item, u8"hint" }) };
    REQUIRE(label->SetFocus() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));
    REQUIRE(host.focus_requested == luil::ui_element_id {});

    label->Release();
    button->Release();
    root->Release();
}

TEST_CASE("UIA focus follows the logical focus, not the OS focus", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree();
    host.focus = { kind_item, u8"confirm" };
    auto* const root { new luil::win32::uia_root_provider { host } };

    IRawElementProviderFragment* focused { nullptr };
    REQUIRE(root->GetFocus(&focused) == S_OK);
    REQUIRE(focused != nullptr);
    auto* const button { static_cast<luil::win32::uia_element_provider*>(focused) };
    REQUIRE(button->element_id().owner == u8"confirm");
    VARIANT has_focus {};
    REQUIRE(button->GetPropertyValue(UIA_HasKeyboardFocusPropertyId, &has_focus) == S_OK);
    REQUIRE(has_focus.vt == VT_BOOL);
    REQUIRE(has_focus.boolVal == VARIANT_TRUE);
    button->Release();

    // 초점이 없으면 답도 없다.
    host.focus = {};
    IRawElementProviderFragment* none { nullptr };
    REQUIRE(root->GetFocus(&none) == S_OK);
    REQUIRE(none == nullptr);

    root->Release();
}

TEST_CASE("A UIA provider re-resolves its element from the newest tree", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree(3.0f);
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const slider { root->make_element_provider({ kind_item, u8"volume" }) };

    double value { 0.0 };
    REQUIRE(slider->get_Value(&value) == S_OK);
    REQUIRE(value == 3.0);

    // frame이 tree를 통째로 갈아끼워도 provider는 id로 지금 tree에 다시 묻는다.
    host.tree = make_page_tree(7.0f);
    REQUIRE(slider->get_Value(&value) == S_OK);
    REQUIRE(value == 7.0);

    slider->Release();
    root->Release();
}

TEST_CASE("UIA runtime ids stay stable per element and differ between elements", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree();
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const slider { root->make_element_provider({ kind_item, u8"volume" }) };
    luil::win32::uia_element_provider* const button { root->make_element_provider({ kind_item, u8"confirm" }) };

    const int first { runtime_key_of(*slider) };
    REQUIRE(runtime_key_of(*slider) == first);
    REQUIRE(runtime_key_of(*button) != first);

    button->Release();
    slider->Release();
    root->Release();
}

TEST_CASE("A UIA bounding rectangle is the clipped client rectangle", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree();
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const label { root->make_element_provider({ kind_item, u8"hint" }) };

    // 창이 없어 화면 원점만 빠진다 — client 물리 픽셀 그대로다 (배율을 곱하지 않는다).
    UiaRect rect {};
    REQUIRE(label->get_BoundingRectangle(&rect) == S_OK);
    REQUIRE(rect.left == 0.0);
    REQUIRE(rect.top == 0.0);
    REQUIRE(rect.width == 200.0);
    REQUIRE(rect.height == 100.0);

    label->Release();
    root->Release();
}

TEST_CASE("A cached provider cannot execute under a parent hidden in the new frame", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_section_tree(false);
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const child { root->make_element_provider({ kind_item, u8"child" }) };
    REQUIRE(child->Invoke() == S_OK);
    REQUIRE(dispatched_tag(host) == u8"child");
    host.dispatched.clear();

    // 다음 frame이 부모만 숨겼다. 자식의 플래그는 참으로 남지만 사람의 손이
    // 닿지 않는 자리다 — 쥐고 있던 provider로도 실행과 초점이 서지 않는다.
    host.tree = make_section_tree(true);
    REQUIRE(child->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));
    REQUIRE(child->SetFocus() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION));
    REQUIRE(host.dispatched.empty());
    REQUIRE(host.focus_requested == luil::ui_element_id {});

    child->Release();
    root->Release();
}

TEST_CASE("UIA navigation stays calm on a tree without a root", "[win32][uia]")
{
    fake_uia_host host {};
    // 공개 API가 허용하는 입력이다 — 앱이 빈 tree를 게시해도 탐색은 충돌 없이
    // 빈 답을 낸다.
    host.tree = std::make_shared<const luil::ui_tree>(nullptr);
    host.focus = { kind_item, u8"gone" };
    auto* const root { new luil::win32::uia_root_provider { host } };

    IRawElementProviderFragment* fragment { nullptr };
    REQUIRE(root->Navigate(NavigateDirection_FirstChild, &fragment) == S_OK);
    REQUIRE(fragment == nullptr);
    REQUIRE(root->Navigate(NavigateDirection_LastChild, &fragment) == S_OK);
    REQUIRE(fragment == nullptr);
    // 초점 id가 남아 있어도 빈 tree에는 없는 것이다.
    REQUIRE(root->GetFocus(&fragment) == S_OK);
    REQUIRE(fragment == nullptr);

    // 쥐고 있던 요소 provider도 같은 답이다 — 사라진 것으로 낡는다.
    luil::win32::uia_element_provider* const stale { root->make_element_provider({ kind_item, u8"gone" }) };
    REQUIRE(stale->Navigate(NavigateDirection_Parent, &fragment) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));

    stale->Release();
    root->Release();
}

TEST_CASE("A full UIA sibling traversal of thousands of items stays flat", "[win32][uia]")
{
    // 형제 이동마다 root에서 부모를 재귀 탐색하면 긴 평면 목록의 순회가 항목
    // 수의 제곱이 된다 — 색인 답은 목록 길이와 무관하다. 시간 상한은 색인
    // 걸음(수 ms)에는 헐겁고 제곱 걸음(수십 초)에는 한참 모자라는 값이다.
    constexpr std::size_t item_count { 4000 };
    fake_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    for (std::size_t index = 0; index < item_count; ++index)
        page->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, numbered_owner(u8"item", index) }, luil::label_config { .text = u8"항목" }));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100000.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };

    const std::chrono::steady_clock::time_point started { std::chrono::steady_clock::now() };

    // 앞으로 완주 — 순서와 개수가 정확해야 한다.
    IRawElementProviderFragment* fragment { nullptr };
    REQUIRE(root->Navigate(NavigateDirection_FirstChild, &fragment) == S_OK);
    std::size_t visited { 0 };
    while (fragment != nullptr)
    {
        auto* const provider { static_cast<luil::win32::uia_element_provider*>(fragment) };
        if (provider->element_id().owner != numbered_owner(u8"item", visited))
            break;
        ++visited;
        IRawElementProviderFragment* next { nullptr };
        REQUIRE(provider->Navigate(NavigateDirection_NextSibling, &next) == S_OK);
        fragment->Release();
        fragment = next;
    }
    REQUIRE(visited == item_count);

    // 뒤로도 완주한다.
    REQUIRE(root->Navigate(NavigateDirection_LastChild, &fragment) == S_OK);
    visited = 0;
    while (fragment != nullptr)
    {
        auto* const provider { static_cast<luil::win32::uia_element_provider*>(fragment) };
        if (provider->element_id().owner != numbered_owner(u8"item", item_count - 1 - visited))
            break;
        ++visited;
        IRawElementProviderFragment* previous { nullptr };
        REQUIRE(provider->Navigate(NavigateDirection_PreviousSibling, &previous) == S_OK);
        fragment->Release();
        fragment = previous;
    }
    REQUIRE(visited == item_count);

    const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started) };
    REQUIRE(elapsed < std::chrono::milliseconds { 2000 });

    root->Release();
}

TEST_CASE("Runtime keys of vanished dynamic ids are pruned while live ids stay stable", "[win32][uia]")
{
    // 세대마다 동적 id가 전부 갈리는 목록이다 (가상화된 행처럼). 고정 id
    // 하나가 모든 세대에 함께 산다.
    constexpr std::size_t generation_count { 40 };
    constexpr std::size_t row_count { 50 };
    fake_uia_host host {};
    auto* const root { new luil::win32::uia_root_provider { host } };
    const luil::ui_element_id anchor_id { kind_item, u8"anchor" };
    int anchor_key { 0 };

    for (std::size_t generation = 0; generation < generation_count; ++generation)
    {
        auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        page->add(std::make_unique<luil::label_element>(anchor_id, luil::label_config { .text = u8"고정" }));
        for (std::size_t row = 0; row < row_count; ++row)
            page->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, numbered_owner(u8"row", generation * 1000 + row) }, luil::label_config { .text = u8"행" }));
        host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 1000.0f }, 1.0f));

        luil::win32::uia_element_provider* const first { root->make_element_provider({ kind_item, numbered_owner(u8"row", generation * 1000) }) };
        const int first_key { runtime_key_of(*first) };
        for (std::size_t row = 1; row < row_count; ++row)
        {
            luil::win32::uia_element_provider* const provider { root->make_element_provider({ kind_item, numbered_owner(u8"row", generation * 1000 + row) }) };
            static_cast<void>(runtime_key_of(*provider));
            provider->Release();
        }
        // 산 id의 key는 같은 세대 안에서 정리를 지나도 그대로다.
        REQUIRE(runtime_key_of(*first) == first_key);
        first->Release();

        luil::win32::uia_element_provider* const anchor { root->make_element_provider(anchor_id) };
        if (generation == 0)
            anchor_key = runtime_key_of(*anchor);
        else
            REQUIRE(runtime_key_of(*anchor) == anchor_key);
        anchor->Release();
    }

    // id가 2천 개 넘게 지나갔지만 지도는 산 id 수의 상수 배에 머문다 —
    // 사라진 세대의 key는 문턱에서 정리된다.
    REQUIRE(root->runtime_key_count() < 512);

    root->Release();
}

TEST_CASE("A UIA entry point turns exceptions into HRESULTs at the COM boundary", "[win32][uia]")
{
    // C++ 예외는 COM ABI를 넘을 수 없다 — 표면 배분이 던져도(주로 할당 실패)
    // provider가 HRESULT로 바꿔 답한다.
    throwing_uia_host host {};
    auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"confirm" }, luil::text_button_config { .text = u8"확인" }) };
    button->set_action(luil::ui_trigger::left_click, tagged_action(u8"confirm"));
    page->add(std::move(button));
    host.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const confirm { root->make_element_provider({ kind_item, u8"confirm" }) };

    REQUIRE(confirm->Invoke() == E_OUTOFMEMORY);
    host.out_of_memory = false;
    REQUIRE(confirm->Invoke() == E_FAIL);

    confirm->Release();
    root->Release();
}

TEST_CASE("Each state change raises exactly one UIA event with the right word", "[win32][uia]")
{
    // 상태를 바꿔 가며 같은 화면을 두 번 짓는다 — frame마다 새 tree다.
    const auto make_state_tree = [](const bool checked, const float volume, const bool collapsed, const std::u8string_view note, const std::u8string_view selected) {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        root->add(std::make_unique<luil::check_element>(luil::check_config {
            .owner = u8"alerts",
            .label = u8"알림",
            .checked = checked,
            .toggle = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; },
        }));
        root->add(std::make_unique<luil::slider_element>(luil::ui_element_id { kind_item, u8"volume" },
            luil::slider_config { .minimum = 0.0f, .maximum = 10.0f, .value = volume }));
        root->add(std::make_unique<luil::group_element>(luil::group_config { .owner = u8"advanced", .title = u8"고급", .collapsed = collapsed, .toggle = tagged_action(u8"advanced") }));
        root->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, u8"hint" }, luil::label_config { .text = std::u8string { note } }));
        luil::list_config files {};
        files.owner = u8"files";
        files.items = { { .key = u8"a", .label = u8"가" }, { .key = u8"b", .label = u8"나" } };
        files.selected = std::u8string { selected };
        files.select = [](const std::u8string&) { return luil::input_action {}; };
        root->add(std::make_unique<luil::list_element>(std::move(files)));
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f));
    };

    fake_uia_host host {};
    host.tree = make_state_tree(false, 3.0f, false, u8"안내", u8"a");
    const luil::access_snapshot before { luil::make_access_snapshot(*host.tree) };
    auto* const root { new luil::win32::uia_root_provider { host } };
    recording_event_sink sink {};
    sink.root = static_cast<IRawElementProviderSimple*>(root);
    root->set_event_sink_for_test(sink);

    host.tree = make_state_tree(true, 7.0f, true, u8"다른 안내", u8"b");
    root->announce_changes(luil::diff_access_snapshots(before, luil::make_access_snapshot(*host.tree)));

    // 변화 다섯에 이벤트 다섯 — 켬끔·범위 값·펼침·이름은 property, 새 고름은
    // ElementSelected다. 풀린 쪽(a)과 바뀌지 않은 요소는 아무 이벤트도 없다.
    REQUIRE(sink.records.size() == 5u);
    REQUIRE(sink.records[0].kind == u8"property");
    REQUIRE(sink.records[0].owner == u8"alerts");
    REQUIRE(sink.records[0].property == UIA_ToggleToggleStatePropertyId);
    REQUIRE(sink.records[0].old_number == static_cast<double>(ToggleState_Off));
    REQUIRE(sink.records[0].new_number == static_cast<double>(ToggleState_On));
    REQUIRE(sink.records[1].owner == u8"volume");
    REQUIRE(sink.records[1].property == UIA_RangeValueValuePropertyId);
    REQUIRE(sink.records[1].old_number == 3.0);
    REQUIRE(sink.records[1].new_number == 7.0);
    REQUIRE(sink.records[2].owner == u8"advanced");
    REQUIRE(sink.records[2].property == UIA_ExpandCollapseExpandCollapseStatePropertyId);
    REQUIRE(sink.records[2].old_number == static_cast<double>(ExpandCollapseState_Expanded));
    REQUIRE(sink.records[2].new_number == static_cast<double>(ExpandCollapseState_Collapsed));
    REQUIRE(sink.records[3].owner == u8"hint");
    REQUIRE(sink.records[3].property == UIA_NamePropertyId);
    REQUIRE(sink.records[3].old_text == L"안내");
    REQUIRE(sink.records[3].new_text == L"다른 안내");
    REQUIRE(sink.records[4].kind == u8"selected");
    REQUIRE(sink.records[4].owner == u8"b");

    // 같은 발행본을 다시 견주면 아무 이벤트도 없다.
    sink.records.clear();
    root->announce_changes(luil::diff_access_snapshots(luil::make_access_snapshot(*host.tree), luil::make_access_snapshot(*host.tree)));
    REQUIRE(sink.records.empty());

    root->Release();
}

TEST_CASE("Structure changes and focus reach the UIA sink once each", "[win32][uia]")
{
    const auto make_section_state = [](const bool collapsed, const bool with_extra) {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        auto confirm { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"confirm" }, luil::text_button_config { .text = u8"확인" }) };
        confirm->set_action(luil::ui_trigger::left_click, tagged_action(u8"confirm"));
        root->add(std::move(confirm));
        if (with_extra)
            root->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, u8"extra" }, luil::label_config { .text = u8"둘" }));
        luil::group_config section { .owner = u8"advanced", .title = u8"고급", .collapsed = collapsed, .toggle = tagged_action(u8"advanced"), .content_height = 20.0f };
        root->add(std::make_unique<luil::group_element>(std::move(section),
            std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, u8"nested" }, luil::label_config { .text = u8"안쪽" })));
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f));
    };

    fake_uia_host host {};
    host.tree = make_section_state(false, false);
    auto* const root { new luil::win32::uia_root_provider { host } };
    recording_event_sink sink {};
    sink.root = static_cast<IRawElementProviderSimple*>(root);
    root->set_event_sink_for_test(sink);

    // 최상위에 요소가 들면 표면 root의 structure 하나다.
    const luil::access_snapshot narrow { luil::make_access_snapshot(*host.tree) };
    host.tree = make_section_state(false, true);
    root->announce_changes(luil::diff_access_snapshots(narrow, luil::make_access_snapshot(*host.tree)));
    REQUIRE(sink.records.size() == 1u);
    REQUIRE(sink.records.front().kind == u8"structure");
    REQUIRE(sink.records.front().owner == u8"");

    // 그룹이 접히면 펼침 property와 그 그룹의 structure가 하나씩이다.
    sink.records.clear();
    const luil::access_snapshot open { luil::make_access_snapshot(*host.tree) };
    host.tree = make_section_state(true, true);
    root->announce_changes(luil::diff_access_snapshots(open, luil::make_access_snapshot(*host.tree)));
    REQUIRE(sink.records.size() == 2u);
    REQUIRE(sink.records[0].kind == u8"property");
    REQUIRE(sink.records[0].property == UIA_ExpandCollapseExpandCollapseStatePropertyId);
    REQUIRE(sink.records[1].kind == u8"structure");
    REQUIRE(sink.records[1].owner == u8"advanced");

    // 초점 알림도 같은 창구를 지난다.
    sink.records.clear();
    root->announce_focus({ kind_item, u8"confirm" });
    REQUIRE(sink.records.size() == 1u);
    REQUIRE(sink.records.front().kind == u8"focus");
    REQUIRE(sink.records.front().owner == u8"confirm");

    // Invoke는 상태 변화가 아니라 대조로 알 수 없다 — 배분된 순간 Invoked
    // 하나가 난다.
    sink.records.clear();
    luil::win32::uia_element_provider* const confirm { root->make_element_provider({ kind_item, u8"confirm" }) };
    REQUIRE(confirm->Invoke() == S_OK);
    REQUIRE(sink.records.size() == 1u);
    REQUIRE(sink.records.front().kind == u8"invoked");
    REQUIRE(sink.records.front().owner == u8"confirm");

    confirm->Release();
    root->Release();
}

namespace {
    // 텍스트 칸 하나가 든 화면이다.
    // 문서는 u8"a가b😀c" — 1·3·1·4·1 byte라 문자 경계와 surrogate 쌍을 함께 잰다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_note_tree(luil::text_input_view value)
    {
        auto page { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        page->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_item, u8"note" }, std::move(value), luil::text_input_config { .placeholder = u8"메모" }));
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(page), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    }

    [[nodiscard]] std::wstring range_text(ITextRangeProvider& range, const int maximum_length = -1)
    {
        BSTR text { nullptr };
        REQUIRE(range.GetText(maximum_length, &text) == S_OK);
        REQUIRE(text != nullptr);
        std::wstring value { text, SysStringLen(text) };
        SysFreeString(text);
        return value;
    }
} // namespace

TEST_CASE("A wired text box serves the Text pattern from its published document", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_note_tree({ .text = u8"a가b😀c", .caret = 5, .anchor = 1 });
    host.text_target = static_cast<luil::text_input_target>(7);
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const note { root->make_element_provider({ kind_item, u8"note" }) };

    // 문서를 내주는 텍스트 칸이 Text 패턴을 내건다.
    IUnknown* pattern { nullptr };
    REQUIRE(note->GetPatternProvider(UIA_TextPatternId, &pattern) == S_OK);
    REQUIRE(pattern != nullptr);
    ITextProvider* text { nullptr };
    REQUIRE(pattern->QueryInterface(__uuidof(ITextProvider), reinterpret_cast<void**>(&text)) == S_OK);

    SupportedTextSelection selection_kind { SupportedTextSelection_None };
    REQUIRE(text->get_SupportedTextSelection(&selection_kind) == S_OK);
    REQUIRE(selection_kind == SupportedTextSelection_Single);

    // 문서 전체를 읽는다.
    ITextRangeProvider* document { nullptr };
    REQUIRE(text->get_DocumentRange(&document) == S_OK);
    REQUIRE(document != nullptr);
    REQUIRE(range_text(*document) == L"a가b😀c");

    // 지금 선택은 anchor 1 ~ caret 5다 — "가b".
    SAFEARRAY* selection { nullptr };
    REQUIRE(text->GetSelection(&selection) == S_OK);
    REQUIRE(selection != nullptr);
    LONG index { 0 };
    IUnknown* selected_unknown { nullptr };
    REQUIRE(SafeArrayGetElement(selection, &index, &selected_unknown) == S_OK);
    auto* const selected { static_cast<luil::win32::uia_text_range_provider*>(static_cast<ITextRangeProvider*>(selected_unknown)) };
    REQUIRE(selected->begin_offset() == 1u);
    REQUIRE(selected->end_offset() == 5u);
    REQUIRE(range_text(*selected) == L"가b");
    selected_unknown->Release();
    static_cast<void>(SafeArrayDestroy(selection));

    // caret range를 편집으로 옮긴다 — Select는 TSF와 같은 편집 파이프라인
    // (place_caret)을 탄다. SetFocus 뒤의 편집이 이 길로 선다.
    ITextRangeProvider* moved_range { nullptr };
    REQUIRE(document->Clone(&moved_range) == S_OK);
    REQUIRE(moved_range->Select() == S_OK);
    REQUIRE(host.edits.size() == 2u);
    REQUIRE(host.edits[0].command == luil::text::text_edit_command::place_caret);
    REQUIRE(host.edits[0].target == *host.text_target);
    REQUIRE(host.edits[0].offset == 0u);
    REQUIRE(host.edits[0].extend == false);
    REQUIRE(host.edits[1].command == luil::text::text_edit_command::place_caret);
    REQUIRE(host.edits[1].offset == 10u);
    REQUIRE(host.edits[1].extend == true);

    moved_range->Release();
    document->Release();
    text->Release();
    pattern->Release();
    note->Release();
    root->Release();
}

TEST_CASE("Text ranges walk characters, not bytes, across a surrogate pair", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_note_tree({ .text = u8"a가b😀c", .caret = 0, .anchor = 0 });
    host.text_target = static_cast<luil::text_input_target>(7);
    host.text_offset = 10;
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const note { root->make_element_provider({ kind_item, u8"note" }) };

    // 좌표 질의는 표면이 옮긴 offset의 빈 구간이다.
    ITextRangeProvider* caret_range { nullptr };
    REQUIRE(note->RangeFromPoint(UiaPoint { 0.0, 0.0 }, &caret_range) == S_OK);
    auto* const walker { static_cast<luil::win32::uia_text_range_provider*>(caret_range) };
    REQUIRE(walker->begin_offset() == 10u);
    REQUIRE(walker->end_offset() == 10u);

    // 문자 단위 걷기는 byte가 아니라 codepoint다 — 😀(4 byte)이 한 걸음이다.
    int moved { 0 };
    REQUIRE(caret_range->Move(TextUnit_Character, -2, &moved) == S_OK);
    REQUIRE(moved == -2);
    REQUIRE(walker->begin_offset() == 5u);
    REQUIRE(caret_range->ExpandToEnclosingUnit(TextUnit_Character) == S_OK);
    REQUIRE(walker->begin_offset() == 5u);
    REQUIRE(walker->end_offset() == 9u);
    REQUIRE(range_text(*caret_range) == L"😀");
    // 잘라서 surrogate 쌍의 앞쪽만 남기지 않는다.
    REQUIRE(range_text(*caret_range, 1).empty());

    // 지원하지 않는 단위(낱말)는 다음 큰 단위(문서)로 물러선다.
    REQUIRE(caret_range->ExpandToEnclosingUnit(TextUnit_Word) == S_OK);
    REQUIRE(walker->begin_offset() == 0u);
    REQUIRE(walker->end_offset() == 10u);

    // 구간 찾기는 ASCII 대소문자를 접는다. 없는 글은 빈 답이다.
    ITextRangeProvider* found { nullptr };
    BSTR needle { SysAllocString(L"B") };
    REQUIRE(caret_range->FindText(needle, FALSE, TRUE, &found) == S_OK);
    SysFreeString(needle);
    REQUIRE(found != nullptr);
    REQUIRE(range_text(*found) == L"b");
    found->Release();
    found = nullptr;
    needle = SysAllocString(L"없다");
    REQUIRE(caret_range->FindText(needle, FALSE, FALSE, &found) == S_OK);
    SysFreeString(needle);
    REQUIRE(found == nullptr);

    caret_range->Release();
    note->Release();
    root->Release();
}

TEST_CASE("A text range asks the surface for its screen bounds", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_note_tree({ .text = u8"a가b😀c", .caret = 5, .anchor = 1 });
    host.text_target = static_cast<luil::text_input_target>(7);
    host.text_span = RECT { 10, 20, 110, 40 };
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const note { root->make_element_provider({ kind_item, u8"note" }) };

    ITextRangeProvider* document { nullptr };
    {
        ITextProvider* text { nullptr };
        REQUIRE(note->QueryInterface(__uuidof(ITextProvider), reinterpret_cast<void**>(&text)) == S_OK);
        REQUIRE(text->GetSelection(nullptr) == E_POINTER);
        SAFEARRAY* selection { nullptr };
        REQUIRE(text->GetSelection(&selection) == S_OK);
        LONG index { 0 };
        IUnknown* selected { nullptr };
        REQUIRE(SafeArrayGetElement(selection, &index, &selected) == S_OK);
        REQUIRE(selected->QueryInterface(__uuidof(ITextRangeProvider), reinterpret_cast<void**>(&document)) == S_OK);
        selected->Release();
        static_cast<void>(SafeArrayDestroy(selection));
        text->Release();
    }

    // 화면 사각형은 표면의 몫이다 — 같은 문서와 같은 구간이 그대로 넘어간다.
    SAFEARRAY* rectangles { nullptr };
    REQUIRE(document->GetBoundingRectangles(&rectangles) == S_OK);
    REQUIRE(rectangles != nullptr);
    REQUIRE(host.spanned_document == u8"a가b😀c");
    REQUIRE(host.spanned_begin == 1u);
    REQUIRE(host.spanned_end == 5u);
    double values[4] { 0.0, 0.0, 0.0, 0.0 };
    for (LONG index = 0; index < 4; ++index)
        REQUIRE(SafeArrayGetElement(rectangles, &index, &values[index]) == S_OK);
    REQUIRE(values[0] == 10.0);
    REQUIRE(values[1] == 20.0);
    REQUIRE(values[2] == 100.0);
    REQUIRE(values[3] == 20.0);
    static_cast<void>(SafeArrayDestroy(rectangles));

    document->Release();
    note->Release();
    root->Release();
}

TEST_CASE("While composing, the Text pattern reads the composition document", "[win32][uia]")
{
    // 조합 중의 정본은 화면에 보이는 그 글이다 — TSF가 같은 원천에서 같은
    // 문서를 본다. 선택은 조합 caret 하나로 접힌다 (IME가 문서를 소유한다).
    fake_uia_host host {};
    luil::text_input_view value {};
    value.text = u8"토";
    value.caret = 3;
    value.anchor = 3;
    value.composing = true;
    value.composition_text = u8"토스";
    value.composition_caret = 6;
    host.tree = make_note_tree(std::move(value));
    host.text_target = static_cast<luil::text_input_target>(7);
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const note { root->make_element_provider({ kind_item, u8"note" }) };

    ITextProvider* text { nullptr };
    REQUIRE(note->QueryInterface(__uuidof(ITextProvider), reinterpret_cast<void**>(&text)) == S_OK);
    ITextRangeProvider* document { nullptr };
    REQUIRE(text->get_DocumentRange(&document) == S_OK);
    REQUIRE(range_text(*document) == L"토스");

    SAFEARRAY* selection { nullptr };
    REQUIRE(text->GetSelection(&selection) == S_OK);
    LONG index { 0 };
    IUnknown* selected_unknown { nullptr };
    REQUIRE(SafeArrayGetElement(selection, &index, &selected_unknown) == S_OK);
    auto* const selected { static_cast<luil::win32::uia_text_range_provider*>(static_cast<ITextRangeProvider*>(selected_unknown)) };
    REQUIRE(selected->begin_offset() == 6u);
    REQUIRE(selected->end_offset() == 6u);
    selected_unknown->Release();
    static_cast<void>(SafeArrayDestroy(selection));

    // Value 패턴의 값은 여전히 확정 글이다 — 조합 글은 아직 사용자의 글이 아니다.
    BSTR committed { nullptr };
    REQUIRE(note->get_Value(&committed) == S_OK);
    REQUIRE(std::wstring { committed, SysStringLen(committed) } == L"토");
    SysFreeString(committed);

    document->Release();
    text->Release();
    note->Release();
    root->Release();
}

TEST_CASE("Writing a value routes through the shared edit pipeline", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_note_tree({ .text = u8"메모", .caret = 0, .anchor = 0 });
    host.text_target = static_cast<luil::text_input_target>(7);
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const note { root->make_element_provider({ kind_item, u8"note" }) };

    // 편집 대상이 이어진 칸은 더 이상 읽기 전용이 아니다.
    BOOL read_only { TRUE };
    REQUIRE(note->get_IsReadOnly(&read_only) == S_OK);
    REQUIRE(read_only == FALSE);

    // 쓰기는 TSF가 확정 글을 넣는 그 길(replace_all)이다.
    REQUIRE(note->SetValue(L"바꾼 글") == S_OK);
    REQUIRE(host.edits.size() == 1u);
    REQUIRE(host.edits.front().command == luil::text::text_edit_command::replace_all);
    REQUIRE(host.edits.front().target == *host.text_target);
    REQUIRE(host.edits.front().text == u8"바꾼 글");
    REQUIRE(host.edits.front().offset == host.edits.front().text.size());

    // 편집 대상이 없으면 읽기 전용으로 보고 set-value 요청을 거절한다.
    host.text_target.reset();
    host.edits.clear();
    REQUIRE(note->get_IsReadOnly(&read_only) == S_OK);
    REQUIRE(read_only == TRUE);
    REQUIRE(note->SetValue(L"안 된다") == E_ACCESSDENIED);
    REQUIRE(host.edits.empty());

    note->Release();
    root->Release();
}

TEST_CASE("A detached UIA provider answers that elements are gone", "[win32][uia]")
{
    fake_uia_host host {};
    host.tree = make_page_tree();
    host.focus = { kind_item, u8"confirm" };
    auto* const root { new luil::win32::uia_root_provider { host } };
    luil::win32::uia_element_provider* const button { root->make_element_provider({ kind_item, u8"confirm" }) };

    root->detach();

    VARIANT value {};
    REQUIRE(button->GetPropertyValue(UIA_NamePropertyId, &value) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    IRawElementProviderFragment* fragment { nullptr };
    REQUIRE(button->Navigate(NavigateDirection_Parent, &fragment) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    REQUIRE(root->Navigate(NavigateDirection_FirstChild, &fragment) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    REQUIRE(root->GetFocus(&fragment) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));

    button->Release();
    root->Release();
}

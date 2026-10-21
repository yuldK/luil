#include "win32/uia_provider.h"

#include "luil/text/text_edit.h"
#include "luil/text/utf8_text.h"
#include "luil/ui/ui_interaction.h"
#include "win32/utf8.h"

#include <uiautomation.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luil::win32 {
    namespace {
        // 어휘의 역할을 UIA ControlType으로 옮기는 표다.
        // 여기 한 곳뿐이다 — 어휘는 platform을 모른다 (accessibility-design.md).
        [[nodiscard]] long control_type_for(const access_role role) noexcept
        {
            switch (role)
            {
            case access_role::button:
                return UIA_ButtonControlTypeId;
            case access_role::link:
                return UIA_HyperlinkControlTypeId;
            case access_role::check_box:
                return UIA_CheckBoxControlTypeId;
            case access_role::radio_button:
                return UIA_RadioButtonControlTypeId;
            // UIA에 스위치 ControlType이 없다.
            // Toggle 패턴을 단 단추가 그 관례다 (WinUI의 ToggleSwitch와 같다).
            case access_role::toggle_switch:
                return UIA_ButtonControlTypeId;
            case access_role::slider:
                return UIA_SliderControlTypeId;
            case access_role::scroll_bar:
                return UIA_ScrollBarControlTypeId;
            case access_role::progress:
                return UIA_ProgressBarControlTypeId;
            case access_role::handle:
                return UIA_ThumbControlTypeId;
            case access_role::image:
                return UIA_ImageControlTypeId;
            case access_role::edit:
                return UIA_EditControlTypeId;
            case access_role::combo_box:
                return UIA_ComboBoxControlTypeId;
            case access_role::list:
                return UIA_ListControlTypeId;
            case access_role::list_item:
                return UIA_ListItemControlTypeId;
            case access_role::header:
                return UIA_HeaderControlTypeId;
            case access_role::tab_list:
                return UIA_TabControlTypeId;
            case access_role::tab:
                return UIA_TabItemControlTypeId;
            // 라디오 묶음의 ControlType은 UIA에 따로 없다.
            // Group + Selection 패턴이 그 관례다.
            case access_role::radio_group:
                return UIA_GroupControlTypeId;
            case access_role::menu:
                return UIA_MenuControlTypeId;
            case access_role::menu_item:
                return UIA_MenuItemControlTypeId;
            case access_role::pane:
                return UIA_PaneControlTypeId;
            case access_role::group:
                return UIA_GroupControlTypeId;
            case access_role::title_bar:
                return UIA_TitleBarControlTypeId;
            case access_role::dialog:
                return UIA_WindowControlTypeId;
            // 알림 ControlType도 없다. 글로 두고 알림 성질은 뒤(LiveRegion)의 몫이다.
            case access_role::alert:
            case access_role::static_text:
                return UIA_TextControlTypeId;
            case access_role::tree:
                return UIA_TreeControlTypeId;
            case access_role::tree_item:
                return UIA_TreeItemControlTypeId;
            // 행을 고르는 표라 DataGrid다 (`access_role::table`).
            case access_role::table:
                return UIA_DataGridControlTypeId;
            case access_role::data_item:
                return UIA_DataItemControlTypeId;
            case access_role::header_item:
                return UIA_HeaderItemControlTypeId;
            case access_role::tool_bar:
                return UIA_ToolBarControlTypeId;
            case access_role::separator:
                return UIA_SeparatorControlTypeId;
            case access_role::status_bar:
                return UIA_StatusBarControlTypeId;
            default:
                return UIA_PaneControlTypeId;
            }
        }

        // 체크의 세 상태를 UIA ToggleState로 옮긴다. 일부(`access_info::mixed`)가 켬·끔보다 앞선다.
        [[nodiscard]] ToggleState toggle_state_of(const access_info& info) noexcept
        {
            if (info.mixed)
                return ToggleState_Indeterminate;
            return info.checked == true ? ToggleState_On : ToggleState_Off;
        }

        // Invoke를 내걸 자리인가.
        // **상태를 가진 것은 Invoke가 아니다** — UIA는 체크박스·라디오·펼침을
        // 그 상태의 패턴으로 실행하게 한다. 역할로도 가리는 이유는 텍스트 칸이
        // **hit를 흡수하려고** 빈 클릭 액션을 들고 있어서다: "누를 수 있다"만으로는
        // 걸러지지 않는다 (accessibility-action-design.md).
        [[nodiscard]] bool invokable(const access_info& info) noexcept
        {
            if (info.checked.has_value() || info.selected.has_value() || info.expanded.has_value() || info.range.has_value())
                return false;
            return info.role == access_role::button || info.role == access_role::link || info.role == access_role::menu_item;
        }

        [[nodiscard]] BSTR bstr_from_utf8(const std::u8string_view text)
        {
            const utf_conversion_result<std::wstring> converted { utf8_to_utf16(text) };
            if (converted.value.has_value() == false)
                return SysAllocString(L"");
            return SysAllocStringLen(converted.value->data(), static_cast<UINT>(converted.value->size()));
        }

        void set_bool(VARIANT& variant, const bool value) noexcept
        {
            variant.vt = VT_BOOL;
            variant.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        }

        void set_int(VARIANT& variant, const long value) noexcept
        {
            variant.vt = VT_I4;
            variant.lVal = value;
        }

        // kind:owner — 자동화 script가 요소를 집는 손잡이다 (AutomationId).
        [[nodiscard]] std::wstring automation_id_of(const ui_element_id& id)
        {
            std::wstring identifier { std::to_wstring(static_cast<std::uint32_t>(id.kind)) };
            identifier.push_back(L':');
            if (const utf_conversion_result<std::wstring> owner { utf8_to_utf16(id.owner) }; owner.value.has_value())
                identifier.append(*owner.value);
            return identifier;
        }

        // `automation_id_of`의 역이다. 모양이 맞지 않으면 nullopt다.
        [[nodiscard]] std::optional<ui_element_id> id_from_automation_id(const std::wstring_view identifier)
        {
            const std::size_t colon { identifier.find(L':') };
            if (colon == std::wstring_view::npos || colon == 0u)
                return std::nullopt;
            std::uint32_t kind { 0 };
            for (const wchar_t digit : identifier.substr(0, colon))
            {
                if (digit < L'0' || digit > L'9')
                    return std::nullopt;
                kind = kind * 10u + static_cast<std::uint32_t>(digit - L'0');
            }
            const utf_conversion_result<std::u8string> owner { utf16_to_utf8(identifier.substr(colon + 1u)) };
            if (owner.value.has_value() == false)
                return std::nullopt;
            return ui_element_id { static_cast<ui_element_kind>(kind), std::move(*owner.value) };
        }

        // 텍스트 칸의 표시 문서다 — 확정 글, 조합 중에는 조합 글 (caret은 그
        // 문서 기준, 조합 중의 선택은 caret 하나다 — IME가 문서를 소유한다).
        // TSF가 같은 원천(요소의 발행본)에서 같은 문서를 본다.
        struct access_text_document
        {
            std::u8string_view text {};
            std::size_t caret { 0 };
            std::size_t anchor { 0 };
            bool composing { false };
        };

        // 암호 칸은 문서를 내주지 않는다 — Text pattern 없이 가린 값(Value)과 IsPassword만 알린다.
        [[nodiscard]] std::optional<access_text_document> text_document_of(const ui_element& element)
        {
            const std::optional<text_input_snapshot> snapshot { element.text_input() };
            if (snapshot.has_value() == false || snapshot->secret)
                return std::nullopt;
            if (snapshot->composing)
                return access_text_document { snapshot->composition_text, snapshot->composition_caret, snapshot->composition_caret, true };
            return access_text_document { snapshot->text, snapshot->caret, snapshot->anchor, false };
        }

        // UTF-8 문자 경계 걷기다 (경계 밖 offset은 끝으로 잘려 들어온다).
        [[nodiscard]] std::size_t step_character_forward(const std::u8string_view text, const std::size_t offset) noexcept
        {
            if (offset >= text.size())
                return text.size();
            return offset + text::utf8_decode(text, offset).length;
        }

        [[nodiscard]] std::size_t step_character_backward(const std::u8string_view text, const std::size_t offset) noexcept
        {
            if (offset == 0)
                return 0;
            std::size_t previous { offset - 1 };
            // continuation byte(0b10xxxxxx)를 지나 문자 시작까지 되돌아간다.
            while (previous > 0 && (static_cast<unsigned char>(text[previous]) & 0xC0u) == 0x80u)
                --previous;
            return previous;
        }

        // COM 경계다 — C++ 예외는 지원되는 오류 전달 수단이 아니라 HRESULT로
        // 바꾼다 (`deliver_file_drop`이 WndProc 경계에서 하는 것과 같은 불변식).
        // 할당·vector·string·SafeArray를 만지는 진입점의 본문이 이 안을 지난다.
        template<typename callable_type>
        [[nodiscard]] HRESULT guarded(callable_type&& body) noexcept
        {
            try
            {
                return body();
            }
            catch (const std::bad_alloc&)
            {
                return E_OUTOFMEMORY;
            }
            catch (...)
            {
                return E_FAIL;
            }
        }
    } // namespace

    namespace {
        // 기본 이벤트 창구다 — OS로 그대로 쏜다.
        // 상태가 없어 프로세스 공용 하나로 충분하다.
        // 클라이언트가 없으면 아무것도 쏘지 않는다 — `UiaRaise*`도 스스로
        // 거르지만, 이 관문이 있어야 창 없는 test 실행이 UIA core를 아예
        // 건드리지 않는다.
        class live_uia_event_sink final : public uia_event_sink
        {
        public:
            void focus_changed(uia_element_provider& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationEvent(&provider, UIA_AutomationFocusChangedEventId));
            }

            void invoked(uia_element_provider& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationEvent(&provider, UIA_Invoke_InvokedEventId));
            }

            void property_changed(uia_element_provider& provider, const PROPERTYID property, const VARIANT& old_value, const VARIANT& new_value) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationPropertyChangedEvent(&provider, property, old_value, new_value));
            }

            void element_selected(uia_element_provider& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationEvent(&provider, UIA_SelectionItem_ElementSelectedEventId));
            }

            void element_added_to_selection(uia_element_provider& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationEvent(&provider, UIA_SelectionItem_ElementAddedToSelectionEventId));
            }

            void element_removed_from_selection(uia_element_provider& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationEvent(&provider, UIA_SelectionItem_ElementRemovedFromSelectionEventId));
            }

            void selection_invalidated(uia_element_provider& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                static_cast<void>(UiaRaiseAutomationEvent(&provider, UIA_Selection_InvalidatedEventId));
            }

            void structure_changed(IRawElementProviderSimple& provider) override
            {
                if (UiaClientsAreListening() == FALSE)
                    return;
                // 무엇이 들고 났는지까지 세지 않는다 — 자식 구성이 바뀌었으니
                // 다시 읽으라는 알림이 이 저장소 규모의 정직한 최소다.
                static_cast<void>(UiaRaiseStructureChangedEvent(&provider, StructureChangeType_ChildrenInvalidated, nullptr, 0));
            }
        };

        [[nodiscard]] uia_event_sink& default_event_sink() noexcept
        {
            static live_uia_event_sink sink {};
            return sink;
        }
    } // namespace

    // --- uia_root_provider ---

    uia_root_provider::uia_root_provider(uia_surface_host& host) noexcept
        : host_ { &host }
        , events_ { &default_event_sink() }
    {}

    void uia_root_provider::detach() noexcept
    {
        host_ = nullptr;
    }

    uia_surface_host* uia_root_provider::host() const noexcept
    {
        return host_;
    }

    uia_element_provider* uia_root_provider::make_element_provider(ui_element_id id, ui_element_id container)
    {
        return new uia_element_provider { *this, std::move(id), std::move(container) };
    }

    int uia_root_provider::runtime_key(const ui_element_id& id)
    {
        const auto [entry, inserted] { runtime_keys_.try_emplace(id, next_runtime_key_) };
        if (inserted)
            ++next_runtime_key_;
        const int key { entry->second };
        prune_runtime_keys(id);
        return key;
    }

    void uia_root_provider::prune_runtime_keys(const ui_element_id& keep)
    {
        if (runtime_keys_.size() < runtime_key_prune_threshold_)
            return;
        // tree가 없으면(첫 frame 전·detach 후) 산 id를 알 수 없다 — 비우지
        // 않는다. 그동안 지도는 새로 물어 온 id만큼만 자란다.
        const ui_tree* const tree { host_ != nullptr ? host_->accessibility_tree() : nullptr };
        if (tree == nullptr)
            return;
        for (auto entry { runtime_keys_.begin() }; entry != runtime_keys_.end();)
        {
            // 지금 tree에 있는 id는 절대 비우지 않는다 — 산 요소의 RuntimeId가
            // 흔들리면 클라이언트의 같음 비교가 깨진다. 지금 물은 id도 남긴다.
            if (entry->first != keep && tree->find(entry->first) == nullptr)
                entry = runtime_keys_.erase(entry);
            else
                ++entry;
        }
        runtime_key_prune_threshold_ = std::max(runtime_key_prune_floor, runtime_keys_.size() * 2);
    }

    std::size_t uia_root_provider::runtime_key_count() const noexcept
    {
        return runtime_keys_.size();
    }

    void uia_root_provider::announce_focus(const ui_element_id& id)
    {
        uia_element_provider* const provider { make_element_provider(id) };
        events_->focus_changed(*provider);
        provider->Release();
    }

    namespace {
        [[nodiscard]] VARIANT variant_from_utf8(const std::u8string_view text)
        {
            VARIANT value {};
            value.vt = VT_BSTR;
            value.bstrVal = bstr_from_utf8(text);
            return value;
        }

        [[nodiscard]] VARIANT variant_from_int(const long number) noexcept
        {
            VARIANT value {};
            value.vt = VT_I4;
            value.lVal = number;
            return value;
        }

        [[nodiscard]] VARIANT variant_from_double(const double number) noexcept
        {
            VARIANT value {};
            value.vt = VT_R8;
            value.dblVal = number;
            return value;
        }

        // property 짝 하나를 이벤트 하나로 쏘고 VARIANT를 정리한다.
        void raise_property(uia_event_sink& events, uia_element_provider& provider, const PROPERTYID property, VARIANT old_value, VARIANT new_value)
        {
            events.property_changed(provider, property, old_value, new_value);
            static_cast<void>(VariantClear(&old_value));
            static_cast<void>(VariantClear(&new_value));
        }
    } // namespace

    void uia_root_provider::announce_changes(const std::vector<access_change>& changes)
    {
        for (const access_change& change : changes)
        {
            if (change.kind == access_change_kind::structure)
            {
                // 빈 id는 표면 root 바로 아래가 바뀌었다는 뜻이다.
                if (change.id == ui_element_id {})
                {
                    events_->structure_changed(*this);
                    continue;
                }
                uia_element_provider* const provider { make_element_provider(change.id) };
                events_->structure_changed(*provider);
                provider->Release();
                continue;
            }

            uia_element_provider* const provider { make_element_provider(change.id) };
            if (change.kind == access_change_kind::selected)
            {
                events_->element_selected(*provider);
                provider->Release();
                continue;
            }
            // 다중 선택의 변화다 — 더하기·빼기는 항목, 한꺼번의 변화는 container가 쏜다.
            if (change.kind == access_change_kind::added_to_selection || change.kind == access_change_kind::removed_from_selection || change.kind == access_change_kind::selection_invalidated)
            {
                if (change.kind == access_change_kind::added_to_selection)
                    events_->element_added_to_selection(*provider);
                else if (change.kind == access_change_kind::removed_from_selection)
                    events_->element_removed_from_selection(*provider);
                else
                    events_->selection_invalidated(*provider);
                provider->Release();
                continue;
            }

            // property 하나의 짝에서 바뀐 낱말들을 편다 — 낱말마다 이벤트 하나다.
            const access_info& before { change.previous };
            const access_info& after { change.current };
            if (before.name != after.name)
                raise_property(*events_, *provider, UIA_NamePropertyId, variant_from_utf8(before.name), variant_from_utf8(after.name));
            if (before.value != after.value)
                raise_property(*events_, *provider, UIA_ValueValuePropertyId, variant_from_utf8(before.value), variant_from_utf8(after.value));
            if (before.checked != after.checked || before.mixed != after.mixed)
            {
                raise_property(*events_, *provider, UIA_ToggleToggleStatePropertyId, variant_from_int(toggle_state_of(before)), variant_from_int(toggle_state_of(after)));
            }
            if (before.expanded != after.expanded)
            {
                const auto state { [](const std::optional<bool>& expanded) { return expanded == true ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed; } };
                raise_property(*events_, *provider, UIA_ExpandCollapseExpandCollapseStatePropertyId, variant_from_int(state(before.expanded)), variant_from_int(state(after.expanded)));
            }
            // 범위는 값의 변화만 알린다 — 경계(minimum·maximum)가 흔들리는
            // frame은 구조가 아니라 표시의 사정이고, 보조 기술이 듣는 것은 값이다.
            if (before.range.has_value() && after.range.has_value() && before.range->value != after.range->value)
                raise_property(
                    *events_, *provider, UIA_RangeValueValuePropertyId, variant_from_double(static_cast<double>(before.range->value)), variant_from_double(static_cast<double>(after.range->value)));
            provider->Release();
        }
    }

    void uia_root_provider::set_event_sink_for_test(uia_event_sink& sink) noexcept
    {
        events_ = &sink;
    }

    void uia_root_provider::announce_invoked(uia_element_provider& provider)
    {
        events_->invoked(provider);
    }

    HRESULT uia_root_provider::QueryInterface(REFIID riid, void** object)
    {
        if (object == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple))
            *object = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *object = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot))
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (riid == __uuidof(IRawElementProviderHwndOverride))
            *object = static_cast<IRawElementProviderHwndOverride*>(this);
        else
        {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ui_element_id uia_root_provider::embedding_element(const HWND window) const
    {
        const ui_tree* const tree { host_ != nullptr ? host_->accessibility_tree() : nullptr };
        if (tree == nullptr || window == nullptr)
            return {};
        // 표면이 답하고 tree가 확인한다. 지금 tree에 없는 요소는 대신 설 수 없다 (그 창은 기본 provider대로 선다).
        const ui_element_id element { host_->accessibility_window_element(window) };
        const ui_element* const found { element != ui_element_id {} ? tree->find(element) : nullptr };
        if (found == nullptr || host_->accessibility_embedded_window(*found) != window)
            return {};
        return element;
    }

    HRESULT uia_root_provider::GetOverrideProviderForHwnd(const HWND window, IRawElementProviderSimple** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            if (host_ == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 대신하는 요소가 없으면 그 창은 기본 provider대로 선다.
            //  - 대신하는 것은 **요소 자신**이다 (WinUI의 WebView2가 같은 자리에서 컨트롤 자신의 provider를 낸다).
            //    따로 감싼 자식을 내세우면 그 자식이 우리 fragment tree에 속하는지 그 창에 속하는지가 갈려,
            //    UIA가 그 창에서 나오는 길(형제 찾기)을 끝없이 돈다.
            const ui_element_id element { embedding_element(window) };
            if (element == ui_element_id {})
                return S_OK;
            *result = static_cast<IRawElementProviderSimple*>(make_element_provider(element));
            return S_OK;
        });
    }

    ULONG uia_root_provider::AddRef()
    {
        return ++references_;
    }

    ULONG uia_root_provider::Release()
    {
        const ULONG remaining { --references_ };
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT uia_root_provider::get_ProviderOptions(ProviderOptions* options)
    {
        if (options == nullptr)
            return E_POINTER;
        // 모든 호출을 창 thread(STA)로 모은다 — 표면 상태의 "UI thread 전용"
        // 계약이 새 동기화 없이 지켜진다 (accessibility-design.md).
        *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT uia_root_provider::GetPatternProvider(const PATTERNID pattern, IUnknown** result)
    {
        static_cast<void>(pattern);
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        return S_OK;
    }

    HRESULT uia_root_provider::GetPropertyValue(const PROPERTYID property, VARIANT* result)
    {
        static_cast<void>(property);
        if (result == nullptr)
            return E_POINTER;
        // 창 수준 속성(이름·자리)은 host 창(HWND) provider가 답한다.
        result->vt = VT_EMPTY;
        return host_ != nullptr ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT uia_root_provider::get_HostRawElementProvider(IRawElementProviderSimple** result)
    {
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        if (host_ == nullptr)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const HWND window { host_->accessibility_window() };
        if (window == nullptr)
            return S_OK;
        return UiaHostProviderFromHwnd(window, result);
    }

    HRESULT uia_root_provider::Navigate(const NavigateDirection direction, IRawElementProviderFragment** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            if (host_ == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            if (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)
                return S_OK;
            const ui_tree* const tree { host_->accessibility_tree() };
            if (tree == nullptr)
                return S_OK;
            // tree root가 구조(`none`)면 접혀 그 자식이 최상위가 된다 — 접근
            // tree의 통상 규칙 그대로다. 빈 tree(null root)면 자식도 없다.
            const std::vector<const ui_element*>& children { tree->access_top_level() };
            if (children.empty())
                return S_OK;
            const ui_element* const child { direction == NavigateDirection_FirstChild ? children.front() : children.back() };
            *result = make_element_provider(child->id());
            return S_OK;
        });
    }

    HRESULT uia_root_provider::GetRuntimeId(SAFEARRAY** result)
    {
        if (result == nullptr)
            return E_POINTER;
        // 창의 id는 host(HWND)가 정한다.
        *result = nullptr;
        return S_OK;
    }

    HRESULT uia_root_provider::get_BoundingRectangle(UiaRect* result)
    {
        if (result == nullptr)
            return E_POINTER;
        // 창의 자리도 host가 정한다.
        *result = {};
        return S_OK;
    }

    HRESULT uia_root_provider::GetEmbeddedFragmentRoots(SAFEARRAY** result)
    {
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        return S_OK;
    }

    HRESULT uia_root_provider::SetFocus()
    {
        // 창 초점은 OS의 것이다.
        return S_OK;
    }

    HRESULT uia_root_provider::get_FragmentRoot(IRawElementProviderFragmentRoot** result)
    {
        if (result == nullptr)
            return E_POINTER;
        AddRef();
        *result = this;
        return S_OK;
    }

    HRESULT uia_root_provider::ElementProviderFromPoint(const double x, const double y, IRawElementProviderFragment** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            if (host_ == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const ui_tree* const tree { host_->accessibility_tree() };
            const HWND window { host_->accessibility_window() };
            if (tree == nullptr || window == nullptr)
                return S_OK;
            POINT point { static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y)) };
            if (ScreenToClient(window, &point) == FALSE)
                return S_OK;
            const ui_element* const element { access_element_at(*tree, static_cast<float>(point.x), static_cast<float>(point.y)) };
            if (element == nullptr)
                return S_OK;
            *result = make_element_provider(element->id());
            return S_OK;
        });
    }

    HRESULT uia_root_provider::GetFocus(IRawElementProviderFragment** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            if (host_ == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 정본은 논리 초점이다 — popup의 초점은 OS focus로는 보이지 않는다
            // (accessibility-design.md).
            const ui_element_id focused { host_->accessibility_focus() };
            if (focused == ui_element_id {})
                return S_OK;
            const ui_tree* const tree { host_->accessibility_tree() };
            if (tree == nullptr || tree->find(focused) == nullptr)
                return S_OK;
            *result = make_element_provider(focused);
            return S_OK;
        });
    }

    // --- uia_element_provider ---

    uia_element_provider::uia_element_provider(uia_root_provider& root, ui_element_id id, ui_element_id container) noexcept
        : root_ { &root }
        , id_ { std::move(id) }
        , container_ { std::move(container) }
    {
        // root가 host·runtime 색인을 들고 있어 요소보다 오래 살아야 한다.
        root_->AddRef();
    }

    uia_element_provider::~uia_element_provider()
    {
        root_->Release();
    }

    const ui_element_id& uia_element_provider::element_id() const noexcept
    {
        return id_;
    }

    const ui_element* uia_element_provider::resolve(const ui_tree** tree) const
    {
        const uia_surface_host* const host { root_->host() };
        if (host == nullptr)
            return nullptr;
        const ui_tree* const current { host->accessibility_tree() };
        if (current == nullptr)
            return nullptr;
        if (tree != nullptr)
            *tree = current;
        return current->find(id_);
    }

    const ui_element* uia_element_provider::resolve_item(const ui_tree** tree, std::unique_ptr<ui_element>& probe) const
    {
        const ui_tree* current { nullptr };
        if (const ui_element* const element { resolve(&current) }; element != nullptr || current == nullptr)
        {
            if (tree != nullptr)
                *tree = current;
            return element;
        }
        if (tree != nullptr)
            *tree = current;
        // 지어지지 않은 모델 항목이다. 숨은 container의 항목은 읽을 자리가 아니다 (`access_children`과 같은 답).
        if (container_ == ui_element_id {} || current->visibly_contains(container_) == false)
            return nullptr;
        const ui_element* const container { current->find(container_) };
        const access_item_model* const items { container != nullptr ? container->access_items() : nullptr };
        if (items == nullptr)
            return nullptr;
        const std::optional<std::size_t> index { items->access_item_index(id_) };
        if (index.has_value() == false)
            return nullptr;
        probe = items->access_item_probe(*index);
        return probe.get();
    }

    const ui_element* uia_element_provider::item_container(const ui_tree& tree, std::size_t* index) const
    {
        // 지어진 행은 감싼 선택 container가, 지어지지 않은 항목은 쥔 container가 모델이다.
        const ui_element* container { tree.find(id_) != nullptr ? access_item_container_of(tree, id_) : tree.find(container_) };
        const access_item_model* const items { container != nullptr ? container->access_items() : nullptr };
        if (items == nullptr)
            return nullptr;
        const std::optional<std::size_t> found { items->access_item_index(id_) };
        if (found.has_value() == false)
            return nullptr;
        if (index != nullptr)
            *index = *found;
        return container;
    }

    HRESULT uia_element_provider::perform(const access_request& request)
    {
        // Invoke·Toggle·Select·Expand/Collapse·SetValue가 전부 이 문을 지나므로
        // COM 경계 가드도 여기 하나다.
        return guarded([&]() -> HRESULT {
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(&tree, probe) };
            uia_surface_host* const host { root_->host() };
            if (element == nullptr || tree == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 비활성과 "손이 닿지 않는 자리"(가둠 뒤·숨은 것)는 클라이언트에게 같은
            // 말이다 — 지금은 안 된다.
            //  - 지어지지 않은 항목은 화면에 없으므로 그 container가 손이 닿는지를 본다. 창 밖으로 밀려난 행이
            //    키보드로 닿는 것과 같다.
            const ui_element_id& reached { probe != nullptr ? container_ : id_ };
            if (element->enabled() == false || access_reachable(*tree, reached) == false)
                return UIA_E_ELEMENTNOTENABLED;
            std::optional<std::vector<input_action>> planned { plan_access_request(*element, request) };
            if (planned.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            // 빈 계획은 "이미 그 상태"다. 누름에는 그 뜻이 없으므로 거절이 된다 —
            // S_OK를 돌려주고 아무 일도 하지 않는 것은 클라이언트를 속이는 것이다.
            if (planned->empty())
                return request.command == access_command::invoke ? UIA_E_INVALIDOPERATION : S_OK;
            host->accessibility_dispatch(std::move(*planned));
            // Invoked는 상태 변화가 아니라 발행본 대조로는 알 수 없다 — 배분된
            // 순간이 알릴 수 있는 가장 정직한 시점이다 (쓰기는 비동기다).
            if (request.command == access_command::invoke)
                root_->announce_invoked(*this);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::QueryInterface(REFIID riid, void** object)
    {
        if (object == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple))
            *object = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *object = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IInvokeProvider))
            *object = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IRangeValueProvider))
            *object = static_cast<IRangeValueProvider*>(this);
        else if (riid == __uuidof(IToggleProvider))
            *object = static_cast<IToggleProvider*>(this);
        else if (riid == __uuidof(ISelectionItemProvider))
            *object = static_cast<ISelectionItemProvider*>(this);
        else if (riid == __uuidof(ISelectionProvider))
            *object = static_cast<ISelectionProvider*>(this);
        else if (riid == __uuidof(IExpandCollapseProvider))
            *object = static_cast<IExpandCollapseProvider*>(this);
        else if (riid == __uuidof(IValueProvider))
            *object = static_cast<IValueProvider*>(this);
        else if (riid == __uuidof(ITextProvider))
            *object = static_cast<ITextProvider*>(this);
        else if (riid == __uuidof(IScrollItemProvider))
            *object = static_cast<IScrollItemProvider*>(this);
        else if (riid == __uuidof(IItemContainerProvider))
            *object = static_cast<IItemContainerProvider*>(this);
        else if (riid == __uuidof(IVirtualizedItemProvider))
            *object = static_cast<IVirtualizedItemProvider*>(this);
        else
        {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG uia_element_provider::AddRef()
    {
        return ++references_;
    }

    ULONG uia_element_provider::Release()
    {
        const ULONG remaining { --references_ };
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT uia_element_provider::get_ProviderOptions(ProviderOptions* options)
    {
        return guarded([&]() -> HRESULT {
            if (options == nullptr)
                return E_POINTER;
            *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
            // 자식 창을 대신하는 요소는 그 창의 기본 provider를 **덮는** provider라고 밝힌다. 밝히지 않으면 UIA가 그 창의
            // 자리를 정하지 못해 그 가지에서 나오는 길(형제 찾기)을 끝없이 돈다 — demo에서 UIA3 client가 그렇게 멈췄다.
            const ui_element* const element { resolve() };
            const uia_surface_host* const host { root_->host() };
            if (element != nullptr && host != nullptr && host->accessibility_embedded_window(*element) != nullptr)
                *options = static_cast<ProviderOptions>(*options | ProviderOptions_OverrideProvider);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::GetPatternProvider(const PATTERNID pattern, IUnknown** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(&tree, probe) };
            if (element == nullptr)
                return S_OK;
            // 값이 있는 것만 패턴이 된다 — "없는 것은 두지 않는다".
            const access_info info { element->accessibility() };
            switch (pattern)
            {
            case UIA_InvokePatternId:
                if (invokable(info) && plan_access_request(*element, access_request { access_command::invoke }).has_value())
                    *result = static_cast<IInvokeProvider*>(this);
                break;
            case UIA_RangeValuePatternId:
                if (info.range.has_value())
                    *result = static_cast<IRangeValueProvider*>(this);
                break;
            case UIA_TogglePatternId:
                if (info.checked.has_value())
                    *result = static_cast<IToggleProvider*>(this);
                break;
            case UIA_SelectionItemPatternId:
                if (info.selected.has_value())
                    *result = static_cast<ISelectionItemProvider*>(this);
                break;
            case UIA_SelectionPatternId:
                // 항목의 `get_SelectionContainer`와 같은 술어다 — 내주는 container가
                // 곧 이 패턴을 내건다.
                if (access_selection_container(info))
                    *result = static_cast<ISelectionProvider*>(this);
                break;
            case UIA_ScrollItemPatternId:
                // 흘리는 창 **안에** 있을 때만 뜻이 있다. 창이 없으면 들일 자리가
                // 없고, 그것을 매 요소마다 root에서 다시 찾지 않도록 tree가
                // 배치 부모 색인으로 답한다.
                //  - **그 창의 막대는 그 창의 내용이 아니다.** 창이 자기 안에
                //    세우는 손잡이라 배치로는 안에 있지만, 자기를 자기 안으로
                //    들이라는 명령은 없는 일이다 ("상태와 기능은 서로 일치해야
                //    한다"). 그 판정을 술어 하나가 쥔다 (`access_scroll_item`).
                //  - 지어지지 않은 모델 항목은 모델이 흘릴 길을 답한다 (`access_realize`).
                if (tree != nullptr && (probe != nullptr ? item_container(*tree, nullptr) != nullptr : access_scroll_item(*tree, id_)))
                    *result = static_cast<IScrollItemProvider*>(this);
                break;
            case UIA_ItemContainerPatternId:
                // 창 밖 항목까지 찾는 container만이다 — tree의 자식이 전부인 container는 형제 순회로 충분하다.
                if (probe == nullptr && element->access_items() != nullptr)
                    *result = static_cast<IItemContainerProvider*>(this);
                break;
            case UIA_VirtualizedItemPatternId:
                // 모델 container의 항목이다. 지어진 뒤에도 내건다 — 클라이언트가 쥔 패턴이 행이 지어졌다고 사라지지
                // 않는다 (지어진 항목의 Realize는 창 안으로 들이기다).
                if (tree != nullptr && item_container(*tree, nullptr) != nullptr)
                    *result = static_cast<IVirtualizedItemProvider*>(this);
                break;
            case UIA_ExpandCollapsePatternId:
                if (info.expanded.has_value())
                    *result = static_cast<IExpandCollapseProvider*>(this);
                break;
            case UIA_ValuePatternId:
                // 텍스트 칸은 글이 비어도 값 패턴이다 — 빈 글이 곧 값이다.
                if (info.role == access_role::edit || info.role == access_role::combo_box || info.value.empty() == false)
                    *result = static_cast<IValueProvider*>(this);
                break;
            case UIA_TextPatternId:
                // 문서를 내주는 텍스트 칸만이다 — caret·선택·구간 질의가 전부
                // 그 문서 위에서 선다.
                if (info.role == access_role::edit && text_document_of(*element).has_value())
                    *result = static_cast<ITextProvider*>(this);
                break;
            default:
                break;
            }
            if (*result != nullptr)
                AddRef();
            return S_OK;
        });
    }

    HRESULT uia_element_provider::GetPropertyValue(const PROPERTYID property, VARIANT* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            result->vt = VT_EMPTY;
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(&tree, probe) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            switch (property)
            {
            case UIA_PositionInSetPropertyId:
            case UIA_SizeOfSetPropertyId:
            case UIA_LevelPropertyId: {
                // 모델 container의 항목은 모델 전체에서의 자리를 답한다. 지어진 행만 세면 "30개 중 3번째"가 된다.
                std::size_t index { 0 };
                const ui_element* const container { item_container(*tree, &index) };
                if (container == nullptr)
                    break;
                const access_item_position position { container->access_items()->access_item_position_of(index) };
                if (property == UIA_PositionInSetPropertyId)
                    set_int(*result, static_cast<int>(position.position));
                else if (property == UIA_SizeOfSetPropertyId)
                    set_int(*result, static_cast<int>(position.size));
                else if (position.depth > 0 || element->accessibility().expanded.has_value())
                    // 깊이는 계층의 항목만 말한다 (UIA Level은 1부터). 평평한 목록의 행에 단계를 붙이지 않는다.
                    set_int(*result, position.depth + 1);
                break;
            }
            case UIA_NamePropertyId:
                result->vt = VT_BSTR;
                result->bstrVal = bstr_from_utf8(element->accessibility().name);
                break;
            case UIA_ControlTypePropertyId:
                set_int(*result, control_type_for(element->accessibility().role));
                break;
            case UIA_AutomationIdPropertyId: {
                const std::wstring identifier { automation_id_of(id_) };
                result->vt = VT_BSTR;
                result->bstrVal = SysAllocStringLen(identifier.data(), static_cast<UINT>(identifier.size()));
                break;
            }
            case UIA_OrientationPropertyId: {
                const std::optional<access_orientation> orientation { element->accessibility().orientation };
                if (orientation.has_value())
                {
                    result->vt = VT_I4;
                    result->lVal = *orientation == access_orientation::horizontal ? OrientationType_Horizontal : OrientationType_Vertical;
                }
                break;
            }
            case UIA_IsEnabledPropertyId:
                set_bool(*result, element->enabled());
                break;
            case UIA_IsKeyboardFocusablePropertyId:
                // 보조 버튼(`set_tab_stop(false)`)은 여기서 거짓이 된다 —
                // 키보드로 갈 수 없는 자리를 갈 수 있다고 말하지 않는다.
                set_bool(*result, element->focusable());
                break;
            case UIA_HasKeyboardFocusPropertyId: {
                const uia_surface_host* const host { root_->host() };
                set_bool(*result, host != nullptr && host->accessibility_focus() == id_);
                break;
            }
            case UIA_IsOffscreenPropertyId:
                // 지어지지 않은 항목은 화면에 없다. 지어진 요소는 UIA가 사각형에서 정한다.
                if (probe != nullptr)
                    set_bool(*result, true);
                break;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                set_bool(*result, true);
                break;
            case UIA_IsPasswordPropertyId:
                set_bool(*result, element->accessibility().secret);
                break;
            default:
                break;
            }
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_HostRawElementProvider(IRawElementProviderSimple** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            // 요소는 대개 자기 HWND가 없다 — host 창은 fragment root의 몫이다.
            *result = nullptr;
            // 자식 창을 대신하는 요소(웹뷰 자리표)는 그 창이 host다. UIA가 이 요소와 그 창을 한 요소로 합치고,
            // 그 창의 HWND 가지(페이지의 tree)가 이 요소의 자식으로 선다.
            const ui_element* const element { resolve() };
            const uia_surface_host* const host { root_->host() };
            const HWND window { element != nullptr && host != nullptr ? host->accessibility_embedded_window(*element) : nullptr };
            if (window == nullptr || IsWindow(window) == FALSE)
                return S_OK;
            return UiaHostProviderFromHwnd(window, result);
        });
    }

    HRESULT uia_element_provider::Navigate(const NavigateDirection direction, IRawElementProviderFragment** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(&tree, probe) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 지어지지 않은 항목은 접근 tree에 서지 않는다. 부모만 있다 (그 container). 형제·자식 순회는 지어진
            // 행끼리다 — 들이기(Realize) 뒤에 그 행이 형제 줄에 선다.
            if (probe != nullptr)
            {
                if (direction == NavigateDirection_Parent)
                    *result = root_->make_element_provider(container_);
                return S_OK;
            }

            switch (direction)
            {
            case NavigateDirection_Parent: {
                const ui_element* const parent { access_parent(*tree, id_) };
                if (parent == nullptr)
                {
                    // 감싸는 접근 요소가 없으면 부모는 표면 root다.
                    root_->AddRef();
                    *result = static_cast<IRawElementProviderFragment*>(root_);
                    return S_OK;
                }
                *result = root_->make_element_provider(parent->id());
                return S_OK;
            }
            case NavigateDirection_FirstChild:
            case NavigateDirection_LastChild: {
                // 형제 줄과 **같은 줄을 읽는다** (표면 root가 최상위를 읽는 것과
                // 같다). 여기서만 요소를 들고 자식을 다시 접으면 뒤로 훑은 답이
                // 앞으로 훑은 답과 갈린다 — 같은 id가 둘 선 화면이 그 자리다.
                const std::vector<const ui_element*>& children { tree->access_children_of(*element) };
                if (children.empty())
                    return S_OK;
                const ui_element* const child { direction == NavigateDirection_FirstChild ? children.front() : children.back() };
                *result = root_->make_element_provider(child->id());
                return S_OK;
            }
            case NavigateDirection_NextSibling:
            case NavigateDirection_PreviousSibling: {
                // tree가 생성 때 지은 색인이 답한다 — 형제마다 root에서 부모를
                // 다시 찾으면 긴 평면 목록의 순회가 형제 수의 제곱이 된다.
                const ui_element* const sibling { access_sibling(*tree, id_, direction == NavigateDirection_NextSibling) };
                if (sibling != nullptr)
                    *result = root_->make_element_provider(sibling->id());
                return S_OK;
            }
            default:
                return S_OK;
            }
        });
    }

    HRESULT uia_element_provider::GetRuntimeId(SAFEARRAY** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const int identifiers[2] { UiaAppendRuntimeId, root_->runtime_key(id_) };
            SAFEARRAY* const array { SafeArrayCreateVector(VT_I4, 0, 2) };
            if (array == nullptr)
                return E_OUTOFMEMORY;
            for (LONG index = 0; index < 2; ++index)
            {
                int value { identifiers[index] };
                if (FAILED(SafeArrayPutElement(array, &index, &value)))
                {
                    static_cast<void>(SafeArrayDestroy(array));
                    return E_FAIL;
                }
            }
            *result = array;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_BoundingRectangle(UiaRect* result)
    {
        if (result == nullptr)
            return E_POINTER;
        *result = {};
        const ui_tree* tree { nullptr };
        std::unique_ptr<ui_element> probe {};
        const ui_element* const element { resolve_item(&tree, probe) };
        if (element == nullptr)
            return UIA_E_ELEMENTNOTAVAILABLE;
        // 지어지지 않은 항목은 자리가 없다 (빈 사각형 — 화면 밖).
        if (probe != nullptr)
            return S_OK;
        // 조상의 잘라내기를 거친 사각형이다 — 스크롤로 잘린 행이 화면 밖
        // 사각형을 내면 안 된다 (accessibility-design.md).
        const std::optional<rect_f> bounds { tree->visible_bounds(*element) };
        if (bounds.has_value() == false)
            return S_OK;
        POINT origin { 0, 0 };
        const uia_surface_host* const host { root_->host() };
        if (host != nullptr)
        {
            if (const HWND window { host->accessibility_window() }; window != nullptr)
                static_cast<void>(ClientToScreen(window, &origin));
        }
        // bounds는 이미 물리 client 픽셀이다 — 배율을 곱하면 이중 배율이다
        // (`text_screen_rect`와 같은 걸음).
        result->left = static_cast<double>(origin.x) + static_cast<double>(bounds->x);
        result->top = static_cast<double>(origin.y) + static_cast<double>(bounds->y);
        result->width = static_cast<double>(bounds->width);
        result->height = static_cast<double>(bounds->height);
        return S_OK;
    }

    HRESULT uia_element_provider::GetEmbeddedFragmentRoots(SAFEARRAY** result)
    {
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        return S_OK;
    }

    HRESULT uia_element_provider::SetFocus()
    {
        return guarded([&]() -> HRESULT {
            const ui_tree* tree { nullptr };
            const ui_element* const element { resolve(&tree) };
            uia_surface_host* const host { root_->host() };
            if (element == nullptr || tree == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 자리가 아니거나 손이 닿지 않으면 청하지 않는다. 청해도 서지 않을 것을
            // S_OK로 답하면 클라이언트가 초점이 옮겨 갔다고 믿는다.
            if (element->focusable() == false || access_reachable(*tree, id_) == false)
                return UIA_E_INVALIDOPERATION;
            // 초점은 입력 상태라 여기서 세울 수 없다 — input thread에 청한다
            // (accessibility-action-design.md).
            host->accessibility_request_focus(id_);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_FragmentRoot(IRawElementProviderFragmentRoot** result)
    {
        if (result == nullptr)
            return E_POINTER;
        return root_->get_FragmentRoot(result);
    }

    HRESULT uia_element_provider::Invoke()
    {
        return perform(access_request { access_command::invoke });
    }

    HRESULT uia_element_provider::ScrollIntoView()
    {
        return guarded([&]() -> HRESULT {
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(&tree, probe) };
            uia_surface_host* const host { root_->host() };
            if (element == nullptr || tree == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 지어지지 않은 항목은 들이기와 같다.
            if (probe != nullptr)
                return Realize();
            if (access_reachable(*tree, id_) == false)
                return UIA_E_ELEMENTNOTENABLED;
            // 내걸 때와 같은 술어를 쓴다 — 패턴이 서지 않는 자리는 실행도 없다.
            if (access_scroll_item(*tree, id_) == false)
                return UIA_E_INVALIDOPERATION;
            // 초점 되살리기와 **같은 길**이다. 얼마나 흘릴지는 그 창이 답하고,
            // 이미 보이면 빈 목록이다 — 그때 S_OK로 답하는 것은 다른 절대 명령이
            // "이미 그 상태"에 답하는 것과 같은 규약이다
            // (accessibility-action-design.md).
            std::vector<input_action> actions { route_reveal(*tree, id_) };
            if (actions.empty() == false)
                host->accessibility_dispatch(std::move(actions));
            return S_OK;
        });
    }

    HRESULT uia_element_provider::SetValue(const double value)
    {
        return guarded([&]() -> HRESULT {
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.range.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            // 범위 밖은 UIA의 약속대로 인자 오류다 — 자르지 않는다. 무엇을 잘못
            // 말했는지 클라이언트가 알아야 한다.
            if (value < static_cast<double>(info.range->minimum) || value > static_cast<double>(info.range->maximum))
                return E_INVALIDARG;
            // 읽기 전용에는 접근 거부가 UIA의 낱말이다 (표시 전용 막대·진행률).
            BOOL read_only { TRUE };
            if (const HRESULT read { get_IsReadOnly(&read_only) }; FAILED(read))
                return read;
            if (read_only != FALSE)
                return E_ACCESSDENIED;
            return perform(access_request { access_command::set_value, static_cast<float>(value) });
        });
    }

    HRESULT uia_element_provider::get_Value(double* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = 0.0;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.range.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = static_cast<double>(info.range->value);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_IsReadOnly(BOOL* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = TRUE;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 값을 정할 수 있는가는 **계획이** 답한다. 지금 값으로 물으므로 할 일이
            // 없어(빈 목록), 답이 갈리는 자리는 오직 "이 요소가 절대 값을 받는가"다 —
            // 표시 전용 막대·진행률과 절대 factory(`change_to`·`scroll_to`)가 없는
            // 막대가 참이 되고, 절대 메시지를 가진 막대·스크롤 막대가 거짓이 된다.
            //  - 텍스트 칸은 편집 파이프라인의 유무가 가른다 —
            //    signature가 같은 두 interface(IRangeValueProvider·IValueProvider)의
            //    질문을 여기 하나가 함께 답하고, 범위와 문서는 겹치지 않는다.
            //  - 비활성은 보지 않는다 — `IsEnabled`가 이미 답하는 다른 질문이다.
            const access_info info { element->accessibility() };
            if ((info.role == access_role::edit || info.role == access_role::combo_box) && element->text_input().has_value())
            {
                const uia_surface_host* const host { root_->host() };
                *result = element->text_input()->read_only == false && host != nullptr && host->accessibility_text_target(id_.kind).has_value() ? FALSE : TRUE;
                return S_OK;
            }
            const float current { info.range.has_value() ? info.range->value : 0.0f };
            *result = plan_access_request(*element, access_request { access_command::set_value, current }).has_value() ? FALSE : TRUE;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_Maximum(double* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = 0.0;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.range.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = static_cast<double>(info.range->maximum);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_Minimum(double* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = 0.0;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.range.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = static_cast<double>(info.range->minimum);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_LargeChange(double* result)
    {
        if (result == nullptr)
            return E_POINTER;
        // 걸음의 크기는 element가 정하고 아직 어휘에 없다 (value-step-design.md).
        *result = 0.0;
        return S_OK;
    }

    HRESULT uia_element_provider::get_SmallChange(double* result)
    {
        if (result == nullptr)
            return E_POINTER;
        *result = 0.0;
        return S_OK;
    }

    HRESULT uia_element_provider::Toggle()
    {
        return perform(access_request { access_command::toggle });
    }

    HRESULT uia_element_provider::get_ToggleState(ToggleState* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = ToggleState_Off;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.checked.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = toggle_state_of(info);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::Select()
    {
        return perform_selection(access_command::select);
    }

    HRESULT uia_element_provider::AddToSelection()
    {
        // 다중 선택 container의 항목이면 앱이 받는 더하기 명령이다. 단일 선택에서는 이미 골라진 것을
        // 다시 더하는 것만 뜻이 통하고(할 일 없음), 그 밖은 받는 앱이 없어 무효다.
        return perform_selection(access_command::add_to_selection);
    }

    HRESULT uia_element_provider::RemoveFromSelection()
    {
        // 단일 선택은 고름을 비울 수 없다 — 골라진 것을 빼는 명령은 받는 앱이 없어 무효이고, 골라지지 않은 것을
        // 빼는 것은 할 일이 없다. 다중 선택이면 앱이 받는 빼기 명령이다.
        return perform_selection(access_command::remove_from_selection);
    }

    HRESULT uia_element_provider::perform_selection(const access_command command)
    {
        // 항목을 감싼 container가 여러 항목을 함께 고르는지를 실어 보낸다 — 고르기의 "이미 골라짐"이 그것에
        // 따라 갈린다 (`access_request::multiple_selection`).
        bool multiple { false };
        const auto read_multiple = [&]() -> HRESULT {
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            if (resolve_item(&tree, probe) == nullptr || tree == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            if (probe != nullptr)
            {
                const ui_element* const container { tree->find(container_) };
                multiple = container != nullptr && container->accessibility().multiple_selection;
            }
            else
                multiple = access_multiple_selection(*tree, id_);
            return S_OK;
        };
        const HRESULT read { guarded(read_multiple) };
        if (FAILED(read))
            return read;
        access_request request { command };
        request.multiple_selection = multiple;
        return perform(request);
    }

    HRESULT uia_element_provider::get_IsSelected(BOOL* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = FALSE;
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(nullptr, probe) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.selected.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = *info.selected ? TRUE : FALSE;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_SelectionContainer(IRawElementProviderSimple** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            if (resolve_item(&tree, probe) == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 지어지지 않은 항목은 쥔 container가 곧 선택 container다.
            if (probe != nullptr)
            {
                *result = root_->make_element_provider(container_);
                return S_OK;
            }
            // 감싸는 가장 안쪽 **선택 container**다 — Selection 패턴을 내걸지 않는
            // 부모를 내주면 SelectionItem의 짝 계약이 깨진다. 묶음 밖의
            // 홀로 선 라디오는 container가 없다 (빈 답).
            const ui_element* const container { access_selection_container_of(*tree, id_) };
            if (container == nullptr)
                return S_OK;
            *result = root_->make_element_provider(container->id());
            return S_OK;
        });
    }

    HRESULT uia_element_provider::GetSelection(SAFEARRAY** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // signature가 같은 두 interface(ISelectionProvider·ITextProvider)를
            // 하나가 답한다 — container면 골라진 항목들, 텍스트 칸이면 선택 구간.
            const access_info info { element->accessibility() };
            if (access_selection_container(info))
            {
                // 모델 container는 모델이 답한다 — 창 밖의 고른 항목까지 한계 없이 담긴다. 지어지지 않은 항목의
                // provider는 이 container를 쥐고 모델로 답한다.
                const std::vector<ui_element_id> selected { access_selected_ids(*element) };
                const ui_tree* const tree { root_->host()->accessibility_tree() };
                SAFEARRAY* const array { SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selected.size())) };
                if (array == nullptr)
                    return E_OUTOFMEMORY;
                for (LONG index = 0; index < static_cast<LONG>(selected.size()); ++index)
                {
                    const ui_element_id& item { selected[static_cast<std::size_t>(index)] };
                    const bool built { tree != nullptr && tree->find(item) != nullptr };
                    uia_element_provider* const provider { root_->make_element_provider(item, built ? ui_element_id {} : id_) };
                    const HRESULT put { SafeArrayPutElement(array, &index, static_cast<IRawElementProviderSimple*>(provider)) };
                    // SafeArray가 자기 참조를 쥐었다 (실패했으면 아무도 쥐지 않았다).
                    provider->Release();
                    if (FAILED(put))
                    {
                        static_cast<void>(SafeArrayDestroy(array));
                        return E_FAIL;
                    }
                }
                *result = array;
                return S_OK;
            }
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            SAFEARRAY* const array { SafeArrayCreateVector(VT_UNKNOWN, 0, 1) };
            if (array == nullptr)
                return E_OUTOFMEMORY;
            LONG index { 0 };
            uia_text_range_provider* const range {
                new uia_text_range_provider { *root_, id_, std::min(document->caret, document->anchor), std::max(document->caret, document->anchor) },
            };
            const HRESULT put { SafeArrayPutElement(array, &index, static_cast<ITextRangeProvider*>(range)) };
            range->Release();
            if (FAILED(put))
            {
                static_cast<void>(SafeArrayDestroy(array));
                return E_FAIL;
            }
            *result = array;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::GetVisibleRanges(SAFEARRAY** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            // 한 줄 칸이라 문서 전체가 보이는 구간이다 — 가로 스크롤로 잘린
            // 부분까지 세밀히 자르지 않는다 (caret이 서면 그 자리가 드러난다).
            SAFEARRAY* const array { SafeArrayCreateVector(VT_UNKNOWN, 0, 1) };
            if (array == nullptr)
                return E_OUTOFMEMORY;
            LONG index { 0 };
            uia_text_range_provider* const range { new uia_text_range_provider { *root_, id_, 0, document->text.size() } };
            const HRESULT put { SafeArrayPutElement(array, &index, static_cast<ITextRangeProvider*>(range)) };
            range->Release();
            if (FAILED(put))
            {
                static_cast<void>(SafeArrayDestroy(array));
                return E_FAIL;
            }
            *result = array;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::RangeFromChild(IRawElementProviderSimple* child, ITextRangeProvider** result)
    {
        static_cast<void>(child);
        if (result == nullptr)
            return E_POINTER;
        *result = nullptr;
        // 텍스트 칸 안에 자식 요소가 없다.
        return E_INVALIDARG;
    }

    HRESULT uia_element_provider::RangeFromPoint(const UiaPoint point, ITextRangeProvider** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve() };
            const uia_surface_host* const host { root_->host() };
            if (element == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            if (text_document_of(*element).has_value() == false)
                return UIA_E_INVALIDOPERATION;
            // 좌표를 문서 offset으로 옮기는 것은 표면의 몫이다 (측정 함수가
            // 그쪽에 산다). 모르면 문서 머리의 빈 구간이다.
            const std::optional<std::size_t> offset { host->accessibility_text_offset(id_, static_cast<float>(point.x), static_cast<float>(point.y)) };
            const std::size_t position { offset.value_or(0) };
            *result = new uia_text_range_provider { *root_, id_, position, position };
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_DocumentRange(ITextRangeProvider** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = new uia_text_range_provider { *root_, id_, 0, document->text.size() };
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_SupportedTextSelection(SupportedTextSelection* result)
    {
        if (result == nullptr)
            return E_POINTER;
        // 이 저장소의 텍스트 선택은 하나뿐이다.
        *result = SupportedTextSelection_Single;
        return S_OK;
    }

    HRESULT uia_element_provider::get_CanSelectMultiple(BOOL* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            // container가 다중 선택을 알렸을 때만 참이다 (`access_info::multiple_selection`).
            // 라디오 묶음·탭 막대·보통 목록은 하나뿐이다.
            *result = FALSE;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            *result = element->accessibility().multiple_selection ? TRUE : FALSE;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::get_IsSelectionRequired(BOOL* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = FALSE;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (access_selection_container(info) == false)
                return UIA_E_INVALIDOPERATION;
            // 다중 선택은 모두 빼서 비울 수 있다.
            if (info.multiple_selection)
                return S_OK;
            // 이 저장소의 선택은 비울 수 없다 — 골라진 것을 다시 골라도 아무 일도
            // 없고 `RemoveFromSelection`은 무효다. 그래서 지금 하나가 골라져
            // 있으면 앞으로도 언제나 하나다. 아직 아무것도 고르지 않은 묶음만
            // 빈 채로 남을 수 있다.
            *result = access_selected_ids(*element).empty() ? FALSE : TRUE;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::Expand()
    {
        return perform(access_request { access_command::expand });
    }

    HRESULT uia_element_provider::Collapse()
    {
        return perform(access_request { access_command::collapse });
    }

    HRESULT uia_element_provider::get_ExpandCollapseState(ExpandCollapseState* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = ExpandCollapseState_Collapsed;
            std::unique_ptr<ui_element> probe {};
            const ui_element* const element { resolve_item(nullptr, probe) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_info info { element->accessibility() };
            if (info.expanded.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            *result = *info.expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
            return S_OK;
        });
    }

    HRESULT uia_element_provider::SetValue(const LPCWSTR value)
    {
        return guarded([&]() -> HRESULT {
            const ui_tree* tree { nullptr };
            const ui_element* const element { resolve(&tree) };
            uia_surface_host* const host { root_->host() };
            if (element == nullptr || tree == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            // 글을 넣는 길은 편집 파이프라인에 이어진 텍스트 칸뿐이다.
            // 그 밖의 값(드롭다운의 고른 라벨 등)은 여전히 읽기 전용이다.
            if ((element->accessibility().role != access_role::edit && element->accessibility().role != access_role::combo_box) || element->text_input().has_value() == false)
                return E_ACCESSDENIED;
            if (const auto snapshot { element->text_input() }; snapshot.has_value() && snapshot->read_only)
                return E_ACCESSDENIED;
            const std::optional<text_input_target> target { host->accessibility_text_target(id_.kind) };
            if (target.has_value() == false)
                return E_ACCESSDENIED;
            // 실행과 같은 거절이다 — 비활성·가둠 뒤·숨은 것에는 쓰지 않는다.
            if (element->enabled() == false || access_reachable(*tree, id_) == false)
                return UIA_E_ELEMENTNOTENABLED;
            const utf_conversion_result<std::u8string> converted { utf16_to_utf8(value != nullptr ? std::wstring_view { value } : std::wstring_view {}) };
            if (converted.value.has_value() == false)
                return E_INVALIDARG;
            // TSF가 확정 글을 넣는 그 길(replace_all)이다 — caret은 새 글의 끝에
            // 선다. 적용은 앱 logic의 `apply_text_edit` 몫이라 쓰기는 비동기다.
            text_edit_request request {};
            request.target = *target;
            request.command = text::text_edit_command::replace_all;
            request.offset = converted.value->size();
            request.text = std::move(*converted.value);
            host->accessibility_text_edit(std::move(request));
            return S_OK;
        });
    }

    HRESULT uia_element_provider::FindItemByProperty(IRawElementProviderSimple* const start_after, const PROPERTYID property, const VARIANT value,
        IRawElementProviderSimple** const result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const access_item_model* const items { element->access_items() };
            if (items == nullptr)
                return UIA_E_INVALIDOPERATION;
            const std::size_t count { items->access_item_count() };

            // 찾기는 `start_after`의 **다음**부터다. 이 container의 항목이 아니면 잘못 준 값이다.
            //  - 이 provider가 만든 요소만 돌아온다 — UIA는 남의 provider를 섞지 않는다 (`Compare`와 같은 가정).
            std::size_t begin { 0 };
            if (start_after != nullptr)
            {
                const auto* const after { static_cast<const uia_element_provider*>(start_after) };
                const std::optional<std::size_t> index { items->access_item_index(after->id_) };
                if (index.has_value() == false)
                    return E_INVALIDARG;
                begin = *index + 1u;
            }

            std::optional<std::size_t> found {};
            switch (property)
            {
            case 0:
                // property 없음은 "다음 항목"이다.
                if (begin < count)
                    found = begin;
                break;
            case UIA_NamePropertyId: {
                if (value.vt != VT_BSTR)
                    return E_INVALIDARG;
                const utf_conversion_result<std::u8string> name { utf16_to_utf8(std::wstring_view { value.bstrVal, SysStringLen(value.bstrVal) }) };
                if (name.value.has_value() == false)
                    return E_INVALIDARG;
                for (std::size_t index = begin; index < count && found.has_value() == false; ++index)
                    if (items->access_item_name(index) == *name.value)
                        found = index;
                break;
            }
            case UIA_SelectionItemIsSelectedPropertyId: {
                if (value.vt != VT_BOOL)
                    return E_INVALIDARG;
                // 골라짐은 모델 전체를 한 번 훑은 색인으로 답한다 — 항목마다 고름을 물으면 고른 수만큼 곱해진다.
                const std::vector<std::size_t> selected { items->access_selected_indices() };
                const auto first_selected { std::lower_bound(selected.begin(), selected.end(), begin) };
                if (value.boolVal != VARIANT_FALSE)
                {
                    if (first_selected != selected.end())
                        found = *first_selected;
                    break;
                }
                auto next_selected { first_selected };
                for (std::size_t index = begin; index < count; ++index)
                {
                    if (next_selected != selected.end() && *next_selected == index)
                    {
                        ++next_selected;
                        continue;
                    }
                    found = index;
                    break;
                }
                break;
            }
            case UIA_AutomationIdPropertyId: {
                if (value.vt != VT_BSTR)
                    return E_INVALIDARG;
                const std::optional<ui_element_id> wanted { id_from_automation_id(std::wstring_view { value.bstrVal, SysStringLen(value.bstrVal) }) };
                if (wanted.has_value() == false)
                    break;
                if (const std::optional<std::size_t> index { items->access_item_index(*wanted) }; index.has_value() && *index >= begin)
                    found = index;
                break;
            }
            default:
                // 이 container가 모델로 답할 수 없는 property다 (UIA 규약의 거절).
                return E_INVALIDARG;
            }
            if (found.has_value() == false)
                return S_OK;
            const ui_element_id item { items->access_item_id(*found) };
            const ui_tree* const tree { root_->host()->accessibility_tree() };
            const bool built { tree != nullptr && tree->find(item) != nullptr };
            *result = root_->make_element_provider(item, built ? ui_element_id {} : id_);
            return S_OK;
        });
    }

    HRESULT uia_element_provider::Realize()
    {
        return guarded([&]() -> HRESULT {
            const ui_tree* tree { nullptr };
            std::unique_ptr<ui_element> probe {};
            uia_surface_host* const host { root_->host() };
            if (resolve_item(&tree, probe) == nullptr || tree == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            std::size_t index { 0 };
            const ui_element* const container { item_container(*tree, &index) };
            if (container == nullptr)
                return UIA_E_INVALIDOPERATION;
            // 실행과 같은 거절이다 — 숨은 것·가둠 뒤의 container는 흘리지 않는다.
            if (access_reachable(*tree, container->id()) == false)
                return UIA_E_ELEMENTNOTENABLED;
            std::optional<std::vector<input_action>> actions { container->access_items()->access_realize(index) };
            if (actions.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            // 이미 창 안이면 빈 목록이다. "이미 그 상태"는 S_OK다 (`ScrollIntoView`와 같은 규약).
            if (actions->empty() == false)
                host->accessibility_dispatch(std::move(*actions));
            return S_OK;
        });
    }

    // --- uia_text_range_provider ---

    namespace {
        // 요소 provider의 resolve와 같은 걸음이다 — 지금 tree에서 다시 찾는다.
        [[nodiscard]] const ui_element* resolve_text_element(uia_root_provider& root, const ui_element_id& id)
        {
            const uia_surface_host* const host { root.host() };
            if (host == nullptr)
                return nullptr;
            const ui_tree* const tree { host->accessibility_tree() };
            if (tree == nullptr)
                return nullptr;
            return tree->find(id);
        }

        // 문자 아닌 단위를 이 저장소가 아는 단위로 모은다. 모르는 단위는 다음 큰 단위로 물러선다 (UIA의 규칙):
        // 서식·낱말은 줄로, 쪽은 문서로 간다.
        enum class text_unit_kind
        {
            line,
            paragraph,
            document,
        };

        [[nodiscard]] text_unit_kind text_unit_of(const TextUnit unit) noexcept
        {
            switch (unit)
            {
            case TextUnit_Format:
            case TextUnit_Word:
            case TextUnit_Line:
                return text_unit_kind::line;
            case TextUnit_Paragraph:
                return text_unit_kind::paragraph;
            default:
                return text_unit_kind::document;
            }
        }

        // 그 단위가 시작하는 offset이다 (첫 값은 0, 오름차순).
        //  - 줄은 요소가 답한다 — 넘겨 접은 화면 줄을 아는 것은 칸뿐이다. 답이 없으면 문서가 한 줄이다.
        //  - 문단은 줄 바꿈 뒤에서 시작한다. 한 줄 칸에는 줄 바꿈이 없어 문서 하나다.
        [[nodiscard]] std::vector<std::size_t> text_unit_starts(const ui_element& element, const std::u8string_view text, const text_unit_kind unit)
        {
            std::vector<std::size_t> starts { 0 };
            if (unit == text_unit_kind::line)
            {
                for (const std::size_t start : element.text_line_starts())
                    if (start > starts.back() && start <= text.size())
                        starts.push_back(start);
            }
            else if (unit == text_unit_kind::paragraph)
            {
                for (std::size_t offset = 0; offset < text.size(); ++offset)
                    if (text[offset] == u8'\n')
                        starts.push_back(offset + 1);
            }
            return starts;
        }

        // offset을 품은 단위의 차례다. 단위가 시작하는 자리는 그 단위의 것이다.
        [[nodiscard]] std::size_t enclosing_text_unit(const std::vector<std::size_t>& starts, const std::size_t offset) noexcept
        {
            return static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin()) - 1u;
        }

        [[nodiscard]] std::size_t text_unit_end(const std::vector<std::size_t>& starts, const std::size_t index, const std::u8string_view text) noexcept
        {
            return index + 1u < starts.size() ? starts[index + 1u] : text.size();
        }

        // ASCII만 접는 소문자화다 — FindText의 ignore_case가 이 범위다.
        // 유니코드 대소문자 접기는 이 한 줄 칸의 쓰임보다 큰 계약이다.
        [[nodiscard]] std::u8string ascii_lowered(const std::u8string_view text)
        {
            std::u8string lowered { text };
            for (char8_t& character : lowered)
                if (character >= u8'A' && character <= u8'Z')
                    character = static_cast<char8_t>(character + (u8'a' - u8'A'));
            return lowered;
        }
    } // namespace

    uia_text_range_provider::uia_text_range_provider(uia_root_provider& root, ui_element_id id, const std::size_t begin, const std::size_t end) noexcept
        : root_ { &root }
        , id_ { std::move(id) }
        , begin_ { begin }
        , end_ { end }
    {
        root_->AddRef();
    }

    uia_text_range_provider::~uia_text_range_provider()
    {
        root_->Release();
    }

    std::size_t uia_text_range_provider::begin_offset() const noexcept
    {
        return begin_;
    }

    std::size_t uia_text_range_provider::end_offset() const noexcept
    {
        return end_;
    }

    HRESULT uia_text_range_provider::QueryInterface(REFIID riid, void** object)
    {
        if (object == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ITextRangeProvider))
            *object = static_cast<ITextRangeProvider*>(this);
        else
        {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG uia_text_range_provider::AddRef()
    {
        return ++references_;
    }

    ULONG uia_text_range_provider::Release()
    {
        const ULONG remaining { --references_ };
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT uia_text_range_provider::Clone(ITextRangeProvider** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = new uia_text_range_provider { *root_, id_, begin_, end_ };
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::Compare(ITextRangeProvider* other, BOOL* result)
    {
        if (other == nullptr || result == nullptr)
            return E_POINTER;
        // 이 provider가 만든 구간만 돌아온다 — UIA는 남의 구간을 섞지 않는다.
        const auto* const range { static_cast<uia_text_range_provider*>(other) };
        *result = range->id_ == id_ && range->begin_ == begin_ && range->end_ == end_ ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT uia_text_range_provider::CompareEndpoints(const TextPatternRangeEndpoint endpoint, ITextRangeProvider* other, const TextPatternRangeEndpoint other_endpoint, int* result)
    {
        if (other == nullptr || result == nullptr)
            return E_POINTER;
        const auto* const range { static_cast<uia_text_range_provider*>(other) };
        const std::size_t ours { endpoint == TextPatternRangeEndpoint_Start ? begin_ : end_ };
        const std::size_t theirs { other_endpoint == TextPatternRangeEndpoint_Start ? range->begin_ : range->end_ };
        *result = ours < theirs ? -1 : (ours > theirs ? 1 : 0);
        return S_OK;
    }

    HRESULT uia_text_range_provider::ExpandToEnclosingUnit(const TextUnit unit)
    {
        return guarded([&]() -> HRESULT {
            const ui_element* const element { resolve_text_element(*root_, id_) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            const std::u8string_view text { document->text };
            begin_ = text::text_edit_clamp_offset(text, begin_);
            if (unit == TextUnit_Character)
            {
                end_ = step_character_forward(text, begin_);
                return S_OK;
            }
            // 줄·문단은 그것을 품은 단위로, 문서는 문서 전체로 넓힌다. 한 줄 칸은 줄·문단도 곧 문서다.
            const std::vector<std::size_t> starts { text_unit_starts(*element, text, text_unit_of(unit)) };
            const std::size_t index { enclosing_text_unit(starts, begin_) };
            begin_ = starts[index];
            end_ = text_unit_end(starts, index, text);
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::FindAttribute(const TEXTATTRIBUTEID attribute, const VARIANT value, const BOOL backward, ITextRangeProvider** result)
    {
        static_cast<void>(attribute);
        static_cast<void>(value);
        static_cast<void>(backward);
        if (result == nullptr)
            return E_POINTER;
        // 평문 한 벌이라 속성이 갈리는 구간이 없다.
        *result = nullptr;
        return S_OK;
    }

    HRESULT uia_text_range_provider::FindText(const BSTR text, const BOOL backward, const BOOL ignore_case, ITextRangeProvider** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve_text_element(*root_, id_) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            const utf_conversion_result<std::u8string> needle {
                utf16_to_utf8(text != nullptr ? std::wstring_view { text, SysStringLen(text) } : std::wstring_view {}),
            };
            if (needle.value.has_value() == false || needle.value->empty())
                return E_INVALIDARG;

            const std::u8string_view whole { document->text };
            const std::size_t begin { text::text_edit_clamp_offset(whole, begin_) };
            const std::size_t end { std::max(begin, text::text_edit_clamp_offset(whole, end_)) };
            std::u8string haystack { whole.substr(begin, end - begin) };
            std::u8string wanted { *needle.value };
            if (ignore_case != FALSE)
            {
                haystack = ascii_lowered(haystack);
                wanted = ascii_lowered(wanted);
            }
            const std::size_t found { backward != FALSE ? haystack.rfind(wanted) : haystack.find(wanted) };
            if (found == std::u8string::npos)
                return S_OK;
            *result = new uia_text_range_provider { *root_, id_, begin + found, begin + found + wanted.size() };
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::GetAttributeValue(const TEXTATTRIBUTEID attribute, VARIANT* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            result->vt = VT_EMPTY;
            if (attribute == UIA_IsReadOnlyAttributeId)
            {
                const uia_surface_host* const host { root_->host() };
                result->vt = VT_BOOL;
                const ui_element* const element { resolve() };
                const auto snapshot { element != nullptr ? element->text_input() : std::nullopt };
                result->boolVal = snapshot.has_value() && snapshot->read_only == false && host != nullptr && host->accessibility_text_target(id_.kind).has_value() ? VARIANT_FALSE : VARIANT_TRUE;
                return S_OK;
            }
            // 그 밖의 속성은 지원하지 않는다는 예약 값이다.
            IUnknown* not_supported { nullptr };
            if (FAILED(UiaGetReservedNotSupportedValue(&not_supported)))
                return E_FAIL;
            result->vt = VT_UNKNOWN;
            result->punkVal = not_supported;
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::GetBoundingRectangles(SAFEARRAY** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve_text_element(*root_, id_) };
            const uia_surface_host* const host { root_->host() };
            if (element == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            const std::u8string_view text { document->text };
            const std::size_t begin { text::text_edit_clamp_offset(text, begin_) };
            const std::size_t end { std::max(begin, text::text_edit_clamp_offset(text, end_)) };
            const std::optional<RECT> rect { host->accessibility_text_span(id_, text, document->caret, begin, end) };
            if (rect.has_value() == false)
            {
                // 아직 배치를 모른다 — 빈 목록이 UIA의 답이다.
                *result = SafeArrayCreateVector(VT_R8, 0, 0);
                return *result != nullptr ? S_OK : E_OUTOFMEMORY;
            }
            SAFEARRAY* const array { SafeArrayCreateVector(VT_R8, 0, 4) };
            if (array == nullptr)
                return E_OUTOFMEMORY;
            const double values[4] {
                static_cast<double>(rect->left),
                static_cast<double>(rect->top),
                static_cast<double>(rect->right - rect->left),
                static_cast<double>(rect->bottom - rect->top),
            };
            for (LONG index = 0; index < 4; ++index)
            {
                double value { values[index] };
                if (FAILED(SafeArrayPutElement(array, &index, &value)))
                {
                    static_cast<void>(SafeArrayDestroy(array));
                    return E_FAIL;
                }
            }
            *result = array;
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::GetEnclosingElement(IRawElementProviderSimple** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = root_->make_element_provider(id_);
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::GetText(const int maximum_length, BSTR* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve_text_element(*root_, id_) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            const std::u8string_view text { document->text };
            const std::size_t begin { text::text_edit_clamp_offset(text, begin_) };
            const std::size_t end { std::max(begin, text::text_edit_clamp_offset(text, end_)) };
            const utf_conversion_result<std::wstring> converted { utf8_to_utf16(text.substr(begin, end - begin)) };
            if (converted.value.has_value() == false)
                return E_FAIL;
            std::wstring wide { std::move(*converted.value) };
            if (maximum_length >= 0 && wide.size() > static_cast<std::size_t>(maximum_length))
            {
                wide.resize(static_cast<std::size_t>(maximum_length));
                // surrogate 쌍의 앞쪽만 남기지 않는다 — 반쪽 문자는 문자가 아니다.
                if (wide.empty() == false && wide.back() >= 0xD800 && wide.back() <= 0xDBFF)
                    wide.pop_back();
            }
            *result = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
            return *result != nullptr ? S_OK : E_OUTOFMEMORY;
        });
    }

    HRESULT uia_text_range_provider::Move(const TextUnit unit, const int count, int* moved)
    {
        return guarded([&]() -> HRESULT {
            if (moved == nullptr)
                return E_POINTER;
            *moved = 0;
            const ui_element* const element { resolve_text_element(*root_, id_) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            const std::u8string_view text { document->text };
            std::size_t position { text::text_edit_clamp_offset(text, begin_) };
            const bool degenerate { position == text::text_edit_clamp_offset(text, end_) };
            if (unit != TextUnit_Character)
            {
                // 품은 단위의 처음에서 단위만큼 옮기고 그 단위로 넓힌다 (빈 구간은 빈 채로 둔다).
                //  - 뒤로 옮길 때 단위 가운데에 있었으면 그 단위의 처음까지가 한 걸음이다.
                //  - 문서는 하나뿐이라 옮길 곳이 없다.
                const std::vector<std::size_t> starts { text_unit_starts(*element, text, text_unit_of(unit)) };
                std::size_t index { enclosing_text_unit(starts, position) };
                int steps { 0 };
                if (count > 0)
                    while (steps < count && index + 1u < starts.size())
                    {
                        ++index;
                        ++steps;
                    }
                else if (count < 0)
                {
                    if (position > starts[index])
                        ++steps;
                    while (steps < -count && index > 0)
                    {
                        --index;
                        ++steps;
                    }
                }
                begin_ = starts[index];
                end_ = degenerate ? begin_ : text_unit_end(starts, index, text);
                *moved = count >= 0 ? steps : -steps;
                return S_OK;
            }
            int steps { 0 };
            while (steps < (count >= 0 ? count : -count))
            {
                const std::size_t next { count >= 0 ? step_character_forward(text, position) : step_character_backward(text, position) };
                if (next == position)
                    break;
                position = next;
                ++steps;
            }
            begin_ = position;
            end_ = degenerate ? position : step_character_forward(text, position);
            *moved = count >= 0 ? steps : -steps;
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::MoveEndpointByUnit(const TextPatternRangeEndpoint endpoint, const TextUnit unit, const int count, int* moved)
    {
        return guarded([&]() -> HRESULT {
            if (moved == nullptr)
                return E_POINTER;
            *moved = 0;
            const ui_element* const element { resolve_text_element(*root_, id_) };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            const std::u8string_view text { document->text };
            std::size_t position { text::text_edit_clamp_offset(text, endpoint == TextPatternRangeEndpoint_Start ? begin_ : end_) };
            if (unit == TextUnit_Character)
            {
                int steps { 0 };
                while (steps < (count >= 0 ? count : -count))
                {
                    const std::size_t next { count >= 0 ? step_character_forward(text, position) : step_character_backward(text, position) };
                    if (next == position)
                        break;
                    position = next;
                    ++steps;
                }
                *moved = count >= 0 ? steps : -steps;
            }
            else if (count != 0)
            {
                // 단위의 경계(시작들과 문서의 끝)를 하나씩 넘는다. 문서 단위의 경계는 처음과 끝뿐이다.
                std::vector<std::size_t> boundaries { text_unit_starts(*element, text, text_unit_of(unit)) };
                if (boundaries.back() != text.size())
                    boundaries.push_back(text.size());
                int steps { 0 };
                while (steps < (count >= 0 ? count : -count))
                {
                    const auto after { std::upper_bound(boundaries.begin(), boundaries.end(), position) };
                    const auto before { std::lower_bound(boundaries.begin(), boundaries.end(), position) };
                    if (count > 0 ? after == boundaries.end() : before == boundaries.begin())
                        break;
                    position = count > 0 ? *after : *(before - 1);
                    ++steps;
                }
                *moved = count >= 0 ? steps : -steps;
            }
            if (endpoint == TextPatternRangeEndpoint_Start)
            {
                begin_ = position;
                if (end_ < begin_)
                    end_ = begin_;
            }
            else
            {
                end_ = position;
                if (begin_ > end_)
                    begin_ = end_;
            }
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::MoveEndpointByRange(const TextPatternRangeEndpoint endpoint, ITextRangeProvider* other, const TextPatternRangeEndpoint other_endpoint)
    {
        if (other == nullptr)
            return E_POINTER;
        const auto* const range { static_cast<uia_text_range_provider*>(other) };
        const std::size_t position { other_endpoint == TextPatternRangeEndpoint_Start ? range->begin_ : range->end_ };
        if (endpoint == TextPatternRangeEndpoint_Start)
        {
            begin_ = position;
            if (end_ < begin_)
                end_ = begin_;
        }
        else
        {
            end_ = position;
            if (begin_ > end_)
                begin_ = end_;
        }
        return S_OK;
    }

    HRESULT uia_text_range_provider::Select()
    {
        return guarded([&]() -> HRESULT {
            uia_surface_host* const host { root_->host() };
            const ui_element* const element { resolve_text_element(*root_, id_) };
            if (element == nullptr || host == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            const ui_tree* const tree { host->accessibility_tree() };
            const std::optional<access_text_document> document { text_document_of(*element) };
            if (document.has_value() == false || tree == nullptr)
                return UIA_E_INVALIDOPERATION;
            const std::optional<text_input_target> target { host->accessibility_text_target(id_.kind) };
            if (target.has_value() == false)
                return UIA_E_INVALIDOPERATION;
            if (element->enabled() == false || access_reachable(*tree, id_) == false)
                return UIA_E_ELEMENTNOTENABLED;

            const std::u8string_view text { document->text };
            const std::size_t begin { text::text_edit_clamp_offset(text, begin_) };
            const std::size_t end { std::max(begin, text::text_edit_clamp_offset(text, end_)) };
            // TSF·포인터와 같은 편집 파이프라인이다 — anchor를 두고(place_caret)
            // 끝까지 늘린다(extend). 적용은 앱 logic의 몫이라 쓰기는 비동기다.
            text_edit_request anchor {};
            anchor.target = *target;
            anchor.command = text::text_edit_command::place_caret;
            anchor.offset = begin;
            host->accessibility_text_edit(std::move(anchor));
            if (end != begin)
            {
                text_edit_request extend {};
                extend.target = *target;
                extend.command = text::text_edit_command::place_caret;
                extend.extend = true;
                extend.offset = end;
                host->accessibility_text_edit(std::move(extend));
            }
            return S_OK;
        });
    }

    HRESULT uia_text_range_provider::AddToSelection()
    {
        // 텍스트 선택은 하나뿐이다 (`get_SupportedTextSelection`).
        return UIA_E_INVALIDOPERATION;
    }

    HRESULT uia_text_range_provider::RemoveFromSelection()
    {
        return UIA_E_INVALIDOPERATION;
    }

    HRESULT uia_text_range_provider::ScrollIntoView(const BOOL align_to_top)
    {
        static_cast<void>(align_to_top);
        if (resolve_text_element(*root_, id_) == nullptr)
            return UIA_E_ELEMENTNOTAVAILABLE;
        // 한 줄 칸은 caret이 선 자리를 편집이 스스로 드러낸다 — 따로 흘릴 것이
        // 없다.
        return S_OK;
    }

    HRESULT uia_text_range_provider::GetChildren(SAFEARRAY** result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            // 텍스트 안에 담긴 요소가 없다.
            *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
            return *result != nullptr ? S_OK : E_OUTOFMEMORY;
        });
    }

    HRESULT uia_element_provider::get_Value(BSTR* result)
    {
        return guarded([&]() -> HRESULT {
            if (result == nullptr)
                return E_POINTER;
            *result = nullptr;
            const ui_element* const element { resolve() };
            if (element == nullptr)
                return UIA_E_ELEMENTNOTAVAILABLE;
            *result = bstr_from_utf8(element->accessibility().value);
            return S_OK;
        });
    }
} // namespace luil::win32

#pragma once

#include "luil/ui/accessibility.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <windows.h>

// `uiautomationcore.h`는 COM 기반 어휘(interface 매크로)를 스스로 세우지
// 않는다 — `win32_drop.h`가 <ole2.h>를 먼저 두는 것과 같은 이유다.
#include <ole2.h>

#include <uiautomationcore.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace luil::win32 {
    // provider가 표면에게 묻는 것 전부다.
    // `window_surface`가 구현하고 test는 가짜를 꽂는다 — COM 껍데기와 표면이
    // 서로의 형태를 모른다 (`drop_target_host`와 같은 경계).
    class uia_surface_host
    {
    public:
        uia_surface_host() = default;
        uia_surface_host(const uia_surface_host&) = delete;
        uia_surface_host(uia_surface_host&&) = delete;
        uia_surface_host& operator=(const uia_surface_host&) = delete;
        uia_surface_host& operator=(uia_surface_host&&) = delete;
        virtual ~uia_surface_host() = default;

        // 지금 게시된 이 표면의 tree다. 없으면 nullptr다 (첫 frame 전·smoke).
        [[nodiscard]] virtual const ui_tree* accessibility_tree() const noexcept = 0;
        // 이 표면의 창이다. 없으면 nullptr다 — 화면 좌표 변환만 빠지고 나머지는 선다.
        [[nodiscard]] virtual HWND accessibility_window() const noexcept = 0;
        // 이 표면의 것으로 걸러진 논리 초점이다 (빈 id면 초점 없음).
        // OS focus가 아니다 — popup의 초점은 OS focus로는 영영 보이지 않는다
        // (accessibility-design.md).
        [[nodiscard]] virtual ui_element_id accessibility_focus() const = 0;

        // 접근 실행이 낸 액션을 통상 규칙대로 배분한다 (앱 메시지·창 명령·클립보드).
        // caption 버튼·파일 놓기와 같은 자리다 — 실행할 것은 tree에 이미 있고
        // 표면이 그것을 배분한다 (accessibility-action-design.md).
        virtual void accessibility_dispatch(std::vector<input_action> actions) = 0;

        // 논리 초점을 이 요소로 옮겨 달라고 input thread에 청한다.
        // 초점은 tree가 아니라 **입력 상태**라 여기서 곧바로 세울 수 없다 (3.4).
        virtual void accessibility_request_focus(const ui_element_id& id) = 0;

        // --- 텍스트 칸의 Text pattern·값 쓰기가 딛는 자리다 ---
        // TSF와 같은 편집 파이프라인을 탄다: 대상 해석은 앱 정책
        // (`text_target_of`)이, 적용은 앱 logic(`apply_text_edit`)이 한다.

        // 이 kind의 텍스트 칸이 편집 파이프라인에 이어져 있는가 (그 대상 id).
        // nullopt면 글을 넣는 길이 없다 — Value 쓰기가 읽기 전용으로 선다.
        [[nodiscard]] virtual std::optional<text_input_target> accessibility_text_target(ui_element_kind kind) const = 0;
        // 편집 하나를 앱 메시지로 바꿔 통상 규칙대로 배분한다
        // (`accessibility_dispatch`와 같은 미룸 — 창의 자기 차례다).
        virtual void accessibility_text_edit(text_edit_request request) = 0;
        // 그 요소의 문서 byte 구간이 화면(스크린 좌표)에서 차지하는 자리다.
        // 문서는 부르는 쪽이 넘긴다 — 조합 중에는 요소의 확정 글보다 길어서다
        // (`tsf_host::text_screen_rect`와 같은 이유·같은 모양). 모르면 nullopt다.
        [[nodiscard]] virtual std::optional<RECT> accessibility_text_span(const ui_element_id& id, std::u8string_view document, std::size_t caret, std::size_t begin,
            std::size_t end) const = 0;
        // 화면 좌표를 그 요소의 문서 byte offset으로 옮긴다. 모르면 nullopt다.
        [[nodiscard]] virtual std::optional<std::size_t> accessibility_text_offset(const ui_element_id& id, float screen_x, float screen_y) const = 0;
    };

    class uia_element_provider;

    // UIA 이벤트가 실제로 나가는 창구다 (`UiaRaise*` 호출 하나가 메서드 하나다).
    // 기본 구현은 OS로 쏘고 test는 mock을 꽂아 "각 변화에 정확히 한 번, 옳은
    // 낱말로" 나가는지를 잰다 — COM 밖으로 나가는 호출은 이 경계 없이는 잴 수
    // 없다 (`drop_target_host`·`uia_surface_host`와 같은 갈래다).
    class uia_event_sink
    {
    public:
        uia_event_sink() = default;
        uia_event_sink(const uia_event_sink&) = delete;
        uia_event_sink(uia_event_sink&&) = delete;
        uia_event_sink& operator=(const uia_event_sink&) = delete;
        uia_event_sink& operator=(uia_event_sink&&) = delete;
        virtual ~uia_event_sink() = default;

        // 초점이 이 요소로 옮겨 갔다.
        virtual void focus_changed(uia_element_provider& provider) = 0;
        // Invoke가 이 요소의 클릭을 배분했다.
        // 쓰기는 비동기라 "실행이 끝난 뒤"는 없다 — 액션이 앱으로 떠난 순간이
        // 이 저장소에서 알릴 수 있는 가장 정직한 시점이다.
        virtual void invoked(uia_element_provider& provider) = 0;
        // 이 요소의 property 하나가 old → new로 바뀌었다.
        // VARIANT의 소유는 부르는 쪽에 남는다 (BSTR도 그쪽이 지운다).
        virtual void property_changed(uia_element_provider& provider, PROPERTYID property, const VARIANT& old_value, const VARIANT& new_value) = 0;
        // 이 항목이 골라졌다 (단일 선택의 ElementSelected).
        virtual void element_selected(uia_element_provider& provider) = 0;
        // 이 요소(요소 provider 또는 표면 root)의 자식 구성이 바뀌었다.
        virtual void structure_changed(IRawElementProviderSimple& provider) = 0;
    };

    // 표면 하나의 UIA fragment root다.
    // `WM_GETOBJECT`가 돌려주는 그 객체이고, 요소 provider는 여기서 태어난다.
    // UI thread(STA) 전용이라 참조 수는 원자가 아니다 —
    // `ProviderOptions_UseComThreading`이 모든 호출을 창 thread로 모은다
    // (accessibility-design.md).
    class uia_root_provider final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot
    {
    public:
        explicit uia_root_provider(uia_surface_host& host) noexcept;
        uia_root_provider(const uia_root_provider&) = delete;
        uia_root_provider(uia_root_provider&&) = delete;
        uia_root_provider& operator=(const uia_root_provider&) = delete;
        uia_root_provider& operator=(uia_root_provider&&) = delete;
        virtual ~uia_root_provider() = default;

        // 창이 죽었다. 이후 모든 질문은 `UIA_E_ELEMENTNOTAVAILABLE`이다 —
        // 클라이언트가 아직 쥔 provider가 있어도 안전하게 낡는다.
        void detach() noexcept;
        // 표면은 provider가 소유하지 않는 협력자라 const 자리에서도 그대로 내준다
        // (실행은 표면을 통해 나간다).
        [[nodiscard]] uia_surface_host* host() const noexcept;
        // id의 요소 provider를 만든다 (참조 수 1 — 부르는 쪽이 놓는다).
        [[nodiscard]] uia_element_provider* make_element_provider(ui_element_id id);
        // RuntimeId의 둘째 칸이다 (이 root 안에서 id마다 유일, **지금 tree에
        // 있는 동안** 안정). 동적 id가 갈릴 때마다 지도가 자라므로 문턱을 넘으면
        // 지금 tree에 없는 id의 key를 비운다 — 사라진 id가 되돌아오면 새 key를
        // 받지만, 그 사이 그 요소의 다른 질문은 이미 "없다"로 낡아 있었다.
        [[nodiscard]] int runtime_key(const ui_element_id& id);
        // 들고 있는 runtime key 수다 (test와 진단이 본다 — 정리 정책의 관찰 창).
        [[nodiscard]] std::size_t runtime_key_count() const noexcept;
        // 초점 이동을 UIA 클라이언트에 알린다.
        void announce_focus(const ui_element_id& id);
        // 접근 발행본의 대조(`diff_access_snapshots`)가 편 변화들을 UIA 이벤트로
        // 알린다 — 변화 하나가 이벤트 하나다 (property는 바뀐 낱말마다 하나).
        // 어느 property가 어떻게 바뀌었는지는 여기서 짝(previous·current)을 견줘
        // 정한다 — 어휘(ui 계층)는 platform 낱말을 모른다.
        void announce_changes(const std::vector<access_change>& changes);
        // 이벤트 창구를 mock으로 바꾼다 (test 전용 — 기본은 `UiaRaise*`다).
        // sink는 이 provider보다 오래 살아야 한다.
        void set_event_sink_for_test(uia_event_sink& sink) noexcept;
        // Invoke가 배분됐다 (요소 provider의 `perform`이 부른다).
        void announce_invoked(uia_element_provider& provider);

        // IUnknown이다.
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // IRawElementProviderSimple이다.
        HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override;
        HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) override;
        HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) override;
        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override;

        // IRawElementProviderFragment다.
        HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override;
        HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override;
        HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE SetFocus() override;
        HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override;

        // IRawElementProviderFragmentRoot다.
        HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) override;
        HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override;

    private:
        struct id_hash
        {
            [[nodiscard]] std::size_t operator()(const ui_element_id& id) const noexcept
            {
                // `ui_tree`의 색인과 같은 섞음이다.
                return std::hash<std::u8string> {}(id.owner) ^ (static_cast<std::size_t>(id.kind) * 1099511628211ull);
            }
        };

        // 문턱을 넘으면 지금 tree에 없는 id의 key를 비운다 (`keep`은 지금 물은
        // id라 남긴다). 정리 후 문턱을 크기의 두 배로 다시 잡아 물음당 상수
        // 비용으로 만든다 — 지도는 산 id 수의 상수 배에 머문다.
        void prune_runtime_keys(const ui_element_id& keep);

        // 이 크기 전에는 정리를 생각하지 않는다 — 작은 지도는 자라게 둔다.
        static constexpr std::size_t runtime_key_prune_floor { 256 };

        uia_surface_host* host_ { nullptr };
        // 이벤트가 나가는 창구다. 기본은 `UiaRaise*`를 부르는 공용 sink 하나다.
        uia_event_sink* events_ { nullptr };
        ULONG references_ { 1 };
        std::unordered_map<ui_element_id, int, id_hash> runtime_keys_ {};
        int next_runtime_key_ { 1 };
        std::size_t runtime_key_prune_threshold_ { runtime_key_prune_floor };
    };

    // 요소 하나의 provider다.
    // element 포인터가 아니라 **id**를 들고, 물을 때마다 지금 tree에서 다시
    // 찾는다 — tree는 frame마다 통째로 새로 지어지고 id만이 그것을 넘는
    // 정체성이다 (accessibility-design.md). 지금 tree에 없으면
    // `UIA_E_ELEMENTNOTAVAILABLE`이다.
    // 쓰기(Invoke·Toggle·Select·Expand/Collapse·SetValue)는 **지금 tree의 액션**을
    // 계획해 표면에 넘긴다 — 마우스로 누를 수 없는 것은 보조 기술로도 누를 수
    // 없다 (accessibility-action-design.md). 글을 넣는 쓰기(`IValueProvider`)만
    // 아직 없다.
    class uia_element_provider final : public IRawElementProviderSimple,
        public IRawElementProviderFragment,
        public IInvokeProvider,
        public IRangeValueProvider,
        public IToggleProvider,
        public ISelectionItemProvider,
        public ISelectionProvider,
        public IExpandCollapseProvider,
        public IValueProvider,
        public ITextProvider
    {
    public:
        uia_element_provider(uia_root_provider& root, ui_element_id id) noexcept;
        uia_element_provider(const uia_element_provider&) = delete;
        uia_element_provider(uia_element_provider&&) = delete;
        uia_element_provider& operator=(const uia_element_provider&) = delete;
        uia_element_provider& operator=(uia_element_provider&&) = delete;
        virtual ~uia_element_provider();

        // 이 provider가 대신하는 요소다 (test와 진단이 본다).
        [[nodiscard]] const ui_element_id& element_id() const noexcept;

        // IUnknown이다.
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // IRawElementProviderSimple이다.
        HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override;
        HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) override;
        HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) override;
        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override;

        // IRawElementProviderFragment다.
        HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override;
        HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override;
        HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE SetFocus() override;
        HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override;

        // IInvokeProvider다.
        HRESULT STDMETHODCALLTYPE Invoke() override;

        // IRangeValueProvider다.
        HRESULT STDMETHODCALLTYPE SetValue(double value) override;
        HRESULT STDMETHODCALLTYPE get_Value(double* result) override;
        HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* result) override;
        HRESULT STDMETHODCALLTYPE get_Maximum(double* result) override;
        HRESULT STDMETHODCALLTYPE get_Minimum(double* result) override;
        HRESULT STDMETHODCALLTYPE get_LargeChange(double* result) override;
        HRESULT STDMETHODCALLTYPE get_SmallChange(double* result) override;

        // IToggleProvider다.
        HRESULT STDMETHODCALLTYPE Toggle() override;
        HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* result) override;

        // ISelectionItemProvider다.
        HRESULT STDMETHODCALLTYPE Select() override;
        HRESULT STDMETHODCALLTYPE AddToSelection() override;
        HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
        HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* result) override;
        HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** result) override;

        // ISelectionProvider다 (선택 container — 목록·탭 막대·라디오 묶음).
        // SelectionItem의 짝인 container 계약이다: 항목의
        // `get_SelectionContainer`가 돌려주는 요소가 이 패턴을 내걸어야
        // 클라이언트가 선택 목록과 정책을 조회할 수 있다.
        //  - `GetSelection`은 ITextProvider와 signature가 같아 하나가 두
        //    interface를 함께 답한다 — container면 골라진 항목들, 텍스트 칸이면
        //    선택 구간이다 (`get_IsReadOnly`와 같은 갈래).
        HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* result) override;
        HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* result) override;

        // IExpandCollapseProvider다.
        HRESULT STDMETHODCALLTYPE Expand() override;
        HRESULT STDMETHODCALLTYPE Collapse() override;
        HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* result) override;

        // IValueProvider다.
        // 텍스트 칸의 쓰기는 TSF와 같은 편집 파이프라인(`replace_all`)을 탄다 —
        // 편집 대상이 이어진 칸만 쓸 수 있다.
        // `get_IsReadOnly`는 IRangeValueProvider와 signature가 같아 위의 것 하나가
        // 두 interface를 함께 답한다 — 범위가 있으면 계획이, 텍스트 칸이면 편집
        // 대상의 유무가 가른다.
        HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
        HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) override;

        // ITextProvider다 (편집 정본과 이어진 Text pattern).
        // 문서는 요소가 내주는 표시 문서다: 확정 글, 조합 중에는 조합 글 —
        // TSF가 같은 원천에서 같은 문서를 본다. offset은 UTF-8 byte로 들고
        // UTF-16 변환은 경계(GetText)에서 한다.
        // `GetSelection`은 위(ISelectionProvider)의 것이 함께 답한다.
        HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple* child, ITextRangeProvider** result) override;
        HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider** result) override;
        HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** result) override;
        HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* result) override;

    private:
        // 지금 tree에서 이 id를 다시 찾는다. 없으면 nullptr다.
        [[nodiscard]] const ui_element* resolve(const ui_tree** tree = nullptr) const;
        // 명령 하나를 지금 tree에 물어 표면으로 넘긴다.
        // 거절은 낱말을 가려 답한다: 사라졌으면 `UIA_E_ELEMENTNOTAVAILABLE`,
        // 비활성이거나 손이 닿지 않으면 `UIA_E_ELEMENTNOTENABLED`, 이 자리에서
        // 할 수 없는 일이면 `UIA_E_INVALIDOPERATION`이다.
        [[nodiscard]] HRESULT perform(const access_request& request);

        uia_root_provider* root_ { nullptr };
        ui_element_id id_ {};
        ULONG references_ { 1 };
    };

    // 텍스트 칸 문서의 구간 하나다 (ITextRangeProvider).
    // 요소 provider처럼 **id와 UTF-8 byte offset**만 들고, 물을 때마다 지금
    // tree의 표시 문서로 다시 자른다 — 문서는 frame마다 바뀔 수 있고 offset은
    // 문자 경계로 되돌아간다. 단위는 문자(codepoint)와 문서뿐이다: 한 줄 칸이라
    // 줄·문단·쪽이 곧 문서이고, 낱말은 다음 큰 단위(문서)로 물러선다
    // (UIA가 허락하는 규칙이다).
    class uia_text_range_provider final : public ITextRangeProvider
    {
    public:
        uia_text_range_provider(uia_root_provider& root, ui_element_id id, std::size_t begin, std::size_t end) noexcept;
        uia_text_range_provider(const uia_text_range_provider&) = delete;
        uia_text_range_provider(uia_text_range_provider&&) = delete;
        uia_text_range_provider& operator=(const uia_text_range_provider&) = delete;
        uia_text_range_provider& operator=(uia_text_range_provider&&) = delete;
        virtual ~uia_text_range_provider();

        // 구간(UTF-8 byte)이다 — test와 진단이 본다.
        [[nodiscard]] std::size_t begin_offset() const noexcept;
        [[nodiscard]] std::size_t end_offset() const noexcept;

        // IUnknown이다.
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // ITextRangeProvider다.
        HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** result) override;
        HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* other, BOOL* result) override;
        HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* other, TextPatternRangeEndpoint other_endpoint, int* result) override;
        HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
        HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID attribute, VARIANT value, BOOL backward, ITextRangeProvider** result) override;
        HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignore_case, ITextRangeProvider** result) override;
        HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* result) override;
        HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** result) override;
        HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** result) override;
        HRESULT STDMETHODCALLTYPE GetText(int maximum_length, BSTR* result) override;
        HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override;
        HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int* moved) override;
        HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* other, TextPatternRangeEndpoint other_endpoint) override;
        HRESULT STDMETHODCALLTYPE Select() override;
        HRESULT STDMETHODCALLTYPE AddToSelection() override;
        HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
        HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL align_to_top) override;
        HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** result) override;

    private:
        uia_root_provider* root_ { nullptr };
        ui_element_id id_ {};
        // UTF-8 byte offset이다. 물을 때마다 지금 문서로 자른다.
        std::size_t begin_ { 0 };
        std::size_t end_ { 0 };
        ULONG references_ { 1 };
    };
} // namespace luil::win32

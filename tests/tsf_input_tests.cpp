#include "win32/win32_tsf_input.h"

#include <objbase.h>
#include <olectl.h>
#include <textstor.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// TSF는 문서를 **ACP(UTF-16 코드 단위)**로만 다루고 그림자 문서는 UTF-8이다.
// 단위가 섞이면 조합이 엉뚱한 자리에 들어가므로 경계 변환을 못박는다.
// COM에 기대지 않는 순수 부분이라 test가 가능하다.
TEST_CASE("ACP offsets count UTF-16 code units", "[win32][tsf]")
{
    constexpr std::u8string_view latin { u8"abc" };
    REQUIRE(luil::win32::acp_length(latin) == 3);
    REQUIRE(luil::win32::acp_from_utf8(latin, 2) == 2);

    // 한글은 UTF-8 3바이트 · UTF-16 1단위다.
    constexpr std::u8string_view hangul { u8"한글" };
    REQUIRE(hangul.size() == 6u);
    REQUIRE(luil::win32::acp_length(hangul) == 2);
    REQUIRE(luil::win32::acp_from_utf8(hangul, 3) == 1);

    // 이모지는 UTF-8 4바이트 · UTF-16 2단위(surrogate 쌍)다.
    constexpr std::u8string_view emoji { u8"a😀b" };
    REQUIRE(emoji.size() == 6u);
    REQUIRE(luil::win32::acp_length(emoji) == 4);
    REQUIRE(luil::win32::acp_from_utf8(emoji, 1) == 1);
    REQUIRE(luil::win32::acp_from_utf8(emoji, 5) == 3);
}

TEST_CASE("ACP offsets convert back to UTF-8 byte offsets", "[win32][tsf]")
{
    constexpr std::u8string_view text { u8"a한😀b" };
    // a(1) 한(3) 😀(4) b(1) = 9바이트, ACP는 1 + 1 + 2 + 1 = 5단위다.
    REQUIRE(text.size() == 9u);
    REQUIRE(luil::win32::acp_length(text) == 5);

    for (const long acp : { 0, 1, 2, 4, 5 })
        REQUIRE(luil::win32::acp_from_utf8(text, luil::win32::utf8_from_acp(text, acp)) == acp);

    REQUIRE(luil::win32::utf8_from_acp(text, 0) == 0u);
    REQUIRE(luil::win32::utf8_from_acp(text, 1) == 1u);
    REQUIRE(luil::win32::utf8_from_acp(text, 2) == 4u);
    REQUIRE(luil::win32::utf8_from_acp(text, 4) == 8u);
    REQUIRE(luil::win32::utf8_from_acp(text, 5) == 9u);
}

TEST_CASE("ACP inside a surrogate pair falls back to the character start", "[win32][tsf]")
{
    constexpr std::u8string_view text { u8"a😀b" };
    // ACP 2는 이모지의 뒤쪽 surrogate다.
    // 그 자리를 byte offset으로 옮기면 4바이트 문자의 한가운데가 되어 글이 깨진다.
    // 문자의 시작으로 되돌려야 한다.
    REQUIRE(luil::win32::utf8_from_acp(text, 2) == 1u);
    REQUIRE(luil::win32::utf8_from_acp(text, 3) == 5u);
}

TEST_CASE("ACP conversions clamp instead of running off the end", "[win32][tsf]")
{
    constexpr std::u8string_view text { u8"ab" };
    REQUIRE(luil::win32::acp_from_utf8(text, 99) == 2);
    REQUIRE(luil::win32::utf8_from_acp(text, 99) == 2u);
    REQUIRE(luil::win32::utf8_from_acp(text, -1) == 0u);
    REQUIRE(luil::win32::acp_length({}) == 0);
    REQUIRE(luil::win32::utf8_from_acp({}, 3) == 0u);
}

namespace {
    // TSF가 묻는 것에 고정된 답을 주는 최소 host다.
    // 실제 배선은 `application_window`가 한다.
    class stub_host final : public luil::win32::tsf_host
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> focused_text_target() const override
        {
            return focused;
        }

        [[nodiscard]] luil::win32::shadow_document committed_document() const override
        {
            return document;
        }

        [[nodiscard]] std::optional<RECT> text_screen_rect(const luil::win32::shadow_document&, std::size_t, std::size_t) const override
        {
            return RECT { 0, 0, 100, 20 };
        }

        void post_composition(luil::text_composition_event intent) override
        {
            compositions.push_back(std::move(intent));
        }

        void post_edit(luil::text_edit_request intent) override
        {
            edits.push_back(std::move(intent));
        }

        std::optional<luil::text_input_target> focused {};
        luil::win32::shadow_document document {};
        std::vector<luil::text_composition_event> compositions {};
        std::vector<luil::text_edit_request> edits {};
    };

    // COM STA를 이 test 동안만 잡는다.
    // 앱에서는 `main.cpp`가 잡는다.
    class com_scope
    {
    public:
        com_scope() noexcept
            : result_ { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) }
        {}

        com_scope(const com_scope&) = delete;
        com_scope(com_scope&&) = delete;
        com_scope& operator=(const com_scope&) = delete;
        com_scope& operator=(com_scope&&) = delete;

        ~com_scope()
        {
            if (SUCCEEDED(result_))
                CoUninitialize();
        }

        [[nodiscard]] bool ready() const noexcept
        {
            return SUCCEEDED(result_);
        }

    private:
        HRESULT result_ { S_OK };
    };

    // sink 계약 검증용 최소 sink다. 어떤 알림이 왔는지만 센다.
    // test scope 밖으로 참조가 남지 않으므로 스스로 지우지 않는다.
    class counting_sink final : public ITextStoreACPSink
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (ppv == nullptr)
                return E_INVALIDARG;
            *ppv = nullptr;
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextStoreACPSink))
                *ppv = static_cast<ITextStoreACPSink*>(this);
            else
                return E_NOINTERFACE;
            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++references;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            return --references;
        }

        HRESULT STDMETHODCALLTYPE OnTextChange(DWORD, const TS_TEXTCHANGE*) override
        {
            ++text_changes;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnSelectionChange() override
        {
            ++selection_changes;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode, TsViewCookie) override
        {
            ++layout_changes;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG, LONG, ULONG, const TS_ATTRID*) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnStartEditTransaction() override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnEndEditTransaction() override
        {
            return S_OK;
        }

        ULONG references { 0 };
        int text_changes { 0 };
        int selection_changes { 0 };
        int layout_changes { 0 };
    };

    // text store의 COM 참조를 test 동안만 잡는다.
    struct store_release
    {
        void operator()(ITextStoreACP2* const store) const noexcept
        {
            if (store != nullptr)
                store->Release();
        }
    };
    using store_pointer = std::unique_ptr<ITextStoreACP2, store_release>;
} // namespace

// TSF 연결이 실제로 서는지다.
// ITfThreadMgr2 활성화 · document manager · context · text
// store 한 벌이 모두 성립해야 조합이 박스 안에서 이루어진다.
TEST_CASE("A TSF session activates and tears down cleanly", "[win32][tsf]")
{
    const com_scope com {};
    if (com.ready() == false)
        SKIP("COM apartment initialization failed.");

    stub_host host {};
    const std::unique_ptr<luil::win32::tsf_input_session> session {
        luil::win32::tsf_input_session::create(nullptr, host),
    };
    if (session == nullptr)
        SKIP("Text Services Framework is not available on this machine.");

    REQUIRE(session->composing() == false);

    // 초점이 없으면 아무 일도 하지 않는다.
    session->synchronize();
    REQUIRE(host.compositions.empty());
    REQUIRE(host.edits.empty());

    // 초점이 들어오면 그림자를 채우고 TIP에 문서를 넘긴다.
    // 조합이 없으므로 logic으로 나가는 것은 아직 없다.
    host.focused = luil::text_input_target { 1 };
    host.document = { u8"main", 4u, 4u };
    session->synchronize();
    REQUIRE(session->composing() == false);
    REQUIRE(session->owns_tsf_focus());
    REQUIRE(host.edits.empty());

    SECTION("Window focus loss detaches and does not restore a cleared logical target")
    {
        // HWND focus 상실은 session target과 TSF focus를 즉시 떼고, 뒤따르는
        // surface focus event가 app의 논리 text focus도 거둔다.
        session->set_window_focused(false);
        REQUIRE(session->owns_tsf_focus() == false);

        host.focused.reset();
        session->set_window_focused(true);
        REQUIRE(session->owns_tsf_focus() == false);
        REQUIRE(session->composing() == false);
        REQUIRE(host.edits.empty());
        REQUIRE(host.compositions.empty());
    }
}

// `ITextStoreACP2` 계약: sink는 하나만 살고, 같은 COM identity의
// 재등록만 mask 갱신으로 허용한다.
TEST_CASE("A text store enforces the sink identity contract", "[win32][tsf]")
{
    stub_host host {};
    counting_sink sink {};
    const store_pointer store { luil::win32::create_text_store_for_test(host) };

    REQUIRE(store->AdviseSink(IID_ITextStoreACPSink, &sink, TS_AS_ALL_SINKS) == S_OK);

    SECTION("Re-advising the same sink only updates the mask")
    {
        REQUIRE(store->AdviseSink(IID_ITextStoreACPSink, &sink, TS_AS_SEL_CHANGE) == S_OK);
        luil::win32::reset_text_store_document_for_test(*store, luil::text_input_target { 1 }, { u8"a", 1u, 1u });
        REQUIRE(sink.text_changes == 0);
        REQUIRE(sink.selection_changes == 1);
        REQUIRE(sink.layout_changes == 0);
    }

    SECTION("A different sink is rejected while one is registered")
    {
        counting_sink other {};
        REQUIRE(store->AdviseSink(IID_ITextStoreACPSink, &other, TS_AS_ALL_SINKS) == CONNECT_E_ADVISELIMIT);
        REQUIRE(other.references == 0u);

        // 기존 sink의 연결은 그대로다.
        luil::win32::reset_text_store_document_for_test(*store, luil::text_input_target { 1 }, { u8"a", 1u, 1u });
        REQUIRE(sink.text_changes == 1);
        REQUIRE(other.text_changes == 0);
    }

    SECTION("Only the registered sink can unadvise itself")
    {
        counting_sink other {};
        REQUIRE(store->UnadviseSink(&other) == CONNECT_E_NOCONNECTION);

        REQUIRE(store->UnadviseSink(&sink) == S_OK);
        REQUIRE(sink.references == 0u);
        // 이미 떼어낸 뒤에는 등록된 sink가 없다.
        REQUIRE(store->UnadviseSink(&sink) == CONNECT_E_NOCONNECTION);
    }
}

// sink가 mask로 청약한 알림만 받아야 한다.
TEST_CASE("A text store only sends notifications the sink subscribed to", "[win32][tsf]")
{
    stub_host host {};
    counting_sink sink {};
    const store_pointer store { luil::win32::create_text_store_for_test(host) };

    REQUIRE(store->AdviseSink(IID_ITextStoreACPSink, &sink, TS_AS_TEXT_CHANGE | TS_AS_LAYOUT_CHANGE) == S_OK);
    luil::win32::reset_text_store_document_for_test(*store, luil::text_input_target { 1 }, { u8"main", 4u, 4u });
    REQUIRE(sink.text_changes == 1);
    REQUIRE(sink.selection_changes == 0);
    REQUIRE(sink.layout_changes == 1);

    // 재등록으로 바꾼 mask는 다음 알림부터 적용된다.
    REQUIRE(store->AdviseSink(IID_ITextStoreACPSink, &sink, TS_AS_SEL_CHANGE) == S_OK);
    luil::win32::reset_text_store_document_for_test(*store, luil::text_input_target { 1 }, { u8"other", 5u, 5u });
    REQUIRE(sink.text_changes == 1);
    REQUIRE(sink.selection_changes == 1);
    REQUIRE(sink.layout_changes == 1);
}

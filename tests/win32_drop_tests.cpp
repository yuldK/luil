#include "win32/win32_drop.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "win32/window_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <shlobj.h>
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
    // 셸이 만드는 것과 같은 모양의 DROPFILES 블록이다.
    // DragQueryFileW는 이 메모리 구조만 보므로 실제 드롭 없이도 읽을 수 있다.
    [[nodiscard]] HGLOBAL make_drop_block(const std::vector<std::wstring>& paths)
    {
        // 경로들 + 각 NUL + 마지막 이중 NUL이다.
        std::size_t characters { 1 };
        for (const std::wstring& path : paths)
            characters += path.size() + 1;
        const HGLOBAL storage { GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + characters * sizeof(wchar_t)) };
        REQUIRE(storage != nullptr);

        auto* const block { static_cast<DROPFILES*>(GlobalLock(storage)) };
        REQUIRE(block != nullptr);
        *block = DROPFILES {};
        block->pFiles = sizeof(DROPFILES);
        block->fWide = TRUE;
        auto* cursor { reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(block) + sizeof(DROPFILES)) };
        for (const std::wstring& path : paths)
        {
            std::memcpy(cursor, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
            cursor += path.size() + 1;
        }
        *cursor = L'\0';
        GlobalUnlock(storage);
        return storage;
    }

    struct drop_handle
    {
        HGLOBAL storage { nullptr };

        explicit drop_handle(const std::vector<std::wstring>& paths)
            : storage { make_drop_block(paths) }
        {}

        drop_handle(const drop_handle&) = delete;
        drop_handle(drop_handle&&) = delete;
        drop_handle& operator=(const drop_handle&) = delete;
        drop_handle& operator=(drop_handle&&) = delete;

        ~drop_handle()
        {
            GlobalFree(storage);
        }

        [[nodiscard]] HDROP get() const noexcept
        {
            return static_cast<HDROP>(storage);
        }
    };

    // CF_HDROP 하나만 답하는 최소 IDataObject다.
    // GetData마다 새 블록을 지어 준다 — 받는 쪽(ReleaseStgMedium)이 해제한다.
    // test 스택이 수명을 쥐므로 참조 수는 형식만 갖춘다.
    class file_data_object final : public IDataObject
    {
    public:
        explicit file_data_object(std::vector<std::wstring> paths)
            : paths_ { std::move(paths) }
        {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
        {
            if (object == nullptr)
                return E_POINTER;
            if (riid == IID_IUnknown || riid == IID_IDataObject)
            {
                *object = static_cast<IDataObject*>(this);
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return 2;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            return 1;
        }

        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
        {
            if (format == nullptr || medium == nullptr)
                return E_POINTER;
            if (format->cfFormat != CF_HDROP || (format->tymed & TYMED_HGLOBAL) == 0)
                return DV_E_FORMATETC;
            *medium = STGMEDIUM {};
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = make_drop_block(paths_);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
        {
            return E_NOTIMPL;
        }

    private:
        std::vector<std::wstring> paths_ {};
    };

    // drop_target_host의 기록자다.
    struct drop_event_record
    {
        std::u8string kind {};
        float x { 0.0f };
        float y { 0.0f };
        std::vector<std::u8string> files {};
    };

    class recording_drop_host final : public luil::win32::drop_target_host
    {
    public:
        std::vector<drop_event_record> events {};

        void file_drag_entered(const float x, const float y, std::vector<std::u8string> files) override
        {
            events.push_back({ u8"entered", x, y, std::move(files) });
        }

        void file_drag_moved(const float x, const float y) override
        {
            events.push_back({ u8"moved", x, y, {} });
        }

        void file_drag_left() override
        {
            events.push_back({ u8"left", 0.0f, 0.0f, {} });
        }

        void file_drag_dropped(const float x, const float y, const std::vector<std::u8string>& files) override
        {
            events.push_back({ u8"dropped", x, y, files });
        }
    };

    // 표면 실행 test의 최소 문맥이다.
    // 배분된 액션과 물러섬 호출만 기록한다.
    class fake_surface_context final : public luil::win32::surface_context
    {
    public:
        std::vector<luil::input_action> dispatched {};
        std::vector<std::vector<std::u8string>> fallbacks {};

        [[nodiscard]] luil::win32::app_host* host() noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] const luil::win32::window_config& config() const noexcept override
        {
            return config_;
        }

        [[nodiscard]] luil::interaction_policy* policy() noexcept override
        {
            return nullptr;
        }

        // 창 없는 test는 합성하지 않는다 — Direct3D 렌더러를 세우지 않으므로
        // device를 물을 일이 없다.
        [[nodiscard]] IDCompositionDevice* composition_device(std::u8string&) noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] sk_sp<SkTypeface> apply_frame_appearance(luil::frame_state&, const luil::win32::ui_frame*) override
        {
            return nullptr;
        }

        bool dismiss_popups(luil::win32::popup_dismiss_reason) override
        {
            return false;
        }

        void surface_moved(const std::u8string&) override
        {}

        void dispatch_action(luil::input_action action) override
        {
            dispatched.push_back(std::move(action));
        }

        void deliver_file_drop(const std::vector<std::u8string>& paths) override
        {
            fallbacks.push_back(paths);
        }

        void schedule_update_repaint(const std::u8string&, const luil::interaction_snapshot&) noexcept override
        {}

        void surface_focus_lost(const std::u8string&) override
        {}

        void surface_focus_gained(const std::u8string&) override
        {}

        [[nodiscard]] const luil::win32::window_surface* anchored_popup(const std::u8string&, const std::u8string&) const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] bool pointer_capture_active() const noexcept override
        {
            return false;
        }

        void set_pointer_capture_active(bool) noexcept override
        {}

        void report_error(const std::u8string&) noexcept override
        {}

    private:
        luil::win32::window_config config_ {};
    };

    // 배치와 그리기만 갖춘 test element다.
    class drop_panel final : public luil::ui_element
    {
    public:
        using luil::ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    struct drop_intent
    {
        std::u8string path {};
    };
} // namespace

TEST_CASE("Dropped file paths read every path in order", "[win32][drop]")
{
    // 두 번째 경로의 한글이 UTF-8로 그대로 넘어와야 한다.
    const drop_handle drop { { L"C:\\a\\first.txt", L"C:\\b\\두번째.png" } };
    const std::vector<std::u8string> paths { luil::win32::dropped_file_paths(drop.get()) };
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == u8"C:\\a\\first.txt");
    REQUIRE(paths[1] == u8"C:\\b\\두번째.png");
}

TEST_CASE("An empty drop block yields no paths", "[win32][drop]")
{
    const drop_handle drop { std::vector<std::wstring> {} };
    REQUIRE(luil::win32::dropped_file_paths(drop.get()).empty());
}

TEST_CASE("A surface drop target relays OLE calls to its host", "[win32][drop]")
{
    // 창 없이 만든다 — 화면·client 변환이 빠질 뿐 전달 경로는 같다.
    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    file_data_object data { { L"C:\\a\\first.txt" } };

    // 들어오는 effect는 source가 허락한 집합이다 — 탐색기처럼 셋 다 허락한다.
    DWORD effect { DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 10, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.size() == 1u);
    REQUIRE(host.events[0].kind == u8"entered");
    REQUIRE(host.events[0].files == std::vector<std::u8string> { u8"C:\\a\\first.txt" });

    effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    REQUIRE(target->DragOver(0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.size() == 2u);
    REQUIRE(host.events[1].kind == u8"moved");

    // DragOver에는 IDataObject가 오지 않는다 — 들어올 때 읽은 목록이 놓기까지 실린다.
    effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    REQUIRE(target->Drop(&data, 0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.size() == 3u);
    REQUIRE(host.events[2].kind == u8"dropped");
    REQUIRE(host.events[2].files == std::vector<std::u8string> { u8"C:\\a\\first.txt" });
    target->Release();
}

TEST_CASE("A drag without files answers none and stays silent", "[win32][drop]")
{
    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    file_data_object data { std::vector<std::wstring> {} };

    // source가 복사를 허락해도 실린 것이 없으면 답할 것이 없다.
    DWORD effect { DROPEFFECT_COPY };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 1, 2 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);
    effect = DROPEFFECT_COPY;
    REQUIRE(target->DragOver(0, POINTL { 3, 4 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);
    REQUIRE(target->DragLeave() == S_OK);
    // 파일이 아니면 표면은 아무것도 듣지 않는다.
    REQUIRE(host.events.empty());
    target->Release();
}

TEST_CASE("A drag whose source forbids copy is refused", "[win32][drop]")
{
    // MOVE를 답하면 source(탐색기)는 우리가 파일을 옮겼다고 믿고 원본을 지운다.
    // 우리는 경로만 읽으므로 그것은 실제 파일 손실이다 — 허락 집합에 COPY가
    // 없으면 파일이 실려 있어도 받지 않는다.
    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    file_data_object data { { L"C:\\a\\first.txt" } };

    DWORD effect { DROPEFFECT_MOVE | DROPEFFECT_LINK };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 10, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);

    effect = DROPEFFECT_MOVE | DROPEFFECT_LINK;
    REQUIRE(target->DragOver(0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);

    effect = DROPEFFECT_MOVE | DROPEFFECT_LINK;
    REQUIRE(target->Drop(&data, 0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);

    // 받지 않을 끌기에는 수락 대상 강조도 켜지 않는다.
    // 나가는 것도 마찬가지다 — 들어왔다고 알린 적이 없으니 거둘 것도 없다.
    REQUIRE(target->DragLeave() == S_OK);
    REQUIRE(host.events.empty());
    target->Release();
}

TEST_CASE("A drag that allows only copy is accepted", "[win32][drop]")
{
    // 허락 집합이 COPY 하나뿐이어도 답은 그대로 COPY다.
    // 고른 값이 언제나 허락 집합의 부분이므로 masking으로 깎을 것이 없다.
    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    file_data_object data { { L"C:\\a\\first.txt" } };

    DWORD effect { DROPEFFECT_COPY };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 10, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.size() == 1u);
    REQUIRE(host.events[0].kind == u8"entered");

    effect = DROPEFFECT_COPY;
    REQUIRE(target->Drop(&data, 0, POINTL { 10, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.size() == 2u);
    REQUIRE(host.events[1].kind == u8"dropped");
    target->Release();
}

TEST_CASE("A huge drop block is read up to the cap", "[win32][drop]")
{
    // DragQueryFileW는 색인마다 앞에서부터 훑으므로 전부 읽으면 O(n²)다.
    // 상한이 UI thread의 멈춤을 막는다.
    std::vector<std::wstring> paths {};
    paths.reserve(luil::win32::max_dropped_paths + 5u);
    for (UINT index = 0; index < luil::win32::max_dropped_paths + 5u; ++index)
        paths.push_back(L"C:\\many\\" + std::to_wstring(index) + L".txt");

    const drop_handle drop { paths };
    const std::vector<std::u8string> loaded { luil::win32::dropped_file_paths(drop.get()) };
    REQUIRE(loaded.size() == luil::win32::max_dropped_paths);
    // 자르는 쪽은 뒤다 — 첫 항목이 그대로 남는다.
    REQUIRE(loaded.front() == u8"C:\\many\\0.txt");
}

TEST_CASE("A dropped file runs the accepting element or falls back", "[win32][drop]")
{
    fake_surface_context context {};
    luil::win32::window_surface surface { context };

    auto root { std::make_unique<drop_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto target { std::make_unique<drop_panel>(luil::ui_element_id { luil::application_element_kind(0), u8"files" }) };
    target->arrange({ { 100.0f, 100.0f, 50.0f, 50.0f }, 1.0f });
    luil::drop_target accepting {};
    accepting.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
    accepting.on_drop = [](const luil::drag_payload& payload, const luil::ui_action_context&) -> std::vector<luil::input_action> {
        return { luil::make_app_action(drop_intent { payload.files.front() }) };
    };
    target->set_drop_target(std::move(accepting));
    root->add(std::move(target));
    static_cast<void>(surface.set_tree(std::make_shared<const luil::ui_tree>(std::move(root))));

    luil::win32::drop_target_host& drops { surface };
    const std::vector<std::u8string> files { u8"C:\\a.txt" };

    // 수락 element 위 — on_drop의 액션이 통상 규칙으로 배분된다.
    drops.file_drag_dropped(120.0f, 120.0f, files);
    REQUIRE(context.dispatched.size() == 1u);
    REQUIRE(context.fallbacks.empty());
    const auto* const message { std::get_if<luil::app_message>(&context.dispatched[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<drop_intent>() != nullptr);
    REQUIRE(message->get<drop_intent>()->path == u8"C:\\a.txt");

    // 수락 element 밖 — delegate 물러섬이다.
    drops.file_drag_dropped(10.0f, 10.0f, files);
    REQUIRE(context.dispatched.size() == 1u);
    REQUIRE(context.fallbacks.size() == 1u);
    REQUIRE(context.fallbacks[0] == files);
}

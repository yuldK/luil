#include "win32/win32_drop.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/drop_data.h"
#include "luil/win32/win32_window.h"
#include "win32/window_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <shlobj.h>
#include <windows.h>

#include <array>
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
        {
        }

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
        {
        }

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
        std::vector<std::u8string> formats {};
        // 놓기에 넘어온 데이터 객체다.
        IDataObject* data { nullptr };
    };

    class recording_drop_host final : public luil::win32::drop_target_host
    {
    public:
        std::vector<drop_event_record> events {};
        // 거짓이면 그 자리를 불투명 표면이 가린 것처럼 답한다.
        bool open { true };

        bool file_drag_entered(const float x, const float y, luil::win32::os_drag_data drag) override
        {
            events.push_back({ u8"entered", x, y, std::move(drag.files), std::move(drag.formats) });
            return open;
        }

        bool file_drag_moved(const float x, const float y) override
        {
            events.push_back({ u8"moved", x, y, {} });
            return open;
        }

        void file_drag_left() override
        {
            events.push_back({ u8"left", 0.0f, 0.0f, {} });
        }

        bool file_drag_dropped(const float x, const float y, const luil::win32::os_drag_data& drag, IDataObject* const data, const DWORD) override
        {
            events.push_back({ u8"dropped", x, y, drag.files, drag.formats, data });
            return open;
        }
    };

    // 표면 실행 test의 최소 문맥이다.
    // 배분된 액션과 물러섬 호출만 기록한다.
    class fake_surface_context final : public luil::win32::surface_context
    {
    public:
        std::vector<luil::input_action> dispatched {};
        std::vector<std::vector<std::u8string>> fallbacks {};

        [[nodiscard]] luil::app_host* host() noexcept override
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

        // 창 없는 test는 웹뷰를 세우지 않는다.
        [[nodiscard]] std::span<const luil::pixel_rect> apply_webviews(
            const std::u8string&, const luil::ui_tree*, IDCompositionVisual*, int, int) override
        {
            return {};
        }

        // 창 없는 test는 웹뷰를 세우지 않는다.
        [[nodiscard]] bool relay_webview_pointer(const std::u8string&, UINT, WPARAM, int, int) override
        {
            return false;
        }

        void webview_pointer_left(const std::u8string&) override
        {
        }
        void cancel_webview_pointer(const std::u8string&) override
        {
        }

        [[nodiscard]] sk_sp<SkTypeface> apply_frame_appearance(luil::frame_state&, const luil::ui_frame*) override
        {
            return nullptr;
        }

        bool dismiss_popups(luil::popup_dismiss_reason) override
        {
            return false;
        }

        void surface_moved(const std::u8string&) override
        {
        }

        void dispatch_action(luil::input_action action) override
        {
            dispatched.push_back(std::move(action));
        }

        void deliver_file_drop(const std::vector<std::u8string>& paths) override
        {
            fallbacks.push_back(paths);
        }

        void schedule_update_repaint(const std::u8string&, const luil::interaction_snapshot&) noexcept override
        {
        }

        void surface_focus_lost(const std::u8string&) override
        {
        }

        void surface_focus_gained(const std::u8string&) override
        {
        }

        [[nodiscard]] const luil::win32::window_surface* anchored_popup(const std::u8string&, const std::u8string&) const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] bool pointer_capture_active() const noexcept override
        {
            return false;
        }

        void set_pointer_capture_active(bool) noexcept override
        {
        }

        void report_error(const std::u8string&) noexcept override
        {
        }

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
        {
        }
    };

    struct drop_intent
    {
        std::u8string path {};
    };

    // 놓는 순간의 수식 키다.
    struct drop_keys_intent
    {
        bool control { false };
        bool shift { false };
        bool alt { false };
    };
}

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

TEST_CASE("A covered spot answers none while the drag stays tracked", "[win32][drop]")
{
    // 불투명 표면이 가린 자리에서 COPY를 보이면 커서는 "받음"인데 놓아도 아무 일이 없다.
    recording_drop_host host {};
    host.open = false;
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    file_data_object data { { L"C:\\a\\first.txt" } };

    DWORD effect { DROPEFFECT_COPY };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 10, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);
    effect = DROPEFFECT_COPY;
    REQUIRE(target->DragOver(0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);

    // 가린 것 밖으로 옮기면 같은 끌기를 받는다 — 목록은 들어올 때 쥔 그대로다.
    host.open = true;
    effect = DROPEFFECT_COPY;
    REQUIRE(target->DragOver(0, POINTL { 50, 60 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);

    // 가린 자리에 놓으면 아무도 받지 않았다고 답한다.
    host.open = false;
    effect = DROPEFFECT_COPY;
    REQUIRE(target->Drop(&data, 0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);
    REQUIRE(host.events.back().kind == u8"dropped");
    REQUIRE(host.events.back().files == std::vector<std::u8string> { u8"C:\\a\\first.txt" });
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
    REQUIRE(drops.file_drag_dropped(120.0f, 120.0f, luil::win32::os_drag_data { files, { u8"CF_HDROP" } }, nullptr, 0));
    REQUIRE(context.dispatched.size() == 1u);
    REQUIRE(context.fallbacks.empty());
    const auto* const message { std::get_if<luil::app_message>(&context.dispatched[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<drop_intent>() != nullptr);
    REQUIRE(message->get<drop_intent>()->path == u8"C:\\a.txt");

    // 수락 element 밖 — delegate 물러섬이다.
    REQUIRE(drops.file_drag_dropped(10.0f, 10.0f, luil::win32::os_drag_data { files, { u8"CF_HDROP" } }, nullptr, 0));
    REQUIRE(context.dispatched.size() == 1u);
    REQUIRE(context.fallbacks.size() == 1u);
    REQUIRE(context.fallbacks[0] == files);
}

TEST_CASE("A file dropped from another program carries the key state OLE reports", "[win32][drop]")
{
    // 끌어오는 동안 키보드 입력은 끄는 쪽 프로그램에 있다. 이 thread의 키 상태가 아니라 `Drop`의 키 상태가 답이다.
    fake_surface_context context {};
    luil::win32::window_surface surface { context };
    auto root { std::make_unique<drop_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto target { std::make_unique<drop_panel>(luil::ui_element_id { luil::application_element_kind(0), u8"files" }) };
    target->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    luil::drop_target accepting {};
    accepting.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
    accepting.on_drop = [](const luil::drag_payload&, const luil::ui_action_context& keys) -> std::vector<luil::input_action> {
        return { luil::make_app_action(drop_keys_intent { keys.control, keys.shift, keys.alt }) };
    };
    target->set_drop_target(std::move(accepting));
    root->add(std::move(target));
    static_cast<void>(surface.set_tree(std::make_shared<const luil::ui_tree>(std::move(root))));

    luil::win32::drop_target_host& drops { surface };
    const auto keys_of = [&context](const std::size_t index) {
        const auto* const message { std::get_if<luil::app_message>(&context.dispatched.at(index)) };
        REQUIRE(message != nullptr);
        REQUIRE(message->get<drop_keys_intent>() != nullptr);
        return *message->get<drop_keys_intent>();
    };
    REQUIRE(drops.file_drag_dropped(20.0f, 20.0f, luil::win32::os_drag_data { { u8"C:\\a.txt" }, { u8"CF_HDROP" } }, nullptr, MK_LBUTTON | MK_CONTROL));
    REQUIRE(keys_of(0).control);
    REQUIRE(keys_of(0).shift == false);
    REQUIRE(keys_of(0).alt == false);
    REQUIRE(drops.file_drag_dropped(20.0f, 20.0f, luil::win32::os_drag_data { { u8"C:\\a.txt" }, { u8"CF_HDROP" } }, nullptr, MK_LBUTTON | MK_SHIFT | MK_ALT));
    REQUIRE(keys_of(1).control == false);
    REQUIRE(keys_of(1).shift);
    REQUIRE(keys_of(1).alt);
}

TEST_CASE("An opaque overlay prevents a file drop from reaching the window fallback", "[win32][drop]")
{
    fake_surface_context context {};
    luil::win32::window_surface surface { context };
    auto root { std::make_unique<drop_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

    auto target { std::make_unique<drop_panel>(luil::ui_element_id { luil::application_element_kind(0), u8"files" }) };
    target->arrange({ { 100.0f, 100.0f, 50.0f, 50.0f }, 1.0f });
    luil::drop_target accepting {};
    accepting.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
    accepting.on_drop
        = [](const luil::drag_payload& payload, const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(drop_intent { payload.files.front() }) }; };
    target->set_drop_target(std::move(accepting));
    root->add(std::move(target));

    auto overlay { std::make_unique<drop_panel>(luil::ui_element_id { luil::ui_element_kind::modal_scrim }) };
    overlay->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    overlay->set_hit_opaque(true);
    root->add(std::move(overlay));
    static_cast<void>(surface.set_tree(std::make_shared<const luil::ui_tree>(std::move(root))));

    luil::win32::drop_target_host& drops { surface };
    // 끄는 동안에도 같은 답이라 커서가 "받음"을 보이지 않는다.
    REQUIRE(drops.file_drag_entered(120.0f, 120.0f, luil::win32::os_drag_data { { u8"C:\\a.txt" }, { u8"CF_HDROP" } }) == false);
    REQUIRE(drops.file_drag_moved(10.0f, 10.0f) == false);
    REQUIRE(drops.file_drag_dropped(120.0f, 120.0f, luil::win32::os_drag_data { { u8"C:\\a.txt" }, { u8"CF_HDROP" } }, nullptr, 0) == false);
    REQUIRE(context.dispatched.empty());
    REQUIRE(context.fallbacks.empty());
}

namespace {
    // 파일 없이 형식만 싣는 source다 — 브라우저가 그림을 끌 때처럼 가상 파일(`FileGroupDescriptorW` + `FileContents`)과
    // URL만 준다. 형식 열거는 셸의 표준 열거자에 맡긴다.
    class virtual_data_object final : public IDataObject
    {
    public:
        struct virtual_item
        {
            std::wstring name {};
            std::string content {};
        };

        explicit virtual_data_object(std::vector<virtual_item> items, std::wstring url)
            : items_ { std::move(items) }
            , url_ { std::move(url) }
            , descriptor_format_ { static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)) }
            , contents_format_ { static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILECONTENTS)) }
            , url_format_ { static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_INETURLW)) }
        {
        }

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
            *medium = STGMEDIUM {};
            if (format->cfFormat == descriptor_format_ && (format->tymed & TYMED_HGLOBAL) != 0)
            {
                const std::size_t size { sizeof(FILEGROUPDESCRIPTORW) + (items_.empty() ? 0 : items_.size() - 1) * sizeof(FILEDESCRIPTORW) };
                const HGLOBAL global { GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size) };
                auto* const group { static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(global)) };
                group->cItems = static_cast<UINT>(items_.size());
                for (std::size_t index { 0 }; index < items_.size(); ++index)
                {
                    FILEDESCRIPTORW& descriptor { group->fgd[index] };
                    descriptor.dwFlags = FD_FILESIZE;
                    descriptor.nFileSizeLow = static_cast<DWORD>(items_[index].content.size());
                    wcsncpy_s(descriptor.cFileName, items_[index].name.c_str(), _TRUNCATE);
                }
                GlobalUnlock(global);
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = global;
                return S_OK;
            }
            if (format->cfFormat == contents_format_ && (format->tymed & TYMED_ISTREAM) != 0)
            {
                if (format->lindex < 0 || static_cast<std::size_t>(format->lindex) >= items_.size())
                    return DV_E_LINDEX;
                const std::string& content { items_[static_cast<std::size_t>(format->lindex)].content };
                medium->tymed = TYMED_ISTREAM;
                const HGLOBAL global { GlobalAlloc(GMEM_MOVEABLE, content.empty() ? 1 : content.size()) };
                if (content.empty() == false)
                    std::memcpy(GlobalLock(global), content.data(), content.size());
                GlobalUnlock(global);
                // 흐름이 HGLOBAL을 쥐고 풀 때 함께 놓는다. 크기는 내용만큼으로 맞춘다 (할당 단위로 커진 꼬리를 읽지 않게).
                if (FAILED(CreateStreamOnHGlobal(global, TRUE, &medium->pstm)))
                    return E_OUTOFMEMORY;
                ULARGE_INTEGER size {};
                size.QuadPart = content.size();
                static_cast<void>(medium->pstm->SetSize(size));
                // 다 읽은 자리에 둔 흐름을 준다 — 받는 쪽이 앞으로 되감는지 본다.
                LARGE_INTEGER end {};
                static_cast<void>(medium->pstm->Seek(end, STREAM_SEEK_END, nullptr));
                return S_OK;
            }
            if (format->cfFormat == url_format_ && (format->tymed & TYMED_HGLOBAL) != 0)
            {
                // HGLOBAL은 할당 단위로 커질 수 있어 NUL 뒤에도 자리가 남는다.
                const std::size_t size { (url_.size() + 8) * sizeof(wchar_t) };
                const HGLOBAL global { GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size) };
                std::memcpy(GlobalLock(global), url_.c_str(), url_.size() * sizeof(wchar_t));
                GlobalUnlock(global);
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = global;
                return S_OK;
            }
            return DV_E_FORMATETC;
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

        HRESULT STDMETHODCALLTYPE EnumFormatEtc(const DWORD direction, IEnumFORMATETC** enumerator) override
        {
            if (direction != DATADIR_GET)
                return E_NOTIMPL;
            // 같은 형식을 매체마다 거듭 알리는 source가 있다 — 목록에는 한 번만 실려야 한다.
            const std::array<FORMATETC, 4> formats {
                FORMATETC { descriptor_format_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
                FORMATETC { contents_format_, nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM },
                FORMATETC { contents_format_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
                FORMATETC { url_format_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            };
            return SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), enumerator);
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
        std::vector<virtual_item> items_ {};
        std::wstring url_ {};
        CLIPFORMAT descriptor_format_ { 0 };
        CLIPFORMAT contents_format_ { 0 };
        CLIPFORMAT url_format_ { 0 };
    };

    // 놓는 순간 꺼낸 가상 파일이다.
    struct virtual_drop_intent
    {
        std::vector<std::u8string> names {};
        std::string first_content {};
        std::u8string url {};
        bool had_data { false };
    };
}

TEST_CASE("Format names round-trip between standard constants and registered names", "[win32][drop]")
{
    REQUIRE(luil::win32::clipboard_format_name(CF_HDROP) == u8"CF_HDROP");
    REQUIRE(luil::win32::clipboard_format_name(CF_UNICODETEXT) == u8"CF_UNICODETEXT");
    REQUIRE(luil::win32::clipboard_format_of(u8"CF_DIB") == CF_DIB);
    const CLIPFORMAT registered { luil::win32::clipboard_format_of(u8"FileGroupDescriptorW") };
    REQUIRE(registered == RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    REQUIRE(luil::win32::clipboard_format_name(registered) == u8"FileGroupDescriptorW");
    REQUIRE(luil::win32::clipboard_format_of(u8"") == 0);
}

TEST_CASE("A drag without files lists each format once in the order the source gave", "[win32][drop]")
{
    virtual_data_object data { { { L"photo.png", "PNG" } }, L"https://example.com/photo.png" };
    const luil::win32::os_drag_data drag { luil::win32::read_os_drag(&data) };
    REQUIRE(drag.files.empty());
    REQUIRE(drag.formats == std::vector<std::u8string> { u8"FileGroupDescriptorW", u8"FileContents", u8"UniformResourceLocatorW" });
}

TEST_CASE("A virtual file drag reaches the host with its formats and the Drop data object", "[win32][drop]")
{
    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    virtual_data_object data { { { L"photo.png", "PNG" } }, L"https://example.com/photo.png" };

    // 브라우저는 복사와 링크를 허락한다. 우리는 그 안에서 복사만 고른다.
    DWORD effect { DROPEFFECT_COPY | DROPEFFECT_LINK };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 10, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.size() == 1u);
    REQUIRE(host.events[0].files.empty());
    REQUIRE(host.events[0].formats.size() == 3u);

    // 그 자리에 받는 element가 없으면 host가 거절하고 커서는 "받지 않음"이다.
    host.open = false;
    effect = DROPEFFECT_COPY | DROPEFFECT_LINK;
    REQUIRE(target->DragOver(0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);

    host.open = true;
    effect = DROPEFFECT_COPY | DROPEFFECT_LINK;
    REQUIRE(target->Drop(&data, 0, POINTL { 30, 40 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.back().kind == u8"dropped");
    REQUIRE(host.events.back().data == &data);
    REQUIRE(host.events.back().formats.size() == 3u);
    target->Release();
}

TEST_CASE("A virtual file drag whose source allows only link is refused", "[win32][drop]")
{
    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { nullptr, host } };
    virtual_data_object data { { { L"photo.png", "PNG" } }, L"https://example.com/photo.png" };
    DWORD effect { DROPEFFECT_LINK };
    REQUIRE(target->DragEnter(&data, 0, POINTL { 1, 2 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);
    REQUIRE(target->DragLeave() == S_OK);
    REQUIRE(host.events.empty());
    target->Release();
}

TEST_CASE("A drag without files is taken only where an element accepts its formats", "[win32][drop]")
{
    fake_surface_context context {};
    luil::win32::window_surface surface { context };
    auto root { std::make_unique<drop_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto target { std::make_unique<drop_panel>(luil::ui_element_id { luil::application_element_kind(0), u8"images" }) };
    target->arrange({ { 100.0f, 100.0f, 50.0f, 50.0f }, 1.0f });
    luil::drop_target accepting {};
    // 들어온 순간에는 형식만 본다.
    accepting.accepts = [](const luil::drag_payload& payload) { return payload.has_format(u8"FileGroupDescriptorW"); };
    // 놓는 순간 그 자리에서 내용을 꺼내 메시지에 담는다.
    accepting.on_drop = [](const luil::drag_payload&, const luil::ui_action_context& context) -> std::vector<luil::input_action> {
        virtual_drop_intent intent {};
        intent.had_data = context.native_drop_data != nullptr;
        for (const luil::win32::dropped_virtual_file& file : luil::win32::drop_virtual_files(context.native_drop_data))
            intent.names.push_back(file.name);
        if (const auto bytes { luil::win32::drop_data_bytes(context.native_drop_data, u8"FileContents", 0) }; bytes.has_value())
            intent.first_content.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        intent.url = luil::win32::drop_data_text(context.native_drop_data, u8"UniformResourceLocatorW").value_or(std::u8string {});
        return { luil::make_app_action(std::move(intent)) };
    };
    target->set_drop_target(std::move(accepting));
    root->add(std::move(target));
    static_cast<void>(surface.set_tree(std::make_shared<const luil::ui_tree>(std::move(root))));

    virtual_data_object data { { { L"photo.png", "PNG-BYTES" }, { L"notes.txt", "hello" } }, L"https://example.com/photo.png" };
    const luil::win32::os_drag_data drag { luil::win32::read_os_drag(&data) };
    luil::win32::drop_target_host& drops { surface };

    // 받는 element 밖은 받지 않는다 — 파일 끌기와 달리 창 전체 물러섬이 없어 복사 커서를 보이면 놓아도 아무 일이 없다.
    REQUIRE(drops.file_drag_entered(10.0f, 10.0f, drag) == false);
    REQUIRE(drops.file_drag_moved(120.0f, 120.0f));
    REQUIRE(drops.file_drag_dropped(10.0f, 10.0f, drag, &data, 0) == false);
    REQUIRE(context.dispatched.empty());
    REQUIRE(context.fallbacks.empty());

    // 받는 element 위에 놓으면 그 on_drop이 원시 객체로 내용을 꺼낸다.
    REQUIRE(drops.file_drag_dropped(120.0f, 120.0f, drag, &data, 0));
    REQUIRE(context.dispatched.size() == 1u);
    REQUIRE(context.fallbacks.empty());
    const auto* const message { std::get_if<luil::app_message>(&context.dispatched[0]) };
    REQUIRE(message != nullptr);
    const auto* const intent { message->get<virtual_drop_intent>() };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->had_data);
    REQUIRE(intent->names == std::vector<std::u8string> { u8"photo.png", u8"notes.txt" });
    REQUIRE(intent->first_content == "PNG-BYTES");
    REQUIRE(intent->url == u8"https://example.com/photo.png");
}

TEST_CASE("Drop data helpers refuse content above the limit and missing formats", "[win32][drop]")
{
    virtual_data_object data { { { L"big.bin", std::string(100, 'x') } }, L"https://example.com" };
    REQUIRE(luil::win32::drop_data_bytes(&data, u8"FileContents", 0, 99).has_value() == false);
    REQUIRE(luil::win32::drop_data_bytes(&data, u8"FileContents", 0, 100).value().size() == 100u);
    REQUIRE(luil::win32::drop_data_bytes(&data, u8"FileContents", 5).has_value() == false);
    REQUIRE(luil::win32::drop_data_bytes(&data, u8"CF_DIB").has_value() == false);
    REQUIRE(luil::win32::drop_data_text(nullptr, u8"CF_UNICODETEXT").has_value() == false);
    REQUIRE(luil::win32::drop_virtual_files(nullptr).empty());
    const std::vector<luil::win32::dropped_virtual_file> files { luil::win32::drop_virtual_files(&data) };
    REQUIRE(files.size() == 1u);
    REQUIRE(files[0].size == 100u);
    REQUIRE(files[0].directory == false);
}

TEST_CASE("A drop target on a real window passes every OLE call through the shell drag image", "[win32][drop]")
{
    // 끄는 그림(`IDropTargetHelper`)은 창이 있을 때만 선다. 기존 앱의 파일 드롭마다 지나는 길이라 실제 창으로 네 호출을
    // 모두 지나 보고, 답이 그대로인지 본다.
    const luil::win32::com_sta_scope com {};
    REQUIRE(com.succeeded());
    WNDCLASSEXW type {};
    type.cbSize = sizeof(type);
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"Luil.Test.DropHelper";
    type.lpfnWndProc = &DefWindowProcW;
    REQUIRE((RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS));
    // 보이지 않는 창이다. 사람의 화면을 덮지 않는다.
    const HWND window { CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"", WS_POPUP, 0, 0, 200, 200, nullptr, nullptr, type.hInstance, nullptr) };
    REQUIRE(window != nullptr);

    recording_drop_host host {};
    auto* const target { new luil::win32::surface_drop_target { window, host } };
    file_data_object files { { L"C:\\a\\first.txt" } };
    virtual_data_object virtuals { { { L"photo.png", "PNG" } }, L"https://example.com/photo.png" };

    // 파일 끌기를 들였다가 떠난다.
    DWORD effect { DROPEFFECT_COPY | DROPEFFECT_MOVE };
    REQUIRE(target->DragEnter(&files, 0, POINTL { 10, 10 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    REQUIRE(target->DragOver(0, POINTL { 20, 20 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(target->DragLeave() == S_OK);

    // 가상 파일 끌기를 들였다가 놓는다.
    effect = DROPEFFECT_COPY | DROPEFFECT_LINK;
    REQUIRE(target->DragEnter(&virtuals, 0, POINTL { 10, 10 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    effect = DROPEFFECT_COPY | DROPEFFECT_LINK;
    REQUIRE(target->Drop(&virtuals, 0, POINTL { 30, 30 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_COPY);
    REQUIRE(host.events.back().kind == u8"dropped");
    REQUIRE(host.events.back().data == &virtuals);

    // 받지 않은 끌기도 그림에는 알린다. 짝인 놓기까지 지나도 아무도 받지 않았다고 답한다.
    effect = DROPEFFECT_LINK;
    REQUIRE(target->DragEnter(&virtuals, 0, POINTL { 10, 10 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);
    effect = DROPEFFECT_LINK;
    REQUIRE(target->Drop(&virtuals, 0, POINTL { 10, 10 }, &effect) == S_OK);
    REQUIRE(effect == DROPEFFECT_NONE);

    target->Release();
    DestroyWindow(window);
}

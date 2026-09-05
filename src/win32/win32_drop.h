#pragma once

#include <windows.h>

#include <ole2.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace luil::win32 {
    // 한 번에 읽을 경로의 상한이다.
    // DROPFILES는 색인표 없는 평평한 이중 NUL 목록이라 DragQueryFileW(index)가
    // 부를 때마다 앞에서부터 훑는다 — 전부 읽으면 O(n²)다. 상한이 없으면 수천 개를
    // 끄는 것만으로 DragEnter 한 번이 UI thread를 수십 ms에서 수 초까지 세운다.
    // 소비자(첫 경로 하나를 쓰는 붙여넣기·노트 칸)가 이보다 많이 쓰지 않으므로
    // 앞에서부터 이만큼만 읽고 나머지는 버린다.
    inline constexpr UINT max_dropped_paths { 1024 };

    // HDROP의 경로 목록을 UTF-8로 읽는다.
    // 읽지 못한 항목은 건너뛰고, 목록이 max_dropped_paths보다 길면 앞에서부터
    // 그만큼만 읽는다.
    // handle의 수명은 부르는 쪽의 것이다 — WM_DROPFILES는 DragFinish,
    // 클립보드는 시스템, OLE는 STGMEDIUM이 각자 맡는다.
    [[nodiscard]] std::vector<std::u8string> dropped_file_paths(HDROP drop);

    // IDataObject에 실린 파일 목록이다 (CF_HDROP).
    // 파일이 아니거나 읽지 못하면 빈 목록이다.
    [[nodiscard]] std::vector<std::u8string> dropped_file_paths(IDataObject* data);

    // 표면이 파일 끌기를 받는 창구다.
    // IDropTarget이 OLE의 부름을 이 훅으로 옮긴다 — COM 껍데기와 표면이 서로의
    // 형태를 모른다. 좌표는 그 표면의 client 물리 픽셀이다 (포인터 이벤트와 같은 규약).
    class drop_target_host
    {
    public:
        drop_target_host() = default;
        drop_target_host(const drop_target_host&) = delete;
        drop_target_host(drop_target_host&&) = delete;
        drop_target_host& operator=(const drop_target_host&) = delete;
        drop_target_host& operator=(drop_target_host&&) = delete;
        virtual ~drop_target_host() = default;

        virtual void file_drag_entered(float x, float y, std::vector<std::u8string> files) = 0;
        virtual void file_drag_moved(float x, float y) = 0;
        virtual void file_drag_left() = 0;
        virtual void file_drag_dropped(float x, float y, const std::vector<std::u8string>& files) = 0;
    };

    // 창 하나의 IDropTarget이다.
    // RegisterDragDrop이 요구하는 COM 껍데기일 뿐 판단은 전부 host의 몫이다.
    // 만들기·등록·해제는 표면이 한다. STA 전용이라 참조 수는 원자가 아니다.
    class surface_drop_target final : public IDropTarget
    {
    public:
        surface_drop_target(HWND window, drop_target_host& host) noexcept;
        surface_drop_target(const surface_drop_target&) = delete;
        surface_drop_target(surface_drop_target&&) = delete;
        surface_drop_target& operator=(const surface_drop_target&) = delete;
        surface_drop_target& operator=(surface_drop_target&&) = delete;
        virtual ~surface_drop_target() = default;

        // IUnknown이다.
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

        // IDropTarget이다.
        // 경로 목록은 들어올 때 한 번 읽는다 — DragOver에는 IDataObject가 오지 않는다.
        // QueryGetData("형식이 있는가")로 대신할 수 없다: 경로 목록 자체가 payload
        // 계약이라(os-dragdrop-design.md) 강조도 drop 실행도 그 목록을 요구한다.
        // 대신 max_dropped_paths가 이 한 번의 읽기를 유한하게 묶는다.
        //
        // effect는 [in, out]이다 — 들어오는 값이 source가 허락한 집합이므로
        // 덮어쓰기 전에 읽고 그 안에서만 고른다.
        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD key_state, POINTL point, DWORD* effect) override;
        HRESULT STDMETHODCALLTYPE DragOver(DWORD key_state, POINTL point, DWORD* effect) override;
        HRESULT STDMETHODCALLTYPE DragLeave() override;
        HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD key_state, POINTL point, DWORD* effect) override;

    private:
        [[nodiscard]] POINT client_point(POINTL point) const noexcept;

        HWND window_ { nullptr };
        drop_target_host* host_ { nullptr };
        ULONG references_ { 1 };
        // 받기로 한 끌기의 경로다 — 거절한 끌기는 여기 남기지 않는다.
        // 비어 있지 않다는 것이 곧 "host에게 들어왔다고 알렸다"라서,
        // DragLeave가 짝 없는 알림을 내지 않는다.
        std::vector<std::u8string> files_ {};
    };
} // namespace luil::win32

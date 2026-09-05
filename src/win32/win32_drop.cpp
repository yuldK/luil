#include "win32/win32_drop.h"

#include "win32/utf8.h"

#include <shellapi.h>

#include <cstddef>
#include <utility>

namespace luil::win32 {
    namespace {
        // source가 복사를 허락했을 때만 받는다.
        // MOVE를 답하면 source가 원본을 지운다 — 우리는 경로만 읽으므로 그것은 손실이다.
        // 같은 이유로 grfKeyState(Shift/Ctrl)도 보지 않는다: 사용자의 손가락이
        // 우리가 답할 수 없는 뜻을 고르게 두면 위험만 커진다.
        [[nodiscard]] DWORD accepted_effect(const DWORD allowed, const bool has_files) noexcept
        {
            return (has_files && (allowed & DROPEFFECT_COPY) != 0u) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }

        // 허락 집합은 effect가 실어 온다 — 포인터가 없으면 허락을 알 길이 없어 받지 않는다.
        [[nodiscard]] DWORD allowed_effect(const DWORD* const effect) noexcept
        {
            return effect != nullptr ? *effect : static_cast<DWORD>(DROPEFFECT_NONE);
        }
    } // namespace

    std::vector<std::u8string> dropped_file_paths(const HDROP drop)
    {
        const UINT total { DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0) };
        // 상한을 넘으면 앞에서부터 그만큼만 읽는다 (win32_drop.h의 이유).
        const UINT file_count { total < max_dropped_paths ? total : max_dropped_paths };

        std::vector<std::u8string> paths {};
        paths.reserve(file_count);
        for (UINT index = 0; index < file_count; ++index)
        {
            // 길이를 먼저 물어 버퍼를 잡는다 — 경로는 MAX_PATH보다 길 수 있다.
            const UINT path_length { DragQueryFileW(drop, index, nullptr, 0) };
            if (path_length == 0)
                continue;

            std::wstring path(static_cast<std::size_t>(path_length) + 1, L'\0');
            if (DragQueryFileW(drop, index, path.data(), path_length + 1) == 0)
                continue;
            path.resize(path_length);

            if (auto converted { utf16_to_utf8(path) }; converted.value.has_value())
                paths.push_back(std::move(*converted.value));
        }
        return paths;
    }

    std::vector<std::u8string> dropped_file_paths(IDataObject* const data)
    {
        FORMATETC format { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium {};
        if (data == nullptr || FAILED(data->GetData(&format, &medium)))
            return {};

        std::vector<std::u8string> paths {};
        if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr)
            paths = dropped_file_paths(static_cast<HDROP>(medium.hGlobal));
        ReleaseStgMedium(&medium);
        return paths;
    }

    surface_drop_target::surface_drop_target(const HWND window, drop_target_host& host) noexcept
        : window_ { window }
        , host_ { &host }
    {}

    HRESULT surface_drop_target::QueryInterface(REFIID riid, void** const object)
    {
        if (object == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG surface_drop_target::AddRef()
    {
        return ++references_;
    }

    ULONG surface_drop_target::Release()
    {
        const ULONG remaining { --references_ };
        if (remaining == 0)
            delete this;
        return remaining;
    }

    POINT surface_drop_target::client_point(const POINTL point) const noexcept
    {
        // OLE는 화면 좌표를 준다.
        // 이벤트의 규약은 표면 client 물리 픽셀이다 (휠과 같은 변환이다).
        POINT value { point.x, point.y };
        static_cast<void>(ScreenToClient(window_, &value));
        return value;
    }

    HRESULT surface_drop_target::DragEnter(IDataObject* const data, DWORD, const POINTL point, DWORD* const effect)
    {
        files_ = dropped_file_paths(data);
        // 덮어쓰기 전에 들어온 허락 집합을 읽는다.
        const DWORD chosen { accepted_effect(allowed_effect(effect), files_.empty() == false) };
        if (effect != nullptr)
            *effect = chosen;
        if (chosen == DROPEFFECT_NONE)
        {
            // 받지 않을 끌기다 — 목록을 쥐고 있지 않는다.
            // 수락 대상 강조를 켜지도, 뒤이은 DragLeave가 짝 없는 알림을 내지도 않는다.
            files_.clear();
            return S_OK;
        }

        const POINT client { client_point(point) };
        host_->file_drag_entered(static_cast<float>(client.x), static_cast<float>(client.y), files_);
        return S_OK;
    }

    HRESULT surface_drop_target::DragOver(DWORD, const POINTL point, DWORD* const effect)
    {
        // 창 단위 수락이다 — 받기로 한 파일이 실려 있으면 받는다.
        // 어디에 떨어지는가(element 대상인가 물러섬인가)는 놓는 순간 정한다
        // (os-dragdrop-design.md).
        const DWORD chosen { accepted_effect(allowed_effect(effect), files_.empty() == false) };
        if (effect != nullptr)
            *effect = chosen;
        if (chosen != DROPEFFECT_NONE)
        {
            const POINT client { client_point(point) };
            host_->file_drag_moved(static_cast<float>(client.x), static_cast<float>(client.y));
        }
        return S_OK;
    }

    HRESULT surface_drop_target::DragLeave()
    {
        if (files_.empty() == false)
            host_->file_drag_left();
        files_.clear();
        return S_OK;
    }

    HRESULT surface_drop_target::Drop(IDataObject*, DWORD, const POINTL point, DWORD* const effect)
    {
        // 여기서도 허락 집합을 먼저 읽는다 — 거절이면 놓았다고 알리지 않는다.
        // 소비자가 이미 파일을 처리한 뒤에 NONE을 답하면 답과 행동이 어긋난다.
        const DWORD chosen { accepted_effect(allowed_effect(effect), files_.empty() == false) };
        if (effect != nullptr)
            *effect = chosen;
        if (chosen != DROPEFFECT_NONE)
        {
            const POINT client { client_point(point) };
            host_->file_drag_dropped(static_cast<float>(client.x), static_cast<float>(client.y), files_);
        }
        files_.clear();
        return S_OK;
    }
} // namespace luil::win32

#include "win32/win32_drop.h"

#include "win32/utf8.h"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace luil::win32 {
    namespace {
        // source가 복사를 허락했을 때만 받는다.
        // MOVE를 답하면 source가 원본을 지운다 — 우리는 경로·내용을 읽기만 하므로 그것은 손실이다.
        // 같은 이유로 grfKeyState(Shift/Ctrl)도 보지 않는다: 사용자의 손가락이
        // 우리가 답할 수 없는 뜻을 고르게 두면 위험만 커진다.
        [[nodiscard]] DWORD accepted_effect(const DWORD allowed, const bool has_content) noexcept
        {
            return (has_content && (allowed & DROPEFFECT_COPY) != 0u) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }

        // 허락 집합은 effect가 실어 온다 — 포인터가 없으면 허락을 알 길이 없어 받지 않는다.
        [[nodiscard]] DWORD allowed_effect(const DWORD* const effect) noexcept
        {
            return effect != nullptr ? *effect : static_cast<DWORD>(DROPEFFECT_NONE);
        }

        struct standard_format
        {
            CLIPFORMAT format { 0 };
            std::u8string_view name {};
        };

        // 이름이 없는 표준 형식이다. 상수 이름을 그대로 이름으로 쓴다 (앱이 문서에서 찾는 이름이다).
        constexpr std::array standard_formats {
            standard_format { CF_TEXT, u8"CF_TEXT" },
            standard_format { CF_BITMAP, u8"CF_BITMAP" },
            standard_format { CF_METAFILEPICT, u8"CF_METAFILEPICT" },
            standard_format { CF_SYLK, u8"CF_SYLK" },
            standard_format { CF_DIF, u8"CF_DIF" },
            standard_format { CF_TIFF, u8"CF_TIFF" },
            standard_format { CF_OEMTEXT, u8"CF_OEMTEXT" },
            standard_format { CF_DIB, u8"CF_DIB" },
            standard_format { CF_PALETTE, u8"CF_PALETTE" },
            standard_format { CF_PENDATA, u8"CF_PENDATA" },
            standard_format { CF_RIFF, u8"CF_RIFF" },
            standard_format { CF_WAVE, u8"CF_WAVE" },
            standard_format { CF_UNICODETEXT, u8"CF_UNICODETEXT" },
            standard_format { CF_ENHMETAFILE, u8"CF_ENHMETAFILE" },
            standard_format { CF_HDROP, u8"CF_HDROP" },
            standard_format { CF_LOCALE, u8"CF_LOCALE" },
            standard_format { CF_DIBV5, u8"CF_DIBV5" },
        };
    }

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

    std::u8string clipboard_format_name(const CLIPFORMAT format)
    {
        for (const standard_format& standard : standard_formats)
            if (standard.format == format)
                return std::u8string { standard.name };
        // 등록 형식만 이름이 있다 (0xC000부터). 그 밖의 표준·개인 형식은 이름이 없어 목록에 싣지 않는다.
        if (format < 0xC000)
            return {};
        std::array<wchar_t, 256> buffer {};
        const int length { GetClipboardFormatNameW(format, buffer.data(), static_cast<int>(buffer.size())) };
        if (length <= 0)
            return {};
        auto converted { utf16_to_utf8(std::wstring_view { buffer.data(), static_cast<std::size_t>(length) }) };
        return converted.value.value_or(std::u8string {});
    }

    CLIPFORMAT clipboard_format_of(const std::u8string_view name)
    {
        if (name.empty())
            return 0;
        for (const standard_format& standard : standard_formats)
            if (standard.name == name)
                return standard.format;
        const auto wide { utf8_to_utf16(name) };
        if (wide.value.has_value() == false)
            return 0;
        // 없는 이름이면 새로 등록된다. 그 형식을 싣는 source가 없으니 읽기는 그대로 실패한다.
        return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(wide.value->c_str()));
    }

    std::vector<std::u8string> data_object_formats(IDataObject* const data)
    {
        std::vector<std::u8string> names {};
        IEnumFORMATETC* enumerator { nullptr };
        if (data == nullptr || FAILED(data->EnumFormatEtc(DATADIR_GET, &enumerator)) || enumerator == nullptr)
            return names;
        // 같은 형식이 매체마다 거듭 올 수 있어 상한의 몇 배까지만 묻는다 — 끝없이 같은 것을 답하는 열거자도 멈춘다.
        for (std::size_t asked { 0 }; asked < max_drag_formats * 4 && names.size() < max_drag_formats; ++asked)
        {
            FORMATETC format {};
            ULONG fetched { 0 };
            if (enumerator->Next(1, &format, &fetched) != S_OK || fetched != 1)
                break;
            if (format.ptd != nullptr)
                CoTaskMemFree(format.ptd);
            std::u8string name { clipboard_format_name(format.cfFormat) };
            if (name.empty() == false && std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(std::move(name));
        }
        enumerator->Release();
        return names;
    }

    os_drag_data read_os_drag(IDataObject* const data)
    {
        os_drag_data drag {};
        drag.files = dropped_file_paths(data);
        drag.formats = data_object_formats(data);
        // 열거를 지원하지 않는 source도 경로가 읽혔으면 파일 형식을 실은 것이다.
        if (drag.files.empty() == false && std::find(drag.formats.begin(), drag.formats.end(), u8"CF_HDROP") == drag.formats.end())
            drag.formats.insert(drag.formats.begin(), u8"CF_HDROP");
        return drag;
    }

    surface_drop_target::surface_drop_target(const HWND window, drop_target_host& host) noexcept
        : window_ { window }
        , host_ { &host }
    {
        // 창이 없으면 그림을 얹을 자리가 없다 (test).
        if (window_ != nullptr && FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_IDropTargetHelper, reinterpret_cast<void**>(&helper_))))
            helper_ = nullptr;
    }

    surface_drop_target::~surface_drop_target()
    {
        if (helper_ != nullptr)
            helper_->Release();
    }

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
        drag_ = read_os_drag(data);
        // 덮어쓰기 전에 들어온 허락 집합을 읽는다.
        DWORD chosen { accepted_effect(allowed_effect(effect), drag_.empty() == false) };
        if (chosen == DROPEFFECT_NONE)
        {
            // 받지 않을 끌기다 — 실린 것을 쥐고 있지 않는다.
            // 수락 대상 강조를 켜지도, 뒤이은 DragLeave가 짝 없는 알림을 내지도 않는다.
            drag_ = {};
        }
        else
        {
            // 끌기는 창 단위로 받아 두고, 답만 자리에 따라 가린다 — 불투명 표면 위에서
            // 들어와도 그 밖으로 옮기면 놓을 수 있다.
            const POINT client { client_point(point) };
            if (host_->file_drag_entered(static_cast<float>(client.x), static_cast<float>(client.y), drag_) == false)
                chosen = DROPEFFECT_NONE;
        }
        if (effect != nullptr)
            *effect = chosen;
        // 받지 않는 끌기라도 그림은 보인다 — 커서가 "받지 않음"을 보이는 동안에도 무엇을 끄는지는 사용자가 안다.
        if (helper_ != nullptr && data != nullptr)
        {
            POINT screen { point.x, point.y };
            helper_entered_ = SUCCEEDED(helper_->DragEnter(window_, data, &screen, chosen));
        }
        return S_OK;
    }

    HRESULT surface_drop_target::DragOver(DWORD, const POINTL point, DWORD* const effect)
    {
        // 받기로 한 끌기면 그 자리에 놓을 수 있는지를 host가 답한다 — 불투명 표면(modal·메뉴·토스트)이 가린 자리,
        // 파일 없는 끌기에서 받는 element가 없는 자리는 NONE이다. 어느 element가 받는가는 놓는 순간 정한다
        // (os-dragdrop-design.md).
        DWORD chosen { accepted_effect(allowed_effect(effect), drag_.empty() == false) };
        if (chosen != DROPEFFECT_NONE)
        {
            const POINT client { client_point(point) };
            if (host_->file_drag_moved(static_cast<float>(client.x), static_cast<float>(client.y)) == false)
                chosen = DROPEFFECT_NONE;
        }
        if (effect != nullptr)
            *effect = chosen;
        if (helper_entered_)
        {
            POINT screen { point.x, point.y };
            static_cast<void>(helper_->DragOver(&screen, chosen));
        }
        return S_OK;
    }

    HRESULT surface_drop_target::DragLeave()
    {
        if (drag_.empty() == false)
            host_->file_drag_left();
        drag_ = {};
        if (helper_entered_)
        {
            static_cast<void>(helper_->DragLeave());
            helper_entered_ = false;
        }
        return S_OK;
    }

    HRESULT surface_drop_target::Drop(IDataObject* const data, const DWORD key_state, const POINTL point, DWORD* const effect)
    {
        // 여기서도 허락 집합을 먼저 읽는다 — 거절이면 놓았다고 알리지 않는다.
        // NONE은 **아무도 받지 않았을 때만** 답한다. 소비자가 이미 내용을 처리한 뒤에
        // NONE을 답하면 답과 행동이 어긋난다.
        //  - 내용은 `Drop`이 준 데이터 객체에서 꺼낸다. 들어올 때의 것과 대개 같지만 OLE가 이 호출에 준 것이 계약이다.
        DWORD chosen { accepted_effect(allowed_effect(effect), drag_.empty() == false) };
        if (chosen != DROPEFFECT_NONE)
        {
            const POINT client { client_point(point) };
            if (host_->file_drag_dropped(static_cast<float>(client.x), static_cast<float>(client.y), drag_, data, key_state) == false)
                chosen = DROPEFFECT_NONE;
        }
        if (effect != nullptr)
            *effect = chosen;
        drag_ = {};
        // 그림은 놓기의 답과 함께 거둔다.
        if (helper_entered_)
        {
            POINT screen { point.x, point.y };
            static_cast<void>(helper_->Drop(data, &screen, chosen));
            helper_entered_ = false;
        }
        return S_OK;
    }
}

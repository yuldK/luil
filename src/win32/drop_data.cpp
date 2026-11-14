#include "luil/win32/drop_data.h"

#include "win32/utf8.h"
#include "win32/win32_drop.h"

#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace luil::win32 {
    namespace {
        // 한 번에 읽을 가상 파일 수의 상한이다 (경로의 `max_dropped_paths`와 같은 까닭).
        constexpr UINT max_virtual_files { 4096 };

        // `GetData`가 준 매체를 놓는 RAII다.
        struct medium_scope
        {
            STGMEDIUM value {};
            bool held { false };

            medium_scope() = default;
            medium_scope(const medium_scope&) = delete;
            medium_scope& operator=(const medium_scope&) = delete;

            ~medium_scope()
            {
                if (held)
                    ReleaseStgMedium(&value);
            }
        };

        // 그 형식을 받아 온다. 실패하면 거짓이다.
        [[nodiscard]] bool get_medium(IDataObject* const data, const std::u8string_view format, const int index, const DWORD media, medium_scope& medium)
        {
            const CLIPFORMAT id { clipboard_format_of(format) };
            if (data == nullptr || id == 0)
                return false;
            FORMATETC request { id, nullptr, DVASPECT_CONTENT, index, media };
            medium.held = SUCCEEDED(data->GetData(&request, &medium.value));
            return medium.held;
        }

        [[nodiscard]] std::optional<std::vector<std::byte>> global_bytes(const HGLOBAL global, const std::size_t limit)
        {
            if (global == nullptr)
                return std::nullopt;
            const SIZE_T size { GlobalSize(global) };
            if (size > limit)
                return std::nullopt;
            const void* const locked { GlobalLock(global) };
            if (locked == nullptr)
                return size == 0 ? std::optional<std::vector<std::byte>> { std::vector<std::byte> {} } : std::nullopt;
            std::vector<std::byte> bytes(size);
            if (size != 0)
                std::memcpy(bytes.data(), locked, size);
            GlobalUnlock(global);
            return bytes;
        }

        [[nodiscard]] std::optional<std::vector<std::byte>> stream_bytes(IStream* const stream, const std::size_t limit)
        {
            if (stream == nullptr)
                return std::nullopt;
            // 앞에서부터 읽는다. 처음 자리에 두지 않고 주는 source가 있다 (되감기를 못 하는 흐름이면 지금 자리부터다).
            LARGE_INTEGER origin {};
            static_cast<void>(stream->Seek(origin, STREAM_SEEK_SET, nullptr));
            std::vector<std::byte> bytes {};
            std::vector<std::byte> chunk(64u * 1024u);
            for (;;)
            {
                ULONG read { 0 };
                const HRESULT result { stream->Read(chunk.data(), static_cast<ULONG>(chunk.size()), &read) };
                if (FAILED(result))
                    return std::nullopt;
                if (read == 0)
                    break;
                // 상한을 넘으면 잘린 내용을 돌려주지 않는다.
                if (bytes.size() + read > limit)
                    return std::nullopt;
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
                if (result == S_FALSE)
                    break;
            }
            return bytes;
        }
    }

    std::vector<dropped_virtual_file> drop_virtual_files(void* const data)
    {
        std::vector<dropped_virtual_file> files {};
        medium_scope medium {};
        if (get_medium(static_cast<IDataObject*>(data), u8"FileGroupDescriptorW", -1, TYMED_HGLOBAL, medium) == false || medium.value.tymed != TYMED_HGLOBAL)
            return files;
        const HGLOBAL global { medium.value.hGlobal };
        const SIZE_T size { global != nullptr ? GlobalSize(global) : 0 };
        if (size < sizeof(UINT))
            return files;
        const auto* const group { static_cast<const FILEGROUPDESCRIPTORW*>(GlobalLock(global)) };
        if (group == nullptr)
            return files;
        // 블록이 알린 개수보다 짧으면 들어 있는 만큼만 읽는다 — 밖에서 온 개수를 믿고 넘어 읽지 않는다.
        const SIZE_T fit { (size - offsetof(FILEGROUPDESCRIPTORW, fgd)) / sizeof(FILEDESCRIPTORW) };
        const UINT count { static_cast<UINT>(std::min<SIZE_T>({ group->cItems, fit, max_virtual_files })) };
        files.reserve(count);
        for (UINT index { 0 }; index < count; ++index)
        {
            const FILEDESCRIPTORW& descriptor { group->fgd[index] };
            const std::size_t length { wcsnlen(descriptor.cFileName, MAX_PATH) };
            dropped_virtual_file file {};
            if (auto converted { utf16_to_utf8(std::wstring_view { descriptor.cFileName, length }) }; converted.value.has_value())
                file.name = std::move(*converted.value);
            if ((descriptor.dwFlags & FD_FILESIZE) != 0)
                file.size = (static_cast<std::uint64_t>(descriptor.nFileSizeHigh) << 32) | descriptor.nFileSizeLow;
            file.directory = (descriptor.dwFlags & FD_ATTRIBUTES) != 0 && (descriptor.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            files.push_back(std::move(file));
        }
        GlobalUnlock(global);
        return files;
    }

    std::optional<std::vector<std::byte>> drop_data_bytes(void* const data, const std::u8string_view format, const int index, const std::size_t limit)
    {
        medium_scope medium {};
        if (get_medium(static_cast<IDataObject*>(data), format, index, TYMED_HGLOBAL | TYMED_ISTREAM, medium) == false)
            return std::nullopt;
        if (medium.value.tymed == TYMED_HGLOBAL)
            return global_bytes(medium.value.hGlobal, limit);
        if (medium.value.tymed == TYMED_ISTREAM)
            return stream_bytes(medium.value.pstm, limit);
        return std::nullopt;
    }

    std::optional<std::u8string> drop_data_text(void* const data, const std::u8string_view format, const std::size_t limit)
    {
        const std::optional<std::vector<std::byte>> bytes { drop_data_bytes(data, format, -1, limit) };
        if (bytes.has_value() == false)
            return std::nullopt;
        std::wstring text(bytes->size() / sizeof(wchar_t), L'\0');
        if (text.empty() == false)
            std::memcpy(text.data(), bytes->data(), text.size() * sizeof(wchar_t));
        // 매체는 흔히 NUL 뒤에 남는 자리가 있다 (HGLOBAL은 할당 단위로 커진다).
        if (const std::size_t end { text.find(L'\0') }; end != std::wstring::npos)
            text.resize(end);
        auto converted { utf16_to_utf8(text) };
        return std::move(converted.value);
    }
}

#include "luil/text/fonts.h"

#include "win32/embedded_assets.h"

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkTypeface_win.h"

#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] sk_sp<SkTypeface> typeface_from_data(sk_sp<SkData> data, const int face_index)
        {
            if (data == nullptr || data->isEmpty() || face_index < 0)
                return nullptr;
            const sk_sp<SkFontMgr> manager { SkFontMgr_New_DirectWrite() };
            return manager != nullptr ? manager->makeFromData(std::move(data), face_index) : nullptr;
        }
    } // namespace

    sk_sp<SkTypeface> load_typeface_bytes(const std::span<const std::uint8_t> bytes, const int face_index)
    {
        if (bytes.empty() || face_index < 0)
            return nullptr;
        return typeface_from_data(SkData::MakeWithCopy(bytes.data(), bytes.size()), face_index);
    }

    sk_sp<SkTypeface> load_typeface_file(const std::filesystem::path& path, const int face_index)
    {
        if (path.empty() || path.native().find(L'\0') != std::wstring::npos || face_index < 0)
            return nullptr;
        std::error_code error {};
        if (std::filesystem::is_regular_file(path, error) == false || error)
            return nullptr;
        const std::uintmax_t size { std::filesystem::file_size(path, error) };
        if (error || size == 0 || size > std::numeric_limits<std::size_t>::max() || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            return nullptr;
        std::ifstream file { path, std::ios::binary };
        if (file.is_open() == false)
            return nullptr;
        sk_sp<SkData> data { SkData::MakeUninitialized(static_cast<std::size_t>(size)) };
        if (data == nullptr || file.read(static_cast<char*>(data->writable_data()), static_cast<std::streamsize>(size)).fail())
            return nullptr;
        return typeface_from_data(std::move(data), face_index);
    }

    sk_sp<SkTypeface> load_codicon_typeface()
    {
        return win32::load_codicon_typeface();
    }
} // namespace luil

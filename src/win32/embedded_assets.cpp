#include "win32/embedded_assets.h"

#include "win32/resource_ids.h"

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_win.h"

#include <windows.h>

#include <array>
#include <mutex>
#include <utility>
#include <vector>

namespace luil::win32 {
    embedded_resource_view find_embedded_resource(const int resource_id) noexcept
    {
        const HMODULE module { GetModuleHandleW(nullptr) };
        const HRSRC resource { FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA) };
        if (resource == nullptr)
            return {};

        const HGLOBAL loaded_resource { LoadResource(module, resource) };
        if (loaded_resource == nullptr)
            return {};

        const DWORD resource_size { SizeofResource(module, resource) };
        const void* resource_data { LockResource(loaded_resource) };
        if (resource_data == nullptr || resource_size == 0)
            return {};
        return { resource_data, static_cast<std::size_t>(resource_size) };
    }

    sk_sp<SkTypeface> load_codicon_typeface()
    {
        const embedded_resource_view font_resource { find_embedded_resource(IDR_CODICONS_FONT) };
        if (font_resource.data == nullptr)
            return nullptr;

        sk_sp<SkData> font_data { SkData::MakeWithCopy(font_resource.data, font_resource.size) };
        sk_sp<SkFontMgr> font_manager { SkFontMgr_New_DirectWrite() };
        if (font_manager == nullptr || font_data == nullptr)
            return nullptr;
        return font_manager->makeFromData(std::move(font_data));
    }

    namespace {
        // 기본 UI 글꼴 선호 목록과 자물쇠다.
        // 시작 시 한 번 지정하고 이후에는 읽기만 하므로 잠금 구간이 짧다.
        struct ui_typeface_preference
        {
            std::mutex mutex {};
            std::vector<std::u8string> families { u8"Segoe UI" };
        };

        ui_typeface_preference& preference()
        {
            static ui_typeface_preference instance {};
            return instance;
        }
    } // namespace

    void set_ui_typeface_families(std::vector<std::u8string> families)
    {
        ui_typeface_preference& state { preference() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        state.families = std::move(families);
    }

    sk_sp<SkTypeface> load_ui_typeface()
    {
        sk_sp<SkFontMgr> font_manager { SkFontMgr_New_DirectWrite() };
        if (font_manager == nullptr)
            return nullptr;

        std::vector<std::u8string> families {};
        {
            ui_typeface_preference& state { preference() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            families = state.families;
        }

        for (const std::u8string& family : families)
        {
            const std::string name { reinterpret_cast<const char*>(family.c_str()), family.size() };
            if (sk_sp<SkTypeface> typeface { font_manager->matchFamilyStyle(name.c_str(), SkFontStyle::Normal()) }; typeface != nullptr)
                return typeface;
        }
        return font_manager->matchFamilyStyle("Segoe UI", SkFontStyle::Normal());
    }

    bool verify_embedded_resources(std::u8string& error)
    {
        constexpr std::array required_resources {
            IDR_CODICONS_FONT,
            IDR_CODICONS_LICENSE,
            IDR_CODICONS_LICENSE_CODE,
            IDR_THIRD_PARTY_NOTICES,
        };

        for (const int resource_id : required_resources)
        {
            if (find_embedded_resource(resource_id).data == nullptr)
            {
                error = u8"Required executable resource not found.";
                return false;
            }
        }

        if (load_codicon_typeface() == nullptr)
        {
            error = u8"Failed to create a Skia typeface from the embedded Codicons font.";
            return false;
        }
        return true;
    }
} // namespace luil::win32

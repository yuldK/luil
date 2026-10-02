#include "host/font_registry.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_win.h"

#include <windows.h>

#include <dwrite.h>

#include <cstddef>
#include <string>

namespace luil {
    namespace {
        // Win32의 글꼴 자원이다. cache·설정·대체 규칙은 core의 registry가 갖고,
        // 여기는 DirectWrite 관리자와 OS 언어만 공급한다 (host/font_registry.h).
        class win32_font_source final : public font_source
        {
        public:
            // **GDI가 아니라 DirectWrite다.** Skia는 `UNICODE` 없이 빌드되어
            // GDI font manager가 ANSI API로 컴파일된다 (`EnumFontFamiliesExA`).
            // 그러면 한국어 Windows에서 `맑은 고딕` 같은 이름이 CP949 바이트로
            // 와서 UTF-8이 아니게 되고, 그리는 순간 Skia가 abort한다.
            // DirectWrite는 이름을 사용자 로캘의 UTF-8로 주고 글자 단위 대체(fallback)도 지원한다.
            //
            // **font collection을 직접 얻어 넘긴다.** 인자 없이 부르면 Skia가 스스로
            // `GetSystemFontCollection(&collection, FALSE)`를 불러 그때의 목록을 관리자 수명
            // 내내 들고 있는다 (`third_party/skia/src/ports/SkFontMgr_win_dw.cpp`).
            // `WM_FONTCHANGE` 뒤 관리자를 다시 만들어도 update check를 켜지 않으면 새로 설치된
            // 글꼴이 끝내 보이지 않아 무효화가 헛돈다.
            [[nodiscard]] sk_sp<SkFontMgr> make_system_manager() const override
            {
                sk_sp<SkFontMgr> manager {};
                IDWriteFactory* factory { nullptr };
                // SHARED는 프로세스마다 하나뿐인 factory라 Skia가 안에서 얻는 것과 같은 객체가 온다.
                //  - factory가 갈라지면 같은 글꼴이 서로 다른 typeface로 와 cache가 어긋난다.
                if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory))) && factory != nullptr)
                {
                    IDWriteFontCollection* collection { nullptr };
                    // `TRUE`가 요점이다 — OS에 글꼴 목록이 바뀌었는지 다시 보라고 시킨다.
                    //  - 수백 ms까지 걸릴 수 있어 무효화가 아니라 다음 조회에서 게으르게 치른다.
                    if (SUCCEEDED(factory->GetSystemFontCollection(&collection, TRUE)) && collection != nullptr)
                    {
                        // 관리자가 factory와 collection을 각각 AddRef하므로 우리 몫은 여기서 놓는다.
                        manager = SkFontMgr_New_DirectWrite(factory, collection);
                        collection->Release();
                    }
                    factory->Release();
                }
                // dwrite 초기화가 어긋나도 글꼴이 통째로 죽지는 않게 한다.
                //  - 목록이 낡을 뿐인 것과 아무것도 그리지 못하는 것은 무게가 다르다.
                if (manager == nullptr)
                    manager = SkFontMgr_New_DirectWrite();
                return manager;
            }

            [[nodiscard]] sk_sp<SkFontMgr> make_uncached_manager() const override
            {
                return SkFontMgr_New_DirectWrite();
            }

            [[nodiscard]] std::u8string_view default_ui_family() const override
            {
                return u8"Segoe UI";
            }

            // 사용자 UI 언어를 OS에서 읽는다 (`ko-KR` 꼴).
            // 읽지 못하면 빈 문자열이고, 그때는 언어 없이 대체 글꼴을 고른다.
            [[nodiscard]] std::string read_user_language() const override
            {
                wchar_t buffer[LOCALE_NAME_MAX_LENGTH] {};
                const int length { GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH) };
                // 반환값은 끝의 NUL을 포함한 길이다. 1이면 이름이 비어 있다.
                if (length <= 1)
                    return {};
                // 로케일 이름은 ASCII다. 좁히는 것으로 충분하다.
                std::string result {};
                result.reserve(static_cast<std::size_t>(length) - 1u);
                for (int index = 0; index < length - 1; ++index)
                    result.push_back(static_cast<char>(buffer[index]));
                return result;
            }
        };
    } // namespace

    const font_source& platform_font_source() noexcept
    {
        static const win32_font_source source {};
        return source;
    }
} // namespace luil

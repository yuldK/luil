#include "host/font_registry.h"
#include "luil/text/fonts.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_block.h"
#include "ui/font_synthesis.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 굵기·기울임 해석을 실제 Windows 글꼴로 잰다 (docs/font-style-design.md).
// core test의 글꼴 관리자는 비어 있어 진짜 face를 찾는지는 여기서만 볼 수 있다.
namespace {
    [[nodiscard]] std::filesystem::path windows_font(const char* const file)
    {
        wchar_t root[MAX_PATH] {};
        const UINT length { GetWindowsDirectoryW(root, MAX_PATH) };
        if (length == 0 || length >= MAX_PATH)
            return {};
        return std::filesystem::path { root } / L"Fonts" / file;
    }

    [[nodiscard]] sk_sp<SkTypeface> font_file(const char* const file)
    {
        const std::filesystem::path path { windows_font(file) };
        if (path.empty() || std::filesystem::exists(path) == false)
            return nullptr;
        return luil::load_typeface_file(path);
    }

    class registry_fallback final : public luil::font_fallback
    {
    public:
        [[nodiscard]] sk_sp<SkTypeface> for_codepoint(const char32_t codepoint) const override
        {
            return luil::fallback_typeface(codepoint);
        }

        [[nodiscard]] sk_sp<SkTypeface> for_styled_codepoint(const char32_t codepoint, const luil::font_style style) const override
        {
            return luil::fallback_typeface(codepoint, style);
        }
    };

    class installed_fallback_scope
    {
    public:
        explicit installed_fallback_scope(const luil::font_fallback& fallback)
        {
            luil::set_font_fallback(&fallback);
        }

        installed_fallback_scope(const installed_fallback_scope&) = delete;
        installed_fallback_scope(installed_fallback_scope&&) = delete;
        installed_fallback_scope& operator=(const installed_fallback_scope&) = delete;
        installed_fallback_scope& operator=(installed_fallback_scope&&) = delete;

        ~installed_fallback_scope()
        {
            luil::set_font_fallback(nullptr);
        }
    };

    // 등록한 앱 가족을 test 끝에 지운다. 다른 test가 보는 설치 목록과 UI 글꼴을 흔들지 않는다.
    class registered_family_scope
    {
    public:
        registered_family_scope(std::u8string family, std::vector<sk_sp<SkTypeface>> faces)
            : family_ { std::move(family) }
        {
            luil::register_font_family(family_, std::move(faces));
        }

        registered_family_scope(const registered_family_scope&) = delete;
        registered_family_scope(registered_family_scope&&) = delete;
        registered_family_scope& operator=(const registered_family_scope&) = delete;
        registered_family_scope& operator=(registered_family_scope&&) = delete;

        ~registered_family_scope()
        {
            luil::set_ui_typeface_families({});
            luil::unregister_font_family(family_);
        }

    private:
        std::u8string family_ {};
    };

    constexpr luil::font_style bold { luil::font_weight::bold };
}

// 시스템 가족은 그 굵기의 진짜 face로 그린다. 덧칠은 face가 없을 때만이다.
TEST_CASE("A system family resolves real weights and italics", "[win32][fonts][font-style]")
{
    luil::clear_font_caches();
    const sk_sp<SkTypeface> regular { luil::family_typeface(u8"Segoe UI") };
    if (regular == nullptr)
        SKIP("Segoe UI is not installed on this machine.");
    REQUIRE(luil::achieved_weight(*regular) == 400);

    const luil::styled_typeface heavy { luil::style_typeface(regular.get(), bold) };
    REQUIRE(heavy.typeface != nullptr);
    REQUIRE(heavy.typeface.get() != regular.get());
    REQUIRE(luil::achieved_weight(*heavy.typeface) == 700);
    REQUIRE_FALSE(heavy.embolden);

    const luil::styled_typeface semibold { luil::style_typeface(regular.get(), { luil::font_weight::semibold }) };
    REQUIRE(luil::achieved_weight(*semibold.typeface) == 600);
    REQUIRE_FALSE(semibold.embolden);

    const luil::styled_typeface italic { luil::style_typeface(regular.get(), { luil::font_weight::regular, luil::font_slant::italic }) };
    REQUIRE(italic.typeface->fontStyle().slant() != SkFontStyle::kUpright_Slant);
    REQUIRE_FALSE(italic.skew);

    // 굵기를 바꾼 face를 다시 바꿔도 같은 가족에 묻는다 (범위 강조가 semibold label 안에서 Bold를 고를 때다).
    const luil::styled_typeface from_semibold { luil::style_typeface(semibold.typeface.get(), bold) };
    REQUIRE(luil::achieved_weight(*from_semibold.typeface) == 700);

    // 같은 요청은 같은 face다. 다르면 배치 cache가 frame마다 어긋난다.
    REQUIRE(luil::style_typeface(regular.get(), bold).typeface.get() == heavy.typeface.get());
    // 기본 모양은 받은 face 그대로다.
    REQUIRE(luil::style_typeface(regular.get(), {}).typeface.get() == regular.get());
    luil::clear_font_caches();
}

// 파일로 읽은 face는 시스템 가족에 묻지 않는다. 같은 이름이 깔려 있어도 앱 글꼴이 시스템 글꼴로 바뀌면 안 된다.
TEST_CASE("A face loaded from a file keeps itself and is emboldened instead", "[win32][fonts][font-style]")
{
    const sk_sp<SkTypeface> file_face { font_file("segoeui.ttf") };
    if (file_face == nullptr)
        SKIP("segoeui.ttf is not available.");

    const luil::styled_typeface heavy { luil::style_typeface(file_face.get(), bold) };
    REQUIRE(heavy.typeface.get() == file_face.get());
    REQUIRE(heavy.embolden);
    REQUIRE_FALSE(heavy.skew);

    const luil::styled_typeface italic { luil::style_typeface(file_face.get(), { luil::font_weight::regular, luil::font_slant::italic }) };
    REQUIRE(italic.skew);
    REQUIRE_FALSE(italic.embolden);

    // 덧칠 문턱 아래는 메우지 않는다 — medium을 덧칠하면 Bold가 된다.
    REQUIRE_FALSE(luil::style_typeface(file_face.get(), { luil::font_weight::medium }).embolden);
}

// 가변 글꼴은 `wght` 축으로 요청한 굵기를 정확히 낸다.
TEST_CASE("A variable font is cloned along its weight axis", "[win32][fonts][font-style]")
{
    sk_sp<SkTypeface> variable { font_file("SegUIVar.ttf") };
    float minimum { 0.0f };
    float maximum { 0.0f };
    if (variable == nullptr || luil::weight_axis_range(*variable, minimum, maximum) == false)
        SKIP("No variable font with a weight axis is available.");

    const luil::font_style wanted { static_cast<luil::font_weight>(650) };
    const luil::styled_typeface heavy { luil::style_typeface(variable.get(), wanted) };
    REQUIRE(heavy.typeface != nullptr);
    REQUIRE(heavy.typeface.get() != variable.get());
    REQUIRE(luil::achieved_weight(*heavy.typeface) == std::clamp(650, static_cast<int>(minimum), static_cast<int>(maximum)));
    REQUIRE_FALSE(heavy.embolden);
    // clone도 cache된다.
    REQUIRE(luil::style_typeface(variable.get(), wanted).typeface.get() == heavy.typeface.get());

    // 축 범위 밖은 끝으로 자른다.
    const luil::styled_typeface lightest { luil::style_typeface(variable.get(), { static_cast<luil::font_weight>(1) }) };
    REQUIRE(luil::achieved_weight(*lightest.typeface) == static_cast<int>(minimum));
}

// 등록한 앱 가족은 설치된 가족처럼 이름으로 찾고, 굵기는 가족 안에서 고른다.
TEST_CASE("A registered app family serves its weights by name", "[win32][fonts][font-style]")
{
    const sk_sp<SkTypeface> regular { font_file("segoeui.ttf") };
    const sk_sp<SkTypeface> heavy { font_file("segoeuib.ttf") };
    if (regular == nullptr || heavy == nullptr)
        SKIP("segoeui.ttf or segoeuib.ttf is not available.");

    const std::u8string family { u8"luil test family" };
    const registered_family_scope scope { family, { heavy, regular, nullptr } };

    const std::vector<std::u8string> installed { luil::installed_font_families() };
    REQUIRE(std::ranges::find(installed, family) != installed.end());

    // 이름만으로는 regular다.
    const sk_sp<SkTypeface> named { luil::family_typeface(family) };
    REQUIRE(named.get() == regular.get());
    // 굵게 요청하면 같은 가족의 Bold face다 — 덧칠이 아니다.
    const luil::styled_typeface styled { luil::style_typeface(named.get(), bold) };
    REQUIRE(styled.typeface.get() == heavy.get());
    REQUIRE_FALSE(styled.embolden);
    // 가족에 없는 기울임은 눕혀 메운다.
    REQUIRE(luil::style_typeface(named.get(), { luil::font_weight::regular, luil::font_slant::italic }).skew);

    // 기본 UI 글꼴 목록에서도 이름으로 고른다.
    luil::set_ui_typeface_families({ family });
    REQUIRE(luil::load_ui_typeface().get() == regular.get());
    REQUIRE(luil::configured_ui_typeface().get() == regular.get());

    // 지우면 이름이 사라진다.
    luil::unregister_font_family(family);
    const std::vector<std::u8string> after { luil::installed_font_families() };
    REQUIRE(std::ranges::find(after, family) == after.end());
    REQUIRE(luil::family_typeface(family) == nullptr);
}

// 굵은 글 속 한글은 굵은 대체 face로 그린다. 바탕의 덧칠을 그대로 잇지 않는다.
TEST_CASE("Fallback glyphs inside bold text take a bold fallback face", "[win32][fonts][font-style]")
{
    const sk_sp<SkTypeface> regular { luil::family_typeface(u8"Segoe UI") };
    if (regular == nullptr)
        SKIP("Segoe UI is not installed on this machine.");
    // 전제: Segoe UI에는 한글이 없다.
    REQUIRE(regular->unicharToGlyph(U'한') == 0);

    const sk_sp<SkTypeface> plain { luil::fallback_typeface(U'한') };
    const sk_sp<SkTypeface> heavy { luil::fallback_typeface(U'한', bold) };
    REQUIRE(plain != nullptr);
    REQUIRE(heavy != nullptr);
    REQUIRE(luil::achieved_weight(*heavy) > luil::achieved_weight(*plain));

    const registry_fallback fallback {};
    const installed_fallback_scope scope { fallback };
    SkFont font { regular, 14.0f };
    luil::apply_font_style(font, bold);
    REQUIRE_FALSE(font.isEmbolden());
    const std::vector<luil::text_run> runs { luil::split_text_runs(u8"Bold 한글", font) };
    REQUIRE(runs.size() == 2u);
    REQUIRE(runs[1].typeface.get() == heavy.get());
    REQUIRE(luil::font_style_of(font).weight == luil::font_weight::bold);
}

// 직접 그리는 글의 굵기가 진짜 face로 서는지와, 측정이 같은 face로 재는지다.
TEST_CASE("A text font with a weight draws and measures with the weighted face", "[win32][fonts][font-style]")
{
    luil::clear_font_caches();
    const sk_sp<SkTypeface> ui { luil::configured_ui_typeface() };
    REQUIRE(ui != nullptr);

    SkBitmap bitmap {};
    bitmap.allocN32Pixels(8, 8);
    SkCanvas canvas { bitmap };
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::light) };
    const luil::draw_context context { .canvas = canvas, .ui_typeface = ui.get(), .code_typeface = ui.get(), .palette = palette };

    const SkFont regular_font { luil::make_text_font(context, { .size = 14.0f }) };
    const SkFont bold_font { luil::make_text_font(context, { .size = 14.0f, .weight = luil::font_weight::bold }) };
    REQUIRE(regular_font.getTypeface() == ui.get());
    REQUIRE(bold_font.getTypeface() != ui.get());
    REQUIRE(luil::achieved_weight(*bold_font.getTypeface()) == 700);
    REQUIRE_FALSE(bold_font.isEmbolden());

    // 측정 문맥의 굵기 측정도 같은 face다.
    const luil::measure_context measure {};
    const std::u8string_view text { u8"Weighted width" };
    REQUIRE(measure.text_width(text, 14.0f, bold) == luil::measure_text(text, bold_font));
    REQUIRE(measure.text_width(text, 14.0f, bold) > measure.text_width(text, 14.0f));
    REQUIRE(luil::styled_text_measurer(bold)(text, 14.0f) == luil::measure_text(text, bold_font));
    luil::clear_font_caches();
}

// 굵기 해석 cache는 상한이 있어 굵기를 아무 수로나 요청해도 한없이 자라지 않는다.
TEST_CASE("The styled typeface cache is bounded and cleared with the font caches", "[win32][fonts][font-style]")
{
    luil::clear_font_caches();
    REQUIRE(luil::styled_cache_size() == 0u);
    const sk_sp<SkTypeface> ui { luil::configured_ui_typeface() };
    REQUIRE(ui != nullptr);
    for (int weight = 1; weight <= 1000; ++weight)
    {
        static_cast<void>(luil::style_typeface(ui.get(), { static_cast<luil::font_weight>(weight) }));
        static_cast<void>(luil::style_typeface(ui.get(), { static_cast<luil::font_weight>(weight), luil::font_slant::italic }));
    }
    REQUIRE(luil::styled_cache_size() > 0u);
    REQUIRE(luil::styled_cache_size() <= 1024u);
    luil::clear_font_caches();
    REQUIRE(luil::styled_cache_size() == 0u);
}

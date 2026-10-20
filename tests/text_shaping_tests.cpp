#include "ui/text_shaping.h"

#include <catch2/catch_test_macros.hpp>

#include "modules/skshaper/include/SkShaper.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <cstdlib>
#include <memory>
#include <string_view>

// ICU의 초기화와 luil의 공장은 프로세스에 한 번만 선다. 별도 실행 파일에서 ICU를
// 처음부터 끈다 — 다른 이미지 test가 먼저 ICU를 열어 거저 통과하는 일이 없어야 한다.
TEST_CASE("Text shaping works when the platform ICU is unavailable", "[ui][text_shaping]")
{
#if defined(_WIN32)
    REQUIRE(_putenv_s("SKIA_PREP_ICU_DISABLE", "1") == 0);
#else
    REQUIRE(setenv("SKIA_PREP_ICU_DISABLE", "1", 1) == 0);
#endif
    REQUIRE(SkUnicodes::ICU::Make() == nullptr);
    const sk_sp<SkShapers::Factory> factory { luil::text_shaping_factory() };
    REQUIRE(factory != nullptr);
#if LUIL_TESTS_TEXT_SHAPING_FALLBACK
    SkUnicode* const unicode { factory->getUnicode() };
    REQUIRE(unicode != nullptr);

    // ZWJ 이모지는 UTF-8 바이트 여럿이어도 글자소 하나다.
    constexpr std::u8string_view emoji { u8"👩‍💻" };
    const auto breaks { unicode->makeBreakIterator(SkUnicode::BreakType::kGraphemes) };
    REQUIRE(breaks != nullptr);
    REQUIRE(breaks->setText(reinterpret_cast<const char*>(emoji.data()), static_cast<int>(emoji.size())));
    REQUIRE(breaks->first() == 0);
    REQUIRE(breaks->next() == static_cast<int>(emoji.size()));
    static_cast<void>(breaks->next());
    REQUIRE(breaks->isDone());

#else
    REQUIRE(factory->getUnicode() == nullptr);
#endif

    // primitive라면 RTL 문단도 0번 방향으로 남는다. 양쪽 모두 실제 iterator를 만들어
    // 써야 null인 Unicode를 가진 HarfBuzz 공장이 거저 통과하지 않는다.
    constexpr std::u8string_view arabic { u8"سلام" };
    const auto bidi { factory->makeBidiRunIterator(reinterpret_cast<const char*>(arabic.data()), arabic.size(), 0) };
    REQUIRE(bidi != nullptr);
#if LUIL_TESTS_TEXT_SHAPING_FALLBACK
    REQUIRE(bidi->atEnd() == false);
    bidi->consume();
    REQUIRE((bidi->currentLevel() & 1u) == 1u);
    REQUIRE(bidi->endOfCurrentRun() == arabic.size());
    REQUIRE(bidi->atEnd());
#else
    REQUIRE(bidi->currentLevel() == 0);
#endif
}

#include "ui/text_shaping.h"

#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <utility>

// 텍스트 모양 잡기를 켠 구성이다 (`LUIL_ENABLE_TEXT_SHAPING`). 이 파일이 HarfBuzz와
// SkUnicode를 끌어온다.
namespace luil {
    namespace {
        [[nodiscard]] sk_sp<SkShapers::Factory> make_text_shaping_factory()
        {
            // ICU는 링크하지 않고 실행 시점에 플랫폼의 것을 연다. 열리지 않으면 nullptr이고,
            // 그때 HarfBuzz 공장을 지으면 안에서 다시 열어 보다 null인 SkUnicode를 든다 —
            // 그래서 그 자리를 여기서 primitive로 가른다 (skia-prep docs/skia-build.md 6.4).
            sk_sp<SkUnicode> unicode { SkUnicodes::ICU::Make() };
            if (unicode == nullptr)
                return SkShapers::Primitive::Factory();
            return sk_make_sp<SkShapers::HarfbuzzFactory>(std::move(unicode));
        }
    } // namespace

    sk_sp<SkShapers::Factory> text_shaping_factory()
    {
        static const sk_sp<SkShapers::Factory> factory { make_text_shaping_factory() };
        return factory;
    }
} // namespace luil

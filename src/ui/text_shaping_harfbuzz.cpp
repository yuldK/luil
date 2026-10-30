#include "ui/text_shaping.h"

#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/skunicode/include/SkUnicode.h"

#include <utility>

// 텍스트 모양 잡기를 켠 구성이다 (`LUIL_ENABLE_TEXT_SHAPING`). 이 파일이 HarfBuzz와
// SkUnicode를 끌어온다.
namespace luil {
    namespace {
        [[nodiscard]] sk_sp<SkShapers::Factory> make_text_shaping_factory()
        {
            // 플랫폼 ICU를 먼저 열고, fallback을 켠 구성은 없을 때 libgrapheme을 쓴다.
            // 둘 다 없으면 null인 SkUnicode를 HarfBuzz에 넘기지 않고 primitive로 물러선다.
            sk_sp<SkUnicode> unicode { SkShapers::BestAvailableUnicode() };
            if (unicode == nullptr)
                return SkShapers::Primitive::Factory();
            return sk_make_sp<SkShapers::HarfbuzzFactory>(std::move(unicode));
        }
    }

    sk_sp<SkShapers::Factory> text_shaping_factory()
    {
        static const sk_sp<SkShapers::Factory> factory { make_text_shaping_factory() };
        return factory;
    }
}

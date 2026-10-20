#include "ui/text_shaping.h"

#include "modules/skshaper/include/SkShaper_factory.h"

// 텍스트 모양 잡기를 끈 구성이다. SVG·Lottie 모듈의 기본(primitive)을 쓰라고 답한다.
namespace luil {
    sk_sp<SkShapers::Factory> text_shaping_factory()
    {
        return nullptr;
    }
} // namespace luil

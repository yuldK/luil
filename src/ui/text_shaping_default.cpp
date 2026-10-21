#include "ui/text_shaping.h"

#include "modules/skshaper/include/SkShaper_factory.h"

// 텍스트 모양 잡기를 끈 구성이다. UI는 기존 그리기를, SVG·Lottie는 모듈의 기본(primitive)을 쓴다.
namespace luil {
    sk_sp<SkShapers::Factory> text_shaping_factory()
    {
        return nullptr;
    }
} // namespace luil

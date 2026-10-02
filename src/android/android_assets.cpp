#include "luil/text/fonts.h"

#include <cstddef>
#include <cstdint>
#include <span>

// 빌드가 assets/codicons/codicon.ttf에서 만든 바이트 배열이다 (cmake/embed_binary.cmake).
// Android는 실행 파일 resource가 없어, 글꼴을 라이브러리 안에 싣는다. 앱이 APK assets에
// 따로 넣는 단계가 없고, 나중에 iOS도 같은 방식을 쓴다 (docs/android-port-plan.md 3단계).
extern const std::uint8_t luil_codicon_font[];
extern const std::size_t luil_codicon_font_size;

namespace luil {
    sk_sp<SkTypeface> load_codicon_typeface()
    {
        return load_typeface_bytes(std::span<const std::uint8_t> { luil_codicon_font, luil_codicon_font_size });
    }
} // namespace luil

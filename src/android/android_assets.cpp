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
    // 프로세스에 하나만 만든다. 렌더러는 창이 생길 때마다(홈에서 돌아올 때마다) 새로 서는데,
    // 그때마다 만들면 렌더러가 사라진 뒤에도 풀리지 않아, 오갈 때마다 Native Heap이 글꼴
    // 바이트의 복사본(149KB)만큼 늘었다.
    sk_sp<SkTypeface> load_codicon_typeface()
    {
        static const sk_sp<SkTypeface> typeface { load_typeface_bytes(std::span<const std::uint8_t> { luil_codicon_font, luil_codicon_font_size }) };
        return typeface;
    }
} // namespace luil

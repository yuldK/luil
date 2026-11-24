#include "host/fail_fast.h"
#include "luil/text/fonts.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

// 빌드가 assets/codicons/codicon.ttf에서 만든 바이트 배열이다 (cmake/embed_binary.cmake).
// macOS도 Android처럼 글꼴을 라이브러리 안에 싣는다. 앱 번들에 자원을 따로 넣는 단계가 없다.
extern const std::uint8_t luil_codicon_font[];
extern const std::size_t luil_codicon_font_size;

namespace luil {
    // 프로세스에 하나만 만든다. 렌더러가 다시 설 때마다 글꼴 바이트를 다시 복사하지 않는다.
    sk_sp<SkTypeface> load_codicon_typeface()
    {
        static const sk_sp<SkTypeface> typeface { load_typeface_bytes(std::span<const std::uint8_t> { luil_codicon_font, luil_codicon_font_size }) };
        return typeface;
    }

    void platform_fail_fast() noexcept
    {
        // abort는 SIGABRT로 끝나 크래시 보고서에 호출 스택이 남는다.
        std::abort();
    }
}

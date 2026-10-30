#pragma once

#include "luil/text/font_style.h"

#include "include/core/SkRefCnt.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class SkFontMgr;
class SkTypeface;

namespace luil {
    // 플랫폼이 공급하는 글꼴 자원이다.
    //
    // 글꼴 registry의 cache·자물쇠·설정·대체 규칙은 플랫폼을 가리지 않는다. 플랫폼마다
    // 다른 것은 아래 넷뿐이라 이 interface 하나로 받는다 (docs/android-port-plan.md).
    // 구현은 thread-safe해야 한다 — registry가 자물쇠를 쥔 채 부르지만, 새로 만드는
    // 관리자는 자물쇠 밖에서도 만든다.
    class font_source
    {
    public:
        font_source() = default;
        font_source(const font_source&) = delete;
        font_source(font_source&&) = delete;
        font_source& operator=(const font_source&) = delete;
        font_source& operator=(font_source&&) = delete;
        virtual ~font_source() = default;

        // 시스템에 설치된 글꼴을 읽는 관리자다.
        // registry가 첫 조회 때 만들어 들고 있다가 `clear_font_caches`에서 놓는다.
        // 만들지 못하면 nullptr이고, 그때는 이름으로 찾는 글꼴과 대체가 모두 없다.
        [[nodiscard]] virtual sk_sp<SkFontMgr> make_system_manager() const = 0;

        // registry가 들고 있지 않는, 부를 때마다 새로 만드는 관리자다.
        // 파일·바이트에서 typeface를 만들고(앱 글꼴, codicon) 내장 UI 글꼴을 찾는다.
        // 시스템 목록의 무효화와 수명이 달라 따로 만든다.
        [[nodiscard]] virtual sk_sp<SkFontMgr> make_uncached_manager() const = 0;

        // 앱이 기본 UI 글꼴 목록을 정하지 않았거나 목록이 모두 없는 이름일 때 찾는 가족이다.
        [[nodiscard]] virtual std::u8string_view default_ui_family() const = 0;

        // 사용자 UI 언어의 BCP-47 태그를 OS에서 읽는다 (`ko-KR` 같은 것).
        // 읽지 못하면 빈 문자열이다. registry가 한 번만 부른다.
        [[nodiscard]] virtual std::string read_user_language() const = 0;
    };

    // 이 플랫폼의 글꼴 자원이다.
    // **플랫폼 계층이 하나를 정의한다** (Win32는 src/win32/win32_fonts.cpp). 등록 순서가
    // 없도록 링크로 묶는다 — 창 없이 registry를 쓰는 test와 input thread의 글자 폭
    // 측정이 어느 시작 순서에서든 같은 자원을 본다.
    [[nodiscard]] const font_source& platform_font_source() noexcept;

    // 설정이 고른 가족을 반영한다.
    // 빈 이름은 내장 글꼴을 뜻하고, 목록에 없는 이름도 조용히 내장 글꼴로 되돌아간다.
    // 값이 그대로면 아무 일도 하지 않으므로 매 frame 불러도 된다 (UI thread).
    void set_configured_fonts(std::u8string_view ui_family, std::u8string_view code_family);
    // 지금 설정된 가족 이름이다. 잠시 바꾼 설정을 원래대로 되돌릴 때 쓴다.
    [[nodiscard]] std::u8string configured_ui_family();
    [[nodiscard]] std::u8string configured_code_family();

    // 현재 설정된 글꼴이다.
    // 그리기 thread와 입력 thread가 같은 값을 봐야 caret 자리와
    // 글자 그림이 어긋나지 않으므로 registry를 공유한다.
    // 앱 상태가 아니라 OS 자원 cache라 단일 소유 규칙 밖이다.
    [[nodiscard]] sk_sp<SkTypeface> configured_ui_typeface();
    [[nodiscard]] sk_sp<SkTypeface> configured_code_typeface();

    // 미리 보기용이다.
    // 빈 이름이면 내장 글꼴이고, 찾지 못하면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> family_typeface(std::u8string_view family);

    // 내장 UI 글꼴이다.
    // 앱이 정한 기본 UI 글꼴 목록(`set_ui_typeface_families`)을 앞에서부터 찾고, 모두
    // 없으면 플랫폼의 기본 가족이다. 찾지 못하면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> load_ui_typeface();

    // 대체 cache가 담아 두는 글자 수의 상한이다.
    // 넘으면 통째로 비운다 — 어느 글자를 버릴지 고르는 것(LRU)은 그 값어치만큼
    // 복잡하지 않다. 한 화면이 쓰는 글자 수는 이보다 훨씬 적어서, 이 상한에 닿는
    // 것은 "아주 많은 글자를 지나온 긴 세션" 하나뿐이다.
    inline constexpr std::size_t fallback_cache_limit { 4096 };

    // 글꼴 cache를 통째로 비운다.
    // 글꼴이 설치·삭제되면(Win32는 `WM_FONTCHANGE`) 지금 답이 낡은 것이 되므로 platform이 부른다.
    //  - **글꼴 관리자까지 놓는다.** DirectWrite 관리자는 만들 때 얻은 font collection을
    //    수명 내내 들고 있어, cache만 비우면 다시 해석한 답이 똑같이 낡아 있다.
    //    다시 만드는 것은 다음 조회 때다 — 목록을 새로 읽는 데 수백 ms까지 걸려
    //    무효화 안에서 곧바로 치를 수 없다.
    //  - 비워도 **이미 건네진 typeface는 살아 있다.** 대체 조회가 `sk_sp`를 돌려주기
    //    때문이고, 그 계약이 이 함수를 세울 수 있게 한 것이다.
    //    DirectWrite typeface는 관리자로 되돌아가는 참조를 들지 않아 관리자가 바뀌어도 그대로다.
    void clear_font_caches();

    // 지금 대체 cache에 담긴 글자 수다.
    // 상한과 비우기를 test가 확인한다.
    [[nodiscard]] std::size_t fallback_cache_size();

    // 글꼴 관리자를 지금까지 몇 번 만들었는지다.
    // 무효화가 관리자까지 놓았는지는 밖에서 볼 수 없어 test가 이 값으로 잰다.
    //  - 세기만 한다. 이 함수가 관리자를 만들지는 않는다.
    [[nodiscard]] std::size_t font_manager_generation();

    // 사용자 UI 언어의 BCP-47 태그다 (`ko-KR` 같은 것).
    // 읽지 못하면 빈 값이다.
    //
    // 대체 글꼴을 고를 때 **자형을 가르는 값**이라 여기 있다 — 한자는 한국어·
    // 일본어·중국어에서 모양이 다르고, 언어를 주지 않으면 시스템이 임의로 고른다.
    // 한 번 읽어 두고, host가 언어가 바뀌었다고 알릴 때만 다시 읽는다.
    [[nodiscard]] std::string user_ui_language();
    // 읽어 둔 언어와 그 언어로 고른 대체 글꼴을 거둔다. 다음 조회가 다시 읽는다.
    //  - Android는 시스템 언어가 바뀌면 프로세스를 남긴 채 Activity만 다시 세운다.
    void refresh_user_ui_language();

    // 고른 글꼴에 없는 글자를 대신 그릴 typeface다.
    // `Cascadia Code`에 한글이 없고 어느 코드 글꼴에도 이모지가
    // 없으므로, 대체가 없으면 그 자리는 빈 네모가 된다.
    // 시스템이 고른 결과를 codepoint별로 cache한다.
    //  - 매 frame 시스템에 물으면 비싸다.
    //    찾지 못하면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> fallback_typeface(char32_t codepoint);
    // 굵기·기울임을 맞춰 고른 대체 typeface다. 굵은 글 속 한글이 Bold 대체 face로 그려진다.
    //  - cache 키는 (글자, 굵기, 기울임)이다. 찾은 face가 모양을 다 내지 못하면 메움은 부르는 쪽이 건다 (`use_face`).
    [[nodiscard]] sk_sp<SkTypeface> fallback_typeface(char32_t codepoint, font_style style);

    // 굵기·기울임을 건 face와 그 face가 다 내지 못한 몫의 메움이다.
    struct styled_typeface
    {
        sk_sp<SkTypeface> typeface {};
        bool embolden { false };
        bool skew { false };
    };

    // `base`를 `style`로 그릴 face다 (docs/font-style-design.md).
    // face가 어디서 왔는지(출처)에 따라 묻는 곳이 다르다.
    //  - 시스템 가족: 글꼴 관리자에 (가족, 굵기, 기울임)으로 묻는다. 등록부가 내준 face만 이 출처다.
    //  - 앱이 등록한 가족(`register_font_family`): 그 가족의 face 중 가장 가까운 것이다.
    //  - 그 밖(파일·바이트로 읽은 face): 그 face 그대로다. 시스템에 같은 이름이 깔려 있어도 묻지 않는다 —
    //    이름으로 찾으면 앱 글꼴이 조용히 시스템 글꼴로 바뀐다.
    //  고른 face에 `wght` 축이 있으면 요청한 굵기로 clone한다. 그래도 모자라면 덧칠·눕힘을 표시한다.
    //  - 기본 모양(regular·upright)이면 `base`를 그대로 돌려주고 자물쇠를 쥐지 않는다 (매 frame 그리기의 대부분이다).
    //  - 결과를 (base, 굵기, 기울임)으로 cache한다. clone을 매번 새로 만들면 typeface id가 frame마다 바뀌어 배치 cache가
    //    한 번도 맞지 않는다.
    [[nodiscard]] styled_typeface style_typeface(SkTypeface* base, font_style style);

    // 굵기 해석 cache에 담긴 항목 수다. 상한과 비우기를 test가 확인한다.
    [[nodiscard]] std::size_t styled_cache_size();
}

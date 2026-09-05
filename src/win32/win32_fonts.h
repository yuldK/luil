#pragma once

#include "include/core/SkRefCnt.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class SkTypeface;

namespace luil::win32 {
    // 시스템에 설치된 글꼴 가족 이름을 이름 순으로 돌려준다.
    // GDI font manager를 쓰며, 만들지 못하면 빈 목록이다.
    // OS 호출이라 UI thread에서 한 번만 부른다.
    [[nodiscard]] std::vector<std::u8string> installed_font_families();

    // 설정이 고른 가족을 반영한다.
    // 빈 이름은 내장 글꼴을 뜻하고, 목록에 없는 이름도 조용히 내장 글꼴로 되돌아간다.
    // 값이 그대로면 아무 일도 하지 않으므로 매 frame 불러도 된다 (UI thread).
    void set_configured_fonts(std::u8string_view ui_family, std::u8string_view code_family);

    // 현재 설정된 글꼴이다.
    // 그리기 thread와 입력 thread가 같은 값을 봐야 caret 자리와
    // 글자 그림이 어긋나지 않으므로 registry를 공유한다.
    // 앱 상태가 아니라 OS 자원 cache라 단일 소유 규칙 밖이다.
    [[nodiscard]] sk_sp<SkTypeface> configured_ui_typeface();
    [[nodiscard]] sk_sp<SkTypeface> configured_code_typeface();

    // 미리 보기용이다.
    // 빈 이름이면 내장 글꼴이고, 찾지 못하면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> family_typeface(std::u8string_view family);

    // 대체 cache가 담아 두는 글자 수의 상한이다.
    // 넘으면 통째로 비운다 — 어느 글자를 버릴지 고르는 것(LRU)은 그 값어치만큼
    // 복잡하지 않다. 한 화면이 쓰는 글자 수는 이보다 훨씬 적어서, 이 상한에 닿는
    // 것은 "아주 많은 글자를 지나온 긴 세션" 하나뿐이다.
    inline constexpr std::size_t fallback_cache_limit { 4096 };

    // 글꼴 cache를 통째로 비운다.
    // 글꼴이 설치·삭제되면(`WM_FONTCHANGE`) 지금 답이 낡은 것이 되므로 platform이 부른다.
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
    // OS 설정이라 프로세스가 도는 동안 바뀌지 않아 한 번만 읽는다.
    [[nodiscard]] std::string_view user_ui_language();

    // 고른 글꼴에 없는 글자를 대신 그릴 typeface다.
    // `Cascadia Code`에 한글이 없고 어느 코드 글꼴에도 이모지가
    // 없으므로, 대체가 없으면 그 자리는 빈 네모가 된다.
    // 시스템이 고른 결과를 codepoint별로 cache한다.
    //  - 매 frame DirectWrite에 물으면 비싸다.
    //    찾지 못하면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> fallback_typeface(char32_t codepoint);
} // namespace luil::win32

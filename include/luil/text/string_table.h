#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace luil {
    // 화면 글을 언어마다 다른 문장으로 고르는 표다 (docs/i18n-design.md).
    //  - 언어마다 JSON 파일 하나를 두고 빌드 때 생성기(`luil_add_string_table`)가 이 형으로 된 정적 표 헤더를 짓는다.
    //  - 실행 중에는 그 표에서 고른 언어의 문장을 찾고 자리 값(`{이름}`)과 복수형을 채운다. 표를 실행 중에 읽거나 다시 읽지 않는다.
    //  - 숫자·날짜·목록 서식은 없다. 수는 십진 숫자 글자로만 짓는다.

    // CLDR 복수 범주다. 1단계 규칙이 내는 것은 one과 other뿐이다.
    //  - 값은 `string_message::forms`의 칸 차례다.
    enum class plural_category : std::uint8_t
    {
        zero,
        one,
        two,
        few,
        many,
        other,
    };
    // 언어의 복수 규칙이다. JSON 머리의 "plural" 값과 같은 이름이다.
    enum class plural_rule : std::uint8_t
    {
        other,  // 늘 other다 (ko·ja·zh 같은 언어)
        one,  // 절댓값이 1이면 one, 아니면 other다 (en 같은 언어)
    };
    // 규칙이 수에 고르는 범주다. 음수는 절댓값으로 본다 (CLDR의 n).
    [[nodiscard]] plural_category plural_category_for(plural_rule rule, std::int64_t count) noexcept;

    // 문장 하나다. 범주 차례로 둔다. 복수형이 아니면 other 칸만 찬다.
    struct string_message
    {
        std::array<std::u8string_view, 6> forms {};
    };
    struct string_table_language
    {
        std::u8string_view tag {};  // BCP 47 (ko, en, pt-BR)
        plural_rule plural { plural_rule::other };
        std::span<const string_message> messages {};  // 키 차례
    };
    // 생성 헤더가 정적 저장소에 짓는 표다. 첫 언어가 기본 언어다. 모든 언어의 키 차례가 같다.
    //  - 생성기는 키를 이름 차례로 둔다. 손으로 지은 표도 받는다 (`find`는 차례를 가정하지 않는다).
    struct string_table
    {
        std::span<const std::u8string_view> keys {};
        std::span<const string_table_language> languages {};
    };

    // 자리 값 하나다. 수는 십진 숫자 글자로 짓는다 (자릿점 없음, 음수는 '-').
    //  - `count`라는 이름의 수가 복수형의 꼴을 고른다.
    struct string_argument
    {
        std::u8string_view name {};
        std::variant<std::u8string_view, std::int64_t> value {};
    };

    // 표 하나와 고른 언어다. 값이라 앱이 상태로 쥐고 언어가 바뀌면 다시 고른다.
    //  - 표를 가리키기만 한다. 표는 정적 저장소라(생성 헤더) 수명 걱정이 없다. 손으로 지은 표는 조회보다 오래 살아야 한다.
    //  - 기본값은 빈 조회다. 모든 글이 빈 글이다.
    class string_lookup
    {
    public:
        string_lookup() noexcept = default;
        // 범위 밖 언어 차례는 기본 언어(0)다.
        string_lookup(const string_table& table, std::size_t language) noexcept;
        // 고른 언어의 태그다. 빈 조회는 빈 글이다.
        [[nodiscard]] std::u8string_view tag() const noexcept;
        // 자리 값을 채우지 않은 문장이다. 복수형이면 other 꼴이다. 범위 밖 키는 빈 글이다. view는 정적 저장소다.
        [[nodiscard]] std::u8string_view text(std::size_t key) const noexcept;
        // 자리 값을 채운 문장이다. 복수형이면 `count` 인자(정수)로 꼴을 고른다.
        //  - 인자에 없는 이름은 `{이름}`을 그대로 남겨 개발자가 화면에서 보게 한다. `{{`·`}}`는 글자 `{`·`}`다.
        //  - `count`가 없거나 글이면 other 꼴이다. 고른 범주의 꼴이 비면 other 꼴이다.
        [[nodiscard]] std::u8string format(std::size_t key, std::span<const string_argument> arguments) const;
        // 이름으로 키를 찾는다. 실행 중에 이름만 아는 쪽(마크업 따위)이 쓴다.
        [[nodiscard]] std::optional<std::size_t> find(std::u8string_view name) const noexcept;

    private:
        const string_table* table_ { nullptr };
        std::size_t language_ { 0 };
    };

    // 선호 목록의 차례로 표의 언어를 고른다. 없으면 기본 언어다.
    //  - 대소문자와 `_`·`-`를 가리지 않는다. 태그마다 그대로 찾고, 끝 부분을 하나씩 떼어(`zh-Hant-TW`, `zh-Hant`, `zh`) 찾은 뒤
    //    다음 태그로 간다.
    [[nodiscard]] string_lookup select_strings(const string_table& table, std::span<const std::u8string> preferences) noexcept;

    // 라이브러리 기본 문구가 따를 언어의 선호 목록이다. 어느 thread에서나 부른다. 그 뒤에 짓는 요소부터 바뀐다.
    //  - 정한 적이 없거나 빈 목록이면 한국어다. 라이브러리 표에 없는 언어만 든 목록도 한국어다.
    //  - 프로세스에 하나다. 앱의 표는 앱이 같은 목록으로 `select_strings`를 불러 고른다.
    void set_text_languages(std::span<const std::u8string> preferences);
    // 앱이 정한 목록이다. 정한 적이 없으면 빈 값이다. 잠금 아래에서 사본을 지으므로 문구 함수마다 부르지 않는다.
    [[nodiscard]] std::optional<std::vector<std::u8string>> text_languages();
}

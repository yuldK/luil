#pragma once

#include "luil/theme/ui_style.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 스타일 JSON 글을 실행 중에 읽는다 (docs/style-sheet-design.md).
// 빌드 때 LUIL_STYLE로 굽는 파일과 같은 스키마, 같은 판정, 같은 대신 값이다 — 같은 파일은 굽든 읽든 같은 `ui_style`이다.
//  - 키 목록은 생성기와 같은 데이터 파일(cmake/style_keys.cmake)에서 나온 표다. 키가 늘어도 이 헤더는 그대로다.
//  - 파서는 엄한 JSON(RFC 8259)이다. 생성기(CMake의 JSON 읽기)가 너그럽게 받는 글(주석, 끝 쉼표, 뿌리 뒤의 글,
//    `+1`·`01` 같은 숫자, 겹친 키)은 여기서 막는다. 이것이 "같은 판정"의 유일한 예외 묶음이다.
//  - 파일을 읽지 않는다. 플랫폼을 모르는 순수 함수라 어느 thread에서나 부른다.
namespace luil {
    enum class style_error_kind
    {
        // JSON 문법이 깨졌다. `offset`이 바이트 자리다.
        syntax,
        // 뿌리나 칸(`dark`, `dark.caption`, `tabBar` …)이 객체가 아니다. `path`가 그 칸이다.
        not_object,
        missing_key,
        unknown_key,
        // 한 객체에 같은 키가 두 번이다. 생성기는 뒤의 것을 남겨 조용히 지나가므로 여기서만 막는다.
        duplicate_key,
        // 숫자 자리에 글, 이름 자리에 숫자다.
        wrong_type,
        // `#rrggbb`·`#rrggbbaa`가 아니다.
        invalid_color,
        // 범위(tone 0~1, 치수 0~1000) 밖이다. 음수는 `-0.0`까지 막는다.
        out_of_range,
        // 허용 값 목록에 없는 이름이다.
        invalid_value,
        // 중첩이 8단을 넘는다. 값을 짓기 전에 막는다.
        too_deep,
    };

    struct style_error
    {
        style_error_kind kind { style_error_kind::syntax };
        // 오류가 난 객체의 자리다. 생성기와 같은 글이다("dark", "dark.caption", "metrics.fontWeights"). 뿌리는 "root"다.
        std::u8string path {};
        // 문제의 키다. 키가 없는 오류(not_object, syntax, too_deep)는 비어 있다.
        std::u8string key {};
        // 받은 값의 글이다(색·숫자·이름). 64바이트에서 자른다(글자 경계에서).
        //  - 숫자는 생성기가 보는 글로 적는다. 정수는 그대로, 실수는 유효 17자리이고 정수 값이면 `.0`을 붙인다
        //    (`1e2` → `100.0`).
        //  - wrong_type·not_object는 받은 값의 JSON 형 이름이다(`STRING`, `NUMBER`, `OBJECT`, `ARRAY`, `BOOLEAN`, `NULL`).
        std::u8string value {};
        // syntax일 때만 쓴다. 0부터 센 바이트 자리이고, 글이 도중에 끝났으면 글 길이다.
        std::size_t offset { 0 };

        [[nodiscard]] bool operator==(const style_error&) const = default;
    };

    // 생성기의 오류 글과 같은 한 줄이다("Unknown key in dark: windowBackgrund"). 개발자용 영문 진단이다.
    //  - syntax·duplicate_key·too_deep은 생성기에 짝이 없는 글이다.
    [[nodiscard]] std::u8string describe(const style_error& error);

    struct style_parse_result
    {
        // 오류가 하나라도 있으면 비어 있다. 부르는 쪽은 쥔 스타일을 그대로 둔다.
        std::optional<ui_style> style {};
        // style이 있으면 비어 있다. 문법 오류와 too_deep은 그 하나뿐이고, 나머지는 끝까지 모은다.
        //  - 순서는 키 표 순서다(뿌리, dark, light, tones, metrics, fontWeights, 컴포넌트 칸). 한 객체 안에서는 낯선 키
        //    (JSON에 적힌 순서), 빠진 키, 값 오류 순이다 — 생성기가 멈추는 첫 오류가 이 목록에 있다.
        std::vector<style_error> errors {};

        [[nodiscard]] bool ok() const noexcept
        {
            return style.has_value();
        }
    };

    // 스타일 JSON 글(UTF-8)을 읽는다. 파일을 읽지 않는다. 전역 상태가 없어 어느 thread에서나 부른다.
    //  - 빠진 컴포넌트 칸(`tabBar`·`popup`·`text`)과 `metrics.fontWeights`, 칸 안의 빠진 키는 struct의 기본값이다.
    //    이 빌드가 구운 값(`default_ui_style()`)이 아니다 — 생성기와 같다.
    //  - 빠진 선택 중립 역할은 같은 테마의 대신할 키 값이다(구문 색 여섯은 `primaryForeground`).
    [[nodiscard]] style_parse_result parse_ui_style(std::u8string_view json);
}

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// 화면 마크업이다 (docs/markup-design.md, docs/markup-binding-design.md).
// 앱이 실행 중에 마크업 글을 읽어 불변인 틀(`ui_markup_document`)로 두고, frame마다 그 틀을 자기 상태로 펼친다.
//  - 문법은 XML 1.0의 엄한 부분집합이다. 받는 글은 모두 바른 XML이고, 받지 않는 구문(DTD·CDATA·이름 공간·처리 지시)은
//    조용히 넘기지 않고 오류다. 바깥 엔티티와 넣기가 없어 읽기는 파일·주소를 열지 않는다.
//  - 이름·enum·수·자리·id 꼴은 읽을 때 모두 확인한다. 펼칠 때(`build_ui_markup`, 바인딩 문서)는 값을 찾고 요소를 짓기만 한다.
//  - 이름은 글 본문을 그리는 `markdown_*`과 섞이지 않도록 늘 `ui_markup`으로 시작한다.
namespace luil {
    // 읽기와 펼치기의 한도다. 마크업 글을 앱 밖에서 온 신뢰하지 않는 파일로 보고 정한다.
    //  - 넘으면 `too_large` 하나를 남기고 멈춘다. 읽기는 깊이를 읽는 자리에서 세어 재귀가 깊어지기 전에 멈춘다.
    struct ui_markup_limits
    {
        // 글의 바이트 수다 (맨 앞의 BOM 포함). 넘으면 읽지 않는다.
        std::size_t max_bytes { std::size_t { 1 } * 1024 * 1024 };
        // 요소 중첩이다. 뿌리가 1단이고 `for`·`if`·`unless`도 한 단이다.
        std::size_t max_depth { 64 };
        // 틀의 요소 수다 (구조 요소 포함, 글과 주석은 세지 않는다).
        std::size_t max_nodes { 10000 };
        // 한 번 펼칠 때 짓는 element 수다. 틀이 쥐고 펼칠 때 쓴다.
        std::size_t max_built { 50000 };
    };

    // 읽은 틀이다. 이름·속성·수·자리를 모두 확인하고 enum과 수를 C++ 값으로 바꿔 둔 불변 값이다. 속은 비공개다.
    //  - 불변이라 thread 사이에 `shared_ptr<const ui_markup_document>`로 나눠 쥔다.
    class ui_markup_document;

    enum class ui_markup_error_kind
    {
        // 읽을 때 (parse_ui_markup)
        // 닫지 않은 태그, 짝이 틀린 닫기, 따옴표 없는 값, 모르는 엔티티, 깨진 UTF-8, XML 글자 밖의 글자, 뿌리 둘, 뿌리 뒤의 글이다.
        syntax,
        // DTD, 처리 지시, CDATA, 이름 공간(이름 속의 `:`와 `xmlns`), UTF-8 밖의 encoding, 1.0 밖의 version이다.
        unsupported,
        // max_bytes·max_depth·max_nodes를 넘었다.
        too_large,
        unknown_element,
        unknown_attribute,
        duplicate_attribute,
        // 단추·체크의 id, slot의 name, for의 each·as·key, if·unless의 test다.
        missing_attribute,
        // 허용 이름 밖, 수가 아님, 범위 밖, id·이름·바인딩의 꼴, 구체 색, 함께 못 쓰는 자리 속성이다.
        invalid_value,
        // 받지 않는 자식이나 글이다 (panel의 둘째 자식, label 안의 요소, 뿌리가 된 구조 요소).
        invalid_child,
        // 원하는 크기를 답하지 않는 요소나 slot에 stack 안의 자리가 없다.
        missing_length,
        // 같은 id를 두 번 적었다.
        duplicate_id,
        // 펼칠 때 (build_ui_markup, docs/markup-binding-design.md)
        // 값 표에 없는 이름이다.
        unknown_value,
        // 형이 맞지 않는 값이다 (글 자리에 수, each에 글).
        wrong_value_type,
        // make_action이 빈 값을 돌려주었다.
        unknown_action,
        // make_slot이 nullptr를 돌려주었다.
        unknown_slot,
        // kind 이름이 이름표에 없다.
        unknown_kind,
        // 한 반복 안에서 key가 겹친다.
        duplicate_key,
        // max_built를 넘었다.
        too_many_built,
    };

    struct ui_markup_error
    {
        ui_markup_error_kind kind { ui_markup_error_kind::syntax };
        // 1부터다. 펼칠 때 오류는 그 속성이 적힌 자리다.
        std::size_t line { 0 };
        // 1부터, 줄 처음부터 센 코드 포인트다. 맨 앞의 BOM은 세지 않는다.
        std::size_t column { 0 };
        // 0부터 센 바이트 자리다 (BOM 포함).
        std::size_t offset { 0 };
        // 요소 자리다 ("stack/stack[2]/textButton"). 뿌리부터 요소 이름을 `/`로 잇고, 같은 이름의 형제가 여럿이면 `[n]`(1부터)을 붙인다.
        //  - 구조 요소(`for`·`if`·`unless`)도 한 단이다.
        //  - `invalid_child`는 그 자식을 받지 않은 부모의 자리다. 뿌리가 될 수 없는 요소면 비어 있다.
        //  - `syntax`·`unsupported`·`too_large`는 비어 있다. 글이 다 읽히기 전이라 형제를 다 모른다. 줄·열이 자리를 말한다.
        std::u8string path {};
        // 문제의 요소·속성·값·사건 이름이다.
        //  - `syntax`는 걸린 이름(닫지 않았거나 짝이 틀린 요소, 모르는 엔티티, 값이 깨진 속성)이고 없으면 비어 있다.
        //  - `too_large`는 넘은 한도의 필드 이름(`max_bytes`·`max_depth`·`max_nodes`·`max_built`)이다.
        //  - `invalid_child`는 받지 않은 자식 요소의 이름이고, 글이면 비어 있다(글은 `value`).
        std::u8string name {};
        // 받은 글이다. 64바이트에서 글자 경계로 자른다.
        //  - `syntax`는 그 자리에서 만난 것이다 (짝이 틀린 닫기의 이름, `&nbsp;`, 깨진 바이트의 `0xC3`, 막는 글자의 `U+0001`).
        //  - `too_large`는 한도의 값이다.
        std::u8string value {};

        [[nodiscard]] bool operator==(const ui_markup_error&) const = default;
    };

    // "4:21 Unknown attribute on label: colr" 꼴의 영문 한 줄이다. 개발자용 진단이라 앱의 언어를 따르지 않는다.
    [[nodiscard]] std::u8string describe(const ui_markup_error& error);

    struct ui_markup_parse_result
    {
        // 오류가 하나라도 있으면 비어 있다. 부르는 쪽은 쥔 틀을 그대로 둔다.
        std::shared_ptr<const ui_markup_document> document {};
        // document가 있으면 비어 있다. 순서는 글의 자리(offset) 순이다.
        //  - `syntax`·`unsupported`·`too_large`는 그 하나뿐이다. 앞서 모은 어휘 오류도 남기지 않는다.
        //  - 나머지는 끝까지 모은다.
        std::vector<ui_markup_error> errors {};

        [[nodiscard]] bool ok() const noexcept
        {
            return document != nullptr;
        }
    };

    // 마크업 글(UTF-8)을 읽는다. 파일을 읽지 않는다. 전역 상태가 없어 어느 thread에서나 부른다.
    [[nodiscard]] ui_markup_parse_result parse_ui_markup(std::u8string_view text, const ui_markup_limits& limits = {});
}

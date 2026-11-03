#include "luil/ui/ui_markup.h"

#include "ui/ui_markup_document.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// 화면 마크업의 엄한 XML 읽기다 (docs/markup-design.md 규칙 절).
// 받는 글은 모두 바른 XML이고, 막는 글은 종류 하나와 그 자리(줄·열·바이트)로 막힌다. 신뢰하지 않는 글을 받는 자리라
// 잘린 글, 깊은 중첩, 큰 글, 망가뜨린 글이 멈추거나 넘치지 않는지도 본다.
namespace {
    using luil::ui_markup_error_kind;

    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] std::u8string widen(const std::string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] std::string described(const luil::ui_markup_parse_result& result)
    {
        std::string text {};
        for (const luil::ui_markup_error& error : result.errors)
            text += narrow(luil::describe(error)) + "\n";
        return text;
    }

    // 오류가 꼭 하나인 글이다.
    [[nodiscard]] luil::ui_markup_error only_error(const std::u8string_view text, const luil::ui_markup_limits& limits = {})
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text, limits) };
        INFO(narrow(text));
        INFO(described(result));
        REQUIRE(result.ok() == false);
        REQUIRE(result.errors.size() == 1);
        return result.errors.front();
    }

    void require_error(const std::u8string_view text, const ui_markup_error_kind kind, const std::size_t line, const std::size_t column)
    {
        const luil::ui_markup_error error { only_error(text) };
        INFO(narrow(text));
        INFO(narrow(luil::describe(error)));
        REQUIRE(error.kind == kind);
        REQUIRE(error.line == line);
        REQUIRE(error.column == column);
    }

    [[nodiscard]] const luil::ui_markup_document& document_of(const luil::ui_markup_parse_result& result)
    {
        INFO(described(result));
        REQUIRE(result.ok());
        REQUIRE(result.errors.empty());
        return *result.document;
    }

    // 노드의 그 속성이 적은 글이다.
    [[nodiscard]] std::u8string literal_text(const luil::ui_markup_node& node, const std::string_view name)
    {
        const luil::ui_markup_attribute* found { nullptr };
        for (const luil::ui_markup_attribute& attribute : node.attributes)
        {
            if (attribute.row != nullptr && attribute.row->name == name)
                found = &attribute;
        }
        INFO("attribute " << name);
        REQUIRE(found != nullptr);
        REQUIRE(found->bound() == false);
        return std::get<std::u8string>(found->literal);
    }

    // 그 깊이로 겹친 stack이다.
    [[nodiscard]] std::u8string nested(const std::size_t depth)
    {
        std::u8string text {};
        for (std::size_t index { 0 }; index < depth; ++index)
            text += u8"<stack>";
        for (std::size_t index { 0 }; index < depth; ++index)
            text += u8"</stack>";
        return text;
    }

    // 바른 견본이다. 선언·주석·엔티티·숫자 참조·한글·작은따옴표·빈 요소가 다 든다. 끝은 뿌리를 닫는 태그다.
    constexpr std::u8string_view sample {
        u8"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        u8"<!-- 누른 횟수를 세는 칸 -->\n"
        u8"<stack id=\"counter\" direction=\"column\" spacing=\"12\" padding=\"24\">\n"
        u8"  <label id=\"count\" text=\"{countText}\" fontSize=\"14\" color=\"primary\"/>\n"
        u8"  <stack direction=\"row\" spacing=\"8\" length=\"30\">\n"
        u8"    <textButton id=\"more\" text='하나 &amp; 더' action=\"increment\"/>\n"
        u8"    <check id=\"list\" label=\"목록&#10;보기\" checked=\"{showList}\" action=\"toggleList\"/>\n"
        u8"  </stack>\n"
        u8"  <if test=\"{showList}\">\n"
        u8"    <for each=\"{items}\" as=\"item\" key=\"{item.key}\">\n"
        u8"      <textButton id=\"item\" text=\"{item.title}\" visual=\"link\" action=\"select\" argument=\"{item.key}\"/>\n"
        u8"    </for>\n"
        u8"  </if>\n"
        u8"  <slot name=\"search\" length=\"28\"/>\n"
        u8"</stack>"
    };

    // 결정적인 의사 난수다. 시험이 돌 때마다 같은 글을 망가뜨린다.
    struct sequence
    {
        std::uint32_t state { 0x12345678u };

        std::uint32_t next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return state >> 8;
        }
    };

    // 망가뜨린 글이 막혔으면 오류가 자리 순이고 자리가 글 안이다. 받았으면 오류가 없다.
    void require_well_formed_answer(const std::u8string_view text)
    {
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(text) };
        if (result.ok())
        {
            REQUIRE(result.errors.empty());
            REQUIRE(result.document->nodes.empty() == false);
            return;
        }
        REQUIRE(result.errors.empty() == false);
        for (std::size_t index { 0 }; index < result.errors.size(); ++index)
        {
            const luil::ui_markup_error& error { result.errors[index] };
            REQUIRE(error.line >= 1);
            REQUIRE(error.column >= 1);
            REQUIRE(error.offset <= text.size());
            REQUIRE(error.value.size() <= 64);
            REQUIRE(luil::describe(error).empty() == false);
            if (index > 0)
                REQUIRE(result.errors[index - 1].offset <= error.offset);
        }
    }
}

TEST_CASE("Markup reads elements, attributes, comments and entities", "[ui][markup][read]")
{
    const luil::ui_markup_parse_result result { luil::parse_ui_markup(sample) };
    const luil::ui_markup_document& document { document_of(result) };
    // 뿌리, label, 안쪽 stack, 단추, 체크, if, for, 반복 단추, slot이다. 주석과 공백은 노드가 아니다.
    REQUIRE(document.nodes.size() == 9);
    REQUIRE(document.nodes[0].tag == luil::ui_markup_tag::stack);
    REQUIRE(document.nodes[0].children.size() == 4);
    REQUIRE(narrow(document.nodes[0].id) == "counter");
    const luil::ui_markup_node& button { document.nodes[3] };
    REQUIRE(button.tag == luil::ui_markup_tag::text_button);
    REQUIRE(narrow(literal_text(button, "text")) == narrow(u8"하나 & 더"));
    const luil::ui_markup_node& check { document.nodes[4] };
    REQUIRE(narrow(literal_text(check, "label")) == narrow(u8"목록\n보기"));
    // 자리는 뿌리부터 요소 이름을 잇는다. 같은 이름의 형제가 여럿이면 `[n]`이다.
    REQUIRE(narrow(luil::ui_markup_path(document.nodes, 3)) == "stack/stack/textButton");
    REQUIRE(narrow(luil::ui_markup_path(document.nodes, 7)) == "stack/if/for/textButton");
    REQUIRE(document.limits.max_built == luil::ui_markup_limits {}.max_built);
}

TEST_CASE("Markup takes a BOM, an XML declaration and either quote", "[ui][markup][read]")
{
    REQUIRE(luil::parse_ui_markup(u8"\uFEFF<stack/>").ok());
    REQUIRE(luil::parse_ui_markup(u8"<?xml version='1.0'?><stack/>").ok());
    REQUIRE(luil::parse_ui_markup(u8"<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\" ?>\r\n<stack/>").ok());
    REQUIRE(luil::parse_ui_markup(u8"\uFEFF<?xml version=\"1.0\"?><!-- a - b --><stack/><!-- end -->\n").ok());
    REQUIRE(luil::parse_ui_markup(u8"<stack ></stack >").ok());

    const luil::ui_markup_parse_result quoted { luil::parse_ui_markup(u8"<label text='say \"hi\"' tooltip=\"it's\"/>") };
    const luil::ui_markup_document& document { document_of(quoted) };
    REQUIRE(narrow(literal_text(document.nodes[0], "text")) == "say \"hi\"");
    REQUIRE(narrow(literal_text(document.nodes[0], "tooltip")) == "it's");
}

TEST_CASE("Markup attribute values follow XML whitespace normalization", "[ui][markup][read]")
{
    // 값 속의 탭·LF·CR·CR LF는 공백 하나다. 숫자 참조로 적은 줄 바꿈과 탭은 그대로 남는다.
    const luil::ui_markup_parse_result result { luil::parse_ui_markup(u8"<label text=\"a\tb\nc\r\nd\re\" tooltip=\"x&#10;y&#9;z&#x41;&#65;\"/>") };
    const luil::ui_markup_document& document { document_of(result) };
    REQUIRE(narrow(literal_text(document.nodes[0], "text")) == "a b c d e");
    REQUIRE(narrow(literal_text(document.nodes[0], "tooltip")) == "x\ny\tzAA");

    // 다섯 엔티티와 그 밖의 글자 참조다.
    const luil::ui_markup_parse_result entities { luil::parse_ui_markup(u8"<label text=\"&amp;&lt;&gt;&quot;&apos;&#xAC00;&#x1F600;\"/>") };
    REQUIRE(narrow(literal_text(document_of(entities).nodes[0], "text")) == narrow(u8"&<>\"'가😀"));
}

TEST_CASE("Markup rejects malformed XML with one syntax error at its place", "[ui][markup][read]")
{
    // 닫지 않은 태그는 그 여는 태그를 가리킨다.
    require_error(u8"<stack>\n  <label text=\"a\"/>", ui_markup_error_kind::syntax, 1, 1);
    REQUIRE(narrow(only_error(u8"<stack>\n  <label text=\"a\"/>").name) == "stack");
    require_error(u8"<stack><stack>", ui_markup_error_kind::syntax, 1, 8);
    // 짝이 틀린 닫기는 그 닫는 태그다. name은 열린 요소, value는 받은 이름이다.
    require_error(u8"<stack></stak>", ui_markup_error_kind::syntax, 1, 8);
    const luil::ui_markup_error mismatched { only_error(u8"<stack></stak>") };
    REQUIRE(narrow(mismatched.name) == "stack");
    REQUIRE(narrow(mismatched.value) == "stak");
    REQUIRE(narrow(luil::describe(mismatched)) == "1:8 Syntax error in stack: stak");
    // 따옴표 없는 값, 등호 없는 속성, 속성 사이의 공백 없음, `/` 뒤의 공백, 이름 없는 태그다.
    require_error(u8"<label text=a/>", ui_markup_error_kind::syntax, 1, 13);
    require_error(u8"<label text \"a\"/>", ui_markup_error_kind::syntax, 1, 13);
    require_error(u8"<label text=\"a\"id=\"b\"/>", ui_markup_error_kind::syntax, 1, 16);
    require_error(u8"<stack/ >", ui_markup_error_kind::syntax, 1, 7);
    require_error(u8"< stack/>", ui_markup_error_kind::syntax, 1, 2);
    require_error(u8"<label text=\"abc", ui_markup_error_kind::syntax, 1, 8);
    require_error(u8"<label text=\"a<b\"/>", ui_markup_error_kind::syntax, 1, 15);
    // 모르는 엔티티와 홀로 선 `&`, `;`가 없는 참조다.
    require_error(u8"<label text=\"&nbsp;\"/>", ui_markup_error_kind::syntax, 1, 14);
    REQUIRE(narrow(only_error(u8"<label text=\"&nbsp;\"/>").value) == "&nbsp;");
    require_error(u8"<label text=\"a & b\"/>", ui_markup_error_kind::syntax, 1, 16);
    require_error(u8"<label text=\"&amp\"/>", ui_markup_error_kind::syntax, 1, 14);
    require_error(u8"<label text=\"&#65\"/>", ui_markup_error_kind::syntax, 1, 14);
    require_error(u8"<label text=\"&#;\"/>", ui_markup_error_kind::syntax, 1, 14);
    require_error(u8"<label text=\"&#x;\"/>", ui_markup_error_kind::syntax, 1, 14);
    // 뿌리가 없거나 둘이거나 뿌리 앞뒤에 글이 있다.
    require_error(u8"", ui_markup_error_kind::syntax, 1, 1);
    require_error(u8"  <!-- only -->  ", ui_markup_error_kind::syntax, 1, 18);
    require_error(u8"<stack/><stack/>", ui_markup_error_kind::syntax, 1, 9);
    require_error(u8"<stack/>\nx", ui_markup_error_kind::syntax, 2, 1);
    require_error(u8"x<stack/>", ui_markup_error_kind::syntax, 1, 1);
    // 주석 속 `--`, `-`로 끝나는 주석, 닫지 않은 주석, 글 속의 `]]>`다.
    require_error(u8"<!-- a -- b --><stack/>", ui_markup_error_kind::syntax, 1, 8);
    require_error(u8"<!-- a ---><stack/>", ui_markup_error_kind::syntax, 1, 8);
    require_error(u8"<stack><!-- a", ui_markup_error_kind::syntax, 1, 8);
    require_error(u8"<stack>]]></stack>", ui_markup_error_kind::syntax, 1, 8);
    // XML 선언은 맨 앞에만 오고 version이 먼저다. standalone은 yes·no다.
    require_error(u8" <?xml version=\"1.0\"?><stack/>", ui_markup_error_kind::syntax, 1, 2);
    require_error(u8"<?xml encoding=\"UTF-8\"?><stack/>", ui_markup_error_kind::syntax, 1, 7);
    require_error(u8"<?xml version=\"1.0\" standalone=\"maybe\"?><stack/>", ui_markup_error_kind::syntax, 1, 21);
    require_error(u8"<?xml?><stack/>", ui_markup_error_kind::syntax, 1, 1);
}

TEST_CASE("Markup rejects characters outside UTF-8 and the XML character range", "[ui][markup][read]")
{
    // 깨진 UTF-8이다: 홀로 선 이어짐 바이트, 과잉 인코딩, surrogate, U+10FFFF 밖, 잘린 끝.
    require_error(widen("<label text=\"\x80\"/>"), ui_markup_error_kind::syntax, 1, 14);
    REQUIRE(narrow(only_error(widen("<label text=\"\x80\"/>")).value) == "0x80");
    require_error(widen("<label text=\"\xC0\xAF\"/>"), ui_markup_error_kind::syntax, 1, 14);
    require_error(widen("<label text=\"\xED\xA0\x80\"/>"), ui_markup_error_kind::syntax, 1, 14);
    require_error(widen("<label text=\"\xF4\x90\x80\x80\"/>"), ui_markup_error_kind::syntax, 1, 14);
    require_error(widen("<stack/>\xE2\x82"), ui_markup_error_kind::syntax, 1, 9);
    // XML 글자 밖이다. 글로 적어도, 숫자 참조로 적어도 막는다 (U+0000, 제어 문자, surrogate, U+FFFE·FFFF, U+10FFFF 밖).
    require_error(widen("<label text=\"a\x01\"/>"), ui_markup_error_kind::syntax, 1, 15);
    REQUIRE(narrow(only_error(widen("<label text=\"a\x01\"/>")).value) == "U+0001");
    require_error(u8"<label text=\"\uFFFE\"/>", ui_markup_error_kind::syntax, 1, 14);
    require_error(u8"<label text=\"\uFFFF\"/>", ui_markup_error_kind::syntax, 1, 14);
    for (const std::u8string_view reference : { u8"&#0;", u8"&#1;", u8"&#x1F;", u8"&#xD800;", u8"&#xDFFF;", u8"&#xFFFE;", u8"&#xFFFF;", u8"&#x110000;", u8"&#99999999999;" })
    {
        const std::u8string text { u8"<label text=\"" + std::u8string { reference } + u8"\"/>" };
        require_error(text, ui_markup_error_kind::syntax, 1, 14);
        REQUIRE(narrow(only_error(text).value) == narrow(reference));
    }
    // 받는 글자다: 탭·LF·CR, U+D7FF, U+E000, U+FFFD, U+10000, U+10FFFF.
    REQUIRE(luil::parse_ui_markup(u8"<label text=\"&#9;&#xA;&#xD;&#xD7FF;&#xE000;&#xFFFD;&#x10000;&#x10FFFF;\uFFFD\"/>").ok());
}

TEST_CASE("Markup refuses DTD, CDATA, processing instructions and namespaces", "[ui][markup][read]")
{
    require_error(u8"<!DOCTYPE stack>\n<stack/>", ui_markup_error_kind::unsupported, 1, 1);
    REQUIRE(narrow(only_error(u8"<!DOCTYPE stack>\n<stack/>").name) == "DOCTYPE");
    require_error(u8"<!DOCTYPE stack [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><stack text=\"&x;\"/>", ui_markup_error_kind::unsupported, 1, 1);
    require_error(u8"<stack><![CDATA[x]]></stack>", ui_markup_error_kind::unsupported, 1, 8);
    require_error(u8"<![CDATA[x]]><stack/>", ui_markup_error_kind::unsupported, 1, 1);
    require_error(u8"<stack><?php echo 1 ?></stack>", ui_markup_error_kind::unsupported, 1, 8);
    REQUIRE(narrow(only_error(u8"<stack><?php echo 1 ?></stack>").name) == "php");
    require_error(u8"<?xml-stylesheet href=\"a.css\"?><stack/>", ui_markup_error_kind::unsupported, 1, 1);
    require_error(u8"<ui:stack/>", ui_markup_error_kind::unsupported, 1, 1);
    require_error(u8"<stack xlink:href=\"a\"/>", ui_markup_error_kind::unsupported, 1, 8);
    require_error(u8"<stack xmlns=\"urn:x\"/>", ui_markup_error_kind::unsupported, 1, 8);
    require_error(u8"<stack><!ELEMENT stack ANY></stack>", ui_markup_error_kind::syntax, 1, 8);
    // UTF-8 밖의 encoding과 1.0 밖의 version이다.
    require_error(u8"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><stack/>", ui_markup_error_kind::unsupported, 1, 21);
    const luil::ui_markup_error encoding { only_error(u8"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><stack/>") };
    REQUIRE(narrow(encoding.name) == "encoding");
    REQUIRE(narrow(encoding.value) == "ISO-8859-1");
    REQUIRE(narrow(luil::describe(encoding)) == "1:21 Unsupported markup: encoding=ISO-8859-1");
    require_error(u8"<?xml version=\"1.1\"?><stack/>", ui_markup_error_kind::unsupported, 1, 7);
}

TEST_CASE("Markup error positions count lines and code points", "[ui][markup][read]")
{
    // LF, CR LF, CR가 모두 한 줄 바꿈이다.
    require_error(u8"<stack>\n  <label colr=\"x\"/>\n</stack>", ui_markup_error_kind::unknown_attribute, 2, 10);
    require_error(u8"<stack>\r\n  <label colr=\"x\"/>\r\n</stack>", ui_markup_error_kind::unknown_attribute, 2, 10);
    require_error(u8"<stack>\r  <label colr=\"x\"/>\r</stack>", ui_markup_error_kind::unknown_attribute, 2, 10);
    require_error(u8"<stack>\r\n\r\n\n\r<label colr=\"x\"/></stack>", ui_markup_error_kind::unknown_attribute, 5, 8);
    // 열은 코드 포인트이고 offset은 바이트다. 한글 한 글자는 열 하나, 3바이트다.
    const luil::ui_markup_error hangul { only_error(u8"<stack>\n  <label text=\"한글 글자\" colr=\"x\"/>\n</stack>") };
    REQUIRE(hangul.kind == ui_markup_error_kind::unknown_attribute);
    REQUIRE(hangul.line == 2);
    REQUIRE(hangul.column == 23);
    REQUIRE(hangul.offset == 38);
    REQUIRE(narrow(hangul.path) == "stack/label");
    REQUIRE(narrow(luil::describe(hangul)) == "2:23 Unknown attribute on label: colr");
    // 문법 오류도 같다. 이모지(4바이트)도 열 하나다.
    const luil::ui_markup_error emoji { only_error(u8"<label text=\"😀가\" tooltip=\"&bad;\"/>") };
    REQUIRE(emoji.kind == ui_markup_error_kind::syntax);
    REQUIRE(emoji.column == 27);
    REQUIRE(emoji.offset == 31);
    // 맨 앞의 BOM은 열로 세지 않지만 offset에는 든다.
    const luil::ui_markup_error bom { only_error(u8"\uFEFF<stack colr=\"x\"/>") };
    REQUIRE(bom.column == 8);
    REQUIRE(bom.offset == 10);
}

TEST_CASE("Markup limits stop with a single too_large", "[ui][markup][read][limits]")
{
    luil::ui_markup_limits limits {};
    limits.max_bytes = 16;
    REQUIRE(luil::parse_ui_markup(u8"<stack></stack> ", limits).ok());
    const luil::ui_markup_error bytes { only_error(u8"<stack></stack>  ", limits) };
    REQUIRE(bytes.kind == ui_markup_error_kind::too_large);
    REQUIRE(narrow(bytes.name) == "max_bytes");
    REQUIRE(narrow(bytes.value) == "16");
    REQUIRE(bytes.line == 1);
    REQUIRE(bytes.column == 1);
    REQUIRE(bytes.offset == 0);
    REQUIRE(narrow(luil::describe(bytes)) == "1:1 Markup exceeds max_bytes (16)");

    // 기본 깊이는 64다. 뿌리가 1단이다.
    REQUIRE(luil::parse_ui_markup(nested(64)).ok());
    const luil::ui_markup_error deep { only_error(nested(65)) };
    REQUIRE(deep.kind == ui_markup_error_kind::too_large);
    REQUIRE(narrow(deep.name) == "max_depth");
    REQUIRE(deep.offset == 64 * 7);
    // 구조 요소도 한 단이다.
    limits = {};
    limits.max_depth = 2;
    REQUIRE(luil::parse_ui_markup(u8"<stack><gap length=\"1\"/></stack>", limits).ok());
    REQUIRE(only_error(u8"<stack><if test=\"true\"><gap length=\"1\"/></if></stack>", limits).kind == ui_markup_error_kind::too_large);

    // 요소 수다. 구조 요소도 세고 글과 주석은 세지 않는다.
    limits = {};
    limits.max_nodes = 3;
    REQUIRE(luil::parse_ui_markup(u8"<stack><!-- a --><gap length=\"1\"/><gap length=\"1\"/></stack>", limits).ok());
    const luil::ui_markup_error nodes { only_error(u8"<stack><gap length=\"1\"/><gap length=\"1\"/><gap length=\"1\"/></stack>", limits) };
    REQUIRE(nodes.kind == ui_markup_error_kind::too_large);
    REQUIRE(narrow(nodes.name) == "max_nodes");
    REQUIRE(nodes.column == 42);

    // 넘친 글에 어휘 오류가 앞에 있어도 too_large 하나만 남는다.
    const luil::ui_markup_error alone { only_error(u8"<stack colr=\"x\"><label wat=\"1\"/><label/><label/></stack>", limits) };
    REQUIRE(alone.kind == ui_markup_error_kind::too_large);
    // 문법 오류도 앞의 어휘 오류를 남기지 않는다.
    REQUIRE(only_error(u8"<stack colr=\"x\"><label wat=\"1\"></stack>").kind == ui_markup_error_kind::syntax);
}

TEST_CASE("Markup survives every truncation of a valid document", "[ui][markup][read][fuzz]")
{
    REQUIRE(luil::parse_ui_markup(sample).ok());
    // 잘린 글은 뿌리가 닫히지 않았거나 UTF-8이 잘려 늘 오류 하나다. 선언이 `<?xm`에서 잘리면 처리 지시로 읽혀 unsupported다.
    for (std::size_t size { 0 }; size < sample.size(); ++size)
    {
        const std::u8string_view prefix { sample.substr(0, size) };
        const luil::ui_markup_parse_result result { luil::parse_ui_markup(prefix) };
        INFO(size);
        REQUIRE(result.ok() == false);
        REQUIRE(result.errors.size() == 1);
        const ui_markup_error_kind kind { result.errors.front().kind };
        REQUIRE((kind == ui_markup_error_kind::syntax || (kind == ui_markup_error_kind::unsupported && size < 5)));
        REQUIRE(result.errors.front().offset <= size);
    }
}

TEST_CASE("Markup survives mutated documents", "[ui][markup][read][fuzz]")
{
    // 바이트를 바꾸고, 끼우고, 지운 글이다. 멈추거나 넘치지 않고, 막혔으면 오류가 자리 순이다.
    constexpr char8_t interesting[] { u8'<', u8'>', u8'/', u8'"', u8'\'', u8'&', u8'{', u8'}', u8'=', u8'-', u8'!', u8'?', u8']', u8'[', u8' ', u8'\n', u8'\r', u8':', u8'#', u8';', u8'x', u8'0', 0x00, 0x01, 0x80, 0xC3, 0xED, 0xEF, 0xFF };
    sequence random {};
    for (int round { 0 }; round < 3000; ++round)
    {
        std::u8string text { sample };
        const int edits { 1 + static_cast<int>(random.next() % 4u) };
        for (int edit { 0 }; edit < edits && text.empty() == false; ++edit)
        {
            const std::size_t at { random.next() % text.size() };
            const char8_t byte { interesting[random.next() % std::size(interesting)] };
            switch (random.next() % 3u)
            {
            case 0:
                text[at] = byte;
                break;
            case 1:
                text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), byte);
                break;
            default:
                text.erase(text.begin() + static_cast<std::ptrdiff_t>(at));
                break;
            }
        }
        INFO(round);
        require_well_formed_answer(text);
    }
}

TEST_CASE("Markup stops deep and large documents without exhausting the stack", "[ui][markup][read][fuzz]")
{
    // 깊이 십만이다. 읽기는 재귀하지 않고 65단째에서 멈춘다.
    std::u8string deep {};
    for (int index { 0 }; index < 100000; ++index)
        deep += u8"<if>";
    REQUIRE(only_error(deep).kind == ui_markup_error_kind::too_large);
    // 한도를 풀어도 읽기와 어휘 확인이 재귀하지 않아 호출 깊이가 글을 따르지 않는다.
    luil::ui_markup_limits open {};
    open.max_depth = 1000000;
    open.max_nodes = 1000000;
    std::u8string stacks {};
    for (int index { 0 }; index < 50000; ++index)
        stacks += u8"<stack>";
    for (int index { 0 }; index < 50000; ++index)
        stacks += u8"</stack>";
    REQUIRE(luil::parse_ui_markup(stacks, open).ok());
    REQUIRE(luil::parse_ui_markup(deep + u8"x", open).ok() == false);

    // 1 MiB에 가까운 바른 글이다. 큰 속성 값과 긴 주석이다.
    std::u8string large { u8"<stack><!-- " };
    large.append(std::size_t { 600 } * 1024, u8'c');
    large += u8" --><label text=\"";
    large.append(std::size_t { 400 } * 1024, u8'a');
    large += u8"\"/></stack>";
    REQUIRE(large.size() < luil::ui_markup_limits {}.max_bytes);
    REQUIRE(luil::parse_ui_markup(large).ok());
    // 큰 값의 오류는 64바이트에서 자른 글을 싣는다. 한글을 글자 중간에서 자르지 않는다.
    std::u8string unknown { u8"<label colr=\"" };
    for (int index { 0 }; index < 1000; ++index)
        unknown += u8"가";
    unknown += u8"\"/>";
    const luil::ui_markup_error clipped { only_error(unknown) };
    REQUIRE(clipped.value.size() == 63);
    REQUIRE(narrow(clipped.value) == narrow(std::u8string_view { unknown }.substr(13, 63)));

    // 속성이 많은 요소다. 모르는 속성마다 오류 하나이고 자리 순이다. 겹친 속성 찾기가 제곱 시간을 쓰지 않는다.
    std::u8string many { u8"<stack" };
    for (int index { 0 }; index < 60000; ++index)
        many += u8" a" + widen(std::to_string(index)) + u8"=\"1\"";
    many += u8"/>";
    REQUIRE(many.size() < luil::ui_markup_limits {}.max_bytes);
    const luil::ui_markup_parse_result result { luil::parse_ui_markup(many) };
    REQUIRE(result.errors.size() == 60000);
    for (std::size_t index { 1 }; index < result.errors.size(); ++index)
        REQUIRE(result.errors[index - 1].offset < result.errors[index].offset);
}

TEST_CASE("Markup reading is deterministic", "[ui][markup][read]")
{
    const std::u8string_view text { u8"<stack colr=\"x\">\n  <label wat=\"1\" text=\"a\" text=\"b\"/>\n  <panel/>\n</stack>" };
    const luil::ui_markup_parse_result first { luil::parse_ui_markup(text) };
    const luil::ui_markup_parse_result second { luil::parse_ui_markup(text) };
    REQUIRE(first.ok() == false);
    REQUIRE(first.errors == second.errors);
    // 모르는 속성, 겹친 속성, 자리 없는 panel이 모두 모이고 글의 자리 순이다.
    REQUIRE(first.errors.size() == 4);
    REQUIRE(first.errors[0].kind == ui_markup_error_kind::unknown_attribute);
    REQUIRE(first.errors[1].kind == ui_markup_error_kind::unknown_attribute);
    REQUIRE(first.errors[2].kind == ui_markup_error_kind::duplicate_attribute);
    REQUIRE(first.errors[3].kind == ui_markup_error_kind::missing_length);
    REQUIRE(narrow(luil::describe(first.errors[2])) == "2:27 Duplicate attribute on label: text");
}

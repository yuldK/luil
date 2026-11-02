#include "luil/theme/ui_style_json.h"

#include "luil/generated/ui_style_keys.h"
#include "luil/theme/appearance.h"
#include "luil/theme/ui_style.h"
#include "luil/theme/ui_theme.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// 시험 실행 파일에 실은 파일이다 (cmake/embed_binary.cmake). 기기에는 소스 트리가 없다.
//  - LUIL_STYLE이 가리킨 파일(내장값의 원본), 사례의 바탕인 assets/style.json, 공유 사례 표다.
extern const std::uint8_t luil_test_built_in_style[];
extern const std::size_t luil_test_built_in_style_size;
extern const std::uint8_t luil_test_base_style[];
extern const std::size_t luil_test_base_style_size;
extern const std::uint8_t luil_test_style_cases[];
extern const std::size_t luil_test_style_cases_size;

// 실행 중 스타일 파서의 계약이다 (docs/style-sheet-design.md).
// 같은 파일은 굽든 읽든 같은 값이다 — 받는 파일·막는 파일·대신 값이 생성기와 같다. 사례는 생성기 시험
// (style_tests.cmake)과 같은 표를 돈다.
namespace {
    namespace keys = luil::generated::style_keys;
    using json = nlohmann::ordered_json;

    [[nodiscard]] std::u8string_view embedded(const std::uint8_t* const data, const std::size_t size) noexcept
    {
        return { reinterpret_cast<const char8_t*>(data), size };
    }

    [[nodiscard]] std::string to_string(const std::u8string_view text)
    {
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    [[nodiscard]] std::u8string to_u8(const std::string_view text)
    {
        return { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    [[nodiscard]] luil::style_parse_result parse(const std::string_view text)
    {
        return luil::parse_ui_style(to_u8(text));
    }

    [[nodiscard]] std::string base_style()
    {
        return to_string(embedded(luil_test_base_style, luil_test_base_style_size));
    }

    // 객체 주소에서 필드까지의 거리다. 실제 객체로 잰다 (널 포인터 산술은 쓰지 않는다).
    template <class Section, class Value>
    [[nodiscard]] std::ptrdiff_t offset_of(const Section& object, Value Section::* const field) noexcept
    {
        return reinterpret_cast<const char*>(&(object.*field)) - reinterpret_cast<const char*>(&object);
    }

    // 행들이 선언 순서로 빈틈 없이 이어져 [0, end)를 덮는가다. 행이 빠지거나 순서가 어긋나면 어긋난다.
    template <class Section, class Rows>
    void require_contiguous(const Section& object, const Rows& rows, const std::size_t end)
    {
        std::size_t expected { 0 };
        for (const auto& row : rows)
        {
            INFO(std::string { row.key });
            REQUIRE(offset_of(object, row.field) == static_cast<std::ptrdiff_t>(expected));
            expected += sizeof(object.*row.field);
        }
        REQUIRE(expected == end);
    }

    template <class Section, class... Rows>
    void require_contiguous_tuple(const Section& object, const std::tuple<Rows...>& rows, const std::size_t end)
    {
        std::size_t expected { 0 };
        const auto check = [&](const auto& row) {
            INFO(std::string { row.key });
            REQUIRE(offset_of(object, row.field) == static_cast<std::ptrdiff_t>(expected));
            expected += sizeof(object.*row.field);
        };
        std::apply([&check](const auto&... row) { (check(row), ...); }, rows);
        REQUIRE(expected == end);
    }

    [[nodiscard]] luil::style_error_kind kind_from(const std::string_view name)
    {
        using luil::style_error_kind;
        constexpr std::pair<std::string_view, style_error_kind> names[] {
            { "syntax", style_error_kind::syntax },
            { "not_object", style_error_kind::not_object },
            { "missing_key", style_error_kind::missing_key },
            { "unknown_key", style_error_kind::unknown_key },
            { "duplicate_key", style_error_kind::duplicate_key },
            { "wrong_type", style_error_kind::wrong_type },
            { "invalid_color", style_error_kind::invalid_color },
            { "out_of_range", style_error_kind::out_of_range },
            { "invalid_value", style_error_kind::invalid_value },
            { "too_deep", style_error_kind::too_deep },
        };
        for (const auto& [text, kind] : names)
            if (text == name)
                return kind;
        FAIL("Unknown error kind in the case table: " << name);
    }

    [[nodiscard]] luil::ui_color color_from(const std::string& text)
    {
        REQUIRE((text.size() == 7 || text.size() == 9));
        std::uint32_t rgb { 0 };
        std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
        std::uint32_t alpha { 0xFFu };
        if (text.size() == 9)
            std::from_chars(text.data() + 7, text.data() + 9, alpha, 16);
        return (alpha << 24) | rgb;
    }

    // 사례 표의 JSON 경로가 가리키는 필드의 값이다. 표를 따라 찾으므로 시험이 키를 다시 적지 않는다.
    struct field_value
    {
        std::optional<luil::ui_color> color {};
        std::optional<float> number {};
        std::optional<std::string> name {};

        [[nodiscard]] bool operator==(const field_value&) const = default;
    };

    template <class Section, class... Rows>
    [[nodiscard]] std::optional<std::string> enum_name_of(const Section& section, const std::tuple<Rows...>& rows, const std::string_view key)
    {
        std::optional<std::string> found {};
        const auto visit = [&](const auto& row) {
            if (row.key != key)
                return;
            for (const auto& entry : row.names)
                if (entry.value == section.*row.field)
                    found = std::string { entry.name };
        };
        std::apply([&visit](const auto&... row) { (visit(row), ...); }, rows);
        return found;
    }

    [[nodiscard]] field_value read_field(const luil::ui_style& style, const std::vector<std::string>& path)
    {
        field_value value {};
        REQUIRE(path.empty() == false);
        const std::string& section { path.front() };
        const std::string& key { path.back() };
        if (section == "dark" || section == "light")
        {
            const luil::neutral_color_palette& palette { section == "dark" ? style.dark : style.light };
            if (path.size() == 3)
            {
                for (const auto& row : keys::caption)
                    if (row.key == key)
                        value.color = palette.caption.*row.field;
            }
            else
            {
                for (const keys::color_row& row : keys::neutral)
                    if (row.key == key)
                        value.color = palette.*row.field;
            }
        }
        else if (section == "tones")
        {
            for (const auto& row : keys::tones)
                if (row.key == key)
                    value.number = style.tones.*row.field;
        }
        else if (section == "metrics" && path.size() == 3)
        {
            value.name = enum_name_of(style.metrics.font_weights, keys::font_weights, key);
        }
        else if (section == "metrics")
        {
            for (const auto& row : keys::metrics)
                if (row.key == key)
                    value.number = style.metrics.*row.field;
        }
        else
        {
            const auto visit = [&](const auto& component) {
                if (component.key == section)
                    value.name = enum_name_of(style.*component.field, component.rows, key);
            };
            std::apply([&visit](const auto&... component) { (visit(component), ...); }, keys::components);
        }
        REQUIRE(value != field_value {});
        return value;
    }

    [[nodiscard]] std::vector<std::string> path_of(const json& entry)
    {
        std::vector<std::string> path {};
        for (const json& part : entry.at("path"))
            path.push_back(part.get<std::string>());
        return path;
    }

    // 사례의 고침을 바탕 파일에 가한다. 생성기 시험의 `string(JSON SET|REMOVE)`와 같은 고침이다.
    [[nodiscard]] std::string case_text(const json& entry)
    {
        if (const auto text { entry.find("text") }; text != entry.end())
            return text->get<std::string>();
        json document { json::parse(base_style()) };
        if (const auto edits { entry.find("edits") }; edits != entry.end())
        {
            for (const json& edit : *edits)
            {
                const std::vector<std::string> path { path_of(edit) };
                json* parent { &document };
                for (std::size_t index { 0 }; index + 1 < path.size(); ++index)
                    parent = &parent->at(path[index]);
                if (edit.at("op") == "set")
                    (*parent)[path.back()] = json::parse(edit.at("value").get<std::string>());
                else
                    parent->erase(path.back());
            }
        }
        return document.dump(4);
    }

    [[nodiscard]] bool has_error(const luil::style_parse_result& result, const luil::style_error_kind kind, const std::u8string_view path, const std::u8string_view key)
    {
        return std::any_of(result.errors.begin(), result.errors.end(), [&](const luil::style_error& error) {
            return error.kind == kind && error.path == path && error.key == key;
        });
    }
}

TEST_CASE("The built-in style file reads back as the built-in style", "[theme][style][style_json]")
{
    // LUIL_STYLE로 구운 값과 같은 파일을 읽은 값이 같다 — 같은 판정, 같은 대신 값, 같은 숫자 변환이다.
    const luil::style_parse_result result { luil::parse_ui_style(embedded(luil_test_built_in_style, luil_test_built_in_style_size)) };
    INFO((result.errors.empty() ? std::string {} : to_string(luil::describe(result.errors.front()))));
    REQUIRE(result.ok());
    REQUIRE(result.errors.empty());
    REQUIRE(*result.style == luil::default_ui_style());

    // 라이브러리의 견본 파일(assets/style.json)은 여러 줄 정책을 적어 둔다. 앱이 복사해 고치는 파일에 키가 보이고,
    // 값은 struct 기본값과 같다.
    const std::u8string_view base { embedded(luil_test_base_style, luil_test_base_style_size) };
    REQUIRE(base.find(u8"\"wrapPolicy\"") != std::u8string_view::npos);
    const luil::style_parse_result sample { luil::parse_ui_style(base) };
    REQUIRE(sample.ok());
    REQUIRE(sample.style->text.wrap_policy == luil::line_break_policy::hangul_words);
    REQUIRE(sample.style->text.wrap_policy == luil::text_style {}.wrap_policy);
}

TEST_CASE("The key table covers every field in declaration order", "[theme][style][style_json]")
{
    // 기본값 헤더는 지정 초기화라 순서가 어긋나면 컴파일이 막지만, 행이 **빠진** 것은 그 필드가 조용히 0이 된다.
    // 표의 필드 자리가 빈틈 없이 이어져 struct를 덮는지로 잡는다.
    const luil::ui_style style {};
    require_contiguous(style.dark, keys::neutral, static_cast<std::size_t>(offset_of(style.dark, &luil::neutral_color_palette::caption)));
    REQUIRE(sizeof(style.dark) == static_cast<std::size_t>(offset_of(style.dark, &luil::neutral_color_palette::caption)) + sizeof(style.dark.caption));
    require_contiguous(style.dark.caption, keys::caption, sizeof(style.dark.caption));
    require_contiguous(style.tones, keys::tones, sizeof(style.tones));
    require_contiguous(style.metrics, keys::metrics, static_cast<std::size_t>(offset_of(style.metrics, &luil::ui_metrics::font_weights)));
    REQUIRE(sizeof(style.metrics) == static_cast<std::size_t>(offset_of(style.metrics, &luil::ui_metrics::font_weights)) + sizeof(style.metrics.font_weights));
    require_contiguous_tuple(style.metrics.font_weights, keys::font_weights, sizeof(style.metrics.font_weights));

    // 컴포넌트 칸도 칸마다 덮고, 칸들이 `ui_style`의 끝까지 이어진다 — 표에 없는 새 칸은 여기서 걸린다.
    std::size_t expected { static_cast<std::size_t>(offset_of(style, &luil::ui_style::tab_bar)) };
    REQUIRE(expected == static_cast<std::size_t>(offset_of(style, &luil::ui_style::metrics)) + sizeof(style.metrics));
    const auto check = [&](const auto& component) {
        INFO(std::string { component.key });
        REQUIRE(offset_of(style, component.field) == static_cast<std::ptrdiff_t>(expected));
        require_contiguous_tuple(style.*component.field, component.rows, sizeof(style.*component.field));
        expected += sizeof(style.*component.field);
    };
    std::apply([&check](const auto&... component) { (check(component), ...); }, keys::components);
    REQUIRE(expected == sizeof(luil::ui_style));

    // 대신할 필드는 꼭 있는 행이다. 선택 행을 대신하면 그 값이 아직 채워지지 않았을 수 있다.
    for (const keys::color_row& row : keys::neutral)
    {
        if (row.fallback == nullptr)
            continue;
        INFO(std::string { row.key });
        REQUIRE(std::any_of(std::begin(keys::neutral), std::end(keys::neutral), [&row](const keys::color_row& other) {
            return other.field == row.fallback && other.fallback == nullptr;
        }));
    }
}

TEST_CASE("The shared case table holds for the runtime parser", "[theme][style][style_json]")
{
    // 생성기 시험과 같은 표다. 받음 여부가 같고, 막을 때 생성기의 첫 오류가 목록에 있고 그 글이 생성기의 글과 같다.
    const json table { json::parse(to_string(embedded(luil_test_style_cases, luil_test_style_cases_size))) };
    const luil::ui_style defaults {};
    std::size_t count { 0 };
    for (const json& entry : table.at("cases"))
    {
        ++count;
        const std::string name { entry.at("name").get<std::string>() };
        INFO("case: " << name);
        const luil::style_parse_result result { parse(case_text(entry)) };
        const bool accept { entry.at("accept").get<bool>() };
        if (result.errors.empty() == false)
            INFO("first error: " << to_string(luil::describe(result.errors.front())));
        REQUIRE(result.ok() == accept);
        REQUIRE(result.errors.empty() == accept);
        if (accept == false)
        {
            const json& error { entry.at("error") };
            const luil::style_error_kind kind { kind_from(error.at("kind").get<std::string>()) };
            const std::u8string path { to_u8(error.at("path").get<std::string>()) };
            const std::u8string key { to_u8(error.at("key").get<std::string>()) };
            REQUIRE(has_error(result, kind, path, key));
            for (const luil::style_error& found : result.errors)
                if (found.kind == kind && found.path == path && found.key == key)
                    REQUIRE(to_string(luil::describe(found)) == error.at("message").get<std::string>());
            continue;
        }
        if (const auto expectations { entry.find("expect") }; expectations != entry.end())
        {
            for (const json& expect : *expectations)
            {
                const std::vector<std::string> path { path_of(expect) };
                INFO("field: " << path.back());
                const field_value actual { read_field(*result.style, path) };
                field_value wanted {};
                if (expect.contains("default"))
                    wanted = read_field(defaults, path);
                else if (expect.contains("color"))
                    wanted.color = color_from(expect.at("color").get<std::string>());
                else if (expect.contains("number"))
                    wanted.number = static_cast<float>(expect.at("number").get<double>());
                else
                    wanted.name = expect.at("name").get<std::string>();
                REQUIRE(actual == wanted);
            }
        }
    }
    REQUIRE(count >= 40);
}

TEST_CASE("Style errors are all collected in table order", "[theme][style][style_json]")
{
    json document { json::parse(base_style()) };
    document["dark"]["windowBackgrund"] = "#1a1b26";
    document["light"]["divider"] = "#12345";
    const luil::style_parse_result result { parse(document.dump()) };
    REQUIRE(result.ok() == false);
    REQUIRE(result.style.has_value() == false);
    REQUIRE(result.errors.size() == 2);
    REQUIRE(result.errors[0] == luil::style_error { .kind = luil::style_error_kind::unknown_key, .path = u8"dark", .key = u8"windowBackgrund" });
    REQUIRE(result.errors[1] == luil::style_error { .kind = luil::style_error_kind::invalid_color, .path = u8"light", .key = u8"divider", .value = u8"#12345" });
    REQUIRE(luil::describe(result.errors[0]) == u8"Unknown key in dark: windowBackgrund");

    SECTION("낯선 키는 JSON에 적힌 순서다")
    {
        json unknown { json::parse(base_style()) };
        unknown["zeta"] = 1;
        unknown["alpha"] = 2;
        const luil::style_parse_result two { parse(unknown.dump()) };
        REQUIRE(two.errors.size() == 2);
        REQUIRE(two.errors[0].key == u8"zeta");
        REQUIRE(two.errors[1].key == u8"alpha");
    }

    SECTION("빈 객체는 꼭 있는 뿌리 키 넷을 모두 알린다")
    {
        const luil::style_parse_result empty { parse("{}") };
        REQUIRE(empty.errors.size() == 4);
        REQUIRE(empty.errors[3] == luil::style_error { .kind = luil::style_error_kind::missing_key, .path = u8"root", .key = u8"metrics" });
    }
}

TEST_CASE("Broken JSON stops at one error with its byte offset", "[theme][style][style_json]")
{
    SECTION("닫는 괄호가 없으면 글 끝이 자리다")
    {
        std::string text { base_style() };
        text.erase(text.find_last_of('}'));
        const luil::style_parse_result result { parse(text) };
        REQUIRE(result.errors.size() == 1);
        REQUIRE(result.errors[0].kind == luil::style_error_kind::syntax);
        REQUIRE(result.errors[0].offset == text.size());
    }

    SECTION("자리는 0부터 센 바이트다")
    {
        // `{"a" 1}`의 다섯째 바이트(`1`, 0부터 5)에서 쌍점이 없다.
        const luil::style_parse_result result { parse("{\"a\" 1}") };
        REQUIRE(result.errors.size() == 1);
        REQUIRE(result.errors[0].offset == 5);
        REQUIRE(luil::describe(result.errors[0]) == u8"Invalid JSON at byte 5");
    }

    SECTION("빈 글은 문법 오류다")
    {
        const luil::style_parse_result result { parse("") };
        REQUIRE(result.errors.size() == 1);
        REQUIRE(result.errors[0].kind == luil::style_error_kind::syntax);
        REQUIRE(result.errors[0].offset == 0);
    }

    SECTION("문법 오류가 나면 앞서 센 겹친 키는 남기지 않는다")
    {
        const luil::style_parse_result result { parse("{\"a\": 1, \"a\": 2") };
        REQUIRE(result.errors.size() == 1);
        REQUIRE(result.errors[0].kind == luil::style_error_kind::syntax);
    }
}

TEST_CASE("Deep nesting is refused before the value is built", "[theme][style][style_json]")
{
    // 뿌리가 1단이다. 8단까지는 문법상 받고(값은 스키마가 막는다), 9단째에서 멈춘다.
    const std::string eight { "{\"dark\": [[[[[[[1]]]]]]]}" };
    const luil::style_parse_result allowed { parse(eight) };
    REQUIRE(std::none_of(allowed.errors.begin(), allowed.errors.end(), [](const luil::style_error& error) {
        return error.kind == luil::style_error_kind::too_deep;
    }));
    const luil::style_parse_result refused { parse("{\"dark\": [[[[[[[[1]]]]]]]]}") };
    REQUIRE(refused.errors.size() == 1);
    REQUIRE(refused.errors[0].kind == luil::style_error_kind::too_deep);
    REQUIRE(luil::describe(refused.errors[0]) == u8"JSON nested deeper than 8 levels");
    // 깊이는 문법보다 먼저 온다. 깨진 글이라도 깊으면 too_deep 하나다.
    const std::string deep_and_broken { std::string(10000, '[') };
    const luil::style_parse_result deep { parse(deep_and_broken) };
    REQUIRE(deep.errors.size() == 1);
    REQUIRE(deep.errors[0].kind == luil::style_error_kind::too_deep);
}

TEST_CASE("A key given twice in one object is refused", "[theme][style][style_json]")
{
    // nlohmann도 생성기(CMake)도 뒤의 것을 남겨 조용히 지나간다. 파서만 막는다 — 같은 판정의 예외 묶음이다.
    std::string text { base_style() };
    const std::string first { "\"windowBackground\": \"#1e1e1e\"," };
    const std::size_t at { text.find(first) };
    REQUIRE(at != std::string::npos);
    text.insert(at, "\"windowBackground\": \"#000000\",");
    const luil::style_parse_result result { parse(text) };
    REQUIRE(result.ok() == false);
    REQUIRE(result.errors.size() == 1);
    REQUIRE(result.errors[0] == luil::style_error { .kind = luil::style_error_kind::duplicate_key, .path = u8"dark", .key = u8"windowBackground" });
    REQUIRE(luil::describe(result.errors[0]) == u8"Duplicate key in dark: windowBackground");
}

TEST_CASE("The parser is strict JSON where the generator is lenient", "[theme][style][style_json]")
{
    // CMake의 JSON 읽기는 주석·끝 쉼표·뿌리 뒤의 글·`+`·앞자리 0을 받는다. 파서는 RFC 8259 그대로 막는다.
    const std::string base { base_style() };
    const std::size_t close { base.find_last_of('}') };
    const auto with_tail = [&base, close](const std::string_view tail) {
        std::string text { base };
        text.insert(close + 1, tail);
        return text;
    };
    std::string comment { base };
    comment.insert(1, "// comment\n");
    std::string trailing_comma { base };
    trailing_comma.insert(trailing_comma.rfind('"', close) + 1, ",");
    std::string plus { base };
    plus.replace(plus.find("\"bodyFontSize\": 12"), 18, "\"bodyFontSize\": +12");
    std::string leading_zero { base };
    leading_zero.replace(leading_zero.find("\"bodyFontSize\": 12"), 18, "\"bodyFontSize\": 012");
    for (const std::string& text : { comment, trailing_comma, with_tail(" {}"), plus, leading_zero })
    {
        const luil::style_parse_result result { parse(text) };
        REQUIRE(result.errors.size() == 1);
        REQUIRE(result.errors[0].kind == luil::style_error_kind::syntax);
    }

    // 숫자는 값으로 본다. 지수 표기도 생성기(값을 다시 적어 본다)와 같이 받는다.
    std::string exponent { base };
    exponent.replace(exponent.find("\"bodyFontSize\": 12"), 18, "\"bodyFontSize\": 1.2e1");
    const luil::style_parse_result accepted { parse(exponent) };
    REQUIRE(accepted.ok());
    REQUIRE(accepted.style->metrics.body_font_size == 12.0f);
}

TEST_CASE("Values in errors are clipped at 64 bytes on a character boundary", "[theme][style][style_json]")
{
    json document { json::parse(base_style()) };
    // 한글 한 자는 3바이트다. 22자(66바이트)는 64바이트 앞의 21자(63바이트)에서 잘린다.
    std::string long_name {};
    for (int index { 0 }; index < 22; ++index)
        long_name += "\xEA\xB0\x80";
    document["text"]["lineBreak"] = long_name;
    const luil::style_parse_result result { parse(document.dump()) };
    REQUIRE(result.errors.size() == 1);
    REQUIRE(result.errors[0].kind == luil::style_error_kind::invalid_value);
    REQUIRE(result.errors[0].value.size() == 63);
}

TEST_CASE("A missing component section is the struct default, not the built-in style", "[theme][style][style_json]")
{
    json document { json::parse(base_style()) };
    document.erase("tabBar");
    document["popup"].erase("corner");
    document["text"]["lineBreak"] = "symbol";
    const luil::style_parse_result result { parse(document.dump()) };
    REQUIRE(result.ok());
    REQUIRE(result.style->tab_bar == luil::tab_bar_style {});
    REQUIRE(result.style->popup == luil::popup_style {});
    REQUIRE(result.style->text.line_break == luil::single_line_break::symbol);

    // 칸 안에서 빠진 키도 그 필드의 struct 기본값이다. 적은 키만 바뀐다.
    json partial { json::parse(base_style()) };
    partial["tabBar"] = { { "variant", "connected" } };
    const luil::style_parse_result connected { parse(partial.dump()) };
    REQUIRE(connected.ok());
    REQUIRE(connected.style->tab_bar.variant == luil::tab_bar_variant::connected);
    REQUIRE(connected.style->tab_bar.close_visibility == luil::tab_bar_style {}.close_visibility);
    REQUIRE(connected.style->tab_bar.selected_background == luil::tab_bar_style {}.selected_background);
}

TEST_CASE("High contrast ignores a style read at run time", "[theme][style][style_json]")
{
    json document { json::parse(base_style()) };
    document["dark"]["windowBackground"] = "#123456";
    const luil::style_parse_result result { parse(document.dump()) };
    REQUIRE(result.ok());
    REQUIRE(result.style->dark.window_background == luil::make_ui_color(0x12, 0x34, 0x56));
    const luil::accent_definition accent { luil::accent_for(luil::default_accent_id) };
    REQUIRE(luil::color_palette_for(*result.style, luil::color_theme::dark, accent).window_background == luil::make_ui_color(0x12, 0x34, 0x56));
    REQUIRE(luil::color_palette_for(*result.style, luil::color_theme::high_contrast, accent).window_background != luil::make_ui_color(0x12, 0x34, 0x56));
    REQUIRE(luil::color_palette_for(*result.style, luil::color_theme::high_contrast, accent)
        == luil::color_palette_for(luil::default_ui_style(), luil::color_theme::high_contrast, accent)
    );
}

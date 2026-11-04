#include "text/library_text.h"
#include "ui/default_text.h"

#include "test_strings.h"
#include "text_languages_guard.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using test_strings::key;

    // 시험 표는 정적 값이다. 컴파일 때 읽히는지까지 함께 잠근다.
    static_assert(test_strings::table.languages.size() == 5u);
    static_assert(test_strings::table.keys.size() == 9u);

    [[nodiscard]] luil::string_lookup pick(const std::vector<std::u8string>& preferences)
    {
        return luil::select_strings(test_strings::table, preferences);
    }

    [[nodiscard]] luil::string_lookup language(const std::u8string& tag)
    {
        const luil::string_lookup found { pick({ tag }) };
        REQUIRE(found.tag() == tag);
        return found;
    }

    [[nodiscard]] std::u8string files_selected(const luil::string_lookup& strings, const std::int64_t count)
    {
        const std::array<luil::string_argument, 1> arguments { { { u8"count", count } } };
        return test_strings::format(strings, key::files_selected, arguments);
    }

    using luil_test::text_languages_guard;
}

TEST_CASE("The generator puts the base language first and the keys in name order", "[text][string-table]")
{
    REQUIRE(test_strings::table.languages.size() == 5u);
    CHECK(test_strings::table.languages[0].tag == u8"en");
    CHECK(test_strings::table.languages[1].tag == u8"fr");
    CHECK(test_strings::table.languages[2].tag == u8"ko");
    CHECK(test_strings::table.languages[3].tag == u8"zh");
    CHECK(test_strings::table.languages[4].tag == u8"zh-Hant");
    CHECK(test_strings::table.languages[0].plural == luil::plural_rule::one);
    CHECK(test_strings::table.languages[2].plural == luil::plural_rule::other);
    // `plural`을 적지 않은 파일은 other 규칙이다.
    CHECK(test_strings::table.languages[3].plural == luil::plural_rule::other);

    CHECK(test_strings::table.keys[static_cast<std::size_t>(key::braces)] == u8"braces");
    CHECK(test_strings::table.keys[static_cast<std::size_t>(key::save)] == u8"save");
    for (std::size_t index { 1 }; index < test_strings::table.keys.size(); ++index)
        CHECK(test_strings::table.keys[index - 1u] < test_strings::table.keys[index]);
    for (const luil::string_table_language& entry : test_strings::table.languages)
        CHECK(entry.messages.size() == test_strings::table.keys.size());
}

TEST_CASE("select_strings walks the preference list and cuts tag ends", "[text][string-table]")
{
    SECTION("A region falls back to its language")
    {
        CHECK(pick({ u8"en-US" }).tag() == u8"en");
        CHECK(pick({ u8"fr-CA" }).tag() == u8"fr");
    }

    SECTION("Tag ends come off one at a time")
    {
        CHECK(pick({ u8"zh-Hant-TW" }).tag() == u8"zh-Hant");
        CHECK(pick({ u8"zh-Hant" }).tag() == u8"zh-Hant");
        CHECK(pick({ u8"zh-Hans-CN" }).tag() == u8"zh");
        CHECK(pick({ u8"zh" }).tag() == u8"zh");
    }

    SECTION("Case and the separator do not matter")
    {
        CHECK(pick({ u8"EN-us" }).tag() == u8"en");
        CHECK(pick({ u8"zh_hant_tw" }).tag() == u8"zh-Hant");
        CHECK(pick({ u8"ZH-HANT" }).tag() == u8"zh-Hant");
        CHECK(pick({ u8"KO" }).tag() == u8"ko");
    }

    SECTION("The next tag is tried only after the whole tag is cut")
    {
        CHECK(pick({ u8"de", u8"ko" }).tag() == u8"ko");
        // 앞 태그가 끝을 떼어 맞으면 뒤 태그의 정확한 값보다 앞선다.
        CHECK(pick({ u8"fr-CA", u8"ko" }).tag() == u8"fr");
    }

    SECTION("An empty list, unknown tags and empty tags are the base language")
    {
        CHECK(pick({}).tag() == u8"en");
        CHECK(pick({ u8"de", u8"xx-YY" }).tag() == u8"en");
        CHECK(pick({ u8"" }).tag() == u8"en");
    }

    SECTION("Only the preferred tag loses its ends, never the table's tag")
    {
        static constexpr std::array<std::u8string_view, 1> keys { u8"save" };
        static constexpr std::array<luil::string_message, 1> messages {
            {
                { { { {}, {}, {}, {}, {}, u8"Save" } } },
            }
        };
        static constexpr std::array<luil::string_table_language, 2> languages {
            {
                { u8"en", luil::plural_rule::one, messages },
                { u8"zh-Hant", luil::plural_rule::other, messages },
            }
        };
        static constexpr luil::string_table table { keys, languages };
        const std::vector<std::u8string> plain_chinese { u8"zh" };
        CHECK(luil::select_strings(table, plain_chinese).tag() == u8"en");
    }
}

TEST_CASE("string_lookup finds text by key", "[text][string-table]")
{
    const luil::string_lookup english { language(u8"en") };
    const luil::string_lookup korean { language(u8"ko") };

    SECTION("Plain text and generated helpers")
    {
        CHECK(test_strings::text(english, key::save) == u8"Save");
        CHECK(test_strings::text(korean, key::save) == u8"저장");
        CHECK(english.text(static_cast<std::size_t>(key::save)) == u8"Save");
    }

    SECTION("A plural key reads as its other form and keeps its placeholders")
    {
        CHECK(test_strings::text(english, key::files_selected) == u8"{count} files selected");
        CHECK(test_strings::text(korean, key::files_selected) == u8"파일 {count}개 선택됨");
    }

    SECTION("A key out of range is empty text")
    {
        const std::size_t past_end { test_strings::table.keys.size() };
        CHECK(english.text(past_end).empty());
        CHECK(english.format(past_end, {}).empty());
    }

    SECTION("Names find keys")
    {
        CHECK(english.find(u8"save") == static_cast<std::size_t>(key::save));
        CHECK(korean.find(u8"files_selected") == static_cast<std::size_t>(key::files_selected));
        CHECK_FALSE(english.find(u8"missing").has_value());
        CHECK_FALSE(english.find(u8"").has_value());
    }

    SECTION("The view is static storage and outlives a language change")
    {
        luil::string_lookup current { english };
        const std::u8string_view saved { test_strings::text(current, key::save) };
        current = korean;
        CHECK(saved == u8"Save");
        CHECK(test_strings::text(current, key::save) == u8"저장");
    }

    SECTION("A language out of range is the base language")
    {
        const luil::string_lookup lookup { test_strings::table, 99u };
        CHECK(lookup.tag() == u8"en");
    }

    SECTION("A default lookup is empty")
    {
        const luil::string_lookup lookup {};
        CHECK(lookup.tag().empty());
        CHECK(lookup.text(0u).empty());
        CHECK(lookup.format(0u, {}).empty());
        CHECK_FALSE(lookup.find(u8"save").has_value());
    }
}

TEST_CASE("string_lookup::format fills placeholders", "[text][string-table]")
{
    const luil::string_lookup english { language(u8"en") };
    const luil::string_lookup korean { language(u8"ko") };

    SECTION("Two names in each language's own order")
    {
        const std::array<luil::string_argument, 2> arguments { { { u8"name", std::u8string_view { u8"Ada" } }, { u8"place", std::u8string_view { u8"Seoul" } } } };
        CHECK(test_strings::format(english, key::greeting, arguments) == u8"Hello, Ada of Seoul");
        CHECK(test_strings::format(korean, key::greeting, arguments) == u8"Seoul의 Ada님 안녕하세요");
    }

    SECTION("The same name twice")
    {
        const std::array<luil::string_argument, 1> arguments { { { u8"word", std::u8string_view { u8"go" } } } };
        CHECK(test_strings::format(english, key::echo, arguments) == u8"go and go");
    }

    SECTION("A missing argument stays as its placeholder so the developer sees it")
    {
        const std::array<luil::string_argument, 1> arguments { { { u8"name", std::u8string_view { u8"Ada" } } } };
        CHECK(test_strings::format(english, key::greeting, arguments) == u8"Hello, Ada of {place}");
        CHECK(test_strings::format(english, key::greeting, {}) == u8"Hello, {name} of {place}");
    }

    SECTION("Doubled braces are literal braces")
    {
        const std::array<luil::string_argument, 1> arguments { { { u8"value", std::u8string_view { u8"x" } } } };
        CHECK(test_strings::format(english, key::braces, arguments) == u8"{x} and {value}");
    }

    SECTION("Numbers are plain decimal digits")
    {
        const auto number = [&](const std::int64_t value)
        {
            const std::array<luil::string_argument, 1> arguments { { { u8"number", value } } };
            return test_strings::format(english, key::number, arguments);
        };

        CHECK(number(0) == u8"n=0");
        CHECK(number(1234567) == u8"n=1234567");
        CHECK(number(-5) == u8"n=-5");
        CHECK(number(std::numeric_limits<std::int64_t>::min()) == u8"n=-9223372036854775808");
        CHECK(number(std::numeric_limits<std::int64_t>::max()) == u8"n=9223372036854775807");
    }

    SECTION("Arguments the sentence does not use are ignored")
    {
        const std::array<luil::string_argument, 2> arguments { { { u8"extra", std::int64_t { 1 } }, { u8"word", std::u8string_view { u8"go" } } } };
        CHECK(test_strings::format(english, key::echo, arguments) == u8"go and go");
    }

    SECTION("A language may use a name only the base language declared")
    {
        const std::array<luil::string_argument, 4> arguments {
            {
                { u8"year", std::int64_t { 2026 } },
                { u8"month", std::int64_t { 11 } },
                { u8"day", std::int64_t { 3 } },
                { u8"month_name", std::u8string_view { u8"novembre" } },
            }
        };
        CHECK(test_strings::format(language(u8"fr"), key::date_line, arguments) == u8"3 novembre 2026");
        CHECK(test_strings::format(korean, key::date_line, arguments) == u8"2026년 11월 3일");
    }
}

TEST_CASE("The count argument chooses the plural form", "[text][string-table]")
{
    const luil::string_lookup english { language(u8"en") };
    const luil::string_lookup korean { language(u8"ko") };

    SECTION("The other rule is always other")
    {
        for (const std::int64_t count : { std::int64_t { 0 }, std::int64_t { 1 }, std::int64_t { -1 }, std::int64_t { 2 }, std::numeric_limits<std::int64_t>::min() })
            CHECK(luil::plural_category_for(luil::plural_rule::other, count) == luil::plural_category::other);
        CHECK(files_selected(korean, 1) == u8"파일 1개 선택됨");
        CHECK(files_selected(korean, 2) == u8"파일 2개 선택됨");
    }

    SECTION("The one rule picks one for an absolute value of 1")
    {
        CHECK(luil::plural_category_for(luil::plural_rule::one, 1) == luil::plural_category::one);
        CHECK(luil::plural_category_for(luil::plural_rule::one, -1) == luil::plural_category::one);
        for (const std::int64_t count : { std::int64_t { 0 }, std::int64_t { 2 }, std::int64_t { -2 }, std::int64_t { 1000000 }, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max() })
            CHECK(luil::plural_category_for(luil::plural_rule::one, count) == luil::plural_category::other);
        CHECK(files_selected(english, 1) == u8"1 file selected");
        CHECK(files_selected(english, -1) == u8"-1 file selected");
        CHECK(files_selected(english, 0) == u8"0 files selected");
        CHECK(files_selected(english, 2) == u8"2 files selected");
        CHECK(files_selected(english, 1000000) == u8"1000000 files selected");
    }

    SECTION("A form may leave the count out")
    {
        const std::array<luil::string_argument, 1> one { { { u8"count", std::int64_t { 1 } } } };
        const std::array<luil::string_argument, 1> two { { { u8"count", std::int64_t { 2 } } } };
        CHECK(test_strings::format(english, key::file_summary, one) == u8"a file");
        CHECK(test_strings::format(english, key::file_summary, two) == u8"2 files");
    }

    SECTION("An empty form falls back to other")
    {
        const std::array<luil::string_argument, 1> arguments { { { u8"count", std::int64_t { 1 } } } };
        CHECK(test_strings::format(english, key::files_found, arguments) == u8"1 files found");
    }

    SECTION("A missing or text count is other")
    {
        const std::array<luil::string_argument, 1> text_count { { { u8"count", std::u8string_view { u8"1" } } } };
        CHECK(test_strings::format(english, key::files_selected, text_count) == u8"1 files selected");
        CHECK(test_strings::format(english, key::files_selected, {}) == u8"{count} files selected");
    }
}

TEST_CASE("A hand-built table works the same and keeps broken braces as text", "[text][string-table]")
{
    // 생성기를 거치지 않은 표는 괄호 검사도 키 차례도 없다. 조회는 어느 쪽에도 기대지 않는다.
    static constexpr std::array<std::u8string_view, 2> keys { u8"zeta", u8"alpha" };
    static constexpr std::array<luil::string_message, 2> messages {
        {
            { { { {}, {}, {}, {}, {}, u8"open {name and } close } and {{ end {" } } },
            { { { {}, {}, {}, {}, {}, u8"plain" } } },
        }
    };
    static constexpr std::array<luil::string_table_language, 1> languages { { { u8"xx", luil::plural_rule::other, messages } } };
    static constexpr luil::string_table table { keys, languages };

    const luil::string_lookup strings { table, 0u };
    CHECK(strings.find(u8"alpha") == 1u);
    CHECK(strings.find(u8"zeta") == 0u);
    CHECK(strings.text(1u) == u8"plain");
    CHECK(strings.format(0u, {}) == u8"open {name and } close } and { end {");
}

TEST_CASE("The library table follows set_text_languages and stays Korean until the app chooses", "[text][string-table]")
{
    const text_languages_guard guard {};
    using luil::library_text::key;

    SECTION("Never set")
    {
        luil::reset_text_languages();
        CHECK_FALSE(luil::text_languages().has_value());
        CHECK(luil::library_strings().tag() == u8"ko");
        CHECK(luil::library_text::text(luil::library_strings(), key::info_bar_close) == u8"닫기");
    }

    SECTION("An empty list is a chosen list that still means Korean")
    {
        luil::set_text_languages({});
        REQUIRE(luil::text_languages().has_value());
        CHECK(luil::text_languages()->empty());
        CHECK(luil::library_strings().tag() == u8"ko");
    }

    SECTION("A language the table has")
    {
        const std::vector<std::u8string> preferences { u8"en-GB", u8"ko" };
        luil::set_text_languages(preferences);
        CHECK(luil::text_languages() == preferences);
        CHECK(luil::library_strings().tag() == u8"en");
        CHECK(luil::library_text::text(luil::library_strings(), key::info_bar_close) == u8"Close");
    }

    SECTION("Only languages the table does not have")
    {
        const std::vector<std::u8string> preferences { u8"de", u8"xx" };
        luil::set_text_languages(preferences);
        CHECK(luil::text_languages() == preferences);
        CHECK(luil::library_strings().tag() == u8"ko");
    }

    SECTION("A later list replaces an earlier one")
    {
        const std::vector<std::u8string> english { u8"en" };
        const std::vector<std::u8string> korean { u8"ko" };
        luil::set_text_languages(english);
        const luil::string_lookup before { luil::library_strings() };
        luil::set_text_languages(korean);
        CHECK(luil::library_strings().tag() == u8"ko");
        // 이미 고른 조회와 그 글은 그대로다. 그 뒤에 짓는 요소부터 바뀐다.
        CHECK(before.tag() == u8"en");
        CHECK(luil::library_text::text(before, key::zoom_fit) == u8"Fit");
    }
}

TEST_CASE("The Korean library table is byte for byte the text the default text functions write", "[text][string-table]")
{
    // 함수 몸통이 표 조회가 된 뒤에는 글이 아니라 함수와 키의 짝을 잠근다. 글자 기대값은 default_text_tests가 든다.
    using luil::library_text::key;
    namespace default_text = luil::default_text;
    const luil::string_lookup korean { luil::library_text::table, 0u };
    REQUIRE(korean.tag() == u8"ko");

    const std::array<std::pair<key, std::u8string_view>, 50> words {
        {
            { key::info_bar_close, default_text::info_bar_close() },
            { key::severity_name_info, default_text::severity_name(luil::ui_severity::info) },
            { key::severity_name_success, default_text::severity_name(luil::ui_severity::success) },
            { key::severity_name_warning, default_text::severity_name(luil::ui_severity::warning) },
            { key::severity_name_error, default_text::severity_name(luil::ui_severity::error) },
            { key::zoom_decrease, default_text::zoom_decrease() },
            { key::zoom_increase, default_text::zoom_increase() },
            { key::zoom_fit, default_text::zoom_fit() },
            { key::theme_picker_name, default_text::theme_picker_name() },
            { key::theme_picker_label_light, default_text::theme_picker_label(luil::theme_preference::light) },
            { key::theme_picker_label_system, default_text::theme_picker_label(luil::theme_preference::system) },
            { key::theme_picker_label_dark, default_text::theme_picker_label(luil::theme_preference::dark) },
            { key::accent_picker_name, default_text::accent_picker_name() },
            { key::combo_box_loading, default_text::combo_box_loading() },
            { key::message_box_ok_label, default_text::message_box_ok_label() },
            { key::message_box_cancel_label, default_text::message_box_cancel_label() },
            { key::message_box_yes_label, default_text::message_box_yes_label() },
            { key::message_box_no_label, default_text::message_box_no_label() },
            { key::rating_name, default_text::rating_name() },
            { key::undo_command_undo, default_text::undo_command_undo() },
            { key::undo_command_redo, default_text::undo_command_redo() },
            { key::ink_canvas_name, default_text::ink_canvas_name() },
            { key::color_picker_name, default_text::color_picker_name() },
            { key::color_picker_plane_name, default_text::color_picker_plane_name() },
            { key::color_picker_hue_name, default_text::color_picker_hue_name() },
            { key::color_picker_alpha_name, default_text::color_picker_alpha_name() },
            { key::color_picker_hex_name, default_text::color_picker_hex_name() },
            { key::rich_text_bold, default_text::rich_text_bold() },
            { key::rich_text_italic, default_text::rich_text_italic() },
            { key::rich_text_underline, default_text::rich_text_underline() },
            { key::rich_text_code, default_text::rich_text_code() },
            { key::rich_text_link, default_text::rich_text_link() },
            { key::rich_text_clear_format, default_text::rich_text_clear_format() },
            { key::menu_bar_name, default_text::menu_bar_name() },
            { key::date_field_name, default_text::date_field_name() },
            { key::date_field_placeholder, default_text::date_field_placeholder() },
            { key::calendar_name, default_text::calendar_name() },
            { key::calendar_previous_month, default_text::calendar_previous_month() },
            { key::calendar_next_month, default_text::calendar_next_month() },
            { key::calendar_weekday_short_sunday, default_text::calendar_weekday_short(std::chrono::Sunday) },
            { key::calendar_weekday_short_monday, default_text::calendar_weekday_short(std::chrono::Monday) },
            { key::calendar_weekday_short_tuesday, default_text::calendar_weekday_short(std::chrono::Tuesday) },
            { key::calendar_weekday_short_wednesday, default_text::calendar_weekday_short(std::chrono::Wednesday) },
            { key::calendar_weekday_short_thursday, default_text::calendar_weekday_short(std::chrono::Thursday) },
            { key::calendar_weekday_short_friday, default_text::calendar_weekday_short(std::chrono::Friday) },
            { key::calendar_weekday_short_saturday, default_text::calendar_weekday_short(std::chrono::Saturday) },
            { key::calendar_today, default_text::calendar_today() },
            { key::calendar_unavailable, default_text::calendar_unavailable() },
            { key::teaching_tip_name, default_text::teaching_tip_name() },
            { key::teaching_tip_close, default_text::teaching_tip_close() },
        }
    };
    for (const auto& [word, expected] : words)
        CHECK(luil::library_text::text(korean, word) == expected);

    using namespace std::chrono_literals;
    const std::array<luil::string_argument, 1> label { { { u8"label", std::u8string_view { u8"저장" } } } };
    CHECK(luil::library_text::format(korean, key::split_button_arrow_name, label) == default_text::split_button_arrow_name(u8"저장"));
    const std::array<luil::string_argument, 1> no_label { { { u8"label", std::u8string_view {} } } };
    CHECK(luil::library_text::format(korean, key::split_button_arrow_name, no_label) == default_text::split_button_arrow_name({}));

    const std::array<luil::string_argument, 2> rating { { { u8"value", std::u8string_view { u8"3.5" } }, { u8"maximum", std::int64_t { 5 } } } };
    CHECK(luil::library_text::format(korean, key::rating_value_name, rating) == default_text::rating_value_name(3.5f, 5u));

    const std::array<luil::string_argument, 1> strokes { { { u8"count", std::int64_t { 3 } } } };
    CHECK(luil::library_text::format(korean, key::ink_canvas_summary, strokes) == default_text::ink_canvas_summary(3u));

    const std::array<luil::string_argument, 2> plane { { { u8"saturation", std::int64_t { 40 } }, { u8"value", std::int64_t { 80 } } } };
    CHECK(luil::library_text::format(korean, key::color_picker_plane_value, plane) == default_text::color_picker_plane_value(0.4f, 0.8f));

    const std::array<luil::string_argument, 5> date {
        {
            { u8"year", std::int64_t { 2026 } },
            { u8"month", std::int64_t { 11 } },
            { u8"day", std::int64_t { 3 } },
            { u8"month_name", luil::library_text::text(korean, key::calendar_month_name_november) },
            { u8"weekday", luil::library_text::text(korean, key::calendar_weekday_long_tuesday) },
        }
    };
    constexpr std::chrono::year_month_day day { 2026y / std::chrono::November / 3d };
    CHECK(luil::library_text::format(korean, key::date_field_text, date) == default_text::date_field_text(day));
    CHECK(luil::library_text::format(korean, key::calendar_month_title, date) == default_text::calendar_month_title(2026y / std::chrono::November));
    CHECK(luil::library_text::format(korean, key::calendar_day_name, date) == default_text::calendar_day_name(day));
}

TEST_CASE("The English library table has the sentences the adoption promised", "[text][string-table]")
{
    using luil::library_text::key;
    const std::vector<std::u8string> preferences { u8"en" };
    const luil::string_lookup english { luil::select_strings(luil::library_text::table, preferences) };
    REQUIRE(english.tag() == u8"en");

    const std::array<luil::string_argument, 1> label { { { u8"label", std::u8string_view { u8"Save" } } } };
    CHECK(luil::library_text::format(english, key::split_button_arrow_name, label) == u8"Save options");

    const std::array<luil::string_argument, 2> rating { { { u8"value", std::u8string_view { u8"3.5" } }, { u8"maximum", std::int64_t { 5 } } } };
    CHECK(luil::library_text::format(english, key::rating_value_name, rating) == u8"3.5 / 5");

    const auto strokes = [&](const std::int64_t count)
    {
        const std::array<luil::string_argument, 1> arguments { { { u8"count", count } } };
        return luil::library_text::format(english, key::ink_canvas_summary, arguments);
    };

    CHECK(strokes(0) == u8"0 strokes");
    CHECK(strokes(1) == u8"1 stroke");
    CHECK(strokes(2) == u8"2 strokes");

    const std::array<luil::string_argument, 5> date {
        {
            { u8"year", std::int64_t { 2026 } },
            { u8"month", std::int64_t { 11 } },
            { u8"day", std::int64_t { 3 } },
            { u8"month_name", luil::library_text::text(english, key::calendar_month_name_november) },
            { u8"weekday", luil::library_text::text(english, key::calendar_weekday_long_tuesday) },
        }
    };
    CHECK(luil::library_text::format(english, key::date_field_text, date) == u8"November 3, 2026");
    CHECK(luil::library_text::format(english, key::calendar_month_title, date) == u8"November 2026");
    CHECK(luil::library_text::format(english, key::calendar_day_name, date) == u8"Tuesday, November 3, 2026");

    CHECK(luil::library_text::text(english, key::calendar_weekday_short_sunday) == u8"Su");
    CHECK(luil::library_text::text(english, key::date_field_placeholder) == u8"Select date");
    CHECK(luil::library_text::text(english, key::text_edit_menu_copy_link) == u8"Copy link");
    CHECK(luil::library_text::text(english, key::system_accent_label) == u8"System");
}

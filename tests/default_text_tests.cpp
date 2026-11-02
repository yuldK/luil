#include "ui/default_text.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace {
    constexpr std::array<luil::ui_severity, 4> severities { luil::ui_severity::info, luil::ui_severity::success, luil::ui_severity::warning, luil::ui_severity::error };
    constexpr std::array<luil::theme_preference, 3> preferences { luil::theme_preference::light, luil::theme_preference::system, luil::theme_preference::dark };
}

TEST_CASE("Every default text is a non-empty Korean word, the same as the ones the elements wrote themselves", "[ui][default-text]")
{
    // 요소마다 박혀 있던 낱말을 옮긴 것이라 글자가 전과 같아야 한다. 열다섯 낱말에 확인 대화 상자의 표준 단추 넷을 더한 열아홉이다.
    // 분할 버튼의 이름은 문자열을 새로 짓는다. view로 들기 전에 수명을 잡아 둔다.
    const std::u8string arrow_name { luil::default_text::split_button_arrow_name(u8"저장") };
    const std::array<std::pair<std::u8string_view, std::u8string_view>, 19> words {
        std::pair { luil::default_text::info_bar_close(), std::u8string_view { u8"닫기" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::info), std::u8string_view { u8"정보" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::success), std::u8string_view { u8"성공" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::warning), std::u8string_view { u8"경고" } },
        std::pair { luil::default_text::severity_name(luil::ui_severity::error), std::u8string_view { u8"오류" } },
        std::pair { std::u8string_view { arrow_name }, std::u8string_view { u8"저장 옵션" } },
        std::pair { luil::default_text::zoom_decrease(), std::u8string_view { u8"축소" } },
        std::pair { luil::default_text::zoom_increase(), std::u8string_view { u8"확대" } },
        std::pair { luil::default_text::zoom_fit(), std::u8string_view { u8"맞춤" } },
        std::pair { luil::default_text::theme_picker_name(), std::u8string_view { u8"테마" } },
        std::pair { luil::default_text::theme_picker_label(luil::theme_preference::light), std::u8string_view { u8"밝게" } },
        std::pair { luil::default_text::theme_picker_label(luil::theme_preference::system), std::u8string_view { u8"시스템" } },
        std::pair { luil::default_text::theme_picker_label(luil::theme_preference::dark), std::u8string_view { u8"어둡게" } },
        std::pair { luil::default_text::accent_picker_name(), std::u8string_view { u8"강조색" } },
        std::pair { luil::default_text::combo_box_loading(), std::u8string_view { u8"추천 항목을 불러오는 중" } },
        std::pair { luil::default_text::message_box_ok_label(), std::u8string_view { u8"확인" } },
        std::pair { luil::default_text::message_box_cancel_label(), std::u8string_view { u8"취소" } },
        std::pair { luil::default_text::message_box_yes_label(), std::u8string_view { u8"예" } },
        std::pair { luil::default_text::message_box_no_label(), std::u8string_view { u8"아니요" } },
    };
    for (const auto& [text, expected] : words)
    {
        REQUIRE(text.empty() == false);
        REQUIRE(text == expected);
    }
}

TEST_CASE("The four severities and the three theme preferences each have their own word", "[ui][default-text]")
{
    for (std::size_t first = 0; first < severities.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < severities.size(); ++second)
            REQUIRE(luil::default_text::severity_name(severities[first]) != luil::default_text::severity_name(severities[second]));
    }

    for (std::size_t first = 0; first < preferences.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < preferences.size(); ++second)
            REQUIRE(luil::default_text::theme_picker_label(preferences[first]) != luil::default_text::theme_picker_label(preferences[second]));
    }
}

TEST_CASE("An empty configured text falls back to the default and anything else is the app's own", "[ui][default-text]")
{
    const std::u8string_view fallback { luil::default_text::zoom_fit() };
    REQUIRE(luil::default_text::text_or_default({}, fallback) == fallback);
    REQUIRE(luil::default_text::text_or_default(u8"", fallback) == fallback);
    REQUIRE(luil::default_text::text_or_default(u8"Fit", fallback) == u8"Fit");

    // 공백만 있는 글은 앱이 일부러 준 글이다. 기본 문구로 바꾸지 않는다.
    REQUIRE(luil::default_text::text_or_default(u8" ", fallback) == u8" ");
    REQUIRE(luil::default_text::text_or_default(u8"\t\n", fallback) == u8"\t\n");

    // 앱 글이 기본 문구와 같아도 앱 글이다. 돌려준 view는 인자가 가리키던 글자다.
    const std::u8string configured { u8"Zoom out" };
    const std::u8string_view picked { luil::default_text::text_or_default(configured, fallback) };
    REQUIRE(picked.data() == configured.data());
}

TEST_CASE("The split button arrow name keeps the word order in one place", "[ui][default-text]")
{
    REQUIRE(luil::default_text::split_button_arrow_name(u8"저장") == u8"저장 옵션");
    REQUIRE(luil::default_text::split_button_arrow_name(u8"Save") == u8"Save 옵션");
    // 본체 이름이 비어도 앞 공백이 든 채 그대로다 (분할 버튼이 비어 있던 때의 동작).
    REQUIRE(luil::default_text::split_button_arrow_name({}) == u8" 옵션");
}

#include "luil/ui/info_bar_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/text_block.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    // 코드포인트마다 글자 크기의 절반 폭이다 (12px 글이면 6px). 배율이 2면 폭도 두 배가 되어 줄이 바뀌는 자리가 같다.
    // 공백으로만 나뉜 ASCII 글을 쓰면 줄을 나누는 규칙(uax14가 나중에 바꾼다)과 무관하게 줄 수가 정해진다.
    [[nodiscard]] float half_size_width(const std::u8string_view text, const float pixel_size)
    {
        return static_cast<float>(text.size()) * pixel_size / 2.0f;
    }

    const luil::text_measurer half_size_measurer { half_size_width };

    [[nodiscard]] luil::measure_context context_at(const float scale)
    {
        return luil::measure_context { .scale = scale, .measurer = &half_size_measurer };
    }

    [[nodiscard]] luil::arrange_context arrange_at(const luil::rect_f& slot, const float scale)
    {
        return luil::arrange_context { .slot = slot, .scale = scale, .measurer = &half_size_measurer };
    }

    struct retry_intent
    {
    };

    struct dismiss_intent
    {
    };

    const luil::ui_element_id bar_id { luil::ui_element_kind::info_bar, u8"sync" };
    const luil::ui_element_id action_id { luil::ui_element_kind::info_bar_action, u8"sync" };
    const luil::ui_element_id close_id { luil::ui_element_kind::info_bar_close, u8"sync" };

    // 한 줄의 높이와 위·아래 여백이다 (논리 픽셀). 규칙은 설계 문서의 "높이와 줄바꿈"이다.
    [[nodiscard]] float line_height()
    {
        return luil::text_block_line_height(luil::text_block_style { .font = { .size = 12.0f } });
    }

    [[nodiscard]] float top_padding(const float minimum_height = luil::info_bar_minimum_height)
    {
        return std::max(8.0f, (minimum_height - line_height()) / 2.0f);
    }

    // 글 줄 수가 n이고 그 밖의 요소가 없는 막대의 높이다.
    [[nodiscard]] float height_of_rows(const std::size_t rows)
    {
        return std::max(luil::info_bar_minimum_height, 2.0f * top_padding() + static_cast<float>(rows) * line_height());
    }

    // 닫기가 있고 동작 단추가 없는 막대다. 글 폭은 폭에서 78(앞 38 + 뒤 8 + 24 + 8)을 뺀 값이다.
    [[nodiscard]] luil::info_bar_config closable()
    {
        luil::info_bar_config config {};
        config.owner = u8"sync";
        config.severity = luil::ui_severity::error;
        config.message = u8"aaaa bbbb cccc dddd";
        config.close = luil::make_message_action(dismiss_intent {});
        return config;
    }

    // 닫기와 동작 단추가 모두 있는 막대다. 짧은 글("Retry")이라 동작 단추는 최소 폭(64)이다.
    [[nodiscard]] luil::info_bar_config actionable()
    {
        luil::info_bar_config config { closable() };
        config.message = u8"short";
        config.action_label = u8"Retry";
        config.action = luil::make_message_action(retry_intent {});
        return config;
    }

    // 요소는 옮길 수 없어(자식이 부모를 가리킨다) 만들어 둔 막대를 배치한다.
    void arrange_in(luil::info_bar_element& bar, const luil::rect_f& slot, const float scale = 1.0f)
    {
        bar.arrange(arrange_at(slot, scale));
    }

    [[nodiscard]] const luil::ui_element& child_of(const luil::info_bar_element& bar, const luil::ui_element_id& id)
    {
        const luil::ui_element* found { nullptr };
        for (const std::unique_ptr<luil::ui_element>& child : bar.children())
            if (child->id() == id)
                found = child.get();
        REQUIRE(found != nullptr);
        return *found;
    }

    // 액션이 내는 메시지가 어떤 intent인지 짧은 글로 옮긴다.
    [[nodiscard]] std::u8string described(const luil::ui_element& element)
    {
        const luil::ui_action* const action { element.action(luil::ui_trigger::left_click) };
        if (action == nullptr)
            return u8"none";
        const std::vector<luil::input_action> actions { (*action)(luil::ui_action_context {}) };
        if (actions.size() != 1u)
            return u8"?";
        const auto* const message { std::get_if<luil::app_message>(&actions.front()) };
        if (message == nullptr)
            return u8"?";
        if (message->get<retry_intent>() != nullptr)
            return u8"retry";
        if (message->get<dismiss_intent>() != nullptr)
            return u8"dismiss";
        return u8"?";
    }
}

TEST_CASE("An info bar makes its action only with both label and action, and its close only with a close action", "[ui][info-bar]")
{
    const luil::info_bar_element both { actionable() };
    REQUIRE(both.id() == bar_id);
    REQUIRE(both.children().size() == 2u);
    REQUIRE(both.children()[0]->id() == action_id);
    REQUIRE(both.children()[1]->id() == close_id);

    // 글만 있거나 액션만 있으면 눌러도 아무 일 없는 단추라 만들지 않는다.
    luil::info_bar_config label_only { actionable() };
    label_only.action = nullptr;
    const luil::info_bar_element labeled { std::move(label_only) };
    REQUIRE(labeled.children().size() == 1u);
    REQUIRE(labeled.children()[0]->id() == close_id);

    luil::info_bar_config action_only { actionable() };
    action_only.action_label = {};
    const luil::info_bar_element silent { std::move(action_only) };
    REQUIRE(silent.children().size() == 1u);
    REQUIRE(silent.children()[0]->id() == close_id);

    // 닫기가 없으면 앱이 지울 때까지 남는 막대다.
    luil::info_bar_config sticky { actionable() };
    sticky.close = nullptr;
    const luil::info_bar_element unclosable { std::move(sticky) };
    REQUIRE(unclosable.children().size() == 1u);
    REQUIRE(unclosable.children()[0]->id() == action_id);

    const luil::info_bar_element plain { luil::info_bar_config { .owner = u8"sync", .message = u8"text" } };
    REQUIRE(plain.children().empty());
}

TEST_CASE("Both buttons of an info bar are Tab stops with a hand cursor and post their messages", "[ui][info-bar]")
{
    const luil::info_bar_element bar { actionable() };
    const luil::ui_element& action { child_of(bar, action_id) };
    const luil::ui_element& close { child_of(bar, close_id) };

    // 토스트가 비운 까닭(곧 사라지는 알림)이 상주 알림에는 없다.
    REQUIRE(action.tab_stop());
    REQUIRE(close.tab_stop());
    REQUIRE(action.cursor() == luil::ui_cursor::hand);
    REQUIRE(close.cursor() == luil::ui_cursor::hand);
    REQUIRE(described(action) == u8"retry");
    REQUIRE(described(close) == u8"dismiss");

    luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::info_bar_element>(actionable()), { 0.0f, 0.0f, 400.0f, 60.0f }, 1.0f) };
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { action_id, close_id });
}

TEST_CASE("An info bar body absorbs hits and the buttons are hit first", "[ui][info-bar][hit]")
{
    // 단추 없는 막대도 내용 위에 놓이는 표면이다. 몸통 클릭이 아래 element로 새면 유령 클릭이 된다.
    luil::info_bar_element plain { luil::info_bar_config { .owner = u8"sync", .message = u8"text" } };
    arrange_in(plain, { 100.0f, 100.0f, 400.0f, 40.0f });
    REQUIRE(plain.hit_opaque());
    const luil::ui_element* const body { plain.hit_test(200.0f, 120.0f) };
    REQUIRE(body != nullptr);
    REQUIRE(body->id() == bar_id);
    REQUIRE(plain.hit_test(50.0f, 50.0f) == nullptr);

    // 닫기 단추는 오른쪽 가장자리에서 8 떨어진 24 칸이다.
    luil::info_bar_element bar { closable() };
    arrange_in(bar, { 100.0f, 100.0f, 400.0f, 40.0f });
    const luil::ui_element* const close { bar.hit_test(100.0f + 400.0f - 8.0f - 12.0f, 120.0f) };
    REQUIRE(close != nullptr);
    REQUIRE(close->id() == close_id);
    const luil::ui_element* const text { bar.hit_test(150.0f, 120.0f) };
    REQUIRE(text != nullptr);
    REQUIRE(text->id() == bar_id);
}

TEST_CASE("An info bar measures one line as the minimum height and grows by line count", "[ui][info-bar][measure]")
{
    const luil::measure_context context { context_at(1.0f) };
    // 글 폭은 폭에서 78을 뺀 값이다. 글 폭 130이면 한 줄(114), 100이면 두 줄, 50이면 낱말마다 한 줄이다.
    const luil::info_bar_size one { luil::info_bar_element::measure(closable(), context, 78.0f + 130.0f) };
    REQUIRE(one.height == Catch::Approx(luil::info_bar_minimum_height));
    REQUIRE(one.height == Catch::Approx(height_of_rows(1)));
    REQUIRE(one.action_below == false);

    REQUIRE(luil::info_bar_element::measure(closable(), context, 78.0f + 100.0f).height == Catch::Approx(height_of_rows(2)));
    REQUIRE(luil::info_bar_element::measure(closable(), context, 78.0f + 50.0f).height == Catch::Approx(height_of_rows(4)));

    // max_lines가 상한이다.
    luil::info_bar_config capped { closable() };
    capped.max_lines = 2;
    REQUIRE(luil::info_bar_element::measure(capped, context, 78.0f + 50.0f).height == Catch::Approx(height_of_rows(2)));

    // 제목은 한 줄을 더한다. 길어도 한 줄이다.
    luil::info_bar_config titled { closable() };
    titled.title = u8"a title that does not fit on a single line of this bar at all";
    REQUIRE(luil::info_bar_element::measure(titled, context, 78.0f + 130.0f).height == Catch::Approx(height_of_rows(2)));
    titled.message = {};
    REQUIRE(luil::info_bar_element::measure(titled, context, 78.0f + 130.0f).height == Catch::Approx(height_of_rows(1)));

    // 글이 없어도 첫 줄은 있다.
    REQUIRE(luil::info_bar_element::measure(luil::info_bar_config {}, context, 300.0f).height == Catch::Approx(luil::info_bar_minimum_height));
}

TEST_CASE("An info bar raises its minimum height with the control size", "[ui][info-bar][measure]")
{
    const luil::measure_context context { context_at(1.0f) };
    luil::info_bar_config touch { closable() };
    touch.control_size = 44.0f;
    // 닫기가 44 정사각이라 글 몫에서 20을 더 가져간다. 글 폭이 130이 되는 폭이 78에서 98로 늘어난다.
    // 터치 앱이 단추를 키우면 한 줄 막대도 그 높이이고 첫 줄은 그 가운데 선다.
    REQUIRE(luil::info_bar_element::measure(touch, context, 98.0f + 130.0f).height == Catch::Approx(44.0f));
    REQUIRE(luil::info_bar_element::measure(touch, context, 98.0f + 100.0f).height == Catch::Approx(2.0f * top_padding(44.0f) + 2.0f * line_height()));

    // 잘못된 값은 정하지 않은 것으로 읽는다.
    luil::info_bar_config invalid { closable() };
    invalid.control_size = -10.0f;
    REQUIRE(luil::info_bar_element::measure(invalid, context, 78.0f + 130.0f).height == Catch::Approx(luil::info_bar_minimum_height));
    invalid.control_size = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(luil::info_bar_element::measure(invalid, context, 78.0f + 130.0f).height == Catch::Approx(luil::info_bar_minimum_height));
}

TEST_CASE("An info bar height does not depend on the scale", "[ui][info-bar][measure]")
{
    // 높이는 논리 픽셀이다. 배율은 글을 물리 크기로 재고 되돌리는 데만 쓰여 줄이 바뀌는 자리가 같다.
    for (const float width : { 78.0f + 130.0f, 78.0f + 100.0f, 78.0f + 50.0f })
    {
        CAPTURE(width);
        const float expected { luil::info_bar_element::measure(closable(), context_at(1.0f), width).height };
        REQUIRE(luil::info_bar_element::measure(closable(), context_at(2.0f), width).height == Catch::Approx(expected));
        REQUIRE(luil::info_bar_element::measure(closable(), context_at(1.5f), width).height == Catch::Approx(expected));
    }
}

TEST_CASE("The action button goes below the text when the beside text width drops under 200", "[ui][info-bar][measure]")
{
    const luil::measure_context context { context_at(1.0f) };
    // 닫기(24)와 동작 단추(최소 폭 64)가 오른쪽을 가져가면 글 폭은 폭에서 150을 뺀 값이다.
    //  - 앞 38 + 가장자리 8 + 닫기 24 + 8 + 단추 64 + 글 앞 8.
    const luil::info_bar_size beside { luil::info_bar_element::measure(actionable(), context, 350.0f) };
    REQUIRE(beside.action_below == false);
    REQUIRE(beside.height == Catch::Approx(luil::info_bar_minimum_height));

    // 글 폭이 199이면 아래 줄이다. 글 아래 8과 단추 높이가 더해진다.
    const luil::info_bar_size below { luil::info_bar_element::measure(actionable(), context, 349.0f) };
    REQUIRE(below.action_below);
    REQUIRE(below.height == Catch::Approx(2.0f * top_padding() + line_height() + 8.0f + luil::text_button_height));

    // 닫기가 없으면 단추 몫이 80(8 + 64 + 8)이라 경계가 그만큼 좁은 폭으로 내려온다.
    luil::info_bar_config sticky { actionable() };
    sticky.close = nullptr;
    REQUIRE(luil::info_bar_element::measure(sticky, context, 318.0f).action_below == false);
    REQUIRE(luil::info_bar_element::measure(sticky, context, 317.0f).action_below);

    // 동작 단추가 없으면 갈릴 것이 없다.
    REQUIRE(luil::info_bar_element::measure(closable(), context, 120.0f).action_below == false);

    // control_size가 단추를 키우면 아래 줄의 단추 높이도 그만큼이다.
    luil::info_bar_config touch { actionable() };
    touch.control_size = 44.0f;
    const luil::info_bar_size touch_below { luil::info_bar_element::measure(touch, context, 300.0f) };
    REQUIRE(touch_below.action_below);
    REQUIRE(touch_below.height == Catch::Approx(2.0f * top_padding(44.0f) + line_height() + 8.0f + 44.0f));
}

TEST_CASE("An info bar answers a preferred size only when the width is known", "[ui][info-bar][measure]")
{
    const luil::info_bar_element bar { closable() };
    const luil::measure_context context { context_at(1.0f) };
    // 가로 stack은 폭 제한 없이 묻는다. 줄바꿈은 폭이 있어야 높이가 나오므로 답하지 않는다.
    REQUIRE(bar.preferred_size(context, std::numeric_limits<float>::infinity()).has_value() == false);
    REQUIRE(bar.preferred_size(context, std::numeric_limits<float>::quiet_NaN()).has_value() == false);

    const std::optional<luil::size_f> size { bar.preferred_size(context, 78.0f + 100.0f) };
    REQUIRE(size.has_value());
    REQUIRE(size->width == 78.0f + 100.0f);
    REQUIRE(size->height == Catch::Approx(luil::info_bar_element::measure(closable(), context, 78.0f + 100.0f).height));
}

TEST_CASE("An info bar centers the close button on a one line bar", "[ui][info-bar][arrange]")
{
    // 슬롯 (10, 20, 400, 40). 한 줄 막대는 높이 40이라 첫 줄 가운데가 막대 가운데(y 40)다.
    luil::info_bar_element bar { closable() };
    arrange_in(bar, { 10.0f, 20.0f, 400.0f, 40.0f });
    const luil::rect_f close { child_of(bar, close_id).bounds() };
    REQUIRE(close.width == 24.0f);
    REQUIRE(close.height == 24.0f);
    // 오른쪽 가장자리에서 8이다.
    REQUIRE(close.x == Catch::Approx(10.0f + 400.0f - 8.0f - 24.0f));
    REQUIRE(close.y + close.height / 2.0f == Catch::Approx(20.0f + 20.0f));
}

TEST_CASE("An info bar keeps its buttons on the first line when the text wraps", "[ui][info-bar][arrange]")
{
    // 글 폭 100이면 두 줄이라 막대는 높이가 59다. 닫기는 막대 가운데(y 49.5)가 아니라 첫 줄 가운데(y 40)에 붙는다.
    luil::info_bar_element bar { closable() };
    arrange_in(bar, { 10.0f, 20.0f, 178.0f, height_of_rows(2) });
    const luil::rect_f close { child_of(bar, close_id).bounds() };
    REQUIRE(close.y + close.height / 2.0f == Catch::Approx(20.0f + 20.0f));
    REQUIRE(bar.bounds().height == Catch::Approx(height_of_rows(2)));
}

TEST_CASE("An info bar puts the action button beside the text, left of the close button", "[ui][info-bar][arrange]")
{
    luil::info_bar_element bar { actionable() };
    arrange_in(bar, { 10.0f, 20.0f, 400.0f, 40.0f });
    const luil::rect_f close { child_of(bar, close_id).bounds() };
    const luil::rect_f action { child_of(bar, action_id).bounds() };
    // 단추 사이는 8이다.
    REQUIRE(action.width == luil::text_button_minimum_width);
    REQUIRE(action.height == luil::text_button_height);
    REQUIRE(action.x + action.width + 8.0f == Catch::Approx(close.x));
    REQUIRE(action.y + action.height / 2.0f == Catch::Approx(20.0f + 20.0f));
}

TEST_CASE("An info bar puts the action button below the text, aligned to the text and 8 under it", "[ui][info-bar][arrange]")
{
    const float height { 2.0f * top_padding() + line_height() + 8.0f + luil::text_button_height };
    luil::info_bar_element bar { actionable() };
    arrange_in(bar, { 10.0f, 20.0f, 300.0f, height });
    const luil::rect_f action { child_of(bar, action_id).bounds() };
    // 글의 왼쪽(앞 몫 38)에 맞추고 본문 한 줄 아래 8이다. 아래 여백은 위 여백과 같다.
    REQUIRE(action.x == Catch::Approx(10.0f + 38.0f));
    REQUIRE(action.y == Catch::Approx(20.0f + top_padding() + line_height() + 8.0f));
    REQUIRE(action.y + action.height + top_padding() == Catch::Approx(20.0f + height));
    // 닫기는 첫 줄 가운데에 그대로 있다.
    const luil::rect_f close { child_of(bar, close_id).bounds() };
    REQUIRE(close.y + close.height / 2.0f == Catch::Approx(20.0f + top_padding() + line_height() / 2.0f));
}

TEST_CASE("An info bar arranges in physical pixels at scale 2", "[ui][info-bar][arrange]")
{
    // 논리 슬롯 (10, 20, 400, 40)의 두 배다. 높이는 논리 40이 물리 80이 된다.
    luil::info_bar_element bar { actionable() };
    arrange_in(bar, { 20.0f, 40.0f, 800.0f, 80.0f }, 2.0f);
    const luil::rect_f close { child_of(bar, close_id).bounds() };
    REQUIRE(close.x == Catch::Approx(20.0f + 800.0f - 16.0f - 48.0f));
    REQUIRE(close.width == Catch::Approx(48.0f));
    REQUIRE(close.height == Catch::Approx(48.0f));
    REQUIRE(close.y + close.height / 2.0f == Catch::Approx(40.0f + 40.0f));
    const luil::rect_f action { child_of(bar, action_id).bounds() };
    REQUIRE(action.width == Catch::Approx(luil::text_button_minimum_width * 2.0f));
    REQUIRE(action.height == Catch::Approx(luil::text_button_height * 2.0f));
    REQUIRE(action.x + action.width + 16.0f == Catch::Approx(close.x));

    // 배율이 달라도 같은 논리 폭이면 같은 쪽에 선다.
    luil::info_bar_element narrow { actionable() };
    arrange_in(narrow, { 0.0f, 0.0f, 698.0f, 152.0f }, 2.0f);
    REQUIRE(child_of(narrow, action_id).bounds().y > child_of(narrow, close_id).bounds().y + child_of(narrow, close_id).bounds().height);
}

TEST_CASE("An info bar reads as an alert named by severity word, title, and message", "[ui][info-bar][access]")
{
    luil::info_bar_config config {};
    config.owner = u8"sync";
    config.severity = luil::ui_severity::error;
    config.title = u8"동기화하지 못했습니다";
    config.message = u8"서버에 닿지 않습니다.";
    const luil::info_bar_element bar { config };
    REQUIRE(bar.accessibility().role == luil::access_role::alert);
    REQUIRE(bar.accessibility().name == u8"오류 동기화하지 못했습니다 서버에 닿지 않습니다.");

    // 화면을 보지 않는 사용자가 심각도를 가를 수 있도록 낱말이 앞에 선다.
    const std::pair<luil::ui_severity, std::u8string_view> words[] {
        { luil::ui_severity::info, u8"정보" },
        { luil::ui_severity::success, u8"성공" },
        { luil::ui_severity::warning, u8"경고" },
        { luil::ui_severity::error, u8"오류" },
    };
    for (const auto& [severity, word] : words)
    {
        config.severity = severity;
        REQUIRE(luil::info_bar_element { config }.accessibility().name == std::u8string { word } + u8" 동기화하지 못했습니다 서버에 닿지 않습니다.");
    }

    // 앱이 낱말을 주면 그것이 앞선다.
    config.severity_name = u8"Warning";
    REQUIRE(luil::info_bar_element { config }.accessibility().name == u8"Warning 동기화하지 못했습니다 서버에 닿지 않습니다.");

    // 빈 것은 건너뛴다.
    luil::info_bar_config message_only { .owner = u8"sync", .severity = luil::ui_severity::warning, .message = u8"공간이 모자랍니다." };
    REQUIRE(luil::info_bar_element { message_only }.accessibility().name == u8"경고 공간이 모자랍니다.");
    luil::info_bar_config title_only { .owner = u8"sync", .severity = luil::ui_severity::success, .title = u8"저장했습니다" };
    REQUIRE(luil::info_bar_element { title_only }.accessibility().name == u8"성공 저장했습니다");
}

TEST_CASE("An info bar name is the whole text even when the lines are elided", "[ui][info-bar][access]")
{
    luil::info_bar_config config { closable() };
    config.max_lines = 1;
    config.title = u8"title";
    luil::info_bar_element bar { config };
    arrange_in(bar, { 0.0f, 0.0f, 128.0f, height_of_rows(3) });
    REQUIRE(bar.accessibility().name == u8"오류 title aaaa bbbb cccc dddd");
}

TEST_CASE("The buttons of an info bar read as buttons named by the action label and the close tooltip", "[ui][info-bar][access]")
{
    luil::info_bar_config config { actionable() };
    const luil::info_bar_element bar { config };
    const luil::access_info action { child_of(bar, action_id).accessibility() };
    REQUIRE(action.role == luil::access_role::button);
    REQUIRE(action.name == u8"Retry");
    // 닫기의 기본 이름은 "닫기"다.
    const luil::access_info close { child_of(bar, close_id).accessibility() };
    REQUIRE(close.role == luil::access_role::button);
    REQUIRE(close.name == u8"닫기");

    config.close_tooltip = u8"Dismiss";
    const luil::info_bar_element named { config };
    REQUIRE(child_of(named, close_id).accessibility().name == u8"Dismiss");
}

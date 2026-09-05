#include "luil/ui/accessibility.h"

#include "luil/ui/app_message.h"
#include "luil/ui/badge_element.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/dropdown_element.h"
#include "luil/ui/group_element.h"
#include "luil/ui/grouped_list_element.h"
#include "luil/ui/image_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/list_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/modal_host_element.h"
#include "luil/ui/progress_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/split_handle_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/toast_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 기반 클래스 계약만 검증하는 최소 element다.
    // 자식 전부에게 자기 slot을 그대로 주므로 좌표 질의 test가 겹친 요소의
    // 위아래를 볼 수 있다.
    class test_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    luil::ui_action noop_action()
    {
        return [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    }

    // test 정의 kind다.
    // 앱이 자기 kind를 정의하는 것과 같은 경로다.
    constexpr luil::ui_element_kind kind_panel { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_item { luil::application_element_kind(1) };

    // 실행이 실제로 나갔는지는 알아볼 수 있는 메시지로 본다.
    struct access_intent
    {
        std::u8string tag {};
    };

    struct access_delta_intent
    {
        float delta { 0.0f };
    };

    // 절대 값·절대 상태를 나르는 메시지다 (델타·토글은 오래된 발행본
    // 기준으로 겹쳐 쌓이므로 접근 실행은 목표를 그대로 싣는다).
    struct access_value_intent
    {
        float value { 0.0f };
    };

    struct access_state_intent
    {
        std::u8string tag {};
        bool state { false };
    };

    luil::ui_action tagged_action(std::u8string tag)
    {
        return [tag = std::move(tag)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(access_intent { tag }) }; };
    }

    // 계획이 낸 액션 하나의 표식이다 (없거나 모양이 다르면 그 자리에서 실패한다).
    [[nodiscard]] std::u8string tag_of(const std::optional<std::vector<luil::input_action>>& planned)
    {
        REQUIRE(planned.has_value());
        REQUIRE(planned->size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&planned->front()) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<access_intent>() };
        REQUIRE(intent != nullptr);
        return intent->tag;
    }

    [[nodiscard]] float value_of(const std::optional<std::vector<luil::input_action>>& planned)
    {
        REQUIRE(planned.has_value());
        REQUIRE(planned->size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&planned->front()) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<access_value_intent>() };
        REQUIRE(intent != nullptr);
        return intent->value;
    }

    [[nodiscard]] access_state_intent state_of(const std::optional<std::vector<luil::input_action>>& planned)
    {
        REQUIRE(planned.has_value());
        REQUIRE(planned->size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&planned->front()) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<access_state_intent>() };
        REQUIRE(intent != nullptr);
        return *intent;
    }

    // "내 것이지만 할 일이 없다"의 답이다 (이미 그 상태다).
    void require_nothing_to_do(const std::optional<std::vector<luil::input_action>>& planned)
    {
        REQUIRE(planned.has_value());
        REQUIRE(planned->empty());
    }

    std::unique_ptr<luil::label_element> make_note(const std::u8string_view owner, const std::u8string_view text)
    {
        luil::label_config config {};
        config.text = std::u8string { text };
        return std::make_unique<luil::label_element>(luil::ui_element_id { kind_item, std::u8string { owner } }, std::move(config));
    }
} // namespace

TEST_CASE("A text button reads as a button named by its text", "[ui][access]")
{
    const luil::text_button_element button { luil::ui_element_id { kind_item, u8"confirm" }, { .text = u8"확인" } };
    const luil::access_info info { button.accessibility() };
    REQUIRE(info.role == luil::access_role::button);
    REQUIRE(info.name == u8"확인");

    const luil::text_button_element link { luil::ui_element_id { kind_item, u8"docs" }, { .text = u8"자세히", .visual = luil::text_button_visual::link } };
    REQUIRE(link.accessibility().role == luil::access_role::link);
}

TEST_CASE("A check control reads its style, label, and state", "[ui][access]")
{
    const luil::check_element box { { .owner = u8"alerts", .label = u8"알림", .checked = true, .toggle = noop_action() } };
    const luil::access_info info { box.accessibility() };
    REQUIRE(info.role == luil::access_role::check_box);
    REQUIRE(info.name == u8"알림");
    REQUIRE(info.checked == true);

    // 라디오만 켬/끔이 아니라 고르기다 — Toggle이 아니라 SelectionItem으로 선다.
    const luil::check_element radio { { .owner = u8"pick", .style = luil::check_style::radio, .label = u8"하나", .checked = false } };
    REQUIRE(radio.accessibility().role == luil::access_role::radio_button);
    REQUIRE(radio.accessibility().selected == false);
    REQUIRE(radio.accessibility().checked.has_value() == false);

    const luil::check_element flip { { .owner = u8"dark", .style = luil::check_style::toggle_switch, .label = u8"어두운 테마" } };
    REQUIRE(flip.accessibility().role == luil::access_role::toggle_switch);

    // 라벨이 밖에 있는 칸(표 안)은 tooltip이 이름을 대신한다.
    luil::check_element bare { { .owner = u8"row", .checked = true } };
    bare.set_tooltip(u8"이 행을 포함한다");
    REQUIRE(bare.accessibility().name == u8"이 행을 포함한다");
}

TEST_CASE("A slider reads its range and clamped value", "[ui][access]")
{
    const luil::slider_element slider { luil::ui_element_id { kind_item, u8"volume" }, { .minimum = 0.0f, .maximum = 10.0f, .value = 25.0f } };
    const luil::access_info info { slider.accessibility() };
    REQUIRE(info.role == luil::access_role::slider);
    REQUIRE(info.range.has_value());
    REQUIRE(info.range->minimum == 0.0f);
    REQUIRE(info.range->maximum == 10.0f);
    // 범위 밖 값은 그리기와 같은 함수로 잘린다.
    REQUIRE(info.range->value == 10.0f);
}

TEST_CASE("A scrollbar reads how far it can scroll", "[ui][access]")
{
    const luil::scrollbar_element bar { luil::ui_element_id { kind_item, u8"log" }, { .content_height = 400.0f, .viewport_height = 100.0f, .scroll_offset = 50.0f } };
    const luil::access_info info { bar.accessibility() };
    REQUIRE(info.role == luil::access_role::scroll_bar);
    REQUIRE(info.range.has_value());
    REQUIRE(info.range->maximum == 300.0f);
    REQUIRE(info.range->value == 50.0f);

    // 내용이 창보다 짧으면 흘릴 것이 없다.
    const luil::scrollbar_element still { luil::ui_element_id { kind_item, u8"short" }, { .content_height = 50.0f, .viewport_height = 100.0f, .scroll_offset = 30.0f } };
    REQUIRE(still.accessibility().range->maximum == 0.0f);
    REQUIRE(still.accessibility().range->value == 0.0f);
}

TEST_CASE("A progress bar reads a read-only range and a split handle reads none", "[ui][access]")
{
    const luil::progress_element progress { luil::ui_element_id { kind_item, u8"load" }, { .value = 1.4f } };
    const luil::access_info info { progress.accessibility() };
    REQUIRE(info.role == luil::access_role::progress);
    REQUIRE(info.range.has_value());
    REQUIRE(info.range->value == 1.0f);

    // 손잡이는 자기 범위를 모른다 — Home/End를 흘려보내는 것과 같은 이유다.
    const luil::split_handle_element handle { luil::ui_element_id { kind_item, u8"split" }, {} };
    REQUIRE(handle.accessibility().role == luil::access_role::handle);
    REQUIRE(handle.accessibility().range.has_value() == false);
}

TEST_CASE("A dropdown reads placeholder, current value, and open state", "[ui][access]")
{
    const luil::dropdown_element dropdown { { .owner = u8"drink", .text = u8"라떼", .placeholder = u8"음료", .open = true, .toggle = noop_action() } };
    const luil::access_info info { dropdown.accessibility() };
    REQUIRE(info.role == luil::access_role::combo_box);
    REQUIRE(info.name == u8"음료");
    REQUIRE(info.value == u8"라떼");
    REQUIRE(info.expanded == true);
}

TEST_CASE("A text input reads its committed text, not the composition", "[ui][access]")
{
    luil::text_input_view view {};
    view.text = u8"메모";
    view.composing = true;
    view.composition_text = u8"메모ㅅ";
    const luil::text_input_element input { luil::ui_element_id { kind_item, u8"note" }, view, { .placeholder = u8"메모를 적는 칸" } };
    const luil::access_info info { input.accessibility() };
    REQUIRE(info.role == luil::access_role::edit);
    REQUIRE(info.name == u8"메모를 적는 칸");
    REQUIRE(info.value == u8"메모");

    // 편집 상태(`text_input`)는 조합 문서도 함께 내준다 — Text pattern이 화면에
    // 보이는 그대로를 읽는 근거다 (확정 글은 그대로 남는다).
    const std::optional<luil::text_input_snapshot> snapshot { input.text_input() };
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->text == u8"메모");
    REQUIRE(snapshot->composing);
    REQUIRE(snapshot->composition_text == u8"메모ㅅ");
}

TEST_CASE("Labels and badges read as static text", "[ui][access]")
{
    const luil::label_element label { luil::ui_element_id { kind_item, u8"hint" }, { .text = u8"안내 글" } };
    REQUIRE(label.accessibility().role == luil::access_role::static_text);
    REQUIRE(label.accessibility().name == u8"안내 글");

    const luil::badge_element badge { luil::ui_element_id { kind_item, u8"state" }, { .text = u8"새것" } };
    REQUIRE(badge.accessibility().role == luil::access_role::static_text);
    REQUIRE(badge.accessibility().name == u8"새것");
}

TEST_CASE("An image reads as an image only when described", "[ui][access]")
{
    const std::vector<std::uint8_t> pixels(4, 255);
    const luil::image_element described { luil::ui_element_id { kind_item, u8"chart" }, { .image = luil::make_rgba_image(1, 1, pixels), .description = u8"매출 그래프" } };
    REQUIRE(described.accessibility().role == luil::access_role::image);
    REQUIRE(described.accessibility().name == u8"매출 그래프");

    // 설명이 없으면 장식이라 접근 tree에서 접힌다 (HTML alt=""의 관례).
    const luil::image_element decorative { luil::ui_element_id { kind_item, u8"deco" }, { .image = luil::make_rgba_image(1, 1, pixels) } };
    REQUIRE(decorative.accessibility().role == luil::access_role::none);
}

TEST_CASE("Choice items read selection where the radio style knows it", "[ui][access]")
{
    const luil::choice_message_factory select { [](const std::u8string&) { return luil::input_action {}; } };
    const luil::choice_group_element radios { { .owner = u8"coffee", .items = { { u8"latte", u8"라떼" }, { u8"mocha", u8"모카" } }, .selected = u8"latte", .select = select } };
    // 라디오 묶음은 항목들의 선택 container로 선다 — 항목의
    // SelectionItem이 container를 물으면 이 자리가 답이어야 한다.
    REQUIRE(radios.accessibility().role == luil::access_role::radio_group);
    REQUIRE(radios.children()[0]->accessibility().role == luil::access_role::radio_button);
    REQUIRE(radios.children()[0]->accessibility().name == u8"라떼");
    REQUIRE(radios.children()[0]->accessibility().selected == true);
    REQUIRE(radios.children()[1]->accessibility().selected == false);
    // 켬/끔이 아니므로 Toggle의 자리를 내걸지 않는다.
    REQUIRE(radios.children()[0]->accessibility().checked.has_value() == false);

    // toggle 스타일 항목은 글자 버튼이라 선택을 모른다 — 범위 밖으로 미룬 한계다
    // (accessibility-design.md). container가 내줄 선택 목록이 없으므로 묶음도
    // 구조 그대로다.
    const luil::choice_group_element toggles { { .owner = u8"theme", .style = luil::choice_style::toggle, .items = { { u8"dark", u8"어두움" } }, .selected = u8"dark", .select = select } };
    REQUIRE(toggles.accessibility().role == luil::access_role::none);
    REQUIRE(toggles.children()[0]->accessibility().role == luil::access_role::button);
    REQUIRE(toggles.children()[0]->accessibility().checked.has_value() == false);
}

TEST_CASE("A selection item finds its selection container, not just any parent", "[ui][access]")
{
    // 목록·탭 막대·라디오 묶음이 선택 container다 — SelectionItem의 짝인
    // Selection 패턴이 서는 자리와 같은 술어다.
    REQUIRE(luil::access_selection_container(luil::access_role::list));
    REQUIRE(luil::access_selection_container(luil::access_role::tab_list));
    REQUIRE(luil::access_selection_container(luil::access_role::radio_group));
    REQUIRE(luil::access_selection_container(luil::access_role::group) == false);
    REQUIRE(luil::access_selection_container(luil::access_role::list_item) == false);

    luil::list_config config {};
    config.owner = u8"files";
    config.items = { { .key = u8"a", .label = u8"가" }, { .key = u8"b", .label = u8"나" } };
    config.selected = u8"b";
    config.select = [](const std::u8string&) { return luil::input_action {}; };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 48.0f }, 1.0f) };

    // 행을 감싸는 창·레인은 구조라 지나치고 목록이 container다.
    const luil::ui_element* const container { luil::access_selection_container_of(tree, { luil::ui_element_kind::list_row, u8"b" }) };
    REQUIRE(container != nullptr);
    REQUIRE(container->accessibility().role == luil::access_role::list);
    // 골라진 항목이 발행 순서 그대로 나온다 (이 저장소의 선택은 하나뿐이다).
    const std::vector<const luil::ui_element*> selected { luil::access_selected_items(*container) };
    REQUIRE(selected.size() == 1u);
    REQUIRE(selected.front()->id().owner == u8"b");

    // 묶음 밖의 홀로 선 라디오에는 container가 없다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    root->add(std::make_unique<luil::check_element>(
        luil::check_config { .owner = u8"pick", .style = luil::check_style::radio, .label = u8"하나", .checked = true, .toggle = noop_action() }));
    const luil::ui_tree lone { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 48.0f }, 1.0f) };
    REQUIRE(luil::access_selection_container_of(lone, { luil::ui_element_kind::check, u8"pick" }) == nullptr);
}

TEST_CASE("List rows read name, selection, and expansion", "[ui][access]")
{
    const luil::list_message_factory factory { [](const std::u8string&) { return luil::input_action {}; } };
    luil::list_config config {};
    config.owner = u8"files";
    config.items = {
        { .key = u8"a", .label = u8"가" },
        { .key = u8"b", .label = u8"나", .expansion = luil::list_expansion::collapsed },
        { .key = u8"c", .label = u8"다", .expansion = luil::list_expansion::expanded },
    };
    config.selected = u8"b";
    config.select = factory;
    config.toggle = factory;
    auto list { std::make_unique<luil::list_element>(std::move(config)) };
    REQUIRE(list->accessibility().role == luil::access_role::list);

    const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 48.0f }, 1.0f) };
    const luil::ui_element* const leaf { tree.find({ luil::ui_element_kind::list_row, u8"a" }) };
    REQUIRE(leaf != nullptr);
    REQUIRE(leaf->accessibility().role == luil::access_role::list_item);
    REQUIRE(leaf->accessibility().name == u8"가");
    REQUIRE(leaf->accessibility().selected == false);
    // 펼침 상태는 가지에만 있다 — 잎에 "접힘"을 말하면 거짓이 된다.
    REQUIRE(leaf->accessibility().expanded.has_value() == false);

    const luil::ui_element* const branch { tree.find({ luil::ui_element_kind::list_row, u8"b" }) };
    REQUIRE(branch->accessibility().selected == true);
    REQUIRE(branch->accessibility().expanded == false);
    REQUIRE(tree.find({ luil::ui_element_kind::list_row, u8"c" })->accessibility().expanded == true);
}

TEST_CASE("A grouped list exposes its sticky headers by title", "[ui][access]")
{
    std::vector<luil::list_group> groups {};
    groups.push_back({ .key = u8"today", .title = u8"오늘" });
    const luil::grouped_list_element list { { .owner = u8"history" }, std::move(groups) };
    REQUIRE(list.accessibility().role == luil::access_role::list);
    REQUIRE(list.children()[0]->accessibility().role == luil::access_role::header);
    REQUIRE(list.children()[0]->accessibility().name == u8"오늘");
}

TEST_CASE("A tab reads its label and selection inside the tab list", "[ui][access]")
{
    const luil::tab_message_factory select { [](const std::u8string&) { return luil::input_action {}; } };
    luil::tab_bar_config config {};
    config.owner = u8"docs";
    config.items = { { .key = u8"one", .label = u8"첫째" }, { .key = u8"two", .label = u8"둘째" } };
    config.selected = u8"one";
    config.select = select;
    auto bar { std::make_unique<luil::tab_bar_element>(std::move(config)) };
    REQUIRE(bar->accessibility().role == luil::access_role::tab_list);

    const luil::ui_tree tree { luil::make_arranged_tree(std::move(bar), { 0.0f, 0.0f, 400.0f, 32.0f }, 1.0f) };
    const luil::ui_element* const tab { tree.find({ luil::ui_element_kind::tab, u8"one" }) };
    REQUIRE(tab != nullptr);
    REQUIRE(tab->accessibility().role == luil::access_role::tab);
    REQUIRE(tab->accessibility().name == u8"첫째");
    REQUIRE(tab->accessibility().selected == true);
    REQUIRE(tree.find({ luil::ui_element_kind::tab, u8"two" })->accessibility().selected == false);
}

TEST_CASE("The tab overflow button is named by its tooltip", "[ui][access]")
{
    const luil::tab_message_factory select { [](const std::u8string&) { return luil::input_action {}; } };
    luil::tab_bar_config config {};
    config.owner = u8"docs";
    config.items = { { .key = u8"one", .label = u8"첫째" }, { .key = u8"two", .label = u8"둘째" }, { .key = u8"three", .label = u8"셋째" } };
    config.selected = u8"one";
    config.select = select;
    config.overflow = noop_action();
    config.overflow_tooltip = u8"탭 더 보기";

    // 탭 셋이 들어가지 않는 폭이라 넘침 버튼이 선다.
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, 300.0f, 32.0f }, 1.0f) };
    const luil::ui_element* const button { tree.find({ luil::ui_element_kind::tab_overflow, u8"docs" }) };
    REQUIRE(button != nullptr);
    REQUIRE(button->visible());
    // 이름이 생겨 기본 구현이 단추로 승격한다 — 이름이 없으면 접근 tree에서 빠진다.
    REQUIRE(button->accessibility().role == luil::access_role::button);
    REQUIRE(button->accessibility().name == u8"탭 더 보기");
}

TEST_CASE("Menu items read their labels", "[ui][access]")
{
    const luil::menu_message_factory select { [](const std::u8string&) { return luil::input_action {}; } };
    const luil::menu_element menu { { .owner = u8"context", .items = { { .key = u8"open", .label = u8"열기" } }, .select = select } };
    REQUIRE(menu.accessibility().role == luil::access_role::menu);
    REQUIRE(menu.children()[0]->accessibility().role == luil::access_role::menu_item);
    REQUIRE(menu.children()[0]->accessibility().name == u8"열기");
}

TEST_CASE("A group reads its title and collapse state", "[ui][access]")
{
    const luil::group_element open { { .owner = u8"advanced", .title = u8"고급", .collapsed = false, .toggle = noop_action() } };
    REQUIRE(open.accessibility().role == luil::access_role::group);
    REQUIRE(open.accessibility().name == u8"고급");
    REQUIRE(open.accessibility().expanded == true);
    // 제목 줄은 누를 수 있으면 접기 단추다.
    REQUIRE(open.children()[0]->accessibility().role == luil::access_role::button);
    REQUIRE(open.children()[0]->accessibility().name == u8"고급");

    const luil::group_element still { { .owner = u8"fixed", .title = u8"고정", .collapsed = true } };
    REQUIRE(still.accessibility().expanded == false);
    REQUIRE(still.children()[0]->accessibility().role == luil::access_role::static_text);
}

TEST_CASE("A caption reads as the title bar and its window buttons carry tooltips", "[ui][access]")
{
    luil::caption_config config {};
    config.title = u8"luil demo";
    config.minimize_tooltip = u8"Minimize";
    const luil::caption_element caption { config };
    REQUIRE(caption.accessibility().role == luil::access_role::title_bar);
    REQUIRE(caption.accessibility().name == u8"luil demo");

    // 창 버튼은 재정의 없이 기본 규칙으로 승격된다 — 누를 수 있고 tooltip이 있다.
    bool found { false };
    for (const std::unique_ptr<luil::ui_element>& child : caption.children())
    {
        if (child->id().kind != luil::ui_element_kind::caption_minimize)
            continue;
        found = true;
        REQUIRE(child->accessibility().role == luil::access_role::button);
        REQUIRE(child->accessibility().name == u8"Minimize");
    }
    REQUIRE(found);
}

TEST_CASE("A modal host reads as a dialog and its scrim stays silent", "[ui][access]")
{
    const luil::modal_host_element host { { .owner = u8"confirm", .outside = noop_action() } };
    REQUIRE(host.accessibility().role == luil::access_role::dialog);
    // scrim은 바깥 클릭 액션이 있어도 이름이 없어 승격되지 않는다.
    REQUIRE(host.children()[0]->id().kind == luil::ui_element_kind::modal_scrim);
    REQUIRE(host.children()[0]->accessibility().role == luil::access_role::none);
}

TEST_CASE("A toast reads as an alert with its message", "[ui][access]")
{
    const luil::toast_element toast { { .id = u8"saved", .text = u8"저장했다", .severity = luil::toast_severity::success } };
    REQUIRE(toast.accessibility().role == luil::access_role::alert);
    REQUIRE(toast.accessibility().name == u8"저장했다");
}

TEST_CASE("A pressable element with a name reads as a button by default", "[ui][access]")
{
    test_panel card { luil::ui_element_id { kind_item, u8"card" } };
    REQUIRE(card.accessibility().role == luil::access_role::none);

    // 누를 수 있어도 이름이 없으면 승격하지 않는다 — 이름 없는 단추는 소음이다.
    card.set_action(luil::ui_trigger::left_click, noop_action());
    REQUIRE(card.accessibility().role == luil::access_role::none);

    card.set_tooltip(u8"열기");
    REQUIRE(card.accessibility().role == luil::access_role::button);
    REQUIRE(card.accessibility().name == u8"열기");

    // 글자 탐색이 보는 글이 tooltip보다 먼저다.
    card.set_search_label(u8"문서");
    REQUIRE(card.accessibility().name == u8"문서");
}

// --- 접근 실행 (accessibility-action-design.md) ---

TEST_CASE("An access command becomes the element's click at its centre", "[ui][access]")
{
    luil::text_button_element button { luil::ui_element_id { kind_item, u8"confirm" }, { .text = u8"확인" } };
    button.set_action(luil::ui_trigger::left_click, tagged_action(u8"confirm"));
    button.arrange({ { 10.0f, 20.0f, 100.0f, 40.0f }, 1.0f });
    REQUIRE(tag_of(luil::plan_access_request(button, {})) == u8"confirm");

    // 좌표는 요소의 한가운데다 — 포인터가 관여하지 않은 실행의 통상 규칙이다.
    luil::ui_action_context seen {};
    button.set_action(luil::ui_trigger::left_click, [&seen](const luil::ui_action_context& context) -> std::vector<luil::input_action> {
        seen = context;
        return {};
    });
    REQUIRE(luil::plan_access_request(button, {}).has_value());
    REQUIRE(seen.element.owner == u8"confirm");
    REQUIRE(seen.x == 60.0f);
    REQUIRE(seen.y == 40.0f);

    // 누를 자리가 없으면 할 수 없는 일이다 — 마우스로 누를 수 없는 것은 보조 기술로도 누를 수 없다.
    REQUIRE(luil::plan_access_request(*make_note(u8"note", u8"안내"), {}).has_value() == false);
    // 상태가 없는 요소에는 상태 명령이 서지 않는다.
    REQUIRE(luil::plan_access_request(button, { luil::access_command::toggle }).has_value() == false);
    REQUIRE(luil::plan_access_request(button, { luil::access_command::expand }).has_value() == false);
    REQUIRE(luil::plan_access_request(button, { luil::access_command::set_value, 1.0f }).has_value() == false);
}

TEST_CASE("A checkbox toggles where a radio selects", "[ui][access]")
{
    const luil::check_element box { { .owner = u8"alerts", .label = u8"알림", .toggle = tagged_action(u8"alerts") } };
    REQUIRE(tag_of(luil::plan_access_request(box, { luil::access_command::toggle })) == u8"alerts");
    REQUIRE(luil::plan_access_request(box, { luil::access_command::select }).has_value() == false);

    const luil::check_element radio { { .owner = u8"pick", .style = luil::check_style::radio, .label = u8"하나", .toggle = tagged_action(u8"pick") } };
    REQUIRE(luil::plan_access_request(radio, { luil::access_command::toggle }).has_value() == false);
    REQUIRE(tag_of(luil::plan_access_request(radio, { luil::access_command::select })) == u8"pick");

    // 이미 골라진 라디오는 할 일이 없다 — 실행이 클릭 하나라 그대로 흘리면 도리어 뒤집는다.
    const luil::check_element chosen { { .owner = u8"pick", .style = luil::check_style::radio, .checked = true, .toggle = tagged_action(u8"pick") } };
    require_nothing_to_do(luil::plan_access_request(chosen, { luil::access_command::select }));
}

TEST_CASE("A group collapses through an absolute message and an open group does nothing", "[ui][access]")
{
    const auto set_collapsed { [](const bool collapsed) { return luil::make_app_action(access_state_intent { u8"advanced", collapsed }); } };
    const luil::group_element open { { .owner = u8"advanced", .title = u8"고급", .toggle = tagged_action(u8"advanced"), .set_collapsed = set_collapsed } };
    // Expand는 "펼쳐진 상태로 만들라"이지 "펼침을 뒤집으라"가 아니다.
    require_nothing_to_do(luil::plan_access_request(open, { luil::access_command::expand }));
    // 접기는 목표 상태를 실은 절대 메시지다 — 오래된 발행본을 보고 같은 명령이
    // 두 번 와도 같은 상태가 두 번 실릴 뿐, 겹쳐 뒤집히지 않는다.
    const access_state_intent first { state_of(luil::plan_access_request(open, { luil::access_command::collapse })) };
    REQUIRE(first.tag == u8"advanced");
    REQUIRE(first.state == true);
    const access_state_intent again { state_of(luil::plan_access_request(open, { luil::access_command::collapse })) };
    REQUIRE(again.state == first.state);

    // 절대 메시지가 없으면 토글 클릭으로 물러서지 않는다 — 그 길이 곧 겹쳐
    // 뒤집히는 길이라, 이 자리에서 할 수 없는 일로 거절된다.
    const luil::group_element toggle_only { { .owner = u8"legacy", .title = u8"옛", .toggle = tagged_action(u8"legacy") } };
    REQUIRE(luil::plan_access_request(toggle_only, { luil::access_command::collapse }).has_value() == false);
    // 토글 없이 절대 메시지만 있으면 머리행의 클릭이 그 factory에서 나온다.
    const luil::group_element absolute_only { { .owner = u8"advanced", .title = u8"고급", .collapsed = true, .set_collapsed = set_collapsed } };
    const luil::ui_element& header { *absolute_only.children().front() };
    const luil::ui_action* const click { header.action(luil::ui_trigger::left_click) };
    REQUIRE(click != nullptr);
    const std::optional<std::vector<luil::input_action>> pressed { (*click)({}) };
    REQUIRE(state_of(pressed).state == false);

    const luil::group_element fixed { { .owner = u8"fixed", .title = u8"고정" } };
    REQUIRE(luil::plan_access_request(fixed, { luil::access_command::collapse }).has_value() == false);
}

TEST_CASE("A list row expands through its expander, not its selection", "[ui][access]")
{
    luil::list_config config {};
    config.owner = u8"files";
    config.items = { { .key = u8"a", .label = u8"가" }, { .key = u8"b", .label = u8"나", .expansion = luil::list_expansion::collapsed } };
    config.selected = u8"a";
    config.select = [](const std::u8string& key) { return luil::make_app_action(access_intent { u8"select:" + key }); };
    config.toggle = [](const std::u8string& key) { return luil::make_app_action(access_intent { u8"toggle:" + key }); };
    config.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(access_state_intent { key, expanded }); };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 64.0f }, 1.0f) };

    const luil::ui_element* const branch { tree.find({ luil::ui_element_kind::list_row, u8"b" }) };
    REQUIRE(branch != nullptr);
    REQUIRE(tag_of(luil::plan_access_request(*branch, { luil::access_command::select })) == u8"select:b");
    // 행의 클릭은 고르기다 — 펼침은 삼각형이 쥔다. 펼치기는 목표 상태를 실은
    // 절대 메시지라, 같은 명령이 두 번 와도 겹쳐 뒤집히지 않는다.
    const access_state_intent expand { state_of(luil::plan_access_request(*branch, { luil::access_command::expand })) };
    REQUIRE(expand.tag == u8"b");
    REQUIRE(expand.state == true);
    REQUIRE(state_of(luil::plan_access_request(*branch, { luil::access_command::expand })).state == true);

    // 잎에는 펼침이 없다.
    const luil::ui_element* const leaf { tree.find({ luil::ui_element_kind::list_row, u8"a" }) };
    REQUIRE(leaf != nullptr);
    REQUIRE(luil::plan_access_request(*leaf, { luil::access_command::expand }).has_value() == false);
}

TEST_CASE("Expansion without an absolute message refuses assistive writes", "[ui][access]")
{
    // 토글 factory만 있으면 삼각형 클릭은 서지만, 접근 실행은 거절된다 —
    // 토글은 오래된 발행본 기준으로 겹쳐 뒤집히는 길이다.
    luil::list_config config {};
    config.owner = u8"files";
    config.items = { { .key = u8"b", .label = u8"나", .expansion = luil::list_expansion::collapsed } };
    config.select = [](const std::u8string& key) { return luil::make_app_action(access_intent { u8"select:" + key }); };
    config.toggle = [](const std::u8string& key) { return luil::make_app_action(access_intent { u8"toggle:" + key }); };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 64.0f }, 1.0f) };

    const luil::ui_element* const branch { tree.find({ luil::ui_element_kind::list_row, u8"b" }) };
    REQUIRE(branch != nullptr);
    REQUIRE(branch->accessibility().expanded == false);
    REQUIRE(luil::plan_access_request(*branch, { luil::access_command::expand }).has_value() == false);

    // 절대 메시지만 있으면 삼각형의 클릭이 그 factory에서 나온다 (지금 상태의 반대).
    luil::list_config absolute {};
    absolute.owner = u8"files";
    absolute.items = { { .key = u8"b", .label = u8"나", .expansion = luil::list_expansion::collapsed } };
    absolute.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(access_state_intent { key, expanded }); };
    const luil::ui_tree tree_absolute { luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(absolute)), { 0.0f, 0.0f, 200.0f, 64.0f }, 1.0f) };
    const luil::ui_element* const expander { tree_absolute.find({ luil::ui_element_kind::list_expander, u8"b" }) };
    REQUIRE(expander != nullptr);
    const luil::ui_action* const click { expander->action(luil::ui_trigger::left_click) };
    REQUIRE(click != nullptr);
    const std::optional<std::vector<luil::input_action>> pressed { (*click)({}) };
    REQUIRE(state_of(pressed).tag == u8"b");
    REQUIRE(state_of(pressed).state == true);
}

TEST_CASE("A slider and a scrollbar carry a target value as an absolute message", "[ui][access]")
{
    luil::slider_config slider_config {};
    slider_config.maximum = 10.0f;
    slider_config.value = 3.0f;
    slider_config.change = [](const float delta) { return luil::make_app_action(access_delta_intent { delta }); };
    slider_config.change_to = [](const float value) { return luil::make_app_action(access_value_intent { value }); };
    const luil::slider_element slider { luil::ui_element_id { kind_item, u8"volume" }, slider_config };
    // 목표 값이 그대로 실린다 — 델타 환산은 오래된 발행본 기준으로 겹쳐 쌓인다.
    // 같은 목표가 두 번 와도(다음 frame 전) 같은 절대 값이라 두 배로 움직이지
    // 않는다.
    REQUIRE(value_of(luil::plan_access_request(slider, { luil::access_command::set_value, 7.5f })) == 7.5f);
    REQUIRE(value_of(luil::plan_access_request(slider, { luil::access_command::set_value, 7.5f })) == 7.5f);
    // 범위 밖은 끝으로 자른다 (그리기와 같은 자르기 함수다).
    REQUIRE(value_of(luil::plan_access_request(slider, { luil::access_command::set_value, 99.0f })) == 10.0f);
    require_nothing_to_do(luil::plan_access_request(slider, { luil::access_command::set_value, 3.0f }));

    luil::scrollbar_config bar_config {};
    bar_config.scroll = [](const float delta) { return luil::make_app_action(access_delta_intent { delta }); };
    bar_config.scroll_to = [](const float offset) { return luil::make_app_action(access_value_intent { offset }); };
    bar_config.content_height = 400.0f;
    bar_config.viewport_height = 100.0f;
    bar_config.scroll_offset = 60.0f;
    luil::scrollbar_element bar { luil::ui_element_id { kind_item, u8"scroll" }, bar_config };
    bar.arrange({ { 0.0f, 0.0f, 8.0f, 100.0f }, 1.0f });
    // 범위는 흘릴 수 있는 양이다 (내용 − 창). 자리도 절대 메시지로 나간다.
    REQUIRE(value_of(luil::plan_access_request(bar, { luil::access_command::set_value, 200.0f })) == 200.0f);
    REQUIRE(value_of(luil::plan_access_request(bar, { luil::access_command::set_value, 999.0f })) == 300.0f);
    require_nothing_to_do(luil::plan_access_request(bar, { luil::access_command::set_value, 60.0f }));
}

TEST_CASE("Display-only value elements refuse to be set", "[ui][access]")
{
    // 표시 전용 막대는 끌기도 키도 없다 — 값 정하기도 같다. 판정이 산술보다 앞이라
    // 지금 값으로 물어도 "없음"이 나온다 — `IsReadOnly`가 그 답에 얹힌다.
    const luil::slider_element display { luil::ui_element_id { kind_item, u8"level" }, { .maximum = 10.0f, .value = 3.0f } };
    REQUIRE(display.accessibility().range.has_value());
    REQUIRE(luil::plan_access_request(display, { luil::access_command::set_value, 5.0f }).has_value() == false);
    REQUIRE(luil::plan_access_request(display, { luil::access_command::set_value, 3.0f }).has_value() == false);

    // 델타 factory만 있는 막대도 값 정하기는 거절한다 — 절대 메시지가 없으면
    // 환산으로 물러서지 않는다. 그 환산이 곧 겹쳐 쌓이는 길이다.
    luil::slider_config delta_only {};
    delta_only.maximum = 10.0f;
    delta_only.value = 3.0f;
    delta_only.change = [](const float delta) { return luil::make_app_action(access_delta_intent { delta }); };
    const luil::slider_element relative { luil::ui_element_id { kind_item, u8"volume" }, delta_only };
    REQUIRE(luil::plan_access_request(relative, { luil::access_command::set_value, 5.0f }).has_value() == false);

    const luil::progress_element progress { luil::ui_element_id { kind_item, u8"load" }, { .value = 0.4f } };
    REQUIRE(luil::plan_access_request(progress, { luil::access_command::set_value, 0.8f }).has_value() == false);

    // 흘릴 것이 없는 막대도 마찬가지다.
    luil::scrollbar_config short_config {};
    short_config.scroll = [](const float delta) { return luil::make_app_action(access_delta_intent { delta }); };
    short_config.scroll_to = [](const float offset) { return luil::make_app_action(access_value_intent { offset }); };
    short_config.content_height = 50.0f;
    short_config.viewport_height = 100.0f;
    luil::scrollbar_element still { luil::ui_element_id { kind_item, u8"scroll" }, short_config };
    still.arrange({ { 0.0f, 0.0f, 8.0f, 100.0f }, 1.0f });
    REQUIRE(luil::plan_access_request(still, { luil::access_command::set_value, 10.0f }).has_value() == false);
}

TEST_CASE("A dropdown opens and closes through an absolute message", "[ui][access]")
{
    const auto set_open { [](const bool open) { return luil::make_app_action(access_state_intent { u8"roast", open }); } };
    const luil::dropdown_element closed { { .owner = u8"roast", .placeholder = u8"원두", .open = false, .set_open = set_open } };
    // 열기는 목표 상태를 실은 절대 메시지다 — 같은 명령 둘이 열었다 도로 닫지
    // 않는다.
    REQUIRE(state_of(luil::plan_access_request(closed, { luil::access_command::expand })).state == true);
    REQUIRE(state_of(luil::plan_access_request(closed, { luil::access_command::expand })).state == true);
    require_nothing_to_do(luil::plan_access_request(closed, { luil::access_command::collapse }));
    // 절대 메시지만 있으면 클릭도 그 factory에서 나온다 (지금 상태의 반대).
    const luil::ui_action* const click { closed.action(luil::ui_trigger::left_click) };
    REQUIRE(click != nullptr);
    REQUIRE(state_of((*click)({})).state == true);

    // 토글 액션만 있으면 클릭은 서지만 접근 실행은 거절된다.
    const luil::dropdown_element toggling { { .owner = u8"roast", .placeholder = u8"원두", .open = false, .toggle = tagged_action(u8"roast") } };
    REQUIRE(luil::plan_access_request(toggling, { luil::access_command::expand }).has_value() == false);
}

TEST_CASE("Only elements a person could reach are execution targets", "[ui][access]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto behind { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"behind" }, luil::text_button_config { .text = u8"뒤" }) };
    behind->set_action(luil::ui_trigger::left_click, tagged_action(u8"behind"));
    root->add(std::move(behind));
    auto hidden { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"hidden" }, luil::text_button_config { .text = u8"숨음" }) };
    hidden->set_visible(false);
    root->add(std::move(hidden));

    const luil::ui_tree open { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    REQUIRE(luil::access_reachable(open, { kind_item, u8"behind" }));
    // 보이지 않는 것은 읽을 자리도 실행할 자리도 아니다 (넘치지 않은 탭의 넘침 버튼이 그렇다).
    REQUIRE(luil::access_reachable(open, { kind_item, u8"hidden" }) == false);
    REQUIRE(luil::access_reachable(open, { kind_item, u8"gone" }) == false);

    // 가둠이 서면 그 밖은 손이 닿지 않는다 — scrim이 포인터를, 가둠이 키보드를 막는 그 자리다.
    auto trapped_root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto outside { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"behind" }, luil::text_button_config { .text = u8"뒤" }) };
    outside->set_action(luil::ui_trigger::left_click, tagged_action(u8"behind"));
    trapped_root->add(std::move(outside));
    auto dialog { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"dialog" }) };
    dialog->set_focus_trap(true);
    auto inside { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"inside" }, luil::text_button_config { .text = u8"안" }) };
    inside->set_action(luil::ui_trigger::left_click, tagged_action(u8"inside"));
    dialog->add(std::move(inside));
    trapped_root->add(std::move(dialog));

    const luil::ui_tree modal { luil::make_arranged_tree(std::move(trapped_root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    REQUIRE(luil::access_reachable(modal, { kind_item, u8"inside" }));
    REQUIRE(luil::access_reachable(modal, { kind_item, u8"behind" }) == false);
}

TEST_CASE("A child under a hidden parent is out of reach", "[ui][access]")
{
    // 부모 panel만 숨기면 자식의 visible 플래그는 참으로 남는다 — 경로 전체를
    // 보지 않으면 사람이 누를 수 없는 것을 보조 기술이 누르게 된다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    auto section { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"section" }) };
    auto child { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_item, u8"child" }, luil::text_button_config { .text = u8"자식" }) };
    child->set_action(luil::ui_trigger::left_click, tagged_action(u8"child"));
    section->add(std::move(child));
    section->set_visible(false);
    root->add(std::move(section));

    const luil::ui_tree tree { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    // 색인에는 있고 자기 플래그도 참이다 — 숨은 것은 조상이다.
    const luil::ui_element* const found { tree.find({ kind_item, u8"child" }) };
    REQUIRE(found != nullptr);
    REQUIRE(found->visible());
    REQUIRE(luil::access_reachable(tree, { kind_item, u8"child" }) == false);
    REQUIRE(tree.visibly_contains({ kind_item, u8"child" }) == false);
    // 조상이 숨은 자리는 접근 색인에도 없다 — 탐색도 같은 답이다.
    REQUIRE(luil::access_parent(tree, { kind_item, u8"child" }) == nullptr);
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"child" }, true) == nullptr);
}

TEST_CASE("Accessibility queries answer an empty tree without a root", "[ui][access]")
{
    // 공개 API가 null root를 허용하므로 접근성 질의도 역참조 없이 "없다"로 답한다.
    const luil::ui_tree empty { nullptr };
    REQUIRE(luil::access_reachable(empty, { kind_item, u8"gone" }) == false);
    REQUIRE(luil::access_parent(empty, { kind_item, u8"gone" }) == nullptr);
    REQUIRE(luil::access_sibling(empty, { kind_item, u8"gone" }, true) == nullptr);
    REQUIRE(empty.access_top_level().empty());
    REQUIRE(luil::access_element_at(empty, 10.0f, 10.0f) == nullptr);
}

TEST_CASE("The access tree collapses structural containers and skips hidden branches", "[ui][access]")
{
    test_panel root { luil::ui_element_id { kind_panel, u8"root" } };
    root.add(make_note(u8"outer", u8"바깥"));
    auto inner { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"inner" }) };
    inner->add(make_note(u8"nested", u8"안쪽"));
    root.add(std::move(inner));
    auto hidden { make_note(u8"hidden", u8"숨김") };
    hidden->set_visible(false);
    root.add(std::move(hidden));

    const std::vector<const luil::ui_element*> children { luil::access_children(root) };
    REQUIRE(children.size() == 2);
    REQUIRE(children[0]->accessibility().name == u8"바깥");
    REQUIRE(children[1]->accessibility().name == u8"안쪽");
}

TEST_CASE("The access parent is the nearest accessible ancestor", "[ui][access]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    luil::group_config section_config { .owner = u8"advanced", .title = u8"고급", .toggle = noop_action(), .content_height = 20.0f };
    auto section { std::make_unique<luil::group_element>(std::move(section_config), make_note(u8"nested", u8"안쪽")) };
    root->add(std::move(section));
    root->add(make_note(u8"outer", u8"바깥"));
    const luil::ui_tree tree { std::move(root) };

    const luil::ui_element_id group_id { luil::ui_element_kind::group, u8"advanced" };
    REQUIRE(luil::access_parent(tree, { kind_item, u8"nested" }) == tree.find(group_id));
    // 구조(root panel)만 감싼 자리의 부모는 창(fragment root)이다.
    REQUIRE(luil::access_parent(tree, group_id) == nullptr);
    REQUIRE(luil::access_parent(tree, { kind_item, u8"outer" }) == nullptr);
}

TEST_CASE("Access siblings follow the collapsed order and stay inside their parent", "[ui][access]")
{
    // outer1 / 구조 panel [nested1, nested2] / outer2 / 숨긴 라벨.
    // 구조는 접히므로 최상위 형제 줄은 [outer1, nested1, nested2, outer2]다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    root->add(make_note(u8"outer1", u8"앞"));
    auto inner { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"inner" }) };
    inner->add(make_note(u8"nested1", u8"안1"));
    inner->add(make_note(u8"nested2", u8"안2"));
    root->add(std::move(inner));
    root->add(make_note(u8"outer2", u8"뒤"));
    auto hidden { make_note(u8"hidden", u8"숨김") };
    hidden->set_visible(false);
    root->add(std::move(hidden));
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

    // 접기를 지나 한 줄이다 — 구조 경계에서 형제가 끊기지 않는다.
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"outer1" }, true) == tree.find({ kind_item, u8"nested1" }));
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"nested2" }, true) == tree.find({ kind_item, u8"outer2" }));
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"nested1" }, false) == tree.find({ kind_item, u8"outer1" }));
    // 숨은 것은 줄에 서지 않는다 — 마지막 보이는 자리에서 다음은 없다.
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"outer2" }, true) == nullptr);
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"outer1" }, false) == nullptr);
    // 구조와 숨은 것과 없는 id는 형제 줄의 자리가 아니다.
    REQUIRE(luil::access_sibling(tree, { kind_panel, u8"inner" }, true) == nullptr);
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"hidden" }, true) == nullptr);
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"hidden" }, false) == nullptr);
    REQUIRE(luil::access_sibling(tree, { kind_item, u8"gone" }, true) == nullptr);

    // 접근 부모가 있는 자리는 그 안에서만 돈다 — 밖의 형제로 새지 않는다.
    auto grouped_root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    luil::group_config section_config { .owner = u8"advanced", .title = u8"고급", .toggle = noop_action(), .content_height = 20.0f };
    auto section { std::make_unique<luil::group_element>(std::move(section_config), make_note(u8"nested", u8"안쪽")) };
    grouped_root->add(std::move(section));
    grouped_root->add(make_note(u8"outer", u8"바깥"));
    const luil::ui_tree grouped { std::move(grouped_root) };

    const luil::ui_element_id group_id { luil::ui_element_kind::group, u8"advanced" };
    REQUIRE(luil::access_sibling(grouped, group_id, true) == grouped.find({ kind_item, u8"outer" }));
    REQUIRE(luil::access_sibling(grouped, { kind_item, u8"outer" }, false) == grouped.find(group_id));
    // 묶음 안의 이웃은 머리행이고, 끝에서 밖(outer)으로 넘어가지 않는다.
    REQUIRE(luil::access_sibling(grouped, { kind_item, u8"nested" }, false) == grouped.find({ luil::ui_element_kind::group_header, u8"advanced" }));
    REQUIRE(luil::access_sibling(grouped, { kind_item, u8"nested" }, true) == nullptr);
}

TEST_CASE("An access snapshot flattens the collapsed tree in publish order", "[ui][access]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    root->add(make_note(u8"outer", u8"바깥"));
    luil::group_config section_config { .owner = u8"advanced", .title = u8"고급", .toggle = noop_action(), .content_height = 20.0f };
    root->add(std::make_unique<luil::group_element>(std::move(section_config), make_note(u8"nested", u8"안쪽")));
    auto hidden { make_note(u8"hidden", u8"숨김") };
    hidden->set_visible(false);
    root->add(std::move(hidden));
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

    // 접기 규칙 그대로다: root panel은 접히고, 숨긴 가지는 빠지며, 게시 순서로
    // 늘어선다. 부모 표식은 감싸는 접근 요소다 (빈 id면 표면 root).
    const luil::access_snapshot snapshot { luil::make_access_snapshot(tree) };
    REQUIRE(snapshot.entries.size() == 4u);
    REQUIRE(snapshot.entries[0].id.owner == u8"outer");
    REQUIRE(snapshot.entries[0].parent == luil::ui_element_id {});
    REQUIRE(snapshot.entries[1].id.owner == u8"advanced");
    REQUIRE(snapshot.entries[2].info.role == luil::access_role::button);
    REQUIRE(snapshot.entries[2].parent == snapshot.entries[1].id);
    REQUIRE(snapshot.entries[3].id.owner == u8"nested");
    REQUIRE(snapshot.entries[3].parent == snapshot.entries[1].id);

    // 빈 tree의 발행본도 비어 있다.
    REQUIRE(luil::make_access_snapshot(luil::ui_tree { nullptr }).entries.empty());
}

namespace {
    // 상태를 바꿔 가며 같은 화면을 두 번 짓는 도우미다 (frame마다 새 tree).
    [[nodiscard]] luil::ui_tree make_state_tree(const bool checked, const float volume, const bool collapsed, const std::u8string_view note)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        root->add(std::make_unique<luil::check_element>(luil::check_config { .owner = u8"alerts", .label = u8"알림", .checked = checked, .toggle = noop_action() }));
        root->add(std::make_unique<luil::slider_element>(luil::ui_element_id { kind_item, u8"volume" },
            luil::slider_config { .minimum = 0.0f, .maximum = 10.0f, .value = volume }));
        root->add(std::make_unique<luil::group_element>(luil::group_config { .owner = u8"advanced", .title = u8"고급", .collapsed = collapsed, .toggle = noop_action() }));
        root->add(make_note(u8"hint", note));
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f);
    }
} // namespace

TEST_CASE("Snapshot diff reports each changed element once", "[ui][access]")
{
    const luil::access_snapshot before { luil::make_access_snapshot(make_state_tree(false, 3.0f, false, u8"안내")) };

    // 같은 발행본이면 알릴 것이 없다.
    REQUIRE(luil::diff_access_snapshots(before, luil::make_access_snapshot(make_state_tree(false, 3.0f, false, u8"안내"))).empty());

    // 네 요소가 각각 바뀌면 property 기록이 요소마다 하나씩이다 — 이전·현재
    // 정보의 짝이 실려, 어느 낱말이 바뀌었는지는 받는 쪽이 편다.
    const luil::access_snapshot after { luil::make_access_snapshot(make_state_tree(true, 7.0f, true, u8"다른 안내")) };
    const std::vector<luil::access_change> changes { luil::diff_access_snapshots(before, after) };
    REQUIRE(changes.size() == 4u);
    for (const luil::access_change& change : changes)
        REQUIRE(change.kind == luil::access_change_kind::property);
    REQUIRE(changes[0].id.owner == u8"alerts");
    REQUIRE(changes[0].previous.checked == false);
    REQUIRE(changes[0].current.checked == true);
    REQUIRE(changes[1].id.owner == u8"volume");
    REQUIRE(changes[1].previous.range->value == 3.0f);
    REQUIRE(changes[1].current.range->value == 7.0f);
    REQUIRE(changes[2].id.owner == u8"advanced");
    REQUIRE(changes[2].previous.expanded == true);
    REQUIRE(changes[2].current.expanded == false);
    REQUIRE(changes[3].id.owner == u8"hint");
    REQUIRE(changes[3].current.name == u8"다른 안내");
}

TEST_CASE("Snapshot diff turns a new selection into one selected record", "[ui][access]")
{
    const auto make_list_tree = [](const std::u8string_view selected) {
        luil::list_config config {};
        config.owner = u8"files";
        config.items = { { .key = u8"a", .label = u8"가" }, { .key = u8"b", .label = u8"나" } };
        config.selected = std::u8string { selected };
        config.select = [](const std::u8string&) { return luil::input_action {}; };
        return luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 48.0f }, 1.0f);
    };

    const luil::access_snapshot before { luil::make_access_snapshot(make_list_tree(u8"a")) };
    const luil::access_snapshot after { luil::make_access_snapshot(make_list_tree(u8"b")) };
    // 단일 선택이라 새로 골라진 항목의 알림 하나가 곧 그 사건이다 — 풀린 쪽(a)은
    // property로도 겹쳐 알리지 않는다.
    const std::vector<luil::access_change> changes { luil::diff_access_snapshots(before, after) };
    REQUIRE(changes.size() == 1u);
    REQUIRE(changes.front().kind == luil::access_change_kind::selected);
    REQUIRE(changes.front().id.owner == u8"b");
}

TEST_CASE("Snapshot diff reports structure changes on the parent", "[ui][access]")
{
    const auto make_panel_tree = [](const bool with_extra) {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        root->add(make_note(u8"first", u8"하나"));
        if (with_extra)
            root->add(make_note(u8"second", u8"둘"));
        luil::group_config section_config { .owner = u8"advanced", .title = u8"고급", .toggle = noop_action(), .content_height = 20.0f };
        root->add(std::make_unique<luil::group_element>(std::move(section_config), make_note(u8"nested", u8"안쪽")));
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f);
    };

    // 최상위에 요소가 들면 표면 root(빈 id)의 structure 하나다.
    const luil::access_snapshot narrow { luil::make_access_snapshot(make_panel_tree(false)) };
    const luil::access_snapshot wide { luil::make_access_snapshot(make_panel_tree(true)) };
    const std::vector<luil::access_change> grown { luil::diff_access_snapshots(narrow, wide) };
    REQUIRE(grown.size() == 1u);
    REQUIRE(grown.front().kind == luil::access_change_kind::structure);
    REQUIRE(grown.front().id == luil::ui_element_id {});

    // 그룹이 접혀 내용이 사라지면 그 그룹의 property(펼침)와 structure가 함께
    // 난다 — 내용이 없어진 부모는 그룹이지 표면 root가 아니다.
    const auto make_group_tree = [](const bool collapsed) {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
        luil::group_config section_config { .owner = u8"advanced", .title = u8"고급", .collapsed = collapsed, .toggle = noop_action(), .content_height = 20.0f };
        root->add(std::make_unique<luil::group_element>(std::move(section_config), make_note(u8"nested", u8"안쪽")));
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f);
    };
    const std::vector<luil::access_change> collapsed {
        luil::diff_access_snapshots(luil::make_access_snapshot(make_group_tree(false)), luil::make_access_snapshot(make_group_tree(true))),
    };
    REQUIRE(collapsed.size() == 2u);
    REQUIRE(collapsed[0].kind == luil::access_change_kind::property);
    REQUIRE(collapsed[0].id.owner == u8"advanced");
    REQUIRE(collapsed[1].kind == luil::access_change_kind::structure);
    REQUIRE(collapsed[1].id == luil::ui_element_id { luil::ui_element_kind::group, u8"advanced" });
}

TEST_CASE("The point query hits read-only elements and stops at an absorbing surface", "[ui][access]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    root->add(make_note(u8"note", u8"안내"));
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

    // 상호작용 hit는 라벨을 지나치지만 접근 질의는 맞힌다 — 두 술어가 갈리는 지점이다.
    REQUIRE(tree.hit_test(50.0f, 50.0f) == nullptr);
    const luil::ui_element* const hit { luil::access_element_at(tree, 50.0f, 50.0f) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id().owner == u8"note");

    // scrim이 흡수한 좌표를 그 아래에서 다시 찾지 않는다.
    auto covered { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"root" }) };
    covered->add(make_note(u8"note", u8"안내"));
    auto scrim { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"scrim" }) };
    scrim->set_hit_opaque(true);
    covered->add(std::move(scrim));
    const luil::ui_tree blocked { luil::make_arranged_tree(std::move(covered), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    REQUIRE(luil::access_element_at(blocked, 50.0f, 50.0f) == nullptr);
}

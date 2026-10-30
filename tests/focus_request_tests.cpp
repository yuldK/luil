#include "host/focus_request_tracker.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {
    using luil::focus_request;
    using luil::focus_request_outcome;
    using luil::focus_request_tracker;
    using clock_type = std::chrono::steady_clock;

    const luil::ui_element_id field_id { luil::application_element_kind(1), u8"field" };

    [[nodiscard]] std::unique_ptr<luil::text_button_element> make_field(const luil::rect_f& slot)
    {
        auto field { std::make_unique<luil::text_button_element>(field_id, luil::text_button_config { .text = u8"검색" }) };
        field->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        field->arrange({ slot, 1.0f });
        return field;
    }

    // 400 × 300 표면에 칸 하나를 세운 tree다.
    [[nodiscard]] luil::ui_tree make_tree(std::unique_ptr<luil::ui_element> field)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
        if (field != nullptr)
            root->add(std::move(field));
        return luil::ui_tree { std::move(root) };
    }

    // 자식을 자기 bounds 안으로 자르는 흘리는 창의 대역이다.
    class clipping_view final : public luil::ui_element
    {
    public:
        clipping_view()
            : ui_element { luil::ui_element_id { luil::application_element_kind(2), u8"view" } }
        {
            set_clip_children(true);
        }

        void add(std::unique_ptr<luil::ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] focus_request make_request(const std::uint64_t id)
    {
        return focus_request { .id = id, .surface = u8"tool", .target = field_id, .timeout = std::chrono::milliseconds { 500 } };
    }
}

TEST_CASE("A focus target is ready once it stands visible and focusable", "[focus][deferred]")
{
    // 아직 없는 칸은 서지 않았다.
    REQUIRE(luil::focus_target_ready(make_tree(nullptr), field_id) == false);
    REQUIRE(luil::focus_target_ready(make_tree(make_field({ 20.0f, 20.0f, 80.0f, 28.0f })), field_id));

    // 숨긴 칸·비활성 칸은 초점을 받을 수 없다.
    auto hidden { make_field({ 20.0f, 20.0f, 80.0f, 28.0f }) };
    hidden->set_visible(false);
    REQUIRE(luil::focus_target_ready(make_tree(std::move(hidden)), field_id) == false);
    auto disabled { make_field({ 20.0f, 20.0f, 80.0f, 28.0f }) };
    disabled->set_enabled(false);
    REQUIRE(luil::focus_target_ready(make_tree(std::move(disabled)), field_id) == false);

    // 표면 밖으로 흘려 감춘 칸도 아직 서지 않았다.
    REQUIRE(luil::focus_target_ready(make_tree(make_field({ 20.0f, 400.0f, 80.0f, 28.0f })), field_id) == false);
}

TEST_CASE("A held request is fulfilled the moment its target stands", "[focus][deferred]")
{
    focus_request_tracker tracker {};
    const clock_type::time_point start {};
    bool standing { false };
    const auto ready = [&standing](const focus_request&) { return standing; };

    // 요청한 frame에는 대상이 없다. 들고 있는다.
    focus_request_tracker::step step { tracker.advance(make_request(1), start, ready) };
    REQUIRE(step.fulfil.has_value() == false);
    REQUIRE(step.reports.empty());
    REQUIRE(tracker.deadline() == start + std::chrono::milliseconds { 500 });

    // 다음 frame에 대상이 섰다. 한 번 이행한다.
    standing = true;
    step = tracker.advance(make_request(1), start + std::chrono::milliseconds { 16 }, ready);
    REQUIRE(step.fulfil.has_value());
    REQUIRE(step.fulfil->target == field_id);
    REQUIRE(step.fulfil->surface == u8"tool");
    REQUIRE(step.reports.size() == 1);
    REQUIRE(step.reports[0].id == 1);
    REQUIRE(step.reports[0].outcome == focus_request_outcome::fulfilled);
    REQUIRE(tracker.deadline().has_value() == false);

    // 같은 번호를 다시 실어도 다시 이행하지 않는다.
    step = tracker.advance(make_request(1), start + std::chrono::milliseconds { 32 }, ready);
    REQUIRE(step.fulfil.has_value() == false);
    REQUIRE(step.reports.empty());
}

TEST_CASE("A newer request supersedes the one being held", "[focus][deferred]")
{
    focus_request_tracker tracker {};
    const clock_type::time_point start {};
    const auto never = [](const focus_request&) { return false; };
    static_cast<void>(tracker.advance(make_request(1), start, never));

    const focus_request_tracker::step step { tracker.advance(make_request(2), start + std::chrono::milliseconds { 10 }, never) };
    REQUIRE(step.reports.size() == 1);
    REQUIRE(step.reports[0].id == 1);
    REQUIRE(step.reports[0].outcome == focus_request_outcome::superseded);
    // 새 요청의 기한은 받은 때부터다.
    REQUIRE(tracker.deadline() == start + std::chrono::milliseconds { 510 });
}

TEST_CASE("A request whose target never stands expires at its deadline", "[focus][deferred]")
{
    focus_request_tracker tracker {};
    const clock_type::time_point start {};
    const auto never = [](const focus_request&) { return false; };
    static_cast<void>(tracker.advance(make_request(7), start, never));

    // frame이 오지 않아도 기한에 다시 부르면 버린다. 요청이 빠진 frame도 같다.
    REQUIRE(tracker.advance(std::nullopt, start + std::chrono::milliseconds { 499 }, never).reports.empty());
    const focus_request_tracker::step step { tracker.advance(std::nullopt, start + std::chrono::milliseconds { 500 }, never) };
    REQUIRE(step.fulfil.has_value() == false);
    REQUIRE(step.reports.size() == 1);
    REQUIRE(step.reports[0].id == 7);
    REQUIRE(step.reports[0].outcome == focus_request_outcome::expired);
    REQUIRE(tracker.deadline().has_value() == false);
}

TEST_CASE("Request zero means no request", "[focus][deferred]")
{
    focus_request_tracker tracker {};
    const auto always = [](const focus_request&) { return true; };
    const focus_request_tracker::step step { tracker.advance(make_request(0), clock_type::time_point {}, always) };
    REQUIRE(step.fulfil.has_value() == false);
    REQUIRE(step.reports.empty());
}

TEST_CASE("A field scrolled out of a clipping view stands once it scrolls back in", "[focus][deferred]")
{
    // 흘리는 창 밖의 칸은 tree에 있어도 잘려 보이지 않는다. 앱이 같은 frame에 드러내는 스크롤을 실으면
    // 다시 지은 tree에서 칸이 창 안에 선다.
    const auto make_scrolled_tree = [](const float field_y) {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
        auto view { std::make_unique<clipping_view>() };
        view->arrange({ { 0.0f, 0.0f, 400.0f, 100.0f }, 1.0f });
        view->add(make_field({ 20.0f, field_y, 80.0f, 28.0f }));
        root->add(std::move(view));
        return luil::ui_tree { std::move(root) };
    };
    REQUIRE(luil::focus_target_ready(make_scrolled_tree(180.0f), field_id) == false);
    REQUIRE(luil::focus_target_ready(make_scrolled_tree(40.0f), field_id));
}

#include "mobile_demo/device_page.h"
#include "mobile_demo/sheets_page.h"
#include "mobile_demo/smb_page.h"
#include "mobile_demo/zoom_page.h"

#include "luil/ui/ui_interaction.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using time_point = std::chrono::steady_clock::time_point;

    template<typename Message, typename Page>
    void deliver(Page& page, Message message)
    {
        REQUIRE(page.handle(luil::app_message { std::move(message) }));
    }

    // 시각을 정해 보내는 짝이다. 시트의 스프링은 메시지를 받은 시각에서 출발한다.
    template<typename Message, typename Page>
    void deliver_at(Page& page, Message message, const time_point now)
    {
        REQUIRE(page.handle(luil::app_message { std::move(message) }, now));
    }

    [[nodiscard]] time_point after_milliseconds(const time_point start, const int milliseconds)
    {
        return start + std::chrono::milliseconds { milliseconds };
    }

    // 400x800 창에서 `now` 시각의 시트 tree다.
    [[nodiscard]] luil::ui_tree sheet_tree_at(mobile_demo::sheets_page& page, const time_point now)
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, 400.0f, 800.0f, 1.0f, {}, now);
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 400.0f, 800.0f }, 1.0f);
    }

    // `now` 시각에 보이는 시트 표면의 높이다 (논리 픽셀, 배율 1).
    [[nodiscard]] float sheet_height_at(mobile_demo::sheets_page& page, const time_point now)
    {
        const auto tree { sheet_tree_at(page, now) };
        const auto* const host { tree.find({ luil::ui_element_kind::sheet_host, u8"demo-sheet" }) };
        REQUIRE(host != nullptr);
        return static_cast<const luil::sheet_host_element*>(host)->surface_bounds().height;
    }

    [[nodiscard]] std::u8string motion_state_at(mobile_demo::sheets_page& page, const time_point now)
    {
        const auto tree { sheet_tree_at(page, now) };
        const auto* const label { tree.find({ mobile_demo::kind_text, u8"sheets-motion" }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    // 시트 안의 단추를 눌러 나온 메시지다. 실제 입력 경로를 그대로 탄다.
    [[nodiscard]] std::optional<luil::app_message> press_button(mobile_demo::sheets_page& page, const time_point now, const std::u8string& key)
    {
        auto tree { std::make_shared<const luil::ui_tree>(sheet_tree_at(page, now)) };
        const luil::ui_element* const button { tree->find({ mobile_demo::kind_layout, u8"sheet-" + key }) };
        REQUIRE(button != nullptr);
        const luil::rect_f box { button->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        for (const luil::input_action& action : controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } }))
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                return *message;
        return std::nullopt;
    }

    [[nodiscard]] luil::ui_tree sheet_tree(mobile_demo::sheets_page& page, const float width, const float height)
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, width, height, 1.0f, {});
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, 1.0f);
    }

    // 창 폭 400dp에서 조립해 배치한 기기 페이지다. 움직임 줄이기는 창 metrics가 싣는 값이다.
    [[nodiscard]] luil::ui_tree device_tree(mobile_demo::device_page& page, const bool reduce_motion)
    {
        const mobile_demo::window_metrics_intent window { .width = 400.0f, .height = 800.0f, .scale = 1.0f, .reduce_motion = reduce_motion };
        auto content { page.build(400.0f, 1.0f, window) };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f);
    }

    // 400x800 창에서 조립해 배치한 확대 보기 페이지다 (배율 1).
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> zoom_tree(mobile_demo::zoom_page& page)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(page.build(400.0f, 800.0f, 1.0f), { 0.0f, 0.0f, 400.0f, 800.0f }, 1.0f));
    }

    // UIA가 되읽는 이름이 화면의 글과 같다.
    [[nodiscard]] std::u8string zoom_text(mobile_demo::zoom_page& page, const std::u8string& owner)
    {
        const auto tree { zoom_tree(page) };
        const auto* const label { tree->find({ mobile_demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] luil::rect_f zoom_node_bounds(mobile_demo::zoom_page& page, const int index)
    {
        const auto tree { zoom_tree(page) };
        const auto* const node { tree->find({ mobile_demo::kind_text, mobile_demo::to_u8(index) }) };
        REQUIRE(node != nullptr);
        return node->bounds();
    }

    // 페이지가 받은 메시지 수를 센다. 컨트롤러가 낸 동작 가운데 앱 메시지만 페이지로 돌려준다.
    int deliver_actions(mobile_demo::zoom_page& page, const std::vector<luil::input_action>& actions)
    {
        int delivered { 0 };
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
            {
                REQUIRE(page.handle(*message));
                ++delivered;
            }
        return delivered;
    }

    // 단추를 눌러 나온 메시지를 페이지에 돌려준다. 실제 입력 경로를 그대로 탄다. 돌려준 수를 돌려준다.
    int press_zoom_button(mobile_demo::zoom_page& page, const std::u8string& owner)
    {
        const auto tree { zoom_tree(page) };
        const auto* const button { tree->find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        const luil::rect_f box { button->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        return deliver_actions(page, controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } }));
    }

    // 요청을 기록하고 사람이 고른 차례로 답한다. 실제 페이지의 send·cancel·응답 처리 경로를 그대로 쓴다.
    struct smb_fixture
    {
        std::vector<luil::net::smb_request> requests {};
        std::vector<luil::net::smb_ticket> cancelled {};
        std::uint64_t next_ticket { 0 };
        int discoveries { 0 };
        bool reject { false };
        mobile_demo::smb_page page { transport() };

        [[nodiscard]] mobile_demo::smb_page_transport transport()
        {
            return {
                .send =
                    [this](luil::net::smb_request request, std::u8string& error) {
                        requests.push_back(std::move(request));
                        if (reject)
                        {
                            error = u8"busy";
                            return luil::net::smb_ticket {};
                        }
                        return luil::net::smb_ticket { ++next_ticket };
                    },
                .cancel = [this](const luil::net::smb_ticket ticket) { cancelled.push_back(ticket); },
                .discover =
                    [this](const luil::net::smb_discovery_request, std::u8string&) {
                        ++discoveries;
                        return luil::net::smb_ticket { ++next_ticket };
                    },
            };
        }

        smb_fixture()
        {
            address(u8"nas/share");
            deliver(page, mobile_demo::smb_open_intent {});
            luil::net::smb_response response {
                .ticket = { next_ticket },
                .entries = { { .name = u8"photos", .directory = true }, { .name = u8"old.txt" } },
            };
            deliver(page, mobile_demo::smb_result_intent { std::move(response) });
        }

        void address(std::u8string value)
        {
            deliver(page,
                mobile_demo::edit_intent { luil::text_edit_request { .target = mobile_demo::target_smb_address, .command = luil::text::text_edit_command::replace_all, .text = std::move(value) } });
        }

        [[nodiscard]] bool has_entry(const std::u8string& name)
        {
            auto content { page.build(400.0f, 1.0f) };
            const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
            return tree.find({ mobile_demo::kind_smb_entry, u8"smb-entry-" + name }) != nullptr;
        }
    };
}

TEST_CASE("The device page states whether the system reduces motion", "[mobile-demo][reduce_motion]")
{
    mobile_demo::device_page page {};
    for (const bool reduced : { false, true })
    {
        CAPTURE(reduced);
        const std::u8string_view expected { reduced ? std::u8string_view { u8"켜짐 · 도는 표시가 멈춘다" } : std::u8string_view { u8"꺼짐 · 도는 표시가 돈다" } };
        const auto tree { device_tree(page, reduced) };
        // UIA가 되읽는 이름이 화면의 글과 같다.
        REQUIRE(tree.find({ mobile_demo::kind_text, u8"motion-heading" }) != nullptr);
        const auto* const state { tree.find({ mobile_demo::kind_text, u8"motion-state" }) };
        REQUIRE(state != nullptr);
        REQUIRE(state->accessibility().name == expected);
        // 상태 밑에 도는 표시가 선다. 멈춘 그림으로 서는 것은 시스템 값을 받는 쪽(draw_context)이 정한다.
        const auto* const spinner { tree.find({ mobile_demo::kind_layout, u8"motion-spinner" }) };
        REQUIRE(spinner != nullptr);
        REQUIRE(spinner->accessibility().role == luil::access_role::progress);
        REQUIRE(spinner->bounds().y > state->bounds().y);
    }
}

TEST_CASE("The demo clears an interrupted adaptive sheet drag while preserving its stop and list", "[ui][sheet][mobile-demo]")
{
    for (const auto detent : { luil::sheet_detent::half, luil::sheet_detent::full })
    {
        CAPTURE(detent);
        mobile_demo::sheets_page page {};
        deliver(page, mobile_demo::sheet_open_intent {});
        deliver(page, mobile_demo::sheet_settle_intent { detent });
        deliver(page, mobile_demo::sheet_scroll_intent { 150.0f });
        deliver(page, mobile_demo::sheet_drag_intent { 20.0f });
        const float height { detent == luil::sheet_detent::half ? 400.0f : 776.0f };
        const luil::ui_element_id id { luil::ui_element_kind::sheet_host, u8"demo-sheet" };
        auto portrait { sheet_tree(page, 400.0f, 800.0f) };
        REQUIRE(static_cast<const luil::sheet_host_element*>(portrait.find(id))->surface_bounds().height == height - 20.0f);
        const auto landscape { sheet_tree(page, 800.0f, 400.0f) };
        REQUIRE(static_cast<const luil::sheet_host_element*>(landscape.find(id))->kind() == luil::sheet_kind::end);
        portrait = sheet_tree(page, 400.0f, 800.0f);
        REQUIRE(page.sheet_open());
        REQUIRE(static_cast<const luil::sheet_host_element*>(portrait.find(id))->surface_bounds().height == height);
        const auto* const list { portrait.find({ mobile_demo::kind_page_scroll, u8"sheet-list" }) };
        REQUIRE(list != nullptr);
        REQUIRE(list->scroll()->at_start == false);
    }
}

TEST_CASE("A released sheet continues from the height and velocity it was let go at", "[ui][sheet][mobile-demo][spring]")
{
    const time_point start { time_point {} + std::chrono::seconds { 1 } };
    // 위로 올리며 놓는다: 300에서 전체(776)로, 초당 600 논리 픽셀로 오르는 중이다.
    {
        mobile_demo::sheets_page page {};
        deliver_at(page, mobile_demo::sheet_open_intent {}, start);
        deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::full, false }, 300.0f, 776.0f, 600.0f } }, start);
        // 놓은 직후의 높이가 놓은 높이이고, 기울기가 놓은 속도다. 아주 짧은 간격으로 잰다 (스프링의 가속이 섞이지 않게).
        REQUIRE(sheet_height_at(page, start) == Catch::Approx(300.0f).margin(1.0e-3));
        const float later { sheet_height_at(page, start + std::chrono::microseconds { 20 }) };
        REQUIRE((later - 300.0f) / 20.0e-6f == Catch::Approx(600.0f).epsilon(0.03));
        // 도는 동안은 다음 frame을 예고하고, 위로 넘치는 만큼은 전체 높이에서 잘린다.
        REQUIRE(page.next_update(start).has_value());
        for (int milliseconds { 0 }; milliseconds <= 1000; milliseconds += 10)
            REQUIRE(sheet_height_at(page, after_milliseconds(start, milliseconds)) <= 776.0f + 1.0e-3f);
        // 끝나면 정확히 멈춤점이고 더는 깨우지 않는다.
        REQUIRE(sheet_height_at(page, after_milliseconds(start, 5000)) == 776.0f);
        REQUIRE(page.next_update(after_milliseconds(start, 5000)).has_value() == false);
    }
    // 아래로 내리며 놓는다: 600에서 반(400)으로, 초당 900 논리 픽셀로 내려가는 중이다.
    {
        mobile_demo::sheets_page page {};
        deliver_at(page, mobile_demo::sheet_open_intent {}, start);
        deliver_at(page, mobile_demo::sheet_settle_intent { luil::sheet_detent::full }, start);
        deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::half, false }, 600.0f, 400.0f, -900.0f } }, start);
        REQUIRE(sheet_height_at(page, start) == Catch::Approx(600.0f).margin(1.0e-3));
        const float later { sheet_height_at(page, start + std::chrono::microseconds { 20 }) };
        REQUIRE((later - 600.0f) / 20.0e-6f == Catch::Approx(-900.0f).epsilon(0.03));
        REQUIRE(sheet_height_at(page, after_milliseconds(start, 5000)) == 400.0f);
    }
}

TEST_CASE("Grabbing a settling sheet again continues from its current height", "[ui][sheet][mobile-demo][spring]")
{
    const time_point start { time_point {} + std::chrono::seconds { 1 } };
    mobile_demo::sheets_page page {};
    deliver_at(page, mobile_demo::sheet_open_intent {}, start);
    deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::full, false }, 300.0f, 776.0f, 600.0f } }, start);
    const time_point grab { after_milliseconds(start, 20) };
    const float height { sheet_height_at(page, grab) };
    REQUIRE(height > 300.0f);
    REQUIRE(height < 776.0f);
    // 손가락이 30만큼 더 끌면 지금 높이에서 그만큼 내려간다. 스프링은 거둔다.
    deliver_at(page, mobile_demo::sheet_drag_intent { 30.0f }, grab);
    REQUIRE(sheet_height_at(page, grab) == Catch::Approx(height - 30.0f).margin(1.0e-3));
    REQUIRE(page.next_update(grab).has_value() == false);
    REQUIRE(sheet_height_at(page, after_milliseconds(grab, 2000)) == Catch::Approx(height - 30.0f).margin(1.0e-3));
    // 이어 놓으면 그 높이에서 다시 출발한다.
    deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::full, false }, height - 30.0f, 776.0f, 0.0f } }, after_milliseconds(grab, 2000));
    REQUIRE(sheet_height_at(page, after_milliseconds(grab, 2000)) == Catch::Approx(height - 30.0f).margin(1.0e-3));
    REQUIRE(sheet_height_at(page, after_milliseconds(grab, 7000)) == 776.0f);
}

TEST_CASE("With reduced motion a released sheet stands on its stop at once", "[ui][sheet][mobile-demo][spring]")
{
    const time_point start { time_point {} + std::chrono::seconds { 1 } };
    mobile_demo::sheets_page page {};
    page.set_reduce_motion(true);
    deliver_at(page, mobile_demo::sheet_open_intent {}, start);
    deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::full, false }, 300.0f, 776.0f, 600.0f } }, start);
    REQUIRE(sheet_height_at(page, start) == 776.0f);
    REQUIRE(page.next_update(start).has_value() == false);
    REQUIRE(motion_state_at(page, start) == u8"멈춤");

    // 도는 중에 켜지면 멈춤점에 붙는다.
    page.set_reduce_motion(false);
    deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::half, false }, 776.0f, 400.0f, -900.0f } }, start);
    REQUIRE(page.next_update(start).has_value());
    REQUIRE(sheet_height_at(page, start) == Catch::Approx(776.0f).margin(1.0e-3));
    page.set_reduce_motion(true);
    REQUIRE(sheet_height_at(page, start) == 400.0f);
    REQUIRE(page.next_update(start).has_value() == false);
}

TEST_CASE("A release toward closing removes the sheet at once as before", "[ui][sheet][mobile-demo][spring]")
{
    const time_point start { time_point {} + std::chrono::seconds { 1 } };
    mobile_demo::sheets_page page {};
    deliver_at(page, mobile_demo::sheet_open_intent {}, start);
    deliver_at(page, mobile_demo::sheet_release_intent { luil::sheet_release { { luil::sheet_detent::half, true }, 120.0f, 0.0f, -1500.0f } }, start);
    REQUIRE(page.sheet_open() == false);
    REQUIRE(page.next_update(start).has_value() == false);
}

TEST_CASE("The fling button repeats a release and the label says whether the sheet is moving", "[ui][sheet][mobile-demo][spring]")
{
    const time_point start { time_point {} + std::chrono::seconds { 1 } };
    mobile_demo::sheets_page page {};
    deliver_at(page, mobile_demo::sheet_open_intent {}, start);
    REQUIRE(motion_state_at(page, start) == u8"멈춤");

    // 반에서 위로 튕기면 전체 높이(776)로 간다.
    const auto up { press_button(page, start, u8"fling") };
    REQUIRE(up.has_value());
    REQUIRE(up->get<mobile_demo::sheet_release_intent>() != nullptr);
    deliver_at(page, *up->get<mobile_demo::sheet_release_intent>(), start);
    REQUIRE(sheet_height_at(page, start) == Catch::Approx(400.0f).margin(1.0e-3));
    REQUIRE(motion_state_at(page, start) == u8"도는 중 · 목표 높이 776");
    REQUIRE(page.next_update(start).has_value());
    const time_point settled { after_milliseconds(start, 5000) };
    REQUIRE(sheet_height_at(page, settled) == 776.0f);
    REQUIRE(motion_state_at(page, settled) == u8"멈춤");
    REQUIRE(page.next_update(settled).has_value() == false);

    // 전체에서 아래로 튕기면 반(400)으로 온다.
    const auto down { press_button(page, settled, u8"fling") };
    REQUIRE(down.has_value());
    REQUIRE(down->get<mobile_demo::sheet_release_intent>() != nullptr);
    deliver_at(page, *down->get<mobile_demo::sheet_release_intent>(), settled);
    REQUIRE(motion_state_at(page, settled) == u8"도는 중 · 목표 높이 400");
    REQUIRE(sheet_height_at(page, after_milliseconds(settled, 5000)) == 400.0f);

    // 옆 시트와 서랍에는 멈춤점이 없어 이 줄이 없다.
    mobile_demo::sheets_page side {};
    deliver_at(side, mobile_demo::sheet_open_intent { luil::sheet_kind::end, true }, start);
    const auto side_tree { sheet_tree_at(side, start) };
    REQUIRE(side_tree.find({ mobile_demo::kind_layout, u8"sheet-fling" }) == nullptr);
    REQUIRE(side_tree.find({ mobile_demo::kind_text, u8"sheets-motion" }) == nullptr);
}

TEST_CASE("SMB navigation removes stale rows while pending or failed and keeps the confirmed folder", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    REQUIRE(fixture.has_entry(u8"old.txt"));
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"photos", true });
    REQUIRE(fixture.requests.back().path == u8"photos");
    REQUIRE(fixture.has_entry(u8"old.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    deliver(fixture.page, mobile_demo::smb_up_intent {});
    REQUIRE(fixture.requests.size() == 2u);
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { fixture.next_ticket }, .error = { .kind = luil::net::smb_error_kind::access_denied } } });
    REQUIRE(fixture.has_entry(u8"old.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    REQUIRE(fixture.requests.size() == 2u);
    deliver(fixture.page, mobile_demo::smb_up_intent {});
    REQUIRE(fixture.requests.back().path.empty());

    // 두 단계 아래로 가다 실패해도 마지막으로 확인된 photos의 위는 공유 루트다.
    for (const bool reject : { false, true })
    {
        CAPTURE(reject);
        smb_fixture nested {};
        deliver(nested.page, mobile_demo::smb_entry_intent { u8"photos", true });
        deliver(nested.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { nested.next_ticket }, .entries = { { .name = u8"albums", .directory = true } } } });
        nested.reject = reject;
        deliver(nested.page, mobile_demo::smb_entry_intent { u8"albums", true });
        REQUIRE(nested.requests.back().path == u8"photos/albums");
        if (reject == false)
            deliver(nested.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { nested.next_ticket }, .error = { .kind = luil::net::smb_error_kind::access_denied } } });
        nested.reject = false;
        deliver(nested.page, mobile_demo::smb_up_intent {});
        REQUIRE(nested.requests.back().path.empty());
    }
}

TEST_CASE("SMB successful navigation binds the new rows to the new folder", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"photos", true });
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { fixture.next_ticket }, .entries = { { .name = u8"new.txt" } } } });
    REQUIRE(fixture.has_entry(u8"new.txt"));
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"new.txt", false });
    REQUIRE(fixture.requests.back().operation == luil::net::smb_operation::read);
    REQUIRE(fixture.requests.back().path == u8"photos/new.txt");
    deliver(fixture.page, mobile_demo::smb_up_intent {});
    REQUIRE(fixture.requests.back().path.empty());
    REQUIRE(fixture.cancelled.back() == luil::net::smb_ticket { 3 });
}

TEST_CASE("SMB file reading continues range by range until the end", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    REQUIRE(fixture.requests.back().operation == luil::net::smb_operation::read);
    REQUIRE(fixture.requests.back().offset == 0u);
    const std::size_t preview { fixture.requests.back().length };
    luil::net::smb_response first { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::read };
    first.data.resize(preview);
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(first) });

    // 미리 보기가 꽉 찼다. 다음 구간은 그 끝에서 시작한다.
    deliver(fixture.page, mobile_demo::smb_next_range_intent {});
    REQUIRE(fixture.requests.back().path == u8"old.txt");
    REQUIRE(fixture.requests.back().offset == preview);
    const std::size_t range { fixture.requests.back().length };
    luil::net::smb_response last { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::read };
    last.data.resize(range / 2u);
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(last) });

    // 짧게 왔다 — 파일 끝이다. 더 보내지 않는다.
    const std::size_t sent { fixture.requests.size() };
    deliver(fixture.page, mobile_demo::smb_next_range_intent {});
    CHECK(fixture.requests.size() == sent);
}

TEST_CASE("SMB with only a server lists its visible shares and opens the chosen one", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    fixture.address(u8"nas");
    deliver(fixture.page, mobile_demo::smb_open_intent {});
    REQUIRE(fixture.requests.back().operation == luil::net::smb_operation::shares);
    REQUIRE(fixture.requests.back().location.server == u8"nas");
    luil::net::smb_response response { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::shares };
    using luil::net::smb_share_kind;
    response.shares = {
        { .name = u8"photos", .kind = smb_share_kind::disk, .comment = u8"Family" },
        { .name = u8"IPC$", .kind = smb_share_kind::ipc, .special = true },
        { .name = u8"C$", .kind = smb_share_kind::disk, .special = true },
        { .name = u8"backup$", .kind = smb_share_kind::disk },
        { .name = u8"printer", .kind = smb_share_kind::printer },
    };
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(response) });
    // 디스크이고 숨지 않은 공유만 보인다.
    auto content { fixture.page.build(400.0f, 1.0f) };
    const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-photos" }) != nullptr);
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-C$" }) == nullptr);
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-backup$" }) == nullptr);
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-printer" }) == nullptr);

    // 목록에 없는 공유는 열지 않는다.
    const std::size_t sent { fixture.requests.size() };
    deliver(fixture.page, mobile_demo::smb_share_intent { u8"C$" });
    CHECK(fixture.requests.size() == sent);
    // 고른 공유는 주소를 바꾸고 그 맨 위를 연다.
    deliver(fixture.page, mobile_demo::smb_share_intent { u8"photos" });
    REQUIRE(fixture.requests.size() == sent + 1u);
    CHECK(fixture.requests.back().operation == luil::net::smb_operation::list);
    CHECK(fixture.requests.back().location.server == u8"nas");
    CHECK(fixture.requests.back().location.share == u8"photos");
    CHECK(fixture.requests.back().path.empty());
}

TEST_CASE("SMB discovery lists found servers and opens the share list of the chosen one", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    deliver(fixture.page, mobile_demo::smb_discover_intent {});
    REQUIRE(fixture.discoveries == 1);
    luil::net::smb_response found { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::discover };
    found.servers = { { .name = u8"Living Room NAS", .host = u8"nas.local", .addresses = { u8"192.168.0.10" }, .port = 445 } };
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(found) });
    auto content { fixture.page.build(400.0f, 1.0f) };
    const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-server-Living Room NAS" }) != nullptr);

    // 찾지 않은 서버는 열지 않는다.
    const std::size_t sent { fixture.requests.size() };
    deliver(fixture.page, mobile_demo::smb_server_intent { u8"10.0.0.9" });
    CHECK(fixture.requests.size() == sent);
    // 찾은 서버를 누르면 그 서버의 공유 목록을 묻는다.
    deliver(fixture.page, mobile_demo::smb_server_intent { u8"192.168.0.10" });
    REQUIRE(fixture.requests.size() == sent + 1u);
    CHECK(fixture.requests.back().operation == luil::net::smb_operation::shares);
    CHECK(fixture.requests.back().location.server == u8"192.168.0.10");
}

TEST_CASE("SMB rejected and replaced requests cannot revive an old listing", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    fixture.reject = true;
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"photos", true });
    REQUIRE(fixture.has_entry(u8"old.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    REQUIRE(fixture.requests.size() == 2u);
    fixture.reject = false;
    deliver(fixture.page, mobile_demo::smb_open_intent {});
    const auto replaced { fixture.next_ticket };
    deliver(fixture.page, mobile_demo::smb_open_intent {});
    REQUIRE(fixture.cancelled.back() == luil::net::smb_ticket { replaced });
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { replaced }, .entries = { { .name = u8"late.txt" } } } });
    REQUIRE(fixture.has_entry(u8"late.txt") == false);
    const auto edited { fixture.next_ticket };
    fixture.address(u8"other/share");
    REQUIRE(fixture.cancelled.back() == luil::net::smb_ticket { edited });
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { edited }, .entries = { { .name = u8"late.txt" } } } });
    REQUIRE(fixture.has_entry(u8"late.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"late.txt", false });
    REQUIRE(fixture.requests.size() == 4u);
}

TEST_CASE("The zoom page names its rotation and the buttons repeat the message the keys send", "[mobile-demo][zoom][rotate]")
{
    mobile_demo::zoom_page page {};
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
    // 오른쪽 단추를 두 번 누르면 시계 방향으로 30도다.
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-right") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 15°");
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-right") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 30°");
    // 왼쪽 단추를 세 번 누르면 -15도다.
    for (int count { 0 }; count < 3; ++count)
        REQUIRE(press_zoom_button(page, u8"zoom-rotate-left") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 -15°");
    // 0도 복귀는 지금 각도를 되돌린다. 이미 0도면 키처럼 메시지가 없다.
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-reset") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-reset") == 0);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
}

TEST_CASE("Rotating the zoom page moves the nodes while their text stays upright", "[mobile-demo][zoom][rotate]")
{
    mobile_demo::zoom_page page {};
    const luil::rect_f first { zoom_node_bounds(page, 0) };
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-right") == 1);
    const luil::rect_f turned { zoom_node_bounds(page, 0) };
    // 자리는 옮겨 가고 글 상자의 크기는 그대로다.
    REQUIRE((turned.x != Catch::Approx(first.x).margin(1.0e-3) || turned.y != Catch::Approx(first.y).margin(1.0e-3)));
    REQUIRE(turned.width == Catch::Approx(first.width));
    REQUIRE(turned.height == Catch::Approx(first.height));
    // 0도로 돌아오면 처음 자리다.
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-reset") == 1);
    const luil::rect_f back { zoom_node_bounds(page, 0) };
    REQUIRE(back.x == Catch::Approx(first.x).margin(1.0e-3));
    REQUIRE(back.y == Catch::Approx(first.y).margin(1.0e-3));
}

TEST_CASE("Shift with the arrows and Home rotate the zoom page through the focused view", "[mobile-demo][zoom][rotate]")
{
    mobile_demo::zoom_page page {};
    luil::interaction_controller controller {};
    // 키는 발행본 위에서 처리된다. 메시지를 받을 때마다 새 발행본을 건넨다.
    const auto key = [&page, &controller](const luil::key_code code) {
        const int delivered { deliver_actions(page, controller.process(luil::key_pressed_event { .key = code, .shift = true })) };
        controller.set_tree(zoom_tree(page));
        return delivered;
    };
    controller.set_tree(zoom_tree(page));
    static_cast<void>(controller.process(luil::access_focus_event { .target = { luil::ui_element_kind::zoom_view, u8"diagram" } }));
    REQUIRE(key(luil::key_code::arrow_right) == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 15°");
    REQUIRE(key(luil::key_code::arrow_left) == 1);
    REQUIRE(key(luil::key_code::arrow_left) == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 -15°");
    REQUIRE(key(luil::key_code::home) == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
    REQUIRE(key(luil::key_code::home) == 0);
}

TEST_CASE("Two fingers turning the zoom page show the angle they turned", "[mobile-demo][zoom][rotate]")
{
    mobile_demo::zoom_page page {};
    const auto tree { zoom_tree(page) };
    const auto* const view { tree->find({ luil::ui_element_kind::zoom_view, u8"diagram" }) };
    REQUIRE(view != nullptr);
    const luil::rect_f box { view->bounds() };
    const float x { box.x + box.width / 2.0f };
    const float y { box.y + box.height / 2.0f };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = x - 100.0f, .y = y, .device = luil::pointer_device::touch, .pointer_id = 1 }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = x + 100.0f, .y = y, .device = luil::pointer_device::touch, .pointer_id = 2 }));
    // 오른쪽 손가락이 40 내려가면 선이 atan(0.2) = 11.3도 시계 방향으로 돈다: 이동, 확대, 회전이 차례로 온다.
    const auto actions { controller.process(luil::pointer_moved_event { .x = x + 100.0f, .y = y + 40.0f, .device = luil::pointer_device::touch, .pointer_id = 2, .in_contact = true }) };
    REQUIRE(deliver_actions(page, actions) == 3);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 11°");
}

TEST_CASE("Turning to another zoom page stands the page upright", "[mobile-demo][zoom][rotate]")
{
    mobile_demo::zoom_page page {};
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-right") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 15°");
    // 왼쪽으로 쓸어 다음 쪽으로 간다.
    REQUIRE(page.handle(luil::app_message { mobile_demo::edge_swipe_intent { luil::zoom_edge_swipe { .horizontal = -1 } } }));
    REQUIRE(zoom_text(page, u8"zoom-page-number") == u8"쪽 2 / 5");
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
}

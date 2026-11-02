#include "demo/code_lexer.h"
#include "demo/line_break_sample.h"
#include "mobile_demo/device_page.h"
#include "mobile_demo/files_page.h"
#include "mobile_demo/popups_page.h"
#include "mobile_demo/sheets_page.h"
#include "mobile_demo/smb_page.h"
#include "mobile_demo/toasts_page.h"
#include "mobile_demo/zoom_page.h"

#include "luil/ui/split_button_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_platform.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
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

    // 메뉴·팝업 페이지를 폭 400으로 지은 tree다. 필드의 포인터가 이 tree의 것이라 popup을 만들 때까지 살려 둔다.
    struct popups_screen
    {
        luil::ui_tree tree;
        float height;

        [[nodiscard]] luil::rect_f surface() const
        {
            return { 0.0f, 0.0f, 400.0f, height };
        }

        [[nodiscard]] std::u8string label(const std::u8string& owner) const
        {
            const luil::ui_element* const found { tree.find({ mobile_demo::kind_text, owner }) };
            REQUIRE(found != nullptr);
            return found->accessibility().name;
        }
    };

    [[nodiscard]] popups_screen build_popups_screen(mobile_demo::popups_page& page)
    {
        auto content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        return { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f), height };
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

TEST_CASE("The device page says whether the platform has a taskbar", "[mobile-demo][taskbar]")
{
    // 성질은 프로세스 전역이라 시험 뒤에 되돌린다.
    const luil::ui_platform previous { luil::current_ui_platform() };
    mobile_demo::device_page page {};
    for (const bool capable : { false, true })
    {
        CAPTURE(capable);
        luil::ui_platform platform { previous };
        platform.capabilities.taskbar = capable;
        luil::set_ui_platform(platform);
        const auto tree { device_tree(page, false) };
        REQUIRE(tree.find({ mobile_demo::kind_text, u8"taskbar-heading" }) != nullptr);
        const auto* const none { tree.find({ mobile_demo::kind_text, u8"taskbar-none" }) };
        const auto* const note { tree.find({ mobile_demo::kind_text, u8"taskbar-note" }) };
        if (capable)
        {
            REQUIRE(none == nullptr);
            REQUIRE(note != nullptr);
        }
        else
        {
            // 능력 플래그가 거짓인 플랫폼은 항목 대신 이유를 보인다 (`haptic-none` 등과 같은 방식이다).
            REQUIRE(note == nullptr);
            REQUIRE(none != nullptr);
            REQUIRE(none->accessibility().name == u8"이 플랫폼에는 작업 표시줄이 없다.");
        }
    }
    luil::set_ui_platform(previous);
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

TEST_CASE("The mobile split button is sized for a finger and reads its state back", "[ui][split-button][mobile-demo]")
{
    mobile_demo::popups_page page {};
    auto screen { build_popups_screen(page) };
    const luil::ui_element* const body { screen.tree.find({ luil::ui_element_kind::split_button_body, u8"save" }) };
    const luil::ui_element* const arrow { screen.tree.find({ luil::ui_element_kind::split_button_arrow, u8"save" }) };
    REQUIRE(body != nullptr);
    REQUIRE(arrow != nullptr);
    // 손가락으로 누르므로 높이는 터치 영역이고 화살표는 기본 폭보다 넓다.
    REQUIRE(body->bounds().height == mobile_demo::touch_height);
    REQUIRE(arrow->bounds().height == mobile_demo::touch_height);
    REQUIRE(arrow->bounds().width > luil::split_button_arrow_width);
    // 드롭다운 단추는 본체 없이 버튼 전체가 하나의 누르는 자리다.
    REQUIRE(screen.tree.find({ luil::ui_element_kind::split_button_body, u8"send" }) == nullptr);
    const luil::ui_element* const send { screen.tree.find({ luil::ui_element_kind::split_button_arrow, u8"send" }) };
    REQUIRE(send != nullptr);
    REQUIRE(send->accessibility().name == u8"보내기");
    REQUIRE(send->bounds().height == mobile_demo::touch_height);

    REQUIRE(screen.label(u8"split-state-save") == u8"저장 메뉴 열림: 닫힘");
    REQUIRE(screen.label(u8"split-state-send") == u8"보내기 메뉴 열림: 닫힘");
    REQUIRE(screen.label(u8"split-last-action") == u8"마지막 동작: 없음");
    REQUIRE(arrow->accessibility().expanded == false);

    // 본체를 누르면 마지막 동작이 바뀐다.
    const auto invoked { (*body->action(luil::ui_trigger::left_click))({ body->id(), 0.0f, 0.0f }) };
    REQUIRE(invoked.size() == 1u);
    deliver(page, std::get<luil::app_message>(invoked.front()));
    // 화살표는 열림의 반대를 담은 절대 메시지를 내고, 그것으로 열면 label과 접근 상태가 따라온다.
    const auto pressed { (*arrow->action(luil::ui_trigger::left_click))({ arrow->id(), 0.0f, 0.0f }) };
    REQUIRE(std::get<luil::app_message>(pressed.front()).get<mobile_demo::field_menu_intent>()->open);
    deliver(page, std::get<luil::app_message>(pressed.front()));
    screen = build_popups_screen(page);
    REQUIRE(screen.label(u8"split-last-action") == u8"마지막 동작: 저장");
    REQUIRE(screen.label(u8"split-state-save") == u8"저장 메뉴 열림: 열림");
    REQUIRE(screen.tree.find({ luil::ui_element_kind::split_button_arrow, u8"save" })->accessibility().expanded == true);
}

TEST_CASE("The mobile field menu follows its button and only one popup is open", "[ui][split-button][mobile-demo]")
{
    mobile_demo::popups_page page {};
    deliver(page, mobile_demo::field_menu_intent { u8"save", true });
    auto screen { build_popups_screen(page) };
    const luil::rect_f field { screen.tree.find({ luil::ui_element_kind::split_button, u8"save" })->bounds() };

    // 필드 아래에 간격 2를 두고 왼쪽에 맞춘다.
    auto popups { page.make_popups(1.0f, screen.surface()) };
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"save-menu");
    REQUIRE(popups.front().x == field.x);
    REQUIRE(popups.front().y == field.y + field.height + 2.0f);

    // 내용 영역이 필드 바로 아래에서 끝나면 위로 뒤집힌다.
    popups = page.make_popups(1.0f, { 0.0f, 0.0f, 400.0f, field.y + field.height + 10.0f });
    REQUIRE(popups.front().y + popups.front().height + 2.0f == Catch::Approx(field.y));

    // 다른 필드를 열면 앞의 것이 닫히고, 이미 닫힌 필드의 늦은 닫기는 지금 것을 건드리지 않는다.
    deliver(page, mobile_demo::field_menu_intent { u8"send", true });
    deliver(page, mobile_demo::field_menu_intent { u8"save", false });
    screen = build_popups_screen(page);
    popups = page.make_popups(1.0f, screen.surface());
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"send-menu");
    REQUIRE(screen.label(u8"split-state-save") == u8"저장 메뉴 열림: 닫힘");
    REQUIRE(screen.label(u8"split-state-send") == u8"보내기 메뉴 열림: 열림");

    // 드롭다운과 컨텍스트 메뉴도 같은 한 자리를 쓴다.
    deliver(page, mobile_demo::dropdown_toggle_intent { true });
    popups = page.make_popups(1.0f, screen.surface());
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"dropdown");
    deliver(page, mobile_demo::card_menu_intent { 50.0f, 60.0f });
    popups = page.make_popups(1.0f, screen.surface());
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"card-menu");
    page.close_popups();
    REQUIRE(page.make_popups(1.0f, screen.surface()).empty());
}

TEST_CASE("Choosing a mobile field menu item records it and the follow-up message closes the menu", "[ui][split-button][mobile-demo]")
{
    mobile_demo::popups_page page {};
    deliver(page, mobile_demo::field_menu_intent { u8"send", true });
    auto screen { build_popups_screen(page) };
    const auto popups { page.make_popups(1.0f, screen.surface()) };
    REQUIRE(popups.size() == 1u);
    const auto& menu { static_cast<const luil::menu_element*>(popups.front().tree->root())->config() };
    REQUIRE(menu.items.size() == 2u);

    const luil::input_action picked { menu.select(u8"link") };
    deliver(page, std::get<luil::app_message>(picked));
    const auto after { menu.after_select(luil::ui_action_context { {}, 0.0f, 0.0f }) };
    REQUIRE(after.size() == 1u);
    REQUIRE(std::get<luil::app_message>(after.front()).get<mobile_demo::field_menu_intent>()->open == false);
    deliver(page, std::get<luil::app_message>(after.front()));

    screen = build_popups_screen(page);
    REQUIRE(screen.label(u8"split-last-action") == u8"마지막 동작: 링크 복사");
    REQUIRE(screen.label(u8"split-state-send") == u8"보내기 메뉴 열림: 닫힘");
    REQUIRE(page.make_popups(1.0f, screen.surface()).empty());
}

namespace {
    struct bar_names
    {
        std::u8string_view owner {};
        std::u8string_view word {};
    };

    constexpr bar_names bar_list[] { { u8"info", u8"정보" }, { u8"success", u8"성공" }, { u8"warning", u8"경고" }, { u8"error", u8"오류" } };

    // 토스트 페이지를 폭 `width`로 지은 tree다. 막대의 높이가 폭에 달려 있어 회전은 폭이 다른 두 tree로 본다.
    struct toasts_screen
    {
        luil::ui_tree tree;
        float height;

        [[nodiscard]] std::u8string label(const std::u8string& owner) const
        {
            const luil::ui_element* const found { tree.find({ mobile_demo::kind_text, owner }) };
            REQUIRE(found != nullptr);
            return found->accessibility().name;
        }

        [[nodiscard]] luil::rect_f bounds(const luil::ui_element_kind kind, const std::u8string_view owner) const
        {
            const luil::ui_element* const found { tree.find({ kind, std::u8string { owner } }) };
            REQUIRE(found != nullptr);
            return found->bounds();
        }
    };

    [[nodiscard]] toasts_screen build_toasts_screen(mobile_demo::toasts_page& page, const float width)
    {
        auto content { page.build(width, 1.0f) };
        const float height { content.height };
        return { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, width, height }, 1.0f), height };
    }

    // 정보 막대의 단추를 눌러 나온 메시지를 페이지에 넘긴다. 누름은 tree에 있던 그 액션이다.
    void press(mobile_demo::toasts_page& page, const toasts_screen& screen, const luil::ui_element_id& id)
    {
        const luil::ui_element* const button { screen.tree.find(id) };
        REQUIRE(button != nullptr);
        const luil::ui_action* const action { button->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        const auto pressed { (*action)(luil::ui_action_context { id, 0.0f, 0.0f }) };
        REQUIRE(pressed.size() == 1u);
        deliver(page, std::get<luil::app_message>(pressed.front()));
    }
}

TEST_CASE("The mobile toasts page lists four info bars with finger sized buttons and state labels", "[ui][info-bar][mobile-demo]")
{
    mobile_demo::toasts_page page {};
    const toasts_screen screen { build_toasts_screen(page, 400.0f) };
    for (const bar_names& bar : bar_list)
    {
        CAPTURE(bar.owner);
        const luil::ui_element* const element { screen.tree.find({ luil::ui_element_kind::info_bar, std::u8string { bar.owner } }) };
        REQUIRE(element != nullptr);
        REQUIRE(element->accessibility().role == luil::access_role::alert);
        REQUIRE(element->accessibility().name.starts_with(std::u8string { bar.word } + u8" "));
        // 손가락으로 누르므로 닫기는 터치 영역 정사각이고 막대의 최소 높이도 그만큼이다.
        const luil::rect_f close { screen.bounds(luil::ui_element_kind::info_bar_close, bar.owner) };
        REQUIRE(close.width == mobile_demo::touch_height);
        REQUIRE(close.height == mobile_demo::touch_height);
        REQUIRE(screen.bounds(luil::ui_element_kind::info_bar_action, bar.owner).height == mobile_demo::touch_height);
        REQUIRE(screen.bounds(luil::ui_element_kind::info_bar, bar.owner).height >= mobile_demo::touch_height);
        REQUIRE(screen.label(u8"info-bar-state-" + std::u8string { bar.owner }) == std::u8string { bar.word } + u8" 막대: 열림");
    }
    REQUIRE(screen.label(u8"info-bar-last-action") == u8"마지막 동작: 없음");
    REQUIRE(screen.tree.find({ mobile_demo::kind_toast_trigger, u8"info-bar-reopen" }) == nullptr);
}

TEST_CASE("Pressing a mobile bar action records it and closing a bar removes it until it is reopened", "[ui][info-bar][mobile-demo]")
{
    mobile_demo::toasts_page page {};
    auto screen { build_toasts_screen(page, 400.0f) };

    press(page, screen, { luil::ui_element_kind::info_bar_action, u8"error" });
    screen = build_toasts_screen(page, 400.0f);
    REQUIRE(screen.label(u8"info-bar-last-action") == u8"마지막 동작: 오류 막대 다시 시도");
    REQUIRE(screen.tree.find({ luil::ui_element_kind::info_bar, u8"error" }) != nullptr);

    // 닫는 것은 앱이다. 다음 frame에 그 막대를 짓지 않고 다시 여는 단추가 선다.
    press(page, screen, { luil::ui_element_kind::info_bar_close, u8"info" });
    screen = build_toasts_screen(page, 400.0f);
    REQUIRE(screen.tree.find({ luil::ui_element_kind::info_bar, u8"info" }) == nullptr);
    REQUIRE(screen.tree.find({ luil::ui_element_kind::info_bar, u8"success" }) != nullptr);
    REQUIRE(screen.label(u8"info-bar-state-info") == u8"정보 막대: 닫힘");
    REQUIRE(screen.label(u8"info-bar-last-action") == u8"마지막 동작: 정보 막대 닫기");

    press(page, screen, { mobile_demo::kind_toast_trigger, u8"info-bar-reopen" });
    screen = build_toasts_screen(page, 400.0f);
    REQUIRE(screen.tree.find({ luil::ui_element_kind::info_bar, u8"info" }) != nullptr);
    REQUIRE(screen.label(u8"info-bar-state-info") == u8"정보 막대: 열림");
    REQUIRE(screen.label(u8"info-bar-last-action") == u8"마지막 동작: 막대 다시 열기");
    REQUIRE(screen.tree.find({ mobile_demo::kind_toast_trigger, u8"info-bar-reopen" }) == nullptr);
}

TEST_CASE("The mobile info bars are measured again when the width changes", "[ui][info-bar][mobile-demo]")
{
    mobile_demo::toasts_page page {};
    // 가로로 눕히면 글 폭이 넉넉해 동작 단추가 닫기 옆에 서고, 세로에서는 글 아래로 내려가 막대가 높아진다.
    const toasts_screen landscape { build_toasts_screen(page, 900.0f) };
    const toasts_screen portrait { build_toasts_screen(page, 360.0f) };
    const luil::rect_f wide_action { landscape.bounds(luil::ui_element_kind::info_bar_action, u8"error") };
    const luil::rect_f wide_close { landscape.bounds(luil::ui_element_kind::info_bar_close, u8"error") };
    REQUIRE(wide_action.y + wide_action.height / 2.0f == Catch::Approx(wide_close.y + wide_close.height / 2.0f));

    const luil::rect_f narrow_bar { portrait.bounds(luil::ui_element_kind::info_bar, u8"error") };
    const luil::rect_f narrow_action { portrait.bounds(luil::ui_element_kind::info_bar_action, u8"error") };
    const luil::rect_f narrow_close { portrait.bounds(luil::ui_element_kind::info_bar_close, u8"error") };
    REQUIRE(narrow_action.y >= narrow_close.y + narrow_close.height);
    REQUIRE(narrow_action.y + narrow_action.height <= narrow_bar.y + narrow_bar.height + 0.5f);
    REQUIRE(narrow_bar.height > landscape.bounds(luil::ui_element_kind::info_bar, u8"error").height);

    // 페이지가 센 높이가 실제 배치와 맞아 마지막 줄이 페이지 안에 든다.
    for (const toasts_screen* const screen : { &landscape, &portrait })
    {
        const luil::rect_f last { screen->bounds(mobile_demo::kind_text, u8"info-bar-last-action") };
        REQUIRE(last.y + last.height <= screen->height + 0.5f);
    }
}

TEST_CASE("The demo code lexer paints six roles with bold keywords and italic comments", "[demo][syntax]")
{
    luil::syntax_highlighter highlighter { demo::code_highlighter_config() };
    // 둘째 줄이 열고 셋째 줄이 닫는 여러 줄 주석이 든다.
    const std::u8string text { u8"if (a) return f(10); // end\nint n = \"xy\"; /* a\n still */ 7" };
    highlighter.update(text);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 10u);
    const auto role_of = [&](const std::size_t index) { return (*spans)[index].color; };
    const auto begin_of = [&](const std::size_t index) { return (*spans)[index].begin; };
    const auto end_of = [&](const std::size_t index) { return (*spans)[index].end; };
    // 키워드는 굵게다.
    REQUIRE(role_of(0) == luil::text_color_role::keyword);
    REQUIRE((*spans)[0].bold);
    REQUIRE((begin_of(0) == 0u && end_of(0) == 2u));
    REQUIRE(role_of(1) == luil::text_color_role::keyword);
    REQUIRE((begin_of(1) == 7u && end_of(1) == 13u));
    // 호출되는 이름은 함수, 숫자는 숫자다.
    REQUIRE(role_of(2) == luil::text_color_role::function);
    REQUIRE((begin_of(2) == 14u && end_of(2) == 15u));
    REQUIRE(role_of(3) == luil::text_color_role::number);
    REQUIRE((begin_of(3) == 16u && end_of(3) == 18u));
    // 주석은 기울임이고 줄 바꿈 바이트를 덮지 않는다. 여러 줄 주석은 줄마다 구간이다.
    for (const std::size_t index : { std::size_t { 4 }, std::size_t { 7 }, std::size_t { 8 } })
    {
        REQUIRE(role_of(index) == luil::text_color_role::comment);
        REQUIRE((*spans)[index].italic);
        REQUIRE_FALSE((*spans)[index].bold);
    }
    REQUIRE((begin_of(4) == 21u && end_of(4) == 27u));
    REQUIRE(role_of(5) == luil::text_color_role::type);
    REQUIRE((begin_of(5) == 28u && end_of(5) == 31u));
    REQUIRE(role_of(6) == luil::text_color_role::string);
    REQUIRE((begin_of(6) == 36u && end_of(6) == 40u));
    REQUIRE((begin_of(7) == 42u && end_of(7) == 46u));
    REQUIRE((begin_of(8) == 47u && end_of(8) == 56u));
    REQUIRE(role_of(9) == luil::text_color_role::number);
    REQUIRE((begin_of(9) == 57u && end_of(9) == 58u));

    const demo::code_span_counts counts { demo::count_code_spans(*spans) };
    REQUIRE(counts.total == 10u);
    REQUIRE(counts.keywords == 2u);
    REQUIRE(counts.strings == 1u);
    REQUIRE(counts.comments == 3u);
    REQUIRE(demo::code_status_text(counts, highlighter.stats().relexed) == u8"구간 10 · 키워드 2 · 문자열 1 · 주석 3 · 마지막에 다시 칠한 줄 3");
}

TEST_CASE("The demo code lexer reads escapes in a string and an unclosed string ends at the line", "[demo][syntax]")
{
    luil::syntax_highlighter highlighter { demo::code_highlighter_config() };
    const std::u8string text { u8"a \"b\\\"c\" d\ne 'f\ng 1.5e3 h" };
    highlighter.update(text);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 3u);
    // 역슬래시 뒤의 따옴표는 문자열을 닫지 않는다.
    REQUIRE((*spans)[0].color == luil::text_color_role::string);
    REQUIRE((*spans)[0].begin == 2u);
    REQUIRE((*spans)[0].end == 8u);
    // 닫히지 않은 따옴표는 줄 끝까지이고 다음 줄로 이어지지 않는다.
    REQUIRE((*spans)[1].color == luil::text_color_role::string);
    REQUIRE((*spans)[1].begin == 13u);
    REQUIRE((*spans)[1].end == 15u);
    // 소수와 지수가 든 숫자는 한 구간이다.
    REQUIRE((*spans)[2].color == luil::text_color_role::number);
    REQUIRE((*spans)[2].begin == 18u);
    REQUIRE((*spans)[2].end == 23u);
}

TEST_CASE("The code sample shows a one-line edit relexing one line and a comment toggling the counts", "[demo][syntax]")
{
    luil::syntax_highlighter highlighter { demo::code_highlighter_config() };
    const std::u8string sample { demo::code_sample };
    highlighter.update(sample);
    const demo::code_span_counts original { demo::count_code_spans(*highlighter.spans()) };
    const std::vector<luil::text_span> original_spans { *highlighter.spans() };
    REQUIRE(original.total == 22u);
    REQUIRE(original.keywords == 4u);
    REQUIRE(original.strings == 1u);
    REQUIRE(original.comments == 4u);
    // 처음 불러올 때는 14줄(마지막 빈 줄 포함) 모두를 렉서에 보낸다. 실행한 데모의 상태 줄이 처음에 이 글을 그린다.
    REQUIRE(demo::code_status_text(original, highlighter.stats().relexed) == u8"구간 22 · 키워드 4 · 문자열 1 · 주석 4 · 마지막에 다시 칠한 줄 14");

    // 한 줄 편집이다. 숫자 하나를 바꾸면 렉서에 가는 줄은 하나이고 구간 수는 그대로다.
    std::u8string edited { sample };
    edited.replace(edited.find(u8"total = 0;") + 8, 1, u8"9");
    highlighter.update(edited);
    REQUIRE(highlighter.stats().relexed == 1u);
    REQUIRE(demo::code_status_text(demo::count_code_spans(*highlighter.spans()), highlighter.stats().relexed) == u8"구간 22 · 키워드 4 · 문자열 1 · 주석 4 · 마지막에 다시 칠한 줄 1");

    // `int total` 줄 앞에서 `/*`를 열면 끝까지 주석이 되어 주석 수가 늘고 키워드는 줄어든다.
    std::u8string opened { sample };
    opened.insert(opened.find(u8"    int total"), u8"/*");
    highlighter.update(opened);
    const demo::code_span_counts commented { demo::count_code_spans(*highlighter.spans()) };
    REQUIRE(highlighter.stats().relexed == 9u);
    REQUIRE(commented.comments > original.comments);
    REQUIRE(commented.keywords < original.keywords);
    REQUIRE(demo::code_status_text(commented, highlighter.stats().relexed) == u8"구간 15 · 키워드 1 · 문자열 0 · 주석 10 · 마지막에 다시 칠한 줄 9");

    // 지우면 처음 그림으로 돌아온다.
    highlighter.update(sample);
    const demo::code_span_counts restored { demo::count_code_spans(*highlighter.spans()) };
    REQUIRE(restored.total == original.total);
    REQUIRE(restored.comments == original.comments);
    REQUIRE(restored.keywords == original.keywords);
    REQUIRE(demo::code_status_text(restored, highlighter.stats().relexed) == u8"구간 22 · 키워드 4 · 문자열 1 · 주석 4 · 마지막에 다시 칠한 줄 9");
    REQUIRE(highlighter.spans()->size() == original_spans.size());
}

TEST_CASE("The demo line break cell counts lines with both policies", "[demo][line-break]")
{
    // ASCII는 글자 크기의 0.6배, 그 밖(한글·한자·가나)은 글자 크기만큼의 폭이다. 실제 글꼴과 다르므로 화면의 값은
    // 검증 때 다시 읽는다.
    const luil::text_measurer measurer { [](const std::u8string_view text, const float size) {
        float width { 0.0f };
        for (std::size_t offset { 0 }; offset < text.size();)
        {
            const auto decoded { luil::text::utf8_decode(text, offset) };
            width += decoded.codepoint < 0x80 ? size * 0.6f : size;
            offset += decoded.length;
        }
        return width;
    }
    };
    const auto status_of = [&measurer](const luil::text::line_break_policy policy) {
        const luil::label_config label { demo::line_break_label_config(std::u8string { demo::line_break_sample }, policy) };
        luil::text_area_element area { luil::ui_element_id { luil::application_element_kind(44) },
            luil::text_input_view { .text = std::u8string { demo::line_break_sample } }, demo::line_break_area_config(policy, std::make_shared<luil::text_area_wrap_cache>())
        };
        return demo::measure_line_break_status(policy, label, area, 1.0f, &measurer);
    };
    const std::u8string words { status_of(luil::text::line_break_policy::hangul_words) };
    const std::u8string syllables { status_of(luil::text::line_break_policy::unicode) };
    // 기본 정책은 첫 줄을 어절 끝에서 끊고, unicode 정책은 같은 폭을 음절 사이까지 채운다.
    //  - 칸은 안쪽 여백만큼 label보다 좁게 접는다. 이 측정기에서 label 첫 줄은 다음 어절의 첫 음절도 들이지 못해
    //    두 정책이 같고, 칸 첫 줄 끝이 정책을 가른다. 상태 줄이 둘을 함께 싣는 까닭이다.
    REQUIRE(words == u8"정책 hangul_words · 라벨 5줄 · 칸 5줄 · 첫 줄 끝 '낱말' / '바꾸고'");
    REQUIRE(syllables == u8"정책 unicode · 라벨 5줄 · 칸 5줄 · 첫 줄 끝 '낱말' / '고 낱'");

    // 줄 끝 글자는 매단 공백을 뺀 끝 글자소 셋이다.
    REQUIRE(demo::line_break_line_end(u8"가나다라  ", 0, 14) == u8"나다라");
    REQUIRE(demo::line_break_line_end(u8"ab", 0, 2) == u8"ab");
    // 셋의 앞머리에 걸린 공백도 뺀다.
    REQUIRE(demo::line_break_line_end(u8"가 나다", 0, 10) == u8"나다");
    REQUIRE(demo::line_break_line_end(u8"ab", 2, 2).empty());
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

namespace {
    // 페이지를 짓고 label 하나의 이름(UIA Name과 같은 글)을 읽는다.
    [[nodiscard]] std::u8string files_label(mobile_demo::files_page& page, const std::u8string& owner)
    {
        auto content { page.build(400.0f, 1.0f) };
        const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
        const luil::ui_element* const label { tree.find({ mobile_demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }
}

TEST_CASE("The files page draws the last watch batch with rescan and end marks", "[file_watch][mobile-demo]")
{
    mobile_demo::files_page page {};
    REQUIRE(files_label(page, u8"files-watch-batch") == u8"변경: 아직 없음");

    int stopped { 0 };
    mobile_demo::file_watcher_intent attach {};
    attach.folder = u8"watch-demo";
    attach.ticket = luil::file_watch_ticket { 3 };
    attach.stop = [&stopped] { ++stopped; };
    deliver(page, std::move(attach));
    REQUIRE(files_label(page, u8"files-watch-state") == u8"감시: 켜짐");

    luil::file_change_batch batch {};
    batch.ticket = luil::file_watch_ticket { 3 };
    batch.changes = {
        { luil::file_change_kind::created, u8"a.txt", {} },
        { luil::file_change_kind::renamed, u8"b.txt", u8"a~.txt" },
    };
    batch.rescan = true;
    deliver(page, mobile_demo::files_batch_intent { batch });
    REQUIRE(files_label(page, u8"files-watch-batch") == u8"변경: created a.txt, renamed a~.txt→b.txt, rescan");

    // 다른 표의 묶음은 화면을 덮지 않는다.
    luil::file_change_batch other {};
    other.ticket = luil::file_watch_ticket { 9 };
    other.changes = { { luil::file_change_kind::removed, u8"x.txt", {} } };
    deliver(page, mobile_demo::files_batch_intent { other });
    REQUIRE(files_label(page, u8"files-watch-batch") == u8"변경: created a.txt, renamed a~.txt→b.txt, rescan");

    // 끝 묶음 뒤로 감시가 끝났다고 적는다.
    luil::file_change_batch ended {};
    ended.ticket = luil::file_watch_ticket { 3 };
    ended.end = luil::file_watch_end::lost;
    ended.error = u8"gone";
    deliver(page, mobile_demo::files_batch_intent { ended });
    REQUIRE(files_label(page, u8"files-watch-batch") == u8"변경: 끝 lost");
    REQUIRE(files_label(page, u8"files-watch-state") == u8"감시: 끝남");

    page.stop();
    REQUIRE(stopped == 1);
}

TEST_CASE("The files page reports a watch that could not be added", "[file_watch][mobile-demo]")
{
    mobile_demo::files_page page {};
    mobile_demo::file_watcher_intent attach {};
    attach.folder = u8"missing";
    attach.error = u8"The folder does not exist.";
    deliver(page, std::move(attach));
    REQUIRE(files_label(page, u8"files-watch-state") == u8"감시: 걸지 못함 (The folder does not exist.)");
    // 감시가 없으면 단추가 폴더를 바꾸지 않는다. 멈출 감시자도 없다.
    deliver(page, mobile_demo::files_operation_intent { mobile_demo::files_operation::create });
    page.stop();
    REQUIRE(mobile_demo::describe_file_change_batch({}) == u8"변경: 없음");
}

TEST_CASE("Every editable field of the mobile demo has a text target", "[mobile-demo][text-input]")
{
    // 편집 대상이 없는 칸은 키 입력이 닿지 않고 접근성 값이 읽기 전용으로 답한다. 칸을 더하면 이 표에도 더한다.
    struct field
    {
        luil::ui_element_kind kind {};
        luil::text_input_target target {};
    };
    const field fields[] {
        { mobile_demo::kind_number_input, mobile_demo::target_number },
        { mobile_demo::kind_note_input, mobile_demo::target_note },
        { mobile_demo::kind_secret_input, mobile_demo::target_secret },
        { mobile_demo::kind_memo_input, mobile_demo::target_memo },
        { mobile_demo::kind_code_input, mobile_demo::target_code },
        { mobile_demo::kind_line_break_input, mobile_demo::target_line_break },
        { mobile_demo::kind_dropdown_search_input, mobile_demo::target_dropdown_search },
        { mobile_demo::kind_smb_address_input, mobile_demo::target_smb_address },
        { mobile_demo::kind_smb_user_input, mobile_demo::target_smb_user },
        { mobile_demo::kind_smb_password_input, mobile_demo::target_smb_password },
    };
    for (const field& each : fields)
    {
        const std::optional<luil::text_input_target> target { mobile_demo::text_target_of_kind(each.kind) };
        REQUIRE(target.has_value());
        REQUIRE(*target == each.target);
    }
    // 글이 아닌 element는 편집 대상이 없다.
    REQUIRE(mobile_demo::text_target_of_kind(mobile_demo::kind_text).has_value() == false);
}

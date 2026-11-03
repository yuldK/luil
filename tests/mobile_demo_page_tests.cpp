#include "demo/code_lexer.h"
#include "demo/line_break_sample.h"
#include "demo/readonly_undo_keys.h"
#include "demo/rich_text_sample.h"
#include "mobile_demo/basics_page.h"
#include "mobile_demo/device_page.h"
#include "mobile_demo/files_page.h"
#include "mobile_demo/ink_page.h"
#include "mobile_demo/inspector_page.h"
#include "mobile_demo/lists_page.h"
#include "mobile_demo/popups_page.h"
#include "mobile_demo/sheets_page.h"
#include "mobile_demo/smb_page.h"
#include "mobile_demo/theme_page.h"
#include "mobile_demo/toasts_page.h"
#include "mobile_demo/zoom_page.h"
#include "sample_image_bytes.h"

#include "luil/ui/message_box.h"
#include "luil/ui/rating_element.h"
#include "luil/ui/root_element.h"
#include "luil/ui/split_button_element.h"
#include "luil/ui/strip_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_platform.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 시험 실행 파일에 실은 assets/style.json이다 (tests/CMakeLists.txt). 스타일 파일 견본의 바탕으로 준다.
extern const std::uint8_t luil_test_base_style[];
extern const std::size_t luil_test_base_style_size;

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

    // 400x800 창에서 조립해 배치한 목록 페이지다 (배율 1).
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> lists_tree(mobile_demo::lists_page& page)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(page.build(400.0f, 800.0f, 1.0f), { 0.0f, 0.0f, 400.0f, 800.0f }, 1.0f));
    }

    // UIA가 되읽는 이름이 화면의 글과 같다.
    [[nodiscard]] std::u8string lists_text(mobile_demo::lists_page& page, const std::u8string& owner)
    {
        const auto tree { lists_tree(page) };
        const auto* const label { tree->find({ mobile_demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] bool lists_button_enabled(mobile_demo::lists_page& page, const std::u8string& owner)
    {
        const auto tree { lists_tree(page) };
        const auto* const button { tree->find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        return button->enabled();
    }

    // 순서 목록의 항목이 위에서부터 서는 높이다. 행은 모두 tree에 담기므로 순서가 높이로 읽힌다.
    [[nodiscard]] float list_row_top(const luil::ui_tree& tree, const int number)
    {
        const auto* const row { tree.find({ luil::ui_element_kind::list_row, u8"항목 " + mobile_demo::to_u8(number) }) };
        REQUIRE(row != nullptr);
        return row->bounds().y;
    }

    // 단추를 눌러 나온 메시지를 페이지에 돌려준다. 실제 입력 경로를 그대로 탄다. 돌려준 수를 돌려준다.
    int press_list_button(mobile_demo::lists_page& page, const std::u8string& owner)
    {
        const auto tree { lists_tree(page) };
        const auto* const button { tree->find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        const luil::rect_f box { button->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        int delivered { 0 };
        for (const luil::input_action& action : controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } }))
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
            {
                REQUIRE(page.handle(*message));
                ++delivered;
            }
        return delivered;
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
    const auto reset_enabled = [&page] {
        const auto tree { zoom_tree(page) };
        const auto* const button { tree->find({ mobile_demo::kind_layout, u8"zoom-rotate-reset" }) };
        REQUIRE(button != nullptr);
        return button->enabled();
    };
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
    // 0도에서는 되돌릴 것이 없어 0도 단추가 꺼져 있다. 켜진 채 빈 동작을 내면 보조 기술의 Invoke가 오류로 끝난다.
    REQUIRE(reset_enabled() == false);
    // 오른쪽 단추를 두 번 누르면 시계 방향으로 30도다.
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-right") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 15°");
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-right") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 30°");
    // 왼쪽 단추를 세 번 누르면 -15도다.
    for (int count { 0 }; count < 3; ++count)
        REQUIRE(press_zoom_button(page, u8"zoom-rotate-left") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 -15°");
    // 0도 복귀는 지금 각도를 되돌린다. 0도로 돌아오면 단추가 다시 꺼지고 눌러도 메시지가 없다.
    REQUIRE(reset_enabled());
    REQUIRE(press_zoom_button(page, u8"zoom-rotate-reset") == 1);
    REQUIRE(zoom_text(page, u8"zoom-rotation") == u8"회전 0°");
    REQUIRE(reset_enabled() == false);
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
    // UIA가 되읽는 줌 단추의 이름이다. 단추의 글이 곧 접근 이름이다.
    [[nodiscard]] std::u8string zoom_button_name(mobile_demo::zoom_page& page, const luil::ui_element_kind kind)
    {
        const auto tree { zoom_tree(page) };
        const auto* const button { tree->find({ kind, u8"diagram" }) };
        REQUIRE(button != nullptr);
        return button->accessibility().name;
    }
}

TEST_CASE("The zoom page names its buttons in the default language until the button switches them to English", "[mobile-demo][zoom][default-text]")
{
    mobile_demo::zoom_page page {};
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_decrease) == u8"축소");
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_increase) == u8"확대");
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_fit) == u8"맞춤");
    REQUIRE(zoom_text(page, u8"zoom-label-language") == u8"줌 단추 글: 기본");

    // 바꾸는 단추는 반대 언어를 절대 값으로 보낸다.
    REQUIRE(press_zoom_button(page, u8"zoom-label-toggle") == 1);
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_decrease) == u8"Zoom out");
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_increase) == u8"Zoom in");
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_fit) == u8"Fit");
    REQUIRE(zoom_text(page, u8"zoom-label-language") == u8"줌 단추 글: English");

    // 같은 메시지가 거듭 와도 결과가 같다.
    REQUIRE(page.handle(luil::app_message { mobile_demo::zoom_labels_intent { true } }));
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_fit) == u8"Fit");

    REQUIRE(press_zoom_button(page, u8"zoom-label-toggle") == 1);
    REQUIRE(zoom_button_name(page, luil::ui_element_kind::zoom_decrease) == u8"축소");
    REQUIRE(zoom_text(page, u8"zoom-label-language") == u8"줌 단추 글: 기본");
}

TEST_CASE("The language row of the zoom page fits the 400dp window below the rotation row", "[mobile-demo][zoom][default-text]")
{
    mobile_demo::zoom_page page {};
    const auto tree { zoom_tree(page) };
    const auto* const rotate { tree->find({ mobile_demo::kind_layout, u8"zoom-rotate-left" }) };
    const auto* const toggle { tree->find({ mobile_demo::kind_layout, u8"zoom-label-toggle" }) };
    const auto* const language { tree->find({ mobile_demo::kind_text, u8"zoom-label-language" }) };
    const auto* const view { tree->find({ luil::ui_element_kind::zoom_view, u8"diagram" }) };
    REQUIRE(rotate != nullptr);
    REQUIRE(toggle != nullptr);
    REQUIRE(language != nullptr);
    REQUIRE(view != nullptr);
    // 줄이 늘어도 단추는 창 안에 들고 보기를 가리지 않는다.
    REQUIRE(toggle->bounds().y >= rotate->bounds().y + rotate->bounds().height);
    REQUIRE(toggle->bounds().x + toggle->bounds().width <= language->bounds().x);
    REQUIRE(language->bounds().x + language->bounds().width <= 400.0f);
    REQUIRE(toggle->bounds().y + toggle->bounds().height <= view->bounds().y);
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
        { mobile_demo::kind_readonly_input, mobile_demo::target_readonly },
        { mobile_demo::kind_rich_editor_input, mobile_demo::target_rich_editor },
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

namespace {
    // 창 전체를 덮는 확인 대화 상자의 tree다 (배율 1, 안전 영역 없음). 대화가 열려 있을 때만 의미가 있다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> basics_overlay(mobile_demo::basics_page& page, const float width = 400.0f, const float height = 800.0f)
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, width, height, 1.0f, {});
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, 1.0f));
    }

    // 400dp 창에서 조립해 배치한 기본 페이지에서 읽은 글이다. 화면의 글이 곧 보조 기술이 읽는 이름이다.
    [[nodiscard]] std::u8string basics_text(mobile_demo::basics_page& page, const luil::ui_element_kind kind, const std::u8string& owner)
    {
        auto content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f) };
        const luil::ui_element* const found { tree.find({ kind, owner }) };
        REQUIRE(found != nullptr);
        return found->accessibility().name;
    }

    // 입력 경로가 돌려준 메시지를 페이지에 그대로 전한다. 앱 메시지가 아닌 것은 건너뛴다.
    void deliver_actions(mobile_demo::basics_page& page, const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                REQUIRE(page.handle(*message));
    }

    // 자리 한가운데를 눌렀다 뗀 입력이 내는 메시지다. 실제 입력 경로를 그대로 탄다.
    [[nodiscard]] std::vector<luil::input_action> click_element(luil::interaction_controller& controller, const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::rect_f box { element->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        return controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } });
    }

    [[nodiscard]] luil::ui_element_id box_button(const std::u8string_view owner, const std::u8string_view key)
    {
        return luil::message_box_button_id(owner, key);
    }

    [[nodiscard]] luil::rect_f bounds_of(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const found { tree.find(id) };
        REQUIRE(found != nullptr);
        return found->bounds();
    }

    [[nodiscard]] std::u8string result_key_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        REQUIRE(message != nullptr);
        const auto* const result { message->get<mobile_demo::dialog_result_intent>() };
        REQUIRE(result != nullptr);
        return result->key;
    }
}

TEST_CASE("The mobile reset box opens, reads its state back, and closes with one result from every way", "[mobile-demo][basics][message-box]")
{
    mobile_demo::basics_page page {};
    REQUIRE(page.dialog_open() == false);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-state") == u8"대화: 닫힘");
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-result") == u8"마지막 결과: 없음");

    deliver(page, mobile_demo::dialog_intent { mobile_demo::dialog_kind::reset });
    REQUIRE(page.dialog_open());
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-state") == u8"대화: 열림");

    // 뒤로 가기가 낼 메시지는 Esc가 내는 결과와 같다. 앱이 한 길을 빼먹어 대화가 열린 채 남지 않는다.
    REQUIRE(result_key_of(page.back_action()) == u8"cancel");
    {
        luil::interaction_controller controller {};
        controller.set_tree(basics_overlay(page));
        const std::vector<luil::input_action> escaped { controller.process(luil::key_pressed_event { luil::key_code::escape, false, false, false, false, time_point {} }) };
        REQUIRE(escaped.size() == 1u);
        REQUIRE(result_key_of(escaped.front()) == u8"cancel");
    }

    // 뒤로 가기가 낸 메시지로 닫힌다.
    deliver_actions(page, { page.back_action() });
    REQUIRE(page.dialog_open() == false);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-result") == u8"마지막 결과: cancel");

    // 초기화 단추는 카운터를 0으로 되돌린다.
    deliver(page, mobile_demo::increment_intent {});
    deliver(page, mobile_demo::increment_intent {});
    REQUIRE(basics_text(page, mobile_demo::kind_counter_label, {}) == u8"누른 횟수: 2");
    deliver(page, mobile_demo::dialog_intent { mobile_demo::dialog_kind::reset });
    const std::shared_ptr<const luil::ui_tree> tree { basics_overlay(page) };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    deliver_actions(page, click_element(controller, *tree, box_button(u8"reset", u8"reset")));
    REQUIRE(page.dialog_open() == false);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-result") == u8"마지막 결과: reset");
    REQUIRE(basics_text(page, mobile_demo::kind_counter_label, {}) == u8"누른 횟수: 0");
}

TEST_CASE("The mobile box buttons are finger sized and stand inside the screen margin", "[mobile-demo][basics][message-box]")
{
    mobile_demo::basics_page page {};
    deliver(page, mobile_demo::dialog_intent { mobile_demo::dialog_kind::reset });
    // 폭이 좁은 휴대폰이다. 자리 폭 320에서 양쪽 여백 16을 남기면 상자 폭은 288이다.
    const std::shared_ptr<const luil::ui_tree> tree { basics_overlay(page, 320.0f, 640.0f) };
    const luil::rect_f box { bounds_of(*tree, { luil::ui_element_kind::message_box, u8"reset" }) };
    REQUIRE(box.x >= 16.0f);
    REQUIRE(box.x + box.width <= 320.0f - 16.0f);
    REQUIRE(bounds_of(*tree, box_button(u8"reset", u8"cancel")).height == Catch::Approx(mobile_demo::touch_height));
    REQUIRE(bounds_of(*tree, box_button(u8"reset", u8"reset")).height == Catch::Approx(mobile_demo::touch_height));
}

TEST_CASE("The long mobile box stacks its three buttons on a narrow phone and keeps a row on a wide window", "[mobile-demo][basics][message-box]")
{
    mobile_demo::basics_page page {};
    deliver(page, mobile_demo::dialog_intent { mobile_demo::dialog_kind::details });
    const luil::ui_element_id later { box_button(u8"details", u8"later") };
    const luil::ui_element_id discard { box_button(u8"details", u8"discard") };
    const luil::ui_element_id save { box_button(u8"details", u8"ok") };

    // 넓은 창에서는 세 단추가 한 행이다.
    {
        const std::shared_ptr<const luil::ui_tree> wide { basics_overlay(page, 480.0f, 800.0f) };
        REQUIRE(bounds_of(*wide, later).y == Catch::Approx(bounds_of(*wide, save).y));
        REQUIRE(bounds_of(*wide, later).x < bounds_of(*wide, discard).x);
        REQUIRE(bounds_of(*wide, discard).x < bounds_of(*wide, save).x);
    }

    // 좁은 화면에서는 넘쳐서 쌓인다. 순서는 그대로 위에서 아래이고 모두 안쪽 폭 전체다.
    const std::shared_ptr<const luil::ui_tree> narrow { basics_overlay(page, 320.0f, 800.0f) };
    const luil::rect_f box { bounds_of(*narrow, { luil::ui_element_kind::message_box, u8"details" }) };
    const luil::rect_f first { bounds_of(*narrow, later) };
    const luil::rect_f second { bounds_of(*narrow, discard) };
    const luil::rect_f third { bounds_of(*narrow, save) };
    REQUIRE(first.y < second.y);
    REQUIRE(second.y < third.y);
    REQUIRE(first.x == Catch::Approx(second.x));
    REQUIRE(second.x == Catch::Approx(third.x));
    REQUIRE(first.width == Catch::Approx(box.width - 40.0f));
    REQUIRE(second.y == Catch::Approx(first.y + mobile_demo::touch_height + 8.0f));
    REQUIRE(third.y == Catch::Approx(second.y + mobile_demo::touch_height + 8.0f));
    // 맨 아래 단추 아래가 안쪽 여백이라 상자가 단추를 모두 감싼다.
    REQUIRE(third.y + third.height + 20.0f == Catch::Approx(box.y + box.height));
}

TEST_CASE("The long mobile box keeps its check apart from its buttons and closes only on a button", "[mobile-demo][basics][message-box]")
{
    mobile_demo::basics_page page {};
    deliver(page, mobile_demo::dialog_intent { mobile_demo::dialog_kind::details });
    REQUIRE(page.make_dialog_config().check.has_value());
    REQUIRE(page.make_dialog_config().check->checked == false);
    {
        const std::shared_ptr<const luil::ui_tree> tree { basics_overlay(page) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        deliver_actions(page, click_element(controller, *tree, luil::message_box_check_id(u8"details")));
    }
    REQUIRE(page.dialog_open());
    REQUIRE(page.make_dialog_config().check->checked);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-result") == u8"마지막 결과: 없음");

    // 뒤로 가기는 나중에 다시 묻기(거절)의 key로 닫는다.
    REQUIRE(result_key_of(page.back_action()) == u8"later");
    deliver_actions(page, { page.back_action() });
    REQUIRE(page.dialog_open() == false);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"dialog-result") == u8"마지막 결과: later");
    // 닫히면 설정이 비어 단추도 없다.
    REQUIRE(page.make_dialog_config().buttons.empty());
}

namespace {
    // 400dp 창에서 조립해 배치한 기본 페이지의 tree다. 높이는 내용이 정한다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> basics_tree(mobile_demo::basics_page& page)
    {
        auto content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f));
    }

    [[nodiscard]] const luil::ui_element& rating_in(const luil::ui_tree& tree, const std::u8string& owner)
    {
        const luil::ui_element* const rating { tree.find({ luil::ui_element_kind::rating, owner }) };
        REQUIRE(rating != nullptr);
        return *rating;
    }

    // 낸 메시지 가운데 별점 값만 뽑는다.
    [[nodiscard]] std::vector<float> rating_values_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<float> values {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const intent { message->get<mobile_demo::rating_intent>() }; intent != nullptr)
                    values.push_back(intent->value);
        return values;
    }

    [[nodiscard]] luil::pointer_pressed_event touch_press(const float x, const float y, const int milliseconds)
    {
        luil::pointer_pressed_event event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { milliseconds } };
        event.device = luil::pointer_device::touch;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event touch_move(const float x, const float y, const int milliseconds)
    {
        luil::pointer_moved_event event { x, y, time_point {} + std::chrono::milliseconds { milliseconds } };
        event.device = luil::pointer_device::touch;
        event.in_contact = true;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event touch_release(const float x, const float y, const int milliseconds)
    {
        luil::pointer_released_event event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { milliseconds } };
        event.device = luil::pointer_device::touch;
        return event;
    }

    [[nodiscard]] bool has_rating_scroll(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr && message->get<mobile_demo::rating_scroll_intent>() != nullptr)
                return true;
        return false;
    }
}

TEST_CASE("The mobile rating row has finger sized stars and reads its value back", "[mobile-demo][basics][rating]")
{
    mobile_demo::basics_page page {};
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-value") == u8"별점: 0 / 5");
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-strip-value") == u8"가로 창 별점: 0 / 10");

    // 별 다섯이 칸 44라 폭 220이다. 칸마다 손가락 크기다.
    const auto tree { basics_tree(page) };
    const luil::ui_element& stars { rating_in(*tree, u8"rating") };
    REQUIRE(stars.bounds().width == 220.0f);
    REQUIRE(stars.bounds().height == 44.0f);
    REQUIRE(stars.children().size() == 5u);
    for (const std::unique_ptr<luil::ui_element>& cell : stars.children())
    {
        REQUIRE(cell->bounds().width == 44.0f);
        REQUIRE(cell->bounds().height == 44.0f);
    }
    REQUIRE(stars.accessibility().name == u8"별점");
    REQUIRE(stars.accessibility().value == u8"0 / 5");

    // 값은 앱 상태다. 절대 값이 오면 label과 접근 값이 함께 바뀐다.
    deliver(page, mobile_demo::rating_intent { u8"stars", 3.0f });
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-value") == u8"별점: 3 / 5");
    REQUIRE(rating_in(*basics_tree(page), u8"rating").accessibility().value == u8"3 / 5");
    // 범위 밖 값은 앱이 자른다.
    deliver(page, mobile_demo::rating_intent { u8"stars", 9.0f });
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-value") == u8"별점: 5 / 5");
}

TEST_CASE("Tapping a mobile star rates it and tapping it again clears it", "[mobile-demo][basics][rating]")
{
    mobile_demo::basics_page page {};
    {
        const auto tree { basics_tree(page) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        const std::vector<luil::input_action> tapped { click_element(controller, *tree, rating_in(*tree, u8"rating").children()[3]->id()) };
        REQUIRE(rating_values_of(tapped) == std::vector<float> { 4.0f });
        deliver_actions(page, tapped);
    }
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-value") == u8"별점: 4 / 5");

    // 같은 별을 다시 탭하면 지운다.
    const auto tree { basics_tree(page) };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const std::vector<luil::input_action> again { click_element(controller, *tree, rating_in(*tree, u8"rating").children()[3]->id()) };
    REQUIRE(rating_values_of(again) == std::vector<float> { 0.0f });
    deliver_actions(page, again);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-value") == u8"별점: 0 / 5");
}

TEST_CASE("The mobile star row inside the horizontal window scrolls on a swipe and rates on a tap", "[mobile-demo][basics][rating]")
{
    mobile_demo::basics_page page {};
    const auto tree { basics_tree(page) };

    // 별 열은 열 개라 폭 440이고 400dp 창의 흘리는 창보다 넓다. 넘치므로 창이 가로 흘림의 임자다.
    const luil::ui_element& stars { rating_in(*tree, u8"rating-strip") };
    REQUIRE(stars.children().size() == 10u);
    REQUIRE(stars.bounds().width == 440.0f);
    const luil::ui_element* const window { tree->find({ mobile_demo::kind_layout, u8"rating-strip-window" }) };
    REQUIRE(window != nullptr);
    REQUIRE(window->scroll() != nullptr);
    REQUIRE(window->scroll()->axis == luil::scroll_axis::horizontal);
    const float before { stars.bounds().x };

    // 별 위에서 시작한 빠른 가로 쓸기는 창을 흘리고 값을 정하지 않는다.
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const luil::rect_f start { stars.children()[2]->bounds() };
    const float x { start.x + start.width / 2.0f };
    const float y { start.y + start.height / 2.0f };
    static_cast<void>(controller.process(touch_press(x, y, 0)));
    const std::vector<luil::input_action> swiped { controller.process(touch_move(x - 60.0f, y, 50)) };
    REQUIRE(has_rating_scroll(swiped));
    REQUIRE(rating_values_of(swiped).empty());
    static_cast<void>(controller.process(touch_release(x - 60.0f, y, 80)));
    deliver_actions(page, swiped);
    REQUIRE(rating_in(*basics_tree(page), u8"rating-strip").bounds().x < before);

    // 탭은 값을 정한다. 흘러간 뒤에도 같은 자리에서 칸의 값이 나간다.
    const auto scrolled_tree { basics_tree(page) };
    luil::interaction_controller tapper {};
    tapper.set_tree(scrolled_tree);
    const std::vector<luil::input_action> tapped { click_element(tapper, *scrolled_tree, rating_in(*scrolled_tree, u8"rating-strip").children()[5]->id()) };
    REQUIRE(rating_values_of(tapped) == std::vector<float> { 6.0f });
    deliver_actions(page, tapped);
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rating-strip-value") == u8"가로 창 별점: 6 / 10");
}

TEST_CASE("The mobile read-only field reads the access name the page gave it", "[mobile-demo][basics][read-only]")
{
    // 보조 기술이 읽는 이름은 페이지가 단 이름이다. 칸에 안내 글이 없어도 이름이 빈 채로 읽히지 않는다.
    mobile_demo::basics_page page {};
    const auto tree { basics_tree(page) };
    const luil::ui_element* const area { tree->find({ mobile_demo::kind_readonly_input }) };
    REQUIRE(area != nullptr);
    REQUIRE(area->accessibility().role == luil::access_role::edit);
    REQUIRE(area->accessibility().name == u8"읽기 전용 안내 칸");
}

TEST_CASE("Undoing a reorder on the mobile list page brings back the earlier order and the buttons follow", "[mobile-demo][lists][undo]")
{
    mobile_demo::lists_page page {};
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 0 · 다시 실행 0 · 다음: 없음");
    // 할 일이 없으면 꺼 둔다. 켜 둔 채 빈 동작을 내면 보조 기술의 Invoke가 오류로 끝난다.
    REQUIRE(lists_button_enabled(page, u8"list-undo") == false);
    REQUIRE(lists_button_enabled(page, u8"list-redo") == false);
    REQUIRE(press_list_button(page, u8"list-undo") == 0);

    {
        const auto tree { lists_tree(page) };
        REQUIRE(list_row_top(*tree, 1) < list_row_top(*tree, 2));
    }
    // 항목 1을 항목 4 앞으로 끌어 놓는다.
    deliver(page, mobile_demo::reorder_intent { u8"항목 1", u8"항목 4" });
    {
        const auto tree { lists_tree(page) };
        REQUIRE(list_row_top(*tree, 2) < list_row_top(*tree, 3));
        REQUIRE(list_row_top(*tree, 3) < list_row_top(*tree, 1));
        REQUIRE(list_row_top(*tree, 1) < list_row_top(*tree, 4));
    }
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 1 · 다시 실행 0 · 다음: 순서 바꿈");
    REQUIRE(lists_button_enabled(page, u8"list-undo"));
    REQUIRE(lists_button_enabled(page, u8"list-redo") == false);

    // 단추가 되돌림 메시지를 낸다. 처음 순서로 돌아온다.
    REQUIRE(press_list_button(page, u8"list-undo") == 1);
    {
        const auto tree { lists_tree(page) };
        REQUIRE(list_row_top(*tree, 1) < list_row_top(*tree, 2));
        REQUIRE(list_row_top(*tree, 3) < list_row_top(*tree, 4));
    }
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 0 · 다시 실행 1 · 다음: 없음");
    REQUIRE(lists_button_enabled(page, u8"list-undo") == false);
    REQUIRE(lists_button_enabled(page, u8"list-redo"));

    // 다시 실행하면 옮긴 순서다.
    REQUIRE(press_list_button(page, u8"list-redo") == 1);
    {
        const auto tree { lists_tree(page) };
        REQUIRE(list_row_top(*tree, 3) < list_row_top(*tree, 1));
    }
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 1 · 다시 실행 0 · 다음: 순서 바꿈");
}

TEST_CASE("A new reorder after an undo clears the redo side and a reorder that moves nothing is not a step", "[mobile-demo][lists][undo]")
{
    mobile_demo::lists_page page {};
    // 제자리로 놓거나 없는 항목을 옮기면 걸음이 없다.
    deliver(page, mobile_demo::reorder_intent { u8"항목 2", u8"항목 2" });
    deliver(page, mobile_demo::reorder_intent { u8"없는 항목", u8"항목 2" });
    deliver(page, mobile_demo::reorder_intent { u8"항목 1", u8"항목 2" });
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 0 · 다시 실행 0 · 다음: 없음");

    deliver(page, mobile_demo::reorder_intent { u8"항목 1", u8"항목 3" });
    deliver(page, mobile_demo::reorder_intent { u8"항목 5", u8"항목 1" });
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 2 · 다시 실행 0 · 다음: 순서 바꿈");
    REQUIRE(press_list_button(page, u8"list-undo") == 1);
    REQUIRE(lists_button_enabled(page, u8"list-redo"));
    deliver(page, mobile_demo::reorder_intent { u8"항목 6", u8"항목 1" });
    REQUIRE(lists_button_enabled(page, u8"list-redo") == false);
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 2 · 다시 실행 0 · 다음: 순서 바꿈");

    // 두 걸음을 모두 되돌리면 처음 순서다. 되돌림이 기록하지 않아 걸음이 늘지 않는다.
    REQUIRE(press_list_button(page, u8"list-undo") == 1);
    REQUIRE(press_list_button(page, u8"list-undo") == 1);
    const auto tree { lists_tree(page) };
    for (int number { 1 }; number < 8; ++number)
        REQUIRE(list_row_top(*tree, number) < list_row_top(*tree, number + 1));
    REQUIRE(lists_text(page, u8"list-history") == u8"되돌리기 0 · 다시 실행 2 · 다음: 없음");
}

TEST_CASE("The mobile theme page applies a sample style and keeps it when a broken sample is refused", "[mobile-demo][style_json]")
{
    // 글 칸 대신 견본 단추만 있다. 받은 견본은 셸이 frame과 본문 배치에 실을 스타일이 되고, 막힌 견본은 그것을 그대로 둔다.
    const std::u8string source { reinterpret_cast<const char8_t*>(luil_test_base_style), luil_test_base_style_size };
    mobile_demo::theme_page page { source };
    const auto status = [&page] {
        auto content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f) };
        for (const char8_t* const owner : { u8"theme-style-sample-background", u8"theme-style-sample-unknown", u8"theme-style-sample-broken", u8"theme-style-sample-wrap", u8"theme-style-reset" })
            REQUIRE(tree.find({ mobile_demo::kind_style_sheet_button, owner }) != nullptr);
        const luil::ui_element* const label { tree.find({ mobile_demo::kind_text, u8"theme-style-status" }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    };
    REQUIRE(status() == u8"내장값 · dark.windowBackground #1e1e1e");

    deliver(page, demo::style_sheet_intent { demo::style_sheet_action::sample_background });
    REQUIRE(status() == u8"적용됨 · 오류 0 · dark.windowBackground #1a1b26");
    const std::shared_ptr<const luil::ui_style> applied { page.style() };
    REQUIRE(applied != nullptr);
    REQUIRE(applied->metrics.control_corner_radius == 8.0f);

    deliver(page, demo::style_sheet_intent { demo::style_sheet_action::sample_broken });
    REQUIRE(status().starts_with(u8"거부 · 오류 1 · Invalid JSON at byte "));
    REQUIRE(status().ends_with(u8" · 이전 스타일 유지"));
    REQUIRE(page.style() == applied);

    deliver(page, demo::style_sheet_intent { demo::style_sheet_action::reset });
    REQUIRE(status() == u8"내장값 · dark.windowBackground #1e1e1e");
    REQUIRE(page.style() == nullptr);
}

TEST_CASE("The mobile theme page wrap sample switches the policy of a label that leaves its field empty", "[mobile-demo][style_json][wrap-policy]")
{
    // 정책 칸이 빈 label과 그 상태 줄이다 (docs/wrap-policy-key-design.md의 확인). 견본이 스타일을 바꾸면 상태 줄의 정책이 바뀐다.
    const std::u8string source { reinterpret_cast<const char8_t*>(luil_test_base_style), luil_test_base_style_size };
    mobile_demo::theme_page page { source };
    const auto label_of = [&page](const char8_t* const owner) {
        auto content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f, {}, nullptr, page.style().get()) };
        const luil::ui_element* const label { tree.find({ mobile_demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    };
    REQUIRE(label_of(u8"theme-wrap-label") == demo::line_break_sample);
    REQUIRE(label_of(u8"theme-wrap-status").starts_with(u8"스타일 정책 hangul_words · 라벨 "));
    deliver(page, demo::style_sheet_intent { demo::style_sheet_action::sample_wrap });
    REQUIRE(page.style() != nullptr);
    REQUIRE(page.style()->text.wrap_policy == luil::line_break_policy::unicode);
    REQUIRE(label_of(u8"theme-wrap-status").starts_with(u8"스타일 정책 unicode · 라벨 "));
    deliver(page, demo::style_sheet_intent { demo::style_sheet_action::sample_broken });
    REQUIRE(label_of(u8"theme-wrap-status").starts_with(u8"스타일 정책 unicode · 라벨 "));
    deliver(page, demo::style_sheet_intent { demo::style_sheet_action::reset });
    REQUIRE(label_of(u8"theme-wrap-status").starts_with(u8"스타일 정책 hangul_words · 라벨 "));

    // 줄 수와 첫 줄 끝은 고정 폭 측정기로 잠근다. ASCII는 글자 크기의 0.6배, 그 밖은 0.9배다. 기본 페이지 시험의 측정기(그 밖이
    // 글자 크기만큼)로는 label 첫 줄에 다음 어절의 첫 음절도 들지 않아 두 정책이 같다.
    const luil::text_measurer measurer { [](const std::u8string_view text, const float size) {
        float width { 0.0f };
        for (std::size_t offset { 0 }; offset < text.size();)
        {
            const auto decoded { luil::text::utf8_decode(text, offset) };
            width += decoded.codepoint < 0x80 ? size * 0.6f : size * 0.9f;
            offset += decoded.length;
        }
        return width;
    }
    };
    luil::ui_style unicode_style { luil::default_ui_style() };
    unicode_style.text.wrap_policy = luil::line_break_policy::unicode;
    luil::ui_style words_style { luil::default_ui_style() };
    words_style.text.wrap_policy = luil::line_break_policy::hangul_words;
    const luil::label_config label { demo::style_wrap_label_config() };
    REQUIRE(label.line_break.has_value() == false);
    const std::u8string words { demo::measure_style_wrap_status(label, &words_style, 1.0f, &measurer) };
    const std::u8string syllables { demo::measure_style_wrap_status(label, &unicode_style, 1.0f, &measurer) };
    // 어절 정책은 첫 줄을 '낱말'에서 끊고, 음절 정책은 다음 어절의 두 음절까지 채운다.
    REQUIRE(words == u8"스타일 정책 hangul_words · 라벨 5줄 · 첫 줄 끝 '낱말'");
    REQUIRE(syllables == u8"스타일 정책 unicode · 라벨 5줄 · 첫 줄 끝 '한가'");
    // 적은 칸은 스타일보다 앞선다. 기본 페이지의 줄 나눔 칸이 스타일에 닿지 않는 까닭이다.
    luil::label_config chosen { label };
    chosen.line_break = luil::line_break_policy::hangul_words;
    REQUIRE(demo::measure_style_wrap_status(chosen, &unicode_style, 1.0f, &measurer) == words);
}

namespace {
    // 400x800 창에서 조립해 배치한 잉크 페이지다 (배율 1).
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> ink_tree(mobile_demo::ink_page& page)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(page.build(400.0f, 800.0f, 1.0f), { 0.0f, 0.0f, 400.0f, 800.0f }, 1.0f));
    }

    [[nodiscard]] std::u8string ink_text(mobile_demo::ink_page& page, const std::u8string& owner)
    {
        const auto tree { ink_tree(page) };
        const auto* const label { tree->find({ mobile_demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] luil::rect_f ink_canvas_bounds(mobile_demo::ink_page& page)
    {
        const auto tree { ink_tree(page) };
        const auto* const canvas { tree->find({ luil::ui_element_kind::ink_canvas, u8"ink-canvas" }) };
        REQUIRE(canvas != nullptr);
        return canvas->bounds();
    }

    // 컨트롤러가 낸 앱 메시지를 페이지에 돌려준다. 돌려준 수를 돌려준다.
    int deliver_ink(mobile_demo::ink_page& page, const std::vector<luil::input_action>& actions)
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

    // 한 접촉을 실제 입력 경로로 긋는다. 표본마다 tree를 다시 지어 앱 상태(진행 중 획)가 따라오는 길과 같다.
    void stroke_ink(mobile_demo::ink_page& page, const luil::pointer_device device, const std::vector<luil::point_f>& points, const bool eraser = false)
    {
        luil::interaction_controller controller {};
        const std::optional<luil::pen_sample> pen { device == luil::pointer_device::pen ? std::optional { luil::pen_sample { 0.6f, 0.0f, 0.0f, eraser } } : std::nullopt };
        const time_point start {};
        controller.set_tree(ink_tree(page));
        luil::pointer_pressed_event pressed { points.front().x, points.front().y, luil::pointer_button::left, start };
        pressed.device = device;
        pressed.pointer_id = 4;
        pressed.pen = pen;
        static_cast<void>(deliver_ink(page, controller.process(pressed)));
        for (std::size_t index { 1 }; index < points.size(); ++index)
        {
            controller.set_tree(ink_tree(page));
            luil::pointer_moved_event moved { points[index].x, points[index].y, after_milliseconds(start, static_cast<int>(index) * 8) };
            moved.device = device;
            moved.pointer_id = 4;
            moved.in_contact = true;
            moved.pen = pen;
            static_cast<void>(deliver_ink(page, controller.process(moved)));
        }
        luil::pointer_released_event released { points.back().x, points.back().y, luil::pointer_button::left, after_milliseconds(start, 200) };
        released.device = device;
        released.pointer_id = 4;
        released.pen = pen;
        static_cast<void>(deliver_ink(page, controller.process(released)));
    }

    int press_ink_button(mobile_demo::ink_page& page, const std::u8string& owner)
    {
        const auto tree { ink_tree(page) };
        const auto* const button { tree->find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        const luil::rect_f box { button->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        return deliver_ink(page, controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, after_milliseconds(time_point {}, 40) }));
    }
}

TEST_CASE("A pen stroke on the ink page counts the stroke and says it carried pressure", "[mobile-demo][ink]")
{
    mobile_demo::ink_page page {};
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 0개");
    REQUIRE(ink_text(page, u8"ink-tool") == u8"도구: 펜");
    REQUIRE(ink_text(page, u8"ink-last") == u8"마지막 획: 없음");
    const luil::rect_f box { ink_canvas_bounds(page) };
    REQUIRE(box.width > 100.0f);
    REQUIRE(box.height > 100.0f);
    const float y { box.y + 60.0f };
    stroke_ink(page, luil::pointer_device::pen, { { box.x + 20.0f, y }, { box.x + 60.0f, y + 5.0f }, { box.x + 100.0f, y } });
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 1개");
    REQUIRE(ink_text(page, u8"ink-last") == u8"마지막 획: 펜, 압력 있음");
    // 캔버스의 Value(UIA 요약)가 상태 줄과 같은 글이다.
    const auto tree { ink_tree(page) };
    const auto* const canvas { tree->find({ luil::ui_element_kind::ink_canvas, u8"ink-canvas" }) };
    REQUIRE(canvas->accessibility().value == u8"획 1개");
    REQUIRE(canvas->accessibility().name == u8"잉크 캔버스");

    // 지우개 끝으로 그 획을 가로지르면 지워진다.
    stroke_ink(page, luil::pointer_device::pen, { { box.x + 60.0f, y - 40.0f }, { box.x + 60.0f, y + 40.0f } }, true);
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 0개");
    REQUIRE(ink_text(page, u8"ink-last") == u8"지운 획 1");
}

TEST_CASE("The ink page buttons add sample strokes, switch tools and let fingers draw", "[mobile-demo][ink]")
{
    mobile_demo::ink_page page {};
    // 견본 단추는 캔버스가 내는 것과 같은 메시지를 낸다 (UIA가 그리기 대신 되풀이한다).
    REQUIRE(press_ink_button(page, u8"ink-sample") > 2);
    REQUIRE(press_ink_button(page, u8"ink-sample") > 2);
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 2개");
    REQUIRE(ink_text(page, u8"ink-last") == u8"마지막 획: 펜, 압력 있음");
    REQUIRE(press_ink_button(page, u8"ink-clear") == 1);
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 0개");

    REQUIRE(press_ink_button(page, u8"ink-tool-toggle") == 1);
    REQUIRE(ink_text(page, u8"ink-tool") == u8"도구: 지우개");
    REQUIRE(press_ink_button(page, u8"ink-tool-toggle") == 1);
    REQUIRE(ink_text(page, u8"ink-tool") == u8"도구: 펜");

    // 손가락은 켜기 전까지 그리지 않는다.
    const luil::rect_f box { ink_canvas_bounds(page) };
    const float y { box.y + 80.0f };
    stroke_ink(page, luil::pointer_device::touch, { { box.x + 20.0f, y }, { box.x + 120.0f, y } });
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 0개");
    REQUIRE(page.handle(luil::app_message { demo::ink_command_intent { demo::ink_command::toggle_touch } }));
    stroke_ink(page, luil::pointer_device::touch, { { box.x + 20.0f, y }, { box.x + 120.0f, y } });
    REQUIRE(ink_text(page, u8"ink-count") == u8"획 1개");
    REQUIRE(ink_text(page, u8"ink-last") == u8"마지막 획: 손가락, 압력 없음");
}

namespace {
    // 폭 400의 휴대폰 화면에서 조립해 배치한 검사기 페이지다 (배율 1). 높이는 페이지 내용 높이다.
    [[nodiscard]] luil::ui_tree inspector_tree(mobile_demo::inspector_page& page)
    {
        mobile_demo::page_content content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f);
    }

    [[nodiscard]] std::u8string inspector_label(mobile_demo::inspector_page& page, const std::u8string& owner)
    {
        const luil::ui_tree tree { inspector_tree(page) };
        const luil::ui_element* const label { tree.find({ mobile_demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] std::u8string inspector_button(mobile_demo::inspector_page& page, const std::u8string& owner)
    {
        const luil::ui_tree tree { inspector_tree(page) };
        const luil::ui_element* const button { tree.find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        return button->accessibility().name;
    }

    [[nodiscard]] std::u8string inspector_number(const std::size_t value)
    {
        const std::string digits { std::to_string(value) };
        return std::u8string { digits.begin(), digits.end() };
    }

    // 손가락 탭이 단추에서 낸 앱 메시지를 페이지에 돌려준다. 돌려준 수를 돌려준다.
    int tap_inspector_button(mobile_demo::inspector_page& page, const std::u8string& owner)
    {
        const auto tree { std::make_shared<const luil::ui_tree>(inspector_tree(page)) };
        const luil::ui_element* const button { tree->find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        const luil::rect_f box { button->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        int delivered { 0 };
        for (const luil::input_action& action : controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, after_milliseconds(time_point {}, 40) }))
        {
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
            {
                REQUIRE(page.handle(*message));
                ++delivered;
            }
        }
        return delivered;
    }
}

TEST_CASE("The inspector page starts off and counts the elements of its own tree", "[mobile-demo][inspector]")
{
    mobile_demo::inspector_page page {};
    const luil::ui_tree tree { inspector_tree(page) };
    REQUIRE(inspector_label(page, u8"inspector-state") == u8"검사기: 끔");
    REQUIRE(inspector_label(page, u8"inspector-pick") == u8"집기: focused");
    REQUIRE(inspector_label(page, u8"inspector-levels") == u8"조상 단계: 0");
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: 없음");
    const std::size_t elements { luil::inspect_nodes(tree).size() };
    REQUIRE(elements > 20);
    REQUIRE(inspector_label(page, u8"inspector-count") == u8"요소 " + inspector_number(elements) + u8"개");
    // 꺼져 있으면 frame에 켜지 않은 설정이 실린다.
    REQUIRE(page.config().enabled == false);
}

TEST_CASE("The inspector buttons switch it on, move the ancestor level and toggle the extras", "[mobile-demo][inspector]")
{
    mobile_demo::inspector_page page {};
    REQUIRE(tap_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(inspector_label(page, u8"inspector-state") == u8"검사기: 켬");
    REQUIRE(page.config().enabled);
    REQUIRE(page.config().pick == luil::inspector_pick::focused);

    REQUIRE(tap_inspector_button(page, u8"inspector-up") == 1);
    REQUIRE(tap_inspector_button(page, u8"inspector-up") == 1);
    REQUIRE(inspector_label(page, u8"inspector-levels") == u8"조상 단계: 2");
    REQUIRE(page.config().ancestor_levels == 2);
    REQUIRE(tap_inspector_button(page, u8"inspector-down") == 1);
    REQUIRE(tap_inspector_button(page, u8"inspector-down") == 1);
    // 0 아래로는 내려가지 않는다.
    REQUIRE(tap_inspector_button(page, u8"inspector-down") == 1);
    REQUIRE(inspector_label(page, u8"inspector-levels") == u8"조상 단계: 0");

    REQUIRE(inspector_button(page, u8"inspector-outline") == u8"모든 윤곽: 끔");
    REQUIRE(tap_inspector_button(page, u8"inspector-outline") == 1);
    REQUIRE(inspector_button(page, u8"inspector-outline") == u8"모든 윤곽: 켬");
    REQUIRE(page.config().outline_all);
    REQUIRE(inspector_button(page, u8"inspector-palette") == u8"팔레트 견본: 끔");
    REQUIRE(tap_inspector_button(page, u8"inspector-palette") == 1);
    REQUIRE(inspector_button(page, u8"inspector-palette") == u8"팔레트 견본: 켬");
    REQUIRE(page.config().show_palette);

    REQUIRE(tap_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(inspector_label(page, u8"inspector-state") == u8"검사기: 끔");
    REQUIRE(page.config().enabled == false);
}

TEST_CASE("The inspector selection buttons walk the page tree in drawing order", "[mobile-demo][inspector]")
{
    mobile_demo::inspector_page page {};
    // 선택이 없을 때 다음은 뿌리(페이지 기둥)다. 집는 길이 선택으로 바뀐다.
    REQUIRE(tap_inspector_button(page, u8"inspector-next") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: layout:inspector-page");
    REQUIRE(inspector_label(page, u8"inspector-pick") == u8"집기: selected");
    REQUIRE(page.config().pick == luil::inspector_pick::selected);
    REQUIRE(page.config().selected == luil::ui_element_id { mobile_demo::kind_layout, u8"inspector-page" });

    REQUIRE(tap_inspector_button(page, u8"inspector-child") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: text:inspector-help");
    REQUIRE(tap_inspector_button(page, u8"inspector-next") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: text:inspector-state");
    REQUIRE(tap_inspector_button(page, u8"inspector-prev") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: text:inspector-help");
    REQUIRE(tap_inspector_button(page, u8"inspector-parent") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: layout:inspector-page");
    // 뿌리 위로는 갈 곳이 없어 선택이 그대로다.
    REQUIRE(tap_inspector_button(page, u8"inspector-parent") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: layout:inspector-page");

    // 끄고 다시 켜면 처음 모습이다.
    REQUIRE(tap_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(tap_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(inspector_label(page, u8"inspector-selected") == u8"선택: 없음");
    REQUIRE(inspector_label(page, u8"inspector-pick") == u8"집기: focused");
}

TEST_CASE("The inspector buttons keep the tapped sample focused so the ancestor steps climb from it", "[mobile-demo][inspector]")
{
    // 실기기의 손가락 탭과 같은 길이다. 탭마다 페이지가 메시지를 받고 tree를 새로 지어 같은 controller에 건넨다.
    mobile_demo::inspector_page page {};
    luil::interaction_controller controller {};
    auto tree { std::make_shared<const luil::ui_tree>(inspector_tree(page)) };
    controller.set_tree(tree);
    int now { 0 };
    const auto tap = [&](const std::u8string& owner) {
        const luil::ui_element* const target { tree->find({ mobile_demo::kind_layout, owner }) };
        REQUIRE(target != nullptr);
        const luil::rect_f box { target->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        static_cast<void>(controller.process(touch_press(x, y, now)));
        const std::vector<luil::input_action> actions { controller.process(touch_release(x, y, now + 40)) };
        now += 1000;
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                REQUIRE(page.handle(*message));
        tree = std::make_shared<const luil::ui_tree>(inspector_tree(page));
        controller.set_tree(tree);
    };
    const auto picked_owner = [&]() {
        const luil::ui_element* const picked { luil::pick_inspected(*tree, controller.snapshot(), page.config()) };
        REQUIRE(picked != nullptr);
        return picked->id().owner;
    };
    const luil::ui_element_id sample_button { mobile_demo::kind_layout, u8"inspector-sample-button" };

    tap(u8"inspector-toggle");
    tap(u8"inspector-sample-button");
    REQUIRE(controller.snapshot().focused == sample_button);
    REQUIRE(picked_owner() == u8"inspector-sample-button");

    // 조상 위로를 누를 때마다 견본 단추에서 견본 stack, 견본 panel, 페이지 순으로 오른다. 초점은 견본 단추에 남는다.
    tap(u8"inspector-up");
    REQUIRE(controller.snapshot().focused == sample_button);
    REQUIRE(inspector_label(page, u8"inspector-levels") == u8"조상 단계: 1");
    REQUIRE(picked_owner() == u8"inspector-sample-stack");
    tap(u8"inspector-up");
    REQUIRE(picked_owner() == u8"inspector-sample");
    tap(u8"inspector-up");
    REQUIRE(picked_owner() == u8"inspector-page");
    REQUIRE(controller.snapshot().focused == sample_button);

    // 다른 제어 단추도 초점을 옮기지 않는다.
    tap(u8"inspector-outline");
    tap(u8"inspector-down");
    REQUIRE(controller.snapshot().focused == sample_button);
    REQUIRE(picked_owner() == u8"inspector-sample");
}

TEST_CASE("The inspector names the demo kinds and reads the sample's inner padding", "[mobile-demo][inspector]")
{
    mobile_demo::inspector_page page {};
    const luil::inspector_config config { page.config() };
    REQUIRE(luil::kind_display_name(mobile_demo::kind_layout, config) == u8"layout");
    REQUIRE(luil::kind_display_name(mobile_demo::kind_text, config) == u8"text");
    REQUIRE(luil::kind_display_name(luil::ui_element_kind::ink_canvas, config) == u8"ink_canvas");

    const luil::ui_tree tree { inspector_tree(page) };
    const luil::ui_element* const stack { tree.find({ mobile_demo::kind_layout, u8"inspector-sample-stack" }) };
    REQUIRE(stack != nullptr);
    const std::u8string text { luil::describe_element(tree, *stack, 1.0f, config) };
    REQUIRE(text.starts_with(u8"layout:inspector-sample-stack  "));
    // 견본 stack의 안쪽 여백 위 8, 오른쪽 12, 아래 8, 왼쪽 12다.
    REQUIRE(text.find(u8"  inset 8 12 8 12\n") != std::u8string::npos);

    // 조상 한 단계를 올리면 견본 stack에서 그것을 감싼 panel이 집힌다.
    luil::inspector_config picking { config };
    picking.enabled = true;
    picking.pick = luil::inspector_pick::selected;
    picking.selected = stack->id();
    picking.ancestor_levels = 1;
    const luil::interaction_snapshot interaction {};
    const luil::ui_element* const picked { luil::pick_inspected(tree, interaction, picking) };
    REQUIRE(picked != nullptr);
    REQUIRE(picked->id() == luil::ui_element_id { mobile_demo::kind_layout, u8"inspector-sample" });
}

namespace {
    // 모바일 데모 앱이 on_key에서 하는 일과 같은 정책이다. 읽기 전용 안내 칸에 초점이 있는 채 닿은 되돌리기 키를 메시지로 바꾼다.
    class undo_key_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return mobile_demo::text_target_of_kind(kind);
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot) override
        {
            if (const auto undo { demo::readonly_undo_key_of(event, snapshot, mobile_demo::kind_readonly_input) }; undo.has_value())
                return { luil::make_app_action(*undo) };
            return {};
        }
    };

    [[nodiscard]] luil::key_pressed_event undo_chord(const luil::key_code code, const bool control, const bool shift = false)
    {
        luil::key_pressed_event event { code };
        event.control = control;
        event.shift = shift;
        return event;
    }

    // 안내 칸 하나를 든 tree에서 칸을 눌러 초점을 주고 되돌리기 키와 그 밖의 키를 보낸 뒤, 앱이 센 값이다.
    [[nodiscard]] demo::readonly_undo_keys press_undo_keys(const bool read_only)
    {
        undo_key_policy policy {};
        luil::interaction_controller controller { &policy };
        luil::text_input_view view {};
        view.text = u8"읽기 전용 안내";
        luil::text_area_config config {};
        config.read_only = read_only;
        auto area { std::make_unique<luil::text_area_element>(luil::ui_element_id { mobile_demo::kind_readonly_input }, std::move(view), std::move(config)) };
        controller.set_tree(std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(area), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f)));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 12.0f, luil::pointer_button::left, time_point {} }));
        static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 12.0f, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 5 } }));
        REQUIRE(controller.snapshot().focused_input.kind == mobile_demo::kind_readonly_input);

        demo::readonly_undo_keys counts {};
        const luil::key_pressed_event keys[] {
            undo_chord(luil::key_code::key_z, true),
            undo_chord(luil::key_code::key_y, true),
            undo_chord(luil::key_code::key_z, true, true),
            // 수식 키 없는 z와 다른 Ctrl 키는 되돌리기가 아니다.
            undo_chord(luil::key_code::key_z, false),
            undo_chord(luil::key_code::key_x, true),
        };
        for (const luil::key_pressed_event& event : keys)
        {
            for (const luil::input_action& action : controller.process(event))
            {
                if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                    static_cast<void>(counts.handle(*message));
            }
        }
        return counts;
    }
}

TEST_CASE("A read-only mobile field hands Ctrl+Z and Ctrl+Y to the app and the label counts them", "[mobile-demo][read-only]")
{
    // Ctrl+Z 하나, Ctrl+Y 하나, Ctrl+Shift+Z 하나(다시 실행)가 앱 on_key에 닿는다.
    const demo::readonly_undo_keys passed { press_undo_keys(true) };
    REQUIRE(passed.undo_count() == 1);
    REQUIRE(passed.redo_count() == 2);
    REQUIRE(passed.text() == u8"읽기 전용 칸에서 앱이 받은 Ctrl+Z 1회 · Ctrl+Y 2회");

    // 같은 칸이 편집 칸이면 칸이 되돌리기를 가져가 앱까지 오지 않는다.
    const demo::readonly_undo_keys kept { press_undo_keys(false) };
    REQUIRE(kept.undo_count() == 0);
    REQUIRE(kept.redo_count() == 0);
    REQUIRE(kept.text() == u8"읽기 전용 칸에서 앱이 받은 Ctrl+Z 0회 · Ctrl+Y 0회");
}

TEST_CASE("The device page reads the notification permission, the send result and the launch id", "[mobile-demo][notification]")
{
    const luil::ui_platform previous { luil::current_ui_platform() };
    luil::ui_platform platform { previous };
    platform.capabilities.notifications = true;
    luil::set_ui_platform(platform);
    const auto name_of = [](const luil::ui_tree& tree, const std::u8string& owner) {
        const luil::ui_element* const element { tree.find({ mobile_demo::kind_text, owner }) };
        REQUIRE(element != nullptr);
        return element->accessibility().name;
    };

    mobile_demo::device_page page {};
    auto tree { device_tree(page, false) };
    REQUIRE(name_of(tree, u8"notification-state") == u8"알림 권한: 묻지 않아도 된다.");
    REQUIRE(name_of(tree, u8"notification-result") == u8"알림 결과: 없음");
    REQUIRE(name_of(tree, u8"notification-launch") == u8"알림으로 시작: 아니오");
    REQUIRE(tree.find({ mobile_demo::kind_layout, u8"notification-send" }) != nullptr);
    REQUIRE(tree.find({ mobile_demo::kind_layout, u8"notification-ask" }) == nullptr);

    // 권한이 없으면 묻기 단추가 선다.
    REQUIRE(page.handle(luil::app_message { mobile_demo::notification_permission_intent { luil::permission_state::denied } }));
    tree = device_tree(page, false);
    REQUIRE(name_of(tree, u8"notification-state") == u8"알림 권한: 아직 받지 못했다.");
    const luil::ui_element* const ask { tree.find({ mobile_demo::kind_layout, u8"notification-ask" }) };
    REQUIRE(ask != nullptr);
    REQUIRE(ask->accessibility().name == u8"알림 권한 묻기");

    REQUIRE(page.handle(luil::app_message { mobile_demo::notification_permission_intent { luil::permission_state::granted, true } }));
    REQUIRE(page.handle(luil::app_message { mobile_demo::notification_send_intent { luil::platform_result { luil::platform_status::failed, u8"blocked", std::nullopt } } }));
    REQUIRE(page.handle(luil::app_message { mobile_demo::notification_launch_intent { u8"mobile-demo-1" } }));
    tree = device_tree(page, false);
    REQUIRE(name_of(tree, u8"notification-state") == u8"알림 권한: 받았다 (방금 허락).");
    REQUIRE(name_of(tree, u8"notification-result") == u8"알림 결과: 실패 · blocked");
    REQUIRE(name_of(tree, u8"notification-launch") == u8"알림으로 시작: mobile-demo-1");
    REQUIRE(tree.find({ mobile_demo::kind_layout, u8"notification-send" })->accessibility().name == u8"알림 보내기");

    // 능력이 없는 플랫폼은 단추 대신 이유를 보인다.
    platform.capabilities.notifications = false;
    luil::set_ui_platform(platform);
    tree = device_tree(page, false);
    REQUIRE(name_of(tree, u8"notification-none") == u8"이 플랫폼은 OS 알림을 올리지 못한다.");
    REQUIRE(tree.find({ mobile_demo::kind_layout, u8"notification-send" }) == nullptr);
    luil::set_ui_platform(previous);
}

namespace {
    // 400dp 창에서 지은 기본 페이지의 tree다 (배율 1).
    [[nodiscard]] luil::ui_tree clipboard_basics_tree(mobile_demo::basics_page& page)
    {
        auto content { page.build(400.0f, 1.0f) };
        const float height { content.height };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f);
    }

    // 클립보드 붙여넣기 결과 줄의 이름이다. 화면의 글이 곧 UIA Name이다.
    [[nodiscard]] std::u8string clipboard_basics_result(mobile_demo::basics_page& page)
    {
        const luil::ui_tree tree { clipboard_basics_tree(page) };
        const luil::ui_element* const found { tree.find({ mobile_demo::kind_text, u8"clipboard-image-result" }) };
        REQUIRE(found != nullptr);
        return found->accessibility().name;
    }

    [[nodiscard]] luil::clipboard_data clipboard_basics_data(const luil::clipboard_image_format format, std::vector<std::uint8_t> bytes)
    {
        luil::clipboard_data data {};
        data.image_format = format;
        data.image = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        return data;
    }
}

TEST_CASE("The basics page pastes a clipboard image and names what it read", "[mobile-demo][clipboard][image]")
{
    mobile_demo::basics_page page {};
    {
        const luil::ui_tree tree { clipboard_basics_tree(page) };
        const luil::ui_element* const button { tree.find({ mobile_demo::kind_layout, u8"clipboard-image-paste" }) };
        REQUIRE(button != nullptr);
        REQUIRE(button->accessibility().name == u8"클립보드 그림 붙여넣기");
    }
    REQUIRE(clipboard_basics_result(page) == u8"붙여넣기 결과: 아직 없음");

    // 그림이 없어도 답은 온다.
    REQUIRE(page.handle(luil::app_message { mobile_demo::clipboard_image_intent { luil::clipboard_data { .text = u8"글만" } } }));
    REQUIRE(clipboard_basics_result(page) == u8"붙여넣기 결과: 그림 없음");

    const std::vector<std::uint8_t> png { luil::testing::sample_quadrant_png.begin(), luil::testing::sample_quadrant_png.end() };
    REQUIRE(page.handle(luil::app_message { mobile_demo::clipboard_image_intent { clipboard_basics_data(luil::clipboard_image_format::encoded, png) } }));
    REQUIRE(clipboard_basics_result(page) == u8"붙여넣기 결과: 2×2 PNG");

    // 24비트 1×1 DIB다 (40바이트 머리, 행은 4바이트).
    std::vector<std::uint8_t> dib(44, 0u);
    dib[0] = 40;
    dib[4] = 1;
    dib[8] = 1;
    dib[12] = 1;
    dib[14] = 24;
    dib[40] = 0xFFu;
    REQUIRE(page.handle(luil::app_message { mobile_demo::clipboard_image_intent { clipboard_basics_data(luil::clipboard_image_format::dib, dib) } }));
    REQUIRE(clipboard_basics_result(page) == u8"붙여넣기 결과: 1×1 DIB");

    REQUIRE(page.handle(luil::app_message { mobile_demo::clipboard_image_intent { clipboard_basics_data(luil::clipboard_image_format::encoded, { 1, 2, 3 }) } }));
    REQUIRE(clipboard_basics_result(page) == u8"붙여넣기 결과: 풀지 못함");

    luil::clipboard_data large {};
    large.image_format = luil::clipboard_image_format::encoded;
    large.image_too_large = true;
    REQUIRE(page.handle(luil::app_message { mobile_demo::clipboard_image_intent { large } }));
    REQUIRE(clipboard_basics_result(page) == u8"붙여넣기 결과: 너무 큼");
}

TEST_CASE("The mobile basics page names its format toggles and reads the rich text state back", "[mobile-demo][basics][rich-text]")
{
    // 견본 글의 caret은 처음이라 다음 글자 서식은 뒤 글자("굵게")의 굵게다.
    mobile_demo::basics_page page {};
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rich-state") == u8"굵게 켜짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐");
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rich-count") == u8"구간 4 · 링크 1 · 되돌리기 0");
    REQUIRE(basics_text(page, luil::ui_element_kind::check, u8"rich-bold") == u8"굵게");
    REQUIRE(basics_text(page, luil::ui_element_kind::check, u8"rich-italic") == u8"기울임");
    REQUIRE(basics_text(page, luil::ui_element_kind::check, u8"rich-underline") == u8"밑줄");
    REQUIRE(basics_text(page, luil::ui_element_kind::check, u8"rich-code") == u8"코드");
    REQUIRE(basics_text(page, luil::ui_element_kind::check, u8"rich-link") == u8"링크");

    // 모두 골라 굵게를 누르면 한 걸음으로 글 전체가 굵어진다. 다른 서식과 겹친 곳은 따로 센다.
    luil::text_edit_request select_all {};
    select_all.target = mobile_demo::target_rich_editor;
    select_all.command = luil::text::text_edit_command::select_all;
    deliver(page, mobile_demo::edit_intent { select_all });
    deliver(page, demo::rich_text_command_intent { luil::rich_text_command::bold });
    REQUIRE(basics_text(page, mobile_demo::kind_text, u8"rich-count") == u8"구간 7 · 링크 1 · 되돌리기 1");
}

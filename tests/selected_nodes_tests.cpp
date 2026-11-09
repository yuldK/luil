// 선택 노드 여섯(dock·pager·time_picker·합자 caret·media·websocket)의 실제 시험이다.
// 외부 자원이 드는 시험은 숨은 태그 `[.live]`다. WebSocket은 `LUIL_WS_PORT`의 시험 서버가 있어야 하고,
// 미디어는 Windows 기본 파일(`LUIL_MEDIA_AUDIO`·`LUIL_MEDIA_VIDEO`로 바꿀 수 있다)을 소리 없이 재생한다.
#include "luil/luil.h"
#include "luil/net/websocket_client.h"
#include "luil/win32/media_player.h"
#include "host/font_registry.h"
#include "ui/ligature_carets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <limits>
#include <atomic>
#include <vector>

using namespace std::chrono_literals;

namespace {
    std::string s8(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }
}

namespace {
    // ---------- 공통 ----------
    const luil::ui_element* find_id(const luil::ui_element& node, const luil::ui_element_id& id)
    {
        if (node.id() == id)
            return &node;
        for (const auto& child : node.children())
            if (const auto* found { find_id(*child, id) }; found != nullptr)
                return found;
        return nullptr;
    }

    void collect(const luil::ui_element& node, std::vector<const luil::ui_element*>& out)
    {
        out.push_back(&node);
        for (const auto& child : node.children())
            collect(*child, out);
    }

    template<typename T>
    std::vector<T> messages_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<T> out {};
        for (const auto& action : actions)
            if (const auto* message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* value { message->get<T>() }; value != nullptr)
                    out.push_back(*value);
        return out;
    }

    // ---------- dock ----------
    luil::dock_layout three_groups()
    {
        // root = [editor, inspector] | right: [console]
        luil::dock_layout layout {};
        layout.root.panels = { u8"editor", u8"inspector", u8"console" };
        layout.root.selected = u8"editor";
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"console", .target = u8"root", .placement = luil::dock_placement::right }));
        return layout;
    }

    const luil::dock_node* group_of(const luil::dock_node& node, const std::u8string& panel)
    {
        if (std::ranges::find(node.panels, panel) != node.panels.end())
            return &node;
        for (const auto& child : node.children)
            if (const auto* found { group_of(child, panel) }; found != nullptr)
                return found;
        return nullptr;
    }

    const luil::dock_node* group_of(const luil::dock_layout& layout, const std::u8string& panel)
    {
        if (const auto* found { group_of(layout.root, panel) }; found != nullptr)
            return found;
        for (const auto& item : layout.floating)
            if (const auto* found { group_of(item.group, panel) }; found != nullptr)
                return found;
        return nullptr;
    }

    std::size_t panel_count(const luil::dock_node& node)
    {
        std::size_t count { node.panels.size() };
        for (const auto& child : node.children)
            count += panel_count(child);
        return count;
    }

    std::size_t panel_count(const luil::dock_layout& layout)
    {
        std::size_t count { panel_count(layout.root) };
        for (const auto& item : layout.floating)
            count += panel_count(item.group);
        return count;
    }

    struct dock_fixture
    {
        luil::dock_layout layout {};
        std::shared_ptr<luil::ui_tree> tree {};

        std::shared_ptr<luil::ui_tree> build(const float scale = 1.0f)
        {
            luil::dock_config config {};
            config.owner = u8"d";
            config.layout = layout;
            config.panels = { { u8"editor", u8"Editor" }, { u8"inspector", u8"Inspector" }, { u8"console", u8"Console" }, { u8"notes", u8"Notes" } };
            config.change = [](luil::dock_command command) { return luil::make_app_action(std::move(command)); };
            config.content = [](const std::u8string& panel) {
                return std::make_unique<luil::label_element>(luil::ui_element_id { luil::application_element_kind(900), panel }, luil::label_config { .text = panel });
            };
            tree = std::make_shared<luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::dock_element>(std::move(config)), { 0.0f, 0.0f, 1000.0f * scale, 600.0f * scale }, scale));
            return tree;
        }
    };

    // ---------- websocket ----------
    struct ws_log
    {
        std::mutex mutex {};
        std::condition_variable changed {};
        std::vector<luil::net::websocket_event> events {};
        void push(luil::net::websocket_event event)
        {
            {
                const std::scoped_lock lock { mutex };
                events.push_back(std::move(event));
            }
            changed.notify_all();
        }
        bool wait(const std::function<bool(const std::vector<luil::net::websocket_event>&)>& done, const std::chrono::milliseconds timeout = 5000ms)
        {
            std::unique_lock lock { mutex };
            return changed.wait_for(lock, timeout, [&] { return done(events); });
        }
        std::vector<luil::net::websocket_event> snapshot()
        {
            const std::scoped_lock lock { mutex };
            return events;
        }
    };

    bool has_kind(const std::vector<luil::net::websocket_event>& events, const luil::net::websocket_event_kind kind)
    {
        return std::ranges::any_of(events, [&](const auto& e) { return e.kind == kind; });
    }

    std::size_t count_kind(const std::vector<luil::net::websocket_event>& events, const luil::net::websocket_event_kind kind)
    {
        return static_cast<std::size_t>(std::ranges::count_if(events, [&](const auto& e) { return e.kind == kind; }));
    }

    std::string text_of(const luil::net::websocket_event& event)
    {
        return event.data != nullptr ? std::string { event.data->begin(), event.data->end() } : std::string {};
    }

    std::optional<std::string> env(const char* name)
    {
        char* value { nullptr };
        std::size_t size { 0 };
        if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
            return std::nullopt;
        std::string out { value };
        std::free(value);
        return out;
    }

    std::u8string u8(const std::string& text)
    {
        return { text.begin(), text.end() };
    }

    std::u8string ws_url(const std::string& path)
    {
        return u8(std::string { "ws://127.0.0.1:" } + env("LUIL_WS_PORT").value_or("0") + path);
    }

    std::unique_ptr<luil::net::websocket_client> connect(ws_log& log, const std::string& path, luil::net::websocket_client_config config = {})
    {
        config.deliver = [&log](luil::net::websocket_event event) { log.push(std::move(event)); };
        std::u8string error {};
        auto client { luil::net::websocket_client::create({ .url = ws_url(path), .connect_timeout = 3000ms }, std::move(config), error) };
        REQUIRE(client != nullptr);
        return client;
    }

    // ---------- media ----------
    struct media_log
    {
        std::mutex mutex {};
        std::vector<luil::win32::media_event> events {};
        std::thread::id thread {};
        void push(luil::win32::media_event event)
        {
            const std::scoped_lock lock { mutex };
            thread = std::this_thread::get_id();
            events.push_back(std::move(event));
        }
        std::vector<luil::win32::media_event> snapshot()
        {
            const std::scoped_lock lock { mutex };
            return events;
        }
        bool saw(const luil::win32::media_state state)
        {
            const std::scoped_lock lock { mutex };
            return std::ranges::any_of(events, [&](const auto& e) { return e.state == state; });
        }
    };

    // UI thread의 메시지를 돌리며 조건을 기다린다.
    bool pump_until(const std::function<bool()>& done, const std::chrono::milliseconds timeout = 8000ms)
    {
        const auto until { std::chrono::steady_clock::now() + timeout };
        while (std::chrono::steady_clock::now() < until)
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (done())
                return true;
            std::this_thread::sleep_for(10ms);
        }
        return done();
    }

    std::u8string media_path(const char* name, const char* fallback)
    {
        return u8(env(name).value_or(fallback));
    }

    // ---------- GDEF 표 짓기 ----------
    struct caret_spec
    {
        std::uint16_t format { 1 };
        std::int16_t value { 0 };
        std::uint16_t device { 0 };
    };

    void put(std::vector<std::uint8_t>& bytes, const std::size_t at, const std::uint16_t value)
    {
        if (bytes.size() < at + 2)
            bytes.resize(at + 2);
        bytes[at] = static_cast<std::uint8_t>(value >> 8);
        bytes[at + 1] = static_cast<std::uint8_t>(value & 0xff);
    }

    // glyph마다 caret 목록이 있는 GDEF 1.0 표다. coverage_format 2면 연속 범위 하나로 적는다.
    std::vector<std::uint8_t> gdef_table(const std::vector<std::pair<std::uint16_t, std::vector<caret_spec>>>& glyphs, const int coverage_format = 1)
    {
        std::vector<std::uint8_t> b(12, 0);
        put(b, 0, 1);
        put(b, 2, 0);
        put(b, 8, 12);
        const std::size_t list { 12 };
        const std::size_t count { glyphs.size() };
        std::size_t at { list + 4 + count * 2 };
        put(b, list + 2, static_cast<std::uint16_t>(count));
        // coverage
        put(b, list, static_cast<std::uint16_t>(at - list));
        if (coverage_format == 1)
        {
            put(b, at, 1);
            put(b, at + 2, static_cast<std::uint16_t>(count));
            for (std::size_t i { 0 }; i < count; ++i)
                put(b, at + 4 + i * 2, glyphs[i].first);
            at += 4 + count * 2;
        }
        else
        {
            put(b, at, 2);
            put(b, at + 2, 1);
            put(b, at + 4, glyphs.front().first);
            put(b, at + 6, glyphs.back().first);
            put(b, at + 8, 0);
            at += 10;
        }
        for (std::size_t i { 0 }; i < count; ++i)
        {
            const auto& carets { glyphs[i].second };
            put(b, list + 4 + i * 2, static_cast<std::uint16_t>(at - list));
            const std::size_t lig { at };
            put(b, lig, static_cast<std::uint16_t>(carets.size()));
            std::size_t value { lig + 2 + carets.size() * 2 };
            for (std::size_t j { 0 }; j < carets.size(); ++j)
            {
                put(b, lig + 2 + j * 2, static_cast<std::uint16_t>(value - lig));
                put(b, value, carets[j].format);
                put(b, value + 2, static_cast<std::uint16_t>(carets[j].value));
                value += 4;
                if (carets[j].format == 3)
                {
                    put(b, value, carets[j].device);
                    value += 2;
                }
            }
            at = value;
        }
        b.resize(at);
        return b;
    }
}

// =====================================================================
// 도킹 패널
// =====================================================================

TEST_CASE("dock: moves keep every panel and refuse bad targets without changing the layout", "[selected][dock]")
{
    auto layout { three_groups() };
    REQUIRE(luil::valid_dock_layout(layout));
    REQUIRE(layout.root.children.size() == 2);
    REQUIRE(panel_count(layout) == 3);

    SECTION("같은 그룹 안의 순서 바꾸기")
    {
        const auto key { group_of(layout, u8"editor")->key };
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"editor", .target = key, .relative_to = u8"inspector", .side = luil::drop_side::after }));
        REQUIRE(group_of(layout, u8"editor")->panels == std::vector<std::u8string> { u8"inspector", u8"editor" });
        REQUIRE(group_of(layout, u8"editor")->selected == u8"editor");
        // 자기 자신 앞에 놓기는 이동이 아니다.
        const auto before { layout };
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .panel = u8"editor", .target = key, .relative_to = u8"editor" }));
        REQUIRE(group_of(layout, u8"editor")->panels == group_of(before, u8"editor")->panels);
    }
    SECTION("다른 그룹으로 옮기면 빈 그룹과 한쪽만 남은 분할이 정리된다")
    {
        const auto target { group_of(layout, u8"editor")->key };
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"console", .target = target }));
        REQUIRE(layout.root.children.empty());
        REQUIRE(layout.root.panels.size() == 3);
        REQUIRE(layout.root.selected == u8"console");
        REQUIRE(luil::valid_dock_layout(layout));
    }
    SECTION("가장자리 놓기는 분할을 만든다 (네 방향)")
    {
        for (const auto placement : { luil::dock_placement::left, luil::dock_placement::right, luil::dock_placement::top, luil::dock_placement::bottom })
        {
            auto copy { layout };
            const auto target { group_of(copy, u8"editor")->key };
            REQUIRE(luil::apply_dock_command(copy, { .panel = u8"inspector", .target = target, .placement = placement }));
            REQUIRE(luil::valid_dock_layout(copy));
            REQUIRE(panel_count(copy) == 3);
            REQUIRE(group_of(copy, u8"inspector")->panels == std::vector<std::u8string> { u8"inspector" });
            REQUIRE(group_of(copy, u8"inspector") != group_of(copy, u8"editor"));
        }
    }
    SECTION("한 패널뿐인 그룹을 자기 가장자리에 놓아도 패널이 사라지지 않는다")
    {
        const auto target { group_of(layout, u8"console")->key };
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"console", .target = target, .placement = luil::dock_placement::left }) == true);
        REQUIRE(panel_count(layout) == 3);
        REQUIRE(group_of(layout, u8"console") != nullptr);
        REQUIRE(luil::valid_dock_layout(layout));
    }
    SECTION("중첩 분할 셋")
    {
        layout.root = { .key = u8"root", .panels = { u8"editor", u8"inspector", u8"console", u8"notes" }, .selected = u8"editor" };
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"inspector", .target = u8"root", .placement = luil::dock_placement::right }));
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"console", .target = group_of(layout, u8"inspector")->key, .placement = luil::dock_placement::bottom }));
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"notes", .target = group_of(layout, u8"console")->key, .placement = luil::dock_placement::left }));
        REQUIRE(luil::valid_dock_layout(layout));
        REQUIRE(panel_count(layout) == 4);
        // 하나씩 되붙이면 다시 한 그룹이 된다.
        const auto home { group_of(layout, u8"editor")->key };
        for (const auto* panel : { u8"notes", u8"console", u8"inspector" })
            REQUIRE(luil::apply_dock_command(layout, { .panel = panel, .target = group_of(layout, u8"editor")->key }));
        REQUIRE(layout.root.children.empty());
        REQUIRE(layout.root.panels.size() == 4);
        REQUIRE(layout.root.key == home);
    }
    SECTION("떼어 띄우고 다시 붙이기")
    {
        REQUIRE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::detach, .panel = u8"inspector", .bounds = { 10.0f, 20.0f, 200.0f, 100.0f } }));
        REQUIRE(layout.floating.size() == 1);
        REQUIRE(layout.floating[0].bounds.x == 10.0f);
        REQUIRE(layout.floating[0].group.panels == std::vector<std::u8string> { u8"inspector" });
        const auto floating_key { layout.floating[0].group.key };
        // 떠 있는 그룹에 다른 패널을 합친다.
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"console", .target = floating_key }));
        REQUIRE(layout.floating[0].group.panels.size() == 2);
        REQUIRE(layout.root.children.empty());
        // 띄운 것의 위치 옮기기·앞으로 세우기
        REQUIRE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::reposition, .target = floating_key, .bounds = { 5.0f, -5.0f } }));
        REQUIRE(layout.floating[0].bounds.x == 15.0f);
        REQUIRE(layout.floating[0].bounds.y == 15.0f);
        // 모두 되붙이면 떠 있는 그룹이 사라진다.
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"inspector", .target = layout.root.key }));
        REQUIRE(luil::apply_dock_command(layout, { .panel = u8"console", .target = layout.root.key }));
        REQUIRE(layout.floating.empty());
        REQUIRE(panel_count(layout) == 3);
    }
    SECTION("잘못된 명령은 거절하고 원본을 지킨다")
    {
        const auto before { layout };
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .panel = u8"nope", .target = u8"root" }));
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .panel = u8"editor", .target = u8"missing" }));
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::select, .panel = u8"console", .target = group_of(layout, u8"editor")->key }));
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::resize, .target = group_of(layout, u8"editor")->key, .delta = 0.1f }));
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::raise, .target = u8"root" }));
        // 분할(자식 있는 node) 위로의 move는 받지 않는다.
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .panel = u8"editor", .target = layout.root.key }));
        REQUIRE(panel_count(layout) == panel_count(before));
        REQUIRE(layout.root.children.size() == before.root.children.size());
    }
    SECTION("resize는 비율을 0.05~0.95로 묶는다")
    {
        REQUIRE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::resize, .target = layout.root.key, .delta = 5.0f }));
        REQUIRE(layout.root.ratio == Catch::Approx(0.95f));
        REQUIRE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::resize, .target = layout.root.key, .delta = -5.0f }));
        REQUIRE(layout.root.ratio == Catch::Approx(0.05f));
        REQUIRE_FALSE(luil::apply_dock_command(layout, { .operation = luil::dock_operation::resize, .target = layout.root.key, .delta = std::numeric_limits<float>::quiet_NaN() }));
    }
    SECTION("중복 key·panel은 잘못된 layout이다")
    {
        auto bad { layout };
        bad.root.children[1].panels.push_back(u8"editor");
        REQUIRE_FALSE(luil::valid_dock_layout(bad));
        REQUIRE_FALSE(luil::apply_dock_command(bad, { .panel = u8"inspector", .target = bad.root.children[1].key }));
    }
}

TEST_CASE("dock: an element wires tab select, group edge drop, tab-bar drop, and unclaimed detach to commands", "[selected][dock]")
{
    dock_fixture fixture {};
    fixture.layout = three_groups();
    auto tree { fixture.build() };
    const auto& root { *tree->root() };
    const auto left_key { group_of(fixture.layout, u8"editor")->key };
    const auto right_key { group_of(fixture.layout, u8"console")->key };

    // 탭은 tab_bar 안의 tab이다. 같은 key의 탭이 다른 그룹에 둘 있지 않으니 kind+owner로 찾는다.
    const auto* editor_tab { find_id(root, { luil::ui_element_kind::tab, u8"editor" }) };
    const auto* console_tab { find_id(root, { luil::ui_element_kind::tab, u8"console" }) };
    const auto* inspector_tab { find_id(root, { luil::ui_element_kind::tab, u8"inspector" }) };
    REQUIRE(editor_tab != nullptr);
    REQUIRE(console_tab != nullptr);
    REQUIRE(inspector_tab != nullptr);

    SECTION("탭 누르기는 select 명령이다")
    {
        const auto* action { inspector_tab->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        const auto commands { messages_of<luil::dock_command>((*action)({ inspector_tab->id(), 0.0f, 0.0f })) };
        REQUIRE(commands.size() == 1);
        REQUIRE(commands[0].operation == luil::dock_operation::select);
        REQUIRE(commands[0].target == left_key);
        REQUIRE(luil::apply_dock_command(fixture.layout, commands[0]));
        REQUIRE(group_of(fixture.layout, u8"inspector")->selected == u8"inspector");
    }
    SECTION("다른 그룹 본문의 가장자리로 놓으면 그 방향 분할이다")
    {
        REQUIRE(console_tab->drag() != nullptr);
        const auto payload { console_tab->drag()->make_payload({ console_tab->id(), console_tab->bounds().x + 5.0f, console_tab->bounds().y + 5.0f }) };
        const auto* group { find_id(root, { luil::ui_element_kind::dock_group, u8"d/" + left_key }) };
        REQUIRE(group != nullptr);
        REQUIRE(group->drop() != nullptr);
        REQUIRE(group->drop()->accepts(payload));
        const auto box { group->bounds() };
        struct probe
        {
            float fx;
            float fy;
            luil::dock_placement expected;
        };
        const probe probes[] {
            { 0.05f, 0.5f, luil::dock_placement::left }
            , { 0.95f, 0.5f, luil::dock_placement::right }
            , { 0.5f, 0.05f, luil::dock_placement::top }
            , { 0.5f, 0.95f, luil::dock_placement::bottom }
            , { 0.5f, 0.5f, luil::dock_placement::center }
        };
        for (const auto& p : probes)
        {
            const auto commands { messages_of<luil::dock_command>(group->drop()->on_drop(payload, { group->id(), box.x + box.width * p.fx, box.y + box.height * p.fy })) };
            REQUIRE(commands.size() == 1);
            CHECK(commands[0].placement == p.expected);
            CHECK(commands[0].panel == u8"console");
            auto copy { fixture.layout };
            REQUIRE(luil::apply_dock_command(copy, commands[0]));
            REQUIRE(panel_count(copy) == 3);
        }
    }
    SECTION("다른 그룹의 탭 막대로 놓으면 탭 사이에 들어간다")
    {
        const auto payload { console_tab->drag()->make_payload({ console_tab->id(), console_tab->bounds().x + 5.0f, console_tab->bounds().y + 5.0f }) };
        REQUIRE(editor_tab->drop() != nullptr);
        REQUIRE(editor_tab->drop()->accepts(payload));
        const auto commands { messages_of<luil::dock_command>(editor_tab->drop()->on_drop(payload, { editor_tab->id(), editor_tab->bounds().x + 2.0f, editor_tab->bounds().y + 2.0f })) };
        REQUIRE(commands.size() == 1);
        REQUIRE(commands[0].target == left_key);
        REQUIRE(luil::apply_dock_command(fixture.layout, commands[0]));
        REQUIRE(group_of(fixture.layout, u8"console")->panels.size() == 3);
        REQUIRE(fixture.layout.root.children.empty());
    }
    SECTION("받는 곳 없이 놓으면 그 자리에 떠 있는 패널이 된다")
    {
        const auto payload { console_tab->drag()->make_payload({ console_tab->id(), console_tab->bounds().x + 5.0f, console_tab->bounds().y + 5.0f }) };
        REQUIRE(console_tab->drag()->on_unclaimed_drop != nullptr);
        luil::drag_release release {};
        release.x = 300.0f;
        release.y = 200.0f;
        const auto commands { messages_of<luil::dock_command>(console_tab->drag()->on_unclaimed_drop(payload, release)) };
        REQUIRE(commands.size() == 1);
        REQUIRE(commands[0].operation == luil::dock_operation::detach);
        REQUIRE(commands[0].release.has_value());
        REQUIRE(luil::apply_dock_command(fixture.layout, commands[0]));
        REQUIRE(fixture.layout.floating.size() == 1);
        // 띄운 자리는 놓은 자리에서 잡은 지점만큼 뺀 dock 좌표다.
        CHECK(fixture.layout.floating[0].bounds.x == Catch::Approx(300.0f - payload.grab_offset_x));
        CHECK(fixture.layout.floating[0].bounds.y == Catch::Approx(200.0f - payload.grab_offset_y));
        // 새 tree에 떠 있는 그룹이 선다.
        auto rebuilt { fixture.build() };
        REQUIRE(find_id(*rebuilt->root(), { luil::ui_element_kind::dock_group, u8"d/" + fixture.layout.floating[0].group.key }) != nullptr);
    }
    SECTION("다른 dock의 탭은 받지 않는다")
    {
        auto payload { console_tab->drag()->make_payload({ console_tab->id(), 0.0f, 0.0f }) };
        payload.container.owner = u8"other/" + right_key;
        const auto* group { find_id(root, { luil::ui_element_kind::dock_group, u8"d/" + left_key }) };
        CHECK_FALSE(group->drop()->accepts(payload));
        CHECK_FALSE(editor_tab->drop()->accepts(payload));
    }
}

TEST_CASE("dock: the split handle resize is a ratio delta of the pane extent at any scale", "[selected][dock]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        dock_fixture fixture {};
        fixture.layout = three_groups();
        auto tree { fixture.build(scale) };
        const auto* handle { find_id(*tree->root(), { luil::ui_element_kind::dock_drop, u8"d/" + fixture.layout.root.key }) };
        REQUIRE(handle != nullptr);
        // 손잡이는 가운데 근처(ratio 0.5)에 선다.
        const auto* left { find_id(*tree->root(), { luil::ui_element_kind::dock_group, u8"d/" + group_of(fixture.layout, u8"editor")->key }) };
        REQUIRE(left != nullptr);
        CHECK(left->bounds().width == Catch::Approx((1000.0f - 6.0f) * scale * 0.5f).margin(1.0f));
        CHECK(handle->bounds().width == Catch::Approx(6.0f * scale));
    }
}

TEST_CASE("dock: a real mouse drag through the interaction controller docks a tab to an edge", "[selected][dock][interaction]")
{
    dock_fixture fixture {};
    fixture.layout = three_groups();
    auto tree { fixture.build() };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const auto* console_tab { find_id(*tree->root(), { luil::ui_element_kind::tab, u8"console" }) };
    const auto* left { find_id(*tree->root(), { luil::ui_element_kind::dock_group, u8"d/" + group_of(fixture.layout, u8"editor")->key }) };
    REQUIRE(console_tab != nullptr);
    REQUIRE(left != nullptr);
    const auto start_x { console_tab->bounds().x + 10.0f };
    const auto start_y { console_tab->bounds().y + 10.0f };
    const auto now { std::chrono::steady_clock::now() };
    std::vector<luil::input_action> all {};
    const auto take = [&](std::vector<luil::input_action> actions) { all.insert(all.end(), actions.begin(), actions.end()); };
    take(controller.process(luil::pointer_moved_event { start_x, start_y, now }));
    take(controller.process(luil::pointer_pressed_event { start_x, start_y, luil::pointer_button::left, now }));
    // 왼쪽 그룹 본문의 아래 가장자리로 끈다.
    const auto box { left->bounds() };
    const float end_x { box.x + box.width * 0.5f };
    const float end_y { box.y + box.height * 0.95f };
    for (int step { 1 }; step <= 20; ++step)
    {
        const float t { static_cast<float>(step) / 20.0f };
        take(controller.process(luil::pointer_moved_event { start_x + (end_x - start_x) * t, start_y + (end_y - start_y) * t, now + std::chrono::milliseconds { step * 16 } }));
    }
    REQUIRE(controller.snapshot().drag.has_value());
    CHECK(controller.snapshot().drag->hovered_drop_target == left->id());
    take(controller.process(luil::pointer_released_event { end_x, end_y, luil::pointer_button::left, now + 400ms }));
    const auto commands { messages_of<luil::dock_command>(all) };
    REQUIRE(commands.empty() == false);
    const auto& last { commands.back() };
    CHECK(last.placement == luil::dock_placement::bottom);
    REQUIRE(luil::apply_dock_command(fixture.layout, last));
    REQUIRE(panel_count(fixture.layout) == 3);
    REQUIRE(luil::valid_dock_layout(fixture.layout));
}

TEST_CASE("dock: content is built only for selected panels and names are exposed to assistive tech", "[selected][dock][accessibility]")
{
    dock_fixture fixture {};
    fixture.layout = three_groups();
    auto tree { fixture.build() };
    const auto& root { *tree->root() };
    CHECK(find_id(root, { luil::application_element_kind(900), u8"editor" }) != nullptr);
    CHECK(find_id(root, { luil::application_element_kind(900), u8"inspector" }) == nullptr);
    CHECK(find_id(root, { luil::application_element_kind(900), u8"console" }) != nullptr);
    // 탭 막대(묶음)와 분할 손잡이의 접근 이름. 비면 화면 읽기가 "탭", "엄지"로만 읽는다.
    std::vector<const luil::ui_element*> all {};
    collect(root, all);
    for (const auto* element : all)
    {
        const auto info { element->accessibility() };
        if (element->id().kind == luil::ui_element_kind::tab_bar || element->id().kind == luil::ui_element_kind::dock_drop)
        {
            INFO("kind " << static_cast<int>(element->id().kind) << " owner " << s8(element->id().owner));
            CHECK(info.name.empty() == false);
        }
    }
}

// =====================================================================
// 넘기는 페이지
// =====================================================================

TEST_CASE("pager: commands clamp, step from the settled page, and settle by projected velocity", "[selected][pager]")
{
    CHECK(luil::apply_pager_command(0.0f, 3, { .delta = 0.25f }) == Catch::Approx(0.25f));
    CHECK(luil::apply_pager_command(0.0f, 3, { .delta = -1.0f }) == 0.0f);
    CHECK(luil::apply_pager_command(1.75f, 3, { .delta = 5.0f }) == 2.0f);
    CHECK(luil::apply_pager_command(1.4f, 3, { .operation = luil::pager_operation::step, .delta = 1.0f }) == 2.0f);
    CHECK(luil::apply_pager_command(0.6f, 3, { .operation = luil::pager_operation::step, .delta = -1.0f }) == 0.0f);
    CHECK(luil::apply_pager_command(0.0f, 3, { .operation = luil::pager_operation::select, .page = 9 }) == 2.0f);
    CHECK(luil::apply_pager_command(0.0f, 0, { .delta = 3.0f }) == 0.0f);
    CHECK(luil::apply_pager_command(std::numeric_limits<float>::quiet_NaN(), 3, { .delta = 1.0f }) == 1.0f);
    // 느린 놓기: 가까운 쪽
    CHECK(luil::apply_pager_command(0.4f, 3, { .operation = luil::pager_operation::settle }) == 0.0f);
    CHECK(luil::apply_pager_command(0.6f, 3, { .operation = luil::pager_operation::settle }) == 1.0f);
    // 빠른 튕김: 0.2만 넘겨도 다음 장 (0.18초 투영)
    CHECK(luil::apply_pager_command(0.2f, 3, { .operation = luil::pager_operation::settle, .velocity = 3.0f }) == 1.0f);
    // 아주 빠른 튕김도 한 장까지만
    CHECK(luil::apply_pager_command(0.1f, 3, { .operation = luil::pager_operation::settle, .velocity = 100.0f }) == 1.0f);
    CHECK(luil::apply_pager_command(1.0f, 3, { .operation = luil::pager_operation::settle, .velocity = -100.0f }) == 0.0f);
    // 뗄 때의 마지막 이동도 반영한다
    CHECK(luil::apply_pager_command(0.3f, 3, { .operation = luil::pager_operation::settle, .delta = 0.3f }) == 1.0f);
}

namespace {
    struct pager_state
    {
        float offset { 0.0f };
        std::shared_ptr<luil::ui_tree> build(const float scale = 1.0f)
        {
            luil::pager_config config {};
            config.offset = offset;
            config.name = u8"pages";
            config.change = [](luil::pager_command command) { return luil::make_app_action(command); };
            auto pager { std::make_unique<luil::pager_element>(luil::ui_element_id { luil::ui_element_kind::pager, u8"p" }, std::move(config)) };
            for (int i { 0 }; i < 3; ++i)
                pager->add_page(std::make_unique<luil::label_element>(luil::ui_element_id { luil::application_element_kind(901), u8(std::to_string(i)) }, luil::label_config { .text = u8"page" }));
            return std::make_shared<luil::ui_tree>(luil::make_arranged_tree(std::move(pager), { 0.0f, 0.0f, 400.0f * scale, 300.0f * scale }, scale));
        }
        void apply(const std::vector<luil::input_action>& actions)
        {
            for (const auto& command : messages_of<luil::pager_command>(actions))
                offset = luil::apply_pager_command(offset, 3, command);
        }
    };
}

TEST_CASE("pager: pages are laid out side by side and clipped", "[selected][pager]")
{
    pager_state state {};
    state.offset = 1.25f;
    auto tree { state.build() };
    const auto* first { find_id(*tree->root(), { luil::application_element_kind(901), u8"0" }) };
    const auto* second { find_id(*tree->root(), { luil::application_element_kind(901), u8"1" }) };
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->bounds().x == Catch::Approx(-500.0f));
    CHECK(second->bounds().x == Catch::Approx(-100.0f));
    CHECK(tree->root()->accessibility().name == u8"pages");
}

TEST_CASE("pager: a touch swipe moves with the finger and settles on release", "[selected][pager][interaction][touch]")
{
    for (const float scale : { 1.0f, 2.0f })
    {
        pager_state state {};
        auto tree { state.build(scale) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        const auto now { std::chrono::steady_clock::now() };
        luil::pointer_pressed_event press { 300.0f * scale, 150.0f * scale, luil::pointer_button::left, now };
        press.device = luil::pointer_device::touch;
        press.pointer_id = 1;
        press.scale = scale;
        state.apply(controller.process(press));
        // 천천히 왼쪽으로 120 논리 픽셀(0.3장) 끈다.
        float last_offset { 0.0f };
        for (int step { 1 }; step <= 12; ++step)
        {
            luil::pointer_moved_event move { (300.0f - 10.0f * static_cast<float>(step)) * scale, 150.0f * scale, now + std::chrono::milliseconds { step * 40 } };
            move.device = luil::pointer_device::touch;
            move.pointer_id = 1;
            move.in_contact = true;
            state.apply(controller.process(move));
            controller.set_tree(tree = state.build(scale));
            CHECK(state.offset >= last_offset);
            last_offset = state.offset;
        }
        INFO("scale " << scale << " offset during drag " << state.offset);
        CHECK(state.offset > 0.15f);
        CHECK(state.offset < 0.35f);
        // 멈춘 뒤 뗀다 → 가까운 0장으로 정착
        luil::pointer_released_event release { 180.0f * scale, 150.0f * scale, luil::pointer_button::left, now + 1200ms };
        release.device = luil::pointer_device::touch;
        release.pointer_id = 1;
        state.apply(controller.process(release));
        CHECK(state.offset == 0.0f);

        // 빠른 튕김 → 다음 장
        state.offset = 0.0f;
        controller.set_tree(tree = state.build(scale));
        const auto t0 { now + 3s };
        press.time = t0;
        state.apply(controller.process(press));
        for (int step { 1 }; step <= 4; ++step)
        {
            luil::pointer_moved_event move { (300.0f - 20.0f * static_cast<float>(step)) * scale, 150.0f * scale, t0 + std::chrono::milliseconds { step * 10 } };
            move.device = luil::pointer_device::touch;
            move.pointer_id = 1;
            move.in_contact = true;
            state.apply(controller.process(move));
            controller.set_tree(tree = state.build(scale));
        }
        release.x = 220.0f * scale;
        release.time = t0 + 45ms;
        state.apply(controller.process(release));
        INFO("fling result " << state.offset);
        CHECK(state.offset == 1.0f);
    }
}

TEST_CASE("pager: keyboard arrows step and Home/End jump while focused", "[selected][pager][interaction][keyboard]")
{
    pager_state state {};
    auto tree { state.build() };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    REQUIRE(tree->root()->tab_stop());
    REQUIRE(tree->root()->key_step() != nullptr);
    const auto step = [&](luil::value_step value) {
        auto result { tree->root()->key_step()->on_step(value) };
        REQUIRE(result.has_value());
        state.apply(*result);
        tree = state.build();
    };
    step(luil::value_step::increase);
    CHECK(state.offset == 1.0f);
    step(luil::value_step::increase);
    step(luil::value_step::increase);
    CHECK(state.offset == 2.0f);
    step(luil::value_step::minimum);
    CHECK(state.offset == 0.0f);
    step(luil::value_step::maximum);
    CHECK(state.offset == 2.0f);
    step(luil::value_step::decrease);
    CHECK(state.offset == 1.0f);
}

TEST_CASE("pager: a mouse wheel notch lands on a page instead of a fraction", "[selected][pager][interaction][wheel]")
{
    pager_state state {};
    auto tree { state.build() };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    // 가로 휠 한 눈금
    luil::mouse_wheel_event wheel { 200.0f, 150.0f, -120.0f, std::chrono::steady_clock::now() };
    wheel.horizontal = true;
    state.apply(controller.process(wheel));
    INFO("offset after one horizontal notch " << state.offset);
    CHECK((state.offset == 0.0f || state.offset == 1.0f));
    // 세로 휠(가로 내용 위의 일반 마우스)
    state.offset = 0.0f;
    controller.set_tree(tree = state.build());
    luil::mouse_wheel_event vertical { 200.0f, 150.0f, -120.0f, std::chrono::steady_clock::now() + 1s };
    state.apply(controller.process(vertical));
    INFO("offset after one vertical notch " << state.offset);
    CHECK((state.offset == 0.0f || state.offset == 1.0f));
}

// =====================================================================
// 시·분 선택기
// =====================================================================

TEST_CASE("time_picker: reads 24 and 12 hour values and rejects bad input", "[selected][time]")
{
    using luil::time_of_day;
    CHECK(luil::read_time_picker(u8"0", u8"0") == time_of_day { 0, 0 });
    CHECK(luil::read_time_picker(u8"23", u8"59") == time_of_day { 23, 59 });
    CHECK(luil::read_time_picker(u8"07", u8"05") == time_of_day { 7, 5 });
    CHECK_FALSE(luil::read_time_picker(u8"24", u8"00").has_value());
    CHECK_FALSE(luil::read_time_picker(u8"12", u8"60").has_value());
    CHECK_FALSE(luil::read_time_picker(u8"", u8"10").has_value());
    CHECK_FALSE(luil::read_time_picker(u8"1.5", u8"10").has_value());
    CHECK_FALSE(luil::read_time_picker(u8"-1", u8"10").has_value());
    CHECK_FALSE(luil::read_time_picker(u8"ab", u8"10").has_value());
    CHECK(luil::read_time_picker(u8"12", u8"00", true, false) == time_of_day { 0, 0 });
    CHECK(luil::read_time_picker(u8"12", u8"00", true, true) == time_of_day { 12, 0 });
    CHECK(luil::read_time_picker(u8"1", u8"30", true, true) == time_of_day { 13, 30 });
    CHECK(luil::read_time_picker(u8"11", u8"59", true, false) == time_of_day { 11, 59 });
    CHECK_FALSE(luil::read_time_picker(u8"0", u8"00", true, false).has_value());
    CHECK_FALSE(luil::read_time_picker(u8"13", u8"00", true, true).has_value());
    CHECK(luil::format_time_part(5) == u8"05");
    CHECK(luil::format_time_part(59) == u8"59");
    CHECK(luil::format_time_part(60).empty());
}

namespace {
    std::shared_ptr<luil::ui_tree> time_tree(const bool twelve)
    {
        luil::time_picker_config config {};
        config.owner = u8"t";
        config.hour = { .text = u8"09" };
        config.minute = { .text = u8"30" };
        config.twelve_hour = twelve;
        config.hour_to = [](std::u8string text) { return luil::make_app_action(std::pair<int, std::u8string> { 0, std::move(text) }); };
        config.minute_to = [](std::u8string text) { return luil::make_app_action(std::pair<int, std::u8string> { 1, std::move(text) }); };
        config.toggle_period = luil::make_message_action(std::pair<int, std::u8string> { 2, u8"period" });
        return std::make_shared<luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::time_picker_element>(std::move(config)), { 0.0f, 0.0f, 300.0f, 28.0f }, 1.0f));
    }
}

TEST_CASE("time_picker: every element id in the picker is unique so both spin pairs reach assistive tech", "[selected][time][accessibility]")
{
    for (const bool twelve : { false, true })
    {
        auto tree { time_tree(twelve) };
        std::vector<const luil::ui_element*> all {};
        collect(*tree->root(), all);
        std::set<std::pair<int, std::u8string>> seen {};
        std::vector<std::string> duplicates {};
        for (const auto* element : all)
        {
            if (element->id() == luil::ui_element_id {})
                continue;
            if (seen.insert({ static_cast<int>(element->id().kind), element->id().owner }).second == false)
                duplicates.push_back(std::to_string(static_cast<int>(element->id().kind)) + ":" + s8(element->id().owner));
        }
        INFO("twelve_hour " << twelve);
        for (const auto& d : duplicates)
            UNSCOPED_INFO("duplicate id " << d);
        CHECK(duplicates.empty());
        const auto* hour { find_id(*tree->root(), { luil::ui_element_kind::time_picker_hour, u8"t/hour" }) };
        const auto* minute { find_id(*tree->root(), { luil::ui_element_kind::time_picker_minute, u8"t/minute" }) };
        REQUIRE(hour != nullptr);
        REQUIRE(minute != nullptr);
        CHECK(hour->accessibility().name.empty() == false);
        CHECK(minute->accessibility().name.empty() == false);
        CHECK(hour->accessibility().name != minute->accessibility().name);
    }
}

TEST_CASE("time_picker: pressing the minute spin buttons changes the minute, not the hour", "[selected][time][interaction]")
{
    auto tree { time_tree(false) };
    // 분 칸 안의 늘리기 단추를 찾는다(분 칸의 자식).
    const auto* minute { find_id(*tree->root(), { luil::ui_element_kind::time_picker_minute, u8"t/minute" }) };
    REQUIRE(minute != nullptr);
    std::vector<const luil::ui_element*> inside {};
    collect(*minute, inside);
    const luil::ui_element* plus { nullptr };
    for (const auto* element : inside)
        if (element != minute && element->action(luil::ui_trigger::left_click) != nullptr && element->accessibility().name.find(u8"늘") != std::u8string::npos)
            plus = element;
    if (plus == nullptr)
        for (const auto* element : inside)
            if (element != minute && element->action(luil::ui_trigger::left_click) != nullptr)
                plus = element;
    REQUIRE(plus != nullptr);
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const float x { plus->bounds().x + plus->bounds().width / 2.0f };
    const float y { plus->bounds().y + plus->bounds().height / 2.0f };
    const auto now { std::chrono::steady_clock::now() };
    std::vector<luil::input_action> all {};
    for (auto a : controller.process(luil::pointer_moved_event { x, y, now }))
        all.push_back(std::move(a));
    for (auto a : controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, now }))
        all.push_back(std::move(a));
    for (auto a : controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, now + 50ms }))
        all.push_back(std::move(a));
    const auto changes { messages_of<std::pair<int, std::u8string>>(all) };
    REQUIRE(changes.empty() == false);
    CHECK(changes.back().first == 1);
    CHECK(changes.back().second == u8"31");
}

// =====================================================================
// 합자 caret (GDEF)
// =====================================================================

TEST_CASE("lig_caret: GDEF parser reads formats 1 and 3, both coverage formats, and refuses the rest", "[selected][ligature]")
{
    const auto table { gdef_table({ { 10, { { 1, 300 } } }, { 11, { { 1, 200 }, { 3, 400, 0 } } }, { 12, { { 1, -50 } } } }) };
    CHECK(luil::ligature_caret_coordinates(table, 10) == std::vector<std::int16_t> { 300 });
    CHECK(luil::ligature_caret_coordinates(table, 11) == std::vector<std::int16_t> { 200, 400 });
    CHECK(luil::ligature_caret_coordinates(table, 12) == std::vector<std::int16_t> { -50 });
    CHECK(luil::ligature_caret_coordinates(table, 13).empty());
    const auto ranged { gdef_table({ { 20, { { 1, 111 } } }, { 21, { { 1, 222 } } }, { 22, { { 1, 333 } } } }, 2) };
    CHECK(luil::ligature_caret_coordinates(ranged, 21) == std::vector<std::int16_t> { 222 });
    CHECK(luil::ligature_caret_coordinates(ranged, 22) == std::vector<std::int16_t> { 333 });
    CHECK(luil::ligature_caret_coordinates(ranged, 23).empty());
    // Format 2(윤곽 점)와 device가 붙은 Format 3은 균등 분할로 돌아간다.
    CHECK(luil::ligature_caret_coordinates(gdef_table({ { 5, { { 2, 7 } } } }), 5).empty());
    CHECK(luil::ligature_caret_coordinates(gdef_table({ { 5, { { 3, 100, 40 } } } }), 5).empty());
    // 손상: 잘린 표의 모든 길이에서 넘겨 읽지 않는다.
    for (std::size_t length { 0 }; length < table.size(); ++length)
    {
        const std::span<const std::uint8_t> cut { table.data(), length };
        const auto result { luil::ligature_caret_coordinates(cut, 11) };
        CHECK((result.empty() || result == std::vector<std::int16_t> { 200, 400 }));
    }
    // 버전 2.0이나 offset 0은 읽지 않는다.
    auto v2 { table };
    v2[1] = 2;
    v2[0] = 0;
    put(v2, 0, 2);
    CHECK(luil::ligature_caret_coordinates(v2, 10).empty());
    auto no_list { table };
    put(no_list, 8, 0);
    CHECK(luil::ligature_caret_coordinates(no_list, 10).empty());
    // 큰 count가 표 길이를 넘으면 거절한다.
    auto huge { table };
    put(huge, 12 + 2, 0xffff);
    CHECK(luil::ligature_caret_coordinates(huge, 12).size() <= 1);
}

TEST_CASE("lig_caret: a real font's GDEF moves the inner caret of 'ff' to its table position", "[selected][ligature][native]")
{
    if (luil::ui_text_shaping_enabled() == false)
        SKIP("text shaping is off in this configuration");
    struct restore_fonts
    {
        std::u8string ui {};
        std::u8string code {};
        ~restore_fonts() { luil::set_configured_fonts(ui, code); }
    } restore { luil::configured_ui_family(), luil::configured_code_family() };
    luil::set_configured_fonts(u8"Sans Serif Collection", {});
    if (luil::configured_ui_family() != u8"Sans Serif Collection")
        SKIP("Sans Serif Collection is not installed");
    // SansSerifCollection.ttf: 'ff' → glyph 1653, advance 688, caret 301 (0.4375). 'ffi' → 946, carets 315·631 (1/3·2/3).
    struct expected_case
    {
        std::u8string text;
        std::vector<float> fractions;
    };
    for (const auto& item : { expected_case { u8"ff", { 301.0f / 688.0f } }, expected_case { u8"ffi", { 315.0f / 946.0f, 631.0f / 946.0f } }, expected_case { u8"fl", { 301.0f / 602.0f } } })
    {
        const auto layout { luil::make_text_layout({ .text = item.text, .font_size = 48.0f }) };
        REQUIRE(layout.valid());
        INFO("text " << s8(item.text) << " backend " << static_cast<int>(layout.backend()) << " clusters " << layout.clusters().size());
        CHECK(layout.backend() == luil::text_layout_backend::harfbuzz);
        REQUIRE(layout.clusters().size() == 1);
        const auto cluster { layout.clusters()[0] };
        const auto stops { layout.caret_stops() };
        REQUIRE(stops.size() == item.text.size() + 1);
        CHECK_FALSE(layout.approximate_ligature_carets());
        for (std::size_t index { 0 }; index < item.fractions.size(); ++index)
        {
            const float fraction { (stops[index + 1].x - cluster.left) / (cluster.right - cluster.left) };
            INFO("stop " << index + 1 << " x " << stops[index + 1].x << " fraction " << fraction);
            CHECK(fraction == Catch::Approx(item.fractions[index]).margin(0.01));
        }
        // caret 왕복: 각 경계의 x로 hit test하면 그 경계가 나온다. 선택 사각형은 그 x에서 끊긴다.
        for (std::size_t at { 0 }; at <= item.text.size(); ++at)
        {
            const auto rect { layout.caret_rect(at) };
            const auto hit { layout.hit_test({ rect.x + 0.01f, rect.y + rect.height / 2.0f }) };
            INFO("offset " << at << " caret x " << rect.x << " hit " << hit.offset);
            CHECK(hit.offset == at);
        }
        const auto selection { layout.selection_rects(0, 1) };
        REQUIRE(selection.empty() == false);
        CHECK(selection.front().x + selection.front().width == Catch::Approx(stops[1].x).margin(0.5));
    }
    // 합자가 없는 글은 근사 표시가 없다.
    CHECK_FALSE(luil::make_text_layout({ .text = u8"abc", .font_size = 48.0f }).approximate_ligature_carets());
}

TEST_CASE("lig_caret: a ligature in the middle of a longer line keeps its caret offsets aligned", "[selected][ligature][native]")
{
    if (luil::ui_text_shaping_enabled() == false)
        SKIP("text shaping is off in this configuration");
    struct restore_fonts
    {
        std::u8string ui {};
        std::u8string code {};
        ~restore_fonts() { luil::set_configured_fonts(ui, code); }
    } restore { luil::configured_ui_family(), luil::configured_code_family() };
    luil::set_configured_fonts(u8"Sans Serif Collection", {});
    if (luil::configured_ui_family() != u8"Sans Serif Collection")
        SKIP("Sans Serif Collection is not installed");
    // 앞에 글이 있고(오프셋 이동), 두 줄로 접히고(줄 시작 이동), 한글이 섞인다.
    const std::u8string text { u8"한글 office ff staff\noffer ff" };
    for (const bool wrap : { false, true })
    {
        const auto layout { luil::make_text_layout({ .text = text, .font_size = 40.0f, .width = wrap ? 260.0f : 0.0f, .wrap = wrap }) };
        REQUIRE(layout.valid());
        const auto stops { layout.caret_stops() };
        // 모든 'ff' 합자 안쪽 경계: 그 cluster 폭의 0.4375 지점이어야 한다.
        int found { 0 };
        for (const auto& cluster : layout.clusters())
        {
            if (cluster.end - cluster.begin != 2 || text.substr(cluster.begin, 2) != u8"ff")
                continue;
            const auto inner { std::ranges::find_if(stops, [&](const auto& s) { return s.offset == cluster.begin + 1; }) };
            REQUIRE(inner != stops.end());
            const float fraction { (inner->x - cluster.left) / (cluster.right - cluster.left) };
            INFO("wrap " << wrap << " cluster at " << cluster.begin << " line " << cluster.line << " fraction " << fraction);
            CHECK(fraction == Catch::Approx(301.0f / 688.0f).margin(0.01));
            ++found;
        }
        INFO("wrap " << wrap);
        CHECK(found >= 3);
    }
}

// =====================================================================
// WebSocket (Windows WinHTTP) — LUIL_WS_PORT에 시험 서버가 있어야 한다
// =====================================================================

TEST_CASE("websocket: create refuses bad URLs and configurations", "[selected][websocket]")
{
    std::u8string error {};
    const auto refused = [&](std::u8string url, luil::net::websocket_client_config config = {}) {
        error.clear();
        auto client { luil::net::websocket_client::create({ .url = std::move(url) }, std::move(config), error) };
        return client == nullptr && error.empty() == false;
    };
    CHECK(refused(u8"http://example.com/"));
    CHECK(refused(u8"ws://user:pass@example.com/"));
    CHECK(refused(u8"ws://example.com/a b"));
    CHECK(refused(u8"ws://example.com/한"));
    CHECK(refused(u8"ws://example.com/#frag"));
    CHECK(refused(u8"ws://example.com/", { .max_message_bytes = 0 }));
    CHECK(refused(u8"ws://example.com/", { .max_message_bytes = 65u * 1024u * 1024u }));
    CHECK(refused(u8"ws://example.com/", { .max_pending_messages = 0 }));
    // 쿼리 안의 @는 자격 증명이 아니다.
    luil::net::websocket_client_config config {};
    auto ok { luil::net::websocket_client::create({ .url = u8"ws://127.0.0.1:1/?mail=a@b" }, std::move(config), error) };
    CHECK(ok != nullptr);
    if (ok != nullptr)
        ok->stop();
}

TEST_CASE("websocket: echo of text, binary, empty, Korean, and large messages", "[selected][websocket][.live]")
{
    if (env("LUIL_WS_PORT").has_value() == false)
        SKIP("LUIL_WS_PORT is not set");
    ws_log log {};
    auto client { connect(log, "/echo") };
    REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened) || has_kind(e, luil::net::websocket_event_kind::closed); }));
    REQUIRE(has_kind(log.snapshot(), luil::net::websocket_event_kind::opened));
    REQUIRE(client->send_text(u8"hello"));
    REQUIRE(client->send_text(u8""));
    REQUIRE(client->send_text(u8"한글 메시지 ✓"));
    const std::vector<std::uint8_t> binary { 0, 1, 2, 0xff, 0x80 };
    REQUIRE(client->send_binary(binary));
    REQUIRE(client->send_binary({}));
    std::vector<std::uint8_t> big(200000);
    for (std::size_t i { 0 }; i < big.size(); ++i)
        big[i] = static_cast<std::uint8_t>(i * 7);
    REQUIRE(client->send_binary(big));
    REQUIRE(log.wait([](const auto& e) { return count_kind(e, luil::net::websocket_event_kind::text) + count_kind(e, luil::net::websocket_event_kind::binary) >= 6; }, 8000ms));
    const auto events { log.snapshot() };
    std::vector<luil::net::websocket_event> data {};
    for (const auto& e : events)
        if (e.kind == luil::net::websocket_event_kind::text || e.kind == luil::net::websocket_event_kind::binary)
            data.push_back(e);
    REQUIRE(data.size() == 6);
    CHECK(text_of(data[0]) == "hello");
    CHECK(data[1].kind == luil::net::websocket_event_kind::text);
    CHECK(text_of(data[1]).empty());
    CHECK(text_of(data[2]) == std::string { reinterpret_cast<const char*>(u8"한글 메시지 ✓") });
    CHECK(data[3].kind == luil::net::websocket_event_kind::binary);
    CHECK(*data[3].data == binary);
    CHECK(data[4].kind == luil::net::websocket_event_kind::binary);
    CHECK(data[4].data->empty());
    CHECK(*data[5].data == big);
    // sequence는 1부터 늘기만 한다.
    for (std::size_t i { 1 }; i < events.size(); ++i)
        CHECK(events[i].sequence == events[i - 1].sequence + 1);
    // 정상 닫기
    REQUIRE(client->close(1000, u8"done"));
    CHECK_FALSE(client->send_text(u8"after close"));
    REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::closed); }, 5000ms));
    const auto closed { log.snapshot().back() };
    CHECK(closed.kind == luil::net::websocket_event_kind::closed);
    CHECK(closed.close_code == 1000);
    CHECK(closed.error.empty());
    client->stop();
    client->stop();
}

TEST_CASE("websocket: fragments with an interleaved ping are reassembled", "[selected][websocket][.live]")
{
    if (env("LUIL_WS_PORT").has_value() == false)
        SKIP("LUIL_WS_PORT is not set");
    ws_log log {};
    auto client { connect(log, "/fragment") };
    REQUIRE(log.wait([](const auto& e) { return count_kind(e, luil::net::websocket_event_kind::text) + count_kind(e, luil::net::websocket_event_kind::binary) >= 4 || has_kind(e, luil::net::websocket_event_kind::closed); }));
    const auto events { log.snapshot() };
    std::vector<luil::net::websocket_event> data {};
    for (const auto& e : events)
        if (e.kind == luil::net::websocket_event_kind::text || e.kind == luil::net::websocket_event_kind::binary)
            data.push_back(e);
    REQUIRE(data.size() == 4);
    CHECK(text_of(data[0]) == "hello world");
    CHECK(data[1].kind == luil::net::websocket_event_kind::text);
    CHECK(data[1].data->empty());
    CHECK(data[2].kind == luil::net::websocket_event_kind::binary);
    CHECK(data[2].data->empty());
    CHECK(text_of(data[3]) == std::string { reinterpret_cast<const char*>(u8"한글 ✓") });
    client->stop();
}

TEST_CASE("websocket: protocol errors, limits, and remote endings close with the right code", "[selected][websocket][.live]")
{
    if (env("LUIL_WS_PORT").has_value() == false)
        SKIP("LUIL_WS_PORT is not set");
    const auto run = [](const std::string& path, luil::net::websocket_client_config config = {}) {
        ws_log log {};
        auto client { connect(log, path, std::move(config)) };
        const bool ended { log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::closed); }, 8000ms) };
        client->stop();
        auto events { log.snapshot() };
        INFO("path " << path << " ended " << ended);
        REQUIRE(ended);
        REQUIRE(events.back().kind == luil::net::websocket_event_kind::closed);
        // closed는 하나뿐이다.
        REQUIRE(count_kind(events, luil::net::websocket_event_kind::closed) == 1);
        return events;
    };
    SECTION("잘못된 UTF-8 텍스트")
    {
        const auto events { run("/badutf8") };
        INFO("code " << events.back().close_code << " error " << s8(events.back().error.message));
        CHECK(count_kind(events, luil::net::websocket_event_kind::text) == 0);
        CHECK(events.back().error.empty() == false);
    }
    SECTION("서버가 mask를 켠 frame")
    {
        const auto events { run("/badframe") };
        INFO("code " << events.back().close_code << " error " << s8(events.back().error.message) << " native " << events.back().error.native_error);
        CHECK(count_kind(events, luil::net::websocket_event_kind::text) == 0);
        CHECK(events.back().error.empty() == false);
    }
    SECTION("메시지 상한을 넘는 바이너리는 받지 않고 닫는다")
    {
        luil::net::websocket_client_config config {};
        config.max_message_bytes = 50000;
        const auto events { run("/big?n=60000", std::move(config)) };
        CHECK(count_kind(events, luil::net::websocket_event_kind::binary) == 0);
        INFO("error kind " << static_cast<int>(events.back().error.kind) << " message " << s8(events.back().error.message));
        // 전송부가 받는 자리에서 상한을 먼저 보아 1009 닫기 frame 없이 끊는다(문서의 남은 일).
        CHECK(events.back().error.kind == luil::net::http_error_kind::body_too_large);
    }
    SECTION("상한 안쪽은 받는다")
    {
        ws_log log {};
        luil::net::websocket_client_config config {};
        config.max_message_bytes = 50000;
        auto client { connect(log, "/big?n=50000", std::move(config)) };
        REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::binary) || has_kind(e, luil::net::websocket_event_kind::closed); }));
        const auto events { log.snapshot() };
        REQUIRE(has_kind(events, luil::net::websocket_event_kind::binary));
        CHECK(std::ranges::find_if(events, [](const auto& e) { return e.kind == luil::net::websocket_event_kind::binary; })->data->size() == 50000);
        client->stop();
    }
    SECTION("서버가 먼저 닫으면 그 코드와 이유")
    {
        const auto events { run("/remoteclose") };
        CHECK(events.back().close_code == 4001);
        CHECK(events.back().close_reason == u8"bye");
        CHECK(events.back().error.empty());
    }
    SECTION("서버가 요청한 닫기 코드")
    {
        ws_log log {};
        auto client { connect(log, "/echo") };
        REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened); }));
        REQUIRE(client->send_text(u8"close:3005"));
        REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::closed); }));
        CHECK(log.snapshot().back().close_code == 3005);
        CHECK(log.snapshot().back().close_reason == u8"asked");
        client->stop();
    }
    SECTION("TCP가 그냥 끊기면 1006과 오류")
    {
        const auto events { run("/drop") };
        CHECK(events.back().close_code == 1006);
        CHECK(events.back().error.empty() == false);
    }
    SECTION("upgrade 거절")
    {
        const auto events { run("/reject") };
        CHECK_FALSE(has_kind(events, luil::net::websocket_event_kind::opened));
        CHECK(events.back().error.kind == luil::net::http_error_kind::cannot_connect);
    }
    SECTION("서버 ping에 응답하고 연결이 이어진다")
    {
        ws_log log {};
        auto client { connect(log, "/echo") };
        REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened); }));
        REQUIRE(client->send_text(u8"ping-me"));
        REQUIRE(client->send_text(u8"still here"));
        REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::text); }));
        const auto ping_events { log.snapshot() };
        CHECK(text_of(*std::ranges::find_if(ping_events, [](const auto& e) { return e.kind == luil::net::websocket_event_kind::text; })) == "still here");
        client->stop();
    }
}

TEST_CASE("websocket: an idle connection outlives connect_timeout", "[selected][websocket][.live]")
{
    if (env("LUIL_WS_PORT").has_value() == false)
        SKIP("LUIL_WS_PORT is not set");
    ws_log log {};
    luil::net::websocket_client_config config {};
    config.deliver = [&log](luil::net::websocket_event event) { log.push(std::move(event)); };
    std::u8string error {};
    auto client { luil::net::websocket_client::create({ .url = ws_url("/echo"), .connect_timeout = 1500ms }, std::move(config), error) };
    REQUIRE(client != nullptr);
    REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened) || has_kind(e, luil::net::websocket_event_kind::closed); }));
    REQUIRE(has_kind(log.snapshot(), luil::net::websocket_event_kind::opened));
    std::this_thread::sleep_for(4000ms);
    CHECK_FALSE(has_kind(log.snapshot(), luil::net::websocket_event_kind::closed));
    REQUIRE(client->send_text(u8"after idle"));
    REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::text) || has_kind(e, luil::net::websocket_event_kind::closed); }));
    CHECK(has_kind(log.snapshot(), luil::net::websocket_event_kind::text));
    client->stop();
}

TEST_CASE("websocket: close waits for the reply only up to close_timeout", "[selected][websocket][.live]")
{
    if (env("LUIL_WS_PORT").has_value() == false)
        SKIP("LUIL_WS_PORT is not set");
    ws_log log {};
    luil::net::websocket_client_config config {};
    config.close_timeout = 700ms;
    auto client { connect(log, "/silent", std::move(config)) };
    REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened); }));
    const auto start { std::chrono::steady_clock::now() };
    REQUIRE(client->close());
    REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::closed); }, 5000ms));
    const auto elapsed { std::chrono::steady_clock::now() - start };
    INFO("elapsed ms " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    CHECK(elapsed < 2500ms);
    client->stop();
}

TEST_CASE("websocket: stop during connect, during receive, and from many senders returns promptly with no later callbacks", "[selected][websocket][.live]")
{
    if (env("LUIL_WS_PORT").has_value() == false)
        SKIP("LUIL_WS_PORT is not set");
    SECTION("응답 없는 주소로 연결 중 stop")
    {
        ws_log log {};
        std::u8string error {};
        // 10.255.255.1은 라우팅되지 않아 SYN이 답 없이 걸린다.
        auto client { luil::net::websocket_client::create({ .url = u8"ws://10.255.255.1:9/", .connect_timeout = 10000ms }, { .deliver = [&](auto e) { log.push(std::move(e)); } }, error) };
        REQUIRE(client != nullptr);
        std::this_thread::sleep_for(200ms);
        const auto start { std::chrono::steady_clock::now() };
        client->stop();
        const auto elapsed { std::chrono::steady_clock::now() - start };
        INFO("stop took ms " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        CHECK(elapsed < 2000ms);
        const auto count { log.snapshot().size() };
        std::this_thread::sleep_for(300ms);
        CHECK(log.snapshot().size() == count);
        if (count > 0)
            CHECK(log.snapshot().back().error.kind == luil::net::http_error_kind::stopped);
    }
    SECTION("수신 대기 중 stop, 여러 thread가 보내는 중 stop")
    {
        for (int round { 0 }; round < 20; ++round)
        {
            ws_log log {};
            auto client { connect(log, "/echo") };
            REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened) || has_kind(e, luil::net::websocket_event_kind::closed); }));
            std::atomic<bool> go { true };
            std::vector<std::thread> senders {};
            for (int t { 0 }; t < 4; ++t)
                senders.emplace_back([&] {
                    while (go)
                        (void)client->send_text(u8"spam spam spam");
                });
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 + round * 3 });
            const auto start { std::chrono::steady_clock::now() };
            client->stop();
            const auto elapsed { std::chrono::steady_clock::now() - start };
            go = false;
            for (auto& s : senders)
                s.join();
            CHECK(elapsed < 2000ms);
            const auto count { log.snapshot().size() };
            std::this_thread::sleep_for(50ms);
            CHECK(log.snapshot().size() == count);
            CHECK(count_kind(log.snapshot(), luil::net::websocket_event_kind::closed) <= 1);
        }
    }
    SECTION("열리기 전 send는 거절하고, 대기 개수 상한을 지킨다")
    {
        ws_log log {};
        luil::net::websocket_client_config config {};
        config.max_pending_messages = 3;
        auto client { connect(log, "/echo", std::move(config)) };
        // create 직후에는 아직 열리지 않았을 수 있다.
        const bool early { client->send_text(u8"too early") };
        REQUIRE(log.wait([](const auto& e) { return has_kind(e, luil::net::websocket_event_kind::opened); }));
        int accepted { 0 };
        for (int i { 0 }; i < 1000; ++i)
            accepted += client->send_text(u8"x") ? 1 : 0;
        INFO("early accepted " << early << " accepted of 1000 " << accepted);
        CHECK(accepted >= 3);
        CHECK(accepted < 1000);
        CHECK_FALSE(client->close(1005));
        CHECK_FALSE(client->close(1000, std::u8string(124, u8'a')));
        client->stop();
    }
}

// =====================================================================
// 동영상·소리 (Windows MFPlay)
// =====================================================================

TEST_CASE("media: audio-only open, muted play, pause, seek, end, and stop without later callbacks", "[selected][media][.live]")
{
    const luil::win32::com_sta_scope com {};
    media_log log {};
    std::u8string error {};
    auto player { luil::win32::media_player::create({ .parent_window = nullptr, .deliver = [&](luil::win32::media_event event) { log.push(std::move(event)); } }, error) };
    REQUIRE(player != nullptr);
    // 소리를 내지 않는다.
    const auto source { media_path("LUIL_MEDIA_AUDIO", "C:\\Windows\\Media\\Windows Notify System Generic.wav") };
    REQUIRE(player->open(source, error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::ready) || log.saw(luil::win32::media_state::failed); }));
    REQUIRE(log.saw(luil::win32::media_state::ready));
    const auto ready { player->snapshot() };
    INFO("duration ms " << ready.duration.count());
    CHECK(ready.duration.count() > 0);
    CHECK(log.thread != std::this_thread::get_id());
    REQUIRE(player->volume(0.0f, true, error));
    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::playing); }));
    REQUIRE(player->pause(error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::paused); }));
    REQUIRE(player->seek(std::chrono::milliseconds { ready.duration.count() / 2 }, error));
    REQUIRE(pump_until([&] { return player->snapshot().position.count() >= ready.duration.count() / 2 - 50; }, 3000ms));

    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::ended); }, 10000ms));
    CHECK(log.snapshot().back().duration == ready.duration);
    CHECK_FALSE(player->seek(std::chrono::milliseconds { -1 }, error));
    CHECK_FALSE(player->volume(1.5f, false, error));
    CHECK_FALSE(player->volume(std::numeric_limits<float>::quiet_NaN(), false, error));
    REQUIRE(player->rewind(error));
    REQUIRE(pump_until([&] { return player->snapshot().position.count() == 0; }, 3000ms));
    player->stop();
    const auto count { log.snapshot().size() };
    pump_until([] { return false; }, 300ms);
    CHECK(log.snapshot().size() == count);
    player->stop();
    CHECK_FALSE(player->play(error));
    CHECK_FALSE(player->open(source, error));
}

TEST_CASE("media: a missing or non-media file reports failed with a native error", "[selected][media][.live]")
{
    const luil::win32::com_sta_scope com {};
    for (const auto* source : { u8"C:\\definitely\\missing\\file.mp4", u8"C:\\Windows\\win.ini" })
    {
        media_log log {};
        std::u8string error {};
        auto player { luil::win32::media_player::create({ .deliver = [&](luil::win32::media_event event) { log.push(std::move(event)); } }, error) };
        REQUIRE(player != nullptr);
        const bool opened { player->open(source, error) };
        INFO("source " << reinterpret_cast<const char*>(source) << " open returned " << opened);
        if (opened)
        {
            REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::failed) || log.saw(luil::win32::media_state::ready); }));
            CHECK(log.saw(luil::win32::media_state::failed));
            const auto events { log.snapshot() };
            CHECK(events.back().native_error != 0);
            CHECK(events.back().error.empty() == false);
        }
        player->stop();
    }
}

TEST_CASE("media: opening twice keeps only the latest source", "[selected][media][.live]")
{
    const luil::win32::com_sta_scope com {};
    media_log log {};
    std::u8string error {};
    auto player { luil::win32::media_player::create({ .deliver = [&](luil::win32::media_event event) { log.push(std::move(event)); } }, error) };
    REQUIRE(player != nullptr);
    const auto first { media_path("LUIL_MEDIA_VIDEO", "C:\\Windows\\SystemApps\\Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy\\media\\oobe-intro.mp4") };
    const auto second { media_path("LUIL_MEDIA_AUDIO", "C:\\Windows\\Media\\Windows Notify System Generic.wav") };
    REQUIRE(player->open(first, error));
    REQUIRE(player->open(second, error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::ready) || log.saw(luil::win32::media_state::failed); }));
    pump_until([] { return false; }, 1000ms);
    const auto events { log.snapshot() };
    CHECK(std::ranges::count_if(events, [](const auto& e) { return e.state == luil::win32::media_state::ready; }) == 1);
    // 둘째(소리)는 수 초 길이, 첫째(동영상)는 그와 다르다.
    const auto shot { player->snapshot() };
    INFO("ready duration ms " << shot.duration.count());
    CHECK(shot.duration.count() < 5000);
    player->stop();
}

TEST_CASE("media: a video child surface follows set_video_bounds and is destroyed by stop", "[selected][media][.live][video]")
{
    const luil::win32::com_sta_scope com {};
    WNDCLASSW window_class {};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"Luil.SelectedNodesTest.Parent";
    RegisterClassW(&window_class);
    // 보이지 않는 부모 창이다. 화면에 띄우지 않는다.
    const HWND parent { CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, window_class.lpszClassName, L"", WS_POPUP, -32000, -32000, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
    REQUIRE(parent != nullptr);
    media_log log {};
    std::u8string error {};
    auto player { luil::win32::media_player::create({ .parent_window = parent, .deliver = [&](luil::win32::media_event event) { log.push(std::move(event)); } }, error) };
    REQUIRE(player != nullptr);
    const HWND surface { FindWindowExW(parent, nullptr, L"Luil.Media.Surface", nullptr) };
    REQUIRE(surface != nullptr);
    CHECK((GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE) == 0);
    const auto source { media_path("LUIL_MEDIA_VIDEO", "C:\\Windows\\SystemApps\\Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy\\media\\oobe-intro.mp4") };
    REQUIRE(player->open(source, error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::ready) || log.saw(luil::win32::media_state::failed); }, 10000ms));
    REQUIRE(log.saw(luil::win32::media_state::ready));
    player->set_video_bounds({ 10.0f, 20.0f, 320.0f, 180.0f }, true);
    RECT rect {};
    GetWindowRect(surface, &rect);
    POINT origin { rect.left, rect.top };
    ScreenToClient(parent, &origin);
    CHECK(origin.x == 10);
    CHECK(origin.y == 20);
    CHECK(rect.right - rect.left == 320);
    CHECK(rect.bottom - rect.top == 180);
    CHECK((GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE) != 0);
    REQUIRE(player->volume(0.0f, true, error));
    REQUIRE(player->play(error));
    REQUIRE(pump_until([&] { return log.saw(luil::win32::media_state::playing); }));
    REQUIRE(pump_until([&] { return player->snapshot().position.count() > 300; }, 5000ms));
    player->set_video_bounds({ 0.0f, 0.0f, 0.0f, 0.0f }, false);
    CHECK((GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE) == 0);
    // 무한·NaN bounds는 무시한다.
    player->set_video_bounds({ std::numeric_limits<float>::infinity(), 0.0f, 10.0f, 10.0f }, true);
    CHECK((GetWindowLongW(surface, GWL_STYLE) & WS_VISIBLE) == 0);
    player->stop();
    CHECK(IsWindow(surface) == FALSE);
    DestroyWindow(parent);
}

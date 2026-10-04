#include "mobile_demo/sheets_page.h"
#include "mobile_demo/smb_page.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace {
    template<typename Message, typename Page>
    void deliver(Page& page, Message message)
    {
        REQUIRE(page.handle(luil::app_message { std::move(message) }));
    }

    [[nodiscard]] luil::ui_tree sheet_tree(mobile_demo::sheets_page& page, const float width, const float height)
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, width, height, 1.0f, {});
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, 1.0f);
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
} // namespace

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

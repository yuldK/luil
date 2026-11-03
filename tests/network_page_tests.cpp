#include "demo/network_page.h"

#include "update_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 데스크톱 데모 네트워크 페이지의 자동 업데이트 구역이다 (docs/auto-update-design.md의 확인 절).
// 단계를 실은 메시지가 상태 줄의 글(곧 UIA Name)과 단추의 잠김, 작업 표시줄 진행률로 옮겨지는지를 본다.

namespace {
    [[nodiscard]] luil::ui_tree page_tree(demo::network_page& page)
    {
        return luil::make_arranged_tree(page.build(900.0f, 900.0f, 1.0f), { 0.0f, 0.0f, 900.0f, 900.0f }, 1.0f);
    }

    [[nodiscard]] std::u8string name_of(const luil::ui_tree& tree, const std::u8string_view owner)
    {
        const luil::ui_element* const label { tree.find({ demo::kind_text, std::u8string { owner } }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] bool button_enabled(const luil::ui_tree& tree, const std::u8string_view owner)
    {
        const luil::ui_element* const button { tree.find({ demo::kind_network_target, std::u8string { owner } }) };
        REQUIRE(button != nullptr);
        return button->enabled();
    }

    void report(demo::network_page& page, luil::update_status status)
    {
        REQUIRE(page.handle(luil::app_message { demo::network_update_status_intent { std::move(status) } }));
    }

    [[nodiscard]] luil::update_status status_of(const luil::update_phase phase)
    {
        luil::update_status status {};
        status.phase = phase;
        status.offer = luil::update_offer { u8"1.0.1", {}, {}, 3u * 1024u * 1024u, {} };
        status.total = 3u * 1024u * 1024u;
        return status;
    }

    [[nodiscard]] std::string ascii(const std::u8string_view text)
    {
        return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
    }
}

TEST_CASE("the update section reads its phase as a label", "[demo][network][update]")
{
    demo::network_page page {};
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(name_of(tree, u8"network-update-status") == u8"업데이트 상태: 대기");
        // updater가 서기 전에는 누를 단추가 없다.
        CHECK(button_enabled(tree, u8"update-check") == false);
    }
    page.set_updater(true, {});
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(button_enabled(tree, u8"update-check"));
        CHECK(button_enabled(tree, u8"update-download") == false);
        CHECK(button_enabled(tree, u8"update-cancel") == false);
        CHECK(button_enabled(tree, u8"update-clear"));
        CHECK(page.update_taskbar_progress().has_value() == false);
    }

    report(page, status_of(luil::update_phase::checking));
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(name_of(tree, u8"network-update-status") == u8"업데이트 상태: 확인 중");
        CHECK(button_enabled(tree, u8"update-check") == false);
        CHECK(button_enabled(tree, u8"update-cancel"));
        CHECK(page.update_taskbar_progress()->state == luil::taskbar_progress_state::indeterminate);
    }

    report(page, status_of(luil::update_phase::available));
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(name_of(tree, u8"network-update-status") == u8"업데이트 상태: 새 판 1.0.1 · 3.0 MiB");
        CHECK(button_enabled(tree, u8"update-download"));
    }

    luil::update_status downloading { status_of(luil::update_phase::downloading) };
    downloading.received = 3u * 512u * 1024u;
    report(page, downloading);
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(name_of(tree, u8"network-update-status") == u8"업데이트 상태: 받는 중 1.5/3.0 MiB");
        const std::optional<luil::taskbar_progress> progress { page.update_taskbar_progress() };
        REQUIRE(progress.has_value());
        CHECK(progress->state == luil::taskbar_progress_state::normal);
        CHECK(progress->value == 0.5f);
    }

    report(page, status_of(luil::update_phase::verifying));
    CHECK(page.update_status_text() == u8"업데이트 상태: 검증 중");
    report(page, status_of(luil::update_phase::ready));
    CHECK(page.update_status_text() == u8"업데이트 상태: 준비됨");
    CHECK(page.update_taskbar_progress().has_value() == false);

    luil::update_status failed { status_of(luil::update_phase::failed) };
    failed.failure = luil::update_failure::hash_mismatch;
    failed.error = u8"The downloaded bytes do not match the manifest sha256.";
    report(page, failed);
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(name_of(tree, u8"network-update-status") == u8"업데이트 상태: 실패 (hash_mismatch)");
        CHECK(name_of(tree, u8"network-update-note") == u8"업데이트 진단: The downloaded bytes do not match the manifest sha256.");
        // 받다 실패한 판은 다시 받을 수 있다 (받던 파일을 잇는다).
        CHECK(button_enabled(tree, u8"update-download"));
        CHECK(page.update_taskbar_progress()->state == luil::taskbar_progress_state::error);
    }

    page.note_update_cleared(true, {});
    {
        const luil::ui_tree tree { page_tree(page) };
        CHECK(name_of(tree, u8"network-update-status") == u8"업데이트 상태: 대기");
        CHECK(button_enabled(tree, u8"update-download") == false);
    }
}

TEST_CASE("the update section's checkboxes steer the demo server", "[demo][network][update]")
{
    demo::network_page page {};
    const std::shared_ptr<const demo::network_update_serve_flags> flags { page.update_serve_flags() };
    const auto handler { demo::make_network_handler(flags) };

    luil::testing::loopback_request manifest {};
    manifest.method = "GET";
    manifest.target = std::string { demo::network_update_manifest_path };
    CHECK(handler(manifest).body == luil::testing::update_fixture_bytes(luil::testing::update_fixture_envelope(true)));
    REQUIRE(page.handle(luil::app_message { demo::network_update_flag_intent { demo::network_update_flag::break_signature, true } }));
    CHECK(handler(manifest).body == luil::testing::update_fixture_bytes(luil::testing::update_fixture_broken_signature_envelope()));

    luil::testing::loopback_request asset {};
    asset.method = "GET";
    asset.target = "/update/" + ascii(luil::testing::update_fixture_asset_name);
    asset.headers.push_back({ "range", "bytes=524288-1048575" });
    const luil::testing::loopback_response ranged { handler(asset) };
    CHECK(ranged.status == 206);
    CHECK(ranged.body.size() == 512u * 1024u);
    const std::vector<std::uint8_t> whole { luil::testing::update_fixture_asset() };
    CHECK(std::vector<std::uint8_t> { whole.begin() + 524288, whole.begin() + 1048576 } == ranged.body);

    REQUIRE(page.handle(luil::app_message { demo::network_update_flag_intent { demo::network_update_flag::ignore_range, true } }));
    const luil::testing::loopback_response ignored { handler(asset) };
    CHECK(ignored.status == 200);
    CHECK(ignored.body.size() == whole.size());

    REQUIRE(page.handle(luil::app_message { demo::network_update_flag_intent { demo::network_update_flag::tamper_asset, true } }));
    CHECK(handler(asset).body != whole);
    {
        // 확인란은 서버가 읽는 값을 그대로 그린다.
        const luil::ui_tree tree { page_tree(page) };
        const luil::ui_element* const tamper { tree.find({ luil::ui_element_kind::check, u8"update-tamper" }) };
        REQUIRE(tamper != nullptr);
        CHECK(tamper->accessibility().name == u8"변조된 파일 내기");
    }
}

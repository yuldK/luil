#include "host/tray_sync.h"

#include "luil/app/app_host.h"
#include "luil/ui/image_element.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace {
    using luil::tray_appearance;
    using luil::tray_call;
    using luil::tray_call_kind;
    using luil::tray_call_outcome;
    using luil::tray_icon;
    using luil::tray_sync;
    using luil::tray_sync_state;

    // 1x1 불투명 이미지다. 만들 때마다 새 몸통이라 `backing()`이 서로 다르다.
    [[nodiscard]] luil::ui_image make_dot()
    {
        constexpr std::array<std::uint8_t, 4> pixel { 255, 255, 255, 255 };
        return luil::make_rgba_image(1, 1, pixel);
    }

    [[nodiscard]] tray_icon make_icon(const luil::ui_image& image, std::u8string tooltip = u8"luil tray")
    {
        tray_icon icon {};
        icon.icon = image;
        icon.tooltip = std::move(tooltip);
        return icon;
    }

    // 호출 하나를 "종류[그림][툴팁]" 글로 줄인다. 없으면 "none"이다.
    [[nodiscard]] std::string describe(const std::optional<tray_call>& call)
    {
        if (call.has_value() == false)
            return "none";
        std::string text {};
        switch (call->kind)
        {
        case tray_call_kind::add:
            text = "add";
            break;
        case tray_call_kind::modify:
            text = "modify";
            break;
        case tray_call_kind::remove:
            text = "remove";
            break;
        }
        if (call->icon)
            text += "+icon";
        if (call->tooltip)
            text += "+tip";
        return text;
    }

    // 더하기를 성공시킨 sync다.
    [[nodiscard]] tray_sync shown_sync(const tray_icon& icon, const tray_appearance& appearance = {})
    {
        tray_sync sync {};
        const std::optional<tray_call> add { sync.update(icon, appearance) };
        REQUIRE(describe(add) == "add+icon+tip");
        REQUIRE(sync.completed(*add, tray_call_outcome::succeeded).has_value() == false);
        REQUIRE(sync.state() == tray_sync_state::shown);
        return sync;
    }
}

TEST_CASE("No tray icon asks the shell for nothing", "[tray][sync]")
{
    tray_sync sync {};
    REQUIRE(describe(sync.update(std::nullopt, {})) == "none");
    REQUIRE(sync.state() == tray_sync_state::off);
    REQUIRE(sync.added() == false);
}

TEST_CASE("The first tray icon is added with its picture and tooltip", "[tray][sync]")
{
    const luil::ui_image dot { make_dot() };
    tray_sync sync {};
    const std::optional<tray_call> add { sync.update(make_icon(dot, u8"hello"), tray_appearance { .icon_size = 24 }) };
    REQUIRE(describe(add) == "add+icon+tip");
    REQUIRE(add->image.backing() == dot.backing());
    REQUIRE(add->icon_size == 24);
    REQUIRE(add->text == u8"hello");
    REQUIRE(sync.completed(*add, tray_call_outcome::succeeded).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::shown);
    REQUIRE(sync.added());
}

TEST_CASE("The same tray request asks for nothing again", "[tray][sync]")
{
    const luil::ui_image dot { make_dot() };
    tray_sync sync { shown_sync(make_icon(dot)) };
    // 같은 그림의 복사본은 같은 몸통이다.
    const luil::ui_image copy { dot };
    REQUIRE(describe(sync.update(make_icon(copy), {})) == "none");
    REQUIRE(sync.state() == tray_sync_state::shown);
}

TEST_CASE("Only the changed tray field is modified", "[tray][sync]")
{
    const luil::ui_image dot { make_dot() };
    tray_sync sync { shown_sync(make_icon(dot)) };

    std::optional<tray_call> call { sync.update(make_icon(dot, u8"new tooltip"), {}) };
    REQUIRE(describe(call) == "modify+tip");
    REQUIRE(call->text == u8"new tooltip");
    REQUIRE(sync.completed(*call, tray_call_outcome::succeeded).has_value() == false);

    const luil::ui_image other { make_dot() };
    call = sync.update(make_icon(other, u8"new tooltip"), {});
    REQUIRE(describe(call) == "modify+icon");
    REQUIRE(call->image.backing() == other.backing());
    REQUIRE(sync.completed(*call, tray_call_outcome::succeeded).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::shown);
}

TEST_CASE("A new icon size rebuilds the tray picture", "[tray][sync]")
{
    const luil::ui_image dot { make_dot() };
    tray_sync sync { shown_sync(make_icon(dot), tray_appearance { .icon_size = 16 }) };
    const std::optional<tray_call> call { sync.update(make_icon(dot), tray_appearance { .icon_size = 32 }) };
    REQUIRE(describe(call) == "modify+icon");
    REQUIRE(call->icon_size == 32);
}

TEST_CASE("The tray picture follows the taskbar theme and high contrast", "[tray][sync]")
{
    const luil::ui_image dark { make_dot() };
    const luil::ui_image light { make_dot() };
    const luil::ui_image contrast { make_dot() };

    SECTION("The choice falls back in order")
    {
        tray_icon icon { make_icon(dark) };
        icon.light_icon = light;
        icon.high_contrast_icon = contrast;
        REQUIRE(luil::choose_tray_image(icon, false, false).backing() == dark.backing());
        REQUIRE(luil::choose_tray_image(icon, false, true).backing() == light.backing());
        REQUIRE(luil::choose_tray_image(icon, true, false).backing() == contrast.backing());
        REQUIRE(luil::choose_tray_image(icon, true, true).backing() == contrast.backing());
        icon.high_contrast_icon = {};
        REQUIRE(luil::choose_tray_image(icon, true, true).backing() == light.backing());
        REQUIRE(luil::choose_tray_image(icon, true, false).backing() == dark.backing());
        icon.light_icon = {};
        REQUIRE(luil::choose_tray_image(icon, true, true).backing() == dark.backing());
        // 셋이 다 비면 빈 그림이다. 플랫폼이 창 아이콘으로 채운다.
        REQUIRE(luil::choose_tray_image(tray_icon {}, true, true).valid() == false);
    }

    SECTION("A theme change that picks another picture modifies it")
    {
        tray_icon icon { make_icon(dark) };
        icon.light_icon = light;
        tray_sync sync { shown_sync(icon) };
        const std::optional<tray_call> call { sync.update(icon, tray_appearance { .light_taskbar = true }) };
        REQUIRE(describe(call) == "modify+icon");
        REQUIRE(call->image.backing() == light.backing());
    }

    SECTION("A theme change that falls back to the same picture asks for nothing")
    {
        const tray_icon icon { make_icon(dark) };
        tray_sync sync { shown_sync(icon) };
        REQUIRE(describe(sync.update(icon, tray_appearance { .light_taskbar = true })) == "none");
        REQUIRE(describe(sync.update(icon, tray_appearance { .high_contrast = true, .light_taskbar = true })) == "none");
    }
}

TEST_CASE("Removing the tray request deletes the icon", "[tray][sync]")
{
    tray_sync sync { shown_sync(make_icon(make_dot())) };
    const std::optional<tray_call> call { sync.update(std::nullopt, {}) };
    REQUIRE(describe(call) == "remove");
    REQUIRE(sync.state() == tray_sync_state::off);
    REQUIRE(sync.added() == false);
    REQUIRE(sync.completed(*call, tray_call_outcome::succeeded).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::off);
    REQUIRE(describe(sync.update(std::nullopt, {})) == "none");
}

TEST_CASE("A failed add waits for the shell to restart", "[tray][sync]")
{
    const tray_icon icon { make_icon(make_dot()) };
    tray_sync sync {};
    const std::optional<tray_call> add { sync.update(icon, {}) };
    REQUIRE(sync.completed(*add, tray_call_outcome::failed).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::waiting);
    REQUIRE(sync.added() == false);

    // 탐색기가 없을 때는 요청이 바뀌어도 다시 더하지 않는다. 방송을 기다린다.
    REQUIRE(describe(sync.update(make_icon(make_dot(), u8"changed"), {})) == "none");
    REQUIRE(sync.state() == tray_sync_state::waiting);

    sync.shell_restarted();
    const std::optional<tray_call> again { sync.update(icon, {}) };
    REQUIRE(describe(again) == "add+icon+tip");
    REQUIRE(sync.completed(*again, tray_call_outcome::succeeded).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::shown);
}

TEST_CASE("A timed out add probes with a modify", "[tray][sync]")
{
    const luil::ui_image dot { make_dot() };
    const tray_icon icon { make_icon(dot) };

    SECTION("The probe finds the icon")
    {
        tray_sync sync {};
        const std::optional<tray_call> add { sync.update(icon, {}) };
        const std::optional<tray_call> probe { sync.completed(*add, tray_call_outcome::timed_out) };
        REQUIRE(describe(probe) == "modify+icon+tip");
        REQUIRE(probe->image.backing() == dot.backing());
        REQUIRE(sync.completed(*probe, tray_call_outcome::succeeded).has_value() == false);
        REQUIRE(sync.state() == tray_sync_state::shown);
        REQUIRE(sync.added());
        REQUIRE(describe(sync.update(icon, {})) == "none");
    }

    SECTION("The probe fails and the next change adds again")
    {
        tray_sync sync {};
        const std::optional<tray_call> add { sync.update(icon, {}) };
        const std::optional<tray_call> probe { sync.completed(*add, tray_call_outcome::timed_out) };
        REQUIRE(sync.completed(*probe, tray_call_outcome::failed).has_value() == false);
        REQUIRE(sync.state() == tray_sync_state::waiting);
        // 같은 요청으로는 다시 하지 않는다.
        REQUIRE(describe(sync.update(icon, {})) == "none");
        const std::optional<tray_call> again { sync.update(make_icon(dot, u8"changed"), {}) };
        REQUIRE(describe(again) == "add+icon+tip");
        REQUIRE(sync.completed(*again, tray_call_outcome::succeeded).has_value() == false);
        REQUIRE(sync.state() == tray_sync_state::shown);
    }

    SECTION("An icon that may stand after a failed probe is still deleted")
    {
        tray_sync sync {};
        const std::optional<tray_call> add { sync.update(icon, {}) };
        const std::optional<tray_call> probe { sync.completed(*add, tray_call_outcome::timed_out) };
        REQUIRE(sync.completed(*probe, tray_call_outcome::failed).has_value() == false);
        REQUIRE(describe(sync.update(std::nullopt, {})) == "remove");
    }
}

TEST_CASE("A shell restart without a tray request asks for nothing", "[tray][sync]")
{
    tray_sync sync {};
    sync.shell_restarted();
    REQUIRE(describe(sync.update(std::nullopt, {})) == "none");

    // 섰던 아이콘도 탐색기가 다시 뜨면 처음부터 더한다. 여러 번 와도 같다.
    const tray_icon icon { make_icon(make_dot()) };
    tray_sync shown { shown_sync(icon) };
    shown.shell_restarted();
    shown.shell_restarted();
    REQUIRE(describe(shown.update(icon, {})) == "add+icon+tip");
}

TEST_CASE("A failed modify is reported once and not retried", "[tray][sync]")
{
    const luil::ui_image dot { make_dot() };
    tray_sync sync { shown_sync(make_icon(dot)) };
    const tray_icon changed { make_icon(dot, u8"changed") };
    const std::optional<tray_call> call { sync.update(changed, {}) };
    REQUIRE(describe(call) == "modify+tip");
    REQUIRE(sync.completed(*call, tray_call_outcome::failed).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::failed);
    REQUIRE(describe(sync.update(changed, {})) == "none");
    REQUIRE(sync.state() == tray_sync_state::failed);

    // 다음 변화는 다시 낸다. 성공하면 실패를 거둔다.
    const std::optional<tray_call> next { sync.update(make_icon(dot, u8"again"), {}) };
    REQUIRE(describe(next) == "modify+tip");
    REQUIRE(sync.completed(*next, tray_call_outcome::succeeded).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::shown);
}

TEST_CASE("A failed delete is reported", "[tray][sync]")
{
    tray_sync sync { shown_sync(make_icon(make_dot())) };
    const std::optional<tray_call> call { sync.update(std::nullopt, {}) };
    REQUIRE(sync.completed(*call, tray_call_outcome::failed).has_value() == false);
    REQUIRE(sync.state() == tray_sync_state::failed);
    REQUIRE(describe(sync.update(std::nullopt, {})) == "none");
    REQUIRE(sync.state() == tray_sync_state::failed);
}

TEST_CASE("A frame carries no tray icon by default", "[tray][app]")
{
    const luil::ui_frame frame {};
    REQUIRE(frame.tray.has_value() == false);
}

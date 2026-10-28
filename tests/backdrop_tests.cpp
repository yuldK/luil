#include "win32/backdrop.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>

namespace {
    using luil::window_backdrop;
    using luil::win32::backdrop_decision;
    using luil::win32::backdrop_environment;
    using luil::win32::backdrop_mechanism;
    using luil::win32::backdrop_status;
    using luil::win32::decide_backdrop;

    // 효과가 다 서는 Windows 11의 표준 창이다.
    [[nodiscard]] backdrop_environment standard_window()
    {
        return backdrop_environment { .system_backdrop_supported = true, .accent_supported = true };
    }

    [[nodiscard]] backdrop_environment layered_window()
    {
        backdrop_environment environment { standard_window() };
        environment.layered = true;
        return environment;
    }
} // namespace

TEST_CASE("A standard window raises the system backdrop for each effect", "[win32][backdrop]")
{
    REQUIRE(decide_backdrop(window_backdrop::mica, standard_window()) == backdrop_decision { backdrop_mechanism::system_mica, backdrop_status::applied });
    REQUIRE(decide_backdrop(window_backdrop::mica_alt, standard_window()) == backdrop_decision { backdrop_mechanism::system_mica_alt, backdrop_status::applied });
    REQUIRE(decide_backdrop(window_backdrop::acrylic, standard_window()) == backdrop_decision { backdrop_mechanism::system_acrylic, backdrop_status::applied });
    REQUIRE(decide_backdrop(window_backdrop::none, standard_window()) == backdrop_decision {});
}

TEST_CASE("A layered window raises acrylic through the accent path and refuses mica", "[win32][backdrop]")
{
    // 레이어드 창의 시스템 배경 효과는 단색 판이다 (실측). acrylic은 accent로, mica는 길이 없다.
    REQUIRE(decide_backdrop(window_backdrop::acrylic, layered_window()) == backdrop_decision { backdrop_mechanism::accent_acrylic, backdrop_status::applied });
    REQUIRE(decide_backdrop(window_backdrop::mica, layered_window()).status == backdrop_status::unsupported_presentation);
    REQUIRE(decide_backdrop(window_backdrop::mica_alt, layered_window()).active() == false);

    backdrop_environment no_accent { layered_window() };
    no_accent.accent_supported = false;
    REQUIRE(decide_backdrop(window_backdrop::acrylic, no_accent).status == backdrop_status::unsupported_system);
}

TEST_CASE("An older Windows without system backdrops reports it", "[win32][backdrop]")
{
    backdrop_environment older { standard_window() };
    older.system_backdrop_supported = false;
    REQUIRE(decide_backdrop(window_backdrop::acrylic, older) == backdrop_decision { backdrop_mechanism::none, backdrop_status::unsupported_system });
}

TEST_CASE("User settings turn the backdrop off with their own reason", "[win32][backdrop]")
{
    backdrop_environment transparency_off { standard_window() };
    transparency_off.transparency_enabled = false;
    REQUIRE(decide_backdrop(window_backdrop::mica, transparency_off) == backdrop_decision { backdrop_mechanism::none, backdrop_status::transparency_disabled });

    backdrop_environment contrast { standard_window() };
    contrast.high_contrast = true;
    REQUIRE(decide_backdrop(window_backdrop::mica, contrast).status == backdrop_status::high_contrast);

    backdrop_environment remote { standard_window() };
    remote.remote_session = true;
    REQUIRE(decide_backdrop(window_backdrop::mica, remote).status == backdrop_status::remote_session);
}

TEST_CASE("A reason the user cannot change is reported before one they can", "[win32][backdrop]")
{
    // 투명 효과를 켜도 레이어드 창의 mica는 서지 않는다. 그 안내를 먼저 해야 한다.
    backdrop_environment layered_off { layered_window() };
    layered_off.transparency_enabled = false;
    REQUIRE(decide_backdrop(window_backdrop::mica, layered_off).status == backdrop_status::unsupported_presentation);
    // 고칠 수 있는 까닭끼리는 고대비가 먼저다.
    backdrop_environment both { standard_window() };
    both.high_contrast = true;
    both.transparency_enabled = false;
    REQUIRE(decide_backdrop(window_backdrop::acrylic, both).status == backdrop_status::high_contrast);
}

namespace {
    using luil::win32::surface_backdrop;

    // 활성화되지 않는 표준 표면이다 (popup).
    [[nodiscard]] backdrop_environment never_active_surface()
    {
        backdrop_environment environment { standard_window() };
        environment.activates = false;
        return environment;
    }
} // namespace

TEST_CASE("A surface that never activates raises acrylic through the accent path and refuses mica", "[win32][backdrop]")
{
    // 활성화되지 않는 표면의 시스템 효과는 비활성 대체 색이다 (실측). 레이어드 창과 같은 길이다.
    REQUIRE(decide_backdrop(window_backdrop::acrylic, never_active_surface()) == backdrop_decision { backdrop_mechanism::accent_acrylic, backdrop_status::applied });
    REQUIRE(decide_backdrop(window_backdrop::mica, never_active_surface()) == backdrop_decision { backdrop_mechanism::none, backdrop_status::unsupported_presentation });
    REQUIRE(decide_backdrop(window_backdrop::mica_alt, never_active_surface()).status == backdrop_status::unsupported_presentation);
    REQUIRE(decide_backdrop(window_backdrop::none, never_active_surface()) == backdrop_decision {});

    // 시스템 배경 효과가 없는 Windows여도 accent 길은 선다.
    backdrop_environment older { never_active_surface() };
    older.system_backdrop_supported = false;
    REQUIRE(decide_backdrop(window_backdrop::acrylic, older).mechanism == backdrop_mechanism::accent_acrylic);
    backdrop_environment no_accent { never_active_surface() };
    no_accent.accent_supported = false;
    REQUIRE(decide_backdrop(window_backdrop::acrylic, no_accent).status == backdrop_status::unsupported_system);
}

TEST_CASE("Each presentation and activation picks its path and keeps the reason order", "[win32][backdrop]")
{
    struct expectation
    {
        bool layered { false };
        bool activates { true };
        window_backdrop requested { window_backdrop::none };
        backdrop_mechanism mechanism { backdrop_mechanism::none };
    };

    // 표준 창이 지금 비활성인 것은 환경에 들지 않는다 — 비활성 창의 단색은 OS의 비활성 모습이라 길이 그대로다.
    const expectation cases[] {
        { false, true, window_backdrop::mica, backdrop_mechanism::system_mica },
        { false, true, window_backdrop::acrylic, backdrop_mechanism::system_acrylic },
        { false, false, window_backdrop::mica, backdrop_mechanism::none },
        { false, false, window_backdrop::acrylic, backdrop_mechanism::accent_acrylic },
        { true, true, window_backdrop::mica, backdrop_mechanism::none },
        { true, true, window_backdrop::acrylic, backdrop_mechanism::accent_acrylic },
        { true, false, window_backdrop::mica, backdrop_mechanism::none },
        { true, false, window_backdrop::acrylic, backdrop_mechanism::accent_acrylic },
    };
    for (const expectation& item : cases)
    {
        backdrop_environment environment { standard_window() };
        environment.layered = item.layered;
        environment.activates = item.activates;
        const backdrop_decision decision { decide_backdrop(item.requested, environment) };
        REQUIRE(decision.mechanism == item.mechanism);
        REQUIRE(decision.status == (item.mechanism == backdrop_mechanism::none ? backdrop_status::unsupported_presentation : backdrop_status::applied));

        // 어느 표면이든 사용자가 바꿀 수 있는 까닭은 고칠 수 없는 까닭 뒤다. 길이 있는 조합에서만 그 까닭이 나온다.
        for (const auto& [setting, reason] : {
                 std::pair { &backdrop_environment::high_contrast, backdrop_status::high_contrast },
                 std::pair { &backdrop_environment::remote_session, backdrop_status::remote_session },
             })
        {
            backdrop_environment changed { environment };
            changed.*setting = true;
            REQUIRE(decide_backdrop(item.requested, changed).status == (item.mechanism == backdrop_mechanism::none ? backdrop_status::unsupported_presentation : reason));
        }
        backdrop_environment transparency_off { environment };
        transparency_off.transparency_enabled = false;
        const backdrop_status off_status { decide_backdrop(item.requested, transparency_off).status };
        REQUIRE(off_status == (item.mechanism == backdrop_mechanism::none ? backdrop_status::unsupported_presentation : backdrop_status::transparency_disabled));
    }
}

TEST_CASE("A surface reapplies only when the path changes and reports only when the status changes", "[win32][backdrop]")
{
    surface_backdrop main {};
    // 주 창은 처음 상태도 알린다. 효과가 없으니 DWM에는 아무것도 말하지 않는다.
    surface_backdrop::update first { main.refresh(window_backdrop::none, standard_window()) };
    REQUIRE(first.apply.has_value() == false);
    REQUIRE(first.report.has_value());
    REQUIRE(first.report->status == backdrop_status::off);
    REQUIRE(main.refresh(window_backdrop::none, standard_window()).report.has_value() == false);

    // 켬: 새 길을 세우고 알린다.
    surface_backdrop::update on { main.refresh(window_backdrop::acrylic, standard_window()) };
    REQUIRE(on.apply == backdrop_mechanism::system_acrylic);
    REQUIRE(on.previous == backdrop_mechanism::none);
    REQUIRE(on.report->status == backdrop_status::applied);
    REQUIRE(main.decision().active());

    // 같은 길이면 다시 걸지 않는다. 바뀜: 앞의 길을 거두고 새 길을 세운다.
    REQUIRE(main.refresh(window_backdrop::acrylic, standard_window()).apply.has_value() == false);
    surface_backdrop::update changed { main.refresh(window_backdrop::mica, standard_window()) };
    REQUIRE(changed.apply == backdrop_mechanism::system_mica);
    REQUIRE(changed.previous == backdrop_mechanism::system_acrylic);

    // OS 사정으로 꺼짐: 길을 거두고 까닭을 알린다. 사정이 풀리면 다시 선다.
    backdrop_environment transparency_off { standard_window() };
    transparency_off.transparency_enabled = false;
    surface_backdrop::update dropped { main.refresh(window_backdrop::mica, transparency_off) };
    REQUIRE(dropped.apply == backdrop_mechanism::none);
    REQUIRE(dropped.report->status == backdrop_status::transparency_disabled);
    REQUIRE(main.refresh(window_backdrop::mica, standard_window()).apply == backdrop_mechanism::system_mica);

    // 끔: 거두고 알린다.
    surface_backdrop::update off { main.refresh(window_backdrop::none, standard_window()) };
    REQUIRE(off.apply == backdrop_mechanism::none);
    REQUIRE(off.previous == backdrop_mechanism::system_mica);
    REQUIRE(off.report->status == backdrop_status::off);
}

TEST_CASE("A secondary window that never chose a backdrop stays quiet", "[win32][backdrop]")
{
    surface_backdrop secondary { true };
    REQUIRE(secondary.refresh(window_backdrop::none, standard_window()).report.has_value() == false);
    // 서지 않는 효과를 고르면 까닭을 알린다. 서지 않았으니 다시 걸 것도 없다.
    backdrop_environment contrast { standard_window() };
    contrast.high_contrast = true;
    surface_backdrop::update refused { secondary.refresh(window_backdrop::acrylic, contrast) };
    REQUIRE(refused.apply.has_value() == false);
    REQUIRE(refused.report->status == backdrop_status::high_contrast);
    // 다시 끄면 그 사실도 알린다 — 앱이 마지막으로 받은 것이 "고대비"로 남지 않게.
    REQUIRE(secondary.refresh(window_backdrop::none, contrast).report->status == backdrop_status::off);
}

TEST_CASE("A popup switches its backdrop in place on the accent path", "[win32][backdrop]")
{
    surface_backdrop popup { true };
    REQUIRE(popup.refresh(window_backdrop::acrylic, never_active_surface()).apply == backdrop_mechanism::accent_acrylic);
    // mica로 바꾸면 길이 없어 거둔다. 창을 다시 만들 일은 없다 — 표시 방식이 그대로다.
    surface_backdrop::update mica { popup.refresh(window_backdrop::mica, never_active_surface()) };
    REQUIRE(mica.apply == backdrop_mechanism::none);
    REQUIRE(mica.previous == backdrop_mechanism::accent_acrylic);
    REQUIRE(popup.decision().status == backdrop_status::unsupported_presentation);
    REQUIRE(popup.refresh(window_backdrop::none, never_active_surface()).apply.has_value() == false);
}

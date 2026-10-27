#include "win32/backdrop.h"

#include <catch2/catch_test_macros.hpp>

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

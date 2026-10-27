#include "win32/backdrop.h"

namespace luil::win32 {
    namespace {
        [[nodiscard]] backdrop_mechanism system_mechanism_for(const window_backdrop requested) noexcept
        {
            switch (requested)
            {
            case window_backdrop::mica:
                return backdrop_mechanism::system_mica;
            case window_backdrop::mica_alt:
                return backdrop_mechanism::system_mica_alt;
            case window_backdrop::acrylic:
                return backdrop_mechanism::system_acrylic;
            default:
                return backdrop_mechanism::none;
            }
        }
    } // namespace

    backdrop_decision decide_backdrop(const window_backdrop requested, const backdrop_environment& environment) noexcept
    {
        if (requested == window_backdrop::none)
            return {};

        backdrop_mechanism mechanism { backdrop_mechanism::none };
        if (environment.layered)
        {
            // 레이어드 창에서 서는 것은 accent acrylic뿐이다. mica는 바탕화면을 깔 길이 없다.
            if (requested != window_backdrop::acrylic)
                return { backdrop_mechanism::none, backdrop_status::unsupported_presentation };
            if (environment.accent_supported == false)
                return { backdrop_mechanism::none, backdrop_status::unsupported_system };
            mechanism = backdrop_mechanism::accent_acrylic;
        }
        else
        {
            if (environment.system_backdrop_supported == false)
                return { backdrop_mechanism::none, backdrop_status::unsupported_system };
            mechanism = system_mechanism_for(requested);
        }

        if (environment.high_contrast)
            return { backdrop_mechanism::none, backdrop_status::high_contrast };
        if (environment.remote_session)
            return { backdrop_mechanism::none, backdrop_status::remote_session };
        if (environment.transparency_enabled == false)
            return { backdrop_mechanism::none, backdrop_status::transparency_disabled };
        return { mechanism, backdrop_status::applied };
    }
} // namespace luil::win32

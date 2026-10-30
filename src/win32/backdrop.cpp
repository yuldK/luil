#include "win32/backdrop.h"

#include <dwmapi.h>

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

        [[nodiscard]] bool system_mechanism(const backdrop_mechanism mechanism) noexcept
        {
            return mechanism == backdrop_mechanism::system_mica || mechanism == backdrop_mechanism::system_mica_alt || mechanism == backdrop_mechanism::system_acrylic;
        }

        // 비공개 `SetWindowCompositionAttribute`의 인자다.
        struct accent_policy
        {
            int state { 0 };
            int flags { 0 };
            DWORD gradient_color { 0 };
            int animation_id { 0 };
        };

        struct window_composition_attribute_data
        {
            int attribute { 0 };
            void* data { nullptr };
            SIZE_T size { 0 };
        };

        using set_window_composition_attribute_function = BOOL(WINAPI*)(HWND, window_composition_attribute_data*);

        [[nodiscard]] set_window_composition_attribute_function find_set_window_composition_attribute() noexcept
        {
            const HMODULE user32 { GetModuleHandleW(L"user32.dll") };
            if (user32 == nullptr)
                return nullptr;
            return reinterpret_cast<set_window_composition_attribute_function>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
        }

        [[nodiscard]] set_window_composition_attribute_function set_window_composition_attribute() noexcept
        {
            static const set_window_composition_attribute_function function { find_set_window_composition_attribute() };
            return function;
        }
    }

    backdrop_decision decide_backdrop(const window_backdrop requested, const backdrop_environment& environment) noexcept
    {
        if (requested == window_backdrop::none)
            return {};

        backdrop_mechanism mechanism { backdrop_mechanism::none };
        if (environment.layered || environment.activates == false)
        {
            // 레이어드 창의 시스템 효과는 단색 판이고, 활성화되지 않는 표면의 시스템 효과는 비활성 대체 색이다 (실측).
            // 그 둘에서 서는 것은 accent acrylic뿐이다. mica는 바탕화면을 깔 길이 없다.
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

    surface_backdrop::surface_backdrop(const bool quiet_off) noexcept
    {
        if (quiet_off)
            posted_ = backdrop_report { window_backdrop::none, backdrop_status::off };
    }

    surface_backdrop::update surface_backdrop::refresh(const window_backdrop requested, const backdrop_environment& environment) noexcept
    {
        const backdrop_decision decision { decide_backdrop(requested, environment) };
        update result {};
        result.previous = decision_.mechanism;
        if (decision.mechanism != decision_.mechanism)
            result.apply = decision.mechanism;
        decision_ = decision;

        const backdrop_report report { requested, decision.status };
        if (posted_ != report)
        {
            posted_ = report;
            result.report = report;
        }
        return result;
    }

    bool accent_backdrop_available() noexcept
    {
        return set_window_composition_attribute() != nullptr;
    }

    void apply_backdrop_mechanism(const HWND window, const backdrop_mechanism mechanism, const backdrop_mechanism previous, const bool system_supported, const bool dark) noexcept
    {
        if (window == nullptr)
            return;
        if (system_supported && (system_mechanism(mechanism) || system_mechanism(previous)))
        {
            DWM_SYSTEMBACKDROP_TYPE type { DWMSBT_NONE };
            if (mechanism == backdrop_mechanism::system_mica)
                type = DWMSBT_MAINWINDOW;
            else if (mechanism == backdrop_mechanism::system_mica_alt)
                type = DWMSBT_TABBEDWINDOW;
            else if (mechanism == backdrop_mechanism::system_acrylic)
                type = DWMSBT_TRANSIENTWINDOW;
            static_cast<void>(DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type)));
            // 시스템 효과를 건 창은 위쪽 캡션 높이만큼을 따로 칠한다 — 캡션이 없는 창(`WM_NCCALCSIZE` 0)인데도
            // 효과 위에 회색 띠가 선다 (실측). 캡션 색을 "없음"으로 두면 그 띠가 효과와 같아진다.
            const COLORREF caption { type == DWMSBT_NONE ? DWMWA_COLOR_DEFAULT : DWMWA_COLOR_NONE };
            static_cast<void>(DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption)));
        }
        const set_window_composition_attribute_function accent { set_window_composition_attribute() };
        if (accent != nullptr && (mechanism == backdrop_mechanism::accent_acrylic || previous == backdrop_mechanism::accent_acrylic))
        {
            // 4는 acrylic blur behind, 0은 끔이다. 19는 `WCA_ACCENT_POLICY`다.
            //  - 플래그 0이 표준 창의 시스템 acrylic과 가장 가까운 밝기다. 플래그 2(색 막을 쓴다)는 알파 0x10
            //    이하에서 거의 검게 되고, 0x40에서도 한 단 어둡다 (실측). 색 막은 명암에 맞춰 둔다.
            const DWORD tint { dark ? 0x40202020u : 0x40f3f3f3u };
            accent_policy policy { mechanism == backdrop_mechanism::accent_acrylic ? 4 : 0, 0, tint, 0 };
            window_composition_attribute_data data { 19, &policy, sizeof(policy) };
            static_cast<void>(accent(window, &data));
        }
    }
}

#include "luil/win32/renderer_policy.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Renderer strings map to fixed modes", "[renderer]")
{
    REQUIRE(luil::parse_renderer_mode(u8"auto") == luil::renderer_mode::automatic);
    REQUIRE(luil::parse_renderer_mode(u8"direct3d") == luil::renderer_mode::direct3d);
    REQUIRE(luil::parse_renderer_mode(u8"cpu") == luil::renderer_mode::cpu);
    REQUIRE_FALSE(luil::parse_renderer_mode(u8"vulkan").has_value());
}

TEST_CASE("Auto renderer tries Direct3D before CPU fallback", "[renderer]")
{
    const auto gpu = luil::select_renderer_backend(luil::renderer_mode::automatic, true);
    REQUIRE(gpu.status == luil::renderer_selection_status::selected);
    REQUIRE(gpu.backend == luil::renderer_backend::direct3d);
    REQUIRE_FALSE(gpu.used_fallback);

    const auto fallback = luil::select_renderer_backend(luil::renderer_mode::automatic, false);
    REQUIRE(fallback.status == luil::renderer_selection_status::selected);
    REQUIRE(fallback.backend == luil::renderer_backend::cpu);
    REQUIRE(fallback.used_fallback);
}

TEST_CASE("Explicit renderer selection preserves fallback policy", "[renderer]")
{
    const auto cpu = luil::select_renderer_backend(luil::renderer_mode::cpu, true);
    REQUIRE(cpu.status == luil::renderer_selection_status::selected);
    REQUIRE(cpu.backend == luil::renderer_backend::cpu);
    REQUIRE_FALSE(cpu.used_fallback);

    const auto unavailable = luil::select_renderer_backend(luil::renderer_mode::direct3d, false);
    REQUIRE(unavailable.status == luil::renderer_selection_status::unavailable);
    REQUIRE(unavailable.backend == luil::renderer_backend::direct3d);
    REQUIRE_FALSE(unavailable.used_fallback);
}

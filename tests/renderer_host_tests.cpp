#include "host/skia_renderer.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {
    // 렌더러의 생성·소멸·그리기를 차례로 적는 기록이다.
    // 물러설 때 옛 렌더러가 새 렌더러보다 먼저 사라지는지를 이 순서로 본다.
    struct renderer_log
    {
        std::vector<std::string> events {};
    };

    class fake_renderer final : public luil::skia_renderer
    {
    public:
        fake_renderer(renderer_log& log, const luil::renderer_backend backend, const bool fail_render, const bool fail_resize)
            : log_ { log }
            , backend_ { backend }
            , fail_render_ { fail_render }
            , fail_resize_ { fail_resize }
        {
            log_.events.push_back(name() + " created");
        }

        ~fake_renderer() override
        {
            log_.events.push_back(name() + " destroyed");
        }

        [[nodiscard]] luil::renderer_backend backend() const noexcept override
        {
            return backend_;
        }

        [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
        {
            static_cast<void>(width);
            static_cast<void>(height);
            if (fail_resize_)
            {
                error = u8"resize failed.";
                return false;
            }
            return true;
        }

        [[nodiscard]] bool render(const luil::frame_state& state, std::u8string& error) override
        {
            log_.events.push_back(name() + " rendered" + (state.used_fallback ? " after fallback" : ""));
            if (fail_render_)
            {
                error = u8"render failed.";
                return false;
            }
            return true;
        }

    private:
        [[nodiscard]] std::string name() const
        {
            return backend_ == luil::renderer_backend::cpu ? "cpu" : "gpu";
        }

        renderer_log& log_;
        luil::renderer_backend backend_;
        bool fail_render_;
        bool fail_resize_;
    };

    struct factory_plan
    {
        bool gpu_creates { true };
        bool gpu_render_fails { false };
        bool gpu_resize_fails { false };
        bool cpu_creates { true };
        int gpu_attempts { 0 };
    };

    [[nodiscard]] luil::renderer_factories make_factories(renderer_log& log, factory_plan& plan)
    {
        luil::renderer_factories factories {};
        factories.gpu_name = u8"Fake GPU";
        factories.create_gpu = [&log, &plan] {
            ++plan.gpu_attempts;
            luil::renderer_factory_result result {};
            if (plan.gpu_creates)
                result.renderer = std::make_unique<fake_renderer>(log, luil::renderer_backend::direct3d, plan.gpu_render_fails, plan.gpu_resize_fails);
            else
                result.error = u8"no gpu.";
            return result;
        };
        factories.create_cpu = [&log, &plan] {
            luil::renderer_factory_result result {};
            if (plan.cpu_creates)
                result.renderer = std::make_unique<fake_renderer>(log, luil::renderer_backend::cpu, false, false);
            else
                result.error = u8"no cpu.";
            return result;
        };
        return factories;
    }
} // namespace

TEST_CASE("Automatic rendering starts on the GPU when it can", "[renderer]")
{
    renderer_log log {};
    factory_plan plan {};
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, {}, make_factories(log, plan), error) };

    REQUIRE(host != nullptr);
    REQUIRE(host->backend() == luil::renderer_backend::direct3d);
    REQUIRE_FALSE(host->used_fallback());
}

TEST_CASE("Automatic rendering falls back to the CPU when the GPU cannot start", "[renderer]")
{
    renderer_log log {};
    factory_plan plan { .gpu_creates = false };
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, {}, make_factories(log, plan), error) };

    REQUIRE(host != nullptr);
    REQUIRE(host->backend() == luil::renderer_backend::cpu);
    REQUIRE(host->used_fallback());
}

TEST_CASE("GPU modes return the GPU error instead of falling back", "[renderer]")
{
    for (const luil::renderer_mode mode : { luil::renderer_mode::gpu, luil::renderer_mode::direct3d })
    {
        renderer_log log {};
        factory_plan plan { .gpu_creates = false };
        std::u8string error {};
        const auto host { luil::renderer_host::create(mode, {}, make_factories(log, plan), error) };

        REQUIRE(host == nullptr);
        REQUIRE(error == u8"no gpu.");
    }
}

TEST_CASE("CPU mode never asks for the GPU", "[renderer]")
{
    renderer_log log {};
    factory_plan plan {};
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::cpu, {}, make_factories(log, plan), error) };

    REQUIRE(host != nullptr);
    REQUIRE(host->backend() == luil::renderer_backend::cpu);
    REQUIRE_FALSE(host->used_fallback());
    REQUIRE(plan.gpu_attempts == 0);
}

TEST_CASE("A platform without a GPU factory falls back in automatic mode", "[renderer]")
{
    renderer_log log {};
    factory_plan plan {};
    luil::renderer_factories factories { make_factories(log, plan) };
    factories.create_gpu = nullptr;
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, {}, std::move(factories), error) };

    REQUIRE(host != nullptr);
    REQUIRE(host->backend() == luil::renderer_backend::cpu);
    REQUIRE(host->used_fallback());
}

TEST_CASE("A failed GPU frame releases the GPU renderer before the CPU draws", "[renderer]")
{
    renderer_log log {};
    factory_plan plan { .gpu_render_fails = true };
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, {}, make_factories(log, plan), error) };
    REQUIRE(host != nullptr);

    REQUIRE(host->render({}, error));
    REQUIRE(host->backend() == luil::renderer_backend::cpu);
    REQUIRE(host->used_fallback());
    const std::vector<std::string> expected { "gpu created", "gpu rendered", "cpu created", "gpu destroyed", "cpu rendered after fallback" };
    REQUIRE(log.events == expected);
}

TEST_CASE("A failed GPU resize falls back in automatic mode only", "[renderer]")
{
    {
        renderer_log log {};
        factory_plan plan { .gpu_resize_fails = true };
        std::u8string error {};
        const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, {}, make_factories(log, plan), error) };
        REQUIRE(host != nullptr);
        REQUIRE(host->resize(10, 10, error));
        REQUIRE(host->backend() == luil::renderer_backend::cpu);
    }
    {
        renderer_log log {};
        factory_plan plan { .gpu_resize_fails = true };
        std::u8string error {};
        const auto host { luil::renderer_host::create(luil::renderer_mode::gpu, {}, make_factories(log, plan), error) };
        REQUIRE(host != nullptr);
        REQUIRE_FALSE(host->resize(10, 10, error));
        REQUIRE(host->backend() == luil::renderer_backend::direct3d);
    }
}

TEST_CASE("Injected device loss names the platform GPU and falls back", "[renderer]")
{
    renderer_log log {};
    factory_plan plan {};
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, { .after_frames = 1 }, make_factories(log, plan), error) };
    REQUIRE(host != nullptr);

    REQUIRE(host->render({}, error));
    REQUIRE(host->backend() == luil::renderer_backend::direct3d);
    REQUIRE(host->render({}, error));
    REQUIRE(host->backend() == luil::renderer_backend::cpu);
    REQUIRE(error == u8"The smoke test injected a Fake GPU device loss.");
}

TEST_CASE("Losing both renderers reports both", "[renderer]")
{
    renderer_log log {};
    factory_plan plan { .gpu_creates = false, .cpu_creates = false };
    std::u8string error {};
    const auto host { luil::renderer_host::create(luil::renderer_mode::automatic, {}, make_factories(log, plan), error) };

    REQUIRE(host == nullptr);
    REQUIRE(error == u8"Both Fake GPU and CPU renderer initialization failed. no cpu.");
}

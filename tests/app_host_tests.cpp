#include "luil/win32/app_host.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    struct ping_intent
    {
        std::u8string name {};
    };

    struct close_intent
    {};

    // 처리 횟수를 세고 그 수를 frame에 싣는 최소 driver다.
    // 창 없는 조립 test라 tree는 싣지 않는다.
    class counting_driver final : public luil::win32::logic_driver
    {
    public:
        void start() override
        {
            started.store(true);
        }

        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
            {
                closed.store(true);
                return;
            }
            if (message.get<ping_intent>() != nullptr)
                pings.fetch_add(1);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            auto frame { std::make_shared<luil::win32::ui_frame>() };
            frame->window_placement_revision = static_cast<std::uint64_t>(pings.load());
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        void stop_workers() override
        {
            workers_stopped.store(true);
        }

        std::atomic<bool> started { false };
        std::atomic<bool> closed { false };
        std::atomic<bool> workers_stopped { false };
        std::atomic<int> pings { 0 };
    };

    // f5를 앱 메시지로 바꾸는 정책이다.
    // input pump → app inbox 배선을 검증한다.
    class key_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            return { luil::input_action { luil::app_message { ping_intent { u8"key" } } } };
        }
    };

    template<typename condition_type>
    [[nodiscard]] bool wait_until(const condition_type& condition)
    {
        const auto deadline { std::chrono::steady_clock::now() + 3s };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return condition();
    }
} // namespace

TEST_CASE("The app host runs the driver on the logic thread and publishes frames", "[win32][host]")
{
    counting_driver driver {};
    key_policy policy {};
    {
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };
        REQUIRE(wait_until([&] { return driver.started.load(); }));

        // 직접 게시한 앱 메시지가 driver에 닿는다.
        host.post_app_message(luil::app_message { ping_intent { u8"direct" } });
        REQUIRE(wait_until([&] { return driver.pings.load() >= 1; }));

        // raw input은 input thread의 pump가 정책을 거쳐 app inbox로 나른다.
        host.post_raw_input(luil::key_pressed_event { luil::key_code::f5 });
        REQUIRE(wait_until([&] { return driver.pings.load() >= 2; }));

        // frame은 처리 후 게시된다.
        // 최신 frame이 처리 횟수를 반영할 때까지 기다린다.
        REQUIRE(wait_until([&] {
            const std::shared_ptr<const luil::win32::ui_frame> frame { host.acquire_frame() };
            return frame != nullptr && frame->window_placement_revision >= 2u;
        }));

        host.shutdown();
        REQUIRE(driver.closed.load());
        REQUIRE(driver.workers_stopped.load());
    }
    // 소멸자는 shutdown 뒤에도 멱등으로 안전하다.
    REQUIRE(driver.pings.load() >= 2);
}

namespace {
    // 메시지 없이 시간만으로 상태가 바뀌는 driver다.
    // next_tick 예고 → tick 호출 → frame 재게시의 경로를 검증한다.
    class ticking_driver final : public luil::win32::logic_driver
    {
    public:
        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
                closed.store(true);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            auto frame { std::make_shared<luil::win32::ui_frame>() };
            frame->window_placement_revision = static_cast<std::uint64_t>(ticks.load());
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override
        {
            // 세 번까지만 예고한다.
            // 그 뒤에는 시간 경로가 완전히 잠잔다.
            if (ticks.load() >= 3)
                return std::nullopt;
            return std::chrono::steady_clock::now() + 5ms;
        }

        void tick(std::chrono::steady_clock::time_point) override
        {
            ticks.fetch_add(1);
        }

        std::atomic<bool> closed { false };
        std::atomic<int> ticks { 0 };
    };
} // namespace

TEST_CASE("The logic thread ticks on schedule without any messages", "[win32][host][tick]")
{
    ticking_driver driver {};
    {
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, nullptr };

        // 메시지를 하나도 보내지 않아도 예고한 시각마다 tick이 돌고
        // 새 frame이 게시된다.
        REQUIRE(wait_until([&] { return driver.ticks.load() >= 3; }));
        REQUIRE(wait_until([&] {
            const std::shared_ptr<const luil::win32::ui_frame> frame { host.acquire_frame() };
            return frame != nullptr && frame->window_placement_revision >= 3u;
        }));
        // 예고가 끝나면 더 돌지 않는다.
        std::this_thread::sleep_for(30ms);
        REQUIRE(driver.ticks.load() == 3);
    }
}

TEST_CASE("Shutting down the host follows the close-first order", "[win32][host]")
{
    counting_driver driver {};
    {
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, nullptr };
        REQUIRE(wait_until([&] { return driver.started.load(); }));
        // shutdown은 소멸자가 부른다.
    }
    REQUIRE(driver.closed.load());
    REQUIRE(driver.workers_stopped.load());
}

namespace {
    // gate가 열릴 때까지 메시지 하나를 붙잡아 app inbox를 포화 상태로 두는 driver다.
    // 종료 신호만은 gate와 무관하게 즉시 처리한다 — 이 test가 묻는 것은
    // "close가 도착하기만 하면 종료 저장이 도는가"라서 도착 여부만 남겨야 한다.
    class blocking_driver final : public luil::win32::logic_driver
    {
    public:
        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
            {
                closed.store(true);
                return;
            }
            while (gate.load() == false)
                std::this_thread::sleep_for(1ms);
            handled.fetch_add(1);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            return std::make_shared<luil::win32::ui_frame>();
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        std::atomic<bool> gate { false };
        std::atomic<bool> closed { false };
        std::atomic<int> handled { 0 };
    };
} // namespace

TEST_CASE("The close message survives a saturated app inbox", "[win32][host][shutdown]")
{
    blocking_driver driver {};
    {
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, nullptr };

        // gate를 닫은 채 app inbox 용량(1024)의 두 배를 밀어 넣는다.
        // 넘친 것은 reject_newest가 버린다 — 여기서 중요한 것은 넘친 쪽이 아니라
        // 이 상태에서 종료 신호가 들어갈 자리가 남아 있지 않다는 사실이다.
        for (int index { 0 }; index < 2048; ++index)
            host.post_app_message(luil::app_message { ping_intent { u8"flood" } });

        // 넘친 게시는 조용히 버려진다 (reject_newest의 의도된 한계).
        // 유실이 있었다는 사실은 통계로만 관찰된다.
        REQUIRE(host.app_inbox_statistics().rejected > 0);

        const std::chrono::steady_clock::time_point started { std::chrono::steady_clock::now() };
        std::thread closer { [&host] { host.shutdown(); } };
        // 게시가 마감시각까지 다시 시도하는 동안 소비가 풀리고 자리가 난다.
        std::this_thread::sleep_for(50ms);
        driver.gate.store(true);
        closer.join();
        const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started) };

        // 종료 신호가 포화에 삼켜지면 이 둘이 함께 무너진다:
        // 종료 저장이 돌지 않고, 대기 상한 3초를 통째로 헛태운다.
        REQUIRE(driver.closed.load());
        REQUIRE(elapsed < 2500ms);
        // gate가 열린 뒤 막혀 있던 소비가 실제로 풀렸다는 확인이다.
        REQUIRE(driver.handled.load() > 0);
    }
}

namespace {
    // 지정한 콜백 하나에서 던지는 driver다.
    // 스레드 최상단 가드가 예외를 faulted로 바꾸는지 검증한다.
    class throwing_driver final : public luil::win32::logic_driver
    {
    public:
        enum class throw_point
        {
            start,
            handle,
            make_frame,
            tick,
        };

        explicit throwing_driver(const throw_point point)
            : point_ { point }
        {}

        void start() override
        {
            if (point_ == throw_point::start)
                throw std::runtime_error { "start" };
        }

        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
            {
                closed.store(true);
                return;
            }
            if (point_ == throw_point::handle)
                throw std::runtime_error { "handle" };
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            if (point_ == throw_point::make_frame)
                throw std::runtime_error { "make_frame" };
            return std::make_shared<luil::win32::ui_frame>();
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override
        {
            // tick 지점 검증에서만 한 번 예고한다.
            if (point_ != throw_point::tick || ticked.load())
                return std::nullopt;
            return std::chrono::steady_clock::now() + 1ms;
        }

        void tick(std::chrono::steady_clock::time_point) override
        {
            ticked.store(true);
            throw std::runtime_error { "tick" };
        }

        std::atomic<bool> closed { false };
        std::atomic<bool> ticked { false };

    private:
        throw_point point_ {};
    };

    // 키 입력에서 던지는 정책이다.
    // input thread의 최상단 가드를 검증한다.
    class throwing_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            throw std::runtime_error { "on_key" };
        }
    };
} // namespace

TEST_CASE("A driver exception faults the host instead of terminating the process", "[win32][host][fault]")
{
    const auto verify_fault = [](const throwing_driver::throw_point point, const bool needs_ping) {
        throwing_driver driver { point };
        {
            luil::win32::app_host host { luil::win32::app_host::config {}, driver, nullptr };
            if (needs_ping)
                host.post_app_message(luil::app_message { ping_intent { u8"boom" } });
            REQUIRE(wait_until([&] { return host.faulted(); }));
        }
        // 파괴(=shutdown)가 terminate·hang 없이 여기까지 온 것 자체가 검증이다.
    };

    SECTION("start") { verify_fault(throwing_driver::throw_point::start, false); }
    SECTION("make_frame") { verify_fault(throwing_driver::throw_point::make_frame, false); }
    SECTION("handle") { verify_fault(throwing_driver::throw_point::handle, true); }
    SECTION("tick") { verify_fault(throwing_driver::throw_point::tick, false); }
}

TEST_CASE("A policy exception faults the host instead of terminating the process", "[win32][host][fault]")
{
    counting_driver driver {};
    throwing_policy policy {};
    {
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };
        REQUIRE(wait_until([&] { return driver.started.load(); }));
        // pump가 정책을 부르다 던지면 input thread의 가드가 받는다.
        host.post_raw_input(luil::key_pressed_event { luil::key_code::f5 });
        REQUIRE(wait_until([&] { return host.faulted(); }));
    }
}

namespace {
    // 종료 콜백에서 던지는 driver다.
    // 예외가 채널 close와 join을 건너뛰게 하지 않는지 검증한다.
    class throwing_shutdown_driver final : public luil::win32::logic_driver
    {
    public:
        enum class throw_point
        {
            close_message,
            stop_workers,
        };

        explicit throwing_shutdown_driver(const throw_point point)
            : point_ { point }
        {}

        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
                closed.store(true);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            return std::make_shared<luil::win32::ui_frame>();
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            if (point_ == throw_point::close_message)
                throw std::runtime_error { "make_close_message" };
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        void stop_workers() override
        {
            if (point_ == throw_point::stop_workers)
                throw std::runtime_error { "stop_workers" };
        }

        std::atomic<bool> closed { false };

    private:
        throw_point point_ {};
    };

    // cancel이 올 때까지 handle 하나가 돌아오지 않는 driver다.
    // 협력 취소가 종료 예산 안에 logic thread를 꺼내는지 검증한다.
    class stalling_driver final : public luil::win32::logic_driver
    {
    public:
        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
            {
                closed.store(true);
                return;
            }
            stalled.store(true);
            // gate도 시한도 없다 — cancel만이 이 대기를 끝낸다.
            while (cancelled.load() == false)
                std::this_thread::sleep_for(1ms);
        }

        void cancel() noexcept override
        {
            cancelled.store(true);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            return std::make_shared<luil::win32::ui_frame>();
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        std::atomic<bool> stalled { false };
        std::atomic<bool> cancelled { false };
        std::atomic<bool> closed { false };
    };
} // namespace

TEST_CASE("Shutdown finishes cleanup even when driver callbacks throw", "[win32][host][shutdown]")
{
    const auto verify_cleanup = [](const throwing_shutdown_driver::throw_point point) {
        throwing_shutdown_driver driver { point };
        const std::chrono::steady_clock::time_point started { std::chrono::steady_clock::now() };
        {
            luil::win32::app_host host { luil::win32::app_host::config {}, driver, nullptr };
        }
        const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started) };
        // 콜백이 던져도 close와 join은 실행되어야 파괴가 제때 끝난다.
        REQUIRE(elapsed < 2500ms);
    };

    SECTION("make_close_message") { verify_cleanup(throwing_shutdown_driver::throw_point::close_message); }
    SECTION("stop_workers") { verify_cleanup(throwing_shutdown_driver::throw_point::stop_workers); }
}

TEST_CASE("Cancel frees a stalled driver within the shutdown budget", "[win32][host][shutdown]")
{
    stalling_driver driver {};
    std::chrono::steady_clock::time_point started {};
    {
        luil::win32::app_host host { luil::win32::app_host::config {}, driver, nullptr };
        host.post_app_message(luil::app_message { ping_intent { u8"stall" } });
        REQUIRE(wait_until([&] { return driver.stalled.load(); }));
        started = std::chrono::steady_clock::now();
        // 소멸자의 shutdown이 cancel → 종료 신호 → close 순으로 꺼낸다.
    }
    const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started) };
    REQUIRE(driver.cancelled.load());
    REQUIRE(driver.closed.load());
    REQUIRE(elapsed < 2500ms);
}

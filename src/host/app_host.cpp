#include "luil/app/app_host.h"

#include "host/fail_fast.h"
#include "host/font_registry.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

namespace luil {
    namespace {
        void input_thread_main(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot,
            messaging::latest_slot<surface_tree_list>& surface_tree_slot, messaging::channel<app_message>& app_inbox, messaging::latest_slot<interaction_snapshot>& interaction_slot,
            const std::function<void(ui_command)> wake_ui_command, interaction_policy* const policy, interaction_config config,
            const std::function<void(app_ui_command)> execute_app_ui_command, const std::function<void(clipboard_request)> execute_clipboard,
            messaging::latest_slot<touch_gesture_config>& touch_slot, std::atomic<bool>& faulted)
        {
            // policy·wake callback의 예외는 계약 밖 입력이다.
            // 스레드 진입 함수를 벗어난 예외는 곧 terminate라, 여기서 삼키고
            // faulted로 알린 뒤 정상 반환한다 — join은 평소처럼 즉시 돌아온다.
            try
            {
                // 측정 함수가 비어 있으면 설정된 UI 글꼴로 기본값을 채운다.
                // 그리기와 같은 글꼴이라야 caret 자리와 글자 그림이 어긋나지 않는다.
                if (config.measure_text == nullptr)
                    config.measure_text = [](const std::u8string_view text, const float pixel_size) {
                        const SkFont font { configured_ui_typeface(), pixel_size };
                        return measure_text(text, font);
                    };
                run_ui_input_pump(
                    input_inbox, tree_slot, surface_tree_slot, app_inbox, interaction_slot,
                    [wake_ui_command](const ui_command command) {
                        if (wake_ui_command != nullptr)
                            wake_ui_command(command);
                    },
                    policy, std::move(config), execute_app_ui_command, execute_clipboard, &touch_slot);
            }
            catch (...)
            {
                faulted.store(true);
                input_inbox.close();
            }
        }

        // 종료 신호를 마감시각까지 게시한다.
        // app inbox는 reject_newest라 포화한 순간의 게시는 payload째로 버려지는데,
        // 버려진 것이 종료 신호면 종료 저장이 함께 사라진다 — 이 한 건만은 물러났다 다시 넣는다.
        // 닫힌 inbox는 이미 끝난 종료다 (멱등 shutdown 방어) — 곧바로 포기한다.
        [[nodiscard]] bool post_close_message(messaging::channel<app_message>& inbox, const app_message& message, const std::chrono::steady_clock::time_point deadline)
        {
            while (true)
            {
                app_message attempt { message };
                const messaging::post_result result { inbox.post(std::move(attempt)) };
                if (result == messaging::post_result::channel_closed)
                    return false;
                if (result != messaging::post_result::channel_full)
                    return true;
                // 마감을 넘기면서까지 버티지 않는다.
                // 여기서 예산을 다 쓰면 뒤따르는 대기 단계가 쓸 몫이 남지 않는다.
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
            }
        }
    } // namespace

    // 구성 요소의 선언 순서가 곧 파괴의 역순이다.
    // 스레드는 shutdown()이 먼저 멈추므로 여기서는 소유만 담당한다.
    struct app_host::assembly
    {
        messaging::channel<raw_input_event> input_inbox { messaging::channel_options { 4096, messaging::overflow_policy::drop_oldest, {} } };
        messaging::channel<app_message> app_inbox { messaging::channel_options { 1024, messaging::overflow_policy::reject_newest, {} } };
        messaging::latest_slot<std::shared_ptr<const ui_frame>> frame_slot {};
        messaging::latest_slot<std::shared_ptr<const ui_tree>> tree_slot {};
        // 주 창 밖 표면(popup·보조 창)의 tree들이다.
        // 주 tree처럼 신호 없이 게시하고 input thread가 처리 직전에 받는다.
        messaging::latest_slot<surface_tree_list> surface_tree_slot {};
        messaging::latest_slot<interaction_snapshot> interaction_slot {};
        // 실행 중 바꾼 터치 설정이다. 최신 값 하나만 의미가 있다.
        messaging::latest_slot<touch_gesture_config> touch_slot {};

        // 창 명령 큐다.
        // input thread가 넣고 UI thread가 신호를 받아 꺼낸다.
        // 신호는 유실될 수 있어도(창 메시지 게시 실패) 내용은 여기 남는다.
        std::mutex ui_command_mutex {};
        std::vector<ui_command> ui_commands {};

        // 앱 UI 명령 큐다.
        // input thread가 넣고 UI thread가 신호를 받아 꺼낸다.
        // 창 메시지는 인자를 담지 못하므로 내용은 여기로 나른다.
        std::mutex app_ui_command_mutex {};
        std::vector<app_ui_command> app_ui_commands {};

        // 클립보드 요청 큐다.
        // 앱 UI 명령과 같은 방식으로 나른다.
        std::mutex clipboard_mutex {};
        std::vector<clipboard_request> clipboard_requests {};

        // shutdown()이 정한 종료 예산의 마감시각이다.
        // 종료 신호 게시와 logic 처리 대기가 이 하나를 나눠 본다.
        // shutdown() 안에서만 쓰지만 두 함수가 함께 읽으므로 조립체가 들고 있다.
        std::chrono::steady_clock::time_point shutdown_deadline {};

        // 스레드 최상단 예외 가드가 세운다 (`app_host::faulted()`가 읽는다).
        std::atomic<bool> faulted { false };
        // logic thread가 진입 함수를 벗어나기 직전에 세운다.
        // shutdown()이 무기한 join에 들어가기 전에 이 값을 유한 시간만 기다린다.
        std::atomic<bool> logic_thread_exited { false };

        std::thread logic_thread {};
        std::thread input_thread {};
    };

    app_host::app_host(config configuration, logic_driver& driver, interaction_policy* const policy)
        : assembly_ { std::make_unique<assembly>() }
        , driver_ { driver }
    {
        // 조립 순서는 "채널 생성 → callback 설정 → 스레드 시작"으로 고정한다.
        // wake 신호는 platform이 준 불투명 함수라 여기에는 창 개념이 없다.
        // 던질 수 있는 준비(callback 구성)는 전부 스레드 시작 앞에 있어야 한다 —
        // 시작 뒤의 준비 실패는 joinable thread 파괴, 곧 terminate가 된다.
        if (configuration.wake.snapshot != nullptr)
        {
            assembly_->frame_slot.set_signal_callback(configuration.wake.snapshot);
            // hover·tooltip·drag 표시가 바뀌면 다시 그린다.
            // wake 신호는 frame과 같다.
            assembly_->interaction_slot.set_signal_callback(configuration.wake.snapshot);
        }

        assembly& parts { *assembly_ };
        // 요청 내용은 큐로 나르고 wake는 신호만 전한다.
        // 종료 시 input join이 이 callback보다 늦게 오므로 assembly 수명은 안전하다.
        std::function<void(ui_command)> forward_ui_command {};
        if (configuration.wake.ui_command != nullptr)
        {
            forward_ui_command = [&parts, wake = configuration.wake.ui_command](const ui_command command) {
                {
                    const std::lock_guard<std::mutex> lock { parts.ui_command_mutex };
                    parts.ui_commands.push_back(command);
                }
                wake();
            };
        }

        std::function<void(app_ui_command)> execute_app_ui_command {};
        if (configuration.wake.app_ui_command != nullptr)
        {
            execute_app_ui_command = [&parts, wake = configuration.wake.app_ui_command](app_ui_command command) {
                {
                    const std::lock_guard<std::mutex> lock { parts.app_ui_command_mutex };
                    parts.app_ui_commands.push_back(std::move(command));
                }
                wake();
            };
        }

        std::function<void(clipboard_request)> execute_clipboard {};
        if (configuration.wake.clipboard != nullptr)
        {
            execute_clipboard = [&parts, wake = configuration.wake.clipboard](clipboard_request request) {
                {
                    const std::lock_guard<std::mutex> lock { parts.clipboard_mutex };
                    parts.clipboard_requests.push_back(std::move(request));
                }
                wake();
            };
        }

        assembly_->logic_thread = std::thread { &app_host::logic_thread_main, this };

        try
        {
            assembly_->input_thread = std::thread {
                &input_thread_main,
                std::ref(parts.input_inbox),
                std::ref(parts.tree_slot),
                std::ref(parts.surface_tree_slot),
                std::ref(parts.app_inbox),
                std::ref(parts.interaction_slot),
                std::move(forward_ui_command),
                policy,
                std::move(configuration.interaction),
                std::move(execute_app_ui_command),
                std::move(execute_clipboard),
                std::ref(parts.touch_slot),
                std::ref(parts.faulted),
            };
        }
        catch (...)
        {
            // input thread를 만들지 못했다 (자원 고갈).
            // shutdown() 4단계와 같은 되감기다: inbox를 닫으면 logic이 곧 나온다.
            // 실패는 호출자의 것이고 프로세스는 산다.
            assembly_->app_inbox.close();
            assembly_->logic_thread.join();
            throw;
        }
    }

    app_host::~app_host()
    {
        shutdown();
    }

    void app_host::shutdown() noexcept
    {
        if (shut_down_)
            return;
        shut_down_ = true;

        // 게시(1)와 처리 대기(2)가 쓰는 예산이다.
        // 둘이 이 하나를 나눠 써야 그 구간의 최악이 3초로 유지된다 —
        // 따로 재면 3초 게시 + 3초 대기가 되어 붙잡는 시간이 두 배가 된다.
        // UI thread를 이만큼 붙잡는 것 자체는 새 위험이 아니다: 대기 단계가 전부터
        // 정확히 그만큼 잡고 있었고, 이 시점의 창은 이미 파괴 중이라 그릴 것이 없다.
        // join 앞의 탈출 확인은 wait_for_logic_exit의 별도 상한이 맡는다 —
        // 두 상한을 합쳐도 세션 종료 유예(기본 5초) 안이다.
        constexpr std::chrono::milliseconds shutdown_limit { 3000 };
        assembly_->shutdown_deadline = std::chrono::steady_clock::now() + shutdown_limit;

        // 0. 취소를 먼저 알린다.
        //    오래 걸리는 handle·tick·make_frame이 이를 보고 일찍 돌아와야
        //    아래 단계들이 예산 안에 끝난다.
        driver_.cancel();

        // 1. logic이 종료 상태를 게시하게 한다.
        //    포화된 inbox는 게시를 버리므로 마감시각까지 다시 넣어 본다.
        //    메시지가 몰린 순간의 세션 종료에서 종료 저장이 통째로 날아가던 자리다.
        //    make_close_message는 앱 코드라 던질 수 있다 — 만들지 못한 종료
        //    신호는 게시 실패와 같은 취급이고, 남은 정리는 그대로 진행한다.
        bool close_posted { false };
        try
        {
            close_posted = post_close_message(assembly_->app_inbox, driver_.make_close_message(), assembly_->shutdown_deadline);
        }
        catch (...)
        {}

        // 2. logic이 종료 신호를 처리할 때까지 기다린다.
        //    이 시점 이후에는 종료 저장 요청이 이미 worker에 들어가 있다.
        //    게시하지 못했으면 기다릴 이유가 없다 — 남은 예산을 태우지 않고 정리로 간다.
        if (close_posted)
            wait_for_logic_shutdown();

        // 3. worker를 먼저 끝낸다.
        //    취소된 작업의 event가 아직 열린 app inbox로
        //    들어가거나, 이후 닫힌 inbox에서 조용히 버려진다.
        //    이것도 앱 코드다 — 던져도 채널 close와 join은 반드시 실행한다.
        try
        {
            driver_.stop_workers();
        }
        catch (...)
        {}

        // 4. app inbox를 닫아 남은 메시지를 소진시키고 join한다.
        //    join 자체는 무기한이라, 스레드가 실제로 나오는지 먼저 유한하게
        //    확인한다. cancel과 close에도 나오지 않는 logic은 회복 불능이다 —
        //    보이지 않는 무기한 hang(창 닫힘·세션 종료가 영원히 멎는다) 대신
        //    즉시 실패를 택한다. detach는 assembly 수명을 깨므로 금지다.
        assembly_->app_inbox.close();
        if (assembly_->logic_thread.joinable())
        {
            wait_for_logic_exit();
            if (assembly_->logic_thread_exited.load() == false)
                platform_fail_fast();
            assembly_->logic_thread.join();
        }

        // 5. input을 닫고 join한 뒤 slot을 닫는다.
        //    input pump는 닫힌 inbox에서 즉시 돌아오므로 이 join은 짧다.
        assembly_->input_inbox.close();
        if (assembly_->input_thread.joinable())
            assembly_->input_thread.join();
        assembly_->frame_slot.close();
        assembly_->tree_slot.close();
        assembly_->surface_tree_slot.close();
        assembly_->interaction_slot.close();
    }

    void app_host::wait_for_logic_shutdown() noexcept
    {
        // logic이 멈춘 비정상 상황에서 종료가 매달리지 않도록 상한을 둔다.
        // 마감시각은 shutdown()이 정하며 신호 게시가 이미 쓴 만큼이 빠져 있다.
        // 상한을 넘으면 종료 저장을 포기하고 기존 순서대로 정리한다.
        while (driver_.shutdown_completed() == false)
        {
            if (std::chrono::steady_clock::now() >= assembly_->shutdown_deadline)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
        }
    }

    void app_host::wait_for_logic_exit() noexcept
    {
        // 닫힌 inbox를 본 logic thread에 빠져나올 시간을 주는 별도 유예다.
        // 건강한 logic은 남은 메시지(최대 1024건)를 소진하고 마지막 frame을
        // 게시한 뒤 수 ms 안에 나온다 — 1초를 다 쓰는 것은 이미 비정상이다.
        constexpr std::chrono::milliseconds exit_limit { 1000 };
        const std::chrono::steady_clock::time_point deadline { std::chrono::steady_clock::now() + exit_limit };
        while (assembly_->logic_thread_exited.load() == false)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
        }
    }

    bool app_host::faulted() const noexcept
    {
        return assembly_->faulted.load();
    }

    messaging::channel_statistics app_host::app_inbox_statistics() const
    {
        return assembly_->app_inbox.statistics();
    }

    void app_host::post_raw_input(raw_input_event event) noexcept
    {
        try
        {
            // drop_oldest 채널이라 반환값 확인이 통계로 충분하다.
            static_cast<void>(assembly_->input_inbox.post(std::move(event)));
        }
        catch (...)
        {}
    }

    bool app_host::set_touch_gesture_config(const touch_gesture_config& config) noexcept
    {
        if (valid_touch_gesture_config(config) == false)
            return false;
        try
        {
            static_cast<void>(assembly_->touch_slot.publish(config));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void app_host::post_app_message(app_message message) noexcept
    {
        try
        {
            static_cast<void>(assembly_->app_inbox.post(std::move(message)));
        }
        catch (...)
        {}
    }

    std::shared_ptr<const ui_frame> app_host::acquire_frame()
    {
        if (const auto newer { assembly_->frame_slot.take_newer(seen_frame_version_) }; newer.has_value())
        {
            seen_frame_version_ = newer->version;
            current_frame_ = newer->value;
        }
        return current_frame_;
    }

    std::shared_ptr<const ui_tree> app_host::acquire_ui_tree()
    {
        if (const auto newer { assembly_->tree_slot.take_newer(seen_tree_version_) }; newer.has_value())
        {
            seen_tree_version_ = newer->version;
            current_tree_ = newer->value;
        }
        return current_tree_;
    }

    std::vector<ui_command> app_host::take_ui_commands()
    {
        const std::lock_guard<std::mutex> lock { assembly_->ui_command_mutex };
        return std::exchange(assembly_->ui_commands, {});
    }

    std::vector<app_ui_command> app_host::take_app_ui_commands()
    {
        const std::lock_guard<std::mutex> lock { assembly_->app_ui_command_mutex };
        return std::exchange(assembly_->app_ui_commands, {});
    }

    std::vector<clipboard_request> app_host::take_clipboard_requests()
    {
        const std::lock_guard<std::mutex> lock { assembly_->clipboard_mutex };
        return std::exchange(assembly_->clipboard_requests, {});
    }

    interaction_snapshot app_host::acquire_interaction()
    {
        if (const auto newer { assembly_->interaction_slot.take_newer(seen_interaction_version_) }; newer.has_value())
        {
            seen_interaction_version_ = newer->version;
            current_interaction_ = newer->value;
        }
        return current_interaction_;
    }

    void app_host::publish_frame()
    {
        const std::shared_ptr<const ui_frame> frame { driver_.make_frame() };
        if (frame == nullptr)
            return;
        // wake 신호는 frame 게시에 붙어 있고 tree slot에는 신호가 없다.
        // tree를 먼저 게시해야 깨어난 UI 스레드가 새 tree를 읽는다.
        static_cast<void>(assembly_->tree_slot.publish(frame->tree));
        // popup과 보조 창의 tree가 한 표면 목록으로 input thread에 간다.
        // controller에는 표면만 있고 둘의 구분이 없다.
        surface_tree_list surfaces {};
        surfaces.reserve(frame->popups.size() + frame->windows.size());
        for (const ui_popup& popup : frame->popups)
            surfaces.emplace_back(popup.id, popup.tree);
        for (const ui_window& window : frame->windows)
            surfaces.emplace_back(window.id, window.tree);
        static_cast<void>(assembly_->surface_tree_slot.publish(std::move(surfaces)));
        static_cast<void>(assembly_->frame_slot.publish(frame));
    }

    void app_host::logic_thread_main()
    {
        // driver 예외는 계약 밖 입력이다 (헤더의 예외 계약).
        // 스레드 진입 함수를 벗어난 예외는 곧 terminate라, 여기서 삼키고
        // faulted로 알린 뒤 inbox를 닫는다 — shutdown()의 게시도 곧바로
        // 포기하게 되고, 탈출 표시 덕에 join은 즉시 돌아온다.
        try
        {
            logic_loop();
        }
        catch (...)
        {
            assembly_->faulted.store(true);
            assembly_->app_inbox.close();
        }
        assembly_->logic_thread_exited.store(true);
    }

    void app_host::logic_loop()
    {
        driver_.start();
        publish_frame();
        std::optional<std::chrono::steady_clock::time_point> next_tick { driver_.next_tick() };
        std::vector<messaging::envelope<app_message>> batch {};
        messaging::envelope<app_message> received {};
        while (true)
        {
            // 예고된 tick이 있으면 그때까지만 기다린다.
            // 없으면 250ms — 종료 신호 확인 주기다.
            std::chrono::milliseconds wait { 250 };
            if (next_tick.has_value())
            {
                const auto remaining { std::chrono::duration_cast<std::chrono::milliseconds>(*next_tick - std::chrono::steady_clock::now()) };
                if (remaining < wait)
                    wait = remaining > std::chrono::milliseconds { 0 } ? remaining : std::chrono::milliseconds { 0 };
            }

            const messaging::receive_status status { assembly_->app_inbox.receive_wait(received, wait) };
            if (status == messaging::receive_status::closed)
            {
                publish_frame();
                return;
            }

            bool changed { false };
            if (status == messaging::receive_status::received)
            {
                driver_.handle(std::move(received.payload));

                // 몰려온 메시지는 한 번에 처리하고 frame은 batch당 한 번만 게시한다.
                // 상한이 렌더링 기회를 보존한다.
                batch.clear();
                static_cast<void>(assembly_->app_inbox.drain(batch, 63));
                for (messaging::envelope<app_message>& entry : batch)
                    driver_.handle(std::move(entry.payload));
                changed = true;
            }

            // 예고 시각이 지났으면 메시지가 없어도 시간이 logic을 움직인다.
            if (next_tick.has_value() && std::chrono::steady_clock::now() >= *next_tick)
            {
                driver_.tick(std::chrono::steady_clock::now());
                changed = true;
            }

            if (changed == false)
                continue;
            publish_frame();
            // 예고는 frame 게시 뒤마다 다시 묻는다 (다음 만료가 바뀌었을 수 있다).
            next_tick = driver_.next_tick();
        }
    }
} // namespace luil

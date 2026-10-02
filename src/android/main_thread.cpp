#include "android/main_thread.h"

#include <game-activity/GameActivity.h>

#include <android/log.h>
#include <android/looper.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace luil::android {
    namespace {
        // 지금 Activity의 메인 thread 다리다. Activity는 프로세스에 하나씩 서고 진다.
        struct main_thread_bridge
        {
            std::mutex mutex {};
            GameActivity* activity { nullptr };
            ALooper* looper { nullptr };
            int wake_fd { -1 };
            std::deque<main_thread_work> pending {};
            // glue가 단 `onDestroy`다. 다리를 거둔 뒤 그것으로 넘긴다.
            void (*glue_destroy)(GameActivity*) { nullptr };
        };

        main_thread_bridge bridge {};
        std::atomic<bool> warned_missing { false };

        // 메인 looper가 깨우기 fd를 보고 부른다. 쌓인 일을 이 thread에서 돈다.
        int drain_pending(const int fd, const int events, void*)
        {
            std::uint64_t count { 0 };
            static_cast<void>(read(fd, &count, sizeof(count)));
            if ((events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP)) != 0)
                return 0;

            std::deque<main_thread_work> work {};
            GameActivity* activity { nullptr };
            {
                std::lock_guard lock { bridge.mutex };
                work.swap(bridge.pending);
                activity = bridge.activity;
            }
            if (activity == nullptr || activity->env == nullptr)
                return 1;
            for (main_thread_work& item : work)
            {
                item(*activity->env, activity->javaGameActivity);
                activity->env->ExceptionClear();
            }
            return 1;
        }

        // Activity가 지기 전에 다리를 거둔다. 메인 thread에서 불린다.
        void detach_bridge(GameActivity* const activity)
        {
            void (*glue_destroy)(GameActivity*) { nullptr };
            {
                std::lock_guard lock { bridge.mutex };
                if (bridge.activity == activity)
                {
                    if (bridge.looper != nullptr && bridge.wake_fd >= 0)
                        ALooper_removeFd(bridge.looper, bridge.wake_fd);
                    if (bridge.wake_fd >= 0)
                        close(bridge.wake_fd);
                    if (bridge.looper != nullptr)
                        ALooper_release(bridge.looper);
                    bridge.activity = nullptr;
                    bridge.looper = nullptr;
                    bridge.wake_fd = -1;
                    bridge.pending.clear();
                }
                glue_destroy = bridge.glue_destroy;
            }
            if (glue_destroy != nullptr)
                glue_destroy(activity);
        }

        void attach_bridge(GameActivity* const activity)
        {
            ALooper* const looper { ALooper_forThread() };
            const int wake_fd { eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK) };
            if (looper == nullptr || wake_fd < 0 || ALooper_addFd(looper, wake_fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, &drain_pending, nullptr) != 1)
            {
                if (wake_fd >= 0)
                    close(wake_fd);
                __android_log_print(ANDROID_LOG_ERROR, "luil", "Failed to set up the main thread bridge.");
                return;
            }
            ALooper_acquire(looper);

            std::lock_guard lock { bridge.mutex };
            bridge.activity = activity;
            bridge.looper = looper;
            bridge.wake_fd = wake_fd;
            bridge.pending.clear();
        }

        // glue가 단 `onDestroy`를 감싼다. 콜백은 메인 thread에서만 읽고 쓰이므로 엇갈리지 않는다.
        void wrap_destroy(GameActivity* const activity)
        {
            std::lock_guard lock { bridge.mutex };
            if (bridge.activity != activity)
                return;
            bridge.glue_destroy = activity->callbacks->onDestroy;
            activity->callbacks->onDestroy = &detach_bridge;
        }
    } // namespace

    bool post_to_main_thread(main_thread_work work)
    {
        int wake_fd { -1 };
        {
            std::lock_guard lock { bridge.mutex };
            if (bridge.activity != nullptr && bridge.wake_fd >= 0)
            {
                bridge.pending.push_back(std::move(work));
                wake_fd = bridge.wake_fd;
            }
        }
        if (wake_fd < 0)
        {
            if (warned_missing.exchange(true) == false)
                __android_log_print(ANDROID_LOG_WARN, "luil", "The main thread bridge is not set up; the app must link luil::luil, which wraps GameActivity_onCreate.");
            return false;
        }
        const std::uint64_t one { 1 };
        return write(wake_fd, &one, sizeof(one)) == static_cast<ssize_t>(sizeof(one));
    }
} // namespace luil::android

// glue의 생성 함수다. 링크가 `--wrap=GameActivity_onCreate`로 GameActivity의 부름을 아래로 돌린다.
extern "C" GameActivity_createFunc __real_GameActivity_onCreate;

// GameActivity가 메인 thread에서 부르는 생성 함수를 먼저 받는다. 메인 looper에 다리를 놓고 glue의 것으로
// 넘긴다. 다리가 먼저인 이유는 glue가 그 안에서 앱의 `android_main` thread를 띄우고, 그 thread가 곧바로
// 일을 넘길 수 있어서다.
extern "C" void __wrap_GameActivity_onCreate(GameActivity* const activity, void* const saved_state, const size_t saved_state_size)
{
    luil::android::attach_bridge(activity);
    __real_GameActivity_onCreate(activity, saved_state, saved_state_size);
    luil::android::wrap_destroy(activity);
}

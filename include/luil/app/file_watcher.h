#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 감시 하나의 표다. 0은 "받아들이지 않았다"이다 (`net::http_ticket`과 같다).
    struct file_watch_ticket
    {
        std::uint64_t value { 0 };

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] bool operator==(const file_watch_ticket&) const noexcept = default;
    };

    enum class file_watch_scope
    {
        // 그 폴더의 바로 아래 항목이다.
        folder,
        // 그 아래 전부다.
        subtree,
        // 파일 하나다. 부모 폴더를 보고 이름으로 거르므로, 파일이 지워졌다 다시 생겨도 감시가 이어지고 아직 없어도 걸린다.
        file,
    };

    enum class file_change_kind
    {
        created,
        removed,
        // "내용이 바뀌었을 수 있다"이다. 쓰기가 끝났다는 뜻이 아니다 (Windows에는 닫힘 알림이 없다).
        modified,
        renamed,
    };

    struct file_change
    {
        file_change_kind kind { file_change_kind::modified };
        // 감시 경로 기준 상대 경로다. 구분자는 두 플랫폼 모두 '/'다.
        //  - `file` 감시는 부모 폴더 기준이라 그 파일의 이름이다.
        std::u8string path {};
        // `renamed`의 옛 이름이다. 나머지는 비었다.
        std::u8string old_path {};

        [[nodiscard]] bool operator==(const file_change&) const = default;
    };

    enum class file_watch_end
    {
        // 감시가 이어진다.
        none,
        // 앱이 `remove`했다.
        removed,
        // 대상이 사라졌거나 OS가 감시를 끊었다. `error`가 이유다.
        lost,
    };

    // 감시 하나의 변경 묶음이다. 같은 경로는 한 항목으로 합쳐 있고, 한 경로는 묶음 안에서 한 번만 나온다
    // (`path`로든 `old_path`로든). 묶음은 묶기 시작한 때와 지금 사이의 순 변화다 (file-watch-design.md의 합치기 표).
    struct file_change_batch
    {
        file_watch_ticket ticket {};
        std::vector<file_change> changes {};
        // 넘쳤다. `changes`는 일부이거나 비었고, 앱은 그 범위를 다시 훑어야 한다.
        bool rescan { false };
        file_watch_end end { file_watch_end::none };
        // 개발자용 영문 진단이다 (`lost`의 이유). 화면에 그대로 내지 않는다.
        std::u8string error {};
        // 앞서 `deliver`가 거절한 묶음을 다시 낸 것이다. 그 사이의 변경이 합쳐졌을 수 있다.
        //  - 거절한 변경이 순 변화 없이 사라졌으면 쥔 것이 없어지므로 다음 묶음은 거짓이다.
        bool retried { false };
    };

    struct file_watcher_config
    {
        // 묶음을 앱에 건네는 유일한 자리다. **감시 thread에서 불린다.** 받았으면 참이다.
        //
        //     config.deliver = [&host](luil::file_change_batch batch) {
        //         return host.post_app_message_ticketed(luil::app_message { watch_intent { std::move(batch) } }) != 0;
        //     };
        //
        //  - 거짓이면 감시자가 그 묶음을 버리지 않는다. 다음 변경과 합쳐 `settle` 뒤에 다시 낸다. app inbox가
        //    포화해 메시지를 버려도 변경이 조용히 사라지지 않게 하는 자리다 (`post_app_message`는 버린 것을 알리지 않는다).
        //  - 여기서 `add`·`remove`는 불러도 된다. `stop`은 안 된다. 자기 자신을 join한다. debug 빌드가 assert로 잡는다.
        //  - 던지면 삼키고 거절로 본다.
        //  - 비어 있으면 묶음을 받은 것으로 보고 버린다 (test의 자리다).
        std::function<bool(file_change_batch)> deliver {};
        // 마지막 변경 뒤 이만큼 조용하면 묶음을 낸다. 편집기의 저장은 수 ms 안에 끝난다.
        std::chrono::milliseconds settle { 100 };
        // 변경이 그치지 않아도 첫 변경 뒤 이만큼이면 낸다. 쉬지 않고 쓰는 로그도 1초마다 보인다.
        std::chrono::milliseconds max_delay { 1000 };
        // 감시 하나의 묶음에 담을 상한이다. 넘으면 비우고 `rescan`이다.
        std::size_t max_changes { 4096 };
        // `stop()`이 OS가 감시 버퍼를 놓기를 기다릴 상한이다. `net::http_client_config::stop_budget`과 같은 종료 예산 산술 안이다.
        std::chrono::milliseconds stop_budget { 1000 };
    };

    // 폴더 하나, 그 아래 전부, 또는 파일 하나의 변경을 OS 알림으로 받아 묶음으로 건넨다 (file-watch-design.md).
    // Windows는 `ReadDirectoryChangesW`, Android는 inotify다.
    //
    // **thread를 하나 소유한다.** 일반 worker는 앱 정책이고 그 판단은 그대로다. 이것은 `net::http_client`처럼 OS API를
    // 옳게 쓰는 데 필요한 부품이다. OS 알림은 막히는 기다림으로 오고 logic thread는 막힐 수 없다.
    //
    // **끝맺음.** 받은 표마다 `end != none`인 묶음이 정확히 하나 온다 (`stop`이 먼저 오면 오지 않는다). 앱 상태 기계는
    // "표를 받았으면 끝 묶음을 기다린다" 한 줄이다.
    //
    // **처음 훑기는 하지 않는다.** 앱은 `add`를 먼저 하고 그다음 읽는다. 거꾸로 하면 그 사이의 변경을 놓친다.
    //
    // **종료는 앱의 종료 순서에 낀다.** `logic_driver::stop_workers()`에서 `stop()`을 부른다. `deliver`가 쥔 자원보다 먼저다.
    // 모든 메서드는 어느 thread에서든 부를 수 있다.
    class file_watcher
    {
    public:
        file_watcher(const file_watcher&) = delete;
        file_watcher(file_watcher&&) = delete;
        file_watcher& operator=(const file_watcher&) = delete;
        file_watcher& operator=(file_watcher&&) = delete;
        // `stop()`을 부른다.
        ~file_watcher();

        // 감시자 하나를 세운다. 실패하면 null이고 `error`에 이유를 적는다 (`net::http_client::create`와 같은 규약이다).
        [[nodiscard]] static std::unique_ptr<file_watcher> create(file_watcher_config configuration, std::u8string& error);

        // 감시를 건다. 경로가 없거나(`file`이면 부모 폴더가 없거나) OS가 거절하면 빈 표이고 `error`에 적는다.
        //  - 경로는 UTF-8이다. 표를 받았으면 끝 묶음이 온다.
        //  - 다른 thread에서 부르면 감시 thread가 감시를 걸 때까지 기다린다. 돌아온 뒤의 변경은 놓치지 않는다.
        [[nodiscard]] file_watch_ticket add(std::u8string_view path, file_watch_scope scope, std::u8string& error);
        // 감시를 내린다. 남은 변경을 담은 마지막 묶음(`removed`)이 온다. 모르는 표·끝난 표는 아무 일도 하지 않는다.
        void remove(file_watch_ticket ticket) noexcept;

        // 멈춘다. 멱등이고 어느 thread에서든 부른다 (`deliver` 안은 안 된다). 차례가 계약이다.
        //  1. 새 `add`를 거절한다.
        //  2. thread를 깨워 멈추고 join한다. 이 뒤로 `deliver`는 없다. 아직 내지 않은 변경은 버린다.
        //  3. 걸어 둔 OS 감시를 모두 풀고 `stop_budget` 안에서 OS가 버퍼를 놓기를 기다린다. 넘기면 버퍼를 일부러 남긴다.
        void stop() noexcept;

    private:
        struct engine;
        explicit file_watcher(std::unique_ptr<engine> owned) noexcept;

        std::unique_ptr<engine> engine_ {};
    };
}

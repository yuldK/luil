#pragma once

#include "luil/app/file_watcher.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 파일 감시의 OS를 모르는 몫이다: 원 이벤트 합치기, 이름 바꿈 짝짓기, 디바운스, 넘침, 거절된 묶음의 재전달
// (file-watch-design.md의 "묶음 짓기"). Win32와 Android 백엔드는 OS 알림을 아래 이벤트로 옮기고, 기다릴 시한을
// `due_at`에서 받고, 때가 되면 `offer_file_change_batch`를 부르기만 한다. 두 플랫폼의 뜻이 여기서 하나가 된다.
namespace luil {
    struct file_watch_timing
    {
        std::chrono::milliseconds settle { 100 };
        std::chrono::milliseconds max_delay { 1000 };
        std::size_t max_changes { 4096 };
    };

    // 감시 하나의 묶음을 모은다. 감시 thread 하나만 만진다.
    //
    // 묶음은 묶기 시작한 때와 지금 사이의 **순 변화**다. 그래서 한 경로는 묶음 안에서 한 번만 나온다 (`path`로든
    // `old_path`로든). 이름을 바꾼 뒤 옛 이름에 새 파일이 서면(편집기의 백업 이름 저장) 이름 바꿈은 옛 이름의
    // modified와 새 이름의 created로 풀린다. 앱이 경로마다 항목 하나만 보면 되게 하려는 것이다.
    class file_watch_batch
    {
    public:
        using clock = std::chrono::steady_clock;

        explicit file_watch_batch(file_watch_timing timing) noexcept;

        // `file` 감시의 이름 거름이다. 묶음을 낼 때 이름이 맞는 항목만 남긴다.
        //  - 합친 **뒤에** 거른다. `a.json.tmp`에 쓰고 `a.json`으로 바꾸는 저장은 임시 이름의 created가 있어야
        //    `a.json`의 created 하나로 합쳐진다.
        void set_name_filter(std::function<bool(std::u8string_view)> matches);

        void created(std::u8string path, clock::time_point now);
        void modified(std::u8string path, clock::time_point now);
        void removed(std::u8string path, clock::time_point now);
        // 짝을 아는 이름 바꿈이다.
        void renamed(std::u8string from, std::u8string to, clock::time_point now);
        // 짝을 모르는 이름 바꿈의 반쪽이다. Windows의 `RENAMED_OLD_NAME`·`NEW_NAME`, inotify의 `IN_MOVED_FROM`·`TO`다.
        //  - 바로 다음 이벤트가 같은 cookie의 `moved_to`이면 이름 바꿈이다. 다른 이벤트가 끼거나 묶음을 낼 때까지
        //    짝이 없으면 옛 이름은 removed, 새 이름은 created다 (감시 밖으로 나가고 들어온 것).
        //  - 반쪽이 두 번의 읽기에 걸쳐 와도 된다. 읽기 사이에는 다른 이벤트가 끼지 않는다.
        void moved_from(std::u8string path, std::uint32_t cookie, clock::time_point now);
        void moved_to(std::u8string path, std::uint32_t cookie, clock::time_point now);
        // OS가 넘쳤거나 옮길 수 없는 이름이 왔다. 모은 것을 비우고 다시 훑으라고 단다.
        void overflow(clock::time_point now);
        // 감시가 끝났다. 남은 변경과 함께 곧바로 낸다. 그 뒤의 이벤트는 받지 않는다.
        void finish(file_watch_end end, std::u8string error, clock::time_point now);

        // 다음에 묶음을 낼 시각이다. 낼 것이 없으면 nullopt다.
        //  - 마지막 변경 뒤 `settle`, 첫 변경 뒤 `max_delay` 가운데 이른 쪽이다. 끝 묶음은 곧바로다.
        //  - 거절된 뒤에는 그 시각부터 다시 센다. 새 변경이 없어도 `settle` 뒤에 다시 낸다.
        [[nodiscard]] std::optional<clock::time_point> due_at() const noexcept;
        // 끝 묶음을 받았다. 백엔드가 그 감시를 거둔다.
        [[nodiscard]] bool closed() const noexcept
        {
            return closed_;
        }
        // 끝을 알렸다 (받았는지와 상관없다). 이 뒤의 OS 이벤트는 버린다.
        [[nodiscard]] bool ending() const noexcept
        {
            return end_ != file_watch_end::none;
        }
        // 지금 모은 항목 수다 (거르기 전).
        [[nodiscard]] std::size_t size() const noexcept
        {
            return live_;
        }
        // 지운 자리까지 센 칸 수다. 낼 것이 없는데 칸만 쌓이지 않는지 test가 본다.
        [[nodiscard]] std::size_t slots() const noexcept
        {
            return entries_.size();
        }

        // 지금 모은 것을 묶음으로 짓는다. 거르고 나서 낼 것이 없으면 nullopt이고 모은 것을 비운다.
        //  - 짝 없는 반쪽 이름 바꿈은 여기서 풀린다.
        [[nodiscard]] std::optional<file_change_batch> prepare(file_watch_ticket ticket);
        // `prepare`가 지은 묶음을 앱이 받았는지다. 받았으면 비우고, 거절했으면 쥔 채 `settle` 뒤를 기다린다.
        void delivered(bool accepted, clock::time_point now);

    private:
        [[nodiscard]] bool pending() const noexcept;
        void touch(clock::time_point now) noexcept;
        void resolve_pending_move();
        void apply_created(std::u8string path);
        void apply_modified(std::u8string path);
        void apply_removed(std::u8string path);
        void apply_renamed(std::u8string from, std::u8string to);
        // 이 경로를 옛 이름으로 든 이름 바꿈을 created로 풀고 그 경로에 removed를 세운다. 경로가 다시 쓰이는 자리다.
        void release_old_path(const std::u8string& path);
        [[nodiscard]] std::optional<std::size_t> find(const std::u8string& path) const;
        void place(std::size_t index, file_change change);
        void append(file_change change);
        void erase(std::size_t index);
        void unindex(std::size_t index);
        void check_limit();
        void clear() noexcept;
        // 이 항목이 앱에 나가는가다. 거름이 없으면 모두 나간다. 이름 바꿈은 옛 이름이 맞아도 나간다 ("지금은 없다").
        [[nodiscard]] bool passes_filter(const file_change& entry) const;
        // 거름에 맞는 항목이 없어졌으면 거절한 묶음이 사라진 것이다. 표식만 푼다.
        void forget_vanished_rejection();

        struct pending_move
        {
            std::u8string path {};
            std::uint32_t cookie { 0 };
        };

        file_watch_timing timing_ {};
        std::function<bool(std::u8string_view)> name_filter_ {};
        // 들어온 차례대로다. 지운 자리는 비워 두어 색인이 흔들리지 않게 한다.
        std::vector<std::optional<file_change>> entries_ {};
        std::size_t live_ { 0 };
        std::unordered_map<std::u8string, std::size_t> by_path_ {};
        std::unordered_map<std::u8string, std::size_t> by_old_path_ {};
        std::optional<pending_move> move_ {};
        bool rescan_ { false };
        file_watch_end end_ { file_watch_end::none };
        std::u8string error_ {};
        bool closed_ { false };
        // 거절해 쥔 묶음이 있다. 쥔 것이 사라지면 풀어, 다음 묶음이 새것임을 안다.
        //  - 거름이 있으면 거름에 맞는 항목을 본다 (`forget_vanished_rejection`). 옆 이름이 남아도 풀린다.
        //  - 거름이 없으면 모은 것 전체를 본다. 항목마다 세지 않으므로 모두 사라져야 `clear`가 푼다.
        bool retried_ { false };
        std::optional<clock::time_point> first_ {};
        std::optional<clock::time_point> last_ {};
        std::optional<clock::time_point> end_due_ {};
    };

    // 때가 된 묶음을 짓고 `deliver`에 건넨다. `deliver`가 던지면 삼키고 거절로 본다. 비어 있으면 받은 것으로 본다.
    //  - 감시 thread에서 엔진 잠금 없이 부른다. `deliver` 안에서 `add`·`remove`가 다시 들어올 수 있다.
    void offer_file_change_batch(file_watch_batch& batch, file_watch_ticket ticket, const std::function<bool(file_change_batch)>& deliver, file_watch_batch::clock::time_point now);

    // OS의 경로 구분자 `\`를 `/`로 바꾼다. 두 플랫폼의 상대 경로가 같은 모양이 된다.
    [[nodiscard]] std::u8string to_watch_path(std::u8string path);
}

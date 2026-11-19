#pragma once

#include "luil/ui/ui_image.h"
#include "luil/win32/media_player.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace luil::win32 {
    struct video_frame_statistics
    {
        std::optional<std::uint32_t> dropped {};
        std::optional<std::uint32_t> rendered {};
    };

    // 논리 스레드가 만들고 닫는다. UI 트리는 마지막으로 복사된 불변 프레임만 가진다.
    class video_frame_player final
    {
    public:
        video_frame_player();
        ~video_frame_player();
        // 같은 MTA 논리 스레드에서 호출한다. 다시 열면 이전 재생기를 닫는다.
        void open(const std::filesystem::path& path);
        void play();
        // 처음 장으로 되감는다. 이후 play()로 다시 재생할 수 있다.
        void stop();
        // needs_tick() 동안 주기적으로 부른다. 새 이미지가 있을 때만 트리를 게시한다.
        void update();
        [[nodiscard]] bool needs_tick() const;
        [[nodiscard]] std::chrono::milliseconds position() const;
        [[nodiscard]] const luil::ui_image& image() const;
        [[nodiscard]] media_state state() const;
        // image()가 가진 장의 원본 표시 시각이다. 단위는 100 ns다.
        [[nodiscard]] std::int64_t frame_timestamp_100ns() const;
        // 엔진에서 제공하는 누적 계수다. 조회할 수 없는 계수는 빈 값이다.
        [[nodiscard]] video_frame_statistics statistics() const;
        [[nodiscard]] const std::u8string& error() const;

    private:
        struct engine;
        std::unique_ptr<engine> engine_;
    };
}

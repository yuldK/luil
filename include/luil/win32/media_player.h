#pragma once

#include "luil/ui/ui_element.h"

namespace luil::win32 {
    enum class media_state { empty, opening, ready, playing, paused, stopped, ended, failed };

    struct media_event
    {
        media_state state { media_state::empty };
        std::chrono::milliseconds position { 0 };
        std::chrono::milliseconds duration { 0 };
        std::u8string error {};
        long native_error { 0 };
    };

    struct media_player_config
    {
        // HWND다. null이면 오디오 전용이다. 영상이면 이 창 안에 네이티브 자식 표면을 만든다.
        // create/배치/stop/소멸은 이 부모 창의 UI thread에서 부른다. 부모보다 먼저 stop한다.
        void* parent_window { nullptr };
        // MF worker에서 부른다. post_app_message로 넘기고 여기서 stop하거나 소멸하지 않는다.
        std::function<void(media_event)> deliver {};
    };

    // Windows Media Foundation(MFPlay)이다. 모든 공개 메서드와 소멸은 COM을 초기화한 같은 UI thread에서 부른다.
    // 파일 경로·URL을 열고 OS 코덱으로 재생한다. DRM·자막·녹화는 이 API의 범위 밖이다.
    class media_player final
    {
    public:
        media_player(const media_player&) = delete;
        media_player& operator=(const media_player&) = delete;
        ~media_player();
        [[nodiscard]] static std::unique_ptr<media_player> create(media_player_config config, std::u8string& error);
        [[nodiscard]] bool open(std::u8string_view source, std::u8string& error);
        [[nodiscard]] bool play(std::u8string& error);
        [[nodiscard]] bool pause(std::u8string& error);
        [[nodiscard]] bool rewind(std::u8string& error);
        [[nodiscard]] bool seek(std::chrono::milliseconds position, std::u8string& error);
        [[nodiscard]] bool volume(float value, bool muted, std::u8string& error);
        // tree의 물리 bounds다. video_element와 같은 slot으로 맞추고 가려지면 visible=false로 감춘다.
        void set_video_bounds(rect_f bounds, bool visible = true) noexcept;
        // 부모의 WM_PAINT/WM_SIZE 뒤에 부른다. native 표면의 다시 그리기다.
        void repaint() noexcept;
        [[nodiscard]] media_event snapshot() const;
        // MF callback의 배달이 끝나기를 기다린다. 돌아온 뒤에는 deliver가 없다. 멱등이다.
        void stop() noexcept;

    private:
        struct engine;
        explicit media_player(std::unique_ptr<engine> engine);
        std::unique_ptr<engine> engine_ {};
    };
}

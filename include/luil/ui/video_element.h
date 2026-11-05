#pragma once

#include "luil/ui/ui_element.h"

namespace luil {
    struct video_config
    {
        std::u8string name {};
        // 미디어가 없을 때 앱이 놓을 설명이다. 비면 빈 표면이다.
        std::u8string placeholder {};
    };

    // 네이티브 동영상이 앉을 자리다. Windows UI thread가 이 bounds로 media_player::set_video_bounds를 부른다.
    // HWND는 tree에 담지 않는다. 플랫폼·재생기 수명은 앱의 UI thread가 소유한다.
    class video_element final : public ui_element
    {
    public:
        video_element(ui_element_id id, video_config config);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        video_config config_ {};
    };
}

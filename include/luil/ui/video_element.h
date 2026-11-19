#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_image.h"

namespace luil {
    struct video_config
    {
        std::u8string name {};
        // 미디어가 없을 때 앱이 놓을 설명이다. 비면 빈 표면이다.
        std::u8string placeholder {};
        ui_image frame {};
    };

    // frame이 있으면 원본 비율로 그리고 같은 영역에 overlay를 배치한다.
    // 네이티브 표면만 쓸 때는 빈 frame을 넘긴다. Windows UI thread가 이 bounds로 media_player::set_video_bounds를 부른다.
    // HWND는 tree에 담지 않는다. 플랫폼·재생기 수명은 앱의 UI thread가 소유한다.
    class video_element final : public ui_element
    {
    public:
        video_element(ui_element_id id, video_config config, std::unique_ptr<ui_element> overlay = {});
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        video_config config_ {};
    };
}

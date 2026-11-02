#pragma once

#include "luil/ui/ink.h"
#include "luil/ui/ui_element.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    // 캔버스의 도구다. 지우개 끝의 접촉은 도구와 상관없이 지우개다.
    enum class ink_tool
    {
        pen,
        eraser,
    };

    // 캔버스가 앱에 건네는 획 입력이다 (ink-design.md). 자리는 내용 좌표다.
    //  - `point.pressure`는 0~1로 접은 압력이고, 장치가 알리지 않았으면 1이다. 알렸는지는 `pressure_reported`다.
    //  - `time`은 표본의 시각이다. 뗄 때의 속도는 앱이 이 시각으로 잰다 (spring-design.md의 약속).
    //  - `interrupted`는 `end`가 정상적인 뗌이 아니라는 뜻이다 (입력 큐 유실, 접촉 중 배럴 전환). 받은 점까지가 획이다.
    //  - cancel의 `point`에는 뜻이 없다.
    struct ink_stroke_input
    {
        ink_phase phase { ink_phase::begin };
        pointer_device device { pointer_device::pen };
        std::uint32_t pointer_id { 0 };
        ink_point point {};
        float tilt_x { 0.0f };
        float tilt_y { 0.0f };
        bool interrupted { false };
        bool pressure_reported { false };
        std::chrono::steady_clock::time_point time {};
    };

    // 잉크 캔버스의 설정이다. 상태는 앱이 들고 캔버스는 그리며 앱 메시지를 낸다.
    //  - strokes는 굳은 획, wet는 진행 중 획이다 (결정 2). eraser_radius는 내용 논리 픽셀이다.
    //  - mouse·touch를 켜면 마우스 왼쪽 버튼과 손가락도 그린다 (기본은 펜만, 결정 4). 끈 장치에는 일반 요소다.
    //  - rotation·content_bounds는 내용 좌표 변환이다 (rotate-gesture-design.md의 규약). `content_bounds`는 돌리기 전의
    //    내용 경계이고 확대 보기에는 `rotated_bounds(content_bounds, rotation)`을 준다. 확대 보기 밖이면 기본값이다.
    //    zoom 칸은 없다. 확대 보기가 준 `context.scale`에 이미 들어 있다.
    //  - access_name·access_summary가 비면 기본 문구다 (G6).
    struct ink_canvas_config
    {
        std::u8string owner {};
        ink_layer strokes {};
        ink_stroke wet {};
        ink_tool tool { ink_tool::pen };
        bool mouse { false };
        bool touch { false };
        float eraser_radius { 8.0f };
        float rotation { 0.0f };
        std::optional<rect_f> content_bounds {};
        std::function<input_action(const ink_stroke_input&)> on_stroke {};
        std::function<input_action(std::vector<std::uint64_t> ids)> on_erase {};
        std::u8string access_name {};
        std::function<std::u8string(std::size_t strokes)> access_summary {};
    };

    // 표면 물리 픽셀의 점 (x, y)를 캔버스의 내용 좌표로 옮긴다 (ink-design.md의 좌표와 배율).
    //  - 원점의 화면 자리 `o = slot의 왼쪽 위 − rotated_bounds(content_bounds, rotation)의 왼쪽 위 × scale`, 점은
    //    `rotate_point((점 − o) / scale, −rotation)`이다. `content_bounds`가 비면 원점은 slot의 왼쪽 위다.
    //  - `scale`은 캔버스가 받은 `context.scale`이다 (창 배율 × zoom). 양수가 아니거나 유한하지 않으면 1이다.
    [[nodiscard]] point_f ink_content_point(const rect_f& slot, float scale, float rotation, const std::optional<rect_f>& content_bounds, float x, float y) noexcept;

    // 펜으로 그린 획을 받아 그리고 지운다. 바탕은 칠하지 않는다. 앱이 판 위에 놓는다.
    //  - 펜촉 접촉은 `on_stroke`의 begin·extend·end다. 지우개 끝과 `tool == eraser`의 접촉은 직전 표본에서 이번 표본까지를
    //    `eraser_radius`로 쓸어 닿은 획 id를 `on_erase`로 낸다 (획을 쪼개지 않는다, 결정 3).
    //  - 그리기는 캔버스 안으로 자른다. 굳은 획은 묶음의 그림 한 장, 진행 중 획은 그 path로 그린다.
    //  - 역할은 이미지다. 이름은 `access_name` → `set_access_name` → 기본 문구, 값은 `access_summary(획 수)` → 기본 요약이다.
    //    Tab 자리가 아니다. 키보드로 그리지 않는다. 도구·지우기·되돌리기는 앱이 단추와 단축키로 둔다 (결정 9).
    class ink_canvas_element final : public ui_element
    {
    public:
        explicit ink_canvas_element(ink_canvas_config config);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        [[nodiscard]] ink_source make_source() const;
        ink_canvas_config config_ {};
        float scale_ { 1.0f };
    };
}

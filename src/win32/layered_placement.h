#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace luil::win32 {
    // 화면의 창 사각형이다 (가상 화면 좌표, 물리 픽셀).
    // Win32 `RECT`를 쓰지 않는 것은 미룬 배치의 규칙을 창 없이 test하려는 것이다.
    struct screen_bounds
    {
        int x { 0 };
        int y { 0 };
        int width { 0 };
        int height { 0 };

        [[nodiscard]] bool operator==(const screen_bounds&) const noexcept = default;
    };

    // 새 tree를 기다리는 동안 이만큼 지나면 가진 tree로 낸다.
    //  - 앱이 크기 메시지를 받아도 tree를 다시 짓지 않거나(빈 metrics 메시지) 오래 걸리면, 기다리는 동안 창이 크기
    //    조절을 따라오지 않는다. 그대로 두면 창이 굳은 것처럼 보이므로 한도를 둔다.
    //  - 넘겨서 낸 frame은 standard 방식과 같은 어긋남이 한 번 보일 수 있다.
    inline constexpr std::chrono::milliseconds layered_placement_timeout { 250 };

    // 레이어드 표시에서 미룬 창 배치들이다 (layered-presentation-design.md).
    //
    // 크기가 바뀌는 배치는 OS에 바로 넘기지 않고 여기 쌓는다. 표마다 "이 크기로 배치하라"는 metrics 메시지의 표가
    // 붙는다. 그 표까지 처리한 tree가 오면, 그 tree가 배치된 크기의 배치를 꺼내 frame과 함께 낸다.
    //  - 끌기 도중에는 배치가 계속 쌓이고 logic은 몇 개를 한 batch로 처리한다. 마지막 배치의 tree만 기다리면 끄는
    //    동안 창이 멈추므로, 온 tree가 반영한 **가장 새 배치**를 내고 그보다 오래된 것을 버린다.
    //  - 크기 없이 자리만 바뀐 배치는 앞 배치의 표를 이어받는다. 앞 배치보다 먼저 나가면 순서가 뒤집힌다.
    class deferred_placements
    {
    public:
        using clock = std::chrono::steady_clock;

        // 배치를 쌓는다. `ticket`이 0이면 기다릴 tree가 없다 — 다음 frame에 낸다.
        void push(const screen_bounds& bounds, std::uint64_t ticket, clock::time_point now);
        // 크기가 그대로인 배치(자리만 옮김)를 쌓는다. 앞 배치의 표를 이어받는다.
        void push_move(const screen_bounds& bounds, clock::time_point now);
        // 가장 새 배치의 표를 바꾼다. 크기를 알리는 메시지는 그 배치를 쌓은 뒤에야 지을 수 있어서다 — metrics가
        // 가장 새 배치의 크기를 싣는다.
        void set_latest_ticket(std::uint64_t ticket) noexcept
        {
            if (entries_.empty() == false)
                entries_.back().ticket = ticket;
        }

        // `tree_ticket`까지 처리한 tree로 낼 배치를 꺼낸다.
        //  - 그 tree가 반영한 가장 새 배치를 꺼내고, 그보다 오래된 배치는 버린다.
        //  - 반영한 배치가 없어도 가장 오래 기다린 배치가 `timeout`을 넘었으면 가장 새 배치를 꺼내고 모두 비운다.
        [[nodiscard]] std::optional<screen_bounds> take_ready(std::uint64_t tree_ticket, clock::time_point now, clock::duration timeout);

        [[nodiscard]] bool empty() const noexcept
        {
            return entries_.empty();
        }

        // 가장 새 배치다. 비어 있으면 없다.
        //  - 앱에 알릴 배치 크기(metrics)는 지금 창이 아니라 이 값이다. 끄는 동안 창은 아직 옛 크기다.
        [[nodiscard]] std::optional<screen_bounds> latest() const noexcept;

        // 가장 오래 기다린 배치의 기한이다. 비어 있으면 없다.
        [[nodiscard]] std::optional<clock::time_point> deadline(clock::duration timeout) const noexcept;

        void clear() noexcept
        {
            entries_.clear();
        }

    private:
        struct entry
        {
            screen_bounds bounds {};
            std::uint64_t ticket { 0 };
            clock::time_point queued_at {};
        };

        std::vector<entry> entries_ {};
    };

    // 주 표면이 그릴 때 미룬 배치를 묻는 자리다.
    // 쌓는 쪽(창 메시지)과 꺼내는 쪽(그리기)이 다른 객체라 이 interface로 잇는다.
    class layered_placement_host
    {
    public:
        layered_placement_host() = default;
        layered_placement_host(const layered_placement_host&) = delete;
        layered_placement_host(layered_placement_host&&) = delete;
        layered_placement_host& operator=(const layered_placement_host&) = delete;
        layered_placement_host& operator=(layered_placement_host&&) = delete;
        virtual ~layered_placement_host() = default;

        // 이 표까지 처리한 tree로 그릴 frame이 함께 낼 배치다. 없으면 지금 자리·크기로 그린다.
        [[nodiscard]] virtual std::optional<screen_bounds> take_layered_placement(std::uint64_t tree_ticket) = 0;
        // 꺼낸 배치를 frame과 함께 냈다. OS가 배치 변경 메시지를 보내지 않으므로 그 뒤처리를 여기서 한다.
        virtual void layered_placement_presented(const screen_bounds& previous, const screen_bounds& bounds) = 0;
    };
} // namespace luil::win32

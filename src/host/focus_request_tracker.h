#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/ui_tree.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace luil {
    // 대상이 초점을 받을 만큼 섰는가 (deferred-focus-design.md).
    //  - tree에 있고, 초점을 받을 수 있으며(`focusable`), 보이는 자리가 표면 안에 조금이라도 걸친다.
    //  - 흘려 감춘 칸·접힌 묶음 안의 칸은 아직 서지 않은 것이다.
    [[nodiscard]] bool focus_target_ready(const ui_tree& tree, const ui_element_id& target);

    // UI thread가 초점 요청을 들고 있다가 대상이 서는 순간 한 번 이행하게 하는 장부다.
    // 플랫폼을 모른다. 이행(초점 요청 보내기·창 활성화)과 결과 알리기는 부르는 쪽이 한다.
    class focus_request_tracker
    {
    public:
        struct report
        {
            std::uint64_t id { 0 };
            focus_request_outcome outcome { focus_request_outcome::fulfilled };
        };

        // 한 번의 대조가 낸 일이다.
        struct step
        {
            // 지금 이행할 요청이다.
            std::optional<focus_request> fulfil {};
            // 알릴 결과다 (덮어쓴 요청·기한이 지난 요청·이행한 요청 차례).
            std::vector<report> reports {};
        };

        // 게시된 frame의 요청을 보고, 들고 있는 요청을 이행할 수 있으면 이행한다.
        //  - 새 번호의 요청이면 받는다. 들고 있던 것은 `superseded`다.
        //  - `ready`는 들고 있는 요청의 대상이 섰는지 묻는다 (표면을 찾는 것은 부르는 쪽이다).
        //  - 이행하지 못했는데 기한이 지났으면 `expired`로 버린다.
        [[nodiscard]] step advance(const std::optional<focus_request>& published, std::chrono::steady_clock::time_point now, const std::function<bool(const focus_request&)>& ready);

        // 들고 있는 요청의 기한이다. 들고 있지 않으면 없다.
        //  - 부르는 쪽이 이 시각에 다시 `advance`를 불러 frame이 오지 않아도 기한을 지킨다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> deadline() const noexcept;

    private:
        std::optional<focus_request> pending_ {};
        std::chrono::steady_clock::time_point deadline_ {};
        // 받은 마지막 번호다. 같은 번호의 재게시를 다시 받지 않는다.
        std::uint64_t last_id_ { 0 };
    };
}

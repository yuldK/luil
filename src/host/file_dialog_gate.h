#pragma once

#include "luil/app/file_dialog.h"

#include <cstdint>
#include <optional>
#include <unordered_set>

namespace luil {
    // OS modal loop가 재진입해도 같은 요청이나 다른 요청을 겹쳐 열지 않는다.
    class file_dialog_gate
    {
    public:
        // id 0은 기본값 그대로 둔 요청이다. 한 번 열면 그 뒤의 0은 모두 중복으로 버려져 결과 없이 사라지므로 열지 않는다.
        [[nodiscard]] bool try_begin(const std::uint64_t id)
        {
            if (id == 0)
                return false;
            zero_reported_ = false;
            if (active_ || seen_.contains(id))
                return false;
            seen_.insert(id);
            active_ = true;
            return true;
        }

        // id 0 요청이면 열지 않고 돌려줄 `failed` 결과다. host는 try_begin보다 먼저 묻고 결과를 게시한다.
        //  - frame은 게시마다 다시 확인되므로 같은 요청이 남아 있는 동안 한 번만 알린다. 매번 알리면
        //    결과 메시지가 새 frame을 낳고 그 frame이 다시 실패를 낳는 고리가 된다.
        //  - 요청이 빠지거나(idle) 다른 id가 오면 다음 id 0을 다시 알린다.
        [[nodiscard]] std::optional<file_dialog_result> rejection(const file_dialog_request& request)
        {
            if (request.id != 0 || active_ || zero_reported_)
                return std::nullopt;
            zero_reported_ = true;
            return file_dialog_result { .id = 0, .status = file_dialog_status::failed, .error = u8"The file dialog request needs a nonzero id" };
        }

        // frame에 요청이 없다.
        void idle() noexcept
        {
            zero_reported_ = false;
        }

        void finish() noexcept
        {
            active_ = false;
        }

    private:
        std::unordered_set<std::uint64_t> seen_ {};
        bool active_ { false };
        bool zero_reported_ { false };
    };
} // namespace luil

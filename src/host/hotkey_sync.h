#pragma once

#include "luil/app/global_hotkey.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace luil {
    // 전역 단축키 하나를 걸어 본 결과다 (docs/global-hotkey-design.md). `win32::hotkey_status`가 같은 이름과 차례로 옮긴다.
    enum class hotkey_state
    {
        registered,
        conflict,
        reserved,
        needs_modifier,
        invalid_key,
        invalid_id,
        failed,
    };

    // 걸기 전에 라이브러리가 거르는 조합인가다. OS를 부르지 않고 `invalid_key`·`reserved`·`needs_modifier` 가운데 하나를
    // 돌려주고, 걸어 볼 만하면 빈 값이다.
    //  - 가상 키로 옮긴 뒤에 가른다. 플랫폼 대역의 값으로 쓴 F12나 Alt+F4도 같은 조합이다.
    //  - 순서: 키가 없다 → 예약 → 수정자가 필요하다. 먼저 맞는 것이 답이다.
    [[nodiscard]] std::optional<hotkey_state> classify_hotkey_chord(const key_chord& chord) noexcept;

    enum class hotkey_call_kind
    {
        // `RegisterHotKey`다.
        add,
        // `UnregisterHotKey`다.
        remove,
    };

    // 플랫폼이 낼 호출 하나다.
    struct hotkey_call
    {
        hotkey_call_kind kind { hotkey_call_kind::add };
        // OS에 알리는 번호다 (1..0xBFFF). 눌림(`WM_HOTKEY`)의 `wParam`으로 돌아온다.
        int number { 0 };
        std::u8string id {};
        // 더하기만 쓴다. 조합과 그 가상 키다.
        key_chord chord {};
        std::uint32_t virtual_key { 0 };
    };

    // 더하기 호출을 낸 결과다.
    enum class hotkey_call_outcome
    {
        succeeded,
        // 이미 다른 프로그램이 쓰는 조합이다 (`ERROR_HOTKEY_ALREADY_REGISTERED`).
        conflict,
        failed,
    };

    // 항목 하나의 시도 결과다. 알릴 것만 담긴다.
    struct hotkey_decision
    {
        std::u8string id {};
        hotkey_state state { hotkey_state::registered };
        // `failed`일 때의 영문 진단이다.
        std::u8string error {};

        [[nodiscard]] bool operator==(const hotkey_decision&) const = default;
    };

    struct hotkey_plan
    {
        // 풀기가 모두 먼저고 걸기가 나중이다. 플랫폼은 순서대로 내고 더하기마다 `completed`로 결과를 되돌린다.
        std::vector<hotkey_call> calls {};
        // OS를 부르지 않고 정해진 결과 가운데 알릴 것이다.
        std::vector<hotkey_decision> decisions {};
    };

    // 전역 단축키 목록의 바라는 상태와 OS에 건 상태를 견줘 할 호출을 고른다. OS를 모른다.
    //  - 작업 표시줄(`taskbar_sync`)·알림 영역(`tray_sync`)과 같은 틀이다. 호출 결과가 다음 할 일을 바꾸므로(걸지 못한 항목은
    //    요청이 같으면 다시 걸지 않는다) 플랫폼은 더하기마다 `completed`로 결과를 곧바로, 다음 `update` 전에 되돌린다.
    //  - 견주는 값은 id와 조합이다. `pressed`는 견주지 않는다.
    //  - 항목의 결과는 시도가 바뀔 때만 알린다. 처음 시도, 조합을 바꾼 시도, 다시 걸어 결과가 달라진 때다.
    class hotkey_sync
    {
    public:
        // OS 번호의 끝이다. `RegisterHotKey`의 앱 번호는 0..0xBFFF이고 0은 쓰지 않는다.
        static constexpr int max_number { 0xBFFF };

        // `last_number`는 번호를 매기는 범위의 끝이다. test가 작은 값으로 끝을 돌려 본다.
        explicit hotkey_sync(int last_number = max_number) noexcept;

        // 바라는 목록을 마지막으로 적용한 것과 견준다. 키 잡기 중이면 건 것을 모두 풀고(알리지 않는다) 끝나면 다시 건다.
        //  - 한 번의 대조는 풀기를 모두 한 뒤에 건다. 두 id가 조합을 맞바꾸는 frame이 서로 막지 않는다.
        //  - 같은 목록 안의 같은 조합은 앞 항목만 걸고 뒤 항목은 OS를 부르지 않고 `conflict`다.
        [[nodiscard]] hotkey_plan update(std::span<const global_hotkey> wanted, bool key_capture);

        // 더하기 호출 하나를 낸 결과다. 알릴 결과가 정해졌으면 돌려준다. `error`는 `failed`에 싣는 영문 진단이다.
        [[nodiscard]] std::optional<hotkey_decision> completed(const hotkey_call& call, hotkey_call_outcome outcome, std::u8string error = {});

        // 주 창이 활성화됐다. 다음 `update`가 `conflict`·`failed` 항목을 한 번 다시 건다 (같은 요청으로는 그때뿐이다).
        void reactivated() noexcept;

        // 끝날 때 건 것을 모두 푸는 호출이다. 장부도 비운다.
        [[nodiscard]] std::vector<hotkey_call> release_all();

        // 눌림의 번호가 가리키는 id다. 장부에 없는 번호(풀린 것, 시스템 번호)는 빈 값이다.
        [[nodiscard]] std::optional<std::u8string> id_of(std::uintptr_t number) const;

    private:
        struct record
        {
            std::u8string id {};
            // 같은 id가 목록에 여럿일 때 몇째인가다 (0이 처음). 겹친 id의 항목도 항목끼리 알아보기 위한 값이다.
            std::size_t occurrence { 0 };
            key_chord chord {};
            // 마지막으로 알린 결과다. `reported`가 거짓이면 아직 알린 적이 없다.
            hotkey_state state { hotkey_state::registered };
            bool reported { false };
            // `state`가 OS를 불러서 얻은 답인가다. 거짓이면 라이브러리가 걸러 정한 것이다.
            bool from_os { false };
            // OS에 건 번호다. 걸려 있지 않으면 0이다.
            int number { 0 };
        };

        [[nodiscard]] bool unchanged(std::span<const global_hotkey> wanted, bool key_capture) const noexcept;
        [[nodiscard]] bool wants_attempt(const record& item) const noexcept;
        [[nodiscard]] bool number_in_use(int number, const std::vector<record>& pending) const noexcept;
        [[nodiscard]] int allocate(const std::vector<record>& pending);

        std::vector<record> records_ {};
        // 마지막 `update`가 본 목록이다. 같으면 대조를 건너뛴다.
        std::vector<std::pair<std::u8string, key_chord>> last_wanted_ {};
        bool last_capture_ { false };
        // `reactivated()`가 켜고 키 잡기 밖의 `update`가 끈다.
        bool retry_ { false };
        // 번호는 1부터 차례로 매기고 풀어도 다시 쓰지 않는다. 끝 다음에야 1로 돌아가 살아 있는 번호를 건너뛴다.
        int next_number_ { 1 };
        int last_number_ { max_number };
    };
}

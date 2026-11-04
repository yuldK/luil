#pragma once

#include "luil/ui/app_message.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 앱 수준 실행 취소 stack이다. 앱이 한 번의 사용자 동작(항목 삭제, 체크, 순서 바꿈)을 되돌리고 다시 실행하는 기록을 든다.
    // 칸 하나의 글만 되돌리는 `text_edit_state`의 기록과 따로 간다.
    //
    // 한 걸음은 앱이 정한 `app_message` 쌍(되돌림, 다시 실행)이고, stack은 메시지를 열어 보지도 앱 상태를 바꾸지도 않는다.
    // 되돌림 메시지를 `apply`로 받은 앱이 자기 `handle`의 길로 상태를 바꾼다 (docs/undo-stack-design.md).
    inline constexpr std::size_t undo_stack_default_steps { 1000 };
    // 칸 기록의 상한(`text_edit_history_byte_limit`)과 같다.
    inline constexpr std::size_t undo_stack_default_bytes { std::size_t { 32 } * 1024 * 1024 };

    struct undo_step
    {
        // 메뉴에 보일 걸음의 이름이다 ("항목 삭제"). 글은 앱의 말이다.
        std::u8string label {};
        // 되돌릴 때와 다시 실행할 때 앱이 적용할 메시지다. 둘 다 비어 있지 않아야 쌓인다.
        //  - 메시지는 절대 값을 담는다. 합친 걸음은 앞 걸음의 undo와 뒤 걸음의 redo만 남기므로, "다음 값으로 토글"이 아니라
        //    "이 값으로 정함"이라야 중간 걸음이 없어도 맞다.
        app_message undo {};
        app_message redo {};
        // 비어 있지 않고 바로 앞에 쌓은 걸음의 키와 같으면 한 걸음으로 합친다. 같은 대상을 절대 값으로 바꾸는 걸음끼리만 같은 키를 쓴다.
        std::u8string merge_key {};
        // 걸음이 쥔 바이트의 어림이다. 메시지 안을 볼 수 없어 앱이 알린다. 0이면 걸음 수만 센다.
        std::size_t weight { 0 };
    };

    struct undo_stack_limits
    {
        // 0이면 제한이 없다.
        std::size_t steps { undo_stack_default_steps };
        // 0이면 제한이 없다.
        std::size_t bytes { undo_stack_default_bytes };
    };

    // 되돌리거나 다시 실행하는 메시지를 받아 적용하는 앱의 함수다. 앱의 `handle`을 그대로 부르면 된다.
    using undo_apply = std::function<void(const app_message& message)>;

    // logic thread 전용이다. 스레드 안전하지 않다.
    //
    // **재생 중에는 쌓지 않는다.** `undo`·`redo`가 `apply`를 부르는 동안 stack은 재생 중이다.
    //  - 이때 `push`·`end`·`undo`·`redo`는 거짓을 돌려주고 `begin`·`set_clean`·`break_merge`·`clear`·`set_limits`는 아무 일도 하지 않는다.
    //    질의는 된다. 위치는 `apply`를 부르기 전에 이미 옮겨 둔다.
    //  - 앱이 재생 메시지를 사용자 동작과 같은 타입으로 만들었거나 기록하는 `handle`에 그대로 흘려도, 그 `handle`이 낸 `push`가
    //    무시되어 redo가 지워지거나 걸음이 두 번 쌓이지 않는다.
    class undo_stack
    {
    public:
        explicit undo_stack(undo_stack_limits limits = {});

        // 걸음을 쌓고 redo를 비운다. 합쳐도 참이다. 메시지가 비었거나 재생 중이면 거짓이고 바뀌는 것이 없다.
        //  - 묶음이 열려 있으면 묶음에 담긴다. redo는 묶음을 확정할 때 비운다.
        bool push(undo_step step);

        // `begin`과 `end` 사이의 `push`는 한 걸음이 된다. 중첩할 수 있고 바깥 `end`에서 확정한다. 이름은 바깥 `begin`의 것이다.
        //  - 열린 동안 `can_undo`·`can_redo`·`clean`은 거짓이고 `undo`·`redo`·`set_clean`은 아무 일도 하지 않는다.
        //  - 빈 묶음은 걸음을 만들지 않고 redo도 비우지 않는다.
        void begin(std::u8string label);
        // 짝 없는 `end`는 거짓이다.
        bool end();

        // 한 걸음을 되돌리거나 다시 실행한다. 걸음 안의 메시지를 `undo`는 거꾸로, `redo`는 쌓은 차례로 `apply`에 넘긴다.
        // `apply`가 비었거나 옮길 걸음이 없거나 묶음이 열려 있으면 거짓이고 바뀌는 것이 없다.
        bool undo(const undo_apply& apply);
        bool redo(const undo_apply& apply);

        [[nodiscard]] bool can_undo() const noexcept;
        [[nodiscard]] bool can_redo() const noexcept;
        // 다음에 되돌릴(다시 실행할) 걸음의 이름이다. 없으면 빈 글이다.
        [[nodiscard]] std::u8string_view undo_label() const noexcept;
        [[nodiscard]] std::u8string_view redo_label() const noexcept;
        [[nodiscard]] std::size_t undo_count() const noexcept;
        [[nodiscard]] std::size_t redo_count() const noexcept;
        // 되돌릴 수 있는 걸음의 `weight` 합이다. `undo_stack_limits::bytes`가 견주는 값이고 redo 쪽은 세지 않는다.
        [[nodiscard]] std::size_t bytes() const noexcept;

        // 다음 `push`는 합치지 않는다. 한 번의 끌기가 끝났을 때 앱이 부른다.
        void break_merge() noexcept;
        // 지금 자리를 저장 지점으로 삼는다.
        void set_clean() noexcept;
        // 지금 자리가 저장 지점이면 참이다. 앱의 "수정됨" 표시가 이것으로 선다.
        //  - 저장 지점이 버려진 redo 쪽에 있었거나 맨 아래 걸음이 상한으로 버려져 걸음 밖으로 나가면 `set_clean`을 다시 부를 때까지 거짓이다.
        [[nodiscard]] bool clean() const noexcept;
        // 걸음을 모두 버린다. 열린 묶음도 버리고, 빈 자리가 저장 지점이다.
        //  - 파일을 다시 열거나 자동 갱신이 상태를 밖에서 바꾸면 걸음의 메시지가 가리킬 대상이 사라지므로 앱이 부른다.
        void clear() noexcept;
        // 상한을 바꾸고 되돌릴 쪽을 새 상한에 맞춘다. redo 쪽은 줄이지 않는다.
        void set_limits(undo_stack_limits limits);

    private:
        // 걸음 하나의 속살이다. 묶음과 합친 걸음이 같은 모양이도록 메시지를 목록으로 든다 (쌓은 차례).
        struct record
        {
            std::u8string label {};
            std::vector<app_message> undo {};
            std::vector<app_message> redo {};
            std::u8string merge_key {};
            std::size_t weight { 0 };
        };

        // redo를 비우고 되돌릴 쪽 끝에 쌓은 뒤 상한에 맞춘다. 합치기는 끊긴다.
        void append(record entry);
        void discard_redo() noexcept;
        // 가장 오래된 걸음부터 상한에 맞춘다. 가장 새 걸음은 상한보다 커도 남겨 한 번은 되돌린다.
        void trim();

        undo_stack_limits limits_ {};
        // 오래된 걸음이 앞이다. 앞에서 버리는 일이 잦아 deque다.
        std::deque<record> undo_ {};
        // 가장 먼저 다시 실행할 걸음이 뒤다.
        std::vector<record> redo_ {};
        std::size_t bytes_ { 0 };
        // 저장 지점이 찍힌 자리다 (되돌릴 수 있는 걸음 수). 없으면 저장 지점이 사라진 것이다.
        //  - redo 쪽에 있으면 `undo_.size()`보다 크다.
        std::optional<std::size_t> clean_ { 0 };
        // 바로 앞 `push`가 키가 있는 걸음이어서 다음 `push`가 합쳐질 수 있다.
        bool merge_armed_ { false };
        std::size_t macro_depth_ { 0 };
        record macro_ {};
        bool replaying_ { false };
    };

    // `begin`/`end`의 범위 짝이다. 복사·이동할 수 없다.
    class undo_macro
    {
    public:
        undo_macro(undo_stack& stack, std::u8string label);
        ~undo_macro();
        undo_macro(const undo_macro&) = delete;
        undo_macro& operator=(const undo_macro&) = delete;

    private:
        undo_stack& stack_;
    };
}

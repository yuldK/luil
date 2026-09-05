#pragma once

// luil 최소 앱의 "앱 쪽 절반"이다.
//
// 라이브러리와 앱의 경계는 세 조각이다.
//   1. logic_driver       — 앱 상태를 소유하고, 메시지를 처리하고, 화면(frame)을 게시한다.
//   2. interaction_policy — 입력(휠·키·텍스트)을 앱 메시지로 바꾸는 정책이다.
//                           이 예제는 텍스트 입력이 없어 기본 구현(아무 일도 안 함)이면 충분하다.
//   3. window_delegate    — 창 크기·DPI 변화 같은 UI thread의 사건을 앱 메시지로 바꾼다.
//
// 세 조각 모두 "상태를 직접 바꾸지 않고 메시지만 만든다"가 규칙이다.
// 상태는 언제나 logic thread의 driver가 소유하고, 다른 스레드는 메시지를 보낼 뿐이다.

#include "luil/luil.h"

#include <atomic>
#include <memory>

namespace hello {
    // --- 앱이 정의하는 element 종류(kind) ---
    //
    // 라이브러리는 element를 kind(정수)로만 구분하고 값을 해석하지 않는다.
    // 낮은 대역은 라이브러리가 예약하고, 앱은 application_element_kind(index)로
    // 자기 대역에서 정의한다. 같은 kind가 여럿이면 owner 문자열로 구분한다.
    constexpr luil::ui_element_kind kind_label { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(1) };

    // --- 앱 메시지(intent) ---
    //
    // 앱 메시지는 평범한 구조체다. 라이브러리의 불투명 운반체 app_message에
    // 담겨 스레드를 건너고, 받는 쪽(driver::handle)이 get<T>()로 복원한다.
    // 라이브러리는 이 타입들을 전혀 모른다.

    // 버튼을 눌렀다.
    struct increment_intent
    {};

    // 창 크기·배율이 정해졌거나 바뀌었다 (delegate가 보낸다).
    struct metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    // 창을 닫는다 (캡션 X, Alt+F4).
    struct close_intent
    {};

    // --- logic thread: 앱 상태와 화면 ---
    //
    // 모든 메서드는 라이브러리가 만든 logic thread에서 불린다.
    // 예외는 둘뿐이다: make_close_message·shutdown_completed는 종료 절차 중
    // UI thread에서도 불리므로 atomic으로 지킨다.
    class hello_driver final : public luil::win32::logic_driver
    {
    public:
        // app inbox에 든 메시지 1건을 처리한다.
        // 여기서 앱 상태를 바꾼다 — 상태를 바꾸는 곳은 이 함수 하나뿐이다.
        void handle(luil::app_message message) override;

        // 처리 후 게시할 화면이다.
        // element tree를 매번 새로 만들어 불변 값으로 게시한다.
        // (tree는 만들고 나면 아무도 고치지 않으므로 세 스레드가 lock 없이 읽는다.)
        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override;

        // 종료 신호로 쓸 메시지다. 창이 닫힐 때 라이브러리가 inbox에 넣는다.
        [[nodiscard]] luil::app_message make_close_message() override;

        // 종료 신호를 처리했는지다. 라이브러리가 스레드를 정리하기 전에 기다린다.
        [[nodiscard]] bool shutdown_completed() const override;

    private:
        // 창의 실제 크기·배율이다. metrics 메시지가 채운다.
        // 첫 frame(메시지가 오기 전)은 기본값으로 배치한다.
        metrics_intent metrics_ {};
        int clicks_ { 0 };
        std::atomic<bool> closed_ { false };
    };

    // --- UI thread: 창 사건을 메시지로 ---
    class hello_delegate final : public luil::win32::window_delegate
    {
    public:
        // 창 크기·DPI가 바뀌면 라이브러리가 부른다.
        // 반환한 메시지가 app inbox로 들어가 driver::handle에 닿는다.
        [[nodiscard]] luil::app_message make_window_metrics_message(float width, float height, float scale) override;
    };
} // namespace hello

#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_events.h"

namespace luil {
    // 앱이 보이는 상태다. 플랫폼의 수명 주기를 앱이 알아야 하는 둘로 줄인 것이다.
    enum class app_lifecycle
    {
        // 사용자에게 보인다 (Android `onStart`).
        foreground,
        // 사용자에게서 사라졌다 (Android `onStop`).
        // **저장할 마지막 기회일 수 있다.** 모바일 OS는 이 뒤에 종료 알림 없이 프로세스를
        // 거둘 수 있어, 종료 신호(`logic_driver::make_close_message`)만 믿을 수 없다.
        background,
    };

    // 앱이 UI thread에 꽂는 훅이다. 플랫폼을 가리지 않는 부분이다.
    // 모든 메서드는 UI thread에서 불리고 기본 구현은 "아무 일도 하지 않음"이다.
    // 플랫폼은 이것을 넓혀 자기 일을 더한다 (Win32는 `win32::window_delegate`).
    class app_delegate
    {
    public:
        app_delegate() = default;
        app_delegate(const app_delegate&) = delete;
        app_delegate(app_delegate&&) = delete;
        app_delegate& operator=(const app_delegate&) = delete;
        app_delegate& operator=(app_delegate&&) = delete;
        virtual ~app_delegate() = default;

        // 창(표면)과 host가 준비된 직후다 (이벤트 루프 전).
        // 시작 메시지(글꼴 목록, 시작 문서 열기 등)를 여기서 게시한다.
        virtual void on_started(app_host& host)
        {
            static_cast<void>(host);
        }

        // input thread가 요청한 앱 UI 명령이다 (파일 dialog, shell 실행 등).
        virtual void execute_app_ui_command(app_host& host, const app_ui_command& command)
        {
            static_cast<void>(host);
            static_cast<void>(command);
        }

        // 창 크기·배율이 바뀌었다.
        // 반환 메시지는 app inbox로 간다.
        // 빈 메시지는 게시하지 않는다.
        [[nodiscard]] virtual app_message make_window_metrics_message(float width, float height, float scale)
        {
            static_cast<void>(width);
            static_cast<void>(height);
            static_cast<void>(scale);
            return {};
        }

        // 앱이 보이거나 사라졌다.
        // 반환 메시지는 app inbox로 간다. 빈 메시지는 게시하지 않는다.
        //  - 데스크톱 창(Win32)은 지금 이것을 부르지 않는다. 창이 닫히기 전까지 프로세스가
        //    살아 있고 종료 신호가 저장을 맡기 때문이다.
        [[nodiscard]] virtual app_message make_lifecycle_message(app_lifecycle lifecycle)
        {
            static_cast<void>(lifecycle);
            return {};
        }
    };
} // namespace luil

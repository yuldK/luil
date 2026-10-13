#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/app_message.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"
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

    // 창(표면) 하나의 크기·배율과 폭 등급이다.
    struct window_metrics
    {
        // 앱이 배치할 자리의 크기다 (물리 픽셀). Android는 시스템 막대·컷아웃을 뺀 안전 영역이다.
        float width { 0.0f };
        float height { 0.0f };
        // 물리 픽셀 / 논리 픽셀이다.
        float scale { 1.0f };
        // 논리 폭(`width / scale`)의 등급이다 (기본 경계 600·840).
        window_width_class width_class { window_width_class::compact };
        // 앱의 자리에서 표면 끝까지의 거리다 (물리 픽셀). Android는 안전 영역 밖(상태 표시줄·제스처 막대·컷아웃)이고
        // Windows는 0이다. scrim·시트 바탕을 화면 끝까지 칠할 때 쓴다 (`window_surface`).
        edge_insets outside {};
        // 시스템이 키운 12sp 글자의 dp / 12다. Android 비선형 변환의 대표값이다.
        // 실제 크기는 scaled_font_size로 조회한다. 글자 설정만 바뀌어도 metrics를 다시 보낸다.
        float text_scale { 1.0f };

        [[nodiscard]] bool operator==(const window_metrics&) const noexcept = default;
    };

    // 크기와 배율로 metrics를 짓는다. 폭 등급은 기본 경계로 매긴다. 배율이 0 이하면 1로 본다.
    [[nodiscard]] window_metrics make_window_metrics(float width, float height, float scale) noexcept;

    // root를 (0, 0)에서 `width × height`로 배치할 때, 그릴 수 있는 표면 전체를 tree 좌표로 본 사각형이다. 배치 문맥의
    // `surface`로 넘기면 scrim과 시트 바탕이 안전 영역 밖까지 칠해진다. `outside`가 0이면 root 자리와 같다.
    [[nodiscard]] rect_f window_surface(const window_metrics& metrics) noexcept;

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

        // 창 크기·배율이 바뀌었다 — 폭 등급을 함께 받는 짝이다. host는 이것을 부른다.
        // 기본은 위의 세 값짜리를 부르므로, 폭 등급이 필요한 앱만 이것을 넓힌다. 이름이 다른 것은 기존 앱이 세
        // 값짜리만 넓혔을 때 이것을 가리지 않게 하려는 것이다 (같은 이름의 가상 함수 하나만 넓히면 나머지가 가려진다).
        [[nodiscard]] virtual app_message make_metrics_message(const window_metrics& metrics)
        {
            return make_window_metrics_message(metrics.width, metrics.height, metrics.scale);
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

        // 클립보드의 파일 목록을 읽었다 (`clipboard_files_paste_request`의 답). 반환 메시지는 app inbox로 간다.
        // 빈 메시지는 게시하지 않는다. 파일이 없거나 플랫폼이 다루지 않아도(`supported` 거짓) 답은 온다.
        [[nodiscard]] virtual app_message make_file_clipboard_message(const file_clipboard_content& content)
        {
            static_cast<void>(content);
            return {};
        }

        // 클립보드의 내용이 바뀌었다 (이 앱이 넣은 것도 포함). 반환 메시지는 app inbox로 간다.
        // 빈 메시지는 게시하지 않는다. 지금은 Win32만 알린다 (`WM_CLIPBOARDUPDATE`).
        [[nodiscard]] virtual app_message make_clipboard_changed_message(const clipboard_change& change)
        {
            static_cast<void>(change);
            return {};
        }

        // 물은 권한에 사용자가 답했다 (`app_host::request_permission`). 반환 메시지는 app inbox로 간다.
        // 빈 메시지는 게시하지 않는다. 거절도 답이다 — `denied`로 온다.
        [[nodiscard]] virtual app_message make_permission_message(app_permission permission, permission_state state)
        {
            static_cast<void>(permission);
            static_cast<void>(state);
            return {};
        }
    };
} // namespace luil

#pragma once

namespace luil {
    // 앱이 도는 화면의 형태다.
    enum class ui_form_factor
    {
        // 창을 띄우고 마우스·키보드가 기본인 데스크톱이다 (Windows).
        desktop,
        // 화면 하나를 앱이 덮고 터치가 기본인 휴대폰·태블릿이다 (Android).
        mobile,
    };

    // 플랫폼이 실제로 해 주는 기기 기능이다 (device-request-design.md).
    // 앱의 요청(`ui_frame::device`)은 어느 플랫폼에서나 실을 수 있고, 하지 못하는 플랫폼은 받기만 하고
    // 아무 일도 하지 않는다. 앱은 이것으로 그 기능의 설정 항목을 화면에 둘지 고른다.
    //  - 기본값이 전부 거짓이다. 플랫폼 host가 할 수 있는 것만 켠다.
    struct ui_platform_capabilities
    {
        // 화면 방향을 요청할 수 있다 (`device_request::orientation`).
        bool screen_orientation { false };
        // 시스템 막대를 숨기고 컷아웃 밑까지 그릴 수 있다 (`device_request::system_bars`, `draw_under_cutout`).
        bool system_bars { false };
        // 볼륨 키를 앱이 받을 수 있다 (`device_request::volume_keys`).
        bool volume_keys { false };
        // 햅틱을 낼 수 있다 (`haptic_request`, `app_host::perform_haptic`). 기기에 진동 장치가 없거나 사용자가
        // 껐으면 요청해도 조용하다.
        bool haptics { false };

        [[nodiscard]] bool operator==(const ui_platform_capabilities&) const noexcept = default;
    };

    // 플랫폼이 UI에 알리는 성질이다.
    // 앱 host가 시작할 때 한 번 정하고, element와 앱이 읽는다. 앱은 이것으로 화면의 모양을
    // 고른다 — 데스크톱은 caption, 모바일은 앱 바 (app_bar_element).
    struct ui_platform
    {
        ui_form_factor form_factor { ui_form_factor::desktop };
        // 앱이 창의 caption(제목·최소화·최대화·닫기)을 직접 그리는가다.
        // 거짓이면 시스템이 상태 표시줄을 그리고 창 버튼이 없다. 그때 `caption_element`는
        // 자리를 차지하지 않는다 — 앱이 잊고 넣어도 모바일 화면에 데스크톱 chrome이 서지 않는다.
        bool window_caption { true };
        ui_platform_capabilities capabilities {};

        [[nodiscard]] bool operator==(const ui_platform&) const noexcept = default;
    };

    // 앱 host가 시작할 때 정한다. 정하지 않으면 데스크톱이다.
    //  - logic thread가 첫 frame을 짓기 전에 정해야 한다. host는 스레드를 세우기 전에 부른다.
    void set_ui_platform(const ui_platform& platform) noexcept;

    // 지금 플랫폼의 성질이다. 어느 thread에서 불러도 된다.
    [[nodiscard]] ui_platform current_ui_platform() noexcept;
} // namespace luil

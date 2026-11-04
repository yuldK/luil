#pragma once

#include <optional>
#include <string>
#include <string_view>

// Windows 토스트 XML을 짓는 순수 함수다 (os-notification-design.md의 "토스트 XML"). OS를 부르지 않는다.
//  - 글은 `prepare_notification_text`가 이미 거르고 자른 것을 받는다.
namespace luil::win32 {
    // 토스트의 group이다. 이 앱이 올린 것을 tag(id)와 함께 가린다.
    inline constexpr std::wstring_view toast_group { L"luil" };

    // 활성화 인자 앞머리다. `launch` 속성과 COM 활성화기의 `invokedArgs`가 이것으로 시작한다.
    inline constexpr std::u8string_view toast_launch_prefix { u8"luil:" };

    struct toast_payload
    {
        std::u8string_view id {};
        std::u8string_view title {};
        std::u8string_view body {};
        // 그림 파일의 절대 경로다 (`C:\...`). 비면 그림이 없다.
        std::u8string_view image_path {};
        bool silent { false };
    };

    // XML 글과 속성 값으로 넣을 글이다. `& < > " '`는 개체 참조로 바꾸고, XML 1.0이 받지 않는 제어 문자
    // (탭·줄바꿈·복귀 밖의 U+0000~U+001F)는 뺀다.
    [[nodiscard]] std::u8string escape_toast_text(std::u8string_view text);

    // 그림 경로를 `file:///` 주소로 옮긴다. 백슬래시는 `/`로 바꾸고 공백·`#`·`%`를 백분율 부호화한다.
    [[nodiscard]] std::u8string toast_image_url(std::u8string_view path);

    // `luil:<id>`다.
    [[nodiscard]] std::u8string toast_launch_argument(std::u8string_view id);

    // 활성화 인자에서 id를 꺼낸다. `luil:`로 시작하지 않거나 id가 규칙 밖이면 빈 값이다.
    [[nodiscard]] std::optional<std::u8string> toast_id_from_launch_argument(std::u8string_view argument);

    // `ToastGeneric` 템플릿의 토스트 XML이다. `<text>` 둘(본문이 비면 하나), 그림이 있으면 `appLogoOverride`,
    // 무음이면 `<audio silent="true"/>`다.
    [[nodiscard]] std::u8string make_toast_xml(const toast_payload& payload);
}

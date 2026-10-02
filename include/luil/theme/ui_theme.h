#pragma once

#include "luil/theme/appearance.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace luil {
    using ui_color = std::uint32_t;

    enum class color_theme
    {
        dark,
        // 밝은 바탕이다.
        // 설정의 선호나 OS 설정이 고른다.
        light,
        high_contrast,
    };

    [[nodiscard]] constexpr ui_color make_ui_color(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue, const std::uint8_t alpha = 255) noexcept
    {
        return static_cast<ui_color>(alpha) << 24U | static_cast<ui_color>(red) << 16U | static_cast<ui_color>(green) << 8U | static_cast<ui_color>(blue);
    }

    // 색의 알파만 바꾼다 (0~1, 범위 밖은 잘라 낸다).
    // 팔레트 합성이 accent 위에 tone을 얹는 자리이자 그리기가 역할색을 겹치는 자리다 —
    // 같은 함수라야 "합성이 만든 색"과 "그리기가 만든 색"이 같은 바이트다.
    [[nodiscard]] constexpr ui_color with_alpha(const ui_color color, const float alpha) noexcept
    {
        const float clamped { alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha) };
        const auto value { static_cast<std::uint32_t>(clamped * 255.0f + 0.5f) };
        return (color & 0x00ffffffU) | (value << 24U);
    }

    // sRGB 상대 밝기다 (0=검정, 1=흰색). 알파는 보지 않는다.
    // 바탕이 어두운지 밝은지를 **색 자체로** 판정한다 — 앱이 세운 중립 팔레트는
    // "dark 테마"라는 이름과 무관하게 밝을 수 있어, 이름이 아니라 값이 답이어야 한다.
    [[nodiscard]] float relative_luminance(ui_color color) noexcept;
    // 그 바탕 위에는 밝은 글자가 맞는가다 (창 테두리·OS 그림의 명암을 고를 때).
    [[nodiscard]] bool is_dark_background(ui_color color) noexcept;

    struct caption_color_palette
    {
        ui_color background { 0 };
        ui_color foreground { 0 };
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color close_button_hover_background { 0 };
        ui_color close_button_hover_foreground { 0 };

        [[nodiscard]] bool operator==(const caption_color_palette&) const noexcept = default;
    };

    // 키 컬러 하나가 한 테마에서 맡는 네 역할이다.
    // 값은 assets/accents.json에서 빌드 시점에 들어온다.
    struct accent_color_set
    {
        // 채움·테두리·상태 표시의 기본 색이다.
        ui_color accent { 0 };
        // 위 요소의 hover다.
        ui_color hover { 0 };
        // 낮은 알파로 겹치는 옅은 강조 바탕이다 (배지·토글 트랙·선택 행).
        ui_color soft { 0 };
        // 바탕 위 강조 글자다 (설정 행 제목·강조 버튼 라벨).
        ui_color emphasis_foreground { 0 };

        [[nodiscard]] bool operator==(const accent_color_set&) const noexcept = default;
    };

    // 고를 수 있는 키 컬러 하나다.
    // 문자열은 생성 표를 가리키는 정적 수명이다.
    struct accent_definition
    {
        std::u8string_view id {};
        std::u8string_view label {};
        // 설정의 색 동그라미다.
        // 테마와 무관한 대표색이다.
        ui_color swatch { 0 };
        accent_color_set dark {};
        accent_color_set light {};

        [[nodiscard]] const accent_color_set& for_theme(color_theme theme) const noexcept;
    };

    // 앱 설정이 모르는 id를 담고 있을 때 물러설 기본 키 컬러다.
    // default_accent_id는 LUIL_DEFAULT_ACCENT_ID로 구성하며 appearance.h가 제공한다.

    // 빌드 시점에 내장된 키 컬러 목록이다.
    // 순서는 JSON 그대로다.
    [[nodiscard]] std::span<const accent_definition> accent_catalog() noexcept;
    // id로 찾는다.
    // 없으면 기본 키 컬러다.
    //  - 저장된 값은 지우지 않는다.
    // 참조가 아니라 값이다 — 참조는 답의 수명을 정적 표에 묶는 계약이라,
    // 표 밖에서 합성되는 항목이 설 수 없다.
    [[nodiscard]] accent_definition accent_for(std::u8string_view id) noexcept;
    // 목록에 그 id가 있는지다.
    // 설정을 읽을 때 경고를 남길지 판정한다.
    [[nodiscard]] bool accent_exists(std::u8string_view id) noexcept;

    // 실행 시점 항목이 쓰는 예약 id다.
    // 정적 목록(assets/accents.json)은 이 id를 가질 수 없다 — 생성이 거른다.
    inline constexpr std::u8string_view system_accent_id { u8"system" };

    // OS가 알려 준 키 컬러 하나로 시스템 accent 항목을 합성한다.
    // 입력의 알파는 무시하고 역할은 전부 불투명이다.
    // 내장 표와 같은 OKLCH 자리(고정 밝기 사다리·색상 보존·채도 상한)에 세우므로
    // 정적 항목들 곁에 두어도 다른 규칙으로 만든 색처럼 보이지 않는다.
    [[nodiscard]] accent_definition make_system_accent(ui_color key) noexcept;

    // 실행 시점 항목을 세우거나 거둔다.
    // platform이 OS accent를 읽어 부른다. 빈 값은 "OS가 답하지 않았다"이고 항목이 내려간다.
    // 어느 스레드에서든 안전하다 — 키 하나만 원자로 바꾸고, 조회가 그때마다 합성한다.
    //  - 항목을 통째로 저장하면 읽는 중에 바뀌는 자리가 생긴다.
    void set_system_accent(std::optional<ui_color> key) noexcept;
    // 실행 시점 항목이다. 서 있지 않으면 빈 값이다.
    // 정적 목록(accent_catalog)에는 넣지 않는다 — span은 정적 수명·연속 배열의
    // 계약이라 실행 시점 항목이 낄 수 없다. 목록을 보여 주는 쪽이 곁에 세운다.
    [[nodiscard]] std::optional<accent_definition> system_accent() noexcept;

    // 키 컬러와 무관한 한 테마의 중립 색이다.
    // 값은 assets/style.json에서 빌드 시점에 들어오고(`default_ui_style`), 앱은
    // 자기 값으로 세운 것을 `ui_style`에 실어 같은 자리에 둘 수 있다 (ui_style.h).
    //
    // 파생 역할(secondary·disabled·divider·input·control_border·group_border)은
    // **전경색의 낮은 알파**로 두는 것이 규약이다. element가 알파를 발명하지 않고
    // 역할 하나를 쓰므로, 바탕과 전경만 뒤집어도 그리기 코드가 그대로 성립한다.
    // 규약이지 강제는 아니다 — 앱이 불투명한 회색을 넣어도 합성은 그대로 지나간다.
    struct neutral_color_palette
    {
        ui_color window_background { 0 };
        ui_color surface_background { 0 };
        ui_color primary_foreground { 0 };
        // 흐린 보조 글자다 (아이콘·설명·머리행 제목).
        ui_color secondary_foreground { 0 };
        // 비활성 글자·글리프다.
        ui_color disabled_foreground { 0 };
        // 1px 구분선이다.
        ui_color divider { 0 };
        // 입력칸(텍스트 박스·드롭다운)의 표면과 테두리다.
        ui_color input_background { 0 };
        ui_color input_border { 0 };
        // 꺼진 체크 상자·라디오 고리의 선이다.
        // input_border보다 진하다 — 칸의 테두리가 아니라 표시 자체의 윤곽이라 눈에 띄어야 한다.
        ui_color control_border { 0 };
        // 접히는 그룹의 둘레 선이다.
        ui_color group_border { 0 };
        ui_color warning_accent { 0 };
        ui_color error_accent { 0 };
        // caption 밖의 일반 버튼(도구 막대·목록 항목)의 hover와 눌림 표시다.
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color button_pressed_background { 0 };
        ui_color tooltip_background { 0 };
        ui_color tooltip_border { 0 };
        // 상단 막대 아래로 내용이 지나갈 때 쓰는 그림자다.
        // 알파는 그리는 쪽이 거리에 따라 낮춘다.
        ui_color content_shadow { 0 };
        // notice 배너의 바탕이다.
        // 일반 표면과 같은 색으로 보이지 않도록 구분한다.
        ui_color notice_background { 0 };
        caption_color_palette caption {};

        [[nodiscard]] bool operator==(const neutral_color_palette&) const noexcept = default;
    };

    // accent 4역할 위에 얹어 파생 역할을 만드는 알파들이다 (0~1).
    // element마다 흩어져 있던 숫자(0.70·0.25·0.35·0.18·0.30·0.22·0.42…)를 한 표로 모았다.
    // 어느 accent 역할에 얹는지는 `compose_palette`가 정하고, 여기에는 **양**만 있다.
    struct accent_tones
    {
        // 채운 강조 버튼을 누르는 동안이다 (`accent`에 얹는다).
        float pressed { 0.70f };
        // 옅은 강조 버튼의 쉼과 hover다 (`soft`·`hover`에 얹는다).
        float soft_button { 0.25f };
        float soft_button_hover { 0.35f };
        // 켜진 토글 버튼의 바탕이다 (`soft`에 얹는다).
        float active_toggle { 0.22f };
        // 텍스트 입력의 고른 구간이다 (`soft`에 얹는다).
        float selection { 0.35f };
        // 고른 목록 행의 옅은 채움이다 (`soft`에 얹는다).
        float row_selection { 0.18f };
        // 끌어다 놓을 자리의 채움이다 (`accent`에 얹는다).
        float drop_target { 0.30f };
        // 옅은 위험 버튼의 쉼·hover·눌림이다 (`error_accent`에 얹는다 — 오류색에는
        // hover·soft 짝이 없어 역할 하나를 알파로 층 짓는다).
        float danger_button { 0.20f };
        float danger_button_hover { 0.32f };
        float danger_button_pressed { 0.42f };

        [[nodiscard]] bool operator==(const accent_tones&) const noexcept = default;
    };

    // 그리기가 보는 팔레트다. 중립 색 + accent 4역할 + 둘에서 파생한 역할이다.
    // element는 여기의 역할만 고르고 알파를 발명하지 않는다 (concepts/theming.md).
    struct ui_color_palette
    {
        ui_color window_background { 0 };
        ui_color surface_background { 0 };
        ui_color primary_foreground { 0 };
        // 흐린 보조 글자다 (아이콘·설명·머리행 제목).
        ui_color secondary_foreground { 0 };
        // 비활성 글자·글리프다.
        // element마다 알파를 발명하지 않고 이 역할 하나를 쓴다.
        ui_color disabled_foreground { 0 };
        // 1px 구분선이다.
        ui_color divider { 0 };
        // 입력칸(텍스트 박스·드롭다운)의 표면과 테두리다.
        ui_color input_background { 0 };
        ui_color input_border { 0 };
        // 꺼진 체크 상자·라디오 고리의 선이다.
        ui_color control_border { 0 };
        // 접히는 그룹의 둘레 선이다.
        ui_color group_border { 0 };
        // 키 컬러의 네 역할이다.
        // 상태 표시(정상)도 이 색을 쓴다.
        ui_color accent { 0 };
        ui_color accent_hover { 0 };
        ui_color accent_soft { 0 };
        // 옅은 강조 바탕(고른 토글, 옅은 강조 단추) 위의 글자다.
        ui_color accent_emphasis_foreground { 0 };
        // --- accent에서 파생한 역할이다 (`accent_tones`가 양을 정한다) ---
        // 채운 강조 바탕(`accent`) 위의 글자와 기호다 (기본 단추, 체크 표시, 켜진 스위치 손잡이, 강조 배지).
        // 바탕에 대고 대비가 큰 쪽(흰색이나 거의 검정)을 고른다. 옅은 바탕의 글자(`accent_emphasis_foreground`)와
        // 따로인 이유는 밝은 테마에서 두 바탕의 밝기가 반대쪽이라 한 색으로 둘 다 읽히게 할 수 없어서다.
        ui_color accent_foreground { 0 };
        // 채운 오류색 바탕(`error_accent`) 위의 글자다 (위험 단추의 hover). 고르는 규칙은 `accent_foreground`와 같다.
        ui_color error_foreground { 0 };
        // 채운 강조 버튼을 누르는 동안의 바탕이다.
        ui_color accent_pressed { 0 };
        // 옅은 강조 버튼의 쉼과 hover 바탕이다.
        ui_color soft_button_background { 0 };
        ui_color soft_button_hover_background { 0 };
        // 켜진 토글 버튼이 쉬는 동안 깔고 앉는 바탕이다.
        ui_color active_toggle_background { 0 };
        // 텍스트 입력의 고른 구간과 그 위의 글자다.
        // 고대비는 highlight 짝이라 글자도 함께 바뀐다 — 둘을 한 쌍으로 쓴다.
        ui_color selection_background { 0 };
        ui_color selection_foreground { 0 };
        // 고른 목록 행의 옅은 채움이다 (왼쪽 표식은 `accent`).
        // 알파 0일 수 있다 — 고대비는 채움을 접고 표식만 남긴다.
        ui_color row_selection_background { 0 };
        // 끌어다 놓을 자리의 채움이다.
        ui_color drop_target_background { 0 };
        // 옅은 위험 버튼의 쉼·hover·눌림 바탕이다.
        ui_color danger_button_background { 0 };
        ui_color danger_button_hover_background { 0 };
        ui_color danger_button_pressed_background { 0 };
        ui_color warning_accent { 0 };
        ui_color error_accent { 0 };
        // caption 밖의 일반 버튼(도구 막대·목록 항목)의 hover와 눌림 표시다.
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color button_pressed_background { 0 };
        ui_color tooltip_background { 0 };
        ui_color tooltip_border { 0 };
        // 상단 막대 아래로 내용이 지나갈 때 쓰는 그림자다.
        // 알파는 그리는 쪽이 거리에 따라 낮춘다.
        ui_color content_shadow { 0 };
        // notice 배너의 바탕이다.
        // 일반 표면과 같은 색으로 보이지 않도록 구분한다.
        ui_color notice_background { 0 };
        caption_color_palette caption {};

        [[nodiscard]] bool operator==(const ui_color_palette&) const noexcept = default;
    };

    // 고대비 팔레트를 합성할 시스템 색이다.
    // 고대비는 사용자가 색을 직접 고르는 접근성 설정이라(검정 바탕만이 아니라
    // 흰 바탕 테마·사용자 지정 색이 있다) 라이브러리가 색을 정하지 않고
    // platform이 OS에서 읽어 채운다 (Win32는 GetSysColor).
    // 기본값은 OS를 읽을 수 없을 때 물러설 검정 바탕이다.
    struct high_contrast_colors
    {
        ui_color window_background { make_ui_color(0, 0, 0) };
        ui_color window_foreground { make_ui_color(255, 255, 255) };
        // 선택·hover의 바탕과 글자다 (COLOR_HIGHLIGHT 계열).
        ui_color highlight_background { make_ui_color(255, 255, 255) };
        ui_color highlight_foreground { make_ui_color(0, 0, 0) };
        // 링크·강조의 색이다 (COLOR_HOTLIGHT).
        ui_color emphasis { make_ui_color(255, 255, 255) };
        // 버튼·caption 표면과 글자다 (COLOR_BTNFACE 계열).
        ui_color button_background { make_ui_color(0, 0, 0) };
        ui_color button_foreground { make_ui_color(255, 255, 255) };
        // 비활성 글자다 (COLOR_GRAYTEXT).
        ui_color disabled_foreground { make_ui_color(0, 255, 0) };

        [[nodiscard]] bool operator==(const high_contrast_colors&) const = default;
    };

    // 시스템 색으로 고대비 팔레트를 합성한다.
    // 키 컬러도 앱의 중립 색도 쓰지 않고, 강조·선택은 시스템의 hotlight·highlight가 맡는다.
    [[nodiscard]] ui_color_palette high_contrast_palette_for(const high_contrast_colors& colors) noexcept;

    // 채운 바탕 위의 글자색이다. 흰색과 거의 검정 가운데 그 바탕에 대고 대비가 큰 쪽이다
    // (`is_dark_background`의 경계). `compose_palette`가 채운 강조·오류 바탕의 글자를 이것으로 짓는다.
    [[nodiscard]] ui_color foreground_on(ui_color background) noexcept;

    // 중립 색 위에 accent 4역할을 얹고 tone으로 파생 역할을 만든다.
    // **동적 합성의 진입점이다.** 테마 이름을 묻지 않는다 — 어떤 중립 색과 어떤 accent를
    // 짝지을지는 부르는 쪽의 일이고, 여기는 그 둘을 한 팔레트로 접기만 한다.
    // 실행 시점에 색을 계산하는 앱은 이것으로 팔레트를 세우고 `ui_style`로 싣는다.
    [[nodiscard]] ui_color_palette compose_palette(const neutral_color_palette& neutral, const accent_color_set& accent, const accent_tones& tones = {}) noexcept;

    // 내장 스타일(assets/style.json)의 중립 색 위에 키 컬러를 얹은 팔레트다.
    // 고대비는 키 컬러를 무시하고 기본 시스템 색(검정 바탕 fallback)으로
    // 합성한다 — 실제 시스템 색을 아는 platform은 `high_contrast_palette_for`를 쓴다.
    // 앱이 세운 스타일로 합성하는 오버로드는 ui_style.h에 있다.
    [[nodiscard]] ui_color_palette color_palette_for(color_theme theme, const accent_definition& accent) noexcept;
    // 기본 키 컬러(mint)를 쓰는 팔레트다.
    // 설정을 아직 모르는 경로가 쓴다.
    [[nodiscard]] ui_color_palette color_palette_for(color_theme theme) noexcept;

    // 설정의 선호를 실제 팔레트 선택으로 바꾼다.
    // 고대비가 가장 세고, `system`은 OS의 밝은 앱 설정(`system_prefers_light`)을 따른다.
    [[nodiscard]] color_theme resolve_color_theme(theme_preference preference, bool high_contrast, bool system_prefers_light) noexcept;
} // namespace luil

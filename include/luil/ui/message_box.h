#pragma once

#include "luil/ui/modal_host_element.h"
#include "luil/ui/severity.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 확인 대화 상자의 치수다 (논리 픽셀).
    //  - 상자 폭은 `min(width, 자리 폭 - 2 * message_box_screen_margin)`이다.
    //  - 단추 폭은 글에 맞추되 `message_box_button_minimum_width`보다 좁지 않다.
    inline constexpr float message_box_default_width { 360.0f };
    inline constexpr float message_box_screen_margin { 16.0f };
    inline constexpr float message_box_button_minimum_width { 88.0f };

    // 표준 단추의 key다 (`message_box_ok` 같은 helper가 채운다). 앱이 `result`에서 가를 때 쓴다.
    inline constexpr std::u8string_view message_box_ok_key { u8"ok" };
    inline constexpr std::u8string_view message_box_cancel_key { u8"cancel" };
    inline constexpr std::u8string_view message_box_yes_key { u8"yes" };
    inline constexpr std::u8string_view message_box_no_key { u8"no" };

    // 단추의 역할이다. 모양, Enter의 대상, 첫 초점, Esc의 결과를 한꺼번에 정한다.
    enum class message_box_role
    {
        normal,
        // 긍정이다. 강조색이고, 마지막 accept 단추가 Enter의 대상(강조색 채움)이다.
        accept,
        // 거절이다. Esc, 바깥 누름, 뒤로 가기의 결과가 된다 (여럿이면 마지막).
        reject,
        // 되돌릴 수 없는 일이다. 오류색이고 Enter의 대상도 첫 초점도 되지 않는다.
        //  - 한 번의 Enter가 되돌릴 수 없는 일을 하지 않게 한다. 그런 대화가 필요하면 앱이 `modal_host`로 직접 짓는다.
        destructive,
    };

    struct message_box_button
    {
        // 한 상자 안에서 겹치지 않는다. 눌림이 낼 결과의 이름이자 단추 owner의 한 부분이다.
        std::u8string key {};
        std::u8string text {};
        message_box_role role { message_box_role::normal };
    };

    // "다시 묻지 않기" 같은 칸이다. 켜졌는지는 앱 상태이고 칸은 바꾸자는 메시지만 낸다.
    struct message_box_check
    {
        std::u8string label {};
        bool checked { false };
        ui_action toggle {};
    };

    struct message_box_config
    {
        // host, 내용, 단추의 owner다.
        std::u8string owner {};
        // 있으면 제목 앞에 글리프를 그리고 접근 이름에 심각도 낱말을 붙인다.
        std::optional<ui_severity> severity {};
        std::u8string title {};
        std::u8string body {};
        // 왼쪽에서 오른쪽, 쌓이면 위에서 아래의 순서다. 표준 조합은 거절, 긍정 순이다.
        std::vector<message_box_button> buttons {};
        std::optional<message_box_check> check {};
        // 단추 눌림, Esc, 바깥 누름이 낸 결과를 앱 메시지로 바꾼다. 비면 단추도 Esc도 메시지를 내지 않는다.
        //  - 닫는 모든 계기가 같은 함수를 부르므로 앱은 메시지 하나를 받아 닫고 key로 가른다.
        std::function<input_action(std::u8string_view key)> result {};
        // 거짓이면 바깥 누름은 흡수만 한다.
        bool outside_dismisses { true };
        // 닫힌 뒤 초점이 돌아갈 바깥 자리다 (`modal_host_config::focus_return`과 같다).
        ui_element_id focus_return {};
        float width { message_box_default_width };
        // 단추 높이다 (논리 픽셀). 0이면 `text_button_height`다. 터치 앱이 올린다.
        float control_size { 0.0f };
        // 접근 이름 앞에 붙는 심각도 낱말이다. 비면 기본 문구(정보·성공·경고·오류)다.
        std::u8string severity_name {};
    };

    // 제목, 본문, 단추 행을 담은 확인 대화 상자를 한 번에 짓는다 (scrim, 초점 가둠, Esc까지 host가 맡는다).
    //
    // 크기는 내용에서 나온다. host의 `fit_height`와 `edge_margin`이 배치할 때 내용에게 높이를 묻고 자리 안으로 자르므로,
    // 앱은 높이도 창 크기도 넘기지 않고 `host->arrange`(또는 root에 담기)만 한다.
    //  - 열림은 앱 상태다. 앱이 이 함수를 부르는 때가 열려 있는 때이고, 닫기는 `result`가 낸 메시지를 받아 다음 frame에 짓지 않는 것이다.
    //  - 첫 초점은 마지막 accept, 없으면 마지막 reject, 그것도 없으면 첫 단추다. 뜨자마자 친 Enter는 그 단추를 누른다
    //    (초점이 이긴다). 마지막 accept만 기본 버튼(강조색 채움)이고 destructive는 되지 않는다.
    //  - Esc는 마지막 reject의 key를, reject가 없고 단추가 하나면 그 단추의 key를 `result`에 넘긴다. 둘 다 없으면 Esc는 앱으로 흐른다.
    [[nodiscard]] std::unique_ptr<modal_host_element> make_message_box(message_box_config config);

    // Esc가 낼 결과다. 닫는 길이 없으면 `result`가 비었거나 Esc에 응할 단추가 없으면 nullopt다.
    // 앱이 뒤로 가기(`ui_frame::back`)에 단다. Esc와 바깥 누름은 host가 같은 결과를 낸다.
    [[nodiscard]] std::optional<input_action> message_box_dismiss_message(const message_box_config& config);

    // 단추의 id다. owner는 상자 owner와 key 사이에 단위 구분자(U+001F)를 둔다 (표의 부품과 같은 꼴이다).
    [[nodiscard]] ui_element_id message_box_button_id(std::u8string_view owner, std::u8string_view key);
    // 확인 칸의 id다. 기존 `check` kind에 같은 꼴의 owner다.
    [[nodiscard]] ui_element_id message_box_check_id(std::u8string_view owner);

    // 표준 단추다. 글이 비면 기본 문구(확인·취소·예·아니요)이고 앱이 준 글은 그대로 쓴다.
    // key는 `message_box_ok_key`·`message_box_cancel_key`·`message_box_yes_key`·`message_box_no_key`다.
    [[nodiscard]] message_box_button message_box_ok(std::u8string text = {});  // accept
    [[nodiscard]] message_box_button message_box_cancel(std::u8string text = {});  // reject
    [[nodiscard]] message_box_button message_box_yes(std::u8string text = {});  // accept
    [[nodiscard]] message_box_button message_box_no(std::u8string text = {});  // reject
}

#pragma once

#include "luil/app/notification.h"
#include "luil/ui/app_message.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

// OS 알림의 플랫폼을 가리지 않는 몫이다 (os-notification-design.md). OS 헤더가 없어 core test에서 돈다.
//  - 올릴 글의 검사와 자르기: Windows와 Android가 같은 규칙으로 거른다.
//  - 사건 대기열: OS가 다른 thread에서 낸 사건을 UI thread가 앱에 낼 때까지 쥔다 (결정 8).
namespace luil {
    // id의 글자 수 상한이다.
    inline constexpr std::size_t notification_id_limit { 64 };
    // 제목·본문의 코드 포인트 상한이다. 라이브러리가 정한 값이다 (OS마다 다른 상한보다 작게 잡아 어디서나 같게 잘린다).
    inline constexpr std::size_t notification_title_limit { 256 };
    inline constexpr std::size_t notification_body_limit { 1024 };
    // 그림의 긴 변 상한이다 (px). Android 바인더 거래 크기를 넘지 않게 하려는 값이고 Windows도 같이 쓴다.
    inline constexpr int notification_image_limit { 256 };

    // ASCII 글자·숫자와 `.` `_` `-`로 1~64자인가.
    [[nodiscard]] bool notification_id_valid(std::u8string_view id) noexcept;

    // 코드 포인트가 `limit`개를 넘으면 앞의 `limit - 1`개에 `…`(U+2026)를 단다. 결과는 `limit`개를 넘지 않는다.
    //  - 입력은 올바른 UTF-8이어야 한다 (`prepare_notification_text`가 먼저 거른다). `limit`가 0이면 빈 글이다.
    [[nodiscard]] std::u8string truncate_code_points(std::u8string_view text, std::size_t limit);

    // OS에 넘길 제목과 본문이다.
    struct notification_text
    {
        std::u8string title {};
        std::u8string body {};
    };

    // id와 글을 검사하고 상한에 맞춰 자른다. 규칙 밖이면 빈 값과 `error`(진단 영문)다.
    [[nodiscard]] std::optional<notification_text> prepare_notification_text(const notification& value, std::u8string& error);

    // Android `PendingIntent`의 요청 번호다. 같은 id면 늘 같은 FNV-1a 해시다. 충돌은 Intent의 data에 든 id 전체로 가른다.
    [[nodiscard]] std::int32_t notification_request_code(std::u8string_view id) noexcept;

    // OS가 낸 사건을 UI thread가 앱에 낼 때까지 쥐는 대기열이다. 어느 thread에서나 부른다.
    //  - 64개를 넘으면 가장 오래된 것을 버리고 남은 가장 오래된 사건의 `lost_before`에 센다.
    //  - 앱 inbox가 차서 내지 못한 사건은 `put_back`으로 맨 앞에 되돌린다. 다음에 가장 먼저 나간다.
    class notification_queue
    {
    public:
        static constexpr std::size_t capacity { 64 };

        void push(notification_event event);
        [[nodiscard]] std::optional<notification_event> take();
        void put_back(notification_event event);
        void clear();
        [[nodiscard]] bool empty() const;
        [[nodiscard]] std::size_t size() const;

    private:
        // 잠금을 쥔 채 부른다.
        void trim();

        mutable std::mutex mutex_ {};
        std::deque<notification_event> events_ {};
    };

    struct notification_delivery
    {
        // 앱에 게시했거나(빈 메시지가 아닌 것) 앱이 빈 메시지로 넘긴 사건 수다.
        std::size_t delivered { 0 };
        // 그 가운데 눌림이 있었다. 플랫폼이 주 창을 앞으로 세운다.
        bool activated { false };
        // 게시하지 못해 사건을 되돌리고 멈췄다. 다음 frame 게시 때 다시 낸다.
        bool stalled { false };
        // 메시지를 짓다 던진 사건 수다. 그 사건은 버린다.
        std::size_t failed_calls { 0 };
    };

    // 대기열을 비우며 사건마다 `make`로 메시지를 짓고 `post`로 게시한다. `post`가 0(표 없음)이면 그 사건을 되돌리고 멈춘다.
    //  - 빈 메시지는 게시하지 않고 사건을 다 쓴 것으로 본다. `make`가 던지면 그 사건을 버리고 이어 간다.
    [[nodiscard]] notification_delivery deliver_notification_events(
        notification_queue& queue, const std::function<app_message(const notification_event&)>& make, const std::function<std::uint64_t(app_message)>& post);
}

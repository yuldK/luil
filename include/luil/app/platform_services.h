#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 플랫폼 도구의 답이다 (platform-services-design.md).
    enum class platform_status
    {
        ok,
        // 대상이 없다 (경로, 또는 취소할 I/O).
        not_found,
        // 이 플랫폼에는 그 일이 없다.
        unsupported,
        // OS가 거절했다. `error`가 그 이유를 든다.
        failed,
    };

    struct platform_result
    {
        platform_status status { platform_status::failed };
        // 개발자가 읽을 진단 영문이다. `ok`면 비었다.
        std::u8string error {};
        // `launch_program`을 기다렸으면 그 종료 코드다.
        std::optional<int> exit_code {};

        [[nodiscard]] bool ok() const noexcept
        {
            return status == platform_status::ok;
        }
    };

    // 파일을 통째로 원자적으로 바꾼다. 읽는 쪽은 언제나 옛 파일 전체나 새 파일 전체를 본다.
    //  - 같은 폴더의 임시 파일(`<이름>.<pid>.tmp`)에 다 쓰고 디스크까지 내린 뒤 이름을 바꾼다. 실패하면 임시 파일을 지운다.
    //  - 폴더가 없으면 `failed`다 (만들지 않는다).
    [[nodiscard]] platform_result replace_file_atomically(std::u8string_view path, std::span<const std::uint8_t> contents);

    // 둘째가 첫째에 건넨 값이다 (single-instance-args-design.md). 라이브러리는 해석하지 않는다 (경로를 풀지도, 열지도 않는다).
    struct instance_arguments
    {
        // 보낸 쪽이 준 목록 그대로다. 실행 파일 경로(argv[0])는 앱이 넣어야 들어간다.
        std::vector<std::u8string> arguments {};
        // 보낸 프로세스의 작업 폴더다. 상대 경로를 풀 때 쓴다. 보내는 쪽이 늘 채운다.
        std::u8string working_directory {};
    };

    struct instance_send_options
    {
        // 첫 창이 설 때까지의 기다림과 답을 기다림을 합친 시한이다.
        std::chrono::milliseconds timeout { 3000 };
        // 첫째의 보이는 주 창을 앞으로 세운다. 숨은 주 창(`ui_frame::main_hidden`)은 보이게 하지 않는다.
        //  - 다르게 하려는 앱은 거짓으로 보내고 자기 `focus_request`를 쓴다.
        bool activate { true };
    };

    // 같은 이름의 인스턴스 가운데 첫째인가. 객체가 사는 동안 쥐고 없어질 때 놓는다.
    //  - Windows: 이름 있는 뮤텍스 `Local\luil-instance:<name>`(같은 사용자 세션 안에서 하나).
    //  - Android: OS가 앱마다 프로세스를 하나로 묶는다. `primary()`는 참, `status()`는 `unsupported`다.
    class single_instance
    {
    public:
        explicit single_instance(std::u8string_view name);
        single_instance(const single_instance&) = delete;
        single_instance(single_instance&&) noexcept;
        single_instance& operator=(const single_instance&) = delete;
        single_instance& operator=(single_instance&&) noexcept;
        ~single_instance();

        [[nodiscard]] bool primary() const noexcept;
        [[nodiscard]] const platform_result& status() const noexcept;
        // 받은 이름이다. 옮겨 간 뒤의 빈 객체는 빈 글이다.
        [[nodiscard]] std::u8string_view name() const noexcept;

        // 첫째에 인자를 보내고 앱이 받을 때까지 기다린다. 둘째(`primary() == false`)가 어느 thread에서나 부른다.
        //  - `ok`: 첫째의 앱이 받았다 (`window_delegate::on_instance_arguments`가 true).
        //  - `failed`: 받지 않았다. 앱이 false를 답했거나, 인자가 잘못되었거나(잘못된 UTF-8, NUL, 항목 4096개·글 256 KiB 초과,
        //    이름이 UTF-16 241 코드 단위 초과), 시한이 지났거나, 첫째가 더 높은 권한이다. 시한이 지난 보내기는 첫째가 나중에 받을 수 있다.
        //  - `not_found`: 시한 안에 첫째의 주 창이 서지 않았거나 이 객체가 첫째다.
        //  - 보내는 동안 부른 thread는 다른 메시지를 풀지 않는다. Android는 늘 `unsupported`다.
        [[nodiscard]] platform_result send_to_primary(std::span<const std::u8string> arguments, const instance_send_options& options = {}) const;

    private:
        struct state;
        std::unique_ptr<state> state_ {};
    };

    // 파일 하나의 배타 잠금이다. 없으면 파일을 만든다. 이미 잠겨 있으면 기다리지 않고 `locked()`가 거짓이다.
    //  - 같은 프로세스의 두 객체도 서로 막는다. 객체가 없어지거나 프로세스가 죽으면 놓인다.
    class file_lock
    {
    public:
        explicit file_lock(std::u8string_view path);
        file_lock(const file_lock&) = delete;
        file_lock(file_lock&&) noexcept;
        file_lock& operator=(const file_lock&) = delete;
        file_lock& operator=(file_lock&&) noexcept;
        ~file_lock();

        [[nodiscard]] bool locked() const noexcept;
        [[nodiscard]] const platform_result& status() const noexcept;

    private:
        struct state;
        std::unique_ptr<state> state_ {};
    };

    // 파일 관리자를 열고 그 파일(폴더면 그 폴더)을 고른 채로 둔다. 없는 경로는 `not_found`다. Android는 `unsupported`다.
    [[nodiscard]] platform_result reveal_in_file_manager(std::u8string_view path);

    struct launch_options
    {
        // 비어 있으면 부른 프로세스의 지금 폴더다.
        std::u8string working_directory {};
        // 거짓이면 콘솔 창을 띄우지 않는다.
        bool show_window { true };
        // 참이면 끝날 때까지 기다려 `exit_code`를 싣는다.
        bool wait { false };
    };

    // 외부 프로그램을 띄운다. 인자는 Windows 명령 줄 규칙(따옴표 안의 백슬래시와 따옴표)으로 이어 넘긴다.
    //  - Android는 `unsupported`다 (다른 앱은 프로세스가 아니라 Intent로 열린다. 주소는 `open_url`이 연다).
    [[nodiscard]] platform_result launch_program(std::u8string_view program, std::span<const std::u8string> arguments, const launch_options& options = {});

    struct open_url_options
    {
        // 열어도 되는 scheme이다. 대소문자를 가리지 않는다.
        //  - 글 안의 링크처럼 믿을 수 없는 주소가 들어오는 길이라, 실행 파일·`file:`·스크립트 scheme을 기본으로 막는다.
        std::vector<std::u8string> schemes { u8"http", u8"https", u8"mailto" };
    };

    // 주소의 scheme(RFC 3986: 알파벳 하나 뒤에 알파벳·숫자·`+-.`, 그리고 `:`)이 목록에 있는가.
    //  - scheme이 없거나 깨졌으면, `:` 뒤가 비었으면, 공백으로 시작·끝나거나 제어 문자가 섞였으면 거짓이다.
    [[nodiscard]] bool url_scheme_allowed(std::u8string_view url, std::span<const std::u8string> schemes);

    // 기본 브라우저·메일 앱으로 주소를 연다. 어느 thread에서나 부른다.
    //  - 허용되지 않은 주소는 열지 않고 `failed`다. 열 앱이 없으면 `not_found`다.
    //  - Windows: `ShellExecuteExW`(open). Android: `ACTION_VIEW` Intent를 앱 context로 띄운다.
    [[nodiscard]] platform_result open_url(std::u8string_view url, const open_url_options& options = {});

    // **부른 thread**를 배경 우선순위로 내리거나(참) 되돌린다(거짓). 색인 작성·섬네일처럼 사용자를 기다리게 하지 않는 일에 쓴다.
    //  - Windows: CPU·I/O·메모리 우선순위를 함께 내리는 배경 모드다. Android: nice 10 / 0이다.
    [[nodiscard]] platform_result set_background_priority(bool background);

    // 지은 thread를 붙들어, 다른 thread에서 그 thread의 막힌 동기 I/O를 끊는다.
    //  - 막힌 I/O가 없으면 `not_found`다. 끊긴 쪽은 `ERROR_OPERATION_ABORTED`를 받는다.
    //  - Android는 `unsupported`다 (같은 일을 하는 시스템 호출이 없다).
    class blocking_io_canceller
    {
    public:
        blocking_io_canceller();
        blocking_io_canceller(const blocking_io_canceller&) = delete;
        blocking_io_canceller(blocking_io_canceller&&) noexcept;
        blocking_io_canceller& operator=(const blocking_io_canceller&) = delete;
        blocking_io_canceller& operator=(blocking_io_canceller&&) noexcept;
        ~blocking_io_canceller();

        // 어느 thread에서나 부른다.
        [[nodiscard]] platform_result cancel() const;

    private:
        struct state;
        std::unique_ptr<state> state_ {};
    };

    // 사용자 지역 설정의 주 첫 요일이다. OS가 답하지 않으면 빈 값이다. 어느 thread에서나 부른다.
    //  - 라이브러리는 이 값을 스스로 쓰지 않는다. `calendar_config::first_day`의 기본은 일요일 그대로이고 앱이 넣는다.
    //  - 부를 때마다 OS에 묻는다. frame마다 부르지 말고 시작할 때와 앞으로 올 때(`app_lifecycle::foreground`) 읽어 둔다.
    //  - Windows: `GetLocaleInfoEx`(`LOCALE_IFIRSTDAYOFWEEK`). Android: `java.util.Calendar.getFirstDayOfWeek()`.
    [[nodiscard]] std::optional<std::chrono::weekday> user_first_weekday();

    // 사용자가 고른 표시 언어의 BCP 47 태그다. 앞이 먼저다. 읽지 못하면 빈 목록이다. 어느 thread에서나 부른다.
    //  - 라이브러리 기본 문구는 이 값을 읽지 않는다. OS 언어를 따르려는 앱이 `set_text_languages(user_ui_languages())`로 넣는다.
    //  - Windows: `GetUserPreferredUILanguages`의 목록. Android: 호스트가 `AConfiguration`에서 읽어 둔 태그 하나(Activity가 다시 설 때 새로 읽는다).
    [[nodiscard]] std::vector<std::u8string> user_ui_languages();
}

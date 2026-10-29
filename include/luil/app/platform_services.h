#pragma once

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
} // namespace luil

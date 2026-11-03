#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 자동 업데이트의 확인·신뢰·받기·적용이다 (docs/auto-update-design.md, docs/auto-update-apply-design.md).
//  - 서버의 봉투 하나를 받아 ECDSA P-256 서명을 OS 암호 API로 확인한 뒤에만 매니페스트를 읽는다.
//  - Win32는 실행 파일을 덩이로 받아 SHA-256을 맞추고, 받은 새 exe를 도우미로 띄워 옛 exe를 같은 경로에서 바꾼다.
//  - Android는 확인만 하고 받지 않는다. 적용은 스토어 페이지를 연다.
namespace luil {
    // 앱에 박는 서명 확인 키다. 키를 바꾸는 동안 둘 이상을 싣는다.
    struct update_key
    {
        // 서명 항목의 `key`와 맞춘다. 비거나 겹치면 `updater::create`가 실패한다.
        std::u8string id {};
        // P-256 공개 키의 SubjectPublicKeyInfo DER을 base64로 적은 것이다 (PEM 몸통 한 줄, openssl `ec -pubout -outform DER`).
        std::u8string spki_base64 {};
    };

    enum class update_phase
    {
        idle,
        checking,
        up_to_date,
        available,
        downloading,
        verifying,
        ready,
        applying,
        handed_off,
        failed,
    };

    enum class update_failure
    {
        none,
        // 전송 오류다 (이름 풀기·연결·끊김·시한).
        network,
        // 답이 2xx가 아니다. `update_status::http_status`에 코드가 있다.
        http_status,
        // 봉투나 서명을 지난 매니페스트의 칸이 없거나 꼴이 틀렸다.
        manifest_invalid,
        // 아는 키의 서명이 없거나 맞는 것이 없다. `error`가 둘을 가른다.
        signature_invalid,
        // 매니페스트의 `app`·`channel`이 config와 다르다.
        manifest_mismatch,
        // 매니페스트의 `sequence`가 이 PC가 본 가장 큰 값보다 작다.
        replayed,
        // 이 빌드의 플랫폼 칸이 없다.
        no_asset,
        // asset 크기가 `max_asset_bytes`보다 크다.
        too_large,
        // 서버가 요청한 바이트 범위로 답하지 않았다.
        range_unsupported,
        // 받은 바이트의 SHA-256이 매니페스트와 다르다. 받던 파일을 지웠다.
        hash_mismatch,
        // 받기 폴더에 쓰거나 이름을 바꾸지 못했다.
        disk,
        // 같은 받기 폴더를 다른 updater가 쥐고 있다 (`update.lock`).
        busy,
        // 실행 파일 폴더에 쓸 수 없다 (적용 문서).
        not_writable,
        // 적용을 띄우지 못했다 (적용 문서).
        launch_failed,
        // 이 플랫폼에는 그 일이 없다.
        unsupported,
        // 앱이 `cancel`했다.
        cancelled,
        // `stop` 중에 끊겼다. 이 실패는 알리지 않는다 (`stop` 뒤에는 `report`가 없다).
        stopped,
    };

    // 서명을 지난 매니페스트가 이 플랫폼에 내준 판이다.
    struct update_offer
    {
        std::u8string version {};
        // 서버가 준 글이다. 앱이 그대로 보인다. 없으면 비었다.
        std::u8string notes {};
        // 판의 설명 주소다. https만 받는다. 없으면 비었다.
        std::u8string notes_url {};
        // Win32가 받을 바이트다. Android는 0이다.
        std::uint64_t size { 0 };
        // Android가 열 주소다. Win32는 비었다.
        std::u8string store_url {};
    };

    struct update_status
    {
        update_phase phase { update_phase::idle };
        // `failed`일 때만 뜻이 있다.
        update_failure failure { update_failure::none };
        // `available`부터 선다. 서명을 지난 판이 있으면 `no_asset`·`too_large` 실패에도 선다.
        std::optional<update_offer> offer {};
        // `downloading`·`verifying`의 진행이다 (바이트).
        std::uint64_t received { 0 };
        std::uint64_t total { 0 };
        // `http_status` 실패의 상태 코드다.
        int http_status { 0 };
        // 개발자가 읽을 영문 진단이다. 사람에게 보일 글은 앱이 단계와 실패로 짓는다.
        std::u8string error {};
    };

    // 받기의 덩이 크기 범위다 (바이트). 덩이 하나가 메모리에 서고 요청 하나가 된다.
    inline constexpr std::uint64_t update_minimum_chunk_bytes { 64u * 1024u };
    inline constexpr std::uint64_t update_maximum_chunk_bytes { 16u * 1024u * 1024u };

    struct updater_config
    {
        // 매니페스트의 `app`과 같아야 한다. 비면 `create`가 실패한다.
        std::u8string app_id {};
        // 채널마다 하나다. https이거나 되돌이 host(`127.0.0.1`·`[::1]`·`localhost`)의 http여야 한다.
        std::u8string manifest_url {};
        // 매니페스트의 `channel`과 같아야 한다.
        std::u8string channel { u8"stable" };
        // 지금 판이다. 점으로 나눈 십진 마디 1~4개(각 0~4294967295, 앞자리 0 없음) 밖이면 `create`가 실패한다.
        std::u8string current_version {};
        // 비거나 P-256 SPKI가 아닌 키가 있으면 `create`가 실패한다.
        std::vector<update_key> keys {};
        // 앱이 정하는 사용자 폴더다. 없으면 만든다. 판이 바뀌어도 같은 경로여야 한다 (`update-state.json`의 `sequence`).
        std::u8string download_directory {};
        std::uint64_t max_asset_bytes { 512u * 1024u * 1024u };
        // 받기의 덩이 크기다. `update_minimum_chunk_bytes`~`update_maximum_chunk_bytes` 밖이면 `create`가 실패한다.
        std::uint64_t download_chunk_bytes { 4u * 1024u * 1024u };
        std::u8string user_agent { u8"luil" };
        // 시스템 proxy를 따른다. 되돌이 서버를 쓰는 test와 데모는 끈다.
        bool use_system_proxy { true };
        // Android 적용이 열어도 되는 scheme이다. 확인도 `store_url`을 이것으로 거른다.
        std::vector<std::u8string> store_schemes { u8"https", u8"market" };
        // 단계가 바뀔 때와 받은 덩이마다 **updater의 thread**에서 불린다. 짧아야 하고 앱은 `post_app_message`로 나른다.
        //  - 여기서 `check`·`download`·`apply`·`cancel`을 불러도 된다. `stop`은 부르면 안 된다 (자기 thread를 기다리게 된다).
        //  - 던지면 삼킨다. 계약은 던지지 않는 것이다.
        std::function<void(const update_status&)> report {};
    };

    // 적용 한 번의 선택이다 (docs/auto-update-apply-design.md).
    struct update_apply_options
    {
        // 거짓이면 바꾸기만 하고 띄우지 않는다 (앱이 끝날 때 적용). 띄우지 않으니 새 판을 지켜보지도 않는다.
        bool restart { true };
        // 다시 띄울 때 실행 파일 뒤에 붙일 인자다. 라이브러리는 더하지 않는다.
        std::vector<std::u8string> restart_arguments {};
        // 도우미가 앱의 종료를 기다리는 시한이다. 0보다 커야 한다.
        std::chrono::milliseconds parent_timeout { 60000 };
        // 다시 띄운 새 판을 지켜보는 시간이다 (결정 9). 0이면 지켜보지 않고 되돌리지도 않는다.
        std::chrono::milliseconds confirm_timeout { 30000 };
    };

    enum class update_apply_outcome
    {
        // 바꾸었다. 다시 띄웠으면 그 판이 확인 전에 쓰러지지 않았다.
        replaced,
        // 바꾼 판이 확인 전에 0이 아닌 코드로 끝나 옛 exe로 되돌렸다. 그 판은 건너뛸 판으로 남았다.
        rolled_back,
        // 바꾸지 못했다. 옛 exe가 그대로다. `restart`였으면 옛 exe를 다시 띄웠다.
        replace_failed,
        // 앱이 `parent_timeout` 안에 끝나지 않았다. 아무것도 바꾸지도 띄우지도 않았다.
        parent_timeout,
    };

    // 지난 적용에서 도우미가 남긴 결과다. `updater::create`가 받기 폴더에서 읽고 지운다.
    struct update_apply_result
    {
        update_apply_outcome outcome { update_apply_outcome::replaced };
        std::u8string from_version {};
        std::u8string to_version {};
        // 영문 진단이다. `replaced`면 대개 비었다.
        std::u8string error {};
    };

    enum class update_apply_mode
    {
        // 받은 실행 파일로 옛 실행 파일을 바꾼다 (Win32).
        replace_executable,
        // 스토어 페이지를 연다 (Android).
        open_store,
    };

    // 이 플랫폼의 적용 방식이다. 창이 없어도 답한다.
    [[nodiscard]] update_apply_mode platform_update_apply_mode() noexcept;

    // 도우미 실행이면 적용을 하고 종료 코드를 준다. 아니면 비었다. Android는 늘 비었다.
    //  - `arguments`는 `win32::command_line_arguments()` 그대로다 (`[0]`이 실행 파일).
    //  - `wWinMain`에서 `command_line_arguments()` 바로 뒤, `single_instance`·`send_to_primary`·알림 실행 갈래보다 먼저 부른다.
    //  - 도우미는 창·`single_instance`·앱 식별의 레지스트리·COM 활성화기를 세우지 않는다. 그 전에 끝난다.
    //  - 종료 코드는 바꾸었으면 0, 바꾸지 못했거나 되돌렸으면 1, 표식은 있는데 인자가 틀리면 2다(아무것도 바꾸지 않는다).
    [[nodiscard]] std::optional<int> run_update_helper(std::span<const std::u8string> arguments);

    // 갱신 흐름 하나다. 확인·받기·적용을 자기 thread와 자기 `http_client`로 몬다.
    //  - 라이브러리는 스스로 확인하거나 받지 않는다. 언제 부를지는 앱이 정한다. 일은 한 번에 하나다.
    //  - 종료 순서는 `http_client`와 같다. `logic_driver::cancel()`에서 `cancel()`, `stop_workers()`에서 `stop()`이다.
    //  - **앱은 시작할 때 `updater`를 만든다.** 새 판의 `create`가 도우미에게 확인을 남긴다. 늦게 만들면
    //    `confirm_timeout` 안에 확인이 서지 않아, 다른 까닭으로 곧 0이 아닌 코드로 끝나는 판이 잘못 되돌려질 수 있다.
    class updater
    {
    public:
        updater(const updater&) = delete;
        updater(updater&&) = delete;
        updater& operator=(const updater&) = delete;
        updater& operator=(updater&&) = delete;
        // `stop()`을 부른다.
        ~updater();

        // 실패하면 null이고 `error`에 까닭을 적는다 (`http_client::create`와 같다).
        //  - 받기 폴더에 지난 적용의 결과가 있으면 읽어 지우고 `last_apply`로 낸다. 지켜보는 도우미에게 확인을 남긴다.
        //  - 그 결과를 이미 읽은 지난 시작이 남긴 것(옛 exe 옆의 `.old`·`.tmp`, 받아 둔 exe)은 여기서 지운다.
        [[nodiscard]] static std::unique_ptr<updater> create(updater_config configuration, std::u8string& error);

        // 어느 thread에서나 부른다. 받아들이면 참이고 결과는 `report`로 온다 (`checking`부터).
        //  - 일하는 중이거나 멈췄으면 거짓이고 `error`에 까닭을 적는다.
        [[nodiscard]] bool check(std::u8string& error);
        // 마지막 확인이 판을 내주었을 때(`available`, 그 뒤의 `ready`·`failed`) 받는다. 받던 파일이 있으면 잇는다.
        //  - Android는 늘 거짓이다 (받기가 없다).
        [[nodiscard]] bool download(std::u8string& error);
        // 하던 일을 접고 `failed`·`cancelled`로 알린다. 받던 파일은 남아 다음 `download`가 잇는다. 하는 일이 없으면 아무 일도 없다.
        void cancel() noexcept;
        // 멱등이고 어느 thread에서나 부른다(`report` 안은 빼고). 돌아온 뒤 `report`는 불리지 않는다.
        void stop() noexcept;

        // 적용한다. 받아들이면 참이고 `applying` 뒤 `handed_off`나 `failed`가 `report`로 온다.
        //  - Win32는 마지막 일이 `ready`로 끝났을 때 받는다. 받은 exe의 해시와 실행 파일 폴더의 쓰기를 다시 보고 도우미를 띄운다.
        //    `handed_off` 뒤로 이 updater는 일을 받지 않는다. 앱은 저장을 마치고 끝난다(`ui_command::application_exit`).
        //  - Android는 마지막 확인이 판을 내주었을 때 받고 `store_url`을 연다. 사용자가 단추를 누른 때 부른다
        //    (앞에 보이지 않는 앱이 띄운 화면은 OS가 막는다). 열린 뒤에도 앱은 그대로 돌아 다시 확인할 수 있다.
        [[nodiscard]] bool apply(const update_apply_options& options, std::u8string& error);
        // `create`가 받기 폴더에서 읽은 지난 적용의 결과다. 없었으면 비었다. Android는 늘 비었다.
        [[nodiscard]] std::optional<update_apply_result> last_apply() const;

    private:
        struct engine;
        explicit updater(std::unique_ptr<engine> owned) noexcept;

        std::unique_ptr<engine> engine_ {};
    };
}

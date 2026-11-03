// 자동 업데이트 적용 test가 띄우는 시험 앱이다 (docs/auto-update-apply-design.md의 확인 절).
//
// test(update_apply_tests.cpp)는 이 실행 파일을 임시 폴더에 두 번 복사하고 사본 끝에 판 표지(`luil-probe-version:1`·`:2`)를
// 붙인다. PE 뒤에 덧붙인 바이트는 실행에 닿지 않는다고 알려져 있어 같은 코드가 두 판으로 돈다. 옛 사본이 도우미를 띄우고
// 끝나면 도우미(새 사본)가 옛 사본 자리를 바꾸고 다시 띄운다. 평소 시작마다 시작 기록에 줄을 더해 test가 되읽는다.
//
// 인자 (평소 시작):
//   --log <파일> --instance <이름> --download <받기 폴더>   기록 자리, single_instance 이름, updater의 받기 폴더
//   --apply <sha256> <from> <to>    기록 뒤 비공개 `launch_update_helper`로 `<받기 폴더>/<sha256>.exe`를 도우미로 띄운다
//   --update <매니페스트 주소>       기록 뒤 updater로 확인·받기·적용을 끝까지 돈다
//   --no-restart, --parent-timeout <ms>, --confirm-timeout <ms>    적용의 선택
//   --linger <ms>                   도우미를 띄운 뒤 끝나기 전에 쉰다 (parent_timeout test)
//   --exit-if-version <판> <코드>    판 표지가 그것이면 기록 뒤 updater를 만들지 않고 그 코드로 끝난다 (확인 전에 쓰러지는 판)
//   --hold <ms>                     끝나기 전에 쉰다
//   --window                        판을 label로 그리는 창을 띄운다 (사람 확인용). 닫으면 끝난다
//   -- <나머지...>                  해석하지 않는다. 다시 띄울 인자가 그대로 돌아오는지 보는 자리다
// 다시 띄울 인자는 자기 인자에서 적용의 것(--apply·--update와 그 값, --no-restart, 두 시한, --linger)만 뺀 것이다.
//
// 기록은 한 줄에 `<pid>\t<열쇠>\t<값>`이다. 열쇠는 version, primary, lock, last_apply, arg(인자마다), apply, update, check_after_handoff다.

#include "luil/luil.h"

#include "update_fixtures.h"
#include "win32/executable_path.h"
#include "win32/update_apply_win32.h"

#include <windows.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    constexpr std::u8string_view version_marker { u8"luil-probe-version:" };
    // 서버를 두지 않는 시작은 이 주소로 updater를 세운다. 확인을 부르지 않으니 닿지 않는다.
    constexpr std::u8string_view idle_manifest_url { u8"http://127.0.0.1:9/update/manifest.json" };

    struct probe_options
    {
        std::filesystem::path log {};
        std::u8string instance {};
        std::filesystem::path download {};
        std::optional<std::u8string> apply_sha256 {};
        std::u8string apply_from {};
        std::u8string apply_to {};
        std::optional<std::u8string> update_url {};
        bool restart { true };
        std::chrono::milliseconds parent_timeout { 60000 };
        std::chrono::milliseconds confirm_timeout { 30000 };
        std::chrono::milliseconds linger { 0 };
        std::optional<std::u8string> exit_version {};
        int exit_code { 0 };
        std::chrono::milliseconds hold { 0 };
        bool window { false };
        // 다시 띄울 때 돌려줄 인자다.
        std::vector<std::u8string> restart_arguments {};
    };

    [[nodiscard]] std::optional<long long> number(const std::u8string_view text)
    {
        long long value { 0 };
        const auto* const first { reinterpret_cast<const char*>(text.data()) };
        const auto [end, code] { std::from_chars(first, first + text.size(), value) };
        if (code != std::errc {} || end != first + text.size())
            return std::nullopt;
        return value;
    }

    [[nodiscard]] std::optional<probe_options> parse_options(const std::span<const std::u8string> arguments)
    {
        probe_options options {};
        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            const std::u8string& argument { arguments[index] };
            const std::size_t left { arguments.size() - index - 1 };
            const auto keep = [&](const std::size_t count) {
                for (std::size_t at = 0; at <= count; ++at)
                    options.restart_arguments.push_back(arguments[index + at]);
            };
            if (argument == u8"--")
            {
                options.restart_arguments.insert(options.restart_arguments.end(), arguments.begin() + static_cast<std::ptrdiff_t>(index), arguments.end());
                break;
            }
            if (argument == u8"--log" && left >= 1)
            {
                keep(1);
                options.log = std::filesystem::path { arguments[++index] };
            }
            else if (argument == u8"--instance" && left >= 1)
            {
                keep(1);
                options.instance = arguments[++index];
            }
            else if (argument == u8"--download" && left >= 1)
            {
                keep(1);
                options.download = std::filesystem::path { arguments[++index] };
            }
            else if (argument == u8"--apply" && left >= 3)
            {
                options.apply_sha256 = arguments[index + 1];
                options.apply_from = arguments[index + 2];
                options.apply_to = arguments[index + 3];
                index += 3;
            }
            else if (argument == u8"--update" && left >= 1)
                options.update_url = arguments[++index];
            else if (argument == u8"--no-restart")
                options.restart = false;
            else if ((argument == u8"--parent-timeout" || argument == u8"--confirm-timeout" || argument == u8"--linger" || argument == u8"--hold") && left >= 1)
            {
                const std::optional<long long> value { number(arguments[index + 1]) };
                if (value.has_value() == false || *value < 0)
                    return std::nullopt;
                const std::chrono::milliseconds duration { *value };
                if (argument == u8"--hold")
                    keep(1);
                if (argument == u8"--parent-timeout")
                    options.parent_timeout = duration;
                else if (argument == u8"--confirm-timeout")
                    options.confirm_timeout = duration;
                else if (argument == u8"--linger")
                    options.linger = duration;
                else
                    options.hold = duration;
                ++index;
            }
            else if (argument == u8"--exit-if-version" && left >= 2)
            {
                const std::optional<long long> code { number(arguments[index + 2]) };
                if (code.has_value() == false)
                    return std::nullopt;
                keep(2);
                options.exit_version = arguments[index + 1];
                options.exit_code = static_cast<int>(*code);
                index += 2;
            }
            else if (argument == u8"--window")
            {
                keep(0);
                options.window = true;
            }
            else
                return std::nullopt;
        }
        if (options.log.empty() || options.instance.empty() || options.download.empty())
            return std::nullopt;
        return options;
    }

    // 자기 파일 끝의 판 표지다. 없으면 "0"이다.
    [[nodiscard]] std::u8string read_version()
    {
        const std::wstring self { luil::win32::current_executable_path() };
        std::ifstream file { std::filesystem::path { self }, std::ios::binary | std::ios::ate };
        if (file.is_open() == false)
            return u8"0";
        const std::streamoff size { file.tellg() };
        const std::streamoff tail { std::min<std::streamoff>(size, 64) };
        std::string bytes(static_cast<std::size_t>(tail), '\0');
        file.seekg(size - tail);
        file.read(bytes.data(), tail);
        const std::string marker { reinterpret_cast<const char*>(version_marker.data()), version_marker.size() };
        const std::size_t at { bytes.rfind(marker) };
        if (at == std::string::npos)
            return u8"0";
        std::u8string version {};
        for (std::size_t index = at + marker.size(); index < bytes.size() && bytes[index] >= '0' && bytes[index] <= '9'; ++index)
            version.push_back(static_cast<char8_t>(bytes[index]));
        return version.empty() ? std::u8string { u8"0" } : version;
    }

    class probe_log
    {
    public:
        explicit probe_log(std::filesystem::path path)
            : path_ { std::move(path) }
            , pid_ { std::to_string(GetCurrentProcessId()) }
        {
        }

        void write(const std::string_view key, const std::u8string_view value) const
        {
            std::ofstream file { path_, std::ios::binary | std::ios::app };
            file << pid_ << '\t' << key << '\t' << std::string_view { reinterpret_cast<const char*>(value.data()), value.size() } << '\n';
        }

    private:
        std::filesystem::path path_ {};
        std::string pid_ {};
    };

    [[nodiscard]] std::u8string ascii(const std::string_view text)
    {
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    // updater의 알림을 모아 끝 단계를 기다린다.
    class phase_wait
    {
    public:
        void add(const luil::update_status& status)
        {
            {
                const std::lock_guard lock { mutex_ };
                statuses_.push_back(status);
            }
            signal_.notify_all();
        }

        // `from`번째부터 처음 오는 끝 단계다.
        [[nodiscard]] std::optional<luil::update_status> wait_terminal(const std::size_t from)
        {
            std::unique_lock lock { mutex_ };
            std::optional<luil::update_status> found {};
            static_cast<void>(signal_.wait_for(lock, 60s, [&] {
                for (std::size_t index = from; index < statuses_.size(); ++index)
                {
                    const luil::update_phase phase { statuses_[index].phase };
                    if (phase == luil::update_phase::up_to_date || phase == luil::update_phase::available || phase == luil::update_phase::ready
                        || phase == luil::update_phase::handed_off || phase == luil::update_phase::failed)
                    {
                        found = statuses_[index];
                        return true;
                    }
                }
                return false;
            }));
            return found;
        }

        [[nodiscard]] std::size_t size() const
        {
            const std::lock_guard lock { mutex_ };
            return statuses_.size();
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable signal_ {};
        std::vector<luil::update_status> statuses_ {};
    };

    [[nodiscard]] std::u8string phase_text(const std::optional<luil::update_status>& status)
    {
        if (status.has_value() == false)
            return u8"timeout";
        switch (status->phase)
        {
        case luil::update_phase::up_to_date:
            return u8"up_to_date";
        case luil::update_phase::available:
            return u8"available";
        case luil::update_phase::ready:
            return u8"ready";
        case luil::update_phase::handed_off:
            return u8"handed_off";
        case luil::update_phase::failed:
            return u8"failed " + status->error;
        case luil::update_phase::idle:
        case luil::update_phase::checking:
        case luil::update_phase::downloading:
        case luil::update_phase::verifying:
        case luil::update_phase::applying:
            break;
        }
        return u8"other";
    }

    [[nodiscard]] luil::update_apply_options apply_options(const probe_options& options)
    {
        luil::update_apply_options apply {};
        apply.restart = options.restart;
        apply.restart_arguments = options.restart_arguments;
        apply.parent_timeout = options.parent_timeout;
        apply.confirm_timeout = options.confirm_timeout;
        return apply;
    }

    // updater로 확인·받기·적용을 끝까지 돈다. 단계마다 기록에 적는다.
    void run_update(luil::updater& updater, phase_wait& waits, const probe_options& options, const probe_log& log)
    {
        const auto step = [&](const std::string_view name, const auto call) {
            const std::size_t from { waits.size() };
            std::u8string error {};
            if (call(error) == false)
            {
                log.write("update", ascii(name) + u8" refused " + error);
                return std::optional<luil::update_status> {};
            }
            std::optional<luil::update_status> status { waits.wait_terminal(from) };
            log.write("update", ascii(name) + u8" " + phase_text(status));
            return status;
        };
        const std::optional<luil::update_status> checked { step("check", [&](std::u8string& error) { return updater.check(error); }) };
        if (checked.has_value() == false || checked->phase != luil::update_phase::available)
            return;
        const std::optional<luil::update_status> downloaded { step("download", [&](std::u8string& error) { return updater.download(error); }) };
        if (downloaded.has_value() == false || downloaded->phase != luil::update_phase::ready)
            return;
        const luil::update_apply_options apply { apply_options(options) };
        const std::optional<luil::update_status> applied { step("apply", [&](std::u8string& error) { return updater.apply(apply, error); }) };
        if (applied.has_value() == false || applied->phase != luil::update_phase::handed_off)
            return;
        // 넘긴 뒤로는 일을 받지 않는다.
        std::u8string error {};
        log.write("check_after_handoff", updater.check(error) ? u8"1" : u8"0");
    }

    // --- 사람 확인용 창 ---

    constexpr luil::ui_element_kind kind_probe_label { luil::application_element_kind(0) };

    struct probe_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct probe_close_intent
    {
    };

    class probe_driver final : public luil::win32::logic_driver
    {
    public:
        explicit probe_driver(std::u8string text)
            : text_ { std::move(text) }
        {
        }

        void handle(luil::app_message message) override
        {
            if (message.get<probe_close_intent>() != nullptr)
            {
                closed_.store(true);
                return;
            }
            if (const auto* const metrics { message.get<probe_metrics_intent>() }; metrics != nullptr)
                metrics_ = *metrics;
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
            const float width { metrics_.width > 0.0f ? metrics_.width : 480.0f };
            const float height { metrics_.height > 0.0f ? metrics_.height : 240.0f };
            auto root { std::make_unique<luil::root_element>() };
            root->arrange({ { 0.0f, 0.0f, width, height }, scale });
            const luil::caption_config caption_config { .title = u8"luil update probe" };
            const float caption_height { luil::caption_element::height_for(caption_config) * scale };
            auto caption { std::make_unique<luil::caption_element>(caption_config) };
            caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(caption));

            luil::stack_config column_config {};
            column_config.padding = luil::edge_insets::all(24.0f);
            auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_probe_label, u8"layout" }, column_config) };
            luil::label_config label_config {};
            label_config.text = text_;
            label_config.font_size = 16.0f;
            label_config.color = luil::label_color_role::primary;
            const float label_height { luil::label_element::height_for(label_config) };
            column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_probe_label, u8"version" }, std::move(label_config)), label_height);
            column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
            root->add(std::move(column));

            auto frame { std::make_shared<luil::win32::ui_frame>() };
            frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { probe_close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed_.load();
        }

    private:
        std::u8string text_ {};
        probe_metrics_intent metrics_ {};
        std::atomic<bool> closed_ { false };
    };

    class probe_delegate final : public luil::win32::window_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
        {
            return luil::app_message { probe_metrics_intent { width, height, scale } };
        }
    };

    [[nodiscard]] int show_window(const std::u8string& version, const bool applying)
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        const luil::win32::com_sta_scope com {};
        luil::win32::window_config config {};
        config.class_name = L"Luil.UpdateProbe.Window";
        config.caption.title = u8"luil update probe";
        config.initial_width = 480;
        config.initial_height = 240;
        const std::u8string text { u8"판 " + version + (applying ? u8" · 창을 닫으면 새 판으로 바뀐다" : u8"") };
        probe_driver driver { text };
        probe_delegate delegate {};
        luil::win32::window_environment environment {};
        environment.driver = &driver;
        environment.delegate = &delegate;
        return luil::win32::run_application_window(config, environment);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    const auto arguments { luil::win32::command_line_arguments() };
    if (arguments.has_value() == false)
        return 1;
    // 도우미 실행은 single_instance보다 먼저 갈라진다. 도우미는 시작 기록을 쓰지 않는다 (평소 길을 타지 않았다는 test의 근거다).
    if (const std::optional<int> helper { luil::run_update_helper(*arguments) }; helper.has_value())
        return *helper;
    const std::optional<probe_options> options { parse_options(*arguments) };
    if (options.has_value() == false)
        return 64;

    const luil::single_instance instance { options->instance };
    const std::u8string version { read_version() };
    const probe_log log { options->log };
    log.write("version", version);
    log.write("primary", instance.primary() ? u8"1" : u8"0");
    for (std::size_t index = 1; index < arguments->size(); ++index)
        log.write("arg", (*arguments)[index]);
    if (options->exit_version.has_value() && *options->exit_version == version)
        return options->exit_code;

    // 앱은 시작할 때 updater를 만든다. 지난 적용의 결과를 읽고 지켜보는 도우미에게 확인을 남긴다.
    phase_wait waits {};
    luil::updater_config config {};
    config.app_id = luil::testing::update_fixture_app;
    config.manifest_url = options->update_url.value_or(std::u8string { idle_manifest_url });
    config.channel = luil::testing::update_fixture_channel;
    config.current_version = version == u8"2" ? u8"1.0.1" : u8"1.0.0";
    config.keys.push_back(luil::update_key { std::u8string { luil::testing::update_fixture_key_id }, std::u8string { luil::testing::update_fixture_key_spki } });
    config.download_directory = options->download.u8string();
    config.download_chunk_bytes = 4u * 1024u * 1024u;
    config.user_agent = u8"luil-update-probe";
    config.use_system_proxy = false;
    config.report = [&waits](const luil::update_status& status) { waits.add(status); };
    std::u8string error {};
    std::unique_ptr<luil::updater> updater { luil::updater::create(std::move(config), error) };
    if (updater == nullptr)
    {
        log.write("last_apply", u8"create-failed " + error);
        return 65;
    }
    const std::optional<luil::update_apply_result> last { updater->last_apply() };
    log.write("last_apply", last.has_value() ? std::u8string { luil::update_apply_outcome_name(last->outcome) } : std::u8string { u8"none" });
    {
        // 다시 띄운 앱이 받기 폴더를 쓸 수 있는가다. 도우미가 다시 띄우기 전에 놓았어야 잡힌다.
        const luil::file_lock lock { (options->download / L"update.lock").u8string() };
        log.write("lock", lock.locked() ? u8"1" : u8"0");
    }

    if (options->update_url.has_value())
        run_update(*updater, waits, *options, log);
    else if (options->apply_sha256.has_value())
    {
        const std::optional<luil::update_digest> digest { luil::parse_update_digest(*options->apply_sha256) };
        luil::win32::update_helper_launch launch {};
        launch.helper_executable = options->download / std::filesystem::path { *options->apply_sha256 + u8".exe" };
        launch.old_executable = std::filesystem::path { luil::win32::current_executable_path() };
        launch.download_directory = options->download;
        launch.sha256 = digest.value_or(luil::update_digest {});
        launch.from_version = options->apply_from;
        launch.to_version = options->apply_to;
        launch.options = apply_options(*options);
        std::u8string launch_error {};
        const bool launched { digest.has_value() && luil::win32::launch_update_helper(launch, launch_error) };
        log.write("apply", launched ? std::u8string { u8"launched" } : u8"failed " + launch_error);
    }
    // 도우미가 기다리는 부모는 이 프로세스다. 늦게 끝나는 부모를 흉내 낸다.
    if (options->linger > 0ms)
        std::this_thread::sleep_for(options->linger);
    if (options->window)
        static_cast<void>(show_window(version, options->apply_sha256.has_value() || options->update_url.has_value()));
    if (options->hold > 0ms)
        std::this_thread::sleep_for(options->hold);
    updater.reset();
    return 0;
}

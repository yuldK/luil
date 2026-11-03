#include "luil/app/updater.h"

#include "host/update_manifest.h"
#include "loopback_http_server.h"
#include "luil/app/platform_services.h"
#include "update_fixtures.h"

#if defined(_WIN32)
#include "update_test_signing.h"

#include <windows.h>
#else
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// 자동 업데이트의 상태 기계를 되돌이 서버로 끝까지 돈다 (docs/auto-update-design.md의 확인 절).
//  - 미리 서명한 봉투만 쓰는 test는 Windows와 Android 기기(luil_net_tests)에서 같이 돈다.
//  - 받기와 그 자리에서 서명한 매니페스트가 드는 test는 Windows에서만 돈다 (Android는 받지 않고, 시험 서명은 CNG다).

namespace {
    using namespace std::chrono_literals;

    using luil::update_failure;
    using luil::update_phase;
    using luil::update_status;
    using luil::testing::loopback_http_server;
    using luil::testing::loopback_request;
    using luil::testing::loopback_response;

    constexpr std::chrono::milliseconds wait_limit { 20000 };

    [[nodiscard]] unsigned long process_number() noexcept
    {
#if defined(_WIN32)
        return GetCurrentProcessId();
#else
        return static_cast<unsigned long>(::getpid());
#endif
    }

    // 기기 셸에는 쓸 수 있는 임시 폴더가 정해져 있지 않다. 실행 파일이 도는 자리 아래에 둔다 (file_watcher_tests.cpp와 같다).
    [[nodiscard]] std::filesystem::path scratch_base()
    {
#if defined(__ANDROID__)
        return std::filesystem::current_path();
#else
        return std::filesystem::temp_directory_path();
#endif
    }

    // `%TEMP%\luil-update-<pid>-<n>\`이다. 끝나면 지운다. 끊긴 test가 남긴 것은 `%TEMP%\luil-update-*`를 지운다.
    class scratch_folder
    {
    public:
        scratch_folder()
            : path_ { scratch_base() / ("luil-update-" + std::to_string(process_number()) + "-" + std::to_string(++counter_)) }
        {
            std::error_code ignored {};
            std::filesystem::remove_all(path_, ignored);
            std::filesystem::create_directories(path_);
        }
        scratch_folder(const scratch_folder&) = delete;
        scratch_folder& operator=(const scratch_folder&) = delete;
        ~scratch_folder()
        {
            std::error_code ignored {};
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] std::filesystem::path file(const std::u8string_view name) const
        {
            return path_ / std::filesystem::path { std::u8string { name } };
        }

    private:
        static inline std::atomic<unsigned> counter_ { 0 };
        std::filesystem::path path_ {};
    };

    [[nodiscard]] bool terminal(const update_phase phase) noexcept
    {
        return phase == update_phase::up_to_date || phase == update_phase::available || phase == update_phase::ready || phase == update_phase::failed
            || phase == update_phase::handed_off;
    }

    // 알림을 모은다. 시각이 아니라 개수와 단계로 기다린다.
    class report_sink
    {
    public:
        void add(const update_status& status)
        {
            {
                const std::lock_guard lock { mutex_ };
                if (sealed_)
                    ++late_;
                statuses_.push_back(status);
                threads_.push_back(std::this_thread::get_id());
            }
            signal_.notify_all();
        }

        // `from`번째부터 처음 오는 끝 단계다. 시한 안에 오지 않으면 비었다.
        [[nodiscard]] std::optional<update_status> wait_terminal(const std::size_t from, const std::chrono::milliseconds limit = wait_limit)
        {
            std::unique_lock lock { mutex_ };
            std::optional<update_status> found {};
            static_cast<void>(signal_.wait_for(lock, limit, [&] {
                for (std::size_t index = from; index < statuses_.size(); ++index)
                    if (terminal(statuses_[index].phase))
                    {
                        found = statuses_[index];
                        return true;
                    }
                return false;
            }));
            return found;
        }

        // `from`번째부터 그 단계가 오기를 기다린다.
        [[nodiscard]] bool wait_phase(const update_phase phase, const std::size_t from, const std::chrono::milliseconds limit = wait_limit)
        {
            std::unique_lock lock { mutex_ };
            return signal_.wait_for(lock, limit, [&] {
                for (std::size_t index = from; index < statuses_.size(); ++index)
                    if (statuses_[index].phase == phase)
                        return true;
                return false;
            });
        }

        [[nodiscard]] std::size_t size() const
        {
            const std::lock_guard lock { mutex_ };
            return statuses_.size();
        }

        [[nodiscard]] std::vector<update_status> snapshot() const
        {
            const std::lock_guard lock { mutex_ };
            return statuses_;
        }

        [[nodiscard]] std::vector<std::thread::id> threads() const
        {
            const std::lock_guard lock { mutex_ };
            return threads_;
        }

        void seal()
        {
            const std::lock_guard lock { mutex_ };
            sealed_ = true;
        }

        [[nodiscard]] std::size_t late() const
        {
            const std::lock_guard lock { mutex_ };
            return late_;
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable signal_ {};
        std::vector<update_status> statuses_ {};
        std::vector<std::thread::id> threads_ {};
        bool sealed_ { false };
        std::size_t late_ { 0 };
    };

    [[nodiscard]] std::string ascii(const std::u8string_view text)
    {
        return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    // 봉투 하나와 asset 하나를 내는 서버다. asset은 `Range: bytes=a-b`에 206으로 답한다.
    class update_server
    {
    public:
        update_server()
            : state_ { std::make_shared<shared>() }
        {
            state_->envelope = ascii(luil::testing::update_fixture_envelope(true));
            state_->asset = luil::testing::update_fixture_asset();
            server_.set_handler([state = state_](const loopback_request& request) { return state->answer(request); });
        }

        [[nodiscard]] bool listening() const noexcept
        {
            return server_.port() != 0;
        }

        [[nodiscard]] std::u8string manifest_url() const
        {
            return server_.url("/update/manifest.json");
        }

        [[nodiscard]] std::u8string url(const std::string_view path) const
        {
            return server_.url(path);
        }

        void set_envelope(const std::u8string_view text)
        {
            const std::lock_guard lock { state_->mutex };
            state_->envelope = ascii(text);
        }

        void set_asset(std::vector<std::uint8_t> bytes)
        {
            const std::lock_guard lock { state_->mutex };
            state_->asset = std::move(bytes);
        }

        void set_ignore_range(const bool on)
        {
            const std::lock_guard lock { state_->mutex };
            state_->ignore_range = on;
        }

        // 그 번째(1부터) asset 요청의 몸을 반만 보내고 끊는다.
        void set_truncate_request(const std::size_t index)
        {
            const std::lock_guard lock { state_->mutex };
            state_->truncate_at = index;
        }

        // 그 번째(1부터) asset 요청부터 머리 앞에서 쉰다.
        void set_asset_delay(const std::chrono::milliseconds delay, const std::size_t from_request = 1)
        {
            const std::lock_guard lock { state_->mutex };
            state_->asset_delay = delay;
            state_->delay_from = from_request;
        }

        void set_manifest_delay(const std::chrono::milliseconds delay)
        {
            const std::lock_guard lock { state_->mutex };
            state_->manifest_delay = delay;
        }

        void set_manifest_status(const int status)
        {
            const std::lock_guard lock { state_->mutex };
            state_->manifest_status = status;
        }

        [[nodiscard]] std::size_t asset_requests() const
        {
            const std::lock_guard lock { state_->mutex };
            return state_->asset_count;
        }

        [[nodiscard]] std::size_t manifest_requests() const
        {
            const std::lock_guard lock { state_->mutex };
            return state_->manifest_count;
        }

        [[nodiscard]] std::vector<std::string> ranges() const
        {
            const std::lock_guard lock { state_->mutex };
            return state_->ranges;
        }

    private:
        struct shared
        {
            std::mutex mutex {};
            std::string envelope {};
            std::vector<std::uint8_t> asset {};
            bool ignore_range { false };
            std::size_t truncate_at { 0 };
            std::chrono::milliseconds asset_delay { 0 };
            std::size_t delay_from { 1 };
            std::chrono::milliseconds manifest_delay { 0 };
            int manifest_status { 200 };
            std::size_t asset_count { 0 };
            std::size_t manifest_count { 0 };
            std::vector<std::string> ranges {};

            [[nodiscard]] loopback_response answer(const loopback_request& request)
            {
                const std::lock_guard lock { mutex };
                loopback_response response {};
                if (request.target == "/update/manifest.json")
                {
                    ++manifest_count;
                    response.status = manifest_status;
                    response.headers.push_back({ "Content-Type", "application/json" });
                    response.body = luil::testing::loopback_bytes(envelope);
                    response.delay_before_headers = manifest_delay;
                    return response;
                }
                if (request.target != "/update/" + ascii(luil::testing::update_fixture_asset_name))
                {
                    response.status = 404;
                    return response;
                }
                ++asset_count;
                const std::string range { request.header("range").value_or(std::string {}) };
                ranges.push_back(range);
                response.headers.push_back({ "Content-Type", "application/octet-stream" });
                if (asset_count >= delay_from)
                    response.delay_before_headers = asset_delay;
                std::uint64_t first { 0 };
                std::uint64_t last { asset.empty() ? 0 : asset.size() - 1 };
                const std::string_view prefix { "bytes=" };
                const std::size_t dash { range.find('-') };
                const bool ranged { ignore_range == false && range.starts_with(prefix) && dash != std::string::npos };
                if (ranged)
                {
                    static_cast<void>(std::from_chars(range.data() + prefix.size(), range.data() + dash, first));
                    static_cast<void>(std::from_chars(range.data() + dash + 1, range.data() + range.size(), last));
                    if (first >= asset.size())
                    {
                        response.status = 416;
                        return response;
                    }
                    last = std::min<std::uint64_t>(last, asset.size() - 1);
                    response.status = 206;
                    response.headers.push_back({ "Content-Range", "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(asset.size()) });
                }
                response.body.assign(asset.begin() + static_cast<std::ptrdiff_t>(first), asset.begin() + static_cast<std::ptrdiff_t>(last + 1));
                if (truncate_at != 0 && asset_count == truncate_at)
                    response.truncate_body_after = response.body.size() / 2;
                return response;
            }
        };

        std::shared_ptr<shared> state_ {};
        // 서버가 마지막에 서고 먼저 진다. 처리기가 쥔 상태는 그 뒤에 진다.
        loopback_http_server server_ {};
    };

    [[nodiscard]] luil::updater_config make_config(const update_server& server, const scratch_folder& folder, report_sink& sink, const std::u8string_view current = u8"1.0.0")
    {
        luil::updater_config config {};
        config.app_id = luil::testing::update_fixture_app;
        config.manifest_url = server.manifest_url();
        config.channel = luil::testing::update_fixture_channel;
        config.current_version = current;
        config.keys.push_back(luil::update_key { std::u8string { luil::testing::update_fixture_key_id }, std::u8string { luil::testing::update_fixture_key_spki } });
        config.download_directory = folder.path().u8string();
        config.download_chunk_bytes = 1024u * 1024u;
        config.user_agent = u8"luil-tests";
        config.use_system_proxy = false;
        config.report = [&sink](const update_status& status) { sink.add(status); };
        return config;
    }

    [[nodiscard]] std::unique_ptr<luil::updater> make_updater(luil::updater_config config)
    {
        std::u8string error {};
        std::unique_ptr<luil::updater> updater { luil::updater::create(std::move(config), error) };
        INFO(ascii(error));
        REQUIRE(updater != nullptr);
        return updater;
    }

    [[nodiscard]] update_status run_check(luil::updater& updater, report_sink& sink)
    {
        const std::size_t from { sink.size() };
        std::u8string error {};
        REQUIRE(updater.check(error));
        const std::optional<update_status> status { sink.wait_terminal(from) };
        REQUIRE(status.has_value());
        return *status;
    }

    [[nodiscard]] std::vector<update_phase> phases_from(const report_sink& sink, const std::size_t from)
    {
        std::vector<update_phase> phases {};
        const std::vector<update_status> statuses { sink.snapshot() };
        for (std::size_t index = from; index < statuses.size(); ++index)
            phases.push_back(statuses[index].phase);
        return phases;
    }

    void write_file(const std::filesystem::path& path, const std::span<const std::uint8_t> bytes)
    {
        std::ofstream file { path, std::ios::binary | std::ios::trunc };
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    [[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
    {
        std::ifstream file { path, std::ios::binary };
        return std::vector<std::uint8_t> { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
    }

    [[nodiscard]] bool contains(const std::u8string_view text, const std::u8string_view part)
    {
        return text.find(part) != std::u8string_view::npos;
    }
}

TEST_CASE("an updater refuses a configuration it cannot keep", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    const luil::updater_config good { make_config(server, folder, sink) };

    const auto refused {
        [](luil::updater_config config) {
            std::u8string error {};
            const bool failed { luil::updater::create(std::move(config), error) == nullptr };
            CHECK(error.empty() == false);
            return failed;
        },
    };
    luil::updater_config config { good };
    config.current_version = u8"1.0-beta";
    CHECK(refused(config));
    config = good;
    config.app_id.clear();
    CHECK(refused(config));
    config = good;
    config.manifest_url = u8"http://example.com/update/manifest.json";
    CHECK(refused(config));
    config = good;
    config.keys.clear();
    CHECK(refused(config));
    config = good;
    config.keys.push_back(config.keys.front());
    CHECK(refused(config));
    config = good;
    config.keys.front().spki_base64 = u8"AAAA";
    CHECK(refused(config));
    config = good;
    config.download_chunk_bytes = luil::update_minimum_chunk_bytes - 1;
    CHECK(refused(config));
    config = good;
    config.download_chunk_bytes = luil::update_maximum_chunk_bytes + 1;
    CHECK(refused(config));
    config = good;
    config.download_directory.clear();
    CHECK(refused(config));

    std::u8string error {};
    CHECK(luil::updater::create(good, error) != nullptr);
}

TEST_CASE("the same offered version is up to date", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink, u8"1.0.1")) };
    const update_status status { run_check(*updater, sink) };
    CHECK(status.phase == update_phase::up_to_date);
    CHECK(status.offer.has_value() == false);
    CHECK(phases_from(sink, 0) == std::vector<update_phase> { update_phase::checking, update_phase::up_to_date });
    // 받아들인 단계까지 updater의 thread가 알린다. 부른 thread가 아니다.
    for (const std::thread::id thread : sink.threads())
        CHECK(thread != std::this_thread::get_id());
}

TEST_CASE("a lower offered version is never offered", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink, u8"2.0")) };
    CHECK(run_check(*updater, sink).phase == update_phase::up_to_date);
}

TEST_CASE("a newer version is available with its offer", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    const update_status status { run_check(*updater, sink) };
    REQUIRE(status.phase == update_phase::available);
    REQUIRE(status.offer.has_value());
    CHECK(status.offer->version == luil::testing::update_fixture_version);
    CHECK(status.offer->notes == luil::testing::update_fixture_notes);
    CHECK(status.offer->notes_url == luil::testing::update_fixture_notes_url);
#if defined(__ANDROID__)
    CHECK(status.offer->size == 0);
    CHECK(status.offer->store_url == luil::testing::update_fixture_store_url);
#else
    CHECK(status.offer->size == luil::testing::update_fixture_asset_size);
    CHECK(status.offer->store_url.empty());
    CHECK(server.asset_requests() == 0);
#endif
    // 지난 매니페스트의 sequence가 받기 폴더에 남는다.
    const std::optional<luil::update_state> state { luil::parse_update_state(read_file(folder.file(u8"update-state.json"))) };
    REQUIRE(state.has_value());
    CHECK(state->sequence == luil::testing::update_fixture_sequence);
}

TEST_CASE("a changed payload or signature does not pass", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    for (const std::u8string& envelope : { luil::testing::update_fixture_tampered_envelope(), luil::testing::update_fixture_broken_signature_envelope() })
    {
        server.set_envelope(envelope);
        const update_status status { run_check(*updater, sink) };
        CHECK(status.phase == update_phase::failed);
        CHECK(status.failure == update_failure::signature_invalid);
        CHECK(contains(status.error, u8"matches"));
        CHECK(status.offer.has_value() == false);
    }
    // 서명을 지나지 못한 매니페스트는 sequence를 남기지 않는다.
    CHECK(std::filesystem::exists(folder.file(u8"update-state.json")) == false);
}

TEST_CASE("a signature from an unknown or different key does not pass", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    luil::updater_config unknown { make_config(server, folder, sink) };
    unknown.keys.front().id = luil::testing::update_fixture_other_key_id;
    unknown.keys.front().spki_base64 = luil::testing::update_fixture_other_key_spki;
    std::unique_ptr<luil::updater> first { make_updater(std::move(unknown)) };
    const update_status status { run_check(*first, sink) };
    CHECK(status.failure == update_failure::signature_invalid);
    CHECK(contains(status.error, u8"knows"));
    first->stop();

    // 이름은 같은데 키가 다르면 "맞지 않는다"다.
    luil::updater_config other { make_config(server, folder, sink) };
    other.keys.front().spki_base64 = luil::testing::update_fixture_other_key_spki;
    std::unique_ptr<luil::updater> second { make_updater(std::move(other)) };
    const update_status mismatch { run_check(*second, sink) };
    CHECK(mismatch.failure == update_failure::signature_invalid);
    CHECK(contains(mismatch.error, u8"matches"));

    // 키를 바꾸는 동안 두 키를 싣고, 아는 키의 서명 하나만 맞아도 지난다.
    luil::updater_config rotating { make_config(server, folder, sink) };
    rotating.keys.insert(rotating.keys.begin(), luil::update_key { std::u8string { luil::testing::update_fixture_other_key_id }, std::u8string { luil::testing::update_fixture_other_key_spki } });
    std::unique_ptr<luil::updater> third { make_updater(std::move(rotating)) };
    CHECK(run_check(*third, sink).phase == update_phase::available);
}

TEST_CASE("another app or channel is a manifest mismatch", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    luil::updater_config beta { make_config(server, folder, sink) };
    beta.channel = u8"beta";
    std::unique_ptr<luil::updater> first { make_updater(std::move(beta)) };
    CHECK(run_check(*first, sink).failure == update_failure::manifest_mismatch);
    luil::updater_config other { make_config(server, folder, sink) };
    other.app_id = u8"other.app";
    std::unique_ptr<luil::updater> second { make_updater(std::move(other)) };
    CHECK(run_check(*second, sink).failure == update_failure::manifest_mismatch);
}

TEST_CASE("a lower sequence than this computer has seen is replayed", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    write_file(folder.file(u8"update-state.json"), luil::serialize_update_state(luil::update_state { luil::testing::update_fixture_sequence + 1 }));
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    const update_status status { run_check(*updater, sink) };
    CHECK(status.phase == update_phase::failed);
    CHECK(status.failure == update_failure::replayed);
    // 기록은 내려가지 않는다.
    CHECK(luil::parse_update_state(read_file(folder.file(u8"update-state.json")))->sequence == luil::testing::update_fixture_sequence + 1);
}

TEST_CASE("transport and envelope failures are told apart", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };

    server.set_manifest_status(404);
    const update_status missing { run_check(*updater, sink) };
    CHECK(missing.failure == update_failure::http_status);
    CHECK(missing.http_status == 404);

    server.set_manifest_status(200);
    server.set_envelope(u8"{\"format\":\"luil-update-envelope/1\"}");
    CHECK(run_check(*updater, sink).failure == update_failure::manifest_invalid);
    server.set_envelope(u8"not json at all");
    CHECK(run_check(*updater, sink).failure == update_failure::manifest_invalid);

    luil::testing::loopback_dead_port dead {};
    REQUIRE(dead.port() != 0);
    luil::updater_config config { make_config(server, folder, sink) };
    config.manifest_url = dead.url("/update/manifest.json");
    std::unique_ptr<luil::updater> offline { make_updater(std::move(config)) };
    CHECK(run_check(*offline, sink).failure == update_failure::network);
}

TEST_CASE("an updater takes one job at a time and none after stop", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    server.set_manifest_delay(500ms);
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    std::u8string error {};
    // 판을 내주기 전에는 받지 않는다.
    CHECK(updater->download(error) == false);
    CHECK(error.empty() == false);
    REQUIRE(updater->check(error));
    error.clear();
    CHECK(updater->check(error) == false);
    CHECK(error.empty() == false);
    REQUIRE(sink.wait_terminal(0).has_value());
    updater->stop();
    error.clear();
    CHECK(updater->check(error) == false);
    CHECK(updater->download(error) == false);
    // 멱등이다.
    updater->stop();
}

TEST_CASE("cancel ends a check as cancelled", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    server.set_manifest_delay(5000ms);
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    std::u8string error {};
    REQUIRE(updater->check(error));
    REQUIRE(sink.wait_phase(update_phase::checking, 0));
    updater->cancel();
    const std::optional<update_status> status { sink.wait_terminal(0, 4000ms) };
    REQUIRE(status.has_value());
    CHECK(status->phase == update_phase::failed);
    CHECK(status->failure == update_failure::cancelled);
    // 일하지 않을 때의 취소는 아무 일도 없다.
    const std::size_t count { sink.size() };
    updater->cancel();
    std::this_thread::sleep_for(100ms);
    CHECK(sink.size() == count);
}

TEST_CASE("no report arrives after stop returns", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    server.set_manifest_delay(2000ms);
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    std::u8string error {};
    REQUIRE(updater->check(error));
    REQUIRE(sink.wait_phase(update_phase::checking, 0));
    const auto started { std::chrono::steady_clock::now() };
    updater->stop();
    sink.seal();
    // 멈추기는 날아가 있는 요청을 접고 곧 돌아온다.
    CHECK(std::chrono::steady_clock::now() - started < 1500ms);
    std::this_thread::sleep_for(300ms);
    CHECK(sink.late() == 0);
    updater.reset();
    CHECK(sink.late() == 0);
}

#if defined(__ANDROID__)
TEST_CASE("Android checks but does not download", "[update][updater][android]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    std::u8string error {};
    CHECK(updater->download(error) == false);
    CHECK(contains(error, u8"unsupported"));
    CHECK(server.asset_requests() == 0);
}

TEST_CASE("Android refuses a store address outside the allowed schemes", "[update][updater][android]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    luil::updater_config config { make_config(server, folder, sink) };
    config.store_schemes = { u8"market" };
    std::unique_ptr<luil::updater> updater { make_updater(std::move(config)) };
    CHECK(run_check(*updater, sink).failure == update_failure::manifest_invalid);
}
#endif

#if defined(_WIN32)
namespace {
    [[nodiscard]] update_status run_download(luil::updater& updater, report_sink& sink)
    {
        const std::size_t from { sink.size() };
        std::u8string error {};
        const bool accepted { updater.download(error) };
        INFO(ascii(error));
        REQUIRE(accepted);
        const std::optional<update_status> status { sink.wait_terminal(from) };
        REQUIRE(status.has_value());
        return *status;
    }

    [[nodiscard]] std::filesystem::path ready_file(const scratch_folder& folder)
    {
        return folder.file(std::u8string { luil::testing::update_fixture_asset_sha256 } + u8".exe");
    }

    [[nodiscard]] std::filesystem::path part_file(const scratch_folder& folder)
    {
        return folder.file(std::u8string { luil::testing::update_fixture_asset_sha256 } + u8".part");
    }

    // 고정 payload의 글 하나를 모두 바꿔 그 자리에서 서명한 봉투다.
    [[nodiscard]] std::u8string signed_variant(const std::u8string_view from, const std::u8string_view to)
    {
        std::u8string payload { luil::testing::update_fixture_payload };
        std::size_t at { payload.find(from) };
        REQUIRE(at != std::u8string::npos);
        while (at != std::u8string::npos)
        {
            payload.replace(at, from.size(), to);
            at = payload.find(from, at + to.size());
        }
        const std::u8string envelope { luil::testing::make_signed_update_envelope(payload, true) };
        REQUIRE(envelope.empty() == false);
        return envelope;
    }
}

TEST_CASE("a manifest without this platform is no_asset with its offer", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    // 두 Windows 칸을 모르는 이름으로 바꾼다. 이 빌드가 어느 쪽이든 칸이 없다.
    server.set_envelope(signed_variant(u8"\"windows-", u8"\"later-"));
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    const update_status status { run_check(*updater, sink) };
    CHECK(status.failure == update_failure::no_asset);
    REQUIRE(status.offer.has_value());
    CHECK(status.offer->version == luil::testing::update_fixture_version);
    CHECK(status.offer->notes_url == luil::testing::update_fixture_notes_url);
    std::u8string error {};
    CHECK(updater->download(error) == false);
}

TEST_CASE("an asset over the size limit is too_large with its offer", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    luil::updater_config config { make_config(server, folder, sink) };
    config.max_asset_bytes = 1024u * 1024u;
    std::unique_ptr<luil::updater> updater { make_updater(std::move(config)) };
    const update_status status { run_check(*updater, sink) };
    CHECK(status.failure == update_failure::too_large);
    REQUIRE(status.offer.has_value());
    CHECK(status.offer->size == luil::testing::update_fixture_asset_size);
}

TEST_CASE("a signed sequence is remembered and an older manifest is then replayed", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    server.set_envelope(signed_variant(u8"\"sequence\":7", u8"\"sequence\":9"));
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    CHECK(run_check(*updater, sink).phase == update_phase::available);
    CHECK(luil::parse_update_state(read_file(folder.file(u8"update-state.json")))->sequence == 9);
    server.set_envelope(luil::testing::update_fixture_envelope(false));
    CHECK(run_check(*updater, sink).failure == update_failure::replayed);
}

TEST_CASE("an absolute asset address is followed", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    const std::u8string absolute { server.url("/update/" + ascii(luil::testing::update_fixture_asset_name)) };
    server.set_envelope(signed_variant(u8"\"url\":\"fixture-1.0.1.bin\"", u8"\"url\":\"" + absolute + u8"\""));
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    CHECK(run_download(*updater, sink).phase == update_phase::ready);
}

TEST_CASE("a chunked download reports growing progress and ends ready", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    luil::updater_config config { make_config(server, folder, sink) };
    config.download_chunk_bytes = 512u * 1024u;
    std::unique_ptr<luil::updater> updater { make_updater(std::move(config)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    const std::size_t from { sink.size() };
    const update_status status { run_download(*updater, sink) };
    REQUIRE(status.phase == update_phase::ready);
    CHECK(status.received == luil::testing::update_fixture_asset_size);

    const std::vector<update_status> statuses { sink.snapshot() };
    std::uint64_t previous { 0 };
    std::size_t downloading { 0 };
    std::uint64_t last_received { 0 };
    for (std::size_t index = from; index < statuses.size(); ++index)
    {
        if (statuses[index].phase != update_phase::downloading)
            continue;
        ++downloading;
        CHECK(statuses[index].received >= previous);
        CHECK(statuses[index].total == luil::testing::update_fixture_asset_size);
        REQUIRE(statuses[index].offer.has_value());
        previous = statuses[index].received;
        last_received = statuses[index].received;
    }
    // 처음 하나와 덩이마다 하나다.
    CHECK(downloading == 1 + 6);
    CHECK(last_received == luil::testing::update_fixture_asset_size);
    CHECK(phases_from(sink, statuses.size() - 2) == std::vector<update_phase> { update_phase::verifying, update_phase::ready });

    const std::vector<std::string> ranges { server.ranges() };
    REQUIRE(ranges.size() == 6);
    CHECK(ranges.front() == "bytes=0-524287");
    CHECK(ranges.back() == "bytes=2621440-3145727");
    CHECK(read_file(ready_file(folder)) == luil::testing::update_fixture_asset());
    CHECK(std::filesystem::exists(part_file(folder)) == false);
    for (const std::thread::id thread : sink.threads())
        CHECK(thread != std::this_thread::get_id());
}

TEST_CASE("a server that ignores Range fails a large file but serves a small one", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    server.set_ignore_range(true);
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    CHECK(run_download(*updater, sink).failure == update_failure::range_unsupported);
    updater->stop();

    // 덩이보다 작은 파일은 200 한 번으로 받는다.
    luil::updater_config whole { make_config(server, folder, sink) };
    whole.download_chunk_bytes = 4u * 1024u * 1024u;
    std::unique_ptr<luil::updater> second { make_updater(std::move(whole)) };
    REQUIRE(run_check(*second, sink).phase == update_phase::available);
    CHECK(run_download(*second, sink).phase == update_phase::ready);
    CHECK(read_file(ready_file(folder)) == luil::testing::update_fixture_asset());
}

TEST_CASE("changed bytes are a hash mismatch and the partial file goes", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    std::vector<std::uint8_t> tampered { luil::testing::update_fixture_asset() };
    tampered[1234567] ^= 0x01;
    server.set_asset(std::move(tampered));
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    CHECK(run_download(*updater, sink).failure == update_failure::hash_mismatch);
    CHECK(std::filesystem::exists(part_file(folder)) == false);
    CHECK(std::filesystem::exists(ready_file(folder)) == false);
}

TEST_CASE("a download cut inside a chunk resumes from the file length", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    server.set_truncate_request(3);
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    CHECK(run_download(*updater, sink).failure == update_failure::network);
    REQUIRE(std::filesystem::exists(part_file(folder)));
    const std::uintmax_t kept { std::filesystem::file_size(part_file(folder)) };
    CHECK(kept == 2u * 1024u * 1024u);

    server.set_truncate_request(0);
    const std::size_t asked { server.ranges().size() };
    const update_status status { run_download(*updater, sink) };
    CHECK(status.phase == update_phase::ready);
    const std::vector<std::string> ranges { server.ranges() };
    REQUIRE(ranges.size() > asked);
    CHECK(ranges[asked] == "bytes=" + std::to_string(kept) + "-3145727");
    CHECK(read_file(ready_file(folder)) == luil::testing::update_fixture_asset());
}

TEST_CASE("a file already downloaded is ready without asking for it again", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    {
        std::unique_ptr<luil::updater> first { make_updater(make_config(server, folder, sink)) };
        REQUIRE(run_check(*first, sink).phase == update_phase::available);
        REQUIRE(run_download(*first, sink).phase == update_phase::ready);
    }
    const std::size_t assets { server.asset_requests() };
    const std::size_t manifests { server.manifest_requests() };
    std::unique_ptr<luil::updater> second { make_updater(make_config(server, folder, sink)) };
    const std::size_t from { sink.size() };
    const update_status status { run_check(*second, sink) };
    CHECK(status.phase == update_phase::ready);
    CHECK(phases_from(sink, from) == std::vector<update_phase> { update_phase::checking, update_phase::verifying, update_phase::ready });
    CHECK(server.asset_requests() == assets);
    CHECK(server.manifest_requests() == manifests + 1);

    // 다른 바이트가 그 이름으로 있으면 지우고 받을 판으로 알린다.
    std::vector<std::uint8_t> wrong { luil::testing::update_fixture_asset() };
    wrong[0] ^= 0x01;
    write_file(ready_file(folder), wrong);
    CHECK(run_check(*second, sink).phase == update_phase::available);
    CHECK(std::filesystem::exists(ready_file(folder)) == false);
}

TEST_CASE("another holder of the download lock makes the download busy", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    {
        const luil::file_lock holder { folder.file(u8"update.lock").u8string() };
        REQUIRE(holder.locked());
        CHECK(run_download(*updater, sink).failure == update_failure::busy);
        CHECK(server.asset_requests() == 0);
    }
    CHECK(run_download(*updater, sink).phase == update_phase::ready);
}

TEST_CASE("cancel stops a download and the next download continues it", "[update][updater]")
{
    update_server server {};
    REQUIRE(server.listening());
    scratch_folder folder {};
    report_sink sink {};
    std::unique_ptr<luil::updater> updater { make_updater(make_config(server, folder, sink)) };
    REQUIRE(run_check(*updater, sink).phase == update_phase::available);
    // 첫 덩이를 받은 뒤 둘째에서 오래 머문다.
    server.set_asset_delay(3000ms, 2);
    std::u8string error {};
    const std::size_t from { sink.size() };
    REQUIRE(updater->download(error));
    const auto first_chunk {
        [&] {
            for (const update_status& status : sink.snapshot())
                if (status.phase == update_phase::downloading && status.received > 0)
                    return true;
            return false;
        },
    };
    const auto deadline { std::chrono::steady_clock::now() + wait_limit };
    while (first_chunk() == false && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    updater->cancel();
    const std::optional<update_status> status { sink.wait_terminal(from, 5000ms) };
    REQUIRE(status.has_value());
    CHECK(status->failure == update_failure::cancelled);
    CHECK(std::filesystem::exists(part_file(folder)));

    server.set_asset_delay(0ms);
    CHECK(run_download(*updater, sink).phase == update_phase::ready);
    CHECK(read_file(ready_file(folder)) == luil::testing::update_fixture_asset());
}
#endif

#include "luil/app/updater.h"

#include "host/update_manifest.h"
#include "luil/app/platform_services.h"
#include "luil/net/http_body.h"
#include "luil/net/http_client.h"
#include "luil/net/http_message.h"
#include "net/update_crypto.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// 자동 업데이트의 상태 기계다 (docs/auto-update-design.md).
//
// **thread가 둘이다.** 하나는 updater가 쥔 `http_client`의 것이고 하나는 updater의 것이다. client의 `deliver`는 답을 updater의
// 일감 줄에 넣기만 하고, 단계 바꾸기·서명 확인·파일 쓰기·해시·`report`는 모두 updater의 thread에서 차례대로 돈다. 그래서
//  - `check`·`download`를 부른 thread에서 `report`가 불리지 않는다. 받아들인 단계(`checking`·`downloading`)도 updater의 thread가 알린다.
//  - 큰 파일을 다시 해시하는 동안에도 client의 thread가 막히지 않고, 해시 고리가 1 MiB마다 `cancel`·`stop`을 본다.
namespace luil {
    namespace {
        constexpr std::size_t manifest_body_limit { 1024u * 1024u };
        constexpr std::size_t hash_block_bytes { 1024u * 1024u };
        constexpr std::chrono::milliseconds chunk_total_timeout { 5 * 60 * 1000 };
        constexpr std::u8string_view state_file_name { u8"update-state.json" };
        constexpr std::u8string_view lock_file_name { u8"update.lock" };

        [[nodiscard]] bool downloads_supported() noexcept
        {
            return current_update_platform() != update_platform::android;
        }

        void append_number(std::u8string& text, const std::uint64_t value)
        {
            const std::string digits { std::to_string(value) };
            text.append(digits.begin(), digits.end());
        }

        // OS 오류 글은 지역 code page일 수 있어 싣지 않는다. 번호만 붙인다.
        [[nodiscard]] std::u8string with_code(std::u8string message, const std::error_code& code)
        {
            message += u8" (error ";
            if (code.value() < 0)
            {
                message.push_back(u8'-');
                append_number(message, static_cast<std::uint64_t>(-static_cast<long long>(code.value())));
            }
            else
                append_number(message, static_cast<std::uint64_t>(code.value()));
            message += u8").";
            return message;
        }

        [[nodiscard]] std::filesystem::path file_in(const std::filesystem::path& directory, const std::u8string_view name)
        {
            return directory / std::filesystem::path { std::u8string { name } };
        }

        [[nodiscard]] std::vector<std::uint8_t> read_small_file(const std::filesystem::path& path)
        {
            std::ifstream file { path, std::ios::binary };
            if (file.is_open() == false)
                return {};
            std::vector<std::uint8_t> bytes {};
            char buffer[4096] {};
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
            {
                bytes.insert(bytes.end(), reinterpret_cast<const std::uint8_t*>(buffer), reinterpret_cast<const std::uint8_t*>(buffer) + file.gcount());
                // 상태 파일은 작다. 큰 파일은 꼴이 틀린 것으로 본다.
                if (bytes.size() > 64u * 1024u)
                    return {};
            }
            return bytes;
        }

        // 해시 고리가 멈춘 까닭이다.
        enum class feed_outcome
        {
            done,
            cancelled,
            stopped,
            failed,
        };
    }

    struct updater::engine
    {
        // --- 만든 뒤 바뀌지 않는다 ---
        updater_config config {};
        update_version current {};
        // `config.keys`와 같은 차례의 SPKI DER이다.
        std::vector<std::vector<std::uint8_t>> key_bytes {};
        std::filesystem::path directory {};
        std::unique_ptr<net::http_client> client {};
        std::thread worker {};

        // --- `mutex` 아래 ---
        std::mutex mutex {};
        std::condition_variable wake {};
        std::deque<std::function<void()>> tasks {};
        bool quitting { false };
        // `check`·`download`를 받아들인 뒤 끝 단계를 알릴 때까지다.
        bool busy { false };
        // 마지막 확인이 받을 판을 내주었다 (`download`의 조건).
        bool has_offer { false };
        net::http_ticket in_flight {};
        std::thread::id worker_id {};

        std::atomic<bool> stopping { false };
        std::atomic<bool> cancel_requested { false };
        // `stop`이 둘 함께 불려도 뒤엣것이 앞엣것의 끝을 기다린다.
        std::mutex stop_mutex {};

        // --- updater의 thread만 ---
        enum class waiting
        {
            none,
            manifest,
            chunk,
        };

        struct download_plan
        {
            std::u8string url {};
            std::uint64_t size { 0 };
            update_digest sha256 {};
        };

        update_status status {};
        download_plan plan {};
        waiting waiting_for { waiting::none };
        std::optional<file_lock> lock {};
        std::ofstream part {};
        std::optional<net::update_sha256> hasher {};
        std::uint64_t received { 0 };
        std::uint64_t requested_first { 0 };
        std::uint64_t requested_last { 0 };

        void post(std::function<void()> task)
        {
            {
                const std::lock_guard guard { mutex };
                if (quitting)
                    return;
                tasks.push_back(std::move(task));
            }
            wake.notify_one();
        }

        void run()
        {
            {
                const std::lock_guard guard { mutex };
                worker_id = std::this_thread::get_id();
            }
            while (true)
            {
                std::function<void()> task {};
                {
                    std::unique_lock guard { mutex };
                    wake.wait(guard, [this] { return quitting || tasks.empty() == false; });
                    if (quitting)
                        return;
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
                try
                {
                    task();
                }
                catch (...)
                {
                    // 일감 하나가 던지면 단계가 바쁨에 멈춰 다시는 받지 않는다. 실패로 닫는다.
                    finish(update_phase::failed, update_failure::disk, u8"An unexpected error stopped the update step.");
                }
            }
        }

        [[nodiscard]] std::filesystem::path state_path() const
        {
            return file_in(directory, state_file_name);
        }

        [[nodiscard]] std::filesystem::path ready_path() const
        {
            return file_in(directory, format_update_digest(plan.sha256) + u8".exe");
        }

        [[nodiscard]] std::filesystem::path part_path() const
        {
            return file_in(directory, format_update_digest(plan.sha256) + u8".part");
        }

        void emit()
        {
            if (stopping.load() || !config.report)
                return;
            try
            {
                config.report(status);
            }
            catch (...)
            {
                // 계약은 던지지 않는 것이다. 던져도 상태 기계는 이어 간다.
            }
        }

        void close_download() noexcept
        {
            if (part.is_open())
                part.close();
            hasher.reset();
            lock.reset();
            waiting_for = waiting::none;
        }

        // 끝 단계 하나를 알리고 다음 `check`·`download`를 받는다.
        void finish(const update_phase phase, const update_failure failure, std::u8string error)
        {
            close_download();
            status.phase = phase;
            status.failure = phase == update_phase::failed ? failure : update_failure::none;
            status.error = std::move(error);
            {
                const std::lock_guard guard { mutex };
                busy = false;
                in_flight = {};
            }
            // `stopped`는 `stop` 중의 끊김이라 알리지 않는다 (그 뒤로 `report`가 없다는 계약).
            if (failure != update_failure::stopped)
                emit();
        }

        void finish_cancelled()
        {
            finish(update_phase::failed, update_failure::cancelled, u8"The update step was cancelled.");
        }

        // 바쁨을 끊을 까닭이 있으면 닫고 참이다.
        [[nodiscard]] bool interrupted()
        {
            if (stopping.load())
            {
                finish(update_phase::failed, update_failure::stopped, u8"The updater stopped.");
                return true;
            }
            if (cancel_requested.load())
            {
                finish_cancelled();
                return true;
            }
            return false;
        }

        // 요청 하나를 낸다. 거절되면 실패로 닫고 거짓이다.
        [[nodiscard]] bool send(net::http_request request, const waiting what)
        {
            std::u8string error {};
            const net::http_ticket ticket { client->send(std::move(request), error) };
            if (ticket == net::http_ticket {})
            {
                finish(update_phase::failed, stopping.load() ? update_failure::stopped : update_failure::network, std::move(error));
                return false;
            }
            waiting_for = what;
            {
                const std::lock_guard guard { mutex };
                in_flight = ticket;
            }
            // `cancel`이 표가 서기 전에 왔으면 여기서 건다.
            if (cancel_requested.load())
                client->cancel(ticket);
            return true;
        }

        // 전송 오류를 실패로 닫았으면 참이다.
        [[nodiscard]] bool transport_failed(const net::http_response& response, const update_failure too_large)
        {
            switch (response.error.kind)
            {
            case net::http_error_kind::none:
                return false;
            case net::http_error_kind::cancelled:
                finish_cancelled();
                return true;
            case net::http_error_kind::stopped:
                finish(update_phase::failed, update_failure::stopped, response.error.message);
                return true;
            case net::http_error_kind::body_too_large:
                finish(update_phase::failed, too_large, response.error.message);
                return true;
            case net::http_error_kind::invalid_url:
            case net::http_error_kind::unsupported_scheme:
            case net::http_error_kind::invalid_header:
            case net::http_error_kind::invalid_method:
            case net::http_error_kind::name_not_resolved:
            case net::http_error_kind::cannot_connect:
            case net::http_error_kind::connection_lost:
            case net::http_error_kind::secure_failure:
            case net::http_error_kind::timed_out:
            case net::http_error_kind::too_many_redirects:
            case net::http_error_kind::system_error:
                break;
            }
            finish(update_phase::failed, update_failure::network, response.error.message);
            return true;
        }

        // 파일 앞 `length` 바이트를 해시에 먹인다. 1 MiB마다 `cancel`·`stop`을 본다.
        [[nodiscard]] feed_outcome feed_file(const std::filesystem::path& path, std::uint64_t length, net::update_sha256& target, std::u8string& error)
        {
            std::ifstream file { path, std::ios::binary };
            if (file.is_open() == false)
            {
                error = u8"Failed to open a downloaded file for hashing.";
                return feed_outcome::failed;
            }
            std::vector<char> buffer(hash_block_bytes);
            while (length > 0)
            {
                if (stopping.load())
                    return feed_outcome::stopped;
                if (cancel_requested.load())
                    return feed_outcome::cancelled;
                const std::size_t count { static_cast<std::size_t>(std::min<std::uint64_t>(length, buffer.size())) };
                if (file.read(buffer.data(), static_cast<std::streamsize>(count)).gcount() != static_cast<std::streamsize>(count))
                {
                    error = u8"Failed to read a downloaded file for hashing.";
                    return feed_outcome::failed;
                }
                if (target.update(std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(buffer.data()), count }) == false)
                {
                    error = target.error();
                    return feed_outcome::failed;
                }
                length -= count;
            }
            return feed_outcome::done;
        }

        // 해시 고리가 끊긴 까닭을 실패로 닫는다.
        void finish_feed(const feed_outcome outcome, std::u8string error)
        {
            switch (outcome)
            {
            case feed_outcome::done:
                return;
            case feed_outcome::cancelled:
                finish_cancelled();
                return;
            case feed_outcome::stopped:
                finish(update_phase::failed, update_failure::stopped, u8"The updater stopped.");
                return;
            case feed_outcome::failed:
                finish(update_phase::failed, update_failure::disk, std::move(error));
                return;
            }
        }

        // 받아 둔 `<sha256>.exe`가 매니페스트의 바이트와 같은가. 다르면 지운다.
        //  - 비었으면 계속하고, 값이 있으면 그 단계로 닫았다 (참이면 `ready`).
        [[nodiscard]] std::optional<bool> verify_ready_file()
        {
            std::error_code code {};
            const std::filesystem::path path { ready_path() };
            if (std::filesystem::is_regular_file(path, code) == false)
                return std::nullopt;
            status.phase = update_phase::verifying;
            status.received = 0;
            status.total = plan.size;
            emit();
            const std::uintmax_t size { std::filesystem::file_size(path, code) };
            bool same { false };
            if (code.value() == 0 && size == plan.size)
            {
                net::update_sha256 checker {};
                std::u8string error {};
                const feed_outcome outcome { checker.valid() ? feed_file(path, plan.size, checker, error) : feed_outcome::failed };
                if (outcome != feed_outcome::done)
                {
                    finish_feed(outcome, error.empty() ? checker.error() : std::move(error));
                    return false;
                }
                const std::optional<update_digest> digest { checker.finish() };
                same = digest.has_value() && *digest == plan.sha256;
            }
            if (same)
            {
                status.received = plan.size;
                finish(update_phase::ready, update_failure::none, {});
                return true;
            }
            std::filesystem::remove(path, code);
            return std::nullopt;
        }

        [[nodiscard]] update_state read_state() const
        {
            // 읽지 못하는 상태 파일은 없는 것으로 본다. 이 PC의 파일을 쓸 수 있는 사람은 어차피 폴더를 지울 수 있다 (결정 4의 대가).
            const std::optional<update_state> state { parse_update_state(read_small_file(state_path())) };
            return state.value_or(update_state {});
        }

        // --- 확인 ---

        void start_check()
        {
            status = update_status {};
            status.phase = update_phase::checking;
            emit();
            if (interrupted())
                return;
            net::http_request request {};
            request.url = config.manifest_url;
            request.parse.assume_kind = net::http_body_kind::bytes;
            request.max_body_bytes = manifest_body_limit;
            static_cast<void>(send(std::move(request), waiting::manifest));
        }

        // 아는 키의 서명 가운데 하나라도 맞으면 참이다. 아니면 `signature_invalid`로 닫는다.
        [[nodiscard]] bool signature_passes(const update_envelope& envelope)
        {
            const std::vector<update_signature_match> matches { match_update_signatures(envelope, config.keys) };
            if (matches.empty())
            {
                finish(update_phase::failed, update_failure::signature_invalid, u8"No signature in the envelope uses a key this app knows.");
                return false;
            }
            std::u8string system_error {};
            for (const update_signature_match& match : matches)
            {
                const update_signature_entry& entry { envelope.signatures[match.signature] };
                std::optional<update_raw_signature> raw {};
                if (entry.form == update_signature_form::raw)
                {
                    update_raw_signature value {};
                    std::copy(entry.bytes.begin(), entry.bytes.end(), value.begin());
                    raw = value;
                }
                else
                    // 깨진 DER은 "맞지 않는다"다. 두 플랫폼이 같은 바이트를 받도록 Android도 core가 다시 지은 DER을 받는다.
                    raw = update_signature_from_der(entry.bytes);
                if (raw.has_value() == false)
                    continue;
                const net::update_signature_result result { net::verify_update_signature(key_bytes[match.key], *raw, envelope.payload) };
                if (result.check == net::update_signature_check::valid)
                    return true;
                if (result.check == net::update_signature_check::failed && system_error.empty())
                    system_error = result.error;
            }
            std::u8string message { u8"No signature from a known key matches the payload." };
            if (system_error.empty() == false)
                message += u8" " + system_error;
            finish(update_phase::failed, update_failure::signature_invalid, std::move(message));
            return false;
        }

        void on_manifest(const net::http_response& response)
        {
            if (transport_failed(response, update_failure::manifest_invalid))
                return;
            if (net::http_status_is_success(response.status_code) == false)
            {
                status.http_status = response.status_code;
                std::u8string message { u8"The manifest request answered HTTP " };
                append_number(message, static_cast<std::uint64_t>(response.status_code < 0 ? 0 : response.status_code));
                message += u8".";
                finish(update_phase::failed, update_failure::http_status, std::move(message));
                return;
            }

            std::u8string error {};
            const std::optional<update_envelope> envelope { parse_update_envelope(response.body.data(), error) };
            if (envelope.has_value() == false)
            {
                finish(update_phase::failed, update_failure::manifest_invalid, std::move(error));
                return;
            }
            // **payload는 서명을 지난 뒤에만 JSON으로 읽는다.**
            if (signature_passes(*envelope) == false)
                return;
            const std::optional<update_manifest> manifest { parse_update_manifest(envelope->payload, error) };
            if (manifest.has_value() == false)
            {
                finish(update_phase::failed, update_failure::manifest_invalid, std::move(error));
                return;
            }

            const update_state state { read_state() };
            update_judgement judgement {};
            judgement.app = config.app_id;
            judgement.channel = config.channel;
            judgement.seen_sequence = state.sequence;
            judgement.current = current;
            judgement.platform = current_update_platform();
            judgement.max_asset_bytes = config.max_asset_bytes;
            const update_verdict verdict { judge_update_manifest(*manifest, judgement) };
            if (verdict == update_verdict::mismatch)
            {
                finish(update_phase::failed, update_failure::manifest_mismatch, u8"The manifest app or channel differs from this app.");
                return;
            }
            if (verdict == update_verdict::replayed)
            {
                finish(update_phase::failed, update_failure::replayed, u8"The manifest sequence is lower than one this computer has seen.");
                return;
            }
            // 지난 매니페스트의 `sequence`를 적는다. 판이 같거나 낮아도 적는다.
            if (manifest->sequence > state.sequence)
            {
                const std::vector<std::uint8_t> bytes { serialize_update_state(update_state { manifest->sequence }) };
                if (const platform_result written { replace_file_atomically(state_path().u8string(), bytes) }; written.ok() == false)
                {
                    finish(update_phase::failed, update_failure::disk, written.error);
                    return;
                }
            }
            if (verdict == update_verdict::up_to_date)
            {
                finish(update_phase::up_to_date, update_failure::none, {});
                return;
            }

            update_offer offer {};
            offer.version = manifest->version_text;
            offer.notes = manifest->notes;
            offer.notes_url = manifest->notes_url;
            const std::optional<update_asset>& asset { manifest->asset(judgement.platform) };
            if (asset.has_value())
            {
                offer.size = asset->size;
                offer.store_url = asset->store_url;
            }
            status.offer = offer;
            if (verdict == update_verdict::no_asset)
            {
                finish(update_phase::failed, update_failure::no_asset, u8"The manifest has no asset for this platform.");
                return;
            }
            if (verdict == update_verdict::too_large)
            {
                finish(update_phase::failed, update_failure::too_large, u8"The asset is larger than max_asset_bytes.");
                return;
            }

            if (downloads_supported() == false)
            {
                if (url_scheme_allowed(asset->store_url, config.store_schemes) == false)
                {
                    finish(update_phase::failed, update_failure::manifest_invalid, u8"The store address uses a scheme the app does not allow.");
                    return;
                }
                finish(update_phase::available, update_failure::none, {});
                return;
            }

            const std::optional<std::u8string> url { resolve_update_asset_url(asset->url, response.final_url.empty() ? config.manifest_url : response.final_url) };
            if (url.has_value() == false)
            {
                finish(update_phase::failed, update_failure::manifest_invalid, u8"The asset address is not https or a single file name.");
                return;
            }
            plan = download_plan { *url, asset->size, asset->sha256 };
            status.total = plan.size;
            {
                const std::lock_guard guard { mutex };
                has_offer = true;
            }
            if (verify_ready_file().has_value())
                return;
            finish(update_phase::available, update_failure::none, {});
        }

        // --- 받기 ---

        void start_download()
        {
            status.failure = update_failure::none;
            status.error.clear();
            status.http_status = 0;
            status.received = 0;
            status.total = plan.size;
            if (interrupted())
                return;
            lock.emplace(file_in(directory, lock_file_name).u8string());
            if (lock->locked() == false)
            {
                std::u8string message { u8"Another updater holds update.lock in the download folder." };
                if (lock->status().error.empty() == false)
                    message += u8" " + lock->status().error;
                finish(update_phase::failed, update_failure::busy, std::move(message));
                return;
            }
            if (verify_ready_file().has_value())
                return;

            hasher.emplace();
            if (hasher->valid() == false)
            {
                finish(update_phase::failed, update_failure::disk, hasher->error());
                return;
            }
            std::error_code code {};
            const std::filesystem::path path { part_path() };
            std::uint64_t existing { 0 };
            if (std::filesystem::is_regular_file(path, code))
            {
                existing = std::filesystem::file_size(path, code);
                // 매니페스트보다 긴 받던 파일은 이어 쓸 수 없다. 처음부터 받는다.
                if (code.value() != 0 || existing > plan.size)
                {
                    std::filesystem::remove(path, code);
                    existing = 0;
                }
            }
            received = existing;
            status.phase = update_phase::downloading;
            status.received = received;
            emit();
            if (existing > 0)
            {
                // 이어받기는 남은 바이트를 해시에 다시 먹인 뒤 그 길이부터 잇는다.
                std::u8string error {};
                if (const feed_outcome outcome { feed_file(path, existing, *hasher, error) }; outcome != feed_outcome::done)
                {
                    finish_feed(outcome, std::move(error));
                    return;
                }
            }
            part.open(path, std::ios::binary | std::ios::app);
            if (part.is_open() == false)
            {
                finish(update_phase::failed, update_failure::disk, u8"Failed to open the partial download for writing.");
                return;
            }
            if (received == plan.size)
            {
                complete();
                return;
            }
            request_next_chunk();
        }

        void request_next_chunk()
        {
            if (interrupted())
                return;
            requested_first = received;
            requested_last = std::min<std::uint64_t>(received + config.download_chunk_bytes, plan.size) - 1;
            net::http_request request {};
            request.url = plan.url;
            request.headers.push_back(net::http_header { u8"Range", make_update_range(requested_first, requested_last) });
            // 압축을 풀면 바이트 자리가 어긋난다.
            request.decompress = false;
            request.parse.assume_kind = net::http_body_kind::bytes;
            request.max_body_bytes = static_cast<std::size_t>(config.download_chunk_bytes);
            // 느린 선에서 4 MiB 한 덩이는 기본 60초를 넘을 수 있다. 멈춘 서버는 조각 사이 시한(`receive_timeout`)이 잡는다.
            request.total_timeout = chunk_total_timeout;
            static_cast<void>(send(std::move(request), waiting::chunk));
        }

        void on_chunk(const net::http_response& response)
        {
            // 덩이보다 큰 200은 몸 상한에 걸려 오고, 그것은 `Range`를 무시한 서버다.
            if (transport_failed(response, update_failure::range_unsupported))
                return;
            if (net::http_status_is_success(response.status_code) == false)
            {
                status.http_status = response.status_code;
                std::u8string message { u8"The asset request answered HTTP " };
                append_number(message, static_cast<std::uint64_t>(response.status_code < 0 ? 0 : response.status_code));
                message += u8".";
                finish(update_phase::failed, update_failure::http_status, std::move(message));
                return;
            }
            const std::span<const std::uint8_t> bytes { response.body.data() };
            const std::uint64_t expected { requested_last - requested_first + 1 };
            bool fits { false };
            if (response.status_code == 206)
            {
                const std::optional<update_content_range> range { parse_update_content_range(response.header(u8"Content-Range")) };
                fits = range.has_value() && range->first == requested_first && range->last == requested_last && range->total == plan.size && bytes.size() == expected;
            }
            else if (response.status_code == 200)
                // 200은 덩이보다 작은 파일 전체일 때만 받는다.
                fits = requested_first == 0 && plan.size <= config.download_chunk_bytes && bytes.size() == plan.size;
            if (fits == false)
            {
                finish(update_phase::failed, update_failure::range_unsupported, u8"The server did not answer the requested byte range.");
                return;
            }

            part.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            part.flush();
            if (part.good() == false)
            {
                finish(update_phase::failed, update_failure::disk, u8"Failed to write the partial download.");
                return;
            }
            if (hasher->update(bytes) == false)
            {
                finish(update_phase::failed, update_failure::disk, hasher->error());
                return;
            }
            received += bytes.size();
            status.received = received;
            emit();
            if (received == plan.size)
            {
                complete();
                return;
            }
            request_next_chunk();
        }

        void complete()
        {
            status.phase = update_phase::verifying;
            status.received = received;
            emit();
            part.close();
            const std::optional<update_digest> digest { hasher->finish() };
            if (digest.has_value() == false)
            {
                finish(update_phase::failed, update_failure::disk, hasher->error());
                return;
            }
            std::error_code code {};
            const std::filesystem::path path { part_path() };
            if (*digest != plan.sha256)
            {
                // 다른 바이트는 이어 쓸 수 없다. 지우고 처음부터다.
                std::filesystem::remove(path, code);
                finish(update_phase::failed, update_failure::hash_mismatch, u8"The downloaded bytes do not match the manifest sha256.");
                return;
            }
            std::u8string error {};
            if (net::flush_update_file(path, error) == false)
            {
                finish(update_phase::failed, update_failure::disk, std::move(error));
                return;
            }
            std::filesystem::rename(path, ready_path(), code);
            if (code.value() != 0)
            {
                finish(update_phase::failed, update_failure::disk, with_code(u8"Failed to rename the finished download", code));
                return;
            }
            finish(update_phase::ready, update_failure::none, {});
        }

        void on_response(const net::http_response& response)
        {
            {
                const std::lock_guard guard { mutex };
                if (response.ticket != in_flight)
                    return;
                in_flight = {};
            }
            const waiting what { waiting_for };
            waiting_for = waiting::none;
            if (what == waiting::manifest)
                on_manifest(response);
            else if (what == waiting::chunk)
                on_chunk(response);
        }
    };

    updater::updater(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {
    }

    updater::~updater()
    {
        stop();
    }

    std::unique_ptr<updater> updater::create(updater_config configuration, std::u8string& error)
    {
        auto owned { std::make_unique<engine>() };
        if (configuration.app_id.empty() || configuration.channel.empty())
        {
            error = u8"The updater needs an app id and a channel.";
            return nullptr;
        }
        const std::optional<update_version> current { parse_update_version(configuration.current_version) };
        if (current.has_value() == false)
        {
            error = u8"The current version is not one to four dotted numbers.";
            return nullptr;
        }
        if (update_url_allowed(configuration.manifest_url) == false)
        {
            error = u8"The manifest address must be https, or http on a loopback host.";
            return nullptr;
        }
        if (configuration.keys.empty())
        {
            error = u8"The updater needs at least one signing key.";
            return nullptr;
        }
        for (std::size_t index = 0; index < configuration.keys.size(); ++index)
        {
            const update_key& key { configuration.keys[index] };
            const bool repeated {
                std::any_of(configuration.keys.begin(), configuration.keys.begin() + static_cast<std::ptrdiff_t>(index), [&key](const update_key& earlier) { return earlier.id == key.id; }),
            };
            std::optional<std::vector<std::uint8_t>> spki { decode_update_base64(key.spki_base64) };
            if (key.id.empty() || repeated || spki.has_value() == false || update_spki_point(*spki).has_value() == false)
            {
                error = u8"Every signing key needs a unique id and a base64 P-256 SubjectPublicKeyInfo.";
                return nullptr;
            }
            owned->key_bytes.push_back(std::move(*spki));
        }
        if (configuration.max_asset_bytes == 0 || configuration.download_chunk_bytes < update_minimum_chunk_bytes
            || configuration.download_chunk_bytes > update_maximum_chunk_bytes)
        {
            error = u8"max_asset_bytes must be positive and download_chunk_bytes must be between 64 KiB and 16 MiB.";
            return nullptr;
        }
        if (configuration.download_directory.empty())
        {
            error = u8"The updater needs a download directory.";
            return nullptr;
        }
        owned->directory = std::filesystem::path { configuration.download_directory };
        std::error_code code {};
        std::filesystem::create_directories(owned->directory, code);
        if (code.value() != 0 || std::filesystem::is_directory(owned->directory, code) == false)
        {
            error = with_code(u8"Failed to create the download directory", code);
            return nullptr;
        }
        owned->current = *current;

        engine* const raw { owned.get() };
        net::http_client_config client_config {};
        client_config.user_agent = configuration.user_agent;
        client_config.use_system_proxy = configuration.use_system_proxy;
        client_config.max_in_flight = 4;
        // 답은 updater의 일감 줄에 넣기만 한다. 푸는 일은 updater의 thread가 한다.
        client_config.deliver = [raw](net::http_response response) { raw->post([raw, response = std::move(response)] { raw->on_response(response); }); };
        owned->config = std::move(configuration);
        owned->client = net::http_client::create(std::move(client_config), error);
        if (owned->client == nullptr)
            return nullptr;
        try
        {
            owned->worker = std::thread { [raw] { raw->run(); } };
        }
        catch (const std::exception&)
        {
            owned->client->stop();
            error = u8"Failed to start the updater thread.";
            return nullptr;
        }
        return std::unique_ptr<updater> { new updater { std::move(owned) } };
    }

    bool updater::check(std::u8string& error)
    {
        engine& state { *engine_ };
        {
            const std::lock_guard guard { state.mutex };
            if (state.quitting || state.stopping.load())
            {
                error = u8"The updater is stopped.";
                return false;
            }
            if (state.busy)
            {
                error = u8"The updater is already working.";
                return false;
            }
            state.busy = true;
            state.has_offer = false;
            state.cancel_requested.store(false);
            state.tasks.push_back([&state] { state.start_check(); });
        }
        state.wake.notify_one();
        return true;
    }

    bool updater::download(std::u8string& error)
    {
        if (downloads_supported() == false)
        {
            error = u8"Downloading is unsupported on this platform; open the store address instead.";
            return false;
        }
        engine& state { *engine_ };
        {
            const std::lock_guard guard { state.mutex };
            if (state.quitting || state.stopping.load())
            {
                error = u8"The updater is stopped.";
                return false;
            }
            if (state.busy)
            {
                error = u8"The updater is already working.";
                return false;
            }
            if (state.has_offer == false)
            {
                error = u8"There is no offered version to download; check first.";
                return false;
            }
            state.busy = true;
            state.cancel_requested.store(false);
            state.tasks.push_back([&state] { state.start_download(); });
        }
        state.wake.notify_one();
        return true;
    }

    void updater::cancel() noexcept
    {
        engine& state { *engine_ };
        net::http_ticket ticket {};
        {
            const std::lock_guard guard { state.mutex };
            if (state.busy == false)
                return;
            state.cancel_requested.store(true);
            ticket = state.in_flight;
        }
        if (ticket)
            state.client->cancel(ticket);
    }

    void updater::stop() noexcept
    {
        if (engine_ == nullptr)
            return;
        engine& state { *engine_ };
        {
            const std::lock_guard guard { state.mutex };
            // `report` 안에서 부르면 자기 thread를 기다리게 된다.
            assert(state.worker_id != std::this_thread::get_id());
        }
        const std::lock_guard stop_guard { state.stop_mutex };
        state.stopping.store(true);
        // client가 멈춘 뒤로는 답이 일감 줄에 들어오지 않는다.
        if (state.client != nullptr)
        {
            state.client->cancel_all();
            state.client->stop();
        }
        {
            const std::lock_guard guard { state.mutex };
            state.quitting = true;
            state.tasks.clear();
        }
        state.wake.notify_all();
        if (state.worker.joinable())
            state.worker.join();
        state.close_download();
    }
}

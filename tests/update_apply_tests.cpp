#include "host/update_manifest.h"
#include "loopback_http_server.h"
#include "luil/app/platform_services.h"
#include "luil/app/updater.h"
#include "net/update_crypto.h"
#include "update_fixtures.h"
#include "update_test_signing.h"
#include "win32/command_line.h"
#include "win32/executable_path.h"
#include "win32/owned_handle.h"
#include "win32/update_apply_win32.h"
#include "win32/utf8.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// 자동 업데이트 적용을 시험 앱(luil_update_probe) 사본으로 끝까지 돈다 (docs/auto-update-apply-design.md의 확인 절).
//
// **이 test 프로세스는 자기 자신을 적용의 대상으로 삼지 않는다.** `updater::apply`의 옛 exe는 부른 프로세스의 실행 파일이라,
// 여기서 받은 probe를 적용하면 도우미가 luil_tests.exe를 바꾼다. 적용은 늘 임시 폴더의 probe 사본이 하고, 이 프로세스는 사본을
// 놓고 띄우고 결과를 되읽기만 한다.
//
// 이 PC 흔적: `%TEMP%\luil-update-apply-<pid>-<n>\`(사본, 받기 폴더, 결과 파일, 시작 기록)은 test가 끝에 지운다. 띄운 프로세스는
// 모두 Job 개체(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)에 들어 test가 Job을 닫으면 끝난다. 쓰기를 막은 폴더는 ACL을 되돌린 뒤 지운다.

namespace {
    using namespace std::chrono_literals;

    using luil::win32::owned_handle;

    [[nodiscard]] std::string ascii(const std::u8string_view text)
    {
        return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    [[nodiscard]] std::u8string from_ascii(const std::string_view text)
    {
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    // probe 사본은 수십 MiB라 한 번에 읽는다. debug 빌드의 바이트 반복자는 느리다.
    [[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path)
    {
        std::ifstream file { path, std::ios::binary | std::ios::ate };
        if (file.is_open() == false)
            return {};
        const std::streamoff size { file.tellg() };
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<std::streamoff>(size, 0)));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (file.gcount() != static_cast<std::streamsize>(bytes.size()))
            return {};
        return bytes;
    }

    void write_bytes(const std::filesystem::path& path, const std::span<const std::uint8_t> bytes)
    {
        std::ofstream file { path, std::ios::binary | std::ios::trunc };
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    [[nodiscard]] luil::update_digest sha256_of(const std::span<const std::uint8_t> bytes)
    {
        luil::net::update_sha256 hasher {};
        REQUIRE(hasher.update(bytes));
        const std::optional<luil::update_digest> digest { hasher.finish() };
        REQUIRE(digest.has_value());
        return *digest;
    }

    // 쓰기를 막은 폴더를 지우기 전에 되돌린다. 상속하는 기본 ACL로 돌아간다.
    void reset_folder_acl(const std::filesystem::path& folder)
    {
        PSECURITY_DESCRIPTOR descriptor { nullptr };
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:", SDDL_REVISION_1, &descriptor, nullptr) == FALSE)
            return;
        BOOL present { FALSE };
        BOOL defaulted { FALSE };
        PACL acl { nullptr };
        if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) != FALSE)
        {
            std::wstring name { folder.wstring() };
            static_cast<void>(SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr));
        }
        LocalFree(descriptor);
    }

    // `%TEMP%\luil-update-apply-<pid>-<n>\`이다. 앱 폴더는 빈칸과 한글이 든 이름이라 명령 줄 따옴표를 함께 잰다.
    class apply_scratch
    {
    public:
        apply_scratch()
            : root_ { std::filesystem::temp_directory_path() / ("luil-update-apply-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++counter_)) }
        {
            std::error_code ignored {};
            std::filesystem::remove_all(root_, ignored);
            std::filesystem::create_directories(app());
            std::filesystem::create_directories(download());
        }
        apply_scratch(const apply_scratch&) = delete;
        apply_scratch& operator=(const apply_scratch&) = delete;
        ~apply_scratch()
        {
            for (const std::filesystem::path& folder : locked_)
                reset_folder_acl(folder);
            std::error_code ignored {};
            std::filesystem::remove_all(root_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& root() const noexcept
        {
            return root_;
        }

        [[nodiscard]] std::filesystem::path app() const
        {
            return root_ / std::filesystem::path { u8"앱 폴더" };
        }

        [[nodiscard]] std::filesystem::path download() const
        {
            return root_ / L"download";
        }

        [[nodiscard]] std::filesystem::path old_executable() const
        {
            return app() / L"probe app.exe";
        }

        [[nodiscard]] std::filesystem::path log() const
        {
            return root_ / L"starts.log";
        }

        // 지울 때 ACL을 되돌릴 폴더로 적어 둔다.
        void remember_locked(std::filesystem::path folder)
        {
            locked_.push_back(std::move(folder));
        }

    private:
        static inline std::atomic<unsigned> counter_ { 0 };
        std::filesystem::path root_ {};
        std::vector<std::filesystem::path> locked_ {};
    };

    [[nodiscard]] std::filesystem::path probe_path()
    {
        return std::filesystem::path { luil::win32::current_executable_path() }.parent_path() / L"luil_update_probe.exe";
    }

    // probe를 복사하고 끝에 판 표지를 붙인다. 붙인 바이트를 돌려준다.
    [[nodiscard]] std::vector<std::uint8_t> place_probe(const std::filesystem::path& target, const int version)
    {
        std::vector<std::uint8_t> bytes { read_bytes(probe_path()) };
        REQUIRE(bytes.size() > 4096);
        const std::string marker { "luil-probe-version:" + std::to_string(version) };
        bytes.insert(bytes.end(), marker.begin(), marker.end());
        write_bytes(target, bytes);
        return bytes;
    }

    // 띄운 probe와 그 자식(도우미, 다시 띄운 앱)을 한 Job에 담는다. 닫으면 남은 것이 모두 끝난다.
    class probe_job
    {
    public:
        probe_job()
            : job_ { CreateJobObjectW(nullptr, nullptr) }
        {
            REQUIRE(job_.valid());
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            REQUIRE(SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE);
        }

        // 멈춘 채 띄워 Job에 넣은 뒤 풀어 준다. 그래야 자식이 Job 밖에서 서는 틈이 없다.
        void start(const std::filesystem::path& program, const std::span<const std::u8string> arguments)
        {
            std::wstring line { L"\"" + program.wstring() + L"\"" };
            for (const std::u8string& argument : arguments)
            {
                const std::optional<std::wstring> wide { luil::win32::utf8_to_utf16(argument).value };
                REQUIRE(wide.has_value());
                line += L" " + luil::win32::quote_argument(*wide);
            }
            STARTUPINFOW startup {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION information {};
            REQUIRE(CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &information) != FALSE);
            const owned_handle process { information.hProcess };
            const owned_handle thread { information.hThread };
            REQUIRE(AssignProcessToJobObject(job_.get(), process.get()) != FALSE);
            REQUIRE(ResumeThread(thread.get()) != static_cast<DWORD>(-1));
        }

        // Job 안의 프로세스가 모두 끝나기를 기다린다.
        [[nodiscard]] bool wait_empty(const std::chrono::milliseconds limit) const
        {
            const auto deadline { std::chrono::steady_clock::now() + limit };
            while (std::chrono::steady_clock::now() < deadline)
            {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting {};
                if (QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) == FALSE)
                    return false;
                if (accounting.ActiveProcesses == 0)
                    return true;
                std::this_thread::sleep_for(50ms);
            }
            return false;
        }

    private:
        owned_handle job_ {};
    };

    // probe 시작 하나의 기록이다 (update_probe_main.cpp의 기록 꼴).
    struct probe_start
    {
        std::string pid {};
        std::u8string version {};
        std::u8string primary {};
        std::u8string lock {};
        std::u8string last_apply {};
        std::u8string apply {};
        std::u8string check_after_handoff {};
        std::vector<std::u8string> updates {};
        std::vector<std::u8string> arguments {};
    };

    [[nodiscard]] std::vector<probe_start> read_starts(const std::filesystem::path& log)
    {
        std::vector<probe_start> starts {};
        const std::vector<std::uint8_t> bytes { read_bytes(log) };
        const std::string text { bytes.begin(), bytes.end() };
        std::size_t at { 0 };
        while (at < text.size())
        {
            std::size_t end { text.find('\n', at) };
            if (end == std::string::npos)
                end = text.size();
            const std::string_view line { std::string_view { text }.substr(at, end - at) };
            at = end + 1;
            const std::size_t first { line.find('\t') };
            const std::size_t second { first == std::string_view::npos ? std::string_view::npos : line.find('\t', first + 1) };
            if (second == std::string_view::npos)
                continue;
            const std::string pid { line.substr(0, first) };
            const std::string_view key { line.substr(first + 1, second - first - 1) };
            const std::u8string value { from_ascii(line.substr(second + 1)) };
            auto found { std::find_if(starts.begin(), starts.end(), [&pid](const probe_start& start) { return start.pid == pid; }) };
            if (found == starts.end())
            {
                starts.push_back(probe_start { pid });
                found = starts.end() - 1;
            }
            if (key == "version")
                found->version = value;
            else if (key == "primary")
                found->primary = value;
            else if (key == "lock")
                found->lock = value;
            else if (key == "last_apply")
                found->last_apply = value;
            else if (key == "apply")
                found->apply = value;
            else if (key == "check_after_handoff")
                found->check_after_handoff = value;
            else if (key == "update")
                found->updates.push_back(value);
            else if (key == "arg")
                found->arguments.push_back(value);
        }
        return starts;
    }

    [[nodiscard]] std::u8string number_text(const unsigned long value)
    {
        return from_ascii(std::to_string(value));
    }

    // 모든 probe가 쓰는 앞 인자다. 다시 띄운 probe도 같은 기록·이름·받기 폴더를 쓴다.
    [[nodiscard]] std::vector<std::u8string> common_arguments(const apply_scratch& scratch)
    {
        return {
            u8"--log",
            scratch.log().u8string(),
            u8"--instance",
            u8"luil-update-probe-" + number_text(GetCurrentProcessId()),
            u8"--download",
            scratch.download().u8string(),
        };
    }

    // 다시 띄울 인자가 그대로 돌아오는지 보는 꼬리다. 빈칸, 빈 글, 따옴표, 끝 백슬래시, 한글, 표식과 같은 글이 든다.
    [[nodiscard]] std::vector<std::u8string> extra_arguments()
    {
        return { u8"--", u8"인자 하나", u8"", u8"say \"hi\"", u8"folder\\", u8"-luil-update-apply" };
    }

    [[nodiscard]] std::vector<std::u8string> joined(std::vector<std::u8string> first, const std::vector<std::u8string>& second)
    {
        first.insert(first.end(), second.begin(), second.end());
        return first;
    }

    [[nodiscard]] std::vector<std::u8string> apply_arguments(const luil::update_digest& digest, const std::chrono::milliseconds parent, const std::chrono::milliseconds confirm)
    {
        return {
            u8"--apply",
            luil::format_update_digest(digest),
            u8"1.0.0",
            u8"1.0.1",
            u8"--parent-timeout",
            number_text(static_cast<unsigned long>(parent.count())),
            u8"--confirm-timeout",
            number_text(static_cast<unsigned long>(confirm.count())),
        };
    }

    [[nodiscard]] std::optional<luil::update_apply_record> read_result(const apply_scratch& scratch)
    {
        return luil::parse_update_apply_record(read_bytes(scratch.download() / L"apply-result.json"));
    }

    // 옛 exe 옆에 `.tmp`가 남지 않았는가.
    [[nodiscard]] bool no_temporaries(const apply_scratch& scratch)
    {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator { scratch.app() })
            if (entry.path().extension() == L".tmp")
                return false;
        return true;
    }

    // 옛 사본(판 1)과 받아 둔 새 사본(판 2)을 놓는다.
    struct placed_copies
    {
        std::vector<std::uint8_t> old_bytes {};
        std::vector<std::uint8_t> new_bytes {};
        luil::update_digest digest {};
    };

    [[nodiscard]] placed_copies place_copies(const apply_scratch& scratch)
    {
        placed_copies copies {};
        copies.old_bytes = place_probe(scratch.old_executable(), 1);
        const std::filesystem::path staging { scratch.root() / L"staging.exe" };
        copies.new_bytes = place_probe(staging, 2);
        copies.digest = sha256_of(copies.new_bytes);
        std::filesystem::rename(staging, scratch.download() / std::filesystem::path { luil::format_update_digest(copies.digest) + u8".exe" });
        return copies;
    }

    constexpr std::chrono::milliseconds job_limit { 40000 };
}

TEST_CASE("a helper run puts the new executable at the old path and the restarted app is first", "[update][update_apply]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    const std::vector<std::u8string> restart { joined(common_arguments(scratch), extra_arguments()) };
    probe_job job {};
    job.start(scratch.old_executable(), joined(joined(common_arguments(scratch), apply_arguments(copies.digest, 20s, 20s)), extra_arguments()));
    REQUIRE(job.wait_empty(job_limit));

    CHECK(read_bytes(scratch.old_executable()) == copies.new_bytes);
    // 되돌리기에 쓸 `.old`와 도우미의 이미지인 받아 둔 exe는 다시 띄운 시작에 남는다.
    CHECK(read_bytes(scratch.app() / L"probe app.exe.old") == copies.old_bytes);
    const std::filesystem::path ready { scratch.download() / std::filesystem::path { luil::format_update_digest(copies.digest) + u8".exe" } };
    CHECK(std::filesystem::exists(ready));
    CHECK(no_temporaries(scratch));
    // 결과는 다시 띄운 앱의 `create`가 읽고 지웠고, 지켜보던 도우미에게 확인을 남겼다.
    CHECK(std::filesystem::exists(scratch.download() / L"apply-result.json") == false);
    CHECK(read_bytes(scratch.download() / L"apply-confirmed") == luil::testing::update_fixture_bytes(luil::format_update_digest(copies.digest)));

    const std::vector<probe_start> starts { read_starts(scratch.log()) };
    // 도우미는 평소 길을 타지 않아 기록이 둘뿐이다 (옛 판과 다시 띄운 새 판).
    REQUIRE(starts.size() == 2);
    CHECK(starts[0].version == u8"1");
    CHECK(starts[0].last_apply == u8"none");
    CHECK(starts[0].apply == u8"launched");
    CHECK(starts[1].version == u8"2");
    CHECK(starts[1].primary == u8"1");
    CHECK(starts[1].last_apply == u8"replaced");
    CHECK(starts[1].lock == u8"1");
    CHECK(starts[1].arguments == restart);

    // 그다음 시작이 남은 파일을 지운다.
    job.start(scratch.old_executable(), common_arguments(scratch));
    REQUIRE(job.wait_empty(job_limit));
    const std::vector<probe_start> later { read_starts(scratch.log()) };
    REQUIRE(later.size() == 3);
    CHECK(later[2].last_apply == u8"none");
    CHECK(std::filesystem::exists(scratch.app() / L"probe app.exe.old") == false);
    CHECK(std::filesystem::exists(ready) == false);
    CHECK(std::filesystem::exists(scratch.download() / L"apply-confirmed") == false);
}

TEST_CASE("a helper run without restart replaces and starts nothing", "[update][update_apply]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    probe_job job {};
    job.start(scratch.old_executable(), joined(joined(common_arguments(scratch), apply_arguments(copies.digest, 20s, 20s)), { u8"--no-restart" }));
    REQUIRE(job.wait_empty(job_limit));

    CHECK(read_bytes(scratch.old_executable()) == copies.new_bytes);
    CHECK(read_starts(scratch.log()).size() == 1);
    // 앱이 끝날 때 적용한 것이라 결과는 다음 시작이 읽는다.
    const std::optional<luil::update_apply_record> result { read_result(scratch) };
    REQUIRE(result.has_value());
    CHECK(result->result.outcome == luil::update_apply_outcome::replaced);
    CHECK(result->result.from_version == u8"1.0.0");
    CHECK(result->result.to_version == u8"1.0.1");
}

TEST_CASE("an app that outlives parent_timeout changes and starts nothing", "[update][update_apply]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    probe_job job {};
    job.start(scratch.old_executable(), joined(joined(common_arguments(scratch), apply_arguments(copies.digest, 500ms, 20s)), { u8"--linger", u8"2500" }));
    REQUIRE(job.wait_empty(job_limit));

    CHECK(read_bytes(scratch.old_executable()) == copies.old_bytes);
    CHECK(read_starts(scratch.log()).size() == 1);
    const std::optional<luil::update_apply_record> result { read_result(scratch) };
    REQUIRE(result.has_value());
    CHECK(result->result.outcome == luil::update_apply_outcome::parent_timeout);
    CHECK(std::filesystem::exists(scratch.app() / L"probe app.exe.old") == false);
    CHECK(no_temporaries(scratch));
}

TEST_CASE("an old executable held open is replace_failed and the old version starts again", "[update][update_apply]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    // 읽기 공유만 둔다. 실행과 `.old` 복사는 되고, 바꾸기(DELETE 접근)는 공유 위반으로 막힌다.
    owned_handle held { CreateFileW(scratch.old_executable().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
    REQUIRE(held.valid());
    probe_job job {};
    job.start(scratch.old_executable(), joined(common_arguments(scratch), apply_arguments(copies.digest, 20s, 20s)));
    // 도우미는 10초 동안 다시 해 본 뒤 그만둔다.
    REQUIRE(job.wait_empty(job_limit));
    held = owned_handle {};

    CHECK(read_bytes(scratch.old_executable()) == copies.old_bytes);
    CHECK(std::filesystem::exists(scratch.app() / L"probe app.exe.old") == false);
    CHECK(no_temporaries(scratch));
    const std::vector<probe_start> starts { read_starts(scratch.log()) };
    REQUIRE(starts.size() == 2);
    CHECK(starts[1].version == u8"1");
    CHECK(starts[1].primary == u8"1");
    CHECK(starts[1].last_apply == u8"replace_failed");
    // 받아 둔 exe는 남아 다음 확인이 그대로 `ready`로 본다.
    CHECK(std::filesystem::exists(scratch.download() / std::filesystem::path { luil::format_update_digest(copies.digest) + u8".exe" }));
}

TEST_CASE("a new version that exits with 3 before confirming is rolled back and skipped", "[update][update_apply]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    // 되돌림이 건너뛸 판을 더하면서 기억한 `sequence`를 지우지 않는지 본다.
    write_bytes(scratch.download() / L"update-state.json", luil::serialize_update_state(luil::update_state { 7, {} }));
    probe_job job {};
    job.start(scratch.old_executable(),
        joined(joined(common_arguments(scratch), apply_arguments(copies.digest, 20s, 20s)), { u8"--exit-if-version", u8"2", u8"3" })
    );
    REQUIRE(job.wait_empty(job_limit));

    CHECK(read_bytes(scratch.old_executable()) == copies.old_bytes);
    const std::vector<probe_start> starts { read_starts(scratch.log()) };
    REQUIRE(starts.size() == 3);
    CHECK(starts[1].version == u8"2");
    CHECK(starts[1].last_apply.empty());
    CHECK(starts[2].version == u8"1");
    CHECK(starts[2].last_apply == u8"rolled_back");
    const std::optional<luil::update_state> state { luil::parse_update_state(read_bytes(scratch.download() / L"update-state.json")) };
    REQUIRE(state.has_value());
    CHECK(state->sequence == 7);
    CHECK(state->skipped_version == u8"1.0.1");
}

TEST_CASE("a new version that exits with 0 before confirming stays replaced", "[update][update_apply]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    probe_job job {};
    job.start(scratch.old_executable(),
        joined(joined(common_arguments(scratch), apply_arguments(copies.digest, 20s, 20s)), { u8"--exit-if-version", u8"2", u8"0" })
    );
    REQUIRE(job.wait_empty(job_limit));

    CHECK(read_bytes(scratch.old_executable()) == copies.new_bytes);
    CHECK(read_starts(scratch.log()).size() == 2);
    // 새 판은 updater를 만들기 전에 끝나 결과가 읽히지 않고 남았다.
    const std::optional<luil::update_apply_record> result { read_result(scratch) };
    REQUIRE(result.has_value());
    CHECK(result->result.outcome == luil::update_apply_outcome::replaced);
}

TEST_CASE("a folder that denies creating files is not writable", "[update][update_apply]")
{
    apply_scratch scratch {};
    std::u8string error {};
    CHECK(luil::win32::folder_writable(scratch.app(), error));
    CHECK(std::filesystem::is_empty(scratch.app()));

    const std::filesystem::path locked { scratch.root() / L"locked" };
    std::filesystem::create_directories(locked);
    scratch.remember_locked(locked);
    // 모두에게 파일·폴더 만들기를 막고 나머지는 연다. 보호한 DACL이라 위에서 물려받지 않는다.
    PSECURITY_DESCRIPTOR descriptor { nullptr };
    REQUIRE(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x6;;;WD)(A;OICI;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr) != FALSE);
    BOOL present { FALSE };
    BOOL defaulted { FALSE };
    PACL acl { nullptr };
    REQUIRE(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) != FALSE);
    std::wstring name { locked.wstring() };
    const DWORD applied { SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) };
    LocalFree(descriptor);
    REQUIRE(applied == ERROR_SUCCESS);

    error.clear();
    CHECK(luil::win32::folder_writable(locked, error) == false);
    CHECK(error.find(u8"not writable") != std::u8string::npos);
}

TEST_CASE("wrong helper arguments return 2 and change nothing", "[update][update_apply]")
{
    apply_scratch scratch {};
    const std::vector<std::uint8_t> bytes { luil::testing::update_fixture_bytes(u8"untouched") };
    write_bytes(scratch.old_executable(), bytes);
    luil::update_helper_arguments arguments {};
    arguments.old_executable = scratch.old_executable().u8string();
    arguments.parent_handle = 4;
    arguments.download_directory = scratch.download().u8string();
    arguments.from_version = u8"1.0.0";
    arguments.to_version = u8"1.0.1";
    arguments.parent_timeout_ms = 1000;
    arguments.confirm_timeout_ms = 1000;
    std::vector<std::u8string> good { u8"probe.exe" };
    const std::vector<std::u8string> tail { luil::make_update_helper_arguments(arguments) };
    good.insert(good.end(), tail.begin(), tail.end());

    std::vector<std::vector<std::u8string>> wrong {};
    wrong.push_back(good);
    wrong.back()[2] = u8"2";
    wrong.push_back(good);
    wrong.back()[6] = u8"not-a-digest";
    wrong.push_back(good);
    wrong.back()[12] = u8"extra";
    wrong.push_back(good);
    wrong.back()[3].clear();
    for (const std::vector<std::u8string>& list : wrong)
    {
        // 꼴이 맞는 인자를 이 프로세스에서 돌리면 이 프로세스를 부모로 보고 기다린다. 틀린 꼴인지 먼저 잠근다.
        REQUIRE(luil::parse_update_helper_arguments(list).kind == luil::update_helper_parse::malformed);
        CHECK(luil::run_update_helper(list) == std::optional<int> { 2 });
    }
    CHECK(read_bytes(scratch.old_executable()) == bytes);
    CHECK(std::filesystem::exists(scratch.app() / L"probe app.exe.old") == false);
    CHECK(no_temporaries(scratch));
    CHECK(std::filesystem::is_empty(scratch.download()));
}

TEST_CASE("arguments without the helper marker are a normal start", "[update][update_apply]")
{
    const std::vector<std::u8string> none { u8"app.exe" };
    const std::vector<std::u8string> other { u8"app.exe", u8"--page", u8"network" };
    const std::vector<std::u8string> later { u8"app.exe", u8"--", u8"-luil-update-apply" };
    CHECK(luil::run_update_helper(none).has_value() == false);
    CHECK(luil::run_update_helper(other).has_value() == false);
    CHECK(luil::run_update_helper(later).has_value() == false);
    CHECK(luil::platform_update_apply_mode() == luil::update_apply_mode::replace_executable);
}

namespace {
    // 확인·받기·적용을 끝까지 도는 서버다. 그 자리에서 서명한 매니페스트가 probe의 새 사본을 asset으로 가리킨다.
    class probe_update_server
    {
    public:
        explicit probe_update_server(std::vector<std::uint8_t> asset)
            : state_ { std::make_shared<shared>() }
        {
            state_->asset = std::move(asset);
            server_.set_handler([state = state_](const luil::testing::loopback_request& request) { return state->answer(request); });
        }

        [[nodiscard]] bool listening() const noexcept
        {
            return server_.port() != 0;
        }

        [[nodiscard]] std::u8string url(const std::string_view path) const
        {
            return server_.url(path);
        }

        void set_envelope(const std::u8string_view envelope)
        {
            const std::lock_guard lock { state_->mutex };
            state_->envelope = ascii(envelope);
        }

    private:
        struct shared
        {
            std::mutex mutex {};
            std::string envelope {};
            std::vector<std::uint8_t> asset {};

            [[nodiscard]] luil::testing::loopback_response answer(const luil::testing::loopback_request& request)
            {
                const std::lock_guard lock { mutex };
                luil::testing::loopback_response response {};
                if (request.target == "/update/manifest.json")
                {
                    response.headers.push_back({ "Content-Type", "application/json" });
                    response.body = luil::testing::loopback_bytes(envelope);
                    return response;
                }
                if (request.target != "/update/probe.exe")
                {
                    response.status = 404;
                    return response;
                }
                const std::string range { request.header("range").value_or(std::string {}) };
                std::uint64_t first { 0 };
                std::uint64_t last { asset.size() - 1 };
                const std::size_t dash { range.find('-') };
                if (range.starts_with("bytes=") && dash != std::string::npos)
                {
                    static_cast<void>(std::from_chars(range.data() + 6, range.data() + dash, first));
                    static_cast<void>(std::from_chars(range.data() + dash + 1, range.data() + range.size(), last));
                    last = std::min<std::uint64_t>(last, asset.size() - 1);
                    response.status = 206;
                    response.headers.push_back({ "Content-Range", "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(asset.size()) });
                }
                response.headers.push_back({ "Content-Type", "application/octet-stream" });
                response.body.assign(asset.begin() + static_cast<std::ptrdiff_t>(first), asset.begin() + static_cast<std::ptrdiff_t>(last + 1));
                return response;
            }
        };

        std::shared_ptr<shared> state_ {};
        luil::testing::loopback_http_server server_ {};
    };

    // 고정 payload의 두 Windows asset을 probe 사본으로 바꿔 시험 키로 서명한 봉투다.
    [[nodiscard]] std::u8string probe_envelope(const probe_update_server& server, const std::vector<std::uint8_t>& asset)
    {
        std::u8string payload { luil::testing::update_fixture_payload };
        const std::u8string from {
            u8"{\"url\":\"" + std::u8string { luil::testing::update_fixture_asset_name } + u8"\",\"size\":3145728,\"sha256\":\""
            + std::u8string { luil::testing::update_fixture_asset_sha256 } + u8"\"}"
        };
        const std::u8string to {
            u8"{\"url\":\"" + server.url("/update/probe.exe") + u8"\",\"size\":" + number_text(static_cast<unsigned long>(asset.size())) + u8",\"sha256\":\""
            + luil::format_update_digest(sha256_of(asset)) + u8"\"}"
        };
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

TEST_CASE("an app checks, downloads and applies through its updater to the end", "[update][update_apply]")
{
    apply_scratch scratch {};
    const std::vector<std::uint8_t> old_bytes { place_probe(scratch.old_executable(), 1) };
    const std::filesystem::path staging { scratch.root() / L"staging.exe" };
    const std::vector<std::uint8_t> new_bytes { place_probe(staging, 2) };
    std::filesystem::remove(staging);
    probe_update_server server { new_bytes };
    REQUIRE(server.listening());
    server.set_envelope(probe_envelope(server, new_bytes));

    const std::vector<std::u8string> restart { joined(common_arguments(scratch), extra_arguments()) };
    std::vector<std::u8string> arguments { common_arguments(scratch) };
    arguments.insert(arguments.end(), { u8"--update", server.url("/update/manifest.json"), u8"--parent-timeout", u8"20000", u8"--confirm-timeout", u8"20000" });
    probe_job job {};
    job.start(scratch.old_executable(), joined(arguments, extra_arguments()));
    REQUIRE(job.wait_empty(job_limit));

    CHECK(read_bytes(scratch.old_executable()) == new_bytes);
    const std::vector<probe_start> starts { read_starts(scratch.log()) };
    REQUIRE(starts.size() == 2);
    CHECK(starts[0].updates == std::vector<std::u8string> { u8"check available", u8"download ready", u8"apply handed_off" });
    // 넘긴 뒤로는 일을 받지 않는다.
    CHECK(starts[0].check_after_handoff == u8"0");
    CHECK(starts[1].version == u8"2");
    CHECK(starts[1].primary == u8"1");
    CHECK(starts[1].last_apply == u8"replaced");
    CHECK(starts[1].lock == u8"1");
    CHECK(starts[1].arguments == restart);
    static_cast<void>(old_bytes);
}

// 사람이 보는 흐름이다. ctest는 돌리지 않는다 (숨은 태그). 옛 판의 창을 닫으면 새 판의 창이 앞에 서는지 본다.
TEST_CASE("a windowed probe hands over to the new version in front of a person", "[.update-live]")
{
    apply_scratch scratch {};
    const placed_copies copies { place_copies(scratch) };
    probe_job job {};
    job.start(scratch.old_executable(), joined(joined(common_arguments(scratch), apply_arguments(copies.digest, 300s, 30s)), { u8"--window" }));
    REQUIRE(job.wait_empty(10min));
    CHECK(read_bytes(scratch.old_executable()) == copies.new_bytes);
    const std::vector<probe_start> starts { read_starts(scratch.log()) };
    REQUIRE(starts.size() == 2);
    CHECK(starts[1].last_apply == u8"replaced");
}

namespace {
    // 이 test 실행 파일의 앞 64 KiB다. 같은 빌드의 실행 파일이라 머리 검사를 지나야 한다.
    [[nodiscard]] std::vector<std::byte> own_executable_header()
    {
        std::ifstream file { std::filesystem::path { luil::win32::current_executable_path() }, std::ios::binary };
        // 중괄호는 초기값 목록이 되어 크기를 줄 수 없다.
        std::vector<std::byte> bytes(64 * 1024);
        if (file.is_open() == false)
            return {};
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(file.gcount()));
        return bytes;
    }

    // `header` 사본의 `offset` 자리에 `value`를 쓴다.
    template <typename value_type>
    [[nodiscard]] std::vector<std::byte> with_value(const std::vector<std::byte>& header, const std::size_t offset, const value_type value)
    {
        std::vector<std::byte> copy { header };
        std::memcpy(copy.data() + offset, &value, sizeof(value));
        return copy;
    }
}

TEST_CASE("the helper launch refuses a file Windows cannot run before the OS sees it", "[update][update_apply]")
{
    // 실행 파일이 아닌 받은 파일은 OS에 넘기지 않는다. 넘기면 OS가 사용자 화면에 "지원되지 않는 16비트 응용 프로그램" 상자를 띄운다.
    const std::vector<std::byte> own { own_executable_header() };
    REQUIRE(own.size() >= sizeof(IMAGE_DOS_HEADER));
    std::u8string error {};
    CHECK(luil::win32::runnable_executable_header(own, error));
    CHECK(error.empty());

    IMAGE_DOS_HEADER dos {};
    std::memcpy(&dos, own.data(), sizeof(dos));
    REQUIRE(dos.e_lfanew > 0);
    const std::size_t nt { static_cast<std::size_t>(dos.e_lfanew) };
    const std::size_t machine { nt + sizeof(DWORD) + offsetof(IMAGE_FILE_HEADER, Machine) };
    const std::size_t characteristics { nt + sizeof(DWORD) + offsetof(IMAGE_FILE_HEADER, Characteristics) };
    REQUIRE(characteristics + sizeof(WORD) <= own.size());
    WORD own_machine { 0 };
    WORD own_characteristics { 0 };
    std::memcpy(&own_machine, own.data() + machine, sizeof(own_machine));
    std::memcpy(&own_characteristics, own.data() + characteristics, sizeof(own_characteristics));

    const auto refused {
        [](const std::span<const std::byte> header, const std::u8string_view reason) {
            std::u8string why {};
            const bool runnable { luil::win32::runnable_executable_header(header, why) };
            INFO(ascii(why));
            CHECK(runnable == false);
            CHECK(why.find(reason) != std::u8string::npos);
        },
    };

    refused({}, u8"shorter than an MZ header");
    const std::vector<std::uint8_t> asset { luil::testing::update_fixture_asset() };
    refused(std::as_bytes(std::span { asset }), u8"does not start with MZ");
    refused(with_value(own, offsetof(IMAGE_DOS_HEADER, e_lfanew), static_cast<LONG>(own.size())), u8"PE header is missing");
    refused(with_value(own, offsetof(IMAGE_DOS_HEADER, e_lfanew), static_cast<LONG>(-4)), u8"PE header is missing");
    refused(with_value(own, nt, static_cast<DWORD>(0)), u8"PE signature is missing");
    const WORD other_machine { static_cast<WORD>(own_machine == IMAGE_FILE_MACHINE_AMD64 ? IMAGE_FILE_MACHINE_ARM64 : IMAGE_FILE_MACHINE_AMD64) };
    refused(with_value(own, machine, other_machine), u8"another machine");
    refused(with_value(own, characteristics, static_cast<WORD>(own_characteristics | IMAGE_FILE_DLL)), u8"not an executable image");
    refused(with_value(own, characteristics, static_cast<WORD>(own_characteristics & ~IMAGE_FILE_EXECUTABLE_IMAGE)), u8"not an executable image");
}

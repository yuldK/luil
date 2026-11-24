#include "luil/app/platform_services.h"

#include "macos/instance_message.h"
#include "macos/single_instance_receiver.h"

#include <catch2/catch_test_macros.hpp>

#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <mach-o/dyld.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// 단일 인스턴스와 인자 넘기기의 macOS 쪽을 실제 잠금과 Unix 소켓으로 본다 (single-instance-args-design.md).
// Windows의 platform_services_tests.cpp가 숨은 창으로 보는 경우를 첫째 객체의 받는 자리로 본다.
//  - 한 이름의 잠금은 같은 프로세스의 두 객체도 서로 막아, 첫째와 둘째를 한 프로세스에 세울 수 있다.
//  - 받는 함수는 메인 queue에서 불린다. test(메인 thread)가 run loop를 돌리는 동안 둘째는 다른 thread에서 보낸다.
//  - 다른 프로세스에서 보내는 길은 이 실행 파일을 숨은 test 하나로 다시 띄워 본다.

namespace {
    using namespace std::chrono_literals;

    [[nodiscard]] std::u8string unique_instance_name(const std::string_view purpose)
    {
        static int counter { 0 };
        const std::string text { "luil-tests-send-" + std::string { purpose } + "-" + std::to_string(::getpid()) + "-" + std::to_string(++counter) };
        return std::u8string { text.begin(), text.end() };
    }

    [[nodiscard]] luil::instance_send_options send_options(const std::chrono::milliseconds timeout, const bool activate = false)
    {
        luil::instance_send_options options {};
        options.timeout = timeout;
        options.activate = activate;
        return options;
    }

    // 메인 run loop를 돌려 메인 queue의 건넴을 받는다. 조건이 서면 참이다.
    [[nodiscard]] bool pump_until(const std::function<bool()>& done, const std::chrono::milliseconds timeout = 5s)
    {
        const auto deadline { std::chrono::steady_clock::now() + timeout };
        while (done() == false)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true));
        }
        return true;
    }

    // 받는 함수가 받은 것을 적는다.
    struct received
    {
        luil::instance_arguments arguments {};
        bool activate { false };
        bool main_thread { false };
    };

    class receiver_log
    {
    public:
        [[nodiscard]] luil::macos::instance_receiver make(const bool answer)
        {
            return [this, answer](const luil::instance_arguments& arguments, const bool activate) {
                const std::lock_guard lock { mutex_ };
                entries_.push_back(received { arguments, activate, ::pthread_main_np() != 0 });
                return answer;
            };
        }

        [[nodiscard]] std::vector<received> entries() const
        {
            const std::lock_guard lock { mutex_ };
            return entries_;
        }

    private:
        mutable std::mutex mutex_ {};
        std::vector<received> entries_ {};
    };

    // 다른 thread에서 보내고, 그동안 메인 thread는 run loop를 돌린다.
    [[nodiscard]] luil::platform_result send_while_pumping(const luil::single_instance& second, std::vector<std::u8string> arguments, const luil::instance_send_options& options)
    {
        std::atomic<bool> done { false };
        luil::platform_result result {};
        const auto send = [&] {
            result = second.send_to_primary(arguments, options);
            done.store(true);
        };
        std::thread sender { send };
        const bool finished { pump_until([&done] { return done.load(); }, options.timeout + 5s) };
        sender.join();
        REQUIRE(finished);
        return result;
    }

    [[nodiscard]] std::string executable_path()
    {
        std::uint32_t size { 0 };
        static_cast<void>(_NSGetExecutablePath(nullptr, &size));
        std::string path(size, '\0');
        REQUIRE(_NSGetExecutablePath(path.data(), &size) == 0);
        path.resize(std::char_traits<char>::length(path.c_str()));
        return path;
    }

    [[nodiscard]] std::string text_of(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    constexpr const char* child_name_variable { "LUIL_TEST_INSTANCE_NAME" };
}

TEST_CASE("A macOS single instance remembers the name it was given", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("name") };
    luil::single_instance first { name };
    REQUIRE(first.name() == name);
    REQUIRE(first.status().ok());

    const luil::single_instance moved { std::move(first) };
    REQUIRE(moved.name() == name);
    REQUIRE(moved.primary());
    REQUIRE(first.name().empty());
    REQUIRE(first.primary() == false);
    REQUIRE(first.send_to_primary({}, send_options(50ms)).status == luil::platform_status::failed);
    REQUIRE(luil::macos::set_instance_receiver(first, {}).status == luil::platform_status::not_found);
}

TEST_CASE("Only the first macOS instance of a name is primary until it lets go", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("primary") };
    std::optional<luil::single_instance> first { std::in_place, name };
    REQUIRE(first->status().ok());
    REQUIRE(first->primary());
    {
        const luil::single_instance second { name };
        REQUIRE(second.status().ok());
        REQUIRE(second.primary() == false);
        // 둘째에는 받는 자리가 없다.
        REQUIRE(luil::macos::set_instance_receiver(second, {}).status == luil::platform_status::not_found);
    }
    REQUIRE(luil::single_instance { name }.primary() == false);
    first.reset();
    REQUIRE(luil::single_instance { name }.primary());
    // 이름은 바이트로 가린다. 대소문자만 다른 이름은 다른 인스턴스다.
    const luil::single_instance upper { name + u8"A" };
    const luil::single_instance lower { name + u8"a" };
    REQUIRE(upper.primary());
    REQUIRE(lower.primary());
}

TEST_CASE("The first macOS instance has no one to send to", "[platform][instance]")
{
    const luil::single_instance first { unique_instance_name("first") };
    REQUIRE(first.primary());
    REQUIRE(first.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(50ms)).status == luil::platform_status::not_found);
}

TEST_CASE("A second macOS instance gives up after the time limit when the first has not opened its receiver", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("none") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    REQUIRE(second.primary() == false);

    const auto before { std::chrono::steady_clock::now() };
    const luil::platform_result result { second.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(50ms)) };
    const auto elapsed { std::chrono::steady_clock::now() - before };
    REQUIRE(result.status == luil::platform_status::not_found);
    REQUIRE(elapsed < 2s);
}

TEST_CASE("Arguments, the working directory and the flag reach the macOS receiver on the main thread", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("deliver") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());

    const std::vector<std::u8string> arguments { u8"a.txt", u8"b c", u8"\"q\" \\", u8"한글 \U0001F600", u8"" };
    const luil::platform_result result { send_while_pumping(second, arguments, send_options(2000ms)) };
    INFO(text_of(result.error));
    REQUIRE(result.ok());
    REQUIRE(result.error.empty());
    REQUIRE(send_while_pumping(second, {}, send_options(2000ms, true)).ok());

    const std::vector<received> entries { log.entries() };
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].arguments.arguments == arguments);
    REQUIRE(entries[0].arguments.working_directory == std::filesystem::current_path().u8string());
    REQUIRE(entries[0].activate == false);
    REQUIRE(entries[0].main_thread);
    REQUIRE(entries[1].arguments.arguments.empty());
    REQUIRE(entries[1].activate);
    REQUIRE(entries[1].main_thread);
}

TEST_CASE("A macOS receiver that answers false, throws or is empty makes the send fail", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("reject") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(false)).ok());
    const luil::platform_result rejected { send_while_pumping(second, { u8"a.txt" }, send_options(2000ms)) };
    REQUIRE(rejected.status == luil::platform_status::failed);
    REQUIRE(rejected.error.empty() == false);
    REQUIRE(log.entries().size() == 1);

    // 다시 달면 함수만 바뀐다.
    REQUIRE(luil::macos::set_instance_receiver(first, [](const luil::instance_arguments&, bool) -> bool { throw 1; }).ok());
    REQUIRE(send_while_pumping(second, { u8"a.txt" }, send_options(2000ms)).status == luil::platform_status::failed);
    REQUIRE(luil::macos::set_instance_receiver(first, {}).ok());
    REQUIRE(send_while_pumping(second, { u8"a.txt" }, send_options(2000ms)).status == luil::platform_status::failed);
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());
    REQUIRE(send_while_pumping(second, { u8"b.txt" }, send_options(2000ms)).ok());
    REQUIRE(log.entries().size() == 2);
}

TEST_CASE("A macOS first instance whose main thread does not answer fails at the time limit", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("stall") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());

    // 메인 run loop를 돌리지 않는다. 건넴은 메인 queue에서 기다린다.
    const auto before { std::chrono::steady_clock::now() };
    const luil::platform_result result { second.send_to_primary(std::vector<std::u8string> { u8"late.txt" }, send_options(200ms)) };
    const auto elapsed { std::chrono::steady_clock::now() - before };
    REQUIRE(result.status == luil::platform_status::failed);
    REQUIRE(result.error.find(u8"later") != std::u8string::npos);
    REQUIRE(elapsed >= 150ms);
    REQUIRE(elapsed < 1400ms);
    // 시한이 지난 보내기도 첫째는 나중에 받는다.
    REQUIRE(pump_until([&log] { return log.entries().size() == 1; }));
    REQUIRE(log.entries().front().arguments.arguments == std::vector<std::u8string> { u8"late.txt" });
}

TEST_CASE("A closed macOS receiver is no longer found", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("closed") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());
    REQUIRE(send_while_pumping(second, { u8"one" }, send_options(2000ms)).ok());

    luil::macos::clear_instance_receiver(first);
    REQUIRE(send_while_pumping(second, { u8"two" }, send_options(100ms)).status == luil::platform_status::not_found);
    // 다시 열면 받는다. 닫기는 거듭해도 된다.
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());
    REQUIRE(send_while_pumping(second, { u8"three" }, send_options(2000ms)).ok());
    luil::macos::clear_instance_receiver(first);
    luil::macos::clear_instance_receiver(first);
    luil::macos::clear_instance_receiver(second);
    REQUIRE(log.entries().size() == 2);
}

TEST_CASE("A receiver of another name is not the first macOS instance of this one", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("mine") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    const luil::single_instance other { unique_instance_name("other") };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(other, log.make(true)).ok());

    REQUIRE(send_while_pumping(second, {}, send_options(100ms)).status == luil::platform_status::not_found);
    REQUIRE(log.entries().empty());
}

TEST_CASE("macOS input the format cannot carry fails before anything is sent", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("invalid") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());

    const std::u8string with_nul { u8'a', u8'\0', u8'b' };
    const std::u8string broken { static_cast<char8_t>(0xC3), u8'(' };
    const std::vector<std::u8string> too_many(luil::macos::instance_message_max_arguments + 1, u8"a");
    const std::vector<std::u8string> too_large { std::u8string(luil::macos::instance_message_max_bytes, u8'a') };
    for (const std::vector<std::u8string>& arguments : { std::vector<std::u8string> { with_nul }, std::vector<std::u8string> { broken }, too_many, too_large })
        REQUIRE(send_while_pumping(second, arguments, send_options(2000ms)).status == luil::platform_status::failed);
    REQUIRE(pump_until([] { return false; }, 100ms) == false);
    REQUIRE(log.entries().empty());
}

TEST_CASE("A macOS instance name longer than the Windows window mark allows fails even for the first instance", "[platform][instance]")
{
    constexpr std::size_t limit { luil::macos::instance_name_max_code_units };
    const std::u8string prefix { unique_instance_name("long") };
    REQUIRE(prefix.size() < limit);
    const std::u8string longest { prefix + std::u8string(limit - prefix.size(), u8'n') };
    const std::u8string too_long { longest + u8"n" };

    // 잠금 파일 이름은 해시라 생성자는 그대로 선다. 보내기가 첫째 여부를 보기 전에 이름을 거른다.
    const luil::single_instance exact { longest };
    REQUIRE(exact.send_to_primary({}, send_options(50ms)).status == luil::platform_status::not_found);
    const luil::single_instance over { too_long };
    REQUIRE(over.status().ok());
    REQUIRE(over.primary());
    REQUIRE(over.send_to_primary({}, send_options(50ms)).status == luil::platform_status::failed);
}

TEST_CASE("A macOS instance that could not be created cannot send", "[platform][instance]")
{
    const luil::single_instance unnamed { u8"" };
    REQUIRE(unnamed.status().ok() == false);
    REQUIRE(unnamed.primary() == false);
    REQUIRE(unnamed.send_to_primary({}, send_options(50ms)).status == luil::platform_status::failed);
    const luil::single_instance broken { std::u8string { static_cast<char8_t>(0xC3), u8'(' } };
    REQUIRE(broken.status().ok() == false);
}

// 아래 test가 띄우는 둘째 프로세스의 몸이다. 이름 없이 돌면 아무것도 하지 않는다.
TEST_CASE("A second process sends its arguments to the first macOS instance", "[.][instance_child]")
{
    const char* const name { std::getenv(child_name_variable) };
    if (name == nullptr)
        return;
    const std::string text { name };
    const luil::single_instance second { std::u8string { text.begin(), text.end() } };
    REQUIRE(second.status().ok());
    REQUIRE(second.primary() == false);
    const luil::platform_result result { second.send_to_primary(std::vector<std::u8string> { u8"from child", u8"둘째 인자" }, send_options(5000ms)) };
    INFO(text_of(result.error));
    REQUIRE(result.ok());
}

TEST_CASE("A second process reaches the first macOS instance through its socket", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("process") };
    const luil::single_instance first { name };
    receiver_log log {};
    REQUIRE(luil::macos::set_instance_receiver(first, log.make(true)).ok());

    // 둘째는 다른 작업 폴더에서 뜬다. 그 폴더가 그대로 와야 한다.
    const std::filesystem::path folder { std::filesystem::temp_directory_path() / ("luil-instance-child-" + std::to_string(::getpid())) };
    std::filesystem::create_directories(folder);
    const std::string program { executable_path() };
    const std::string variable { std::string { child_name_variable } + "=" + std::string { name.begin(), name.end() } };
    std::vector<std::string> owned { program, "A second process sends its arguments to the first macOS instance" };
    std::vector<char*> argv {};
    for (std::string& argument : owned)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::vector<std::string> environment { variable };
    for (char** at { environ }; *at != nullptr; ++at)
        environment.emplace_back(*at);
    std::vector<char*> envp {};
    for (std::string& entry : environment)
        envp.push_back(entry.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions {};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, folder.c_str());
    pid_t child { 0 };
    const int spawned { ::posix_spawn(&child, program.c_str(), &actions, nullptr, argv.data(), envp.data()) };
    posix_spawn_file_actions_destroy(&actions);
    REQUIRE(spawned == 0);

    int status { 0 };
    bool exited { false };
    REQUIRE(pump_until([&] {
        exited = exited || ::waitpid(child, &status, WNOHANG) == child;
        return exited;
    }, 30s));
    std::error_code ignored {};
    const std::filesystem::path canonical { std::filesystem::canonical(folder, ignored) };
    std::filesystem::remove_all(folder, ignored);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    const std::vector<received> entries { log.entries() };
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().arguments.arguments == std::vector<std::u8string> { u8"from child", u8"둘째 인자" });
    REQUIRE((entries.front().arguments.working_directory == canonical.u8string() || entries.front().arguments.working_directory == folder.u8string()));
    REQUIRE(entries.front().main_thread);
}

#include "luil/app/taskbar.h"
#include "luil/win32/win32_window.h"
#include "win32/jump_list.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <propidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
    using luil::taskbar_jump_list;
    using luil::taskbar_jump_task;
    using luil::win32::jump_list_report;
    using luil::win32::jump_list_status;
    using luil::win32::jump_list_writer;
    using luil::win32::jump_task_link;
    using Microsoft::WRL::ComPtr;

    constexpr std::wstring_view test_executable { L"C:\\Program Files\\Luil Test\\app.exe" };

    // 링크를 되읽은 값이다. 셸 개체가 실제로 받은 것을 본다.
    struct read_link
    {
        std::wstring path {};
        std::wstring arguments {};
        std::wstring description {};
        std::wstring icon_path {};
        int icon_index { 0 };
        std::wstring title {};
    };

    [[nodiscard]] read_link read_shell_link(IUnknown& object)
    {
        ComPtr<IShellLinkW> link {};
        REQUIRE(SUCCEEDED(object.QueryInterface(IID_PPV_ARGS(&link))));
        read_link result {};
        std::wstring buffer(4096, L'\0');
        REQUIRE(SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr, SLGP_RAWPATH)));
        result.path = buffer.c_str();
        REQUIRE(SUCCEEDED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size()))));
        result.arguments = buffer.c_str();
        REQUIRE(SUCCEEDED(link->GetDescription(buffer.data(), static_cast<int>(buffer.size()))));
        result.description = buffer.c_str();
        REQUIRE(SUCCEEDED(link->GetIconLocation(buffer.data(), static_cast<int>(buffer.size()), &result.icon_index)));
        result.icon_path = buffer.c_str();

        ComPtr<IPropertyStore> store {};
        REQUIRE(SUCCEEDED(link.As(&store)));
        PROPVARIANT value {};
        REQUIRE(SUCCEEDED(store->GetValue(luil::win32::jump_task_title_key, &value)));
        if (value.vt == VT_LPWSTR && value.pwszVal != nullptr)
            result.title = value.pwszVal;
        PropVariantClear(&value);
        return result;
    }

    // 셸의 점프 목록 개체 대역이다. 받은 호출을 순서대로 적고, 시험이 정한 결과를 돌려준다.
    class fake_destination_list final : public ICustomDestinationList
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
        {
            if (id == __uuidof(IUnknown) || id == __uuidof(ICustomDestinationList))
            {
                *object = static_cast<ICustomDestinationList*>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++references_;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining { --references_ };
            if (remaining == 0)
                delete this;
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE SetAppID(LPCWSTR id) override
        {
            calls.push_back(L"SetAppID " + std::wstring { id });
            return set_app_id_result;
        }

        HRESULT STDMETHODCALLTYPE BeginList(UINT* minimum_slots, REFIID, void** removed) override
        {
            calls.push_back(L"BeginList");
            *minimum_slots = 10;
            *removed = nullptr;
            return begin_result;
        }

        HRESULT STDMETHODCALLTYPE AppendCategory(LPCWSTR, IObjectArray*) override
        {
            calls.push_back(L"AppendCategory");
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE AppendKnownCategory(KNOWNDESTCATEGORY) override
        {
            calls.push_back(L"AppendKnownCategory");
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE AddUserTasks(IObjectArray* tasks) override
        {
            calls.push_back(L"AddUserTasks");
            UINT count { 0 };
            REQUIRE(SUCCEEDED(tasks->GetCount(&count)));
            for (UINT index = 0; index < count; ++index)
            {
                ComPtr<IUnknown> object {};
                REQUIRE(SUCCEEDED(tasks->GetAt(index, IID_PPV_ARGS(&object))));
                links.push_back(read_shell_link(*object.Get()));
            }
            return add_tasks_result;
        }

        HRESULT STDMETHODCALLTYPE CommitList() override
        {
            calls.push_back(L"CommitList");
            return commit_result;
        }

        HRESULT STDMETHODCALLTYPE GetRemovedDestinations(REFIID, void** object) override
        {
            *object = nullptr;
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DeleteList(LPCWSTR id) override
        {
            calls.push_back(L"DeleteList " + std::wstring { id != nullptr ? id : L"(null)" });
            return delete_result;
        }

        HRESULT STDMETHODCALLTYPE AbortList() override
        {
            calls.push_back(L"AbortList");
            return S_OK;
        }

        std::vector<std::wstring> calls {};
        std::vector<read_link> links {};
        HRESULT set_app_id_result { S_OK };
        HRESULT begin_result { S_OK };
        HRESULT add_tasks_result { S_OK };
        HRESULT commit_result { S_OK };
        HRESULT delete_result { S_OK };

    private:
        ULONG references_ { 1 };
    };

    // 가짜 개체를 꽂은 쓰개다. 개체는 시험이 끝날 때까지 산다.
    struct fake_writer
    {
        explicit fake_writer(const std::u8string_view app_id)
        {
            // `new`가 준 첫 참조를 그대로 넘겨받는다.
            list.Attach(new fake_destination_list {});
            fake_destination_list* const shell { list.Get() };
            writer.configure(app_id, std::wstring { test_executable });
            writer.set_factory([shell](ComPtr<ICustomDestinationList>& result) {
                result = shell;
                return S_OK;
            });
        }

        ComPtr<fake_destination_list> list {};
        jump_list_writer writer {};
    };

    [[nodiscard]] taskbar_jump_list two_tasks()
    {
        taskbar_jump_list list {};
        list.tasks.push_back(taskbar_jump_task { .title = u8"새 메모", .arguments = { u8"--send-to-first", u8"jump:note" }, .description = u8"떠 있는 앱에 메모를 넘긴다" });
        list.tasks.push_back(taskbar_jump_task { .title = u8"그림", .arguments = { u8"a b.png" }, .icon_path = u8"C:\\icons\\image.ico", .icon_index = 2 });
        return list;
    }

    // `CommandLineToArgvW`가 인자 줄을 되읽은 목록이다. 앞에 프로그램 이름을 붙여 첫 인자 규칙을 피한다.
    [[nodiscard]] std::vector<std::wstring> split_arguments(const std::wstring& line)
    {
        const std::wstring command { L"app.exe " + line };
        int count { 0 };
        LPWSTR* const values { CommandLineToArgvW(command.c_str(), &count) };
        REQUIRE(values != nullptr);
        std::vector<std::wstring> result {};
        for (int index = 1; index < count; ++index)
            result.emplace_back(values[index]);
        LocalFree(values);
        return result;
    }
}

TEST_CASE("Jump tasks become links on this executable with quoted arguments", "[jump-list]")
{
    std::u8string error {};
    const auto links { luil::win32::make_jump_task_links(two_tasks(), test_executable, error) };
    REQUIRE(links.has_value());
    REQUIRE(error.empty());
    REQUIRE(links->size() == 2);
    REQUIRE((*links)[0].path == test_executable);
    REQUIRE((*links)[0].title == L"새 메모");
    REQUIRE((*links)[0].arguments == L"--send-to-first jump:note");
    REQUIRE((*links)[0].description == L"떠 있는 앱에 메모를 넘긴다");
    // 아이콘 경로가 비면 실행 파일의 아이콘이다.
    REQUIRE((*links)[0].icon_path == test_executable);
    REQUIRE((*links)[0].icon_index == 0);
    REQUIRE((*links)[1].arguments == L"\"a b.png\"");
    REQUIRE((*links)[1].icon_path == L"C:\\icons\\image.ico");
    REQUIRE((*links)[1].icon_index == 2);
}

TEST_CASE("Jump task arguments read back as the same argv list", "[jump-list]")
{
    const std::vector<std::u8string> arguments { u8"plain", u8"two words", u8"", u8"say \"hi\"", u8"C:\\ends with\\", u8"한글 경로" };
    taskbar_jump_list list {};
    list.tasks.push_back(taskbar_jump_task { .title = u8"인자", .arguments = arguments });
    std::u8string error {};
    const auto links { luil::win32::make_jump_task_links(list, test_executable, error) };
    REQUIRE(links.has_value());
    const std::vector<std::wstring> expected { L"plain", L"two words", L"", L"say \"hi\"", L"C:\\ends with\\", L"한글 경로" };
    REQUIRE(split_arguments((*links)[0].arguments) == expected);
}

TEST_CASE("Jump tasks outside the rules are refused before the shell", "[jump-list]")
{
    const auto refused = [](const taskbar_jump_task& task) {
        taskbar_jump_list list {};
        list.tasks.push_back(taskbar_jump_task { .title = u8"앞 작업" });
        list.tasks.push_back(task);
        std::u8string error {};
        const bool rejected { luil::win32::make_jump_task_links(list, test_executable, error).has_value() == false };
        // 몇 번째 작업인지 진단에 실린다.
        return rejected && error.find(u8"task 2") != std::u8string::npos;
    };
    REQUIRE(refused(taskbar_jump_task {}));
    REQUIRE(refused(taskbar_jump_task { .title = std::u8string(1, static_cast<char8_t>(0xff)) }));
    REQUIRE(refused(taskbar_jump_task { .title = std::u8string { u8"a\0b", 3 } }));
    REQUIRE(refused(taskbar_jump_task { .title = u8"인자", .arguments = { std::u8string { u8"a\0b", 3 } } }));
    REQUIRE(refused(taskbar_jump_task { .title = u8"설명", .description = std::u8string(1, static_cast<char8_t>(0xc0)) }));
    REQUIRE(refused(taskbar_jump_task { .title = u8"설명", .description = std::u8string(1024, u8'd') }));
    REQUIRE(refused(taskbar_jump_task { .title = u8"인자", .arguments = { std::u8string(1024, u8'a') } }));
    // 인자 두 개가 빈칸 하나로 이어져 1024가 된다.
    REQUIRE(refused(taskbar_jump_task { .title = u8"인자", .arguments = { std::u8string(500, u8'a'), std::u8string(523, u8'b') } }));
    REQUIRE(refused(taskbar_jump_task { .title = u8"아이콘", .icon_path = std::u8string(MAX_PATH, u8'i') }));

    taskbar_jump_list edge {};
    edge.tasks.push_back(taskbar_jump_task {
        .title = u8"경계"
        , .arguments = { std::u8string(500, u8'a'), std::u8string(522, u8'b') }
        , .description = std::u8string(1023, u8'd')
        , .icon_path = std::u8string(MAX_PATH - 1, u8'i')
    });
    std::u8string error {};
    REQUIRE(luil::win32::make_jump_task_links(edge, test_executable, error).has_value());

    REQUIRE(luil::win32::make_jump_task_links(two_tasks(), L"", error).has_value() == false);
}

TEST_CASE("Committing a jump list calls the shell in order under the app identity", "[jump-list]")
{
    const luil::win32::com_sta_scope com {};
    fake_writer fake { u8"luil.test.jump" };
    const std::optional<jump_list_report> report { fake.writer.apply(two_tasks()) };
    REQUIRE(report.has_value());
    REQUIRE(report->status == jump_list_status::applied);
    REQUIRE(report->tasks == 2);
    REQUIRE(report->error.empty());
    REQUIRE(fake.list->calls == std::vector<std::wstring> { L"SetAppID luil.test.jump", L"BeginList", L"AddUserTasks", L"CommitList" });

    // 셸 링크가 받은 값을 되읽는다.
    REQUIRE(fake.list->links.size() == 2);
    const read_link& note { fake.list->links[0] };
    REQUIRE(note.path == test_executable);
    REQUIRE(note.arguments == L"--send-to-first jump:note");
    REQUIRE(note.description == L"떠 있는 앱에 메모를 넘긴다");
    REQUIRE(note.icon_path == test_executable);
    REQUIRE(note.icon_index == 0);
    REQUIRE(note.title == L"새 메모");
    const read_link& image { fake.list->links[1] };
    REQUIRE(image.arguments == L"\"a b.png\"");
    REQUIRE(image.icon_path == L"C:\\icons\\image.ico");
    REQUIRE(image.icon_index == 2);
    REQUIRE(image.title == L"그림");

    // 같은 요청은 셸을 부르지 않고 알리지도 않는다.
    REQUIRE(fake.writer.apply(two_tasks()).has_value() == false);
    REQUIRE(fake.writer.apply(std::nullopt).has_value() == false);
    REQUIRE(fake.list->calls.size() == 4);
}

TEST_CASE("Without an app identity the jump list keeps the shell's default id", "[jump-list]")
{
    const luil::win32::com_sta_scope com {};
    fake_writer fake { u8"" };
    REQUIRE(fake.writer.apply(two_tasks())->status == jump_list_status::applied);
    REQUIRE(fake.list->calls == std::vector<std::wstring> { L"BeginList", L"AddUserTasks", L"CommitList" });

    // 넘길 id가 없어 지우기는 빈 목록을 짓는다.
    fake.list->calls.clear();
    const auto removed { fake.writer.apply(taskbar_jump_list {}) };
    REQUIRE(removed->status == jump_list_status::removed);
    REQUIRE(removed->tasks == 0);
    REQUIRE(fake.list->calls == std::vector<std::wstring> { L"BeginList", L"CommitList" });
}

TEST_CASE("Removing a jump list deletes the list of the app identity", "[jump-list]")
{
    const luil::win32::com_sta_scope com {};
    fake_writer fake { u8"luil.test.jump" };
    const auto removed { fake.writer.apply(taskbar_jump_list {}) };
    REQUIRE(removed->status == jump_list_status::removed);
    REQUIRE(fake.list->calls == std::vector<std::wstring> { L"DeleteList luil.test.jump" });

    fake.list->delete_result = E_ACCESSDENIED;
    REQUIRE(fake.writer.apply(two_tasks())->status == jump_list_status::applied);
    const auto failed { fake.writer.apply(taskbar_jump_list {}) };
    REQUIRE(failed->status == jump_list_status::failed);
    REQUIRE(failed->error.find(u8"DeleteList") != std::u8string::npos);
}

TEST_CASE("A failing jump list call is reported once and the started list is aborted", "[jump-list]")
{
    const luil::win32::com_sta_scope com {};
    {
        fake_writer fake { u8"luil.test.jump" };
        fake.list->begin_result = E_FAIL;
        const auto report { fake.writer.apply(two_tasks()) };
        REQUIRE(report->status == jump_list_status::failed);
        REQUIRE(report->tasks == 0);
        REQUIRE(report->error.find(u8"BeginList") != std::u8string::npos);
        REQUIRE(fake.list->calls == std::vector<std::wstring> { L"SetAppID luil.test.jump", L"BeginList" });
        // 같은 요청으로 다시 하지 않는다. 다음 변화에서 다시 한다.
        fake.list->begin_result = S_OK;
        REQUIRE(fake.writer.apply(two_tasks()).has_value() == false);
        taskbar_jump_list changed { two_tasks() };
        changed.tasks.pop_back();
        REQUIRE(fake.writer.apply(changed)->status == jump_list_status::applied);
    }
    {
        fake_writer fake { u8"luil.test.jump" };
        fake.list->add_tasks_result = E_FAIL;
        const auto report { fake.writer.apply(two_tasks()) };
        REQUIRE(report->status == jump_list_status::failed);
        REQUIRE(report->error.find(u8"AddUserTasks") != std::u8string::npos);
        REQUIRE(fake.list->calls == std::vector<std::wstring> { L"SetAppID luil.test.jump", L"BeginList", L"AddUserTasks", L"AbortList" });
    }
    {
        fake_writer fake { u8"luil.test.jump" };
        fake.list->commit_result = E_FAIL;
        const auto report { fake.writer.apply(two_tasks()) };
        REQUIRE(report->status == jump_list_status::failed);
        REQUIRE(report->error.find(u8"CommitList") != std::u8string::npos);
        REQUIRE(fake.list->calls.back() == L"AbortList");
    }
    {
        fake_writer fake { u8"luil.test.jump" };
        fake.list->set_app_id_result = E_INVALIDARG;
        const auto report { fake.writer.apply(two_tasks()) };
        REQUIRE(report->status == jump_list_status::failed);
        REQUIRE(fake.list->calls == std::vector<std::wstring> { L"SetAppID luil.test.jump" });
    }
}

TEST_CASE("A jump list request outside the rules never reaches the shell", "[jump-list]")
{
    const luil::win32::com_sta_scope com {};
    fake_writer fake { u8"luil.test.jump" };
    taskbar_jump_list list {};
    list.tasks.push_back(taskbar_jump_task {});
    const auto report { fake.writer.apply(list) };
    REQUIRE(report->status == jump_list_status::failed);
    REQUIRE(report->error.find(u8"empty title") != std::u8string::npos);
    REQUIRE(fake.list->calls.empty());
}

TEST_CASE("A jump list without the shell object is unavailable", "[jump-list]")
{
    jump_list_writer writer {};
    writer.configure(u8"luil.test.jump", std::wstring { test_executable });
    writer.set_factory([](ComPtr<ICustomDestinationList>&) { return REGDB_E_CLASSNOTREG; });
    const auto report { writer.apply(two_tasks()) };
    REQUIRE(report->status == jump_list_status::unavailable);
    REQUIRE(report->error.empty() == false);
}

namespace {
    // 셸이 점프 목록을 적는 폴더의 파일과 고친 시각이다.
    [[nodiscard]] std::map<std::wstring, std::filesystem::file_time_type> custom_destination_files()
    {
        PWSTR recent { nullptr };
        REQUIRE(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Recent, 0, nullptr, &recent)));
        const std::filesystem::path folder { std::filesystem::path { recent } / L"CustomDestinations" };
        CoTaskMemFree(recent);
        std::map<std::wstring, std::filesystem::file_time_type> files {};
        std::error_code error {};
        for (const auto& entry : std::filesystem::directory_iterator { folder, error })
            files.emplace(entry.path().filename().wstring(), entry.last_write_time());
        return files;
    }

    constexpr std::u8string_view live_app_id { u8"luil.test.jumplist" };

    // Catch 매크로의 출력에 싣는 바이트 글이다. 매크로 인자에 중괄호 초기화의 쉼표를 두지 않으려고 함수로 뺀다.
    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return std::string(text.begin(), text.end());
    }

    [[nodiscard]] std::string narrow(const std::wstring_view text)
    {
        std::string result {};
        for (const wchar_t character : text)
            result.push_back(static_cast<char>(character));
        return result;
    }
}

// 실제 셸에 목록을 짓고 같은 id로 지운다 (docs/jump-list-design.md의 실측). ctest는 돌리지 않는다.
//  - 사용자 프로필의 CustomDestinations 폴더에 파일이 하나 생기고, 지우면 그 파일이 사라지는지 본다.
TEST_CASE("A live jump list is committed and deleted under a test identity", "[.jump-list-live]")
{
    const luil::win32::com_sta_scope com {};
    const auto before { custom_destination_files() };

    jump_list_writer writer {};
    writer.configure(live_app_id, luil::win32::current_executable_path());
    taskbar_jump_list list {};
    list.tasks.push_back(taskbar_jump_task { .title = u8"luil 시험 작업", .arguments = { u8"--luil-jump-list-test" }, .description = u8"시험이 지우는 항목" });
    const jump_list_report committed { writer.run(luil::jump_list_call { .kind = luil::jump_list_call_kind::commit, .list = list }) };
    UNSCOPED_INFO("commit error: " << narrow(committed.error));
    const auto after_commit { custom_destination_files() };
    std::vector<std::wstring> written {};
    for (const auto& [name, time] : after_commit)
    {
        const auto found { before.find(name) };
        if (found == before.end() || found->second != time)
            written.push_back(name);
    }

    // 지우기는 결과와 상관없이 늘 한다. 실측이 남긴 목록이 사용자 프로필에 남지 않게 한다.
    const jump_list_report deleted { writer.run(luil::jump_list_call { .kind = luil::jump_list_call_kind::remove }) };
    UNSCOPED_INFO("delete error: " << narrow(deleted.error));
    const auto after_delete { custom_destination_files() };
    const jump_list_report deleted_again { writer.run(luil::jump_list_call { .kind = luil::jump_list_call_kind::remove }) };

    WARN("committed status " << static_cast<int>(committed.status) << ", written files " << written.size());
    for (const std::wstring& name : written)
        WARN("written " << narrow(name) << (after_delete.contains(name) ? " (still there after delete)" : " (gone after delete)"));
    WARN("delete status " << static_cast<int>(deleted.status) << ", second delete status " << static_cast<int>(deleted_again.status) << " " << narrow(deleted_again.error));

    REQUIRE(committed.status == jump_list_status::applied);
    REQUIRE(written.size() == 1);
    REQUIRE(deleted.status == jump_list_status::removed);
    REQUIRE(after_delete.contains(written.front()) == false);
}

// 실측이 중간에 멈췄을 때 남은 목록을 지운다. ctest는 돌리지 않는다.
TEST_CASE("The live jump list test identity is deleted", "[.jump-list-live-cleanup]")
{
    const luil::win32::com_sta_scope com {};
    jump_list_writer writer {};
    writer.configure(live_app_id, luil::win32::current_executable_path());
    const jump_list_report deleted { writer.run(luil::jump_list_call { .kind = luil::jump_list_call_kind::remove }) };
    WARN("delete status " << static_cast<int>(deleted.status) << " " << narrow(deleted.error));
    REQUIRE(deleted.status == jump_list_status::removed);
}

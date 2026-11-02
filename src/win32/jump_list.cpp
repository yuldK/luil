#include "win32/jump_list.h"

#include "win32/command_line.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <propidl.h>
#include <propsys.h>

#include <cwchar>
#include <utility>

namespace luil::win32 {
    namespace {
        using Microsoft::WRL::ComPtr;

        // 올바른 UTF-8이고 NUL이 없으면 UTF-16으로 옮긴다. NUL은 셸 링크의 글 칸을 거기서 끊는다.
        [[nodiscard]] std::optional<std::wstring> checked_text(const std::u8string_view text)
        {
            if (text.find(u8'\0') != std::u8string_view::npos)
                return std::nullopt;
            return utf8_to_utf16(text).value;
        }

        [[nodiscard]] std::u8string task_error(const std::size_t index, const std::u8string_view what)
        {
            const std::string number { std::to_string(index + 1) };
            return u8"Jump list task " + std::u8string { number.begin(), number.end() } + u8" " + std::u8string { what };
        }

        // 제목 같은 문자열 속성 값을 짓는다. `InitPropVariantFromString`은 shlwapi를 부르므로 손수 짓는다.
        [[nodiscard]] HRESULT set_string_property(IPropertyStore& store, const PROPERTYKEY& key, const std::wstring& text)
        {
            PROPVARIANT value {};
            const std::size_t bytes { (text.size() + 1) * sizeof(wchar_t) };
            auto* const buffer { static_cast<wchar_t*>(CoTaskMemAlloc(bytes)) };
            if (buffer == nullptr)
                return E_OUTOFMEMORY;
            std::wmemcpy(buffer, text.c_str(), text.size() + 1);
            value.vt = VT_LPWSTR;
            value.pwszVal = buffer;
            const HRESULT result { store.SetValue(key, value) };
            PropVariantClear(&value);
            return result;
        }

        [[nodiscard]] jump_list_report failed_report(std::u8string error)
        {
            return jump_list_report { .status = jump_list_status::failed, .error = std::move(error) };
        }
    }

    std::optional<std::vector<jump_task_link>> make_jump_task_links(const taskbar_jump_list& list, const std::wstring_view executable, std::u8string& error)
    {
        if (executable.empty())
        {
            error = u8"The executable path for the jump list could not be read.";
            return std::nullopt;
        }
        std::vector<jump_task_link> links {};
        links.reserve(list.tasks.size());
        for (std::size_t index = 0; index < list.tasks.size(); ++index)
        {
            const taskbar_jump_task& task { list.tasks[index] };
            if (task.title.empty())
            {
                error = task_error(index, u8"has an empty title.");
                return std::nullopt;
            }
            jump_task_link link { .path = std::wstring { executable }, .icon_index = task.icon_index };
            const std::optional<std::wstring> title { checked_text(task.title) };
            const std::optional<std::wstring> description { checked_text(task.description) };
            const std::optional<std::wstring> icon { checked_text(task.icon_path) };
            if (title.has_value() == false
                || description.has_value() == false
                || icon.has_value() == false)
            {
                error = task_error(index, u8"has text that is not valid UTF-8 or contains NUL.");
                return std::nullopt;
            }
            link.title = *title;
            link.description = *description;
            link.icon_path = icon->empty() ? std::wstring { executable } : *icon;

            for (const std::u8string& argument : task.arguments)
            {
                const std::optional<std::wstring> wide { checked_text(argument) };
                if (wide.has_value() == false)
                {
                    error = task_error(index, u8"has an argument that is not valid UTF-8 or contains NUL.");
                    return std::nullopt;
                }
                if (link.arguments.empty() == false)
                    link.arguments += L' ';
                link.arguments += quote_argument(*wide);
            }
            if (link.arguments.size() > jump_task_text_limit || link.description.size() > jump_task_text_limit)
            {
                error = task_error(index, u8"has arguments or a description longer than 1023 UTF-16 code units.");
                return std::nullopt;
            }
            if (link.icon_path.size() >= MAX_PATH)
            {
                error = task_error(index, u8"has an icon path of MAX_PATH or more UTF-16 code units.");
                return std::nullopt;
            }
            links.push_back(std::move(link));
        }
        return links;
    }

    std::wstring current_executable_path()
    {
        std::wstring executable(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD written { GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) };
            if (written == 0)
                return {};
            if (written < executable.size())
            {
                executable.resize(written);
                return executable;
            }
            executable.resize(executable.size() * 2);
        }
    }

    HRESULT make_shell_link(const jump_task_link& link, ComPtr<IShellLinkW>& result)
    {
        ComPtr<IShellLinkW> shell_link {};
        HRESULT code { CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell_link)) };
        if (SUCCEEDED(code))
            code = shell_link->SetPath(link.path.c_str());
        if (SUCCEEDED(code))
            code = shell_link->SetArguments(link.arguments.c_str());
        if (SUCCEEDED(code) && link.description.empty() == false)
            code = shell_link->SetDescription(link.description.c_str());
        if (SUCCEEDED(code))
            code = shell_link->SetIconLocation(link.icon_path.c_str(), link.icon_index);

        // 제목이 없는 셸 링크는 목록에 글이 서지 않는다.
        ComPtr<IPropertyStore> store {};
        if (SUCCEEDED(code))
            code = shell_link.As(&store);
        if (SUCCEEDED(code))
            code = set_string_property(*store.Get(), jump_task_title_key, link.title);
        if (SUCCEEDED(code))
            code = store->Commit();
        if (SUCCEEDED(code))
            result = std::move(shell_link);
        return code;
    }

    jump_list_writer::jump_list_writer()
        : factory_ { [](ComPtr<ICustomDestinationList>& list) { return CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list)); } }
    {
    }

    void jump_list_writer::configure(const std::u8string_view app_id, std::wstring executable)
    {
        // 식별 id는 `run_application_window`가 이미 검사했다. 옮기지 못하면 식별이 없는 것으로 본다.
        app_id_ = utf8_to_utf16(app_id).value.value_or(std::wstring {});
        executable_ = std::move(executable);
    }

    void jump_list_writer::set_factory(list_factory factory)
    {
        factory_ = std::move(factory);
    }

    std::optional<jump_list_report> jump_list_writer::apply(const std::optional<taskbar_jump_list>& wanted)
    {
        const std::optional<jump_list_call> call { sync_.update(wanted) };
        if (call.has_value() == false)
            return std::nullopt;
        return run(*call);
    }

    jump_list_report jump_list_writer::run(const jump_list_call& call)
    {
        // 규칙 밖의 요청은 셸을 부르기 전에 거른다. 앞서 지은 목록은 그대로 남는다.
        std::vector<jump_task_link> links {};
        if (call.kind == jump_list_call_kind::commit)
        {
            std::u8string error {};
            std::optional<std::vector<jump_task_link>> made { make_jump_task_links(call.list, executable_, error) };
            if (made.has_value() == false)
                return failed_report(std::move(error));
            links = std::move(*made);
        }

        ComPtr<ICustomDestinationList> list {};
        if (const HRESULT created { factory_ ? factory_(list) : E_NOINTERFACE }; FAILED(created) || list == nullptr)
        {
            return jump_list_report {
                .status = jump_list_status::unavailable
                , .error = make_hresult_error(u8"Creating the jump list object failed", FAILED(created) ? created : E_NOINTERFACE)
            };
        }

        // 식별이 있으면 지우기는 그 id의 목록을 지운다. 없으면 넘길 id가 없어 빈 목록을 짓는다.
        if (call.kind == jump_list_call_kind::remove && app_id_.empty() == false)
        {
            if (const HRESULT code { list->DeleteList(app_id_.c_str()) }; FAILED(code))
                return failed_report(make_hresult_error(u8"DeleteList failed", code));
            return jump_list_report { .status = jump_list_status::removed };
        }

        if (app_id_.empty() == false)
        {
            if (const HRESULT code { list->SetAppID(app_id_.c_str()) }; FAILED(code))
                return failed_report(make_hresult_error(u8"SetAppID failed", code));
        }
        UINT minimum_slots { 0 };
        ComPtr<IObjectArray> removed {};
        if (const HRESULT code { list->BeginList(&minimum_slots, IID_PPV_ARGS(&removed)) }; FAILED(code))
            return failed_report(make_hresult_error(u8"BeginList failed", code));

        // 시작한 목록은 실패하면 거둔다. 거두지 않으면 셸 개체가 짓는 중인 목록을 쥔다.
        const auto abort = [&list](std::u8string error) {
            static_cast<void>(list->AbortList());
            return failed_report(std::move(error));
        };
        if (links.empty() == false)
        {
            ComPtr<IObjectCollection> collection {};
            if (const HRESULT code { CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&collection)) }; FAILED(code))
                return abort(make_hresult_error(u8"Creating the jump list task collection failed", code));
            for (const jump_task_link& link : links)
            {
                ComPtr<IShellLinkW> shell_link {};
                if (const HRESULT code { make_shell_link(link, shell_link) }; FAILED(code))
                    return abort(make_hresult_error(u8"Creating a jump list task link failed", code));
                if (const HRESULT code { collection->AddObject(shell_link.Get()) }; FAILED(code))
                    return abort(make_hresult_error(u8"Adding a jump list task failed", code));
            }
            ComPtr<IObjectArray> tasks {};
            if (const HRESULT code { collection.As(&tasks) }; FAILED(code))
                return abort(make_hresult_error(u8"Reading the jump list task collection failed", code));
            if (const HRESULT code { list->AddUserTasks(tasks.Get()) }; FAILED(code))
                return abort(make_hresult_error(u8"AddUserTasks failed", code));
        }
        if (const HRESULT code { list->CommitList() }; FAILED(code))
            return abort(make_hresult_error(u8"CommitList failed", code));
        if (call.kind == jump_list_call_kind::remove)
            return jump_list_report { .status = jump_list_status::removed };
        return jump_list_report { .status = jump_list_status::applied, .tasks = links.size() };
    }
}

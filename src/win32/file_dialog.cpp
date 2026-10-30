#include "win32/file_dialog.h"

#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <string>
#include <utility>
#include <vector>

namespace luil::win32 {
    file_dialog_result run_file_dialog(const HWND owner, const file_dialog_request& request)
    {
        file_dialog_result result { request.id };
        const auto fail = [&result](const std::u8string_view text, const HRESULT code) {
            result.status = file_dialog_status::failed;
            result.error = make_hresult_error(text, code);
            result.paths.clear();
            return result;
        };
        if (owner == nullptr || IsWindow(owner) == FALSE)
            return fail(u8"The file dialog owner is unavailable", E_INVALIDARG);
        Microsoft::WRL::ComPtr<IFileDialog> dialog {};
        HRESULT code { CoCreateInstance(request.kind == file_dialog_kind::save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
            nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
        };
        if (FAILED(code))
            return fail(u8"Cannot create the file dialog", code);
        FILEOPENDIALOGOPTIONS options {};
        if (FAILED(code = dialog->GetOptions(&options)))
            return fail(u8"Cannot read file dialog options", code);
        options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
        if (request.kind == file_dialog_kind::open_multiple)
            options |= FOS_ALLOWMULTISELECT;
        if (request.kind == file_dialog_kind::folder)
            options |= FOS_PICKFOLDERS;
        if (request.kind == file_dialog_kind::save)
            options |= FOS_OVERWRITEPROMPT;
        else if (request.kind != file_dialog_kind::folder)
            options |= FOS_FILEMUSTEXIST;
        if (FAILED(code = dialog->SetOptions(options)))
            return fail(u8"Cannot set file dialog options", code);
        const auto title { utf8_to_utf16(request.title) };
        const auto name { utf8_to_utf16(request.default_name) };
        const auto initial { utf8_to_utf16(request.initial_folder) };
        if (title.value.has_value() == false || name.value.has_value() == false || initial.value.has_value() == false)
            return fail(u8"File dialog text must be valid UTF-8", E_INVALIDARG);
        if (title.value->empty() == false && FAILED(code = dialog->SetTitle(title.value->c_str())))
            return fail(u8"Cannot set file dialog title", code);
        if (name.value->empty() == false && FAILED(code = dialog->SetFileName(name.value->c_str())))
            return fail(u8"Cannot set file dialog name", code);
        if (initial.value->empty() == false)
        {
            // 처음 폴더는 힌트다. 지워졌거나 옮겨진 폴더 때문에 요청 전체를 실패시키지 않고 OS가 고른 폴더로 연다.
            Microsoft::WRL::ComPtr<IShellItem> folder {};
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.value->c_str(), nullptr, IID_PPV_ARGS(&folder))))
                static_cast<void>(dialog->SetFolder(folder.Get()));
        }
        std::vector<std::wstring> labels {};
        std::vector<std::wstring> patterns {};
        std::vector<COMDLG_FILTERSPEC> filters {};
        std::u8string default_extension {};
        if (request.kind != file_dialog_kind::folder)
        {
            for (const auto& filter : request.filters)
            {
                const bool first_filter { labels.empty() };
                const auto label { utf8_to_utf16(filter.label) };
                if (label.value.has_value() == false)
                    return fail(u8"Invalid file filter label", E_INVALIDARG);
                labels.push_back(*label.value);
                std::u8string pattern {};
                for (auto extension : filter.extensions)
                {
                    if (pattern.empty() == false)
                        pattern += u8";";
                    if (extension == u8"*")
                        pattern += u8"*.*";
                    else
                    {
                        if (extension.starts_with(u8"*."))
                            extension.erase(0, 2);
                        else if (extension.starts_with(u8"."))
                            extension.erase(0, 1);
                        pattern += u8"*." + extension;
                        if (first_filter && default_extension.empty() && extension.empty() == false && extension != u8"*")
                            default_extension = extension;
                    }
                }
                const auto converted { utf8_to_utf16(pattern.empty() ? u8"*.*" : pattern) };
                if (converted.value.has_value() == false)
                    return fail(u8"Invalid file filter extension", E_INVALIDARG);
                patterns.push_back(*converted.value);
            }
            for (std::size_t index { 0 }; index < labels.size(); ++index)
                filters.push_back({ labels[index].c_str(), patterns[index].c_str() });
            if (filters.empty() == false && FAILED(code = dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data())))
                return fail(u8"Cannot set file filters", code);
            // 저장은 확장자 없이 쓴 이름에 첫 필터의 확장자를 붙인다. 없으면 사용자가 쓴 이름 그대로 저장된다.
            if (request.kind == file_dialog_kind::save && default_extension.empty() == false)
            {
                const auto extension { utf8_to_utf16(default_extension) };
                if (extension.value.has_value() == false)
                    return fail(u8"Invalid file filter extension", E_INVALIDARG);
                if (FAILED(code = dialog->SetDefaultExtension(extension.value->c_str())))
                    return fail(u8"Cannot set the default extension", code);
            }
        }
        code = dialog->Show(owner);
        if (code == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return result;
        if (FAILED(code))
            return fail(u8"The file dialog failed", code);
        const auto append = [&result](IShellItem* item) -> HRESULT {
            PWSTR path { nullptr };
            const HRESULT read { item->GetDisplayName(SIGDN_FILESYSPATH, &path) };
            if (FAILED(read))
                return read;
            const auto converted { utf16_to_utf8(path) };
            CoTaskMemFree(path);
            if (converted.value.has_value() == false)
                return E_INVALIDARG;
            result.paths.push_back(*converted.value);
            return S_OK;
        };
        if (request.kind == file_dialog_kind::open_multiple)
        {
            Microsoft::WRL::ComPtr<IFileOpenDialog> open {};
            Microsoft::WRL::ComPtr<IShellItemArray> items {};
            if (FAILED(code = dialog.As(&open)) || FAILED(code = open->GetResults(&items)))
                return fail(u8"Cannot read chosen files", code);
            DWORD count {};
            if (FAILED(code = items->GetCount(&count)))
                return fail(u8"Cannot count chosen files", code);
            for (DWORD index { 0 }; index < count; ++index)
            {
                Microsoft::WRL::ComPtr<IShellItem> item {};
                if (FAILED(code = items->GetItemAt(index, &item)) || FAILED(code = append(item.Get())))
                    return fail(u8"Cannot read a chosen file path", code);
            }
        }
        else
        {
            Microsoft::WRL::ComPtr<IShellItem> item {};
            if (FAILED(code = dialog->GetResult(&item)) || FAILED(code = append(item.Get())))
                return fail(u8"Cannot read the chosen path", code);
        }
        result.status = file_dialog_status::chosen;
        return result;
    }
}

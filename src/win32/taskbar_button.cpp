#include "win32/taskbar_button.h"

#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <algorithm>
#include <utility>

namespace luil::win32 {
    namespace {
        [[nodiscard]] TBPFLAG progress_flag(const taskbar_progress_state state) noexcept
        {
            switch (state)
            {
            case taskbar_progress_state::indeterminate:
                return TBPF_INDETERMINATE;
            case taskbar_progress_state::normal:
                return TBPF_NORMAL;
            case taskbar_progress_state::error:
                return TBPF_ERROR;
            case taskbar_progress_state::paused:
                return TBPF_PAUSED;
            case taskbar_progress_state::none:
            default:
                return TBPF_NOPROGRESS;
            }
        }

        // 첫 실패만 남긴다. 한 묶음에서 여러 호출이 실패해도 앱이 읽는 까닭은 가장 앞의 것이다.
        void note_failure(std::u8string& error, std::u8string text)
        {
            if (error.empty())
                error = std::move(text);
        }
    }

    bool taskbar_button::open() noexcept
    {
        Microsoft::WRL::ComPtr<ITaskbarList3> shell {};
        if (FAILED(CoCreateInstance(__uuidof(TaskbarList), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell))))
            return false;
        return open(std::move(shell));
    }

    bool taskbar_button::open(Microsoft::WRL::ComPtr<ITaskbarList3> shell) noexcept
    {
        if (shell == nullptr || FAILED(shell->HrInit()))
            return false;
        shell_ = std::move(shell);
        return true;
    }

    taskbar_button::record* taskbar_button::find(const std::u8string& surface) noexcept
    {
        const auto found { std::find_if(records_.begin(), records_.end(), [&surface](const record& item) { return item.surface == surface; }) };
        return found != records_.end() ? &*found : nullptr;
    }

    const taskbar_button::record* taskbar_button::find(const std::u8string& surface) const noexcept
    {
        const auto found { std::find_if(records_.begin(), records_.end(), [&surface](const record& item) { return item.surface == surface; }) };
        return found != records_.end() ? &*found : nullptr;
    }

    taskbar_button::record& taskbar_button::record_for(const std::u8string& surface, const HWND window)
    {
        if (record* const existing { find(surface) }; existing != nullptr)
        {
            // 같은 id로 창이 다시 섰다. 옛 창의 기록과 아이콘은 새 단추와 무관하다.
            if (existing->window != window)
                *existing = record { .surface = surface, .window = window, .reported = taskbar_report { .surface = surface } };
            return *existing;
        }
        records_.push_back(record { .surface = surface, .window = window, .reported = taskbar_report { .surface = surface } });
        return records_.back();
    }

    void taskbar_button::button_created(const std::u8string& surface, const HWND window)
    {
        // 개체가 없으면 어떤 호출도 하지 않으므로 기록도 두지 않는다.
        if (shell_ == nullptr || window == nullptr)
            return;
        record& item { record_for(surface, window) };
        item.sync.button_created();
        // 새 단추에는 아무것도 걸려 있지 않다. 옛 아이콘은 다음 적용이 새로 만들어 건다.
        item.icon.reset();
        item.failed = false;
        item.error.clear();
    }

    void taskbar_button::button_lost(const std::u8string& surface) noexcept
    {
        record* const item { find(surface) };
        if (item == nullptr)
            return;
        item->sync.button_lost();
        item->icon.reset();
        item->failed = false;
        item->error.clear();
    }

    bool taskbar_button::ready(const std::u8string& surface) const noexcept
    {
        const record* const item { find(surface) };
        return item != nullptr && item->sync.ready();
    }

    void taskbar_button::execute(record& item, const taskbar_call& call, std::u8string& error)
    {
        switch (call.kind)
        {
        case taskbar_call_kind::progress_value:
            if (const HRESULT code { shell_->SetProgressValue(item.window, call.units, taskbar_progress_total) }; FAILED(code))
                note_failure(error, make_hresult_error(u8"SetProgressValue failed", code));
            return;
        case taskbar_call_kind::progress_state:
            if (const HRESULT code { shell_->SetProgressState(item.window, progress_flag(call.state)) }; FAILED(code))
                note_failure(error, make_hresult_error(u8"SetProgressState failed", code));
            return;
        case taskbar_call_kind::overlay_set:
        case taskbar_call_kind::overlay_description: {
            // 설명이 잘못된 UTF-8이면 빈 글로 보내고 까닭을 남긴다. 그림까지 버리면 앱은 아무것도 보지 못한다.
            std::wstring description {};
            if (auto converted { utf8_to_utf16(call.description) }; converted.value.has_value())
                description = std::move(*converted.value);
            else
                note_failure(error, u8"The taskbar overlay description is not valid UTF-8.");

            owned_icon fresh {};
            if (call.kind == taskbar_call_kind::overlay_set)
            {
                std::u8string icon_error {};
                fresh = make_icon(call.icon, GetSystemMetricsForDpi(SM_CXSMICON, item.dpi), icon_error);
                if (fresh.valid() == false)
                {
                    // 변환에 실패하면 이전 오버레이를 그대로 둔다.
                    note_failure(error, std::move(icon_error));
                    return;
                }
            }
            else if (item.icon.valid() == false)
                return;

            const HICON handle { fresh.valid() ? fresh.get() : item.icon.get() };
            if (const HRESULT code { shell_->SetOverlayIcon(item.window, handle, description.c_str()) }; FAILED(code))
            {
                note_failure(error, make_hresult_error(u8"SetOverlayIcon failed", code));
                return;
            }
            // 셸이 새 그림을 받은 뒤에 옛 핸들을 놓는다.
            if (fresh.valid())
                item.icon = std::move(fresh);
            return;
        }
        case taskbar_call_kind::overlay_clear:
            if (const HRESULT code { shell_->SetOverlayIcon(item.window, nullptr, L"") }; FAILED(code))
            {
                note_failure(error, make_hresult_error(u8"SetOverlayIcon failed", code));
                return;
            }
            item.icon.reset();
            return;
        }
    }

    std::optional<taskbar_report> taskbar_button::apply(const std::u8string& surface, const HWND window, const std::uint32_t dpi, const taskbar_request& wanted)
    {
        record& item { record_for(surface, window) };
        if (dpi != item.dpi)
        {
            // 오버레이 그림의 크기가 배율을 따른다. 진행률은 그대로다.
            item.dpi = dpi;
            item.sync.forget_overlay();
        }

        taskbar_report report { .surface = surface };
        const std::vector<taskbar_call> calls { item.sync.update(wanted) };
        if (shell_ == nullptr)
        {
            // 개체가 없으면 단추가 서지 않으므로 요청이 있어도 `waiting`에 머문다.
            report.status = item.sync.state() == taskbar_sync_state::off ? taskbar_status::off : taskbar_status::unavailable;
        }
        else
        {
            if (calls.empty() == false)
            {
                std::u8string error {};
                for (const taskbar_call& call : calls)
                    execute(item, call, error);
                item.failed = error.empty() == false;
                item.error = std::move(error);
            }
            switch (item.sync.state())
            {
            case taskbar_sync_state::waiting:
                // 단추가 서면 요청을 처음부터 다시 내므로 앞의 실패는 지난 일이다.
                item.failed = false;
                item.error.clear();
                report.status = taskbar_status::waiting;
                break;
            case taskbar_sync_state::applied:
            case taskbar_sync_state::off:
            default:
                if (item.failed)
                {
                    report.status = taskbar_status::failed;
                    report.error = item.error;
                }
                else
                    report.status = item.sync.state() == taskbar_sync_state::off ? taskbar_status::off : taskbar_status::applied;
                break;
            }
        }
        if (report == item.reported)
            return std::nullopt;
        item.reported = report;
        return report;
    }

    void taskbar_button::retain(const std::vector<std::u8string>& surfaces)
    {
        std::erase_if(records_, [&surfaces](const record& item) { return std::find(surfaces.begin(), surfaces.end(), item.surface) == surfaces.end(); });
    }

    void taskbar_button::forget(const std::u8string& surface) noexcept
    {
        std::erase_if(records_, [&surface](const record& item) { return item.surface == surface; });
    }

    void taskbar_button::forget_all() noexcept
    {
        records_.clear();
    }
}

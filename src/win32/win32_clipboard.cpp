#include "win32/win32_clipboard.h"

#include "win32/utf8.h"
#include "win32/win32_drop.h"

#include <shlobj.h>

#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace luil::win32 {
    namespace {
        // 클립보드를 잡는다. 다른 프로세스가 쥐고 있으면 잠깐 물러났다 다시 잡는다.
        // 끝내 못 잡으면 거짓이고, 부르는 쪽은 아무것도 바꾸지 않고 돌아간다.
        [[nodiscard]] bool open_clipboard(const HWND owner) noexcept
        {
            for (int attempt = 0; attempt < clipboard_open_attempts; ++attempt)
            {
                if (OpenClipboard(owner) != FALSE)
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds { clipboard_open_retry_ms });
            }
            return false;
        }

        // 열렸으면 반드시 닫는다 — 예외가 지나가도 `CloseClipboard`가 빠지지 않는다.
        // 닫지 않으면 같은 데스크톱의 다른 앱이 다음 열기에서 실패한다.
        struct scoped_clipboard final
        {
            explicit scoped_clipboard(const HWND owner) noexcept
                : open { open_clipboard(owner) }
            {}
            scoped_clipboard(const scoped_clipboard&) = delete;
            scoped_clipboard& operator=(const scoped_clipboard&) = delete;
            ~scoped_clipboard()
            {
                if (open)
                    CloseClipboard();
            }

            bool open { false };
        };

        // 잠갔으면 반드시 푼다 — 시스템 소유 블록이라 놓아 주는 것까지가 읽기다.
        struct scoped_global_lock final
        {
            explicit scoped_global_lock(const HANDLE source) noexcept
                : handle { source }
                , memory { GlobalLock(source) }
            {}
            scoped_global_lock(const scoped_global_lock&) = delete;
            scoped_global_lock& operator=(const scoped_global_lock&) = delete;
            ~scoped_global_lock()
            {
                if (memory != nullptr)
                    GlobalUnlock(handle);
            }

            HANDLE handle { nullptr };
            const void* memory { nullptr };
        };

        // 바이트를 담은 이동 가능 블록이다. 실패하면 nullptr다.
        [[nodiscard]] HGLOBAL global_from_bytes(const void* const data, const std::size_t bytes) noexcept
        {
            const HGLOBAL storage { GlobalAlloc(GMEM_MOVEABLE, bytes) };
            if (storage == nullptr)
                return nullptr;
            void* const memory { GlobalLock(storage) };
            if (memory == nullptr)
            {
                GlobalFree(storage);
                return nullptr;
            }
            std::memcpy(memory, data, bytes);
            GlobalUnlock(storage);
            return storage;
        }
    } // namespace

    bool copy_text_to_clipboard(const HWND owner, const std::u8string_view text) noexcept
    {
        try
        {
            const utf_conversion_result<std::wstring> converted { utf8_to_utf16(text) };
            if (converted.value.has_value() == false)
                return false;

            const std::wstring& wide { *converted.value };
            const SIZE_T bytes { (wide.size() + 1) * sizeof(wchar_t) };
            const HGLOBAL storage { GlobalAlloc(GMEM_MOVEABLE, bytes) };
            if (storage == nullptr)
                return false;

            void* const memory { GlobalLock(storage) };
            if (memory == nullptr)
            {
                GlobalFree(storage);
                return false;
            }
            std::memcpy(memory, wide.c_str(), bytes);
            GlobalUnlock(storage);

            if (open_clipboard(owner) == false)
            {
                GlobalFree(storage);
                return false;
            }
            EmptyClipboard();
            // 성공하면 소유권이 시스템으로 넘어가므로 해제하지 않는다.
            const bool stored { SetClipboardData(CF_UNICODETEXT, storage) != nullptr };
            if (stored == false)
                GlobalFree(storage);
            CloseClipboard();
            return stored;
        }
        catch (...)
        {
            return false;
        }
    }

    std::u8string first_clipboard_line(std::u8string text)
    {
        if (const std::size_t line_end { text.find_first_of(u8"\r\n\t") }; line_end != std::u8string::npos)
            text.erase(line_end);
        return text;
    }

    utf_conversion_result<std::u8string> text_from_utf16_block(const void* const data, const std::size_t bytes) noexcept
    {
        // 홀수 꼬리 바이트는 wchar_t 하나가 못 되니 버린다.
        const std::size_t units { bytes / sizeof(wchar_t) };
        if (data == nullptr || units == 0)
            return { std::u8string {}, std::nullopt };

        // NUL은 범위 안에서만 찾는다. 없으면 범위 전체가 글이다 — 경계 밖은 읽지 않는다.
        const auto* const chars { static_cast<const wchar_t*>(data) };
        std::size_t length { wcsnlen(chars, units) };

        if (length > max_clipboard_text_chars)
        {
            length = max_clipboard_text_chars;
            // 자른 자리가 surrogate 쌍의 가운데면 앞쪽 반쪽도 버린다 —
            // 반쪽이 남으면 변환이 블록 전체를 거절한다.
            if (const wchar_t last { chars[length - 1] }; last >= 0xD800u && last <= 0xDBFFu)
                --length;
        }
        return utf16_to_utf8(std::wstring_view { chars, length });
    }

    std::u8string read_text_from_clipboard(const HWND owner) noexcept
    {
        try
        {
            // 글이 없으면 파일 목록을 본다. 둘 다 없으면 열 이유가 없다.
            const bool has_text { IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE };
            const bool has_files { IsClipboardFormatAvailable(CF_HDROP) != FALSE };
            if ((has_text || has_files) == false)
                return {};

            const scoped_clipboard clipboard { owner };
            if (clipboard.open == false)
                return {};

            std::u8string result {};
            if (has_text)
            {
                if (const HANDLE handle { GetClipboardData(CF_UNICODETEXT) }; handle != nullptr)
                {
                    // 다른 프로세스가 만든 블록이다 — NUL 종단을 믿지 않고
                    // `GlobalSize`의 범위 안에서만 읽는다.
                    const scoped_global_lock lock { handle };
                    if (lock.memory != nullptr)
                        if (utf_conversion_result<std::u8string> converted { text_from_utf16_block(lock.memory, GlobalSize(handle)) }; converted.value.has_value())
                            result = std::move(*converted.value);
                }
            }
            else if (const HANDLE handle { GetClipboardData(CF_HDROP) }; handle != nullptr)
            {
                // 여러 개를 복사했어도 첫 경로 하나다 (한 줄 칸이다).
                if (std::vector<std::u8string> paths { dropped_file_paths(static_cast<HDROP>(handle)) }; paths.empty() == false)
                    result = std::move(paths.front());
            }
            return first_clipboard_line(std::move(result));
        }
        catch (...)
        {
            return {};
        }
    }

    bool absolute_clipboard_path(const std::u8string_view path) noexcept
    {
        const auto separator = [](const char8_t value) { return value == u8'\\' || value == u8'/'; };
        // UNC다 (`\\server\share`, `\\?\C:\…`).
        if (path.size() >= 3u && separator(path[0]) && separator(path[1]))
            return true;
        // 드라이브 절대 경로다 (`C:\`). `C:name`은 그 드라이브의 현재 폴더 기준이라 아니다.
        const auto letter = [](const char8_t value) { return (value >= u8'A' && value <= u8'Z') || (value >= u8'a' && value <= u8'z'); };
        return path.size() >= 3u && letter(path[0]) && path[1] == u8':' && separator(path[2]);
    }

    std::optional<std::vector<std::byte>> file_drop_block(const std::span<const std::u8string> paths)
    {
        std::wstring list {};
        for (const std::u8string& path : paths)
        {
            if (absolute_clipboard_path(path) == false || path.find(u8'\0') != std::u8string::npos)
                return std::nullopt;
            utf_conversion_result<std::wstring> converted { utf8_to_utf16(path) };
            if (converted.value.has_value() == false)
                return std::nullopt;
            for (wchar_t& value : *converted.value)
                if (value == L'/')
                    value = L'\\';
            list += *converted.value;
            list.push_back(L'\0');
        }
        // 목록의 끝은 빈 경로 하나다 (NUL 둘).
        list.push_back(L'\0');

        DROPFILES header {};
        header.pFiles = sizeof(DROPFILES);
        header.fWide = TRUE;
        std::vector<std::byte> block(sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
        std::memcpy(block.data(), &header, sizeof(DROPFILES));
        std::memcpy(block.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));
        return block;
    }

    file_clipboard_intent file_clipboard_intent_of(const std::optional<DWORD> effect) noexcept
    {
        // 탐색기의 잘라내기는 이동만(`DROPEFFECT_MOVE`), 복사는 복사+링크다. 둘 다 허락하면 받는 쪽이 고를 수 있는
        // 복사로 본다 — 원본을 옮겨 버리는 쪽으로 추측하지 않는다.
        if (effect.has_value() && (*effect & DROPEFFECT_MOVE) != 0u && (*effect & DROPEFFECT_COPY) == 0u)
            return file_clipboard_intent::cut;
        return file_clipboard_intent::copy;
    }

    bool copy_files_to_clipboard(const HWND owner, const std::span<const std::u8string> paths, const file_clipboard_intent intent) noexcept
    {
        try
        {
            // 빈 목록은 비우기다 (잘라 둔 항목을 붙여 넣은 뒤).
            if (paths.empty())
            {
                const scoped_clipboard clipboard { owner };
                return clipboard.open && EmptyClipboard() != FALSE;
            }

            const std::optional<std::vector<std::byte>> block { file_drop_block(paths) };
            const UINT effect_format { RegisterClipboardFormatW(preferred_drop_effect_format) };
            if (block.has_value() == false || effect_format == 0)
                return false;
            const DWORD effect { intent == file_clipboard_intent::cut ? static_cast<DWORD>(DROPEFFECT_MOVE) : static_cast<DWORD>(DROPEFFECT_COPY) };
            const HGLOBAL files { global_from_bytes(block->data(), block->size()) };
            const HGLOBAL effect_block { files != nullptr ? global_from_bytes(&effect, sizeof(effect)) : nullptr };
            if (effect_block == nullptr)
            {
                if (files != nullptr)
                    GlobalFree(files);
                return false;
            }

            const scoped_clipboard clipboard { owner };
            if (clipboard.open == false)
            {
                GlobalFree(files);
                GlobalFree(effect_block);
                return false;
            }
            EmptyClipboard();
            // 성공한 블록은 소유권이 시스템으로 넘어가므로 해제하지 않는다.
            if (SetClipboardData(CF_HDROP, files) == nullptr)
            {
                GlobalFree(files);
                GlobalFree(effect_block);
                return false;
            }
            // 뜻이 빠지면 잘라내기가 복사로 읽힌다 — 반쪽을 남기지 않고 비운다.
            if (SetClipboardData(effect_format, effect_block) == nullptr)
            {
                GlobalFree(effect_block);
                EmptyClipboard();
                return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    file_clipboard_content read_files_from_clipboard(const HWND owner) noexcept
    {
        file_clipboard_content content {};
        try
        {
            if (IsClipboardFormatAvailable(CF_HDROP) == FALSE)
                return content;
            const scoped_clipboard clipboard { owner };
            if (clipboard.open == false)
                return content;
            if (const HANDLE handle { GetClipboardData(CF_HDROP) }; handle != nullptr)
                content.paths = dropped_file_paths(static_cast<HDROP>(handle));

            std::optional<DWORD> effect {};
            if (const UINT format { RegisterClipboardFormatW(preferred_drop_effect_format) }; format != 0 && IsClipboardFormatAvailable(format) != FALSE)
                if (const HANDLE handle { GetClipboardData(format) }; handle != nullptr)
                {
                    // 다른 프로세스가 만든 블록이다 — 크기를 보고 읽는다.
                    const scoped_global_lock lock { handle };
                    if (lock.memory != nullptr && GlobalSize(handle) >= sizeof(DWORD))
                    {
                        DWORD value { 0 };
                        std::memcpy(&value, lock.memory, sizeof(DWORD));
                        effect = value;
                    }
                }
            content.intent = file_clipboard_intent_of(effect);
            return content;
        }
        catch (...)
        {
            return {};
        }
    }
} // namespace luil::win32

#include "win32/win32_clipboard.h"

#include "win32/utf8.h"
#include "win32/win32_drop.h"

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
} // namespace luil::win32

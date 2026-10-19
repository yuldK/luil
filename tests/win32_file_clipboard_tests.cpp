#include "win32/win32_clipboard.h"
#include "win32/win32_drop.h"

#include <catch2/catch_test_macros.hpp>

#include <shlobj.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// 파일 목록 클립보드다 (`CF_HDROP` + `Preferred DropEffect`, file-clipboard-design.md).
// 실제 클립보드를 쓰는 test는 이 프로세스의 메시지 전용 창을 임자로 삼고, 끝나면 클립보드를 비운다.
namespace {
    // 클립보드의 임자가 될 창이다. 창 없이 연 클립보드에는 `SetClipboardData`가 실패한다.
    class clipboard_window final
    {
    public:
        clipboard_window()
            : window_ { CreateWindowExW(0, L"STATIC", L"luil clipboard test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr) }
        {
            REQUIRE(window_ != nullptr);
        }
        clipboard_window(const clipboard_window&) = delete;
        clipboard_window& operator=(const clipboard_window&) = delete;
        ~clipboard_window()
        {
            if (OpenClipboard(window_) != FALSE)
            {
                EmptyClipboard();
                CloseClipboard();
            }
            DestroyWindow(window_);
        }

        [[nodiscard]] HWND handle() const noexcept
        {
            return window_;
        }

    private:
        HWND window_ { nullptr };
    };

    // 탐색기가 넣는 것처럼 손으로 지은 `DROPFILES`를 넣는다 (이 저장소의 함수를 거치지 않는다).
    void put_explorer_files(const HWND owner, const std::wstring& list, const std::optional<DWORD> effect)
    {
        DROPFILES header {};
        header.pFiles = sizeof(DROPFILES);
        header.fWide = TRUE;
        const std::size_t bytes { sizeof(DROPFILES) + list.size() * sizeof(wchar_t) };
        const HGLOBAL files { GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes) };
        REQUIRE(files != nullptr);
        auto* const memory { static_cast<unsigned char*>(GlobalLock(files)) };
        std::memcpy(memory, &header, sizeof(DROPFILES));
        std::memcpy(memory + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));
        GlobalUnlock(files);

        // 클립보드가 바뀌면 다른 프로세스(클립보드 기록 등)가 잠깐 열어 읽는다. 라이브러리처럼 물러났다 다시 잡는다.
        bool opened { false };
        for (int attempt = 0; attempt < 40 && opened == false; ++attempt)
        {
            opened = OpenClipboard(owner) != FALSE;
            if (opened == false)
                Sleep(25);
        }
        REQUIRE(opened);
        EmptyClipboard();
        REQUIRE(SetClipboardData(CF_HDROP, files) != nullptr);
        if (effect.has_value())
        {
            const HGLOBAL value { GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)) };
            REQUIRE(value != nullptr);
            std::memcpy(GlobalLock(value), &*effect, sizeof(DWORD));
            GlobalUnlock(value);
            REQUIRE(SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), value) != nullptr);
        }
        CloseClipboard();
    }
} // namespace

TEST_CASE("Clipboard paths must be absolute drive or UNC paths", "[win32][clipboard][files]")
{
    REQUIRE(luil::win32::absolute_clipboard_path(u8"C:\\work\\a.txt"));
    REQUIRE(luil::win32::absolute_clipboard_path(u8"d:/work"));
    REQUIRE(luil::win32::absolute_clipboard_path(u8"\\\\server\\share\\file"));
    REQUIRE(luil::win32::absolute_clipboard_path(u8"\\\\?\\C:\\very\\long"));
    REQUIRE(luil::win32::absolute_clipboard_path(u8"\\\\?\\UNC\\server\\share\\file"));
    // 장치 경로와 드라이브·UNC가 아닌 긴 경로는 셸 붙여넣기가 파일로 읽지 못한다.
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8"\\\\.\\COM1"));
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8"\\\\.\\C:\\a.txt"));
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\a.txt"));
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8""));
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8"work\\a.txt"));
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8"C:a.txt"));
    REQUIRE_FALSE(luil::win32::absolute_clipboard_path(u8"\\work"));
    // 셸이 이 이름을 쓴다 — 탐색기와 같은 형식에 싣는다.
    REQUIRE(std::wstring { luil::win32::preferred_drop_effect_format } == std::wstring { CFSTR_PREFERREDDROPEFFECT });
}

TEST_CASE("Clipboard UNC paths require a server and share", "[win32][clipboard][files]")
{
    const std::vector<std::u8string> valid {
        u8"\\\\server\\share",
        u8"\\\\server\\share\\",
        u8"//server/share",
        u8"//server/share/file",
        u8"\\\\?\\UNC\\server\\share",
        u8"\\\\?\\UNC\\server\\share\\",
        u8"//?/unc/server/share",
        u8"//?/UNC/server/share/file",
        u8"\\\\server/share\\file",
        u8"\\\\?\\UnC/server\\share/file",
        u8"C:\\",
        u8"d:/",
        u8"\\\\?\\C:\\",
        u8"//?/D:/",
    };
    for (const std::u8string& path : valid)
    {
        CAPTURE(path);
        REQUIRE(luil::win32::absolute_clipboard_path(path));
        REQUIRE(luil::win32::file_drop_block(std::vector<std::u8string> { path }).has_value());
    }

    // 서버명 누락, 공유명 누락, 두 이름 사이의 빈 구성 요소다.
    // 긴 경로에서도 같은 규칙이며, 하나라도 잘못되면 목록 전체를 거절한다.
    const std::vector<std::u8string> invalid {
        u8"\\\\",
        u8"\\\\server",
        u8"\\\\server\\",
        u8"\\\\\\share",
        u8"\\\\server\\\\share",
        u8"//",
        u8"//server",
        u8"//server/",
        u8"///share",
        u8"//server//share",
        u8"\\\\?\\UNC\\",
        u8"\\\\?\\UNC\\server",
        u8"\\\\?\\UNC\\server\\",
        u8"\\\\?\\UNC\\\\share",
        u8"\\\\?\\UNC\\server\\\\share",
        u8"//?/UNC/",
        u8"//?/UNC/server",
        u8"//?/UNC/server/",
        u8"//?/UNC//share",
        u8"//?/UNC/server//share",
        u8"\\\\server\\/share",
        u8"\\\\?\\UNC\\server/\\share",
    };
    for (const std::u8string& path : invalid)
    {
        CAPTURE(path);
        REQUIRE_FALSE(luil::win32::absolute_clipboard_path(path));
        REQUIRE_FALSE(luil::win32::file_drop_block(std::vector<std::u8string> { path }).has_value());
        REQUIRE_FALSE(luil::win32::file_drop_block(std::vector<std::u8string> { u8"C:\\valid.txt", path }).has_value());
    }
}

TEST_CASE("A file drop block lists wide paths and refuses relative ones", "[win32][clipboard][files]")
{
    const std::vector<std::u8string> paths { u8"C:\\자료\\보고서.txt", u8"D:/work/folder" };
    const std::optional<std::vector<std::byte>> block { luil::win32::file_drop_block(paths) };
    REQUIRE(block.has_value());
    DROPFILES header {};
    std::memcpy(&header, block->data(), sizeof(DROPFILES));
    REQUIRE(header.pFiles == sizeof(DROPFILES));
    REQUIRE(header.fWide == TRUE);
    // 경로들이 NUL로 나뉘고 NUL 둘로 끝난다. `/`는 `\`가 된다.
    const std::wstring expected { std::wstring { L"C:\\자료\\보고서.txt" } + L'\0' + L"D:\\work\\folder" + L'\0' + L'\0' };
    REQUIRE(block->size() == sizeof(DROPFILES) + expected.size() * sizeof(wchar_t));
    REQUIRE(std::memcmp(block->data() + sizeof(DROPFILES), expected.data(), expected.size() * sizeof(wchar_t)) == 0);

    const std::vector<std::u8string> mixed { u8"C:\\a", u8"relative\\b" };
    REQUIRE(luil::win32::file_drop_block(mixed).has_value() == false);
}

TEST_CASE("A file drop block strips long path prefixes the way Explorer lists paths", "[win32][clipboard][files]")
{
    const std::vector<std::u8string> paths { u8"\\\\?\\C:\\long\\a.txt", u8"\\\\?\\UNC\\nas\\share\\b", u8"//?/D:/c" };
    const std::optional<std::vector<std::byte>> block { luil::win32::file_drop_block(paths) };
    REQUIRE(block.has_value());
    const std::wstring expected { std::wstring { L"C:\\long\\a.txt" } + L'\0' + L"\\\\nas\\share\\b" + L'\0' + L"D:\\c" + L'\0' + L'\0' };
    REQUIRE(block->size() == sizeof(DROPFILES) + expected.size() * sizeof(wchar_t));
    REQUIRE(std::memcmp(block->data() + sizeof(DROPFILES), expected.data(), expected.size() * sizeof(wchar_t)) == 0);

    const std::vector<std::u8string> device { u8"C:\\a", u8"\\\\.\\PhysicalDrive0" };
    REQUIRE(luil::win32::file_drop_block(device).has_value() == false);
}

TEST_CASE("The preferred drop effect becomes cut only for a pure move", "[win32][clipboard][files]")
{
    REQUIRE(luil::win32::file_clipboard_intent_of(std::nullopt) == luil::file_clipboard_intent::copy);
    REQUIRE(luil::win32::file_clipboard_intent_of(DROPEFFECT_MOVE) == luil::file_clipboard_intent::cut);
    REQUIRE(luil::win32::file_clipboard_intent_of(DROPEFFECT_COPY) == luil::file_clipboard_intent::copy);
    REQUIRE(luil::win32::file_clipboard_intent_of(DROPEFFECT_COPY | DROPEFFECT_LINK) == luil::file_clipboard_intent::copy);
    REQUIRE(luil::win32::file_clipboard_intent_of(DROPEFFECT_COPY | DROPEFFECT_MOVE) == luil::file_clipboard_intent::copy);
}

TEST_CASE("Files written to the clipboard read back with their intent", "[win32][clipboard][files][system]")
{
    const clipboard_window window {};
    const std::vector<std::u8string> paths { u8"C:\\자료\\보고서.txt", u8"C:\\자료\\그림" };

    REQUIRE(luil::win32::copy_files_to_clipboard(window.handle(), paths, luil::file_clipboard_intent::cut));
    // 탐색기가 읽는 그 형식들이다.
    REQUIRE(IsClipboardFormatAvailable(CF_HDROP) != FALSE);
    REQUIRE(IsClipboardFormatAvailable(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)) != FALSE);
    REQUIRE(GetClipboardOwner() == window.handle());
    luil::file_clipboard_content content { luil::win32::read_files_from_clipboard(window.handle()) };
    REQUIRE(content.supported);
    REQUIRE(content.paths == paths);
    REQUIRE(content.intent == luil::file_clipboard_intent::cut);

    REQUIRE(luil::win32::copy_files_to_clipboard(window.handle(), paths, luil::file_clipboard_intent::copy));
    content = luil::win32::read_files_from_clipboard(window.handle());
    REQUIRE(content.paths == paths);
    REQUIRE(content.intent == luil::file_clipboard_intent::copy);

    // 상대 경로가 섞이면 아무것도 넣지 않는다 — 앞의 목록이 그대로 남는다.
    const std::vector<std::u8string> invalid { u8"relative.txt" };
    REQUIRE_FALSE(luil::win32::copy_files_to_clipboard(window.handle(), invalid, luil::file_clipboard_intent::cut));
    REQUIRE(luil::win32::read_files_from_clipboard(window.handle()).paths == paths);

    // 빈 목록은 클립보드를 비운다 (잘라 둔 것을 붙여 넣은 뒤).
    REQUIRE(luil::win32::copy_files_to_clipboard(window.handle(), {}, luil::file_clipboard_intent::copy));
    REQUIRE(IsClipboardFormatAvailable(CF_HDROP) == FALSE);
    REQUIRE(luil::win32::read_files_from_clipboard(window.handle()).paths.empty());
}

TEST_CASE("Clearing after a move only clears the clipboard that was read", "[win32][clipboard][files][system]")
{
    const clipboard_window window {};
    const std::vector<std::u8string> paths { u8"C:\\자료\\보고서.txt" };
    put_explorer_files(window.handle(), std::wstring { L"C:\\자료\\보고서.txt" } + L'\0' + L'\0', DROPEFFECT_MOVE);

    // 읽은 순간의 번호가 답에 실린다.
    const luil::file_clipboard_content read { luil::win32::read_files_from_clipboard(window.handle()) };
    REQUIRE(read.paths == paths);
    REQUIRE(read.sequence == static_cast<std::uint32_t>(GetClipboardSequenceNumber()));

    // 옮기는 사이 사용자가 글을 복사했다. 그 번호가 아니므로 비우지 않는다.
    REQUIRE(luil::win32::copy_text_to_clipboard(window.handle(), u8"copied meanwhile"));
    REQUIRE_FALSE(luil::win32::copy_files_to_clipboard(window.handle(), {}, luil::file_clipboard_intent::copy, read.sequence));
    REQUIRE(luil::win32::read_text_from_clipboard(window.handle()) == u8"copied meanwhile");

    // 바뀌지 않았으면 비운다.
    put_explorer_files(window.handle(), std::wstring { L"C:\\자료\\보고서.txt" } + L'\0' + L'\0', DROPEFFECT_MOVE);
    const luil::file_clipboard_content again { luil::win32::read_files_from_clipboard(window.handle()) };
    REQUIRE(luil::win32::copy_files_to_clipboard(window.handle(), {}, luil::file_clipboard_intent::copy, again.sequence));
    REQUIRE(IsClipboardFormatAvailable(CF_HDROP) == FALSE);
}

TEST_CASE("Files put on the clipboard the way Explorer does are read", "[win32][clipboard][files][system]")
{
    const clipboard_window window {};
    const std::wstring list { std::wstring { L"C:\\Users\\Public\\a.txt" } + L'\0' + L"\\\\nas\\share\\b" + L'\0' + L'\0' };

    // 뜻이 실려 있지 않으면 복사로 본다.
    put_explorer_files(window.handle(), list, std::nullopt);
    luil::file_clipboard_content content { luil::win32::read_files_from_clipboard(window.handle()) };
    REQUIRE(content.paths == std::vector<std::u8string> { u8"C:\\Users\\Public\\a.txt", u8"\\\\nas\\share\\b" });
    REQUIRE(content.intent == luil::file_clipboard_intent::copy);

    // 탐색기의 잘라내기는 이동만, 복사는 복사+링크다.
    put_explorer_files(window.handle(), list, DROPEFFECT_MOVE);
    REQUIRE(luil::win32::read_files_from_clipboard(window.handle()).intent == luil::file_clipboard_intent::cut);
    put_explorer_files(window.handle(), list, DROPEFFECT_COPY | DROPEFFECT_LINK);
    REQUIRE(luil::win32::read_files_from_clipboard(window.handle()).intent == luil::file_clipboard_intent::copy);
}

TEST_CASE("A text-only clipboard has no files", "[win32][clipboard][files][system]")
{
    const clipboard_window window {};
    REQUIRE(luil::win32::copy_text_to_clipboard(window.handle(), u8"C:\\looks\\like\\a\\path.txt"));
    const luil::file_clipboard_content content { luil::win32::read_files_from_clipboard(window.handle()) };
    REQUIRE(content.supported);
    REQUIRE(content.paths.empty());
    REQUIRE(content.intent == luil::file_clipboard_intent::copy);
    // 글은 그대로 글로 읽힌다.
    REQUIRE(luil::win32::read_text_from_clipboard(window.handle()) == u8"C:\\looks\\like\\a\\path.txt");
}

TEST_CASE("Multiline clipboard reads preserve line breaks and tabs when requested", "[win32][clipboard][system][multiline]")
{
    const clipboard_window window {};
    for (const auto text : { u8"첫 줄\r\n둘째 줄\t끝\n", u8"\n둘째 줄", u8"\t첫 칸" })
    {
        REQUIRE(luil::win32::copy_text_to_clipboard(window.handle(), text));
        REQUIRE(luil::win32::read_text_from_clipboard(window.handle(), true) == text);
        const std::u8string first { luil::win32::first_clipboard_line(text) };
        REQUIRE(luil::win32::read_text_from_clipboard(window.handle()) == first);
        REQUIRE(luil::win32::read_text_from_clipboard(window.handle(), false) == first);
    }
}

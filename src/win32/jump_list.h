#pragma once

#include "host/taskbar_sync.h"
#include "luil/win32/win32_window.h"
// 링크가 가리킬 실행 파일은 `current_executable_path`가 읽는다. 창과 test가 이 헤더만 들여 부르므로 함께 든다.
#include "win32/executable_path.h"

#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 작업 표시줄 점프 목록의 Win32 쪽이다 (docs/jump-list-design.md).
//  - 무엇을 할지(짓기·지우기·그대로)는 core의 `jump_list_sync`가 고르고, 여기는 요청을 셸 링크 값으로 옮겨
//    `ICustomDestinationList`에 낸다. 옮기기(`make_jump_task_links`)는 COM이 없는 순수 계산이다.
//  - UI thread의 STA에서만 부른다.
namespace luil::win32 {
    // 셸 링크 글 칸의 상한이다 (`INFOTIPSIZE` 1024에서 끝 NUL을 뺀다). 이은 인자 줄과 설명이 이 안에 든다.
    inline constexpr std::size_t jump_task_text_limit { 1023 };

    // 셸 링크의 제목 속성(`PKEY_Title`)이다. `propkey.h`의 값과 같다.
    //  - 그 헤더의 상수는 `INITGUID` 없이 쓰면 `propsys`를 링크해야 해서 값을 여기 둔다.
    inline constexpr PROPERTYKEY jump_task_title_key { { 0xf29f85e0u, 0x4ff9u, 0x1068u, { 0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9 } }, 2 };

    // 작업 하나를 셸 링크에 옮긴 값이다.
    struct jump_task_link
    {
        // 띄울 실행 파일이다. 늘 이 프로세스의 실행 파일이다.
        std::wstring path {};
        // 인자를 하나씩 감싸 빈칸으로 이은 줄이다. `CommandLineToArgvW`가 같은 목록으로 되읽는다.
        std::wstring arguments {};
        std::wstring title {};
        std::wstring description {};
        // 비지 않는다. 요청의 경로가 비면 실행 파일이다.
        std::wstring icon_path {};
        int icon_index { 0 };

        [[nodiscard]] bool operator==(const jump_task_link&) const = default;
    };

    // 요청을 셸 링크 값으로 옮긴다. 하나라도 규칙 밖이면 빈 값이고 `error`에 까닭이 있다.
    //  - 제목이 비었거나, 글이 잘못된 UTF-8이거나 NUL을 품었거나, 인자 줄·설명이 `jump_task_text_limit`를 넘거나,
    //    아이콘 경로가 `MAX_PATH` 이상이면 규칙 밖이다. 실행 파일 경로가 비어도 그렇다.
    [[nodiscard]] std::optional<std::vector<jump_task_link>> make_jump_task_links(const taskbar_jump_list& list, std::wstring_view executable, std::u8string& error);

    // 링크 값으로 셸 링크 개체를 짓는다. 제목은 링크의 속성 저장소에 싣는다.
    [[nodiscard]] HRESULT make_shell_link(const jump_task_link& link, Microsoft::WRL::ComPtr<IShellLinkW>& result);

    // 점프 목록을 셸에 내는 자리다. 주 창 하나가 쥔다.
    class jump_list_writer
    {
    public:
        // 셸 개체를 짓는 길이다. 기본은 `CoCreateInstance(CLSID_DestinationList)`이고 test가 가짜 개체로 바꾼다.
        using list_factory = std::function<HRESULT(Microsoft::WRL::ComPtr<ICustomDestinationList>&)>;

        jump_list_writer();

        // 앱 식별(AUMID)과 실행 파일이다. id가 비면 `SetAppID`를 부르지 않고 셸이 실행 파일에서 짓는 기본 식별을 쓴다.
        void configure(std::u8string_view app_id, std::wstring executable);
        void set_factory(list_factory factory);

        // 요청을 마지막으로 낸 것과 견줘 바뀌었으면 셸에 내고 보고를 돌려준다. 할 일이 없으면 빈 값이다.
        [[nodiscard]] std::optional<jump_list_report> apply(const std::optional<taskbar_jump_list>& wanted);

        // 할 일 하나를 셸에 낸다. `apply`가 부르고, 실측 test가 바로 부른다.
        [[nodiscard]] jump_list_report run(const jump_list_call& call);

    private:
        jump_list_sync sync_ {};
        std::wstring app_id_ {};
        std::wstring executable_ {};
        list_factory factory_ {};
    };
}

# 작업 표시줄 점프 목록

작업 표시줄 단추를 오른쪽 클릭하면 서는 점프 목록에 앱이 작업(task)을 싣는 길이다. 작업 하나는 "이 실행 파일을 이 인자로 띄운다"는 셸 링크다. 앱이 떠 있으면 새로 뜬 프로세스가 [인자 넘기기](single-instance-args-design.md)의 `single_instance::send_to_primary`로 첫째에 인자를 넘기고 끝난다. 앱은 `ui_frame::taskbar`의 새 칸 `jump_list`에 지금 바라는 목록을 싣고, UI thread가 바뀌었을 때만 `ICustomDestinationList`로 다시 짓는다. Win32 전용이고 Android는 `unsupported`다(받고 무시한다). 공개 형식은 [taskbar.h](../include/luil/app/taskbar.h)의 `taskbar_jump_task`·`taskbar_jump_list`·`taskbar_request::jump_list`와 [win32_window.h](../include/luil/win32/win32_window.h)의 `jump_list_report`·`window_delegate::make_jump_list_message`다.

이 노드는 [인자 넘기기](single-instance-args-design.md) 결정 8의 길 a(2026-11-02 승인)다. [작업 표시줄](taskbar-design.md)이 "점프 목록" 약속과 범위 밖에서 G7과 앱 식별 뒤로 미룬 일이다. G7은 인자 넘기기로, 앱 식별은 [OS 알림](os-notification-design.md)의 `window_config::identity`로 섰다. 이 노드가 R1 파일 `taskbar.h`·`taskbar_sync`를 다시 연다.

## 문제

- 점프 목록을 짓는 코드가 없다. `src/`·`include/`·`examples/`·`tests/` 어디에도 `ICustomDestinationList`, `IShellLinkW`, `AddUserTasks`가 없다.
- 앱이 직접 짤 수는 있으나 맞추기 어렵다. 목록은 창이 아니라 앱 식별(AUMID)에 묶인다. 식별을 단 앱은 같은 id를 `SetAppID`로 줘야 하고, 어긋나면 목록이 다른 단추에 붙거나 보이지 않는다. 식별은 라이브러리가 `run_application_window` 안에서 건다([os-notification-design.md](os-notification-design.md)의 "앱 식별(Windows)"). 앱이 `wWinMain`에서 따로 지으면 이 순서와 id를 앱마다 다시 맞춘다.
- 목록은 프로세스 밖에 남는다. 셸이 사용자 프로필에 적어 두고 앱이 꺼진 뒤에도, 탐색기가 다시 뜬 뒤에도 보인다. 진행률·오버레이처럼 창이 사라지면 함께 사라지는 상태가 아니라, "지금 바라는 것"과 "지워 달라"를 갈라 말할 길이 든다.
- 작업은 새 프로세스다. 인자 넘기기가 서기 전에는 떠 있는 앱에 닿지 않고 창이 하나 더 떴다. 이제 `send_to_primary`가 있어 앱이 그 길을 쓰면 닿는다.

## 공개 형식

`include/luil/app/taskbar.h`, namespace `luil`. core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake)) OS 헤더가 없다. `<optional>`·`<vector>`만 더한다.

```cpp
namespace luil {
    // 점프 목록의 작업 하나다. 누르면 셸이 이 앱의 실행 파일을 `arguments`로 새로 띄운다.
    struct taskbar_jump_task
    {
        // 목록에 보이는 글이다. 비면 안 된다.
        std::u8string title {};
        // 실행 파일 뒤에 붙일 인자다. 따옴표는 라이브러리가 붙인다 (argv 경계가 그대로 산다).
        std::vector<std::u8string> arguments {};
        // 마우스를 올리면 보이는 설명이다. 비면 없다.
        std::u8string description {};
        // 아이콘 파일(.ico·.exe·.dll)의 절대 경로와 그 안의 번호다. 경로가 비면 이 실행 파일의 `icon_index`번이다.
        std::u8string icon_path {};
        int icon_index { 0 };

        [[nodiscard]] bool operator==(const taskbar_jump_task&) const = default;
    };

    struct taskbar_jump_list
    {
        // 셸의 "작업" 범주에 위에서 아래로 선다. 비면 이 앱의 목록을 지운다.
        std::vector<taskbar_jump_task> tasks {};

        [[nodiscard]] bool operator==(const taskbar_jump_list&) const = default;
    };

    struct taskbar_request
    {
        taskbar_progress progress {};
        taskbar_overlay overlay {};
        // 비어 있으면 셸의 점프 목록을 만지지 않는다 (기본). 값이 있으면 그 목록으로 다시 짓고, 작업이 없으면 지운다.
        // 주 창(`ui_frame::taskbar`)의 것만 읽는다. 독립 창(`ui_window::taskbar`)의 것은 읽지 않는다.
        std::optional<taskbar_jump_list> jump_list {};
    };
}
```

Win32 보고는 [win32_window.h](../include/luil/win32/win32_window.h)의 `taskbar_report` 옆에 둔다.

```cpp
namespace luil::win32 {
    enum class jump_list_status
    {
        applied,     // 작업 목록을 지었다
        removed,     // 목록을 지웠다
        unavailable, // 셸 개체(ICustomDestinationList)를 만들지 못했다 (com_sta_scope가 없는 앱 등)
        failed,      // 요청이 규칙 밖이거나 셸 호출이 실패했다. error에 까닭이 있다
    };

    struct jump_list_report
    {
        jump_list_status status { jump_list_status::applied };
        // 지은 작업 수다. 지웠거나 실패했으면 0이다.
        std::size_t tasks { 0 };
        std::u8string error {};
    };
    // window_delegate::make_jump_list_message(const jump_list_report&)  기본은 빈 메시지
}
```

앱의 모양은 이렇다. 첫째는 목록을 싣고, 작업이 띄운 둘째는 인자를 첫째에 넘기고 끝낸다.

```cpp
luil::taskbar_jump_list list {};
list.tasks.push_back({ .title = u8"새 메모", .arguments = { u8"--new-note" } });
frame->taskbar.jump_list = list;
```

비공개 파일은 `src/win32/jump_list.h`·`.cpp`다. 요청을 셸 링크 값으로 옮기는 순수 변환(`make_jump_task_links`)과 셸에 내는 `jump_list_writer`가 든다. 무엇을 할지(다시 짓기·지우기·그대로)는 core의 `src/host/taskbar_sync.h`에 더하는 `jump_list_sync`가 고른다.

## 규칙

**선언 모델.** 진행률·오버레이와 같다. 앱은 frame마다 지금 바라는 목록을 그대로 싣고, UI thread가 `synchronize_taskbar`에서 주 창 단추를 맞춘 다음 마지막으로 낸 목록과 값으로 견준다. 같으면 아무것도 하지 않는다.

| 요청 | 셸에 하는 일 |
| --- | --- |
| 비어 있음 (`std::nullopt`, 기본) | 없다. 앞 실행이 지은 목록도 그대로 둔다 |
| 작업이 있는 목록 | 마지막으로 낸 것과 다르면 다시 짓는다 |
| 작업이 없는 목록 | 마지막으로 낸 것과 다르면 지운다 |

- 처음 낸 것은 없다고 본다. 그래서 첫 frame의 목록은 앞 실행이 지은 것과 같아도 한 번 다시 짓는다(셸에는 읽는 길이 없다). 빈 목록도 첫 번에 한 번 지운다.
- 값이 있다가 비어 있음으로 돌아가면 아무것도 하지 않고 마지막으로 낸 기록을 쥔다. 그 뒤 같은 목록이 다시 와도 호출이 없다.
- 비교는 값이다. 작업의 글·인자·아이콘 칸이 하나라도 다르면 목록 전체를 다시 짓는다. 셸에는 항목 하나만 바꾸는 길이 없다.
- 셸 호출은 사용자 프로필의 파일을 쓴다. 앱은 목록을 자주 바꾸지 않는다. 라이브러리는 바뀔 때마다 그대로 낸다(묶어 늦추지 않는다).
- 호출이 실패해도 낸 것으로 기록하고 재시도하지 않는다. 다음 변화에서 다시 한다. 보고는 셸에 낸 일마다 한 번이다.

**단추와 무관하다.** 목록은 창이 아니라 앱 식별에 묶이고 셸이 파일로 쥔다. 그래서 진행률과 달리 `TaskbarButtonCreated`를 기다리지 않고, 주 창이 숨은 동안(`main_hidden`)에도 낸다. 탐색기가 다시 떠도 다시 내지 않는다. 같은 앱의 독립 창 단추도 같은 식별이라 같은 목록을 보인다. 라이브러리는 끝날 때 목록을 지우지 않는다. 꺼진 앱의 단추(고정한 단추)에서도 작업을 누를 수 있어야 해서다([OS 알림](os-notification-design.md)이 식별 항목을 남기는 까닭과 같다).

**작업 짓기.** 작업마다 `IShellLinkW`(`CLSID_ShellLink`)를 짓는다.

- 경로는 이 프로세스의 실행 파일(`GetModuleFileNameW`)이다. 다른 프로그램을 띄우는 작업은 없다.
- 인자는 `win32::quote_argument`([command_line.h](../src/win32/command_line.h))로 하나씩 감싸 빈칸으로 잇는다. `CommandLineToArgvW`가 같은 목록으로 되읽는다. 작업 폴더는 정하지 않는다. 상대 경로 인자는 뜻이 없다.
- 제목은 링크의 `IPropertyStore`에 `PKEY_Title`(`VT_LPWSTR`)로 싣는다. 제목이 없는 셸 링크는 목록에 글이 서지 않는다. 설명은 `SetDescription`, 아이콘은 `SetIconLocation(경로, 번호)`이고 경로가 비면 실행 파일이다.
- `PKEY_Title` 값은 `propkey.h`가 내는 것과 같은 상수를 `jump_list.cpp`에 둔다. `PROPVARIANT`는 `CoTaskMemAlloc`으로 손수 짓고 `PropVariantClear`로 푼다. 그래서 `propsys`·`shlwapi`를 링크하지 않는다([작업 표시줄](taskbar-design.md)이 적은 `propsys`는 쓰지 않는다).

**검사.** 셸을 부르기 전에 모두 본다. 하나라도 어기면 셸을 부르지 않고 `failed`다.

- 작업마다 제목이 비지 않았다. 모든 글(제목·인자·설명·아이콘 경로)이 올바른 UTF-8이고 NUL이 없다.
- 이은 인자 줄과 설명은 1023 UTF-16 코드 단위 이하다. 셸 링크의 글 칸 상한(`INFOTIPSIZE`, 끝 NUL 포함 1024)이다. 아이콘 경로는 `MAX_PATH` 미만이다. 제목은 따로 묶지 않는다(셸이 자른다).
- 작업 수는 묶지 않는다. 셸은 화면 높이와 설정에 맞는 수만 보이고 나머지는 잘린다고 알려져 있다(실측 전).

**다시 짓기.** 아래 순서다. 중간에 실패하면 `AbortList`로 거두고 `failed`다.

1. `CoCreateInstance(CLSID_DestinationList)`. 실패하면 `unavailable`이다. UI thread의 STA(`com_sta_scope`)에서 부른다.
2. 앱 식별이 있으면 `SetAppID(identity.id)`. 없으면 부르지 않고 셸이 실행 파일에서 짓는 기본 식별을 쓴다(결정 5).
3. `BeginList`. 사용자가 지운 항목 목록이 오지만 작업 범주는 사용자가 지울 수 없어 읽지 않는다.
4. `CLSID_EnumerableObjectCollection`에 링크를 차례로 넣고 `AddUserTasks`.
5. `CommitList`. 성공하면 `applied`이고 `tasks`가 작업 수다.

**지우기.** 앱 식별이 있으면 `DeleteList(identity.id)`다. 없으면 `DeleteList`에 넘길 id가 없으므로 `BeginList`·`CommitList`로 빈 목록을 짓는다. 둘 다 `removed`다.

**인자의 길.** 작업은 앱의 실행 파일을 새로 띄우는 것이라 라이브러리가 인자를 해석하거나 첫째로 보내지 않는다. 앱은 같은 인자를 두 곳에서 받는다.

- 앱이 떠 있으면 새 프로세스가 둘째다. 앱이 `send_to_primary`로 첫째에 넘기면 첫째의 `window_delegate::on_instance_arguments`로 온다. 첫째는 앞으로 선다([인자 넘기기](single-instance-args-design.md) 결정 5).
- 앱이 꺼져 있으면 새 프로세스가 첫째다. 앱은 명령 줄(`command_line_arguments()`)로 같은 인자를 읽는다.
- 둘 다 명령 줄과 같이 믿을 수 없는 입력이다. 같은 데스크톱의 다른 프로세스도 같은 인자로 앱을 띄울 수 있다.

**접근성·배율·테마·움직임 줄이기.** 목록은 셸이 그리고 읽는다. luil은 글과 아이콘 자리만 준다. 고대비·배율·테마는 셸이 따른다. 제목과 설명은 보조 기술이 읽는 글이라 앱이 사람이 읽는 글로 준다.

## 결정

**1. 담는 자리.** 질문: 앱이 목록을 어디에 싣는가.
- 길 a: `taskbar_request::jump_list`. 주 창의 것만 읽는다. 되는 것: 진행률·오버레이와 같은 frame 칸, 같은 적용 자리(`synchronize_taskbar`)다. 앱이 작업 표시줄의 바라는 상태를 한 곳에서 짓는다. 대가: 독립 창의 `ui_window::taskbar`에도 같은 칸이 생기지만 읽지 않는다는 규칙이 하나 는다. R1 파일(`taskbar.h`·`taskbar_sync`)을 다시 연다.
- 길 b: `ui_frame::jump_list`로 따로 둔다. 되는 것: 독립 창에 읽지 않는 칸이 없다. 대가: 작업 표시줄의 상태가 두 칸으로 갈리고 승인된 범위와 다르다.
- 길 c: `app_host`의 setter. 대가: [작업 표시줄](taskbar-design.md) 결정 3이 setter를 고르지 않은 까닭(마지막 값 slot, 다시 걸기)과 같다.
- 권장: a. 대가: 독립 창의 읽지 않는 칸 하나. **승인: 길 a (2026-11-02, 인자 넘기기 결정 8의 길 a).**

**2. 손대지 않음과 지움.** 질문: 기본값과 "지워 달라"를 어떻게 가르는가. 목록은 프로세스 밖에 남아 기본값이 "아무것도 없음"이면 앞 실행의 목록을 지워야 하는지가 갈린다.
- 길 a: `std::optional<taskbar_jump_list>`. 비어 있으면 손대지 않고, 작업이 없는 목록이면 지운다. 되는 것: 점프 목록을 쓰지 않는 앱은 셸을 한 번도 부르지 않는다. 쓰던 앱은 빈 목록으로 지운다. 대가: 칸 하나에 상태가 셋이라 주석이 길다.
- 길 b: 값 칸이고 빈 목록은 손대지 않음이다. 되는 것: 없다. 대가: 앞 실행이 지은 목록을 지울 길이 없다.
- 길 c: 값 칸이고 빈 목록이면 첫 frame에 늘 지운다. 되는 것: 앞 실행의 목록이 남지 않는다. 대가: 점프 목록을 쓰지 않는 모든 앱이 시작마다 셸 호출을 하나 한다. 설치기 같은 다른 길이 지은 목록도 지운다.
- 권장: a. 대가: 상태 셋. **구현하며 정함(권장, 2026-11-02).**

**3. 항목의 종류.** 질문: 작업 말고도 싣는가.
- 길 a: 작업(`AddUserTasks`)만. 되는 것: 사용자가 항목을 지울 수 없는 범주라 `BeginList`가 돌려주는 지운 항목을 다룰 일이 없다. 대가: 구분선과 사용자 범주가 없다.
- 길 b: 작업 사이의 구분선(`PKEY_AppUserModel_IsDestListSeparator`)을 더한다. 대가: 작업 칸이 종류를 갖고 구분선의 제목·인자를 비워야 하는 규칙이 는다.
- 길 c: 사용자 범주(`AppendCategory`)와 최근·자주 목록. 대가: 사용자가 지운 항목을 다시 넣으면 `AppendCategory`가 실패해 지운 목록을 앱에 돌려줘야 하고, 최근 목록은 파일 형식 등록이 든다.
- 권장: a. 소비자 없이 정하면 틀리기 쉬운 모양이고 필요해지면 `taskbar_jump_list`에 칸을 더해 닫힌다. 대가: 구분선과 범주가 없다. **구현하며 정함(권장, 2026-11-02).**

**4. 아이콘의 원천.** 질문: 작업의 아이콘을 무엇으로 받는가.
- 길 a: 파일 경로와 번호(`SetIconLocation`). 경로가 비면 실행 파일이다. 되는 것: 셸이 아이콘 자원을 직접 읽어 배율마다 알맞은 크기를 고른다. 값 비교가 된다. 대가: 실행 중에 지은 그림을 못 쓴다.
- 길 b: `ui_image`를 받아 임시 `.ico`로 써서 그 경로를 준다. 되는 것: 그린 그림을 쓴다. 대가: 목록이 프로세스 밖에 남으므로 임시 파일도 남겨야 하고 지울 때를 정해야 한다. 그림 비교가 몸통 포인터라 값 비교와 섞인다.
- 길 c: 아이콘을 받지 않는다. 대가: 모든 작업이 실행 파일 아이콘이다.
- 권장: a. 대가: 실행 중에 지은 그림을 못 쓴다. **구현하며 정함(권장, 2026-11-02).**

**5. 앱 식별(AUMID).** 질문: 어느 id의 목록으로 짓는가. 근거: [OS 알림](os-notification-design.md) 결정 2의 실측(2026-11-03)에서 레지스트리 AUMID만 쓴 빌드 폴더의 실행 파일이 토스트를 올렸다. 식별을 거는 길(`SetCurrentProcessExplicitAppUserModelID`)은 선다. 진행률·오버레이는 식별과 무관하게 `HWND`에 건다([작업 표시줄](taskbar-design.md)의 "앱 식별").
- 길 a: `window_config::identity.id`가 있으면 `SetAppID(id)`, 없으면 부르지 않는다. 되는 것: 식별을 단 앱은 단추와 같은 id에 목록이 붙는다. 식별 없는 앱도 실행 파일에서 지은 기본 식별에 목록이 붙는다. 대가: 프로세스에 명시 식별이 있을 때 `SetAppID` 없이도 같은 id가 쓰이는지는 재지 않고 늘 부른다.
- 길 b: 식별이 없으면 목록을 짓지 않는다(`unavailable`). 되는 것: id가 늘 분명하다. 대가: 알림을 쓰지 않는 앱도 점프 목록 때문에 레지스트리 식별을 단다.
- 길 c: `SetAppID`를 부르지 않는다. 대가: 식별을 단 앱의 목록이 프로세스 식별을 따르는지에 기댄다.
- 권장: a. 실측이 성공해 식별 없이 프로세스 AUMID만 쓰거나 막는(blocked) 길로 물러설 까닭이 없다. 대가: 식별 없는 앱의 목록은 실행 파일 경로에 묶여, 실행 파일을 옮기면 목록이 따라가지 않는다. **구현하며 정함(권장, 2026-11-02).**

**6. 상태 보고.** 질문: 셸에 무엇이 닿았는지 앱이 아는가.
- 길 a: `window_delegate::make_jump_list_message(const jump_list_report&)`가 셸에 낸 일마다 한 번 알린다. 되는 것: 데모와 UIA가 적용·지움·실패를 되읽는다. `taskbar_report`의 모양과 같다. 대가: 공개 타입 둘(enum·struct)과 가상 함수 하나.
- 길 b: `taskbar_report`에 칸을 더한다. 대가: 그 보고는 단추마다 상태가 바뀔 때 오는 것이라 단추와 무관한 목록의 일이 주 창 단추 보고에 섞인다. 기존 비교(`operator==`)와 데모의 반영 줄이 바뀐다.
- 길 c: 알리지 않는다. 대가: `ICustomDestinationList`에는 읽는 길이 없어 실패가 디버거 출력에만 남는다.
- 권장: a. 대가: 공개 타입 둘과 가상 함수 하나. **구현하며 정함(권장, 2026-11-02).**

**7. 능력 플래그.** 질문: 앱이 점프 목록을 낼 수 있는지 어디서 아는가.
- 길 a: `ui_platform_capabilities::taskbar`를 그대로 쓴다. 참은 작업 표시줄 요청(진행률·오버레이·점프 목록)을 셸에 낼 수 있다는 뜻이다. 되는 것: 같은 플랫폼, 같은 COM 조건이라 값이 같다. 다른 줄이 끝에 더하는 플래그와 부딪히지 않는다. 대가: 셸 개체가 따로라 드물게 하나만 서는 경우를 플래그가 가르지 못한다. 그때는 보고가 `unavailable`이다.
- 길 b: `bool jump_list`를 끝에 더한다. 대가: 시작할 때 셸 개체를 한 번 더 만들어 봐야 하고 플래그 하나가 는다.
- 권장: a. 대가: 드문 경우는 보고가 말한다. **구현하며 정함(권장, 2026-11-02).**

**8. 데모 작업의 인자.** 질문: 데모가 싣는 작업이 무엇을 띄우는가.
- 길 a: 인자 넘기기가 만든 `--send-to-first`를 앞에 단다(`--send-to-first jump:note`). 되는 것: 작업을 누르면 둘째가 떠 첫째의 창 페이지 `둘째 인스턴스 인자` 줄에 `[jump:note]`가 선다. 승인된 "새 프로세스가 첫째에 닿는다"를 데모가 그대로 보인다. `demo_main.cpp`의 인자 처리를 고치지 않는다. 대가: 데모가 꺼져 있을 때 누르면 그 실행이 첫째라 `not_found`(종료 코드 3)로 끝나고 창이 뜨지 않는다.
- 길 b: 새 데모 플래그(`--jump-task`)를 두어 첫째면 그 페이지로 뜨고 둘째면 보낸다. 되는 것: 꺼진 데모에서도 창이 뜬다. 대가: 여러 줄이 고치는 `demo_main.cpp`의 인자 처리를 이 노드가 다시 연다.
- 권장: a. 대가: 꺼진 데모에서 누르면 아무것도 뜨지 않는다. **구현하며 정함(권장, 2026-11-02).**

## 플랫폼

### Win32

- `application_window`가 `jump_list_writer` 하나를 쥔다. `create()`가 `config_.identity.id`와 실행 파일 경로로 설정한다. smoke 모드는 host가 없어 적용 자리에 오지 않는다.
- 적용 자리는 [win32_window.cpp](../src/win32/win32_window.cpp)의 `synchronize_taskbar`다. 주 창 단추를 맞춘 다음, 독립 창을 훑기 전에 `frame->taskbar.jump_list`로 `jump_list_writer::apply`를 부르고 보고를 `make_jump_list_message`로 게시한다. 이 함수는 게시 wake와 단추 생성 메시지가 부르는데, 값 비교라 단추 생성에서 다시 짓지 않는다.
- 셸 개체는 호출마다 만들고 놓는다. 쥐어 둘 상태가 없고 목록을 바꾸는 일이 드물다. 모두 `ComPtr`이고 새로 링크할 라이브러리는 없다(`ole32`·`shell32`는 이미 있다).
- 레이어드 표시와 Direct3D 합성 창은 같다. 창을 쓰지 않는다.

### Android

- unsupported다. host는 `ui_frame::taskbar`를 통째로 읽지 않으므로 `jump_list`도 받고 무시한다. 로그도 없다. 능력 플래그(`taskbar`)는 거짓이다.
- 가까운 기능은 런처 숏컷(`ShortcutManager`)이다. JNI로 부를 수는 있으나 숏컷이 여는 Intent를 살아 있는 Activity가 받으려면 `onNewIntent`가 든다. `GameActivity`에 그 콜백이 없고 하위 클래스는 Java라 [Java 0줄 방침](android-port-plan.md)과 부딪힌다([인자 넘기기](single-instance-args-design.md)의 Android 절과 같은 까닭). 범위 밖이다.

## 다른 노드와의 약속

- **작업 표시줄(R1).** `taskbar.h`에 타입 둘과 칸 하나, `taskbar_sync.h`·`.cpp`에 `jump_list_sync`를 더한다. 기존 `taskbar_sync`와 `taskbar_button`은 고치지 않는다. 진행률·오버레이의 기본값 판정(`is_default`)은 점프 목록을 보지 않는다. 단추 보고(`taskbar_report`)는 진행률·오버레이만 말한다. 구현 커밋이 [taskbar-design.md](taskbar-design.md)의 머리말·"앱 식별"·"점프 목록"·범위 밖을 이 문서로 잇고 `propsys` 말을 고친다.
- **인자 넘기기(줄 4, 앞).** `send_to_primary`와 `on_instance_arguments`를 그대로 쓴다. 그 API를 고치지 않는다. 구현 커밋이 그 문서의 결정 8·약속·범위 밖을 이 문서로 잇는다.
- **OS 알림(줄 4, 앞).** `window_config::identity`를 읽기만 한다. `app_identity`와 `notification_win32`는 고치지 않는다. 실행 파일 경로를 읽는 몇 줄은 `jump_list.cpp`에 따로 둔다(`notification_win32.cpp`를 다시 열지 않는다). 구현 커밋이 그 문서의 약속과 범위 밖의 "점프 목록"을 이 문서로 잇는다.
- **`win32_window.cpp`.** 이 노드는 `synchronize_taskbar` 안의 한 줄, `post_jump_list_report` 함수 하나, `create()`의 설정 한 줄, 멤버 하나를 더한다. image_clip(줄 4, 다음)이 같은 파일을 이 노드 뒤에 고친다.
- **데모 공용 파일.** `examples/demo_main.cpp`에는 `make_jump_list_message` override 하나를 `make_taskbar_message` 뒤에 더한다. 요청은 이미 `frame->taskbar`에 실린다. `examples/demo/windows_page.*`는 작업 표시줄 구역 끝에 점프 목록 줄을 더한다. 라이브러리 커밋과 데모 커밋을 나눈다. `docs/README.md` 색인은 통합 때 붙인다.

## 확인

- `tests/taskbar_sync_tests.cpp`(core, 추가): 비어 있음은 호출 없음(처음과 중간 모두). 첫 목록은 다시 짓기, 첫 빈 목록은 지우기. 같은 목록은 호출 없음. 비어 있음을 지난 뒤 같은 목록은 호출 없음. 칸 하나(제목·인자·설명·아이콘 번호)가 달라지면 다시 짓기. 작업이 있다가 빈 목록이면 지우기, 다시 빈 목록이면 없음.
- `tests/jump_list_tests.cpp`(새 파일, `luil_tests`): 변환이 실행 파일 경로를 쓰는지, 인자 따옴표(빈칸·따옴표·빈 인자·끝 백슬래시)가 `CommandLineToArgvW`로 같은 목록이 되는지, 아이콘 경로가 비면 실행 파일인지, 빈 제목·잘못된 UTF-8·NUL·1024 코드 단위 인자 줄과 설명·`MAX_PATH` 아이콘 경로의 거절과 1023의 통과. 가짜 `ICustomDestinationList`를 꽂은 `jump_list_writer`로 호출 순서(`SetAppID`→`BeginList`→`AddUserTasks`→`CommitList`), 식별이 없으면 `SetAppID`가 없는지, `AddUserTasks`가 받은 링크를 되읽어 경로·인자·설명·아이콘·`PKEY_Title`이 맞는지, 지우기가 식별이 있으면 `DeleteList(id)`이고 없으면 빈 `BeginList`·`CommitList`인지, `BeginList`·`AddUserTasks`·`CommitList` 실패가 `failed`이고 시작한 목록을 `AbortList`로 거두는지, 셸 개체를 만들지 못하면 `unavailable`인지, 규칙 밖 요청은 셸을 부르지 않는지, 같은 요청을 다시 주면 보고가 없는지.
- 실측(구현 단계, 사용자 허락, 2026-11-02 X-8 a): `luil_tests "[.jump-list-live]"`가 `luil.test.jumplist` 식별로 작업 하나를 실제로 `CommitList`하고 `%APPDATA%\Microsoft\Windows\Recent\CustomDestinations`의 파일이 하나 늘었는지 본 뒤, 같은 id로 `DeleteList`하고 그 파일이 사라졌는지 본다. ctest는 이 test를 돌리지 않는다. 작업 표시줄 오른쪽 클릭으로 보이는지는 사람이 본다.
- `tests/windows_page_tests.cpp`(추가): 점프 목록 두 줄의 이름(`점프 목록 요청: 손대지 않음`, `점프 목록 반영: 아직 보고 없음`), 채우기 뒤 요청이 작업 둘과 `--send-to-first` 인자를 싣는지, 비우기 뒤 빈 목록인지, 보고 intent가 반영 줄(`적용됨 · 작업 2개`, `지움`, `실패 (<진단>)`)에 닿는지, 독립 창 요청에는 목록이 없는지.
- 데모 `examples/demo/windows_page.cpp`의 작업 표시줄 구역 끝: 단추 `점프 목록 채우기`·`점프 목록 비우기`, label 둘이 UIA로 읽힌다. 요청 `점프 목록 요청: 손대지 않음`(채우면 `작업 2개`, 비우면 `비움`), 반영 `점프 목록 반영: 아직 보고 없음`(받으면 `적용됨 · 작업 2개`·`지움`·`쓸 수 없음`·`실패 (<진단>)`). 작업은 `메모로 보내기`(`--send-to-first jump:note`)와 `그림으로 보내기`(`--send-to-first jump:image`)다. `examples/demo_main.cpp`가 `make_jump_list_message`를 `jump_list_report_intent`로 페이지에 넘긴다. 데모는 식별 `luil.Demo`를 달아 그 id로 짓는다.
- 모바일 데모는 고치지 않는다. 기존 `taskbar-none` 줄(`이 플랫폼에는 작업 표시줄이 없다.`)이 점프 목록도 덮는다. 고친다면 읽을 값이 같은 거짓 하나다.
- 격리 데스크톱: UIA 되읽기는 데모 label까지다. 셸 개체는 데스크톱과 무관하게 사용자 프로필에 쓰므로 격리 데스크톱에서 `점프 목록 채우기`를 누르면 실제 `luil.Demo` 목록이 생긴다(추정). 검증 뒤 `점프 목록 비우기`로 지운다.
- 사람 확인: 작업 표시줄 단추를 오른쪽 클릭하면 "작업"에 두 항목이 제목·아이콘과 함께 서는지, 누르면 떠 있는 데모의 `둘째 인스턴스 인자` 줄이 바뀌고 창이 앞으로 서는지, 데모를 끈 뒤 고정한 단추에서도 목록이 보이는지, 비우기 뒤 사라지는지, 식별 없는 앱(빌드 폴더의 실행 파일)에서 서는지, 탐색기를 다시 시작한 뒤에도 남는지, 시작 메뉴의 "최근에 연 항목 표시"를 끈 설정에서 작업이 서는지, 고대비·배율에서 아이콘과 글.

## 범위 밖

- 구분선, 사용자 범주, 최근·자주 목록(결정 3). 파일 형식 등록과 `SHAddToRecentDocs`.
- 실행 중에 지은 그림의 아이콘(결정 4).
- 앱 제거 때 목록 지우기. 앱은 빈 목록을 실어 지운다. 제거기처럼 창이 없는 프로세스의 길은 없다.
- 작업이 띄운 프로세스의 인자 해석과 첫째로 보내기. 앱의 몫이다(인자 넘기기의 둘째 순서).
- Android 런처 숏컷.

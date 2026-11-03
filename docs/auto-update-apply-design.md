# 자동 업데이트 적용

[자동 업데이트](auto-update-design.md)가 받아 확인한 새 판을 적용한다. Win32는 받아 둔 새 exe를 도우미 표식 인자로 띄우고 앱은 끝난다. 도우미는 옛 프로세스가 끝나기를 그 손잡이로 기다리고, 옛 exe를 옆에 남긴 채 새 exe를 같은 경로에 원자적으로 놓고, 같은 경로로 다시 띄우며, 새 판이 곧 쓰러지면 되돌린다. Android는 스토어 페이지를 연다. 공개 형식은 `updater.h`의 `updater::apply`·`updater::last_apply`, `update_apply_options`, `update_apply_result`(`update_apply_outcome`), `platform_update_apply_mode`, `run_update_helper`다. 결정 번호는 앞 문서에 잇는다.

## 문제

- 도는 exe를 덮지 못한다. `replace_file_atomically`의 `MoveFileExW(REPLACE_EXISTING)`([platform_services_win32.cpp](../src/win32/platform_services_win32.cpp) 168줄)는 실행 중인 이미지 위로는 실패한다고 알려져 있다. 바꾸는 쪽은 옛 프로세스 밖에 있어야 한다.
- 부모를 기다리는 길이 없다. `launch_program`은 손잡이를 물려주지 않고(416줄의 `FALSE`) 띄운 자식만 기다린다(426~432줄). PID만 넘기면 부모가 먼저 끝나 PID가 다시 쓰일 때 남의 프로세스를 기다린다.
- 경로를 지켜야 한다. 알림 활성화기의 명령 줄이 이 exe를 가리키고([notification_win32.cpp](../src/win32/notification_win32.cpp) 615~619줄), 점프 목록은 창을 세울 때 지금 exe 경로로 짓는다([win32_window.cpp](../src/win32/win32_window.cpp) 736줄, [점프 목록](jump-list-design.md) 108줄). 경로가 바뀌면 다음 실행이 고쳐 쓸 때까지 옛 경로를 가리킨다([OS 알림](os-notification-design.md) 112줄).
- 단일 인스턴스와의 차례가 없다. 뮤텍스 손잡이는 객체가 사는 동안 쥔다(platform_services_win32.cpp 204줄). 앱은 `wWinMain` 첫머리에 `single_instance`를 만들고 둘째면 인자를 넘기고 끝난다([demo_main.cpp](../examples/demo_main.cpp) 1536~1539줄). 옛 프로세스가 살아 있는 동안 새 exe가 평소 길을 타면 둘째가 되어 인자를 옛 앱에 넘기고 끝난다.
- 권한. Program Files처럼 사용자가 쓸 수 없는 폴더의 exe는 바꿀 수 없고 `launch_program`에는 권한 올리기 동사가 없다.
- Android는 프로세스를 띄우지 못한다([platform_services_android.cpp](../src/android/platform_services_android.cpp) 240~246줄). APK를 직접 설치하면 설치 확인의 답을 살아 있는 앱이 받아야 하는데 새 intent는 `onNewIntent`(Java)가 든다(OS 알림 166줄).

## 공개 형식

앞 문서의 `updater.h`에 함께 든다.

```cpp
namespace luil {
    struct update_apply_options
    {
        bool restart { true };                               // 거짓이면 바꾸기만 하고 띄우지 않는다 (앱이 끝날 때 적용). 지켜보지도 않는다
        std::vector<std::u8string> restart_arguments {};     // 다시 띄울 때 실행 파일 뒤에 붙일 인자다. 라이브러리는 더하지 않는다
        std::chrono::milliseconds parent_timeout { 60000 };  // 도우미가 앱의 종료를 기다리는 시한이다. 0보다 커야 한다
        std::chrono::milliseconds confirm_timeout { 30000 }; // 다시 띄운 새 판을 지켜보는 시간이다 (결정 9). 0이면 지켜보지 않는다
    };

    enum class update_apply_outcome { replaced, rolled_back, replace_failed, parent_timeout };

    // 지난 적용에서 도우미가 남긴 결과다. `updater::create`가 읽고 지운다.
    struct update_apply_result
    {
        update_apply_outcome outcome { update_apply_outcome::replaced };
        std::u8string from_version {};
        std::u8string to_version {};
        std::u8string error {}; // 영문 진단이다
    };

    enum class update_apply_mode { replace_executable, open_store };
    // 이 플랫폼의 적용 방식이다. Win32는 replace_executable, Android는 open_store다. 창이 없어도 답한다.
    [[nodiscard]] update_apply_mode platform_update_apply_mode() noexcept;

    // 도우미 실행이면 적용을 하고 종료 코드를 준다. 아니면 비었다. Android는 늘 비었다.
    //  - `wWinMain`에서 `command_line_arguments()` 바로 뒤, `single_instance`보다 먼저 부른다.
    //  - 종료 코드는 바꾸었으면 0, 바꾸지 못했거나 되돌렸으면 1, 표식은 있는데 인자가 틀리면 2다.
    [[nodiscard]] std::optional<int> run_update_helper(std::span<const std::u8string> arguments);

    class updater
    {
    public:
        // ... (앞 문서)
        [[nodiscard]] bool apply(const update_apply_options& options, std::u8string& error);
        [[nodiscard]] std::optional<update_apply_result> last_apply() const;
    };
}
```

앱의 `wWinMain` 첫머리는 이렇다.

```cpp
const auto arguments { luil::win32::command_line_arguments() };
if (arguments.has_value() == false)
    return 1;
if (const std::optional<int> helper { luil::run_update_helper(*arguments) }; helper.has_value())
    return *helper;
const luil::single_instance instance { u8"my-app" };
```

도우미의 명령 줄은 core가 짓고 읽으며 `win32::make_command_line`이 따옴표를 붙인다.

```text
"<받아 둔 새 exe>" -luil-update-apply 1 <옛 exe> <부모 손잡이 16진> <받기 폴더> <sha256> <from> <to> <parent ms> <confirm ms> <0|1> -- <다시 띄울 인자...>
```

비공개 파일은 다섯이다. `src/net/update_apply.h`는 두 플랫폼 묶음에 같이 드는 `updater.cpp`가 부르는 자리다(`hand_off_update`·`update_target_writable`·`update_target_path`, 앞 문서의 `update_crypto.h`와 같은 꼴). Win32는 `update_apply_win32.cpp`, Android는 `update_apply_android.cpp`가 구현한다. `src/win32/update_apply_win32.h`·`.cpp`는 앱 쪽 `launch_update_helper`, 도우미 `run_update_helper`, 쓰기 시험 `folder_writable`·`executable_folder_writable`이다. `src/win32/executable_path.h`·`.cpp`는 [jump_list.h](../src/win32/jump_list.h) 52줄의 `current_executable_path`를 옮긴 것이고 notification_win32.cpp 615~631줄의 같은 고리도 이것으로 바꾼다. `jump_list.h`는 `executable_path.h`를 include한다. 그래서 `jump_list.h`만 include하는 [win32_window.cpp](../src/win32/win32_window.cpp) 736줄과 [jump_list_tests.cpp](../tests/jump_list_tests.cpp) 451·487줄의 호출은 그대로 컴파일된다(같은 `luil::win32` 이름과 서명). 공개하지 않는다. 앱은 경로를 쓸 일이 없고 도우미는 같은 exe의 내부 함수를 부른다. `src/android/update_apply_android.cpp`는 스토어 열기다. 인자와 결과 파일의 짓기·읽기는 core의 `update_manifest`에 있다.

## 규칙

**앱 쪽 적용(Win32).** `apply`는 마지막 일이 `ready`로 끝났을 때만 받는다. 적용이 실패한 뒤(`hash_mismatch`·`not_writable`·`launch_failed`)에는 `check`나 `download`가 다시 `ready`를 세운 뒤 받는다. 시한이 규칙 밖이면(`parent_timeout` 0 이하, `confirm_timeout` 음수, Win32 대기 상한 넘음) 단계와 상관없이 거짓이다.

1. `applying`을 알리고 `update.lock`을 쥔다(못 쥐면 `busy`). 받아 둔 `<sha256>.exe`의 해시를 다시 맞추고 실행 파일 폴더의 쓰기 시험을 다시 한다. 어긋나면 `hash_mismatch`(그 파일을 지운다)·`not_writable`이다.
2. 자기 프로세스 손잡이를 `DuplicateHandle(SYNCHRONIZE, 상속 가능)`으로 짓고 `STARTUPINFOEXW`의 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`에 그 하나만 실어 `CreateProcessW(bInheritHandles = TRUE)`로 도우미를 띄운다(결정 8). 작업 폴더는 앱의 것이다. 띄운 뒤 `AllowSetForegroundWindow(도우미)`를 건다.
3. 띄우지 못하면 `launch_failed`다. 띄웠으면 `handed_off`이고 updater는 이 뒤로 일을 받지 않는다(`check`·`download`·`apply`가 거짓). `update.lock`은 updater가 없어질 때, 곧 프로세스가 끝날 때 풀린다. `stop()`도 놓지 않는다.
4. 앱은 저장을 마치고 `ui_command::application_exit`를 낸다. 라이브러리는 앱을 끝내지 않는다. `parent_timeout` 안에 끝나지 않으면 도우미가 아무것도 바꾸지 않고 `parent_timeout`을 남긴다.

끝날 때 적용하려면 앱이 닫힘(`ui_frame::close`)에서 `restart = false`로 `apply`를 부르고 `handed_off`를 받은 뒤 끝낸다.

**도우미(Win32).** `run_update_helper(arguments)`가 한다.

1. `arguments[1]`이 `-luil-update-apply`가 아니면 비었다(평소 시작). 표식은 있는데 나머지가 틀리면 아무것도 바꾸지 않고 2를 준다. 틀린 꼴은 꼴 번호가 1이 아님, 빈 칸, 손잡이(앞자리 0 없는 소문자 16진, 0이 아님)·sha256(소문자 16진 64자)·판(앞 문서 결정 3)·시한(앞자리 0 없는 십진, 부모 시한은 0보다 큼, `0xFFFFFFFE` 이하)·다시 띄우기(`0`·`1`)의 꼴, 열두째 칸이 `--`가 아님(칸이 모자라거나 남음)이다. `--` 뒤는 무엇이든 다시 띄울 인자다. 종료 코드는 바꾸었으면 0, 그 밖은 1이다.
2. 부모 손잡이를 `parent_timeout`까지 `WaitForSingleObject`로 기다린다. 넘으면 결과 `parent_timeout`을 쓰고(7의 방법) 다시 띄우지 않는다. 옛 앱이 살아 있어 띄우면 둘째가 된다.
3. `update.lock`을 쥔다. 부모가 쥐던 것은 그 프로세스가 끝날 때 풀렸다. 못 쥐면 250 ms마다 5초까지 다시 한다. 끝내 못 쥐면 바꾸지 않고 `replace_failed`로 7로 간다.
4. 자기 이미지(곧 받아 둔 새 exe)를 옛 exe 폴더의 `<이름>.<pid>.tmp`로 복사하고 디스크까지 내린 뒤 해시를 맞춘다.
5. 옛 exe를 `<이름>.old`로 복사하고 디스크까지 내린다. 옛 exe는 제자리에 그대로다. `<이름>`은 확장자까지 든 파일 이름이다(`app.exe.old`, `app.exe.<pid>.tmp`).
6. `MoveFileExW(tmp, 옛 exe, REPLACE_EXISTING | WRITE_THROUGH)`로 한 번에 바꾼다. 공유 위반·접근 거부·잠금 위반은 250 ms마다 10초까지 다시 한다(백신 검사가 잠깐 쥘 수 있다). 끝내 안 되면 tmp와 5의 `.old`를 지운다. 옛 exe는 그대로다.
7. **다시 띄우기 전에** 결과 파일 `<받기 폴더>/apply-result.json`을 `replace_file_atomically`로 쓰고 `update.lock`을 놓는다. 바꾸었으면 `replaced`(확인 대기), 못 바꾸었으면 `replace_failed`다. 그래서 다시 띄운 앱의 `create`가 늘 결과를 읽고 받기·적용이 `busy`가 되지 않는다.
8. `restart`면 옛 exe 경로로 `restart_arguments`를 붙여 띄우고 `AllowSetForegroundWindow`를 건다. 바꾸지 못했어도 옛 exe를 띄워 사용자가 앱을 잃지 않는다. 띄우지 못하면 같은 결과에 그 진단을 붙여 다시 쓴다. 바꾸었으면 결정 9대로 지켜본다(`confirm_timeout`이 0이면 지켜보지 않는다). 되돌리면 `.old`를 6과 같은 다시 하기로 옛 exe 자리에 옮기고, `update.lock`을 다시 쥐어(5초까지, 못 쥐어도 잇는다) 상태 파일에 건너뛸 판을 더하고, 옛 exe를 다시 띄우기 전에 결과 파일을 `rolled_back`으로 덮어 쓴다. `.old`를 옮기지 못하면 새 판이 그 자리에 남은 것이라 다시 띄우지 않고 결과를 `replaced`에 그 진단을 붙여 남긴다(다시 띄우면 또 쓰러진다). 그 뒤 끝난다.

어디서 꺼져도 옛 exe 자리는 옛 것 전체이거나 새 것 전체다. 바꾸는 것이 6의 이름 바꾸기 하나라서다. `ReplaceFileW`와 "옛 것을 먼저 비키기"는 쓰지 않는다. 앞의 것은 실패 갈래마다 남는 이름이 달라 되돌리기가 길고, 뒤의 것은 두 이름 바꾸기 사이에 꺼지면 exe가 없다. 남은 `.tmp`·`.old`와 받아 둔 exe는 결과를 이미 읽은 뒤의 `updater::create`, 곧 그다음 시작이 지운다.

**결과 전달.** 결과는 인자가 아니라 받기 폴더의 파일이다. 다시 띄운 앱의 인자는 앱이 준 그대로라 앱의 인자 읽기(데모의 `parse_arguments`는 모르는 인자를 거절한다)가 바뀌지 않는다. 다시 띄운 앱의 `updater::create`는 도우미가 띄우기 전에 써 둔 확인 대기 결과를 읽어 지우고 `last_apply()`로 내며, 같은 자리에서 지켜보는 도우미에게 `apply-confirmed`를 쓴다. 지우고 나서 쓴다. 그 사이에 쓰러지면 도우미가 확인을 보지 못해 되돌리는데 쓰러진 판이니 그것이 맞다. 이때 도우미가 아직 살아 있어 되돌리기에 쓸 `.old`와 자기 이미지인 받아 둔 exe는 지우지 않는다. 그 둘은 다음 시작의 `create`가 지운다(결정 13). 계약: **앱은 시작할 때 `updater`를 만든다.** 늦게 만들면 `confirm_timeout` 안에 확인이 서지 않아 결정 9가 잘못 되돌릴 수 있다. 받기 폴더는 판이 바뀌어도 같은 경로여야 한다.

- 결과 파일 `apply-result.json`은 `{"format":1,"outcome":"replaced","from":"1.0.0","to":"1.0.1","sha256":"<받아 둔 exe의 해시>","error":""}`이다. 읽지 못하면 지우고 없는 것으로 본다.
- 확인 표지 `apply-confirmed`의 내용은 적용한 exe의 sha256(16진 64자)이다. 지켜보는 도우미는 내용이 자기 sha256과 같을 때만 확인으로 본다. 그래서 지난 적용이 남긴 표지가 이번 적용을 확인하지 않는다. `create`는 `replaced`·`rolled_back`을 읽었을 때만 표지를 쓴다. `replace_failed`·`parent_timeout`은 지켜보는 도우미가 없고 받아 둔 exe를 다시 적용에 쓸 수 있어 남긴다.
- 결과 파일이 없고 표지가 있으면, 곧 결과를 이미 읽은 지난 시작이 있었으면, 표지가 가리키는 `<sha256>.exe`와 옛 exe 옆의 `<이름>.old`·`<이름>.<숫자>.tmp`를 지우고 표지도 지운다. 표지가 없으면 실행 파일 옆의 파일을 건드리지 않는다. 표지를 덮어 쓸 때 그 전 표지의 해시가 다르면 그 exe를 먼저 지운다.
- Android는 도우미가 없어 결과 파일을 읽지 않고 `last_apply()`는 늘 비었다.

**단일 인스턴스와의 약속(G7).**

- `run_update_helper`는 `single_instance`, `send_to_primary` 갈래, `notification_launch` 갈래보다 먼저다.
- 도우미는 `single_instance`를 만들지 않고 `send_to_primary`를 부르지 않는다. 창, 앱 식별의 레지스트리, COM 활성화기를 세우지 않는다. 모두 `run_application_window` 안의 일이라 도우미는 거기 닿기 전에 끝난다.
- 다시 띄우기는 부모 손잡이가 신호 상태가 된 뒤다. 프로세스가 끝나야 신호가 서고 그때 OS가 그 프로세스의 손잡이를 모두 닫았다고 알려져 있다. 뮤텍스가 풀렸으니 다시 띄운 앱은 첫째다. 그래서 updater는 `send_to_primary`를 쓰지 않는다.
- 같은 세션에 같은 exe의 다른 인스턴스가 남아 있으면 그 이미지가 쓰이는 중이라 6이 실패한다. 다시 띄운 옛 exe는 둘째로 앱의 평소 길(`send_to_primary`)을 탄다.
- 표식 `-luil-update-apply`는 알림의 `-luil-notification`·`-Embedding`과 겹치지 않는다. COM과 점프 목록은 도우미를 띄우지 않는다.

**경로.** 옛 exe 경로를 그대로 지켜 알림 활성화기의 명령 줄, 점프 목록, 바로가기, 앱 식별이 고칠 것 없이 새 판을 가리킨다.

**Android.** `apply`는 마지막 확인이 판을 내주었을 때(`available`) 받는다. `open_url(store_url, { store_schemes })`를 부르고 `ok`면 `handed_off`, 열 앱이 없거나 막히면 `launch_failed`다. 앞에 보이지 않는 앱이 띄운 Activity는 OS가 조용히 막으므로([플랫폼 도구](platform-services-design.md) 91줄) 사용자가 단추를 누른 때 부른다. 스토어를 연 뒤에도 앱은 그대로 돌아 `check`·`apply`를 다시 받는다(결정 14). `run_update_helper`와 `last_apply`는 늘 비었다.

**화면이 없다.** 도우미는 창도 대화 상자도 없다. 실패는 결과 파일로 남아 다음 실행의 앱이 자기 글로 보인다. `default_text`에 더하지 않는다.

## 결정

- 확정(2026-11-03): N4-apply 길 b, N4-android 길 b. 근거는 [앞 문서](auto-update-design.md)의 결정 절에 있다.

**7. 바꾸는 범위.** 질문: 실행 파일 하나만 바꾸는가, 앱 폴더도 바꾸는가.

- 길 a: exe 하나. 되는 것: 매니페스트 칸이 플랫폼마다 파일 하나다. 원자적 바꾸기가 이름 바꾸기 하나로 선다. luil 앱은 정적 링크라 대개 exe 하나로 돈다. 대가: 옆의 DLL·assets는 바뀌지 않는다. 그것이 판마다 바뀌는 앱은 exe에 싣거나 따로 받는다(데모의 `assets` 폴더가 그 예다).
- 길 b: zip 꾸러미를 받아 폴더를 통째로 바꾼다. 되는 것: 무엇이든 바뀐다. 대가: 압축 풀기 코드가 없어 들여야 한다. 폴더를 한 번에 바꾸는 원자적 길이 없어 중간 상태가 생긴다.
- 길 c: 매니페스트에 파일 목록(경로·크기·해시)을 두고 파일마다 바꾼다. 되는 것: 바뀐 파일만 받는다. 대가: 바꾸기가 파일 수만큼이라 중간에 꺼지면 판이 섞인다. 경로 넘어가기(`..`)를 막는 규칙이 든다.
- 권장: a. 확정한 N4-apply 길 b가 "실행 파일 교체"라 그 범위를 그대로 쓴다. c는 매니페스트 `format`을 올려 더할 수 있다. 대가: 옆 파일. **권장대로 진행.**

**8. 부모 종료를 기다리는 길.** 질문: 도우미가 옛 프로세스의 끝을 무엇으로 아는가.

- 길 a: 상속 손잡이. 앱이 자기 손잡이를 상속 가능하게 복제해 손잡이 목록 속성으로 그 하나만 넘기고 값을 인자에 싣는다. 되는 것: PID가 다시 쓰여도 엉뚱한 프로세스를 기다리지 않는다. 부모가 이미 끝났어도 손잡이는 신호 상태로 남는다. 다른 상속 가능 손잡이가 새지 않는다. 대가: `launch_program`을 쓰지 못하고 `STARTUPINFOEXW` 길을 새로 둔다.
- 길 b: PID를 넘기고 도우미가 `OpenProcess(SYNCHRONIZE)`로 연다. 되는 것: `launch_program` 그대로다. 대가: 도우미가 열기 전에 부모가 끝나 PID가 다시 쓰이면 남의 프로세스를 시한까지 기다린다.
- 길 c: 단일 인스턴스 뮤텍스가 풀리기를 기다린다. 되는 것: G7 차례와 바로 맞는다. 대가: `single_instance`를 쓰지 않는 앱은 못 쓴다. 뮤텍스는 `wWinMain`의 지역이 풀릴 때 놓여 프로세스가 아직 exe를 쥔 채일 수 있다.
- 권장: a. 대가: 새 띄우기 길 하나. **권장대로 진행.**

**9. 새 판이 뜨지 못할 때.** 질문: 바꾼 판이 시작하자마자 쓰러지면 어떻게 하는가.

- 길 a: 도우미가 다시 띄운 프로세스를 `confirm_timeout` 동안 지켜본다. `apply-confirmed`가 서거나(새 판의 `updater::create`), 종료 코드 0으로 끝나거나, 시간이 지나면 성공이다. 확인 전에 0이 아닌 코드로 끝나면 `.old`를 `MoveFileExW`로 되돌리고 옛 exe를 다시 띄우며 `rolled_back`과 그 판을 상태 파일의 건너뛸 판으로 남긴다. 그 판은 더 새 판이 나올 때까지 `available`이 되지 않는다. 되는 것: 쓰러지는 판이 사용자 PC에 남지 않고 같은 판을 되풀이해 받지 않는다. 대가: 도우미가 30초 더 산다. `updater`를 늦게 만들면서 다른 까닭으로 곧 0이 아닌 코드로 끝나는 앱은 잘못 되돌려질 수 있다.
- 길 b: 되돌리지 않고 `.old`만 남긴다. 되는 것: 도우미가 짧다. 대가: 쓰러지는 판은 사람이 손으로 되돌린다.
- 길 c: `updater::rollback()`을 공개하고 새 판의 앱이 문제를 알면 부른다. 되는 것: 앱이 고른다. 대가: 시작 전에 쓰러지는 판은 부를 기회가 없다.
- 권장: a. 대가: 위의 잘못 되돌림이다. 그래서 "앱은 시작할 때 `updater`를 만든다"를 계약으로 둔다(결과 전달). `restart = false`는 띄우지 않아 지켜보지 않는다. 사용자 PC에서 보이는 갱신 정책이라 **승인: 길 a (2026-11-03).**
- 건너뛸 판은 상태 파일의 `skipped_version` 하나다. 확인은 그 판보다 크지 않은 판을 `up_to_date`로 본다(결정 15).

**10. 쓸 수 없는 설치 자리.** 질문: 실행 파일 폴더에 쓸 수 없으면 어떻게 하는가.

- 길 a: 확인 때 실행 파일 폴더에 `CREATE_NEW | FILE_FLAG_DELETE_ON_CLOSE`로 시험 파일을 만들어 보고 막혔으면 `not_writable`로 알린다. 앱은 `notes_url`이나 설치기 주소를 `open_url`로 연다. 되는 것: 받기 전에 안다. 권한 올리기가 없다. 대가: Program Files 설치는 이 길로 갱신하지 못한다.
- 길 b: 도우미를 `ShellExecuteExW`의 `runas`로 올려 띄운다. 되는 것: Program Files도 된다. 대가: UAC 창이 뜬다. 관리자 도우미가 띄운 앱도 관리자로 떠 권한을 내리는 길(셸의 토큰을 빌리는 것)이 따로 든다. 관리자 권한 첫째의 UIPI([단일 인스턴스](single-instance-args-design.md) 결정 6)와도 닿는다.
- 길 c: 미리 보지 않고 적용 때 실패로 알린다. 되는 것: 시험 파일이 없다. 대가: 수백 MiB를 받은 뒤에야 안 된다고 안다.
- 권장: a. 확정한 N4-apply 길 b의 대가("사용자 폴더 설치만 된다")와 같다. **권장대로 진행.**

**13. 확인 표지와 남은 파일을 지우는 조건.** 질문: 다음 시작의 `create`가 `.old`·`.tmp`·받아 둔 exe를 무엇을 보고 지우는가. 구현 중 정했다.

- 길 a: 빈 표지 하나를 두고, 있으면 받기 폴더의 `<16진 64자>.exe`를 모두 지운다. 되는 것: 표지가 짧다. 대가: 같은 시작에 받아 두고 적용하지 않은 더 새 판의 exe도 지워 다시 받는다. 지난 적용의 빈 표지가 이번 적용을 확인으로 잘못 세운다.
- 길 b: 표지 내용을 적용한 exe의 sha256으로 둔다. 도우미는 내용이 자기 것과 같을 때만 확인으로 보고, 다음 시작은 그 해시의 exe만 지운다. 실행 파일 옆의 `.old`·`.tmp`는 표지가 있을 때만 지운다. 되는 것: 지난 적용의 표지와 섞이지 않고 이 라이브러리가 남긴 것이 확실한 파일만 지운다. 대가: 표지가 없는 채 끊긴 도우미의 `.tmp`는 다음 적용의 표지가 설 때까지 남는다.
- 길 c: 지울 것을 상태 파일에 적는다. 되는 것: 파일이 하나 준다. 대가: 상태 파일을 앱(sequence)과 도우미(건너뛸 판)에 더해 `create`도 고쳐 쓰게 되어 셋이 엇갈린다.
- 정함: b. **구현 중 정함(2026-11-03).**

**14. Android에서 스토어를 연 뒤.** 질문: `handed_off` 뒤에 updater가 일을 더 받는가. 구현 중 정했다.

- 길 a: Win32처럼 받지 않는다. 되는 것: 두 플랫폼의 계약이 같다. 대가: 스토어에서 설치하지 않고 돌아온 사용자에게 앱이 다시 권할 길이 updater를 새로 만드는 것뿐이다.
- 길 b: 받는다. Win32만 도우미를 띄운 뒤 막는다. 되는 것: Android 앱은 스토어를 연 뒤에도 그대로 돌아 다시 확인하고 다시 연다. 대가: `handed_off`의 뜻이 플랫폼마다 조금 다르다(Win32는 "끝나라", Android는 "넘겼다").
- 정함: b. Win32의 막음은 앱이 곧 끝나고 도우미가 받기 폴더를 쓸 것이라서다. Android에는 그 까닭이 없다. **구현 중 정함(2026-11-03).**

**15. 건너뛸 판의 범위.** 질문: 되돌린 판을 무엇과 견주어 내주지 않는가. 구현 중 정했다.

- 길 a: 같은 판만 내주지 않는다. 되는 것: 서버가 판을 낮춰 다시 내면 받는다. 대가: 결정 9의 "더 새 판이 나올 때까지"와 어긋난다.
- 길 b: 그 판보다 크지 않은 판을 내주지 않는다. 되는 것: 결정 9의 글 그대로다. 쓰러진 판과 그보다 낮은 중간 판으로 다시 가지 않는다. 대가: 서버가 낮은 판으로 되돌려 내도 그 PC는 지금 판에 머문다.
- 정함: b. **구현 중 정함(2026-11-03).**

## 플랫폼

### Win32

- 앱 쪽과 도우미 모두 `src/win32/update_apply_win32.cpp`다. 부르는 것은 `DuplicateHandle`, `InitializeProcThreadAttributeList`·`UpdateProcThreadAttribute`, `CreateProcessW(EXTENDED_STARTUPINFO_PRESENT)`, `WaitForSingleObject`·`GetExitCodeProcess`, `CopyFileExW`·`FlushFileBuffers`, `MoveFileExW`, `AllowSetForegroundWindow`다. kernel32·user32라 새로 링크할 것이 없다.
- `win32_window.cpp`는 고치지 않는다. 도우미는 `run_application_window` 전에 끝나고 앱의 종료는 기존 `application_exit`다. `ui_platform_capabilities`에도 칸을 더하지 않는다. 적용 방식은 창과 무관해 `platform_update_apply_mode`가 답한다.
- 관리자 권한으로 도는 앱의 도우미는 그 권한을 물려받고 다시 띄운 앱도 같은 권한이다.
- 프로세스는 `CreateProcessW`에 실행 파일 경로를 따로 주어 띄운다. PATH나 작업 폴더의 같은 이름을 띄우지 않는다. `CREATE_BREAKAWAY_FROM_JOB`은 쓰지 않아 도우미와 다시 띄운 앱은 앱의 Job에 함께 든다(test가 Job으로 거둔다). 앱이 닫힐 때 Job을 끝내는 실행기 안에서 돌면 도우미도 함께 끝날 수 있다고 본다(추측). 사람 확인이다.
- WinHTTP로 받은 파일에는 인터넷 출처 표식(`Zone.Identifier`)이 붙지 않는다고 알려져 있다. 백신과 SmartScreen이 사용자 폴더의 새 exe와 바꾼 exe를 어떻게 다루는지는 사람 확인이다.
- 받기 폴더의 결과 파일은 다시 띄운 앱의 `create`가 읽고 지운다. `.old`·`.tmp`와 받아 둔 exe는 도우미가 지켜보는 동안 쓰일 수 있어 다음 시작의 `create`가 지운다.

### Android

- `src/android/update_apply_android.cpp`는 `open_url`을 부르는 것이 전부다. Java도 새 JNI도 없다. 설치와 그 결과는 스토어의 몫이라 앱은 모른다.
- PackageInstaller 세션으로 APK를 직접 설치하는 길은 unsupported다. 설치 확인의 답이 살아 있는 앱에 새 intent로 온다고 보는데(추측) 그것을 받으려면 `onNewIntent`를 재정의한 Activity 하위 클래스(Java)가 든다([Java 0줄 방침](android-port-plan.md) 576줄).

## 다른 노드와의 약속

- **단일 인스턴스(R2 G7).** 위 규칙이다. [단일 인스턴스](single-instance-args-design.md) 185줄의 "새 프로세스가 첫째라 이 길을 쓰지 않을 수 있다(추측)"를 이 문서가 닫는다. 쓰지 않는다. 구현 커밋이 그 줄을 이 문서로 잇는 말로 고친다.
- **os_notify·점프 목록(R2).** 경로를 지켜 레지스트리와 셸 링크를 고치지 않는다. `current_executable_path`를 `src/win32/executable_path.h`로 옮기고 `jump_list.h`·`.cpp`와 `notification_win32.cpp`가 그것을 쓴다(같은 줄 파일, 동작 그대로). `jump_list.h`가 `executable_path.h`를 include해 `win32_window.cpp` 736줄과 `tests/jump_list_tests.cpp` 451·487줄의 호출은 고치지 않는다.
- **데모.** `examples/demo_main.cpp`의 `wWinMain`에서 인자 읽기(1530~1532줄) 뒤, `single_instance`(1536줄) 앞에 `run_update_helper` 갈래 하나다. 데모 연결 커밋에 넣고 통합 때 직렬로 쌓는다.
- **줄 1·2·3.** 같은 파일이 없다. 컨트롤러, `window_surface.cpp`·`surface_input.cpp`, `win32_window.cpp`를 고치지 않는다.

## 확인

**이 PC 흔적.** 이 문서의 test가 남기는 것이다.

| 무엇 | 자리 | 지우는 법 |
| --- | --- | --- |
| port | 끝까지 도는 test 하나만 되돌이 127.0.0.1 임의 port를 쓴다(앞 문서와 같다) | 프로세스가 끝나면 닫힌다 |
| 임시 폴더 | `%TEMP%\luil-update-apply-<pid>-<n>\`(옛·새 exe 사본, `.tmp`·`.old`, 받기 폴더, 결과 파일, probe의 시작 기록, 쓰기를 ACL로 막은 `locked` 폴더) | test가 끝에 ACL을 되돌린 뒤 지운다. 끊긴 test는 `icacls <폴더> /reset /t` 뒤 `%TEMP%\luil-update-apply-*`를 지운다 |
| 띄우는 프로세스 | `luil_update_probe.exe` 사본(옛 앱, 도우미, 다시 띄운 앱). test가 Job 개체(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)에 넣어 띄워 그 자식도 같은 Job에 든다 | test가 Job을 닫으면 모두 끝난다. 남았으면 `taskkill /im luil_update_probe.exe`다 |
| 뮤텍스 | probe의 `Local\luil-instance:luil-update-probe-<test pid>` | 프로세스가 끝나면 풀린다 |
| 레지스트리 | 없다. probe는 앱 식별을 세우지 않고 도우미는 레지스트리를 쓰지 않는다 | |
| 데모 | 적용하지 않는다 | |

- `tests/update_manifest_tests.cpp`(core, `luil_core_tests`)에 더한다: 도우미 인자 짓기·읽기 왕복(빈칸·따옴표·한글 경로, 다시 띄울 인자 없음과 빈 인자, `--` 뒤의 `-luil-update-apply`), 틀린 판 번호·빈 칸·남는 칸의 거절, 결과 파일과 건너뛸 판의 왕복.
**test 프로세스는 적용의 대상이 되지 않는다.** `apply`의 옛 exe는 부른 프로세스의 실행 파일이다. `luil_tests` 안에서 실행 파일을 받아 적용하면 도우미가 `luil_tests.exe`를 바꾼다. 그래서 끝까지 도는 적용은 늘 임시 폴더의 probe 사본이 하고, `luil_tests`는 사본을 놓고 띄우고 되읽기만 한다. `luil_tests` 안의 `apply`는 띄우기 전에 멈추는 갈래(거절, `hash_mismatch`)와, 받은 것이 실행 파일이 아닌 고정 asset이라 `CreateProcessW`가 거절하는 `launch_failed`만 돈다.

- `luil_update_probe`(새 시험 실행 파일, `tests/update_probe_main.cpp`, `luil`을 링크하고 `luil_tests` 옆에 놓인다): `wWinMain`이 `run_update_helper`를 먼저 부른다. 평소 시작이면 `single_instance`를 만든 뒤 자기 파일 끝의 판 표지(test가 사본 끝에 붙인 `luil-probe-version:2`. PE 뒤에 덧붙인 바이트는 실행에 닿지 않는다고 알려져 있다)와 인자, 첫째 여부를 시작 기록에 쓰고, 고정 키로 `updater`를 만들어 `last_apply`와 `update.lock`을 잡을 수 있는지를 적는다. `--apply <sha256> <from> <to>`면 비공개 `launch_update_helper`로 받기 폴더의 `<sha256>.exe`를 도우미로 띄운다. `--update <주소>`면 그 `updater`로 확인·받기·적용을 돌고 넘긴 뒤의 `check`가 거짓인지 적는다. `--no-restart`·`--parent-timeout`·`--confirm-timeout`이 적용의 선택이고 `--linger <ms>`는 띄운 뒤 끝나기 전에 쉰다. `--exit-if-version <판> <코드>`면 그 판일 때 `updater`를 만들기 전에 그 코드로 끝난다(확인 전에 쓰러지는 판). `--window`면 판을 label로 그리는 창을 띄운다(사람 확인용). 다시 띄울 인자는 자기 인자에서 적용의 것만 뺀 것이다.
- `tests/update_apply_tests.cpp`(새 파일, `luil_tests`): 옛 probe에서 도우미를 거쳐 다시 띄우기까지 옛 경로의 바이트가 새 사본과 같은지, 다시 띄운 probe가 첫째이고 인자(빈칸·빈 글·따옴표·끝 백슬래시·한글·`--` 뒤의 `-luil-update-apply`)가 준 그대로이며 그 probe의 `last_apply`가 `replaced`이고 받기가 `busy`가 아닌지(`update.lock`을 쥔다), 도우미의 시작 기록이 없는지(평소 길을 타지 않았다), `.old`와 받아 둔 exe가 남고 그다음 시작이 그 둘과 표지를 지우는지. 앱 폴더 이름에 빈칸과 한글이 든다. `restart = false`면 띄우지 않고 결과가 남는다. 부모가 시한보다 늦게 끝나면 `parent_timeout`이고 바뀐 것도 띄운 것도 없다. test가 옛 exe를 읽기 공유만 두고 열어 두면(실행과 `.old` 복사는 되고 바꾸기는 공유 위반이다) 다시 하기 뒤 `replace_failed`이고 옛 exe가 다시 뜨며 `.old`·`.tmp`가 남지 않는다. 새 판이 확인 전에 3으로 끝나면 `rolled_back`이고 옛 바이트로 돌아오며 건너뛸 판이 적히고 `sequence`는 그대로다. 0으로 끝나면 `replaced`다. 쓰기를 ACL로 막은 임시 폴더는 쓰기 시험이 거짓이다. 틀린 도우미 인자는 2이고 아무것도 바뀌지 않는다. 표식 없는 인자는 비었다. 한 test는 되돌이 서버와 probe의 `updater`로 확인·받기·적용을 끝까지 돈다.
- `tests/updater_tests.cpp`(앞 문서)에 더한다: 받을 것이 없는 `apply`와 시한이 규칙 밖인 `apply`는 거짓, 건너뛸 판은 `up_to_date`이고 `sequence`를 적어도 남는지(두 플랫폼), `create`가 확인 대기 결과 파일을 읽어 지우고 `last_apply`를 내며 `apply-confirmed`를 쓰되 받아 둔 exe는 남기는지, 결과를 이미 읽은 다음 `create`가 그것과 표지를 지우는지, `replace_failed` 결과는 표지 없이 받은 파일을 남기는지, 읽지 못하는 결과 파일, 확인 뒤에 바뀐 받은 파일의 `hash_mismatch`, 실행 파일이 아닌 받은 파일의 `launch_failed`(Win32). `handed_off` 뒤 `check`가 거짓인지와 옛 exe 옆 `.old`의 지움은 probe의 기록으로 `update_apply_tests`가 본다(이 test 프로세스는 넘기지 않고, 실행 파일 옆 파일을 만들지 않는다).
- 숨은 `[.update-live]`(`luil_tests`)가 `--window` probe로 같은 흐름을 사람 앞에서 돈다. 옛 판의 창을 닫으면 새 판의 창이 뜬다. ctest에서는 돌리지 않는다.
- 데모는 적용하지 않는다. 빌드 폴더의 데모 실행 파일을 덮지 않기 위해서다. 데모 네트워크 페이지에 label `업데이트 적용 방식: 실행 파일 바꾸기`, 모바일 데모 `device_page.cpp`에 `업데이트 적용 방식: 스토어 열기`를 둔다. 둘 다 `platform_update_apply_mode()`의 답을 글로 옮긴다. `tests/network_page_tests.cpp`(`luil_tests`)와 `tests/mobile_demo_page_tests.cpp`(core, 답을 대역으로 채운다)가 이름을 읽는다. 실행 중 데모의 확인은 격리 데스크톱에서 luil-uia로 하고, 실제 입력이 필요하면 먼저 요청해 허락한 시간만 쓴다.
- 사람 확인: `[.update-live]`로 다시 띄운 창이 앞에 서는지, 앱이 닫힐 때 Job을 끝내는 실행기 안에서 도우미가 살아남는지, 작업 표시줄 단추와 고정한 바로가기가 새 판을 가리키는지, 알림을 눌러 새 판이 뜨는지, 백신·SmartScreen이 사용자 폴더의 새 exe와 바꾼 exe를 어떻게 다루는지(서명한 빌드와 안 한 빌드), Program Files 설치의 `not_writable` 흐름, 적용 중 도우미를 강제로 끝낸 뒤에도 앱이 뜨는지, Android 실기기에서 스토어 주소가 스토어 앱으로 열리는지.

## 범위 밖

- 폴더·DLL·assets 바꾸기(결정 7 b·c), 권한 올리기(결정 10 b), 설치기 실행(N4-apply 길 a).
- 판 폴더와 실행기(N4-apply 길 c), 차등 갱신.
- 다른 세션·다른 사용자의 같은 앱 인스턴스를 닫게 하는 일.
- Android의 APK 직접 설치와 Play의 앱 안 갱신(Play Core 라이브러리 의존이 든다).

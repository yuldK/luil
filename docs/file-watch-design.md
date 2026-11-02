# 파일 변경 감시

폴더 하나, 그 아래 전부, 또는 파일 하나의 변경을 OS 알림으로 받아 앱에 묶음으로 건넨다. Windows는 `ReadDirectoryChangesW`, Android는 NDK의 inotify다. 공개 형식은 새 헤더 `include/luil/app/file_watcher.h`의 `file_watcher`·`file_watcher_config`·`file_change_batch`다. thread 수명과 전달은 [`http_client`](http-client-design.md)와 같은 계약이다.

## 문제

- 저장소 어디에도 감시 원시가 없다. `src/`에 `ReadDirectoryChangesW`도 inotify도 없다. 앱이 직접 thread를 세우고, OS 호출을 걸고, 막힌 기다림을 풀 정지 길을 따로 짓는다.
- 결과를 나르는 길은 `app_host::post_app_message`다. app inbox는 `reject_newest`라 포화하면 조용히 버린다([app_host.h](../include/luil/app/app_host.h), [messaging.md](concepts/messaging.md)). 원 이벤트를 하나씩 메시지로 만들면 폭주 한 번에 알림이 사라지고, 앱은 사라진 것도 모른다.
- 같은 파일의 저장 한 번이 이벤트 여럿이다. luil의 `replace_file_atomically`부터 `<이름>.<pid>.tmp`에 쓰고 이름을 바꾼다([platform-services-design.md](platform-services-design.md)). 합치는 규칙이 없으면 앱은 임시 파일을 읽으려 하거나 같은 파일을 여러 번 다시 읽는다.
- 두 OS의 뜻이 다르다. Windows는 이름 바꿈을 옛 이름·새 이름 두 항목으로, inotify는 cookie로 짝지은 두 이벤트로 준다. inotify는 하위 폴더를 따라가지 않는다. 넘침은 Windows가 빈 결과로, inotify가 `IN_Q_OVERFLOW`로 알린다. 앱마다 이 차이를 다시 맞춘다.
- 정지가 종료 순서에 끼지 않는다. 감시 thread가 `deliver` 도중에 앱 자원이 부서지면 그 자리가 곧 use-after-free다.

## 공개 형식

`include/luil/app/file_watcher.h`, namespace `luil`. [platform_services.h](../include/luil/app/platform_services.h) 옆이다. `include/luil/app`은 core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake)) OS 헤더를 들이지 않는다.

```cpp
namespace luil {
    // 0은 "받아들이지 않았다"이다 (http_ticket과 같다).
    struct file_watch_ticket
    {
        std::uint64_t value { 0 };
        [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
        [[nodiscard]] bool operator==(const file_watch_ticket&) const noexcept = default;
    };

    enum class file_watch_scope
    {
        folder,  // 그 폴더의 바로 아래 항목
        subtree, // 그 아래 전부
        file,    // 파일 하나 (부모 폴더를 보고 이름으로 거른다)
    };

    enum class file_change_kind { created, removed, modified, renamed };

    struct file_change
    {
        file_change_kind kind { file_change_kind::modified };
        // 감시 경로 기준 상대 경로다. 구분자는 두 플랫폼 모두 '/'다.
        // file 감시는 부모 폴더 기준이라 그 파일의 이름이다.
        std::u8string path {};
        // renamed의 옛 이름이다. 나머지는 비었다.
        std::u8string old_path {};
    };

    enum class file_watch_end
    {
        none,    // 감시가 이어진다
        removed, // 앱이 remove했다
        lost,    // 대상이 사라졌거나 OS가 감시를 끊었다. error가 이유다
    };

    struct file_change_batch
    {
        file_watch_ticket ticket {};
        std::vector<file_change> changes {};
        // 한 경로는 묶음 안에서 한 번만 나온다 (path로든 old_path로든).
        // 넘쳤다. changes는 일부이거나 비었고, 앱은 그 범위를 다시 훑어야 한다.
        bool rescan { false };
        file_watch_end end { file_watch_end::none };
        std::u8string error {};
    };

    struct file_watcher_config
    {
        // 감시 thread에서 불린다. 받았으면 참이다 (결정 1). 비어 있으면 받은 것으로 본다.
        std::function<bool(file_change_batch)> deliver {};
        // 마지막 변경 뒤 이만큼 조용하면 묶음을 낸다.
        std::chrono::milliseconds settle { 100 };
        // 변경이 그치지 않아도 첫 변경 뒤 이만큼이면 낸다.
        std::chrono::milliseconds max_delay { 1000 };
        // 감시 하나의 묶음에 담을 상한이다. 넘으면 비우고 rescan이다.
        std::size_t max_changes { 4096 };
        std::chrono::milliseconds stop_budget { 1000 };
    };

    class file_watcher
    {
    public:
        ~file_watcher(); // stop()을 부른다
        [[nodiscard]] static std::unique_ptr<file_watcher> create(file_watcher_config configuration, std::u8string& error);
        // 경로가 없거나(file이면 부모 폴더가 없거나) OS가 거절하면 빈 표이고 error에 적는다.
        [[nodiscard]] file_watch_ticket add(std::u8string_view path, file_watch_scope scope, std::u8string& error);
        // 모르는 표·끝난 표는 아무 일도 하지 않는다.
        void remove(file_watch_ticket ticket) noexcept;
        void stop() noexcept;
    private:
        struct engine;
        std::unique_ptr<engine> engine_ {};
    };
}
```

헤더는 이 위에 복사·이동 금지와 비공개 생성자(`http_client`와 같다), test가 쓰는 `file_change::operator==`를 더한다.

비공개 파일은 셋에 test 손잡이 header 하나다. 합치기·짝짓기·디바운스·넘침·거절된 묶음의 재전달은 OS를 모르는 순수 로직이라 core의 `src/host/file_watch_batch.h`·`.cpp`에 둔다(`file_watch_batch`와 `offer_file_change_batch`). OS 호출은 `src/win32/file_watcher_win32.cpp`와 `src/android/file_watcher_android.cpp`에 둔다. Windows의 작은 버퍼 test 손잡이 `win32::set_file_watch_buffer_bytes_for_tests`만 `src/win32/file_watcher_win32.h`에 따로 둔다.

## 규칙

**thread와 전달.** 감시자는 thread 하나를 소유한다. [threading-model.md](concepts/threading-model.md)는 일반 worker를 앱 정책이라 했고 그 판단은 그대로다. 이것은 `http_client`처럼 OS API를 옳게 쓰는 데 필요한 부품이다. OS 알림은 막히는 기다림으로 오고, logic thread는 막힐 수 없다.

- 그 thread가 OS를 기다리고, 묶음을 짓고, `deliver`를 차례로 부른다. 앱은 `deliver`에서 `post_app_message_ticketed`로 넘기고 그 표가 0이 아닌지를 답한다.
- `deliver` 안에서 `add`·`remove`는 되고 `stop`은 안 된다. 자기 자신을 join한다. debug 빌드가 assert로 잡는다. 던지면 삼키고 거절로 본다.
- 묶음은 감시 표 하나에 하나다. 한 번에 여러 감시의 묶음이 나가면 표마다 따로 부른다.
- `add`를 다른 thread에서 부르면 감시 thread가 OS 감시를 걸 때까지 기다린다. 돌아온 뒤의 변경은 놓치지 않는다. `remove`는 기다리지 않고 감시 thread에 넘긴다.

**끝맺음.** 받은 표마다 `end != none`인 묶음이 정확히 하나 온다. `stop`이 먼저 오면 오지 않는다.

- `remove`는 그 표의 남은 변경을 담은 마지막 묶음(`removed`)을 낸다. 앱 상태 기계는 "표를 받았으면 끝 묶음을 기다린다" 한 줄이다. `http_client`의 `cancel`이 답을 지우지 않는 것과 같은 이유다.
- 감시 폴더(file이면 부모 폴더)가 지워지거나 옮겨지거나 OS가 감시를 끊으면 `lost`다. 그 뒤 그 표에는 묶음이 없다.

**묶음 짓기.** 원 이벤트는 감시마다 모은다. 마지막 변경 뒤 `settle`이 지나거나 첫 변경 뒤 `max_delay`가 지나면 낸다. 같은 경로는 한 항목으로 합친다.

| 앞 | 뒤 | 결과 |
| --- | --- | --- |
| created | modified | created |
| created | removed | 없음 |
| modified | modified | modified |
| modified | removed | removed |
| removed | created | modified |
| created | created | created |
| created(A) | renamed A→B | created(B) |
| modified(A) | renamed A→B | renamed A→B |
| removed(B) | renamed A→B | renamed A→B |
| renamed A→B | modified(B) | renamed A→B |
| renamed A→B | renamed B→C | renamed A→C |
| renamed A→B | removed(B) | removed(A) |
| renamed A→B | created(A) | created(B), modified(A) |

- 묶음은 묶기 시작한 때와 지금 사이의 순 변화다. 한 경로는 묶음 안에서 한 번만 나온다(`path`로든 `old_path`로든). 그래서 마지막 줄(편집기가 원본을 백업 이름으로 옮기고 새 원본을 쓰는 저장)은 이름 바꿈이 아니라 두 항목으로 풀린다. 원본은 내용이 바뀌었고 백업 이름은 새로 생겼다. 제자리로 돌아온 이름 바꿈(A→B 뒤 B→A)은 modified(A)다.
- 이름 바꿈의 짝은 Windows가 이어 오는 `FILE_ACTION_RENAMED_OLD_NAME`·`NEW_NAME`, inotify가 같은 cookie의 `IN_MOVED_FROM`·`IN_MOVED_TO`다. 묶음을 낼 때까지 짝이 없으면 옛 이름만은 removed, 새 이름만은 created다. 감시 밖으로 나가고 들어온 것이 이 길이다. 반쪽 사이에 다른 이벤트가 끼어도 짝이 아니다. 반쪽이 두 번의 읽기에 걸쳐 와도 된다.
- `file` 감시는 부모 폴더의 변경을 다 합친 **뒤에** 이름으로 거른다. 임시 이름의 created가 있어야 이름 바꾼 저장이 그 파일의 항목 하나가 된다. 그래서 `max_changes`는 부모 폴더 전체의 항목 수로 센다. 거른 뒤 남는 것이 없으면 묶음을 내지 않는다.
- `deliver`가 거짓이면 그 묶음을 버리지 않는다. 다음 묶음에 합쳐 `settle` 뒤에 다시 낸다. 합친 것이 `max_changes`를 넘으면 비우고 rescan이다. 끝 묶음도 같은 길로 다시 낸다.
- OS 넘침(Windows의 빈 결과나 `ERROR_NOTIFY_ENUM_DIR`, inotify의 `IN_Q_OVERFLOW`)은 그 감시의 rescan이다. 감시는 다시 걸린다. inotify의 큐는 감시자 하나에 하나라 어느 감시의 것이 빠졌는지 모른다. 그 감시자의 모든 감시에 rescan을 단다.

**종류의 뜻.** 두 플랫폼이 같은 뜻이 되게 맞춘다.

- `modified`는 "내용이 바뀌었을 수 있다"이다. 쓰기가 끝났다는 뜻이 아니다. Windows에는 닫힘 알림이 없다. Android는 `IN_MODIFY`와 `IN_CLOSE_WRITE`를 모두 받아 `modified` 하나로 합친다. `settle`이 유일한 완료 짐작이다.
- Windows 거름은 이름(파일·폴더)과 `LAST_WRITE`·`SIZE`다. 속성만 바뀐 것은 받지 않는다. inotify의 `IN_ATTRIB`도 받지 않는다. 시각만 바꾼 것(`touch`)은 Windows에서 `modified`로 올 수 있다.
- 폴더인지 파일인지는 싣지 않는다. Windows의 `FILE_NOTIFY_INFORMATION`에 그 값이 없다. 필요하면 앱이 본다.
- 폴더 하나를 옮기거나 지우면 그 아래 항목마다 알리지 않을 수 있다. 앱은 그 경로 아래 전부의 변경으로 본다.
- 처음 훑기는 하지 않는다. 앱은 `add`를 먼저 하고 그다음 읽는다. 거꾸로 하면 그 사이의 변경을 놓친다.

**경로.** 경로는 UTF-8이고 감시 경로 기준 상대 경로다. Windows의 `\`는 `/`로 바꾼다. UTF-8·UTF-16으로 옮길 수 없는 이름(Android의 바이트열, Windows의 짝 없는 surrogate)은 항목으로 내지 않고 그 감시에 rescan을 단다. Windows가 짧은 이름(8.3)으로 준 이름은 `GetLongPathNameW`로 늘린다. 지워진 파일은 늘릴 수 없어 그대로 둔다.

**정지.** `stop()`은 멱등이고 어느 thread에서든 부른다. 차례가 계약이다.

1. 새 `add`를 거절한다.
2. thread를 깨워 멈추고 join한다. 이 뒤로 `deliver`는 없다. 아직 내지 않은 변경은 버린다.
3. 걸어 둔 OS 감시를 모두 풀고 `stop_budget` 안에서 OS가 버퍼를 놓기를 기다린다. 넘기면 버퍼를 일부러 남긴다(`http_client`와 같다).

표준 host 순서에서는 `logic_driver::stop_workers()`에서 `stop()`을 부른다. `deliver`가 쥔 자원보다 먼저다. `logic_driver::cancel()`에서 할 일은 없다. 감시자는 오래 끌 일감을 들지 않는다.

## 결정

**1. API 모양.** 질문: 감시자가 앱에 어떻게 건네는가.
- 길 a: `create(config, error)` + `void deliver`. `http_client`와 똑같다. 대가: inbox가 거절해도 감시자는 모른다. 앱이 `post_app_message_ticketed`의 0을 보고 원자적 표지를 세워 스스로 다시 훑어야 한다. 모든 앱이 같은 코드를 쓴다.
- 길 b: `create(config, error)` + `bool deliver`. 거짓이면 감시자가 다음 묶음에 합쳐 다시 낸다. 대가: `http_client`와 서명이 한 곳 다르다. 감시자가 거절된 묶음을 상한까지 쥔다.
- 길 c: `app_host&`와 메시지 짓는 함수를 받아 감시자가 직접 게시한다. 대가: 원시가 host에 묶인다. host 없는 test와 도구에서 못 쓴다.
- 권장: b. 라이브러리가 "조용한 유실이 없다"를 스스로 지키는 길은 이것뿐이다. 대가: 서명 차이 하나와 묶음 하나만큼의 메모리. **승인: 길 b (2026-11-02).**

**2. 전달 단위.** 질문: 원 이벤트를 하나씩 내는가, 묶는가.
- 길 a: 원 이벤트 하나씩. 대가: 저장 한 번이 메시지 여럿이고, 폭주가 inbox를 채운다. 합치기와 짝짓기를 앱마다 다시 짓는다.
- 길 b: 위 표대로 합친 묶음 + rescan 표식. 대가: 최소 `settle`만큼 늦다. 같은 경로의 중간 상태(created 뒤 removed)는 보이지 않는다.
- 권장: b. 변경을 보는 앱이 원하는 것은 "무엇을 다시 읽을까"이고 중간 상태가 아니다. 대가: 기본값으로 100 ms 늦고 중간 상태가 보이지 않는다. **승인: 길 b (2026-11-02).**

**3. 범위와 기본값.** 질문: 무엇을 감시할 수 있고 기본은 무엇인가.
- 길 a: `folder`·`subtree`·`file` 셋을 `add`가 받는다. 기본값을 두지 않는다. 대가: Android의 `subtree`는 폴더마다 watch를 걸어야 한다(아래).
- 길 b: `folder`만. 대가: 하위 폴더를 보는 앱이 inotify 재귀를 다시 짓는다.
- 권장: a. 인자를 꼭 받아 고르는 비용을 앱이 의식한다. 대가: Android `subtree`의 폴더마다 watch와 `max_user_watches` 한계. **권장대로 진행.**

**4. 디바운스 값.** 질문: `settle`·`max_delay`·`max_changes`의 기본은 무엇인가.
- 길 a: 100 ms·1000 ms·4096을 기본으로 둔다. 대가: 백신·색인기가 그 뒤에 건드리면 묶음이 하나 더 온다.
- 길 b: 기본값을 두지 않고 앱이 정한다. 대가: 앱마다 같은 숫자를 다시 고른다.
- 권장: a. 편집기 저장은 수 ms 안에 끝나고, 쉬지 않고 쓰는 로그도 1초마다 보인다. 대가: 늦게 건드리는 도구가 있으면 묶음이 하나 더 온다. **권장대로 진행.**

**5. Android 범위.** 질문: 어느 저장소까지 약속하는가.
- 길 a: 앱 전용 저장소(`GameActivity`의 `internalDataPath`. 앱의 `android_main`이 받는 `android_app`의 `activity`에서 읽는다)만 보장하고, 공유 저장소는 받되 보장하지 않는다. 대가: 공유 저장소에서 다른 앱의 변경을 놓칠 수 있다.
- 길 b: 앱 전용 밖은 `add`가 거절한다. 대가: 그 경로를 쓰는 앱이 길이 없다.
- 길 c: 공유 저장소에 폴링을 더한다. 대가: 배터리와 구현 크기. 이번 범위를 넘는다.
- 권장: a. 앱이 쓰는 설정과 자원은 앱 전용 저장소에 있고, 공유 저장소는 실기기 확인 전이라 약속하지 않는다. 대가: 공유 저장소에서 다른 앱의 변경을 놓칠 수 있다. **권장대로 진행.**

**6. 네트워크 공유·이동식 드라이브.** 질문: 변경 알림이 약한 경로를 어떻게 다루는가.
- 길 a: 보장하지 않는 한계로 적고 폴링 대체를 두지 않는다. 대가: 그 경로를 보는 앱은 스스로 폴링한다.
- 길 b: 폴링 대체를 둔다. 대가: 구현 크기와 주기 정책이 늘어 이번 범위를 넘는다.
- 권장: a. 대가: 그 경로를 보는 앱은 스스로 폴링한다. **권장대로 진행.**

**7. 정지 예산.** 질문: `stop()`이 OS가 버퍼를 놓기를 얼마나 기다리는가.
- 길 a: `stop_budget` 기본 1초. `http_client` 주석이 든 종료 예산 산술 안이다. 대가: 넘기면 버퍼를 일부러 남긴다.
- 길 b: 다 놓을 때까지 기다린다. 대가: OS가 놓지 않으면 종료가 멈춘다.
- 권장: a. 실제로는 OS 감시를 푸는 데 수 ms다. 대가: 넘긴 드문 경우 버퍼 하나가 샌다. **권장대로 진행.**

G4~G7은 이 노드와 닿지 않는다. 오류 글은 `platform_result`처럼 개발자용 영문 진단이고 화면 글이 없다.

## 플랫폼

### Win32

- 감시마다 폴더 핸들 하나다. `CreateFileW(FILE_LIST_DIRECTORY, FILE_SHARE_READ | WRITE | DELETE, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED)`. `FILE_SHARE_DELETE`가 없으면 감시가 그 폴더의 삭제·이름 바꿈을 막는다.
- 겹친 I/O의 `ReadDirectoryChangesW`를 I/O 완료 포트 하나에 묶는다. thread는 `GetQueuedCompletionStatusEx`로 기다리고, 기다림의 시한은 가장 가까운 묶음 시각이다. 핸들 수에 64개 상한이 없다.
- `stop`과 `add`·`remove`는 `PostQueuedCompletionStatus`로 thread를 깨운다. 감시를 풀 때는 `CancelIoEx` 뒤 그 완료를 받고 나서 버퍼를 놓는다. `blocking_io_canceller`는 쓰지 않는다. 동기 I/O가 아니라서다.
- 버퍼는 감시마다 64 KB다. 네트워크 경로는 64 KB를 넘는 버퍼를 거절한다. 모든 경로를 같은 크기로 두어 갈래를 만들지 않는다.
- 감시 폴더가 지워지면 읽기가 오류로 끝난다. 그 오류를 `lost`로 바꾼다(실측은 `ERROR_ACCESS_DENIED`다. 지움이 대기 중인 폴더라 다시 걸기도 같은 오류로 실패한다). 핸들은 옮겨 간 폴더를 따라가므로, 묶음을 낼 때 `GetFinalPathNameByHandleW`를 처음 경로와 견주어 달라졌으면 `lost`다. 안의 변경이 없으면 늦게 안다.
- 핸들 소유는 `src/win32/owned_handle.h`, OS 오류 글은 `win32::make_last_error`를 쓴다(아래 비공개 helper). 경로 변환은 `src/win32/utf8.h`의 `utf8_to_utf16`·`utf16_to_utf8`이 맡는다.
- 한계: SMB 공유는 서버가 변경 알림을 주지 않으면 `add`가 실패하거나 알림이 오지 않는다. 다른 기계가 바꾼 것은 늦거나 빠질 수 있다. 이동식 드라이브는 열린 핸들 때문에 "안전하게 제거"가 막힐 수 있다. 앱이 감시를 내려야 한다.

### Android

- `inotify_init1(IN_NONBLOCK | IN_CLOEXEC)`과 정지용 `eventfd`를 `poll` 하나로 기다린다. 시한은 가장 가까운 묶음 시각이다. JNI도 Java도 쓰지 않는다. 감시 thread는 JVM에 붙지 않는다. Java 0줄 방침([android-port-plan.md](android-port-plan.md))과 부딪칠 것이 없다.
- 거는 표시는 `IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR`이다. 뿌리의 `IN_DELETE_SELF`·`IN_MOVE_SELF`·`IN_IGNORED`·`IN_UNMOUNT`는 `lost`다.
- `subtree`는 폴더마다 watch를 건다. 새 폴더(`IN_CREATE | IN_ISDIR`, 들어온 `IN_MOVED_TO | IN_ISDIR`)는 watch를 먼저 걸고 그 안을 훑어 이미 있는 항목을 created로 낸다. watch를 걸기 전에 생긴 파일이 이렇게 잡히고, 겹친 것은 created+created로 합쳐진다. 안에서 옮긴 폴더는 watch 표의 경로를 고친다. 밖으로 나간 폴더는 그 아래 watch를 푼다. 같은 읽기 안에 `IN_MOVED_TO` 짝이 오지 않은 폴더가 밖으로 나간 것이다.
- 한 감시자 안에서 두 감시가 같은 폴더를 걸면 inotify는 같은 wd를 준다. wd마다 그것을 쓰는 감시를 세고 마지막 감시가 놓을 때만 푼다. 같은 폴더의 파일 둘을 `file`로 따로 거는 것이 흔한 모양이다.
- watch 수가 `max_user_watches`를 넘으면(`ENOSPC`) `add`는 빈 표와 오류다. 이미 선 `subtree`가 새 폴더에서 넘으면 그 감시는 `lost`다. 일부만 보는 감시를 조용히 두지 않는다.
- 앱 전용 저장소는 보장한다. 경로는 `android_app::activity->internalDataPath`(`GameActivity`)이고 `run_application`이 `set_files_directory`에 주는 값과 같다. 비공개 `android::files_directory()`는 앱이 부를 수 없고, 공개 접근자는 범위 밖이다. 앱은 자기 `android_main`이 받은 `android_app`에서 읽는다.
- 공유 저장소는 FUSE를 거쳐 다른 앱이 만든 변경이 오지 않을 수 있다고 알려져 있다. 실기기 확인 전 가정이라 보장하지 않는다.
- JNI helper는 필요 없다. errno 글은 아래 비공개 helper의 `android::make_errno_error`를 쓴다.

### 비공개 helper

이 노드가 두 번째 사용자가 되는 helper 둘을 감시자와 같은 커밋에서 꺼낸다. 오류 글의 `win32::make_last_error`([win32_error.h](../src/win32/win32_error.h))와 JNI의 `jni_local_frame`·`jni_failed`([java_vm.h](../src/android/java_vm.h))는 이미 한 곳에 있다. 감시자는 JNI를 쓰지 않아 앞의 것만 쓴다.

| 옮길 것 | 지금 자리 | 새 자리 |
| --- | --- | --- |
| `owned_handle` | `src/win32/platform_services_win32.cpp`의 익명 namespace | `src/win32/owned_handle.h` (header-only, 그대로 옮긴다) |
| `errno_failure`의 글 | `src/android/platform_services_android.cpp`의 익명 namespace | `src/android/errno_text.h`의 `android::make_errno_error(message)` (`std::u8string`) |

- `owned_handle`은 복제본이 없어 제자리에 두었던 것이다. 감시자가 폴더 핸들과 완료 포트에 쓰는 두 번째 사용자다. `platform_services_win32.cpp`는 자기 사본을 지우고 헤더를 쓴다.
- `errno_failure`는 `platform_result`를 돌려줘 그대로는 옮길 수 없다. `make_last_error`처럼 글만 만들고(`errno`는 부른 바로 뒤에 읽는다), `platform_result` 래퍼는 `platform_services_android.cpp`에 남는다. 감시자는 이 글을 `std::u8string& error`에 쓴다.
- 오류 문구와 호출 순서는 그대로다. 기존 `tests/platform_services_tests.cpp`가 무변경으로 통과해야 한다.
- 범위 밖: `android_settings.cpp`의 `local_frame`. 이 노드 때는 소멸자에서 예외를 거두는 다른 뜻이었다. 뒤의 `local_frame` 노드가 `jni_local_frame`·`jni_failed`로 옮겼다.

## 다른 노드와의 약속

이 노드가 감시 원시의 소유자다. R5의 hot_reload와 style_sheet·markup의 실행 중 로드는 이 API만 쓰고 감시를 따로 짓지 않는다.

- **파일 하나.** `file_watch_scope::file`로 건다. 부모 폴더를 보므로 파일이 지워졌다 다시 생겨도 감시가 이어진다. 파일이 아직 없어도 걸린다.
- **다시 읽을 신호.** 그 파일 이름의 created·modified와, 새 이름이 그 파일인 renamed는 모두 "다시 읽어라"다. 옛 이름이 그 파일인 renamed와 removed는 "지금은 없다"다. rescan도 "다시 읽어라"다.
- **임시 파일 뒤 이름 바꾸기 저장.** `a.json.tmp`에 쓰고 `a.json`으로 바꾸는 저장은 합치기 표대로 `a.json`의 created·modified·renamed 가운데 하나로 온다(덮인 `a.json`의 지움 알림이 먼저 오면 removed+created라 modified다). `file` 감시는 임시 이름을 거르므로 앱은 임시 파일을 보지 않는다. `a.json`의 removed와 created가 따로 오지 않는다.
- **반쯤 쓴 파일.** `modified`는 쓰기 완료가 아니다. 읽다 해석에 실패하면 이전 값을 지키고 다음 묶음을 기다린다. 쓰는 쪽이 luil이면 `replace_file_atomically`를 쓴다.
- **순서.** `add`를 먼저 하고 처음 읽기를 한다.
- **Win32·Android 비공개 helper.** 이 노드가 `owned_handle.h`와 `errno_text.h`를 세운다(플랫폼 절). taskbar는 `win32_error.h`를 쓰고 `owned_handle.h`는 쓰지 않는다. 이 노드의 파일에 helper를 다시 복제하지 않는다.
- **같은 라운드의 줄 3과 데모 공용 파일.** `examples/demo_main.cpp`는 줄 3의 spring이 사이드바와 metrics override를 바꾸고 taskbar가 `make_taskbar_message`를 더한다. 이 노드는 `bind_host`와 `stop_workers`에 한 줄씩만 더해 자리가 겹치지 않는다. 모바일 데모는 `device_page.cpp`를 줄 3과 taskbar가 고치므로 이 노드는 거기에 더하지 않고 새 페이지를 둔다. 새 페이지가 고치는 `app.cpp`는 줄 3도 고친다(reduce_motion의 `make_metrics_message`, spring의 `next_tick`·metrics 처리). 자리가 달라 겹치지 않고, 병합은 줄 3이 먼저다.

## 확인

- `tests/file_watch_batch_tests.cpp`(core, `luil_core_tests`라 Android 기기에서도 돈다): 합치기 표의 각 줄, 짝 없는 옛 이름·새 이름, 이어진 이름 바꿈, `settle` 경계와 `max_delay` 상한, `max_changes`를 넘으면 비우고 rescan, 거절된 묶음이 다음 묶음에 합쳐지는지, 끝 묶음이 하나뿐인지, 경로 구분자.
- `tests/file_watcher_tests.cpp`(실제 폴더, [platform_services_tests.cpp](../tests/platform_services_tests.cpp)의 `scratch_folder` 관용구): 생성·수정·삭제·이름 바꿈, `subtree`의 하위 폴더 파일과 새로 만든 폴더 안의 파일, `file` 감시가 임시 파일 저장을 한 항목으로 받는지, `replace_file_atomically`로 쓴 파일, 감시 폴더 삭제의 `lost`, `remove`의 끝 묶음, 두 감시자 동시, 변경 없을 때와 폭주 중의 `stop`이 예산 안에 끝나는지, `stop` 뒤 `deliver`가 없는지, `deliver`가 거짓을 답한 뒤 다시 오는지.
  - Windows는 `luil_tests`다. 작은 버퍼를 test 손잡이로 주어 넘침을 일으켜 rescan을 본다.
  - Android는 `file_watcher_android.cpp`가 JVM이 필요 없어 `android_input.cpp`처럼 `luil_core_tests`에 소스를 넣어 기기 셸(`cmake/android/adb_run.cmake`)에서 돈다. 넘침은 감시 thread를 `deliver` 안에 묶어 둔 채 `max_queued_events`보다 많은 이벤트가 나도록 파일을 만들어 본다. `replace_file_atomically`는 `luil` 쪽이라 그 경우만 Windows에서 본다.
- 데스크톱 데모: `examples/demo/files_page.cpp`에 임시 폴더 하나를 감시하는 구역을 둔다. "파일 만들기"·"고치기"·"지우기"·"이름 바꾸기" 단추와 마지막 묶음을 그린 label(`변경: created a.txt` 같은 글, rescan·끝 표시 포함)이 있어 UIA로 되읽는다.
  - 감시자는 `files_page`가 쥔다. `deliver`는 `post_app_message_ticketed`로 보내야 하므로 host가 필요하다. `demo_main.cpp`의 `bind_host`가 `files_.bind_host(host)`로 host 포인터를 원자적 손잡이로 넘긴다.
  - 감시자는 감시 구역의 단추를 처음 누를 때 logic thread에서 세우고 `%TEMP%/luil-demo-watch-<pid>`를 `folder`로 건다. 파일을 바꾸기 전에 건다. 한 frame만 그리는 smoke 모드에서는 thread가 서지 않는다. 폴더는 페이지가 없어질 때 지운다.
  - label은 둘이다. `files-watch-state`가 "감시: 켜짐 <폴더>"·"감시: 끝남"을, `files-watch-batch`가 마지막 묶음 글을 그린다.
  - `stop_workers`는 UI thread에서 logic join 전에 불린다. 그 자리가 닿는 것은 감시자의 원자적 손잡이 하나(`files_.stop_watcher()`)이고, `live_client_`와 같은 이유다. `demo_main.cpp`의 몫은 이 두 줄이다.
- 모바일 데모: 새 페이지 `examples/mobile_demo/files_page.cpp`·`.h`다. 앱 전용 폴더에서 같은 일을 하는 단추와 label을 둔다. `device_page.cpp`에는 더하지 않는다(약속).
  - `smb_page`와 같은 길이다. delegate(`android_main.cpp`, Windows 창의 `main.cpp`)가 `on_started`에서 감시자를 세워 `file_watcher_intent`로 driver에 넘긴다. 세우고 거는 것은 `files_page.h`의 inline `start_files_watch` 하나를 두 delegate가 부른다. `mobile_driver::stop_workers`가 `files_.stop()`을 부른다(`smb_.stop()`과 같다).
  - 페이지가 쥔 것은 포인터가 아니라 감시자를 멈추는 함수 하나다(intent의 `stop`). 페이지 소스는 감시자 구현 없이 `luil_core_tests`에 묶이므로, 페이지가 `file_watcher`의 멤버를 직접 부르면 Windows의 core test가 링크되지 않는다. `start_files_watch`가 inline인 것도 같은 이유다.
  - 폴더는 Android가 `android_main`이 읽은 `internalDataPath`의 `watch-demo`, Windows 창이 `std::filesystem::temp_directory_path()`의 `luil-mobile-demo-watch-<pid>`다. 연결은 `app.cpp`의 `page_entries`(11 → 12)·`handle` 끝·`build`·`stop_workers`와 `app.h`·`mobile_demo.cmake`다. 페이지 키 `page_files`는 공용 `common.h`를 고치지 않으려고 `files_page.h`에 둔다.
  - label은 `files-watch-state`("감시: 켜짐"·"감시: 끝남"·"감시: 걸지 못함 (…)")와 `files-watch-batch`(마지막 묶음 글)다. 단추는 "파일 만들기"·"고치기"·"지우기"·"이름 바꾸기"이고 감시가 걸린 뒤에만 폴더를 바꾼다.
  - `tests/mobile_demo_page_tests.cpp`(추가): 묶음 intent를 페이지에 직접 주면 label이 마지막 묶음 글(rescan·끝 표시 포함)로 바뀌는지 본다. 감시자는 세우지 않는다.
- 사람 확인: 실기기 공유 저장소에서 파일 관리자가 만든 변경이 오는지(FUSE 한계 실측). 네트워크 공유·이동식 드라이브와 그 분리 시의 `lost`. 백신·색인기가 도는 실제 개발 폴더에서 묶음이 하나로 모이는지.

## 범위 밖

- 폴링 감시(네트워크 공유·공유 저장소 대체).
- 폴더인지 파일인지, 크기, 시각을 싣는 것. 앱이 필요하면 본다.
- 처음 훑기와 폴더 내용 비교(스냅숏 diff).
- 쓰기 완료를 알리는 종류. 두 플랫폼에서 같은 뜻으로 낼 길이 없다.
- 감시 경로 기준 패턴 거르기(glob). 앱이 묶음을 거른다.
- 다른 프로세스·다른 사용자의 감시 공유, 권한 상승이 필요한 폴더.
- 앱 전용 폴더의 경로를 주는 공개 접근자. 앱이 `android_app`에서 읽는다.

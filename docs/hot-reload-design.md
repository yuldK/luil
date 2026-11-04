# 핫 리로드

스타일 JSON과 화면 마크업 파일을 저장하면 다음 frame에 반영하는 길이다. 라이브러리 도우미 객체 하나가 [파일 감시](file-watch-design.md)의 `file_watcher`를 쥐고, 바뀐 파일을 감시 thread에서 읽어 순수 파서(`parse_ui_style`·`parse_ui_markup`)로 해석한 뒤 결과만 앱에 건넨다. 앱은 그 결과를 자기 앱 메시지로 logic thread에 옮겨 쥔 스타일과 틀을 바꾼다. 공개 형식은 새 헤더 `include/luil/app/hot_reload.h`의 `hot_reloader`·`hot_reloader_config`·`hot_reload_file`·`hot_reload_kind`·`hot_reload_status`·`hot_reload_limits`·`hot_reload_update`와 감시 없이 한 번 읽는 `load_hot_reload_file`(결정 3)이다. 코드(DLL·.so)와 문자열 표는 다시 읽지 않는다.

## 문제

- 다시 읽을 조각은 다 섰지만 잇는 것이 없다. 감시는 `file_watcher`([file_watcher.h](../include/luil/app/file_watcher.h) 117행), 해석은 `parse_ui_style`([ui_style_json.h](../include/luil/theme/ui_style_json.h) 81행)과 `parse_ui_markup`([ui_markup.h](../include/luil/ui/ui_markup.h) 135행)이다. 두 파서는 파일을 읽지 않는다(16행, 134행).
- 파일을 읽는 공개 도구가 없다. 플랫폼 도구의 파일 쪽은 쓰기인 `replace_file_atomically`뿐이다([platform_services.h](../include/luil/app/platform_services.h) 42행). 앱마다 크기 한도, 읽는 사이에 지워진 파일, 반쯤 쓴 파일을 다시 푼다.
- 감시 묶음을 "다시 읽어라"와 "지금은 없다"로 가르는 규칙은 [파일 감시](file-watch-design.md) 236~239행에 글로만 있다. `deliver`가 거짓이면 묶음을 다시 내는 규칙(`file_watcher.h` 88행)도 앱이 지켜야 한다. 한 곳을 틀리면 저장이 화면에 닿지 않거나, inbox가 찬 때의 저장을 조용히 잃는다.
- 데모의 "다시 읽기"는 파일이 아니다. 스타일 묶음은 내장 글을 칸에 싣고 읽고([style_sheet_sample.h](../examples/demo/style_sheet_sample.h) 164행), 마크업 묶음은 견본 글을 처음부터 다시 읽는다([markup_sample.h](../examples/demo/markup_sample.h) 31행). 저장해서 바뀌는 것을 보일 자리가 없다.
- Android의 앱 전용 폴더 경로는 luil이 내주지 않는다. 앱이 `android_app`의 `internalDataPath`를 읽는다([android_main.cpp](../examples/mobile_demo/android_main.cpp) 129~130행).

## 공개 형식

`include/luil/app/hot_reload.h`, namespace `luil`. `file_watcher.h`·`updater.h`처럼 헤더는 core 검사 범위에 두고 표준 헤더와 `luil/app/file_watcher.h`·`luil/theme/ui_style_json.h`·`luil/ui/ui_markup.h`만 include한다. `luil.h`가 include한다. 구현은 둘로 나눈다.

| 몫 | 자리 | target |
|---|---|---|
| 파일 읽기, 한도, 해석, 오류 줄, 감시 묶음 가르기(`read_hot_reload_batch`, 비공개) | `src/host/hot_reload_load.cpp`·`.h` | `luil_core`. OS 헤더 없이 `std::filesystem`·`std::ifstream`만 쓴다([typeface_loading.cpp](../src/host/typeface_loading.cpp) 31~47행의 선례) |
| `hot_reloader`(감시자를 쥐고 `deliver`를 잇는다) | `src/luil/hot_reloader.cpp` | `luil`. `file_watcher::create`가 플랫폼 쪽이라([src/CMakeLists.txt](../src/CMakeLists.txt) 529·850행) Win32·Android 목록 둘에 든다. `luil/url_scheme.cpp`(531·851행), `net/updater.cpp`(704·906행)와 같다 |

```cpp
namespace luil {
    enum class hot_reload_kind
    {
        style,  // parse_ui_style
        markup, // parse_ui_markup
    };
    enum class hot_reload_status
    {
        loaded,     // 새 값이 있다
        rejected,   // 해석이 막혔다. 값이 비고 errors가 이유다. 반쯤 쓴 파일도 여기다 (syntax)
        unreadable, // 열거나 읽지 못했다 (잠김, 읽는 사이에 지워짐, 일반 파일이 아님). 도우미는 약 2초 다시 읽어 본 뒤에 건넨다
        too_large,  // max_bytes를 넘어 열지 않았다
        missing,    // 지금 없다 (removed, 옛 이름이 그 파일인 renamed, 처음 읽을 때 없음)
        lost,       // 감시가 끊겼다. 이 표로는 더 오지 않는다
    };
    struct hot_reload_file
    {
        std::u8string path {}; // UTF-8. file_watch_scope::file로 걸어 파일이 아직 없어도 된다
        hot_reload_kind kind { hot_reload_kind::style };
    };
    struct hot_reload_limits
    {
        // 파일의 바이트 수다. 넘으면 열지 않는다. 스타일 파서에는 크기 한도가 없어 여기서 정한다.
        std::size_t max_bytes { std::size_t { 1 } * 1024 * 1024 };
        // 마크업 해석 한도다. 크기는 max_bytes와 markup.max_bytes 가운데 작은 쪽이다.
        ui_markup_limits markup {};
    };
    // 파일 하나를 한 번 읽은 결과다. 앱이 자기 앱 메시지에 담아 logic thread로 옮긴다.
    struct hot_reload_update
    {
        file_watch_ticket ticket {}; // add가 받은 표다. load_hot_reload_file은 비었다
        std::u8string path {};
        hot_reload_kind kind { hot_reload_kind::style };
        hot_reload_status status { hot_reload_status::missing };
        std::shared_ptr<const ui_style> style {};            // style이고 loaded일 때만
        std::shared_ptr<const ui_markup_document> markup {}; // markup이고 loaded일 때만
        std::u8string text {};                               // 읽은 글이다. loaded·rejected일 때만 (결정 4)
        // 개발자용 영문 한 줄들이다. 파서 오류는 describe 그대로, 읽기 오류는 한 줄이다. loaded면 비었다.
        std::vector<std::u8string> errors {};
        std::vector<style_error> style_errors {};     // 파서가 준 그대로다 (자리, 키, offset)
        std::vector<ui_markup_error> markup_errors {};
    };

    // 감시 없이 한 번 읽는다 (결정 3). 부른 thread에서 읽고, 전역 상태가 없어 어느 thread에서나 부른다. 던지지 않고 실패를 결과로 돌린다.
    [[nodiscard]] hot_reload_update load_hot_reload_file(const hot_reload_file& file, const hot_reload_limits& limits = {});

    struct hot_reloader_config
    {
        // 결과를 앱에 건네는 유일한 자리다. 감시 thread에서 불린다. 받았으면 참이다 (결정 1).
        //
        //     config.deliver = [&host](luil::hot_reload_update update) {
        //         return host.post_app_message_ticketed(luil::app_message { std::move(update) }) != 0;
        //     };
        //
        //  - 거짓이면 감시자가 그 묶음을 다시 내고, 그때 파일을 다시 읽는다.
        //  - 여기서 add·remove는 되고 stop은 안 된다.
        //  - 비어 있으면 파일을 읽지 않고 묶음을 받은 것으로 본다 (file_watcher와 같다).
        std::function<bool(hot_reload_update)> deliver {};
        // file_watcher_config로 그대로 간다 (디바운스, 묶음 늦춤 상한, 종료 예산).
        std::chrono::milliseconds settle { 100 };
        std::chrono::milliseconds max_delay { 1000 };
        std::chrono::milliseconds stop_budget { 1000 };
        hot_reload_limits limits {};
    };

    // 등록한 스타일·마크업 파일을 감시하고, 바뀌면 다시 읽어 deliver로 건넨다.
    //  - thread를 따로 두지 않는다. file_watcher 하나를 쥐고 읽기와 해석은 그 감시 thread의 deliver 안에서 한다.
    //  - 이전 값을 쥐지 않는다. 실패한 결과에는 값이 없고, 쥔 것을 지키는 일은 앱이 한다.
    class hot_reloader
    {
    public:
        // 복사·이동은 지운다 (file_watcher와 같다).
        ~hot_reloader(); // stop()

        // 실패하면 null이고 error에 이유다 (file_watcher::create와 같은 규약).
        [[nodiscard]] static std::unique_ptr<hot_reloader> create(hot_reloader_config configuration, std::u8string& error);

        // 감시를 걸고 그다음 그 자리에서 한 번 읽어 첫 결과를 돌려준다 (결정 2). 첫 결과는 deliver로 오지 않는다.
        //  - 감시를 걸지 못하면(부모 폴더가 없음, OS 거절) ticket이 비고 error에 적는다. 읽은 결과는 그래도 담는다.
        [[nodiscard]] hot_reload_update add(const hot_reload_file& file, std::u8string& error);
        // 감시를 내린다. 남은 묶음은 거두고 앱에 건네지 않는다. 모르는 표는 아무 일도 하지 않는다.
        //  - 이미 deliver에 들어간 결과는 거두지 못한다. 앱 메시지로 떠났을 수도 있으므로 앱은 ticket으로 가린다.
        void remove(file_watch_ticket ticket) noexcept;
        // 멈춘다. 멱등이고 어느 thread에서든 부른다 (deliver 안은 안 된다). 차례는 file_watcher::stop 그대로다.
        void stop() noexcept;
        // 비공개: engine 하나를 unique_ptr로 쥔다 (file_watcher와 같은 꼴).
    };
}
```

## 규칙

**감시 묶음 가르기.** 표 하나가 파일 하나다. 묶음에서 그 파일 이름의 항목 하나로 정한다([파일 감시](file-watch-design.md) 236~239행).

| 묶음 | 하는 일 | 결과 |
|---|---|---|
| `end == lost` | 표를 거둔다 | `lost`. `errors`는 묶음의 `error` 한 줄이다. 이 표로는 더 오지 않는다 |
| `end == removed` | 앱이 `remove`했다 | 건네지 않는다 |
| `rescan`, `created`, `modified`, 새 이름이 그 파일인 `renamed` | 파일을 다시 읽는다 | `loaded`·`rejected`·`unreadable`·`too_large`·`missing` |
| `removed`, 옛 이름이 그 파일인 `renamed` | 읽지 않는다 | `missing` |

- 이름은 대소문자를 접어서 견준다(`text::fold_case`, 결정 7). Windows의 이름은 대소문자를 가리지 않아 OS가 디스크의 표기로 알리므로 앱이 준 표기와 다를 수 있다. 한 묶음에 다시 읽을 항목과 없음 항목이 함께 있으면 다시 읽는다. 읽기가 파일의 지금을 말한다.
- 임시 파일 뒤 이름 바꾸기 저장은 `file` 감시가 임시 이름을 거르므로 그 파일의 created·modified·renamed 가운데 하나로 온다(238행). 따로 할 일이 없다.
- 아직 모르는 표의 묶음은 거짓을 돌려 다시 받는다. 감시자가 `add`의 표를 돌려주기 전에 첫 묶음이 올 수 있어서다. 내린 표는 끝 묶음이 올 때까지 표시만 해 둔다. 내린 표의 묶음은 참을 돌려 버리고, 끝 묶음에서 표를 거둔다. 지우면 그 끝 묶음이 아직 모르는 표가 되어 끝없이 다시 온다. 읽는 사이에 내렸으면 읽은 결과도 건네지 않는다. 앱의 `deliver`는 표를 지키는 잠금 밖에서 부른다. 그래야 `deliver` 안의 `add`·`remove`와, 감시 thread를 기다리는 `file_watcher::add`가 서로 막지 않는다.
- 읽기는 늘 파일 전체다. 합치기나 증분이 없다([스타일 읽기](style-sheet-design.md) 227행 약속 2).
- `unreadable`은 바로 건네지 않는다(결정 8). 약 2초 동안 거짓을 돌려 감시자가 `settle`마다 같은 묶음을 다시 내게 하고, 그때마다 다시 읽는다. 읽으면 그 결과를 건네고, 상한이 지나면 그때 `unreadable`을 건넨다. 건넨 뒤의 다음 실패는 상한을 처음부터 센다. `add`의 첫 읽기는 다시 읽지 않는다.

**읽기.** `load_hot_reload_file`, `add`의 첫 읽기, 감시 thread의 다시 읽기는 같은 함수다. 던지지 않는다. 감시 thread의 `deliver`에서 던지면 감시자가 거절로 보고 같은 묶음을 끝없이 다시 낸다. 경로로 옮길 수 없는 글(깨진 UTF-8), NUL이 든 경로, 메모리 부족은 `unreadable`이다.

1. 크기를 먼저 잰다. 없으면 `missing`, 일반 파일이 아니면 `unreadable`, `max_bytes`를 넘으면 열지 않고 `too_large`다.
2. 한 번에 읽는다. 읽은 바이트가 잰 크기와 다르거나 그 뒤에 글이 더 있으면 `unreadable`이다. 쓰는 중인 파일이다.
3. 바이트를 그대로 파서에 넘긴다. 글의 꼴(BOM, 줄 끝, 엄한 문법)은 파서의 규칙이다. 오류가 하나라도 있으면 `rejected`다. 반쯤 쓴 파일은 여기서 `syntax`로 막힌다.
4. 받으면 스타일은 `make_shared<const ui_style>`로, 마크업은 파서의 `document`를 그대로 싣는다.

오류 줄은 개발자용 영문이다. 파서 오류는 `describe`(`ui_style_json.h` 60행, `ui_markup.h` 117행) 그대로이고, 읽기 오류는 `File not found: style.json` 같은 한 줄이다. 앱의 언어를 따르지 않는 진단이라 기본 문구(`default_text`)와 i18n 표에 들지 않는다.

**앱이 할 일.**

- `deliver`는 결과를 앱 메시지에 담아 `post_app_message_ticketed`로 넘기고 그 표가 0이 아닌지를 돌려준다. `app_message`는 형으로 가리므로([app_message.h](../include/luil/ui/app_message.h)) `hot_reload_update`를 그대로 담아도 된다.
- `loaded` 스타일은 같은 포인터를 `ui_frame::style`과 tree마다의 `make_arranged_tree(..., style)`·`arrange_context::style` 두 자리에 준다. 주 창 tree뿐 아니라 popup·보조 창 tree도 같다([스타일 읽기](style-sheet-design.md) 130행). 그러면 줄 나눔 정책과 cache도 다음 배치에서 따라온다([줄 나눔 정책 스타일 키](wrap-policy-key-design.md) 246행).
- `loaded` 마크업은 쥔 `shared_ptr<const ui_markup_document>`를 바꾼다. 다음 `make_frame`이 새 틀을 펼친다. 초점·hover·값은 id로 이어진다([마크업 바인딩](markup-binding-design.md) 228행).
- 그 밖의 상태는 쥔 것을 그대로 두고 `errors`를 상태 줄이나 로그에 보인다. 화면은 이전 스타일과 틀로 남는다(스타일 읽기 227행 약속 3, 마크업 바인딩 227행).
- `unreadable`을 받은 뒤에는 다음 저장이 다시 읽는다. 상한을 넘겨 잠근 쪽이 닫을 때는 알림이 없을 수 있다(결정 8). 그 저장까지 보여야 하는 앱은 `load_hot_reload_file`로 다시 읽는다.
- 파일을 앱이 쓰면 `replace_file_atomically`로 쓴다. 반쯤 쓴 파일을 읽을 틈이 없다.

**수명과 thread.** 객체는 thread를 따로 두지 않는다. 다시 읽기와 해석은 감시 thread에서 하고, 결과의 포인터는 불변이라 thread 사이에 나눠 쥔다. `add`의 첫 읽기는 부른 thread에서 한다. 세우기는 `app_host`를 쥔 자리(`app_delegate::on_started`나 host를 받은 logic)에서 한다. 멈추기는 `logic_driver::stop_workers()`에서 `stop()`이고 `deliver`가 쥔 host보다 먼저다(`file_watcher.h` 115행). `stop`은 감시자의 `stop`이라 이 뒤로 `deliver`가 없다. 읽던 파일 하나는 끝까지 읽으므로 종료 예산에 `max_bytes` 한 번 읽기와 해석이 `stop_budget`과 함께 든다.

## 결정

- 확정(2026-11-04): N6 a. 스타일 JSON과 마크업 파일을 저장하면 다음 frame에 반영한다. 코드(DLL·.so) 다시 읽기는 범위 밖이고 바깥 도구를 문서로 안내한다. 문자열 표는 다시 읽지 않는다(G6 범위 B, 빌드 때 헤더).
- 확정(2026-11-04): N6-owner a. 라이브러리 도우미 객체 하나가 감시·파일 읽기·해석을 맡고 결과만 앱 메시지로 넘긴다. 앱이 받은 스타일을 frame과 tree 배치 두 자리에 주고, 객체는 앱이 `stop_workers`에서 멈춘다.
- 확정(2026-11-04): N6-android a. Android는 앱 전용 폴더의 파일만 감시하고 다시 읽는다. APK assets는 앱이 직접 읽는다.

**1. 결과를 넘기는 꼴.** 질문: 도우미가 결과를 앱 메시지로 어떻게 넘기는가.

- 길 a: `bool`을 돌려주는 `deliver` 콜백이다(이 문서). 앱이 결과를 앱 메시지에 담아 게시한다. 되는 것: `file_watcher`·`http_client`와 같은 꼴이다. 거짓이면 감시자가 묶음을 다시 내는 규칙을 그대로 잇는다. host 없이 시험한다. 대가: 앱이 lambda 한 줄을 쓴다.
- 길 b: 도우미가 `app_host&`를 받아 `app_message { hot_reload_update }`를 직접 게시한다. 되는 것: 앱이 한 줄을 덜 쓴다. 대가: 도우미가 host 수명에 묶이고 시험이 host를 세운다. 앱이 메시지 형을 고르지 못한다.
- 길 c: 도우미가 결과를 쌓고 앱이 `tick`에서 꺼낸다. 되는 것: 게시가 없다. 대가: 꺼낼 때를 알려고 `next_tick`을 돌리고, 쌓는 곳의 상한과 잠금이 새로 든다.
- 권장: a. 대가: 위의 lambda 한 줄이다. 권장대로 진행.

**2. 처음 읽기.** 질문: 감시를 건 뒤의 첫 읽기를 누가 어느 thread에서 하는가. 감시자는 처음 훑기를 하지 않고 "`add` 먼저, 그다음 읽기"다(`file_watcher.h` 113행).

- 길 a: `add`가 감시를 걸고 그 자리에서 읽어 첫 결과를 돌려준다(이 문서). 되는 것: 앱이 첫 frame부터 파일의 값으로 선다. 차례를 도우미가 지켜 앱이 틀릴 자리가 없다. 대가: 부른 thread가 파일 한 번 읽기(1 MiB 이하)만큼 막힌다. 네트워크 드라이브의 파일은 더 걸린다.
- 길 b: 첫 결과도 `deliver`로 보낸다. 되는 것: 부른 thread가 읽지 않는다. 대가: 감시 thread는 묶음이 와야 깨므로 첫 읽기를 그 thread에 넣는 길을 `file_watcher`에 새로 판다. 첫 frame은 내장 값으로 섰다가 곧 바뀐다.
- 길 c: 앱이 `load_hot_reload_file`로 따로 읽는다. 되는 것: 도우미가 작다. 대가: "`add` 먼저" 차례를 앱이 지킨다. 거꾸로 하면 그 사이의 저장을 놓친다.
- 권장: a. 대가: 위의 막힘이다. 권장대로 진행.

**3. 한 번 읽는 함수의 공개.** 질문: 감시 없이 파일을 한 번 읽는 함수를 공개 면에 두는가. 감시하지 않는 배포판도 같은 파일을 시작할 때 한 번 읽을 수 있다.

- 길 a: 비공개로 둔다(`src/host/hot_reload_load.h`). 되는 것: 공개 면이 객체 하나로 그친다. 대가: 한 번만 읽으려는 앱이 크기 한도와 오류 줄을 다시 쓰거나, 도우미를 세워 `add`의 첫 결과만 받고 멈춘다.
- 길 b: `load_hot_reload_file`로 공개한다(이 문서). 되는 것: 감시 여부와 상관없이 같은 읽기, 같은 오류 줄이다. 도우미 안의 읽기와 같은 함수라 시험이 한 벌이고 core test에서 잰다. 대가: 공개 함수가 하나 늘고 `hot_reload_limits`의 기본값이 공개 약속이 된다.
- 길 c: 플랫폼 도구에 일반 파일 읽기를 더한다. 되는 것: 스타일·마크업 밖의 파일도 읽는다. 대가: `platform_services.h`는 이번 라운드 줄 4의 파일이다. 표준 입출력으로 충분한 일에 플랫폼 구현이 둘 선다.
- 권장: b. 대가: 위의 공개 함수 하나다. 이름의 `hot_reload`가 "감시 없이 읽기"를 덜 말한다. 승인: 길 b (2026-11-04).

**4. 결과에 읽은 글을 싣는가.** 질문: `hot_reload_update`가 파일의 글도 나르는가.

- 길 a: 싣는다(이 문서, `loaded`·`rejected`일 때). 되는 것: 편집 칸에 파일 글을 띄우는 앱(데모의 스타일 칸)이 저장 뒤에도 파일과 같은 글을 본다. 오류의 `offset`·줄·열을 그 글에서 짚는다. 대가: 결과마다 글 하나(1 MiB 이하)를 옮긴다.
- 길 b: 싣지 않는다. 되는 것: 결과가 작다. 대가: 글이 필요한 앱이 파일을 한 번 더 읽고, 그 사이에 파일이 또 바뀌면 값과 글이 어긋난다.
- 권장: a. 대가: 위의 옮김이다. 권장대로 진행.

**5. 데모가 감시할 파일 자리.** 질문: 사람이 편집기로 열어 저장할 파일을 어디에 두는가.

- 길 a: 실행 파일 옆 `hot-reload/<실행 파일 이름>/`의 `style.json`·`markup.xml`, Android는 `internalDataPath/hot-reload/`다(이 문서). 없으면 데모가 내장 글로 만든다. 되는 것: 자리가 늘 같아 사람이 찾는다. 저장소 파일을 건드리지 않는다. 대가: 고친 파일이 다음 실행에도 남아 데모가 달라 보일 수 있다. 그래서 "내장 글을 파일에" 단추를 둔다.
- 길 b: 저장소의 원본(`examples/demo/assets/markup_sample.xml`, `LUIL_STYLE` 파일)을 감시한다. 되는 것: 고친 것이 곧 원본이다. 대가: 견본 단추가 저장소 파일을 고친다. 실기기와 설치본에는 소스 트리가 없다.
- 길 c: 실행마다 새 임시 폴더다([files_page.cpp](../examples/demo/files_page.cpp) 181행 꼴). 되는 것: 실행 사이에 남지 않는다. 대가: 경로가 실행마다 달라 사람이 상태 줄에서 옮겨 적는다.
- 권장: a. 대가: 위의 남는 파일이다. 권장대로 진행.

**6. 빌드 옵션.** 질문: 도우미를 CMake 옵션으로 끄게 하는가.

- 길 a: 늘 빌드한다(이 문서). 앱이 세울 때만 돈다. 되는 것: 새 의존이 없고(감시자와 두 파서는 이미 있다) 옵션 조합이 늘지 않는다. 배포판에서 끄는 것은 앱이 세우지 않는 것이다. 대가: 정적 라이브러리에 목적 파일이 하나 는다. 부르지 않으면 링크에서 빠진다.
- 길 b: `LUIL_ENABLE_HOT_RELOAD`(기본 OFF, 시험·데모 프리셋 ON). 되는 것: 끈 구성에 코드가 없다. 대가: SVG·Lottie처럼 끈 구성의 대신 파일이 들고, 켜지 않은 앱은 헤더를 보고도 링크가 막힌다.
- 길 c: Debug 빌드에서만 선다(`NDEBUG`면 `create`가 null). 되는 것: 배포판에서 실수로 돌지 않는다. 대가: 배포판에서 사용자 테마 파일을 읽으려는 앱을 막는다.
- 권장: a. 차트(확정 N5)와 달리 새 의존과 공개 헤더 묶음이 없고 부르지 않으면 정적 링크에서 빠지므로 옵션을 두지 않는다. 대가: 위의 목적 파일이다. 권장대로 진행.

**7. 묶음의 이름을 견주는 법(구현 때 정함).** 질문: 앱이 준 표기와 OS가 알린 표기가 대소문자만 다르면 그 파일로 보는가.

- 길 a: 플랫폼을 가리지 않고 접어서 견준다(`text::fold_case`, 이 문서). 되는 것: core에 플랫폼 분기가 없다. 표기가 다른 저장을 잃지 않는다. 대가: Android에서 대소문자만 다른 항목을 그 파일로 잘못 보면 한 번 더 읽는다. 읽기가 지금을 말하므로 결과는 틀리지 않는다.
- 길 b: 바이트로 견준다. 대가: Windows에서 `Style.json`을 `style.json`으로 건 앱의 저장이 조용히 사라진다.
- 길 c: `file` 거름을 믿고 견주지 않는다. 대가: 옛 이름 쪽과 새 이름 쪽을 가를 수 없다.
- 정함: a.

**8. 잠겨 읽지 못한 묶음(구현 뒤 정함).** 질문: 쓰는 쪽이 파일을 잠근 채 디스크를 비우고 100ms 넘게 쥐었다가 닫으면 무엇을 하는가. 비움이 알림을 잠긴 동안으로 앞당기고 닫을 때는 새 알림이 없다. 그 묶음을 `unreadable`로 건네고 참을 돌리면 마지막 저장이 읽히지 않는다(격리 데스크톱에서 잠금 300ms·700ms로 재현, 잠금 50ms나 비움 없는 잠금은 이어졌다). 어떤 편집기가 이렇게 쓰는지는 재지 않았다.

- 길 a: `unreadable`이면 약 2초 동안 거짓을 돌려 `settle`마다 다시 읽고, 상한이 지나면 건넨다(이 문서). 되는 것: 그 안에 풀리는 잠금은 저장을 잃지 않고 상태 줄이 "읽지 못함"을 거쳐 가지 않는다. 폴더처럼 다시 읽어도 같은 항목도 상한 뒤에 한 번 건네고 그쳐 묶음이 끝없이 돌지 않는다. 대가: 그런 항목의 상태가 상한만큼 늦게 뜨고, 그 사이 `settle`마다 한 번 열어 본다.
- 길 b: 그대로 두고 문서에 한계로 적어 앱이 `unreadable`을 받으면 다시 읽게 한다. 되는 것: 코드가 그대로다. 대가: 언제 다시 읽을지를 앱마다 시계로 푼다.
- 길 c: 열지 못한 경우만 다시 읽고 폴더 같은 항목은 바로 건넨다. 되는 것: 그 항목의 상태가 바로 뜬다. 대가: 읽기 함수가 실패의 종류를 따로 내는 길이 는다.
- 정함: a. 대가: 상한을 넘긴 잠금은 그대로 한계다. 건넨 뒤에 잠금이 풀려도 알림이 없으면 다음 저장까지 읽지 않는다.

## 플랫폼

**Win32.** 감시는 `ReadDirectoryChangesW`([file_watcher_win32.cpp](../src/win32/file_watcher_win32.cpp))이고 읽기는 UTF-8 경로의 `std::filesystem::path`와 `std::ifstream`이다. 편집기가 쓰는 동안 잠근 파일은 약 2초 동안 다시 읽어 본다(결정 8). 그 뒤에는 `unreadable`을 건네고, 풀린 뒤의 다음 저장이 다시 읽는다. 네트워크 드라이브의 한계는 [파일 감시](file-watch-design.md)의 것을 그대로 받는다.

**Android.** Java 소스 0줄, JNI 없음. 감시는 inotify([file_watcher_android.cpp](../src/android/file_watcher_android.cpp))이고 읽기는 Win32와 같은 표준 입출력이다. 확정 N6-android a대로 앱 전용 폴더의 파일만 보장한다. 경로는 앱이 `android_app`의 `activity->internalDataPath`에서 읽어 넘긴다. luil은 그 경로를 내지 않고 이 노드도 플랫폼 질의를 더하지 않는다. APK assets는 감시할 수 없다. 앱이 `AAssetManager`(NDK C API)로 직접 읽거나 첫 실행에 앱 폴더로 옮겨 쓴다. 데모는 내장 글을 `replace_file_atomically`로 앱 폴더에 써 둔다. 개발자는 디버그 빌드에서 `adb shell run-as <패키지>`로 그 폴더의 파일을 바꾼다. 공유 저장소(`/sdcard`)의 감시는 보장하지 않는다(파일 감시 문서). Android의 luil에는 접근성 계층이 없어 상태 줄은 사람이 화면으로 읽는다. unsupported인 부분은 없다.

## 다른 노드와의 약속

- **file_watch(R1)·style_sheet(R2)·markup(R4), 같은 줄.** [파일 감시](file-watch-design.md) 234~240행, [스타일 읽기](style-sheet-design.md) 227행의 다섯 약속, [마크업 바인딩](markup-binding-design.md) 227~228행을 위의 규칙으로 따른다. [스타일 읽기](style-sheet-design.md) 179행(결정 6, APK assets를 "hot_reload가 다시 본다")은 범위 밖 절의 APK assets 읽기와 확정 N6-android a로 닫는다. 감시를 따로 짓지 않고 `file_watch_scope::file`로 건다. hot_reload 커밋은 `file_watcher.h`·`ui_style_json.h`·`ui_markup.h`와 그 구현을 고치지 않는다.
- **i18n(R5, 줄 4).** (1) 문자열 표는 다시 읽지 않는다. 다시 읽은 마크업의 표 조회(`{t:key}` 같은 접두)도 펼칠 때 빌드한 표에서 찾는다. `{t:key}`는 [문자열 표](i18n-design.md) 결정 6을 따른다. a로 승인되면 줄 2가 hot_reload와 다른 커밋으로 `ui_markup_context` 끝에 글 조회 칸과 `t:` 접두 읽기, 그 test를 더한다(그 문서 결정 6 길 a의 꼴). 이번 라운드에 `ui_markup.h`·`src/ui/ui_markup_*.cpp`를 고치는 줄은 줄 2뿐이다. b면 이번 라운드에 그 파일들을 고치지 않는다. (2) Android 앱 폴더 경로 질의는 필요 없다. 앱이 `internalDataPath`를 읽고, 이 노드는 `platform_services.h`와 플랫폼 구현을 고치지 않는다. (3) 오류 줄은 영문 진단이라 `default_text`에 함수를 더하지 않는다.
- **chart(R5, 줄 1).** 겹치는 것이 목록 파일뿐이다. 차트는 core 밖의 선택 라이브러리라 이 노드의 헤더·소스와 닿지 않는다.
- **줄 3의 controller, 줄 4의 `win32_window.cpp`.** 고치지 않는다. 호스트는 지금처럼 `ui_frame::style`만 읽는다.
- **page_transition(R4).** [화면 전환](page-transition-design.md) 218행의 "다시 읽은 화면으로 바꿀 때 이 요소를 쓸 수 있다"는 쓰지 않는 것으로 닫는다. 다시 읽기는 같은 자리의 포인터를 바꾸고 id로 상태를 잇는 일이라 옛 화면과 새 화면이 따로 서지 않는다. 저장마다 움직임이 끼면 고친 곳을 견주기도 어렵다. 앱이 원하면 자기 전환 요소로 감싼다.
- **미룬 것을 닫는다.** 앞 라운드 문서가 이 노드로 미룬 줄이다. 두 줄은 같은 줄에 R5 i18n 절이 함께 있어 줄 4가 고친다. 같은 줄을 두 줄이 고치지 않게, 줄 4의 그 커밋이 아래 글로 hot_reload 절도 함께 닫는다.

| 위치 | 고치는 줄 | 넣을 글 |
| --- | --- | --- |
| [style-sheet-design.md](style-sheet-design.md) 267행 | 줄 2(이 노드의 구현 커밋) | "파일 읽기, 감시, 다시 읽기의 디바운스는 [핫 리로드](hot-reload-design.md)가 닫았다." 뒤의 APK assets 읽기는 그대로 둔다 |
| [markup-design.md](markup-design.md) 221행 | 줄 2(이 노드의 구현 커밋) | 위와 같은 글. APK assets 읽기는 그대로 둔다 |
| [markup-binding-design.md](markup-binding-design.md) 251행 | 줄 4([문자열 표](i18n-design.md)의 문서 손질) | "다시 읽기의 감시와 디바운스는 [핫 리로드](hot-reload-design.md)가 닫았다." |
| [page-transition-design.md](page-transition-design.md) 218행 | 줄 4([흡수 문서](i18n-adoption-design.md)의 R4 문서 손질) | "hot_reload는 이 요소를 쓰지 않는다. 다시 읽기는 같은 자리의 포인터를 바꾼다." |
- **목록 파일(같은 라운드 세 줄).** 자기 줄과 자기 묶음에만 더한다. `src/CMakeLists.txt`: core "app" 묶음 끝(`updater.h` 다음, 447행)에 헤더 한 줄, core "host" 묶음에 `host/hot_reload_load.cpp`·`.h`(`host/hotkey_sync.cpp` 앞, 이름 차례), Win32 "net/update" 묶음 뒤(707행 다음)와 Android "net/update" 묶음 뒤(906행 다음)에 `luil/hot_reloader.cpp` 한 줄짜리 새 묶음 둘. `tests/CMakeLists.txt`: core 목록에 `hot_reload_load_tests.cpp`(이름 차례), `luil_tests`의 새 `target_sources` 하나(546행 뒤), Android inotify 묶음(292~296행, `file_watcher_android.cpp`는 이미 든다) 뒤에 `luil/hot_reloader.cpp`와 시험 파일만 싣는 새 `target_sources` 하나. `include/luil/luil.h`: `updater.h` 다음(146행). 루트 `CMakeLists.txt`·`CMakePresets.json`·`examples/CMakeLists.txt`는 고치지 않는다.
- **데모는 따로 커밋한다.** 라이브러리 커밋(헤더, 소스 셋, 시험, 목록 파일)을 먼저 내고 데모 연결 커밋을 통합 때 직렬로 쌓는다. 고칠 데모 파일은 새 헤더 `examples/demo/hot_reload_sample.h`(두 데모가 함께 쓰는 상태 줄, 시작과 쓰기의 inline 함수), `examples/demo/theme_page.*`(메시지 처리와 상태 label만)·`style_sheet_sample.h`·`markup_sample.h`·`examples/demo_main.cpp`(도우미 세우기, 파일 쓰기, `bind_host`와 `stop_workers`에 한 줄씩), `examples/mobile_demo/theme_page.*`(메시지 처리, 상태 label, 셸만 부르는 inline 시작 함수)·`app.*`(파일 쓰기와 멈춤)·`android_main.cpp`·`main.cpp`, 그 데모를 읽는 `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`다. 대역 파일은 두지 않으므로 `tests/CMakeLists.txt`의 데모 묶음(175~210행)은 고치지 않는다.
- 구현 때 함께 고치는 문서: [스레드 모델](concepts/threading-model.md) 23행 곁에 한 문단(도우미는 thread를 두지 않고 감시자의 thread에서 읽는다), [테마](concepts/theming.md) 43행의 실행 시점 길에 파일 다시 읽기 한 갈래. 색인(`docs/README.md`)은 통합 때 붙인다.

## 확인

자동 test.

- `tests/hot_reload_load_tests.cpp`(`luil_core_tests`, Windows와 Android 기기, 새 파일): 임시 폴더의 파일로 `load_hot_reload_file`을 잰다. 바른 스타일은 같은 글의 `parse_ui_style`과 같은 값이고 `text`가 파일 글이다. 깨진 스타일은 `rejected`이고 `errors`가 `describe(style_errors[i])`와 같다. 없는 파일은 `missing`, 폴더는 `unreadable`, `max_bytes`를 1바이트 넘는 파일은 열지 않고 `too_large`다. 마크업도 같은 넷이고 크기는 두 한도 가운데 작은 쪽이다. 묶음 가르기는 위 표의 칸마다 한 사례(`end` 둘, `rescan`, 다시 읽을 셋, 없음 둘, 다른 이름의 항목만 든 묶음)다.
- `tests/hot_reloader_tests.cpp`(새 파일. Windows는 `luil_tests`, Android는 inotify 묶음이 이미 싣는 `file_watcher_android.cpp` 위에 `luil/hot_reloader.cpp`를 더한 `luil_core_tests`): `add`가 첫 결과를 돌려주고 그것은 `deliver`로 오지 않는다. 임시 파일 뒤 이름 바꾸기로 새 글을 쓰면 `loaded`가 한 번 온다. 깨진 글은 `rejected`, 지우면 `missing`, 다시 만들면 `loaded`다. `deliver`가 한 번 거짓을 돌리면 같은 파일의 결과가 다시 오고 그때 다시 읽은 글이다. 감시 이름에 폴더가 서면 `unreadable`이 한 번 오고 다시 읽지 않는다. Windows에서 잠근 채 쓰고 디스크를 비운 저장은 300ms 뒤에 닫으면 `unreadable` 없이 `loaded`이고, 상한을 넘겨 잠그면 `unreadable`이 한 번 오며 다음 저장이 읽는다. `remove` 뒤로 그 표의 결과가 없다. 감시 폴더를 지우면 `lost`가 한 번이다. 결과가 이어지는 중의 `stop`이 예산 안에 끝나고 그 뒤로 `deliver`가 없다. 임시 폴더는 [file_watcher_tests.cpp](../tests/file_watcher_tests.cpp) 53~72행의 `process_id`·`scratch_base`·`scratch_folder` 꼴이다. Windows는 `temp_directory_path` 아래, 기기에서는 `current_path`(adb_run.cmake의 기기 폴더) 아래에 둔다. 기기 셸에는 쓸 수 있는 임시 폴더가 정해져 있지 않아서다. 두 시험 파일은 그 도우미를 공용 헤더 `tests/hot_reload_test_folder.h`에서 함께 쓴다. `hot_reload_load_tests.cpp`가 core test에 들어 OS 헤더를 쓰지 않으므로 폴더 이름의 구별은 `process_id` 대신 `steady_clock` 값과 차례 번호다.
- 데모 시험: `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`가 테마 페이지에 `hot_reload_update` 메시지를 넣어 `theme-reload-status`의 글, 셸이 실을 스타일 포인터, `markup-status`를 본다. 파일과 감시자 없이 메시지만 넣는다.
- `luil_umbrella_header`, `luil_core_portability`, `luil_source_style`. `luil_default_text`는 지금 `src/ui`·`include/luil/ui`만 보아 이 노드의 파일(`include/luil/app`, `src/host`, `src/luil`)에 닿지 않는다. 줄 4가 범위를 `src`·`include` 전체로 넓혀도 이 노드의 코드 줄에는 한글이 없다.

데모. 상태를 label로 그려 `luil-uia`로 되읽는다.

- 페이지와 셸을 나눈다. 두 테마 페이지(`examples/demo/theme_page.cpp`, `examples/mobile_demo/theme_page.cpp`)는 `luil_core_tests`에 직접 컴파일되고([tests/CMakeLists.txt](../tests/CMakeLists.txt) 187·203행) 그 실행 파일은 `luil_core`만 링크한다. `hot_reloader::create`(`luil`)와 `replace_file_atomically`(`platform_services_win32.cpp`·`platform_services_android.cpp`)를 페이지가 부르면 Windows와 Android 모두 그 링크가 깨진다. 그래서 도우미를 세우고 파일에 쓰는 일은 core test에 들지 않는 셸이 한다. 데스크톱은 `demo_main.cpp`의 `demo_driver`다(`bind_host` 338행 곁에서 세우고, `stop_workers` 777행에서 멈추고, 쓰기는 `save_placement`가 `replace_file_atomically`를 부르는 280행 꼴). 모바일은 [files_page.h](../examples/mobile_demo/files_page.h) 84행의 `start_files_watch`처럼 셸만 부르는 inline 시작 함수를 `theme_page.h`에 두고 `android_main.cpp` 31행과 `main.cpp` 40행의 `on_started`가 부른다. 쓰기와 멈춤은 core test 밖의 `mobile_driver`(`app.cpp`)가 한다. 테마 페이지는 단추가 내는 "파일에 쓰기" intent와 `hot_reload_update` 메시지 처리, 상태 label만 다룬다. 그러면 대역 파일이 필요 없다. 두 셸이 같은 일을 하므로 시작(없는 파일 만들기, 세우기, `add`)과 쓰기는 `examples/demo/hot_reload_sample.h`의 inline 함수 하나씩이고, 모바일 `theme_page.h`의 시작 함수는 앱이 실은 글을 넘기는 한 줄이다.
- 데스크톱 테마 페이지의 "스타일 파일"·"마크업" 묶음 아래에 "파일 감시" 묶음을 둔다([화면 마크업](markup-design.md) 209행이 모은 자리). 감시자를 쥐는 꼴은 데스크톱 `files_page`의 것과 같다(host 포인터는 atomic, 멈춤은 `stop_workers`). 셸이 시작할 때 결정 5의 두 파일을 없으면 내장 글로 만들고 `add`해, 첫 결과를 다시 읽은 결과와 같은 메시지로 logic에 넘긴다.
- 받은 결과는 스타일 묶음(`style_sheet_state`가 포인터를 받는 새 입구, 칸 글, `theme-style-status`)과 마크업 묶음(쥔 틀, `markup-status`)에 그대로 간다. 셸은 지금처럼 그 포인터를 frame과 배치에 싣는다. 값이 없는 결과는 `theme-reload-status`만 바꾼다. `theme-style-status`·`markup-status`는 쥔 값을 말하므로 그대로다. 칸 글은 `loaded`·`rejected`에서 파일 글로 바뀌어 고칠 곳을 칸에서 본다.
- 첫 결과도 같은 메시지라 파일이 있으면 첫 frame부터 `theme-style-status`가 "적용됨"이다(결정 2 a). "내장값"은 `theme-style-reset`을 누른 뒤에만 보인다.
- label `theme-reload-path`(감시하는 폴더), label `theme-reload-status`("다시 읽기 3번 · style.json 받음", "다시 읽기 4번 · style.json 거부 · Unknown key in dark: windowBackgrund · 이전 값 유지", "markup.xml 없음 · 이전 값 유지"). 단추 `theme-reload-write-style`("견본 스타일을 파일에", 창 바탕을 바꾼 견본), `theme-reload-write-style-broken`("깨진 스타일을 파일에", 괄호를 뺀 견본), `theme-reload-write-markup-broken`("깨진 마크업을 파일에", 닫지 않은 태그 견본), `theme-reload-write-default`("내장 글을 파일에"). 단추는 intent만 내고 셸이 `replace_file_atomically`로 쓴다. 화면은 감시를 거쳐서만 바뀐다.
- 모바일 데모 테마 페이지에 같은 label과 단추를 둔다. delegate가 `on_started`에서 위의 inline 시작 함수로 도우미를 세우고 logic이 `stop_workers`에서 멈춘다(지금의 감시자, `android_main.cpp` 41~43행과 같은 자리). Windows 창으로 띄운 모바일 데모(`main.cpp`)도 같다.
- UIA(격리 데스크톱): `theme-reload-write-style`을 Invoke하고 `theme-reload-status`가 "받음"을, `theme-style-status`가 견본의 창 바탕을 말하는지 읽는다. 깨진 견본 둘 뒤에는 "거부"와 "이전 값 유지"이고 `theme-style-status`·`markup-status`가 그 앞의 값 그대로인지, `theme-reload-write-default` 뒤에 내장값으로 돌아오는지 읽는다. 모바일 데모(Windows 창)도 같은 label을 읽는다.

사람 확인.

- 데스크톱 데모를 띄우고 `style.json`·`markup.xml`을 메모장, VS Code, Visual Studio로 열어 고치고 저장한다. 저장마다 다음 frame에 화면이 바뀌는지, 고친 마크업 가지의 초점과 누른 횟수가 남는지, 틀린 글을 저장하면 상태 줄이 고칠 자리를 가리키고 화면은 그대로인지 본다.
- 실기기 모바일 데모에서 디버그 빌드로 `adb shell run-as io.github.yuldk.luil.mobile_demo`를 거쳐 앱 폴더의 파일을 바꾸고 화면과 상태 줄을 눈으로 읽는다.

이 PC에서 데모가 쓸 파일 자리다. 구현 뒤 데모가 처음 뜰 때 생긴다.

| 데모 | 파일 |
|---|---|
| 데스크톱 데모(`build/vs2026-examples/examples/Debug/luil_demo.exe`) | `build/vs2026-examples/examples/Debug/hot-reload/luil_demo/style.json`·`markup.xml` |
| 시험 프리셋의 같은 데모(`build/vs2026-tests/examples/Debug/luil_demo.exe`) | `build/vs2026-tests/examples/Debug/hot-reload/luil_demo/` 아래 같은 둘 |
| 모바일 데모 Windows 창(`luil_mobile_demo.exe`, 같은 폴더) | `hot-reload/luil_mobile_demo/` 아래 같은 둘 |
| 모바일 데모 실기기 | `internalDataPath`(보통 `/data/user/0/io.github.yuldk.luil.mobile_demo/files`) 아래 `hot-reload/` |

## 범위 밖

- 코드(DLL·.so) 다시 읽기. luil은 정적 라이브러리로만 짓고([src/CMakeLists.txt](../src/CMakeLists.txt) 712·758·908행) Android 앱은 SHARED .so 하나다. 화면 코드를 고치며 바로 보려면 바깥 도구를 쓴다. Visual Studio의 C++ Hot Reload, Live++ 같은 도구가 실행 중 바이너리를 고친다고 알려져 있다. 이 저장소의 빌드(Skia와 같은 정적 CRT, [빌드 체계](concepts/build-system.md) 30행)와 맞는지는 재지 않았다. Android에는 안내할 도구가 없다.
- 문자열 표 다시 읽기(확정 G6 범위 B). 번역 글을 고치면 다시 빌드한다.
- APK assets 읽기와 감시(확정 N6-android a). 공유 저장소 감시의 보장.
- 폴더째 감시(새 파일이 생기면 읽기), 한 파일이 다른 파일을 끌어오는 include. 파일 하나가 스타일이나 틀 하나다.
- 다시 읽을 때의 화면 전환 움직임, 라이브러리가 그리는 오류 띠. 앱이 `errors`로 그린다.

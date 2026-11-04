#pragma once

#include "luil/app/file_watcher.h"
#include "luil/theme/ui_style_json.h"
#include "luil/ui/ui_markup.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 스타일 JSON과 화면 마크업 파일을 저장하면 다음 frame에 반영하는 길이다 (docs/hot-reload-design.md).
//  - 도우미 하나가 `file_watcher`를 쥐고, 바뀐 파일을 감시 thread에서 읽어 순수 파서로 해석한 뒤 결과만 앱에 건넨다.
//  - 앱은 결과를 자기 앱 메시지로 logic thread에 옮겨 쥔 스타일과 틀을 바꾼다. 실패한 결과에는 값이 없어 쥔 것이 그대로 남는다.
//  - 코드(DLL·.so)와 문자열 표는 다시 읽지 않는다. Android는 앱 전용 폴더의 파일만 보장한다. APK assets는 앱이 직접 읽는다.
namespace luil {
    enum class hot_reload_kind
    {
        // `parse_ui_style`로 읽는다.
        style,
        // `parse_ui_markup`으로 읽는다.
        markup,
    };

    enum class hot_reload_status
    {
        // 새 값이 있다.
        loaded,
        // 해석이 막혔다. 값이 비고 `errors`가 이유다. 반쯤 쓴 파일도 여기다 (syntax).
        rejected,
        // 열거나 읽지 못했다 (잠김, 읽는 사이에 지워짐, 일반 파일이 아님).
        unreadable,
        // `max_bytes`를 넘어 열지 않았다.
        too_large,
        // 지금 없다 (removed, 옛 이름이 그 파일인 renamed, 처음 읽을 때 없음).
        missing,
        // 감시가 끊겼다. 이 표로는 더 오지 않는다.
        lost,
    };

    struct hot_reload_file
    {
        // UTF-8이다. `file_watch_scope::file`로 걸어 파일이 아직 없어도 된다.
        std::u8string path {};
        hot_reload_kind kind { hot_reload_kind::style };
    };

    struct hot_reload_limits
    {
        // 파일의 바이트 수다. 넘으면 열지 않는다. 스타일 파서에는 크기 한도가 없어 여기서 정한다.
        std::size_t max_bytes { std::size_t { 1 } * 1024 * 1024 };
        // 마크업 해석 한도다. 크기는 `max_bytes`와 `markup.max_bytes` 가운데 작은 쪽이다.
        ui_markup_limits markup {};
    };

    // 파일 하나를 한 번 읽은 결과다. 앱이 자기 앱 메시지에 담아 logic thread로 옮긴다.
    struct hot_reload_update
    {
        // `add`가 받은 표다. `load_hot_reload_file`은 비었다.
        file_watch_ticket ticket {};
        std::u8string path {};
        hot_reload_kind kind { hot_reload_kind::style };
        hot_reload_status status { hot_reload_status::missing };
        // style이고 loaded일 때만 있다. 불변이라 thread 사이에 나눠 쥔다.
        std::shared_ptr<const ui_style> style {};
        // markup이고 loaded일 때만 있다.
        std::shared_ptr<const ui_markup_document> markup {};
        // 읽은 글이다. loaded·rejected일 때만 있다. 편집 칸에 띄우거나 오류의 자리를 그 글에서 짚는다.
        std::u8string text {};
        // 개발자용 영문 한 줄들이다. 파서 오류는 `describe` 그대로, 읽기 오류는 한 줄이다. loaded면 비었다.
        std::vector<std::u8string> errors {};
        // 파서가 준 그대로다 (자리, 키, offset).
        std::vector<style_error> style_errors {};
        std::vector<ui_markup_error> markup_errors {};
    };

    // 감시 없이 한 번 읽는다. 부른 thread에서 읽고, 전역 상태가 없어 어느 thread에서나 부른다. 던지지 않고 실패를 결과로 돌린다.
    //  - 감시하지 않는 배포판이 같은 파일을 시작할 때 한 번 읽는 자리다. 도우미 안의 읽기와 같은 함수라 오류 줄도 같다.
    [[nodiscard]] hot_reload_update load_hot_reload_file(const hot_reload_file& file, const hot_reload_limits& limits = {});

    struct hot_reloader_config
    {
        // 결과를 앱에 건네는 유일한 자리다. **감시 thread에서 불린다.** 받았으면 참이다.
        //
        //     config.deliver = [&host](luil::hot_reload_update update) {
        //         return host.post_app_message_ticketed(luil::app_message { std::move(update) }) != 0;
        //     };
        //
        //  - 거짓이면 감시자가 그 묶음을 다시 내고, 그때 파일을 다시 읽는다.
        //  - 여기서 `add`·`remove`는 되고 `stop`은 안 된다.
        //  - 던지면 삼키고 거절로 본다 (`file_watcher_config::deliver`와 같다).
        //  - 비어 있으면 파일을 읽지 않고 묶음을 받은 것으로 본다.
        std::function<bool(hot_reload_update)> deliver {};
        // `file_watcher_config`로 그대로 간다 (디바운스, 묶음 늦춤 상한, 종료 예산).
        std::chrono::milliseconds settle { 100 };
        std::chrono::milliseconds max_delay { 1000 };
        std::chrono::milliseconds stop_budget { 1000 };
        hot_reload_limits limits {};
    };

    // 등록한 스타일·마크업 파일을 감시하고, 바뀌면 다시 읽어 `deliver`로 건넨다.
    //  - thread를 따로 두지 않는다. `file_watcher` 하나를 쥐고 읽기와 해석은 그 감시 thread의 `deliver` 안에서 한다.
    //  - 이전 값을 쥐지 않는다. 실패한 결과에는 값이 없고, 쥔 것을 지키는 일은 앱이 한다.
    //  - 종료는 앱의 종료 순서에 낀다. `logic_driver::stop_workers()`에서 `stop()`을 부른다. `deliver`가 쥔 host보다 먼저다.
    //  - 모든 메서드는 어느 thread에서든 부를 수 있다.
    class hot_reloader
    {
    public:
        hot_reloader(const hot_reloader&) = delete;
        hot_reloader(hot_reloader&&) = delete;
        hot_reloader& operator=(const hot_reloader&) = delete;
        hot_reloader& operator=(hot_reloader&&) = delete;
        // `stop()`을 부른다.
        ~hot_reloader();

        // 실패하면 null이고 `error`에 이유를 적는다 (`file_watcher::create`와 같은 규약이다).
        [[nodiscard]] static std::unique_ptr<hot_reloader> create(hot_reloader_config configuration, std::u8string& error);

        // 감시를 걸고 그다음 그 자리에서 한 번 읽어 첫 결과를 돌려준다. 첫 결과는 `deliver`로 오지 않는다.
        //  - 감시를 걸지 못하면(부모 폴더가 없음, OS 거절) `ticket`이 비고 `error`에 적는다. 읽은 결과는 그래도 담는다.
        [[nodiscard]] hot_reload_update add(const hot_reload_file& file, std::u8string& error);
        // 감시를 내린다. 남은 묶음은 거두고 앱에 건네지 않는다. 모르는 표는 아무 일도 하지 않는다.
        //  - 이미 `deliver`에 들어간 결과는 거두지 못한다. 앱 메시지로 떠났을 수도 있으므로 앱은 `ticket`으로 가린다.
        void remove(file_watch_ticket ticket) noexcept;
        // 멈춘다. 멱등이고 어느 thread에서든 부른다 (`deliver` 안은 안 된다). 차례는 `file_watcher::stop` 그대로다.
        //  - 읽던 파일 하나는 끝까지 읽는다. 종료 예산에 `max_bytes` 한 번 읽기와 해석이 `stop_budget`과 함께 든다.
        void stop() noexcept;

    private:
        struct engine;
        explicit hot_reloader(std::unique_ptr<engine> owned) noexcept;

        std::unique_ptr<engine> engine_ {};
    };
}

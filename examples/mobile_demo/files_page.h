#pragma once

// 파일 감시 페이지다: 앱 전용 폴더의 파일 하나를 만들고 고치고 지우고 이름을 바꾸고, `file_watcher`가 낸 마지막 묶음을
// label에 그린다 (file-watch-design.md의 "확인"). 화면과 UIA Name으로 묶음을 되읽는 자리다.
//  - 감시자는 delegate가 `on_started`에서 세워 `file_watcher_intent`로 넘긴다 (SMB 페이지와 같은 길이다). 폴더는 Android가
//    `internalDataPath` 아래, Windows 창이 임시 폴더 아래다.
//  - 페이지가 쥔 것은 감시자를 멈추는 함수뿐이다. 감시자 구현 없이 묶은 core test가 같은 메시지로 페이지 상태를 본다.

#include "luil/app/app_host.h"
#include "luil/app/file_watcher.h"
#include "mobile_demo/common.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace mobile_demo {
    // 이 페이지의 키다. 공용 등록부(common.h)를 고치지 않으려고 페이지가 든다.
    inline constexpr std::u8string_view page_files { u8"files" };

    // --- 이 페이지의 메시지 ---
    enum class files_operation
    {
        create,
        modify,
        remove,
        rename,
    };

    // 단추 하나다. 감시 폴더의 파일을 바꾼다.
    struct files_operation_intent
    {
        files_operation operation { files_operation::create };
    };

    // delegate가 감시를 건 결과다. 걸지 못했으면 표가 비고 `error`가 이유다.
    struct file_watcher_intent
    {
        std::u8string folder {};
        luil::file_watch_ticket ticket {};
        std::u8string error {};
        // 감시자를 멈춘다. `stop_workers`가 부른다.
        std::function<void()> stop {};
    };

    // 감시자가 낸 묶음이다 (감시 thread의 `deliver`가 보낸다).
    struct files_batch_intent
    {
        luil::file_change_batch batch {};
    };

    // 묶음 하나를 한 줄로 적는다 ("변경: created a.txt, renamed a.txt→b.txt, rescan").
    [[nodiscard]] std::u8string describe_file_change_batch(const luil::file_change_batch& batch);

    class files_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 감시자를 멈춘다. 묶음이 앱 host를 거쳐 오므로 host보다 먼저 부른다 (`stop_workers`).
        void stop()
        {
            if (stop_ != nullptr)
                stop_();
        }

    private:
        void run(files_operation operation);

        std::u8string folder_ {};
        luil::file_watch_ticket ticket_ {};
        std::function<void()> stop_ {};
        std::u8string state_ { u8"감시: 아직 걸지 않음" };
        std::u8string text_ { u8"변경: 아직 없음" };
    };

    // delegate가 `on_started`에서 부른다. 감시자를 세우고 폴더를 건 결과를 driver에 넘긴다. 감시자는 delegate가 host보다 오래 쥔다.
    //  - inline인 것은 페이지 소스만 묶는 core test가 감시자 구현 없이 링크되게 하려는 것이다. 부르는 진입점만 구현을 링크한다.
    [[nodiscard]] inline std::unique_ptr<luil::file_watcher> start_files_watch(luil::app_host& host, const std::filesystem::path& folder)
    {
        file_watcher_intent intent {};
        intent.folder = folder.u8string();
        std::error_code ignored {};
        std::filesystem::create_directories(folder, ignored);
        luil::file_watcher_config config {};
        // 감시 thread에서 불린다. inbox가 받았는지를 답해야 넘쳐도 감시자가 그 묶음을 다시 낸다.
        config.deliver = [&host](luil::file_change_batch batch) { return host.post_app_message_ticketed(luil::app_message { files_batch_intent { std::move(batch) } }) != 0; };
        std::unique_ptr<luil::file_watcher> watcher { luil::file_watcher::create(std::move(config), intent.error) };
        if (watcher != nullptr)
        {
            intent.ticket = watcher->add(intent.folder, luil::file_watch_scope::folder, intent.error);
            luil::file_watcher* const raw { watcher.get() };
            intent.stop = [raw] { raw->stop(); };
        }
        host.post_app_message(luil::app_message { std::move(intent) });
        return watcher;
    }
}

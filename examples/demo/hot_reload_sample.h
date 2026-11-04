#pragma once

// 데모의 "파일 감시" 묶음이다 (docs/hot-reload-design.md의 확인).
//  - 셸이 `hot_reloader`로 두 파일을 감시하고 결과(`luil::hot_reload_update`)를 그대로 앱 메시지로 logic에 넘긴다. 테마 페이지가
//    받아 쥔 스타일과 틀을 바꾼다. 데스크톱 demo와 모바일 demo, page test가 이 파일 하나를 함께 쓴다 (style_sheet_sample.h처럼).
//  - 단추는 intent만 낸다. 셸이 `replace_file_atomically`로 쓰고, 화면은 감시를 거쳐서만 바뀐다.
//  - 상태 줄(`theme-reload-status`)이 결과를 말한다. UIA Name과 화면으로 되읽는다.
//  - 시작과 쓰기 함수가 inline인 것은 페이지 소스만 묶는 core test가 도우미와 플랫폼 구현 없이 링크되게 하려는 것이다.
//    부르는 셸만 구현을 링크한다 (mobile_demo/files_page.h의 `start_files_watch`와 같다).

#include "demo/markup_sample.h"
#include "demo/style_sheet_sample.h"
#include "luil/app/app_host.h"
#include "luil/app/hot_reload.h"
#include "luil/app/platform_services.h"
#include "luil/ui/app_message.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace demo {
    inline constexpr std::u8string_view hot_reload_style_file { u8"style.json" };
    inline constexpr std::u8string_view hot_reload_markup_file { u8"markup.xml" };

    // 파일 감시 묶음의 단추가 파일에 쓰는 것이다.
    enum class hot_reload_write
    {
        // 창 바탕과 모서리를 바꾼 스타일이다. 받는다.
        style_sample,
        // 닫는 괄호가 없는 스타일이다. 막혀 이전 스타일이 남는다.
        style_broken,
        // 뿌리의 닫는 태그가 없는 마크업이다. 막혀 이전 틀이 남는다.
        markup_broken,
        // 내장 글 둘이다. 고친 파일이 다음 실행에도 남으므로 되돌리는 자리다.
        defaults,
    };

    struct hot_reload_write_intent
    {
        hot_reload_write what { hot_reload_write::defaults };
    };

    // 셸이 감시를 세운 결과다. 걸지 못했으면 `error`가 이유다.
    struct hot_reload_started_intent
    {
        std::u8string folder {};
        std::u8string error {};
        // 도우미를 멈추는 함수다. 도우미를 쥔 쪽이 `stop_workers`에서 직접 멈추지 못할 때 쓴다 (모바일의 delegate).
        std::function<void()> stop {};
    };

    struct hot_reload_file_text
    {
        std::u8string_view name {};
        std::u8string text {};
    };

    // 단추 하나가 쓸 파일과 글이다. 견본의 바탕은 데모 빌드가 실은 글이다.
    [[nodiscard]] inline std::vector<hot_reload_file_text> hot_reload_write_plan(const hot_reload_write what, const std::u8string_view style_source, const std::u8string_view markup_source)
    {
        std::vector<hot_reload_file_text> files {};
        switch (what)
        {
        case hot_reload_write::style_sample:
            files.push_back({ hot_reload_style_file, style_sheet_sample(style_source, style_sheet_action::sample_background) });
            break;
        case hot_reload_write::style_broken:
            files.push_back({ hot_reload_style_file, style_sheet_sample(style_source, style_sheet_action::sample_broken) });
            break;
        case hot_reload_write::markup_broken:
            files.push_back({ hot_reload_markup_file, markup_sample(markup_source, markup_sample_action::sample_broken) });
            break;
        case hot_reload_write::defaults:
            files.push_back({ hot_reload_style_file, std::u8string { style_source } });
            files.push_back({ hot_reload_markup_file, std::u8string { markup_source } });
            break;
        }
        return files;
    }

    // 파일 감시 묶음의 상태 줄이다. 쥔 스타일과 틀은 페이지가 바꾼다.
    class hot_reload_state
    {
    public:
        void started(const hot_reload_started_intent& intent)
        {
            folder_ = intent.folder;
            stop_ = intent.stop;
            if (intent.error.empty() == false)
                status_ = u8"감시를 걸지 못함 · " + intent.error;
        }

        // 값이 없는 결과는 "이전 값 유지"를 말한다. 파일이 받아졌는지와 고칠 자리를 이 줄에서 읽는다.
        void note(const luil::hot_reload_update& update)
        {
            const std::u8string name { update.kind == luil::hot_reload_kind::markup ? hot_reload_markup_file : hot_reload_style_file };
            const std::u8string reason { update.errors.empty() ? std::u8string {} : u8" · " + update.errors.front() };
            switch (update.status)
            {
            case luil::hot_reload_status::loaded:
                ++reloads_;
                status_ = u8"다시 읽기 " + count_text(reloads_) + u8"번 · " + name + u8" 받음";
                return;
            case luil::hot_reload_status::rejected:
                ++reloads_;
                status_ = u8"다시 읽기 " + count_text(reloads_) + u8"번 · " + name + u8" 거부" + reason + u8" · 이전 값 유지";
                return;
            case luil::hot_reload_status::missing:
                status_ = name + u8" 없음 · 이전 값 유지";
                return;
            case luil::hot_reload_status::unreadable:
                status_ = name + u8" 읽지 못함" + reason + u8" · 이전 값 유지";
                return;
            case luil::hot_reload_status::too_large:
                status_ = name + u8" 너무 큼" + reason + u8" · 이전 값 유지";
                return;
            case luil::hot_reload_status::lost:
                // 끊긴 감시는 다시 오지 않는다. 쓰기 단추가 폴더를 되살려도 이 실행에서는 화면에 닿지 않으므로 되살리는 길을 말한다.
                status_ = name + u8" 감시 끊김" + reason + u8" · 이전 값 유지 · 앱을 다시 띄우면 다시 감시한다";
                return;
            }
        }

        // 셸이 파일에 쓰지 못했다. 쓴 파일은 감시를 거쳐 상태 줄에 오므로 실패만 여기로 온다.
        void note_write_failure(const std::u8string_view error)
        {
            status_ = u8"파일에 쓰지 못함 · " + std::u8string { error };
        }

        [[nodiscard]] const std::u8string& folder() const noexcept
        {
            return folder_;
        }

        [[nodiscard]] const std::u8string& status() const noexcept
        {
            return status_;
        }

        // 도우미를 멈춘다. 결과가 앱 host를 거쳐 오므로 host보다 먼저 부른다 (`stop_workers`).
        void stop()
        {
            if (stop_ != nullptr)
                stop_();
        }

    private:
        [[nodiscard]] static std::u8string count_text(const std::size_t count)
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), count) };
            return { reinterpret_cast<const char8_t*>(buffer.data()), static_cast<std::size_t>(result.ptr - buffer.data()) };
        }

        std::u8string folder_ {};
        std::u8string status_ { u8"아직 감시하지 않습니다." };
        std::size_t reloads_ { 0 };
        std::function<void()> stop_ {};
    };

    [[nodiscard]] inline luil::platform_result write_hot_reload_file(const std::filesystem::path& path, const std::u8string_view text)
    {
        return luil::replace_file_atomically(path.u8string(), { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
    }

    // 셸이 단추의 intent를 받아 부른다. 실패한 파일의 이름과 이유를 돌려주고, 다 썼으면 비었다.
    [[nodiscard]] inline std::u8string write_hot_reload_files(const std::u8string_view folder, const hot_reload_write what, const std::u8string_view style_source, const std::u8string_view markup_source)
    {
        if (folder.empty())
            return u8"감시 폴더가 없습니다.";
        // 폴더를 통째로 지운 뒤에도 쓸 수 있어야 다음 실행의 감시가 그 글을 읽는다. 만들지 못하면 아래 쓰기가 이유를 돌려준다.
        std::error_code ignored {};
        std::filesystem::create_directories(std::filesystem::path { folder }, ignored);
        for (const hot_reload_file_text& file : hot_reload_write_plan(what, style_source, markup_source))
        {
            const luil::platform_result result { write_hot_reload_file(std::filesystem::path { folder } / std::filesystem::path { file.name }, file.text) };
            if (result.ok() == false)
                return std::u8string { file.name } + u8" · " + result.error;
        }
        return {};
    }

    // 셸이 host를 받은 자리(`on_started`)에서 부른다. 없는 파일을 내장 글로 만들고 도우미를 세워 두 파일을 건다.
    //  - 첫 결과도 다시 읽은 결과와 같은 메시지로 logic에 넘긴다. 사람이 고쳐 둔 파일이 첫 frame부터 화면에 선다.
    //  - 돌려준 도우미는 셸이 host보다 오래 쥐고 `stop_workers`에서 멈춘다.
    [[nodiscard]] inline std::unique_ptr<luil::hot_reloader> start_hot_reload(luil::app_host& host, const std::filesystem::path& folder, const std::u8string_view style_source, const std::u8string_view markup_source)
    {
        hot_reload_started_intent started {};
        if (folder.empty())
        {
            started.error = u8"감시할 폴더를 찾지 못했습니다.";
            host.post_app_message(luil::app_message { std::move(started) });
            return nullptr;
        }
        started.folder = folder.u8string();
        std::error_code ignored {};
        std::filesystem::create_directories(folder, ignored);
        const std::array<luil::hot_reload_file, 2> files {
            luil::hot_reload_file { (folder / std::filesystem::path { hot_reload_style_file }).u8string(), luil::hot_reload_kind::style },
            luil::hot_reload_file { (folder / std::filesystem::path { hot_reload_markup_file }).u8string(), luil::hot_reload_kind::markup },
        };
        const std::array<std::u8string_view, 2> built_in { style_source, markup_source };
        for (std::size_t index { 0 }; index < files.size(); ++index)
        {
            const std::filesystem::path path { files[index].path };
            if (std::filesystem::exists(path, ignored) == false)
                static_cast<void>(write_hot_reload_file(path, built_in[index]));
        }

        luil::hot_reloader_config config {};
        // 감시 thread에서 불린다. inbox가 받았는지를 답해야 넘쳐도 감시자가 그 묶음을 다시 내고 그때 파일을 다시 읽는다.
        config.deliver = [&host](luil::hot_reload_update update) { return host.post_app_message_ticketed(luil::app_message { std::move(update) }) != 0; };
        std::unique_ptr<luil::hot_reloader> reloader { luil::hot_reloader::create(std::move(config), started.error) };
        if (reloader != nullptr)
        {
            luil::hot_reloader* const raw { reloader.get() };
            started.stop = [raw] { raw->stop(); };
            for (const luil::hot_reload_file& file : files)
                host.post_app_message(luil::app_message { reloader->add(file, started.error) });
        }
        // 첫 결과 뒤에 보낸다. 걸지 못한 이유가 상태 줄에 남는다.
        host.post_app_message(luil::app_message { std::move(started) });
        return reloader;
    }
}

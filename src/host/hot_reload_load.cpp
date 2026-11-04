#include "host/hot_reload_load.h"

#include "luil/text/case_fold.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace luil {
    namespace {
        enum class batch_action
        {
            discard,
            reload,
            missing,
        };

        [[nodiscard]] std::u8string number_text(const std::uintmax_t value)
        {
            const std::string text { std::to_string(value) };
            return { text.begin(), text.end() };
        }

        [[nodiscard]] hot_reload_update make_failure(const hot_reload_file& file, const hot_reload_status status, std::u8string message)
        {
            hot_reload_update update {};
            update.path = file.path;
            update.kind = file.kind;
            update.status = status;
            update.errors.push_back(std::move(message));
            return update;
        }

        // 오류 줄과 묶음 가르기가 쓰는 이름이다. 깨진 UTF-8은 경로로 옮길 수 없어 빈 이름이다.
        [[nodiscard]] std::u8string file_name_of(const std::u8string_view path)
        {
            try
            {
                return std::filesystem::path { path }.filename().u8string();
            }
            catch (const std::exception&)
            {
                return {};
            }
        }

        void parse_style(hot_reload_update& update)
        {
            style_parse_result parsed { parse_ui_style(update.text) };
            if (parsed.ok())
            {
                update.status = hot_reload_status::loaded;
                update.style = std::make_shared<const ui_style>(std::move(*parsed.style));
                return;
            }
            update.status = hot_reload_status::rejected;
            for (const style_error& error : parsed.errors)
                update.errors.push_back(describe(error));
            update.style_errors = std::move(parsed.errors);
        }

        void parse_markup(hot_reload_update& update, const ui_markup_limits& limits)
        {
            ui_markup_parse_result parsed { parse_ui_markup(update.text, limits) };
            if (parsed.ok())
            {
                update.status = hot_reload_status::loaded;
                update.markup = std::move(parsed.document);
                return;
            }
            update.status = hot_reload_status::rejected;
            for (const ui_markup_error& error : parsed.errors)
                update.errors.push_back(describe(error));
            update.markup_errors = std::move(parsed.errors);
        }

        [[nodiscard]] hot_reload_update read_file(const hot_reload_file& file, const hot_reload_limits& limits)
        {
            const std::u8string name { file_name_of(file.path) };
            const std::u8string shown { name.empty() ? file.path : name };
            // OS API가 NUL에서 끊어 다른 파일을 읽는다.
            if (file.path.find(u8'\0') != std::u8string::npos)
                return make_failure(file, hot_reload_status::unreadable, u8"Invalid file path.");
            const std::filesystem::path path { file.path };
            std::error_code error {};
            // 크기를 먼저 잰다. 없는 것과 재지 못한 것을 가른다.
            const std::filesystem::file_status state { std::filesystem::status(path, error) };
            if (state.type() == std::filesystem::file_type::not_found)
                return make_failure(file, hot_reload_status::missing, u8"File not found: " + shown);
            if (error)
                return make_failure(file, hot_reload_status::unreadable, u8"Could not open file: " + shown);
            if (std::filesystem::is_regular_file(state) == false)
                return make_failure(file, hot_reload_status::unreadable, u8"Not a regular file: " + shown);
            const std::uintmax_t size { std::filesystem::file_size(path, error) };
            if (error)
                return make_failure(file, hot_reload_status::unreadable, u8"Could not open file: " + shown);
            const std::size_t limit { file.kind == hot_reload_kind::markup ? std::min(limits.max_bytes, limits.markup.max_bytes) : limits.max_bytes };
            if (size > limit)
                return make_failure(file, hot_reload_status::too_large, u8"File too large: " + shown + u8" (" + number_text(size) + u8" bytes, limit " + number_text(limit) + u8")");

            std::ifstream stream { path, std::ios::binary };
            if (stream.is_open() == false)
                return make_failure(file, hot_reload_status::unreadable, u8"Could not open file: " + shown);
            std::u8string text(static_cast<std::size_t>(size), u8'\0');
            stream.read(reinterpret_cast<char*>(text.data()), static_cast<std::streamsize>(size));
            // 읽은 바이트가 잰 크기와 다르면 쓰는 중인 파일이다. 다음 묶음이 다시 읽는다.
            if (static_cast<std::uintmax_t>(stream.gcount()) != size || stream.peek() != std::ifstream::traits_type::eof())
                return make_failure(file, hot_reload_status::unreadable, u8"File changed while reading: " + shown);

            hot_reload_update update {};
            update.path = file.path;
            update.kind = file.kind;
            update.text = std::move(text);
            // 바이트를 그대로 넘긴다. 글의 꼴(BOM, 줄 끝, 엄한 문법)은 파서의 규칙이다.
            if (file.kind == hot_reload_kind::markup)
                parse_markup(update, limits.markup);
            else
                parse_style(update);
            return update;
        }

        [[nodiscard]] batch_action classify(const file_change_batch& batch, const std::u8string_view name)
        {
            if (batch.rescan)
                return batch_action::reload;
            // Windows의 이름은 대소문자를 가리지 않아 OS는 디스크의 표기로 알린다. 표기가 다른 저장을 잃으면 안 된다.
            // 대소문자만 다른 항목을 그 파일로 잘못 봐도 한 번 더 읽을 뿐이라 플랫폼을 가리지 않고 접어서 견준다.
            const std::u8string wanted { text::fold_case(name) };
            batch_action action { batch_action::discard };
            for (const file_change& change : batch.changes)
            {
                if (text::fold_case(change.path) == wanted)
                {
                    // 대소문자만 바꾼 이름 바꿈은 옛 이름 쪽 항목과 함께 올 수 있다. 읽는 쪽이 파일의 지금을 말한다.
                    if (change.kind != file_change_kind::removed)
                        return batch_action::reload;
                    action = batch_action::missing;
                }
                else if (change.kind == file_change_kind::renamed && text::fold_case(change.old_path) == wanted)
                    action = batch_action::missing;
            }
            return action;
        }
    }

    hot_reload_update load_hot_reload_file(const hot_reload_file& file, const hot_reload_limits& limits)
    {
        try
        {
            return read_file(file, limits);
        }
        catch (const std::exception&)
        {
            // 경로로 옮길 수 없는 글, 메모리 부족이다.
            return make_failure(file, hot_reload_status::unreadable, u8"Could not read the file.");
        }
    }

    std::optional<hot_reload_update> read_hot_reload_batch(const file_change_batch& batch, const hot_reload_file& file, const hot_reload_limits& limits)
    {
        std::optional<hot_reload_update> update {};
        if (batch.end == file_watch_end::removed)
            return update;
        if (batch.end == file_watch_end::lost)
            update = make_failure(file, hot_reload_status::lost, batch.error);
        else
        {
            const std::u8string name { file_name_of(file.path) };
            const batch_action action { classify(batch, name) };
            if (action == batch_action::discard)
                return update;
            if (action == batch_action::reload)
                update = load_hot_reload_file(file, limits);
            else
                update = make_failure(file, hot_reload_status::missing, u8"File not found: " + (name.empty() ? file.path : name));
        }
        update->ticket = batch.ticket;
        return update;
    }
}

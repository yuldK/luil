#include "mobile_demo/files_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include <fstream>
#include <string_view>

namespace mobile_demo {
    namespace {
        constexpr float button_width { 140.0f };
        constexpr float button_spacing { 8.0f };
        constexpr std::u8string_view watch_file { u8"a.txt" };
        constexpr std::u8string_view renamed_file { u8"b.txt" };

        [[nodiscard]] std::u8string kind_name(const luil::file_change_kind kind)
        {
            switch (kind)
            {
            case luil::file_change_kind::created:
                return u8"created";
            case luil::file_change_kind::removed:
                return u8"removed";
            case luil::file_change_kind::modified:
                return u8"modified";
            case luil::file_change_kind::renamed:
                return u8"renamed";
            }
            return u8"changed";
        }

        void append_text(const std::filesystem::path& path, const std::string_view text)
        {
            std::ofstream file { path, std::ios::binary | std::ios::app };
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_button(const std::u8string& key, const std::u8string_view label, const files_operation operation)
        {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"files-" + key }, luil::text_button_config { .text = std::u8string { label } }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(files_operation_intent { operation }));
            return button;
        }

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_row(const std::u8string& key, std::unique_ptr<luil::ui_element> first, std::unique_ptr<luil::ui_element> second)
        {
            luil::stack_config config {};
            config.direction = luil::stack_direction::row;
            config.spacing = button_spacing;
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"files-row-" + key }, config) };
            row->add(std::move(first), button_width);
            row->add(std::move(second), button_width);
            return row;
        }
    }

    std::u8string describe_file_change_batch(const luil::file_change_batch& batch)
    {
        std::u8string text { u8"변경:" };
        bool first { true };
        const auto add = [&text, &first](const std::u8string& part) {
            text += first ? u8" " : u8", ";
            text += part;
            first = false;
        };
        for (const luil::file_change& change : batch.changes)
        {
            if (change.kind == luil::file_change_kind::renamed)
                add(kind_name(change.kind) + u8" " + change.old_path + u8"→" + change.path);
            else
                add(kind_name(change.kind) + u8" " + change.path);
        }
        if (batch.rescan)
            add(u8"rescan");
        if (batch.end == luil::file_watch_end::removed)
            add(u8"끝 removed");
        else if (batch.end == luil::file_watch_end::lost)
            add(u8"끝 lost");
        if (first)
            text += u8" 없음";
        return text;
    }

    bool files_page::handle(const luil::app_message& message)
    {
        if (const auto* const attach { message.get<file_watcher_intent>() }; attach != nullptr)
        {
            folder_ = attach->folder;
            ticket_ = attach->ticket;
            stop_ = attach->stop;
            state_ = ticket_ ? u8"감시: 켜짐" : u8"감시: 걸지 못함 (" + attach->error + u8")";
            return true;
        }
        if (const auto* const delivered { message.get<files_batch_intent>() }; delivered != nullptr)
        {
            // 늦게 온 다른 표의 묶음이 지금 화면을 덮지 않게 표로 맞춘다.
            if (ticket_ == luil::file_watch_ticket {} || delivered->batch.ticket != ticket_)
                return true;
            text_ = describe_file_change_batch(delivered->batch);
            if (delivered->batch.end != luil::file_watch_end::none)
            {
                ticket_ = {};
                state_ = u8"감시: 끝남";
            }
            return true;
        }
        if (const auto* const operation { message.get<files_operation_intent>() }; operation != nullptr)
        {
            run(operation->operation);
            return true;
        }
        return false;
    }

    void files_page::run(const files_operation operation)
    {
        // 감시가 없으면 바꿔도 볼 것이 없다. 처음 훑기는 하지 않으므로 감시를 먼저 건 뒤에만 바꾼다.
        if (ticket_ == luil::file_watch_ticket {} || folder_.empty())
            return;
        const std::filesystem::path folder { folder_ };
        const std::filesystem::path first { folder / std::filesystem::path { watch_file } };
        const std::filesystem::path second { folder / std::filesystem::path { renamed_file } };
        std::error_code ignored {};
        switch (operation)
        {
        case files_operation::create:
            append_text(first, "");
            break;
        case files_operation::modify:
            append_text(std::filesystem::exists(first, ignored) ? first : second, "x");
            break;
        case files_operation::remove:
            std::filesystem::remove(first, ignored);
            std::filesystem::remove(second, ignored);
            break;
        case files_operation::rename:
            // 있는 쪽을 다른 이름으로 옮긴다. 두 번 누르면 제자리로 돌아온다.
            if (std::filesystem::exists(first, ignored))
                std::filesystem::rename(first, second, ignored);
            else
                std::filesystem::rename(second, first, ignored);
            break;
        }
    }

    page_content files_page::build(const float width, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(scale);
        page_column column { u8"files" };
        column.note(u8"files-hint", u8"앱 전용 폴더의 a.txt를 바꾸면");
        column.note(u8"files-hint-2", u8"감시자가 낸 묶음이 아래에 적힌다.");
        column.gap(10.0f);
        column.body(u8"files-watch-state", state_);
        column.body(u8"files-watch-batch", text_);
        if (folder_.empty() == false)
            column.note(u8"files-watch-folder", folder_);
        column.gap(12.0f);
        column.add(make_row(u8"first", make_button(u8"create", u8"파일 만들기", files_operation::create), make_button(u8"modify", u8"고치기", files_operation::modify)), touch_height);
        column.gap(button_spacing);
        column.add(make_row(u8"second", make_button(u8"remove", u8"지우기", files_operation::remove), make_button(u8"rename", u8"이름 바꾸기", files_operation::rename)), touch_height);
        return column.finish();
    }
}

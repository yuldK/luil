#include "mobile_demo/smb_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include <algorithm>
#include <string>
#include <utility>

namespace mobile_demo {
    namespace {
        constexpr float field_height { 40.0f };
        constexpr float entry_row_height { 44.0f };
        // 파일을 누르면 앞에서 이만큼만 읽는다 (바이트).
        constexpr std::size_t preview_bytes { 4096 };
        // "다음 구간"이 한 번에 읽는 길이다. 큰 파일을 구간으로 나눠 읽을 때 두 번째부터 연결을 다시 쓰는 것이 보인다.
        constexpr std::size_t range_bytes { 1024u * 1024u };
        // 미리 보기로 적는 글자 수다.
        constexpr std::size_t preview_characters { 160 };
        // 여러 줄로 나눠 적는 글의 한 줄 폭과 줄 수 상한이다. 폭은 ASCII 한 자를 1, 그 밖(한글 등)을 2로 센 어림이고,
        // 360 dp 폭의 휴대폰에 맞췄다.
        constexpr std::size_t detail_line_width { 48 };
        constexpr std::size_t detail_max_lines { 6 };

        [[nodiscard]] std::u8string error_name(const luil::net::smb_error_kind kind)
        {
            switch (kind)
            {
            case luil::net::smb_error_kind::none:
                return u8"없음";
            case luil::net::smb_error_kind::invalid_request:
                return u8"요청이 규칙에 맞지 않음";
            case luil::net::smb_error_kind::unsupported:
                return u8"지원 안 함";
            case luil::net::smb_error_kind::cannot_connect:
                return u8"서버에 닿지 못함";
            case luil::net::smb_error_kind::logon_failure:
                return u8"로그온 실패";
            case luil::net::smb_error_kind::access_denied:
                return u8"접근 거부";
            case luil::net::smb_error_kind::not_found:
                return u8"없음 (공유나 경로)";
            case luil::net::smb_error_kind::timed_out:
                return u8"시간 초과";
            case luil::net::smb_error_kind::cancelled:
                return u8"취소";
            case luil::net::smb_error_kind::stopped:
                return u8"정지";
            case luil::net::smb_error_kind::failed:
                return u8"실패";
            }
            return u8"실패";
        }

        [[nodiscard]] std::u8string connection_text(const luil::net::smb_connection_use use)
        {
            switch (use)
            {
            case luil::net::smb_connection_use::none:
                return {};
            case luil::net::smb_connection_use::opened:
                return u8" · 새 연결";
            case luil::net::smb_connection_use::reused:
                return u8" · 다시 씀";
            case luil::net::smb_connection_use::reopened:
                return u8" · 끊겨 다시 엶";
            }
            return {};
        }

        [[nodiscard]] std::u8string number_text(const std::uint64_t value)
        {
            const std::string text { std::to_string(value) };
            return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        [[nodiscard]] std::u8string size_text(const std::uint64_t bytes)
        {
            if (bytes < 1024u)
                return number_text(bytes) + u8" B";
            if (bytes < 1024u * 1024u)
                return number_text(bytes / 1024u) + u8" KB";
            return number_text(bytes / (1024u * 1024u)) + u8" MB";
        }

        [[nodiscard]] std::u8string joined(const std::u8string& folder, const std::u8string& name)
        {
            return folder.empty() ? name : folder + u8"/" + name;
        }

        // 받은 바이트의 앞부분을 글로 적는다. UTF-8로 읽히지 않는 바이트는 점으로 둔다.
        [[nodiscard]] std::u8string preview_of(const std::vector<std::uint8_t>& data)
        {
            std::u8string text {};
            std::size_t characters { 0 };
            for (std::size_t index { 0 }; index < data.size() && characters < preview_characters; ++characters)
            {
                const std::uint8_t lead { data[index] };
                const std::size_t length { lead < 0x80u ? 1u : (lead >> 5) == 0x6u ? 2u : (lead >> 4) == 0xeu ? 3u : (lead >> 3) == 0x1eu ? 4u : 0u };
                bool valid { length != 0 && index + length <= data.size() };
                for (std::size_t extra { 1 }; valid && extra < length; ++extra)
                    valid = (data[index + extra] & 0xc0u) == 0x80u;
                if (valid == false)
                {
                    text.push_back(u8'.');
                    ++index;
                    continue;
                }
                if (length == 1 && (lead < 0x20u || lead == 0x7fu))
                    text.push_back(lead == u8'\n' ? u8' ' : u8'.');
                else
                    text.append(reinterpret_cast<const char8_t*>(data.data() + index), length);
                index += length;
            }
            return text;
        }

        [[nodiscard]] std::size_t glyph_width(const char8_t lead) noexcept
        {
            return lead < 0x80u ? 1u : 2u;
        }

        // UTF-8 글을 어림 폭으로 나눈다. 가능하면 공백에서 끊는다.
        [[nodiscard]] std::vector<std::u8string> wrap_lines(const std::u8string_view text)
        {
            std::vector<std::u8string> lines {};
            std::u8string line {};
            std::size_t width { 0 };
            std::size_t last_space { std::u8string::npos };
            for (std::size_t index { 0 }; index < text.size() && lines.size() < detail_max_lines;)
            {
                const char8_t lead { text[index] };
                const std::size_t length { lead < 0x80u ? 1u : (lead >> 5) == 0x6u ? 2u : (lead >> 4) == 0xeu ? 3u : 4u };
                line.append(text.substr(index, length));
                index += length;
                width += glyph_width(lead);
                if (lead == u8' ')
                    last_space = line.size();
                if (width < detail_line_width)
                    continue;
                // 줄이 찼다. 공백이 있었으면 거기서 끊고 나머지를 다음 줄로 넘긴다.
                if (last_space != std::u8string::npos && last_space < line.size())
                {
                    lines.push_back(line.substr(0, last_space));
                    line = line.substr(last_space);
                }
                else
                {
                    lines.push_back(line);
                    line.clear();
                }
                width = 0;
                for (const char8_t unit : line)
                    if ((unit & 0xc0u) != 0x80u)
                        width += glyph_width(unit);
                last_space = std::u8string::npos;
            }
            if (line.empty() == false && lines.size() < detail_max_lines)
                lines.push_back(line);
            return lines;
        }

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_button(const std::u8string& key, const std::u8string_view label, luil::ui_action action)
        {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"smb-" + key }, luil::text_button_config { .text = std::u8string { label } }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, std::move(action));
            return button;
        }
    } // namespace

    smb_page::smb_page(smb_page_transport transport)
        : transport_ { std::move(transport) }
    {}

    bool smb_page::handle(const luil::app_message& message)
    {
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            if (edit->request.target != target_smb_address && edit->request.target != target_smb_user && edit->request.target != target_smb_password)
                return false;
            apply_edit(edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            const luil::text_input_target target { composition->event.target };
            if (target != target_smb_address && target != target_smb_user && target != target_smb_password)
                return false;
            apply_composition(composition->event);
            return true;
        }
        if (const auto* const attach { message.get<smb_client_intent>() }; attach != nullptr)
        {
            if (attach->client == nullptr)
                transport_ = {};
            else
            {
                luil::net::smb_client* const client { attach->client };
                transport_.send = [client](luil::net::smb_request request, std::u8string& error) { return client->send(std::move(request), error); };
                transport_.cancel = [client](const luil::net::smb_ticket ticket) { client->cancel(ticket); };
                transport_.stop = [client] { client->stop(); };
                transport_.close_idle = [client] { client->close_idle(); };
            }
            return true;
        }
        if (message.get<smb_open_intent>() != nullptr)
        {
            const std::u8string address { address_.text };
            const std::size_t first { address.find(u8'/') };
            const std::size_t second { first == std::u8string::npos ? std::u8string::npos : address.find(u8'/', first + 1) };
            // 공유 없이 서버만 적었으면 공유 목록을 연다.
            const bool server_only { first == std::u8string::npos || first + 1 == address.size() };
            if (server_only)
                send(luil::net::smb_operation::shares, {});
            else
                send(luil::net::smb_operation::list, second == std::u8string::npos ? std::u8string {} : address.substr(second + 1));
            return true;
        }
        if (const auto* const chosen { message.get<smb_share_intent>() }; chosen != nullptr)
        {
            // 새 tree가 오기 전에 이전 목록의 클릭이 와도 지금 목록에 없는 공유는 열지 않는다.
            if (std::ranges::none_of(shares_, [chosen](const luil::net::smb_share& share) { return share.name == chosen->name; }))
                return true;
            const std::optional<luil::net::smb_location> where { location() };
            if (where.has_value() == false)
                return true;
            luil::text_edit_request rename {};
            rename.target = target_smb_address;
            rename.command = luil::text::text_edit_command::replace_all;
            rename.text = where->server + u8"/" + chosen->name;
            luil::apply_text_edit(address_, rename);
            shares_.clear();
            send(luil::net::smb_operation::list, {});
            return true;
        }
        if (message.get<smb_up_intent>() != nullptr)
        {
            if (pending_ && pending_operation_ == luil::net::smb_operation::list)
                return true;
            const std::size_t slash { folder_.rfind(u8'/') };
            send(luil::net::smb_operation::list, slash == std::u8string::npos ? std::u8string {} : folder_.substr(0, slash));
            return true;
        }
        if (const auto* const entry { message.get<smb_entry_intent>() }; entry != nullptr)
        {
            // 새 tree가 오기 전에 이전 행의 클릭이 도착해도 지금 목록에 없는 항목은 열지 않는다.
            if (std::ranges::none_of(entries_, [entry](const luil::net::smb_entry& item) { return item.name == entry->name && item.directory == entry->directory; }))
                return true;
            if (entry->directory)
                send(luil::net::smb_operation::list, joined(folder_, entry->name));
            else
                send(luil::net::smb_operation::read, joined(folder_, entry->name), 0, preview_bytes);
            return true;
        }
        if (const auto* const encrypt { message.get<smb_encrypt_intent>() }; encrypt != nullptr)
        {
            encrypt_ = encrypt->encrypt;
            return true;
        }
        if (const auto* const reveal { message.get<smb_reveal_intent>() }; reveal != nullptr)
        {
            reveal_ = reveal->reveal;
            return true;
        }
        if (message.get<smb_next_range_intent>() != nullptr)
        {
            if (file_.empty() == false && more_)
                send(luil::net::smb_operation::read, file_, next_offset_, range_bytes);
            return true;
        }
        if (message.get<smb_close_idle_intent>() != nullptr)
        {
            if (transport_.close_idle != nullptr)
            {
                transport_.close_idle();
                status_ = u8"쉬는 연결을 닫았다.";
                detail_ = u8"다음 요청은 연결을 새로 연다.";
            }
            return true;
        }
        if (const auto* const result { message.get<smb_result_intent>() }; result != nullptr)
        {
            const luil::net::smb_response& response { result->response };
            if (pending_ == luil::net::smb_ticket {} || response.ticket != pending_)
                return true;
            pending_ = {};
            // 걸린 시간과 쓴 연결이다. 같은 서버를 다시 부르면 두 번째부터 "다시 씀"과 짧아진 시간이 보인다.
            const std::u8string elapsed { u8" (" + number_text(static_cast<std::uint64_t>(response.elapsed.count())) + u8" ms" + connection_text(response.connection) + u8")" };
            if (response.ok() == false)
            {
                // libsmb2의 글을 그대로 적는다. 서버가 없는 쪽에서 사람이 알려 줄 수 있는 단서가 이것이다.
                status_ = error_name(response.error.kind) + elapsed;
                detail_ = response.error.message;
                return true;
            }
            if (response.operation == luil::net::smb_operation::shares)
            {
                // 디스크이고 숨지 않은 공유만 보인다. 관리용·`$`로 끝나는 공유와 프린터·파이프는 센다.
                shares_.clear();
                for (const luil::net::smb_share& share : response.shares)
                    if (share.kind == luil::net::smb_share_kind::disk && share.special == false && share.name.ends_with(u8'$') == false)
                        shares_.push_back(share);
                hidden_shares_ = response.shares.size() - shares_.size();
                entries_.clear();
                folder_.clear();
                file_.clear();
                more_ = false;
                detail_.clear();
                status_ = u8"공유 " + number_text(shares_.size()) + u8"개, 숨김 " + number_text(hidden_shares_) + u8"개" + elapsed;
            }
            else if (response.operation == luil::net::smb_operation::list)
            {
                shares_.clear();
                folder_ = pending_path_;
                file_.clear();
                more_ = false;
                entries_ = response.entries;
                std::ranges::sort(
                    entries_, [](const luil::net::smb_entry& left, const luil::net::smb_entry& right) { return left.directory != right.directory ? left.directory : left.name < right.name; });
                detail_.clear();
                status_ = u8"/" + folder_ + u8" — 항목 " + number_text(entries_.size()) + u8"개" + elapsed;
            }
            else if (response.operation == luil::net::smb_operation::read)
            {
                file_ = pending_path_;
                next_offset_ = pending_offset_ + response.data.size();
                more_ = response.data.size() == pending_length_;
                if (pending_offset_ == 0)
                {
                    status_ = u8"앞 " + size_text(response.data.size()) + u8" 읽음" + elapsed;
                    detail_ = pending_path_ + u8": " + preview_of(response.data);
                }
                else
                {
                    status_ = size_text(pending_offset_) + u8"부터 " + size_text(response.data.size()) + u8" 읽음" + elapsed;
                    detail_ = more_ ? pending_path_ + u8" — 다음 구간을 누르면 이어 읽는다." : pending_path_ + u8" — 파일 끝이다.";
                }
            }
            return true;
        }
        return false;
    }

    void smb_page::apply_edit(const luil::text_edit_request& request)
    {
        if (request.target == target_smb_address)
        {
            const std::u8string before { address_.text };
            luil::apply_text_edit(address_, request);
            if (address_.text != before)
            {
                cancel_pending();
                entries_.clear();
                folder_.clear();
            }
        }
        else if (request.target == target_smb_user)
            luil::apply_text_edit(user_, request);
        else if (request.target == target_smb_password)
            luil::apply_text_edit(password_, request);
    }

    void smb_page::apply_composition(const luil::text_composition_event& event)
    {
        if (event.composing == false)
        {
            composition_.reset();
            return;
        }
        composition_ = event;
    }

    std::optional<luil::net::smb_location> smb_page::location() const
    {
        const std::u8string& address { address_.text };
        const std::size_t first { address.find(u8'/') };
        if (address.empty() || first == 0)
            return std::nullopt;
        luil::net::smb_location location {};
        location.server = address.substr(0, first);
        if (first != std::u8string::npos)
        {
            const std::size_t second { address.find(u8'/', first + 1) };
            location.share = address.substr(first + 1, second == std::u8string::npos ? std::u8string::npos : second - first - 1);
        }
        location.user = user_.text;
        location.password = password_.text;
        location.encrypt = encrypt_;
        return location;
    }

    void smb_page::cancel_pending()
    {
        if (pending_ && transport_.cancel != nullptr)
            transport_.cancel(pending_);
        pending_ = {};
    }

    void smb_page::send(const luil::net::smb_operation operation, std::u8string path, const std::uint64_t offset, const std::size_t length)
    {
        cancel_pending();
        // 응답 대기·실패 중에 이전 폴더의 행을 새 경로로 열지 않는다.
        if (operation == luil::net::smb_operation::list)
            entries_.clear();
        if (transport_.send == nullptr)
            return;
        const std::optional<luil::net::smb_location> where { location() };
        if (where.has_value() == false || (operation != luil::net::smb_operation::shares && where->share.empty()))
        {
            status_ = u8"주소를 \"서버/공유\"로 적는다.";
            detail_ = u8"예: 192.168.0.10/photos (서버만 적으면 공유 목록)";
            return;
        }
        luil::net::smb_request request {};
        request.location = *where;
        request.operation = operation;
        request.path = path;
        request.offset = offset;
        request.length = length;
        std::u8string error {};
        pending_ = transport_.send(std::move(request), error);
        pending_operation_ = operation;
        pending_path_ = std::move(path);
        pending_offset_ = offset;
        pending_length_ = length;
        status_ = pending_ ? std::u8string { u8"기다리는 중…" } : std::u8string { u8"보내지 못함" };
        detail_ = pending_ ? std::u8string {} : error;
    }

    page_content smb_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        const float field_width { std::min(width - 2.0f * page_padding, 420.0f) };
        page_column column { u8"smb" };
        column.heading(u8"smb-heading", u8"SMB 공유");
        if (transport_.send == nullptr)
        {
            column.note(u8"smb-none", u8"이 구성에는 SMB가 없다.");
            column.note(u8"smb-none-why", u8"Android에서 LUIL_ENABLE_SMB를 켜고 세운다.");
            return column.finish();
        }
        column.note(u8"smb-help-1", u8"주소는 \"서버/공유\"다. 서버만 적으면 공유 목록이다.");
        column.note(u8"smb-help-ip", u8"집 안 NAS는 IP 주소로 적는다.");
        column.note(u8"smb-help-2", u8"Android 17은 기기 페이지에서 권한부터 받는다.");
        column.gap(8.0f);

        const auto input
            = [this, &column, field_width](const luil::ui_element_kind kind, const luil::text::text_edit_state& state, const luil::text_input_target target, luil::text_input_config config) {
                  luil::text_input_view view { luil::make_text_input_view(state, composition_, target) };
                  auto element { std::make_unique<luil::text_input_element>(luil::ui_element_id { kind }, std::move(view), std::move(config)) };
                  column.add(std::move(element), field_height, field_width);
                  column.gap(8.0f);
              };
        input(kind_smb_address_input, address_, target_smb_address, { .placeholder = u8"192.168.0.10/photos" });
        input(kind_smb_user_input, user_, target_smb_user, { .placeholder = u8"사용자 (비우면 guest)" });
        input(kind_smb_password_input, password_, target_smb_password, make_secret_input_config(u8"암호", reveal_, luil::make_message_action(smb_reveal_intent { reveal_ == false })));

        luil::check_config encrypt {};
        encrypt.owner = u8"smb-encrypt";
        encrypt.style = luil::check_style::toggle_switch;
        encrypt.label = u8"SMB3 암호화 요구";
        encrypt.checked = encrypt_;
        encrypt.toggle = luil::make_message_action(smb_encrypt_intent { encrypt_ == false });
        column.add(std::make_unique<luil::check_element>(std::move(encrypt)), touch_height, field_width);
        column.gap(8.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"smb-actions" }, row_config) };
        row->add(make_button(u8"open", u8"열기", luil::make_message_action(smb_open_intent {})), 96.0f);
        row->add(make_button(u8"up", u8"위로", luil::make_message_action(smb_up_intent {})), 96.0f);
        row->add(make_button(u8"close-idle", u8"연결 닫기", luil::make_message_action(smb_close_idle_intent {})), 120.0f);
        column.add(std::move(row), touch_height);
        // 읽던 파일이 더 있으면 다음 구간을 읽는다.
        if (file_.empty() == false && more_)
        {
            column.gap(8.0f);
            column.add(make_button(u8"next-range", u8"다음 구간 (1 MB)", luil::make_message_action(smb_next_range_intent {})), touch_height, field_width);
        }
        column.gap(12.0f);
        column.body(u8"smb-status", status_);
        const std::vector<std::u8string> detail { wrap_lines(detail_) };
        for (std::size_t index { 0 }; index < detail.size(); ++index)
            column.note(u8"smb-detail-" + number_text(index), detail[index]);
        column.gap(8.0f);

        // 공유 목록이다. 누르면 그 공유를 연다.
        for (const luil::net::smb_share& share : shares_)
        {
            const std::u8string label { share.comment.empty() ? share.name : share.name + u8"  ·  " + share.comment };
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_smb_entry, u8"smb-share-" + share.name }, luil::text_button_config { .text = label }) };
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(smb_share_intent { share.name }));
            column.add(std::move(button), entry_row_height, field_width);
            column.gap(4.0f);
        }

        // 폴더가 먼저, 이름 차례다. 폴더를 누르면 들어가고 파일을 누르면 앞부분을 읽는다.
        for (std::size_t index { 0 }; index < entries_.size(); ++index)
        {
            const luil::net::smb_entry& entry { entries_[index] };
            // 폴더는 이름 뒤에 `/`를 붙이고, 파일은 크기를 함께 적는다.
            const std::u8string label { entry.directory ? entry.name + u8"/" : entry.name + u8"  ·  " + size_text(entry.size) };
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_smb_entry, u8"smb-entry-" + entry.name }, luil::text_button_config { .text = label }) };
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(smb_entry_intent { entry.name, entry.directory }));
            column.add(std::move(button), entry_row_height, field_width);
            column.gap(4.0f);
        }
        return column.finish();
    }
} // namespace mobile_demo

#include "net/dns_message.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace luil::net {
    namespace {
        constexpr std::size_t header_size { 12 };
        constexpr std::size_t max_label { 63 };
        constexpr std::size_t max_name { 255 };
        // 압축 포인터를 따라가는 횟수의 상한이다. 포인터가 앞쪽만 가리키게 해 고리는 이미 막지만, 긴 사슬도 끊는다.
        constexpr int max_jumps { 64 };
        constexpr std::uint16_t class_in { 1 };

        [[nodiscard]] char ascii_lower(const char value) noexcept
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        }

        [[nodiscard]] bool same_label(const std::string_view left, const std::string_view right) noexcept
        {
            return left.size() == right.size() && std::ranges::equal(left, right, [](const char a, const char b) { return ascii_lower(a) == ascii_lower(b); });
        }

        // 패킷을 앞에서부터 읽는다. 끝을 넘는 읽기는 실패로 남고 그 뒤의 읽기도 모두 실패한다.
        class reader
        {
        public:
            explicit reader(const std::span<const std::uint8_t> packet) noexcept
                : packet_ { packet }
            {
            }

            [[nodiscard]] bool failed() const noexcept
            {
                return failed_;
            }

            [[nodiscard]] std::size_t position() const noexcept
            {
                return position_;
            }

            void seek(const std::size_t position) noexcept
            {
                if (position > packet_.size())
                    failed_ = true;
                else
                    position_ = position;
            }

            [[nodiscard]] std::uint8_t u8()
            {
                if (failed_ || position_ + 1 > packet_.size())
                {
                    failed_ = true;
                    return 0;
                }
                return packet_[position_++];
            }

            [[nodiscard]] std::uint16_t u16()
            {
                const std::uint16_t high { u8() };
                const std::uint16_t low { u8() };
                return static_cast<std::uint16_t>((high << 8) | low);
            }

            [[nodiscard]] std::uint32_t u32()
            {
                const std::uint32_t high { u16() };
                const std::uint32_t low { u16() };
                return (high << 16) | low;
            }

            [[nodiscard]] std::string bytes(const std::size_t count)
            {
                if (failed_ || position_ + count > packet_.size())
                {
                    failed_ = true;
                    return {};
                }
                std::string text { reinterpret_cast<const char*>(packet_.data() + position_), count };
                position_ += count;
                return text;
            }

            // 이름을 읽는다. 압축 포인터는 이 이름이 시작된 자리보다 앞쪽만 가리킬 수 있다.
            [[nodiscard]] dns_name name()
            {
                dns_name labels {};
                std::size_t length { 1 };
                std::size_t resume { 0 };
                bool jumped { false };
                std::size_t floor { position_ };
                for (int jumps { 0 }; failed_ == false;)
                {
                    const std::uint8_t size { u8() };
                    if (failed_)
                        break;
                    if (size == 0)
                        break;
                    if ((size & 0xC0u) == 0xC0u)
                    {
                        const std::size_t pointer { (static_cast<std::size_t>(size & 0x3Fu) << 8) | u8() };
                        if (failed_ || pointer >= floor || ++jumps > max_jumps)
                        {
                            failed_ = true;
                            break;
                        }
                        if (jumped == false)
                            resume = position_;
                        jumped = true;
                        floor = pointer;
                        position_ = pointer;
                        continue;
                    }
                    if ((size & 0xC0u) != 0 || size > max_label)
                    {
                        failed_ = true;
                        break;
                    }
                    length += static_cast<std::size_t>(size) + 1;
                    if (length > max_name)
                    {
                        failed_ = true;
                        break;
                    }
                    labels.push_back(bytes(size));
                }
                if (jumped && failed_ == false)
                    position_ = resume;
                return labels;
            }

        private:
            std::span<const std::uint8_t> packet_ {};
            std::size_t position_ { 0 };
            bool failed_ { false };
        };

        void put16(std::vector<std::uint8_t>& out, const std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value >> 8));
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        }

        [[nodiscard]] bool put_name(std::vector<std::uint8_t>& out, const dns_name& name)
        {
            std::size_t length { 1 };
            for (const std::string& label : name)
            {
                if (label.empty() || label.size() > max_label)
                    return false;
                length += label.size() + 1;
                if (length > max_name)
                    return false;
                out.push_back(static_cast<std::uint8_t>(label.size()));
                out.insert(out.end(), label.begin(), label.end());
            }
            out.push_back(0);
            return true;
        }

        [[nodiscard]] std::optional<dns_record> read_record(reader& in)
        {
            dns_record record {};
            record.name = in.name();
            record.type = in.u16();
            static_cast<void>(in.u16());
            record.ttl = in.u32();
            const std::uint16_t size { in.u16() };
            if (in.failed())
                return std::nullopt;
            const std::size_t start { in.position() };
            const std::size_t end { start + size };
            switch (record.type)
            {
            case dns_type_a:
            case dns_type_aaaa: {
                const std::size_t expected { record.type == dns_type_a ? 4u : 16u };
                if (size != expected)
                    return std::nullopt;
                const std::string raw { in.bytes(size) };
                std::copy(raw.begin(), raw.end(), record.address.begin());
                record.address_size = expected;
                break;
            }
            case dns_type_ptr:
                record.target = in.name();
                break;
            case dns_type_srv:
                record.priority = in.u16();
                record.weight = in.u16();
                record.port = in.u16();
                record.target = in.name();
                break;
            case dns_type_txt:
                while (in.failed() == false && in.position() < end)
                {
                    const std::uint8_t length { in.u8() };
                    record.text.push_back(in.bytes(length));
                }
                break;
            default:
                break;
            }
            if (in.failed() || in.position() > end)
                return std::nullopt;
            // 데이터의 길이가 선언한 것과 맞지 않으면(남는 바이트는 모르는 레코드뿐이다) 선언한 끝으로 넘어간다.
            in.seek(end);
            if (in.failed())
                return std::nullopt;
            return record;
        }

        [[nodiscard]] bool is_type(const dns_record& record, const std::uint16_t type, const dns_name& name)
        {
            return record.type == type && same_dns_name(record.name, name);
        }
    }

    dns_name make_dns_name(const std::string_view dotted)
    {
        dns_name name {};
        std::size_t begin { 0 };
        while (begin <= dotted.size())
        {
            const std::size_t dot { std::min(dotted.find('.', begin), dotted.size()) };
            if (dot > begin)
                name.emplace_back(dotted.substr(begin, dot - begin));
            begin = dot + 1;
        }
        return name;
    }

    bool same_dns_name(const dns_name& left, const dns_name& right) noexcept
    {
        return left.size() == right.size() && std::ranges::equal(left, right, [](const std::string& a, const std::string& b) { return same_label(a, b); });
    }

    std::string dns_name_text(const dns_name& name)
    {
        std::string text {};
        for (const std::string& label : name)
        {
            if (text.empty() == false)
                text.push_back('.');
            text += label;
        }
        return text;
    }

    const dns_name& smb_service_name()
    {
        static const dns_name name { "_smb", "_tcp", "local" };
        return name;
    }

    std::vector<std::uint8_t> make_dns_query(const std::uint16_t id, const std::span<const dns_question> questions)
    {
        std::vector<std::uint8_t> out {};
        put16(out, id);
        put16(out, 0);
        put16(out, static_cast<std::uint16_t>(questions.size()));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        for (const dns_question& question : questions)
        {
            if (put_name(out, question.name) == false)
                return {};
            put16(out, question.type);
            put16(out, class_in);
        }
        return out;
    }

    std::optional<dns_message> parse_dns_message(const std::span<const std::uint8_t> packet)
    {
        if (packet.size() < header_size)
            return std::nullopt;
        reader in { packet };
        dns_message message {};
        message.id = in.u16();
        message.flags = in.u16();
        const std::uint16_t questions { in.u16() };
        const std::uint16_t answers { in.u16() };
        const std::uint16_t authorities { in.u16() };
        const std::uint16_t additions { in.u16() };
        for (std::uint16_t index { 0 }; index < questions; ++index)
        {
            static_cast<void>(in.name());
            static_cast<void>(in.u32());
            if (in.failed())
                return std::nullopt;
        }
        const std::size_t records { static_cast<std::size_t>(answers) + authorities + additions };
        for (std::size_t index { 0 }; index < records; ++index)
        {
            std::optional<dns_record> record { read_record(in) };
            if (record.has_value() == false)
                return std::nullopt;
            if (record->type == dns_type_a || record->type == dns_type_aaaa || record->type == dns_type_ptr || record->type == dns_type_srv || record->type == dns_type_txt)
                message.records.push_back(std::move(*record));
        }
        return message;
    }

    std::string dns_address_text(const dns_record& record)
    {
        char text[64] {};
        if (record.address_size == 4)
        {
            std::snprintf(text, sizeof(text), "%u.%u.%u.%u", record.address[0], record.address[1], record.address[2], record.address[3]);
            return text;
        }
        if (record.address_size != 16)
            return {};
        // 가장 긴 0 묶음 하나를 `::`로 줄인다 (RFC 5952).
        std::array<unsigned, 8> groups {};
        for (std::size_t index { 0 }; index < groups.size(); ++index)
            groups[index] = (static_cast<unsigned>(record.address[index * 2]) << 8) | record.address[index * 2 + 1];
        std::size_t best_begin { groups.size() };
        std::size_t best_length { 1 };
        for (std::size_t index { 0 }; index < groups.size();)
        {
            if (groups[index] != 0)
            {
                ++index;
                continue;
            }
            std::size_t end { index };
            while (end < groups.size() && groups[end] == 0)
                ++end;
            if (end - index > best_length)
            {
                best_begin = index;
                best_length = end - index;
            }
            index = end;
        }
        std::string result {};
        for (std::size_t index { 0 }; index < groups.size(); ++index)
        {
            if (index == best_begin)
            {
                result += "::";
                index += best_length - 1;
                continue;
            }
            if (result.empty() == false && result.back() != ':')
                result.push_back(':');
            std::snprintf(text, sizeof(text), "%x", groups[index]);
            result += text;
        }
        const bool link_local { record.address[0] == 0xFE && (record.address[1] & 0xC0u) == 0x80u };
        if (link_local)
        {
            if (record.scope.empty())
                return {};
            result.push_back('%');
            result += record.scope;
        }
        return result;
    }

    std::vector<smb_server_found> collect_smb_servers(const std::span<const dns_record> records)
    {
        std::vector<smb_server_found> servers {};
        std::vector<dns_name> instances {};
        for (const dns_record& record : records)
            if (is_type(record, dns_type_ptr, smb_service_name()) && std::ranges::none_of(instances, [&record](const dns_name& seen) { return same_dns_name(seen, record.target); }))
                instances.push_back(record.target);
        for (const dns_name& instance : instances)
        {
            const auto service { std::ranges::find_if(records, [&instance](const dns_record& record) { return is_type(record, dns_type_srv, instance); }) };
            if (service == records.end() || instance.empty())
                continue;
            smb_server_found server {};
            const std::string label { instance.front() };
            server.name = std::u8string { reinterpret_cast<const char8_t*>(label.data()), label.size() };
            const std::string host { dns_name_text(service->target) };
            server.host = std::u8string { reinterpret_cast<const char8_t*>(host.data()), host.size() };
            server.port = service->port;
            // IPv4를 먼저 싣는다.
            for (const std::uint16_t type : { dns_type_a, dns_type_aaaa })
                for (const dns_record& record : records)
                {
                    if (is_type(record, type, service->target) == false)
                        continue;
                    const std::string text { dns_address_text(record) };
                    const std::u8string address { reinterpret_cast<const char8_t*>(text.data()), text.size() };
                    if (address.empty() == false && std::ranges::find(server.addresses, address) == server.addresses.end())
                        server.addresses.push_back(address);
                }
            servers.push_back(std::move(server));
        }
        std::ranges::sort(servers, [](const smb_server_found& left, const smb_server_found& right) { return left.name < right.name; });
        return servers;
    }

    std::vector<dns_question> missing_smb_questions(const std::span<const dns_record> records)
    {
        std::vector<dns_question> questions {};
        const auto asked = [&questions](const dns_name& name, const std::uint16_t type) {
            return std::ranges::any_of(questions, [&](const dns_question& question) { return question.type == type && same_dns_name(question.name, name); });
        };
        for (const dns_record& pointer : records)
        {
            if (is_type(pointer, dns_type_ptr, smb_service_name()) == false)
                continue;
            const auto service { std::ranges::find_if(records, [&pointer](const dns_record& record) { return is_type(record, dns_type_srv, pointer.target); }) };
            if (service == records.end())
            {
                if (asked(pointer.target, dns_type_srv) == false)
                    questions.push_back({ pointer.target, dns_type_srv });
                continue;
            }
            const auto address_of = [&service](const dns_record& record) { return (record.type == dns_type_a || record.type == dns_type_aaaa) && same_dns_name(record.name, service->target); };
            const bool addressed { std::ranges::any_of(records, address_of) };
            if (addressed == false && asked(service->target, dns_type_a) == false)
            {
                questions.push_back({ service->target, dns_type_a });
                questions.push_back({ service->target, dns_type_aaaa });
            }
        }
        return questions;
    }
}

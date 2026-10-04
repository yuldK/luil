#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// 시험이 쓰는 DNS 응답 짓기다 (dns_message_tests.cpp, mdns_query_tests.cpp). 실제 응답자가 하듯 이름을 압축하지 않고
// 그대로 싣고, `pointer`로 앞의 이름을 가리키는 압축 이름도 지을 수 있다.
namespace dns_test {
    class packet
    {
    public:
        explicit packet(const std::uint16_t id = 0, const std::uint16_t flags = 0x8400)
        {
            put16(id);
            put16(flags);
            put16(0);
            // 답·권한·추가의 수는 `finish`가 적는다. 시험은 레코드를 모두 답 칸에 싣는다.
            put16(0);
            put16(0);
            put16(0);
        }

        // 이름을 싣고 그 자리를 돌려준다 (압축 포인터가 가리킬 수 있다).
        std::size_t name(const std::string_view dotted)
        {
            const std::size_t at { bytes_.size() };
            std::size_t begin { 0 };
            while (begin < dotted.size())
            {
                std::size_t dot { dotted.find('.', begin) };
                if (dot == std::string_view::npos)
                    dot = dotted.size();
                bytes_.push_back(static_cast<std::uint8_t>(dot - begin));
                bytes_.insert(bytes_.end(), dotted.begin() + static_cast<std::ptrdiff_t>(begin), dotted.begin() + static_cast<std::ptrdiff_t>(dot));
                begin = dot + 1;
            }
            bytes_.push_back(0);
            return at;
        }

        // 앞자리를 가리키는 압축 이름이다 (라벨 몇 개 뒤에 포인터).
        void pointer_name(const std::string_view first_label, const std::size_t target)
        {
            bytes_.push_back(static_cast<std::uint8_t>(first_label.size()));
            bytes_.insert(bytes_.end(), first_label.begin(), first_label.end());
            bytes_.push_back(static_cast<std::uint8_t>(0xC0u | (target >> 8)));
            bytes_.push_back(static_cast<std::uint8_t>(target & 0xFFu));
        }

        void ptr(const std::string_view owner, const std::string_view target)
        {
            begin_record(owner, 12);
            const std::size_t length_at { reserve_length() };
            name(target);
            end_length(length_at);
        }

        void srv(const std::string_view owner, const std::uint16_t port, const std::string_view target)
        {
            begin_record(owner, 33);
            const std::size_t length_at { reserve_length() };
            put16(0);
            put16(0);
            put16(port);
            name(target);
            end_length(length_at);
        }

        void txt(const std::string_view owner, const std::vector<std::string>& texts)
        {
            begin_record(owner, 16);
            const std::size_t length_at { reserve_length() };
            for (const std::string& text : texts)
            {
                bytes_.push_back(static_cast<std::uint8_t>(text.size()));
                bytes_.insert(bytes_.end(), text.begin(), text.end());
            }
            end_length(length_at);
        }

        void a(const std::string_view owner, const std::array<std::uint8_t, 4> address)
        {
            begin_record(owner, 1);
            put16(4);
            bytes_.insert(bytes_.end(), address.begin(), address.end());
        }

        void aaaa(const std::string_view owner, const std::array<std::uint8_t, 16> address)
        {
            begin_record(owner, 28);
            put16(16);
            bytes_.insert(bytes_.end(), address.begin(), address.end());
        }

        // 날 바이트를 붙인다 (잘못된 패킷을 지을 때).
        void raw(const std::vector<std::uint8_t>& extra)
        {
            bytes_.insert(bytes_.end(), extra.begin(), extra.end());
        }

        // 레코드 하나를 센다 (날 바이트로 지은 레코드).
        void count_record()
        {
            ++records_;
        }

        [[nodiscard]] std::vector<std::uint8_t> finish() const
        {
            std::vector<std::uint8_t> out { bytes_ };
            out[6] = static_cast<std::uint8_t>(records_ >> 8);
            out[7] = static_cast<std::uint8_t>(records_ & 0xFFu);
            return out;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return bytes_.size();
        }

    private:
        void put16(const std::uint16_t value)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
            bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        }

        void begin_record(const std::string_view owner, const std::uint16_t type)
        {
            ++records_;
            name(owner);
            put16(type);
            // 클래스 IN에 캐시 비움 비트, TTL 120초다.
            put16(0x8001);
            put16(0);
            put16(120);
        }

        [[nodiscard]] std::size_t reserve_length()
        {
            const std::size_t at { bytes_.size() };
            put16(0);
            return at;
        }

        void end_length(const std::size_t at)
        {
            const std::size_t length { bytes_.size() - at - 2 };
            bytes_[at] = static_cast<std::uint8_t>(length >> 8);
            bytes_[at + 1] = static_cast<std::uint8_t>(length & 0xFFu);
        }

        std::vector<std::uint8_t> bytes_ {};
        std::uint16_t records_ { 0 };
    };

    // 한 서버를 알리는 전형적인 응답이다: PTR 답과 SRV·TXT·A·AAAA(링크 로컬) 추가.
    [[nodiscard]] inline std::vector<std::uint8_t> nas_response(const std::uint16_t id = 0)
    {
        packet response { id };
        response.ptr("_smb._tcp.local", "Living Room NAS._smb._tcp.local");
        response.srv("Living Room NAS._smb._tcp.local", 445, "nas.local");
        response.txt("Living Room NAS._smb._tcp.local", { "" });
        response.a("nas.local", { 192, 168, 0, 10 });
        response.aaaa("nas.local", { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x11, 0x32, 0xff, 0xfe, 0x12, 0x34, 0x56 });
        return response.finish();
    }
} // namespace dns_test

#include "host/update_manifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <exception>
#include <string>
#include <system_error>
#include <utility>

namespace luil {
    namespace {
        // 봉투와 매니페스트는 작은 고정 꼴이다. 이보다 깊은 문서는 짓기 전에 거절한다.
        //  - 깊게 중첩된 값을 **버릴 때** nlohmann이 재귀라, 상한은 파서가 아니라 파괴자 때문에 있다 (http_body.cpp와 같은 까닭).
        constexpr std::size_t json_depth_limit { 16 };

        constexpr std::u8string_view base64_alphabet { u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };
        constexpr std::u8string_view hex_digits { u8"0123456789abcdef" };

        // P-256 SPKI의 고정 머리다: SEQUENCE(89) { SEQUENCE(19) { OID id-ecPublicKey, OID prime256v1 }, BIT STRING(66) { 0, 04 } }.
        //  - openssl `ec -pubout -outform DER`이 내는 91바이트 꼴의 앞 27바이트와 같다 (tests/update_fixtures.cpp의 키로 잰다).
        constexpr std::array<std::uint8_t, 27> p256_spki_header {
            0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
            0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04,
        };

        [[nodiscard]] int base64_value(const char8_t character) noexcept
        {
            const std::size_t at { base64_alphabet.find(character) };
            return at == std::u8string_view::npos ? -1 : static_cast<int>(at);
        }

        [[nodiscard]] int hex_value(const char8_t character) noexcept
        {
            const std::size_t at { hex_digits.find(character) };
            return at == std::u8string_view::npos ? -1 : static_cast<int>(at);
        }

        [[nodiscard]] std::u8string from_ascii(const std::string_view text)
        {
            return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        [[nodiscard]] std::u8string from_json_string(const nlohmann::json& value)
        {
            return from_ascii(value.get_ref<const std::string&>());
        }

        // 문자열 밖의 `[`·`{`로 잰 가장 깊은 중첩이다. 깨진 JSON이어도 답을 낸다 (가리는 것은 파서의 몫이다).
        [[nodiscard]] std::size_t json_nesting_depth(const std::span<const std::uint8_t> bytes) noexcept
        {
            std::size_t depth { 0 };
            std::size_t deepest { 0 };
            bool in_string { false };
            bool escaped { false };
            for (const std::uint8_t byte : bytes)
            {
                if (in_string)
                {
                    if (escaped)
                        escaped = false;
                    else if (byte == '\\')
                        escaped = true;
                    else if (byte == '"')
                        in_string = false;
                    continue;
                }
                if (byte == '"')
                    in_string = true;
                else if (byte == '[' || byte == '{')
                    deepest = std::max(deepest, ++depth);
                else if ((byte == ']' || byte == '}') && depth > 0)
                    --depth;
            }
            return deepest;
        }

        // 던지지 않는 JSON 읽기다. 실패하면 비었고 `error`에 까닭을 적는다.
        [[nodiscard]] std::optional<nlohmann::json> parse_json(const std::span<const std::uint8_t> bytes, const std::u8string_view what, std::u8string& error)
        {
            if (bytes.empty())
            {
                error = std::u8string { what } + u8" is empty.";
                return std::nullopt;
            }
            if (json_nesting_depth(bytes) > json_depth_limit)
            {
                error = std::u8string { what } + u8" is nested too deeply.";
                return std::nullopt;
            }
            try
            {
                const auto* const first { reinterpret_cast<const char*>(bytes.data()) };
                nlohmann::json value { nlohmann::json::parse(first, first + bytes.size()) };
                if (value.is_object() == false)
                {
                    error = std::u8string { what } + u8" is not a JSON object.";
                    return std::nullopt;
                }
                return value;
            }
            catch (const std::exception& failure)
            {
                error = std::u8string { what } + u8" is not valid JSON (" + from_ascii(failure.what()) + u8").";
            }
            catch (...)
            {
                error = std::u8string { what } + u8" is not valid JSON.";
            }
            return std::nullopt;
        }

        // 이름의 칸이다. 없으면 null이다.
        [[nodiscard]] const nlohmann::json* member(const nlohmann::json& object, const char* const name)
        {
            const auto found { object.find(name) };
            return found == object.end() ? nullptr : &*found;
        }

        // 이름의 칸이 문자열이면 그 값이다. 없거나 문자열이 아니면 비었다.
        [[nodiscard]] std::optional<std::u8string> string_member(const nlohmann::json& object, const char* const name)
        {
            const nlohmann::json* const value { member(object, name) };
            if (value == nullptr || value->is_string() == false)
                return std::nullopt;
            return from_json_string(*value);
        }

        [[nodiscard]] std::optional<std::uint64_t> unsigned_member(const nlohmann::json& object, const char* const name)
        {
            const nlohmann::json* const value { member(object, name) };
            if (value == nullptr || value->is_number_unsigned() == false)
                return std::nullopt;
            return value->get<std::uint64_t>();
        }

        [[nodiscard]] bool ascii_equal_ignoring_case(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                char8_t a { left[index] };
                char8_t b { right[index] };
                if (a >= u8'A' && a <= u8'Z')
                    a = static_cast<char8_t>(a - u8'A' + u8'a');
                if (b >= u8'A' && b <= u8'Z')
                    b = static_cast<char8_t>(b - u8'A' + u8'a');
                if (a != b)
                    return false;
            }
            return true;
        }

        // 주소의 scheme 뒤 `//`부터 경로 앞까지다. 꼴이 틀리면 비었다.
        struct url_parts
        {
            std::u8string_view scheme {};
            std::u8string_view authority {};
            // authority 바로 뒤의 자리다.
            std::size_t rest { 0 };
        };

        [[nodiscard]] std::optional<url_parts> split_url(const std::u8string_view url) noexcept
        {
            const std::size_t separator { url.find(u8"://") };
            if (separator == std::u8string_view::npos || separator == 0)
                return std::nullopt;
            url_parts parts {};
            parts.scheme = url.substr(0, separator);
            const std::size_t start { separator + 3 };
            std::size_t end { url.find_first_of(u8"/?#", start) };
            if (end == std::u8string_view::npos)
                end = url.size();
            parts.authority = url.substr(start, end - start);
            parts.rest = end;
            return parts;
        }

        [[nodiscard]] std::u8string_view host_of(const std::u8string_view authority) noexcept
        {
            if (authority.empty() == false && authority.front() == u8'[')
            {
                const std::size_t close { authority.find(u8']') };
                return close == std::u8string_view::npos ? std::u8string_view {} : authority.substr(0, close + 1);
            }
            return authority.substr(0, authority.find(u8':'));
        }

        [[nodiscard]] bool loopback_host(const std::u8string_view host) noexcept
        {
            return host == u8"127.0.0.1" || host == u8"[::1]" || ascii_equal_ignoring_case(host, u8"localhost");
        }

        [[nodiscard]] bool asset_name_character(const char8_t character) noexcept
        {
            return (character >= u8'A' && character <= u8'Z') || (character >= u8'a' && character <= u8'z') || (character >= u8'0' && character <= u8'9') || character == u8'-'
                || character == u8'.' || character == u8'_' || character == u8'~';
        }

        [[nodiscard]] bool unsigned_text(const std::u8string_view text, std::uint64_t& value) noexcept
        {
            if (text.empty() || (text.size() > 1 && text.front() == u8'0'))
                return false;
            for (const char8_t character : text)
                if (character < u8'0' || character > u8'9')
                    return false;
            const auto* const first { reinterpret_cast<const char*>(text.data()) };
            const auto [end, code] { std::from_chars(first, first + text.size(), value) };
            return code == std::errc {} && end == first + text.size();
        }

        void append_number(std::u8string& text, const std::uint64_t value)
        {
            const std::string digits { std::to_string(value) };
            text.append(digits.begin(), digits.end());
        }

        [[nodiscard]] std::u8string decimal_number(const std::uint64_t value)
        {
            std::u8string text {};
            append_number(text, value);
            return text;
        }

        // 앞자리 0 없는 소문자 16진이다.
        [[nodiscard]] std::u8string hex_number(const std::uint64_t value)
        {
            char buffer[16] {};
            const auto [end, code] { std::to_chars(buffer, buffer + sizeof(buffer), value, 16) };
            static_cast<void>(code);
            return std::u8string { reinterpret_cast<const char8_t*>(buffer), static_cast<std::size_t>(end - buffer) };
        }

        // 소문자 16진 1~16자, 앞자리 0 없음이다.
        [[nodiscard]] std::optional<std::uint64_t> parse_hex_number(const std::u8string_view text) noexcept
        {
            if (text.empty() || text.size() > 16 || (text.size() > 1 && text.front() == u8'0'))
                return std::nullopt;
            std::uint64_t value { 0 };
            for (const char8_t character : text)
            {
                const int digit { hex_value(character) };
                if (digit < 0)
                    return std::nullopt;
                value = (value << 4) | static_cast<std::uint64_t>(digit);
            }
            return value;
        }

        [[nodiscard]] std::string to_ascii(const std::u8string_view text)
        {
            return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
        }

        // 잘못된 UTF-8이 섞여도 던지지 않고 바꿔 적는다. 진단 글은 OS가 준 것을 옮길 수 있다.
        [[nodiscard]] std::vector<std::uint8_t> dump_bytes(const nlohmann::json& root)
        {
            const std::string text { root.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) };
            return std::vector<std::uint8_t> { text.begin(), text.end() };
        }

        // 서명된 asset 칸 하나다. `windows`면 url·size·sha256, 아니면 store_url이다.
        [[nodiscard]] std::optional<update_asset> parse_asset(const nlohmann::json& value, const bool windows, const std::u8string_view key, std::u8string& error)
        {
            if (value.is_object() == false)
            {
                error = u8"The manifest asset \"" + std::u8string { key } + u8"\" is not an object.";
                return std::nullopt;
            }
            update_asset asset {};
            if (windows == false)
            {
                const std::optional<std::u8string> store { string_member(value, "store_url") };
                if (store.has_value() == false || store->empty())
                {
                    error = u8"The manifest asset \"" + std::u8string { key } + u8"\" has no store_url.";
                    return std::nullopt;
                }
                asset.store_url = *store;
                return asset;
            }
            const std::optional<std::u8string> url { string_member(value, "url") };
            const std::optional<std::uint64_t> size { unsigned_member(value, "size") };
            const std::optional<std::u8string> digest_text { string_member(value, "sha256") };
            const std::optional<update_digest> digest { digest_text.has_value() ? parse_update_digest(*digest_text) : std::nullopt };
            if (url.has_value() == false || url->empty() || size.has_value() == false || *size == 0 || digest.has_value() == false)
            {
                error = u8"The manifest asset \"" + std::u8string { key } + u8"\" needs a url, a non-zero size and a lowercase sha256.";
                return std::nullopt;
            }
            asset.url = *url;
            asset.size = *size;
            asset.sha256 = *digest;
            return asset;
        }

        // DER 길이 하나다. 짧은 꼴(0~127)만 받는다. 서명은 72바이트를 넘지 않는다.
        [[nodiscard]] bool read_short_length(const std::span<const std::uint8_t> der, std::size_t& at, std::size_t& length) noexcept
        {
            if (at >= der.size() || der[at] >= 0x80)
                return false;
            length = der[at];
            ++at;
            return at + length <= der.size();
        }

        // INTEGER 하나를 32바이트 큰 끝 칸에 옮긴다. 최소 꼴이 아니거나 음수이거나 0이거나 32바이트를 넘으면 거짓이다.
        [[nodiscard]] bool read_scalar(const std::span<const std::uint8_t> der, std::size_t& at, std::uint8_t* const out) noexcept
        {
            if (at >= der.size() || der[at] != 0x02)
                return false;
            ++at;
            std::size_t length { 0 };
            if (read_short_length(der, at, length) == false || length == 0)
                return false;
            const std::span<const std::uint8_t> value { der.subspan(at, length) };
            at += length;
            if ((value[0] & 0x80) != 0)
                return false;
            std::span<const std::uint8_t> magnitude { value };
            if (value[0] == 0x00)
            {
                // 앞의 0은 다음 바이트의 높은 비트가 설 때만 쓴다. 0 하나뿐인 INTEGER는 값이 0이다.
                if (value.size() == 1 || (value[1] & 0x80) == 0)
                    return false;
                magnitude = value.subspan(1);
            }
            if (magnitude.size() > 32)
                return false;
            std::fill(out, out + 32, std::uint8_t { 0 });
            std::copy(magnitude.begin(), magnitude.end(), out + (32 - magnitude.size()));
            return true;
        }

        void append_scalar(std::vector<std::uint8_t>& der, const std::uint8_t* const scalar)
        {
            std::size_t first { 0 };
            while (first < 31 && scalar[first] == 0)
                ++first;
            const bool pad { (scalar[first] & 0x80) != 0 };
            der.push_back(0x02);
            der.push_back(static_cast<std::uint8_t>((32 - first) + (pad ? 1 : 0)));
            if (pad)
                der.push_back(0x00);
            der.insert(der.end(), scalar + first, scalar + 32);
        }
    }

    std::optional<std::vector<std::uint8_t>> decode_update_base64(const std::u8string_view text)
    {
        if (text.size() % 4 != 0)
            return std::nullopt;
        std::vector<std::uint8_t> bytes {};
        bytes.reserve(text.size() / 4 * 3);
        for (std::size_t at = 0; at < text.size(); at += 4)
        {
            const bool last { at + 4 == text.size() };
            std::array<int, 4> values {};
            std::size_t padding { 0 };
            for (std::size_t index = 0; index < 4; ++index)
            {
                const char8_t character { text[at + index] };
                if (character == u8'=')
                {
                    // 패딩은 마지막 넷의 끝 한두 칸에만 선다.
                    if (last == false || index < 2)
                        return std::nullopt;
                    ++padding;
                    values[index] = 0;
                    continue;
                }
                if (padding > 0)
                    return std::nullopt;
                values[index] = base64_value(character);
                if (values[index] < 0)
                    return std::nullopt;
            }
            const std::uint32_t group {
                (static_cast<std::uint32_t>(values[0]) << 18) | (static_cast<std::uint32_t>(values[1]) << 12) | (static_cast<std::uint32_t>(values[2]) << 6)
                | static_cast<std::uint32_t>(values[3]),
            };
            // 쓰이지 않는 끝 비트가 0이 아니면 같은 바이트를 다른 글로 적은 것이다. 한 바이트에 한 꼴만 받는다.
            if ((padding == 1 && (group & 0xFFu) != 0) || (padding == 2 && (group & 0xFFFFu) != 0))
                return std::nullopt;
            bytes.push_back(static_cast<std::uint8_t>(group >> 16));
            if (padding < 2)
                bytes.push_back(static_cast<std::uint8_t>(group >> 8));
            if (padding < 1)
                bytes.push_back(static_cast<std::uint8_t>(group));
        }
        return bytes;
    }

    std::u8string encode_update_base64(const std::span<const std::uint8_t> bytes)
    {
        std::u8string text {};
        text.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t at = 0; at < bytes.size(); at += 3)
        {
            const std::size_t count { std::min<std::size_t>(3, bytes.size() - at) };
            std::uint32_t group { static_cast<std::uint32_t>(bytes[at]) << 16 };
            if (count > 1)
                group |= static_cast<std::uint32_t>(bytes[at + 1]) << 8;
            if (count > 2)
                group |= static_cast<std::uint32_t>(bytes[at + 2]);
            text.push_back(base64_alphabet[(group >> 18) & 0x3Fu]);
            text.push_back(base64_alphabet[(group >> 12) & 0x3Fu]);
            text.push_back(count > 1 ? base64_alphabet[(group >> 6) & 0x3Fu] : u8'=');
            text.push_back(count > 2 ? base64_alphabet[group & 0x3Fu] : u8'=');
        }
        return text;
    }

    std::optional<update_version> parse_update_version(const std::u8string_view text) noexcept
    {
        update_version version {};
        std::size_t count { 0 };
        std::size_t start { 0 };
        while (true)
        {
            const std::size_t dot { text.find(u8'.', start) };
            const std::u8string_view part { text.substr(start, dot == std::u8string_view::npos ? std::u8string_view::npos : dot - start) };
            std::uint64_t value { 0 };
            if (count == version.parts.size() || unsigned_text(part, value) == false || value > 0xFFFFFFFFull)
                return std::nullopt;
            version.parts[count] = static_cast<std::uint32_t>(value);
            ++count;
            if (dot == std::u8string_view::npos)
                break;
            start = dot + 1;
        }
        return version;
    }

    std::optional<update_digest> parse_update_digest(const std::u8string_view text) noexcept
    {
        if (text.size() != 64)
            return std::nullopt;
        update_digest digest {};
        for (std::size_t index = 0; index < digest.size(); ++index)
        {
            const int high { hex_value(text[index * 2]) };
            const int low { hex_value(text[index * 2 + 1]) };
            if (high < 0 || low < 0)
                return std::nullopt;
            digest[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return digest;
    }

    std::u8string format_update_digest(const update_digest& digest)
    {
        std::u8string text {};
        text.reserve(64);
        for (const std::uint8_t byte : digest)
        {
            text.push_back(hex_digits[byte >> 4]);
            text.push_back(hex_digits[byte & 0x0F]);
        }
        return text;
    }

    std::optional<update_envelope> parse_update_envelope(const std::span<const std::uint8_t> bytes, std::u8string& error)
    {
        const std::optional<nlohmann::json> root { parse_json(bytes, u8"The update envelope", error) };
        if (root.has_value() == false)
            return std::nullopt;
        const std::optional<std::u8string> format { string_member(*root, "format") };
        if (format.has_value() == false || *format != update_envelope_format)
        {
            error = u8"The update envelope format is not luil-update-envelope/1.";
            return std::nullopt;
        }
        const std::optional<std::u8string> payload_text { string_member(*root, "payload") };
        std::optional<std::vector<std::uint8_t>> payload { payload_text.has_value() ? decode_update_base64(*payload_text) : std::nullopt };
        if (payload.has_value() == false || payload->empty())
        {
            error = u8"The update envelope has no base64 payload.";
            return std::nullopt;
        }
        const nlohmann::json* const signatures { member(*root, "signatures") };
        if (signatures == nullptr || signatures->is_array() == false || signatures->empty() || signatures->size() > update_maximum_signatures)
        {
            error = u8"The update envelope needs between 1 and 8 signatures.";
            return std::nullopt;
        }

        update_envelope envelope {};
        envelope.payload = std::move(*payload);
        for (const nlohmann::json& item : *signatures)
        {
            const std::optional<std::u8string> key { item.is_object() ? string_member(item, "key") : std::nullopt };
            const bool has_raw { item.is_object() && member(item, "raw") != nullptr };
            const bool has_der { item.is_object() && member(item, "der") != nullptr };
            if (key.has_value() == false || key->empty() || has_raw == has_der)
            {
                error = u8"An update signature needs a key and exactly one of raw or der.";
                return std::nullopt;
            }
            const std::optional<std::u8string> value { string_member(item, has_raw ? "raw" : "der") };
            std::optional<std::vector<std::uint8_t>> signature { value.has_value() ? decode_update_base64(*value) : std::nullopt };
            // raw는 길이가 곧 꼴이다. der는 확인 때 엄격히 푼다 (깨진 DER은 "맞지 않는다"다).
            if (signature.has_value() == false || signature->empty() || (has_raw && signature->size() != 64) || (has_der && signature->size() > 72))
            {
                error = u8"An update signature is not valid base64 of the right length.";
                return std::nullopt;
            }
            envelope.signatures.push_back(update_signature_entry { *key, has_raw ? update_signature_form::raw : update_signature_form::der, std::move(*signature) });
        }
        return envelope;
    }

    std::vector<update_signature_match> match_update_signatures(const update_envelope& envelope, const std::span<const update_key> keys)
    {
        std::vector<update_signature_match> matches {};
        for (std::size_t signature = 0; signature < envelope.signatures.size(); ++signature)
            for (std::size_t key = 0; key < keys.size(); ++key)
                if (keys[key].id == envelope.signatures[signature].key)
                {
                    matches.push_back(update_signature_match { signature, key });
                    break;
                }
        return matches;
    }

    std::optional<update_raw_signature> update_signature_from_der(const std::span<const std::uint8_t> der) noexcept
    {
        if (der.empty() || der[0] != 0x30)
            return std::nullopt;
        std::size_t at { 1 };
        std::size_t length { 0 };
        if (read_short_length(der, at, length) == false || at + length != der.size())
            return std::nullopt;
        update_raw_signature raw {};
        if (read_scalar(der, at, raw.data()) == false || read_scalar(der, at, raw.data() + 32) == false || at != der.size())
            return std::nullopt;
        return raw;
    }

    std::vector<std::uint8_t> update_signature_to_der(const update_raw_signature& raw)
    {
        std::vector<std::uint8_t> body {};
        append_scalar(body, raw.data());
        append_scalar(body, raw.data() + 32);
        std::vector<std::uint8_t> der { 0x30, static_cast<std::uint8_t>(body.size()) };
        der.insert(der.end(), body.begin(), body.end());
        return der;
    }

    bool update_signature_scalars_nonzero(const update_raw_signature& raw) noexcept
    {
        const auto zero { [](const std::uint8_t byte) { return byte == 0; } };
        return std::all_of(raw.begin(), raw.begin() + 32, zero) == false && std::all_of(raw.begin() + 32, raw.end(), zero) == false;
    }

    std::optional<update_public_point> update_spki_point(const std::span<const std::uint8_t> spki) noexcept
    {
        if (spki.size() != p256_spki_header.size() + 64 || std::equal(p256_spki_header.begin(), p256_spki_header.end(), spki.begin()) == false)
            return std::nullopt;
        update_public_point point {};
        std::copy(spki.begin() + p256_spki_header.size(), spki.end(), point.begin());
        return point;
    }

    update_platform current_update_platform() noexcept
    {
#if defined(__ANDROID__)
        return update_platform::android;
#elif defined(_M_ARM64) || defined(__aarch64__)
        return update_platform::windows_arm64;
#else
        return update_platform::windows_x64;
#endif
    }

    std::u8string_view update_platform_key(const update_platform platform) noexcept
    {
        switch (platform)
        {
        case update_platform::windows_x64:
            return u8"windows-x64";
        case update_platform::windows_arm64:
            return u8"windows-arm64";
        case update_platform::android:
            return u8"android";
        }
        return {};
    }

    const std::optional<update_asset>& update_manifest::asset(const update_platform platform) const noexcept
    {
        switch (platform)
        {
        case update_platform::windows_x64:
            return windows_x64;
        case update_platform::windows_arm64:
            return windows_arm64;
        case update_platform::android:
            return android;
        }
        return windows_x64;
    }

    std::optional<update_manifest> parse_update_manifest(const std::span<const std::uint8_t> payload, std::u8string& error)
    {
        const std::optional<nlohmann::json> root { parse_json(payload, u8"The update manifest", error) };
        if (root.has_value() == false)
            return std::nullopt;
        const std::optional<std::uint64_t> format { unsigned_member(*root, "format") };
        if (format.has_value() == false || *format != 1)
        {
            error = u8"The update manifest format is not 1.";
            return std::nullopt;
        }

        update_manifest manifest {};
        const std::optional<std::u8string> app { string_member(*root, "app") };
        const std::optional<std::u8string> channel { string_member(*root, "channel") };
        const std::optional<std::uint64_t> sequence { unsigned_member(*root, "sequence") };
        const std::optional<std::u8string> version { string_member(*root, "version") };
        const std::optional<update_version> parsed_version { version.has_value() ? parse_update_version(*version) : std::nullopt };
        if (app.has_value() == false || app->empty() || channel.has_value() == false || channel->empty() || sequence.has_value() == false || parsed_version.has_value() == false)
        {
            error = u8"The update manifest needs an app, a channel, an unsigned sequence and a dotted version.";
            return std::nullopt;
        }
        manifest.app = *app;
        manifest.channel = *channel;
        manifest.sequence = *sequence;
        manifest.version_text = *version;
        manifest.version = *parsed_version;

        // 설명 칸은 없어도 된다. 있으면 꼴이 맞아야 한다.
        if (const nlohmann::json* const notes { member(*root, "notes") }; notes != nullptr)
        {
            if (notes->is_string() == false)
            {
                error = u8"The update manifest notes are not a string.";
                return std::nullopt;
            }
            manifest.notes = from_json_string(*notes);
        }
        if (const nlohmann::json* const notes_url { member(*root, "notes_url") }; notes_url != nullptr)
        {
            const std::optional<std::u8string> url { notes_url->is_string() ? std::optional<std::u8string> { from_json_string(*notes_url) } : std::nullopt };
            const std::optional<url_parts> parts { url.has_value() ? split_url(*url) : std::nullopt };
            if (parts.has_value() == false || ascii_equal_ignoring_case(parts->scheme, u8"https") == false || host_of(parts->authority).empty()
                || parts->authority.find(u8'@') != std::u8string_view::npos)
            {
                error = u8"The update manifest notes_url is not an https address.";
                return std::nullopt;
            }
            manifest.notes_url = *url;
        }

        const nlohmann::json* const assets { member(*root, "assets") };
        if (assets == nullptr || assets->is_object() == false)
        {
            error = u8"The update manifest has no assets object.";
            return std::nullopt;
        }
        for (const update_platform platform : { update_platform::windows_x64, update_platform::windows_arm64, update_platform::android })
        {
            const std::u8string_view key { update_platform_key(platform) };
            const auto found { assets->find(std::string { reinterpret_cast<const char*>(key.data()), key.size() }) };
            if (found == assets->end())
                continue;
            std::optional<update_asset> asset { parse_asset(*found, platform != update_platform::android, key, error) };
            if (asset.has_value() == false)
                return std::nullopt;
            switch (platform)
            {
            case update_platform::windows_x64:
                manifest.windows_x64 = std::move(asset);
                break;
            case update_platform::windows_arm64:
                manifest.windows_arm64 = std::move(asset);
                break;
            case update_platform::android:
                manifest.android = std::move(asset);
                break;
            }
        }
        return manifest;
    }

    update_verdict judge_update_manifest(const update_manifest& manifest, const update_judgement& judgement) noexcept
    {
        if (manifest.app != judgement.app || manifest.channel != judgement.channel)
            return update_verdict::mismatch;
        if (manifest.sequence < judgement.seen_sequence)
            return update_verdict::replayed;
        // 낮은 판은 어떤 경우에도 내주지 않는다.
        if ((manifest.version > judgement.current) == false)
            return update_verdict::up_to_date;
        // 되돌린 판은 더 새 판이 나올 때까지 내주지 않는다 (적용 문서 결정 9).
        if (judgement.skipped.has_value() && (manifest.version > *judgement.skipped) == false)
            return update_verdict::up_to_date;
        const std::optional<update_asset>& asset { manifest.asset(judgement.platform) };
        if (asset.has_value() == false)
            return update_verdict::no_asset;
        if (judgement.platform != update_platform::android && asset->size > judgement.max_asset_bytes)
            return update_verdict::too_large;
        return update_verdict::available;
    }

    bool update_url_allowed(const std::u8string_view url) noexcept
    {
        const std::optional<url_parts> parts { split_url(url) };
        if (parts.has_value() == false || parts->authority.find(u8'@') != std::u8string_view::npos)
            return false;
        const std::u8string_view host { host_of(parts->authority) };
        if (host.empty())
            return false;
        if (ascii_equal_ignoring_case(parts->scheme, u8"https"))
            return true;
        return ascii_equal_ignoring_case(parts->scheme, u8"http") && loopback_host(host);
    }

    std::optional<std::u8string> resolve_update_asset_url(const std::u8string_view asset_url, const std::u8string_view manifest_final_url)
    {
        if (asset_url.find(u8"://") != std::u8string_view::npos)
        {
            if (update_url_allowed(asset_url) == false)
                return std::nullopt;
            return std::u8string { asset_url };
        }
        if (asset_url.empty() || std::all_of(asset_url.begin(), asset_url.end(), asset_name_character) == false
            || std::all_of(asset_url.begin(), asset_url.end(), [](const char8_t character) { return character == u8'.'; }))
            return std::nullopt;
        const std::optional<url_parts> parts { split_url(manifest_final_url) };
        if (parts.has_value() == false)
            return std::nullopt;
        // 질의와 조각은 매니페스트의 것이라 버린다. 경로의 마지막 조각만 이름으로 바꾼다.
        std::u8string_view path { manifest_final_url.substr(parts->rest) };
        path = path.substr(0, path.find_first_of(u8"?#"));
        const std::size_t slash { path.rfind(u8'/') };
        std::u8string resolved { manifest_final_url.substr(0, parts->rest) };
        resolved += slash == std::u8string_view::npos ? std::u8string_view { u8"/" } : path.substr(0, slash + 1);
        resolved += asset_url;
        if (update_url_allowed(resolved) == false)
            return std::nullopt;
        return resolved;
    }

    std::optional<update_content_range> parse_update_content_range(const std::u8string_view value) noexcept
    {
        constexpr std::u8string_view unit { u8"bytes " };
        if (value.size() <= unit.size() || ascii_equal_ignoring_case(value.substr(0, unit.size()), unit) == false)
            return std::nullopt;
        const std::u8string_view rest { value.substr(unit.size()) };
        const std::size_t dash { rest.find(u8'-') };
        const std::size_t slash { rest.find(u8'/') };
        if (dash == std::u8string_view::npos || slash == std::u8string_view::npos || dash > slash)
            return std::nullopt;
        update_content_range range {};
        if (unsigned_text(rest.substr(0, dash), range.first) == false || unsigned_text(rest.substr(dash + 1, slash - dash - 1), range.last) == false
            || unsigned_text(rest.substr(slash + 1), range.total) == false)
            return std::nullopt;
        if (range.first > range.last || range.last >= range.total)
            return std::nullopt;
        return range;
    }

    std::u8string make_update_range(const std::uint64_t first, const std::uint64_t last)
    {
        std::u8string value { u8"bytes=" };
        append_number(value, first);
        value.push_back(u8'-');
        append_number(value, last);
        return value;
    }

    std::optional<update_state> parse_update_state(const std::span<const std::uint8_t> bytes)
    {
        std::u8string ignored {};
        const std::optional<nlohmann::json> root { parse_json(bytes, u8"The update state", ignored) };
        if (root.has_value() == false)
            return std::nullopt;
        const std::optional<std::uint64_t> sequence { unsigned_member(*root, "sequence") };
        if (sequence.has_value() == false)
            return std::nullopt;
        update_state state { *sequence, {} };
        if (const nlohmann::json* const skipped { member(*root, "skipped_version") }; skipped != nullptr)
        {
            if (skipped->is_string() == false)
                return std::nullopt;
            state.skipped_version = from_json_string(*skipped);
            if (parse_update_version(state.skipped_version).has_value() == false)
                return std::nullopt;
        }
        return state;
    }

    std::vector<std::uint8_t> serialize_update_state(const update_state& state)
    {
        nlohmann::json root {};
        root["format"] = 1;
        root["sequence"] = state.sequence;
        if (state.skipped_version.empty() == false)
            root["skipped_version"] = to_ascii(state.skipped_version);
        return dump_bytes(root);
    }

    std::vector<std::u8string> make_update_helper_arguments(const update_helper_arguments& arguments)
    {
        std::vector<std::u8string> list {};
        list.reserve(12 + arguments.restart_arguments.size());
        list.emplace_back(update_helper_flag);
        list.emplace_back(update_helper_format);
        list.push_back(arguments.old_executable);
        list.push_back(hex_number(arguments.parent_handle));
        list.push_back(arguments.download_directory);
        list.push_back(format_update_digest(arguments.sha256));
        list.push_back(arguments.from_version);
        list.push_back(arguments.to_version);
        list.push_back(decimal_number(arguments.parent_timeout_ms));
        list.push_back(decimal_number(arguments.confirm_timeout_ms));
        list.emplace_back(arguments.restart ? u8"1" : u8"0");
        list.emplace_back(update_helper_separator);
        list.insert(list.end(), arguments.restart_arguments.begin(), arguments.restart_arguments.end());
        return list;
    }

    update_helper_parse_result parse_update_helper_arguments(const std::span<const std::u8string> arguments)
    {
        update_helper_parse_result result {};
        if (arguments.size() < 2 || arguments[1] != update_helper_flag)
            return result;
        result.kind = update_helper_parse::malformed;
        // 고정 칸은 [0] 실행 파일, [1] 표식, [2] 꼴 번호, [3]~[11] 값 아홉, [12] `--`다.
        constexpr std::size_t separator_index { 12 };
        if (arguments.size() < 3 || arguments[2] != update_helper_format)
        {
            result.error = u8"The update helper arguments use an unknown format number.";
            return result;
        }
        if (arguments.size() <= separator_index || arguments[separator_index] != update_helper_separator)
        {
            result.error = u8"The update helper arguments have missing or extra fields before --.";
            return result;
        }
        update_helper_arguments& value { result.arguments };
        value.old_executable = arguments[3];
        value.download_directory = arguments[5];
        value.from_version = arguments[7];
        value.to_version = arguments[8];
        const std::optional<std::uint64_t> handle { parse_hex_number(arguments[4]) };
        const std::optional<update_digest> digest { parse_update_digest(arguments[6]) };
        std::uint64_t parent_ms { 0 };
        std::uint64_t confirm_ms { 0 };
        const bool timeouts_valid {
            unsigned_text(arguments[9], parent_ms) && unsigned_text(arguments[10], confirm_ms) && parent_ms > 0 && parent_ms <= update_helper_maximum_timeout_ms
            && confirm_ms <= update_helper_maximum_timeout_ms
        };
        const bool restart_valid { arguments[11] == u8"0" || arguments[11] == u8"1" };
        if (value.old_executable.empty() || value.download_directory.empty() || value.old_executable.find(u8'\0') != std::u8string::npos
            || value.download_directory.find(u8'\0') != std::u8string::npos || handle.has_value() == false || *handle == 0 || digest.has_value() == false
            || parse_update_version(value.from_version).has_value() == false || parse_update_version(value.to_version).has_value() == false || timeouts_valid == false
            || restart_valid == false)
        {
            result.error = u8"The update helper arguments have an empty or malformed field.";
            return result;
        }
        value.parent_handle = *handle;
        value.sha256 = *digest;
        value.parent_timeout_ms = parent_ms;
        value.confirm_timeout_ms = confirm_ms;
        value.restart = arguments[11] == u8"1";
        value.restart_arguments.assign(arguments.begin() + static_cast<std::ptrdiff_t>(separator_index + 1), arguments.end());
        result.kind = update_helper_parse::helper;
        return result;
    }

    std::u8string_view update_apply_outcome_name(const update_apply_outcome outcome) noexcept
    {
        switch (outcome)
        {
        case update_apply_outcome::replaced:
            return u8"replaced";
        case update_apply_outcome::rolled_back:
            return u8"rolled_back";
        case update_apply_outcome::replace_failed:
            return u8"replace_failed";
        case update_apply_outcome::parent_timeout:
            return u8"parent_timeout";
        }
        return u8"replaced";
    }

    std::optional<update_apply_record> parse_update_apply_record(const std::span<const std::uint8_t> bytes)
    {
        std::u8string ignored {};
        const std::optional<nlohmann::json> root { parse_json(bytes, u8"The update apply result", ignored) };
        if (root.has_value() == false)
            return std::nullopt;
        const std::optional<std::uint64_t> format { unsigned_member(*root, "format") };
        const std::optional<std::u8string> outcome_text { string_member(*root, "outcome") };
        const std::optional<std::u8string> from { string_member(*root, "from") };
        const std::optional<std::u8string> to { string_member(*root, "to") };
        const std::optional<std::u8string> digest_text { string_member(*root, "sha256") };
        const std::optional<std::u8string> error { string_member(*root, "error") };
        const std::optional<update_digest> digest { digest_text.has_value() ? parse_update_digest(*digest_text) : std::nullopt };
        if (format.has_value() == false || *format != 1 || outcome_text.has_value() == false || from.has_value() == false || to.has_value() == false
            || digest.has_value() == false || error.has_value() == false)
            return std::nullopt;
        update_apply_record record {};
        bool known { false };
        for (const update_apply_outcome outcome :
            { update_apply_outcome::replaced, update_apply_outcome::rolled_back, update_apply_outcome::replace_failed, update_apply_outcome::parent_timeout })
        {
            if (*outcome_text == update_apply_outcome_name(outcome))
            {
                record.result.outcome = outcome;
                known = true;
            }
        }
        if (known == false)
            return std::nullopt;
        record.result.from_version = *from;
        record.result.to_version = *to;
        record.result.error = *error;
        record.sha256 = *digest;
        return record;
    }

    std::vector<std::uint8_t> serialize_update_apply_record(const update_apply_record& record)
    {
        nlohmann::json root {};
        root["format"] = 1;
        root["outcome"] = to_ascii(update_apply_outcome_name(record.result.outcome));
        root["from"] = to_ascii(record.result.from_version);
        root["to"] = to_ascii(record.result.to_version);
        root["sha256"] = to_ascii(format_update_digest(record.sha256));
        root["error"] = to_ascii(record.result.error);
        return dump_bytes(root);
    }
}

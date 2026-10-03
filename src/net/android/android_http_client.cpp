#include "luil/net/http_client.h"

#include "android/java_vm.h"
#include "luil/messaging/channel.h"
#include "luil/net/http_body.h"
#include "luil/net/http_media_type.h"
#include "luil/text/utf8_text.h"
#include "net/http_request_rules.h"

#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luil::net {
    namespace {
        using namespace std::chrono_literals;

        // pump가 할 일이 없을 때 자는 상한이다 (Windows 백엔드와 같은 값이다).
        constexpr std::chrono::milliseconds pump_idle_wait { 250 };
        // 완료가 끊이지 않을 때도 마감·회차·끊기 재시도를 이만큼마다는 훑는다.
        constexpr std::chrono::milliseconds pump_sweep_interval { 50 };
        // `stop()`이 기다리는 동안 끊기를 다시 거는 간격이다.
        constexpr std::chrono::milliseconds stop_retry_interval { 50 };
        // 몸을 읽는 조각의 크기다.
        constexpr jint read_chunk_bytes { 64 * 1024 };
        // 미리 잡는 몸 크기의 상한이다. `Content-Length: 4 GB`가 메모리를 예약하게 두지 않는다.
        constexpr std::size_t body_reserve_limit_bytes { 1024u * 1024u };

        std::atomic<std::uint64_t> request_counter { 0 };

        // 표와 요청 번호를 함께 낸다. 0은 "받아들여지지 않았다"는 뜻이라 1부터 준다.
        [[nodiscard]] std::uint64_t issue_request_id() noexcept
        {
            return request_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        // worker가 지어 pump로 건너가는 완료 하나다. 몸을 푸는 것은 pump의 일이다.
        struct completion
        {
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            int status_code { 0 };
            std::u8string reason {};
            std::u8string final_url {};
            std::vector<http_header> headers {};
            std::vector<std::uint8_t> body {};
            http_error error {};
            http_body_parse_options parse {};
            std::chrono::milliseconds elapsed { 0 };
        };

        // 프로세스에 한 벌 붙잡아 두는 Java 클래스와 메서드다.
        //
        // worker는 native thread라 `FindClass`가 시스템 class loader로 찾는다. 여기 쓰는 것은
        // 전부 `java.*`·`javax.*`라 그것으로 충분하고, 그래도 thread마다 찾지 않도록 한 번에 잡는다.
        struct java_api
        {
            jclass url { nullptr };
            jclass http_connection { nullptr };
            jclass input_stream { nullptr };
            jclass gzip_stream { nullptr };
            jclass inflater_stream { nullptr };
            jclass inflater { nullptr };
            jclass pushback_stream { nullptr };
            jclass output_stream { nullptr };
            jclass object { nullptr };
            jclass klass { nullptr };
            jclass throwable { nullptr };
            jclass unknown_host { nullptr };
            jclass connect_failure { nullptr };
            jclass no_route { nullptr };
            jclass port_unreachable { nullptr };
            jclass socket_timeout { nullptr };
            jclass ssl_failure { nullptr };
            jclass security_policy { nullptr };
            jclass illegal_argument { nullptr };
            jobject no_proxy { nullptr };
            // 앱의 network security config다. 평문 http를 보내도 되는지 묻는다.
            jobject cleartext_policy { nullptr };

            jmethodID url_new { nullptr };
            jmethodID url_new_relative { nullptr };
            jmethodID url_open { nullptr };
            jmethodID url_open_proxy { nullptr };
            jmethodID url_to_string { nullptr };
            jmethodID url_get_host { nullptr };
            jmethodID cleartext_permitted { nullptr };

            jmethodID set_request_method { nullptr };
            jmethodID set_follow_redirects { nullptr };
            jmethodID set_connect_timeout { nullptr };
            jmethodID set_read_timeout { nullptr };
            jmethodID set_use_caches { nullptr };
            jmethodID set_do_output { nullptr };
            jmethodID set_fixed_length { nullptr };
            jmethodID set_request_property { nullptr };
            jmethodID get_output_stream { nullptr };
            jmethodID get_response_code { nullptr };
            jmethodID get_response_message { nullptr };
            jmethodID get_header_key { nullptr };
            jmethodID get_header_value { nullptr };
            jmethodID get_input_stream { nullptr };
            jmethodID get_error_stream { nullptr };
            jmethodID disconnect { nullptr };

            jmethodID input_read { nullptr };
            jmethodID gzip_new { nullptr };
            jmethodID inflater_stream_new { nullptr };
            jmethodID inflater_new { nullptr };
            jmethodID inflater_end { nullptr };
            jmethodID pushback_new { nullptr };
            jmethodID pushback_read { nullptr };
            jmethodID pushback_unread { nullptr };
            jmethodID input_close { nullptr };
            jmethodID output_write { nullptr };
            jmethodID output_close { nullptr };

            jmethodID object_get_class { nullptr };
            jmethodID class_get_name { nullptr };
            jmethodID throwable_get_message { nullptr };
        };

        std::mutex java_api_mutex {};
        java_api java_api_instance {};
        bool java_api_ready { false };

        [[nodiscard]] bool global_class(JNIEnv* const env, const char* const name, jclass& slot) noexcept
        {
            const jclass local { env->FindClass(name) };
            if (local == nullptr || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return false;
            }
            slot = static_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
            return slot != nullptr;
        }

        [[nodiscard]] bool method(JNIEnv* const env, const jclass owner, const char* const name, const char* const signature, jmethodID& slot) noexcept
        {
            slot = env->GetMethodID(owner, name, signature);
            if (slot == nullptr || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return false;
            }
            return true;
        }

        struct class_slot
        {
            const char* name { nullptr };
            jclass* slot { nullptr };
        };

        struct method_slot
        {
            const jclass* owner { nullptr };
            const char* name { nullptr };
            const char* signature { nullptr };
            jmethodID* slot { nullptr };
        };

        [[nodiscard]] bool load_java_api(JNIEnv* const env, java_api& api) noexcept
        {
            const std::array classes {
                class_slot { "java/net/URL", &api.url },
                class_slot { "java/net/HttpURLConnection", &api.http_connection },
                class_slot { "java/io/InputStream", &api.input_stream },
                class_slot { "java/util/zip/GZIPInputStream", &api.gzip_stream },
                class_slot { "java/util/zip/InflaterInputStream", &api.inflater_stream },
                class_slot { "java/util/zip/Inflater", &api.inflater },
                class_slot { "java/io/PushbackInputStream", &api.pushback_stream },
                class_slot { "java/io/OutputStream", &api.output_stream },
                class_slot { "java/lang/Object", &api.object },
                class_slot { "java/lang/Class", &api.klass },
                class_slot { "java/lang/Throwable", &api.throwable },
                class_slot { "java/net/UnknownHostException", &api.unknown_host },
                class_slot { "java/net/ConnectException", &api.connect_failure },
                class_slot { "java/net/NoRouteToHostException", &api.no_route },
                class_slot { "java/net/PortUnreachableException", &api.port_unreachable },
                class_slot { "java/net/SocketTimeoutException", &api.socket_timeout },
                class_slot { "javax/net/ssl/SSLException", &api.ssl_failure },
                class_slot { "android/security/NetworkSecurityPolicy", &api.security_policy },
                class_slot { "java/lang/IllegalArgumentException", &api.illegal_argument },
            };
            for (const class_slot& entry : classes)
                if (global_class(env, entry.name, *entry.slot) == false)
                    return false;

            const std::array methods {
                method_slot { &api.url, "<init>", "(Ljava/lang/String;)V", &api.url_new },
                method_slot { &api.url, "<init>", "(Ljava/net/URL;Ljava/lang/String;)V", &api.url_new_relative },
                method_slot { &api.url, "openConnection", "()Ljava/net/URLConnection;", &api.url_open },
                method_slot { &api.url, "openConnection", "(Ljava/net/Proxy;)Ljava/net/URLConnection;", &api.url_open_proxy },
                method_slot { &api.url, "toString", "()Ljava/lang/String;", &api.url_to_string },
                method_slot { &api.url, "getHost", "()Ljava/lang/String;", &api.url_get_host },
                method_slot { &api.security_policy, "isCleartextTrafficPermitted", "(Ljava/lang/String;)Z", &api.cleartext_permitted },
                method_slot { &api.http_connection, "setRequestMethod", "(Ljava/lang/String;)V", &api.set_request_method },
                method_slot { &api.http_connection, "setInstanceFollowRedirects", "(Z)V", &api.set_follow_redirects },
                method_slot { &api.http_connection, "setConnectTimeout", "(I)V", &api.set_connect_timeout },
                method_slot { &api.http_connection, "setReadTimeout", "(I)V", &api.set_read_timeout },
                method_slot { &api.http_connection, "setUseCaches", "(Z)V", &api.set_use_caches },
                method_slot { &api.http_connection, "setDoOutput", "(Z)V", &api.set_do_output },
                method_slot { &api.http_connection, "setFixedLengthStreamingMode", "(J)V", &api.set_fixed_length },
                method_slot { &api.http_connection, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", &api.set_request_property },
                method_slot { &api.http_connection, "getOutputStream", "()Ljava/io/OutputStream;", &api.get_output_stream },
                method_slot { &api.http_connection, "getResponseCode", "()I", &api.get_response_code },
                method_slot { &api.http_connection, "getResponseMessage", "()Ljava/lang/String;", &api.get_response_message },
                method_slot { &api.http_connection, "getHeaderFieldKey", "(I)Ljava/lang/String;", &api.get_header_key },
                method_slot { &api.http_connection, "getHeaderField", "(I)Ljava/lang/String;", &api.get_header_value },
                method_slot { &api.http_connection, "getInputStream", "()Ljava/io/InputStream;", &api.get_input_stream },
                method_slot { &api.http_connection, "getErrorStream", "()Ljava/io/InputStream;", &api.get_error_stream },
                method_slot { &api.http_connection, "disconnect", "()V", &api.disconnect },
                method_slot { &api.input_stream, "read", "([BII)I", &api.input_read },
                method_slot { &api.gzip_stream, "<init>", "(Ljava/io/InputStream;)V", &api.gzip_new },
                method_slot { &api.inflater_stream, "<init>", "(Ljava/io/InputStream;Ljava/util/zip/Inflater;)V", &api.inflater_stream_new },
                method_slot { &api.inflater, "<init>", "(Z)V", &api.inflater_new },
                method_slot { &api.inflater, "end", "()V", &api.inflater_end },
                method_slot { &api.pushback_stream, "<init>", "(Ljava/io/InputStream;I)V", &api.pushback_new },
                method_slot { &api.pushback_stream, "read", "()I", &api.pushback_read },
                method_slot { &api.pushback_stream, "unread", "(I)V", &api.pushback_unread },
                method_slot { &api.input_stream, "close", "()V", &api.input_close },
                method_slot { &api.output_stream, "write", "([BII)V", &api.output_write },
                method_slot { &api.output_stream, "close", "()V", &api.output_close },
                method_slot { &api.object, "getClass", "()Ljava/lang/Class;", &api.object_get_class },
                method_slot { &api.klass, "getName", "()Ljava/lang/String;", &api.class_get_name },
                method_slot { &api.throwable, "getMessage", "()Ljava/lang/String;", &api.throwable_get_message },
            };
            for (const method_slot& entry : methods)
                if (method(env, *entry.owner, entry.name, entry.signature, *entry.slot) == false)
                    return false;

            jclass proxy { nullptr };
            if (global_class(env, "java/net/Proxy", proxy) == false)
                return false;
            const jfieldID no_proxy_field { env->GetStaticFieldID(proxy, "NO_PROXY", "Ljava/net/Proxy;") };
            if (no_proxy_field == nullptr || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return false;
            }
            const jobject no_proxy { env->GetStaticObjectField(proxy, no_proxy_field) };
            api.no_proxy = no_proxy == nullptr ? nullptr : env->NewGlobalRef(no_proxy);
            env->DeleteLocalRef(no_proxy);
            if (api.no_proxy == nullptr)
                return false;

            // 정책은 앱이 시작할 때 서고 바뀌지 않는다 (네이티브 라이브러리가 열리기 전이다).
            const jmethodID get_instance { env->GetStaticMethodID(api.security_policy, "getInstance", "()Landroid/security/NetworkSecurityPolicy;") };
            if (get_instance == nullptr || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return false;
            }
            const jobject policy { env->CallStaticObjectMethod(api.security_policy, get_instance) };
            if (policy == nullptr || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return false;
            }
            api.cleartext_policy = env->NewGlobalRef(policy);
            env->DeleteLocalRef(policy);
            return api.cleartext_policy != nullptr;
        }

        // 한 번 잡은 것은 프로세스가 끝날 때까지 둔다. 반쯤 잡다 실패하면 다음 `create`가 다시 한다.
        [[nodiscard]] const java_api* acquire_java_api(JNIEnv* const env) noexcept
        {
            std::lock_guard lock { java_api_mutex };
            if (java_api_ready)
                return &java_api_instance;
            java_api loaded {};
            if (load_java_api(env, loaded) == false)
                return nullptr;
            java_api_instance = loaded;
            java_api_ready = true;
            return &java_api_instance;
        }

        // UTF-8 글을 Java 문자열로 옮긴다. 올바른 UTF-8이 아니면 nullptr이다.
        //
        // `NewStringUTF`는 modified UTF-8을 받아 BMP 밖 글자를 잘못 읽으므로 UTF-16을 손으로 짓는다.
        [[nodiscard]] jstring to_java(JNIEnv* const env, const std::u8string_view text)
        {
            if (text::utf8_is_valid(text) == false)
                return nullptr;

            std::u16string units {};
            units.reserve(text.size());
            std::size_t offset { 0 };
            while (offset < text.size())
            {
                const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
                offset += decoded.length;
                if (decoded.codepoint >= 0x10000u)
                {
                    const char32_t value { decoded.codepoint - 0x10000u };
                    units.push_back(static_cast<char16_t>(0xD800u + (value >> 10u)));
                    units.push_back(static_cast<char16_t>(0xDC00u + (value & 0x3FFu)));
                }
                else
                {
                    units.push_back(static_cast<char16_t>(decoded.codepoint));
                }
            }
            return env->NewString(reinterpret_cast<const jchar*>(units.data()), static_cast<jsize>(units.size()));
        }

        // Java 문자열을 UTF-8로 옮긴다. 짝 없는 surrogate는 U+FFFD가 된다 (밖에서 온 값이다).
        [[nodiscard]] std::u8string from_java(JNIEnv* const env, const jstring text)
        {
            if (text == nullptr)
                return {};
            const jsize length { env->GetStringLength(text) };
            std::u16string units(static_cast<std::size_t>(length), u'\0');
            env->GetStringRegion(text, 0, length, reinterpret_cast<jchar*>(units.data()));

            std::u8string output {};
            output.reserve(units.size());
            for (std::size_t index { 0 }; index < units.size(); ++index)
            {
                const char16_t unit { units[index] };
                char32_t codepoint { unit };
                if (unit >= 0xD800u && unit <= 0xDBFFu && index + 1u < units.size() && units[index + 1u] >= 0xDC00u && units[index + 1u] <= 0xDFFFu)
                {
                    codepoint = 0x10000u + ((static_cast<char32_t>(unit) - 0xD800u) << 10u) + (static_cast<char32_t>(units[index + 1u]) - 0xDC00u);
                    ++index;
                }
                else if (unit >= 0xD800u && unit <= 0xDFFFu)
                {
                    codepoint = text::utf8_replacement_character;
                }
                output += text::utf8_encode(codepoint);
            }
            return output;
        }

        [[nodiscard]] bool same_ascii_ci(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index { 0 }; index < left.size(); ++index)
            {
                const auto lower = [](const char8_t byte) { return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte; };
                if (lower(left[index]) != lower(right[index]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool is_scheme_character(const char8_t byte) noexcept
        {
            return (byte >= u8'a' && byte <= u8'z') || (byte >= u8'A' && byte <= u8'Z') || (byte >= u8'0' && byte <= u8'9') || byte == u8'+' || byte == u8'-' || byte == u8'.';
        }

        // URL의 출처다 (scheme·host·port). 재지정이 다른 출처로 가는지 견주는 데 쓴다.
        struct url_origin
        {
            bool valid { false };
            bool secure { false };
            std::u8string_view host {};
            unsigned int port { 0 };
        };

        // 절대 http·https URL을 출처까지만 가른다. 경로와 질의는 Java의 `URL`이 읽는다.
        //
        // 선을 건드리기 **전**에 거절할 것을 가르는 자리다 (`send`가 동기 실패로 답한다). 조각을
        // 실제로 읽는 규칙은 `java.net.URL`이고, 여기서 통과한 것을 그것이 거절하면 답으로 온다.
        [[nodiscard]] url_origin parse_origin(const std::u8string_view url, http_error& failure)
        {
            url_origin origin {};
            const std::size_t scheme_end { url.find(u8':') };
            const bool has_scheme { scheme_end != std::u8string_view::npos && scheme_end > 0 && std::all_of(url.begin(), url.begin() + static_cast<std::ptrdiff_t>(scheme_end), is_scheme_character) };
            if (has_scheme == false)
            {
                failure = make_http_error(http_error_kind::invalid_url, u8"The URL is not an absolute http or https URL.");
                return origin;
            }

            const std::u8string_view scheme { url.substr(0, scheme_end) };
            if (same_ascii_ci(scheme, u8"https"))
                origin.secure = true;
            else if (same_ascii_ci(scheme, u8"http") == false)
            {
                failure = make_http_error(http_error_kind::unsupported_scheme, u8"Only http and https URLs are supported.");
                return origin;
            }

            const std::u8string_view rest { url.substr(scheme_end + 1u) };
            if (rest.starts_with(u8"//") == false)
            {
                failure = make_http_error(http_error_kind::invalid_url, u8"The URL has no authority.");
                return origin;
            }
            for (const char8_t byte : url)
            {
                if (byte <= 0x20u || byte == 0x7Fu)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"The URL contains a space or a control character.");
                    return origin;
                }
            }

            std::u8string_view authority { rest.substr(2) };
            authority = authority.substr(0, authority.find_first_of(u8"/?#"));
            if (const std::size_t at { authority.rfind(u8'@') }; at != std::u8string_view::npos)
                authority.remove_prefix(at + 1u);

            std::u8string_view host { authority };
            std::u8string_view port_text {};
            if (host.starts_with(u8'['))
            {
                const std::size_t close { host.find(u8']') };
                if (close == std::u8string_view::npos)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"The URL host is not valid.");
                    return origin;
                }
                if (close + 1u < host.size())
                {
                    if (host[close + 1u] != u8':')
                    {
                        failure = make_http_error(http_error_kind::invalid_url, u8"The URL host is not valid.");
                        return origin;
                    }
                    port_text = host.substr(close + 2u);
                }
                host = host.substr(0, close + 1u);
            }
            else if (const std::size_t colon { host.rfind(u8':') }; colon != std::u8string_view::npos)
            {
                port_text = host.substr(colon + 1u);
                host = host.substr(0, colon);
            }
            if (host.empty())
            {
                failure = make_http_error(http_error_kind::invalid_url, u8"The URL has no host.");
                return origin;
            }

            origin.port = origin.secure ? 443u : 80u;
            if (port_text.empty() == false)
            {
                unsigned int port { 0 };
                const auto* const first { reinterpret_cast<const char*>(port_text.data()) };
                const auto [end, error] { std::from_chars(first, first + port_text.size(), port) };
                if (error != std::errc {} || end != first + port_text.size() || port == 0 || port > 65535u)
                {
                    failure = make_http_error(http_error_kind::invalid_url, u8"The URL port is not valid.");
                    return origin;
                }
                origin.port = port;
            }
            origin.host = host;
            origin.valid = true;
            return origin;
        }

        [[nodiscard]] bool same_origin(const url_origin& left, const url_origin& right) noexcept
        {
            return left.valid && right.valid && left.secure == right.secure && left.port == right.port && same_ascii_ci(left.host, right.host);
        }

        [[nodiscard]] jint timeout_value(const std::chrono::milliseconds limit) noexcept
        {
            // 0은 Java에서 "무한"이다. 우리 값에서 0 이하는 걸지 않는다는 뜻이라 뜻이 같다.
            if (limit <= 0ms)
                return 0;
            if (limit.count() > static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<jint>::max()))
                return std::numeric_limits<jint>::max();
            return static_cast<jint>(limit.count());
        }

        [[nodiscard]] std::size_t parse_content_length(const std::u8string_view text) noexcept
        {
            std::size_t value { 0 };
            const auto* const first { reinterpret_cast<const char*>(text.data()) };
            const auto [end, error] { std::from_chars(first, first + text.size(), value) };
            return error == std::errc {} && end == first + text.size() ? value : 0u;
        }

        // 요청 하나의 상태다. 소유자는 engine의 지도와 그 요청의 worker다.
        struct request_state
        {
            std::uint64_t id { 0 };
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            http_request request {};
            // 검사를 마친 요청 헤더다 (Content-Type 기본값까지 들었다).
            std::vector<http_header> headers {};
            url_origin origin {};
            std::chrono::steady_clock::time_point started {};
            std::chrono::steady_clock::time_point deadline {};
            bool has_deadline { false };

            // 연결·결말을 지킨다. 끊기(`disconnect`)는 이것을 쥔 채로 부른다 — worker가 연결의
            // 전역 참조를 지우는 것과 엇갈리지 않게 하려는 것이고, `disconnect`는 우리 코드로
            // 되돌아오지 않는다.
            std::mutex guard {};
            jobject connection { nullptr };
            bool outcome_latched { false };
            http_error outcome {};
            bool finished { false };
        };

        // 결말을 걸쇠에 적는다. **먼저 적은 것이 이긴다.** 적었으면 참이다.
        bool latch(request_state& state, const http_error& outcome) noexcept
        {
            std::lock_guard lock { state.guard };
            if (state.outcome_latched || state.finished)
                return false;
            state.outcome_latched = true;
            state.outcome = outcome;
            return true;
        }

        // 걸쇠가 적혀 있고 아직 끝나지 않았으면 연결을 끊는다.
        //
        // 연결이 서기 전의 `disconnect`는 아무 일도 하지 않는다 (HttpURLConnection의 계약).
        // 그 틈에 걸린 취소는 worker가 다음 단계 앞에서 걸쇠를 보거나, pump가 다시 끊어 잡는다.
        void disconnect_if_latched(JNIEnv* const env, const java_api& api, request_state& state) noexcept
        {
            std::lock_guard lock { state.guard };
            if (state.outcome_latched == false || state.finished || state.connection == nullptr || env == nullptr)
                return;
            env->CallVoidMethod(state.connection, api.disconnect);
            env->ExceptionClear();
        }

        [[nodiscard]] bool is_latched(request_state& state) noexcept
        {
            std::lock_guard lock { state.guard };
            return state.outcome_latched;
        }

        // Java 예외 하나를 꺼내 오류 값으로 옮긴다. `before_response`는 상태 줄을 받기 전인지다.
        [[nodiscard]] http_error take_exception(JNIEnv* const env, const java_api& api, const bool before_response)
        {
            const jthrowable thrown { env->ExceptionOccurred() };
            env->ExceptionClear();
            if (thrown == nullptr)
                return make_http_error(http_error_kind::system_error, u8"A Java call failed without an exception.");

            std::u8string message {};
            const jobject type { env->CallObjectMethod(thrown, api.object_get_class) };
            if (env->ExceptionCheck() == false && type != nullptr)
            {
                const auto name { static_cast<jstring>(env->CallObjectMethod(type, api.class_get_name)) };
                if (env->ExceptionCheck() == false)
                    message = from_java(env, name);
                env->ExceptionClear();
                env->DeleteLocalRef(name);
            }
            env->ExceptionClear();
            env->DeleteLocalRef(type);

            const auto detail { static_cast<jstring>(env->CallObjectMethod(thrown, api.throwable_get_message)) };
            if (env->ExceptionCheck() == false && detail != nullptr)
            {
                message += u8": ";
                message += from_java(env, detail);
            }
            env->ExceptionClear();
            env->DeleteLocalRef(detail);

            // 그 밖의 입출력 실패는 끊긴 것이다 (닫힌 소켓, 잘린 몸, 상태 줄 전에 닫힌 연결).
            http_error_kind kind { http_error_kind::connection_lost };
            if (env->IsInstanceOf(thrown, api.socket_timeout))
                kind = http_error_kind::timed_out;
            else if (env->IsInstanceOf(thrown, api.unknown_host))
                kind = http_error_kind::name_not_resolved;
            else if (env->IsInstanceOf(thrown, api.connect_failure) || env->IsInstanceOf(thrown, api.no_route) || env->IsInstanceOf(thrown, api.port_unreachable))
                kind = http_error_kind::cannot_connect;
            else if (env->IsInstanceOf(thrown, api.ssl_failure))
                kind = http_error_kind::secure_failure;
            else if (env->IsInstanceOf(thrown, api.illegal_argument))
                kind = before_response ? http_error_kind::invalid_header : http_error_kind::system_error;
            env->DeleteLocalRef(thrown);
            return make_http_error(kind, message);
        }

        // HttpURLConnection은 몸을 실으면 GET을 몰래 POST로 바꾸고 HEAD는 예외로 거절한다.
        // 앱이 고른 것과 다른 동사가 서버에 닿지 않게 선을 건드리기 전에 거절한다.
        [[nodiscard]] bool body_fits_method(const http_request& request, http_error& failure)
        {
            if (request.body.empty() || (request.method != http_method::get && request.method != http_method::head))
                return true;
            failure = make_http_error(http_error_kind::system_error, u8"Android's HttpURLConnection cannot send a body with GET or HEAD.");
            return false;
        }

        // 지역 참조 틀 하나다. 요청 하나가 쓰는 지역 참조를 한 번에 놓는다.
        class local_frame
        {
        public:
            local_frame(JNIEnv* const env, const jint capacity) noexcept
                : env_ { env }
                , pushed_ { env->PushLocalFrame(capacity) == 0 }
            {}

            local_frame(const local_frame&) = delete;
            local_frame(local_frame&&) = delete;
            local_frame& operator=(const local_frame&) = delete;
            local_frame& operator=(local_frame&&) = delete;

            ~local_frame()
            {
                if (pushed_)
                    env_->PopLocalFrame(nullptr);
            }

            [[nodiscard]] bool pushed() const noexcept
            {
                return pushed_;
            }

        private:
            JNIEnv* env_ { nullptr };
            bool pushed_ { false };
        };

        // HttpURLConnection 하나로 요청 하나를 끝까지 몬다. worker thread에서만 돈다.
        //
        // 재지정은 직접 따라간다. 플랫폼의 자동 재지정은 횟수가 붙박이이고, 몸 있는 요청을
        // 따라가지 않는 규칙·다른 출처로 갈 때 헤더를 지우는 규칙·https에서 http로 내려가지
        // 않는 규칙을 모른다.
        class request_runner
        {
        public:
            request_runner(JNIEnv* const env, const java_api& api, request_state& state, const http_client_config& configuration, completion& result) noexcept
                : env_ { env }
                , api_ { api }
                , state_ { state }
                , configuration_ { configuration }
                , result_ { result }
            {}

            [[nodiscard]] http_error run()
            {
                std::vector<http_header> headers { state_.headers };
                bool headers_cleared { false };
                int redirects { 0 };
                std::u8string current_url { state_.request.url };
                url_origin current_origin { state_.origin };
                http_method method { state_.request.method };
                result_.final_url = current_url;

                jobject url { nullptr };
                {
                    const jstring text { to_java(env_, current_url) };
                    if (text == nullptr)
                        return make_http_error(http_error_kind::invalid_url, u8"The URL is not valid UTF-8.");
                    url = env_->NewObject(api_.url, api_.url_new, text);
                    if (env_->ExceptionCheck())
                    {
                        env_->ExceptionClear();
                        return make_http_error(http_error_kind::invalid_url, u8"java.net.URL could not read the URL.");
                    }
                }

                while (true)
                {
                    http_error failure {};
                    if (open_connection(url, method, current_origin.secure, headers, failure) == false)
                        return failure;
                    if (exchange_head(failure) == false)
                        return failure;

                    // 재지정을 따라갈지 본다. 따라가지 않으면 이 답이 곧 답이다.
                    const int status { result_.status_code };
                    const bool redirect_status { status == 301 || status == 302 || status == 303 || status == 307 || status == 308 };
                    const std::u8string_view location { find_header(result_.headers, u8"location") };
                    if (redirect_status == false || location.empty() || state_.request.redirects != http_redirect_policy::follow || state_.request.body.empty() == false)
                        break;

                    const jstring location_text { to_java(env_, location) };
                    if (location_text == nullptr)
                        break;
                    const jobject next { env_->NewObject(api_.url, api_.url_new_relative, url, location_text) };
                    env_->DeleteLocalRef(location_text);
                    if (env_->ExceptionCheck() || next == nullptr)
                    {
                        env_->ExceptionClear();
                        break;
                    }
                    const auto next_text { static_cast<jstring>(env_->CallObjectMethod(next, api_.url_to_string)) };
                    if (env_->ExceptionCheck())
                    {
                        env_->ExceptionClear();
                        break;
                    }
                    std::u8string next_url { from_java(env_, next_text) };
                    env_->DeleteLocalRef(next_text);
                    http_error ignored {};
                    const url_origin next_origin { parse_origin(next_url, ignored) };
                    // https에서 http로 내려가는 걸음과 http·https가 아닌 곳은 따라가지 않는다. 그 3xx가 답이다.
                    if (next_origin.valid == false || (current_origin.secure && next_origin.secure == false))
                        break;

                    ++redirects;
                    if (redirects > state_.request.max_redirects)
                    {
                        release_connection(true);
                        return make_http_error(http_error_kind::too_many_redirects, u8"The response redirected more times than max_redirects allows.");
                    }
                    // 처음 출처를 벗어나는 첫 걸음에서 앱 헤더를 지운다 (http_redirect_policy::follow).
                    if (headers_cleared == false && headers.empty() == false && same_origin(state_.origin, next_origin) == false)
                    {
                        std::erase_if(headers, [](const http_header& header) { return http_header_crosses_origins(header.name) == false; });
                        headers_cleared = true;
                    }

                    // 303은 GET으로 바꾼다 (RFC 9110 15.4.4). 301·302의 POST도 브라우저·WinHTTP처럼
                    // GET으로 바꾼다. 몸 있는 요청은 여기에 닿지 않으므로 바꿀 동사만 남는다.
                    if ((status == 303 && method != http_method::head) || ((status == 301 || status == 302) && method == http_method::post))
                        method = http_method::get;

                    // 걸음마다 지역 참조를 놓는다. 요청 하나의 틀은 끝에서야 비므로, 재지정이 길면
                    // 지역 참조 표가 넘친다.
                    release_connection(true);
                    env_->DeleteLocalRef(url);
                    url = next;
                    current_url = std::move(next_url);
                    current_origin = parse_origin(current_url, ignored);
                    result_.final_url = current_url;
                    result_.status_code = 0;
                    result_.reason.clear();
                    result_.headers.clear();
                }

                http_error failure {};
                static_cast<void>(read_body(failure));
                // 끝까지 읽고 닫은 연결만 연결 풀로 돌려보낸다. 몸 없는 답과 실패한 답은 끊는다.
                release_connection(body_drained_ == false);
                return failure;
            }

        private:
            [[nodiscard]] bool check_call(http_error& failure, const bool before_response)
            {
                if (env_->ExceptionCheck() == false)
                    return true;
                failure = take_exception(env_, api_, before_response);
                return false;
            }

            [[nodiscard]] bool set_property(const std::u8string_view name, const std::u8string_view value, http_error& failure)
            {
                const jstring java_name { to_java(env_, name) };
                const jstring java_value { to_java(env_, value) };
                if (java_name == nullptr || java_value == nullptr)
                {
                    failure = make_http_error(http_error_kind::invalid_header, u8"A request header is not valid UTF-8.");
                    return false;
                }
                env_->CallVoidMethod(connection_, api_.set_request_property, java_name, java_value);
                env_->DeleteLocalRef(java_name);
                env_->DeleteLocalRef(java_value);
                return check_call(failure, true);
            }

            // 앱의 network security config가 이 host로 평문 http를 막는지 본다. 막히면 플랫폼이 뜻이
            // 드러나지 않는 IOException을 던지므로, 선을 건드리기 전에 물어 보안 정책의 거절로 답한다.
            [[nodiscard]] bool cleartext_permitted(const jobject url, http_error& failure)
            {
                const auto host { static_cast<jstring>(env_->CallObjectMethod(url, api_.url_get_host)) };
                if (check_call(failure, true) == false)
                    return false;
                const jboolean permitted { env_->CallBooleanMethod(api_.cleartext_policy, api_.cleartext_permitted, host) };
                if (check_call(failure, true) == false)
                {
                    env_->DeleteLocalRef(host);
                    return false;
                }
                if (permitted == JNI_TRUE)
                {
                    env_->DeleteLocalRef(host);
                    return true;
                }

                std::u8string message { u8"Cleartext HTTP traffic to " };
                message += from_java(env_, host);
                env_->DeleteLocalRef(host);
                message += u8" is not permitted by the app's network security config.";
                failure = make_http_error(http_error_kind::secure_failure, message);
                return false;
            }

            [[nodiscard]] bool open_connection(const jobject url, const http_method method, const bool secure, const std::vector<http_header>& headers, http_error& failure)
            {
                if (secure == false && cleartext_permitted(url, failure) == false)
                    return false;
                const jobject opened { configuration_.use_system_proxy ? env_->CallObjectMethod(url, api_.url_open) : env_->CallObjectMethod(url, api_.url_open_proxy, api_.no_proxy) };
                if (check_call(failure, true) == false)
                    return false;
                if (opened == nullptr || env_->IsInstanceOf(opened, api_.http_connection) == false)
                {
                    failure = make_http_error(http_error_kind::unsupported_scheme, u8"The URL did not open an HTTP connection.");
                    return false;
                }
                connection_ = opened;

                // 연결을 상태에 건다. 이 뒤로는 취소·마감·정지가 끊을 수 있다. 그 전에 걸쇠가
                // 적혔으면 선을 건드리지 않고 접는다 — POST가 취소된 뒤에 서버에 닿는 일이 없게 한다.
                {
                    std::lock_guard lock { state_.guard };
                    if (state_.outcome_latched)
                        return false;
                    state_.connection = env_->NewGlobalRef(opened);
                }

                env_->CallVoidMethod(connection_, api_.set_follow_redirects, JNI_FALSE);
                env_->CallVoidMethod(connection_, api_.set_use_caches, JNI_FALSE);
                env_->CallVoidMethod(connection_, api_.set_connect_timeout, timeout_value(state_.request.connect_timeout));
                env_->CallVoidMethod(connection_, api_.set_read_timeout, timeout_value(state_.request.receive_timeout));
                if (check_call(failure, true) == false)
                    return false;

                const jstring verb { to_java(env_, http_method_name(method)) };
                env_->CallVoidMethod(connection_, api_.set_request_method, verb);
                env_->DeleteLocalRef(verb);
                if (check_call(failure, true) == false)
                    return false;

                if (set_property(u8"User-Agent", configuration_.user_agent, failure) == false)
                    return false;
                for (const http_header& header : headers)
                    if (set_property(header.name, header.value, failure) == false)
                        return false;
                // 풀지 않을 답이면 압축을 청하지 않는다고 적는다. 적지 않으면 플랫폼이 gzip을
                // 청하고 몰래 풀어, 앱이 받는 바이트와 상한이 견주는 바이트가 달라진다.
                if (state_.request.decompress == false && find_header(headers, u8"accept-encoding").empty())
                    if (set_property(u8"Accept-Encoding", u8"identity", failure) == false)
                        return false;

                if (state_.request.body.empty())
                    return true;

                env_->CallVoidMethod(connection_, api_.set_do_output, JNI_TRUE);
                env_->CallVoidMethod(connection_, api_.set_fixed_length, static_cast<jlong>(state_.request.body.size()));
                if (check_call(failure, true) == false || is_latched(state_))
                    return false;
                const jobject output { env_->CallObjectMethod(connection_, api_.get_output_stream) };
                if (check_call(failure, true) == false)
                    return false;

                const std::size_t block_size { std::min<std::size_t>(state_.request.body.size(), static_cast<std::size_t>(read_chunk_bytes)) };
                const jbyteArray block { env_->NewByteArray(static_cast<jsize>(block_size)) };
                if (block == nullptr || check_call(failure, true) == false)
                    return false;
                std::size_t sent { 0 };
                while (sent < state_.request.body.size())
                {
                    const std::size_t count { std::min(block_size, state_.request.body.size() - sent) };
                    env_->SetByteArrayRegion(block, 0, static_cast<jsize>(count), reinterpret_cast<const jbyte*>(state_.request.body.data() + sent));
                    env_->CallVoidMethod(output, api_.output_write, block, 0, static_cast<jint>(count));
                    if (check_call(failure, true) == false)
                        return false;
                    sent += count;
                }
                env_->CallVoidMethod(output, api_.output_close);
                env_->DeleteLocalRef(block);
                env_->DeleteLocalRef(output);
                return check_call(failure, true);
            }

            // 상태 줄과 헤더를 받는다.
            [[nodiscard]] bool exchange_head(http_error& failure)
            {
                if (is_latched(state_))
                    return false;
                const jint status { env_->CallIntMethod(connection_, api_.get_response_code) };
                if (check_call(failure, true) == false)
                    return false;
                if (status < 0)
                {
                    failure = make_http_error(http_error_kind::connection_lost, u8"The response had no valid status line.");
                    return false;
                }
                result_.status_code = static_cast<int>(status);

                const auto message { static_cast<jstring>(env_->CallObjectMethod(connection_, api_.get_response_message)) };
                if (check_call(failure, false) == false)
                    return false;
                result_.reason = from_java(env_, message);
                env_->DeleteLocalRef(message);

                // 이름 없는 줄은 상태 줄이다. `X-Android-`로 시작하는 줄은 플랫폼이 지어 붙인
                // 것이라 서버가 보낸 헤더가 아니다.
                for (jint index { 0 };; ++index)
                {
                    const auto value { static_cast<jstring>(env_->CallObjectMethod(connection_, api_.get_header_value, index)) };
                    if (check_call(failure, false) == false)
                        return false;
                    if (value == nullptr)
                        break;
                    const auto key { static_cast<jstring>(env_->CallObjectMethod(connection_, api_.get_header_key, index)) };
                    if (check_call(failure, false) == false)
                        return false;
                    if (key != nullptr)
                    {
                        std::u8string name { from_java(env_, key) };
                        constexpr std::u8string_view synthetic { u8"x-android-" };
                        if (name.size() < synthetic.size() || same_ascii_ci(std::u8string_view { name }.substr(0, synthetic.size()), synthetic) == false)
                            result_.headers.push_back(http_header { std::move(name), from_java(env_, value) });
                    }
                    env_->DeleteLocalRef(key);
                    env_->DeleteLocalRef(value);
                }
                return true;
            }

            [[nodiscard]] bool read_body(http_error& failure)
            {
                // 몸이 오지 않는 답이다. HEAD와 1xx·204·304의 `Content-Length`는 오지 않을 몸의 크기다.
                const int status { result_.status_code };
                const bool informational { status >= 100 && status < 200 };
                if (state_.request.method == http_method::head || status == 204 || status == 304 || informational)
                    return true;

                // 압축되지 않은 답의 길이가 상한을 넘으면 한 바이트도 읽지 않는다. 플랫폼이 몰래
                // 푼 답은 길이와 Content-Encoding을 지운 채로 오므로 여기에 걸리지 않는다.
                const bool content_encoded { state_.request.decompress && find_header(result_.headers, u8"content-encoding").empty() == false };
                const std::size_t content_length { parse_content_length(find_header(result_.headers, u8"content-length")) };
                if (content_encoded == false && content_length > state_.request.max_body_bytes)
                {
                    failure = make_http_error(http_error_kind::body_too_large, u8"The response Content-Length exceeds max_body_bytes.");
                    return false;
                }
                if (content_encoded == false)
                    result_.body.reserve(std::min(content_length, body_reserve_limit_bytes));

                // 4xx·5xx의 몸은 오류 흐름에 있다. 몸이 없으면 nullptr이다.
                jobject input { status >= 400 ? env_->CallObjectMethod(connection_, api_.get_error_stream) : env_->CallObjectMethod(connection_, api_.get_input_stream) };
                if (check_call(failure, false) == false)
                    return false;
                if (input == nullptr)
                    return true;

                // Accept-Encoding을 직접 주면 플랫폼의 자동 gzip 해제가 꺼진다. 그때만 직접 푼다.
                jobject inflater { nullptr };
                if (state_.request.decompress && wrap_decoder(input, inflater, failure) == false)
                    return false;

                const jbyteArray block { env_->NewByteArray(read_chunk_bytes) };
                if (block == nullptr || check_call(failure, false) == false)
                    return false;
                std::vector<jbyte> staging(static_cast<std::size_t>(read_chunk_bytes));
                while (true)
                {
                    const jint count { env_->CallIntMethod(input, api_.input_read, block, 0, read_chunk_bytes) };
                    if (check_call(failure, false) == false)
                        return false;
                    if (count < 0)
                        break;
                    if (static_cast<std::size_t>(count) > state_.request.max_body_bytes - std::min(result_.body.size(), state_.request.max_body_bytes))
                    {
                        // 압축을 푼 뒤의 크기라 이쪽이 진짜 관문이다.
                        failure = make_http_error(http_error_kind::body_too_large, u8"The response body exceeds max_body_bytes.");
                        return false;
                    }
                    env_->GetByteArrayRegion(block, 0, count, staging.data());
                    const auto* const first { reinterpret_cast<const std::uint8_t*>(staging.data()) };
                    result_.body.insert(result_.body.end(), first, first + count);
                }
                env_->CallVoidMethod(input, api_.input_close);
                body_drained_ = env_->ExceptionCheck() == false;
                env_->ExceptionClear();
                // 직접 준 Inflater는 스트림이 닫아 주지 않는다. 네이티브 메모리를 GC에 맡기지 않는다.
                if (inflater != nullptr)
                {
                    env_->CallVoidMethod(inflater, api_.inflater_end);
                    env_->ExceptionClear();
                }
                return true;
            }

            // 답이 아직 압축된 채로 왔으면(gzip·deflate) 푸는 스트림으로 감싼다.
            //  - 앞의 두 바이트를 엿본다. 몸이 비었으면 GZIPInputStream이 머리를 읽다 EOF로
            //    던지므로 감싸지 않는다. deflate는 RFC 9110대로 zlib 머리가 있으면 그것으로, 없으면
            //    머리 없는 deflate로 푼다 (머리 없이 보내는 서버가 흔하다).
            [[nodiscard]] bool wrap_decoder(jobject& input, jobject& inflater, http_error& failure)
            {
                const std::u8string_view encoding { find_header(result_.headers, u8"content-encoding") };
                const bool gzip { same_ascii_ci(encoding, u8"gzip") || same_ascii_ci(encoding, u8"x-gzip") };
                const bool deflate { same_ascii_ci(encoding, u8"deflate") };
                if (gzip == false && deflate == false)
                    return true;

                const jobject peekable { env_->NewObject(api_.pushback_stream, api_.pushback_new, input, jint { 2 }) };
                if (check_call(failure, false) == false || peekable == nullptr)
                    return false;
                input = peekable;
                const jint first { env_->CallIntMethod(input, api_.pushback_read) };
                if (check_call(failure, false) == false)
                    return false;
                const jint second { first >= 0 ? env_->CallIntMethod(input, api_.pushback_read) : jint { -1 } };
                if (check_call(failure, false) == false)
                    return false;
                if (second >= 0)
                    env_->CallVoidMethod(input, api_.pushback_unread, second);
                if (check_call(failure, false) == false)
                    return false;
                if (first >= 0)
                    env_->CallVoidMethod(input, api_.pushback_unread, first);
                if (check_call(failure, false) == false)
                    return false;

                if (first >= 0)
                {
                    jobject decoder { nullptr };
                    if (gzip)
                        decoder = env_->NewObject(api_.gzip_stream, api_.gzip_new, input);
                    else
                    {
                        const bool zlib { second >= 0 && (first & 0x0F) == 8 && ((first << 8) | second) % 31 == 0 };
                        inflater = env_->NewObject(api_.inflater, api_.inflater_new, zlib ? JNI_FALSE : JNI_TRUE);
                        if (check_call(failure, false) == false || inflater == nullptr)
                            return false;
                        decoder = env_->NewObject(api_.inflater_stream, api_.inflater_stream_new, input, inflater);
                    }
                    if (check_call(failure, false) == false || decoder == nullptr)
                        return false;
                    input = decoder;
                }
                // 앱이 받는 바이트는 푼 것이다. 압축된 길이와 부호화 표시는 남기지 않는다.
                std::erase_if(result_.headers, [](const http_header& header) { return same_ascii_ci(header.name, u8"content-encoding") || same_ascii_ci(header.name, u8"content-length"); });
                return true;
            }

            // 연결을 상태에서 뗀다. 다 읽지 못한 연결은 끊는다 — 덜 읽은 연결이 연결 풀로
            // 돌아가 다음 요청이 남의 몸을 읽는 일이 없게 한다.
            void release_connection(const bool abandon) noexcept
            {
                if (connection_ != nullptr && abandon)
                {
                    env_->CallVoidMethod(connection_, api_.disconnect);
                    env_->ExceptionClear();
                }
                std::lock_guard lock { state_.guard };
                if (state_.connection != nullptr)
                {
                    env_->DeleteGlobalRef(state_.connection);
                    state_.connection = nullptr;
                }
                if (connection_ != nullptr)
                    env_->DeleteLocalRef(connection_);
                connection_ = nullptr;
            }

            JNIEnv* env_ { nullptr };
            const java_api& api_;
            request_state& state_;
            const http_client_config& configuration_;
            completion& result_;
            jobject connection_ { nullptr };
            bool body_drained_ { false };
        };
    } // namespace

    // JNI의 HttpURLConnection 위에 선 실체다 (docs/android-port-plan.md 결정 3).
    //
    // 받아들인 요청마다 worker thread 하나가 막히는 호출로 끝까지 몬다. 동시 수는
    // `max_in_flight`가 받기 전에 막으므로 pool을 두지 않는다 — 숨은 큐는 `in_flight()`의 뜻을
    // 바꾼다. pump thread 하나가 Windows 백엔드와 같은 일을 한다: 몸을 풀고, `deliver`를 부르고,
    // 시계를 돈다 (전체 마감과 되풀이 회차, 끊기 재시도).
    struct http_client::engine final
    {
        struct schedule_record
        {
            http_ticket ticket {};
            http_request request {};
            http_heartbeat schedule {};
            std::chrono::steady_clock::time_point next {};
            std::uint64_t rounds { 0 };
            // 지금 날아가 있는 회차의 요청 번호다 (없으면 0).
            std::uint64_t in_flight_id { 0 };
        };

        struct pending_round
        {
            http_request request {};
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            std::uint64_t id { 0 };
        };

        explicit engine(http_client_config configuration)
            : configuration_ { std::move(configuration) }
            , completions_ { messaging::channel_options { std::numeric_limits<std::size_t>::max(), messaging::overflow_policy::reject_newest, {} } }
        {}

        engine(const engine&) = delete;
        engine(engine&&) = delete;
        engine& operator=(const engine&) = delete;
        engine& operator=(engine&&) = delete;

        ~engine()
        {
            static_cast<void>(stop());
        }

        [[nodiscard]] bool open(std::u8string& error)
        {
            if (text::utf8_is_valid(configuration_.user_agent) == false)
            {
                error = u8"The user agent is not valid UTF-8.";
                return false;
            }

            const android::jni_thread_scope scope { "luil-http" };
            if (scope.env() == nullptr)
            {
                error = u8"No JavaVM is available; the Android app host sets it before the app starts.";
                return false;
            }
            api_ = acquire_java_api(scope.env());
            if (api_ == nullptr)
            {
                error = u8"The java.net HTTP classes could not be loaded.";
                return false;
            }

            transcode_ = configuration_.transcode ? configuration_.transcode : codepage_text_transcoder();
            try
            {
                pump_ = std::thread { [this] { run_pump(); } };
            }
            catch (const std::system_error&)
            {
                error = u8"The client thread could not start.";
                return false;
            }
            pump_id_ = pump_.get_id();
            return true;
        }

        [[nodiscard]] http_ticket send(http_request request, std::u8string& error)
        {
            if (stopping_.load())
            {
                error = u8"client stopped";
                return {};
            }

            if (reserve_ticket(error) == false)
                return {};
            const std::uint64_t id { issue_request_id() };
            const http_ticket ticket { id };
            http_error failure {};
            if (start_request(std::move(request), ticket, 0, id, failure) == false)
            {
                pending_tickets_.fetch_sub(1);
                error = failure.message;
                return {};
            }
            return ticket;
        }

        [[nodiscard]] http_ticket start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error)
        {
            if (stopping_.load())
            {
                error = u8"client stopped";
                return {};
            }

            // URL·헤더는 **먼저** 본다 — 잘못된 요청은 표를 받지 못한다.
            http_error failure {};
            std::vector<http_header> headers {};
            if (parse_origin(request.url, failure).valid == false || collect_request_headers(request, headers, failure) == false || body_fits_method(request, failure) == false)
            {
                error = failure.message;
                return {};
            }

            if (schedule.interval < http_minimum_heartbeat_interval)
                schedule.interval = http_minimum_heartbeat_interval;

            if (reserve_ticket(error) == false)
                return {};

            const http_ticket ticket { issue_request_id() };
            schedule_record record {};
            record.ticket = ticket;
            record.request = std::move(request);
            record.schedule = schedule;
            record.next = std::chrono::steady_clock::now() + (schedule.immediate ? 0ms : schedule.interval);
            {
                std::lock_guard lock { schedules_mutex_ };
                if (stopping_.load())
                {
                    pending_tickets_.fetch_sub(1);
                    error = u8"client stopped";
                    return {};
                }
                schedules_.emplace(ticket.value, std::move(record));
            }
            return ticket;
        }

        void cancel(const http_ticket ticket) noexcept
        {
            if (ticket.value == 0)
                return;

            bool heartbeat { false };
            schedule_record dropped {};
            {
                std::lock_guard lock { schedules_mutex_ };
                const auto entry { schedules_.find(ticket.value) };
                if (entry != schedules_.end())
                {
                    heartbeat = true;
                    dropped = std::move(entry->second);
                    schedules_.erase(entry);
                }
            }

            // 회차가 하나도 나가 있지 않은 되풀이는 답할 것이 없으니 마지막 답을 짓는다.
            // 회차 번호가 적혀 있으면 그 회차의 답이 오고, 되풀이표가 비었으므로 그것이
            // 마지막으로 찍힌다 (Windows 백엔드와 같은 규칙이다).
            if (heartbeat && dropped.in_flight_id == 0)
            {
                post_synthetic(dropped.ticket, dropped.rounds, dropped.request, cancelled_error());
                return;
            }

            const std::uint64_t target { heartbeat ? dropped.in_flight_id : ticket.value };
            if (const std::shared_ptr<request_state> live { borrow_request(target) }; live != nullptr)
                abort(*live, cancelled_error());
        }

        void cancel_all() noexcept
        {
            std::vector<schedule_record> dropped {};
            {
                std::lock_guard lock { schedules_mutex_ };
                for (auto& entry : schedules_)
                    dropped.push_back(std::move(entry.second));
                schedules_.clear();
            }
            for (const schedule_record& record : dropped)
                if (record.in_flight_id == 0)
                    post_synthetic(record.ticket, record.rounds, record.request, cancelled_error());
            abort_all(cancelled_error());
        }

        [[nodiscard]] std::size_t in_flight() const noexcept
        {
            return outstanding_.load();
        }

        // 거짓이면 예산을 넘긴 것이다 — 부르는 쪽이 engine을 일부러 흘려보낸다.
        [[nodiscard]] bool stop() noexcept
        {
            // `deliver` 안에서 부르면 자기 자신을 join하게 된다 (http_client.h의 계약).
            assert(std::this_thread::get_id() != pump_id_ && "http_client::stop must not be called from deliver");
            std::lock_guard serialize { stop_mutex_ };
            if (stopped_.load())
                return true;

            // 1. 새 요청을 거절한다.
            stopping_.store(true);
            // 2. 되풀이표를 비우고 pump를 **먼저** 멈춘다. 배달도 여기서 끝난다.
            {
                std::lock_guard lock { schedules_mutex_ };
                schedules_.clear();
            }
            delivering_.store(false);
            completions_.close();
            if (pump_.joinable())
                pump_.join();
            outstanding_.store(0);

            // 3. 날아가 있는 요청을 전부 접고 worker가 다 끝나기를 기다린다. 연결이 서기
            //    직전에 건 끊기는 빗나갈 수 있어 기다리는 동안 다시 건다.
            const http_error reason { make_http_error(http_error_kind::stopped, u8"The client was stopped.") };
            abort_all(reason);
            const auto limit { std::chrono::steady_clock::now() + configuration_.stop_budget };
            bool finished_in_time { false };
            while (true)
            {
                {
                    std::unique_lock lock { in_flight_mutex_ };
                    const auto now { std::chrono::steady_clock::now() };
                    const auto slice { std::min(limit, now + stop_retry_interval) };
                    finished_in_time = in_flight_empty_.wait_until(lock, slice, [this] { return in_flight_.empty(); });
                }
                if (finished_in_time || std::chrono::steady_clock::now() >= limit)
                    break;
                abort_all(reason);
            }
            stopped_.store(true);

            if (finished_in_time == false)
            {
                // 예산을 넘겼다. worker가 아직 이 객체를 볼 수 있어 부술 수 없다.
                leaked_.store(true);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool leaked() const noexcept
        {
            return leaked_.load();
        }

    private:
        [[nodiscard]] static http_error cancelled_error()
        {
            return make_http_error(http_error_kind::cancelled, u8"The request was cancelled.");
        }

        [[nodiscard]] std::shared_ptr<request_state> borrow_request(const std::uint64_t id) const
        {
            std::lock_guard lock { in_flight_mutex_ };
            const auto entry { in_flight_.find(id) };
            return entry == in_flight_.end() ? nullptr : entry->second;
        }

        // 결말을 적고 연결을 끊는다. 부르는 thread가 JVM에 붙어 있지 않으면 잠시 붙인다.
        void abort(request_state& state, const http_error& reason) noexcept
        {
            if (latch(state, reason) == false)
                return;
            const android::jni_thread_scope scope { "luil-http" };
            disconnect_if_latched(scope.env(), *api_, state);
        }

        [[nodiscard]] std::vector<std::shared_ptr<request_state>> live_requests() const
        {
            std::vector<std::shared_ptr<request_state>> live {};
            std::lock_guard lock { in_flight_mutex_ };
            live.reserve(in_flight_.size());
            for (const auto& entry : in_flight_)
                live.push_back(entry.second);
            return live;
        }

        // 살아 있는 요청 전부에 사유를 걸고 끊는다. 이미 걸린 것도 다시 끊는다.
        void abort_all(const http_error& reason) noexcept
        {
            std::vector<std::shared_ptr<request_state>> live {};
            try
            {
                live = live_requests();
            }
            catch (...)
            {
                return;
            }
            if (live.empty())
                return;

            const android::jni_thread_scope scope { "luil-http" };
            for (const std::shared_ptr<request_state>& state : live)
            {
                static_cast<void>(latch(*state, reason));
                disconnect_if_latched(scope.env(), *api_, *state);
            }
        }

        // 걸쇠는 적혔는데 아직 끝나지 않은 요청을 다시 끊는다 (pump의 훑기).
        void retry_aborts(JNIEnv* const env)
        {
            for (const std::shared_ptr<request_state>& state : live_requests())
                disconnect_if_latched(env, *api_, *state);
        }

        [[nodiscard]] bool reserve_ticket(std::u8string& error)
        {
            std::size_t pending { pending_tickets_.load() };
            while (pending < configuration_.max_pending_responses)
                if (pending_tickets_.compare_exchange_weak(pending, pending + 1))
                    return true;
            error = u8"too many responses awaiting delivery";
            return false;
        }

        void post_synthetic(const http_ticket ticket, const std::uint64_t sequence, const http_request& request, const http_error& error) noexcept
        {
            completion synthetic {};
            synthetic.ticket = ticket;
            synthetic.sequence = sequence;
            synthetic.final_url = request.url;
            synthetic.parse = request.parse;
            synthetic.error = error;
            outstanding_.fetch_add(1);
            if (completions_.post(std::move(synthetic)) != messaging::post_result::posted)
                outstanding_.fetch_sub(1);
        }

        void remember_deadline(const request_state& state)
        {
            if (state.has_deadline == false)
                return;
            std::lock_guard lock { deadlines_mutex_ };
            static_cast<void>(deadlines_.emplace(state.deadline, state.id));
        }

        // 요청 하나를 받아 worker를 띄운다. 거짓이면 띄우기 전에 실패한 것이라 답이 오지 않는다.
        [[nodiscard]] bool start_request(http_request request, const http_ticket ticket, const std::uint64_t sequence, const std::uint64_t id, http_error& failure)
        {
            const url_origin origin { parse_origin(request.url, failure) };
            if (origin.valid == false)
                return false;

            auto state { std::make_shared<request_state>() };
            if (collect_request_headers(request, state->headers, failure) == false)
                return false;
            if (body_fits_method(request, failure) == false)
                return false;

            state->id = id;
            state->ticket = ticket;
            state->sequence = sequence;
            state->started = std::chrono::steady_clock::now();
            if (request.total_timeout > 0ms)
            {
                state->has_deadline = true;
                state->deadline = state->started + request.total_timeout;
            }
            state->request = std::move(request);
            // 출처는 요청의 URL 글을 가리키므로 글이 자리를 잡은 뒤에 다시 가른다.
            http_error ignored {};
            state->origin = parse_origin(state->request.url, ignored);

            {
                // 회차 예약의 취소와 등록을 같은 관문으로 묶는다 (Windows 백엔드와 같다).
                std::unique_lock schedule_lock { schedules_mutex_, std::defer_lock };
                if (sequence != 0)
                {
                    schedule_lock.lock();
                    if (schedules_.contains(ticket.value) == false)
                    {
                        failure = cancelled_error();
                        return false;
                    }
                }
                std::lock_guard lock { in_flight_mutex_ };
                if (stopping_.load())
                {
                    failure = make_http_error(http_error_kind::stopped, u8"client stopped");
                    return false;
                }
                if (in_flight_.size() >= configuration_.max_in_flight)
                {
                    failure = make_http_error(http_error_kind::system_error, u8"too many requests in flight (" + count_text(configuration_.max_in_flight) + u8")");
                    return false;
                }
                in_flight_.emplace(id, state);
                outstanding_.fetch_add(1);
            }

            try
            {
                std::thread { [this, state] { run_worker(state); } }.detach();
            }
            catch (const std::system_error&)
            {
                {
                    std::lock_guard lock { in_flight_mutex_ };
                    if (in_flight_.erase(id) != 0)
                        outstanding_.fetch_sub(1);
                    in_flight_empty_.notify_all();
                }
                failure = make_http_error(http_error_kind::system_error, u8"The request thread could not start.");
                return false;
            }
            remember_deadline(*state);
            return true;
        }

        // worker thread의 전부다. 끝에 `finish_request`를 부른 뒤로는 engine을 만지지 않는다.
        void run_worker(const std::shared_ptr<request_state> state) noexcept
        {
            completion result {};
            result.ticket = state->ticket;
            result.sequence = state->sequence;
            result.parse = state->request.parse;
            result.final_url = state->request.url;

            http_error failure {};
            {
                const android::jni_thread_scope scope { "luil-http" };
                JNIEnv* const env { scope.env() };
                if (env == nullptr)
                {
                    failure = make_http_error(http_error_kind::system_error, u8"The request thread could not attach to the JavaVM.");
                }
                else
                {
                    const local_frame frame { env, 64 };
                    try
                    {
                        request_runner runner { env, *api_, *state, configuration_, result };
                        failure = runner.run();
                    }
                    catch (...)
                    {
                        failure = make_http_error(http_error_kind::system_error, u8"The request failed unexpectedly.");
                    }
                    env->ExceptionClear();

                    std::lock_guard lock { state->guard };
                    if (state->connection != nullptr)
                    {
                        env->DeleteGlobalRef(state->connection);
                        state->connection = nullptr;
                    }
                }
            }

            {
                // 걸쇠에 먼저 적힌 사유가 이긴다 — 우리가 끊은 연결은 "끊겼다"로 돌아오지만 답은
                // 끊은 이유(취소·마감·정지)여야 한다.
                std::lock_guard lock { state->guard };
                if (state->outcome_latched)
                    failure = state->outcome;
                state->outcome_latched = true;
                state->outcome = failure;
                state->finished = true;
            }
            result.error = failure;
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - state->started);
            // 반쪽 몸은 "원래 그런 답"과 구별되지 않아 넘기지 않는다.
            if (result.error.empty() == false)
                result.body.clear();
            finish_request(state->id, std::move(result));
        }

        void finish_request(const std::uint64_t id, completion result) noexcept
        {
            std::lock_guard lock { in_flight_mutex_ };
            if (in_flight_.erase(id) == 0)
                return;
            static_cast<void>(completions_.post(std::move(result)));
            // 알림도 **잠금 안**이다. 밖으로 나가는 순간 `stop()`이 지도가 빈 것을 보고
            // engine을 부술 수 있다.
            in_flight_empty_.notify_all();
        }

        void run_pump()
        {
            const android::jni_thread_scope scope { "luil-http-client" };
            auto swept { std::chrono::steady_clock::now() };
            while (stopping_.load() == false)
            {
                messaging::envelope<completion> entry {};
                const messaging::receive_status status { completions_.receive_wait(entry, next_wait()) };
                if (status == messaging::receive_status::closed)
                    break;
                if (status == messaging::receive_status::received)
                {
                    handle_completion(std::move(entry.payload));
                    if (std::chrono::steady_clock::now() - swept < pump_sweep_interval)
                        continue;
                }
                swept = std::chrono::steady_clock::now();
                expire_deadlines();
                fire_schedules();
                retry_aborts(scope.env());
            }
        }

        [[nodiscard]] std::chrono::milliseconds next_wait()
        {
            const auto now { std::chrono::steady_clock::now() };
            auto limit { now + pump_idle_wait };
            {
                std::lock_guard lock { deadlines_mutex_ };
                if (deadlines_.empty() == false)
                    limit = std::min(limit, deadlines_.begin()->first);
            }
            {
                std::lock_guard lock { schedules_mutex_ };
                for (const auto& entry : schedules_)
                    if (entry.second.in_flight_id == 0)
                        limit = std::min(limit, entry.second.next);
            }
            // 걸쇠만 적히고 끝나지 않은 요청이 있으면 끊기를 다시 걸 때까지만 잔다.
            for (const std::shared_ptr<request_state>& state : live_requests())
            {
                std::lock_guard lock { state->guard };
                if (state->outcome_latched && state->finished == false)
                    limit = std::min(limit, now + pump_sweep_interval);
            }
            if (limit <= now)
                return 0ms;
            return std::chrono::duration_cast<std::chrono::milliseconds>(limit - now) + 1ms;
        }

        void handle_completion(completion finished)
        {
            bool last { true };
            {
                std::lock_guard lock { schedules_mutex_ };
                const auto entry { schedules_.find(finished.ticket.value) };
                if (entry != schedules_.end())
                {
                    schedule_record& record { entry->second };
                    record.in_flight_id = 0;
                    const bool exhausted { record.schedule.max_rounds != 0 && record.rounds >= record.schedule.max_rounds };
                    last = exhausted || failure_is_permanent(finished.error.kind);
                    if (last)
                        schedules_.erase(entry);
                }
            }

            const http_ticket ticket { finished.ticket };
            deliver_safely(make_response(std::move(finished), last));
            outstanding_.fetch_sub(1);
            // **재무장은 배달한 뒤다.** 그래서 회차가 겹칠 수 없다.
            if (last == false)
                rearm_schedule(ticket);
            else
                pending_tickets_.fetch_sub(1);
        }

        [[nodiscard]] http_response make_response(completion finished, const bool last)
        {
            http_response response {};
            response.ticket = finished.ticket;
            response.sequence = finished.sequence;
            response.last = last;
            response.status_code = finished.status_code;
            response.reason = std::move(finished.reason);
            response.final_url = std::move(finished.final_url);
            response.headers = std::move(finished.headers);
            response.content_type = parse_media_type(find_header(response.headers, u8"content-type"));
            response.error = std::move(finished.error);
            response.elapsed = finished.elapsed;
            if (response.error.empty())
                response.body = decode_http_body(std::move(finished.body), response.content_type, finished.parse, transcode_);
            return response;
        }

        void deliver_safely(http_response response) noexcept
        {
            if (delivering_.load() == false || configuration_.deliver == nullptr)
                return;
            try
            {
                configuration_.deliver(std::move(response));
            }
            catch (...)
            {
                // 계약은 던지지 않는 것이다. 던지면 삼키고 그 답만 버린다.
            }
        }

        void rearm_schedule(const http_ticket ticket)
        {
            std::lock_guard lock { schedules_mutex_ };
            const auto entry { schedules_.find(ticket.value) };
            if (entry == schedules_.end())
                return;
            entry->second.next = std::chrono::steady_clock::now() + entry->second.schedule.interval;
        }

        void expire_deadlines()
        {
            const auto now { std::chrono::steady_clock::now() };
            std::vector<std::uint64_t> expired {};
            {
                std::lock_guard lock { deadlines_mutex_ };
                auto entry { deadlines_.begin() };
                while (entry != deadlines_.end() && entry->first <= now)
                {
                    expired.push_back(entry->second);
                    entry = deadlines_.erase(entry);
                }
            }
            for (const std::uint64_t id : expired)
                if (const std::shared_ptr<request_state> live { borrow_request(id) }; live != nullptr)
                    abort(*live, make_http_error(http_error_kind::timed_out, u8"The request exceeded total_timeout."));
        }

        void fire_schedules()
        {
            std::vector<pending_round> ready {};
            const auto now { std::chrono::steady_clock::now() };
            {
                std::lock_guard lock { schedules_mutex_ };
                for (auto& entry : schedules_)
                {
                    schedule_record& record { entry.second };
                    if (record.in_flight_id != 0 || record.next > now)
                        continue;

                    // 번호를 **잠금 안에서** 먼저 발번한다 — 다른 thread의 `cancel`이 날아가는
                    // 중인 회차를 놓치지 않는다.
                    pending_round round {};
                    round.id = issue_request_id();
                    round.ticket = record.ticket;
                    round.request = record.request;
                    ++record.rounds;
                    round.sequence = record.rounds;
                    record.in_flight_id = round.id;
                    ready.push_back(std::move(round));
                }
            }

            for (pending_round& round : ready)
            {
                http_error failure {};
                if (start_request(round.request, round.ticket, round.sequence, round.id, failure) == false)
                    post_synthetic(round.ticket, round.sequence, round.request, failure);
            }
        }

        http_client_config configuration_ {};
        http_text_transcoder transcode_ {};
        const java_api* api_ { nullptr };

        std::atomic<bool> stopping_ { false };
        std::atomic<bool> stopped_ { false };
        std::atomic<bool> delivering_ { true };
        std::atomic<bool> leaked_ { false };
        std::atomic<std::size_t> outstanding_ { 0 };
        std::atomic<std::size_t> pending_tickets_ { 0 };
        std::mutex stop_mutex_ {};

        mutable std::mutex in_flight_mutex_ {};
        std::condition_variable in_flight_empty_ {};
        std::unordered_map<std::uint64_t, std::shared_ptr<request_state>> in_flight_ {};

        std::mutex schedules_mutex_ {};
        std::map<std::uint64_t, schedule_record> schedules_ {};

        std::mutex deadlines_mutex_ {};
        std::multimap<std::chrono::steady_clock::time_point, std::uint64_t> deadlines_ {};

        messaging::channel<completion> completions_;
        std::thread pump_ {};
        std::thread::id pump_id_ {};
    };

    http_client::http_client(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {}

    http_client::~http_client()
    {
        stop();
        // `stop()`이 예산을 넘겼으면 살아 있는 worker가 아직 engine을 보므로 일부러 흘려보낸다.
        if (engine_ != nullptr && engine_->leaked())
            static_cast<void>(engine_.release());
    }

    std::unique_ptr<http_client> http_client::create(http_client_config configuration, std::u8string& error)
    {
        auto owned { std::make_unique<engine>(std::move(configuration)) };
        if (owned->open(error) == false)
            return nullptr;
        return std::unique_ptr<http_client> { new http_client { std::move(owned) } };
    }

    http_ticket http_client::send(http_request request, std::u8string& error)
    {
        if (engine_ == nullptr)
        {
            error = u8"client stopped";
            return {};
        }
        return engine_->send(std::move(request), error);
    }

    http_ticket http_client::start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error)
    {
        if (engine_ == nullptr)
        {
            error = u8"client stopped";
            return {};
        }
        return engine_->start_heartbeat(std::move(request), schedule, error);
    }

    void http_client::cancel(const http_ticket ticket) noexcept
    {
        if (engine_ != nullptr)
            engine_->cancel(ticket);
    }

    void http_client::cancel_all() noexcept
    {
        if (engine_ != nullptr)
            engine_->cancel_all();
    }

    std::size_t http_client::in_flight() const noexcept
    {
        return engine_ == nullptr ? 0u : engine_->in_flight();
    }

    void http_client::stop() noexcept
    {
        if (engine_ != nullptr)
            static_cast<void>(engine_->stop());
    }
} // namespace luil::net

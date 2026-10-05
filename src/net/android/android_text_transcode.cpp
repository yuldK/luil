#include "luil/net/http_body.h"

#include "android/java_vm.h"
#include "luil/text/utf8_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace luil::net {
    namespace {
        enum class utf16_order
        {
            none,
            little,
            big,
        };

        struct utf16_name
        {
            std::u8string_view name {};
            utf16_order order { utf16_order::none };
        };

        // Windows 백엔드의 표와 같은 이름이다 (net/winhttp/http_text_transcode.cpp). 바이트 차례를
        // 말하지 않은 이름은 little endian으로 받는다.
        constexpr std::array<utf16_name, 6> utf16_names {
            utf16_name { u8"utf-16", utf16_order::little },
            utf16_name { u8"utf-16le", utf16_order::little },
            utf16_name { u8"ucs-2", utf16_order::little },
            utf16_name { u8"unicode", utf16_order::little },
            utf16_name { u8"utf-16be", utf16_order::big },
            utf16_name { u8"unicodefffe", utf16_order::big },
        };

        [[nodiscard]] char8_t lower_ascii(const char8_t byte) noexcept
        {
            return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte;
        }

        [[nodiscard]] std::u8string_view trimmed(std::u8string_view charset) noexcept
        {
            while (charset.empty() == false && (charset.front() == u8' ' || charset.front() == u8'\t'))
                charset.remove_prefix(1);
            while (charset.empty() == false && (charset.back() == u8' ' || charset.back() == u8'\t'))
                charset.remove_suffix(1);
            return charset;
        }

        [[nodiscard]] bool same_ascii_ci(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index { 0 }; index < left.size(); ++index)
                if (lower_ascii(left[index]) != lower_ascii(right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] utf16_order find_utf16_order(const std::u8string_view charset) noexcept
        {
            const std::u8string_view name { trimmed(charset) };
            for (const utf16_name& entry : utf16_names)
                if (same_ascii_ci(name, entry.name))
                    return entry.order;
            return utf16_order::none;
        }

        // 오류 글에 charset 이름을 싣는다. ASCII만 옮기고 나머지는 `?`다 — 헤더는 밖에서 오는
        // 값이라 이 글이 올바른 UTF-8이려면 그래야 한다.
        void append_charset(std::u8string& output, const std::u8string_view charset)
        {
            for (const char8_t byte : charset)
                output.push_back(byte >= 0x20u && byte < 0x7Fu ? byte : u8'?');
        }

        [[nodiscard]] std::u8string charset_error(const std::u8string_view prefix, const std::u8string_view charset, const std::u8string_view suffix)
        {
            std::u8string message { prefix };
            append_charset(message, charset);
            message += suffix;
            return message;
        }

        // 서버가 쓰는 별칭을 Windows의 변환 계약과 같은 문자 집합으로 옮긴다.
        // EUC-KR은 확장 한글(CP949), Shift-JIS는 CP932, Latin-1은 CP1252로 읽는다.
        [[nodiscard]] std::string java_charset(const std::u8string_view charset)
        {
            std::string name {};
            for (const char8_t byte : trimmed(charset))
            {
                if (byte < 0x21u || byte > 0x7Eu)
                    return {};
                name.push_back(static_cast<char>(lower_ascii(byte)));
            }
            if (name == "euc-kr" || name == "ks_c_5601-1987" || name == "ks_c_5601-1989" || name == "ksc5601" || name == "ksc_5601" || name == "korean" || name == "cp949" || name == "windows-949"
                || name == "uhc")
                return "windows-949";
            if (name == "shift_jis" || name == "shift-jis" || name == "sjis" || name == "x-sjis" || name == "ms_kanji" || name == "cp932" || name == "windows-31j")
                return "windows-31j";
            if (name == "iso-8859-1" || name == "iso8859-1" || name == "latin1" || name == "latin-1")
                return "windows-1252";
            return name;
        }

        // JVM의 엄격한 decoder를 JNI로 부른다. 잘린 바이트를 대체 문자로 숨기지 않는다.
        // UTF-16은 JVM이 없는 core test에서도 읽을 수 있도록 아래의 기존 변환을 유지한다.
        [[nodiscard]] std::u8string transcode_java(const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string& error)
        {
            const std::string name { java_charset(charset) };
            if (name.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
            {
                error = charset_error(u8"Unsupported charset or oversized body: ", charset, u8".");
                return {};
            }
            android::jni_thread_scope scope { "luil-charset" };
            JNIEnv* const env { scope.env() };
            if (env == nullptr)
            {
                error = charset_error(u8"The Java VM is unavailable for charset ", charset, u8".");
                return {};
            }
            if (env->PushLocalFrame(24) != JNI_OK)
            {
                env->ExceptionClear();
                error = u8"Failed to allocate charset decoder references.";
                return {};
            }
            struct frame_scope
            {
                JNIEnv* env;
                ~frame_scope()
                {
                    env->PopLocalFrame(nullptr);
                }
            };
            const frame_scope frame { env };
            // 예외가 난 뒤 다른 JNI 함수를 부르지 않도록 단계마다 확인한다.
            const auto failed = [&] {
                if (env->ExceptionCheck() == JNI_FALSE)
                    return false;
                env->ExceptionClear();
                return true;
            };
            std::u8string result {};
            const auto decode = [&]() -> bool {
                const jclass charset_class { env->FindClass("java/nio/charset/Charset") };
                if (failed() || charset_class == nullptr)
                    return false;
                const jmethodID for_name { env->GetStaticMethodID(charset_class, "forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;") };
                if (failed() || for_name == nullptr)
                    return false;
                const jstring label { env->NewStringUTF(name.c_str()) };
                if (failed() || label == nullptr)
                    return false;
                const jobject encoding { env->CallStaticObjectMethod(charset_class, for_name, label) };
                if (failed() || encoding == nullptr)
                    return false;
                const jmethodID new_decoder { env->GetMethodID(charset_class, "newDecoder", "()Ljava/nio/charset/CharsetDecoder;") };
                if (failed() || new_decoder == nullptr)
                    return false;
                const jobject decoder { env->CallObjectMethod(encoding, new_decoder) };
                if (failed() || decoder == nullptr)
                    return false;
                const jbyteArray data { env->NewByteArray(static_cast<jsize>(bytes.size())) };
                if (failed() || data == nullptr)
                    return false;
                if (bytes.empty() == false)
                    env->SetByteArrayRegion(data, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
                if (failed())
                    return false;
                const jclass buffer_class { env->FindClass("java/nio/ByteBuffer") };
                if (failed() || buffer_class == nullptr)
                    return false;
                const jmethodID wrap { env->GetStaticMethodID(buffer_class, "wrap", "([B)Ljava/nio/ByteBuffer;") };
                if (failed() || wrap == nullptr)
                    return false;
                const jobject buffer { env->CallStaticObjectMethod(buffer_class, wrap, data) };
                if (failed() || buffer == nullptr)
                    return false;
                const jclass decoder_class { env->FindClass("java/nio/charset/CharsetDecoder") };
                if (failed() || decoder_class == nullptr)
                    return false;
                const jmethodID convert { env->GetMethodID(decoder_class, "decode", "(Ljava/nio/ByteBuffer;)Ljava/nio/CharBuffer;") };
                if (failed() || convert == nullptr)
                    return false;
                const jobject characters { env->CallObjectMethod(decoder, convert, buffer) };
                if (failed() || characters == nullptr)
                    return false;
                const jclass object_class { env->FindClass("java/lang/Object") };
                if (failed() || object_class == nullptr)
                    return false;
                const jmethodID to_string { env->GetMethodID(object_class, "toString", "()Ljava/lang/String;") };
                if (failed() || to_string == nullptr)
                    return false;
                const auto value { static_cast<jstring>(env->CallObjectMethod(characters, to_string)) };
                if (failed() || value == nullptr)
                    return false;
                const jsize length { env->GetStringLength(value) };
                const jchar* const units { env->GetStringChars(value, nullptr) };
                if (failed() || units == nullptr)
                    return false;
                struct string_scope
                {
                    JNIEnv* env;
                    jstring value;
                    const jchar* units;
                    ~string_scope()
                    {
                        env->ReleaseStringChars(value, units);
                    }
                };
                const string_scope characters_scope { env, value, units };
                for (jsize index { 0 }; index < length; ++index)
                {
                    char32_t codepoint { units[index] };
                    if (codepoint >= 0xD800u && codepoint <= 0xDBFFu && index + 1 < length)
                    {
                        if (units[index + 1] < 0xDC00u || units[index + 1] > 0xDFFFu)
                            return false;
                        codepoint = 0x10000u + ((codepoint - 0xD800u) << 10u) + (units[++index] - 0xDC00u);
                    }
                    else if (codepoint >= 0xD800u && codepoint <= 0xDFFFu)
                        return false;
                    result += text::utf8_encode(codepoint);
                }
                return true;
            };
            if (decode() == false)
                error = charset_error(u8"Unsupported charset or invalid byte sequence for '", charset, u8"'.");
            return error.empty() ? result : std::u8string {};
        }

        // utf-16 바이트를 손으로 모은다. 규칙은 Windows 백엔드와 같다.
        //  - 길이가 홀수면 거절한다. 마지막 반쪽을 버리면 잘린 것이 온전한 답으로 보인다.
        //  - BOM이 이름을 이기고, 글에서는 떼어 낸다.
        //  - 짝 없는 surrogate는 거절한다 (Windows의 엄격한 변환과 같다).
        [[nodiscard]] std::u8string transcode_utf16(const std::span<const std::uint8_t> bytes, utf16_order order, const std::u8string_view charset, std::u8string& error)
        {
            if (bytes.size() % 2u != 0u)
            {
                error = charset_error(u8"The body has an odd byte count for ", charset, u8".");
                return {};
            }

            std::size_t index { 0 };
            if (bytes.size() >= 2u && bytes[0] == 0xFFu && bytes[1] == 0xFEu)
            {
                order = utf16_order::little;
                index = 2u;
            }
            else if (bytes.size() >= 2u && bytes[0] == 0xFEu && bytes[1] == 0xFFu)
            {
                order = utf16_order::big;
                index = 2u;
            }

            const auto unit_at = [&bytes, order](const std::size_t at) noexcept {
                const auto first { static_cast<char16_t>(bytes[at]) };
                const auto second { static_cast<char16_t>(bytes[at + 1u]) };
                return order == utf16_order::big ? static_cast<char16_t>((first << 8u) | second) : static_cast<char16_t>((second << 8u) | first);
            };

            std::u8string text {};
            text.reserve(bytes.size());
            while (index < bytes.size())
            {
                const char16_t unit { unit_at(index) };
                index += 2u;
                char32_t codepoint { unit };
                if (unit >= 0xD800u && unit <= 0xDBFFu)
                {
                    const char16_t trail { index < bytes.size() ? unit_at(index) : char16_t { 0 } };
                    if (trail < 0xDC00u || trail > 0xDFFFu)
                    {
                        error = charset_error(u8"Failed to convert the body from ", charset, u8": an unpaired surrogate.");
                        return {};
                    }
                    index += 2u;
                    codepoint = 0x10000u + ((static_cast<char32_t>(unit) - 0xD800u) << 10u) + (static_cast<char32_t>(trail) - 0xDC00u);
                }
                else if (unit >= 0xDC00u && unit <= 0xDFFFu)
                {
                    error = charset_error(u8"Failed to convert the body from ", charset, u8": an unpaired surrogate.");
                    return {};
                }
                text += text::utf8_encode(codepoint);
            }
            return text;
        }
    } // namespace

    // UTF-8은 몸 판정, UTF-16은 직접 변환, 나머지는 Android의 CharsetDecoder가 맡는다.
    http_text_transcoder codepage_text_transcoder()
    {
        return [](const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string& error) -> std::u8string {
            error.clear();
            const utf16_order order { find_utf16_order(charset) };
            if (order == utf16_order::none)
                return transcode_java(bytes, charset, error);
            if (bytes.empty())
                return {};
            return transcode_utf16(bytes, order, charset, error);
        };
    }
} // namespace luil::net

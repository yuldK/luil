#include "android/android_clipboard.h"

#include "android/android_text_input.h"
#include "android/java_vm.h"
#include "luil/ui/clipboard_image.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace luil::android {
    namespace {
        // 그림 내용을 한 번에 옮기는 Java 배열의 크기다. 큰 배열 하나를 되풀이해 써서 JNI 왕복을 줄인다.
        constexpr jint image_read_chunk_bytes { 64 * 1024 };

        [[nodiscard]] jobject clipboard_manager(JNIEnv& env, const jobject context)
        {
            const jclass context_class { env.FindClass("android/content/Context") };
            if (jni_failed(env) || context_class == nullptr)
                return nullptr;
            const jmethodID get_system_service { env.GetMethodID(context_class, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") };
            if (jni_failed(env) || get_system_service == nullptr)
                return nullptr;
            const jstring name { env.NewStringUTF("clipboard") };
            if (jni_failed(env) || name == nullptr)
                return nullptr;
            const jobject manager { env.CallObjectMethod(context, get_system_service, name) };
            return jni_failed(env) ? nullptr : manager;
        }

        // 지금 클립(`ClipData`)이다. 없거나 읽지 못하면 nullptr다.
        [[nodiscard]] jobject primary_clip(JNIEnv& env, const jobject context)
        {
            const jobject manager { clipboard_manager(env, context) };
            if (manager == nullptr)
                return nullptr;
            const jclass manager_class { env.GetObjectClass(manager) };
            const jmethodID get_primary_clip { env.GetMethodID(manager_class, "getPrimaryClip", "()Landroid/content/ClipData;") };
            if (jni_failed(env) || get_primary_clip == nullptr)
                return nullptr;
            const jobject clip { env.CallObjectMethod(manager, get_primary_clip) };
            return jni_failed(env) ? nullptr : clip;
        }

        // `CharSequence`를 글로 옮긴다. null이면 nullopt다.
        [[nodiscard]] std::optional<std::u8string> string_of(JNIEnv& env, const jobject sequence)
        {
            if (sequence == nullptr)
                return std::nullopt;
            const jclass object_class { env.FindClass("java/lang/Object") };
            const jmethodID to_string { object_class != nullptr ? env.GetMethodID(object_class, "toString", "()Ljava/lang/String;") : nullptr };
            if (jni_failed(env) || to_string == nullptr)
                return std::nullopt;
            const auto string { static_cast<jstring>(env.CallObjectMethod(sequence, to_string)) };
            if (jni_failed(env) || string == nullptr)
                return std::nullopt;
            const char* const characters { env.GetStringUTFChars(string, nullptr) };
            if (characters == nullptr)
            {
                static_cast<void>(jni_failed(env));
                return std::nullopt;
            }
            std::u8string result { utf8_from_modified_utf8(characters) };
            env.ReleaseStringUTFChars(string, characters);
            return result;
        }

        // `ClipData.Item`을 묻는 자리들이다.
        struct item_methods
        {
            jmethodID get_text { nullptr };
            jmethodID get_uri { nullptr };
            jmethodID coerce_to_text { nullptr };
            // 항목의 HTML이다 (API 16). 글과 한 항목에 실린다.
            jmethodID get_html_text { nullptr };
        };

        [[nodiscard]] std::optional<item_methods> find_item_methods(JNIEnv& env)
        {
            const jclass item_class { env.FindClass("android/content/ClipData$Item") };
            if (jni_failed(env) || item_class == nullptr)
                return std::nullopt;
            item_methods methods {};
            methods.get_text = env.GetMethodID(item_class, "getText", "()Ljava/lang/CharSequence;");
            if (jni_failed(env) || methods.get_text == nullptr)
                return std::nullopt;
            methods.get_uri = env.GetMethodID(item_class, "getUri", "()Landroid/net/Uri;");
            if (jni_failed(env) || methods.get_uri == nullptr)
                return std::nullopt;
            methods.coerce_to_text = env.GetMethodID(item_class, "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;");
            if (jni_failed(env) || methods.coerce_to_text == nullptr)
                return std::nullopt;
            methods.get_html_text = env.GetMethodID(item_class, "getHtmlText", "()Ljava/lang/String;");
            if (jni_failed(env) || methods.get_html_text == nullptr)
                return std::nullopt;
            return methods;
        }

        // 앱의 `ContentResolver`다.
        [[nodiscard]] jobject content_resolver(JNIEnv& env, const jobject context)
        {
            const jclass context_class { env.FindClass("android/content/Context") };
            if (jni_failed(env) || context_class == nullptr)
                return nullptr;
            const jmethodID get_content_resolver { env.GetMethodID(context_class, "getContentResolver", "()Landroid/content/ContentResolver;") };
            if (jni_failed(env) || get_content_resolver == nullptr)
                return nullptr;
            const jobject resolver { env.CallObjectMethod(context, get_content_resolver) };
            return jni_failed(env) ? nullptr : resolver;
        }

        // URI의 MIME 형식이다 (`ContentResolver.getType`). 모르면 빈 값이다.
        [[nodiscard]] std::u8string mime_type_of(JNIEnv& env, const jobject resolver, const jobject uri)
        {
            if (resolver == nullptr || uri == nullptr)
                return {};
            const jclass resolver_class { env.GetObjectClass(resolver) };
            const jmethodID get_type { env.GetMethodID(resolver_class, "getType", "(Landroid/net/Uri;)Ljava/lang/String;") };
            if (jni_failed(env) || get_type == nullptr)
                return {};
            const jobject type { env.CallObjectMethod(resolver, get_type, uri) };
            if (jni_failed(env) || type == nullptr)
                return {};
            return string_of(env, type).value_or(std::u8string {});
        }

        // URI의 내용을 끝까지 읽는다. 상한을 넘으면 거기서 멈추고 `too_large`가 참이다. 열지 못하면 nullopt다.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_uri_bytes(JNIEnv& env, const jobject resolver, const jobject uri, bool& too_large)
        {
            const jclass resolver_class { env.GetObjectClass(resolver) };
            const jmethodID open_input_stream { env.GetMethodID(resolver_class, "openInputStream", "(Landroid/net/Uri;)Ljava/io/InputStream;") };
            if (jni_failed(env) || open_input_stream == nullptr)
                return std::nullopt;
            // 다른 앱이 넣은 URI의 읽기 권한은 `getPrimaryClip`이 내준다. 권한이 없으면 예외라 그림이 없는 것으로 본다.
            const jobject stream { env.CallObjectMethod(resolver, open_input_stream, uri) };
            if (jni_failed(env) || stream == nullptr)
                return std::nullopt;
            const jclass stream_class { env.GetObjectClass(stream) };
            const jmethodID read { env.GetMethodID(stream_class, "read", "([B)I") };
            if (jni_failed(env) || read == nullptr)
                return std::nullopt;
            const jmethodID close { env.GetMethodID(stream_class, "close", "()V") };
            if (jni_failed(env) || close == nullptr)
                return std::nullopt;
            // 앞의 두 찾기가 실패하면 닫을 메서드를 모른다. 여기부터는 아는 메서드로 닫아 GC가 거둘 때까지 파일을 쥐지 않는다.
            const jbyteArray buffer { env.NewByteArray(image_read_chunk_bytes) };
            if (jni_failed(env) || buffer == nullptr)
            {
                env.CallVoidMethod(stream, close);
                static_cast<void>(jni_failed(env));
                return std::nullopt;
            }

            std::optional<std::vector<std::uint8_t>> bytes { std::vector<std::uint8_t> {} };
            while (true)
            {
                const jint count { env.CallIntMethod(stream, read, buffer) };
                if (jni_failed(env))
                {
                    bytes.reset();
                    break;
                }
                if (count < 0)
                    break;
                if (count == 0)
                    continue;
                if (bytes->size() + static_cast<std::size_t>(count) > max_clipboard_image_bytes)
                {
                    too_large = true;
                    bytes.reset();
                    break;
                }
                const std::size_t offset { bytes->size() };
                bytes->resize(offset + static_cast<std::size_t>(count));
                env.GetByteArrayRegion(buffer, 0, count, reinterpret_cast<jbyte*>(bytes->data() + offset));
                if (jni_failed(env))
                {
                    bytes.reset();
                    break;
                }
            }
            // 닫기의 실패는 읽은 바이트를 버릴 이유가 아니다.
            env.CallVoidMethod(stream, close);
            static_cast<void>(jni_failed(env));
            return bytes;
        }
    }

    bool copy_text_to_clipboard(JNIEnv& env, const jobject context, const std::u8string_view text)
    {
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return false;
        const jobject manager { clipboard_manager(env, context) };
        if (manager == nullptr)
            return false;

        const jclass clip_data_class { env.FindClass("android/content/ClipData") };
        if (jni_failed(env) || clip_data_class == nullptr)
            return false;
        const jmethodID new_plain_text { env.GetStaticMethodID(clip_data_class, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;") };
        if (jni_failed(env) || new_plain_text == nullptr)
            return false;
        // JNI의 글은 modified UTF-8이다. BMP 밖 글자(이모지)는 그대로 넘기면 깨진다.
        const std::string modified { modified_utf8_from_utf8(text) };
        const jstring label { env.NewStringUTF("") };
        if (jni_failed(env) || label == nullptr)
            return false;
        const jstring value { env.NewStringUTF(modified.c_str()) };
        if (jni_failed(env) || value == nullptr)
            return false;
        const jobject clip { env.CallStaticObjectMethod(clip_data_class, new_plain_text, label, value) };
        if (jni_failed(env) || clip == nullptr)
            return false;

        const jclass manager_class { env.GetObjectClass(manager) };
        const jmethodID set_primary_clip { env.GetMethodID(manager_class, "setPrimaryClip", "(Landroid/content/ClipData;)V") };
        if (jni_failed(env) || set_primary_clip == nullptr)
            return false;
        env.CallVoidMethod(manager, set_primary_clip, clip);
        return jni_failed(env) == false;
    }

    bool copy_rich_text_to_clipboard(JNIEnv& env, const jobject context, const std::u8string_view text, const std::u8string_view html)
    {
        if (html.empty())
            return copy_text_to_clipboard(env, context, text);
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return false;
        const jobject manager { clipboard_manager(env, context) };
        if (manager == nullptr)
            return false;

        const jclass clip_data_class { env.FindClass("android/content/ClipData") };
        if (jni_failed(env) || clip_data_class == nullptr)
            return false;
        // 글과 HTML을 한 항목에 싣는 framework 정적 메서드다 (API 16). Java 클래스를 쓰지 않는다.
        const jmethodID new_html_text { env.GetStaticMethodID(clip_data_class, "newHtmlText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;Ljava/lang/String;)Landroid/content/ClipData;") };
        if (jni_failed(env) || new_html_text == nullptr)
            return false;
        // JNI의 글은 modified UTF-8이다. BMP 밖 글자(이모지)는 그대로 넘기면 깨진다.
        const std::string modified_text { modified_utf8_from_utf8(text) };
        const std::string modified_html { modified_utf8_from_utf8(html) };
        const jstring label { env.NewStringUTF("") };
        if (jni_failed(env) || label == nullptr)
            return false;
        const jstring text_value { env.NewStringUTF(modified_text.c_str()) };
        if (jni_failed(env) || text_value == nullptr)
            return false;
        const jstring html_value { env.NewStringUTF(modified_html.c_str()) };
        if (jni_failed(env) || html_value == nullptr)
            return false;
        const jobject clip { env.CallStaticObjectMethod(clip_data_class, new_html_text, label, text_value, html_value) };
        if (jni_failed(env) || clip == nullptr)
            return false;

        const jclass manager_class { env.GetObjectClass(manager) };
        const jmethodID set_primary_clip { env.GetMethodID(manager_class, "setPrimaryClip", "(Landroid/content/ClipData;)V") };
        if (jni_failed(env) || set_primary_clip == nullptr)
            return false;
        env.CallVoidMethod(manager, set_primary_clip, clip);
        return jni_failed(env) == false;
    }

    std::optional<std::u8string> read_text_from_clipboard(JNIEnv& env, const jobject context)
    {
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return std::nullopt;
        const jobject clip { primary_clip(env, context) };
        if (clip == nullptr)
            return std::nullopt;

        const jclass clip_data_class { env.GetObjectClass(clip) };
        const jmethodID get_item_count { env.GetMethodID(clip_data_class, "getItemCount", "()I") };
        if (jni_failed(env) || get_item_count == nullptr)
            return std::nullopt;
        const jmethodID get_item_at { env.GetMethodID(clip_data_class, "getItemAt", "(I)Landroid/content/ClipData$Item;") };
        if (jni_failed(env) || get_item_at == nullptr)
            return std::nullopt;
        const jint count { env.CallIntMethod(clip, get_item_count) };
        if (jni_failed(env) || count <= 0)
            return std::nullopt;
        const jobject item { env.CallObjectMethod(clip, get_item_at, 0) };
        if (jni_failed(env) || item == nullptr)
            return std::nullopt;
        const std::optional<item_methods> methods { find_item_methods(env) };
        if (methods.has_value() == false)
            return std::nullopt;

        // 글이 없는 URI 항목은 형식이 글일 때만 글로 바꾼다. 그림 URI가 `content://…`로 칸에 붙지 않게 한다.
        //  - Win32도 그림만 든 클립보드에서는 아무것도 붙이지 않는다. 링크 주소만 든 URI 항목도 붙지 않는 대가가 있다.
        const jobject text { env.CallObjectMethod(item, methods->get_text) };
        if (jni_failed(env))
            return std::nullopt;
        if (text == nullptr)
        {
            const jobject uri { env.CallObjectMethod(item, methods->get_uri) };
            if (jni_failed(env))
                return std::nullopt;
            if (uri != nullptr && mime_type_of(env, content_resolver(env, context), uri).starts_with(u8"text/") == false)
                return std::nullopt;
        }

        // 글이 아닌 항목(글 URI·intent)도 글로 바꿔 준다.
        const jobject sequence { env.CallObjectMethod(item, methods->coerce_to_text, context) };
        if (jni_failed(env))
            return std::nullopt;
        return string_of(env, sequence);
    }

    clipboard_data read_clipboard_data(JNIEnv& env, const jobject context)
    {
        clipboard_data data {};
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return data;
        const jobject clip { primary_clip(env, context) };
        if (clip == nullptr)
            return data;
        const jclass clip_data_class { env.GetObjectClass(clip) };
        const jmethodID get_item_count { env.GetMethodID(clip_data_class, "getItemCount", "()I") };
        if (jni_failed(env) || get_item_count == nullptr)
            return data;
        const jmethodID get_item_at { env.GetMethodID(clip_data_class, "getItemAt", "(I)Landroid/content/ClipData$Item;") };
        if (jni_failed(env) || get_item_at == nullptr)
            return data;
        const jint count { env.CallIntMethod(clip, get_item_count) };
        if (jni_failed(env) || count <= 0)
            return data;
        const std::optional<item_methods> methods { find_item_methods(env) };
        if (methods.has_value() == false)
            return data;
        // 형식을 묻지 못하면 글만 읽는다.
        const jobject resolver { content_resolver(env, context) };

        bool has_text { false };
        for (jint index = 0; index < count && (has_text == false || data.image_format == clipboard_image_format::none); ++index)
        {
            // 항목마다 참조를 거둔다. 항목이 많아도 지역 참조가 쌓이지 않는다.
            const jni_local_frame item_frame { env };
            if (item_frame.pushed() == false)
                break;
            const jobject item { env.CallObjectMethod(clip, get_item_at, index) };
            if (jni_failed(env) || item == nullptr)
                continue;
            if (has_text == false)
            {
                const jobject text { env.CallObjectMethod(item, methods->get_text) };
                if (jni_failed(env) == false && text != nullptr)
                    if (std::optional<std::u8string> value { string_of(env, text) }; value.has_value())
                    {
                        data.text = std::move(*value);
                        has_text = true;
                        // HTML은 글을 집은 그 항목의 것이다. 없거나 읽지 못하면 비운다 (`rich_text_paste`가 글로 물러선다).
                        const jobject html { env.CallObjectMethod(item, methods->get_html_text) };
                        if (jni_failed(env) == false && html != nullptr)
                            data.html = string_of(env, html).value_or(std::u8string {});
                    }
            }
            if (data.image_format != clipboard_image_format::none || resolver == nullptr)
                continue;
            const jobject uri { env.CallObjectMethod(item, methods->get_uri) };
            if (jni_failed(env) || uri == nullptr)
                continue;
            if (mime_type_of(env, resolver, uri).starts_with(u8"image/") == false)
                continue;
            bool too_large { false };
            std::optional<std::vector<std::uint8_t>> bytes { read_uri_bytes(env, resolver, uri, too_large) };
            if (too_large)
            {
                data.image_format = clipboard_image_format::encoded;
                data.image_too_large = true;
                continue;
            }
            if (bytes.has_value() == false || bytes->empty())
                continue;
            data.image = std::make_shared<const std::vector<std::uint8_t>>(std::move(*bytes));
            data.image_format = clipboard_image_format::encoded;
        }
        return data;
    }
}

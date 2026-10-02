#include "android/android_clipboard.h"

#include "android/android_text_input.h"

namespace luil::android {
    namespace {
        // 지역 참조를 한 묶음으로 거둔다. 호출 하나가 만드는 참조 수는 몇 개뿐이다.
        class local_frame final
        {
        public:
            explicit local_frame(JNIEnv& env) noexcept
                : env_ { &env }
                , pushed_ { env.PushLocalFrame(16) == JNI_OK }
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
            JNIEnv* env_;
            bool pushed_;
        };

        // Java 예외가 났으면 거두고 참이다.
        [[nodiscard]] bool failed(JNIEnv& env) noexcept
        {
            if (env.ExceptionCheck() == JNI_FALSE)
                return false;
            env.ExceptionClear();
            return true;
        }

        [[nodiscard]] jobject clipboard_manager(JNIEnv& env, const jobject context)
        {
            const jclass context_class { env.FindClass("android/content/Context") };
            if (failed(env) || context_class == nullptr)
                return nullptr;
            const jmethodID get_system_service { env.GetMethodID(context_class, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") };
            const jstring name { env.NewStringUTF("clipboard") };
            if (failed(env) || get_system_service == nullptr || name == nullptr)
                return nullptr;
            const jobject manager { env.CallObjectMethod(context, get_system_service, name) };
            return failed(env) ? nullptr : manager;
        }
    } // namespace

    bool copy_text_to_clipboard(JNIEnv& env, const jobject context, const std::u8string_view text)
    {
        const local_frame frame { env };
        if (frame.pushed() == false)
            return false;
        const jobject manager { clipboard_manager(env, context) };
        if (manager == nullptr)
            return false;

        const jclass clip_data_class { env.FindClass("android/content/ClipData") };
        if (failed(env) || clip_data_class == nullptr)
            return false;
        const jmethodID new_plain_text { env.GetStaticMethodID(clip_data_class, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;") };
        // JNI의 글은 modified UTF-8이다. BMP 밖 글자(이모지)는 그대로 넘기면 깨진다.
        const std::string modified { modified_utf8_from_utf8(text) };
        const jstring label { env.NewStringUTF("") };
        const jstring value { env.NewStringUTF(modified.c_str()) };
        if (failed(env) || new_plain_text == nullptr || label == nullptr || value == nullptr)
            return false;
        const jobject clip { env.CallStaticObjectMethod(clip_data_class, new_plain_text, label, value) };
        if (failed(env) || clip == nullptr)
            return false;

        const jclass manager_class { env.GetObjectClass(manager) };
        const jmethodID set_primary_clip { env.GetMethodID(manager_class, "setPrimaryClip", "(Landroid/content/ClipData;)V") };
        if (failed(env) || set_primary_clip == nullptr)
            return false;
        env.CallVoidMethod(manager, set_primary_clip, clip);
        return failed(env) == false;
    }

    std::optional<std::u8string> read_text_from_clipboard(JNIEnv& env, const jobject context)
    {
        const local_frame frame { env };
        if (frame.pushed() == false)
            return std::nullopt;
        const jobject manager { clipboard_manager(env, context) };
        if (manager == nullptr)
            return std::nullopt;

        const jclass manager_class { env.GetObjectClass(manager) };
        const jmethodID get_primary_clip { env.GetMethodID(manager_class, "getPrimaryClip", "()Landroid/content/ClipData;") };
        if (failed(env) || get_primary_clip == nullptr)
            return std::nullopt;
        const jobject clip { env.CallObjectMethod(manager, get_primary_clip) };
        if (failed(env) || clip == nullptr)
            return std::nullopt;

        const jclass clip_data_class { env.GetObjectClass(clip) };
        const jmethodID get_item_count { env.GetMethodID(clip_data_class, "getItemCount", "()I") };
        const jmethodID get_item_at { env.GetMethodID(clip_data_class, "getItemAt", "(I)Landroid/content/ClipData$Item;") };
        if (failed(env) || get_item_count == nullptr || get_item_at == nullptr)
            return std::nullopt;
        const jint count { env.CallIntMethod(clip, get_item_count) };
        if (failed(env) || count <= 0)
            return std::nullopt;
        const jobject item { env.CallObjectMethod(clip, get_item_at, 0) };
        if (failed(env) || item == nullptr)
            return std::nullopt;

        // 글이 아닌 항목(URI·intent)도 글로 바꿔 준다.
        const jclass item_class { env.GetObjectClass(item) };
        const jmethodID coerce_to_text { env.GetMethodID(item_class, "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;") };
        if (failed(env) || coerce_to_text == nullptr)
            return std::nullopt;
        const jobject sequence { env.CallObjectMethod(item, coerce_to_text, context) };
        if (failed(env) || sequence == nullptr)
            return std::nullopt;
        const jclass object_class { env.FindClass("java/lang/Object") };
        const jmethodID to_string { object_class != nullptr ? env.GetMethodID(object_class, "toString", "()Ljava/lang/String;") : nullptr };
        if (failed(env) || to_string == nullptr)
            return std::nullopt;
        const auto string { static_cast<jstring>(env.CallObjectMethod(sequence, to_string)) };
        if (failed(env) || string == nullptr)
            return std::nullopt;

        const char* const characters { env.GetStringUTFChars(string, nullptr) };
        if (characters == nullptr)
        {
            static_cast<void>(failed(env));
            return std::nullopt;
        }
        std::u8string result { utf8_from_modified_utf8(characters) };
        env.ReleaseStringUTFChars(string, characters);
        return result;
    }
} // namespace luil::android

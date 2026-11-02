#include "luil/app/notification.h"

#include "android/android_text_input.h"
#include "android/android_window_control.h"
#include "android/device_request.h"
#include "android/java_vm.h"
#include "host/notification_queue.h"
#include "luil/ui/image_decode.h"
#include "ui/image_pixels.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"

#include <android/api-level.h>

#include <jni.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// OS 알림의 Android 길이다 (os-notification-design.md의 "Android"). 모두 기존 framework 클래스를 JNI로 부르고 Java 소스는 없다.
//  - 앱(Application) Context로 부르므로 Activity 없이 어느 thread에서나 된다. 부른 thread가 JVM에 붙지 않았으면 붙인다.
//  - 누른 알림은 런처 intent의 extra로만 온다. 살아 있는 앱의 눌림은 `onNewIntent`가 들어 Java 없이는 받을 수 없다 (결정 5).
//    콜드 스타트의 extra는 android_app.cpp가 첫 초점에서 읽는다.
namespace luil {
    namespace {
        using android::jni_failed;

        constexpr const char* channel_id { "luil.default" };
        // 런처 intent에 싣는 extra 이름이다. android_app.cpp가 같은 이름으로 읽는다.
        constexpr const char* extra_name { "luil.notification.id" };
        // `NotificationManager.IMPORTANCE_*`다.
        constexpr jint importance_none { 0 };
        constexpr jint importance_default { 3 };
        // `PendingIntent.FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT`다.
        constexpr jint pending_intent_flags { 0x04000000 | 0x08000000 };
        // `Notification.Builder.setSilent`가 생긴 판이다 (Android 12).
        constexpr int api_level_silent { 31 };
        // 같은 id는 같은 알림이다. Android의 (tag, 번호) 쌍에서 번호는 늘 0이다.
        constexpr jint notification_number { 0 };

        // 채널은 프로세스에서 한 번 만든다. 같은 id로 다시 만들어도 해가 없어 경합은 막지 않는다.
        std::atomic<bool> channel_created { false };

        [[nodiscard]] platform_result succeeded() noexcept
        {
            return { platform_status::ok, {}, std::nullopt };
        }

        [[nodiscard]] platform_result failure(std::u8string error)
        {
            return { platform_status::failed, std::move(error), std::nullopt };
        }

        // 한 번의 JNI 호출 묶음이다. 실패하면 `failed`가 서고 뒤의 호출은 아무것도 하지 않는다.
        //  - 호출마다 예외를 거둔다. 예외가 남은 채로는 대부분의 JNI 함수를 부를 수 없다.
        class jni_calls
        {
        public:
            explicit jni_calls(JNIEnv& env) noexcept
                : env_ { env }
            {
            }

            [[nodiscard]] bool failed() const noexcept
            {
                return failed_;
            }

            [[nodiscard]] JNIEnv& env() const noexcept
            {
                return env_;
            }

            [[nodiscard]] jclass find_class(const char* const name)
            {
                if (failed_)
                    return nullptr;
                return checked(env_.FindClass(name));
            }

            [[nodiscard]] jstring string(const std::u8string_view text)
            {
                if (failed_)
                    return nullptr;
                return checked(env_.NewStringUTF(android::modified_utf8_from_utf8(text).c_str()));
            }

            [[nodiscard]] jstring string(const char* const text)
            {
                if (failed_)
                    return nullptr;
                return checked(env_.NewStringUTF(text));
            }

            template<typename... argument_types>
            [[nodiscard]] jobject object(const jobject target, const char* const name, const char* const signature, argument_types... arguments)
            {
                const jmethodID method { method_of(target, name, signature) };
                if (method == nullptr)
                    return nullptr;
                return checked(env_.CallObjectMethod(target, method, arguments...));
            }

            template<typename... argument_types>
            void call(const jobject target, const char* const name, const char* const signature, argument_types... arguments)
            {
                const jmethodID method { method_of(target, name, signature) };
                if (method == nullptr)
                    return;
                env_.CallVoidMethod(target, method, arguments...);
                failed_ = jni_failed(env_);
            }

            template<typename... argument_types>
            [[nodiscard]] std::optional<jint> integer(const jobject target, const char* const name, const char* const signature, argument_types... arguments)
            {
                const jmethodID method { method_of(target, name, signature) };
                if (method == nullptr)
                    return std::nullopt;
                const jint value { env_.CallIntMethod(target, method, arguments...) };
                failed_ = jni_failed(env_);
                if (failed_)
                    return std::nullopt;
                return value;
            }

            template<typename... argument_types>
            [[nodiscard]] std::optional<bool> boolean(const jobject target, const char* const name, const char* const signature, argument_types... arguments)
            {
                const jmethodID method { method_of(target, name, signature) };
                if (method == nullptr)
                    return std::nullopt;
                const jboolean value { env_.CallBooleanMethod(target, method, arguments...) };
                failed_ = jni_failed(env_);
                if (failed_)
                    return std::nullopt;
                return value != JNI_FALSE;
            }

            template<typename... argument_types>
            [[nodiscard]] jobject construct(const jclass type, const char* const signature, argument_types... arguments)
            {
                if (failed_ || type == nullptr)
                {
                    failed_ = true;
                    return nullptr;
                }
                const jmethodID constructor { checked(env_.GetMethodID(type, "<init>", signature)) };
                if (constructor == nullptr)
                    return nullptr;
                return checked(env_.NewObject(type, constructor, arguments...));
            }

            template<typename... argument_types>
            [[nodiscard]] jobject static_object(const jclass type, const char* const name, const char* const signature, argument_types... arguments)
            {
                if (failed_ || type == nullptr)
                {
                    failed_ = true;
                    return nullptr;
                }
                const jmethodID method { checked(env_.GetStaticMethodID(type, name, signature)) };
                if (method == nullptr)
                    return nullptr;
                return checked(env_.CallStaticObjectMethod(type, method, arguments...));
            }

            [[nodiscard]] jobject static_field(const jclass type, const char* const name, const char* const signature)
            {
                if (failed_ || type == nullptr)
                {
                    failed_ = true;
                    return nullptr;
                }
                const jfieldID field { checked(env_.GetStaticFieldID(type, name, signature)) };
                if (field == nullptr)
                    return nullptr;
                return checked(env_.GetStaticObjectField(type, field));
            }

            [[nodiscard]] std::optional<jint> int_field(const jobject target, const char* const name)
            {
                if (failed_ || target == nullptr)
                {
                    failed_ = true;
                    return std::nullopt;
                }
                const jfieldID field { checked(env_.GetFieldID(env_.GetObjectClass(target), name, "I")) };
                if (field == nullptr)
                    return std::nullopt;
                const jint value { env_.GetIntField(target, field) };
                failed_ = jni_failed(env_);
                if (failed_)
                    return std::nullopt;
                return value;
            }

        private:
            [[nodiscard]] jmethodID method_of(const jobject target, const char* const name, const char* const signature)
            {
                if (failed_ || target == nullptr)
                {
                    failed_ = true;
                    return nullptr;
                }
                return checked(env_.GetMethodID(env_.GetObjectClass(target), name, signature));
            }

            // 예외가 났거나 값이 없으면 실패로 둔다.
            template<typename value_type>
            [[nodiscard]] value_type checked(const value_type value)
            {
                if (jni_failed(env_) || value == nullptr)
                {
                    failed_ = true;
                    return nullptr;
                }
                return value;
            }

            JNIEnv& env_;
            bool failed_ { false };
        };

        // 이 호출 동안 쓸 JNI 자리다. 부른 thread를 JVM에 붙이고 지역 참조를 한 틀에 모은다.
        class notification_scope
        {
        public:
            notification_scope() noexcept
                : thread_ { "luil-notification" }
            {
                context_ = android::application_context();
                if (thread_.env() != nullptr)
                    frame_.emplace(*thread_.env(), 64);
            }

            [[nodiscard]] bool ready() const noexcept
            {
                return context_ != nullptr && frame_.has_value() && frame_->pushed();
            }

            [[nodiscard]] JNIEnv& env() const noexcept
            {
                return *thread_.env();
            }

            [[nodiscard]] jobject context() const noexcept
            {
                return context_;
            }

        private:
            android::jni_thread_scope thread_;
            std::optional<android::jni_local_frame> frame_ {};
            jobject context_ { nullptr };
        };

        [[nodiscard]] platform_result not_ready()
        {
            return { platform_status::unsupported, u8"The app context is not available; the app must link luil::luil, which captures it in GameActivity_onCreate.", std::nullopt };
        }

        [[nodiscard]] jobject notification_manager(jni_calls& calls, const jobject context)
        {
            const jstring service { calls.string("notification") };
            return calls.object(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", service);
        }

        // 채널 하나(`luil.default`)를 만든다. 이름은 앱 이름이다. 실패해도 다음 올리기에서 다시 만든다.
        void ensure_channel(jni_calls& calls, const jobject context, const jobject manager)
        {
            if (channel_created.load())
                return;
            const jobject package_manager { calls.object(context, "getPackageManager", "()Landroid/content/pm/PackageManager;") };
            const jobject info { calls.object(context, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;") };
            const jobject label { calls.object(package_manager, "getApplicationLabel", "(Landroid/content/pm/ApplicationInfo;)Ljava/lang/CharSequence;", info) };
            const jstring id { calls.string(channel_id) };
            const jobject channel { calls.construct(calls.find_class("android/app/NotificationChannel"), "(Ljava/lang/String;Ljava/lang/CharSequence;I)V", id, label, importance_default) };
            calls.call(manager, "createNotificationChannel", "(Landroid/app/NotificationChannel;)V", channel);
            if (calls.failed() == false)
                channel_created.store(true);
        }

        [[nodiscard]] notification_availability availability_of(jni_calls& calls, const jobject context, const jobject manager)
        {
            const std::optional<bool> enabled { calls.boolean(manager, "areNotificationsEnabled", "()Z") };
            if (enabled.has_value() == false)
                return notification_availability::unsupported;
            bool open { *enabled };
            // 사용자가 채널만 끈 것도 막힘이다. 아직 만들지 않은 채널은 null이다.
            const jstring id { calls.string(channel_id) };
            if (calls.failed() == false)
            {
                const jmethodID get_channel { calls.env().GetMethodID(calls.env().GetObjectClass(manager), "getNotificationChannel", "(Ljava/lang/String;)Landroid/app/NotificationChannel;") };
                if (get_channel != nullptr && jni_failed(calls.env()) == false)
                {
                    const jobject channel { calls.env().CallObjectMethod(manager, get_channel, id) };
                    if (jni_failed(calls.env()) == false && channel != nullptr)
                    {
                        const std::optional<jint> importance { calls.integer(channel, "getImportance", "()I") };
                        open = open && importance.has_value() && *importance != importance_none;
                    }
                }
            }
            if (open)
                return notification_availability::available;
            JNIEnv& env { calls.env() };
            const char* const permission { android::android_permission_name(app_permission::notifications, android_get_device_api_level(), android::read_target_sdk(env, context)) };
            if (permission != nullptr && android::has_permission(env, context, permission) == false)
                return notification_availability::permission_needed;
            return notification_availability::blocked;
        }

        // 그림을 긴 변 256 px로 줄여 Bitmap으로 짓는다. 실패하면 nullptr다 (그림 없이 올린다).
        [[nodiscard]] jobject make_bitmap(jni_calls& calls, const ui_image& image)
        {
            image_decode_options options {};
            options.max_width = notification_image_limit;
            options.max_height = notification_image_limit;
            options.downscale = image_downscale::high_quality;
            image_decode_error decode_error {};
            const ui_image scaled { downscale_image(image, options, decode_error) };
            SkBitmap pixels {};
            std::u8string error {};
            if (read_image_pixels(scaled.valid() ? scaled : image, pixels, error) == false || pixels.width() <= 0 || pixels.height() <= 0)
                return nullptr;
            // `Bitmap.createBitmap(int[], ...)`은 premul이 아닌 ARGB 정수다. `getColor`가 바이트 순서와 premul을 함께 푼다.
            const int width { pixels.width() };
            const int height { pixels.height() };
            std::vector<jint> colors(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                    colors[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = static_cast<jint>(pixels.getColor(x, y));
            }
            JNIEnv& env { calls.env() };
            const jintArray array { env.NewIntArray(static_cast<jsize>(colors.size())) };
            if (array == nullptr || jni_failed(env))
                return nullptr;
            env.SetIntArrayRegion(array, 0, static_cast<jsize>(colors.size()), colors.data());
            if (jni_failed(env))
                return nullptr;
            const jclass config_type { calls.find_class("android/graphics/Bitmap$Config") };
            const jobject config { calls.static_field(config_type, "ARGB_8888", "Landroid/graphics/Bitmap$Config;") };
            return calls.static_object(calls.find_class("android/graphics/Bitmap"), "createBitmap", "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;", array, width, height, config);
        }

        // 누르면 런처 Activity를 세우는 PendingIntent다. extra는 intent 비교에 들지 않아 task가 살아 있으면 그 task가 앞으로 온다.
        [[nodiscard]] jobject launch_intent(jni_calls& calls, const jobject context, const std::u8string& id)
        {
            const jobject package_manager { calls.object(context, "getPackageManager", "()Landroid/content/pm/PackageManager;") };
            const jobject package_name { calls.object(context, "getPackageName", "()Ljava/lang/String;") };
            const jobject intent { calls.object(package_manager, "getLaunchIntentForPackage", "(Ljava/lang/String;)Landroid/content/Intent;", package_name) };
            const jstring name { calls.string(extra_name) };
            const jstring value { calls.string(id) };
            static_cast<void>(calls.object(intent, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;", name, value));
            const jclass pending_type { calls.find_class("android/app/PendingIntent") };
            return calls.static_object(pending_type, "getActivity", "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;", context, notification_request_code(id), intent, pending_intent_flags);
        }

        constexpr const char* builder_signature_text { "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;" };
    }

    notification_availability notification_status()
    {
        const notification_scope scope {};
        if (scope.ready() == false)
            return notification_availability::unsupported;
        jni_calls calls { scope.env() };
        const jobject manager { notification_manager(calls, scope.context()) };
        if (calls.failed())
            return notification_availability::unsupported;
        return availability_of(calls, scope.context(), manager);
    }

    platform_result show_notification(const notification& value)
    {
        std::u8string error {};
        const std::optional<notification_text> text { prepare_notification_text(value, error) };
        if (text.has_value() == false)
            return failure(std::move(error));
        const notification_scope scope {};
        if (scope.ready() == false)
            return not_ready();
        jni_calls calls { scope.env() };
        const jobject context { scope.context() };
        const jobject manager { notification_manager(calls, context) };
        // 채널을 먼저 만든다. targetSdk가 33보다 낮은 앱은 시스템이 이때 권한을 묻고, 그 전에는 알림이 꺼진 것으로 보인다.
        ensure_channel(calls, context, manager);
        if (calls.failed())
            return failure(u8"Failed to create the notification channel.");
        switch (availability_of(calls, context, manager))
        {
        case notification_availability::available:
            break;
        case notification_availability::permission_needed:
            return failure(u8"The app needs the notification permission (POST_NOTIFICATIONS) before it can show notifications.");
        case notification_availability::blocked:
            return failure(u8"Notifications are turned off for this app or its channel.");
        case notification_availability::unsupported:
            return { platform_status::unsupported, u8"The notification manager is not available.", std::nullopt };
        }

        const jobject info { calls.object(context, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;") };
        const std::optional<jint> icon { calls.int_field(info, "icon") };
        const jstring id { calls.string(channel_id) };
        const jobject builder { calls.construct(calls.find_class("android/app/Notification$Builder"), "(Landroid/content/Context;Ljava/lang/String;)V", context, id) };
        if (calls.failed() || icon.has_value() == false)
            return failure(u8"Failed to start the notification builder.");
        // 작은 아이콘은 앱 아이콘이다. 단색 실루엣으로 그려져 모양이 뭉개질 수 있으나 앱 자원을 모르는 라이브러리가 고를 수 있는 유일한 값이다.
        static_cast<void>(calls.object(builder, "setSmallIcon", "(I)Landroid/app/Notification$Builder;", *icon));
        const jstring title { calls.string(text->title) };
        static_cast<void>(calls.object(builder, "setContentTitle", builder_signature_text, title));
        if (text->body.empty() == false)
        {
            const jstring body { calls.string(text->body) };
            static_cast<void>(calls.object(builder, "setContentText", builder_signature_text, body));
            const jobject style { calls.construct(calls.find_class("android/app/Notification$BigTextStyle"), "()V") };
            static_cast<void>(calls.object(style, "bigText", "(Ljava/lang/CharSequence;)Landroid/app/Notification$BigTextStyle;", body));
            static_cast<void>(calls.object(builder, "setStyle", "(Landroid/app/Notification$Style;)Landroid/app/Notification$Builder;", style));
        }
        static_cast<void>(calls.object(builder, "setAutoCancel", "(Z)Landroid/app/Notification$Builder;", JNI_TRUE));
        // 그 아래 판은 채널 소리를 따른다.
        if (value.silent && android_get_device_api_level() >= api_level_silent)
            static_cast<void>(calls.object(builder, "setSilent", "(Z)Landroid/app/Notification$Builder;", JNI_TRUE));
        if (value.image.valid() && calls.failed() == false)
        {
            if (const jobject bitmap { make_bitmap(calls, value.image) }; bitmap != nullptr)
                static_cast<void>(calls.object(builder, "setLargeIcon", "(Landroid/graphics/Bitmap;)Landroid/app/Notification$Builder;", bitmap));
        }
        const jobject pending { launch_intent(calls, context, value.id) };
        static_cast<void>(calls.object(builder, "setContentIntent", "(Landroid/app/PendingIntent;)Landroid/app/Notification$Builder;", pending));
        const jobject built { calls.object(builder, "build", "()Landroid/app/Notification;") };
        const jstring tag { calls.string(value.id) };
        calls.call(manager, "notify", "(Ljava/lang/String;ILandroid/app/Notification;)V", tag, notification_number, built);
        if (calls.failed())
            return failure(u8"Failed to build or post the notification.");
        return succeeded();
    }

    platform_result close_notification(const std::u8string_view id)
    {
        if (notification_id_valid(id) == false)
            return failure(u8"The notification id must be 1 to 64 ASCII letters, digits, '.', '_' or '-'.");
        const notification_scope scope {};
        if (scope.ready() == false)
            return not_ready();
        jni_calls calls { scope.env() };
        const jobject manager { notification_manager(calls, scope.context()) };
        const jstring tag { calls.string(id) };
        calls.call(manager, "cancel", "(Ljava/lang/String;I)V", tag, notification_number);
        if (calls.failed())
            return failure(u8"Failed to cancel the notification.");
        return succeeded();
    }

    bool notification_launch(const std::span<const std::u8string> arguments) noexcept
    {
        // Android의 알림은 인자가 아니라 intent extra로 앱을 띄운다.
        static_cast<void>(arguments);
        return false;
    }

    platform_result close_all_notifications()
    {
        const notification_scope scope {};
        if (scope.ready() == false)
            return not_ready();
        jni_calls calls { scope.env() };
        const jobject manager { notification_manager(calls, scope.context()) };
        calls.call(manager, "cancelAll", "()V");
        if (calls.failed())
            return failure(u8"Failed to cancel the notifications.");
        return succeeded();
    }
}

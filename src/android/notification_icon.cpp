#include "android/notification_icon.h"

#include "android/java_vm.h"

namespace luil::android {
    namespace {
        // framework 갈음 아이콘의 이름이다. 번호는 판마다 같다는 보장이 없어 박지 않고 정적 필드로 읽는다.
        //  - `ic_dialog_info`는 API 1부터 공개 자원이고 꽉 찬 원판에서 "i"를 투명하게 뚫은 그림이라, 알파만 남기는 작은 아이콘
        //    그리기에서도 원 안의 빈 "i"로 모양이 선다.
        //  - `sym_def_app_icon`(시스템의 기본 앱 아이콘)은 뚫린 데가 없는 런처 그림이라 알파만 남기면 덩어리가 된다.
        //    `stat_notify_chat`·`stat_sys_warning` 같은 상태 표시줄 그림은 대화·경고라는 뜻이 실려 아무 알림에나 맞지 않는다.
        constexpr const char* fallback_icon_name { "ic_dialog_info" };

        [[nodiscard]] small_icon_choice read_framework_icon(JNIEnv& env)
        {
            const jclass drawable_type { env.FindClass("android/R$drawable") };
            if (jni_failed(env) || drawable_type == nullptr)
                return { small_icon_source::missing, 0 };
            const jfieldID field { env.GetStaticFieldID(drawable_type, fallback_icon_name, "I") };
            if (jni_failed(env) || field == nullptr)
                return { small_icon_source::missing, 0 };
            const jint icon { env.GetStaticIntField(drawable_type, field) };
            if (jni_failed(env) || icon == 0)
                return { small_icon_source::missing, 0 };
            return { small_icon_source::framework, icon };
        }
    }

    // 실패 검사는 `jni_failed`를 nullptr 검사보다 앞에 둔다 (android_settings.cpp와 같은 까닭).
    small_icon_choice read_notification_small_icon(JNIEnv& env, const jobject context)
    {
        const jni_local_frame frame { env };
        if (jni_failed(env) || frame.pushed() == false || context == nullptr)
            return {};
        const jmethodID get_info { env.GetMethodID(env.GetObjectClass(context), "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;") };
        if (jni_failed(env) || get_info == nullptr)
            return {};
        const jobject info { env.CallObjectMethod(context, get_info) };
        if (jni_failed(env) || info == nullptr)
            return {};
        const jfieldID icon_field { env.GetFieldID(env.GetObjectClass(info), "icon", "I") };
        if (jni_failed(env) || icon_field == nullptr)
            return {};
        const jint icon { env.GetIntField(info, icon_field) };
        if (jni_failed(env))
            return {};
        if (icon != 0)
            return { small_icon_source::app, icon };
        return read_framework_icon(env);
    }
}

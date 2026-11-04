#include "android/notification_intent.h"

#include "android/java_vm.h"

#include <string>

namespace luil::android {
    bool set_notification_intent_identity(JNIEnv& env, const jobject intent, const std::u8string_view id)
    {
        if (intent == nullptr)
            return false;
        const jni_local_frame frame { env };
        if (frame.pushed() == false)
            return false;

        // id는 ASCII 영숫자·점·밑줄·하이픈뿐이라 URI 안에서 이스케이프 없이 구별된다.
        const std::u8string data { u8"luil-notification:" + std::u8string { id } };
        const jstring text { env.NewStringUTF(reinterpret_cast<const char*>(data.c_str())) };
        if (jni_failed(env) || text == nullptr)
            return false;
        const jclass uri_type { env.FindClass("android/net/Uri") };
        if (jni_failed(env) || uri_type == nullptr)
            return false;
        const jmethodID parse { env.GetStaticMethodID(uri_type, "parse", "(Ljava/lang/String;)Landroid/net/Uri;") };
        if (jni_failed(env) || parse == nullptr)
            return false;
        const jobject uri { env.CallStaticObjectMethod(uri_type, parse, text) };
        if (jni_failed(env) || uri == nullptr)
            return false;
        const jclass intent_type { env.GetObjectClass(intent) };
        if (jni_failed(env) || intent_type == nullptr)
            return false;
        const jmethodID set_data { env.GetMethodID(intent_type, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;") };
        if (jni_failed(env) || set_data == nullptr)
            return false;
        const jobject changed { env.CallObjectMethod(intent, set_data, uri) };
        return jni_failed(env) == false && changed != nullptr;
    }
}

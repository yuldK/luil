#include "android/android_locale.h"

#include "android/java_vm.h"

namespace luil::android {
    std::optional<std::chrono::weekday> weekday_from_calendar_first_day(const jint value) noexcept
    {
        if (value < 1 || value > 7)
            return std::nullopt;
        // `weekday`의 생성자는 0을 일요일로 읽는다.
        return std::chrono::weekday { static_cast<unsigned>(value - 1) };
    }

    // 실패 검사는 `jni_failed`를 nullptr 검사보다 앞에 둔다. 앞 호출의 예외를 거두지 않으면 다음 JNI 호출이 깨진다.
    std::optional<std::chrono::weekday> read_first_weekday(JNIEnv& env)
    {
        const jni_local_frame frame { env };
        if (jni_failed(env) || frame.pushed() == false)
            return std::nullopt;
        const jclass type { env.FindClass("java/util/Calendar") };
        if (jni_failed(env) || type == nullptr)
            return std::nullopt;
        const jmethodID get_instance { env.GetStaticMethodID(type, "getInstance", "()Ljava/util/Calendar;") };
        if (jni_failed(env) || get_instance == nullptr)
            return std::nullopt;
        const jobject calendar { env.CallStaticObjectMethod(type, get_instance) };
        if (jni_failed(env) || calendar == nullptr)
            return std::nullopt;
        const jmethodID get_first_day { env.GetMethodID(type, "getFirstDayOfWeek", "()I") };
        if (jni_failed(env) || get_first_day == nullptr)
            return std::nullopt;
        const jint first_day { env.CallIntMethod(calendar, get_first_day) };
        if (jni_failed(env))
            return std::nullopt;
        return weekday_from_calendar_first_day(first_day);
    }
}

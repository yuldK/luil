#pragma once

#include <jni.h>

#include <chrono>
#include <optional>

namespace luil::android {
    // `java.util.Calendar.getFirstDayOfWeek()`의 값을 요일로 바꾼다. 1이 일요일, 7이 토요일이다. 그 밖의 값은 빈 값이다.
    [[nodiscard]] std::optional<std::chrono::weekday> weekday_from_calendar_first_day(jint value) noexcept;

    // 기기 지역 설정의 주 첫 요일이다 (`Calendar.getInstance().getFirstDayOfWeek()`). 읽지 못하면 빈 값이고 예외는 남지 않는다.
    //  - `platform_services_android.cpp`는 core test에 들지 않아 JNI 흐름을 이 파일에 따로 둔다. 가짜 JNIEnv로 친다 (android_locale_tests.cpp).
    [[nodiscard]] std::optional<std::chrono::weekday> read_first_weekday(JNIEnv& env);
}

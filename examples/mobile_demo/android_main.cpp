// luil mobile demo의 Android 진입점이다.
//
// GameActivity가 네이티브 라이브러리를 열고 별도 thread에서 android_main을 부른다. 그 thread가 luil의
// UI thread가 되고, run_application이 Activity가 끝날 때까지 돈다.

#include "mobile_demo/app.h"

#include "luil/android/android_app.h"

namespace {
    // 화면 크기와 배율을 앱에 알린다. 그 밖의 훅은 기본값이다.
    class mobile_delegate final : public luil::app_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
        {
            return mobile_demo::make_metrics_message(width, height, scale);
        }
    };
} // namespace

extern "C" void android_main(android_app* app)
{
    mobile_demo::mobile_driver driver {};
    mobile_demo::mobile_policy policy {};
    mobile_delegate delegate {};

    luil::android::application_environment environment {};
    environment.driver = &driver;
    environment.policy = &policy;
    environment.delegate = &delegate;
    // 렌더러는 Vulkan이 먼저다. 시스템 속성으로 바꿔 띄울 수 있다 (apply_debug_properties).
    luil::android::application_config config {};
    luil::android::apply_debug_properties(config);
    static_cast<void>(luil::android::run_application(app, config, environment));
}

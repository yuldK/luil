// luil widgets의 Android 진입점이다. 앱 골격과 섹션 파일은 Windows와 같은 파일이다.
//
// GameActivity가 네이티브 라이브러리를 열고 별도 thread에서 android_main을 부른다.
// 그 thread가 luil의 UI thread가 되고, run_application이 Activity가 끝날 때까지 돈다.
// 입력은 아직 옮기지 않았으므로(docs/android-port-plan.md 5단계) 화면은 보기만 한다.

#include "widgets/app.h"

#include "luil/android/android_app.h"

extern "C" void android_main(android_app* app)
{
    widgets::widgets_driver driver {};
    widgets::widgets_policy policy {};
    // Windows용 delegate지만 플랫폼 중립 부분(app_delegate)만 Android host가 부른다.
    widgets::widgets_delegate delegate {};

    luil::android::application_environment environment {};
    environment.driver = &driver;
    environment.policy = &policy;
    environment.delegate = &delegate;
    // 렌더러는 Vulkan이 먼저다. 시스템 속성으로 바꿔 띄울 수 있다 (apply_debug_properties).
    luil::android::application_config config {};
    luil::android::apply_debug_properties(config);
    static_cast<void>(luil::android::run_application(app, config, environment));
}

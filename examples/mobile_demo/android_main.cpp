// luil mobile demo의 Android 진입점이다.
//
// GameActivity가 네이티브 라이브러리를 열고 별도 thread에서 android_main을 부른다. 그 thread가 luil의
// UI thread가 되고, run_application이 Activity가 끝날 때까지 돈다.

#include "mobile_demo/app.h"
#include "mobile_demo/device_page.h"

#include "luil/android/android_app.h"

namespace {
    // 화면 크기·배율·폭 등급과 로컬 네트워크 권한을 앱에 알린다. 그 밖의 훅은 기본값이다.
    class mobile_delegate final : public luil::app_delegate
    {
    public:
        // 시작할 때 권한의 지금 상태를 알린다 (기기 페이지가 보인다).
        void on_started(luil::app_host& host) override
        {
            host.post_app_message(luil::app_message { mobile_demo::local_network_intent { host.permission(luil::app_permission::local_network) } });
        }

        // 기기 페이지의 "묻기"다. 묻지 않았으면(필요 없음·이미 받음) 지금 상태를 그대로 알린다.
        void execute_app_ui_command(luil::app_host& host, const luil::app_ui_command& command) override
        {
            if (command.command != mobile_demo::command_request_local_network)
                return;
            if (host.request_permission(luil::app_permission::local_network) == false)
                host.post_app_message(luil::app_message { mobile_demo::local_network_intent { host.permission(luil::app_permission::local_network) } });
        }

        [[nodiscard]] luil::app_message make_permission_message(const luil::app_permission permission, const luil::permission_state state) override
        {
            if (permission != luil::app_permission::local_network)
                return {};
            return luil::app_message { mobile_demo::local_network_intent { state, true } };
        }

        [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics) override
        {
            return mobile_demo::make_metrics_message(metrics);
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

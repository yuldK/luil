// luil mobile demo의 Android 진입점이다.
//
// GameActivity가 네이티브 라이브러리를 열고 별도 thread에서 android_main을 부른다. 그 thread가 luil의
// UI thread가 되고, run_application이 Activity가 끝날 때까지 돈다.

#include "mobile_demo/app.h"
#include "mobile_demo/device_page.h"

#include "luil/android/android_app.h"

namespace {
    // 화면 크기·배율·폭 등급과 로컬 네트워크 권한을 앱에 알리고, SMB client를 세운다. 그 밖의 훅은 기본값이다.
    class mobile_delegate final : public luil::app_delegate
    {
    public:
        // 시작할 때 권한의 지금 상태를 알리고, SMB가 있는 구성이면 client를 세워 logic에 넘긴다. 답은 client의 배달
        // thread에서 앱 메시지가 된다. client는 logic이 `stop_workers`에서 멈추고, delegate가 host보다 오래 살며 쥔다.
        void on_started(luil::app_host& host) override
        {
            host.post_app_message(luil::app_message { mobile_demo::local_network_intent { host.permission(luil::app_permission::local_network) } });
            luil::net::smb_client_config config {};
            config.deliver = [&host](luil::net::smb_response response) { host.post_app_message(luil::app_message { mobile_demo::smb_result_intent { std::move(response) } }); };
            std::u8string error {};
            smb_ = luil::net::smb_client::create(std::move(config), error);
            if (smb_ != nullptr)
                host.post_app_message(luil::app_message { mobile_demo::smb_client_intent { smb_.get() } });
        }

        // 기기 페이지의 "묻기"다. 묻지 않았으면(필요 없음·이미 받음) 지금 상태를 그대로 알린다.
        void execute_app_ui_command(luil::app_host& host, const luil::app_ui_command& command) override
        {
            if (command.command != mobile_demo::command_request_local_network)
                return;
            if (host.request_permission(luil::app_permission::local_network) == false)
                host.post_app_message(luil::app_message { mobile_demo::local_network_intent { host.permission(luil::app_permission::local_network) } });
        }

        // 앱이 뒤로 가면 SMB의 쉬는 연결을 닫는다. 돌아와서 망이 바뀌었어도 끊긴 연결을 다시 쓰지 않는다.
        [[nodiscard]] luil::app_message make_lifecycle_message(const luil::app_lifecycle lifecycle) override
        {
            if (lifecycle != luil::app_lifecycle::background)
                return {};
            return luil::app_message { mobile_demo::smb_close_idle_intent {} };
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

    private:
        std::unique_ptr<luil::net::smb_client> smb_ {};
    };
}

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

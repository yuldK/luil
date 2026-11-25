// luil hello의 iOS 진입점이다. 앱 쪽 절반(hello_app)은 Windows·Android·macOS와 같은 파일이다.
//
// main thread가 luil의 UI thread다. run_application은 UIApplicationMain을 불러 돌아오지 않는다. 그래서 driver와
// delegate는 프로세스가 끝날 때까지 사는 정적 객체다.

#include "hello/hello_app.h"

#include "luil/ios/ios_app.h"

int main(const int argc, char** const argv)
{
    static hello::hello_driver driver {};
    // Windows용 delegate지만 플랫폼 중립 부분(app_delegate)만 iOS host가 부른다.
    static hello::hello_delegate delegate {};

    luil::ios::application_environment environment {};
    environment.driver = &driver;
    environment.delegate = &delegate;
    // 렌더러는 Metal이 먼저다. `xcrun simctl launch <기기> <번들 id> --renderer=cpu`처럼 인자로 바꿔 띄울 수 있다.
    luil::ios::application_config config {};
    luil::ios::apply_debug_arguments(config, argc, argv);
    return luil::ios::run_application(argc, argv, config, environment);
}

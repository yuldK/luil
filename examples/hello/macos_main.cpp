// luil hello의 macOS 진입점이다. 앱 쪽 절반(hello_app)은 Windows·Android와 같은 파일이다.
//
// main thread가 luil의 UI thread다. run_application이 창이 닫혀 종료가 끝날 때까지 돈다.

#include "hello/hello_app.h"

#include "luil/macos/macos_app.h"

int main(const int argc, const char* const* const argv)
{
    luil::macos::application_config config {};
    config.title = u8"hello luil";
    config.initial_width = 800;
    config.initial_height = 500;
    for (const std::u8string& argument : luil::macos::command_line_arguments(argc, argv))
        if (argument == u8"--smoke-test")
            config.smoke_test = true;

    hello::hello_driver driver {};
    // Windows용 delegate지만 플랫폼 중립 부분(app_delegate)만 macOS host가 부른다.
    hello::hello_delegate delegate {};
    luil::macos::application_environment environment {};
    environment.driver = &driver;
    environment.delegate = &delegate;
    return luil::macos::run_application(config, environment);
}

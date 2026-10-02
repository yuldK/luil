#include "android/java_vm.h"

#include <catch2/catch_session.hpp>

#include <jni.h>

#include <string>
#include <vector>

// JVM 안에서 도는 test 라이브러리의 진입점이다 (tests/android/JvmTestMain.java).
//
// 프로세스의 JavaVM을 앱 host와 같은 자리에 적고 Catch2를 돌린다. 이 thread는 Java의 main
// thread라 이미 JVM에 붙어 있다.
extern "C" JNIEXPORT jint JNICALL Java_luil_testing_JvmTestMain_run(JNIEnv* const env, jclass, const jobjectArray arguments)
{
    JavaVM* vm { nullptr };
    if (env->GetJavaVM(&vm) != JNI_OK)
        return 2;
    luil::android::set_java_vm(vm);

    std::vector<std::string> owned { "luil_net_tests" };
    const jsize count { env->GetArrayLength(arguments) };
    for (jsize index { 0 }; index < count; ++index)
    {
        const auto argument { static_cast<jstring>(env->GetObjectArrayElement(arguments, index)) };
        const char* const text { env->GetStringUTFChars(argument, nullptr) };
        owned.emplace_back(text == nullptr ? "" : text);
        if (text != nullptr)
            env->ReleaseStringUTFChars(argument, text);
        env->DeleteLocalRef(argument);
    }

    std::vector<char*> argv {};
    argv.reserve(owned.size());
    for (std::string& argument : owned)
        argv.push_back(argument.data());
    return Catch::Session().run(static_cast<int>(argv.size()), argv.data());
}

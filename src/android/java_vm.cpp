#include "android/java_vm.h"

#include <atomic>
#include <mutex>

namespace luil::android {
    namespace {
        std::atomic<JavaVM*> process_vm { nullptr };
        std::mutex files_lock {};
        std::string files_path {};
    } // namespace

    void set_files_directory(const char* const path)
    {
        const std::lock_guard lock { files_lock };
        files_path = path != nullptr ? path : "";
    }

    std::string files_directory()
    {
        const std::lock_guard lock { files_lock };
        return files_path;
    }

    void set_java_vm(JavaVM* const vm) noexcept
    {
        process_vm.store(vm, std::memory_order_release);
    }

    JavaVM* java_vm() noexcept
    {
        return process_vm.load(std::memory_order_acquire);
    }

    jni_thread_scope::jni_thread_scope(const char* const thread_name) noexcept
    {
        JavaVM* const vm { java_vm() };
        if (vm == nullptr)
            return;

        void* env { nullptr };
        const jint status { vm->GetEnv(&env, JNI_VERSION_1_6) };
        if (status == JNI_OK)
        {
            env_ = static_cast<JNIEnv*>(env);
            return;
        }
        if (status != JNI_EDETACHED)
            return;

        JavaVMAttachArgs arguments {};
        arguments.version = JNI_VERSION_1_6;
        arguments.name = thread_name;
        arguments.group = nullptr;
        JNIEnv* attached { nullptr };
        if (vm->AttachCurrentThread(&attached, &arguments) != JNI_OK)
            return;
        env_ = attached;
        attached_ = true;
    }

    jni_thread_scope::~jni_thread_scope()
    {
        if (attached_)
            static_cast<void>(java_vm()->DetachCurrentThread());
    }

    JNIEnv* jni_thread_scope::env() const noexcept
    {
        return env_;
    }
} // namespace luil::android

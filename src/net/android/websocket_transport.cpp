#include "net/websocket_transport.h"

#include "android/java_vm.h"

#include <mutex>

namespace luil::net {
    namespace {
        class android_websocket final : public websocket_transport
        {
        public:
            ~android_websocket() override
            {
                shutdown();
                android::jni_thread_scope scope {};
                if (auto* env { scope.env() }; env != nullptr)
                {
                    if (object_ != nullptr)
                        env->DeleteGlobalRef(object_);
                    if (class_ != nullptr)
                        env->DeleteGlobalRef(class_);
                }
            }

            bool open(const websocket_request& request, const std::size_t limit, http_error& error) override
            {
                android::jni_thread_scope scope { "luil-websocket-read" };
                auto* env { scope.env() };
                if (env == nullptr)
                    return fail(error);
                android::jni_local_frame frame { *env, 32 };
                if (frame.pushed() == false)
                    return fail(error);
                {
                    const std::scoped_lock lock { mutex_ };
                    if (stopped_)
                        return fail(error);
                    const auto context { android::application_context() };
                    if (context == nullptr)
                        return fail(error);
                    const auto context_class { env->GetObjectClass(context) };
                    const auto loader_method { env->GetMethodID(context_class, "getClassLoader", "()Ljava/lang/ClassLoader;") };
                    const auto loader { env->CallObjectMethod(context, loader_method) };
                    const auto loader_class { env->FindClass("java/lang/ClassLoader") };
                    const auto load { env->GetMethodID(loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") };
                    const auto name { env->NewStringUTF("io.github.yuldk.luil.WebSocketTransport") };
                    const auto local { env->CallObjectMethod(loader, load, name) };
                    if (android::jni_failed(*env) || local == nullptr)
                        return fail(error);
                    class_ = static_cast<jclass>(env->NewGlobalRef(local));
                    const auto constructor { env->GetMethodID(class_, "<init>", "()V") };
                    open_ = env->GetMethodID(class_, "open", "(Ljava/lang/String;II)V");
                    receive_ = env->GetMethodID(class_, "receive", "()[B");
                    send_ = env->GetMethodID(class_, "send", "([BZ)V");
                    close_ = env->GetMethodID(class_, "close", "(I[B)V");
                    shutdown_ = env->GetMethodID(class_, "shutdown", "()V");
                    if (android::jni_failed(*env) || constructor == nullptr || open_ == nullptr || receive_ == nullptr || send_ == nullptr || close_ == nullptr || shutdown_ == nullptr)
                        return fail(error);
                    const auto object { env->NewObject(class_, constructor) };
                    if (android::jni_failed(*env) || object == nullptr)
                        return fail(error);
                    object_ = env->NewGlobalRef(object);
                    if (object_ == nullptr)
                        return fail(error);
                }
                // URL은 URI 규칙대로 ASCII-escape한 값이다. Modified UTF-8의 보충 평면 차이를 피한다.
                for (const auto character : request.url)
                    if (character >= 128)
                        return fail(error);
                const auto url { env->NewStringUTF(reinterpret_cast<const char*>(request.url.c_str())) };
                env->CallVoidMethod(object_, open_, url, static_cast<jint>(request.connect_timeout.count()), static_cast<jint>(limit));
                if (android::jni_failed(*env))
                    return fail(error);
                return true;
            }

            bool receive(websocket_packet& packet, const std::size_t limit, http_error& error) override
            {
                android::jni_thread_scope scope {};
                auto* env { scope.env() };
                if (env == nullptr)
                    return fail(error);
                android::jni_local_frame frame { *env };
                if (frame.pushed() == false)
                    return fail(error);
                const auto data { static_cast<jbyteArray>(env->CallObjectMethod(object_, receive_)) };
                if (android::jni_failed(*env) || data == nullptr)
                    return fail(error);
                const auto length { env->GetArrayLength(data) };
                if (length < 1 || static_cast<std::size_t>(length - 1) > limit)
                    return fail(error);
                jbyte kind {};
                env->GetByteArrayRegion(data, 0, 1, &kind);
                packet.bytes.resize(static_cast<std::size_t>(length - 1));
                env->GetByteArrayRegion(data, 1, length - 1, reinterpret_cast<jbyte*>(packet.bytes.data()));
                if (android::jni_failed(*env))
                    return fail(error);
                packet.text = kind == 1;
                packet.closed = kind == 8;
                if (packet.closed)
                {
                    packet.code = packet.bytes.size() < 2 ? 1005 : static_cast<std::uint16_t>((static_cast<unsigned>(packet.bytes[0]) << 8) | packet.bytes[1]);
                    if (packet.bytes.size() >= 2)
                        packet.bytes.erase(packet.bytes.begin(), packet.bytes.begin() + 2);
                }
                return true;
            }

            bool send(const std::span<const std::uint8_t> bytes, const bool text, http_error& error) override
            {
                return write(bytes, text ? 1 : 2, error);
            }

            bool close(const std::uint16_t code, const std::u8string_view reason, http_error& error) override
            {
                return write({ reinterpret_cast<const std::uint8_t*>(reason.data()), reason.size() }, code, error);
            }

            void shutdown() noexcept override
            {
                const std::scoped_lock lock { mutex_ };
                stopped_ = true;
                android::jni_thread_scope scope {};
                if (auto* env { scope.env() }; env != nullptr && object_ != nullptr && shutdown_ != nullptr)
                {
                    env->CallVoidMethod(object_, shutdown_);
                    (void)android::jni_failed(*env);
                }
            }

        private:
            bool write(const std::span<const std::uint8_t> bytes, const int kind, http_error& error)
            {
                android::jni_thread_scope scope {};
                auto* env { scope.env() };
                if (env == nullptr)
                    return fail(error);
                android::jni_local_frame frame { *env };
                if (frame.pushed() == false)
                    return fail(error);
                const auto data { env->NewByteArray(static_cast<jsize>(bytes.size())) };
                if (data == nullptr || android::jni_failed(*env))
                    return fail(error);
                env->SetByteArrayRegion(data, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
                if (android::jni_failed(*env))
                    return fail(error);
                if (kind <= 2)
                    env->CallVoidMethod(object_, send_, data, static_cast<jboolean>(kind == 1));
                else
                    env->CallVoidMethod(object_, close_, static_cast<jint>(kind), data);
                return android::jni_failed(*env) == false || fail(error);
            }
            static bool fail(http_error& error)
            {
                error = { .kind = http_error_kind::system_error, .message = u8"Android WebSocket operation failed. Include the luil Java transport in the APK." };
                return false;
            }
            std::mutex mutex_ {};
            bool stopped_ { false };
            jobject object_ { nullptr };
            jclass class_ { nullptr };
            jmethodID open_ { nullptr };
            jmethodID receive_ { nullptr };
            jmethodID send_ { nullptr };
            jmethodID close_ { nullptr };
            jmethodID shutdown_ { nullptr };
        };
    }

    std::unique_ptr<websocket_transport> make_websocket_transport()
    {
        return std::make_unique<android_websocket>();
    }
}

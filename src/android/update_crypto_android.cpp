#include "net/update_crypto.h"

#include "android/errno_text.h"
#include "android/java_vm.h"
#include "host/update_manifest.h"

#include <jni.h>

#include <cerrno>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

// 자동 업데이트의 Android 암호다 (docs/auto-update-design.md). Java 0줄이다.
// 서명 확인은 JNI로 framework의 `KeyFactory`·`X509EncodedKeySpec`·`Signature`를 부른다 (비밀 저장소가 Keystore를 부르는 것과 같은 길).
//  - Android는 받기가 없어 SHA-256을 쓰지 않는다. 해시는 서지 않고 그 까닭을 답한다.
namespace luil::net {
    namespace {
        using android::jni_failed;
        using android::jni_local_frame;

        [[nodiscard]] jbyteArray to_java(JNIEnv& env, const std::span<const std::uint8_t> bytes)
        {
            const jbyteArray array { env.NewByteArray(static_cast<jsize>(bytes.size())) };
            if (jni_failed(env) || array == nullptr)
                return nullptr;
            if (bytes.empty() == false)
                env.SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
            return jni_failed(env) ? nullptr : array;
        }

        // 공개 키를 들인다. 들이지 못하면 null이고 `error`에 단계를 적는다 (`InvalidKeySpecException`은 여기서 거둔다).
        [[nodiscard]] jobject import_key(JNIEnv& env, const std::span<const std::uint8_t> spki, std::u8string& error)
        {
            const jclass factory_class { env.FindClass("java/security/KeyFactory") };
            if (jni_failed(env) || factory_class == nullptr)
            {
                error = u8"java.security.KeyFactory is missing.";
                return nullptr;
            }
            const jmethodID get_instance { env.GetStaticMethodID(factory_class, "getInstance", "(Ljava/lang/String;)Ljava/security/KeyFactory;") };
            const jmethodID generate { jni_failed(env) ? nullptr : env.GetMethodID(factory_class, "generatePublic", "(Ljava/security/spec/KeySpec;)Ljava/security/PublicKey;") };
            const jclass spec_class { jni_failed(env) ? nullptr : env.FindClass("java/security/spec/X509EncodedKeySpec") };
            const jmethodID spec_init { jni_failed(env) || spec_class == nullptr ? nullptr : env.GetMethodID(spec_class, "<init>", "([B)V") };
            if (jni_failed(env) || get_instance == nullptr || generate == nullptr || spec_init == nullptr)
            {
                error = u8"A java.security key method is missing.";
                return nullptr;
            }
            const jstring algorithm { env.NewStringUTF("EC") };
            if (jni_failed(env) || algorithm == nullptr)
            {
                error = u8"Failed to name the EC key factory.";
                return nullptr;
            }
            const jobject factory { env.CallStaticObjectMethod(factory_class, get_instance, algorithm) };
            if (jni_failed(env) || factory == nullptr)
            {
                error = u8"Failed to open the EC key factory.";
                return nullptr;
            }
            const jbyteArray encoded { to_java(env, spki) };
            if (encoded == nullptr)
            {
                error = u8"Failed to copy the public key.";
                return nullptr;
            }
            const jobject spec { env.NewObject(spec_class, spec_init, encoded) };
            if (jni_failed(env) || spec == nullptr)
            {
                error = u8"Failed to describe the public key.";
                return nullptr;
            }
            const jobject key { env.CallObjectMethod(factory, generate, spec) };
            if (jni_failed(env) || key == nullptr)
            {
                error = u8"Failed to import the P-256 public key (InvalidKeySpecException).";
                return nullptr;
            }
            return key;
        }
    }

    struct update_sha256::state
    {
        std::u8string error { u8"SHA-256 is not used on Android (there is no download)." };
    };

    update_sha256::update_sha256()
        : state_ { std::make_unique<state>() }
    {
    }

    update_sha256::update_sha256(update_sha256&&) noexcept = default;
    update_sha256& update_sha256::operator=(update_sha256&&) noexcept = default;
    update_sha256::~update_sha256() = default;

    bool update_sha256::valid() const noexcept
    {
        return false;
    }

    const std::u8string& update_sha256::error() const noexcept
    {
        return state_->error;
    }

    bool update_sha256::update(const std::span<const std::uint8_t> bytes)
    {
        static_cast<void>(bytes);
        return false;
    }

    std::optional<std::array<std::uint8_t, 32>> update_sha256::finish()
    {
        return std::nullopt;
    }

    update_signature_result verify_update_signature(const std::span<const std::uint8_t> spki, const std::array<std::uint8_t, 64>& signature, const std::span<const std::uint8_t> message)
    {
        if (update_signature_scalars_nonzero(signature) == false)
            return { update_signature_check::mismatch, u8"The signature has a zero scalar." };
        // Java의 `SHA256withECDSA`는 DER을 받는다. core가 r||s에서 최소 꼴 DER을 짓는다.
        const std::vector<std::uint8_t> der { update_signature_to_der(signature) };

        const android::jni_thread_scope scope { "luil-updater" };
        JNIEnv* const env { scope.env() };
        if (env == nullptr)
            return { update_signature_check::failed, u8"The signature check needs the app's Java VM." };
        const jni_local_frame frame { *env, 32 };
        if (frame.pushed() == false)
            return { update_signature_check::failed, u8"Failed to reserve JNI local references." };

        std::u8string error {};
        const jobject key { import_key(*env, spki, error) };
        if (key == nullptr)
            return { update_signature_check::failed, std::move(error) };

        const jclass signature_class { env->FindClass("java/security/Signature") };
        if (jni_failed(*env) || signature_class == nullptr)
            return { update_signature_check::failed, u8"java.security.Signature is missing." };
        const jmethodID get_instance { env->GetStaticMethodID(signature_class, "getInstance", "(Ljava/lang/String;)Ljava/security/Signature;") };
        const jmethodID init_verify { jni_failed(*env) ? nullptr : env->GetMethodID(signature_class, "initVerify", "(Ljava/security/PublicKey;)V") };
        const jmethodID update { jni_failed(*env) ? nullptr : env->GetMethodID(signature_class, "update", "([B)V") };
        const jmethodID verify { jni_failed(*env) ? nullptr : env->GetMethodID(signature_class, "verify", "([B)Z") };
        if (jni_failed(*env) || get_instance == nullptr || init_verify == nullptr || update == nullptr || verify == nullptr)
            return { update_signature_check::failed, u8"A java.security.Signature method is missing." };

        const jstring algorithm { env->NewStringUTF("SHA256withECDSA") };
        if (jni_failed(*env) || algorithm == nullptr)
            return { update_signature_check::failed, u8"Failed to name the signature algorithm." };
        const jobject verifier { env->CallStaticObjectMethod(signature_class, get_instance, algorithm) };
        if (jni_failed(*env) || verifier == nullptr)
            return { update_signature_check::failed, u8"Failed to open SHA256withECDSA." };
        env->CallVoidMethod(verifier, init_verify, key);
        if (jni_failed(*env))
            return { update_signature_check::failed, u8"Failed to start the signature check (InvalidKeyException)." };

        const jbyteArray message_array { to_java(*env, message) };
        const jbyteArray signature_array { message_array == nullptr ? nullptr : to_java(*env, der) };
        if (message_array == nullptr || signature_array == nullptr)
            return { update_signature_check::failed, u8"Failed to copy the signed bytes." };
        env->CallVoidMethod(verifier, update, message_array);
        if (jni_failed(*env))
            return { update_signature_check::failed, u8"Failed to feed the signed bytes." };
        // 맞지 않으면 예외 없이 거짓이다. 꼴이 깨진 서명이나 틀린 상태는 `SignatureException`이고 그것도 맞지 않는 것으로 본다.
        const jboolean matched { env->CallBooleanMethod(verifier, verify, signature_array) };
        if (jni_failed(*env) || matched == JNI_FALSE)
            return { update_signature_check::mismatch, u8"The signature does not match." };
        return { update_signature_check::valid, {} };
    }

    bool flush_update_file(const std::filesystem::path& path, std::u8string& error)
    {
        const int descriptor { ::open(path.c_str(), O_WRONLY | O_CLOEXEC) };
        if (descriptor < 0)
        {
            error = android::make_errno_error(u8"Failed to open the downloaded file");
            return false;
        }
        const bool synced { ::fsync(descriptor) == 0 };
        if (synced == false)
            error = android::make_errno_error(u8"Failed to flush the downloaded file");
        static_cast<void>(::close(descriptor));
        return synced;
    }
}

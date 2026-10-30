#include "luil/app/secret_store.h"

#include "android/java_vm.h"

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// 비밀 저장소의 Android 쪽이다 — Keystore의 AES-GCM 키로 암호화한 앱 내부 파일 (secret-store-design.md).
// Java 0줄이다. Keystore·Cipher는 JNI로 프레임워크 클래스를 부른다 (HTTP client와 같은 길).
namespace luil {
    namespace {
        constexpr std::uint8_t file_version { 1 };
        constexpr std::size_t iv_bytes { 12 };
        constexpr jint tag_bits { 128 };
        constexpr jint encrypt_mode { 1 };
        constexpr jint decrypt_mode { 2 };
        // KeyProperties.PURPOSE_ENCRYPT | PURPOSE_DECRYPT
        constexpr jint key_purposes { 1 | 2 };

        // Keystore와 파일을 한 번에 하나씩 다룬다 — 같은 항목을 두 thread가 함께 쓰면 임시 파일이 겹친다.
        std::mutex store_lock {};

        [[nodiscard]] secret_store_result success() noexcept
        {
            return { secret_store_status::ok, {} };
        }

        [[nodiscard]] secret_store_result failure(std::u8string message)
        {
            return { secret_store_status::failed, std::move(message) };
        }

        // 바이트를 16진 글로 옮긴다. 파일 이름과 키 별칭이 ASCII라 JNI의 modified UTF-8과 파일 시스템을 가리지 않는다.
        [[nodiscard]] std::string hex(const std::u8string_view text)
        {
            constexpr char digits[] { "0123456789abcdef" };
            std::string result {};
            for (const char8_t byte : text)
            {
                result.push_back(digits[(static_cast<unsigned>(byte) >> 4u) & 0x0Fu]);
                result.push_back(digits[static_cast<unsigned>(byte) & 0x0Fu]);
            }
            return result;
        }

        [[nodiscard]] std::filesystem::path secret_path(const std::filesystem::path& directory, const std::u8string_view service, const std::u8string_view name)
        {
            return directory / (hex(service) + "-" + hex(name));
        }

        // 이 호출 동안의 지역 참조를 모아 끝에서 한꺼번에 지운다.
        class local_frame
        {
        public:
            explicit local_frame(JNIEnv* const env) noexcept
                : env_ { env }
                , pushed_ { env->PushLocalFrame(64) == 0 }
            {
            }
            local_frame(const local_frame&) = delete;
            local_frame(local_frame&&) = delete;
            local_frame& operator=(const local_frame&) = delete;
            local_frame& operator=(local_frame&&) = delete;

            ~local_frame()
            {
                if (pushed_)
                    static_cast<void>(env_->PopLocalFrame(nullptr));
            }

        private:
            JNIEnv* env_ { nullptr };
            bool pushed_ { false };
        };

        // Keystore 한 번의 일이다. 단계마다 Java 예외를 보고, 걸리면 그 이유를 글로 들고 멈춘다.
        class keystore_session
        {
        public:
            explicit keystore_session(JNIEnv* const env) noexcept
                : env_ { env }
            {
            }

            [[nodiscard]] const std::u8string& error() const noexcept
            {
                return error_;
            }

            // 별칭의 키다. 없고 `create`면 AES-256-GCM 키를 짓는다. 없고 짓지 않으면 nullptr이고 오류가 없다.
            [[nodiscard]] jobject key(const std::string& alias, const bool create)
            {
                const jclass store_class { find("java/security/KeyStore") };
                const jmethodID get_instance { static_method(store_class, "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;") };
                if (get_instance == nullptr)
                    return nullptr;
                const jobject store { env_->CallStaticObjectMethod(store_class, get_instance, env_->NewStringUTF("AndroidKeyStore")) };
                if (check(u8"Failed to open the Android key store") == false)
                    return nullptr;
                const jmethodID load { method(store_class, "load", "(Ljava/security/KeyStore$LoadStoreParameter;)V") };
                if (load == nullptr)
                    return nullptr;
                env_->CallVoidMethod(store, load, nullptr);
                if (check(u8"Failed to load the Android key store") == false)
                    return nullptr;
                const jmethodID get_key { method(store_class, "getKey", "(Ljava/lang/String;[C)Ljava/security/Key;") };
                if (get_key == nullptr)
                    return nullptr;
                const jobject existing { env_->CallObjectMethod(store, get_key, env_->NewStringUTF(alias.c_str()), nullptr) };
                if (check(u8"Failed to read the secret key") == false)
                    return nullptr;
                if (existing != nullptr || create == false)
                    return existing;
                return generate(alias);
            }

            [[nodiscard]] std::optional<std::vector<std::uint8_t>> encrypt(const jobject key, const std::span<const std::uint8_t> plain, std::vector<std::uint8_t>& iv)
            {
                const jclass cipher_class { find("javax/crypto/Cipher") };
                const jobject cipher { make_cipher(cipher_class) };
                const jmethodID init { method(cipher_class, "init", "(ILjava/security/Key;)V") };
                if (cipher == nullptr || init == nullptr)
                    return std::nullopt;
                env_->CallVoidMethod(cipher, init, encrypt_mode, key);
                if (check(u8"Failed to start the encryption") == false)
                    return std::nullopt;
                const jmethodID get_iv { method(cipher_class, "getIV", "()[B") };
                if (get_iv == nullptr)
                    return std::nullopt;
                const auto iv_array { static_cast<jbyteArray>(env_->CallObjectMethod(cipher, get_iv)) };
                if (check(u8"Failed to read the encryption IV") == false || iv_array == nullptr)
                    return std::nullopt;
                iv = from_java(iv_array);
                return final_bytes(cipher_class, cipher, plain, u8"Failed to encrypt the secret");
            }

            [[nodiscard]] std::optional<std::vector<std::uint8_t>> decrypt(const jobject key, const std::span<const std::uint8_t> iv, const std::span<const std::uint8_t> sealed)
            {
                const jclass cipher_class { find("javax/crypto/Cipher") };
                const jclass gcm_class { find("javax/crypto/spec/GCMParameterSpec") };
                const jobject cipher { make_cipher(cipher_class) };
                const jmethodID gcm_init { method(gcm_class, "<init>", "(I[B)V") };
                const jmethodID init { method(cipher_class, "init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V") };
                if (cipher == nullptr || gcm_init == nullptr || init == nullptr)
                    return std::nullopt;
                const jobject parameters { env_->NewObject(gcm_class, gcm_init, tag_bits, to_java(iv)) };
                if (check(u8"Failed to prepare the decryption") == false)
                    return std::nullopt;
                env_->CallVoidMethod(cipher, init, decrypt_mode, key, parameters);
                if (check(u8"Failed to start the decryption") == false)
                    return std::nullopt;
                return final_bytes(cipher_class, cipher, sealed, u8"Failed to decrypt the secret");
            }

        private:
            // 걸린 Java 예외를 글로 옮기고 지운다. 예외의 `toString`이 클래스 이름과 이유를 함께 든다.
            [[nodiscard]] bool check(const std::u8string_view what)
            {
                if (env_->ExceptionCheck() == JNI_FALSE)
                    return true;
                const jthrowable thrown { env_->ExceptionOccurred() };
                env_->ExceptionClear();
                error_ = what;
                const jclass object_class { env_->FindClass("java/lang/Object") };
                const jmethodID to_string { object_class != nullptr ? env_->GetMethodID(object_class, "toString", "()Ljava/lang/String;") : nullptr };
                const auto text { to_string != nullptr && thrown != nullptr ? static_cast<jstring>(env_->CallObjectMethod(thrown, to_string)) : nullptr };
                if (env_->ExceptionCheck() == JNI_FALSE && text != nullptr)
                {
                    const char* const characters { env_->GetStringUTFChars(text, nullptr) };
                    if (characters != nullptr)
                    {
                        error_ += u8" (";
                        for (const char* at { characters }; *at != '\0'; ++at)
                            error_.push_back(static_cast<char8_t>(*at));
                        error_ += u8")";
                        env_->ReleaseStringUTFChars(text, characters);
                    }
                }
                env_->ExceptionClear();
                error_ += u8".";
                return false;
            }

            [[nodiscard]] jclass find(const char* const name)
            {
                if (error_.empty() == false)
                    return nullptr;
                const jclass found { env_->FindClass(name) };
                return check(u8"A key store class is missing") ? found : nullptr;
            }

            [[nodiscard]] jmethodID method(const jclass owner, const char* const name, const char* const signature)
            {
                if (owner == nullptr || error_.empty() == false)
                    return nullptr;
                const jmethodID found { env_->GetMethodID(owner, name, signature) };
                return check(u8"A key store method is missing") ? found : nullptr;
            }

            [[nodiscard]] jmethodID static_method(const jclass owner, const char* const name, const char* const signature)
            {
                if (owner == nullptr || error_.empty() == false)
                    return nullptr;
                const jmethodID found { env_->GetStaticMethodID(owner, name, signature) };
                return check(u8"A key store method is missing") ? found : nullptr;
            }

            [[nodiscard]] jbyteArray to_java(const std::span<const std::uint8_t> bytes)
            {
                const jbyteArray array { env_->NewByteArray(static_cast<jsize>(bytes.size())) };
                if (array != nullptr && bytes.empty() == false)
                    env_->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
                return array;
            }

            [[nodiscard]] std::vector<std::uint8_t> from_java(const jbyteArray array)
            {
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(env_->GetArrayLength(array)));
                if (bytes.empty() == false)
                    env_->GetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<jbyte*>(bytes.data()));
                return bytes;
            }

            [[nodiscard]] jobjectArray single_string(const char* const value)
            {
                const jclass string_class { find("java/lang/String") };
                if (string_class == nullptr)
                    return nullptr;
                const jobjectArray array { env_->NewObjectArray(1, string_class, env_->NewStringUTF(value)) };
                return check(u8"Failed to build a key parameter") ? array : nullptr;
            }

            // AES-256-GCM, 패딩 없음의 Keystore 키를 짓는다. 키는 Keystore 밖으로 나오지 않는다.
            [[nodiscard]] jobject generate(const std::string& alias)
            {
                const jclass builder_class { find("android/security/keystore/KeyGenParameterSpec$Builder") };
                const jmethodID builder_init { method(builder_class, "<init>", "(Ljava/lang/String;I)V") };
                const jmethodID block_modes { method(builder_class, "setBlockModes", "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;") };
                const jmethodID paddings { method(builder_class, "setEncryptionPaddings", "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;") };
                const jmethodID key_size { method(builder_class, "setKeySize", "(I)Landroid/security/keystore/KeyGenParameterSpec$Builder;") };
                const jmethodID build { method(builder_class, "build", "()Landroid/security/keystore/KeyGenParameterSpec;") };
                const jclass generator_class { find("javax/crypto/KeyGenerator") };
                const jmethodID get_instance { static_method(generator_class, "getInstance", "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;") };
                const jmethodID init { method(generator_class, "init", "(Ljava/security/spec/AlgorithmParameterSpec;)V") };
                const jmethodID generate_key { method(generator_class, "generateKey", "()Ljavax/crypto/SecretKey;") };
                if (builder_init == nullptr || block_modes == nullptr || paddings == nullptr || key_size == nullptr || build == nullptr || get_instance == nullptr || init == nullptr
                    || generate_key == nullptr)
                    return nullptr;

                const jobject builder { env_->NewObject(builder_class, builder_init, env_->NewStringUTF(alias.c_str()), key_purposes) };
                if (check(u8"Failed to describe the secret key") == false)
                    return nullptr;
                const jobjectArray gcm { single_string("GCM") };
                const jobjectArray no_padding { single_string("NoPadding") };
                if (gcm == nullptr || no_padding == nullptr)
                    return nullptr;
                // 예외가 걸린 채로 다음 JNI 호출을 하면 안 된다 — 단계마다 본다.
                static_cast<void>(env_->CallObjectMethod(builder, block_modes, gcm));
                if (check(u8"Failed to set the key block mode") == false)
                    return nullptr;
                static_cast<void>(env_->CallObjectMethod(builder, paddings, no_padding));
                if (check(u8"Failed to set the key padding") == false)
                    return nullptr;
                static_cast<void>(env_->CallObjectMethod(builder, key_size, jint { 256 }));
                if (check(u8"Failed to set the key size") == false)
                    return nullptr;
                const jobject spec { env_->CallObjectMethod(builder, build) };
                if (check(u8"Failed to describe the secret key") == false)
                    return nullptr;
                const jobject generator { env_->CallStaticObjectMethod(generator_class, get_instance, env_->NewStringUTF("AES"), env_->NewStringUTF("AndroidKeyStore")) };
                if (check(u8"Failed to open the key generator") == false)
                    return nullptr;
                env_->CallVoidMethod(generator, init, spec);
                const jobject key { check(u8"Failed to prepare the key generator") ? env_->CallObjectMethod(generator, generate_key) : nullptr };
                return check(u8"Failed to generate the secret key") ? key : nullptr;
            }

            [[nodiscard]] jobject make_cipher(const jclass cipher_class)
            {
                const jmethodID get_instance { static_method(cipher_class, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;") };
                if (get_instance == nullptr)
                    return nullptr;
                const jobject cipher { env_->CallStaticObjectMethod(cipher_class, get_instance, env_->NewStringUTF("AES/GCM/NoPadding")) };
                return check(u8"Failed to open the AES-GCM cipher") ? cipher : nullptr;
            }

            [[nodiscard]] std::optional<std::vector<std::uint8_t>> final_bytes(
                const jclass cipher_class, const jobject cipher, const std::span<const std::uint8_t> input, const std::u8string_view what)
            {
                const jmethodID do_final { method(cipher_class, "doFinal", "([B)[B") };
                if (do_final == nullptr)
                    return std::nullopt;
                const auto output { static_cast<jbyteArray>(env_->CallObjectMethod(cipher, do_final, to_java(input))) };
                if (check(what) == false || output == nullptr)
                    return std::nullopt;
                return from_java(output);
            }

            JNIEnv* env_ { nullptr };
            std::u8string error_ {};
        };

        // 이 thread를 JVM에 붙이고 비밀 폴더를 고른다. 어느 하나라도 없으면 이 프로세스에는 저장소가 없다.
        struct store_context
        {
            android::jni_thread_scope scope { "luil-secret-store" };
            std::filesystem::path directory {};
            std::optional<secret_store_result> unavailable {};
        };

        void open_context(store_context& context)
        {
            const std::string files { android::files_directory() };
            if (context.scope.env() == nullptr || files.empty())
            {
                context.unavailable = secret_store_result { secret_store_status::unsupported, u8"The secret store needs the app's Java VM and files directory." };
                return;
            }
            context.directory = std::filesystem::path { files } / "luil-secrets";
        }

        [[nodiscard]] bool valid_names(const std::u8string_view service, const std::u8string_view name) noexcept
        {
            return service.empty() == false && name.empty() == false;
        }

        [[nodiscard]] std::string alias_of(const std::u8string_view service)
        {
            return "luil-secret:" + hex(service);
        }
    }

    secret_store_result store_secret(const std::u8string_view service, const std::u8string_view name, const std::span<const std::uint8_t> secret)
    {
        if (valid_names(service, name) == false)
            return failure(u8"The secret needs a service and a name.");
        if (secret.size() > secret_store_maximum_bytes)
            return failure(u8"The secret is larger than the 2560 byte store limit.");
        const std::lock_guard lock { store_lock };
        store_context context {};
        open_context(context);
        if (context.unavailable.has_value())
            return *context.unavailable;
        JNIEnv* const env { context.scope.env() };
        const local_frame frame { env };
        keystore_session session { env };
        const jobject key { session.key(alias_of(service), true) };
        if (key == nullptr)
            return failure(session.error().empty() ? std::u8string { u8"Failed to create the secret key." } : session.error());
        std::vector<std::uint8_t> iv {};
        const std::optional<std::vector<std::uint8_t>> sealed { session.encrypt(key, secret, iv) };
        if (sealed.has_value() == false)
            return failure(session.error());
        if (iv.size() != iv_bytes)
            return failure(u8"The cipher returned an unexpected IV length.");

        // 임시 파일에 다 쓴 뒤 이름을 바꾼다 — 쓰다 끊긴 반쪽 파일이 그 자리에 남지 않는다.
        std::error_code failed {};
        std::filesystem::create_directories(context.directory, failed);
        if (failed)
            return failure(u8"Failed to create the secret folder.");
        const std::filesystem::path target { secret_path(context.directory, service, name) };
        std::filesystem::path temporary { target };
        temporary += ".tmp";
        {
            std::ofstream file { temporary, std::ios::binary | std::ios::trunc };
            file.put(static_cast<char>(file_version));
            file.write(reinterpret_cast<const char*>(iv.data()), static_cast<std::streamsize>(iv.size()));
            file.write(reinterpret_cast<const char*>(sealed->data()), static_cast<std::streamsize>(sealed->size()));
            if (file.good() == false)
                return failure(u8"Failed to write the secret file.");
        }
        std::filesystem::rename(temporary, target, failed);
        if (failed)
            return failure(u8"Failed to replace the secret file.");
        return success();
    }

    secret_store_result load_secret(const std::u8string_view service, const std::u8string_view name, std::vector<std::uint8_t>& secret)
    {
        secret.clear();
        if (valid_names(service, name) == false)
            return failure(u8"The secret needs a service and a name.");
        const std::lock_guard lock { store_lock };
        store_context context {};
        open_context(context);
        if (context.unavailable.has_value())
            return *context.unavailable;
        const std::filesystem::path target { secret_path(context.directory, service, name) };
        std::ifstream file { target, std::ios::binary };
        if (file.is_open() == false)
            return { secret_store_status::not_found, {} };
        const std::vector<std::uint8_t> contents { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
        file.close();
        // 판 번호·IV·태그(16바이트)가 다 있어야 한다.
        if (contents.size() < 1u + iv_bytes + 16u || contents.front() != file_version)
            return failure(u8"The secret file is not in a known format.");

        JNIEnv* const env { context.scope.env() };
        const local_frame frame { env };
        keystore_session session { env };
        const jobject key { session.key(alias_of(service), false) };
        if (key == nullptr)
        {
            if (session.error().empty() == false)
                return failure(session.error());
            // 키가 없다 — 앱을 다시 깔았거나 다른 기기의 백업에서 온 파일이다. 풀 수 없으니 지우고 없다고 답한다.
            std::error_code ignored {};
            std::filesystem::remove(target, ignored);
            return { secret_store_status::not_found, {} };
        }
        const std::span<const std::uint8_t> bytes { contents };
        const std::optional<std::vector<std::uint8_t>> opened { session.decrypt(key, bytes.subspan(1, iv_bytes), bytes.subspan(1 + iv_bytes)) };
        if (opened.has_value() == false)
            return failure(session.error());
        secret = *opened;
        return success();
    }

    secret_store_result erase_secret(const std::u8string_view service, const std::u8string_view name)
    {
        if (valid_names(service, name) == false)
            return failure(u8"The secret needs a service and a name.");
        const std::lock_guard lock { store_lock };
        store_context context {};
        open_context(context);
        if (context.unavailable.has_value())
            return *context.unavailable;
        std::error_code failed {};
        const bool removed { std::filesystem::remove(secret_path(context.directory, service, name), failed) };
        if (failed)
            return failure(u8"Failed to erase the secret file.");
        if (removed == false)
            return { secret_store_status::not_found, {} };
        return success();
    }
}

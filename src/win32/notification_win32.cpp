#include "win32/notification_win32.h"

#include "luil/app/platform_services.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_encode.h"
#include "win32/app_identity.h"
#include "win32/executable_path.h"
#include "win32/toast_payload.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"
#include "win32/window_messages.h"

#include <NotificationActivationCallback.h>
#include <objbase.h>
#include <roapi.h>
#include <shobjidl_core.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/implements.h>
#include <wrl/wrappers/corewrappers.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        namespace notifications = ABI::Windows::UI::Notifications;
        namespace xml = ABI::Windows::Data::Xml::Dom;
        namespace foundation = ABI::Windows::Foundation;
        using Microsoft::WRL::ComPtr;
        using Microsoft::WRL::Wrappers::HString;
        using Microsoft::WRL::Wrappers::HStringReference;
        using win32::make_hresult_error;

        using activated_handler = foundation::ITypedEventHandler<notifications::ToastNotification*, IInspectable*>;
        using dismissed_handler = foundation::ITypedEventHandler<notifications::ToastNotification*, notifications::ToastDismissedEventArgs*>;
        using failed_handler = foundation::ITypedEventHandler<notifications::ToastNotification*, notifications::ToastFailedEventArgs*>;

        // 올린 토스트 하나다. 거둘 때까지 쥐어 처리기가 살아 있게 한다.
        struct held_toast
        {
            ComPtr<notifications::IToastNotification> toast {};
            std::optional<EventRegistrationToken> activated {};
            std::optional<EventRegistrationToken> dismissed {};
            std::optional<EventRegistrationToken> failed {};
            // 이 토스트가 가리키는 임시 그림이다. 없으면 비었다.
            std::filesystem::path image {};
        };

        // 프로세스에 하나인 알림 상태다 (결정 10). 공개 함수가 어느 thread에서나 들기 때문에 잠금 하나로 지킨다.
        struct center_state
        {
            std::mutex mutex {};
            // 선 식별이다. 비면 알림이 `unsupported`다.
            std::wstring aumid {};
            // 식별을 세우다 실패한 글이다. 있으면 `notification_status`는 `unsupported`, `show_notification`은 이 글로 `failed`다.
            std::u8string identity_error {};
            // 식별이 활성화기를 켰다 (`app_identity::activator`). 레지스트리에 `CustomActivator`가 있다.
            bool activator { false };
            // 주 창이 이 프로세스에 클래스 개체를 걸었다. 걸었으면 눌림은 COM으로 오고 `Activated`는 잇지 않는다.
            std::optional<DWORD> activator_cookie {};
            GUID clsid {};
            // 알림 개체를 얻었다 (`ui_platform_capabilities::notifications`).
            bool available { false };
            std::map<std::u8string, held_toast> toasts {};
            std::uint64_t image_number { 0 };
            std::filesystem::path image_directory {};
        };

        // 프로세스가 끝날 때 부수지 않는다. 정적 소멸은 COM이 내려간 뒤일 수 있어 그때 WinRT 개체를 놓으면 안전하지 않다.
        [[nodiscard]] center_state& center()
        {
            static center_state* const state { new center_state {} };
            return *state;
        }

        [[nodiscard]] notification_queue& event_queue()
        {
            static notification_queue* const queue { new notification_queue {} };
            return *queue;
        }

        std::atomic<HWND> event_window { nullptr };
        // 이 실행이 알림 클릭으로 떴다. 활성화기의 첫 눌림이 `launched`로 가져간다.
        std::atomic<bool> launch_pending { false };

        // COM이 알림 클릭을 넘기는 개체다 (결정 4의 길 b). 주 창 thread의 STA에서 건 클래스 개체가 짓는다.
        //  - `invokedArgs`의 `luil:<id>`만 읽어 대기열에 넣고 곧 돌아온다. 앱 상태는 UI thread가 대기열을 비우며 만진다.
        class notification_activator final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, INotificationActivationCallback>
        {
        public:
            IFACEMETHODIMP Activate(LPCWSTR, const LPCWSTR invoked_arguments, const NOTIFICATION_USER_INPUT_DATA*, ULONG) override
            {
                if (invoked_arguments == nullptr)
                    return S_OK;
                try
                {
                    const std::optional<std::u8string> argument { win32::utf16_to_utf8(invoked_arguments).value };
                    const std::optional<std::u8string> id { argument.has_value() ? win32::toast_id_from_launch_argument(*argument) : std::nullopt };
                    if (id.has_value() == false)
                        return S_OK;
                    notification_event event { notification_event_kind::activated, *id };
                    event.launched = launch_pending.exchange(false);
                    win32::post_notification_event(std::move(event));
                }
                catch (...)
                {
                }
                return S_OK;
            }
        };

        // 활성화기 개체를 짓는 클래스 개체다. COM이 클릭마다 `CreateInstance`를 부른다.
        class activator_factory final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IClassFactory>
        {
        public:
            IFACEMETHODIMP CreateInstance(IUnknown* const outer, REFIID interface_id, void** const object) override
            {
                if (object == nullptr)
                    return E_POINTER;
                *object = nullptr;
                if (outer != nullptr)
                    return CLASS_E_NOAGGREGATION;
                const ComPtr<notification_activator> activator { Microsoft::WRL::Make<notification_activator>() };
                if (activator == nullptr)
                    return E_OUTOFMEMORY;
                return activator.CopyTo(interface_id, object);
            }

            IFACEMETHODIMP LockServer(BOOL) override
            {
                return S_OK;
            }
        };

        // 대소문자를 가리지 않는 ASCII 비교다.
        [[nodiscard]] bool same_flag(const std::u8string_view argument, const std::wstring_view flag) noexcept
        {
            if (argument.size() != flag.size())
                return false;
            for (std::size_t index = 0; index < argument.size(); ++index)
            {
                const char8_t left { argument[index] };
                const wchar_t right { flag[index] };
                if (right > 0x7F || std::tolower(static_cast<unsigned char>(left)) != std::tolower(static_cast<unsigned char>(right)))
                    return false;
            }
            return true;
        }

        [[nodiscard]] platform_result succeeded() noexcept
        {
            return { platform_status::ok, {}, std::nullopt };
        }

        [[nodiscard]] platform_result failure(std::u8string error)
        {
            return { platform_status::failed, std::move(error), std::nullopt };
        }

        [[nodiscard]] platform_result hresult_failure(const std::u8string_view message, const HRESULT result)
        {
            return failure(make_hresult_error(message, result));
        }

        [[nodiscard]] HStringReference reference(const std::wstring& text) noexcept
        {
            return HStringReference { text.c_str(), static_cast<unsigned int>(text.size()) };
        }

        // 부른 thread에 COM apartment가 없으면 프로세스에 MTA 사용 표를 한 번 올린다. 그러면 그 thread는 암묵 MTA에 든다.
        //  - UI thread의 STA(`com_sta_scope`)와 앱의 worker가 각자 apartment를 가졌으면 그대로 쓴다.
        [[nodiscard]] bool increment_mta_usage() noexcept
        {
            CO_MTA_USAGE_COOKIE cookie {};
            return SUCCEEDED(CoIncrementMTAUsage(&cookie));
        }

        void ensure_apartment() noexcept
        {
            APTTYPE type {};
            APTTYPEQUALIFIER qualifier {};
            if (CoGetApartmentType(&type, &qualifier) != CO_E_NOTINITIALIZED)
                return;
            // 표는 내리지 않는다. 프로세스가 끝날 때까지 쥔 토스트 개체가 MTA를 가리킨다.
            static const bool incremented { increment_mta_usage() };
            static_cast<void>(incremented);
        }

        [[nodiscard]] HRESULT toast_manager(ComPtr<notifications::IToastNotificationManagerStatics>& statics) noexcept
        {
            return RoGetActivationFactory(HStringReference { RuntimeClass_Windows_UI_Notifications_ToastNotificationManager }.Get(), IID_PPV_ARGS(&statics));
        }

        [[nodiscard]] HRESULT toast_history(ComPtr<notifications::IToastNotificationHistory>& history) noexcept
        {
            ComPtr<notifications::IToastNotificationManagerStatics> statics {};
            HRESULT result { toast_manager(statics) };
            if (FAILED(result))
                return result;
            ComPtr<notifications::IToastNotificationManagerStatics2> statics2 {};
            result = statics.As(&statics2);
            if (FAILED(result))
                return result;
            return statics2->get_History(&history);
        }

        [[nodiscard]] HRESULT toast_notifier(const std::wstring& aumid, ComPtr<notifications::IToastNotifier>& notifier) noexcept
        {
            ComPtr<notifications::IToastNotificationManagerStatics> statics {};
            const HRESULT result { toast_manager(statics) };
            if (FAILED(result))
                return result;
            return statics->CreateToastNotifierWithId(reference(aumid).Get(), &notifier);
        }

        // 알림 센터에 남은 이 앱의 토스트 tag(id)다. 읽지 못하면 빈 값이다.
        [[nodiscard]] std::optional<std::set<std::u8string>> history_tags(const std::wstring& aumid)
        {
            ComPtr<notifications::IToastNotificationHistory> history {};
            if (FAILED(toast_history(history)))
                return std::nullopt;
            ComPtr<notifications::IToastNotificationHistory2> history2 {};
            if (FAILED(history.As(&history2)))
                return std::nullopt;
            ComPtr<foundation::Collections::IVectorView<notifications::ToastNotification*>> list {};
            if (FAILED(history2->GetHistoryWithId(reference(aumid).Get(), &list)))
                return std::nullopt;
            unsigned int size { 0 };
            if (FAILED(list->get_Size(&size)))
                return std::nullopt;
            std::set<std::u8string> tags {};
            for (unsigned int index = 0; index < size; ++index)
            {
                ComPtr<notifications::IToastNotification> toast {};
                ComPtr<notifications::IToastNotification2> toast2 {};
                HString tag {};
                if (FAILED(list->GetAt(index, &toast)) || FAILED(toast.As(&toast2)) || FAILED(toast2->get_Tag(tag.GetAddressOf())))
                    continue;
                unsigned int length { 0 };
                const wchar_t* const raw { tag.GetRawBuffer(&length) };
                if (auto converted { win32::utf16_to_utf8(std::wstring_view { raw, length }) }; converted.value.has_value())
                    tags.insert(std::move(*converted.value));
            }
            return tags;
        }

        // 쥔 토스트의 처리기를 푼다. 잠금을 쥔 채 부른다.
        void release_handlers(held_toast& held) noexcept
        {
            if (held.toast == nullptr)
                return;
            if (held.activated.has_value())
                static_cast<void>(held.toast->remove_Activated(*held.activated));
            if (held.dismissed.has_value())
                static_cast<void>(held.toast->remove_Dismissed(*held.dismissed));
            if (held.failed.has_value())
                static_cast<void>(held.toast->remove_Failed(*held.failed));
            held.activated.reset();
            held.dismissed.reset();
            held.failed.reset();
        }

        void remove_file(const std::filesystem::path& path) noexcept
        {
            if (path.empty())
                return;
            std::error_code error {};
            static_cast<void>(std::filesystem::remove(path, error));
        }

        // 쥔 토스트 하나를 놓는다. 잠금을 쥔 채 부른다.
        void forget_toast(center_state& state, const std::u8string& id) noexcept
        {
            const auto found { state.toasts.find(id) };
            if (found == state.toasts.end())
                return;
            release_handlers(found->second);
            remove_file(found->second.image);
            state.toasts.erase(found);
        }

        // 임시 그림 폴더에서 알림 센터에 없는 id의 파일을 지운다. 이름은 `<id>-<번호>.png`다.
        void remove_orphan_images(const std::filesystem::path& directory, const std::set<std::u8string>& tags) noexcept
        {
            std::error_code error {};
            std::filesystem::directory_iterator iterator { directory, error };
            if (error)
                return;
            for (const std::filesystem::directory_entry& entry : iterator)
            {
                const std::u8string name { entry.path().stem().u8string() };
                const std::size_t dash { name.rfind(u8'-') };
                if (dash == std::u8string::npos || tags.contains(name.substr(0, dash)) == false)
                    remove_file(entry.path());
            }
        }

        // 그림을 긴 변 256 px로 줄여 PNG로 쓴다. 실패하면 빈 경로와 `error`다.
        [[nodiscard]] std::filesystem::path write_image(const ui_image& image, const std::filesystem::path& directory, const std::u8string& id, const std::uint64_t number, std::u8string& error)
        {
            image_decode_options options {};
            options.max_width = notification_image_limit;
            options.max_height = notification_image_limit;
            options.downscale = image_downscale::high_quality;
            image_decode_error decode_error {};
            const ui_image scaled { downscale_image(image, options, decode_error) };
            // 벡터 그림은 줄일 픽셀이 없어 자기 크기로 편다.
            const std::vector<std::uint8_t> bytes { encode_png(scaled.valid() ? scaled : image, error) };
            if (bytes.empty())
                return {};
            std::error_code directory_error {};
            std::filesystem::create_directories(directory, directory_error);
            const std::string digits { std::to_string(number) };
            std::filesystem::path path { directory / (id + u8"-" + std::u8string { digits.begin(), digits.end() } + u8".png") };
            const platform_result written { replace_file_atomically(path.u8string(), bytes) };
            if (written.ok() == false)
            {
                error = written.error;
                return {};
            }
            return path;
        }

        [[nodiscard]] HRESULT load_toast_xml(const std::u8string& text, ComPtr<xml::IXmlDocument>& document) noexcept
        {
            const std::optional<std::wstring> wide { win32::utf8_to_utf16(text).value };
            if (wide.has_value() == false)
                return E_INVALIDARG;
            ComPtr<IInspectable> instance {};
            HRESULT result { RoActivateInstance(HStringReference { RuntimeClass_Windows_Data_Xml_Dom_XmlDocument }.Get(), &instance) };
            if (FAILED(result))
                return result;
            ComPtr<xml::IXmlDocumentIO> io {};
            result = instance.As(&io);
            if (FAILED(result))
                return result;
            result = io->LoadXml(reference(*wide).Get());
            if (FAILED(result))
                return result;
            return instance.As(&document);
        }

        // thread 풀에서 불려도 되는 처리기다 (FtmBase). STA로 마샬되지 않아 UI thread를 기다리지 않는다.
        template<typename handler_type, typename function_type>
        [[nodiscard]] auto agile_handler(function_type&& function)
        {
            return Microsoft::WRL::Callback<Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, handler_type, Microsoft::WRL::FtmBase>>(std::forward<function_type>(function));
        }

        void post_event_noexcept(notification_event event) noexcept
        {
            try
            {
                win32::post_notification_event(std::move(event));
            }
            catch (...)
            {
            }
        }

        // 처리기를 단다. 처리기는 사건을 대기열에 넣고 신호만 게시한다. 앱 상태를 thread 풀에서 만지는 길은 없다.
        //  - 활성화기를 걸었으면 `Activated`는 잇지 않는다. 같은 눌림이 두 길로 오지 않게 한다.
        [[nodiscard]] HRESULT attach_handlers(held_toast& held, const std::u8string& id, const bool listen_activated)
        {
            const auto on_activated = [id](notifications::IToastNotification*, IInspectable*) -> HRESULT {
                post_event_noexcept({ notification_event_kind::activated, id });
                return S_OK;
            };
            const auto on_dismissed = [id](notifications::IToastNotification*, notifications::IToastDismissedEventArgs* const arguments) -> HRESULT {
                // 앱이 거둔 것(`ApplicationHidden`)과 알림 센터로 옮겨 간 것(`TimedOut`)은 사건이 없다.
                notifications::ToastDismissalReason reason { notifications::ToastDismissalReason_TimedOut };
                if (arguments != nullptr && SUCCEEDED(arguments->get_Reason(&reason)) && reason == notifications::ToastDismissalReason_UserCanceled)
                    post_event_noexcept({ notification_event_kind::dismissed, id });
                return S_OK;
            };
            const auto on_failed = [id](notifications::IToastNotification*, notifications::IToastFailedEventArgs* const arguments) -> HRESULT {
                HRESULT code { E_FAIL };
                if (arguments != nullptr)
                    static_cast<void>(arguments->get_ErrorCode(&code));
                notification_event event { notification_event_kind::failed, id };
                event.error = make_hresult_error(u8"Windows rejected the notification after it was shown", code);
                post_event_noexcept(std::move(event));
                return S_OK;
            };
            auto activated { agile_handler<activated_handler>(on_activated) };
            auto dismissed { agile_handler<dismissed_handler>(on_dismissed) };
            auto failed { agile_handler<failed_handler>(on_failed) };
            if (activated == nullptr || dismissed == nullptr || failed == nullptr)
                return E_OUTOFMEMORY;

            EventRegistrationToken token {};
            HRESULT result { S_OK };
            if (listen_activated)
            {
                result = held.toast->add_Activated(activated.Get(), &token);
                if (FAILED(result))
                    return result;
                held.activated = token;
            }
            result = held.toast->add_Dismissed(dismissed.Get(), &token);
            if (FAILED(result))
                return result;
            held.dismissed = token;
            result = held.toast->add_Failed(failed.Get(), &token);
            if (FAILED(result))
                return result;
            held.failed = token;
            return S_OK;
        }

        // 식별의 지금 값이다. 잠금 밖에서 OS를 부르려고 베낀다.
        struct identity_snapshot
        {
            std::wstring aumid {};
            std::u8string error {};
            bool activator_registered { false };
        };

        [[nodiscard]] identity_snapshot snapshot_identity()
        {
            center_state& state { center() };
            const std::lock_guard lock { state.mutex };
            return { state.aumid, state.identity_error, state.activator_cookie.has_value() };
        }

        [[nodiscard]] notification_availability availability_of(const identity_snapshot& identity)
        {
            if (identity.aumid.empty() || identity.error.empty() == false)
                return notification_availability::unsupported;
            ensure_apartment();
            ComPtr<notifications::IToastNotifier> notifier {};
            if (FAILED(toast_notifier(identity.aumid, notifier)))
                return notification_availability::unsupported;
            // 설정을 읽지 못하면 막혔는지 알 수 없어 올려 본다. 끈 설정은 `Show`가 조용히 받는다.
            notifications::NotificationSetting setting { notifications::NotificationSetting_Enabled };
            if (SUCCEEDED(notifier->get_Setting(&setting)) && setting != notifications::NotificationSetting_Enabled)
                return notification_availability::blocked;
            return notification_availability::available;
        }

        constexpr std::u8string_view missing_identity { u8"The app identity is not set; give luil::win32::window_config::identity to run_application_window." };
    }

    notification_availability notification_status()
    {
        return availability_of(snapshot_identity());
    }

    platform_result show_notification(const notification& value)
    {
        std::u8string error {};
        const std::optional<notification_text> text { prepare_notification_text(value, error) };
        if (text.has_value() == false)
            return failure(std::move(error));
        const identity_snapshot identity { snapshot_identity() };
        if (identity.aumid.empty())
            return { platform_status::unsupported, std::u8string { missing_identity }, std::nullopt };
        if (identity.error.empty() == false)
            return failure(identity.error);
        switch (availability_of(identity))
        {
        case notification_availability::available:
            break;
        case notification_availability::blocked:
        case notification_availability::permission_needed:
            return failure(u8"Notifications are turned off for this app in Windows settings or by policy.");
        case notification_availability::unsupported:
            return { platform_status::unsupported, u8"Windows did not give a toast notifier for the app identity.", std::nullopt };
        }

        // 그림은 잠금 밖에서 쓴다. 번호만 잠금 안에서 받는다.
        std::filesystem::path image {};
        if (value.image.valid())
        {
            std::filesystem::path directory {};
            std::uint64_t number { 0 };
            {
                center_state& state { center() };
                const std::lock_guard lock { state.mutex };
                directory = state.image_directory;
                number = ++state.image_number;
            }
            image = write_image(value.image, directory, value.id, number, error);
            if (image.empty())
                return failure(u8"Failed to write the notification image: " + error);
        }

        const std::u8string xml_text { win32::make_toast_xml({ value.id, text->title, text->body, image.u8string(), value.silent }) };
        ComPtr<xml::IXmlDocument> document {};
        HRESULT result { load_toast_xml(xml_text, document) };
        ComPtr<notifications::IToastNotificationFactory> factory {};
        if (SUCCEEDED(result))
            result = RoGetActivationFactory(HStringReference { RuntimeClass_Windows_UI_Notifications_ToastNotification }.Get(), IID_PPV_ARGS(&factory));
        held_toast held {};
        if (SUCCEEDED(result))
            result = factory->CreateToastNotification(document.Get(), &held.toast);
        ComPtr<notifications::IToastNotification2> tagged {};
        if (SUCCEEDED(result))
            result = held.toast.As(&tagged);
        const std::optional<std::wstring> tag { win32::utf8_to_utf16(value.id).value };
        if (SUCCEEDED(result))
            result = tag.has_value() ? tagged->put_Tag(reference(*tag).Get()) : E_INVALIDARG;
        if (SUCCEEDED(result))
            result = tagged->put_Group(HStringReference { win32::toast_group.data(), static_cast<unsigned int>(win32::toast_group.size()) }.Get());
        if (SUCCEEDED(result))
            result = attach_handlers(held, value.id, identity.activator_registered == false);
        ComPtr<notifications::IToastNotifier> notifier {};
        if (SUCCEEDED(result))
            result = toast_notifier(identity.aumid, notifier);
        if (SUCCEEDED(result))
            result = notifier->Show(held.toast.Get());
        if (FAILED(result))
        {
            release_handlers(held);
            remove_file(image);
            return hresult_failure(u8"Failed to show the notification", result);
        }

        // 같은 id의 옛 토스트는 OS가 바꿨다. 그 처리기와 그림을 놓고 새것을 쥔다.
        held.image = std::move(image);
        center_state& state { center() };
        const std::lock_guard lock { state.mutex };
        forget_toast(state, value.id);
        state.toasts.emplace(value.id, std::move(held));
        return succeeded();
    }

    platform_result close_notification(const std::u8string_view id)
    {
        if (notification_id_valid(id) == false)
            return failure(u8"The notification id must be 1 to 64 ASCII letters, digits, '.', '_' or '-'.");
        const identity_snapshot identity { snapshot_identity() };
        if (identity.aumid.empty())
            return { platform_status::unsupported, std::u8string { missing_identity }, std::nullopt };
        const std::u8string key { id };
        {
            // 처리기부터 푼다. 거두며 오는 `Dismissed`는 앱이 거둔 것이라 사건이 없지만 기다릴 까닭도 없다.
            center_state& state { center() };
            const std::lock_guard lock { state.mutex };
            forget_toast(state, key);
        }
        ensure_apartment();
        ComPtr<notifications::IToastNotificationHistory> history {};
        HRESULT result { toast_history(history) };
        const std::optional<std::wstring> tag { win32::utf8_to_utf16(id).value };
        if (SUCCEEDED(result))
        {
            const HStringReference group { win32::toast_group.data(), static_cast<unsigned int>(win32::toast_group.size()) };
            result = tag.has_value() ? history->RemoveGroupedTagWithId(reference(*tag).Get(), group.Get(), reference(identity.aumid).Get()) : E_INVALIDARG;
        }
        if (FAILED(result))
            return hresult_failure(u8"Failed to remove the notification", result);
        return succeeded();
    }

    platform_result close_all_notifications()
    {
        const identity_snapshot identity { snapshot_identity() };
        if (identity.aumid.empty())
            return { platform_status::unsupported, std::u8string { missing_identity }, std::nullopt };
        {
            center_state& state { center() };
            const std::lock_guard lock { state.mutex };
            while (state.toasts.empty() == false)
            {
                const std::u8string id { state.toasts.begin()->first };
                forget_toast(state, id);
            }
        }
        ensure_apartment();
        ComPtr<notifications::IToastNotificationHistory> history {};
        HRESULT result { toast_history(history) };
        if (SUCCEEDED(result))
            result = history->ClearWithId(reference(identity.aumid).Get());
        if (FAILED(result))
            return hresult_failure(u8"Failed to remove the notifications", result);
        // 앞 실행이 남긴 그림도 이제 가리키는 알림이 없다.
        std::filesystem::path directory {};
        {
            center_state& state { center() };
            const std::lock_guard lock { state.mutex };
            directory = state.image_directory;
        }
        if (directory.empty() == false)
            remove_orphan_images(directory, {});
        return succeeded();
    }

    bool notification_launch(const std::span<const std::u8string> arguments) noexcept
    {
        for (const std::u8string& argument : arguments)
        {
            if (same_flag(argument, win32::activator_flag) || same_flag(argument, L"-Embedding") || same_flag(argument, L"/Embedding"))
                return true;
        }
        return false;
    }
}

namespace luil::win32 {
    platform_result apply_app_identity(const app_identity& identity, const std::u8string_view fallback_display_name)
    {
        // 활성화기 명령 줄이 이 실행 파일을 가리킨다. 실행 파일을 옮기면 다음 실행이 고쳐 쓴다.
        const std::wstring executable { current_executable_path() };
        const std::optional<identity_registry> registry { make_identity_registry(identity, fallback_display_name, executable) };
        const std::optional<std::wstring> aumid { utf8_to_utf16(identity.id).value };
        if (registry.has_value() == false || aumid.has_value() == false)
            return failure(u8"The app identity id is empty, too long, or contains spaces, control characters or backslashes.");

        // 창보다 먼저 건다. 그래야 작업 표시줄 단추가 이 id로 묶인다.
        platform_result result { succeeded() };
        if (const HRESULT applied { SetCurrentProcessExplicitAppUserModelID(aumid->c_str()) }; FAILED(applied))
            result = hresult_failure(u8"Failed to set the process app user model id", applied);
        if (result.ok())
            result = write_identity_registry(*registry).result;

        // 알림 개체를 얻어 본다. 능력 플래그가 이 결과를 따른다.
        bool available { false };
        if (result.ok())
        {
            ensure_apartment();
            ComPtr<notifications::IToastNotifier> notifier {};
            const HRESULT created { toast_notifier(*aumid, notifier) };
            available = SUCCEEDED(created);
            if (available == false)
                result = hresult_failure(u8"Windows did not give a toast notifier for the app identity", created);
        }

        wchar_t temporary[MAX_PATH + 1] {};
        const DWORD length { GetTempPathW(MAX_PATH + 1, temporary) };
        std::filesystem::path directory {};
        if (length > 0 && length <= MAX_PATH)
            directory = std::filesystem::path { std::wstring { temporary, length } } / L"luil-notifications" / *aumid;
        {
            center_state& state { center() };
            const std::lock_guard lock { state.mutex };
            state.aumid = *aumid;
            state.identity_error = result.ok() ? std::u8string {} : result.error;
            state.activator = identity.activator;
            state.clsid = activator_clsid(identity.id).value_or(GUID {});
            state.available = available;
            state.image_directory = directory;
        }
        // 앞 실행의 그림 가운데 알림 센터에 남지 않은 것을 지운다. 남은 알림은 그림을 잃지 않는다.
        if (available && directory.empty() == false)
        {
            if (const std::optional<std::set<std::u8string>> tags { history_tags(*aumid) }; tags.has_value())
                remove_orphan_images(directory, *tags);
        }
        return result;
    }

    bool notifications_available() noexcept
    {
        center_state& state { center() };
        const std::lock_guard lock { state.mutex };
        return state.available;
    }

    void attach_notification_window(const HWND window) noexcept
    {
        event_window.store(window);
        if (window != nullptr && event_queue().empty() == false)
            PostMessageW(window, notification_event_message, 0, 0);
    }

    void set_notification_launch(const bool launched) noexcept
    {
        launch_pending.store(launched);
    }

    void register_notification_activator() noexcept
    {
        center_state& state { center() };
        const std::lock_guard lock { state.mutex };
        if (state.activator == false || state.available == false || state.activator_cookie.has_value())
            return;
        // UI thread STA에서 건다. COM이 `Activate`를 이 thread의 메시지 펌프로 보낸다. COM이 없는 thread면 실패하고
        // 떠 있는 동안의 눌림은 `Activated`로 받는다.
        try
        {
            const ComPtr<activator_factory> factory { Microsoft::WRL::Make<activator_factory>() };
            DWORD cookie { 0 };
            if (factory != nullptr && SUCCEEDED(CoRegisterClassObject(state.clsid, factory.Get(), CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie)))
                state.activator_cookie = cookie;
        }
        catch (...)
        {
        }
    }

    void detach_notification_window(const HWND window) noexcept
    {
        HWND expected { window };
        if (event_window.compare_exchange_strong(expected, nullptr) == false)
            return;
        bool clear { false };
        {
            center_state& state { center() };
            const std::lock_guard lock { state.mutex };
            // 끝나는 중의 클릭은 COM이 새 프로세스로 띄운다. 그 프로세스가 둘째면 앱의 인자 넘기기 길로 표식이 온다.
            if (state.activator_cookie.has_value())
                static_cast<void>(CoRevokeClassObject(*state.activator_cookie));
            state.activator_cookie.reset();
            for (auto& entry : state.toasts)
                release_handlers(entry.second);
            state.toasts.clear();
            // 활성화기가 없으면 꺼진 뒤의 클릭을 받을 곳이 없다. 알림 센터에 죽은 알림을 남기지 않는다 (결정 4 표의 길 a).
            clear = state.activator == false && state.available;
        }
        if (clear)
            static_cast<void>(close_all_notifications());
    }

    notification_queue& notification_events() noexcept
    {
        return event_queue();
    }

    std::optional<std::size_t> notification_history_count()
    {
        const identity_snapshot identity { snapshot_identity() };
        if (identity.aumid.empty())
            return std::nullopt;
        ensure_apartment();
        const std::optional<std::set<std::u8string>> tags { history_tags(identity.aumid) };
        if (tags.has_value() == false)
            return std::nullopt;
        return tags->size();
    }

    void post_notification_event(notification_event event)
    {
        event_queue().push(std::move(event));
        if (const HWND window { event_window.load() }; window != nullptr)
            PostMessageW(window, notification_event_message, 0, 0);
    }
}

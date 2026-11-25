#include "luil/app/notification.h"

#include "macos/macos_text.h"
#include "macos/notification_center.h"
#include "macos/notification_rules.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_encode.h"

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// OS 알림의 macOS 길이다 (os-notification-design.md의 "macOS"). UserNotifications(`UNUserNotificationCenter`)를 부른다.
//  - 알림 센터는 앱 번들에서만 선다. 번들 식별자가 없거나 `.app` 안이 아니면 알림 센터를 건드리지 않고 `unsupported`다.
//  - 알림 센터의 답은 다른 thread로 온다. 공개 함수는 그 답을 잠깐 기다려 돌려준다 (어느 thread에서나 부른다).
//  - 사용자의 눌림·닫음은 delegate가 받아 메인 thread에서 host의 받는 자리로 넘긴다 (notification_center.h).
//  - 이 파일의 전역 상태는 정적 소멸을 하지 않는다. 끝나는 중에 늦게 온 응답이 사라진 개체를 만지지 않게 한다.

// 알림 센터의 delegate다. 앱이 앞에 있을 때도 배너를 띄우고, 사용자의 응답을 사건으로 옮긴다.
@interface LuilNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

namespace {
    using luil::notification_availability;
    using luil::permission_state;
    using luil::platform_result;
    using luil::platform_status;
    namespace macos = luil::macos;

    // 알림 센터의 답을 기다리는 상한이다. 보통 몇 ms 안에 온다.
    constexpr std::int64_t answer_timeout_ns { 5 * static_cast<std::int64_t>(NSEC_PER_SEC) };
    // 붙이는 그림의 첨부 id다.
    NSString* const image_attachment { @"image" };

    [[nodiscard]] platform_result succeeded() noexcept
    {
        return { platform_status::ok, {}, std::nullopt };
    }

    [[nodiscard]] platform_result failure(std::u8string error)
    {
        return { platform_status::failed, std::move(error), std::nullopt };
    }

    [[nodiscard]] platform_result not_bundled()
    {
        return { platform_status::unsupported, u8"OS notifications on macOS need an app bundle (.app) with a bundle identifier (CFBundleIdentifier).", std::nullopt };
    }

    [[nodiscard]] std::u8string error_text(const std::u8string_view what, NSError* const error)
    {
        std::u8string text { what };
        if (error != nil)
            text += u8" (" + macos::from_ns_string(error.localizedDescription) + u8")";
        return text + u8".";
    }

    // 메인 thread에서만 만진다.
    [[nodiscard]] macos::notification_relay& relay()
    {
        static macos::notification_relay* const value { new macos::notification_relay {} };
        return *value;
    }

    [[nodiscard]] macos::notification_launch_tracker& launch_tracker()
    {
        static macos::notification_launch_tracker* const value { new macos::notification_launch_tracker {} };
        return *value;
    }

    // 알림 센터를 쓸 수 있는 앱 번들인가. 맨 실행 파일에서 `currentNotificationCenter`는 예외를 던진다.
    [[nodiscard]] bool bundled()
    {
        NSBundle* const bundle { [NSBundle mainBundle] };
        return bundle.bundleIdentifier != nil && [bundle.bundleURL.pathExtension isEqualToString:@"app"];
    }

    // 사용자의 응답 하나를 메인 thread에서 사건으로 낸다.
    void receive(const std::u8string& identifier, const macos::notification_response_action action)
    {
        const bool launched { action != macos::notification_response_action::dismiss && launch_tracker().take() };
        if (std::optional<luil::notification_event> event { macos::notification_event_from_response(identifier, action, launched) }; event.has_value())
            relay().deliver(std::move(*event));
    }

    // 알림 센터다. 처음 부를 때 delegate와 범주를 걸고 앱 시작 마침을 지켜본다. `bundled()`일 때만 부른다.
    [[nodiscard]] UNUserNotificationCenter* notification_center()
    {
        static dispatch_once_t once {};
        static LuilNotificationDelegate* delegate { nil };
        UNUserNotificationCenter* const center { [UNUserNotificationCenter currentNotificationCenter] };
        dispatch_once(&once, ^{
          delegate = [LuilNotificationDelegate new];
          center.delegate = delegate;
          // 사용자가 닫은 것도 응답으로 오게 한다 (`UNNotificationDismissActionIdentifier`). 단추는 달지 않는다.
          UNNotificationCategory* const category { [UNNotificationCategory categoryWithIdentifier:macos::make_ns_string(macos::notification_category)
                                                                                           actions:@[]
                                                                                 intentIdentifiers:@[]
                                                                                           options:UNNotificationCategoryOptionCustomDismissAction] };
          [center setNotificationCategories:[NSSet setWithObject:category]];
#if TARGET_OS_IPHONE
          // iOS도 시작을 마치기 전에 온 눌림이 꺼진 앱을 띄운 눌림이다. 로컬 알림은 시작 선택에 표지를 싣지 않으므로(UserNotifications는
          // delegate로만 알린다) 마친 뒤의 첫 눌림을 띄운 눌림으로 세지 않는다. host는 시작(`didFinishLaunching`) 안에서 이 함수를 부른다.
          [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidFinishLaunchingNotification
                                                            object:nil
                                                             queue:nil
                                                        usingBlock:^(NSNotification*) {
                                                          dispatch_async(dispatch_get_main_queue(), ^{
                                                            launch_tracker().finish_launching(false);
                                                          });
                                                        }];
          if ([UIApplication sharedApplication] != nil && [UIApplication sharedApplication].applicationState == UIApplicationStateActive)
              dispatch_async(dispatch_get_main_queue(), ^{
                launch_tracker().finish_launching(false);
              });
#else
          // 시작을 마치기 전에 온 눌림은 꺼진 앱을 띄운 눌림이다. 마칠 때 AppKit이 알림으로 떴다고 알리면 그 뒤 첫 눌림도 그렇다.
          [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidFinishLaunchingNotification
                                                            object:nil
                                                             queue:nil
                                                        usingBlock:^(NSNotification* notification) {
                                                          const bool by_notification { notification.userInfo[NSApplicationLaunchUserNotificationKey] != nil };
                                                          dispatch_async(dispatch_get_main_queue(), ^{
                                                            launch_tracker().finish_launching(by_notification);
                                                          });
                                                        }];
          if ([NSRunningApplication currentApplication].finishedLaunching)
              dispatch_async(dispatch_get_main_queue(), ^{
                launch_tracker().finish_launching(false);
              });
#endif
        });
        return center;
    }

    [[nodiscard]] macos::notification_authorization authorization_of(const UNAuthorizationStatus status) noexcept
    {
        switch (status)
        {
        case UNAuthorizationStatusNotDetermined:
            return macos::notification_authorization::not_determined;
        case UNAuthorizationStatusDenied:
            return macos::notification_authorization::denied;
        case UNAuthorizationStatusAuthorized:
            return macos::notification_authorization::authorized;
        case UNAuthorizationStatusProvisional:
            return macos::notification_authorization::provisional;
        default:
            return macos::notification_authorization::other;
        }
    }

    // 알림 설정을 읽는다. 답이 오지 않으면 빈 값이다.
    [[nodiscard]] std::optional<macos::notification_settings> read_settings(UNUserNotificationCenter* const center)
    {
        const dispatch_semaphore_t done { dispatch_semaphore_create(0) };
        __block macos::notification_settings settings {};
        [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* const value) {
          settings.authorization = authorization_of(value.authorizationStatus);
          // 이 판이 모르는 칸(`NotSupported`)은 막지 않는다.
          settings.alerts = value.alertSetting != UNNotificationSettingDisabled;
          settings.center = value.notificationCenterSetting != UNNotificationSettingDisabled;
          dispatch_semaphore_signal(done);
        }];
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, answer_timeout_ns)) != 0)
            return std::nullopt;
        return settings;
    }

    // 그림을 긴 변 256 px PNG로 임시 폴더에 쓴다. 알림 센터가 받으면 그 파일을 제 저장소로 옮긴다. 실패하면 nil이다.
    [[nodiscard]] NSURL* write_image(NSString* const identifier, const luil::ui_image& image)
    {
        luil::image_decode_options options {};
        options.max_width = luil::notification_image_limit;
        options.max_height = luil::notification_image_limit;
        options.downscale = luil::image_downscale::high_quality;
        luil::image_decode_error decode_error {};
        const luil::ui_image scaled { luil::downscale_image(image, options, decode_error) };
        std::u8string error {};
        const std::vector<std::uint8_t> bytes { luil::encode_png(scaled.valid() ? scaled : image, error) };
        if (bytes.empty())
            return nil;
        NSString* const directory { [NSTemporaryDirectory() stringByAppendingPathComponent:@"luil-notifications"] };
        if ([[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil] == NO)
            return nil;
        NSString* const name { [NSString stringWithFormat:@"%@-%@.png", identifier, [NSUUID UUID].UUIDString] };
        NSString* const path { [directory stringByAppendingPathComponent:name] };
        NSData* const data { [NSData dataWithBytes:bytes.data() length:bytes.size()] };
        if ([data writeToFile:path atomically:YES] == NO)
            return nil;
        return [NSURL fileURLWithPath:path];
    }

    void remove_file(NSURL* const url)
    {
        if (url != nil)
            [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    }
}

@implementation LuilNotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    static_cast<void>(center);
    // 앱이 앞에 있어도 Windows·Android처럼 배너를 띄우고 알림 센터에 남긴다. 소리는 올릴 때 단 것만 낸다.
    UNNotificationPresentationOptions options { UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList };
    if (notification.request.content.sound != nil)
        options |= UNNotificationPresentationOptionSound;
    completionHandler(options);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    static_cast<void>(center);
    const std::u8string identifier { macos::from_ns_string(response.notification.request.identifier) };
    NSString* const action { response.actionIdentifier };
    macos::notification_response_action kind { macos::notification_response_action::other };
    if ([action isEqualToString:UNNotificationDefaultActionIdentifier])
        kind = macos::notification_response_action::open;
    else if ([action isEqualToString:UNNotificationDismissActionIdentifier])
        kind = macos::notification_response_action::dismiss;
    // 이 메서드가 어느 thread로 오는지 문서가 정하지 않는다. 앱 쪽 상태는 메인 thread에서만 만진다.
    dispatch_async(dispatch_get_main_queue(), ^{
      receive(identifier, kind);
    });
    completionHandler();
}

@end

namespace luil {
    notification_availability notification_status()
    {
        if (bundled() == false)
            return notification_availability::unsupported;
        const std::optional<macos::notification_settings> settings { read_settings(notification_center()) };
        // 설정을 읽지 못하면 막혔는지 알 수 없어 올려 본다 (Windows의 결정 9와 같다). 올리기가 알림 센터의 거절을 돌려준다.
        return settings.has_value() ? macos::notification_availability_of(*settings) : notification_availability::available;
    }

    platform_result show_notification(const notification& value)
    {
        std::u8string error {};
        const std::optional<macos::notification_request_content> content { macos::make_notification_request(value, error) };
        if (content.has_value() == false)
            return failure(std::move(error));
        if (bundled() == false)
            return not_bundled();
        UNUserNotificationCenter* const center { notification_center() };
        const std::optional<macos::notification_settings> settings { read_settings(center) };
        switch (settings.has_value() ? macos::notification_availability_of(*settings) : notification_availability::available)
        {
        case notification_availability::available:
            break;
        case notification_availability::permission_needed:
            return failure(u8"The app needs the notification permission before it can show notifications (ask for app_permission::notifications).");
        case notification_availability::blocked:
            return failure(u8"Notifications are turned off for this app in System Settings.");
        case notification_availability::unsupported:
            return not_bundled();
        }

        NSString* const identifier { macos::make_ns_string(content->identifier) };
        UNMutableNotificationContent* const body { [UNMutableNotificationContent new] };
        body.title = macos::make_ns_string(content->title);
        body.body = macos::make_ns_string(content->body);
        body.categoryIdentifier = macos::make_ns_string(content->category);
        if (content->sound)
            body.sound = [UNNotificationSound defaultSound];
        // 그림을 붙이지 못하면 그림 없이 올린다 (Android와 같다).
        NSURL* image { nil };
        if (value.image.valid())
        {
            image = write_image(identifier, value.image);
            if (image != nil)
            {
                UNNotificationAttachment* const attachment { [UNNotificationAttachment attachmentWithIdentifier:image_attachment URL:image options:nil error:nil] };
                if (attachment != nil)
                    body.attachments = @[ attachment ];
                else
                {
                    remove_file(image);
                    image = nil;
                }
            }
        }
        // 같은 id의 요청은 알림 센터가 바꾸기로 다룬다.
        UNNotificationRequest* const request { [UNNotificationRequest requestWithIdentifier:identifier content:body trigger:nil] };
        const dispatch_semaphore_t done { dispatch_semaphore_create(0) };
        __block NSError* added_error { nil };
        [center addNotificationRequest:request
                 withCompletionHandler:^(NSError* const added) {
                   added_error = added;
                   if (added != nil)
                       remove_file(image);
                   dispatch_semaphore_signal(done);
                 }];
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, answer_timeout_ns)) != 0)
            return failure(u8"The notification center did not answer in time; the notification may still appear.");
        if (added_error != nil)
            return failure(error_text(u8"The notification center refused the notification", added_error));
        return succeeded();
    }

    platform_result close_notification(const std::u8string_view id)
    {
        if (notification_id_valid(id) == false)
            return failure(u8"The notification id must be 1 to 64 ASCII letters, digits, '.', '_' or '-'.");
        if (bundled() == false)
            return not_bundled();
        UNUserNotificationCenter* const center { notification_center() };
        NSArray<NSString*>* const identifiers { @[ macos::make_ns_string(id) ] };
        [center removeDeliveredNotificationsWithIdentifiers:identifiers];
        [center removePendingNotificationRequestsWithIdentifiers:identifiers];
        return succeeded();
    }

    platform_result close_all_notifications()
    {
        if (bundled() == false)
            return not_bundled();
        UNUserNotificationCenter* const center { notification_center() };
        [center removeAllDeliveredNotifications];
        [center removeAllPendingNotificationRequests];
        return succeeded();
    }

    bool notification_launch(const std::span<const std::u8string> arguments) noexcept
    {
        // macOS의 알림은 인자가 아니라 delegate의 응답으로 앱을 띄운다. 콜드 스타트 눌림은 `notification_event::launched`로 온다.
        static_cast<void>(arguments);
        return false;
    }
}

namespace luil::macos {
    bool notification_center_available()
    {
        return bundled();
    }

    void set_notification_receiver(std::function<void(notification_event)> receiver)
    {
        // delegate를 이때 건다. 꺼진 앱을 띄운 눌림은 시작을 마치기 전에 건 delegate에만 온다.
        if (bundled())
            static_cast<void>(notification_center());
        relay().set_receiver(std::move(receiver));
    }

    permission_state notification_permission()
    {
        if (bundled() == false)
            return permission_state::not_required;
        const std::optional<notification_settings> settings { read_settings(notification_center()) };
        return settings.has_value() ? notification_permission_of(settings->authorization) : permission_state::denied;
    }

    bool request_notification_permission(std::function<void(permission_state)> answered)
    {
        if (bundled() == false || notification_permission() == permission_state::granted)
            return false;
        const std::function<void(permission_state)> callback { std::move(answered) };
        [notification_center() requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                             completionHandler:^(const BOOL granted, NSError* const error) {
                                               static_cast<void>(error);
                                               const permission_state state { granted ? permission_state::granted : permission_state::denied };
                                               dispatch_async(dispatch_get_main_queue(), ^{
                                                 if (callback == nullptr)
                                                     return;
                                                 try
                                                 {
                                                     callback(state);
                                                 }
                                                 catch (...)
                                                 {
                                                 }
                                               });
                                             }];
        return true;
    }
}

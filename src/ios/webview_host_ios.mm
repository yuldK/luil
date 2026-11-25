#include "ios/webview_host_ios.h"

#include "host/webview_message_gate.h"

#import <UIKit/UIGestureRecognizerSubclass.h>
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {
    // 페이지와 앱의 다리 이름이다. `window.webkit.messageHandlers.<이름>`으로 선다.
    NSString* const bridge_name { @"luilWebView" };

    // 페이지에 WebView2와 같은 꼴의 다리를 세운다 (`window.chrome.webview`). macOS와 같은 글이다.
    //  - `postMessage(값)`은 값을 JSON으로 적어 보낸다. 앱은 WebView2처럼 JSON 글을 받는다.
    //  - 앱이 보낸 것은 `message` 사건의 `data`로 온다 (WebView2의 `PostWebMessageAsJson`과 같다 — 풀린 값이다).
    //  - 주 문서에만 넣는다. iframe은 다리를 받지 않는다.
    NSString* const bridge_script { @"(function(){"
                                     "if(window.chrome&&window.chrome.webview)return;"
                                     "var handler=window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers.luilWebView;"
                                     "if(!handler)return;"
                                     "var listeners=[];"
                                     "var hub={"
                                     "postMessage:function(value){handler.postMessage(JSON.stringify(value===undefined?null:value));},"
                                     "addEventListener:function(type,listener){if(type==='message'&&typeof listener==='function'&&listeners.indexOf(listener)<0)listeners.push(listener);},"
                                     "removeEventListener:function(type,listener){var at=listeners.indexOf(listener);if(type==='message'&&at>=0)listeners.splice(at,1);}"
                                     "};"
                                     "window.chrome=window.chrome||{};"
                                     "Object.defineProperty(window.chrome,'webview',{value:hub,configurable:false});"
                                     "Object.defineProperty(window,'__luilWebViewDeliver',{value:function(data){"
                                     "listeners.slice().forEach(function(listener){try{listener({data:data});}catch(error){console.error(error);}});"
                                     "},configurable:false});"
                                     "})();" };

    // UTF-8이 아니면 nil이다.
    [[nodiscard]] NSString* make_ns_string(const std::u8string_view text)
    {
        return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    }

    [[nodiscard]] std::u8string from_ns_string(NSString* const text)
    {
        if (text == nil)
            return {};
        const char* const bytes { [text UTF8String] };
        if (bytes == nullptr)
            return {};
        const std::string_view view { bytes };
        return { view.begin(), view.end() };
    }

    [[nodiscard]] std::u8string lowercase(std::u8string text)
    {
        for (char8_t& character : text)
            if (character >= u8'A' && character <= u8'Z')
                character = static_cast<char8_t>(character - u8'A' + u8'a');
        return text;
    }

    [[nodiscard]] std::u8string url_text(NSURL* const url)
    {
        return url != nil ? from_ns_string(url.absoluteString) : std::u8string {};
    }

    [[nodiscard]] std::uint64_t now_ms() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // 프로필 자리 하나에 웹 자료 저장소 하나다 (iOS 17부터). 같은 자리는 늘 같은 식별자로 푼다.
    [[nodiscard]] NSUUID* store_identifier(const std::u8string& folder)
    {
        unsigned char digest[CC_SHA256_DIGEST_LENGTH] {};
        CC_SHA256(folder.data(), static_cast<CC_LONG>(folder.size()), digest);
        uuid_t bytes {};
        std::memcpy(bytes, digest, sizeof(bytes));
        // 이름 기반 UUID의 판·변형 비트다 (RFC 9562).
        bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x50);
        bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
        return [[NSUUID alloc] initWithUUIDBytes:bytes];
    }

    // 그 view나 그 안의 view가 첫 응답자인가. WKWebView의 첫 응답자는 안쪽 내용 view다.
    [[nodiscard]] bool holds_first_responder(UIView* const view)
    {
        if (view.isFirstResponder)
            return true;
        for (UIView* child in view.subviews)
            if (holds_first_responder(child))
                return true;
        return false;
    }

    // 웹뷰가 쥔 첫 응답자를 Skia view에 돌려준다. 숨은 웹뷰가 키(와 화면 키보드)를 쥐지 않게 한다.
    //  - Skia view가 첫 응답자가 될 수 없으면 놓기만 한다.
    void return_first_responder(UIView* const view, UIView* const skia_view)
    {
        if (view.window == nil || holds_first_responder(view) == false)
            return;
        [view endEditing:YES];
        if (skia_view != nil && skia_view.window != nil && skia_view.canBecomeFirstResponder)
            [skia_view becomeFirstResponder];
    }

    // 확대(핀치)를 정책에 맞춘다.
    //  - iOS의 WKWebView에는 macOS의 `allowsMagnification`이 없다. 스크롤 view의 핀치 인식기를 끄고 켠다.
    //  - 그 인식기는 페이지의 viewport가 바뀌면 WebKit이 새로 세울 수 있어 정책을 낼 때와 항해를 마칠 때마다 다시 맞춘다.
    //  - 페이지가 viewport로 확대를 닫았으면 인식기가 없어 정책이 열어도 확대되지 않는다. 두 번 눌러 확대하는 것은 막지 못한다.
    void apply_pinch(WKWebView* const view, const bool allowed)
    {
        if (UIPinchGestureRecognizer* const pinch { view.scrollView.pinchGestureRecognizer }; pinch != nil && pinch.enabled != allowed)
            pinch.enabled = allowed ? YES : NO;
    }

    // 창의 맨 위 view controller다. 대화상자를 그 위에 띄운다.
    //  - 띄우거나 거두는 중인 것이 있으면 nil이다. UIKit은 그때 새로 띄우기를 거절한다.
    [[nodiscard]] UIViewController* dialog_presenter(UIView* const view)
    {
        UIViewController* top { view.window.rootViewController };
        if (top == nil)
            return nil;
        while (top.presentedViewController != nil)
            top = top.presentedViewController;
        for (UIViewController* at { top }; at != nil; at = at.presentingViewController)
            if (at.isBeingPresented || at.isBeingDismissed)
                return nil;
        return top;
    }

    // 대화상자의 답을 한 번만 낸다. 답하지 못한 채 사라지면(띄우지 못했거나 남이 거두었다) 기본 답을 낸다.
    //  - WebKit은 부르지 않은 completion handler가 풀리면 예외를 던진다. 대화상자의 단추들이 이것을 나눠 쥐고, 마지막 것이 풀릴 때 판정한다.
    class dialog_reply
    {
    public:
        explicit dialog_reply(std::function<void()> fallback)
            : fallback_ { std::move(fallback) }
        {
        }
        dialog_reply(const dialog_reply&) = delete;
        dialog_reply(dialog_reply&&) = delete;
        dialog_reply& operator=(const dialog_reply&) = delete;
        dialog_reply& operator=(dialog_reply&&) = delete;
        ~dialog_reply()
        {
            if (answered_ == false && fallback_ != nullptr)
                fallback_();
        }

        // 처음 답하는 쪽만 참이다.
        [[nodiscard]] bool claim() noexcept
        {
            if (answered_)
                return false;
            answered_ = true;
            return true;
        }

    private:
        std::function<void()> fallback_ {};
        bool answered_ { false };
    };
}

namespace luil::ios {
    struct webview_host::entry
    {
        ui_webview want {};
        WKWebView* view { nil };
        id delegate { nil };
        webview_layout layout {};
        float surface_scale { 1.0f };
        bool placed { false };
        // 첫 문서를 다 읽었다. 위층은 그때까지 숨고, 아래층은 그때부터 구멍을 연다 (설계 문서의 첫 그림 규칙).
        bool loaded { false };
        std::uint64_t navigated_revision { 0 };
        std::uint64_t posted_revision { 0 };
        bool presentation_reported { false };
        // UI delegate에 실은 기본 메뉴 정책이다. 바뀌면 delegate를 다시 단다 (WebKit이 delegate의 응답 목록을 달 때 읽는다).
        std::optional<bool> context_menus_applied {};
        webview_message_gate gate {};
        std::optional<std::chrono::steady_clock::time_point> last_render_failure {};
    };
}

// 웹뷰의 키보드 초점을 host에 알린다.
@interface LuilIosWebView : WKWebView
@property(nonatomic, assign) luil::ios::webview_host::entry* entry;
@property(nonatomic, assign) std::shared_ptr<std::function<void(const std::u8string&)>>* focusReporter;
- (void)luilReportTouch;
@end

// 웹뷰에 닿는 손가락을 지켜보기만 한다. 닿은 순간 웹뷰에 알리고 곧바로 실패한다.
//  - 인식하지 않으니 페이지의 손짓(누름·스크롤·핀치)을 막거나 늦추지 않는다. 페이지가 touchstart를 막아도 손가락은 먼저 여기를 지난다.
//  - 손짓 한 벌(손가락이 모두 떨어질 때까지)에 한 번 알린다. 트랙패드의 두 손가락 스크롤은 손가락이 아니라 알리지 않는다 (macOS와 같다).
@interface LuilIosWebViewTouchObserver : UIGestureRecognizer <UIGestureRecognizerDelegate>
@end

// 항해·UI·다리 사건을 받아 정책을 집행하고 앱에 알린다.
@interface LuilIosWebViewDelegate : NSObject <WKNavigationDelegate, WKUIDelegate, WKScriptMessageHandler>
@property(nonatomic, assign) luil::ios::webview_host::entry* entry;
@property(nonatomic, assign) std::shared_ptr<std::function<void(luil::app_message)>>* deliver;
@end

namespace {
    using luil::webview_event;
    using luil::webview_event_kind;

    // 사건 하나를 앱에 낸다. `gated`면 페이지가 일으킨 사건이라 상한을 지난다 (webview_message_gate).
    void report(luil::ios::webview_host::entry* const target, std::shared_ptr<std::function<void(luil::app_message)>>* const deliver, webview_event event, const std::size_t bytes,
        const bool gated, const std::u8string_view source)
    {
        if (target == nullptr || deliver == nullptr || *deliver == nullptr || **deliver == nullptr || target->want.on_event == nullptr)
            return;
        if (gated)
        {
            const luil::webview_policy& policy { target->want.policy };
            const luil::webview_message_decision decision { target->gate.admit(now_ms(), bytes, policy.maximum_message_bytes, policy.maximum_events_per_second) };
            if (decision.verdict != luil::webview_message_verdict::accept)
            {
                if (decision.notify == false)
                    return;
                webview_event notice {};
                notice.id = target->want.id;
                notice.kind = webview_event_kind::events_dropped;
                notice.error = std::u8string { source } + (decision.verdict == luil::webview_message_verdict::drop_size ? u8": size" : u8": rate");
                event = std::move(notice);
            }
        }
        event.id = target->want.id;
        luil::app_message message { target->want.on_event(std::move(event)) };
        if (message.empty() == false)
            (**deliver)(std::move(message));
    }

    void report_simple(luil::ios::webview_host::entry* const target, std::shared_ptr<std::function<void(luil::app_message)>>* const deliver, const webview_event_kind kind, std::u8string url,
        std::u8string error, const bool gated, const std::u8string_view source)
    {
        webview_event event {};
        event.kind = kind;
        event.url = std::move(url);
        event.error = std::move(error);
        report(target, deliver, std::move(event), 0, gated, source);
    }

    // 정책의 스킴 목록이 그 주소를 여는가. 비어 있으면 https만이다. `javascript`는 언제나 막는다.
    [[nodiscard]] bool scheme_allowed(const luil::webview_policy& policy, NSURL* const url)
    {
        const std::u8string scheme { lowercase(from_ns_string(url.scheme)) };
        if (scheme.empty() || scheme == u8"javascript")
            return false;
        if (policy.allowed_schemes.empty())
            return scheme == u8"https";
        return std::ranges::any_of(policy.allowed_schemes, [&scheme](const std::u8string& allowed) { return lowercase(allowed) == scheme; });
    }

    // 사용자가 취소한 것이나 정책이 끊은 것은 실패가 아니다 (그 사건은 따로 알렸다).
    [[nodiscard]] bool quiet_failure(NSError* const error)
    {
        if ([error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled)
            return true;
        // WebKitErrorFrameLoadInterruptedByPolicyChange다.
        return [error.domain isEqualToString:@"WebKitErrorDomain"] && error.code == 102;
    }

    // 정책이 대화상자를 닫았거나 띄울 자리가 없으면 nil이다. 그때는 곧바로 기본 답을 낸다.
    [[nodiscard]] UIViewController* script_dialog_presenter(const luil::ios::webview_host::entry* const target, WKWebView* const view)
    {
        if (target == nullptr || target->want.policy.default_script_dialogs_enabled == false || view.window == nil)
            return nil;
        return dialog_presenter(view);
    }
}

@implementation LuilIosWebView

- (void)luilReportTouch
{
    if (self.entry != nullptr && self.focusReporter != nullptr && *self.focusReporter != nullptr && **self.focusReporter != nullptr)
        (**self.focusReporter)(self.entry->want.id);
}

@end

@implementation LuilIosWebViewTouchObserver

- (instancetype)init
{
    self = [super initWithTarget:nil action:nil];
    if (self != nil)
    {
        self.cancelsTouchesInView = NO;
        self.delaysTouchesBegan = NO;
        self.delaysTouchesEnded = NO;
        self.delegate = self;
    }
    return self;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    [super touchesBegan:touches withEvent:event];
    if ([self.view isKindOfClass:[LuilIosWebView class]])
        [(LuilIosWebView*)self.view luilReportTouch];
    self.state = UIGestureRecognizerStateFailed;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)otherGestureRecognizer
{
    static_cast<void>(gestureRecognizer);
    static_cast<void>(otherGestureRecognizer);
    return YES;
}

@end

@implementation LuilIosWebViewDelegate

// 정책이 기본 메뉴를 닫았을 때만 메뉴 갈래에 답한다. 답하지 않으면 WebKit이 기본 메뉴를 낸다.
- (BOOL)respondsToSelector:(SEL)selector
{
    if (selector == @selector(webView:contextMenuConfigurationForElement:completionHandler:))
        return self.entry != nullptr && self.entry->want.policy.default_context_menus_enabled == false;
    return [super respondsToSelector:selector];
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                        preferences:(WKWebpagePreferences*)preferences
                    decisionHandler:(void (^)(WKNavigationActionPolicy, WKWebpagePreferences*))decisionHandler
{
    luil::ios::webview_host::entry* const target { self.entry };
    if (target == nullptr)
    {
        decisionHandler(WKNavigationActionPolicyCancel, preferences);
        return;
    }
    const luil::webview_policy& policy { target->want.policy };
    preferences.allowsContentJavaScript = policy.script_enabled;
    NSURL* const url { action.request.URL };
    // 새 창 요청이다 (`window.open`·`target=_blank`). 두 번째 창은 띄우지 않는다.
    //  - 정책이 열면 주 문서가 사용자 손짓(링크 누름)으로 낸 요청만 이 웹뷰에서 연다. 스킴 검사는 그대로다.
    if (action.targetFrame == nil)
    {
        decisionHandler(WKNavigationActionPolicyCancel, preferences);
        const bool from_main { action.sourceFrame != nil && action.sourceFrame.isMainFrame };
        const bool gesture { action.navigationType == WKNavigationTypeLinkActivated };
        if (policy.open_new_windows_here && from_main && gesture && scheme_allowed(policy, url))
            [webView loadRequest:action.request];
        else
            report_simple(target, self.deliver, webview_event_kind::navigation_blocked, url_text(url),
                policy.open_new_windows_here ? u8"new window: not a user gesture from the main document, or its scheme is not allowed" : u8"new window: not allowed", true,
                u8"navigation");
        return;
    }
    if (action.shouldPerformDownload)
    {
        decisionHandler(WKNavigationActionPolicyCancel, preferences);
        report_simple(target, self.deliver, webview_event_kind::download_blocked, url_text(url), u8"download", true, u8"download");
        return;
    }
    // 주 문서의 항해만 스킴을 본다. iframe 안의 항해는 그물 밖이다 (Windows와 같다).
    if (action.targetFrame.isMainFrame && scheme_allowed(policy, url) == false)
    {
        decisionHandler(WKNavigationActionPolicyCancel, preferences);
        report_simple(target, self.deliver, webview_event_kind::navigation_blocked, url_text(url), u8"scheme: " + lowercase(from_ns_string(url.scheme)), true, u8"navigation");
        return;
    }
    decisionHandler(WKNavigationActionPolicyAllow, preferences);
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)response
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler
{
    static_cast<void>(webView);
    bool attachment { false };
    if (NSHTTPURLResponse* const http { [response.response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse*)response.response : nil }; http != nil)
        if (NSString* const disposition { [http valueForHTTPHeaderField:@"Content-Disposition"] }; disposition != nil)
            attachment = [[disposition lowercaseString] hasPrefix:@"attachment"];
    // 내려받기는 디스크에 쓰는 일이라 언제나 막는다. 저장 대화상자도 뜨지 않는다.
    if (attachment || response.canShowMIMEType == NO)
    {
        decisionHandler(WKNavigationResponsePolicyCancel);
        report_simple(self.entry, self.deliver, webview_event_kind::download_blocked, url_text(response.response.URL), u8"download", true, u8"download");
        return;
    }
    decisionHandler(WKNavigationResponsePolicyAllow);
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation
{
    static_cast<void>(navigation);
    report_simple(self.entry, self.deliver, webview_event_kind::navigation_started, url_text(webView.URL), {}, true, u8"navigation");
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation
{
    static_cast<void>(navigation);
    // 새 문서의 viewport가 핀치 인식기를 새로 세웠을 수 있다.
    if (self.entry != nullptr)
        apply_pinch(webView, self.entry->want.policy.pinch_zoom_enabled);
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation
{
    static_cast<void>(navigation);
    if (self.entry != nullptr)
    {
        self.entry->loaded = true;
        apply_pinch(webView, self.entry->want.policy.pinch_zoom_enabled);
    }
    report_simple(self.entry, self.deliver, webview_event_kind::navigation_completed, url_text(webView.URL), {}, true, u8"navigation");
}

- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error
{
    static_cast<void>(navigation);
    if (quiet_failure(error))
        return;
    NSURL* const failed { error.userInfo[NSURLErrorFailingURLErrorKey] };
    report_simple(self.entry, self.deliver, webview_event_kind::navigation_failed, url_text(failed != nil ? failed : webView.URL), from_ns_string(error.localizedDescription), true, u8"navigation");
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error
{
    static_cast<void>(navigation);
    if (quiet_failure(error))
        return;
    report_simple(self.entry, self.deliver, webview_event_kind::navigation_failed, url_text(webView.URL), from_ns_string(error.localizedDescription), true, u8"navigation");
}

// 페이지를 그리던 프로세스가 죽었다. 다시 읽되, 10초 안에 또 죽으면 알리기만 한다 (Windows와 같은 규칙).
//  - iOS는 앱이 뒤에 있는 동안 메모리가 모자라면 이 프로세스를 거두기도 한다. 그것도 같은 갈래다.
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView
{
    luil::ios::webview_host::entry* const target { self.entry };
    if (target == nullptr)
        return;
    const auto now { std::chrono::steady_clock::now() };
    const bool again { target->last_render_failure.has_value() && now - *target->last_render_failure < std::chrono::seconds { 10 } };
    target->last_render_failure = now;
    report_simple(target, self.deliver, webview_event_kind::render_process_failed, url_text(webView.URL),
        again ? u8"The web content process failed again within 10 seconds; not reloading." : std::u8string {}, false, {});
    if (again == false)
        [webView reload];
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)features
{
    // 새 창은 항해 정책(`targetFrame == nil`)이 이미 다뤘다. 여기로 와도 창을 만들지 않는다.
    static_cast<void>(webView);
    static_cast<void>(configuration);
    static_cast<void>(action);
    static_cast<void>(features);
    return nil;
}

// 길게 누름의 기본 메뉴(링크·그림의 미리 보기와 동작)다. 정책이 닫았을 때만 여기로 온다 (`respondsToSelector:`). 메뉴 없이 답한다.
//  - 글 고르기의 편집 메뉴(복사 등)는 브라우저 메뉴가 아니라 글 편집이라 그대로 둔다.
- (void)webView:(WKWebView*)webView
    contextMenuConfigurationForElement:(WKContextMenuElementInfo*)elementInfo
                     completionHandler:(void (^)(UIContextMenuConfiguration*))completionHandler
{
    static_cast<void>(webView);
    static_cast<void>(elementInfo);
    completionHandler(nil);
}

// `alert`·`confirm`·`prompt`는 창의 맨 위 view controller에 경고 상자로 띄운다 (macOS의 sheet 자리다).
- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                      initiatedByFrame:(WKFrameInfo*)frame
                     completionHandler:(void (^)(void))completionHandler
{
    static_cast<void>(frame);
    UIViewController* const presenter { script_dialog_presenter(self.entry, webView) };
    if (presenter == nil)
    {
        completionHandler();
        return;
    }
    const auto reply { std::make_shared<dialog_reply>([completionHandler] { completionHandler(); }) };
    UIAlertController* const alert { [UIAlertController alertControllerWithTitle:nil message:message preferredStyle:UIAlertControllerStyleAlert] };
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction*) {
                                              if (reply->claim())
                                                  completionHandler();
                                            }]];
    [presenter presentViewController:alert animated:YES completion:nil];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptConfirmPanelWithMessage:(NSString*)message
                        initiatedByFrame:(WKFrameInfo*)frame
                       completionHandler:(void (^)(BOOL))completionHandler
{
    static_cast<void>(frame);
    UIViewController* const presenter { script_dialog_presenter(self.entry, webView) };
    if (presenter == nil)
    {
        completionHandler(NO);
        return;
    }
    const auto reply { std::make_shared<dialog_reply>([completionHandler] { completionHandler(NO); }) };
    UIAlertController* const alert { [UIAlertController alertControllerWithTitle:nil message:message preferredStyle:UIAlertControllerStyleAlert] };
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction*) {
                                              if (reply->claim())
                                                  completionHandler(YES);
                                            }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:^(UIAlertAction*) {
                                              if (reply->claim())
                                                  completionHandler(NO);
                                            }]];
    [presenter presentViewController:alert animated:YES completion:nil];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
                              defaultText:(NSString*)defaultText
                         initiatedByFrame:(WKFrameInfo*)frame
                        completionHandler:(void (^)(NSString*))completionHandler
{
    static_cast<void>(frame);
    UIViewController* const presenter { script_dialog_presenter(self.entry, webView) };
    if (presenter == nil)
    {
        completionHandler(nil);
        return;
    }
    const auto reply { std::make_shared<dialog_reply>([completionHandler] { completionHandler(nil); }) };
    UIAlertController* const alert { [UIAlertController alertControllerWithTitle:nil message:prompt preferredStyle:UIAlertControllerStyleAlert] };
    [alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
      field.text = defaultText ?: @"";
    }];
    // 단추가 상자를 강하게 쥐면 서로를 놓지 못한다.
    __weak UIAlertController* const weak_alert { alert };
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:^(UIAlertAction*) {
                                              if (reply->claim())
                                                  completionHandler(weak_alert.textFields.firstObject.text ?: @"");
                                            }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:^(UIAlertAction*) {
                                              if (reply->claim())
                                                  completionHandler(nil);
                                            }]];
    [presenter presentViewController:alert animated:YES completion:nil];
}

// 카메라·마이크다. 언제나 거절하고 알린다 (Windows와 같다 — 여는 스위치는 일부러 없다).
- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                          initiatedByFrame:(WKFrameInfo*)frame
                                      type:(WKMediaCaptureType)type
                           decisionHandler:(void (^)(WKPermissionDecision))decisionHandler
{
    static_cast<void>(origin);
    decisionHandler(WKPermissionDecisionDeny);
    const char8_t* const name { type == WKMediaCaptureTypeCamera ? u8"camera" : type == WKMediaCaptureTypeMicrophone ? u8"microphone" : u8"camera and microphone" };
    report_simple(self.entry, self.deliver, webview_event_kind::permission_denied, url_text(frame.request.URL != nil ? frame.request.URL : webView.URL), name, true, u8"permission request");
}

- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message
{
    static_cast<void>(controller);
    if ([message.body isKindOfClass:[NSString class]] == NO)
        return;
    NSString* const json { (NSString*)message.body };
    webview_event event {};
    event.kind = webview_event_kind::web_message_received;
    event.url = url_text(message.frameInfo.request.URL);
    event.message = from_ns_string(json);
    const std::size_t bytes { event.message.size() };
    report(self.entry, self.deliver, std::move(event), bytes, true, u8"web message");
}

@end

namespace luil::ios {
    webview_host::webview_host(UIView* const container, UIView* const skia_view, std::u8string surface)
        : container_ { container }
        , skia_view_ { skia_view }
        , surface_ { std::move(surface) }
    {
    }

    webview_host::~webview_host()
    {
        shutdown();
    }

    void webview_host::set_deliver(std::function<void(app_message)> deliver)
    {
        *deliver_ = std::move(deliver);
    }

    void webview_host::set_focus_reporter(std::function<void(const std::u8string& id)> reporter)
    {
        *focus_reporter_ = std::move(reporter);
    }

    webview_host::entry* webview_host::find(const std::u8string& id) noexcept
    {
        const auto found { std::ranges::find_if(entries_, [&id](const std::unique_ptr<entry>& candidate) { return candidate->want.id == id; }) };
        return found != entries_.end() ? found->get() : nullptr;
    }

    const webview_host::entry* webview_host::find(const std::u8string& id) const noexcept
    {
        const auto found { std::ranges::find_if(entries_, [&id](const std::unique_ptr<entry>& candidate) { return candidate->want.id == id; }) };
        return found != entries_.end() ? found->get() : nullptr;
    }

    void webview_host::synchronize(const std::span<const ui_webview> wanted)
    {
        // 빠진 것과 프로필이 바뀐 것을 거둔다. 프로필이 다르면 저장소가 달라 같은 웹뷰로 이어 갈 수 없다.
        std::erase_if(entries_, [this, wanted, &surface = surface_](std::unique_ptr<entry>& current) {
            const auto still { std::ranges::find_if(wanted, [&current, &surface](const ui_webview& want) { return want.anchor == surface && want.id == current->want.id; }) };
            if (still != wanted.end() && still->user_data_folder == current->want.user_data_folder)
                return false;
            tear_down(*current);
            return true;
        });
        for (const ui_webview& want : wanted)
        {
            if (want.anchor != surface_ || want.id.empty())
                continue;
            entry* target { find(want.id) };
            if (target == nullptr)
            {
                auto created { std::make_unique<entry>() };
                created->want = want;
                target = created.get();
                entries_.push_back(std::move(created));
                create(*target);
            }
            else
            {
                const webview_layer previous_layer { target->want.layer };
                target->want = want;
                if (previous_layer != want.layer)
                    place(*target);
            }
            apply_policy(*target);
            flush_commands(*target);
        }
    }

    void webview_host::create(entry& target)
    {
        if (target.want.user_data_folder.empty())
        {
            // 프로필 자리가 없으면 만들지 않는다 (webview.h). 앱이 자리를 정하지 않은 것을 알린다.
            webview_event event {};
            event.kind = webview_event_kind::creation_failed;
            event.failure = webview_creation_failure::environment_failed;
            event.error = u8"The webview has no user data folder.";
            report(&target, &deliver_, std::move(event), 0, false, {});
            return;
        }
        WKWebViewConfiguration* const configuration { [WKWebViewConfiguration new] };
        // iOS 16 이하는 기본 저장소 하나를 함께 쓴다.
        if (@available(iOS 17.0, *))
            configuration.websiteDataStore = [WKWebsiteDataStore dataStoreForIdentifier:store_identifier(target.want.user_data_folder)];
        // iPhone의 기본은 동영상을 전체 화면으로 따로 띄우는 것이다. 데스크톱처럼 자리 안에서 튼다.
        configuration.allowsInlineMediaPlayback = YES;
        WKUserContentController* const content { [WKUserContentController new] };
        [content addUserScript:[[WKUserScript alloc] initWithSource:bridge_script injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
        LuilIosWebViewDelegate* const delegate { [LuilIosWebViewDelegate new] };
        delegate.entry = &target;
        delegate.deliver = &deliver_;
        [content addScriptMessageHandler:delegate name:bridge_name];
        configuration.userContentController = content;

        LuilIosWebView* const view { [[LuilIosWebView alloc] initWithFrame:CGRectZero configuration:configuration] };
        view.entry = &target;
        view.focusReporter = &focus_reporter_;
        view.navigationDelegate = delegate;
        view.UIDelegate = delegate;
        target.context_menus_applied = target.want.policy.default_context_menus_enabled;
        // 자리는 host가 이미 안전 영역 안에 앉힌다. 웹뷰가 안전 영역만큼 내용을 한 번 더 밀지 않게 한다.
        view.scrollView.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
        [view addGestureRecognizer:[LuilIosWebViewTouchObserver new]];
        view.hidden = YES;
        target.view = view;
        target.delegate = delegate;
        place(target);

        if (target.want.report_presentation && target.presentation_reported == false)
        {
            // 웹뷰는 창의 UIView라 CPU 렌더러 위에서도 늘 합성된다.
            target.presentation_reported = true;
            webview_event event {};
            event.kind = webview_event_kind::presentation_changed;
            event.presentation_available = true;
            report(&target, &deliver_, std::move(event), 0, false, {});
        }
    }

    // 브라우저 설정 갈래다. 스크립트는 다음 항해부터 든다 (항해 정책이 그 항해의 설정에 싣는다).
    void webview_host::apply_policy(entry& target)
    {
        if (target.view == nil)
            return;
        const webview_policy& policy { target.want.policy };
        apply_pinch(target.view, policy.pinch_zoom_enabled);
        target.view.allowsBackForwardNavigationGestures = policy.swipe_navigation_enabled;
        // iOS 16.3 이하에는 스위치가 없다. 개발 build의 웹뷰는 늘 Safari 웹 속성으로 열린다.
        if (@available(iOS 16.4, *))
            target.view.inspectable = policy.developer_tools_enabled;
        // 기본 메뉴 갈래는 delegate가 응답하는지로 정한다. WebKit이 그 목록을 delegate를 달 때 읽으므로 바뀌면 다시 단다.
        if (target.context_menus_applied != policy.default_context_menus_enabled)
        {
            target.context_menus_applied = policy.default_context_menus_enabled;
            id<WKUIDelegate> const delegate { target.view.UIDelegate };
            target.view.UIDelegate = nil;
            target.view.UIDelegate = delegate;
        }
        // 브라우저 단축키(`browser_accelerator_keys_enabled`)는 macOS처럼 다룰 것이 없다.
    }

    void webview_host::flush_commands(entry& target)
    {
        if (target.view == nil)
            return;
        if (target.want.navigate_revision != 0 && target.want.navigate_revision != target.navigated_revision)
        {
            target.navigated_revision = target.want.navigate_revision;
            NSString* const text { make_ns_string(target.want.navigate_url) };
            NSURL* const url { text != nil ? [NSURL URLWithString:text] : nil };
            if (url == nil)
                report_simple(&target, &deliver_, webview_event_kind::navigation_failed, target.want.navigate_url, u8"The URL is not valid.", false, {});
            else if (scheme_allowed(target.want.policy, url) == false)
                report_simple(&target, &deliver_, webview_event_kind::navigation_blocked, target.want.navigate_url, u8"scheme: " + lowercase(from_ns_string(url.scheme)), false, {});
            else
                [target.view loadRequest:[NSURLRequest requestWithURL:url]];
        }
        if (target.want.post_revision != 0 && target.want.post_revision != target.posted_revision)
        {
            target.posted_revision = target.want.post_revision;
            // JSON은 그대로 JavaScript 식이다. 다리가 그 값을 `message` 사건의 `data`로 낸다.
            if (NSString* const json { make_ns_string(target.want.post_message) }; json != nil && json.length > 0)
                [target.view evaluateJavaScript:[NSString stringWithFormat:@"window.__luilWebViewDeliver&&window.__luilWebViewDeliver(%@)", json] completionHandler:nil];
        }
    }

    void webview_host::apply_layout(const std::u8string& id, const webview_layout& layout, const float surface_scale)
    {
        entry* const target { find(id) };
        if (target == nullptr)
            return;
        target->layout = layout;
        target->surface_scale = surface_scale > 0.0f ? surface_scale : 1.0f;
        target->placed = true;
        place(*target);
    }

    void webview_host::place(entry& target)
    {
        if (target.view == nil)
            return;
        // 층이 정한 자리에 둔다. 위층은 Skia view 위, 아래층은 그 아래다. 페이지는 그대로다.
        const bool above { target.want.layer == webview_layer::overlay };
        const bool beside_skia { skia_view_ != nil && skia_view_.superview == container_ };
        bool reorder { target.view.superview != container_ };
        if (reorder == false && beside_skia)
        {
            NSArray<UIView*>* const siblings { container_.subviews };
            reorder = above != ([siblings indexOfObject:target.view] > [siblings indexOfObject:skia_view_]);
        }
        if (reorder)
        {
            // 같은 부모 안에서는 떼지 않고 순서만 바꾼다. 떼었다 다시 붙이면 첫 응답자를 잃는다.
            if (beside_skia == false)
                [container_ addSubview:target.view];
            else if (above)
                [container_ insertSubview:target.view aboveSubview:skia_view_];
            else
                [container_ insertSubview:target.view belowSubview:skia_view_];
        }
        const webview_layout& layout { target.layout };
        const float scale { target.surface_scale };
        // 숨긴 동안에도 자리의 크기는 먼저 준다. 첫 문서가 0×0 뷰포트로 열리지 않게 한다 (설계 문서).
        //  - UIKit은 뒤집지 않아도 원점이 왼쪽 위다. 자리는 Skia view의 좌표라 그릇의 좌표로 옮긴다.
        if (target.placed && layout.width > 0 && layout.height > 0)
        {
            const CGRect rect { CGRectMake(layout.x / scale, layout.y / scale, layout.width / scale, layout.height / scale) };
            target.view.frame = beside_skia ? [container_ convertRect:rect fromView:skia_view_] : rect;
        }
        const bool shown { target.placed && layout.visible() && (target.want.layer == webview_layer::underlay || target.loaded) };
        // 숨기기 전에 키보드 초점을 돌려준다. 숨은 웹뷰가 키와 화면 키보드를 쥐지 않게 한다.
        if (shown == false && target.view.hidden == NO)
            return_first_responder(target.view, skia_view_);
        target.view.hidden = shown ? NO : YES;
    }

    bool webview_host::opens_hole(const std::u8string& id) const noexcept
    {
        const entry* const target { find(id) };
        return target != nullptr && target->view != nil && target->loaded;
    }

    bool webview_host::passes_through(const float x, const float y) const noexcept
    {
        for (const std::unique_ptr<entry>& target : entries_)
        {
            const webview_layout& layout { target->layout };
            if (target->view == nil || target->view.hidden || target->want.layer != webview_layer::underlay || layout.punch_hole() == false || target->loaded == false)
                continue;
            if (x >= static_cast<float>(layout.x) && y >= static_cast<float>(layout.y) && x < static_cast<float>(layout.x + layout.width) && y < static_cast<float>(layout.y + layout.height))
                return true;
        }
        return false;
    }

    bool webview_host::covers(const float x, const float y) const noexcept
    {
        if (passes_through(x, y))
            return true;
        for (const std::unique_ptr<entry>& target : entries_)
        {
            const webview_layout& layout { target->layout };
            if (target->view == nil || target->view.hidden || target->want.layer != webview_layer::overlay)
                continue;
            if (x >= static_cast<float>(layout.x) && y >= static_cast<float>(layout.y) && x < static_cast<float>(layout.x + layout.width) && y < static_cast<float>(layout.y + layout.height))
                return true;
        }
        return false;
    }

    bool webview_host::move_focus_in(const std::u8string& id)
    {
        entry* const target { find(id) };
        if (target == nullptr || target->view == nil || target->view.hidden || target->view.window == nil)
            return false;
        // WKWebView는 첫 응답자 자리를 안쪽 내용 view에 넘긴다. 하드웨어 키보드의 키가 페이지로 간다.
        return [target->view becomeFirstResponder] == YES;
    }

    void webview_host::snapshot(std::function<void(std::vector<snapshot_layer>)> done) const
    {
        struct pending
        {
            std::vector<snapshot_layer> layers {};
            std::size_t waiting { 0 };
            std::function<void(std::vector<snapshot_layer>)> done {};
        };
        auto state { std::make_shared<pending>() };
        state->done = std::move(done);
        std::vector<const entry*> shown {};
        for (const std::unique_ptr<entry>& target : entries_)
            if (target->view != nil && target->view.hidden == NO && target->layout.width > 0 && target->layout.height > 0)
                shown.push_back(target.get());
        state->waiting = shown.size();
        if (shown.empty())
        {
            state->done({});
            return;
        }
        for (const entry* const target : shown)
        {
            const snapshot_layer place { target->want.layer, target->layout.x, target->layout.y, target->layout.width, target->layout.height, nullptr };
            [target->view takeSnapshotWithConfiguration:nil
                                      completionHandler:^(UIImage* image, NSError* error) {
                                        static_cast<void>(error);
                                        snapshot_layer layer { place };
                                        if (image != nil)
                                            if (CGImageRef const picture { image.CGImage }; picture != nullptr)
                                                layer.image = (void*)CGImageRetain(picture);
                                        state->layers.push_back(layer);
                                        if (--state->waiting == 0)
                                            state->done(std::move(state->layers));
                                      }];
        }
    }

    void webview_host::tear_down(entry& target) noexcept
    {
        if (target.view == nil)
            return;
        // 늦게 오는 사건이 거둔 항목을 보지 않게 먼저 끊는다.
        if (LuilIosWebViewDelegate* const delegate { (LuilIosWebViewDelegate*)target.delegate }; delegate != nil)
        {
            delegate.entry = nullptr;
            delegate.deliver = nullptr;
        }
        LuilIosWebView* const view { (LuilIosWebView*)target.view };
        view.entry = nullptr;
        view.focusReporter = nullptr;
        [view.configuration.userContentController removeScriptMessageHandlerForName:bridge_name];
        [view stopLoading];
        view.navigationDelegate = nil;
        view.UIDelegate = nil;
        return_first_responder(view, skia_view_);
        [view removeFromSuperview];
        target.view = nil;
        target.delegate = nil;
    }

    void webview_host::shutdown() noexcept
    {
        *deliver_ = nullptr;
        *focus_reporter_ = nullptr;
        for (std::unique_ptr<entry>& target : entries_)
            tear_down(*target);
        entries_.clear();
    }
}

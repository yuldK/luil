#include "ios/file_dialog_ios.h"

#include "macos/macos_text.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

// 선택기의 delegate다. 선택기가 delegate를 약하게 쥐므로 선택기에 묶어 함께 산다 (`objc_setAssociatedObject`).
@interface LuilDocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@end

namespace luil::ios {
    namespace {
        // delegate를 선택기에 묶는 표식이다. 주소만 쓴다.
        char picker_delegate_key {};

        [[nodiscard]] NSArray<UTType*>* content_types(const std::vector<file_filter>& filters)
        {
            NSMutableArray<UTType*>* const types { [NSMutableArray array] };
            bool everything { filters.empty() };
            for (const file_filter& filter : filters)
                for (std::u8string extension : filter.extensions)
                {
                    while (extension.empty() == false && (extension.front() == u8'*' || extension.front() == u8'.'))
                        extension.erase(extension.begin());
                    if (extension.empty())
                    {
                        everything = true;
                        continue;
                    }
                    if (NSString* const text { macos::make_ns_string(extension) }; text != nil)
                        if (UTType* const type { [UTType typeWithFilenameExtension:text] }; type != nil)
                            [types addObject:type];
                }
            // 거르지 않으면 모든 파일이다 (`public.item`). 형식으로 옮기지 못한 확장자뿐이어도 그렇다.
            if (everything || types.count == 0)
                return @[ UTTypeItem ];
            return types;
        }

        [[nodiscard]] std::u8string path_of(NSURL* const url)
        {
            return url != nil && url.fileURL ? macos::from_ns_string(url.path) : std::u8string {};
        }
    }
}

@implementation LuilDocumentPickerDelegate {
    std::shared_ptr<std::function<void(luil::file_dialog_result)>> _finish;
    std::uint64_t _id;
    luil::file_dialog_kind _kind;
    std::u8string _name;
}

- (instancetype)initWithFinish:(std::shared_ptr<std::function<void(luil::file_dialog_result)>>)finish
                            id:(std::uint64_t)identifier
                          kind:(luil::file_dialog_kind)kind
                          name:(std::u8string)name
{
    self = [super init];
    if (self != nil)
    {
        _finish = std::move(finish);
        _id = identifier;
        _kind = kind;
        _name = std::move(name);
    }
    return self;
}

- (void)luilComplete:(luil::file_dialog_result)result
{
    // 선택기는 고름과 그만둠 가운데 하나만 부르지만, 두 번 불려도 답은 한 번이다.
    std::shared_ptr<std::function<void(luil::file_dialog_result)>> finish { std::exchange(_finish, nullptr) };
    if (finish != nullptr && *finish != nullptr)
        (*finish)(std::move(result));
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    static_cast<void>(controller);
    luil::file_dialog_result result {};
    result.id = _id;
    result.status = luil::file_dialog_status::chosen;
    const bool folder { _kind == luil::file_dialog_kind::folder || _kind == luil::file_dialog_kind::save };
    for (NSURL* const url in urls)
    {
        // 폴더는 원본이라 보안 범위를 열어 두고 닫지 않는다. 앱이 그 경로를 프로세스가 끝날 때까지 쓴다.
        if (folder)
            static_cast<void>([url startAccessingSecurityScopedResource]);
        std::u8string path { luil::ios::path_of(url) };
        if (path.empty())
            continue;
        if (_kind == luil::file_dialog_kind::save)
        {
            if (path.back() != u8'/')
                path.push_back(u8'/');
            path += _name.empty() ? std::u8string { u8"Untitled" } : _name;
        }
        result.paths.push_back(std::move(path));
        if (_kind != luil::file_dialog_kind::open_multiple)
            break;
    }
    if (result.paths.empty())
    {
        result.status = luil::file_dialog_status::failed;
        result.error = u8"The document picker returned no file path.";
    }
    [self luilComplete:std::move(result)];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    static_cast<void>(controller);
    luil::file_dialog_result result {};
    result.id = _id;
    result.status = luil::file_dialog_status::cancelled;
    [self luilComplete:std::move(result)];
}

@end

namespace luil::ios {
    void run_file_dialog(UIViewController* const presenter, const file_dialog_request& request, std::function<void(file_dialog_result)> done)
    {
        auto finish { std::make_shared<std::function<void(file_dialog_result)>>(std::move(done)) };
        const auto fail = [&finish, &request](std::u8string error) {
            file_dialog_result result {};
            result.id = request.id;
            result.status = file_dialog_status::failed;
            result.error = std::move(error);
            if (*finish != nullptr)
                (*finish)(std::move(result));
        };
        UIViewController* target { presenter };
        // 이미 무언가를 띄웠으면(다른 선택기, 공유 시트) 맨 위의 것에서 띄운다.
        while (target != nil && target.presentedViewController != nil)
            target = target.presentedViewController;
        if (target == nil)
        {
            fail(u8"There is no view controller to present the document picker from.");
            return;
        }

        const bool folder { request.kind == file_dialog_kind::folder || request.kind == file_dialog_kind::save };
        // 열기는 복사본이다 (보안 범위 없이 읽힌다). 폴더는 복사할 수 없어 원본이다.
        UIDocumentPickerViewController* const picker { folder ? [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]]
                                                              : [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:content_types(request.filters) asCopy:YES] };
        if (picker == nil)
        {
            fail(u8"Failed to create the document picker.");
            return;
        }
        picker.allowsMultipleSelection = request.kind == file_dialog_kind::open_multiple;
        if (NSString* const initial { macos::make_ns_string(request.initial_folder) }; initial != nil && initial.length > 0)
            picker.directoryURL = [NSURL fileURLWithPath:initial isDirectory:YES];
        LuilDocumentPickerDelegate* const delegate { [[LuilDocumentPickerDelegate alloc] initWithFinish:finish id:request.id kind:request.kind name:request.default_name] };
        picker.delegate = delegate;
        objc_setAssociatedObject(picker, &picker_delegate_key, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [target presentViewController:picker animated:YES completion:nil];
    }
}

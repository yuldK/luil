#include "macos/file_dialog_macos.h"

#include "macos/macos_text.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace luil::macos {
    namespace {
        // 거르개의 확장자들을 형식으로 옮긴다. 점과 별표는 뗀다 (`*.png`, `.png`, `png`를 같게 본다). `*`는 모두라 거르지 않는다.
        [[nodiscard]] NSArray<UTType*>* content_types(const std::vector<file_filter>& filters, bool& everything)
        {
            NSMutableArray<UTType*>* const types { [NSMutableArray array] };
            everything = filters.empty();
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
                    if (NSString* const text { make_ns_string(extension) }; text != nil)
                        if (UTType* const type { [UTType typeWithFilenameExtension:text] }; type != nil)
                            [types addObject:type];
                }
            return types;
        }

        void configure(NSSavePanel* const panel, const file_dialog_request& request)
        {
            if (NSString* const title { make_ns_string(request.title) }; title != nil && title.length > 0)
            {
                panel.title = title;
                panel.message = title;
            }
            if (NSString* const folder { make_ns_string(request.initial_folder) }; folder != nil && folder.length > 0)
                panel.directoryURL = [NSURL fileURLWithPath:folder isDirectory:YES];
            bool everything { false };
            NSArray<UTType*>* const types { content_types(request.filters, everything) };
            if (everything == false && types.count > 0)
                panel.allowedContentTypes = types;
            panel.canCreateDirectories = YES;
        }

        [[nodiscard]] std::u8string path_of(NSURL* const url)
        {
            return url != nil && url.fileURL ? from_ns_string(url.path) : std::u8string {};
        }
    }

    void run_file_dialog(NSWindow* const owner, const file_dialog_request& request, std::function<void(file_dialog_result)> done)
    {
        auto finish { std::make_shared<std::function<void(file_dialog_result)>>(std::move(done)) };
        const std::uint64_t id { request.id };
        NSSavePanel* panel { nil };
        if (request.kind == file_dialog_kind::save)
        {
            NSSavePanel* const save { [NSSavePanel savePanel] };
            if (NSString* const name { make_ns_string(request.default_name) }; name != nil && name.length > 0)
                save.nameFieldStringValue = name;
            panel = save;
        }
        else
        {
            NSOpenPanel* const open { [NSOpenPanel openPanel] };
            const bool folder { request.kind == file_dialog_kind::folder };
            open.canChooseFiles = folder == false;
            open.canChooseDirectories = folder;
            open.allowsMultipleSelection = request.kind == file_dialog_kind::open_multiple;
            open.resolvesAliases = YES;
            panel = open;
        }
        configure(panel, request);

        NSSavePanel* const shown { panel };
        void (^const complete)(NSModalResponse) = ^(NSModalResponse response) {
          file_dialog_result result {};
          result.id = id;
          if (response != NSModalResponseOK)
              result.status = file_dialog_status::cancelled;
          else
          {
              result.status = file_dialog_status::chosen;
              if ([shown isKindOfClass:[NSOpenPanel class]])
              {
                  for (NSURL* const url in ((NSOpenPanel*)shown).URLs)
                      if (std::u8string path { path_of(url) }; path.empty() == false)
                          result.paths.push_back(std::move(path));
              }
              else if (std::u8string path { path_of(shown.URL) }; path.empty() == false)
                  result.paths.push_back(std::move(path));
              if (result.paths.empty())
              {
                  result.status = file_dialog_status::failed;
                  result.error = u8"The panel returned no file path.";
              }
          }
          if (*finish != nullptr)
              (*finish)(std::move(result));
        };
        if (owner != nil && owner.visible)
            [panel beginSheetModalForWindow:owner completionHandler:complete];
        else
            [panel beginWithCompletionHandler:complete];
    }
}

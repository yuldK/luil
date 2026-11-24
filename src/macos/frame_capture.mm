#include "macos/frame_capture.h"

#include <ImageIO/ImageIO.h>

#include <cstdlib>
#include <cstring>

namespace luil::macos {
    namespace {
        CGImageRef latest { nullptr };
        bool readback_requested { false };

        [[nodiscard]] const char* capture_path()
        {
            static const char* const path { std::getenv("LUIL_MACOS_FRAME_CAPTURE") };
            return path != nullptr && *path != '\0' ? path : nullptr;
        }

        void write_png(CGImageRef const image, const char* const path)
        {
            CFURLRef const url { CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path), static_cast<CFIndex>(std::strlen(path)), false) };
            if (url == nullptr)
                return;
            if (CGImageDestinationRef const destination { CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr) }; destination != nullptr)
            {
                CGImageDestinationAddImage(destination, image, nullptr);
                static_cast<void>(CGImageDestinationFinalize(destination));
                CFRelease(destination);
            }
            CFRelease(url);
        }
    }

    void publish_frame(CGImageRef const image)
    {
        if (image == nullptr)
            return;
        CGImageRetain(image);
        if (latest != nullptr)
            CGImageRelease(latest);
        latest = image;
        readback_requested = false;
        if (const char* const path { capture_path() }; path != nullptr)
            write_png(image, path);
    }

    CGImageRef copy_latest_frame()
    {
        return latest != nullptr ? CGImageRetain(latest) : nullptr;
    }

    void request_frame_readback()
    {
        readback_requested = true;
    }

    bool frame_readback_wanted()
    {
        return readback_requested || capture_path() != nullptr;
    }
}

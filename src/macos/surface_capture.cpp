#include "macos/surface_capture.h"

#include "host/font_registry.h"
#include "luil/text/fonts.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurfaceProps.h"
#include "include/encode/SkWebpEncoder.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace luil::macos {
    namespace {
        // 마지막 장은 이을 것이 없어 직전 간격을 쓴다. 한 장뿐이면 한 frame 몫이다.
        constexpr int default_frame_milliseconds { 16 };

        // 무손실이다. 다시 볼 값이라 손실 압축은 글자 가장자리를 뭉갠다 (Win32와 같다).
        [[nodiscard]] SkWebpEncoder::Options lossless_webp_options() noexcept
        {
            SkWebpEncoder::Options options {};
            options.fCompression = SkWebpEncoder::Compression::kLossless;
            return options;
        }

        [[nodiscard]] bool write_binary_file(const std::u8string& path, const SkData& data, std::u8string& error)
        {
            std::FILE* const file { std::fopen(reinterpret_cast<const char*>(path.c_str()), "wb") };
            if (file == nullptr)
            {
                error = u8"Cannot open the output file.";
                return false;
            }
            const bool written { std::fwrite(data.data(), 1, data.size(), file) == data.size() };
            const bool closed { std::fclose(file) == 0 };
            if (written == false || closed == false)
            {
                error = u8"Cannot write the output file.";
                return false;
            }
            return true;
        }
    }

    bool draw_to_bitmap(const frame_state& state, SkBitmap& image, std::u8string& error)
    {
        if (state.width <= 0 || state.height <= 0)
        {
            error = u8"The surface has no area to draw.";
            return false;
        }
        if (image.tryAllocPixels(SkImageInfo::MakeN32Premul(state.width, state.height)) == false)
        {
            error = u8"Failed to allocate the capture bitmap.";
            return false;
        }
        // 렌더러가 부르는 것과 같은 인자다. codicon 자리에 다른 typeface를 주면 글리프로 그리는 것만 조용히 사라진다.
        const SkSurfaceProps properties { 0, kRGB_H_SkPixelGeometry };
        SkCanvas canvas { image, properties };
        const sk_sp<SkTypeface> codicon { load_codicon_typeface() };
        const sk_sp<SkTypeface> ui { configured_ui_typeface() };
        draw_frame(canvas, codicon.get(), ui.get(), state);
        return true;
    }

    bool write_capture(const frame_state& state, const std::u8string& path, std::u8string& error)
    {
        SkBitmap image {};
        if (draw_to_bitmap(state, image, error) == false)
            return false;
        SkDynamicMemoryWStream memory {};
        if (SkWebpEncoder::Encode(&memory, image.pixmap(), lossless_webp_options()) == false)
        {
            error = u8"Failed to encode the capture.";
            return false;
        }
        return write_binary_file(path, *memory.detachAsData(), error);
    }

    bool frame_recorder::start(const std::u8string& path, std::u8string& error)
    {
        if (recording_)
        {
            error = u8"The surface is already recording.";
            return false;
        }
        frames_.clear();
        path_ = path;
        width_ = 0;
        height_ = 0;
        dropped_ = 0;
        recording_ = true;
        return true;
    }

    void frame_recorder::collect(const frame_state& state)
    {
        if (recording_ == false)
            return;
        if (frames_.empty())
        {
            width_ = state.width;
            height_ = state.height;
        }
        else if (state.width != width_ || state.height != height_)
        {
            ++dropped_;
            return;
        }
        recorded_frame frame {};
        if (std::u8string error {}; draw_to_bitmap(state, frame.image, error) == false)
        {
            ++dropped_;
            return;
        }
        frame.time = std::chrono::steady_clock::now();
        frames_.push_back(std::move(frame));
    }

    bool frame_recorder::stop(std::u8string& error)
    {
        if (recording_ == false)
        {
            error = u8"The surface is not recording.";
            return false;
        }
        recording_ = false;
        const std::u8string path { std::exchange(path_, {}) };
        std::vector<recorded_frame> frames { std::exchange(frames_, {}) };
        const std::size_t dropped { std::exchange(dropped_, 0) };
        if (frames.empty())
        {
            error = u8"The recording has no frames.";
            return false;
        }
        std::vector<SkEncoder::Frame> encoded {};
        encoded.reserve(frames.size());
        for (std::size_t index { 0 }; index < frames.size(); ++index)
        {
            int duration { default_frame_milliseconds };
            if (index + 1 < frames.size())
                duration = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(frames[index + 1].time - frames[index].time).count());
            else if (frames.size() > 1)
                duration = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(frames.back().time - frames[frames.size() - 2].time).count());
            encoded.push_back(SkEncoder::Frame { frames[index].image.pixmap(), std::max(1, duration) });
        }
        SkDynamicMemoryWStream memory {};
        if (SkWebpEncoder::EncodeAnimated(&memory, encoded, lossless_webp_options()) == false)
        {
            error = u8"Failed to encode the recording.";
            return false;
        }
        if (write_binary_file(path, *memory.detachAsData(), error) == false)
            return false;
        if (dropped > 0)
        {
            error = u8"The recording was written, but frames were dropped because the window size changed.";
            return false;
        }
        return true;
    }
}

#include "ui/image_resample.h"

#include "luil/ui/image_decode.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace luil {
    namespace {
        constexpr int channels { 4 };
        constexpr int alpha_channel { 3 };

        // 출력 `row`번 줄을 쓰기 전에 묻는다. 첫 줄도 묻는다 — 작은 그림에서도 접는 자리가 하나는 선다.
        [[nodiscard]] bool asked_to_stop(const std::function<bool()>& cancelled, const int row)
        {
            return cancelled != nullptr && row % image_downscale_cancel_rows == 0 && cancelled();
        }

        // 그림 한 장의 8비트 작업 버퍼다. 던지지 않고 자리를 잡는다 (디코드의 다른 버퍼와 같은 규칙).
        struct byte_plane
        {
            int width { 0 };
            int height { 0 };
            std::unique_ptr<std::uint8_t[]> pixels {};

            [[nodiscard]] bool allocate(const int plane_width, const int plane_height)
            {
                width = plane_width;
                height = plane_height;
                pixels.reset(new (std::nothrow) std::uint8_t[static_cast<std::size_t>(plane_width) * static_cast<std::size_t>(plane_height) * channels]);
                return pixels != nullptr;
            }

            [[nodiscard]] std::size_t row_bytes() const noexcept
            {
                return static_cast<std::size_t>(width) * channels;
            }
        };

        // 읽기 전용 8비트 그림이다. 원본(pixmap)과 작업 버퍼를 같은 꼴로 본다.
        struct byte_view
        {
            const std::uint8_t* pixels { nullptr };
            std::size_t row_bytes { 0 };
            int width { 0 };
            int height { 0 };

            [[nodiscard]] const std::uint8_t* at(const int x, const int y) const noexcept
            {
                return pixels + static_cast<std::size_t>(y) * row_bytes + static_cast<std::size_t>(x) * channels;
            }
        };

        [[nodiscard]] byte_view view_of(const byte_plane& plane) noexcept
        {
            return { plane.pixels.get(), plane.row_bytes(), plane.width, plane.height };
        }

        // 축마다 따로 반으로 줄인다. 홀수 변의 마지막 한 줄은 버린다 — 1/변 만큼의 손실이고, 남은 Mitchell 단계가
        // 목표에 정확히 맞춘다.
        [[nodiscard]] image_resample_result halve(const byte_view& source, const bool across, const bool down, const std::function<bool()>& cancelled, byte_plane& output)
        {
            const int width { across ? source.width / 2 : source.width };
            const int height { down ? source.height / 2 : source.height };
            if (output.allocate(width, height) == false)
                return image_resample_result::out_of_memory;
            const int step_x { across ? 2 : 1 };
            const int step_y { down ? 2 : 1 };
            const int count { step_x * step_y };
            for (int y { 0 }; y < height; ++y)
            {
                if (asked_to_stop(cancelled, y))
                    return image_resample_result::cancelled;
                std::uint8_t* row { output.pixels.get() + static_cast<std::size_t>(y) * output.row_bytes() };
                for (int x { 0 }; x < width; ++x)
                    for (int channel { 0 }; channel < channels; ++channel)
                    {
                        int sum { 0 };
                        for (int dy { 0 }; dy < step_y; ++dy)
                            for (int dx { 0 }; dx < step_x; ++dx)
                                sum += source.at(x * step_x + dx, y * step_y + dy)[channel];
                        row[x * channels + channel] = static_cast<std::uint8_t>((sum + count / 2) / count);
                    }
            }
            return image_resample_result::done;
        }

        // Mitchell–Netravali 커널이다 (B = C = 1/3). 넓이 2의 받침이고 합이 1이다.
        [[nodiscard]] double mitchell(const double distance) noexcept
        {
            constexpr double b { 1.0 / 3.0 };
            constexpr double c { 1.0 / 3.0 };
            const double x { std::abs(distance) };
            if (x < 1.0)
                return ((12.0 - 9.0 * b - 6.0 * c) * x * x * x + (-18.0 + 12.0 * b + 6.0 * c) * x * x + (6.0 - 2.0 * b)) / 6.0;
            if (x < 2.0)
                return ((-b - 6.0 * c) * x * x * x + (6.0 * b + 30.0 * c) * x * x + (-12.0 * b - 48.0 * c) * x + (8.0 * b + 24.0 * c)) / 6.0;
            return 0.0;
        }

        // 출력 한 칸이 읽는 원본 구간과 가중치다. 가장자리에서는 그림 안에 든 가중치로 다시 나눈다.
        struct filter_taps
        {
            int first { 0 };
            std::vector<float> weights {};
        };

        [[nodiscard]] std::vector<filter_taps> taps_for(const int source_extent, const int target_extent)
        {
            const double ratio { static_cast<double>(source_extent) / static_cast<double>(target_extent) };
            // 줄이는 비율만큼 커널을 늘린다 — 줄일 때 받침이 좁으면 원본 픽셀을 건너뛰어 앨리어싱이 남는다.
            const double scale { std::max(1.0, ratio) };
            const double support { 2.0 * scale };
            std::vector<filter_taps> result(static_cast<std::size_t>(target_extent));
            for (int index { 0 }; index < target_extent; ++index)
            {
                const double center { (static_cast<double>(index) + 0.5) * ratio - 0.5 };
                const int first { std::max(0, static_cast<int>(std::ceil(center - support))) };
                const int last { std::min(source_extent - 1, static_cast<int>(std::floor(center + support))) };
                filter_taps& taps { result[static_cast<std::size_t>(index)] };
                taps.first = first;
                double total { 0.0 };
                for (int source_index { first }; source_index <= last; ++source_index)
                {
                    const double weight { mitchell((static_cast<double>(source_index) - center) / scale) };
                    taps.weights.push_back(static_cast<float>(weight));
                    total += weight;
                }
                if (total != 0.0)
                    for (float& weight : taps.weights)
                        weight = static_cast<float>(weight / total);
            }
            return result;
        }

        // 한 축의 필터 단계다. 입력·출력의 칸 형식(8비트·float)이 단계마다 달라 template이다.
        //  - `horizontal`이면 줄마다 가로로 걸고, 아니면 칸마다 세로로 건다. 출력 줄 수가 취소를 묻는 단위다.
        template<typename Read, typename Write>
        [[nodiscard]] image_resample_result filter_axis(
            const int width, const int height, const bool horizontal, const std::vector<filter_taps>& taps, Read read, Write write, const std::function<bool()>& cancelled)
        {
            for (int y { 0 }; y < height; ++y)
            {
                if (asked_to_stop(cancelled, y))
                    return image_resample_result::cancelled;
                for (int x { 0 }; x < width; ++x)
                {
                    const filter_taps& tap { taps[static_cast<std::size_t>(horizontal ? x : y)] };
                    float sum[channels] {};
                    for (std::size_t index { 0 }; index < tap.weights.size(); ++index)
                    {
                        const int source { tap.first + static_cast<int>(index) };
                        const float weight { tap.weights[index] };
                        for (int channel { 0 }; channel < channels; ++channel)
                            sum[channel] += weight * read(horizontal ? source : x, horizontal ? y : source, channel);
                    }
                    write(x, y, sum);
                }
            }
            return image_resample_result::done;
        }

        // 마지막 값을 8비트 premul로 접는다. 필터의 음의 꼬리로 넘친 값은 0–255로, 색은 알파 이하로 자른다.
        void store_premultiplied(std::uint8_t* const pixel, const float* const value) noexcept
        {
            const float alpha { std::clamp(std::round(value[alpha_channel]), 0.0f, 255.0f) };
            for (int channel { 0 }; channel < channels; ++channel)
            {
                const float clamped { std::clamp(std::round(value[channel]), 0.0f, 255.0f) };
                pixel[channel] = static_cast<std::uint8_t>(channel == alpha_channel ? alpha : std::min(clamped, alpha));
            }
        }
    } // namespace

    image_resample_result downscale_premultiplied(const SkPixmap& source, const SkISize target, const std::function<bool()>& cancelled, SkBitmap& output)
    {
        // 늘리지 않는다. 목표가 더 큰 축은 원본 그대로다.
        const int target_width { std::clamp(target.width(), 1, source.width()) };
        const int target_height { std::clamp(target.height(), 1, source.height()) };

        // 1. 반 평균. 두 축을 따로 본다 — 한 축만 많이 줄이는 그림(긴 띠)에서도 버퍼가 목표의 네 배를 넘지 않는다.
        byte_view current { static_cast<const std::uint8_t*>(source.addr()), source.rowBytes(), source.width(), source.height() };
        byte_plane halved {};
        while (current.width >= target_width * 2 || current.height >= target_height * 2)
        {
            byte_plane next {};
            if (const image_resample_result result { halve(current, current.width >= target_width * 2, current.height >= target_height * 2, cancelled, next) }; result != image_resample_result::done)
                return result;
            // 앞 단계의 버퍼는 지금 단계가 다 읽은 뒤에 놓는다.
            halved = std::move(next);
            current = view_of(halved);
        }

        if (output.tryAllocPixels(source.info().makeWH(target_width, target_height)) == false)
            return image_resample_result::out_of_memory;
        const auto output_at = [&output](const int x, const int y) { return static_cast<std::uint8_t*>(output.getAddr(x, y)); };
        const bool across { current.width != target_width };
        const bool down { current.height != target_height };
        const auto read_bytes = [&current](const int x, const int y, const int channel) { return static_cast<float>(current.at(x, y)[channel]); };

        // 2. Mitchell. 남은 비율이 두 배 미만이라 받침이 좁다.
        if (across == false && down == false)
        {
            for (int y { 0 }; y < target_height; ++y)
                std::copy_n(current.at(0, y), static_cast<std::size_t>(target_width) * channels, output_at(0, y));
            return image_resample_result::done;
        }
        if (across != down)
        {
            const std::vector<filter_taps> taps { taps_for(across ? current.width : current.height, across ? target_width : target_height) };
            return filter_axis(
                target_width, target_height, across, taps, read_bytes, [&output_at](const int x, const int y, const float* value) { store_premultiplied(output_at(x, y), value); }, cancelled);
        }
        // 두 축 모두다. 가로 다음 세로이고, 사이는 float 버퍼다 (목표 폭 × 지금 높이, 지금 높이는 목표의 두 배 미만이다).
        const std::size_t middle_count { static_cast<std::size_t>(target_width) * static_cast<std::size_t>(current.height) * channels };
        const std::unique_ptr<float[]> middle { new (std::nothrow) float[middle_count] };
        if (middle == nullptr)
            return image_resample_result::out_of_memory;
        const auto middle_at = [&middle, target_width](
                                   const int x, const int y) { return middle.get() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(target_width) + static_cast<std::size_t>(x)) * channels; };
        const std::vector<filter_taps> across_taps { taps_for(current.width, target_width) };
        const auto write_middle = [&middle_at](const int x, const int y, const float* value) { std::copy_n(value, channels, middle_at(x, y)); };
        if (const image_resample_result result { filter_axis(target_width, current.height, true, across_taps, read_bytes, write_middle, cancelled) }; result != image_resample_result::done)
            return result;
        const std::vector<filter_taps> down_taps { taps_for(current.height, target_height) };
        const auto read_middle = [&middle_at](const int x, const int y, const int channel) { return middle_at(x, y)[channel]; };
        return filter_axis(
            target_width, target_height, false, down_taps, read_middle, [&output_at](const int x, const int y, const float* value) { store_premultiplied(output_at(x, y), value); }, cancelled);
    }
} // namespace luil

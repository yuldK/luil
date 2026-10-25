#include "luil/ui/image_element.h"

#include "ui/image_backing.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTiledImageUtils.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace luil {
    struct image_frame_list
    {
        // 픽셀과 시간표를 **갈라** 든다.
        // 짝지은 구조체 하나를 벡터로 들면 `frame_durations()`가 시간만의 span을
        // 내주지 못한다 — 재생 판정이 픽셀을 보지 않는 것이 요점이고, 그래야 test가
        // 파일도 코덱도 없이 시간표를 손으로 지어 재생을 잠근다.
        std::vector<ui_image> images {};
        std::vector<std::chrono::milliseconds> durations {};
    };

    bool ui_image::valid() const noexcept
    {
        return backing_ != nullptr;
    }

    int ui_image::width() const noexcept
    {
        return width_;
    }

    int ui_image::height() const noexcept
    {
        return height_;
    }

    const image_backing* ui_image::backing() const noexcept
    {
        return backing_.get();
    }

    ui_image make_rgba_image(const int width, const int height, const std::span<const std::uint8_t> pixels)
    {
        if (width <= 0 || height <= 0)
            return {};
        const std::size_t expected { static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 };
        if (pixels.size() != expected)
            return {};

        // straight alpha 입력을 premul로 여기서 한 번 변환한다 (image-design.md).
        // `writePixels`가 형식 사이 변환을 맡는다.
        SkBitmap bitmap {};
        if (bitmap.tryAllocPixels(SkImageInfo::MakeN32Premul(width, height, SkColorSpace::MakeSRGB())) == false)
            return {};
        const SkImageInfo source_info { SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB()) };
        const SkPixmap source { source_info, pixels.data(), static_cast<std::size_t>(width) * 4 };
        if (bitmap.writePixels(source) == false)
            return {};
        bitmap.setImmutable();

        ui_image image {};
        image.backing_ = std::make_shared<const image_backing>(bitmap.asImage());
        image.width_ = width;
        image.height_ = height;
        return image;
    }

    namespace {
        // 범위 밖에서 내주는 빈 장이다.
        // `frame_image`가 참조를 답하는 계약이라 어딘가 서 있어야 한다 — 색인을 순수
        // 함수에서 받아 그대로 넘기는 자리라, 부르는 쪽이 범위를 다시 검사하지 않는다.
        const ui_image empty_frame {};
    } // namespace

    bool ui_animated_image::valid() const noexcept
    {
        return frames_ != nullptr;
    }

    int ui_animated_image::width() const noexcept
    {
        return width_;
    }

    int ui_animated_image::height() const noexcept
    {
        return height_;
    }

    std::size_t ui_animated_image::frame_count() const noexcept
    {
        return frames_ == nullptr ? 0 : frames_->images.size();
    }

    bool ui_animated_image::animated() const noexcept
    {
        // 한 장짜리도 **유효한** 값이라 `valid`와 갈린다 — 움직이지 않는 png를
        // 움직이는 문으로 열면 여기 선다.
        return frame_count() > 1;
    }

    const ui_image& ui_animated_image::frame_image(const std::size_t index) const noexcept
    {
        if (frames_ == nullptr || index >= frames_->images.size())
            return empty_frame;
        return frames_->images[index];
    }

    std::span<const std::chrono::milliseconds> ui_animated_image::frame_durations() const noexcept
    {
        if (frames_ == nullptr)
            return {};
        return frames_->durations;
    }

    int ui_animated_image::play_count() const noexcept
    {
        return play_count_;
    }

    ui_animated_image make_animated_image(const std::span<const ui_image> frames, const std::span<const std::chrono::milliseconds> durations, const int play_count)
    {
        // 장이 없거나 두 목록의 길이가 어긋나면 빈 값이다. 픽셀은 밖에서 오는 값이라
        // 죽지 않고 조용히 비운다 (`make_rgba_image`와 같은 성격이다).
        if (frames.empty() || frames.size() != durations.size())
            return {};
        const int width { frames.front().width() };
        const int height { frames.front().height() };
        if (width <= 0 || height <= 0)
            return {};
        for (const ui_image& frame : frames)
        {
            if (frame.valid() == false)
                return {};
            // 크기가 다른 장이 섞이면 한 값이 답하는 `width`·`height`가 거짓이 되고,
            // 그리기가 장마다 다른 자리에 앉아 그림이 떤다.
            if (frame.width() != width || frame.height() != height)
                return {};
        }

        auto list { std::make_shared<image_frame_list>() };
        list->images.assign(frames.begin(), frames.end());
        list->durations.reserve(durations.size());
        for (const std::chrono::milliseconds duration : durations)
        {
            // 너무 빠르게 적힌 장은 여기서 한 번 늦춘다. **판정이 아니라 값이 이 규칙을
            // 진다** — 관문이 여기 하나뿐이라야 `animation_frame_at`이 그 규칙을 다시
            // 알 필요가 없고, test가 코덱 없이 그 자리를 잠근다.
            list->durations.push_back(duration <= animation_frame_too_fast ? animation_frame_minimum : duration);
        }

        ui_animated_image image {};
        image.frames_ = std::move(list);
        image.width_ = width;
        image.height_ = height;
        // "0바퀴"는 그릴 것이 없다는 뜻이 되어 빈 값과 구별되지 않는다.
        image.play_count_ = play_count < 0 ? image_plays_forever : (play_count < 1 ? 1 : play_count);
        return image;
    }

    namespace {
        // 음수로 적힌 길이는 0으로 본다 — 시간이 뒤로 가면 구간이 뒤집힌다.
        // 파일에서 온 값은 `make_animated_image`가 이미 다듬어 실었고, 손으로 지은
        // 목록만 여기 걸린다.
        [[nodiscard]] std::chrono::nanoseconds frame_span(const std::chrono::milliseconds duration) noexcept
        {
            if (duration <= std::chrono::milliseconds { 0 })
                return std::chrono::nanoseconds { 0 };
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        }

        // 한 바퀴 안의 시각에 서는 장과, 그 장이 끝나는 (바퀴 안의) 시각이다.
        struct cycle_position
        {
            std::size_t index { 0 };
            std::chrono::nanoseconds ends_at { 0 };
        };

        // 장 i는 `[길이합(0..i), 길이합(0..i+1))`에 선다 — **반열린 구간이라 경계
        // 시각에는 다음 장이다.** 길이가 0인 장이 화면에 서지 않는 것도 이 부등호다.
        [[nodiscard]] cycle_position frame_in_cycle(const std::span<const std::chrono::milliseconds> durations, const std::chrono::nanoseconds phase) noexcept
        {
            std::chrono::nanoseconds end { 0 };
            for (std::size_t index { 0 }; index < durations.size(); ++index)
            {
                end += frame_span(durations[index]);
                if (end > phase)
                    return { index, end };
            }
            // `phase`가 한 바퀴 안이면 여기 닿지 않는다 — 부르는 쪽이 나머지 연산으로
            // 줄여 넣는다. 그래도 답을 비워 두지 않는다.
            return { durations.empty() ? 0 : durations.size() - 1, end };
        }
    } // namespace

    animation_frame animation_frame_at(const std::span<const std::chrono::milliseconds> frame_durations, const int play_count, const std::chrono::nanoseconds elapsed) noexcept
    {
        // 목록이 비었거나 **장이 하나뿐이면** 언제 물어도 0번이고 예고가 없다.
        // 한 장짜리를 여기서 자르는 것이 특히 중요하다 — 되풀이 표식만 붙은 한 장짜리
        // gif가 실제로 있고(wuffs가 NETSCAPE loop 0을 장 수와 무관하게 무한으로
        // 옮긴다), 그것을 "아직 안 끝났다"로 읽으면 창이 영원히 깨어난다.
        if (frame_durations.size() <= 1)
            return {};

        // 한 바퀴의 길이다. 0이면 어느 장도 끝나지 않아 시간을 재도 답이 없다.
        std::chrono::nanoseconds cycle { 0 };
        for (const std::chrono::milliseconds duration : frame_durations)
            cycle += frame_span(duration);
        if (cycle <= std::chrono::nanoseconds { 0 })
            return {};

        // 시작 시각이 아직 오지 않았으면(음수) 0번이다 (`transition`이 시작 전에
        // `from`인 것과 같다).
        const std::chrono::nanoseconds measured { elapsed > std::chrono::nanoseconds { 0 } ? elapsed : std::chrono::nanoseconds { 0 } };

        // 바퀴 수를 시간으로 옮긴다.
        //  - 곱셈이 넘치면 사실상 끝없이 도는 것이라 그렇게 본다. 넘친 값으로 "다
        //    돌았다"를 재면 뜨자마자 끝나 마지막 장에 굳는다.
        using clock_rep = std::chrono::nanoseconds::rep;
        bool forever { play_count < 0 };
        const clock_rep passes { play_count > 1 ? static_cast<clock_rep>(play_count) : 1 };
        if (forever == false && passes > std::numeric_limits<clock_rep>::max() / cycle.count())
            forever = true;
        const std::chrono::nanoseconds total { forever ? std::chrono::nanoseconds { 0 } : std::chrono::nanoseconds { cycle.count() * passes } };

        // 다 돌면 **마지막 장에 선다** — 0으로 돌아가면 끝난 재생이 첫 장면으로 튀어,
        // 화면에서 "한 바퀴 더 도는 중"과 구별되지 않는다.
        if (forever == false && measured >= total)
            return { frame_durations.size() - 1, std::nullopt };

        const std::chrono::nanoseconds phase { measured % cycle };
        // 이번 바퀴가 시작한 시각이다. 예고를 재생 시작으로부터의 **절대 시각**으로
        // 답하려고 든다 — 같은 frame에 몇 번을 물어도 같은 시각이 나오고 누적 오차가
        // 없다 (caret이 `now + (interval - phase)`로 답하는 것과 갈리는 자리다).
        const std::chrono::nanoseconds base { measured - phase };
        const cycle_position here { frame_in_cycle(frame_durations, phase) };
        const std::chrono::nanoseconds boundary { base + here.ends_at };

        // 마지막 바퀴의 마지막 경계는 예고하지 않는다 — 그 시각에 깨어나도 그림은
        // 그대로 마지막 장이라 헛도는 깨움이다.
        if (forever == false && boundary >= total)
            return { here.index, std::nullopt };

        // 그 경계에 정말로 **다른** 장이 서는가. 한 바퀴를 다 채운 경계는 다음 바퀴의
        // 첫 자리로 돌아가므로 나머지 연산으로 그 자리를 본다.
        //  - 손으로 지은 `{50ms, 0ms, 0ms}` 같은 목록에서만 같은 장으로 되돌아온다.
        //    거기서 예고하면 깨어나도 그림이 그대로다. 파일에서 온 값은
        //    `make_animated_image`가 전부 양수로 다듬어 실어 걸리지 않는다.
        if (frame_in_cycle(frame_durations, here.ends_at % cycle).index == here.index)
            return { here.index, std::nullopt };
        return { here.index, boundary };
    }

    lottie_frame lottie_frame_at(const std::chrono::nanoseconds duration, const double frame_rate, const int play_count, const std::chrono::nanoseconds elapsed) noexcept
    {
        using clock_rep = std::chrono::nanoseconds::rep;
        if (duration <= std::chrono::nanoseconds { 0 } || std::isfinite(frame_rate) == false || frame_rate <= 0.0)
            return {};
        // 한 장의 길이다. 1 ns보다 짧은 장은 뜻이 없어(그런 장 빠르기는 파일의 잘못이다)
        // 장면 하나로 본다.
        const long double period_nanoseconds { 1'000'000'000.0L / static_cast<long double>(frame_rate) };
        if (std::isfinite(period_nanoseconds) == false || period_nanoseconds < 1.0L)
            return {};
        const clock_rep cycle { duration.count() };
        const long double last_index { std::floor(static_cast<long double>(cycle - 1) / period_nanoseconds) };
        if (last_index <= 0.0L)
            return {};

        // 장의 간격을 먼저 반올림해 누적하면 30 fps가 한 바퀴에 서른한 장이 된다.
        // 장의 경계마다 올림하고, 되풀이의 경계는 파일의 길이 그대로 둔다.
        const auto frame_time = [cycle, period_nanoseconds](const long double index) {
            const long double boundary { std::ceil(index * period_nanoseconds) };
            return std::chrono::nanoseconds { boundary >= static_cast<long double>(cycle) ? cycle - 1 : static_cast<clock_rep>(boundary) };
        };
        const clock_rep measured { elapsed > std::chrono::nanoseconds { 0 } ? elapsed.count() : 0 };
        const clock_rep completed { measured / cycle };
        const bool forever { play_count < 0 };
        const clock_rep passes { play_count > 1 ? static_cast<clock_rep>(play_count) : 1 };
        // 나눗셈으로 바퀴 수를 비교한다. 길이와 바퀴 수를 곱해 넘칠 자리가 없다.
        if (forever == false && completed >= passes)
            return { frame_time(last_index), std::nullopt };

        const clock_rep phase { measured % cycle };
        const long double index { std::floor(static_cast<long double>(phase) / period_nanoseconds) };
        const std::chrono::nanoseconds time { frame_time(index) };
        if (forever == false && completed == passes - 1 && index >= last_index)
            return { time, std::nullopt };

        const long double next_boundary { std::ceil((index + 1.0L) * period_nanoseconds) };
        const clock_rep boundary { next_boundary >= static_cast<long double>(cycle) ? cycle : static_cast<clock_rep>(next_boundary) };
        // 아주 긴 재생에서도 예고는 반드시 지금 뒤이고, clock의 표현 범위를 넘지 않는다.
        const clock_rep offset { boundary > phase ? boundary : phase + 1 };
        const clock_rep base { measured - phase };
        if (base > std::numeric_limits<clock_rep>::max() - offset)
            return { time, std::nullopt };
        return { time, std::chrono::nanoseconds { base + offset } };
    }

    bool ui_lottie::valid() const noexcept
    {
        return backing_ != nullptr;
    }

    int ui_lottie::width() const noexcept
    {
        return width_;
    }

    int ui_lottie::height() const noexcept
    {
        return height_;
    }

    std::chrono::nanoseconds ui_lottie::duration() const noexcept
    {
        return duration_;
    }

    double ui_lottie::frame_rate() const noexcept
    {
        return frame_rate_;
    }

    int ui_lottie::play_count() const noexcept
    {
        return play_count_;
    }

    ui_lottie ui_lottie::with_play_count(const int play_count) const
    {
        ui_lottie copy { *this };
        copy.play_count_ = play_count == image_plays_forever || play_count >= 1 ? play_count : 1;
        return copy;
    }

    const lottie_backing* ui_lottie::backing() const noexcept
    {
        return backing_.get();
    }

    ui_image image_access::picture_image(sk_sp<SkPicture> picture, const int width, const int height)
    {
        if (picture == nullptr || width <= 0 || height <= 0)
            return {};
        ui_image image {};
        image.backing_ = std::make_shared<const image_backing>(nullptr, std::move(picture));
        image.width_ = width;
        image.height_ = height;
        return image;
    }

    ui_image image_access::raster_image(sk_sp<SkImage> raster)
    {
        if (raster == nullptr || raster->width() <= 0 || raster->height() <= 0)
            return {};
        ui_image image {};
        image.width_ = raster->width();
        image.height_ = raster->height();
        image.backing_ = std::make_shared<const image_backing>(std::move(raster));
        return image;
    }

    ui_lottie image_access::lottie(std::shared_ptr<const lottie_backing> backing, const int width, const int height, const std::chrono::nanoseconds duration, const double frame_rate)
    {
        if (backing == nullptr || width <= 0 || height <= 0 || duration <= std::chrono::nanoseconds { 0 } || std::isfinite(frame_rate) == false || frame_rate <= 0.0)
            return {};
        ui_lottie animation {};
        animation.backing_ = std::move(backing);
        animation.width_ = width;
        animation.height_ = height;
        animation.duration_ = duration;
        animation.frame_rate_ = frame_rate;
        return animation;
    }

    rect_f image_destination(const rect_f& bounds, const int width, const int height, const image_fit fit) noexcept
    {
        if (bounds.width <= 0.0f || bounds.height <= 0.0f || width <= 0 || height <= 0)
            return {};
        if (fit == image_fit::fill)
            return bounds;
        const float scale_x { bounds.width / static_cast<float>(width) };
        const float scale_y { bounds.height / static_cast<float>(height) };
        // contain은 작은 배율(전부 들어온다), cover는 큰 배율(전부 덮는다)이다.
        const float scale { fit == image_fit::contain ? (scale_x < scale_y ? scale_x : scale_y) : (scale_x > scale_y ? scale_x : scale_y) };
        const float destination_width { static_cast<float>(width) * scale };
        const float destination_height { static_cast<float>(height) * scale };
        return {
            bounds.x + (bounds.width - destination_width) / 2.0f,
            bounds.y + (bounds.height - destination_height) / 2.0f,
            destination_width,
            destination_height,
        };
    }

    rect_f image_source_rect(const rect_f& source, const int width, const int height) noexcept
    {
        // 빈 이미지는 빈 사각형이다 — 그릴 것이 없다는 답을 "전체"로 적을 수 없다.
        if (width <= 0 || height <= 0)
            return {};
        const rect_f whole { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
        // 비었거나 음수로 적힌 조각은 "전부"다 (`image_config::source`의 계약).
        if (source.width <= 0.0f || source.height <= 0.0f)
            return whole;

        // 걸친 조각은 이미지 안으로 자른다. 밖까지 그리면 그 자리가 조각의 일부인 양
        // 늘어나 그림이 어긋난다.
        const float left { source.x > 0.0f ? source.x : 0.0f };
        const float top { source.y > 0.0f ? source.y : 0.0f };
        const float right { source.x + source.width < whole.width ? source.x + source.width : whole.width };
        const float bottom { source.y + source.height < whole.height ? source.y + source.height : whole.height };
        // 통째로 밖이면 남는 것이 없다. 그때도 전체다 — 아무것도 그리지 않으면 앱은
        // 이미지가 없는 것인지 조각이 어긋난 것인지 화면에서 가릴 수 없다.
        if (right <= left || bottom <= top)
            return whole;
        return { left, top, right - left, bottom - top };
    }

    namespace {
        // luil의 표본 어휘를 Skia 값으로 옮기는 **유일한 자리**다 (`image_sampling`).
        // 공개 API에 Skia 타입을 늘리지 않으므로 이 표가 경계에 하나만 서고, 앱은
        // filter와 mipmap을 손으로 짝짓지 않는다.
        //  - 디코딩에서 줄일 때도 `smooth`와 같은 값을 쓴다 (`image_decode.cpp`의
        //    `drawImageRect`) — 디코딩에서 줄인 그림과 그리기에서 줄인 그림이 같아
        //    보이려면 두 자리의 표본이 같아야 한다.
        [[nodiscard]] SkSamplingOptions sampling_options_for(const image_sampling sampling) noexcept
        {
            // `sharp`는 mipmap도 끈다. mipmap이 곧 줄이면서 섞는 장치라, 섞지
            // 않겠다는 값과 짝지으면 뜻이 어긋난다.
            if (sampling == image_sampling::sharp)
                return SkSamplingOptions { SkFilterMode::kNearest, SkMipmapMode::kNone };
            // linear가 확대의 기본이고, 축소는 mipmap이 함께여야 성기게 뭉개지지
            // 않는다 (썸네일).
            return SkSamplingOptions { SkFilterMode::kLinear, SkMipmapMode::kLinear };
        }

        // 조각이 이미지 전체인가 — 표본 제약을 가르는 물음이다.
        // `image_source_rect`가 이미 이미지 안으로 다듬은 값이라 네 변만 본다.
        [[nodiscard]] bool covers_whole_image(const rect_f& source, const int width, const int height) noexcept
        {
            return source.x <= 0.0f && source.y <= 0.0f && source.width >= static_cast<float>(width) && source.height >= static_cast<float>(height);
        }

        // 조각의 한 변을 `image_destination`에 넣을 정수로 옮긴다.
        //
        // **자르지 않고 반올림한다.** 자르면 32.7픽셀짜리 조각이 32로 앉아 앱이 청한
        // 사각형에서 비율이 밀려나고, 그림은 뜨므로 눈으로는 "원래 저런 그림"과
        // 갈리지 않는다.
        //  - 그리고 **1 아래로 내려가지 않는다.** `image_source_rect`를 지난 조각은
        //    그릴 것이 있는 조각인데, 0.5픽셀짜리 변이 0으로 접히면
        //    `image_destination`이 빈 사각형을 답하고 그리기가 **아무 말 없이**
        //    돌아선다 — 화면에서 "빈 이미지"와 구별되지 않아 아무도 실패를 못 본다.
        //    `decoded_image_size`가 축소한 축을 0으로 만들지 않는 것과 같은 자리이자
        //    같은 이유다.
        //  - 뽑는 자리 자체는 소수점 그대로다. 반올림은 **칸에 맞추는 계산**에만
        //    걸리고, 그리기에 넘기는 조각은 앱이 적은 사각형 그대로다.
        [[nodiscard]] int source_extent(const float extent) noexcept
        {
            const int rounded { static_cast<int>(std::lround(extent)) };
            return rounded > 0 ? rounded : 1;
        }
    } // namespace

    image_element::image_element(ui_element_id id, image_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void image_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void image_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        // 그릴 것을 먼저 고른다. Lottie가 유효하면 그것이고, 아니면 움직이는 그림, 아니면
        // 정지 그림이다 (`image_config`). 어느 장·장면인지는 config의 시계와
        // `context.now`에서 매번 새로 나온다 — element는 재생 위치를 들지 않는다.
        const lottie_backing* scene { nullptr };
        double scene_seconds { 0.0 };
        const ui_image* picture { &config_.image };
        int width { 0 };
        int height { 0 };
        if (config_.lottie.valid())
        {
            const std::chrono::nanoseconds elapsed { playback_elapsed(config_.playback, context.now) };
            const lottie_frame frame { lottie_frame_at(config_.lottie.duration(), config_.lottie.frame_rate(), config_.lottie.play_count(), elapsed) };
            scene = config_.lottie.backing();
            scene_seconds = std::chrono::duration<double> { frame.time }.count();
            width = config_.lottie.width();
            height = config_.lottie.height();
        }
        else
        {
            if (config_.animation.valid())
            {
                const std::chrono::nanoseconds elapsed { playback_elapsed(config_.playback, context.now) };
                const animation_frame frame { animation_frame_at(config_.animation.frame_durations(), config_.animation.play_count(), elapsed) };
                picture = &config_.animation.frame_image(frame.index);
            }
            if (picture->valid() == false)
                return;
            width = picture->width();
            height = picture->height();
        }
        // 뽑아 쓸 조각을 먼저 정한다. 걸치거나 어긋난 값이 여기서 다 다듬어져,
        // 아래 계산은 언제나 이미지 안의 사각형 하나만 본다 (`image_source_rect`).
        const rect_f source { image_source_rect(config_.source, width, height) };
        if (source.width <= 0.0f || source.height <= 0.0f)
            return;
        // 장이 전부 같은 크기라 아래 인자가 장마다 흔들리지 않는다 — 그리기는 여느
        // `ui_image`와 똑같은 한 번의 그리기다.
        // **칸에 맞추는 것은 조각이지 이미지가 아니다.** 8:1 시트의 크기를 넣으면
        // 거기서 잘라 낸 정사각 스프라이트가 시트의 비율로 자리를 잡아, 엉뚱한 여백
        // 안에 앉는다 (`image_config::source`).
        //  - 변은 **반올림해서** 넣는다 (`source_extent`). 잘라 넣으면 소수점이 든
        //    조각이 청한 비율에서 밀려나고, 한 픽셀보다 좁은 조각은 0이 되어 그리기가
        //    아무 말 없이 돌아선다.
        const rect_f destination { image_destination(bounds(), source_extent(source.width), source_extent(source.height), config_.fit) };
        if (destination.width <= 0.0f || destination.height <= 0.0f)
            return;

        // cover만 자기 칸 밖으로 넘친다 — 넘친 만큼 잘라 그린다.
        // 자식이 아니라 자기 그림이라 `clip_children`이 아니다.
        const bool clip { config_.fit == image_fit::cover };
        if (clip)
        {
            context.canvas.save();
            // 잘린 가장자리도 그리기 가장자리와 같은 품질이어야 한다 — AA clip이다.
            context.canvas.clipRect(SkRect::MakeXYWH(bounds().x, bounds().y, bounds().width, bounds().height), true);
        }
        const SkPicture* const vector { scene == nullptr ? picture->backing()->picture.get() : nullptr };
        if (scene != nullptr || vector != nullptr)
        {
            // 벡터 그림이다 (SVG의 기록, Lottie의 장면). 픽셀을 뽑지 않고 그림 자신의
            // 좌표를 목적 사각형으로 옮겨 **이 배율로 다시 그린다** — 확대해도 날카롭다.
            //  - 조각 밖은 목적 사각형으로 자른다. 래스터의 strict 제약이 하는 일이고,
            //    벡터에서는 자르지 않으면 조각 밖의 그림이 그대로 칸에 그려진다.
            //  - 표본(`sampling`)은 뜻이 없다. 뽑을 픽셀이 없다.
            context.canvas.save();
            context.canvas.clipRect(SkRect::MakeXYWH(destination.x, destination.y, destination.width, destination.height), true);
            context.canvas.translate(destination.x, destination.y);
            context.canvas.scale(destination.width / source.width, destination.height / source.height);
            context.canvas.translate(-source.x, -source.y);
            if (scene != nullptr)
                scene->draw(context.canvas, scene_seconds, SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)));
            else
                context.canvas.drawPicture(vector);
            context.canvas.restore();
        }
        else
        {
            // 표본은 `sampling_options_for` 한 자리에서 나온다. 가장자리는 다른
            // 그리기(`solid_paint`)와 같이 AA다.
            SkPaint paint {};
            paint.setAntiAlias(true);
            // 조각이 이미지 전체면 fast다 — mipmap이 살아 있어 지금까지의 축소 품질이
            // 그대로다. 일부면 strict다 — 그러지 않으면 필터가 조각 밖 한두 줄까지 읽어
            // **옆 스프라이트가 가장자리에 번져 든다.**
            //  - 그 대가로 Skia는 strict에서 mipmap을 끈다. 많이 줄여 그린 조각은 그래서
            //    성기게 튀고, 답은 제약을 푸는 것이 아니라 **필요한 크기로 조각을 미리
            //    짓는 것**이다 (image_element.h의 `image_config::source`).
            const bool whole { covers_whole_image(source, width, height) };
            const SkCanvas::SrcRectConstraint constraint { whole ? SkCanvas::kFast_SrcRectConstraint : SkCanvas::kStrict_SrcRectConstraint };
            // GPU는 최대 텍스처(흔히 16384)보다 큰 그림을 한 장으로 올리지 못한다 — 긴 세로 그림이 흔한 자리다. 지금의
            // Ganesh는 그런 비트맵을 스스로 나누지만 스스로 나누지 않는 뒷단(Graphite)도 있어 명시 API로 부른다. 텍스처에
            // 드는 그림과 CPU 렌더러에서는 `drawImageRect` 그대로다 (image-frames-scaling-design.md).
            SkTiledImageUtils::DrawImageRect(&context.canvas, picture->backing()->image.get(), SkRect::MakeXYWH(source.x, source.y, source.width, source.height),
                SkRect::MakeXYWH(destination.x, destination.y, destination.width, destination.height), sampling_options_for(config_.sampling), &paint, constraint);
        }
        if (clip)
            context.canvas.restore();
    }

    std::optional<std::chrono::steady_clock::time_point> image_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        // 비는 자리가 넷이다 — 정지 그림(한 장짜리 포함)·시작 전·멈춤·다 돎. 넷 다
        // 시간이 흘러도 그림이 그대로라 창이 잠잔다.
        if (config_.playback.started.has_value() == false || config_.playback.paused_at.has_value())
            return std::nullopt;
        const std::chrono::nanoseconds elapsed { playback_elapsed(config_.playback, context.now) };
        // Lottie는 장 빠르기의 경계마다 깨운다 (`lottie_frame_at`). 그리기와 같은 함수라
        // 깨운 시각에는 반드시 다른 장면이다.
        std::optional<std::chrono::nanoseconds> next_at {};
        if (config_.lottie.valid())
            next_at = lottie_frame_at(config_.lottie.duration(), config_.lottie.frame_rate(), config_.lottie.play_count(), elapsed).next_at;
        else if (config_.animation.animated())
            next_at = animation_frame_at(config_.animation.frame_durations(), config_.animation.play_count(), elapsed).next_at;
        if (next_at.has_value() == false)
            return std::nullopt;
        // **절대 시각이다.** 그리기와 이 답이 같은 한 함수에서 나오므로 같은 frame에
        // 두 번 물어도 답이 같고, 예고한 시각에 깨어나면 반드시 다른 장이다.
        return *config_.playback.started + *next_at;
    }

    access_info image_element::accessibility() const
    {
        // 설명이 없으면 장식이다 — 접근 tree에서 접힌다 (HTML alt=""의 관례,
        // "이름 없는 단추는 소음"과 같은 문장).
        if (config_.description.empty())
            return {};
        return { .role = access_role::image, .name = config_.description };
    }
} // namespace luil

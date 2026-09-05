#include "luil/ui/image_element.h"

#include "raster_probe.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    // 배율 1이다. 이 축이 보는 것은 **어느 장이 섰는가**이고 그것은 색과 넓이로
    // 갈린다 — 자리를 전부 정수에 맞춰 두면 가장자리도 섞이지 않아 배율을 올릴
    // 이유가 없다 (획을 보는 raster_draw_tests와 갈리는 자리다).
    constexpr float raster_scale { 1.0f };

    constexpr luil::ui_element_kind kind_picture { luil::application_element_kind(0) };

    // 40×40 물리 픽셀 화면에 24×24 칸 하나다 (자리는 전부 물리 픽셀 — raster_probe.h의 규약).
    constexpr int film_frame_size { 40 };
    constexpr luil::rect_f film_slot { 8.0f, 8.0f, 24.0f, 24.0f };
    // 칸 안쪽만 센다. **넓이를 세는 것이 이 축의 요점이다** (raster_probe.h) —
    // 색 하나만 보면 "칸 한 귀퉁이만 칠했다"가 그대로 지나간다.
    constexpr luil::rect_f film_inner { 12.0f, 12.0f, 16.0f, 16.0f };
    constexpr int film_inner_pixels { 16 * 16 };

    constexpr luil::ui_color colour_red { 0xFFFF0000 };
    constexpr luil::ui_color colour_green { 0xFF00FF00 };
    constexpr luil::ui_color colour_blue { 0xFF0000FF };
    constexpr luil::ui_color colour_white { 0xFFFFFFFF };

    // 시각 하나다 (`transition_tests.cpp`의 `at`과 같은 자리).
    [[nodiscard]] constexpr std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 재생을 시작한 뒤로 흐른 시간이다 (`animation_frame_at`이 받는 값).
    [[nodiscard]] constexpr std::chrono::nanoseconds after(const int milliseconds)
    {
        return std::chrono::nanoseconds { std::chrono::milliseconds { milliseconds } };
    }

    // 100·200·300 ms짜리 장 셋이다 (한 바퀴 600 ms).
    // 길이를 전부 다르게 둔 이유가 있다 — 같은 길이면 경계를 하나 놓쳐도 색인이
    // 우연히 맞아떨어져 지나간다.
    constexpr std::array<std::chrono::milliseconds, 3> uneven_film { 100ms, 200ms, 300ms };

    // 색인을 int로 답한다 — 단언마다 형변환을 적지 않으려는 것뿐이다.
    [[nodiscard]] int frame_index(const std::span<const std::chrono::milliseconds> durations, const int play_count, const int milliseconds)
    {
        return static_cast<int>(luil::animation_frame_at(durations, play_count, after(milliseconds)).index);
    }

    [[nodiscard]] std::optional<std::chrono::nanoseconds> frame_boundary(const std::span<const std::chrono::milliseconds> durations, const int play_count, const int milliseconds)
    {
        return luil::animation_frame_at(durations, play_count, after(milliseconds)).next_at;
    }

    // 자식을 실제로 그리는 컨테이너다 (raster_draw_tests와 같은 이유 — 다른
    // 파일의 test_panel은 draw가 빈 몸통이라 픽셀 단언이 서지 않는다).
    class raster_group final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            draw_children(context, interaction);
        }
    };

    // 불투명 단색 이미지다. 어느 픽셀을 찍어도 같은 값이라 표본 자리에 매이지 않는다.
    [[nodiscard]] luil::ui_image make_solid_image(const int width, const int height, const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
    {
        std::vector<std::uint8_t> pixels {};
        pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        for (int index = 0; index < width * height; ++index)
        {
            pixels.push_back(red);
            pixels.push_back(green);
            pixels.push_back(blue);
            pixels.push_back(255);
        }
        return luil::make_rgba_image(width, height, pixels);
    }

    // 사분면 색이 다른 2×2 이미지다 (image_element_tests와 같은 견본).
    // 단색으로는 잡히지 않는 상하/좌우 뒤집힘이 색 자리로 드러난다.
    [[nodiscard]] luil::ui_image make_quadrant_image()
    {
        const std::vector<std::uint8_t> pixels { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
        return luil::make_rgba_image(2, 2, pixels);
    }

    // 빨강·초록·파랑이 100 ms씩 도는 필름이다.
    //
    // **파일도 코덱도 창도 없다.** `make_rgba_image`로 장을 짓고
    // `make_animated_image`로 묶는 것이 전부라, 이 축은 디코딩의 성패에 매이지
    // 않는다 (image_element.h가 그 문에 적어 둔 쓰임이 이것이다).
    [[nodiscard]] luil::ui_animated_image make_traffic_film(const int play_count = luil::image_plays_forever)
    {
        const std::array<luil::ui_image, 3> frames { make_solid_image(1, 1, 255, 0, 0), make_solid_image(1, 1, 0, 255, 0), make_solid_image(1, 1, 0, 0, 255) };
        constexpr std::array<std::chrono::milliseconds, 3> durations { 100ms, 100ms, 100ms };
        return luil::make_animated_image(frames, durations, play_count);
    }

    [[nodiscard]] luil::ui_tree build_tree(const luil::image_config& config, const luil::rect_f& slot)
    {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, static_cast<float>(film_frame_size), static_cast<float>(film_frame_size) }, raster_scale });
        auto picture { std::make_unique<luil::image_element>(luil::ui_element_id { kind_picture, u8"film" }, config) };
        picture->arrange({ slot, raster_scale });
        root->add(std::move(picture));
        return luil::ui_tree { std::move(root) };
    }
} // namespace

TEST_CASE("The playback clock measures from its start and holds while paused", "[ui][image][animation]")
{
    // 시작하지 않은 시계는 언제 물어도 0이다 — 첫 장에 서고 창을 깨우지 않는다.
    STATIC_REQUIRE(luil::playback_elapsed(luil::image_playback {}, at(1000)) == 0ms);

    constexpr luil::image_playback running { at(1000), std::nullopt };
    // 시작 시각이 아직 오지 않았으면 0이다 (`transition`이 시작 전에 `from`인 것과 같다).
    STATIC_REQUIRE(luil::playback_elapsed(running, at(900)) == 0ms);
    STATIC_REQUIRE(luil::playback_elapsed(running, at(1000)) == 0ms);
    STATIC_REQUIRE(luil::playback_elapsed(running, at(1300)) == 300ms);

    constexpr luil::image_playback paused { at(1000), at(1300) };
    // 멈춘 시각까지다. **시간이 더 흘러도 같은 값이라** 그림이 그 장에 얼어붙는다.
    STATIC_REQUIRE(luil::playback_elapsed(paused, at(1300)) == 300ms);
    STATIC_REQUIRE(luil::playback_elapsed(paused, at(9999)) == 300ms);
    // 멈춘 뒤에는 `now`가 시작보다 앞이어도 그 자리다 (재는 것이 `now`가 아니다).
    STATIC_REQUIRE(luil::playback_elapsed(paused, at(0)) == 300ms);

    // 같은 시각을 다시 물어도 같은 값이다 — 그 사이의 상태가 없다.
    STATIC_REQUIRE(luil::playback_elapsed(running, at(1300)) == luil::playback_elapsed(running, at(1300)));
}

TEST_CASE("Resuming a paused playback continues where it stopped", "[ui][image][animation]")
{
    // 300 ms 돌고 멈춘 시계다.
    constexpr luil::image_playback paused { at(1000), at(1300) };
    // 한참(7.7초) 뒤에 이어 간다.
    constexpr luil::image_playback resumed { luil::playback_resumed(paused, at(9000)) };
    STATIC_REQUIRE(resumed.paused_at.has_value() == false);
    // 멈춘 동안 흐른 시간만큼 시작 시각이 밀렸다. 이 한 줄이 "이어 가는 순간
    // 그만큼 건너뛴다"를 막는다 (`transition_to`와 같은 규칙이다).
    STATIC_REQUIRE(resumed.started == at(8700));
    STATIC_REQUIRE(luil::playback_elapsed(resumed, at(9000)) == 300ms);
    STATIC_REQUIRE(luil::playback_elapsed(resumed, at(9100)) == 400ms);

    // 시작하지 않았거나 멈춰 있지 않으면 그대로다.
    STATIC_REQUIRE(luil::playback_resumed(luil::image_playback {}, at(9000)).started.has_value() == false);
    constexpr luil::image_playback running { at(1000), std::nullopt };
    STATIC_REQUIRE(luil::playback_resumed(running, at(9000)).started == at(1000));
    STATIC_REQUIRE(luil::playback_resumed(running, at(9000)).paused_at.has_value() == false);
}

TEST_CASE("The visible frame follows the elapsed time", "[ui][image][animation]")
{
    const auto index_at = [](const int milliseconds) { return frame_index(uneven_film, luil::image_plays_forever, milliseconds); };

    REQUIRE(index_at(0) == 0);
    REQUIRE(index_at(99) == 0);
    // **경계 시각에는 다음 장이다** — 반열린 구간이라 길이가 0인 장은 서지 않는다.
    REQUIRE(index_at(100) == 1);
    REQUIRE(index_at(299) == 1);
    REQUIRE(index_at(300) == 2);
    REQUIRE(index_at(599) == 2);
    // 한 바퀴(600 ms)가 끝나면 처음으로 돌아온다.
    REQUIRE(index_at(600) == 0);
    REQUIRE(index_at(700) == 1);
    // 음수는 아직 시작하지 않은 것이라 첫 장이다.
    REQUIRE(index_at(-5000) == 0);
}

TEST_CASE("The next boundary is exactly when a different frame stands", "[ui][image][animation]")
{
    const auto boundary_at = [](const int milliseconds) { return frame_boundary(uneven_film, luil::image_plays_forever, milliseconds); };

    REQUIRE(boundary_at(0) == after(100));
    REQUIRE(boundary_at(99) == after(100));
    REQUIRE(boundary_at(100) == after(300));
    REQUIRE(boundary_at(550) == after(600));
    // 다음 바퀴로 넘어간 자리도 **재생 시작으로부터의 절대 시각**이다 — 같은
    // frame에 몇 번을 물어도 같은 답이라 누적 오차가 없다.
    REQUIRE(boundary_at(601) == after(700));

    // 이 묶음이 이 파일에서 가장 무거운 자리다.
    // 예고한 시각에 깨어나면 반드시 **다른** 장이어야 하고(아니면 창이 헛돈다),
    // 그 직전까지는 반드시 **같은** 장이어야 한다(아니면 장을 건너뛴다). 둘 다
    // 눈으로만 보이는 종류라, 두 단언이 함께 서야 경계가 이르지도 늦지도 않은
    // 것이 잠긴다.
    constexpr std::array<int, 4> counts { luil::image_plays_forever, 1, 2, 3 };
    for (const int plays : counts)
        for (int millisecond = 0; millisecond <= 1500; ++millisecond)
        {
            const luil::animation_frame standing { luil::animation_frame_at(uneven_film, plays, after(millisecond)) };
            if (standing.next_at.has_value() == false)
                continue;
            REQUIRE(luil::animation_frame_at(uneven_film, plays, *standing.next_at).index != standing.index);
            REQUIRE(luil::animation_frame_at(uneven_film, plays, *standing.next_at - 1ns).index == standing.index);
        }
}

TEST_CASE("A finite playback holds on the last frame and stops asking", "[ui][image][animation]")
{
    // 두 장이 두 바퀴 돈다 (한 바퀴 300 ms, 전부 600 ms).
    constexpr std::array<std::chrono::milliseconds, 2> film { 100ms, 200ms };
    const auto index_at = [&film](const int milliseconds) { return frame_index(film, 2, milliseconds); };
    const auto boundary_at = [&film](const int milliseconds) { return frame_boundary(film, 2, milliseconds); };

    // 첫 바퀴는 여느 재생과 같다.
    REQUIRE(index_at(0) == 0);
    REQUIRE(boundary_at(0) == after(100));
    REQUIRE(index_at(299) == 1);
    // 두 바퀴째의 첫 장으로 돌아온다.
    REQUIRE(index_at(300) == 0);
    REQUIRE(boundary_at(300) == after(400));

    // **마지막 바퀴의 마지막 경계는 예고하지 않는다.** 600 ms에 깨어나 보아도
    // 그림은 그대로 마지막 장이라, 그 예고는 헛도는 깨움이다.
    REQUIRE(index_at(400) == 1);
    REQUIRE(boundary_at(400).has_value() == false);
    REQUIRE(index_at(599) == 1);
    REQUIRE(boundary_at(599).has_value() == false);

    // 다 돌면 **마지막 장에 선다** — 0으로 돌아가면 끝난 재생이 첫 장면으로 튀어
    // 화면에서 "한 바퀴 더 도는 중"과 구별되지 않는다.
    REQUIRE(index_at(600) == 1);
    REQUIRE(boundary_at(600).has_value() == false);
    REQUIRE(index_at(10000) == 1);
    REQUIRE(boundary_at(10000).has_value() == false);
}

TEST_CASE("A single frame never changes and never wakes the window", "[ui][image][animation]")
{
    // 되풀이 표식만 붙은 한 장짜리 gif가 실제로 있다. wuffs는 NETSCAPE loop 0을
    // **장이 하나뿐이어도** "끝없이"로 옮기므로(SkWuffsCodec.cpp:873-883),
    // "다 돌지 않았으면 예고한다"만으로는 그 한 장이 창을 영영 깨운다.
    constexpr std::array<std::chrono::milliseconds, 1> still { 90ms };
    for (const int milliseconds : { -1000, 0, 45, 90, 91, 1000000 })
    {
        REQUIRE(frame_index(still, luil::image_plays_forever, milliseconds) == 0);
        REQUIRE(frame_boundary(still, luil::image_plays_forever, milliseconds).has_value() == false);
    }
    // 유한한 바퀴 수에서도 같다.
    REQUIRE(frame_boundary(still, 3, 45).has_value() == false);
}

TEST_CASE("A duration list that adds to nothing stands still", "[ui][image][animation]")
{
    // 어느 장도 끝나지 않아 시간을 재도 답이 없다.
    constexpr std::array<std::chrono::milliseconds, 2> motionless { 0ms, 0ms };
    REQUIRE(frame_index(motionless, luil::image_plays_forever, 500) == 0);
    REQUIRE(frame_boundary(motionless, luil::image_plays_forever, 500).has_value() == false);

    // 빈 목록도 같다 — 부르는 쪽이 장 수를 다시 검사하지 않게 한다.
    REQUIRE(frame_index({}, luil::image_plays_forever, 500) == 0);
    REQUIRE(frame_boundary({}, luil::image_plays_forever, 500).has_value() == false);
}

TEST_CASE("A boundary that lands on the same frame is not announced", "[ui][image][animation]")
{
    // 길이가 0인 장은 화면에 서지 않는다. 그래서 이 필름은 장이 셋이지만 보이는
    // 것은 0번뿐이고, 한 바퀴의 경계마다 **다시 0번**이 선다 — 경계를 그대로
    // 예고하면 그림이 그대로인 채 창만 50 ms마다 깨어난다.
    //  - 파일에서 온 값은 `make_animated_image`가 전부 양수로 다듬어 싣는다.
    //    손으로 지은 이 span이 그 자리의 **유일한 그물**이다.
    constexpr std::array<std::chrono::milliseconds, 3> blinkless { 50ms, 0ms, 0ms };
    for (const int milliseconds : { 0, 49, 50, 125, 10000 })
    {
        REQUIRE(frame_index(blinkless, luil::image_plays_forever, milliseconds) == 0);
        REQUIRE(frame_boundary(blinkless, luil::image_plays_forever, milliseconds).has_value() == false);
    }
    // 유한한 바퀴 수에서도 같다 — 되풀이 여부와 무관한 자리다.
    REQUIRE(frame_boundary(blinkless, 2, 20).has_value() == false);
}

TEST_CASE("Frames written as too fast are slowed at the gate", "[ui][image][animation]")
{
    const std::array<luil::ui_image, 3> frames { make_solid_image(1, 1, 255, 0, 0), make_solid_image(1, 1, 0, 255, 0), make_solid_image(1, 1, 0, 0, 255) };
    constexpr std::array<std::chrono::milliseconds, 3> written { 0ms, 10ms, 20ms };
    const luil::ui_animated_image film { luil::make_animated_image(frames, written, luil::image_plays_forever) };
    REQUIRE(film.valid());

    const std::span<const std::chrono::milliseconds> durations { film.frame_durations() };
    REQUIRE(durations.size() == 3);
    // 0 ms는 "가능한 한 빨리"라는 뜻이라 문턱 값으로 올린다.
    REQUIRE(durations[0] == luil::animation_frame_minimum);
    // 문턱과 **같은** 값도 올린다 (규칙이 "이하"다).
    REQUIRE(durations[1] == luil::animation_frame_minimum);
    // **짧지만 문턱을 넘는 값은 손대지 않는다** — 파일이 정말로 빠르게 돌라고 적은
    // 값이고, 올려 잡으면 멀쩡한 그림이 느려진다.
    REQUIRE(durations[2] == 20ms);

    // 판정이 아니라 값이 이 규칙을 진다 — `animation_frame_at`은 이미 다듬어진
    // 목록만 보므로 그 규칙을 다시 알 필요가 없다.
    REQUIRE(frame_index(durations, film.play_count(), 150) == 1);
}

TEST_CASE("An animated image refuses frames that do not agree", "[ui][image][animation]")
{
    const luil::ui_image red { make_solid_image(2, 2, 255, 0, 0) };
    const luil::ui_image green { make_solid_image(2, 2, 0, 255, 0) };
    const std::array<luil::ui_image, 2> two { red, green };
    constexpr std::array<std::chrono::milliseconds, 2> pair { 100ms, 100ms };

    // 장이 없다.
    REQUIRE(luil::make_animated_image({}, {}, luil::image_plays_forever).valid() == false);
    // 두 span의 길이가 다르다.
    constexpr std::array<std::chrono::milliseconds, 1> one { 100ms };
    REQUIRE(luil::make_animated_image(two, one, 1).valid() == false);
    // 빈 이미지가 섞였다.
    const std::array<luil::ui_image, 2> holed { red, luil::ui_image {} };
    REQUIRE(luil::make_animated_image(holed, pair, 1).valid() == false);
    // **크기가 다른 장이 섞였다.** 섞이면 한 값이 답하는 `width`·`height`가 거짓이
    // 되고, 그리기가 장마다 다른 자리에 앉아 그림이 떤다.
    const std::array<luil::ui_image, 2> mixed { red, make_solid_image(3, 3, 0, 0, 255) };
    REQUIRE(luil::make_animated_image(mixed, pair, 1).valid() == false);

    // 온전한 것은 선다.
    const luil::ui_animated_image film { luil::make_animated_image(two, pair, 1) };
    REQUIRE(film.valid());
    REQUIRE(film.width() == 2);
    REQUIRE(film.height() == 2);
    REQUIRE(film.frame_count() == 2);
    REQUIRE(film.animated());

    // "0바퀴"는 그릴 것이 없다는 뜻이 되어 빈 값과 구별되지 않는다 — 한 바퀴로 본다.
    REQUIRE(luil::make_animated_image(two, pair, 0).play_count() == 1);
    REQUIRE(luil::make_animated_image(two, pair, 5).play_count() == 5);
    // 음수는 전부 "끝없이" 하나로 모인다.
    REQUIRE(luil::make_animated_image(two, pair, -7).play_count() == luil::image_plays_forever);
}

TEST_CASE("A one frame animation is valid but not animated", "[ui][image][animation]")
{
    const std::array<luil::ui_image, 1> frames { make_solid_image(2, 2, 255, 0, 0) };
    constexpr std::array<std::chrono::milliseconds, 1> durations { 90ms };
    const luil::ui_animated_image film { luil::make_animated_image(frames, durations, luil::image_plays_forever) };

    // 움직이지 않는 png를 움직이는 문으로 열면 여기 선다 — 실패가 아니다.
    REQUIRE(film.valid());
    REQUIRE(film.animated() == false);
    REQUIRE(film.frame_count() == 1);
    REQUIRE(film.frame_image(0).valid());
    // 범위 밖은 빈 이미지다 — 색인을 순수 함수에서 받아 그대로 넘기는 자리라
    // 부르는 쪽이 범위를 다시 검사하지 않는다.
    REQUIRE(film.frame_image(5).valid() == false);

    // 빈 값과는 갈린다.
    REQUIRE(luil::ui_animated_image {}.valid() == false);
    REQUIRE(luil::ui_animated_image {}.animated() == false);
    REQUIRE(luil::ui_animated_image {}.frame_count() == 0);
    REQUIRE(luil::ui_animated_image {}.frame_durations().empty());
}

TEST_CASE("The element draws the frame that belongs to the moment", "[ui][image][animation][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::image_config config {};
    config.fit = luil::image_fit::fill;
    config.animation = make_traffic_film();
    config.playback.started = at(0);
    REQUIRE(config.animation.valid());
    const luil::ui_tree tree { build_tree(config, film_slot) };

    const auto count_at = [&tree, &palette](const int milliseconds, const luil::ui_color colour) {
        luil::testing::raster_frame frame { film_frame_size, film_frame_size, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {}, at(milliseconds));
        return frame.count_color(film_inner, colour);
    };

    // 100 ms마다 다음 장이다.
    REQUIRE(count_at(50, colour_red) == film_inner_pixels);
    REQUIRE(count_at(150, colour_green) == film_inner_pixels);
    REQUIRE(count_at(250, colour_blue) == film_inner_pixels);
    // 끝없이 도는 필름은 한 바퀴 뒤에 처음으로 돌아온다.
    REQUIRE(count_at(350, colour_red) == film_inner_pixels);
    // 그리고 그 시각에 다른 장은 한 점도 서지 않는다 — 넓이만 보면 두 장이 겹쳐
    // 그려진 결함이 지나간다.
    REQUIRE(count_at(150, colour_red) == 0);
    REQUIRE(count_at(150, colour_blue) == 0);
}

TEST_CASE("A playback that has not started shows the first frame", "[ui][image][animation][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::image_config config {};
    config.fit = luil::image_fit::fill;
    config.animation = make_traffic_film();
    // `started`가 비어 있다 — 값을 채우는 것이 곧 시작이다.
    const luil::ui_tree tree { build_tree(config, film_slot) };

    // 시계가 아무리 흘러도 첫 장이다.
    for (const int milliseconds : { 0, 150, 250, 100000 })
    {
        luil::testing::raster_frame frame { film_frame_size, film_frame_size, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {}, at(milliseconds));
        REQUIRE(frame.count_color(film_inner, colour_red) == film_inner_pixels);
    }

    // 다시 그릴 것도 예고하지 않는다.
    const luil::image_element picture { luil::ui_element_id { kind_picture, u8"film" }, config };
    REQUIRE(picture.next_update({ at(150) }, {}).has_value() == false);
}

TEST_CASE("A paused playback freezes on its frame", "[ui][image][animation][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::image_config config {};
    config.fit = luil::image_fit::fill;
    config.animation = make_traffic_film();
    config.playback.started = at(0);
    // 두 번째 장(초록)에서 멈췄다.
    config.playback.paused_at = at(150);
    const luil::ui_tree tree { build_tree(config, film_slot) };

    // 1초를 더 밀어도 그 장이다.
    for (const int milliseconds : { 150, 250, 1150 })
    {
        luil::testing::raster_frame frame { film_frame_size, film_frame_size, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {}, at(milliseconds));
        REQUIRE(frame.count_color(film_inner, colour_green) == film_inner_pixels);
    }

    const luil::image_element picture { luil::ui_element_id { kind_picture, u8"film" }, config };
    REQUIRE(picture.next_update({ at(1150) }, {}).has_value() == false);
}

TEST_CASE("The animation wins over the still image", "[ui][image][animation][raster]")
{
    // 파일을 열어 보기 전에는 그것이 움직이는지 알 수 없다. 앱은 디코더가 돌려준
    // 것을 그대로 놓기만 하고 element 종류를 갈라 쥐지 않으므로, 둘이 함께 실린
    // 자리의 답이 하나로 정해져 있어야 한다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::image_config config {};
    config.image = make_solid_image(1, 1, 255, 255, 255);
    config.fit = luil::image_fit::fill;
    config.animation = make_traffic_film();
    config.playback.started = at(0);
    const luil::ui_tree tree { build_tree(config, film_slot) };

    luil::testing::raster_frame frame { film_frame_size, film_frame_size, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {}, at(50));
    REQUIRE(frame.count_color(film_inner, colour_red) == film_inner_pixels);
    REQUIRE(frame.count_color(film_inner, colour_white) == 0);
}

TEST_CASE("The element wakes exactly at the next frame boundary", "[ui][image][animation][update]")
{
    luil::image_config config {};
    config.animation = make_traffic_film();
    config.playback.started = at(1000);
    const luil::image_element picture { luil::ui_element_id { kind_picture, u8"film" }, config };

    // 예고는 **절대 시각**이다 (`started + next_at`) — 같은 frame에 두 번 물어도
    // 같은 답이라 누적 오차가 없다.
    REQUIRE(picture.next_update({ at(1000) }, {}) == at(1100));
    REQUIRE(picture.next_update({ at(1050) }, {}) == at(1100));
    // 그 시각에 다시 물으면 그다음 경계다. **"지금"을 답하지 않는다** — 회전
    // 표시와 갈리는 자리이고, 100 ms짜리 필름은 초당 열 번만 창을 깨운다.
    REQUIRE(picture.next_update({ at(1100) }, {}) == at(1200));
    REQUIRE(picture.next_update({ at(1050) }, {}) == picture.next_update({ at(1050) }, {}));
}

TEST_CASE("A still image and a finished animation let the window sleep", "[ui][image][animation][update]")
{
    const luil::interaction_snapshot idle {};

    SECTION("정지 그림은 시간이 흘러도 그대로다")
    {
        luil::image_config config {};
        config.image = make_solid_image(1, 1, 255, 0, 0);
        const luil::image_element picture { luil::ui_element_id { kind_picture, u8"still" }, config };
        REQUIRE(picture.next_update({ at(5000) }, idle).has_value() == false);
    }

    SECTION("한 장짜리 필름은 유효하지만 움직이지 않는다")
    {
        const std::array<luil::ui_image, 1> frames { make_solid_image(1, 1, 255, 0, 0) };
        constexpr std::array<std::chrono::milliseconds, 1> durations { 90ms };
        luil::image_config config {};
        config.animation = luil::make_animated_image(frames, durations, luil::image_plays_forever);
        config.playback.started = at(0);
        REQUIRE(config.animation.valid());
        const luil::image_element picture { luil::ui_element_id { kind_picture, u8"one" }, config };
        REQUIRE(picture.next_update({ at(5000) }, idle).has_value() == false);
    }

    SECTION("시작하지 않은 재생은 첫 장에 서 있을 뿐이다")
    {
        luil::image_config config {};
        config.animation = make_traffic_film();
        const luil::image_element picture { luil::ui_element_id { kind_picture, u8"idle" }, config };
        REQUIRE(picture.next_update({ at(5000) }, idle).has_value() == false);
    }

    SECTION("멈춘 재생은 깨울 이유가 없다")
    {
        luil::image_config config {};
        config.animation = make_traffic_film();
        config.playback.started = at(0);
        config.playback.paused_at = at(150);
        const luil::image_element picture { luil::ui_element_id { kind_picture, u8"paused" }, config };
        REQUIRE(picture.next_update({ at(5000) }, idle).has_value() == false);
    }

    SECTION("다 돈 재생은 마지막 장에 선 채 잠잔다")
    {
        // 한 바퀴만 도는 300 ms짜리다.
        luil::image_config config {};
        config.animation = make_traffic_film(1);
        config.playback.started = at(1000);
        const luil::image_element picture { luil::ui_element_id { kind_picture, u8"done" }, config };
        // 아직 마지막 장을 보는 중이어도 예고가 없다 — 마지막 경계에 깨어나 보아야
        // 그림이 그대로다.
        REQUIRE(picture.next_update({ at(1250) }, idle).has_value() == false);
        REQUIRE(picture.next_update({ at(1300) }, idle).has_value() == false);
        REQUIRE(picture.next_update({ at(9000) }, idle).has_value() == false);
        // 그 앞에서는 여느 재생과 같이 예고한다 — 위의 넷이 "언제나 nullopt"인
        // 구현으로도 지나가지 않게 한다.
        REQUIRE(picture.next_update({ at(1050) }, idle) == at(1100));
    }
}

TEST_CASE("The animated element draws through the same fit as a still image", "[ui][image][animation][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 사분면 색이 다른 2×2다 — 단색으로는 자리가 어긋나도 같은 그림으로 보인다.
    const luil::ui_image quadrant { make_quadrant_image() };
    const std::array<luil::ui_image, 2> frames { quadrant, make_solid_image(2, 2, 0, 0, 0) };
    constexpr std::array<std::chrono::milliseconds, 2> durations { 100ms, 100ms };
    // 세로로 긴 칸에서 cover는 좌우로 넘쳐 잘린다 — 자리 계산이 흔들리면 픽셀이 갈린다.
    const luil::rect_f tall { 10.0f, 4.0f, 12.0f, 32.0f };

    luil::image_config still_config {};
    still_config.image = quadrant;
    still_config.fit = luil::image_fit::cover;
    const luil::ui_tree still_tree { build_tree(still_config, tall) };
    luil::testing::raster_frame still_frame { film_frame_size, film_frame_size, palette, raster_scale };
    still_frame.draw(still_tree, luil::interaction_snapshot {}, at(50));

    luil::image_config moving_config {};
    moving_config.fit = luil::image_fit::cover;
    moving_config.animation = luil::make_animated_image(frames, durations, luil::image_plays_forever);
    moving_config.playback.started = at(0);
    const luil::ui_tree moving_tree { build_tree(moving_config, tall) };
    luil::testing::raster_frame moving_frame { film_frame_size, film_frame_size, palette, raster_scale };
    moving_frame.draw(moving_tree, luil::interaction_snapshot {}, at(50));

    // 장이 전부 같은 크기라 `image_destination`의 인자가 흔들리지 않는다 — 그래서
    // 움직이는 쪽이 정지 쪽과 픽셀 하나까지 같아야 한다. 다르면 장을 고르는 자리가
    // 크기를 정지 이미지에서 읽고 있다는 뜻이다.
    int different { 0 };
    for (int y = 0; y < film_frame_size; ++y)
        for (int x = 0; x < film_frame_size; ++x)
            if (still_frame.pixel_at(x, y) != moving_frame.pixel_at(x, y))
                ++different;
    REQUIRE(different == 0);
    // 그리고 실제로 무엇인가 그려졌다 — 둘 다 배경뿐이면 위의 단언이 거저 지나간다.
    REQUIRE(moving_frame.count_color(tall, palette.window_background) == 0);
}

#pragma once

#include "luil/ui/ui_element.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace luil {
    // Skia 포장의 실체다 (`image_element.cpp`에만 산다).
    // 공개 API는 Skia 타입을 더 늘리지 않는다.
    struct image_backing;
    // 움직이는 그림의 장 목록과 시간표다 (같은 자리, 같은 이유).
    struct image_frame_list;

    class ui_image;
    class ui_animated_image;

    // straight alpha RGBA 8888 픽셀로 이미지를 만든다 (행은 빈틈없이 width×4 byte).
    // premul 변환과 Skia 포장을 여기서 한 번 마친다 — 그리기는 매번 값을 다듬지
    // 않는다 (image-design.md).
    // 크기가 맞지 않으면 빈 이미지다 — 픽셀은 밖(파일·네트워크)에서 오는 값이라
    // 죽지 않고 조용히 비운다 (`drawable_text`가 깨진 UTF-8을 거르는 것과 같은
    // 성격이다).
    [[nodiscard]] ui_image make_rgba_image(int width, int height, std::span<const std::uint8_t> pixels);

    // 불변 픽셀 이미지다.
    // 만들어진 뒤 바뀌지 않아 logic thread가 만들고 UI thread가 그려도 새
    // 동기화가 없다 (게시된 tree와 같은 계약). 복사는 참조 공유라 싸다 —
    // frame마다 config에 실린다.
    class ui_image
    {
    public:
        ui_image() = default;

        [[nodiscard]] bool valid() const noexcept;
        // 이미지 자체의 픽셀 크기다 (화면 배율과 무관하다).
        // 담는 쪽이 칸을 잡을 때 쓴다 — 배치에 측정 단계가 없어서다.
        [[nodiscard]] int width() const noexcept;
        [[nodiscard]] int height() const noexcept;
        // 라이브러리 그리기가 읽는 손잡이다 — 앱이 쓸 일은 없다.
        [[nodiscard]] const image_backing* backing() const noexcept;

    private:
        friend ui_image make_rgba_image(int width, int height, std::span<const std::uint8_t> pixels);

        std::shared_ptr<const image_backing> backing_ {};
        int width_ { 0 };
        int height_ { 0 };
    };

    // 끝없이 도는 재생이다 (`ui_animated_image::play_count`의 특별값).
    //
    // 형식이 나르는 수는 "첫 바퀴 뒤에 더 도는 횟수"라 0이 "한 번 돈다"이고
    // 음수가 "끝없이"다. 그 규약을 그대로 내보내면 0을 "돌지 않는다"로 읽는
    // 자리가 반드시 생긴다 — 그래서 공개 어휘는 **바퀴 수**이고, 형식의 수를
    // 바퀴 수로 옮기는 것은 디코딩의 몫이다.
    inline constexpr int image_plays_forever { -1 };

    // 이보다 짧게 적힌 장은 "가능한 한 빨리"라는 뜻으로 읽는다.
    //
    // 형식은 0 ms를 실을 수 있고 실제로 그런 gif가 흔하다 — 오래된 저작 도구가
    // "가능한 한 빨리"를 그렇게 적었다. 그대로 두면 다음 경계가 늘 "지금"이라
    // 창이 쉬지 않고 돈다. 웹 브라우저들이 십수 년 같은 값으로 걸러 온 자리라
    // 같은 규칙을 쓴다.
    inline constexpr std::chrono::milliseconds animation_frame_too_fast { 10 };
    // 그때 대신 쓰는 길이다.
    //  - 20 ms처럼 **짧지만 이 문턱을 넘는** 값은 손대지 않는다. 그것은 파일이
    //    정말로 빠르게 돌라고 적은 값이고, 올려 잡으면 멀쩡한 그림이 느려진다.
    inline constexpr std::chrono::milliseconds animation_frame_minimum { 100 };

    // 이미 만들어진 장들과 각 장의 표시 시간으로 움직이는 그림을 만든다.
    //
    // 디코더가 쓰는 문이자 **코덱 없이 값을 짓는 유일한 길**이다 — 계산으로
    // 만든 장도 여기로 들어오고, test가 파일도 코덱도 없이 재생과 그리기를
    // 통째로 검산한다 (`make_rgba_image`가 원시 픽셀에 낸 그 자리다).
    //
    // 다음 중 하나라도 어긋나면 빈 값이다. 픽셀은 밖에서 오는 값이라 죽지 않고
    // 조용히 비운다 (`make_rgba_image`와 같은 성격이다).
    //  - 장이 없다, 두 span의 길이가 다르다, 빈 이미지가 섞였다.
    //  - **크기가 다른 장이 섞였다.** 섞이면 한 값이 답하는 `width`·`height`가
    //    거짓이 되고, 그리기가 장마다 다른 자리에 앉아 그림이 떤다.
    //
    // 다듬어 싣는 것이 둘이다.
    //  - `animation_frame_too_fast` 이하로 적힌 표시 시간은
    //    `animation_frame_minimum`으로 올린다. **판정이 아니라 값이 이 규칙을
    //    진다** — 관문이 여기 하나뿐이라야 재생 판정이 그 규칙을 다시 알 필요가
    //    없고, test가 코덱 없이 그 자리를 잠근다.
    //  - `play_count`가 1보다 작으면 (`image_plays_forever`가 아닌 한) 1로 본다 —
    //    "0바퀴"는 그릴 것이 없다는 뜻이 되어 빈 값과 구별되지 않는다.
    [[nodiscard]] ui_animated_image make_animated_image(std::span<const ui_image> frames, std::span<const std::chrono::milliseconds> durations, int play_count);

    // 장을 이어 붙인 불변 그림이다 (움직이는 gif·webp·APNG).
    //
    // **장 하나하나가 이미 합성이 끝난 온전한 그림이다.** 형식이 나르는 앞 장과의
    // 차이는 디코딩에서 다 풀린다 — 그리기는 여느 `ui_image`와 똑같은 한 번의
    // 그리기이고, 어느 장을 건너뛰거나 되감아도 화면이 어긋나지 않는다. 장을
    // 차례대로 보아야만 옳은 값이면, tree가 frame마다 다시 지어지고 그리기가
    // `const`인 이 라이브러리에서 재생 상태를 어딘가 들고 있어야 한다.
    //  - 그 대가로 메모리는 장 수에 비례한다. 줄이는 손잡이는
    //    `image_decode_options` 하나뿐이고, 그때그때 합성해 아끼는 것은 범위 밖이다.
    // `ui_image`와 같은 계약으로 만들어진 뒤 바뀌지 않는다 — logic thread가 만들고
    // UI thread가 그려도 새 동기화가 없고, 복사는 참조 공유 하나라 싸다.
    class ui_animated_image
    {
    public:
        ui_animated_image() = default;

        // 장이 하나라도 있으면 참이다. 한 장짜리도 유효하다 (움직이지 않을 뿐이다).
        [[nodiscard]] bool valid() const noexcept;
        // 모든 장이 함께 쓰는 픽셀 크기다 (다른 크기가 섞일 수 없다 — 만드는
        // 자리가 막는다). 화면 배율과 무관하다.
        [[nodiscard]] int width() const noexcept;
        [[nodiscard]] int height() const noexcept;
        [[nodiscard]] std::size_t frame_count() const noexcept;
        // 참이면 장이 둘 이상이다.
        // 한 장짜리도 **유효한** 값이라 `valid`와 갈린다 — 움직이지 않는 png를
        // 움직이는 문으로 열면 여기 선다.
        [[nodiscard]] bool animated() const noexcept;
        // `index`번 장의 그림이다. 범위 밖이면 빈 이미지다.
        //  - 색인을 순수 함수에서 받아 그대로 넘기는 자리라, 부르는 쪽이 범위를
        //    다시 검사하지 않게 한다.
        [[nodiscard]] const ui_image& frame_image(std::size_t index) const noexcept;
        // 장들의 표시 시간이다 (`frame_count()`와 길이가 같고, 전부 양수다).
        // 시간표만 따로 내주는 이유는 재생 판정이 픽셀을 보지 않아서다 — test가
        // 파일도 코덱도 없이 이 목록을 손으로 지어 재생을 확인한다
        // (`decoded_image_size`가 이미 선 그 자리다).
        [[nodiscard]] std::span<const std::chrono::milliseconds> frame_durations() const noexcept;
        // 처음부터 끝까지 몇 바퀴 도는가. `image_plays_forever`면 끝없이 돈다.
        //  - **파일이 적은 값이다.** 앱이 다르게 돌리고 싶으면 같은 장에 다른
        //    바퀴 수를 붙여 `make_animated_image`로 새 값을 지으면 된다. 장이
        //    참조 공유라 픽셀은 복사되지 않는다.
        [[nodiscard]] int play_count() const noexcept;

    private:
        friend ui_animated_image make_animated_image(std::span<const ui_image> frames, std::span<const std::chrono::milliseconds> durations, int play_count);

        std::shared_ptr<const image_frame_list> frames_ {};
        int width_ { 0 };
        int height_ { 0 };
        int play_count_ { 1 };
    };

    // 재생 시계다.
    //
    // **앱이 든다.** 라이브러리는 시계도 frame 루프도 갖지 않는다 — `transition`이
    // 앱 상태인 것과 같은 규칙이고(transition.h), `toast_config::shown_at`이 이미
    // 선 그 자리다. element는 frame마다 다시 지어지고 `draw`가 `const`라 재생
    // 위치를 element 안에 둘 자리가 아예 없다.
    //
    // **시각만의 함수다.** 어느 frame에 물어도 같은 장이 나오고 그 사이의 상태가
    // 없다 — spinner의 각도와 토스트의 불투명도가 이미 선 그 규칙이다. 그래서
    // frame을 건너뛰어도 어긋나지 않고, test가 시각을 손으로 정해 확인할 수 있다.
    struct image_playback
    {
        // 재생이 시작한 시각이다.
        // **비어 있으면 아직 시작하지 않은 것이다** — 첫 장에 서고 다시 그릴 것도
        // 예고하지 않는다. 값을 채우는 것이 곧 시작이고, 다시 채우는 것이 처음부터
        // 다시 돌리는 것이다.
        //  - 기본값이 시계의 원점이 아니라 `optional`인 이유다. 원점이면 채우기를
        //    잊은 앱이 **한참 지난 시각**을 시작으로 받아, 끝없이 도는 gif는 엉뚱한
        //    장면에서 시작하고 한 번만 도는 gif는 뜨자마자 이미 끝나 있다.
        //  - **frame마다 `now`를 새로 넣으면 그림이 첫 장에 멈춘다.** 이 값은 앱이
        //    한 번 정해 들고 있는 상태이지 frame마다 짓는 값이 아니다 — 전환을
        //    frame마다 새로 만들면 아무것도 움직이지 않는 것과 같은 실수다.
        std::optional<std::chrono::steady_clock::time_point> started {};
        // 멈춘 시각이다. 값이 있으면 그 시각의 장에 서고 시간이 흘러도 달라지지
        // 않으며, 다시 그릴 것도 예고하지 않는다.
        //  - 멈추는 것은 `playback.paused_at = now` 한 줄이라 함수를 두지 않는다.
        //    산수가 있는 쪽은 이어 가는 쪽뿐이다 (`playback_resumed`).
        std::optional<std::chrono::steady_clock::time_point> paused_at {};
    };

    // 재생이 시작한 뒤로 흐른 시간이다.
    // 시작하지 않았으면 0이고, 멈춰 있으면 멈춘 시각까지다. 시작 시각이 아직
    // 오지 않았으면 0이다 (`transition`이 시작 전에 `from`인 것과 같다).
    [[nodiscard]] constexpr std::chrono::nanoseconds playback_elapsed(const image_playback& playback, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (playback.started.has_value() == false)
            return std::chrono::nanoseconds { 0 };
        const auto measured { playback.paused_at.has_value() ? *playback.paused_at : now };
        if (measured <= *playback.started)
            return std::chrono::nanoseconds { 0 };
        return std::chrono::duration_cast<std::chrono::nanoseconds>(measured - *playback.started);
    }

    // 멈춘 재생을 `now`에 이어서 돌린다 — **선 자리에서 이어 간다.**
    // 멈춘 동안 흐른 시간만큼 시작 시각을 밀지 않으면 다시 돌리는 순간 그만큼
    // 건너뛴다 (`transition_to`가 도는 전환을 끝값으로 튀게 하지 않는 것과 같은
    // 규칙이다). 시작하지 않았거나 멈춰 있지 않으면 그대로다.
    [[nodiscard]] constexpr image_playback playback_resumed(const image_playback& playback, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (playback.started.has_value() == false || playback.paused_at.has_value() == false)
            return playback;
        return { now - (*playback.paused_at - *playback.started), std::nullopt };
    }

    // 지금 보이는 장과, 그 장이 바뀌는 시각이다.
    struct animation_frame
    {
        // `frame_durations()`의 색인이다.
        std::size_t index { 0 };
        // **다른 장이 서는 가장 이른 시각이다** (재생을 시작한 시각으로부터).
        // 비어 있으면 시간이 흘러도 그림이 그대로다 — `next_update`의 nullopt과
        // 같은 문장이다.
        std::optional<std::chrono::nanoseconds> next_at {};
    };

    // 재생을 시작하고 `elapsed`가 지났을 때 보이는 장이다.
    //
    // 판정이 그리기에 숨으면 test가 닿지 못해 여기로 뗀다 (raster-test-design.md).
    // `image_destination`·`decoded_image_size`와 같은 자리이고, 받는 것이 길이
    // 목록과 값 둘뿐이라 test에 파일도 코덱도 창도 필요 없다.
    //
    // **그리기와 `next_update`가 이 한 함수를 부른다.** 장을 고르는 식과 다음
    // 경계를 재는 식이 따로 서면 한쪽만 고쳐졌을 때 창이 헛돌거나(경계가 이르다)
    // 장을 건너뛴다(경계가 늦다) — 눈으로만 보이는 종류라 한 답에 묶는다.
    //
    // 규칙은 이렇다.
    //  - 장 i는 `[길이합(0..i), 길이합(0..i+1))`에 선다. **반열린 구간이라 경계
    //    시각에는 다음 장이다.** 길이가 0인 장은 그래서 화면에 서지 않는다.
    //  - `next_at`은 **지금 장과 다른 장이 서는 가장 이른 시각**이고, 그런 시각이
    //    없으면 비어 있다. 이 한 문장이 헛도는 깨움을 통째로 막는다 — 예고한
    //    시각에 깨어나면 반드시 다른 그림이다.
    //  - `play_count`가 음수면 끝없이 돈다. 양수면 그만큼 돈 뒤 **마지막 장에
    //    선다** — 0으로 돌아가면 끝난 재생이 첫 장면으로 튀어, 화면에서
    //    "한 바퀴 더 도는 중"과 구별되지 않는다. 그러므로 마지막 바퀴의 마지막
    //    경계는 예고하지 않는다.
    //  - 목록이 비었거나, **장이 하나뿐이거나**, 길이의 합이 0이면 언제 물어도
    //    0번이고 `next_at`이 비어 있다. 장 하나짜리를 자르는 것이 특히
    //    중요하다 — 되풀이 표식만 붙은 한 장짜리 gif가 실제로 있고, 그것을
    //    "아직 안 끝났다"로 읽으면 창이 영원히 깨어난다.
    //  - `elapsed`가 0 이하면 0번이다.
    [[nodiscard]] animation_frame animation_frame_at(std::span<const std::chrono::milliseconds> frame_durations, int play_count, std::chrono::nanoseconds elapsed) noexcept;

    // 이미지를 칸에 맞추는 방식이다.
    enum class image_fit
    {
        // 비율을 지키고 칸 안에 전부 들어온다. 남는 자리는 그리지 않는다.
        contain,
        // 비율을 지키고 칸을 가득 덮는다. 넘치는 부분은 잘린다.
        cover,
        // 비율을 버리고 칸을 그대로 채운다.
        fill,
    };

    // 이미지 픽셀을 화면 픽셀로 옮길 때 표본을 뽑는 방식이다.
    //
    // **luil의 어휘다.** 그리기는 Skia의 표본 값을 쓰지만 그 타입은 이 헤더에
    // 나오지 않는다 — 공개 API에 Skia 타입을 더 늘리지 않는다는 이 파일의
    // 규칙이고(image_element.h:14-15), 두 어휘를 잇는 표는 `image_element.cpp`의
    // 익명 namespace 한 자리에만 산다. 앱이 filter와 mipmap을 직접 짝지어 넘기게
    // 하지 않는 이유도 같다 — "nearest인데 mipmap"처럼 뜻이 없는 짝이 생기고, 그
    // 짝을 막는 검사가 다시 필요해진다.
    enum class image_sampling
    {
        // 이웃 픽셀을 섞는다 (linear + mipmap). **기본값이자 지금까지의 그림이다.**
        // 확대는 linear가, 축소는 mipmap이 함께여야 성기게 뭉개지지 않는다
        // (썸네일). 사진과 큰 그림은 전부 이쪽이다.
        smooth,
        // 가장 가까운 한 픽셀을 그대로 뽑는다 (nearest, mipmap 없음).
        // 픽셀 아트와 아이콘 아틀라스가 여기 선다 — 섞으면 한 픽셀짜리 획이
        // 뿌옇게 번져 원본의 모양이 사라지고, 크게 확대할수록 그 차이가 커진다.
        sharp,
    };

    struct image_config
    {
        ui_image image {};
        image_fit fit { image_fit::contain };
        // 보조 기술이 읽는 설명이다.
        // 비어 있으면 장식으로 취급되어 읽히지 않는다 (image-design.md).
        std::u8string description {};
        // 움직이는 그림과 그 재생 시계다.
        //
        // **유효하면 이쪽이 그려지고 `image`는 보지 않는다.** 파일을 열어 보기
        // 전에는 그것이 움직이는지 알 수 없어, 앱은 디코더가 돌려준 것을 그대로
        // 놓기만 하고 element 종류를 갈라 쥐지 않는다. 두 자리를 하나로 합치지
        // 않는 이유는 그 반대다 — 정지 그림 하나를 놓는 흔한 자리가 시간표와
        // 재생 시계를 지어야 하는 일이 되면 안 된다.
        //  - `animation`이 비면 `playback`은 아무 뜻이 없다.
        //  - **꼬리에 둔다** — 위치 초기화로 만드는 자리가 밖에 있을 수 있어
        //    가운데 끼우면 그 자리들의 뜻이 조용히 어긋난다 (`drag_visual::surface`와
        //    같은 판단이다 — ui_element.h:272).
        ui_animated_image animation {};
        image_playback playback {};
        // 이미지에서 잘라 낼 조각이다 — **한 장을 여러 그림으로 쓰는 문**이다
        // (아틀라스·스프라이트 시트). 좌표는 **이미지 자신의 픽셀**이다.
        //
        // 비어 있으면 이미지 전체다. 한 장을 통째로 그리는 흔한 자리가 아무것도
        // 적지 않아야 해서고, `optional`을 쓰지 않는 이유는 "빈 사각형이 곧 없음"을
        // 이미 `image_destination`이 답으로 쓰고 있어서다. 옆에 `has_source` 같은
        // 깃발을 두지 않는 것도 같은 규칙이다 — 빈 값 자체가 스위치다.
        //  - **꼬리에 둔다** — 바로 위 `animation`과 같은 이유이자 같은 자리다
        //    (image_element.h:286-288). 가운데 끼우면 위치 초기화로 짓는 자리
        //    (`examples/demo/images_page.cpp`)의 뜻이 조용히 어긋난다.
        //
        // **단위가 `bounds`와 다르다.** 배치의 칸과 `image_destination`의 답은
        // 창의 물리 픽셀이고, 이 값은 화면 배율과 무관한 원본 이미지의 픽셀이다.
        // 두 뜻이 `rect_f`라는 한 형식을 나눠 써서 형식이 그 차이를 막아 주지
        // 않는다 — 배율 2의 화면에서도 `{ 0, 0, 32, 32 }`는 언제나 이미지의 32×32
        // 픽셀이지 화면의 32×32가 아니다.
        //
        // 걸치거나 어긋난 값은 `image_source_rect`가 한 자리에서 다듬는다 — 밖으로
        // 걸치면 안으로 자르고, 비었거나 음수이거나 통째로 밖이면 이미지 전체다.
        // 그 판정이 순수 함수라 test가 창도 픽셀도 없이 잠근다.
        //
        // **칸에 맞추는 계산도 조각의 크기로 한다.** 8:1 시트에서 잘라 낸 정사각
        // 스프라이트는 정사각으로 앉아야 한다. 시트의 크기로 계산하면 `contain`이
        // 시트의 비율로 자리를 잡아, 조각은 엉뚱한 여백 안에서 찌그러진다.
        //
        // **소수점이 든 조각도 그려진다.** 칸에 맞추는 그 계산은 변을 **반올림한**
        // 정수로 하고(자르면 32.7이 32로 앉아 청한 비율에서 밀려난다), 어느 변도
        // 1 아래로 내려가지 않는다 — 0.5픽셀짜리 조각이 0으로 접히면 목적 사각형이
        // 비어 아무것도 그려지지 않고, 그것은 화면에서 "빈 이미지"와 구별되지 않아
        // 아무도 실패를 못 본다. 뽑는 자리 자체는 소수점 그대로라, 반올림이 픽셀을
        // 옮겨 오지는 않는다.
        //
        // 그리기의 표본 제약도 이 값이 가른다.
        //  - 조각이 이미지 **전체**면 `kFast_SrcRectConstraint`다. mipmap이 살아
        //    있어 크게 줄여 그린 그림이 성기게 튀지 않는다 — 지금까지의 품질이
        //    그대로다.
        //  - 조각이 **일부**면 `kStrict_SrcRectConstraint`다. 그러지 않으면 필터가
        //    조각 밖 한두 줄까지 함께 읽어 **옆 스프라이트가 가장자리에 번져 든다.**
        //    시트를 한 픽셀 여백으로 짓는 관례로 덮을 수도 있으나, 그것은 앱이
        //    파일을 그렇게 짓기를 바라는 규약이라 라이브러리가 지킬 수 없다.
        //  - **그 대가로 Skia는 strict에서 mipmap을 끈다.** 그래서 조각을 많이
        //    줄여 그리면 성기게 튄다(aliasing). 답은 제약을 푸는 것이 아니라
        //    **필요한 크기로 조각을 미리 짓는 것**이다 — 그 크기의 픽셀을
        //    `make_rgba_image`로 만들거나, 디코딩에서
        //    `image_decode_options::max_width`로 줄여 받는다 (image-design.md).
        rect_f source {};
        // 픽셀을 뽑는 방식이다 (`image_sampling`).
        image_sampling sampling { image_sampling::smooth };
    };

    // 이미지가 칸 안에 놓일 자리다 (좌표는 칸과 같은 단위 — 물리 픽셀).
    // 판정이 그리기에 숨으면 test가 닿지 못해 여기로 뗀다 (raster-test-design.md).
    // 칸이나 이미지가 비어 있으면 빈 사각형이다.
    [[nodiscard]] rect_f image_destination(const rect_f& bounds, int width, int height, image_fit fit) noexcept;

    // 실제로 뽑아 쓸 조각이다 (`image_config::source`를 `width`×`height` 이미지
    // 안으로 다듬은 값 — 좌표는 **이미지 픽셀**이다).
    //
    // 판정이 그리기에 숨으면 test가 닿지 못해 여기로 뗀다 (raster-test-design.md).
    // `image_destination`과 나란한 자리이고, 받는 것이 사각형 하나와 두 수뿐이라
    // test에 파일도 창도 필요 없다.
    //
    // 규칙은 이렇다. 어느 갈래에서도 던지지 않는다 — 조각은 앱이 손으로 적거나
    // 아틀라스 표에서 읽어 오는 값이라 어긋난 값이 오는 것이 정상이다.
    //  - 걸친 조각은 이미지 안으로 **자른다**. 밖까지 그리면 그 자리가 조각의
    //    일부인 양 늘어나 그림이 어긋난다.
    //  - 비었거나 폭·높이가 음수이거나 통째로 이미지 밖이면 **이미지 전체**다.
    //    빈 값이 "전부"라는 것이 `image_config::source`의 계약이고, 나머지 둘도
    //    같은 답으로 모은다 — 아무것도 그리지 않으면 앱은 이미지가 없는 것인지
    //    조각이 어긋난 것인지 화면에서 가릴 수 없다.
    //  - 빈 이미지(`width`나 `height`가 0 이하)는 빈 사각형이다. 그릴 것이 없다는
    //    답을 "전체"로 적을 수는 없다.
    [[nodiscard]] rect_f image_source_rect(const rect_f& source, int width, int height) noexcept;

    // 픽셀 이미지 한 장, 또는 움직이는 그림 한 편이다.
    //
    // 표시 전용이라 액션도 커서도 갖지 않는다 — 누르는 이미지는 담는 쪽이
    // `set_action`으로 만들고, 그러면 기본 규칙("누를 수 있으면 자리다")을
    // 그대로 탄다.
    // 빈 이미지는 아무것도 그리지 않는다 — 자리 표시를 그리면 "이미지가 있다"는
    // 거짓말이 된다.
    //  - **재생 시계를 들지 않는다.** 어느 장인지는 `config.playback`과
    //    `draw_context::now`에서 매번 새로 나온다. tree는 frame마다 다시 지어지고
    //    그리기가 `const`라, 여기에 상태를 두면 다음 frame에 사라진다 (토스트가
    //    `shown_at`을 config로 받는 것과 같은 자리다).
    //  - 그래서 **그리는 차례도 빠뜨린 frame도 뜻이 없다** — 어느 시각에 물어도
    //    답이 하나다.
    class image_element final : public ui_element
    {
    public:
        image_element(ui_element_id id, image_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 다음 장이 뜨는 시각이다.
        // 움직이는 그림이 **도는 중일 때만** 답이 있다 — 한 장짜리·시작 전·멈춤·
        // 다 돎은 전부 시간이 흘러도 그림이 그대로다.
        //  - "지금"을 답하지 않는다. 회전 표시는 매 frame 달라져 `context.now`를
        //    답하지만(그래서 platform이 33 ms마다 다시 그린다 —
        //    `continuous_repaint_interval`), 움직이는 그림은 다음 경계까지 그림이
        //    같다. 90 ms짜리 gif는 초당 열한 번만 창을 깨운다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        image_config config_ {};
    };
} // namespace luil

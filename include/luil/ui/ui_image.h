#pragma once

#include <cstdint>
#include <memory>
#include <span>

// 불변 픽셀 그림 값이다. element를 모르는 잎 헤더라 `ui_events.h`의 요청 구조체가 값으로 든다
// (image-clipboard-design.md). 그림을 그리는 element와 움직이는 그림은 `image_element.h`에 있다.
namespace luil {
    // Skia 포장의 실체다 (`src/ui/image_backing.h`에만 산다).
    // 공개 API는 Skia 타입을 더 늘리지 않는다.
    struct image_backing;
    // 라이브러리 안의 디코더가 값을 짓는 문이다 (SVG·Lottie).
    struct image_access;

    class ui_image;

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
    //  - **SVG에서 온 값은 벡터다** (`decode_svg_bytes`). 픽셀 대신 기록한 그림을 들고
    //    그릴 때마다 화면 배율대로 다시 그려, 확대해도 선이 날카롭다. 크기·칸 맞추기·
    //    조각(`image_config::source`)의 뜻은 래스터와 같다 — `width`·`height`가 그림
    //    자신의 좌표계의 크기다.
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
        friend struct image_access;

        std::shared_ptr<const image_backing> backing_ {};
        int width_ { 0 };
        int height_ { 0 };
    };
}

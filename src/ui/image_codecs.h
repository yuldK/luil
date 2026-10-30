#pragma once

namespace luil {
    // 빌드가 켠 선택 코덱을 Skia의 디코더 목록에 올린다 (지금은 AVIF 하나다).
    //
    // AVIF는 Skia 패키지의 기본 디코더 목록에 없다 — 거기 있으면 AVIF를 쓰지 않는 앱도
    // libavif·libgav1을 링크해야 해서 skia-prep이 따로 떼어 냈다 (skia-prep docs/skia-build.md 6.3).
    // 그래서 쓰는 쪽이 `SkCodecs::Register`로 올린다. 켠 구성(`LUIL_ENABLE_AVIF`)은
    // `image_codecs_avif.cpp`가, 끈 구성은 아무것도 하지 않는 `image_codecs_default.cpp`가 선다.
    //
    // **디코더 목록을 고르는 문은 낡은 `MakeFromStream`뿐이다.** `SelectionPolicy`를 받는 것이
    // 그쪽이고, 그쪽은 전역 목록을 읽는다. 목록을 넘기는 새 오버로드로는 정지 그림과
    // 애니메이션을 가를 수 없다.
    //
    // `Register`는 thread-safe가 아니다. 그래서 이 함수는 첫 디코드 전에 한 번만, 모든
    // 래스터·SVG·Lottie가 Skia 코덱을 열기 전에 이 함수를 부른다. 구현 안 static의
    // 초기화가 끝날 때까지 다른 thread의 디코드도 기다린다.
    void register_optional_image_decoders();
}

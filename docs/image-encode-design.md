# 이미지 인코딩

`ui_image`를 PNG·JPEG·WebP 파일 바이트로 짓는다. API는 [`include/luil/ui/image_encode.h`](../include/luil/ui/image_encode.h)에 있다.

| 함수 | 형식 | 알파 | 인코더 |
| --- | --- | --- | --- |
| `encode_png` | 무손실 PNG | 담는다 | Rust PNG (`SkPngRustEncoder`) |
| `encode_jpeg(image, quality)` | 손실 JPEG | 없다 — 검정 위에 합성한다 | libjpeg-turbo (`SkJpegEncoder`) |
| `encode_webp(image, quality)` | 손실 WebP | 담는다 | libwebp (`SkWebpEncoder`) |
| `encode_webp_lossless` | 무손실 WebP | 담는다 | libwebp (`SkWebpEncoder`) |

## 형식마다 함수가 따로다

형식을 값으로 받는 함수 하나로 두면 그 함수가 세 인코더를 모두 참조한다. 그러면 PNG만 쓰는 앱도 JPEG·WebP 인코더를 싣는다. 함수마다 파일을 나누면(`src/ui/image_encode_png.cpp` 등) **앱이 부른 형식의 인코더만** 실행 파일에 들어온다. skia-prep의 실측으로 Release 실행 파일에서 PNG는 약 0.13 MB, JPEG는 약 0.27 MB가 는다 (win-x64, skia-prep `docs/skia-build.md` 6.6). 부르지 않으면 늘지 않는다.

화면 캡처(`capture_request`)는 이 API가 아니라 창 계층이 직접 무손실 WebP로 남긴다. 창 계층은 창이 있는 모든 앱에 링크되므로, 거기서 PNG 인코더를 부르면 캡처를 쓰지 않는 앱도 그 인코더를 싣는다. PNG 캡처가 필요한 앱은 캡처 파일을 `load_image_file`로 읽어 `encode_png`로 다시 짓는다.

## 계약

- 실패는 예외가 아니라 값이다. 빈 바이트와 개발자가 읽을 진단 영문을 돌려준다. 성공하면 `error`를 건드리지 않는다 ([이미지 디코딩](image-decode-design.md)과 같은 규약).
- 품질은 0–100이고, 범위 밖은 끝으로 자른다. 실패가 아니다.
- 색은 sRGB로 적는다.
- SVG에서 온 벡터 그림은 그 크기(`width`·`height`)로 투명 바탕에 한 번 펴서 인코딩한다. 더 큰 픽셀이 필요하면 상한을 크게 주어 다시 읽는다.
- 동기 CPU 작업이다. 창·GPU·COM을 요구하지 않으므로 어느 thread에서든 부를 수 있고, 큰 그림은 수십 ms가 걸리므로 logic thread나 worker에서 부른다.

## 검증

[`tests/image_encode_tests.cpp`](../tests/image_encode_tests.cpp)는 인코딩한 바이트를 같은 빌드의 디코더로 다시 읽어 그린 픽셀을 본다. 무손실 형식(PNG·무손실 WebP)은 반투명 픽셀까지 같아야 하고, 손실 형식(JPEG·WebP)은 채널의 크기 관계만 본다. 바이트를 견본과 견주지 않는다 — 인코더의 판번 하나에 test가 깨지고, 그것은 우리 계약이 아니다.

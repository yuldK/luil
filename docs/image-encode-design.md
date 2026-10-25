# 이미지 인코딩

`ui_image`를 PNG·JPEG·WebP 파일 바이트로 짓는다. API는 [`include/luil/ui/image_encode.h`](../include/luil/ui/image_encode.h)에 있다.

| 함수 | 형식 | 알파 | 인코더 |
| --- | --- | --- | --- |
| `encode_png` | 무손실 PNG | 담는다 | Rust PNG (`SkPngRustEncoder`) |
| `encode_jpeg(image, quality)` | 손실 JPEG | 없다 — 검정 위에 합성한다 | libjpeg-turbo (`SkJpegEncoder`) |
| `encode_webp(image, quality)` | 손실 WebP | 담는다 | libwebp (`SkWebpEncoder`) |
| `encode_webp_lossless` | 무손실 WebP | 담는다 | libwebp (`SkWebpEncoder`) |
| `encode_webp_within(image, options)` | 바이트 한도 안의 손실 WebP | 담는다 | libwebp (`SkWebpEncoder`) |

## 형식마다 함수가 따로다

형식을 값으로 받는 함수 하나로 두면 그 함수가 세 인코더를 모두 참조한다. 그러면 PNG만 쓰는 앱도 JPEG·WebP 인코더를 싣는다. 함수마다 파일을 나누면(`src/ui/image_encode_png.cpp` 등) **앱이 부른 형식의 인코더만** 실행 파일에 들어온다. skia-prep의 실측으로 Release 실행 파일에서 PNG는 약 0.13 MB, JPEG는 약 0.27 MB가 는다 (win-x64, skia-prep `docs/skia-build.md` 6.6). 부르지 않으면 늘지 않는다.

화면 캡처(`capture_request`)는 이 API가 아니라 창 계층이 직접 무손실 WebP로 남긴다. 창 계층은 창이 있는 모든 앱에 링크되므로, 거기서 PNG 인코더를 부르면 캡처를 쓰지 않는 앱도 그 인코더를 싣는다. PNG 캡처가 필요한 앱은 캡처 파일을 `load_image_file`로 읽어 `encode_png`로 다시 짓는다.

## 계약

- 실패는 예외가 아니라 값이다. 빈 바이트와 개발자가 읽을 진단 영문을 돌려준다. 성공하면 `error`를 건드리지 않는다 ([이미지 디코딩](image-decode-design.md)과 같은 규약).
- 품질은 0–100이고, 범위 밖은 끝으로 자른다. 실패가 아니다.
- 색은 sRGB로 적는다.
- SVG에서 온 벡터 그림은 그 크기(`width`·`height`)로 투명 바탕에 한 번 펴서 인코딩한다. 더 큰 픽셀이 필요하면 상한을 크게 주어 다시 읽는다.
- 동기 CPU 작업이다. 창·GPU·COM을 요구하지 않으므로 어느 thread에서든 부를 수 있고, 큰 그림은 수십 ms가 걸리므로 logic thread나 worker에서 부른다.

## 바이트 한도

업로드·첨부·메시지처럼 받는 쪽이 크기를 정해 둔 자리에서는 품질이 아니라 바이트 수가 목표다. 앱은 지금까지 품질을 낮춰 가며 다시 인코딩하고, 그래도 크면 줄여 다시 시작하는 고리를 직접 돌렸다. `encode_webp_within`이 그 고리를 맡는다.

```cpp
luil::webp_size_limit_options options {};
options.maximum_bytes = 200 * 1024;
const luil::webp_size_limit_result result { luil::encode_webp_within(picture, options, error) };
// result.bytes, result.quality, result.size, result.attempts
```

- **품질 표를 차례로 낮춘다.** 기본 표는 90·80·70·60·50·40이다. 한도에 드는 첫 품질에서 멈춘다. 표는 앱이 바꾼다(`qualities`). 비었거나 범위 밖인 값은 0–100으로 자르고, 표가 비면 기본 표다.
- **표 끝으로도 넘치면 변을 3/4로 줄이고 표를 처음부터 다시 돈다**(`shrink`, 기본 0.75). 줄인 그림은 바이트가 대개 절반 남짓이라, 줄인 뒤에는 높은 품질로도 드는 일이 흔하다. 표 끝 품질로 한 번 더 버티면 작은 그림을 굳이 흐리게 담는다.
  - 축소는 고품질 축소(`downscale_image`)다. 줄일 때마다 원본에서 새로 줄인다 — 줄인 것을 다시 줄이면 흐림이 쌓인다.
  - 긴 변이 `minimum_side`(기본 64)보다 작아지는 축소는 하지 않는다. 그때까지도 넘치면 빈 바이트와 오류다. 오류 글에 마지막 크기·품질·바이트 수를 적는다.
- `maximum_bytes`가 0이면 한도가 없다 — 표의 첫 품질로 한 번 인코딩한다.
- 결과는 고른 품질·크기·시도 수와 함께 온다. 앱이 "품질 70, 1200×900으로 줄여 담았다"를 사람에게 보일 수 있다.
- `cancelled`는 인코딩마다 그 앞에서, 그리고 축소 안에서 묻는다(`image_downscale_cancel_rows`). 접히면 빈 바이트와 `result.cancelled`다.
- 벡터 그림은 다른 인코더와 같이 제 크기로 한 번 편 픽셀에서 시작한다.

## 검증

[`tests/image_encode_tests.cpp`](../tests/image_encode_tests.cpp)는 인코딩한 바이트를 같은 빌드의 디코더로 다시 읽어 그린 픽셀을 본다. 무손실 형식(PNG·무손실 WebP)은 반투명 픽셀까지 같아야 하고, 손실 형식(JPEG·WebP)은 채널의 크기 관계만 본다. 바이트를 견본과 견주지 않는다 — 인코더의 판번 하나에 test가 깨지고, 그것은 우리 계약이 아니다.

바이트 한도는 바이트 수가 아니라 관계를 본다. 고른 결과가 한도 안인지, 첫 품질로 넘치는 한도에서 품질이 내려가는지, 표 끝으로도 넘치면 줄어드는지, 줄인 뒤 표를 처음부터 다시 도는지(시도 수), 못 담는 한도와 취소와 빈 그림을 본다. 데모 "이미지" 페이지가 연 그림을 200 KB 안에 담은 결과를 상태 줄에 적는다.

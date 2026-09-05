# 이미지 디코딩

이미지 decoder는 파일이나 인코딩된 byte span을 불변 `ui_image`와 `ui_animated_image`로 바꾼다. Skia와 함께 빌드된 codec을 사용하므로 지원 형식과 결과가 Windows에 설치된 OS codec에 따라 달라지지 않는다.

API는 [`include/luil/ui/image_decode.h`](../include/luil/ui/image_decode.h)에 있고, 이미지 값 타입은 [`include/luil/ui/image_element.h`](../include/luil/ui/image_element.h)에 있다.

## 지원 형식과 진입점

현재 Skia 구성은 PNG, JPEG, WebP, GIF, BMP, WBMP를 읽는다. PNG에는 APNG 애니메이션 지원이 포함된다. ICO, TIFF, DDS, HEIF, SVG는 이 API로 디코딩하지 않는다.

`load_image_file`은 UTF-8 경로를 읽는다. `decode_image_bytes`는 실행 파일 resource나 HTTP 응답처럼 이미 메모리에 있는 인코딩된 파일 바이트를 읽는다. byte span은 호출이 끝날 때까지만 유효하면 된다.

```cpp
std::u8string error;
luil::image_decode_options options {
    .max_width = 512,
    .max_height = 512,
};

luil::ui_image image = luil::load_image_file(path, options, error);
if (!image.valid()) {
    // error를 진단에 사용하고 사용자용 문장은 앱에서 만든다.
}
```

`image_decode_options`가 없는 overload는 디코딩된 크기를 유지한다.

## 실패 계약

디코딩은 예외를 던지지 않는다. 실패하면 빈 이미지와 `error`의 진단 문구를 돌려준다. 성공하면 `error`를 건드리지 않으므로, 이전 오류 문자열이 비워졌다고 가정하지 말고 `valid()`로 성공을 판단한다.

손상된 입력, 지원하지 않는 형식, 읽을 수 없는 경로, 완성할 수 없는 애니메이션 frame, 안전 상한을 넘는 이미지는 거절한다. 일부 frame만 디코딩된 애니메이션은 유효한 짧은 콘텐츠와 구별할 수 없으므로 돌려주지 않는다.

## 크기 제한과 방향

`image_decode_options::max_width`와 `max_height`는 비율을 유지하며 결과 크기를 제한한다. 0 이하는 그 축을 제한하지 않는다. 원본이 상한보다 작을 때 확대하지 않는다. 유효한 원본을 축소할 때 어느 결과 축도 0이 되지 않는다.

상한은 화면에 표시되는 방향의 축에 적용된다. EXIF orientation을 픽셀에 적용하며, 90도 회전되는 방향은 공개 `width`와 `height`도 바꾼다. 따라서 세로 사진은 앱이 지정한 세로형 상한을 그대로 따른다.

`decoded_image_size(width, height, options)`는 순수 크기 계산을 공개한다. 입력은 이미 방향이 반영된 크기다. 어느 원본 축이 양수가 아니면 `{0, 0}`을 돌려준다.

decoder는 픽셀을 펴기 전에 원본 64 million 픽셀 상한도 적용한다. 작은 압축 파일이 엄청난 출력 크기를 선언해 메모리를 결정하지 못하게 하는 고정 안전 정책이다. 일부 codec은 축소 전에 원본 크기로 풀어야 하므로, 결과 축소를 요청해도 원본 상한을 우회할 수 없다.

## 픽셀과 축소 규칙

결과 `ui_image`는 `make_rgba_image`와 같은 계약을 따른다. API 경계의 논리 색은 straight-alpha RGBA이고 내부에는 Skia가 요구하는 표현으로 저장한다.

축소는 premultiplied-alpha 공간에서 수행한다. straight-alpha channel을 따로 보간할 때 반투명 가장자리에 검거나 물든 테가 생기는 문제를 피한다.

여러 frame이 든 GIF, WebP, APNG를 정지 진입점으로 읽으면 첫 frame만 돌려준다. 첫 frame의 인코딩 사각형이 일부만 덮어도 결과는 형식의 논리 canvas 전체에 합성이 끝난 이미지다. 나중 frame이 필요하면 애니메이션 진입점을 쓴다.

## 애니메이션 진입점

`load_animated_image_file`과 `decode_animated_image_bytes`는 모든 frame을 `ui_animated_image`로 돌려준다. 정지 이미지도 성공한다. frame 하나가 서고 `valid()`는 참이며 `animated()`는 거짓이다.

모든 frame은 합성이 끝난 같은 크기의 논리 canvas다. frame별 수정 사각형, disposal, dependency는 디코딩 중에 해결한다. 따라서 임의 frame 접근과 건너뛴 그리기가 안전하다.

Skia의 모든 애니메이션 codec에서 축소 incremental decode가 옳지는 않으므로, 원본 크기로 합성한 뒤 완성된 frame을 축소한다. 요청 크기는 보관되는 frame 픽셀을 줄이지만 디코딩 중 peak working memory까지 보장하지는 않는다.

고정 픽셀 예산은 둘이다.

- 각 원본 frame은 64 million 픽셀 상한 안에 있어야 한다.
- 축소 후 보관할 모든 frame의 합도 64 million 픽셀 안에 있어야 한다.

보관 예산을 넘으면 `max_width`나 `max_height`를 낮추라는 진단을 돌려준다. 시간과 메모리는 frame 수에 비례한다.

## thread 규칙

decoder는 동기 CPU 작업이며 COM, 창, GPU 문맥을 요구하지 않는다. 어느 thread에서든 부를 수 있다. 파일 I/O, 큰 이미지, 애니메이션 합성은 수십 ms 이상 걸릴 수 있으므로 logic thread나 worker에서 실행한 뒤 불변 결과를 게시한다. UI thread나 frame 생성마다 부르지 않는다.

## 검증

[`tests/image_decode_tests.cpp`](../tests/image_decode_tests.cpp)는 파일·byte 진입점, 지원 형식, straight alpha, premultiplied 축소, EXIF 방향, 방향이 반영된 상한, 논리 canvas 첫 frame, 애니메이션 합성, APNG, 손상 입력, 메모리 상한을 인코딩된 fixture와 raster 픽셀로 확인한다.

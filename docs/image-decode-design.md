# 이미지 디코딩

이미지 decoder는 파일이나 인코딩된 byte span을 불변 `ui_image`와 `ui_animated_image`로 바꾼다. Skia와 함께 빌드된 codec을 사용하므로 지원 형식과 결과가 Windows에 설치된 OS codec에 따라 달라지지 않는다.

API는 [`include/luil/ui/image_decode.h`](../include/luil/ui/image_decode.h)에 있고, 이미지 값 타입은 [`include/luil/ui/image_element.h`](../include/luil/ui/image_element.h)에 있다.

## 지원 형식과 진입점

현재 Skia 구성은 PNG, JPEG, WebP, GIF, BMP, WBMP를 읽는다. PNG에는 APNG 애니메이션 지원이 포함된다. ICO, TIFF, DDS, HEIF, SVG는 이 API로 디코딩하지 않는다.

`load_image_file`은 UTF-8 경로를 읽는다. `decode_image_bytes`는 실행 파일 resource나 HTTP 응답처럼 이미 메모리에 있는 인코딩된 파일 바이트를 읽는다. byte span은 호출이 끝날 때까지만 유효하면 된다.

```cpp
luil::image_decode_error error {};
luil::image_decode_options options {
    .max_width = 512,
    .max_height = 512,
};

luil::ui_image image = luil::load_image_file(path, options, error);
if (image.valid() == false) {
    // error.kind로 갈래를 가르고 error.message는 진단에 쓴다.
    // 사용자용 문장은 앱에서 만든다.
}
```

`image_decode_options`가 없는 overload는 디코딩된 크기를 유지한다.

## 실패 계약

디코딩은 예외를 던지지 않는다. 실패는 값이다. 여덟 진입점은 모두 `luil::image_decode_error&`를 받으며, 실패하면 빈 이미지와 그 값을 돌려준다. 성공하면 `error`를 건드리지 않으므로, 이전 오류가 비워졌다고 가정하지 말고 `valid()`로 성공을 판단한다.

`image_decode_error`는 셋을 든다.

- `kind`는 갈래다 (`image_decode_error_kind`). **코드가 읽는 것은 이것이다.** 문장을 뒤져 갈래를 가리면 문장을 다듬는 날 앱이 조용히 틀린다.
- `message`는 개발자가 읽을 진단 영문이다. 사람에게 보일 문장은 앱이 만든다. `http_error::message`와 같은 규약이다.
- `frame`은 걸린 장의 0부터의 색인이다. 장을 가리지 않는 갈래에서는 0이고, 정지 그림은 언제나 0번 장이다. 애니메이션이 장을 푸는 도중에 실패하면 **갈래와 무관하게** 그 장의 번호를 든다. 40번 frame의 할당 실패를 0으로 적으면 `accept`로 40장을 받은 앱이 "0번 frame에서 끊겼다"를 화면에 적는다.

갈래는 `none`으로 시작해 경로(`path_empty`, `path_invalid`), 파일(`file_unreadable`, `file_empty`, `input_too_large`), 형식(`unsupported_format`, `incomplete_input`, `no_pixels`), 상한(`too_many_pixels`, `animation_too_large`), 진행(`frame_failed`, `cancelled`), 우리 쪽 사고(`out_of_memory`, `internal_error`)로 이어진다. `error.empty()`는 `kind == none`이다.

손상된 입력, 지원하지 않는 형식, 읽을 수 없는 경로, 완성할 수 없는 애니메이션 frame, 안전 상한을 넘는 이미지는 거절한다. 일부 frame만 디코딩된 애니메이션은 유효한 짧은 콘텐츠와 구별할 수 없으므로 **기본값에서는** 돌려주지 않는다. 앱이 그것을 원하면 아래의 정책을 켠다.

## 잘린 입력 정책

`image_decode_options::incomplete`는 자료가 그림보다 먼저 끝난 입력을 어떻게 할지 정한다.

- `image_incomplete_policy::reject` (기본값). 통째로 거절한다. 빈 값과 `incomplete_input`뿐이다.
- `image_incomplete_policy::accept`. 푼 만큼을 돌려주고 **오류도 함께** 돌려준다.

**`accept`가 여는 것은 `incomplete_input` 하나다.** 다른 갈래의 실패는 이 값을 고른 앱에게도 빈 값이다. 장을 풀지 못한 GIF(`frame_failed`), 자리를 잡지 못한 장(`out_of_memory`), 우리 쪽 사고(`internal_error`), 접힌 디코딩(`cancelled`)은 정지든 애니메이션이든 통째로 실패한다. 자료가 모자란 것은 밖에서 온 사고라 앱이 반쪽을 쓸지 고를 수 있지만, 나머지는 우리 쪽 사고다. 그 위에 반쪽을 씌우면 고쳐야 할 우리 잘못이 화면에서 "그냥 그런 그림"이 되어 아무도 보지 못한다. 정지 경로와 애니메이션 경로는 이 판정을 **하나의 식**으로 공유한다. 같은 정책을 두 벌로 적으면 그 둘은 반드시 어긋난다.

`accept`는 이 API에서 값과 오류가 함께 오는 유일한 자리다. 그래서 부르는 쪽은 두 가지를 따로 묻는다. `valid()`가 "그림이 있는가"이고 `error.empty()`가 "그것이 그림 전부인가"다. 둘을 하나로 묻는 코드는 여기서 반드시 틀린다. 같은 짝이 [HTTP 몸](http-client-design.md)의 `http_body::parse_error`에 이미 서 있다. 깨진 UTF-8을 U+FFFD로 바꿔 담은 글이 값과 이유를 함께 든다.

정지 이미지에서는 codec이 이미 써 놓은 줄까지가 값이다. 받기로 한 갈래에서만 디코딩 전에 대상 bitmap을 투명으로 지우므로, 못 푼 자리는 쓰레기가 아니라 투명이다. `kIncompleteInput`만 이 길을 탄다. 나머지 실패는 자료가 아니라 우리 쪽 사고라 빈 그림으로 덮지 않는다.

애니메이션에서는 **잘린 입력에 한해** 첫 실패 앞까지의 frame이 값이다. 한 장도 풀지 못했으면 `accept`라도 실패다. frame이 없는 애니메이션은 값이 아니기 때문이다. 5번 frame의 LZW 블록이 깨진 GIF처럼 잘린 것이 아닌 실패도 `accept`에서 통째로 실패한다. 그렇지 않으면 앱이 유효한 5장짜리 애니메이션을 조용히 받아 들고, 그 화면은 "원래 그런 그림"과 구별되지 않는다. 어느 쪽이든 `error.frame`이 어디서 끊겼는지를 든다.

라이브러리가 한쪽을 고르면 반드시 다른 쪽 앱이 틀린다. 사진을 보는 창은 반쪽이라도 띄우는 것이 옳고, 인쇄로 넘길 원본을 고르는 창은 반쪽을 원본으로 알고 넘기면 안 된다. 그래서 고르는 것을 앱에게 준다.

## 취소

`image_decode_options::cancelled`는 접을 때가 되었는지를 묻는 갈고리다. 비어 있으면 묻지 않는다. 옆에 `bool`을 두지 않는다.

디코딩은 파일 하나가 초 단위까지 갈 수 있는 동기 작업이고 그 시간은 frame 수에 비례한다. 갈고리를 달면 창을 닫거나 다른 그림을 고르는 값이 파일 크기가 아니라 한 걸음이 된다. 묻는 자리는 넷이다.

- 파일을 읽는 고리 안 (1 MiB마다 한 걸음).
- 정지 이미지의 `getPixels` 앞과 뒤.
- `getFrameCount()`와 frame 목록 걷기 앞.
- **애니메이션 frame 고리의 매 바퀴.** 200장짜리 GIF가 frame 하나를 푸는 시간 안에 멈추는 것이 이 한 줄에서 나온다.

갈고리는 디코딩 thread에서 불린다. 값을 읽어 답하는 것 이상을 하지 않아야 하고 던지지 않아야 한다.

접힌 디코딩은 **빈 값과 `image_decode_error_kind::cancelled`**다. 반쪽을 돌려주지 않는다. 그것은 `incomplete`가 답하는 다른 질문이고, 접었는데 그림이 오면 앱이 "접었다"와 "끝났다"를 다시 갈라 들어야 한다. 접힌 것도 알아볼 수 있는 답이라는 규칙은 [HTTP client](http-client-design.md)의 `http_error_kind::cancelled`와 같다.

## 크기 제한과 방향

`image_decode_options::max_width`와 `max_height`는 비율을 유지하며 결과 크기를 제한한다. 0 이하는 그 축을 제한하지 않는다. 원본이 상한보다 작을 때 확대하지 않는다. 유효한 원본을 축소할 때 어느 결과 축도 0이 되지 않는다.

상한은 화면에 표시되는 방향의 축에 적용된다. EXIF orientation을 픽셀에 적용하며, 90도 회전되는 방향은 공개 `width`와 `height`도 바꾼다. 따라서 세로 사진은 앱이 지정한 세로형 상한을 그대로 따른다.

`decoded_image_size(width, height, options)`는 순수 크기 계산을 공개한다. 입력은 이미 방향이 반영된 크기다. 어느 원본 축이 양수가 아니면 `{0, 0}`을 돌려준다. 이 함수가 보는 것은 `max_width`와 `max_height`뿐이다. 나머지 셋은 크기가 아니라 무엇을 거절하고 언제 접을지를 정한다.

## 원본 픽셀 상한

decoder는 픽셀을 펴기 전에 원본 한 장의 픽셀 수 상한도 적용한다. 기본값은 64 million 픽셀이다. 작은 압축 파일이 엄청난 출력 크기를 선언해 메모리를 결정하지 못하게 하는 안전 정책이다. 일부 codec은 축소 전에 원본 크기로 풀어야 하므로, 결과 축소를 요청해도 이 상한을 우회할 수 없다.

`image_decode_options::max_source_pixels`가 그 값을 갈아 끼운다. 0이면 기본값이다. 내리는 것도 올리는 것도 된다. 목록의 섬네일을 짓는 자리는 4 million이면 넉넉하고, 사진을 다루는 앱은 64 million을 넘는 원본을 여는 것이 제 일이다. 올리면 그 그림 하나에 대한 책임이 앱으로 넘어온다.

**상한 셋 중 이것만 앱이 정한다.** 나머지 둘, 곧 축소 후 전체 frame 픽셀 합(64 million)과 인코딩 byte(256 MiB)는 앱의 쓰임이 아니라 프로세스를 지키는 값이다. 앱이 올려서 얻는 것이 없고, 잘못 올리면 그림 하나가 아니라 프로세스가 죽는다. `max_source_pixels`를 아무리 올려도 그 둘은 그대로다. 요청 하나에 붙는 상한이라는 뜻에서 `http_request::max_body_bytes`와 같은 자리다. 기본값은 라이브러리가 대고, 이 한 번에 무엇이 옳은지는 부르는 쪽이 안다.

기본값에 걸린 진단은 `The image is larger than the 64 megapixel decode limit.`이고, 앱이 건 상한에 걸리면 그 값을 적는다. 기본값의 문장을 그대로 쓰면 진단이 거짓이 되어 사람을 엉뚱한 데로 보낸다.

## 픽셀과 축소 규칙

결과 `ui_image`는 `make_rgba_image`와 같은 계약을 따른다. API 경계의 논리 색은 straight-alpha RGBA이고 내부에는 Skia가 요구하는 표현으로 저장한다.

축소는 premultiplied-alpha 공간에서 수행한다. straight-alpha channel을 따로 보간할 때 반투명 가장자리에 검거나 물든 테가 생기는 문제를 피한다.

여러 frame이 든 GIF, WebP, APNG를 정지 진입점으로 읽으면 첫 frame만 돌려준다. 첫 frame의 인코딩 사각형이 일부만 덮어도 결과는 형식의 논리 canvas 전체에 합성이 끝난 이미지다. 나중 frame이 필요하면 애니메이션 진입점을 쓴다.

## 애니메이션 진입점

`load_animated_image_file`과 `decode_animated_image_bytes`는 모든 frame을 `ui_animated_image`로 돌려준다. 정지 이미지도 성공한다. frame 하나가 서고 `valid()`는 참이며 `animated()`는 거짓이다.

모든 frame은 합성이 끝난 같은 크기의 논리 canvas다. frame별 수정 사각형, disposal, dependency는 디코딩 중에 해결한다. 따라서 임의 frame 접근과 건너뛴 그리기가 안전하다.

Skia의 모든 애니메이션 codec에서 축소 incremental decode가 옳지는 않으므로, 원본 크기로 합성한 뒤 완성된 frame을 축소한다. 요청 크기는 보관되는 frame 픽셀을 줄이지만 디코딩 중 peak working memory까지 보장하지는 않는다.

픽셀 예산은 둘이다.

- 각 원본 frame은 `max_source_pixels`(기본 64 million 픽셀) 안에 있어야 한다.
- 축소 후 보관할 모든 frame의 합도 64 million 픽셀 안에 있어야 한다. 이쪽은 앱이 바꾸지 못한다.

보관 예산을 넘으면 `max_width`나 `max_height`를 낮추라는 진단(`animation_too_large`)을 돌려준다. 시간과 메모리는 frame 수에 비례한다.

## thread 규칙

decoder는 동기 CPU 작업이며 COM, 창, GPU 문맥을 요구하지 않는다. 어느 thread에서든 부를 수 있다. 파일 I/O, 큰 이미지, 애니메이션 합성은 수십 ms 이상 걸릴 수 있으므로 logic thread나 worker에서 실행한 뒤 불변 결과를 게시한다. UI thread나 frame 생성마다 부르지 않는다. 그 사이에 그만두는 길은 `image_decode_options::cancelled` 하나다. 갈고리도 그 worker thread에서 불린다.

## 불변

- 성공하면 `error`를 건드리지 않는다. 앞선 실패 값이 그대로 남는다.
- 값과 오류가 함께 서는 자리는 `incomplete == accept`이고 갈래가 `incomplete_input`인 자리 하나뿐이다. 그 밖의 모든 실패는 정책과 무관하게 빈 값이다. 정지 경로와 애니메이션 경로가 그 판정을 한 식으로 공유한다.
- 애니메이션이 장을 푸는 도중에 실패하면 `error.frame`이 그 장의 번호다.
- 접힌 디코딩은 언제나 빈 값과 `cancelled`다. 반쪽이 아니다.
- 갈래는 값(`kind`)이 든다. 진단 문장은 사람이 읽을 것이고, 갈래를 가리는 데 쓰지 않는다.
- `max_source_pixels`는 원본 한 장에만 닿는다. 애니메이션 합 상한과 인코딩 byte 상한은 어떤 옵션으로도 움직이지 않는다.
- 상한은 EXIF로 돌려세운 뒤의 축에 걸리고, 원본이 상한 안이면 늘리지 않으며, 유효한 원본을 줄여도 어느 축도 0이 되지 않는다.
- 애니메이션의 모든 frame은 같은 크기의 합성이 끝난 논리 canvas다. `accept`로 받은 짧은 목록에서도 그렇다.

## 검증

[`tests/image_decode_tests.cpp`](../tests/image_decode_tests.cpp)는 파일·byte 진입점, 지원 형식, straight alpha, premultiplied 축소, EXIF 방향, 방향이 반영된 상한, 논리 canvas 첫 frame, 애니메이션 합성, APNG, 손상 입력, 메모리 상한을 인코딩된 fixture와 raster 픽셀로 확인한다. 실패 갈래는 경로·파일·형식·상한마다 `image_decode_error::kind`로 견주고, 취소(바이트 문·파일 문·frame 고리 도중), 앱이 건 원본 픽셀 상한, 잘린 정지 이미지와 잘린 애니메이션의 두 정책을 함께 확인한다. 잘린 것이 아닌 실패(자르지 않고 LZW 코드 하나를 깨뜨린 GIF)가 `accept`에서도 통째로 거절되는지와 그때 `error.frame`이 실제로 걸린 장을 드는지도 같은 파일에서 잠근다. `decoded_image_size`는 창도 픽셀도 없이 잠긴다.

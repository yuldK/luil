# SVG와 Lottie

SVG와 Lottie는 픽셀이 아니라 그림을 그리는 명령을 담은 형식이다. luil은 둘을 픽셀로 펴지 않는다. SVG는 한 번 기록한 그림(`SkPicture`)으로, Lottie는 시각을 받아 장면을 그리는 값으로 들고, 그릴 때마다 화면 배율대로 다시 그린다. 높은 배율의 창이나 [확대·축소하는 보기](zoom-view-design.md)에서 키워도 선이 날카롭다.

API는 [`include/luil/ui/image_decode.h`](../include/luil/ui/image_decode.h)의 `decode_svg_bytes`·`load_svg_file`·`decode_lottie_bytes`·`load_lottie_file`이고, 값은 [`include/luil/ui/image_element.h`](../include/luil/ui/image_element.h)의 `ui_image`와 `ui_lottie`다.

## 빌드 옵션

셋 다 Skia 패키지의 기능별 아카이브를 쓰고, 기본은 끈다 (skia-prep `docs/skia-build.md` 6장).

| 옵션 | 쓰는 아카이브 | 끈 구성 |
| --- | --- | --- |
| `LUIL_ENABLE_SVG` | `svg` `skshaper` `skresources` | `decode_svg_bytes`·`load_svg_file`이 빈 값과 `unsupported_format` |
| `LUIL_ENABLE_LOTTIE` | `skottie` `sksg` `jsonreader` `skresources` `skshaper` | `decode_lottie_bytes`·`load_lottie_file`이 빈 값과 `unsupported_format` |
| `LUIL_ENABLE_TEXT_SHAPING` | `skparagraph` `skshaper` `skunicode_icu` `skunicode_core` `harfbuzz` | SVG의 `<text>`와 Lottie의 글자 레이어가 primitive shaper로 그려진다 |

공개 API는 옵션과 무관하게 같다. 끈 기능은 아카이브를 링크하지 않고 고지에도 싣지 않는다 (고지는 패키지의 `NOTICE.json`이 기능마다 적은 것을 고른다 — [소비자 계약](concepts/consumer-contract.md)).

**켠 기능도 앱이 부르지 않으면 실행 파일에 들어오지 않는다.** 진입점이 따로 떨어진 파일(`src/ui/svg_decode.cpp`·`src/ui/lottie_decode.cpp`)에 있고, 그리기는 그 파일들을 참조하지 않기 때문이다.

- SVG의 그림은 `SkPicture`라 그리기는 Skia 본체만 쓴다.
- Lottie의 장면은 `lottie_backing`이라는 가상 함수 하나를 거쳐 그린다 (`src/ui/image_backing.h`). `image_element`가 Skottie를 직접 부르면 이미지를 그리는 모든 앱이 Skottie를 링크하게 된다.

크기는 skia-prep의 실측이 기준이다 (Release 실행 파일, 부를 때 느는 양). SVG는 win-x64 약 0.6 MB·android-arm64 약 0.9 MB, Lottie는 약 0.65 MB·1.3 MB, 텍스트 모양 잡기를 더하면 약 1.1 MB·2.2 MB가 더 든다.

## SVG

### 크기

`ui_image`의 `width`·`height`는 뿌리 `<svg>`의 고유 크기(CSS 픽셀)다.

1. `width`·`height`가 절대 단위면 그것이다.
2. 한 변만 적었거나 퍼센트면, 나머지 변을 `viewBox`의 비율로 채운다.
3. 둘 다 없으면 `viewBox`의 크기다. 아이콘 SVG는 대개 이 모양이다.
4. 그래도 크기가 없으면 `no_pixels`다.

고유 크기는 컨테이너 크기를 0으로 두고 잰다. 그래야 퍼센트가 0이 되어 "적지 않았다"와 같은 갈래로 온다. 한 변이 100만을 넘는 SVG는 단위를 잘못 적은 파일로 보고 거절한다.

`image_decode_options`의 `max_width`·`max_height`는 그 크기를 비율대로 줄인다. 벡터라 메모리를 아끼는 값은 아니고, 그림 자신의 좌표계(칸 맞추기의 비율, `image_config::source`의 좌표)를 정한다. 기록은 줄인 크기로 늘이거나 줄여 그린다. `max_source_pixels`와 `incomplete`는 뜻이 없다. 잘린 XML은 반쪽 그림이 아니라 문서가 아니므로 `unsupported_format`이다.

### 기록

문서를 풀어 `SkSVGDOM`을 세우고, 고유 크기를 컨테이너로 두어 한 번 `SkPictureRecorder`에 그린다. 값이 드는 것은 DOM이 아니라 그 기록이다.

- DOM은 그릴 때마다 상태를 바꾼다. `SkPicture`는 만들어진 뒤 바뀌지 않아, logic thread가 만든 값을 UI thread가 그려도 새 동기화가 없다 (`ui_image`의 계약).
- 기록을 다시 그리는 비용은 경로 수에 비례한다. 아이콘 수준에서는 같은 크기의 래스터와 큰 차이가 없지만, 복잡한 일러스트를 큰 목록에 수백 개 놓을 때는 `make_rgba_image`로 미리 편 그림이 싸다.

### 그리기

`image_element`는 `ui_image`가 기록을 들고 있으면 픽셀을 뽑지 않고 그 기록을 다시 그린다.

1. 조각(`image_source_rect`)과 목적 사각형(`image_destination`)은 래스터와 같은 계산이다.
2. 목적 사각형으로 자른다. 래스터의 strict 제약이 하는 일을 벡터에서는 자르기가 한다 — 자르지 않으면 조각 밖의 그림이 칸에 그려진다.
3. 그림 자신의 좌표를 목적 사각형으로 옮겨(`translate`·`scale`) 기록을 그린다.

`image_sampling`은 뜻이 없다. 뽑을 픽셀이 없다.

### 자원과 글자

- **밖의 자원을 열지 않는다.** `<image href>`의 파일·네트워크 경로는 그리지 않는다. SVG 하나를 여는 일이 디스크와 네트워크를 건드리는 길이 되면 안 된다. `data:` URI만 풀고, 그것도 디코딩 때 미리 풀어(`kPreDecode`) 기록에 픽셀로 담는다. 그릴 때 풀면 UI thread가 코덱을 돈다.
- `<text>`는 플랫폼의 글꼴 관리자(`platform_font_source().make_uncached_manager()`)로 그린다. 모양 잡기는 아래 텍스트 절을 본다.

### 범위

Skia SVG 모듈의 범위를 따른다. `<style>` 블록의 CSS, SMIL 애니메이션(`<animate>`), 스크립트는 없다. 필터는 일부만 있다. 아이콘과 일러스트 수준의 SVG는 그리지만, 웹에서 받은 임의의 SVG를 브라우저와 같게 그리는 것이 목적이라면 [웹뷰](webview-composition-design.md)가 맞다.

HTTP 응답의 `image/svg+xml`은 image가 아니라 bytes 갈래다 ([HTTP client](http-client-design.md)). image 갈래는 래스터 디코더가 푸는 몸이라, 앱이 bytes를 받아 `decode_svg_bytes`로 연다.

## Lottie

### 값

`ui_lottie`는 크기(`w`·`h`), 한 바퀴의 길이(`(op - ip) / fr`), 장 빠르기(`fr`), 바퀴 수를 들고, 장면은 참조로 나눠 든다. 파일은 바퀴 수를 나르지 않으므로 기본은 끝없이 돌고(`image_plays_forever`), 앱이 `with_play_count`로 바꾼다. 장면은 다시 읽지 않는다.

크기와 상한(`max_width`·`max_height`)의 뜻은 SVG와 같다. 길이나 장 빠르기가 없는 파일은 `no_pixels`, JSON이 Lottie가 아니면 `unsupported_format`이다.

### 재생

`image_config::lottie`에 놓고 재생은 `image_config::playback`이 정한다. 움직이는 이미지와 같은 시계이고 규칙도 같다 ([움직이는 이미지](image-anim-design.md)). Lottie가 유효하면 `image`·`animation`보다 이긴다.

어느 장면인지는 `lottie_frame_at(duration, frame_rate, play_count, elapsed)`가 정한다. 그리기와 `next_update`가 이 한 함수를 부른다.

- **시간을 `1 / frame_rate` 간격의 장으로 자른다.** Skottie는 장 사이도 보간해 그릴 수 있지만, 그러면 다음 경계가 언제나 "지금"이 되어 창이 쉬지 않는다. 파일이 적은 장 빠르기가 저작 도구가 본 그림이다. 30 fps Lottie는 초당 서른 번만 창을 깨운다.
- 한 바퀴의 장 수는 `ceil(duration × frame_rate)`다. 마지막 장은 길이의 끝 바로 앞이다 — 끝 시각에는 레이어가 이미 끝나 빈 그림일 수 있다.
- 바퀴 수가 양수면 그만큼 돈 뒤 마지막 장에 서고 예고하지 않는다. 0은 1로 본다.
- 길이나 장 빠르기가 양수가 아니거나 한 바퀴가 한 장이면 언제나 첫 장이고 예고가 없다.

### 그리기와 thread

장면은 시각으로 옮길 때마다 바뀐다(`seekFrameTime`). 구현(`skottie_scene`)은 옮기고 그리는 두 걸음을 자물쇠 하나로 묶는다. 같은 값을 두 창이 서로 다른 시각으로 함께 그려도 엉뚱한 장면이 끼지 않는다.

그리기는 SVG와 같이 목적 사각형으로 자르고 장면을 그림 자신의 좌표(`{0, 0, width, height}`)에 그린다.

자원 규칙(밖의 경로를 열지 않고 `data:` URI만 미리 푼다)과 글자 레이어의 모양 잡기는 SVG와 같다. 표현식(expressions)과 Skottie가 지원하지 않는 효과는 그리지 않는다.

## 텍스트 모양 잡기

SVG의 `<text>`와 Lottie의 글자 레이어는 공장(`SkShapers::Factory`)을 받지 않으면 primitive shaper로 그린다. 글자마다 cmap 글리프 하나라, 아랍어 이어 쓰기·bidi·인도계 문자의 재배치·ZWJ 이모지·옛한글 조합이 깨진다. skia-prep은 svg·skottie 아카이브가 HarfBuzz를 참조하지 않게 세웠으므로, 모양 잡기는 공장을 넘길 때만 선다.

`LUIL_ENABLE_TEXT_SHAPING`을 켠 구성은 `text_shaping_factory()`(`src/ui/text_shaping_harfbuzz.cpp`)가 HarfBuzz 공장을 넘긴다.

- ICU는 링크하지 않고 실행 시점에 플랫폼의 것을 연다. Windows는 System32의 `icu.dll`(Windows 10 1903)이나 `icuuc.dll`(1703), Android는 NDK의 `libicu.so`(API 31, Android 12)다. ICU 데이터를 싣지 않아 패키지와 실행 파일이 수십 MB 늘지 않는다.
- 열리지 않으면 primitive 공장이다. 글은 그려지고 위의 모양 잡기만 빠진다. Android의 minSdk는 26 그대로다 — `dlopen`이라 링크가 API 31을 요구하지 않는다.
- 공장은 프로세스에 하나다. shaper를 부를 때마다 새로 짓고 ICU 래퍼만 나눠 쓰므로 여러 디코딩 thread가 함께 써도 된다.

지금 이 공장을 쓰는 것은 SVG와 Lottie뿐이다. luil 자신의 글자 그리기(label·입력 칸)는 여전히 글자마다 글리프 하나를 고르고 없는 글자를 대체 글꼴에서 찾는다 ([설치 글꼴 목록과 기본 UI 글꼴](concepts/fonts.md)). 그 경로를 HarfBuzz와 SkParagraph로 옮기는 것은 측정·caret·줄 넘김·편집 모델을 함께 바꾸는 일이라 따로 다룬다. 옵션은 그 일을 위해서도 미리 아카이브와 고지를 갖춰 둔다.

## 검증

- [`tests/image_animation_tests.cpp`](../tests/image_animation_tests.cpp)는 `lottie_frame_at`의 장 고르기·예고·끝·빈 값을 순수 함수로 잠그고, Skottie 대신 시각으로 색을 고르는 가짜 장면(`painted_scene`)으로 그리기·우선순위·`next_update`를 잠근다. 손으로 기록한 `SkPicture`로 벡터 그림이 칸 배율로 다시 그려지는 것(경계가 섞이지 않는 것)과 조각 밖이 잘리는 것도 같은 파일에서 본다. 어느 구성에서나 돈다.
- [`tests/vector_image_tests.cpp`](../tests/vector_image_tests.cpp)는 켠 구성에서 실제 SVG·Lottie 견본을 읽어 크기·실패 갈래·취소·그린 픽셀을 보고, 끈 구성에서 `unsupported_format`을 본다.
- [`tests/embedded_assets_tests.cpp`](../tests/embedded_assets_tests.cpp)는 고지가 켠 기능의 구성요소(Expat·HarfBuzz·libavif)만 담는지를 본다.

# 이미지

`image_element`는 배치된 사각형 안에 불변 픽셀 이미지나 애니메이션을 그린다. 이미지 생성, 디코딩, 배치, 그리기는 서로 분리된다. 앱은 이미지를 한 번 준비하고, 참조 공유되는 가벼운 손잡이를 frame마다 싣고, UI thread는 그 값을 그리기만 한다.

공개 API는 [`include/luil/ui/image_element.h`](../include/luil/ui/image_element.h)에 있다. 파일과 인코딩된 바이트의 디코딩은 [이미지 디코딩](image-decode-design.md), 애니메이션 시간 규칙은 [움직이는 이미지](image-anim-design.md)를 참고한다.

## 원시 픽셀로 만들기

`make_rgba_image(width, height, pixels)`는 빈틈없이 이어진 straight-alpha RGBA 8888 픽셀을 받는다. 각 행은 정확히 `width * 4` byte다. 이 함수는 픽셀을 Skia가 쓰는 premultiplied 표현으로 바꾸고 backing을 한 번 만든다.

어느 축이 양수가 아니거나, 필요한 byte 수가 overflow하거나, span 크기가 맞지 않으면 빈 `ui_image`를 돌려준다. 밖에서 받은 잘못된 픽셀 때문에 예외를 던지지 않는다.

`ui_image`는 다음을 제공한다.

- `valid()`는 실제 이미지와 빈 값을 가른다.
- `width()`와 `height()`는 화면 배율과 무관한 원본 픽셀 크기다.
- `backing()`은 라이브러리 그리기 코드가 쓰는 손잡이다.

backing은 불변이고 참조 수로 소유된다. `ui_image`를 복사해도 픽셀은 복사되지 않는다. 앱은 worker나 logic thread에서 이미지를 만들거나 디코딩한 뒤 tree에 게시하고, 별도 동기화 없이 UI thread에서 그릴 수 있다.

## 이미지 배치

`image_element`는 `image_config`를 받는다.

```cpp
luil::image_config config {};
config.image = avatar;
config.fit = luil::image_fit::cover;
config.description = u8"프로필 사진";

auto element = std::make_unique<luil::image_element>(image_id, std::move(config));
```

앱은 다른 element와 같은 배치 경로로 slot을 지정한다. 원본 크기를 묻는 별도 측정 단계는 없다. 고유 크기가 필요하면 `ui_image::width()`와 `height()`를 사용해 slot을 계산한다.

`image_fit`은 목적 사각형을 정한다.

| 값 | 결과 |
| --- | --- |
| `contain` | 비율을 유지하고 이미지 전체를 slot 안에 넣는다. |
| `cover` | 비율을 유지한 채 slot을 덮고 넘치는 부분을 자른다. |
| `fill` | 비율을 유지하지 않고 slot 전체로 늘린다. |

`image_destination(bounds, width, height, fit)`는 element와 같은 계산을 공개한다. slot이나 원본 축이 비었으면 빈 사각형을 돌려준다. 조각을 그릴 때는 이미지가 아니라 **조각의 크기**를 넣는다 (아래 [조각 그리기](#조각-그리기)).

그리기의 표본은 `image_config::sampling`이 정하고 기본값은 선형 filtering이다. 표본은 언제나 실제로 그리는 사각형 안에 머문다. 이미지 전체를 그리면 이미지 가장자리에 clamp하고, 조각을 그리면 조각의 가장자리에 clamp한다. `cover`는 배치 경계로 clip한다. 빈 이미지는 아무것도 그리지 않으며 임의의 오류 아이콘을 만들지 않는다.

## 조각 그리기

`image_config::source`는 이미지에서 잘라 낼 사각형이다. 비어 있으면 이미지 전체다. 한 장을 여러 그림으로 나눠 쓰는 아틀라스와 스프라이트 시트가 이 문으로 들어온다. 이미지는 여전히 불변이고 참조 공유되므로, 같은 시트를 가리키는 element를 여럿 놓아도 픽셀은 한 벌이다.

```cpp
luil::image_config config {};
config.image = icon_sheet;
config.fit = luil::image_fit::contain;
// 32×32 아이콘 시트의 세 번째 칸이다. 좌표는 이미지 자신의 픽셀이다.
config.source = { 64.0f, 0.0f, 32.0f, 32.0f };
config.sampling = luil::image_sampling::sharp;
```

`source`의 좌표는 **이미지 자신의 픽셀**이고 화면 배율과 무관하다. 배치 slot과 `image_destination`의 답은 창의 물리 픽셀이다. 두 뜻이 같은 `rect_f` 형식을 쓰므로 형식이 그 차이를 막아 주지 않는다. 배율이 2인 화면에서도 `{ 0, 0, 32, 32 }`는 이미지의 32×32 픽셀이지 화면의 32×32가 아니다.

`source`는 `image_config`의 꼬리에 있다. `animation`, `playback`과 같은 이유이며, 위치 초기화로 config를 짓는 앱 코드가 조용히 어긋나지 않게 한다.

`image_source_rect(source, width, height)`는 그리기와 같은 다듬기를 공개한다. 어떤 입력에도 예외를 던지지 않는다.

| 입력 | 결과 |
| --- | --- |
| 이미지 안에 온전히 든 사각형 | 그대로 |
| 이미지 밖으로 걸친 사각형 | 이미지 안으로 자른 나머지 |
| 빈 사각형, 폭이나 높이가 음수 | 이미지 전체 |
| 통째로 이미지 밖 | 이미지 전체 |
| 빈 이미지 | 빈 사각형 |

어긋난 조각을 "아무것도 그리지 않음"으로 처리하지 않는다. 그러면 앱은 이미지가 없는 것인지 조각이 어긋난 것인지 화면에서 가릴 수 없다.

`image_fit` 계산은 이미지가 아니라 조각의 크기로 한다. 8:1 시트에서 잘라 낸 정사각 스프라이트는 정사각으로 앉는다. 시트의 크기로 계산하면 `contain`이 시트의 비율로 자리를 잡아 조각이 엉뚱한 여백 안에 앉는다.

`source`에 소수점이 들어도 된다. `image_fit` 계산에 넣는 변은 **반올림한** 정수이고, 어느 변도 1 아래로 내려가지 않는다. 자르면 32.7픽셀짜리 조각이 32로 앉아 앱이 청한 비율에서 밀려나고, 0.5픽셀짜리 조각은 0이 되어 목적 사각형이 비고 그리기가 아무 말 없이 돌아선다. 그 화면은 "빈 이미지"와 구별되지 않아 아무도 실패를 보지 못한다. `decoded_image_size`가 축소한 축을 0으로 만들지 않는 것과 같은 규칙이다. 뽑는 자리 자체는 소수점 그대로이므로 반올림이 픽셀을 옮겨 오지는 않는다.

`source`는 애니메이션에도 그대로 적용된다. 모든 장이 같은 크기이므로 장이 바뀌어도 뽑는 자리는 흔들리지 않는다.

## 표본 방식

`image_config::sampling`은 이미지 픽셀을 화면 픽셀로 옮길 때 표본을 뽑는 방식이다.

| 값 | 결과 | 쓰임 |
| --- | --- | --- |
| `smooth` | 이웃 픽셀을 섞는다 (linear + mipmap). 기본값이다. | 사진, 썸네일, 큰 그림 |
| `sharp` | 가장 가까운 한 픽셀을 그대로 뽑는다 (nearest, mipmap 없음). | 픽셀 아트, 아이콘 아틀라스 |

`smooth`는 확대에 linear를, 축소에 mipmap을 함께 쓴다. mipmap이 없으면 크게 줄인 그림이 성기게 튄다. `sharp`는 그 섞음을 모두 끄므로 크게 확대해도 한 픽셀짜리 획이 뿌옇게 번지지 않는다. `sharp`에 mipmap을 함께 주는 선택지는 없다. mipmap 자체가 줄이면서 섞는 장치라 "섞지 않는다"와 뜻이 어긋난다.

`image_sampling`은 luil의 enum이다. 공개 API는 Skia 타입을 늘리지 않으며, Skia 값으로 옮기는 표는 `src/ui/image_element.cpp`의 익명 namespace 한 자리에만 있다.

## 조각과 mipmap 절충

조각을 그릴 때 그리기는 표본 제약을 조각의 크기에 따라 가른다.

- 조각이 이미지 **전체**면 빠른 제약이다. mipmap이 살아 있어 크게 줄인 그림의 품질이 그대로다.
- 조각이 **일부**면 엄격한 제약이다. 그러지 않으면 filter가 조각 밖 한두 줄까지 함께 읽어 아틀라스의 옆 스프라이트가 가장자리에 번져 든다.

Skia는 엄격한 제약에서 mipmap을 끈다. 따라서 조각을 크게 줄여 그리면 성기게 튄다. 이때의 답은 제약을 푸는 것이 아니라 **필요한 크기로 조각을 미리 짓는 것**이다. `make_rgba_image`로 그 크기의 이미지를 만들거나, `image_decode_options::max_width`와 `max_height`로 줄여 디코딩한다. 시트를 한 픽셀 여백으로 짓는 관례로 번짐을 덮는 방법도 있지만, 그것은 앱이 파일을 그렇게 짓기를 바라는 규약이라 라이브러리가 지킬 수 없다.

## 정지 이미지와 애니메이션

`image_config`에는 `image`와 `animation`이 따로 있다. `animation.valid()`가 참이면 애니메이션이 우선하고 정지 이미지는 보지 않는다. 따라서 형식을 미리 모르는 파일을 애니메이션 진입점으로 디코딩한 뒤 element 종류를 가르지 않고 놓을 수 있다.

재생 시계는 앱 상태이며 `image_config::playback`에 싣는다. 앱이 처음 시작한 시각을 유지하면 tree를 다시 지어도 재생이 다시 시작되지 않는다. 일시 정지, 재개, 유한 재생, 다시 그리기 예고는 [움직이는 이미지](image-anim-design.md)를 참고한다.

## 입력과 접근성

이미지는 기본적으로 표시 전용이다. 커서, 클릭, drag 동작이 없다. 누를 수 있는 이미지가 필요하면 이미지나 감싸는 element에 보통의 `ui_element::set_action`을 설정한다. 그러면 다른 control과 같은 hit test와 액션 규칙을 따른다.

`description`은 접근 가능한 이름이다. 비어 있지 않으면 image 역할로 접근 tree에 나타난다. 비어 있으면 장식 이미지로 보고 접근 tree에서 뺀다.

## 성능 지침

- 이미지는 한 번 만들거나 디코딩하고 손잡이를 재사용한다. frame을 지을 때마다 decoder를 부르지 않는다.
- 큰 파일은 표시할 크기에 가까운 `image_decode_options`로 디코딩한다. 작은 사각형에 그린다고 저장된 원본 픽셀이 줄지는 않는다.
- 움직일 필요가 없는 thumbnail은 정지 이미지 디코딩 경로를 쓴다.
- 시작·일시 정지 시각을 포함한 앱 상태는 element 밖에 둔다. 게시된 tree와 이미지 값은 불변으로 유지한다.
- 아틀라스는 한 장을 공유하고 `source`만 바꿔 쓴다. 조각마다 이미지를 따로 디코딩하면 픽셀이 그만큼 늘어난다.
- 크게 줄여 그릴 조각은 필요한 크기로 미리 짓는다. 조각 그리기에서는 mipmap이 도와주지 않는다.

## 반드시 유지할 불변식

- `ui_image`는 만들어진 뒤 바뀌지 않고, 복사는 픽셀이 아니라 참조를 공유한다.
- `image_config::source`의 좌표는 이미지 픽셀이고, slot과 `image_destination`의 좌표는 창의 물리 픽셀이다.
- 빈 `source`는 이미지 전체를 뜻한다. 그 옆에 "조각이 있는가"를 따로 나르는 값을 두지 않는다.
- `image_source_rect`는 어떤 입력에도 던지지 않으며, 빈 이미지에서만 빈 사각형을 돌려준다.
- `image_fit` 계산은 조각의 크기로 한다. 그 크기는 반올림한 정수이고 1 아래로 내려가지 않는다. `image_source_rect`를 지난 조각은 언제나 무언가를 그린다.
- 조각이 이미지 전체가 아니면 표본은 조각을 넘지 않는다.
- `image_config`의 새 필드는 꼬리에 붙인다. 기존 필드의 선언 순서는 바꾸지 않는다.
- 공개 헤더는 Skia 타입을 이름으로도 드러내지 않는다. `image_sampling`에서 Skia 값으로 가는 변환은 `src/ui/image_element.cpp` 안에만 있다.

## 검증

[`tests/image_element_tests.cpp`](../tests/image_element_tests.cpp)는 RGBA 입력 검증, fit 계산, `image_source_rect`의 다듬기, 조각 크기가 목적 사각형의 비율을 정한다는 것, 소수점이 든 조각과 한 픽셀보다 좁은 조각이 사라지지 않는다는 것, 기본 비상호작용 성질, 실제 raster 출력을 확인한다. [`tests/raster_draw_tests.cpp`](../tests/raster_draw_tests.cpp)는 시트에서 조각 하나만 그려지고 이웃 색이 섞이지 않는지, `sharp`가 확대에서 경계를 지키고 `smooth`는 같은 자리를 섞는지를 픽셀로 확인한다. [`tests/image_animation_tests.cpp`](../tests/image_animation_tests.cpp)는 같은 element의 애니메이션 경로를 확인한다.

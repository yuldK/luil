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

`image_destination(bounds, width, height, fit)`는 element와 같은 계산을 공개한다. slot이나 원본 축이 비었으면 빈 사각형을 돌려준다.

그리기는 선형 filtering을 쓰고 샘플을 이미지 가장자리에 clamp한다. `cover`는 배치 경계로 clip한다. 빈 이미지는 아무것도 그리지 않으며 임의의 오류 아이콘을 만들지 않는다.

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

## 검증

[`tests/image_element_tests.cpp`](../tests/image_element_tests.cpp)는 RGBA 입력 검증, fit 계산, 기본 비상호작용 성질, 실제 raster 출력을 확인한다. [`tests/image_animation_tests.cpp`](../tests/image_animation_tests.cpp)는 같은 element의 애니메이션 경로를 확인한다.

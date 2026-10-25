# 장 하나 읽기·고품질 축소·큰 그림 그리기·축 맞춤

큰 그림과 움직이는 그림을 다루는 앱이 직접 해 오던 일 넷을 라이브러리가 맡는다.

| 앱이 하던 일 | 이제 |
| --- | --- |
| 움직이는 그림의 장 수·길이를 알려고 전부 디코드하거나, Skia 코덱을 직접 부른다 | `read_image_frames_bytes`·`read_image_frames_file`이 픽셀 없이 목록만 읽는다 |
| 특정 장(미리 보기·탐색)을 보려고 전부 디코드한다 | `decode_image_frame_bytes`·`load_image_frame_file`이 그 장 하나를 합성까지 마쳐 준다 |
| 섬네일을 선형+mipmap보다 곱게 줄이려고 자기 리샘플러를 든다 | `image_decode_options::downscale = high_quality`, 또는 `downscale_image` |
| GPU 최대 텍스처보다 큰 그림(긴 세로 그림)이 GPU 렌더러에서 비어 보여 CPU로 쪼개 그린다 | `image_element`가 그 그림을 타일로 나눠 그린다 |
| 폭 맞춤·높이 맞춤을 `zoom_by`·`pan_by` 짝으로 직접 계산한다 | `zoom_to_fit`·`zoom_fit_actions`의 `zoom_fit` 축 |

공개 형식은 [image_decode.h](../include/luil/ui/image_decode.h)와 [zoom_view_element.h](../include/luil/ui/zoom_view_element.h)다.

## 장 목록과 장 하나

```cpp
luil::image_decode_error error {};
const luil::image_frame_table table { luil::read_image_frames_bytes(bytes, error) };
// table.size, table.durations.size(), table.play_count
const luil::ui_image middle { luil::decode_image_frame_bytes(bytes, table.durations.size() / 2, {}, error) };
```

- **목록은 픽셀을 펴지 않는다.** 머리를 읽고 장 목록을 짓는 것(파일 전체를 한 번 훑는다)까지만 한다. 길이와 바퀴 수는 `load_animated_image_file`이 같은 파일에서 내는 값과 같다. 너무 짧게 적힌 장은 `animation_frame_minimum`으로 올리고, 바퀴 수는 `image_plays_forever` 규약으로 옮긴다.
- 크기(`size`)는 EXIF로 돌려세운 뒤의 **원본** 크기다. 상한(`max_width`·`max_height`)은 보지 않는다. 그것은 장을 펼 때의 일이다.
- 움직이지 않는 파일도 한 장짜리 목록이다(움직이는 문의 규약과 같다).
- **장 하나는 합성이 끝난 그림이다.** `fPriorFrame = kNoFrame`으로 청하면 코덱이 그 장에 필요한 앞 장들을 스스로 먼저 푼다. 그래서 150번 장을 읽는 값은 대개 앞 장 150개를 푸는 값이다. 여러 장을 차례로 볼 앱은 `load_animated_image_file`이 낫다.
- 장은 원본 크기로 펴고 다 된 장을 줄인다(움직이는 그림의 규칙과 같다). 들고 있는 것은 한 장이라, 장을 모두 합한 상한(`animation_too_large`)은 걸리지 않는다. 원본 한 장의 상한(`max_source_pixels`)은 걸린다.
- 범위 밖 장 번호는 `frame_out_of_range`이고, `error.frame`이 그 번호를 든다.
- 취소는 펴기 전과 직후에 묻는다. 코덱 안의 사슬 풀기는 중간에 끊을 수 없다.

## 고품질 축소

```cpp
luil::image_decode_options options { .max_width = 400 };
options.downscale = luil::image_downscale::high_quality;
options.cancelled = [&] { return stop.load(); };
const luil::ui_image thumbnail { luil::load_image_file(path, options, error) };
// 이미 가진 그림이면
const luil::ui_image smaller { luil::downscale_image(picture, options, error) };
```

- `standard`(기본)는 지금까지의 선형+mipmap 표본이다. 그리기의 `smooth`와 같은 값이라, 디코드에서 줄인 그림과 그리기에서 줄인 그림이 같아 보인다. `high_quality`는 그 짝을 일부러 깬다. 디코드에서만 곱게 줄이고, 그리기는 그대로다.
- **두 단계다.** 두 축이 모두 목표의 두 배 이상인 동안은 2×2 평균으로 반씩 줄인다. 남은 두 배 미만은 Mitchell–Netravali(B = C = 1/3) 3차 필터로 맞춘다. 반 평균은 싸고 정확하며 앨리어싱을 거의 다 걷어 낸다. 마지막 한 걸음만 넓은 필터를 쓰니 값이 크지 않다.
  - 필터는 축마다 따로 건다(가로 다음 세로). 줄이는 비율만큼 커널을 늘리고, 가장자리에서는 그림 안에 든 가중치로 다시 나눈다.
  - 계산은 premul에서 한다. straight를 섞으면 완전히 투명한 픽셀의 색이 가장자리에 테로 번진다(디코드의 다른 축소와 같은 규칙이다).
  - Mitchell은 음의 꼬리가 있어 값이 범위를 넘을 수 있으므로 0–255로 자르고, 색은 알파를 넘지 않게 자른다(premul 불변식).
- **취소는 출력 `image_downscale_cancel_rows`(32)줄마다 묻는다.** 반 평균의 각 단계와 Mitchell의 두 축이 모두 그렇다. 4000×7000 원본도 몇 줄을 푸는 시간 안에 멈춘다. 접힌 축소는 빈 값과 `cancelled`다.
- 줄이는 순서는 파일 축이다. 파일 축 목표 크기로 줄인 뒤 EXIF 방향은 1:1로 돌려세운다. 정지 그림과 움직이는 그림이 같은 한 함수(`finish_frame`)를 지나므로 두 문이 갈라지지 않는다. 움직이는 그림에서는 장마다 같은 축소를 건다.
- `downscale_image`는 같은 옵션 묶음의 `max_width`·`max_height`·`cancelled`를 본다. 늘 고품질로 줄이며, 이미 상한 안이면 같은 그림을 그대로 돌려준다. 벡터 그림(SVG)은 줄일 픽셀이 없으므로 `unsupported_format`이다.

## 큰 그림 그리기

GPU 렌더러(Windows Direct3D 12, Android Vulkan)는 최대 텍스처(흔히 16384)보다 큰 그림을 한 장으로 올리지 못한다. 그 그림은 지금까지 GPU에서만 비어 보였다.

- `image_element`의 래스터 그리기를 `SkTiledImageUtils::DrawImageRect`로 바꿨다. 텍스처에 들지 않는 비트맵 그림만 타일로 나눠 그리고, 나머지는 지금까지의 `drawImageRect` 그대로다. CPU 렌더러에서는 아무것도 바뀌지 않는다.
- 표본과 조각 제약(`kFast`·`kStrict`)도 그대로 넘긴다. 타일 경계에서 옆 타일을 읽는 일은 Skia가 맡는다.
- 앱은 아무것도 고르지 않는다. 확대 보기 안의 그림도 같은 element라 같은 길을 탄다.

## 축 맞춤

```cpp
const auto fit_width { luil::zoom_fit_actions(config, viewport, 16.0f, 4.0f, luil::zoom_fit::width) };
```

- `zoom_fit::whole`(기본)은 지금까지의 맞춤이다. `width`는 내용 폭을, `height`는 내용 높이를 여백 뺀 창에 맞춘다.
- **다른 축은 지금 창 가운데에 있는 내용 자리를 지킨다.** 긴 세로 그림을 읽다가 폭 맞춤을 눌러도 읽던 자리가 그대로다. 그 뒤 경계 다듬기(`zoom_bounds`)가 평소처럼 걸린다. `contain`이면 창보다 작은 축은 가운데로 가고, 큰 축은 빈 곳이 보이지 않게 밀린다.
- 맞춤 축은 `maximum_fit_zoom`과 `minimum_zoom`·`maximum_zoom`으로 자른다.
- `zoom_fit_actions`는 그 상태로 가는 `zoom_by`·`pan_by` 메시지다. 맞춤 단추와 두 번 탭이 쓰던 계산을 그대로 공개한 것이다. 단추 글은 앱이 단다.

## 검증

- `tests/image_decode_tests.cpp`:
  - 장 목록이 `load_animated_image_file`의 길이·바퀴 수와 같은지, 픽셀 없이 읽는지;
  - 가운데 장과 마지막 장이 전체 디코드의 그 장과 같은 픽셀인지, 범위 밖 번호와 취소;
  - 4000×7000 그림을 400폭으로 고품질 축소한 크기와 색, 32줄마다 묻는 취소;
  - 반 평균 경계, 알파 가장자리의 테, EXIF 방향과의 순서.
- `tests/raster_draw_tests.cpp`: 긴 세로 그림을 CPU에서 그린 색.
- `tests/gpu_image_tiling_tests.cpp`: 헤드리스 Direct3D 12 문맥을 세운다. 최대 텍스처보다 한 픽셀 넓은 그림을 `image_element`로 그리고 픽셀을 읽어 색이 닿았는지 본다. 하드웨어 어댑터가 없으면 건너뛴다.
- `tests/zoom_view_tests.cpp`: 폭·높이 맞춤의 배율과 원점(`overlap`·`contain`), 메시지 짝.
- 데모:
  - "이미지" 페이지: 움직이는 그림의 장 번호 보기와 고품질 섬네일;
  - "확대" 페이지: 폭 맞춤·높이 맞춤 단추.

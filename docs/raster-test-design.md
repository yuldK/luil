# Raster 그리기 test

Raster test는 UI의 판정이 실제 픽셀로 이어지는지 확인한다. 순수 geometry와 상태 함수는 여전히 우선적인 test 경계지만, 빠진 draw 호출, 잘못된 clip, 조기 return, fill과 border의 혼동까지 잡지는 못한다. Raster harness는 창이나 GPU 없이 그 빈틈을 확인한다.

harness는 [`tests/raster_probe.h`](../tests/raster_probe.h)와 [`tests/raster_probe.cpp`](../tests/raster_probe.cpp)에 있다.

## `raster_frame`

`luil::testing::raster_frame`은 `SkBitmap`, palette, 화면 배율을 소유한다. 생성자의 너비와 높이는 물리 픽셀이다. `draw(tree, interaction, now)`는 bitmap을 `window_background`로 지운 뒤 보통의 tree 그리기 경로를 부른다.

기본 배율은 2.0이다. 배율 1에서는 antialiasing된 1 논리 픽셀 선이 물리 픽셀 둘에 일부씩 섞여 foreground와 background 어느 색에도 정확히 맞지 않을 수 있다. 배율 2에서는 같은 선의 중앙에 완전히 덮인 픽셀이 생겨 안정적인 정확 색 비교가 가능하다.

픽셀 질의 함수의 모든 사각형은 물리 픽셀 좌표다.

- `pixel_at(x, y)`는 풀린 `ui_color`를 돌려주며 bitmap 밖은 0이다.
- `count_color(area, color)`는 영역 안에서 정확히 같은 색의 픽셀 수를 센다.
- `contains_color(area, color)`는 같은 색이 하나라도 있는지 답한다.

기본 그리기 시각은 steady clock epoch다. 애니메이션, caret blink, tooltip 지연, spinner test는 명시적인 시각을 넘긴다. 고정 시각을 사용해야 같은 tree가 실행마다 같은 픽셀을 만든다.

harness는 typeface를 싣지 않는다. 도형, 색, clip, layer를 검사할 때 test element의 글은 비운다. OS 글꼴과 rasterizer 판본에 따라 결과가 달라지는 일을 피한다.

## 안정적인 단언

옳고 그름을 실제로 가르는 성질을 검사한다.

- fill과 border를 가를 때는 가운데 픽셀을 읽는다.
- 두 결과가 같은 색을 쓸 때는 덮은 넓이를 센다.
- clipping은 경계 안과 밖을 함께 확인한다.
- test container가 자식을 아예 그리지 않아도 통과하지 않도록 배경이 아닌 픽셀이 생겼는지 확인한다.

작은 component 계약에는 전체 이미지 golden file보다 집중된 픽셀과 넓이 단언을 우선한다. 전체 snapshot은 관련 없는 antialiasing, palette, 글꼴 변경에도 깨진다.

Raster test용 container는 `draw_children`을 실제로 불러야 한다. 빈 `draw`를 가진 test panel은 모든 자식을 숨겨 구현 결함과 test 결함을 구별할 수 없게 한다.

## 적용 범위

[`tests/raster_draw_tests.cpp`](../tests/raster_draw_tests.cpp)는 기본 버튼이 fill이고 초점 표시는 ring인지, OS가 drag visual을 그릴 때 library ghost만 빠지고 drop 대상 강조는 남는지 확인한다. 이미지 픽셀 test는 [`tests/image_element_tests.cpp`](../tests/image_element_tests.cpp), [`tests/image_animation_tests.cpp`](../tests/image_animation_tests.cpp), [`tests/image_decode_tests.cpp`](../tests/image_decode_tests.cpp)에 있다.

동작이 실제 canvas 연산에 달렸을 때 raster test를 추가한다. 정책, geometry, 시간 선택은 순수 test도 함께 두어 실패가 판정과 그리기 중 어느 쪽인지 드러낸다.

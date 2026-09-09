# 움직이는 이미지

움직이는 이미지는 합성이 끝난 불변 `ui_image` frame 목록과 정규화된 시간표다. 앱이 재생 시계를 소유하고, element는 시각으로 현재 frame을 계산한다. element 안에는 바뀌는 재생 상태가 없다.

공개 타입과 시간 함수는 [`include/luil/ui/image_element.h`](../include/luil/ui/image_element.h)에 있다. GIF, animated WebP, APNG 로딩은 [이미지 디코딩](image-decode-design.md)을 참고한다.

## 애니메이션 만들기

`make_animated_image(frames, durations, play_count)`는 codec 없이 `ui_animated_image`를 만든다. 계산으로 만든 애니메이션과 결정적인 test에 쓸 수 있다.

다음 경우 빈 값을 돌려준다.

- frame이 없다.
- frame 수와 duration 수가 다르다.
- 빈 이미지가 하나라도 섞였다.
- frame 크기가 서로 다르다.

받아들인 frame은 불변 공유 손잡이다. 같은 frame으로 다른 시간표를 만들어도 픽셀을 복사하지 않는다.

10 ms 이하 duration은 100 ms로 올린다. 오래된 애니메이션 파일에서 이런 값은 흔히 “가능한 한 빨리”를 뜻하고, 그대로 두면 UI thread를 쉬지 않고 깨운다. 실제 빠른 애니메이션이 지정한 20 ms처럼 문턱보다 큰 값은 보존한다.

`play_count`는 처음부터 끝까지 도는 총 횟수다. `image_plays_forever` (`-1`)는 무한 반복이다. 그 밖의 1 미만 값은 1로 본다.

## 이미지 값

`ui_animated_image`는 유효성, 공통 너비와 높이, frame 수, frame별 이미지와 duration, 총 재생 횟수를 제공한다. `animated()`는 frame이 둘 이상이라는 뜻이다. 한 장짜리 값도 유효하지만 다시 그리기를 예약하지 않는다.

각 frame은 완전한 논리 canvas다. 17번을 그리기 위해 16번을 먼저 그릴 필요가 없다. frame을 놓치거나 순서 밖에서 다시 그리거나 tree를 재생성해도 합성이 깨지지 않는다. 그 대가로 메모리는 보관한 frame 수에 비례한다.

## 재생 시계

`image_playback`에는 steady clock 시각 둘이 선택값으로 있다.

- `started`는 재생을 시작하거나 처음부터 다시 시작한다.
- `paused_at`은 특정 시각에 재생을 멈춘다.

`started`가 비어 있으면 아직 시작하지 않은 상태다. 첫 frame을 보이고 update를 예약하지 않는다. 앱 상태에 최초 시작 시각을 유지해야 한다. frame을 다시 지을 때마다 현재 시각을 넣으면 매번 처음부터 시작해 첫 frame에 멈춘다.

```cpp
state.playback.started = std::chrono::steady_clock::now();

luil::image_config config {};
config.animation = state.animation;
config.playback = state.playback;
```

일시 정지는 `paused_at`에 현재 시각을 넣는다. `playback_resumed(playback, now)`는 멈춘 동안만큼 시작 시각을 밀어 같은 장면에서 이어 간다. 시작하지 않았거나 멈추지 않은 값은 그대로 돌려준다.

`playback_elapsed(playback, now)`는 시작 전에는 0이고, 미래의 시작 시각도 0으로 clamp하며, 일시 정지 중에는 더 늘지 않는다.

## frame 선택

`animation_frame_at(durations, play_count, elapsed)`은 현재 색인 `index`와 다른 frame이 처음 서는 경계 `next_at`을 함께 돌려준다.

frame 구간은 반열림이다. 0번 frame이 100 ms라면 정확히 100 ms에는 1번 frame이 선다. 모든 경계에서 답이 하나다.

유한 재생은 마지막 바퀴가 끝난 뒤 마지막 frame에 머물고 다음 경계를 내지 않는다. 무한 재생은 첫 frame으로 돌아간다. 빈 목록, 한 장짜리, 전체 duration이 0인 목록, 양수가 아닌 elapsed는 0번 frame에 서고 update를 청하지 않는다.

`next_at`은 경계를 지나도 같은 frame이라면 그 경계를 건너뛴다. 보이는 변화 없이 창이 계속 깨어나는 고리를 막는다.

이 함수는 duration, 횟수, elapsed만 보는 순수 함수다. 이전 frame 상태를 전진시키지 않으므로 그리기가 frame을 건너뛰어도 현재 시각의 답이 맞다.

## element 연결

`image_config::animation`이 유효하면 `image_element::draw`는 `draw_context::now`로 frame을 고르고 정지 이미지와 같은 `contain`, `cover`, `fill` 경로로 그린다.

`image_element::next_update`도 같은 frame 선택 결과를 쓴다. 다음에 실제로 다른 frame이 서는 steady clock 시각을 돌려준다. 시작 전, 일시 정지, 한 장짜리, 재생 완료에는 `nullopt`다. platform은 고정 frame rate로 polling하지 않고 실제 경계 사이에 잘 수 있다.

애니메이션은 `image_config::image`보다 우선한다. 하나의 element 타입으로 정지 입력과 움직이는 입력을 모두 표시할 수 있다.

## 디코딩 보장

Skia decoder는 public 값을 만들기 전에 frame dependency, blend, disposal을 해결한다. 무조건 직전 frame을 쓰지 않고 각 frame이 요구하는 predecessor를 사용한다. “restore previous” disposal에는 필요한 합성 상태를 보존한다.

움직이는 입력은 원본 크기로 합성한 뒤 완성된 frame을 축소한다. 모든 frame에 같은 EXIF orientation을 적용한다. 큰 애니메이션에는 원본 frame별 상한과 축소 후 전체 frame 픽셀 상한이 모두 적용된다.

frame 하나가 실패하면 전체를 거절하는 것은 **기본값이지 유일한 규칙이 아니다.** `image_decode_options::incomplete`를 `accept`로 두면 첫 실패 앞까지의 frame이 값이 되고, 잘렸다는 사실은 오류로 함께 온다. 그때 `error.frame`이 어디서 끊겼는지를 든다. 한 장도 풀지 못했으면 그때도 실패다. frame이 없는 애니메이션은 값이 아니기 때문이다. 어느 쪽이든 **돌려주는 frame은 온전한 frame이다.** 반쯤 합성된 장은 목록에 들어가지 않는다. 자세한 것은 [이미지 디코딩](image-decode-design.md)의 잘린 입력 정책을 본다.

`accept`가 짧은 목록으로 여는 것은 **잘린 입력 하나다.** 갈래가 `incomplete_input`이 아닌 실패는 그 값에서도 통째로 거절이다. 5번 frame의 LZW 블록이 깨진 GIF(`frame_failed`)나 40번 frame에서 자리를 잡지 못한 애니메이션(`out_of_memory`)은 앞의 frame이 아무리 많이 풀렸어도 빈 값과 오류로 온다. 자료가 모자란 것은 밖에서 온 사고지만 나머지는 우리 쪽 사고이고, 그것을 반쪽 애니메이션으로 덮으면 고쳐야 할 잘못이 화면에서 "원래 그런 그림"이 된다. 정지 경로와 애니메이션 경로는 이 판정을 하나의 식으로 공유한다.

`accept`로 받은 짧은 목록도 여느 애니메이션과 다르지 않다. 한 장만 남았으면 `animated()`가 거짓인 한 장짜리 값이고, 그것은 창을 깨우지 않는다. 재생 시계, frame 선택, update 예약은 목록이 왜 짧은지를 알지 못한다.

디코딩 도중에 그만두는 것은 별개의 손잡이다(`image_decode_options::cancelled`). 접힌 디코딩은 짧은 목록이 아니라 **빈 값**을 돌려준다. 접었는데 애니메이션이 오면 앱이 “접었다”와 “끝났다”를 다시 갈라 들어야 한다.

## 불변

- 모든 frame은 같은 크기의 합성이 끝난 논리 canvas다. 순서 밖의 그리기와 건너뛴 frame이 안전하다.
- 값에 든 frame은 언제나 온전한 frame이다. 짧은 목록은 있어도 반쪽 frame은 없다.
- 짧은 목록이 서는 자리는 잘린 입력(`incomplete_input`)에 `accept`를 고른 자리 하나다. 다른 갈래의 실패는 정책과 무관하게 빈 값이다.
- 장을 푸는 도중의 실패는 갈래와 무관하게 `error.frame`에 그 장의 번호를 든다.
- `image_playback`은 앱이 들고 element는 들지 않는다. 같은 시계를 실은 두 element는 함께 돌고 함께 선다.
- 10 ms 이하 duration은 100 ms로 올라간다. 그 규칙이 사는 자리는 `make_animated_image` 하나다.
- `play_count`는 총 바퀴 수다. `image_plays_forever`(-1)만 무한이고 1 미만은 1로 본다.

## 검증

[`tests/image_animation_tests.cpp`](../tests/image_animation_tests.cpp)는 시계, 일시 정지와 재개, 정확한 경계, 유한·무한 재생, duration 정규화, 잘못된 생성, raster 출력, fit, update 예약을 확인한다. [`tests/image_decode_tests.cpp`](../tests/image_decode_tests.cpp)는 GIF, WebP, APNG, dependency 합성, disposal, 손상 입력, 축소, 메모리 상한과 함께 잘린 애니메이션의 두 정책(통째로 거절 / 푼 만큼 받기), 잘린 것이 아닌 실패가 `accept`에서도 통째로 거절된다는 것, `error.frame`이 실제로 걸린 장을 든다는 것, frame 고리 도중의 취소를 확인한다.

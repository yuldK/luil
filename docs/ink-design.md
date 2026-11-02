# 잉크 캔버스

펜으로 그린 획을 받아 그리고 지운다. 컨트롤러는 펜 접촉 하나를 통째로 쥐고 압력·기울기·시각이 든 표본을 캔버스에 건넨다. 획은 앱 상태이고 캔버스는 그 획을 그리며 앱 메시지를 낸다. 지우개 끝과 지우개 도구는 닿은 획을 통째로 지운다.

공개 형식은 [ui_element.h](../include/luil/ui/ui_element.h)의 입력 역할 `ink_source`·`ink_input`·`ink_sample`, 새 헤더 [ink.h](../include/luil/ui/ink.h)의 `ink_point`·`ink_style`·`ink_stroke`·`ink_layer`와 도우미, 새 헤더 [ink_canvas_element.h](../include/luil/ui/ink_canvas_element.h)의 `ink_canvas_config`·`ink_canvas_element`다. 펜 값이 이벤트에 실리는 길과 지우개 계약의 개정은 [잉크 입력](ink-input-design.md)에 따로 적었다.

## 문제

- 펜 접촉은 지금 마우스 왼쪽 버튼이다([터치 제스처와 펜 입력](touch-pen-input-design.md)). 앱이 그리기를 지으려면 `pointer_drag_target`을 세워야 한다. 그 손잡이가 받는 `ui_action_context`(ui_element.h 99행)는 자리와 수식 키뿐이라 압력·시각·포인터 id가 없다.
- 손잡이가 된 요소 위에서는 터치가 흐르지 않는다. `process_touch_press`(ui_interaction.cpp 1678행)는 `pointer_drag()`가 있는 요소를 확대 보기 이동에서 뺀다. 펜으로 그리는 화면을 손가락으로 옮길 수 없다.
- 지우개 끝의 접촉은 추적기가 삼킨다(`pointer_sequence_tracker::accept`, pointer_sequence.cpp 44·72·76·105행과 `cancel`의 146행). 앱은 지우개를 받을 길이 없다. 문서도 "잉크 편집은 후속 기능의 범위다"라고 남겼다(touch-pen-input-design.md 24행).
- 압력은 Win32가 이미 읽는다(`GetPointerPenInfo`, window_surface.cpp 1171행). 그러나 `pointer_sample`로 옮기는 것은 배럴과 지우개 표식뿐이다(1282~1283행). Android는 압력 축을 켜지도 않는다(android_app.cpp 653~654행은 휠 축 둘만 켠다).
- 획 수천 개를 그릴 자리가 없다. tree는 표본이 올 때마다 다시 지어지고 요소는 매번 새로 선다. 요소에 캐시를 두면 다음 tree에서 사라진다.

## 공개 형식

```cpp
// include/luil/ui/ui_element.h: zoom_source 뒤에 둔다.
enum class ink_phase { begin, extend, end, cancel };

// 표본 하나다. 좌표는 표면 client 물리 픽셀이다 (이벤트와 같다).
//  - pressure는 0~1이고, 장치가 알리지 않으면(마우스·터치·압력 없는 펜) 비어 있다.
//  - 기울기는 도(-90~90)이고 W3C Pointer Events의 tiltX·tiltY와 같다. 모르면 0이다.
struct ink_sample
{
    float x { 0.0f };
    float y { 0.0f };
    std::chrono::steady_clock::time_point time {};
    std::optional<float> pressure {};
    float tilt_x { 0.0f };
    float tilt_y { 0.0f };
};

// `eraser`는 지우개 끝의 접촉이다. `interrupted`는 `end`가 정상적인 뗌이 아니라는 뜻이다
// (입력 큐 유실, 접촉 중 배럴 전환). 받은 표본까지가 획이다. cancel의 `sample`에는 뜻이 없다.
struct ink_input
{
    ink_phase phase { ink_phase::begin };
    pointer_device device { pointer_device::pen };
    std::uint32_t pointer_id { 0 };
    bool eraser { false };
    bool interrupted { false };
    ink_sample sample {};
};

// 접촉 하나를 처음부터 끝까지 받는 역할이다. 콜백은 input thread에서 부른다 (최신 tree의 factory).
//  - 기본은 펜만 받는다. `mouse`·`touch`를 켜면 마우스 왼쪽 버튼과 손가락도 받는다.
//  - `eraser`가 거짓이면 지우개 접촉은 일반 컨트롤처럼 삼킨다.
struct ink_source
{
    std::function<std::vector<input_action>(const ink_input&)> on_ink {};
    bool mouse { false };
    bool touch { false };
    bool eraser { true };
};
// ui_element: void set_ink_source(std::optional<ink_source>); const ink_source* ink() const noexcept;
```

```cpp
// include/luil/ui/ink.h
struct ink_point { float x { 0.0f }; float y { 0.0f }; float pressure { 1.0f }; }; // 내용 논리 픽셀
// color가 비면 팔레트의 `primary_foreground`다 (결정 6). width는 가장 센 압력의 폭이다 (내용 논리 픽셀).
// 폭 = width × (minimum_ratio + (1 − minimum_ratio) × pressure)이다.
struct ink_style { std::optional<ui_color> color {}; float width { 2.0f }; float minimum_ratio { 0.25f }; };
// 굳은 획 하나다. 만들 때 그릴 path와 경계를 지어 든다. 값이 바뀌지 않아 thread 사이로 나눠 그린다.
// 접근자: id(), points(), style(), bounds().
class ink_stroke;
// 유한하지 않은 점은 버린다. 점이 없으면 빈 획이다. 점 하나는 둥근 점이다.
[[nodiscard]] ink_stroke make_ink_stroke(std::uint64_t id, std::vector<ink_point> points, ink_style style);
// 확정 때 점을 줄인다 (Ramer-Douglas-Peucker). 자리 오차와 압력 오차를 함께 본다. 0이면 그대로다.
[[nodiscard]] std::vector<ink_point> simplify_ink_points(std::span<const ink_point> points, float tolerance, float pressure_tolerance);
// 굳은 획 묶음이다. 불변 값이고 그림 한 장(SkPicture)을 memo로 든다 (결정 7).
// 접근자: strokes(). 더하기·빼기는 새 묶음을 돌려준다: with(ink_stroke), without(span<const uint64_t> ids).
class ink_layer;
// 지우개가 `from`에서 `to`까지 반지름 `radius`로 쓸 때 닿는 획 id다. 경계 상자로 먼저 거른다.
[[nodiscard]] std::vector<std::uint64_t> ink_strokes_hit(const ink_layer& layer, ink_point from, ink_point to, float radius);
// 묶음을 그림 하나로 옮긴다 (내보내기). 기본색 획은 `palette`로 칠한다.
[[nodiscard]] ui_image render_ink_image(const ink_layer& layer, size_f size, float scale, const ui_color_palette& palette);
```

```cpp
// include/luil/ui/ink_canvas_element.h
enum class ink_tool { pen, eraser };
// 캔버스가 앱에 건네는 획 입력이다. 자리는 내용 좌표다.
struct ink_stroke_input { ink_phase phase {}; pointer_device device {}; std::uint32_t pointer_id {}; ink_point point {}; float tilt_x {}; float tilt_y {}; bool interrupted { false }; };
//  - strokes는 굳은 획, wet는 진행 중 획이다 (결정 2). eraser_radius는 내용 논리 픽셀이다.
//  - zoom·rotation·content_bounds는 내용 좌표 변환이다 (rotate-gesture-design.md의 규약). 확대 보기 밖이면 기본값이다.
//  - access_name·access_summary가 비면 기본 문구다 (G6).
struct ink_canvas_config
{
    std::u8string owner {};
    ink_layer strokes {};
    ink_stroke wet {};
    ink_tool tool { ink_tool::pen };
    bool mouse { false };
    bool touch { false };
    float eraser_radius { 8.0f };
    float zoom { 1.0f }, rotation { 0.0f };
    std::optional<rect_f> content_bounds {};
    std::function<input_action(const ink_stroke_input&)> on_stroke {};
    std::function<input_action(std::vector<std::uint64_t> ids)> on_erase {};
    std::u8string access_name {};
    std::function<std::u8string(std::size_t strokes)> access_summary {};
};
class ink_canvas_element final : public ui_element { public: explicit ink_canvas_element(ink_canvas_config config); };
```

```cpp
// 앱은 점을 모으다가 끝에 굳힌다. build: .strokes = layer_, .wet = luil::make_ink_stroke(0, points_, style_)
if (input.phase == luil::ink_phase::begin) points_ = { input.point };
else if (input.phase == luil::ink_phase::extend) points_.push_back(input.point);
else if (input.phase == luil::ink_phase::end) layer_ = layer_.with(luil::make_ink_stroke(next_id_++, luil::simplify_ink_points(std::exchange(points_, {}), 0.25f, 0.05f), style_));
else points_.clear(); // cancel
```

## 규칙

입력.

- 펜촉 접촉이 캔버스(`ink()`가 그 장치를 받는 요소)를 누르면 컨트롤러가 그 접촉을 쥔다. 누름마다 `begin`, 이동마다 `extend`, 뗌에 `end`를 하나씩 낸다. 묶여 온 과거 표본도 이동 하나씩이다([잉크 입력](ink-input-design.md)).
- 캔버스 판정은 지금의 누름 대상 찾기(`press_target`) 뒤, 확대 보기 이동(`zoom_owner_at`) 앞이다. 캔버스 위의 자식 단추는 지금처럼 먼저 hit한다. 확대 보기 안의 캔버스는 보기 이동보다 앞선다([확대 보기](zoom-view-design.md)의 "자식의 손잡이가 앞선다"와 같다).
- 쥔 동안 캔버스 밖으로 나간 점도 같은 획이다. 그리기는 캔버스 안으로 자른다. 이동마다 최신 tree에서 같은 id의 캔버스를 다시 찾는다. 사라졌거나 숨었거나 비활성이면 `cancel`을 내고 접촉의 나머지를 삼킨다. 표면 소멸도 같다.
- 시스템의 접촉 취소(`pointer_cancelled_event`)는 `cancel`이다. 손바닥 판정의 취소가 여기로 온다. 배럴 전환으로 온 취소와 입력 큐 유실은 `end`에 `interrupted`를 단다([잉크 입력](ink-input-design.md#결정)의 결정 4).
- 누름 관찰(`interaction_policy::on_press`)은 펜촉·마우스 왼쪽·손가락의 누름이라 지금처럼 부른다. 텍스트 초점은 지금처럼 거둔다. 캔버스는 클릭·더블 클릭·길게 누르기를 만들지 않는다. 배럴 버튼 접촉은 지금처럼 우클릭이다.
- 마우스는 `mouse`를 켠 캔버스에서만 그린다. 끄면 지금처럼 일반 요소다. 손가락은 `touch`를 켠 캔버스에서만 그린다. 끄면 손가락은 감싼 창의 스크롤, 확대 보기 이동·핀치를 한다. 켜면 `pointer_drag()`처럼 누르는 즉시 쥐고 스크롤 후보가 아니다(1678행의 조건에 `ink()`를 더한다).
- 그리는 동안의 둘째 손가락, 펜이 쥔 동안의 터치 누름은 지금처럼 삼킨다. 펜의 새 누름은 진행 중인 터치 조작을 취소한다. 손바닥이 먼저 닿아 시작한 터치 획(`touch`를 켠 캔버스)은 펜이 닿는 순간 `cancel`이다.

지우개.

- 지우개 끝의 접촉과 `tool == eraser`인 펜촉·마우스·손가락은 지우개다. 캔버스는 직전 표본에서 이번 표본까지의 선분을 `eraser_radius`로 쓸어 `ink_strokes_hit`로 닿은 획을 찾고, 있으면 `on_erase(ids)`를 낸다. 획을 쪼개지 않는다(결정 3).
- 같은 id가 앱이 지우기 전에 다시 올 수 있다. 앱은 없는 id를 무시한다. `without`이 그렇게 한다. 캔버스가 아닌 요소 위의 지우개 접촉은 지금처럼 아무 일도 하지 않는다. 그 자리가 추적기에서 컨트롤러로 옮겨 간다([잉크 입력](ink-input-design.md#규칙)).

좌표와 배율.

- 내용 좌표는 [두 손가락 회전](rotate-gesture-design.md)의 규약이다. 원점의 화면 자리 `o = slot의 왼쪽 위 − rotated_bounds(content_bounds, rotation)의 왼쪽 위 × scale × zoom`, 점 `p = rotate_point((표본 − o) / (scale × zoom), −rotation)`이다. `content_bounds`가 비면 캔버스 크기(zoom 1의 논리 픽셀)와 원점 왼쪽 위다.
- 폭·지우개 반지름·단순화 오차는 내용 논리 픽셀이다. 그릴 때 `scale × zoom`을 곱한다. 확대하면 획도 굵어진다.
- 압력은 0~1로 접는다. `pressure`가 비면 점의 압력은 1이다. 마우스와 압력 없는 펜은 `width` 그대로다.

그리기.

- 획은 점마다 반지름이 폭의 절반인 원과 이웃 원을 잇는 사다리꼴을 한 path에 모아 nonzero로 채운다. 겹친 곳이 두 번 칠해지지 않아 반투명 색도 고르다. 선분 사이는 Catmull-Rom으로 보간해 그린다. 저장한 점은 바꾸지 않는다(결정 5).
- 굳은 획은 `strokes`의 그림 한 장으로, 진행 중 획은 `wet`의 path로 그린다. 바탕은 칠하지 않는다. 캔버스는 투명하고 앱이 판 위에 놓는다.
- 기본색 획은 그리기 시점의 팔레트 `primary_foreground`라 테마·고대비 전환을 따른다. 색을 가진 획은 그 색 그대로다(결정 6). 움직임이 없어 [움직임 줄이기](reduce-motion-design.md)와 닿지 않는다.

접근성.

- 역할은 `access_role::image`다. 이름은 `access_name` → `set_access_name` → 기본 문구 순이다. 값(`access_info::value`)은 `access_summary(획 수)`이고 비면 기본 요약이다. 값이 있으면 UIA Value 패턴이 선다(uia_provider.cpp 987~990행).
- Tab 자리가 아니다. 키보드로 그리지 않는다. 지우기·되돌리기·도구 바꾸기는 앱이 단추와 단축키로 둔다(결정 9).

## 결정

**1. 입력 경로.** 질문: 펜 접촉을 어떤 역할로 받는가.
- 길 a: 새 역할 `ink_source`. 컨트롤러가 접촉을 처음부터 끝까지 쥐고 압력·기울기·시각·포인터 id를 건넨다. 장치 고르기와 지우개 받기가 역할에 있다. `zoom_source` 옆의 선례를 따른다. 대가: `ui_element`의 접근자 한 쌍, 컨트롤러 상태 하나(`ink_drag_`)와 누름·이동·뗌·취소·유실 다섯 자리를 짓는다.
- 길 b: `pointer_drag_target`과 `ui_action_context`에 펜 값을 더한다. 변경이 작다. 대가: 모든 액션 문맥에 펜 값이 실린다. 손잡이가 되면 터치가 흐르지 않는다(1678행). 지우개를 받는 손잡이와 삼키는 손잡이를 가를 자리가 없다.
- 권장: a. 펜만 받는 기본이 터치의 스크롤·핀치를 지킨다. 대가: 위의 새 역할이다. **승인 필요.**

**2. 진행 중 획의 자리.** 질문: 아직 떼지 않은 획을 누가 들고 그리는가.
- 길 a: 앱 상태. 표본마다 앱 메시지가 logic으로 가고 tree가 다시 서서 `wet`로 그린다. 불변 tree 규칙이 그대로다. logic은 63개씩 비우고 frame을 batch에 한 번 낸다(app_host.cpp 589~593행). 대가: UI → input → logic → UI 왕복 지연이 펜 끝과 획 끝 사이에 보인다.
- 길 b: input thread가 진행 중 획을 latest slot에 게시하고 그리기가 그 위에 얹는다. logic 왕복이 빠진다. 대가: 새 slot과 `draw_context` 칸, 두 플랫폼의 frame 경로가 는다. Win32 frame 경로는 줄 4의 `win32_window.cpp`다. `interaction_snapshot`(ui_element.h 857행)에 싣는 것은 안 된다. 값 비교·복사가 이동마다 돌아 점이 늘수록 무거워진다.
- 길 c: UI thread가 원시 표본으로 요소 밖 임시 버퍼를 그린다. 지연이 가장 짧다. 대가: 불변 tree 밖에 그림이 생기고 hit 판정을 UI thread가 두 번 한다.
- 권장: a. `ink_source`는 길과 무관한 모양이라 지연을 잰 뒤 b를 더해도 역할이 바뀌지 않는다. 대가: 위의 왕복 지연이다. **승인 필요.**

**3. 지우개의 단위.** 질문: 지우개가 무엇을 지우는가.
- 길 a: 닿은 획을 통째로. 지우개 끝과 지우개 도구가 같다. 대가: 획 일부만 지울 수 없다.
- 길 b: 닿은 점만 지우고 획을 쪼갠다. 대가: 메시지가 "획 하나를 둘로"가 되고 id 규칙과 단순화가 얽힌다.
- 길 c: 지우개 끝만, 도구 없음. 대가: 지우개 끝이 없는 펜과 마우스는 지우지 못한다.
- 권장: a. b는 `on_erase`와 다른 메시지로 후속에 더한다. 대가: 위의 하나다. **권장대로 진행.**

**4. 받는 장치.** 질문: 어느 장치가 기본으로 그리는가.
- 길 a: 펜만. 마우스·손가락은 config로 켠다. 손가락은 스크롤·핀치로 남는다.
- 길 b: 펜과 마우스. 대가: 마우스로 캔버스 위 단추 사이를 끄는 앱이 바뀐다.
- 길 c: 모두. 대가: 손바닥이 그리고 캔버스 위에서 스크롤할 수 없다.
- 권장: a. WinUI `InkPresenter.InputDeviceTypes`의 기본(펜)과 같다. 대가: 마우스로 그리는 앱은 한 칸을 켠다. **권장대로 진행.**

**5. 평활과 단순화.** 질문: 점을 어떻게 다듬는가.
- 길 a: 원본을 두고 그릴 때 보간한다. 확정 때 앱이 `simplify_ink_points`로 줄인다.
- 길 b: 원본을 꺾은선으로 그린다. 대가: 빠른 필기가 각진다.
- 길 c: 입력 중에 점을 걸러 바꾼다(이동 평균). 대가: 펜 끝보다 획이 늦고 원본을 잃는다.
- 권장: a. 대가: 확정 때 점을 한 번 더 훑는다. **권장대로 진행.**

**6. 획의 색과 테마.** 질문: 획 색은 테마를 따르는가. [테마](concepts/theming.md) 69행은 "element가 `ui_color`를 앱에서 받는 자리는 없다"고 적었다.
- 길 a: 획은 색을 데이터로 든다. 비면 팔레트의 `primary_foreground`다. 사용자가 고른 잉크 색은 문서 내용이라 이미지의 픽셀과 같다. 캔버스 자신(테·바탕)은 색을 받지 않는다. 대가: 색 있는 획은 고대비·어두운 테마에서 바뀌지 않는다. 원칙에 "내용의 색" 예외 한 줄을 더한다.
- 길 b: 역할만. 대가: 잉크 앱이 색을 고를 수 없다.
- 길 c: 색은 데이터, 어두운 테마에서 검정·흰색을 뒤집는다. 대가: 같은 문서가 테마마다 다르게 보이고 내보낸 그림과 화면이 갈린다.
- 권장: a. 대가: 위의 원칙 한 줄이다. **승인 필요.**

**7. 그리기 캐시의 자리.** 질문: 획 수천 개의 그림을 누가 드는가.
- 길 a: 획이 path를 만들 때 지어 들고, 묶음(`ink_layer`)이 그림 한 장을 memo로 든다. 그림은 처음 그릴 때 기록하고 기본색이 같으면 다시 쓴다. `image_backing`이 `SkPicture`를 나눠 그리는 것과 같다(image_backing.h 22행). 앱이 획을 더하거나 지울 때만 새 묶음이 선다.
- 길 b: 획 path만. 대가: 표본마다 tree가 서고 frame마다 획 전부를 다시 그린다.
- 길 c: 요소가 캐시한다. 대가: 요소는 tree마다 새로 서서 캐시가 남지 않는다. 남기려면 전역 표가 필요하다.
- 권장: a. 대가: 묶음 안의 mutex 하나와, 획을 더할 때마다 다음 그리기에서 한 번 다시 기록하는 비용이다. **권장대로 진행.**

**8. 저장과 내보내기.** 질문: 라이브러리가 어디까지 하는가.
- 길 a: 점 목록과 순수 모델, 그림 한 장(`render_ink_image`)까지. 저장 형식은 앱이 고른다. PNG는 앱이 `encode_png`(image_encode.h 30행)로 쓴다.
- 길 b: ISF·SVG 저장과 읽기까지. 대가: 형식마다 판과 호환을 떠안는다.
- 권장: a. 대가: 앱이 점을 직접 직렬화한다. **권장대로 진행.**

**9. 보조 기술과 키보드.** 질문: 그림을 어떻게 알리고 무엇을 키 대안으로 두는가.
- 길 a: 이미지 역할, 이름과 요약 값("획 3개"). 키보드로 그리지 않는다. 도구·지우기·되돌리기는 앱의 단추다.
- 길 b: 새 `access_role`과 UIA 사용자 지정 패턴으로 획 목록을 연다. 대가: accessibility.h·uia_provider.cpp를 여러 요소 노드와 함께 고친다.
- 권장: a. 대가: 획 하나하나는 읽지 못한다. **권장대로 진행.**

## 플랫폼

### Win32

- 펜 값은 `handle_pointer_message`가 이미 읽는 `POINTER_PEN_INFO`에서 옮기고, 과거 표본은 `GetPointerPenInfoHistory`다([잉크 입력](ink-input-design.md#win32)). `win32_window.cpp`는 고치지 않는다.
- 캔버스 그리기는 공용 Skia 경로다. Windows Ink(`InkPresenter`, DirectInk)의 저지연 그리기는 쓰지 않는다. 원격 연결이 압력을 보존하지 않으면 `pressure`가 비어 `width`로 그린다.

### Android

- 압력·기울기는 `AMOTION_EVENT_AXIS_PRESSURE`·`TILT`·`ORIENTATION` 축을 켜서 읽는다. 지우개는 `AMOTION_EVENT_TOOL_TYPE_ERASER`, 손바닥 취소는 이미 옮기는 `FLAG_CANCELED`다. Java 소스와 JNI가 필요 없다.
- androidx 저지연 앞 버퍼(`androidx.graphics.lowlatency`)와 움직임 예측(`androidx.input.motionprediction`, framework `MotionPredictor`)은 unsupported다. Java 라이브러리이거나 Java `MotionEvent` 객체가 있어야 해서 Java 0줄 조건([Android 이식 계획](android-port-plan.md))과 맞지 않는다. 지연은 결정 2의 길로만 다룬다. Android에는 접근성 계층이 없어 요약 값은 Windows UIA만 읽는다.

## 다른 노드와의 약속

- **두 손가락 회전(R1)**: 회전한 보기 위의 점을 되돌리는 식은 rotate 문서의 것을 그대로 쓴다. ink는 `zoom_source`를 넓히지 않는다. rotate가 남긴 "ink가 넓히면 그 뒤에 더한다"는 쓸 일이 없어 닫는다. rotate 문서가 ink 몫으로 둔 `process_touch_press`(1678행 조건 하나)와 펜·마우스 경로만 고친다.
- **스프링(R1)**: 뗄 때의 속도를 건네지 않는다. 표본마다 시각이 있어 앱이 잴 수 있다. spring 문서의 "놓은 속도를 건네는 뒤 노드"에 ink는 들지 않는다.
- **기본 문구 정책(줄 1)**: 캔버스의 기본 이름("잉크 캔버스")과 기본 요약("획 N개")은 그 노드가 세우는 비공개 기본 문구 헤더에 두 줄로 더한다. ink는 그 헤더가 main에 들어온 뒤 병합한다. **i18n(R5)**에서 요약은 복수형 항목이 된다.
- **undo_stack(줄 2)**: ink는 되돌리기를 짓지 않는다. `on_stroke`의 끝과 `on_erase`는 앱 메시지라 앱이 그 노드의 쌓기에 넣는다.
- **image_clip(줄 4)**: 클립보드는 그 노드의 것이다. ink는 `render_ink_image`로 `ui_image`를 내줄 뿐이다. `ui_events.h`에서 ink는 `pointer_*_event` 구조체 끝만, image_clip은 `clipboard_*`·`clipboard_request`·`input_action`만 고친다. `android_app.cpp`에서 ink는 `copy_motion`과 `start`의 축 켜기 줄만 고친다.
- **os_notify·인자 넘기기(줄 4)**: `android_app.cpp`의 권한 다시 읽기, `window_surface.cpp`의 `WM_COPYDATA` 자리는 ink가 건드리지 않는다. ink는 `handle_pointer_message`만 고친다.
- **inspector(줄 3, 다음 문서)**: 둘 다 `ui_element_id.h`의 `end_of_library_kinds` 위에 kind를 더한다(ink는 `ink_canvas`). inspector가 kind 이름표를 두면 `ink_canvas` 한 줄을 통합 때 더한다. kind 숫자를 test와 UIA 스크립트에 박지 않는다.
- **menu_bar·rich_edit(R3)·teaching_tip(R4)**: 같은 controller 파일이라 ink 뒤에 직렬로 한다. ink는 `raw_input_event` variant와 키 경로를 고치지 않는다.
- **데모**: 라이브러리 커밋과 데모 연결 커밋을 나눈다. `examples/demo_main.cpp`·`examples/mobile_demo/app.cpp`의 페이지 등록은 통합 때 다른 줄의 연결과 직렬로 쌓는다.

## 확인

자동 test. 입력 쪽 test(추적기·Android·Win32 변환)는 [잉크 입력](ink-input-design.md#확인)에 있다.

- `tests/ink_stroke_tests.cpp`(`luil_core_tests`, 기기에서도 돈다): 압력 → 폭(0·0.5·1, 압력 없음은 `width`), 유한하지 않은 점 버림, 빈 획·한 점 획, 단순화 뒤 원본 점과의 거리가 오차 안, 압력이 크게 바뀌는 점은 남음, `ink_strokes_hit`의 선분-획 거리와 경계 상자 거르기, `with`·`without`(없는 id 무시), 내용 좌표 식(zoom 2·회전 90도에서 되돌린 점이 원래 점).
- `tests/ink_canvas_tests.cpp`(controller 시나리오): 펜 누름·이동·뗌이 `begin`·`extend`·`end`를 내고 압력·기울기가 실린다. 마우스는 끈 캔버스에서 클릭 경로이고 켜면 그린다. 끈 캔버스 위의 손가락은 감싼 창을 흘리고 확대 보기를 옮긴다. 켜면 그린다. 확대 보기 안의 펜은 보기를 옮기지 않는다. 지우개 끝과 지우개 도구가 닿은 획 id를 낸다. 단추 위의 지우개는 클릭·초점·관성·터치 조작을 바꾸지 않는다. 캔버스가 사라지면 `cancel`, 시스템 취소는 `cancel`, 배럴 전환과 큐 유실은 `end`+`interrupted`이고 남은 이벤트를 삼킨다. 펜 누름이 손가락 획을 `cancel`한다. 접근 이름과 요약 값.
- `tests/raster_draw_tests.cpp` 방식의 그리기 확인: 획이 픽셀을 만들고, 압력에 따라 폭이 달라지고, 캔버스 밖은 비고, 반투명 획의 겹친 곳이 한 번만 칠해진다. 기본색 획은 팔레트를 바꾸면 색이 바뀐다.

데모. 상태를 label로 그려 `luil-uia`로 되읽는다. `examples/demo/ink_page.cpp`·`.h`와 `examples/mobile_demo/ink_page.cpp`·`.h`에 같은 것을 둔다.

- label `ink-count`("획 0개"), `ink-tool`("도구: 펜"·"도구: 지우개"), `ink-last`("마지막 획: 펜, 압력 있음" 또는 "지운 획 2")를 둔다. 단추 `ink-tool-toggle`, `ink-clear`, `ink-sample`(정해 둔 획 하나를 같은 앱 메시지로 더한다)과 확인 칸 `ink-mouse`를 둔다.
- UIA(격리 데스크톱): `ink-sample`을 두 번 Invoke하면 `ink-count`가 "획 2개"이고 캔버스의 Value가 같은 요약이다. `ink-clear`면 "획 0개"다. 그리기는 UIA로 만들지 못한다.
- `tests/mobile_demo_page_tests.cpp`(`[mobile-demo][ink]`): 펜 이벤트를 실제 입력 경로로 넣어 `ink-count`·`ink-last`를 되읽는다.
- `luil_pointer_integration`: 합성 펜(압력 512)으로 캔버스에 획 하나를 긋고 상태 JSON의 획 수와 압력을 본다. 지우개 접촉으로 그 획을 지운다. 기존 "pen eraser / no ordinary click"은 단추 위에서 그대로 통과해야 한다.

사람 확인.

- 실제 압력 펜으로 필압에 따른 굵기, 지우개 끝, 배럴 버튼, 빠른 필기의 끊김과 각짐, 펜 끝과 획 끝의 지연(결정 2를 다시 볼 근거). 손바닥을 댄 채 쓰기, 손가락 스크롤·핀치와의 공존, 100%·150%·200% 배율과 다중 모니터, 원격 연결의 압력.
- Android 실기기의 스타일러스: 압력 범위, 지우개 도구 보고, 손바닥 취소. adb 주입은 압력을 주지 못해 사람 손으로 한다.

## 범위 밖

- 획 일부 지우기(결정 3의 b), 올가미 선택·옮기기, 도형·글씨 인식, 둘 이상의 동시 획. 기울기·회전에 따른 붓 모양, 형광펜 겹침 규칙, 압력 곡선 설정.
- 저지연 앞 버퍼와 움직임 예측(결정 2의 b·c, Android의 androidx), 저장 형식(결정 8의 b).

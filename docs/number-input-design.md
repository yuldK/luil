# 숫자 칸 검증

`text_input_kind::number`는 지금까지 키보드 힌트뿐이었다. 앱은 글을 직접 읽고, 범위를 재고, 칸 아래에 오류 글을 그릴 자리를 따로 잡았다. 그러다 보니 같은 화면에서 붙여넣기와 IME 확정은 다른 규칙으로 걸렀고, 보조 기술은 오류를 듣지 못했다. 이제 라이브러리가 그 일을 나눠 맡는다.

- **글 읽기와 범위 검사**는 순수 함수다. 앱이 저장을 막을지 정할 때도 같은 함수를 쓴다.
- **오류 상태와 오류 글**은 칸 설정이다. 칸은 테두리와 칸 아래 줄을 그리고, 보조 기술에도 알린다.
- **↑↓·PageUp/PageDown 증감과 스핀 단추**는 칸이 다음 글을 지어 앱 메시지로 내보낸다.

공개 형식은 [number_input.h](../include/luil/ui/number_input.h)와 [dialog_elements.h](../include/luil/ui/dialog_elements.h)의 `text_input_config::invalid`·`error`·`number`, 그리고 [accessibility.h](../include/luil/ui/accessibility.h)의 `access_info::invalid`·`error`다.

## 값은 앱의 것이다

```cpp
const luil::number_input_range range { .minimum = 0, .maximum = 3600, .step = 5 };
const luil::number_input_check check { luil::check_number_input(timeout_.text, range) };

luil::text_input_config config {};
config.error = check.valid() ? u8"" : error_text_for(check.problem); // 글은 앱의 말이다
config.number = luil::text_input_number {
    .range = range,
    .change_to = [](std::u8string text) { return luil::make_app_action(timeout_text_intent { std::move(text) }); },
    .spin_buttons = true,
    .increase_tooltip = u8"늘리기",
    .decrease_tooltip = u8"줄이기",
};
```

- 라이브러리는 글을 **읽기만** 한다. 칸에 무엇이 적혀 있는지는 앱의 편집 초안이 가진다. 오류인지도 앱이 `invalid`·`error`로 알린다. 칸이 혼자 판단해 붉어지지 않으므로, "입력 중에는 오류를 숨기고 저장할 때 보인다" 같은 앱의 규칙이 그대로 선다.
- 오류 글의 문구도 앱이 정한다. `number_input_problem`이 무엇이 틀렸는지 말하고, 그것을 어떤 말로 보일지는 앱의 번역이다. 지우기 단추의 툴팁을 앱이 주는 것과 같은 규칙이다.
- 저장 막기도 앱의 몫이다. 같은 `check_number_input(...).valid()`로 막는다.

## 글 읽기

`check_number_input(text, range)`는 `number_input_check`를 돌려준다. 담는 것은 문제 하나(`none`·`empty`·`not_a_number`·`too_precise`·`below_minimum`·`above_maximum`)와 읽은 값이다. 범위를 벗어났어도 읽을 수 있었으면 값이 들어 있다.

- 받는 꼴은 `[+-]? 숫자* (. 숫자*)?`이고, 숫자가 하나는 있어야 한다. 앞뒤 공백은 무시한다(붙여넣은 글의 끝 줄바꿈 포함).
- **전각 숫자·전각 마침표·전각 빼기·U+2212(수학 빼기)는 ASCII로 읽는다.** 한글·일본어 IME가 전각 모드로 확정한 숫자, 문서에서 복사한 "−5"가 그대로 읽힌다.
- 천 단위 쉼표는 받지 않는다(`not_a_number`). 쉼표를 소수점으로 쓰는 지역도 있어서, 하나로 정하면 다른 쪽 값이 조용히 천 배가 된다.
- `range.decimals`보다 소수 자리가 많으면 `too_precise`다. 끝의 0은 자릿수로 세지 않는다("1.50"은 소수 한 자리 칸에서도 1.5다).
- 값은 `10^decimals` 배의 정수로 다룬다. 0.1을 세 번 더해도 0.30000000000000004가 되지 않는다. 다룰 수 있는 크기는 그 정수가 ±10^15 안에 드는 만큼이다. 넘치면 부호에 따라 범위 밖(`below_minimum`·`above_maximum`)이다.
- IME 조합 중인 글은 읽지 않는다. 검사는 확정 글(`text_input_view::text`)로 한다. 조합하는 동안 오류 줄이 깜빡이지 않는다.

## 걸러 넣기(붙여넣기·IME)

`number_input_filter(range)`는 `apply_text_edit`에 넘기는 `text_insert_filter`다. 넣는 글에서 그 범위의 숫자에 나올 수 있는 글자만 남긴다.

- 숫자는 남긴다. `-`·`+`는 최솟값이 0보다 작을 때만, `.`은 `decimals`가 0보다 클 때만 남긴다. 전각 글자는 ASCII로 바꿔 남긴다.
- 공백·쉼표·그 밖의 글자는 버린다. 붙여넣은 "1 200 원"은 "1200"이 된다.
- 넣는 조각만 보고 거르므로 "1..2"처럼 조각을 이어 붙여 틀린 글은 막지 못한다. 그것은 검사가 `not_a_number`로 잡는다. 거르기는 "칸에 들어갈 수 없는 글자"를 막고, 검사는 "숫자로 읽히는가·범위 안인가"를 본다.

## 증감

`stepped_number_input(text, range, step)`은 `value_step` 한 걸음 뒤의 글이다.

- `increase`·`decrease`는 `step`, 쪽 걸음은 `page_step`이다. `page_step`이 0이면 `step`의 10배다. `minimum`·`maximum`은 범위의 끝이다(그 끝이 정해져 있을 때만).
- **눈금은 최솟값에서 센다.** 지금 값이 눈금 사이면 ↑는 위 눈금, ↓는 아래 눈금으로 간다. 걸음마다 같은 값이 나오며, 웹 숫자 칸과 같은 규칙이다.
- 결과는 범위 안으로 접는다. 범위 밖에서 시작해도 한 걸음에 가장 가까운 끝으로 돌아온다.
- 비었거나 읽을 수 없는 글에서 시작하면, 걸음 없이 0을 범위 안으로 접은 값이 된다. 처음 누른 ↑가 숫자 하나를 채운다.
- 글은 `decimals` 자리로 쓴다. 이미 같은 글이면(끝에 닿았다) `nullopt`다.

칸은 `text_input_number::change_to`가 있으면 이 함수로 증감을 맡는다.

- **↑는 늘린다.** 막대와 목록은 아래쪽이 큰 값이라 ↑가 줄인다. 숫자 칸과 스핀 단추는 반대다. `key_step_target::up_increases`가 그 갈림이다.
- ←/→·Home/End는 지금처럼 caret의 것이다. 한 줄 칸의 텍스트 경로가 ↑↓·PageUp/PageDown을 쓰지 않으므로, 그 넷만 증감으로 흐른다.
- 조합 중에는 증감하지 않고 키를 삼킨다.
- 끝에 닿은 걸음도 삼킨다. 흘려보내면 PageDown이 바깥 스크롤로 새어 화면이 함께 뛴다(막대와 같은 이유).
- 앱은 받은 글로 초안 전체를 바꾼다. `text_edit_command::replace_all`에 `offset = 글 길이`를 담아 `apply_text_edit`에 넘기면, 실행 취소 한 덩어리로 caret이 끝에 선다.

## 스핀 단추

`spin_buttons`가 참이고 `change_to`가 있으면, 칸 오른쪽 안쪽에 위·아래 화살표 단추 둘을 세운다.

- 지우기 단추처럼 칸 **안의** 보조 단추라 Tab 자리를 갖지 않는다. 키보드로는 ↑↓가 같은 일을 한다.
- 툴팁(접근 이름)은 앱이 준다(`increase_tooltip`·`decrease_tooltip`).
- 지우기 단추가 함께 있으면 지우기가 안쪽, 스핀이 바깥이다. 글이 차지하는 폭은 `trailing_width_for`가 둘 다 뺀다.

## 오류 상태와 오류 글

- `invalid`가 참이거나 `error`가 비어 있지 않으면 오류 상태다. 테두리를 오류색(`error_accent`)으로 그린다. 초점을 받아도 오류색을 지키고 두께만 늘린다. 초점 색으로 바꾸면 고치는 동안 오류 표시가 사라진다.
- `error`가 있으면 칸 아래 `text_input_error_height`(16 논리 픽셀) 줄에 오류색으로 그린다. 칸에는 측정 단계가 없으므로, 담는 쪽이 `text_input_element::error_height_for(config)`만큼 높이를 더해 잡는다. 입력 상자는 slot의 위쪽이다.
- 오류 글은 한 줄이다. 길면 끝을 말줄임한다. 보조 기술은 줄이지 않은 전체 글을 읽는다.

## 보조 기술

- 이름은 `set_access_name`이 있으면 그것이고, 없으면 지금처럼 안내 글(placeholder)이다. 안내 글이 지워지면 칸이 이름을 잃으므로, 숫자 칸은 이름을 다는 편이 좋다.
- `access_info::invalid`·`error`가 오류 상태와 글을 싣는다. Windows UIA에서는 다음과 같이 보인다.
  - `IsDataValidForForm`: 오류 상태의 반대다.
  - `HelpText`: 오류 글이 있으면 오류 글, 없으면 지금처럼 툴팁이다.
  - `FullDescription`: 오류 글이다.
- **알림**: 오류 글이 새로 생기거나 바뀌면 `UiaRaiseNotificationEvent`로 그 글을 읽게 한다(`ImportantMostRecent`, 활동 id `luil.input-error`). Narrator는 속성 변화만으로는 오류를 읽지 않는다. 이 함수는 Windows 10 1709부터 있으므로 실행 중에 찾아 쓰고, 없으면 속성 변화만 알린다. 오류 상태가 뒤집힐 때는 `IsDataValidForForm` 변화도 함께 쏜다.
- 범위 값(RangeValue) 패턴은 싣지 않는다. 편집 칸에 Value와 RangeValue가 함께 있으면 읽기 프로그램마다 어느 값을 읽을지가 갈린다. 범위는 오류 글이 말한다.

## Android

플랫폼 쪽은 나누지 않는다. 증감은 공용 controller 경로이고, 스핀 단추는 보통 자식 단추다. 오류 줄은 공용 그리기다.

- `number`가 있고 키보드 종류가 `text`면, 칸은 키보드 힌트를 `decimals`에 따라 `number`나 `decimal`로 올린다. 앱이 키보드 종류를 따로 고르지 않아도 숫자 키보드가 뜬다.
- 접근성 알림은 Android에서 쏘지 않는다. Android 접근성 계층은 Java 0줄 방침 때문에 들이지 않았다. 그래서 `access_info`의 새 필드는 Windows UIA만 읽는다.

## 검증

- `tests/number_input_tests.cpp`:
  - 읽기: 전각 숫자, U+2212, 공백, 쉼표 거부, 자릿수, 넘침;
  - 범위 밖 판정과 거르기(붙여넣기 조각, IME 확정 조각);
  - 눈금 맞추기·접기·빈 글에서의 증감과 소수 표기;
  - 칸의 ↑↓·PageUp/PageDown 증감, ←→ caret 유지, 조합 중 삼킴, 스핀 단추 클릭;
  - 오류 줄의 높이, 상자 영역, 접근 이름·오류·invalid.
- `tests/uia_provider_tests.cpp`: `IsDataValidForForm`·`HelpText`·`FullDescription` 읽기, 오류 글이 생길 때의 속성 변화와 알림.
- 데모 "기본" 페이지(`--page=basics`)의 "숫자 칸": 0–3600을 5씩 증감하고, 스핀 단추가 있으며, 범위 밖이면 오류 줄이 선다. 붙여넣기와 IME 확정은 `number_input_filter`가 거른다.

# 라이브러리 기본 문구

라이브러리 요소가 스스로 화면이나 접근 tree에 놓는 글(닫기, 심각도 낱말, 줌 단추 글, 테마 고르기 낱말)의 기본값을 정책 하나로 모은다. 앱이 config로 글을 주면 그 글이고, 비어 있으면 한국어 기본 문구다. 기본 문구는 비공개 [default_text.h](../src/ui/default_text.h)의 함수 한 벌에 모은다. 앱이 보는 공개 형식은 새 config 칸이다. `zoom_controls_config`의 `decrease_label`·`increase_label`·`fit_label`과 `combo_box_config`의 `loading_name`이 늘고, `theme_picker_config`·`accent_picker_config`는 공개 헤더에 적어 둔 한국어 기본값을 비운다.

정책은 사용자가 정한 G6 정책 a(한국어 기본 한곳에, 2026-11-02)다. 언어별 표와 자리 값, 복수형을 갖춘 공개 표 API는 R5의 i18n 노드가 세우고, 이 헤더가 그 첫 소비자가 된다.

## 문제

R1의 정보 막대와 분할 버튼이 "앱 값이 비면 한국어 기본값"으로 먼저 만들어졌고 두 설계 문서와 코드 주석이 "G6이 정해지면 문자열 헤더로 옮긴다"고 적어 두었다. 같은 꼴이 요소마다 다른 자리에 흩어져 있다.

- 정보 막대의 닫기 단추 이름 "닫기"와 심각도 낱말 넷이 익명 namespace에 있다([info_bar_element.cpp](../src/ui/info_bar_element.cpp)의 `default_close_name`·`default_severity_name`). 분할 버튼의 화살표 이름은 `default_arrow_suffix`(" 옵션")를 본체 이름 뒤에 이어 붙여 짓는다([split_button_element.cpp](../src/ui/split_button_element.cpp)).
- 줌 단추 이름 "축소"·"확대"·"맞춤"이 `zoom_controls_element` 생성자에 박혀 있다([zoom_view_element.cpp](../src/ui/zoom_view_element.cpp)). `zoom_controls_config`에는 이름 칸이 없어 앱이 바꿀 길이 없다. 이 글은 화면 글이자 접근 이름(`set_access_name(text)`)이다.
- [theme_picker_element.h](../include/luil/ui/theme_picker_element.h)는 공개 헤더에서 `name`("테마"), `labels`(밝게·시스템·어둡게), 강조색 고르기의 `name`("강조색")을 한국어로 초기화한다. 기본 문구가 헤더 안에 있어 한곳에 모을 수 없다.
- 콤보 상자의 후보 받는 중 표시가 접근 이름을 영어 "Loading suggestions"로 박고(`combo_box_element.cpp`의 `suggestion_popup`) config에는 덮을 칸도 없다. 요소가 스스로 놓는 글 가운데 한국어 기본 정책과 어긋나는 유일한 영어 기본값이다.
- 다음 노드가 따를 규칙이 없다. msg_box의 표준 단추, rating의 값 이름, undo_stack의 라벨 문장이 각자 한국어를 박으면 R5가 모을 자리가 다시 흩어진다.

## 공개 형식

비공개 헤더는 낱말마다 함수 하나다. 정의는 `default_text.cpp`에 둔다. R5는 이 .cpp의 몸통만 표 조회로 바꾸고 호출부는 그대로다.

```cpp
// src/ui/default_text.h (비공개). 정의는 src/ui/default_text.cpp
namespace luil::default_text {
    // 닫기 단추의 툴팁이자 접근 이름이다 (닫기).
    [[nodiscard]] std::u8string_view info_bar_close() noexcept;
    // 접근 이름 앞에 붙는 심각도 낱말이다 (정보·성공·경고·오류). 정보 막대가 쓰고 msg_box도 쓴다.
    [[nodiscard]] std::u8string_view severity_name(ui_severity severity) noexcept;
    // 분할 버튼 화살표의 접근 이름이다. 본체 이름 뒤에 " 옵션"을 잇는다. 이 어순이 사는 곳은 여기뿐이다.
    [[nodiscard]] std::u8string split_button_arrow_name(std::u8string_view body_name);
    [[nodiscard]] std::u8string_view zoom_decrease() noexcept;  // 축소
    [[nodiscard]] std::u8string_view zoom_increase() noexcept;  // 확대
    [[nodiscard]] std::u8string_view zoom_fit() noexcept;  // 맞춤
    [[nodiscard]] std::u8string_view theme_picker_name() noexcept;  // 테마
    [[nodiscard]] std::u8string_view theme_picker_label(theme_preference preference) noexcept;  // 밝게·시스템·어둡게
    [[nodiscard]] std::u8string_view accent_picker_name() noexcept;  // 강조색
    [[nodiscard]] std::u8string_view combo_box_loading() noexcept;  // 추천 항목을 불러오는 중 (새 글이다)
    // 앱이 준 글이 비어 있으면 기본 문구다. 돌려준 view는 두 인자보다 오래 살지 않는다.
    [[nodiscard]] std::u8string_view text_or_default(std::u8string_view configured, std::u8string_view fallback) noexcept;
}
```

공개 헤더는 칸을 더하거나 비운다. 새 칸은 구조체 끝에 더해 지정 초기화 순서를 깨지 않는다. 기본 낱말은 헤더 코드에 적지 않고 주석에만 적는다([info_bar_element.h](../include/luil/ui/info_bar_element.h)와 같다).

```cpp
// include/luil/ui/zoom_view_element.h, zoom_controls_config 끝
// 단추의 글이자 접근 이름이다. 칸마다 비면 기본 문구(축소·확대·맞춤)다.
std::u8string decrease_label {};
std::u8string increase_label {};
std::u8string fit_label {};

// include/luil/ui/combo_box_element.h, combo_box_config 끝
// 후보를 받는 동안 popup에 서는 진행 표시의 접근 이름이다. 화면에는 그리지 않는다. 비면 기본 문구다.
std::u8string loading_name {};

// include/luil/ui/theme_picker_element.h. 기본값만 비운다.
std::u8string name {};  // theme_picker_config. 비면 기본 문구("테마")
std::array<std::u8string, 3> labels {};  // 밝게·시스템·어둡게 순서. 칸마다 비면 기본 낱말
std::u8string name {};  // accent_picker_config. 비면 기본 문구("강조색")
```

```cpp
luil::zoom_controls_config controls { .view = config(), .viewport = viewport_ };
controls.decrease_label = u8"Zoom out";  // 영어 앱은 세 칸을 채운다
controls.increase_label = u8"Zoom in";
controls.fit_label = u8"Fit";
```

## 규칙

**흩어진 한국어 글의 전수와 판정.** 코드를 열어 센 결과다. 옮기는 낱말은 열다섯이다(닫기 1, 심각도 4, 분할 버튼 1, 줌 3, 테마 고르기 4, 강조색 고르기 1, 콤보 1).

| 자리 | 지금 | 판정 |
| --- | --- | --- |
| `info_bar_element.cpp` 41행 | 닫기 단추 이름 "닫기" | 옮긴다. `info_bar_close()` |
| `info_bar_element.cpp` 43~57행 | 심각도 낱말 넷 | 옮긴다. `severity_name(severity)` |
| `split_button_element.cpp` 28행, 169행 | 접미사 " 옵션"을 호출부가 이어 붙임 | 옮긴다. `split_button_arrow_name(body)`가 어순까지 가진다(결정 2) |
| `zoom_view_element.cpp` 438·443·446행 | 축소·확대·맞춤 | 옮긴다. 칸 셋을 더한다(결정 6) |
| `theme_picker_element.h` 18·20·51행 | 공개 헤더의 한국어 기본값 | 옮긴다. 헤더는 비우고 생성자가 채운다(결정 3) |
| `combo_box_element.cpp` 164행 | 영어 "Loading suggestions" | 옮긴다. 한국어로 바꾸고 칸을 더한다(결정 5) |
| `ui_theme.cpp` 376행 | 시스템 강조색의 라벨 "시스템" | 그대로 둔다. `src/theme`은 `src/ui`·`src/host`의 비공개 헤더를 include하는 곳이 없는 아래 층이다. 이 헤더를 부르면 층이 뒤집힌다. R5의 표가 아래 층(조사 계획은 `src/text`)에 서면 그때 옮긴다 |
| `assets/accents.json` 라벨 25개 | 빌드 때 생성되는 카탈로그 자료 | 그대로 둔다. 카탈로그는 소비자가 `LUIL_ACCENT_CATALOG`로 통째 바꾸는 자료이고, 표에 넣을지는 R5 B가 정한다(G6) |
| `text_edit_menu.cpp` 25~30행 | 편집 메뉴 여섯 쌍. 이미 `korean` 인자로 OS 언어를 가름 | 그대로 둔다. 한국어 반쪽만 옮기면 영어가 사라진다. R5가 `bool korean`째 표 조회로 바꾼다 |
| 캡션·대화 상자 닫기·사이드바·탭 막대·이동 경로·트리 상태 줄·숫자 칸 증감과 지우기 툴팁·오류 글·명령 팔레트 글 | 앱이 주고 비면 없다 | 그대로 둔다(결정 4) |
| `commands.cpp` 키 이름, 디코드·플랫폼 진단 영어 글, `text_area_element.cpp` 544행 측정 견본 | 단축키 왕복 이름, 개발자용 진단, 글자 높이 재기 | 대상이 아니다 |

**기본값이 서는 자리.** 라이브러리가 스스로 화면이나 접근 tree에 글을 놓고 앱이 줄 config 칸이 있는 곳이다. 칸이 없으면 칸을 더한다. 제목·본문·동작 글처럼 앱이 아니면 만들 수 없는 글에는 기본값이 없다.

**빈 값 판정.** `std::u8string::empty()`뿐이다. 공백만 있는 글은 앱의 글이다. 판정은 글을 쓰는 자리에서 한다. 정보 막대와 분할 버튼은 지금처럼 이름을 짓는 자리에서 `text_or_default`를 부르고, 테마 고르기는 생성자가 `config_`의 빈 칸을 채운다. `labels`는 칸마다 판정해 하나만 덮어도 된다.

**언어.** OS 언어를 읽지 않는다. 기본 문구는 늘 한국어다. 편집 메뉴(Android)만 OS 언어를 따르는 어긋남이 R5까지 남는다.

**접근 이름과 화면 글.** 줌 단추와 테마 카드는 화면 글이 접근 이름이다. 칸은 하나(`*_label`)이고 요소가 둘 다 세운다. 이 노드는 접근 전용 이름 칸을 더하지 않는다.

**글이 길어질 때.** 영어처럼 한국어보다 긴 글은 각 요소의 지금 규칙을 따른다. 줌 단추 폭은 56 논리 픽셀이 기준이고(좁은 슬롯에서는 비율로 준다) 넘치는 글은 `draw_text_within`이 줄인다. 폭을 글에 맞춰 늘리지 않는다.

배율·테마·고대비·움직임 줄이기와 입력·초점에는 닿지 않는다. 앱 글이 길 때는 위의 글 길이 규칙만 걸린다.

## 결정

**1. 헤더의 모양.** 질문: 비공개 헤더가 기본 문구를 어떤 모양으로 내는가.

- 길 a: 문자열 상수 묶음(`inline constexpr std::u8string_view`). 되는 것: 지금 정보 막대의 꼴 그대로라 옮기기만 하면 되고 선언이 가장 짧다. 대가: R5가 언어별 조회로 바꾸려면 상수를 쓰는 호출부를 모두 함수 호출로 고쳐야 한다. 심각도 낱말처럼 값을 골라야 하는 곳은 어차피 함수가 따로 선다.
- 길 b: 낱말마다 함수. 되는 것: R5가 .cpp의 몸통만 표 조회로 바꾸고 호출부는 그대로다. 인자가 있는 문구(심각도, 분할 버튼 이름, 뒤에 올 rating의 값 이름)도 같은 모양으로 선다. 대가: 낱말 하나에 선언 하나와 정의 하나가 들고 새 낱말은 두 곳을 고친다.
- 길 c: enum 키와 조회 함수 하나. 되는 것: 낱말을 더하는 일이 enum 한 줄과 배열 한 줄이고 R5 표와 한 줄씩 맞는다. 대가: R5의 키 이름과 표 모양을 지금 미리 박는다. 공개 표 API가 아직 설계 전이라 어긋나면 enum을 다시 짠다. 인자가 있는 문구는 함수로 따로 서서 모양이 둘로 갈린다.
- 권장: b. 대가: 위의 두 곳 수정. R5가 키 스키마를 먼저 정한다면 c가 낫다. 비공개라 앱에는 닿지 않지만 msg_box·rating 문서가 이 모양을 그대로 따라 쓰고 R5가 갈아 끼울 기준이 된다. 승인 필요.

**2. 어순이 박힌 분할 버튼 이름.** 질문: " 옵션"처럼 어순이 코드에 박힌 곳을 R5의 자리 값이 풀 때까지 어떻게 두는가.

- 길 a: 접미사만 헤더로 옮기고 호출부가 `본체 이름 + 접미사`로 잇는다. 되는 것: 가장 작다. 대가: 어순이 `split_button_element.cpp` 호출부에 남아 R5에서 호출부를 다시 고친다.
- 길 b: 헤더가 `split_button_arrow_name(본체 이름)`을 짓는다. 되는 것: 어순이 헤더 한 곳에 있고 R5는 이 몸통만 `{label} 옵션` 같은 자리 값 문장으로 바꾼다. 호출부는 어순을 모른다. 대가: 문구 헤더에 `std::u8string`을 돌려주는 함수가 하나 든다.
- 길 c: 자리 값 도우미를 지금 만든다. 대가: R5가 정할 공개 표 API의 모양을 미리 박는다.
- 권장: b. 본체 이름이 비면 지금처럼 앞 공백이 든 " 옵션"이 나오고 이 동작은 바꾸지 않는다. 대가: 위의 함수 하나. 권장대로 진행.

**3. 빈 값과 공개 헤더의 기본값.** 질문: 테마·강조색 고르기가 공개 헤더에 적은 한국어 기본값을 어떻게 옮기는가. 빈 값과 "앱이 일부러 빈 글을 준 것"을 가를 수 있는가.

- 길 a: 공개 헤더는 빈 값이고 생성자가 빈 칸을 기본 문구로 채운다. 되는 것: 앱이 칸을 건드리지 않으면 화면과 접근 이름이 전과 같다. 기본 문구가 공개 헤더에서 사라진다. 대가: 가를 수 없다. 앱이 `name`에 일부러 빈 글을 줘 이름 없는 묶음을 세우던 코드는 기본 문구가 읽히게 바뀐다. 이름 없는 radio_group은 접근성 결함이라 지원하지 않는다. 기본 config의 `name`·`labels`를 직접 읽던 코드는 빈 글을 본다(이 저장소의 소스·예제·test에는 없다).
- 길 b: `std::optional<std::u8string>`. 되는 것: 비움과 빈 글을 가른다. 대가: 칸의 형이 바뀌어 `config.name.empty()`·`size()`나 글 비교 같은 읽기가 컴파일되지 않는다(대입은 변환 생성자로 그대로 된다). 정보 막대·분할 버튼의 `std::u8string` 칸과 규칙이 갈린다.
- 길 c: 공개 헤더에 기본값을 그대로 둔다. 대가: 기본 문구가 두 곳에 산다.
- 권장: a. 사용자가 정한 꼴이고 정보 막대·분할 버튼의 선례와 같다. 대가: 위의 가를 수 없음. 권장대로 진행.

**4. 앱이 주는 기존 자리에 기본값을 소급하는가.** 질문: 캡션·대화 상자 닫기·탭 막대·숫자 칸 같은 "앱이 준다" 칸에도 기본값을 켜는가.

- 길 a: 소급하지 않는다. 되는 것: "비면 없다"는 계약이 그대로다. 탭 막대의 앞·뒤 단추는 이름이 비면 접근 tree에서 빠지는 규약이고([tab_bar_element.h](../include/luil/ui/tab_bar_element.h)의 `step_back_tooltip` 주석), 숫자 칸의 오류 글도 앱의 번역이다([number-input-design.md](number-input-design.md)). 대가: 앱이 주지 않은 대화 상자 닫기는 지금처럼 이름 없는 단추로 남는다.
- 길 b: 접근 이름이 필요한 닫기 계열만 켠다. 되는 것: 이름 없는 단추가 줄어든다. 대가: 비워 둔 앱에 툴팁이 새로 뜨고 접근 tree에 단추가 새로 선다. 동작 변경이다.
- 길 c: 모두 켠다. 대가: b의 대가가 스무 칸 가까이에 번지고 "비면 없다"에 기대던 코드가 바뀐다.
- 권장: a. 대가: 위의 이름 없는 단추. 새 요소가 이런 단추를 가지면 그 요소가 자기 층에서 기본값을 채워 내려보낸다(msg_box의 닫기). 권장대로 진행.

**5. 콤보 상자의 영어 로딩 이름.** 질문: 영어로 박힌 접근 이름을 어떻게 하는가.

- 길 a: 한국어 기본으로 옮기고 `loading_name` 칸을 더한다. 되는 것: 정책의 예외가 없어진다. 대가: 지금 영어 이름을 듣던 앱은 기본이 한국어가 된다. 화면에는 그려지지 않는 이름이라 보이는 변화는 없다. 영어 앱은 칸을 채운다.
- 길 b: 영어 그대로 둔다. 대가: 정책에 예외가 하나 남고 덮을 칸도 없다.
- 길 c: 기본 없이 칸만 더한다. 대가: 비우면 진행 표시가 이름 없이 선다.
- 권장: a. 어떤 test도 그 글을 단언하지 않는다. 대가: 위의 기본 변경과 새 한국어 글의 교정. 권장대로 진행.

**6. 칸 이름과 줌 단추의 칸.** 질문: 새 칸을 어떻게 짓고 줌 단추에 무엇을 더하는가.

- 길 a: 접미사로 쓰임을 가른다(`_label` 화면 글이자 이름, `_name` 읽히기만, `_tooltip` 글리프 단추의 툴팁이자 이름). 화면 글과 접근 이름이 같은 낱말이면 칸 하나다. 줌 단추는 `zoom_controls_config` 끝에 `decrease_label`·`increase_label`·`fit_label`을 평평하게 더한다. 되는 것: 정보 막대·분할 버튼·대화 상자의 기존 칸과 같은 규칙이다. 대가: 화면 글과 접근 이름이 갈려야 하는 단추(화면은 "−", 이름은 "축소")는 아직 못 한다. 갈릴 필요가 생긴 단추에만 `*_name` 칸을 더하고 비면 `*_label`을 따른다.
- 길 b: 모든 단추에 화면 글과 접근 이름 칸을 따로 둔다. 대가: 칸이 두 배가 되고 서로 따르는 규칙이 또 든다.
- 길 c: 줌 단추 칸을 구조체 하나(`labels`)로 묶는다. 대가: 다른 요소는 평평한 칸이라 갈리고, 회전 단추가 칸을 더할 때 안팎을 다시 정해야 한다.
- 권장: a. 대가: 위의 갈림 단추. 권장대로 진행.

## 플랫폼

모두 플랫폼을 모르는 core의 요소라 두 백엔드가 같은 코드를 쓴다. `win32_window.cpp`와 `android_app.cpp`를 건드리지 않는다.

**Win32.** 접근 이름은 각 요소의 `accessibility().name`에서 UIA Name으로 간다. 줌 단추는 글이 곧 이름이다. Windows 표시 언어는 읽지 않는다.

**Android.** Java 소스는 0줄이다. JNI도 unsupported도 없다.

- 접근 이름은 Android에서 소비되지 않는다([number-input-design.md](number-input-design.md)와 같은 까닭). 폰에 보이는 것은 줌 단추 글과 테마 카드 낱말이다.
- 영어 폰에서도 이 낱말들은 한국어다. 편집 메뉴만 `user_ui_language()`를 따라 영어로 나온다. 정책 a의 알려진 어긋남이고 R5까지 앱이 칸을 채워 푼다.

## 다른 노드와의 약속

**새 요소가 따를 규칙**(msg_box, rating, undo_stack). 이 문서가 세운다.

- 칸 이름은 쓰임으로 가른다. `*_label`은 화면에 보이고 접근 이름도 되는 글, `*_name`은 읽히기만 하는 글, `*_tooltip`은 글리프 단추의 툴팁이자 이름이다. 화면 글과 접근 이름이 같은 낱말이면 칸 하나로 받고 요소가 둘 다 세운다(결정 6). 새 칸은 구조체 끝에 더한다.
- 기본값이 서는 자리와 빈 값 판정(`empty()`뿐)은 위 규칙을 따른다. 칸 주석에 "비면 기본 문구(…)"로 낱말을 적고 공개 헤더에는 한국어 값을 두지 않는다.
- 요소 `.cpp`와 공개 헤더에 한국어 문자열 리터럴을 두지 않는다. 기본 문구가 필요하면 이 헤더에 함수를 더한다. 이름은 `<요소>_<부품>`이고, 여러 요소가 같은 낱말 집합을 쓰면 낱말 이름(`severity_name`)으로 둔다. 같은 한국어 낱말이라도 쓰임이 다르면 함수를 따로 둔다. 번역이 갈릴 수 있어서다.
- 수·이름이 끼는 문장은 인자를 받는 함수로 둔다. 어순은 그 함수 안에만 있다. 앱이 통째로 바꾸게 하려면 `std::function<std::u8string(…)>` 칸을 더하고, 비면 헤더 함수를 쓴다.
- 앱이 아니면 만들 수 없는 글(제목, 본문, 동작 글, 후보 라벨)은 기본값 없이 앱이 준다.

**msg_box**(줄 1, 다음 문서). 표준 단추 helper가 기본 글(확인·취소 따위)을 낸다면 이 헤더에 함수를 더한다. 심각도 낱말은 `severity_name`을 쓴다. 대화 상자 캡션의 `dialog_caption_config::close_tooltip`은 "비면 없다" 계약을 바꾸지 않고, msg_box가 자기 층에서 기본값을 채워 내려보낸다(결정 4).

**rating**(줄 1). 값 이름("별점 3 / 5" 꼴)은 값과 최댓값을 받는 함수로 이 헤더에 둔다. 앱이 덮을 칸은 위 규칙의 `std::function` 꼴이다.

**undo_stack**(줄 2). 그 브랜치에는 이 헤더가 아직 없다. 새 한국어 리터럴을 만들지 않고 "실행 취소: …" 같은 라벨 문장은 `std::u8string` 인자로 받는다. 기본 문구가 꼭 필요해지면 통합 뒤 이 헤더에 함수를 더하는 후속으로 한다.

**R5 i18n.** 함수 이름이 키 이름의 출처다. 호출부는 바꾸지 않고 `default_text.cpp`의 몸통을 표 조회로 바꾼다. 인자가 있는 문구는 자리 값 문장으로 바꾼다. 편집 메뉴의 `bool korean`, 강조색 이름 25개, 시스템 강조색 라벨은 R5가 같은 표로 흡수한다.

**R1에서 미룬 것을 닫는다.** 구현 커밋이 아래 줄을 고친다. 이 단계에서는 R1 문서를 고치지 않는다.

| 위치 | 닫는 방법 |
| --- | --- |
| `info-bar-design.md` 207행, `info_bar_element.cpp` 40행 주석 | "G6이 정해지면" 문장을 "[default-text-design.md](default-text-design.md)의 헤더로 옮겼다"로 바꾸고 주석을 지운다 |
| `split-button-design.md` 233행, `split_button_element.cpp` 26~27행 주석 | 같은 방식. 분할 버튼 이름은 `split_button_arrow_name`이 짓는다 |
| `rotate-gesture-design.md` 173행 | 줌 단추 이름 칸이 선다고 고친다. 회전 단추 자체를 `zoom_controls_element`에 더하는 일은 그 문서가 G6 뒤로 미뤘고(210행 범위 밖) 줄 3 몫이며 이번 라운드에 하지 않는다. 더할 때 `rotate_*_label` 칸을 위 규칙으로 `zoom_controls_config` 끝에 잇는다 |
| `taskbar-design.md` 162행 | 기본 설명이 필요한 자리가 없다고 닫는다. `taskbar_request::description`([taskbar.h](../include/luil/app/taskbar.h) 38행)은 앱이 주고 라이브러리가 만드는 글이 없다 |

**같은 라운드의 겹침.** `zoom_view_element.*`는 이번 라운드에 이 노드만 고친다. `ui_element_id.h`에는 kind를 더하지 않는다. `src/CMakeLists.txt`(`ui/foundation` 묶음)와 `tests/CMakeLists.txt`는 파일 한 줄씩 덧붙이므로 병합이 기계적이다. [demo-guide.md](demo-guide.md) 66행의 "labels와 name으로 표시 언어를 바꿀 수 있다"는 그대로 참이고 "비면 기본 낱말"을 덧붙인다. `examples/demo/zoom_page.*`와 `examples/mobile_demo/zoom_page.*`는 여러 줄이 고치는 데모라 라이브러리 커밋과 데모 연결 커밋을 나누고 연결은 통합 때 직렬로 쌓는다. `docs/README.md` 색인도 통합 때 붙인다.

## 확인

자동 test. 새로 바뀐 기대값은 없다. 이 정책은 한국어 기본값을 그대로 두므로 기존 test의 기대값을 바꾸지 않는다. 바꿔야 하는 test 목록은 비어 있다. 아래 기존 test가 회귀 잠금이다.

- 그대로 통과해야 하는 것: `tests/info_bar_element_tests.cpp` 384~430행(심각도 낱말, 닫기 이름), `tests/accessibility_tests.cpp` 583·593행(막대 이름, 닫기)과 882·896행("저장 옵션"), `tests/uia_provider_tests.cpp` 990·1001행(`luil_tests`, Windows 전용), `tests/theme_picker_tests.cpp` 34행(config에 `name`을 주지 않고 "테마"를 단언), `tests/zoom_view_tests.cpp` 416~431행(자식 수만 센다).
- `tests/default_text_tests.cpp`(새 파일, `luil_core_tests`): 열다섯 낱말이 비어 있지 않고 전과 같은 한국어인지, 심각도 넷의 대응, `text_or_default`가 빈 글에는 기본 문구를, 공백만 있는 글과 앱 글에는 그 글을 돌려주는지, `split_button_arrow_name(u8"저장") == u8"저장 옵션"`인지.
- `tests/zoom_view_tests.cpp`에 덧붙임: 기본 이름이 축소·확대·맞춤인지, 세 칸을 덮으면 단추의 접근 이름이 그 글인지, 한 칸만 덮으면 나머지는 기본인지. 자식 순서는 축소, 배율 label, 확대, 맞춤이다.
- `tests/theme_picker_tests.cpp`에 덧붙임: 기본 `theme_picker_config`·`accent_picker_config`의 `name`·`labels`가 비어 있는지(공개 헤더에 한국어가 없다), 요소는 "테마"·카드 낱말·"강조색"을 내는지, `labels` 한 칸만 덮어도 나머지는 기본인지, `name` 덮기.
- `tests/combo_box_element_tests.cpp`에 덧붙임: 후보를 받는 중 popup tree의 `…:loading` 요소 이름이 기본 한국어이고 `loading_name`이 덮는지.
- 검사 하나를 더한다(권장, 크기 S). `cmake/verify_default_text.cmake`를 `verify_core_portability.cmake`처럼 `tests/CMakeLists.txt`에 등록하고, `src/ui`의 `.cpp`와 `include/luil/ui`의 `.h`에서 한글이 든 문자열 리터럴(주석 제외)을 찾는다. 예외는 `default_text.cpp`와 `text_area_element.cpp`의 측정 견본 한 줄이다. 다음 노드가 헤더 밖에 한국어를 박는 것을 막는다.
- 새 test는 모두 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다.

데모(`examples/demo`, `examples/mobile_demo`의 확대 보기 페이지): "줌 단추 글: 기본/English" label과 바꾸는 단추 하나를 놓는다. English면 `zoom_controls_config`에 `Zoom out`·`Zoom in`·`Fit`을 준다. 격리 데스크톱에서 luil-uia로 단추 이름을 읽고(기본은 축소·확대·맞춤), 바꾸는 단추를 Invoke한 뒤 이름이 바뀌는지 되읽는다. 테마 페이지의 묶음 이름(테마, 강조색)과 카드 낱말, 토스트 페이지의 정보 막대와 팝업 페이지의 분할 버튼 이름이 전과 같은지도 읽는다. `demo_gallery_tests.cpp`·`mobile_demo_page_tests.cpp`가 페이지 짓기를 지킨다.

사람 확인: 새 한국어 글("추천 항목을 불러오는 중")의 교정, 실기기에서 영어 줌 단추 글이 56 폭 단추 안에서 줄어드는 모양, 영어 폰에서 편집 메뉴만 영어인 어긋남이 문서의 한계와 같은지.

## 범위 밖

- 기본값을 영어로 하거나 OS 언어를 따르는 정책(정책 b·c). 사용자가 a를 골랐다.
- 공개 표 API, 자리 값, 복수형, 숫자·날짜 서식. R5 i18n의 몫이다.
- 편집 메뉴, 강조색 이름 25개, 시스템 강조색 라벨을 헤더나 표로 옮기는 일. R5가 정한다.
- 앱이 주는 기존 칸에 기본값을 켜는 일(결정 4).
- 회전 단추. `zoom_controls_element`의 자식 구성은 그대로다.
- 접근 이름과 화면 글이 갈리는 단추의 `*_name` 칸. 필요가 생긴 단추에서 더한다.
- 디코드·플랫폼 진단 글, 단축키 키 이름의 현지화.

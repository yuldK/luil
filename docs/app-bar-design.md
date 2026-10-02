# 모바일 앱 바

모바일 화면의 맨 위는 데스크톱 창의 caption과 다르다. 시스템이 상태 표시줄을 그리고, 창을
최소화·최대화하거나 닫는 버튼이 없다. 앱은 그 아래에 제목과 몇 개의 아이콘 버튼을 담은 앱
바를 두거나 아무것도 두지 않는다. luil은 둘을 다른 element로 두고, 어느 쪽을 쓸지는 앱이
플랫폼 성질을 보고 고른다.

## 플랫폼 성질

[`ui_platform`](../include/luil/ui/ui_platform.h)은 앱 host가 시작할 때 한 번 정하는 값이다.

| 필드 | 데스크톱(Win32) | 모바일(Android) |
| --- | --- | --- |
| `form_factor` | `desktop` | `mobile` |
| `window_caption` | 참 | 거짓 |

정하지 않으면 데스크톱이다. Android host는 logic thread를 세우기 전에 정하므로 앱의 첫 frame부터
값이 맞다. 어느 thread에서나 `current_ui_platform()`으로 읽는다.

## caption은 자리를 차지하지 않는다

`window_caption`이 거짓이면 `caption_element::height_for`가 0을 돌려주고, caption은 높이 0으로
놓여 그리지 않으며 창 버튼도 크기 0이라 눌리지 않는다. 앱이 데스크톱용 tree를 그대로 써도
모바일 화면에 데스크톱 chrome이 서지 않는 안전망이다. 화면 맨 위를 무엇으로 채울지는 앱이
정한다.

## 앱 바

[`app_bar_element`](../include/luil/ui/app_bar_element.h)는 Material의 작은 상단 앱 바를 따른다.

| 부분 | 기본값 (논리 픽셀) |
| --- | --- |
| 높이 | 64 |
| 아이콘 버튼 | 48 정사각 터치 영역, 아이콘 24 |
| 양 끝 여백 | 4 |
| 제목 | 22, 선행 버튼이 없으면 왼쪽 16에서 시작 |

- 선행 버튼(`navigation`)은 왼쪽 끝에, 동작 버튼(`actions`)은 오른쪽 끝부터 쌓인다. 목록의 앞
  것이 왼쪽에 온다.
- 버튼은 일반 button element다. `label`이 tooltip이자 보조 기술이 읽는 이름이고, 액션은 앱
  메시지를 낸다. 뒤로 가기 버튼도 앱의 화면 상태를 바꾸는 메시지다.
- 색은 caption 팔레트(`palette.caption`)를 쓴다. 두 막대가 같은 테마 역할이라서다.
- 보조 기술에는 제목을 이름으로 한 `title_bar`로 보인다.

모바일 앱에서 논리 픽셀 1은 1dp다(Android 배율은 `density / 160`). 데스크톱에서도 앱 바를 쓸 수
있지만 치수가 터치 기준이라 크다.

```cpp
if (luil::current_ui_platform().window_caption)
    root->add(std::make_unique<luil::caption_element>(caption_config));
else
    root->add(std::make_unique<luil::app_bar_element>(luil::app_bar_config { .title = u8"hello luil" }));
```

[`examples/hello`](../examples/hello/hello_app.cpp)가 같은 앱 코드로 Windows에서는 caption을,
Android에서는 앱 바를 그린다.

## 검증

[`app_bar_element_tests.cpp`](../tests/app_bar_element_tests.cpp)가 플랫폼 기본값, 모바일에서
높이 0이 되는 caption, 버튼 배치, 접근성 역할을 확인한다. core test라 Windows와 기기에서 같이
돈다. Galaxy S22 Ultra에서 hello가 caption 대신 앱 바를 그리는 것을 화면으로 확인했다.

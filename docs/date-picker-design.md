# 날짜 고르기

사용자가 닫힌 날짜 칸을 눌러 칸 아래 달력 popup에서 날짜 하나를 고르거나, 화면에 놓인 인라인 달력에서 고른다. 값은 앱이 드는 `std::optional<std::chrono::year_month_day>`이고, 키보드 커서(곧 보이는 달)도 앱 상태다. 날은 `select`, 커서는 `move_to` 절대 메시지로만 바뀐다. 공개 형식은 [calendar_element.h](../include/luil/ui/calendar_element.h)의 `calendar_config`·`calendar_text`·`calendar_element`·`date_field_config`·`date_field_element`, [date_picker_popup.h](../include/luil/app/date_picker_popup.h)의 `make_date_picker_popup`이다. 날짜 계산(42칸 격자, 달 이동의 말일 접기, 범위, 시작 커서)과 기본 문구·지역은 [calendar_date.h](../include/luil/ui/calendar_date.h)와 [달력 날짜 계산과 문구](calendar-date-design.md)로 나눴다. 한 문서가 250줄을 넘어서다.

닫힌 칸과 popup은 [분할 버튼](split-button-design.md)의 "필드 popup 열림 규약"을, popup 초점은 [색 고르기](color-picker-design.md) 결정 3을, 기본 이름은 [기본 문구](default-text-design.md)의 헤더를 따른다. 날짜는 칸에 글로 쳐 넣지 않는다.

## 문제

- 날짜 형과 계산이 없다. `include`·`src`에 `year_month_day`·`weekday`·`sys_days`를 쓰는 곳이 없다. 표준 형은 두 툴체인에 있다(NDK 27.3의 libc++ `__chrono/year_month_day.h`·`weekday.h`·`calendar.h`. 구현 커밋이 Windows와 Android arm64에서 빌드했다).
- 달력 본체가 없다. 닫힌 칸의 선례는 라벨과 화살표만 그리고 목록은 앱이 popup에 싣는 `dropdown_element`([dropdown_element.h](../include/luil/ui/dropdown_element.h) 34~36행)와 R3 `color_field_element`다. 42칸 격자, 머리 줄, 키, 접근 이름은 앱 몫이다.
- 두 축 초점 이동이 없다. 묶음 화살표는 구성원을 차례로만 돈다. `both` 축도 네 화살표를 같은 차례 이동으로 받는다([ui_interaction.cpp](../src/ui/ui_interaction.cpp) 4471행, 4492~4497행). 한 주(7칸) 이동은 controller가 하지 못한다. 가상 목록은 목록 하나를 자리로 두고 커서를 앱 상태로 둔다([virtual_list_element.h](../include/luil/ui/virtual_list_element.h) 351~353행).
- popup 초점은 앱이 옮긴다. 초점 요청은 `ui_popup::id`를 표면으로 받고([app_host.h](../include/luil/app/app_host.h) 329행), 다른 표면으로 옮긴 초점은 떠남으로 치지 않으며(ui_interaction.cpp 4708~4712행), 초점 element가 tree에서 사라지면 지울 뿐이다(4522~4528행).
- Android 시스템 날짜 대화(`DatePickerDialog`)는 콜백 인터페이스(`OnDateSetListener`) 구현이 필요해 Java 0줄로 부르지 못한다.

## 공개 형식

```cpp
// include/luil/ui/calendar_element.h (치수는 논리 픽셀)
inline constexpr float calendar_cell_size { 32.0f };  // 날 칸과 머리 단추의 한 변
inline constexpr float calendar_weekday_height { 24.0f };

struct calendar_text  // 칸마다 비면 기본 문구다 (calendar-date-design.md)
{
    std::u8string name {}, previous_month {}, next_month {}, today {}, unavailable {};
    std::array<std::u8string, 7> weekdays {};  // 일요일부터 (`weekday::c_encoding` 순)
    std::function<std::u8string(std::chrono::year_month)> month_title {};  // "2026년 11월"
    std::function<std::u8string(std::chrono::year_month_day)> day_name {};  // "2026년 11월 3일 화요일"
};
struct calendar_config
{
    std::u8string owner {};
    std::optional<std::chrono::year_month_day> value {};  // 고른 날. 앱 상태다
    std::chrono::year_month_day cursor {};  // 키보드 커서이자 보이는 달. 앱 상태다 (결정 2)
    std::optional<std::chrono::year_month_day> today {};  // 앱이 준다. 비면 오늘 표시가 없다
    calendar_bounds bounds {};  // 최소·최대 (calendar_date.h)
    std::function<bool(std::chrono::year_month_day)> available {};  // 고를 수 있는 날인가. 비면 범위 안 모두 (결정 6)
    std::chrono::weekday first_day { std::chrono::Sunday };
    std::function<input_action(std::chrono::year_month_day)> select {};  // 없으면 표시 전용
    std::function<input_action(std::chrono::year_month_day)> move_to {};  // 없으면 키·이전·다음 단추가 없다
    float cell_size { 0.0f };  // 0이면 32. 터치 앱이 올린다
    calendar_text text {};
    std::function<input_action(std::chrono::year_month_day)> turn_month {};  // 이전·다음 단추. 비면 move_to (결정 8)
};
class calendar_element final : public ui_element
{
public:
    explicit calendar_element(calendar_config config);
    [[nodiscard]] static size_f size_for(const calendar_config& config) noexcept;  // 폭 7칸, 높이 머리 줄 + 요일 줄 + 6주
    [[nodiscard]] static ui_element_id day_id(std::u8string_view owner, std::chrono::year_month_day day);  // 초점 요청의 대상
    [[nodiscard]] std::chrono::year_month_day cursor() const noexcept;  // ok()·범위로 다듬은 커서
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;
};

struct date_field_config
{
    std::u8string owner {};
    std::optional<std::chrono::year_month_day> value {};
    bool open { false };  // popup이 떠 있는지는 앱 상태다
    std::function<input_action(bool open)> set_open {};
    std::function<std::u8string(std::chrono::year_month_day)> format {};  // 비면 기본 문구 ("2026년 11월 3일")
    std::u8string placeholder {}, name {};  // 비면 기본 문구 (날짜 선택, 날짜)
};
class date_field_element final : public ui_element  // 높이는 dropdown_height(28)
{
public:
    explicit date_field_element(date_field_config config);
    void arrange(const arrange_context& context) override;
    void draw(draw_context& context, const interaction_snapshot& interaction) const override;
    [[nodiscard]] access_info accessibility() const override;
    [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;
};

// include/luil/app/date_picker_popup.h (make_color_picker_popup과 같은 꼴, 정의는 src/host/date_picker_popup.cpp)
inline constexpr float date_picker_popup_padding { 8.0f };
struct date_picker_popup_config
{
    std::u8string id {}, anchor {};
    float scale { 1.0f };
    field_popup_anchor below {};
    std::function<input_action(popup_dismiss_reason)> dismiss {};
    std::optional<popup_corner> corner {};
};
[[nodiscard]] ui_popup make_date_picker_popup(calendar_config calendar, date_picker_popup_config popup);

// include/luil/ui/ui_element_id.h, end_of_library_kinds 바로 위 (menu_bar_item 뒤)
calendar, calendar_nav, calendar_grid, calendar_day, date_field,
```

## 규칙

**구성.** root(`calendar`)가 머리 줄의 달 제목과 요일 줄을 그리고 `draw_children`을 부른다. 자식은 이전 달·다음 달 단추(`calendar_nav`, owner는 `owner`, U+001F, `prev`·`next`), 격자(`calendar_grid`, owner는 `owner`), 날 칸 42개(`calendar_day`, owner는 `owner`, U+001F, ISO 날짜 `2026-11-03`)다. 머리 줄은 단추, 가운데 제목, 단추다. 격자는 `calendar_month_grid(커서의 달, first_day)`의 6주×7일이다. 크기는 논리 상수의 합이고 자리가 넓으면 왼쪽 위에 붙인다. 비활성은 앱이 root에 거는 `set_enabled(false)`이고 자식이 arrange에서 따라간다(별점 결정 12).

**값과 메시지.**

- 보이는 달은 커서의 달이다. 상태가 하나라 이전·다음 단추도 커서를 옮긴다. 열 때 앱은 커서를 `calendar_start_cursor(value, today, bounds)`로 둔다(값, 없으면 오늘, 범위로 접음).
- `move_to(날)`은 키와 커서가 아닌 날의 클릭이 낸다. 목표는 `calendar_moved`로 정하고 범위로 접는다. build 때의 커서(`ok()`·범위로 다듬은 `cursor()`)에서 센 절대 값이고 커서와 같으면 빈 목록이다. 한 frame 안에 키 둘이 오면 둘째가 같은 목표를 내 한 걸음을 잃는다(가상 목록 `move_cursor`와 같다). 이전·다음 단추는 `turn_month`(비면 `move_to`)를 낸다(결정 8).
- `select(날)`은 날 칸 클릭·탭, Enter·Space(초점 날의 클릭, `process_activation_key` 4201행), UIA Select가 낸다. 커서가 아닌 날의 클릭은 `move_to(날)` 다음 `select(날)`이다(결정 9). 고를 수 없는 날과 범위 밖의 날에는 클릭 액션이 없다. 다른 달의 날도 칸이고 고를 수 있다. 커서가 그 날로 가면 보이는 달이 바뀐다.

**키.** 격자의 `set_key_handler`가 받는다. 초점 날에는 처리기가 없어 조상 전파(`process_handler_key`, 3610행)의 첫 자리가 격자다.

| 키 | 커서 |
| --- | --- |
| ←·→ | 하루 앞·뒤 |
| ↑·↓ | 한 주 앞·뒤 |
| PageUp·PageDown | 한 달 앞·뒤. 날은 그 달 끝으로 접는다(1월 31일에서 2월 28·29일) |
| Shift+PageUp·PageDown | 한 해 앞·뒤(2월 29일에서 2월 28일) |
| Home·End | 그 달 첫날·끝날 (결정 5) |
| Enter·Space | 고르기(초점 날의 클릭) |

- 날 칸은 `key_step_target`을 쓰지 않는다. 값 단계(`process_step_key`, 4382행)는 초점 element의 것만 보고 묶음 단계도 날 칸에는 없어 화살표가 처리기까지 온다. 색 판의 두 축 조합을 쓰지 않는 까닭은 초점이 판(값 하나)이 아니라 날 칸마다 서기 때문이다(결정 2).
- Ctrl·Alt·Meta를 든 키는 `std::nullopt`라 앱 단축키로 흐른다. Shift는 PageUp·PageDown에서만 뜻이 있다. 범위 끝에서 더 갈 곳이 없으면 빈 목록으로 삼킨다. 고를 수 없는 날의 Enter도 빈 목록이라 기본 단추로 가지 않는다.
- 이전·다음 단추는 `turn_month(커서 ± 한 달)`이고 그 달 전체가 범위 밖이면 비활성이다. 앱이 초점 요청을 싣지 않아 누른 뒤 초점은 단추에 남는다(결정 8). 단추가 비활성이 되면 초점은 지워진다.
- Tab 자리는 이전 달, 다음 달, 커서 날 순(그리기 순서)이다. 커서 날만 `set_tab_stop(true)`이고 나머지 날은 거짓이다. popup에서는 Tab이 popup tree 안에서 끝에서 처음으로 돈다(`move_focus`, 4245행). 자리가 아닌 날은 `set_pointer_focus_target(커서 날)`이라 누르면 초점이 커서 날에 선다. 고를 수 없는 날도 화살표 커서로 누름을 받아 뒤의 판까지 가 초점을 지우지 않는다.

**초점(결정 2).** 앱은 `move_to`를 처리할 때 커서를 옮기고 `focus_request { surface = 달력이 선 표면, target = day_id(owner, 새 커서) }`를 싣는다. 새 tree에서 그 날이 Tab 자리라 `focus_target_ready`([focus_request_tracker.cpp](../src/host/focus_request_tracker.cpp) 4~15행, 초점 가능·보임)가 선다. 커서가 다른 달로 넘어 옛 날 칸이 tree에서 빠지면 초점은 잠깐 지워졌다가 요청이 다시 세운다. 테는 `ui_tree::draw_focus_ring`이 초점 날에 두른다.

**접근성.**

- root는 `group`이고 이름은 `text.name`, 비면 기본 문구(달력)다. 격자는 `list`(선택 container, [accessibility.cpp](../src/ui/accessibility.cpp) 179~182행)이고 이름은 달 제목이다. 날 칸은 `list_item`이고 `selected = (날 == value)`, 이름은 `day_name`(비면 "2026년 11월 3일 화요일")에 오늘이면 ", 오늘", 고를 수 없으면 ", 고를 수 없음"을 잇는다(결정 4).
- UIA Select는 `plan_access_request`(78~86행)가 이미 고른 날이면 빈 목록, 아니면 클릭(`select`)이다. "42개 중 몇 번째"는 형제 차례다. 이전·다음 단추는 `button`, 이름과 툴팁은 기본 문구(이전 달·다음 달)다. 날짜 칸은 `combo_box`, 이름 기본(날짜), `expanded = open`, 값은 표시 글이다. expand·collapse는 `set_open(목표)`다.

**그리기·테마·고대비·배율.**

- 고른 날은 `accent` 채움과 `accent_foreground` 글이다. 오늘은 `accent` 테 1.5px이고 고른 날이면 채움 안쪽에 `accent_foreground` 테를 더한다. 색 하나로만 가르지 않는다. 다른 달의 날은 `secondary_foreground`, 고를 수 없는 날과 범위 밖은 `disabled_foreground`다. hover는 `button_hover_background`다. 고대비 팔레트도 같은 역할을 채운다.
- 치수는 논리 픽셀이고 그릴 때 배율을 곱한다. 크기가 글 폭을 재지 않는 상수의 합이라 R2 메뉴 폭 고침(`menu_element::width_for`, 배율로 잰 글 폭)이 막은 잘림이 생기지 않는다. 대가로 앱이 준 긴 요일 이름이나 큰 글자 표에서 글이 칸보다 크면 말줄임한다(사람 확인). 움직임이 없다. 달 넘김 전환은 그리지 않아 움직임 줄이기와 무관하다.

**날짜 칸과 popup.**

- `date_field_element`는 `dropdown_element` 모양이다. 표시 글(값이 없으면 placeholder를 흐리게)과 `icon_calendar`를 그린다. 클릭은 `set_open(!open)`, 키는 `field_popup_key_handler(open, set_open, true)`, 열린 동안 root에 `set_focus_leave_action`을 단다. 글을 받지 않는다.
- `make_date_picker_popup`이 `size_for`에 여백을 더해 크기를 정하고, tree를 `scale`로 배치하고, `place_field_popup`으로 놓는다. 바탕 판이 여백의 누름을 흡수한다.
- 열 때 앱은 커서를 시작 커서로 두고 `focus_request { surface = popup id, target = day_id(owner, 커서) }`를 싣는다. 칸이 스스로 닫히지 않는다(4708~4712행).
- 닫는 계기는 분할 버튼 표 그대로이고 모든 계기에 닫는다. 글 칸이 없어 이동·활성 전환에 남기지 않는다. 날 고르기는 앱이 `select`를 처리하며 값을 바꾸고 닫는다(메뉴 `after_select`의 자리).
- Esc(`escape_key`)와 날 고르기로 닫을 때 앱이 칸으로 돌아오는 초점 요청을 한 번 더 싣는다. 다른 계기에서는 돌리지 않고 누른 자리가 초점을 가진다. 초안이 없다. 커서는 값이 아니어서 Esc·바깥 누름으로 닫으면 값은 그대로다. 색 고르기의 "닫을 때 16진 초안 확정"에 해당하는 일이 없다.
- 한 번에 하나만은 앱의 열린 필드 owner 하나(`open_popup_`)로 지킨다. 열린 동안 칸을 다시 누르면 R3 X-4(`pointer_pressed_event::closed_popup`)가 닫는다. 칸이 `expanded`를 알리므로 이 노드가 할 일은 없다.

## 결정

확정(2026-11-03): N1-input 길 a. 읽기 전용 칸이고 날짜는 달력으로만 고른다. 근거는 [색 고르기 결정 2](color-picker-design.md)와 [dropdown_element.h](../include/luil/ui/dropdown_element.h) 34~36행. 확정 설명은 popup 안 시·분 글 칸 품기까지 담았다. 결정 1이 그 시·분 칸을 R4 범위에서 뺄지 묻는다.

확정(2026-11-03): N1-focus 길 a. 열 때 초점을 달력 popup으로 옮기고 Esc·날 고르기로 닫을 때 칸으로 되돌리는 요청을 앱이 싣는다. controller를 고치지 않는다. 근거는 [색 고르기 결정 3](color-picker-design.md)과 [split-button-design.md](split-button-design.md) 156행.

**1. 시간 고르기.** 질문: 확정 a 설명의 시·분 칸을 R4에 넣을까, 날짜만 내고 `calendar_config` 끝 덧붙임으로 미룰까. 노드 제목은 "날짜·시간"이다. 확정 N1-input a의 설명에는 시·분 글 칸이 들어 있었고 길 a는 그 부분을 이번에 뺀다.

- 길 a: 날짜만 낸다. 시간은 범위 밖이다. 되는 것: popup에 글 칸이 없어 닫는 계기·IME·앱 편집 대상이 없고 크기가 날짜에 머문다. 시간은 뒤에 `calendar_config` 끝에 칸을 더하는 덧붙임이라 이 API를 깨지 않는다. 대가: 날짜와 시각을 함께 받는 앱은 칸 곁에 숫자 칸 둘(`number_input_range { 0, 23 }`·`{ 0, 59 }`)을 스스로 둔다.
- 길 b: 확정 N1-input a의 설명대로 popup 아래에 앱 id의 시·분 글 칸 둘을 품는다(색 고르기 결정 2의 길). 되는 것: 한 컨트롤로 날짜와 시각을 고른다. 대가: 칸마다 앱 kind와 편집 대상 둘, popup이 글을 치는 popup이 되어 이동·활성 전환에 남기고 닫을 때 초안을 확정한다. 숫자 칸 걸음은 범위로 접기만 해 23시 다음이 0시로 돌지 않는다([number_input.h](../include/luil/ui/number_input.h) 59~63행, [dialog_elements.cpp](../src/ui/dialog_elements.cpp) 383행). 12시간제는 R5 지역 몫이다. 문서와 구현이 약 절반 는다.
- 권장: a. 대가: 위의 앱 몫이다. 확정 a의 시·분 설명은 시간을 더할 때의 모양으로 남긴다. 승인: 길 a (2026-11-03).

**2. 격자 안의 초점.** 질문: 화살표가 커서를 옮길 때 키보드 초점은 어디에 두는가. 확정 N1-focus a가 고른 날에 초점이 서도록 정했으므로 길 a를 따른다. b는 그 설명과 갈리고 c는 줄 3 파일(`ui_interaction.cpp`)이라 넘긴 길의 기록이다.

- 길 a: 날 칸마다 초점이 선다. 커서 날만 Tab 자리이고 처리기가 `move_to`를 내면 앱이 커서를 옮기며 그 날로 초점 요청을 싣는다. 되는 것: 확정 N1-focus a의 "고른 날에 초점이 서고 화면 읽기가 그 날을 읽는다"가 그대로 선다. UIA FocusChanged가 날마다 나고 테도 그 칸에 선다. 다음 날은 늘 지어진 칸이라 가상 목록이 초점을 행에 두지 못한 까닭(351~353행)이 여기에는 없다. controller를 고치지 않는다. 대가: 키마다 앱 메시지와 초점 요청 한 번이고, 초점이 다음 게시에 선다. 달을 넘을 때 초점이 잠깐 지워진다.
- 길 b: 격자 하나가 자리다(가상 목록 꼴). 커서는 앱 상태이고 격자 값 글이 커서 날 이름이다. 되는 것: 초점 요청이 열 때 한 번뿐이고 키가 같은 frame에 선다. 대가: 화면 읽기는 초점 이동이 아니라 값 바뀜으로만 날을 듣는다(목록의 값 바뀜을 Narrator가 읽는지 재지 못했다). 확정 N1-focus a의 설명과 갈린다.
- 길 c: controller에 열 수를 가진 두 축 묶음을 더한다. 되는 것: 초점 이동이 controller 안에서 곧바로 서고 앱 몫이 없다. 대가: `ui_interaction.cpp`는 이번 라운드 줄 3(page_anim)의 것이라 그 줄과의 약속과 직렬이 된다. 달을 넘는 이동은 여전히 앱 메시지다.
- 권장: a. 대가: 위의 키마다 요청이다. 앱은 `day_id`로 대상을 짓는다. 요청을 짓는 helper를 두지 않는 까닭은 요청 번호와 표면이 앱 상태라서다(색 고르기 결정 3과 같다). 권장대로 진행.

**3. 보기 단계.** 질문: 머리 줄을 눌러 월 보기·연 보기로 넓히는가.

- 길 a: 일 보기만 둔다. 한 해는 Shift+PageUp·PageDown이다. 되는 것: kind와 상태가 적고 키 규칙이 하나다. 대가: 수십 해 떨어진 날(생일)은 키를 여러 번 친다. `bounds`로 시작 커서를 가깝게 두는 것은 앱 몫이다.
- 길 b: 일·월·연 세 보기와 `view` 앱 상태. 되는 것: 먼 날에 몇 번 만에 닿는다. 대가: 보기마다 격자·키·접근 이름이 따로이고 kind가 는다.
- 권장: a. 대가: 위. b는 config 끝에 `view`를 더하는 덧붙임이다. 권장대로 진행.

**4. 격자의 접근 역할.** 질문: 새 역할(UIA Calendar, Grid·Table 패턴)을 더하는가.

- 길 a: 격자 `list`, 날 `list_item`. 되는 것: Selection·SelectionItem이 지금 provider로 선다. `accessibility.h`·`uia_provider.cpp`를 고치지 않는다. 대가: 행·열(GridItem)을 보조 기술이 모른다. 요일은 날 이름에 든다.
- 길 b: `access_role::calendar`와 Grid·GridItem 패턴. 되는 것: UIA 관례에 맞는다. 대가: `uia_provider.cpp`에 Grid·Table 패턴이 없어(1074~1128행의 패턴 표) 공유 파일 두 곳에 새 패턴을 짓는다.
- 권장: a. 대가: 위. 권장대로 진행.

**5. Home·End.** 질문: 주의 처음·끝인가, 달의 첫날·끝날인가.

- 길 a: 달의 첫날·끝날. 되는 것: 한 달의 끝에 한 번에 닿는다. 주 끝은 ←→ 여섯 번 안이다. 대가: WAI-ARIA 날짜 고르기 예(주의 처음·끝)와 다르다.
- 길 b: 주의 처음·끝. 되는 것: 그 예와 같다. 대가: 달 끝으로 가는 키가 없다.
- 권장: a. 대가: 위. 권장대로 진행.

**6. 고를 수 없는 날.** 질문: 커서가 `available`이 거짓인 날에 서는가.

- 길 a: 서되 고르지 않는다. 되는 것: 화살표가 건너뛸 탐색이 없고, 사용자가 그 날이 왜 막혔는지 이름으로 듣는다. 대가: Enter가 빈 목록이다. 칸을 비활성으로 두면 초점이 지워지므로(4522~4528행) 활성으로 두고 그리기와 이름으로만 알린다.
- 길 b: 건너뛴다. 되는 것: 커서가 늘 고를 수 있는 날이다. 대가: 막힌 날이 이어지면 탐색이 길고 범위 끝에서 멈출 자리를 따로 정한다.
- 권장: a. 대가: 위. 권장대로 진행.

**7. 터치의 달 넘기기.** 질문: 격자 위 가로 쓸기로 달을 넘기는가.

- 길 a: 이전·다음 단추만. 되는 것: 인라인 달력이 감싼 창 안에 있어도 쓸기가 창을 흘린다. 대가: 터치에서 달마다 단추를 누른다.
- 길 b: 가로 쓸기. 되는 것: 모바일 관례다. 대가: 가로로 흘리는 창 안에서 다툼이 생기고 끌기 문턱을 새로 잰다.
- 권장: a. 대가: 위. 권장대로 진행.

**8. 이전·다음 단추의 메시지(구현 중 정함).** 질문: 앱은 `move_to`마다 초점 요청을 싣는데(결정 2), 단추도 `move_to`를 내면 초점이 단추를 떠난다. 단추를 어떻게 가르는가.

- 길 a: `calendar_config` 끝에 `turn_month`를 더하고 단추가 그것을 낸다. 비면 `move_to`다. 앱은 커서만 옮기고 초점 요청을 싣지 않는다. 되는 것: 초점이 단추에 남아 같은 단추를 거듭 누른다(WAI-ARIA 날짜 고르기와 같다). 끝 덧붙임이라 공개 형식을 깨지 않는다. 대가: 앱이 메시지 하나를 더 받는다. 비워 두면 단추를 누를 때 초점이 날로 간다.
- 길 b: 단추도 `move_to`를 내고 초점이 날로 간다. 되는 것: 칸이 늘지 않는다. 대가: 키보드로 달을 거듭 넘기려면 Shift+Tab을 매번 친다.
- 고름: a. 대가: 위.

**9. 커서가 아닌 날의 클릭(구현 중 정함).** 질문: 누른 날로 커서와 초점을 누가 옮기는가. 그 날은 Tab 자리가 아니라 초점을 받지 못한다(`focusable`이 `tab_stop`을 본다).

- 길 a: 클릭이 `move_to(날)` 다음 `select(날)`을 낸다. 누름의 초점은 커서 날에 서고(`set_pointer_focus_target`), 앱이 `move_to`의 초점 요청으로 누른 날에 세운다. 되는 것: 앱 규칙이 "`move_to`는 커서와 초점, `select`는 값" 하나다. popup에서는 `select`의 칸 되돌림 요청이 나중이라 이긴다. 대가: 고르면 닫는 popup에서는 `move_to`가 헛걸음이다.
- 길 b: 클릭은 `select`만 내고 앱이 `select`에서 커서를 옮긴다. 대가: 인라인 달력의 앱이 `select`에서도 초점 요청을 지어야 한다.
- 고름: a. 대가: 위.

## 플랫폼

**Win32.** `win32_window.cpp`·`window_surface.cpp`·`uia_provider.cpp`·`accessibility.h`를 고치지 않는다. UIA는 날짜 칸에 ExpandCollapse와 Value, 격자에 Selection, 날에 SelectionItem, 단추에 Invoke를 낸다. 초점은 논리 초점이고(`GetFocus`, uia_provider.cpp 862~880행) popup 안 날의 FocusChanged도 같은 길이다. popup은 활성화를 받지 않는 창이라 키는 앵커 창에서 오고 논리 초점이 popup tree로 보낸다.

**Android.** Java 소스는 0줄이다. 코어 요소와 overlay라 JNI도 unsupported도 없다. 시스템 날짜 대화는 Java 콜백 구현이 필요해 쓰지 않고 같은 요소를 그린다. 초점 요청은 overlay tree에서 이행한다([android_app.cpp](../src/android/android_app.cpp) 1426~1442행). 탭은 클릭이고 터치 앱은 `cell_size`를 44 안팎으로 올린다(7칸과 여백이 좁은 폰 폭에 들어간다). 글 칸이 없어 소프트 키보드가 뜨지 않는다. Android의 luil에는 접근성 계층이 없다. 접근성 규칙은 Win32 UIA 기준이다.

## 다른 노드와의 약속

- **color_picker(R3, 같은 줄 앞).** 결정 3의 초점 규약을 따르고 날 고르기에도 되돌림을 더한다. 결정 2의 칸 품기는 결정 1이 길 a라 쓰지 않는다. 두 축 키 조합 대신 격자 처리기 하나를 쓴다(결정 2). `color_picker_popup.h` 곁에 새 헤더를 두고 `field_popup.h`는 고치지 않는다.
- **teaching_tip(R4, 같은 줄 뒤).** 겹치는 파일은 `ui_element_id.h`, `inspect_names.cpp`, `default_text.h/.cpp`이고 같은 줄이라 차례로 쌓는다. 데모는 이 노드가 `examples/demo/popups_page.*`와 `examples/mobile_demo/popups_page.*`를 가진다. teaching_tip은 toasts 페이지에 싣는다(split-button-design.md 231행의 겹침을 여기서 가른다).
- **줄 3(page_anim).** `ui_interaction.cpp`·`.h`와 `transition.h`·`spring.h`를 고치지 않는다. 달 넘김 전환은 그리지 않는다. 결정 2의 길 c는 고르지 않으므로 그 줄과의 약속이 없다.
- **줄 4(auto_update).** `win32_window.cpp`·`app_host.h`·`ui_platform.h`·`platform_services*`를 고치지 않는다. 지역 질의를 더하지 않는 까닭이다([달력 날짜 계산과 문구](calendar-date-design.md) 결정 2).
- **kind.** 다섯 kind는 `menu_bar_item` 뒤, `end_of_library_kinds` 바로 위에 두고 `library_kind_name`의 case를 같은 커밋에 끝에 더한다. [ui_element_tests.cpp](../tests/ui_element_tests.cpp) 84행 `last[]`는 이 노드가 `date_field`로 맞추고 병합 때 다른 줄의 마지막 kind와 다시 맞춘다. kind 숫자를 test·UIA 스크립트에 박지 않는다.
- **default_text.** 끝에 더하는 함수는 [달력 날짜 계산과 문구](calendar-date-design.md)에 적었다.
- **데모.** 라이브러리 커밋과 데모 연결 커밋을 나누고 연결은 통합 때 직렬로 쌓는다. 모바일 `app.cpp`에 popups 페이지의 초점 요청을 frame에 싣는 줄이 하나 는다.

**미룬 것을 닫는다.** 앞 문서가 이 노드로 넘긴 자리다. 구현 커밋이 아래 문서 넷([split-button-design.md](split-button-design.md), [color-picker-design.md](color-picker-design.md), [default-text-design.md](default-text-design.md), [components.md](components.md))을 고쳤다.

| 고친 위치 | 고친 내용 |
| --- | --- |
| split-button-design.md 156·231행 | 날짜 칸은 열 때 초점을 popup의 커서 날로 옮기고 Esc·날 고르기로 칸에 돌린다. popups 페이지는 이 노드가 쓰고 teaching_tip은 toasts 페이지로 간다 |
| color-picker-design.md 158·168·221행 | 시·분 칸은 내지 않았다(결정 1). 초점 규약은 따르고 날 고르기에도 되돌림을 싣는다. 키 조합 대신 격자 처리기다(결정 2) |
| default-text-design.md 208행 | 한국어 기본 날짜 문구는 지금 두고 지역 서식은 R5로 남긴다 |
| components.md | 날짜 고르기와 달력 날짜 계산 행을 더한다 |

잇기만 하는 곳은 고치지 않는다. [field_popup.h](../include/luil/ui/field_popup.h) 9행과 split-button-design.md 5·133·229행(날짜 칸이 열림 규약을 따른다), [menu-focus-fixes-design.md](menu-focus-fixes-design.md) 103행(날짜 칸이 `expanded`를 알려 X-4를 받는다)이다.

## 확인

자동 test는 UIA 하나만 빼고 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다.

- 날짜 계산의 test(`tests/calendar_date_tests.cpp`)와 기본 문구는 [달력 날짜 계산과 문구](calendar-date-design.md)에 있다.
- `tests/calendar_element_tests.cpp`(새 파일). 자식 id와 owner(ISO 날짜), `size_for`, `day_id`. 표의 키마다 `move_to` 목표, 말일 접기, 범위 끝의 빈 목록, Ctrl·Alt 조합 nullopt, Shift는 Page에서만. 커서 날만 Tab 자리와 포인터 초점 대상. 클릭·Enter·Space가 `select`, 커서가 아닌 날은 `move_to` 다음 `select`, 고를 수 없는 날은 클릭 없음과 기본 단추 앞의 Enter 빈 목록. 다른 달의 날 클릭. 이전·다음 단추의 `turn_month`와 범위 밖 비활성. 틀린·범위 밖 커서. `move_to`·`select`가 없을 때 표시 전용.
- `tests/date_field_element_tests.cpp`(새 파일). 날짜 칸의 클릭·Alt+↓·F4·↓, 열린 동안 떠남 훅, 표시 글과 `format`, 게시 없이 다시 누르면 `set_open(false)`, 닫힌 frame 뒤의 `closed_popup` 누름. `make_date_picker_popup`의 크기·아래 자리·뒤집기·배율.
- `tests/accessibility_tests.cpp`에 tree 모양(root group 아래 단추 둘과 list, list_item 42개), `selected`, 오늘·고를 수 없음 꼬리, Select 계획. `tests/ui_element_tests.cpp` `last[]`.
- `tests/raster_draw_tests.cpp`. 배율 2에서 고른 날 채움, 오늘 테, 둘이 겹칠 때 안쪽 테를 밝은·어두운·고대비 팔레트마다, 고를 수 있는 날만의 hover 채움. 이 축은 글꼴을 싣지 않아 다른 달·고를 수 없는 날의 숫자 색은 사람 확인이다.
- `tests/uia_provider_tests.cpp`(`luil_tests`, Windows). 날짜 칸 Expand가 `set_open(true)`, 격자 Selection의 고른 날, 날 SelectionItem.Select가 `select`.
- `tests/demo_gallery_tests.cpp`와 `tests/mobile_demo_page_tests.cpp`. popups 페이지가 열 때와 `move_to` 처리 뒤 커서 날로, Esc와 날 고르기로 닫을 때 칸으로 초점 요청을 내는지, 다른 계기에서는 내지 않는지.

데모 칸. 데스크톱 popups 페이지는 색 칸 뒤에 날짜 칸(owner `popups/date-field`)과 label `popups/date-state`("날짜 칸: 닫힘 · 2026년 11월 3일"), 인라인 달력(owner `popups/calendar`, 범위와 주말을 막는 `available`)과 label `popups/calendar-state`("고른 날: 2026-11-03 · 커서: 2026-11-10")를 둔다. 모바일 popups 페이지는 `cell_size` 44의 날짜 칸(owner `date-field`)과 label `date-state`를 둔다. 데모는 시작할 때 오늘을 한 번 읽어 page에 주고 test는 고정 날을 준다. 격리 데스크톱에서 luil-uia로 날짜 칸 Expand 뒤 커서 날에 초점이 섰는지, 날 Select 뒤 label과 칸 값, Esc 뒤 칸으로 돌아왔는지를 본다. 실제 입력이 필요하면 먼저 요청하고 허락한 시간만 쓴다.

사람 확인: 화살표·Page로 훑을 때 초점이 따라오는 빠르기와 달을 넘는 순간, Narrator가 날 이름·고름·"42개 중"을 읽는 방식, 밝은·어두운·고대비에서 고른 날과 오늘의 구분, 배율 150%·200%와 큰 글자에서 요일·날 글, 실기기에서 칸 크기와 popup 자리.

## 범위 밖

- 시·분 고르기(결정 1), 월·연 보기(결정 3), 터치 쓸기(결정 7).
- 기간 고르기(시작과 끝), 여러 날 고르기, 주 번호.
- 날짜를 글로 쳐 넣는 칸(확정 N1-input a). 앱이 칸을 따로 둔다.
- 달 넘김 전환 움직임. page_anim 뒤에 따로 정한다.
- OS 지역 설정 읽기와 지역 서식. R5 i18n의 몫이다.
- Android 접근성 계층.

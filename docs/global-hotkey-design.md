# 전역 단축키

앱이 뒤에 있거나 창이 최소화·숨김이어도 받는 키 조합을 OS에 걸고, 눌림을 앱에 돌려주고, 못 걸었을 때 까닭을 알려 주는 길이다. 앱은 `ui_frame::hotkeys`에 지금 바라는 단축키 목록을 싣고, UI thread가 바뀐 것만 `RegisterHotKey`·`UnregisterHotKey`로 옮긴다. 조합은 앱 안 단축키와 같은 `key_chord`다. Win32 전용이고 Android는 unsupported다(받고 무시한다). 공개 형식은 새 헤더 `include/luil/app/global_hotkey.h`의 `global_hotkey`, `ui_frame::hotkeys`, `ui_platform_capabilities::global_hotkeys`, Win32의 `hotkey_status`·`hotkey_report`·`window_delegate::make_hotkey_message`다.

이 노드는 [알림 영역 아이콘](tray-icon-design.md)과 같은 줄에서 같은 주 창과 같은 꼬리 자리를 이어 쓴다. [작업 표시줄](taskbar-design.md)이 hotkey 몫으로 남긴 "`WM_HOTKEY`는 번호표에 넣지 않는다"(196줄)와 [실행 취소](undo-stack-design.md)가 R3로 미룬 "`key_chord`에 칸을 더할지"(185·226줄)를 이 문서가 닫는다.

## 문제

- 전역 키를 거는 코드가 없다. `src/`·`include/`·`tests/`·`examples/` 어디에도 `RegisterHotKey`, `WM_HOTKEY`가 없다.
- 앱 안 단축키는 앱이 앞에 있을 때만 온다. 키는 초점 표면의 `WM_KEYDOWN`으로 와서 `post_key_event`([window_surface.cpp](../src/win32/window_surface.cpp) 1393줄)가 `key_pressed_event`로 만든다. 다른 앱이 앞이거나 주 창이 `main_hidden`이면 오지 않는다.
- 앱이 직접 걸 수 없다. `RegisterHotKey`는 눌림을 받을 `HWND`와 창 안에서 겹치지 않는 번호(0..0xBFFF)를 요구한다. 공개 헤더는 `HWND`를 드러내지 않고 창 프로시저 훅도 없다([win32_window.h](../include/luil/win32/win32_window.h)). 창이 사라질 때 풀어야 한다.
- 키 이름이 OS에 닿지 않는다. VK에서 `key_code`로 가는 표는 둘이다. `named_key_from_virtual`([surface_input.cpp](../src/win32/surface_input.cpp) 163줄)은 방향·F1~F12 따위를, `named_key_from_win32_virtual`([key_mapping.cpp](../src/host/key_mapping.cpp) 4줄)은 구두점·키패드를 낸다. 반대 방향은 영문자·숫자·플랫폼 대역(`platform_key_of`, [ui_events.h](../include/luil/ui/ui_events.h) 445줄)뿐이다.
- 실패가 정상 경로다. 다른 프로그램이 쓰는 조합은 `ERROR_HOTKEY_ALREADY_REGISTERED`로 실패한다. 호출이 UI thread에서 일어나 앱에 동기 반환이 없고 결과를 돌려줄 길이 필요하다.
- 조합의 형은 이미 있다. `key_chord`([commands.h](../include/luil/ui/commands.h) 11줄)는 문자열 왕복(`shortcut_label`·`parse_shortcut`)과 키 잡기 칸까지 갖췄다. 빠진 것은 걸기·받기·결과다.

## 공개 형식

`include/luil/app/global_hotkey.h`, namespace `luil`. core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake)) OS 헤더가 없다. `commands.h`(`key_chord`)와 `ui_events.h`(`input_action`)를 들인다. `app_host.h`와 `luil.h`가 include한다. 고리는 없다. `app_host.h`가 이 헤더를 들이고, 이 헤더가 `commands.h`→`menu_element.h`→`ui_element.h`를 들이는데 그 사슬에 `app_host.h`를 다시 들이는 곳이 없다(`ui` 헤더 가운데 `combo_box_element.h`만 `app_host.h`를 들인다).

```cpp
namespace luil {
    // 전역 단축키 하나다. `ui_frame::hotkeys`에 실으면 걸리고 빼면 풀린다.
    struct global_hotkey
    {
        // 앱이 정한 키다. 목록 안에서 겹치지 않는다. 비었거나 앞 항목과 같으면 걸지 않는다 (`invalid_id`).
        std::u8string id {};
        // 조합이다. Ctrl·Shift·Alt와 키 하나다 (결정 1). 앱 안 단축키(`app_command::shortcut`)와 같은 값을 쓴다.
        key_chord chord {};
        // 눌렀을 때 UI thread에서 부른다. 눌린 순간의 frame에 실린 같은 id의 것이다. 비면 눌림을 버린다.
        //  - 한 번 눌러 한 번 온다. 누르고 있어도 반복하지 않고 뗌 사건은 없다.
        std::function<input_action()> pressed {};
    };

    // ui_frame 끝(`tray` 뒤):  std::vector<global_hotkey> hotkeys {};         비면 건 것이 없다 (기본)
    // ui_platform_capabilities 끝(`tray` 뒤):  bool global_hotkeys { false };  Win32는 늘 참 (실제 걸림은 보고가 말한다)
}
```

Win32 보고는 [win32_window.h](../include/luil/win32/win32_window.h)의 `tray_report` 옆에 둔다.

```cpp
namespace luil::win32 {
    enum class hotkey_status
    {
        registered,     // 걸렸다
        conflict,       // 다른 프로그램이나 같은 목록의 앞 항목이 이미 쓴다 (ERROR_HOTKEY_ALREADY_REGISTERED)
        reserved,       // OS가 예약한 조합이다 (`system_reserved_chord`, F12)
        needs_modifier, // 글자·이동·편집 키는 Ctrl이나 Alt와 함께여야 한다
        invalid_key,    // 키가 없거나 Win32 가상 키로 옮길 수 없다
        invalid_id,     // id가 비었거나 앞 항목과 겹친다
        failed,         // 그 밖의 OS 실패다. error에 까닭이 있다
    };

    struct hotkey_report
    {
        std::u8string id {};
        hotkey_status status { hotkey_status::registered };
        std::u8string error {}; // failed일 때의 영문 진단이다 (win32_error.h의 make_last_error)
        [[nodiscard]] bool operator==(const hotkey_report&) const = default;
    };
    // window_delegate::make_hotkey_message(const hotkey_report&)  기본은 빈 메시지
}
```

비공개 파일은 셋이다. `src/host/hotkey_sync.h`·`.cpp`(core)는 바라는 목록과 적용한 목록을 견줘 할 호출(풀기 먼저, 걸기 나중)과 걸지 못할 항목, OS 번호와 id의 장부를 낸다. 창이 부르는 입구는 셋이다. `update(목록, 키 잡는 중인가)`는 대조하고(참이면 건 것을 모두 푸는 호출을 내고 거짓이 되면 다시 건다), `reactivated()`는 주 창이 활성화됐을 때 `conflict`·`failed` 항목을 다음 `update`에서 다시 걸게 하고(tray의 `shell_restarted`처럼 창이 알리고 sync가 기록을 고친다), `release_all()`은 끝날 때 모두 푸는 호출을 낸다. `src/host/key_mapping.h`·`.cpp` 끝에 `win32_virtual_from_key(key_code)`(0이면 없음)를 더한다. 플랫폼 header 없이 값으로 적어 core test가 앞의 두 표와 왕복을 잠근다. `src/win32/hotkey_win32.h`·`.cpp`는 `RegisterHotKey`·`UnregisterHotKey`를 함수 객체 seam 뒤에서 부르고 오류 번호를 `hotkey_status`로 옮기며 바뀐 항목만 보고한다.

## 규칙

**선언 모델.** [작업 표시줄](taskbar-design.md)·[알림 영역 아이콘](tray-icon-design.md)과 같다. 앱은 frame마다 지금 바라는 목록을 싣는다. UI thread는 `snapshot_wake_message`에서 `synchronize_tray()` 다음에 `synchronize_hotkeys()`를 불러 마지막으로 적용한 것과 견주고 바뀐 것만 OS에 보낸다. 견주는 값은 id와 chord다. `pressed`는 견주지 않고 눌림이 올 때 그 순간의 frame에서 읽는다.

| 바뀐 것 | OS 호출 |
| --- | --- |
| 없음 → 있음 | `RegisterHotKey(주 창, 번호, 수정자 \| MOD_NOREPEAT, VK)` |
| chord가 바뀜 | `UnregisterHotKey(옛 번호)` 다음 새 번호로 `RegisterHotKey` |
| 있음 → 없음 | `UnregisterHotKey` |

- 한 번의 대조는 풀기를 모두 한 뒤에 건다. 두 id가 조합을 맞바꾸는 frame이 서로 막지 않는다. chord를 바꿔 새 조합이 실패하면 옛 조합은 이미 풀렸다. 앱이 새 조합을 바랐으니 옛 조합이 몰래 살아 있지 않게 한다.
- 같은 목록 안의 같은 조합은 앞 항목만 걸고 뒤 항목은 OS를 부르지 않고 `conflict`로 알린다. OS가 같은 창의 둘째 번호를 어떻게 답하는지에 기대지 않는다.
- OS 번호는 라이브러리가 1부터 차례로 매기고 풀어도 다시 쓰지 않는다(0xBFFF 다음은 1로 돌아 살아 있는 번호를 건너뛴다). 풀기 직전에 게시된 `WM_HOTKEY`가 늦게 도착해도 다른 id의 눌림으로 읽히지 않는다. 장부에 없는 번호와 시스템의 `IDHOT_*`는 무시한다.
- 받는 창은 주 창이다([알림 영역 아이콘](tray-icon-design.md) 결정 2). 주 창이 숨거나(`main_hidden`) 최소화돼도 `HWND`가 살아 있어 눌림이 온다고 알려져 있다(사람 확인). 숨김 동안 요청을 쥐기만 하는 작업 표시줄 단추와 달리 숨김이 등록을 잃게 하지 않는다. `host_`가 없는 smoke 모드는 걸지 않는다.
- 걸고 푸는 일은 UI thread의 창 프로시저 안에서만 한다. 끝날 때(`WM_CLOSE`·`WM_ENDSESSION`·`WM_DESTROY`, tray의 지우기 옆) 모두 풀고 더는 눌림을 받지 않는다. 창이 파괴되면 OS도 거둔다고 알려져 있으나 명시적으로 푼다.

**눌림.** `WM_HOTKEY`의 `wParam`이 OS 번호다. 장부에서 id를 찾고 그 순간의 frame에서 같은 id의 `pressed`를 찾아 부른다. 낸 `input_action`은 `dispatch_action`이 나르고(`ui_frame::close`와 같다) 던지면 `report_runtime_error`다. frame에 그 id가 없으면 버린다.

- `MOD_NOREPEAT`는 늘 켠다. 자동 반복이 필요한 소비자가 생기면 `repeat` 칸을 끝에 더한다(범위 밖).
- 라이브러리는 눌림에서 창을 앞으로 세우지 않는다. 앱이 `main_hidden`을 끄고 `focus_request::activate`를 싣는다. 눌린 프로세스에 앞으로 설 권한이 남는다고 알려져 있으나 frame이 늦게 와도 남는지는 사람 확인에서 본다.
- 앱이 앞에 있어도 같은 길이다. 걸린 조합은 OS가 키 입력 단계에서 가져가 `WM_KEYDOWN`이 오지 않는다. 같은 조합을 `app_command`로도 둔 앱은 앱 안 쪽이 죽으므로 앱이 먼저 견준다(`effective_shortcuts`). 합성 `key_pressed_event`는 없다(결정 4).

**키.** 걸 키는 `key_code`이고 VK는 `win32_virtual_from_key`가 낸다.

| `key_code` | VK |
| --- | --- |
| 영문자·숫자 | `'A'`~`'Z'`·`'0'`~`'9'` (`platform_key_of`) |
| 플랫폼 대역(`first_platform_key` 이상) | 값에서 `first_platform_key`를 뺀 것(F13~F24·미디어·브라우저 키 등) |
| F1~F12, 방향·Home·End·PageUp·PageDown·Insert·Delete·Tab·Enter·Esc·Space·Backspace | `named_key_from_virtual`·`modified_key_from_virtual`의 역 |
| 구두점·키패드 | `named_key_from_win32_virtual`의 역 |
| `none`·`zoom_in`·`zoom_out`·`volume_up`·`volume_down` | 없음, `invalid_key` (앞 셋은 키가 아니거나 글자에서 짓는 키, 뒤 둘은 휴대폰 키다) |

- 수정자 VK(0x10~0x12, 0x5B·0x5C, 0xA0~0xA5)와 1~0xFE 밖의 대역 값도 `invalid_key`다. 앱 안 경로도 수정자 키를 이벤트로 삼지 않는다. 앞으로 `key_code`에 더해질 이름 키(menu_bar의 Alt 단독 따위)는 표에 없어 같은 길로 `invalid_key`다.
- 자판 배열은 VK 기준이다. `RegisterHotKey`는 스캔 코드를 받지 않는다. 앱 안 키 이벤트도 같은 VK라 키 잡기 칸이 잡은 chord가 그대로 걸린다. [키보드 단축키](keyboard-shortcut-design.md)의 "이름은 미국 배열의 자리"는 `VK_OEM_*`의 자리를 말한 것이라 이 노드가 새로 만든 문제가 아니다. 걸린 뒤 사용자가 배열을 바꾸면 같은 VK가 다른 글자일 수 있다(결정 2).

**막는 조합.** 걸기 전에 라이브러리가 거른다. OS를 부르지 않는다.

- `reserved`: `system_reserved_chord`가 참인 조합(Alt+F4·Alt+Tab·Alt+Esc·Ctrl+Esc·Alt+Space·Ctrl+Alt+Delete)과 F12다. Alt+F4를 전역으로 가로채면 어느 앱도 못 닫는다. 키 잡기 칸이 같은 목록을 예약 키로 보인다. `RegisterHotKey` 문서가 F12를 디버거 몫으로 예약한다고 알려져 있어 수정자와 상관없이 막는다.
- `needs_modifier`: 글자를 만들거나 이동·편집에 쓰는 키(영문자·숫자·구두점·키패드·방향·Home·End·PageUp·PageDown·Insert·Delete·Backspace·Enter·Tab·Space·Esc)는 Ctrl이나 Alt가 있어야 한다. Shift만이거나 수정자가 없으면 시스템 전체에서 그 키가 앱으로만 가 타자가 막힌다. F1~F11과 플랫폼 대역의 F13~F24·Pause·PrintScreen·ScrollLock·브라우저·미디어·실행 키(0xA6~0xB7)는 수정자 없이도 된다. 그 밖의 플랫폼 대역 키는 `OEM_102`처럼 글자를 만드는 것이 있어 Ctrl이나 Alt가 필요하다(결정 7).

**키 잡기 중.** `interaction_snapshot::key_capture`([ui_element.h](../include/luil/ui/ui_element.h) 985줄)가 참인 동안 `synchronize_hotkeys()`는 걸린 것을 모두 풀고(알리지 않는다) 거짓이 되면 다시 건다. 키 잡기 칸은 다음 키를 모두 받는다고 약속하는데([키보드 단축키](keyboard-shortcut-design.md)) 걸린 조합은 OS가 먼저 가져가 칸에 오지 않아서다. 앱이 할 일은 없다. 풀린 사이 다른 프로그램이 조합을 잡으면 되걸 때 `conflict`로 알린다. 인터랙션 발행도 wake를 깨우므로([win32_window.cpp](../src/win32/win32_window.cpp) 2005줄이 같은 발행본을 본다) 별도 신호가 없다.

**다시 걸기와 보고.** `conflict`·`failed` 항목은 요청이 같으면 frame마다 다시 걸지 않는다. 주 창이 활성화될 때(`WM_ACTIVATE`가 `WA_INACTIVE`가 아닐 때) 그런 항목을 한 번씩 다시 건다(결정 6). 보고는 항목마다 시도 결과가 바뀔 때 한 번이다. 처음 시도, chord를 바꾼 시도, 다시 걸어 결과가 달라진 때가 그렇다. 항목을 빼면 알리지 않는다. 훅은 Win32 헤더의 `window_delegate`에 있다.

**접근성·배율·테마·고대비·움직임 줄이기·기본 문구.** 해당 없다. 그리는 것이 없고 UIA tree에 서지 않으며 라이브러리가 짓는 글이 없다(`error`는 영문 진단이다). 사용자에게 단축키를 알리는 것은 앱의 화면 글이다(`shortcut_label`). 키를 시스템 전체에서 가져가는 기능이라 앱은 사용자가 켜는 설정 뒤에 두기를 권한다.

## 결정

**1. 조합의 모양.** 질문: 전역 단축키의 수정자를 어디까지 받는가.
- 길 a: `key_chord` 그대로(Ctrl·Shift·Alt). 되는 것: 앱 안 단축키와 같은 형이라 `parse_shortcut`·`shortcut_label`·키 잡기 칸이 그대로 통한다. `commands.h`·`commands.cpp`와 왕복 test(89키×8조합)를 건드리지 않는다. 대가: Windows 키 조합을 못 건다. `RegisterHotKey` 문서가 Windows 키 조합을 OS 몫으로 둔다고 알려져 있고 많은 조합이 이미 셸에 걸려 있다.
- 길 b: `key_chord`에 `bool meta`를 더한다. 되는 것: Windows 키 조합이 앱 안·밖 모두 표현된다(`key_pressed_event::meta`는 이미 있다, ui_events.h 487줄). 대가: `matches`(commands.cpp 13줄은 `event.meta == false`를 요구한다)·`shortcut_label`·`parse_shortcut`·`find_conflicts`가 움직이고 왕복 test가 16조합이 된다. 앱 안에서는 Windows 키 조합이 OS에 먼저 가 거의 오지 않아 얻는 것이 전역 쪽에만 있다.
- 길 c: `global_hotkey::windows` 칸을 따로 둔다. 되는 것: `key_chord`와 그 test를 건드리지 않고 Windows 키 조합이 된다. 대가: 칸이 chord 밖이라 문자열 왕복과 키 잡기 칸이 Windows 키를 모른다. 앱이 둘을 따로 저장한다.
- 권장: a. b·c는 나중에도 끝에 칸을 더하는 길로 열려 있다(`key_chord`를 지정 초기화로 지은 곳이 있어 끝 칸이 깨지 않는다, undo_commands.cpp 15줄). 이 길이면 실행 취소 문서가 hotkey 몫으로 둔 `key_chord` 변경은 일어나지 않는다. 대가: Windows 키 조합이 필요한 앱은 당장 못 쓴다. **승인 필요.**

**2. 키를 가르는 기준.** 질문: 배열이 달라도 같은 자리의 키를 거는가.
- 길 a: VK 기준. 되는 것: 앱 안 키 이벤트와 같은 VK라 키 잡기 칸이 잡은 chord가 그대로 걸린다. 새 표는 역표 하나다. 대가: 걸린 뒤 배열을 바꾸면 같은 VK가 다른 글자일 수 있고, 구두점 조합이 비미국 배열에서 기대한 자리가 아닐 수 있다.
- 길 b: 스캔 코드로 자리를 정하고 지금 배열의 VK로 바꿔 건다. 되는 것: 자리가 고정된다. 대가: 앱 안 경로(VK)와 어긋나 같은 chord가 안과 밖에서 다른 키를 뜻한다. `WM_INPUTLANGCHANGE`마다 다시 걸어야 하고 배열 의존 test가 든다.
- 권장: a. 대가: 배열을 바꾼 뒤의 어긋남은 알려진 한계로 둔다. **권장대로 진행.**

**3. 눌림을 넘기는 길.** 질문: 눌림을 앱에 어떻게 주는가.
- 길 a: `global_hotkey::pressed` 함수(`input_action`을 낸다). 되는 것: 걸기와 동작이 한 곳이고 `tray_icon::activate`·`ui_frame::close`와 같은 모양이다([알림 영역 아이콘](tray-icon-design.md) 결정 5). delegate를 넓히지 않고 앱 메시지도 `ui_command`도 낸다. 대가: frame 값에 함수가 든다. 노드 설명이 처음 적은 `app_delegate` 알림에서 한 걸음 벗어난다.
- 길 b: `window_delegate::make_hotkey_message`가 보고와 눌림을 함께 받는다. 되는 것: 훅이 하나이고 frame에 함수가 없다. 대가: 앱 메시지만 낼 수 있고 id를 메시지로 바꾸는 코드가 delegate에 따로 선다. 걸기와 동작이 갈린다.
- 길 c: `app_delegate`의 새 훅. 되는 것: 노드 설명의 모양이다. 대가: Android가 부르지 않는 훅이 플랫폼 중립 자리에 선다(알림 영역 아이콘 결정 5가 같은 까닭으로 물렸다).
- 권장: a. 그 결정과 함께 움직인다(그쪽이 바뀌면 이쪽도 따른다). 대가: frame의 함수 하나. **승인 필요.**

**4. 앱이 앞에 있을 때.** 질문: 걸린 조합이 앱이 앞일 때도 같은 길인가.
- 길 a: 늘 `pressed`로만. 되는 것: 길이 하나이고 같은 조합을 앱 안 단축키로 쓰지 않으면 영향이 없다. 대가: 같은 조합을 `app_command`로도 둔 앱은 앱 안 쪽이 죽는다. 앱이 먼저 견준다.
- 길 b: 앱이 활성일 때 합성 `key_pressed_event`도 낸다. 되는 것: 앱 안 단축키가 그대로 선다. 대가: OS가 가져간 키를 되살려 `pressed`와 두 번 일하게 되고 어느 쪽이 이기는지의 규칙이 든다. 텍스트 칸에서 눌렀을 때 위험하다.
- 길 c: 앱이 활성일 때는 풀고 비활성일 때만 건다. 되는 것: 앱 안은 원래대로이고 밖에서만 전역이다. 대가: 활성 전환마다 풀고 거는 일이 생기고 풀린 사이 다른 프로그램이 조합을 잡아 되걸기가 실패한다. `WM_ACTIVATE` 순서에 기댄다.
- 권장: a. 대가: 겹치는 조합은 앱의 몫이다. **권장대로 진행.**

**5. 결과 보고의 거칠기.** 질문: 못 걸었을 때 앱이 무엇을 아는가.
- 길 a: 일곱 값(위 enum). 되는 것: 앱이 사용자 글을 값마다 달리 쓴다("다른 프로그램이 쓰고 있다", "Ctrl이나 Alt를 함께 누르라"). 대가: 값이 많다.
- 길 b: 성공·실패 둘. 되는 것: 단순하다. 대가: 충돌과 모양 잘못을 가르지 못하고 까닭이 영문 `error`뿐이다.
- 길 c: 알리지 않는다. 대가: 안 되는 까닭을 사용자도 앱도 모른다.
- 권장: a. 대가: 값 일곱과 타입 둘. **권장대로 진행.**

**6. 충돌 뒤 다시 걸기.** 질문: 다른 프로그램이 조합을 놓은 뒤 어떻게 다시 거는가.
- 길 a: 주 창이 활성화될 때 한 번. 되는 것: 앱 코드가 없다. 사용자가 다른 프로그램을 끄고 이 창을 열면 자리를 잡는다. 대가: 활성화가 없는 트레이 앱은 충돌이 풀려도 다음 활성화나 요청 변화까지 모른다.
- 길 b: `global_hotkey::retry`(번호)를 두고 바뀌면 다시 건다. 되는 것: 앱이 "다시 시도" 단추로 정확히 부른다. 대가: 칸과 앱 상태가 하나 는다. frame은 가장 새 것만 읽혀 "한 frame 빼고 다시 싣기"가 다시 걸기를 보장하지 못해 칸이 필요하다.
- 길 c: 다시 걸지 않는다. 대가: 앱을 다시 켜거나 chord를 바꿀 때까지 안 된다.
- 권장: a. b는 끝에 칸을 더해 나중에 낼 수 있다. 대가: 활성화 없는 앱은 늦게 안다. **권장대로 진행.**

**7. 막는 조합.** 질문: 시스템 전체 입력을 해칠 조합을 누가 막는가.
- 길 a: 라이브러리가 `reserved`·`needs_modifier`를 걸러낸다. 되는 것: 앱 실수가 사용자 PC 전체의 입력 장애가 되지 않는다. 대가: 수정자 없는 글자 키를 정말 가로채야 하는 도구는 못 쓴다.
- 길 b: OS에 맡긴다. 되는 것: 제한이 없다. 대가: `Shift+A`나 Space를 걸면 그 키가 시스템 전체에서 앱으로만 가고, 앱이 죽어도 OS가 풀 때까지 이어진다.
- 길 c: 모든 조합에 Ctrl이나 Alt를 요구한다. 되는 것: 규칙이 단순하다. 대가: F키·미디어 키 같은 정당한 단독 키를 막는다.
- 권장: a. 대가: 표(단독 허용 키)를 라이브러리가 쥔다. **권장대로 진행.**

## 플랫폼

### Win32

- `application_window`가 `hotkey_win32` 하나를 쥐고 `host_`가 선 뒤 주 창 `HWND`를 넘긴다. `RegisterHotKey`는 `user32`라 새로 링크할 것이 없다. 능력 플래그는 `publish_platform()`에 한 줄이고, 그 함수의 "이미 같으면 돌아간다" 조건에도 항을 더한다. 늘 참이다(`clipboard_image_copy`와 같다).
- 레이어드 표시와 Direct3D 합성 주 창도 같다. 등록은 창 표면과 무관하다.
- 알려진 한계(사람 확인): 관리자 권한 창이 앞일 때 낮은 권한 앱의 조합은 UIPI로 오지 않는다. 잠금 화면·UAC 보안 데스크톱·다른 세션에서는 오지 않는다. 다른 프로그램이 먼저 건 조합은 못 건다. 저수준 키 훅을 쓰는 프로그램이 먼저 가져갈 수 있다.

### Android

- unsupported다. 앱이 뒤에 있을 때 키를 받는 길은 `AccessibilityService`의 키 필터나 입력기 서비스뿐이고 둘 다 `Service` 하위 클래스를 요구해 Java 클래스를 써야 한다(Java 소스 0줄 방침, [android-port-plan.md](android-port-plan.md)). 앞에 있는 동안의 하드웨어 키보드 단축키는 기존 키 이벤트 길이 처리한다.
- host는 `ui_frame::hotkeys`를 읽지 않는다. 받고 무시하고 로그도 없다(`taskbar`·`tray`와 같다). [android_app.cpp](../src/android/android_app.cpp) 550줄의 `set_ui_platform`은 필드를 이름으로 적으므로 `global_hotkeys`는 손대지 않아도 거짓이다. 보고 훅이 Win32 헤더에만 있어 Android는 `unsupported` 보고를 따로 내지 않는다. 앱은 능력으로 가른다. JNI도 Java도 쓰지 않는다.

## 다른 노드와의 약속

**같은 줄 tray(앞).** 공용 꼬리에 tray가 먼저, hotkey가 뒤에 더한다. 같은 줄이라 차례로 쌓여 텍스트 충돌이 없다.

| 자리 | tray | hotkey |
| --- | --- | --- |
| `ui_frame` 끝(`tray` 뒤) | `tray` | `hotkeys` |
| `ui_platform_capabilities` 끝 | `tray` | `global_hotkeys` |
| `publish_platform()` | 한 줄 | 그 뒤 한 줄(조건에 항 추가) |
| `win32_window.h` 보고 타입(`tray_report` 뒤) | `tray_status`·`tray_report` | `hotkey_status`·`hotkey_report` |
| `window_delegate` 끝(`make_tray_message` 뒤) | `make_tray_message` | `make_hotkey_message` |
| `app_delegate.h` | 고치지 않는다 | 고치지 않는다(결정 3 a) |
| `window_messages.h` | `WM_APP + 7` | 고치지 않는다. `WM_HOTKEY`는 시스템 번호다 |
| `app_host.h`·`luil.h` include | `tray_icon.h` | 그 뒤 `global_hotkey.h` |

- 받는 창은 tray가 정한 주 창이다. 새 창도 새 메시지 번호도 만들지 않는다.
- `win32_window.cpp`에서 hotkey가 고치는 자리: 새 함수 `synchronize_hotkeys`·`handle_hotkey`·`post_hotkey_report`, 멤버 `hotkeys_`. `snapshot_wake_message`(950줄)의 `synchronize_tray()` 다음 줄. 새 `case WM_HOTKEY`. 이미 있는 `case WM_ACTIVATE`(1109줄)의 `WA_INACTIVE`가 아닌 가지(다시 걸기). `WM_CLOSE`(1070줄)·`WM_ENDSESSION`(1094줄)·`WM_DESTROY`(1262줄)의 tray 지우기 옆. `publish_platform()`(2229줄). tray의 `WM_DISPLAYCHANGE`·`WM_SETTINGCHANGE` 가지는 건드리지 않는다.

**줄 3 menu_bar.** `surface_input.cpp`·`window_surface.cpp`·`ui_interaction.*`를 고치지 않는다. 역표는 `key_mapping`에 있어 menu_bar와 같은 파일이 없다. 겹치는 일이 하나 있다. menu_bar의 Alt 단독 판정은 "Alt를 누른 뒤 뗄 때까지 다른 키가 없었다"인데, 걸린 Alt 조합의 글자 키는 OS가 가져가 창에 오지 않는다. 앱이 앞일 때 Alt 조합을 누르고 떼면 판정이 Alt 단독으로 읽혀 막대가 켜질 수 있다(비슷한 현상이 알려져 있다, 실측 전). 약속: menu_bar는 그 판정을 지우는 한 줄 호출을 `window_surface`에 내고(이름은 menu_bar 문서가 정한다), hotkey의 `case WM_HOTKEY`가 그것을 부른다. menu_bar가 그 호출을 내지 않으면 hotkey 구현 커밋은 호출 없이 들어가고 통합이 한 줄로 잇는다.

**줄 1·2.** frozen_col·color_picker·rich_edit와 같은 파일을 고치지 않는다.

**R1·R2 파일.** [키보드 단축키](keyboard-shortcut-design.md)에 구현 커밋이 한 줄("전역 단축키는 [이 문서]")을 더하고, [기기 요청](device-request-design.md)의 능력 표에 한 행을 더한다. `undo_commands.cpp`의 지정 초기화는 결정 1이 a면 필요 없어졌지만 그대로 두어도 안전하다.

**검사기·기본 문구.** 새 요소가 없어 `ui_element_id`의 kind, `inspect_names.cpp`의 case, `default_text`의 함수를 더하지 않는다.

**데모 공용 파일.** `examples/demo_main.cpp`는 `frame->tray` 다음 줄에 `frame->hotkeys = windows_.make_hotkeys_request();`, `make_tray_message` 뒤에 `make_hotkey_message` override 하나다. `examples/demo/windows_page.*`는 `add_tray_section` 뒤에 `add_hotkey_section`을 둔다. `examples/mobile_demo/device_page.cpp`는 tray 줄 뒤에 둔다. 라이브러리 커밋과 데모 연결 커밋을 나누고, 데모 연결은 통합 때 직렬로 쌓는다. `docs/README.md` 색인은 통합 때 붙인다.

## 확인

- `tests/hotkey_sync_tests.cpp`(새 파일, core, `luil_core_tests`): 빈 목록은 호출 없음. 처음 싣으면 걸기 하나, 같은 목록은 호출 없음. chord가 바뀌면 풀기 다음 새 번호로 걸기(번호를 다시 쓰지 않음). 빼면 풀기. 두 id가 조합을 맞바꾸는 frame은 풀기가 모두 먼저. 같은 목록의 같은 조합은 뒤 항목이 `conflict`. 빈·겹친 id는 `invalid_id`. `none`·`zoom_in`·`volume_up`·수정자 VK·범위 밖 대역은 `invalid_key`. `system_reserved_chord`와 F12는 `reserved`. 글자·이동·편집 키가 수정자 없음·Shift만이면 `needs_modifier`, F1~F11과 F13~F24 단독은 통과, 그 밖의 대역은 Ctrl·Alt가 필요. 실패를 알리면 같은 요청으로 다시 걸지 않고 `reactivated()` 뒤의 `update`에서만 다시. 키 잡기 중에는 모두 풀고 끝나면 다시 걸며 그 사이 보고가 없음. 0xBFFF 다음 1로 돌아 살아 있는 번호를 건너뜀. `win32_virtual_from_key`가 `named_key_from_win32_virtual`·`platform_key_of`와 왕복.
- `tests/global_hotkey_tests.cpp`(새 파일, `luil_tests`): 모든 VK(1~0xFE)를 앱 안 경로(`named_key_from_virtual`, 없으면 `modified_key_from_virtual`)에 먹여 나온 키를 `win32_virtual_from_key`로 되돌려 같은 VK인지, 반대로 이름 키마다 거꾸로 가도 같은 키인지 본다(어긋나면 VK 값이 메시지에 나온다). chord → `MOD_*`·VK 변환. 가짜 seam으로 `ERROR_HOTKEY_ALREADY_REGISTERED` → `conflict`, 다른 오류 → `failed`(`error`에 번호). 진짜 `RegisterHotKey`로 드문 조합(Ctrl+Alt+Shift+F13~F18, test마다 하나)을 숨은 창에 걸고 둘째 창이 같은 조합을 걸면 실패하는지, 목록에서 빼면 둘째가 성공하는지, 끝에 모두 푸는지. ctest가 test를 병렬로 돌려도 부딪히지 않게 test마다 키가 다르다. 창 프로시저 길은 [win32_window_tests.cpp](../tests/win32_window_tests.cpp) 969줄의 알림 test와 같은 틀(`run_application_window`, 조합을 실은 frame을 주는 driver, 보고를 받는 delegate)이다. 등록 보고가 오면 다른 thread가 `PostMessageW(WM_HOTKEY, 1, ...)`를 보내 `pressed`가 앱 메시지로 닿는지, 장부에 없는 번호는 무시하는지, 목록에서 뺀 뒤 번호 1이 무시되는지, 창이 닫힌 뒤 같은 조합을 걸 수 있는지(풀렸는지)를 본다. OS의 실제 키 경로가 아니라 창 프로시저 경로만 본다.
- `tests/device_request_tests.cpp`(38~44줄)의 기본 능력 단언에 `global_hotkeys == false`를 더한다. `tests/window_messages_tests.cpp`는 번호표가 그대로라 고치지 않는다.
- `tests/windows_page_tests.cpp`(추가): 전역 단축키 구역의 줄 이름, 켜기·끄기·충돌 시험이 요청에 닿는지, 눌림 intent와 보고 intent가 줄에 닿는지.
- 데모 `examples/demo/windows_page.cpp`의 "전역 단축키" 구역: 단추 `전역 단축키 켜기`·`전역 단축키 끄기`·`충돌 시험 켜기`(같은 조합의 둘째 id를 더한다)와 토글 `눌리면 창 앞으로`. 조합은 충돌이 드문 `Ctrl+Alt+Shift+F11`이다. label 넷이 UIA로 읽힌다: 능력 `전역 단축키 능력: 있음`, 요청 `전역 단축키 요청: 켬 · Ctrl+Alt+Shift+F11`, 반영 `전역 단축키 반영: 등록됨`(`충돌`·`예약된 조합`·`수정자 필요`·`실패 (<진단>)`·`끔`), 눌림 `전역 단축키 눌림: 0번`. 둘째 id를 더하면 반영 줄이 `충돌 (demo-extra)`을 함께 보인다. 눌림 intent는 횟수를 올리고 토글이 켜져 있으면 `main_hidden`을 끄고 `focus_request::activate`를 싣는다.
- `examples/mobile_demo/device_page.cpp`는 `capabilities.global_hotkeys == false`일 때 `hotkeys-none`("이 플랫폼에는 전역 단축키가 없다.")을 보인다. `tests/mobile_demo_page_tests.cpp`(core)가 이름을 읽는다.
- 격리 데스크톱에서 UIA로 되읽는 것은 걸림(`등록됨`)과 같은 목록의 충돌(`충돌`), 끄면 `끔`까지다. 진짜 키를 눌러 `WM_HOTKEY`가 오는 길은 입력 데스크톱에서만 서서 격리 데스크톱으로 재지 못하고, `SendInput`은 사용자의 데스크톱에 닿으므로 쓰지 않는다. 핫키 표가 데스크톱 단위라 격리 데스크톱의 등록이 사용자의 키를 막지 않는다고 보지만 실측 전이다. 실제 `RegisterHotKey`를 부르는 test도 개발자 PC의 키를 잠깐 쥐므로 위의 드문 조합만 쓴다.
- 사람 확인: 다른 앱이 앞일 때·창을 최소화했을 때·`main_hidden`일 때 조합을 누르면 눌림이 늘고 키가 앞 앱으로 새지 않는지. 앱이 앞일 때도 `pressed`로만 오는지. 다른 프로그램이 같은 조합을 쓰는 중에 `충돌`이 보이고, 그 프로그램을 끄고 이 창을 활성화하면 `등록됨`으로 바뀌는지. 키 잡기 칸(버튼과 컨트롤 페이지의 단축키 잡기)에서 걸린 조합을 눌러도 칸이 받는지. 눌림에서 숨은 창이 앞으로 서는지(앞으로 설 권한). 관리자 권한 창이 앞일 때·잠금 화면·UAC 보안 데스크톱(안 오는 것이 정상). 한국어 자판에서 구두점 조합과 배열을 바꾼 뒤. Alt가 든 조합을 앱이 앞일 때 눌러 메뉴 모드나 메뉴 막대가 켜지지 않는지(menu_bar 뒤). 앱을 끝낸 뒤, 비정상 종료한 뒤 다른 프로그램이 같은 조합을 걸 수 있는지. 같은 앱의 둘째 인스턴스가 같은 조합을 걸 때 `충돌`인지. 원격 데스크톱 세션.

## 범위 밖

- Windows 키 조합(결정 1), 스캔 코드 기준(결정 2).
- 뗌 사건과 자동 반복(`repeat` 칸), 누르는 동안만 켜지는 기능. 저수준 키 훅(`WH_KEYBOARD_LL`)은 키 로거와 같은 권한이라 쓰지 않는다.
- 한 id에 chord 여럿, 앱 안 `app_command`와 전역 조합의 충돌 helper, 조합을 고르는 중에 미리 검사하는 순수 함수(지금은 보고의 `reserved`·`needs_modifier`가 대신한다).
- 다시 걸기 타이머와 `retry` 칸(결정 6).
- 사용자 설정의 저장(앱의 몫이다. 문자열은 `shortcut_label`이 왕복한다), 창 앞세우기 정책, 다른 프로그램이 걸어 둔 조합을 알아내는 일.
- Android의 전역 키.

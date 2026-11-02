# 실행 취소 stack

앱이 한 번의 사용자 동작(항목 삭제, 체크, 순서 바꿈)을 되돌리고 다시 실행하는 기록을 라이브러리가 든다. 칸 하나의 글만 되돌리는 `text_edit_state`의 기록과 달리 칸 밖의 앱 상태를 다룬다. 한 걸음은 앱이 정한 `app_message` 쌍(되돌림, 다시 실행)이고, stack은 그 내용을 읽지도 상태를 바꾸지도 않는다. Qt의 `QUndoStack`이 하는 일 가운데 불변 상태와 메시지 구조에 맞는 것만 가져온다. 공개 형식은 [undo_stack.h](../include/luil/ui/undo_stack.h)의 `undo_stack`·`undo_step`·`undo_macro`와 [undo_commands.h](../include/luil/ui/undo_commands.h)의 `make_undo_commands`다. 둘 다 플랫폼을 모르는 값과 순수 함수이고 logic thread가 쓴다.

## 문제

앱이 칸 밖의 동작을 되돌리려면 기록을 직접 짓는다.

- 지금 있는 되돌리기는 칸 하나의 글 스냅샷뿐이다. `text_edit_state::undo`·`redo`가 시점마다 글 전체를 복사한다([text_edit.h](../include/luil/text/text_edit.h)). 칸 밖에서 바뀌는 상태는 되돌릴 길이 없다. 데모 변경 작업대의 체크·스테이지·버리기가 그렇다([workbench_page.cpp](../examples/demo/workbench_page.cpp)의 `workbench_change_check_intent`·`workbench_stage_all_intent`·메뉴의 `discard`).
- 앱마다 같은 규칙을 다시 짠다. 되돌릴 정보를 어떻게 들지, 끌기 같은 연속 동작을 한 걸음으로 합칠지, 용량을 어떻게 막을지, 저장한 시점을 어떻게 기억할지, 메뉴의 "실행 취소: …" 글과 켜짐을 어떻게 맞출지다. 칸은 합치기(`text_edit_group`)와 용량(`text_edit_history_limit`, `text_edit_history_byte_limit`)을 이미 정했지만 앱은 그 규칙을 가져다 쓸 수 없다.
- 키가 어디로 가는지 앱이 알아야 한다. 초점이 칸이면 Ctrl+Z·Ctrl+Shift+Z·Ctrl+Y는 칸이 먼저 먹는다([ui_interaction.cpp](../src/ui/ui_interaction.cpp)의 `process_text_input_key`, `key_code::key_z`·`key_y` 갈래). [key-propagation-design.md](key-propagation-design.md)의 순서에서 텍스트 칸은 4단계, 앱 정책의 `on_key`는 11단계라 앱 stack은 초점이 칸 밖일 때만 닿는다. 이 경계를 약속해 두지 않으면 앱이 이중으로 반응하거나 닿지 않는 자리를 모른다.
- 명령표(`app_command`)는 있지만 실행 취소·다시 실행 두 항목의 id와 단축키 묶음(Ctrl+Z, Ctrl+Y와 Ctrl+Shift+Z)을 앱마다 다시 적는다. [keyboard_shortcut_tests.cpp](../tests/keyboard_shortcut_tests.cpp)의 다시 실행 예(238~250행)가 그 모양이다.

## 공개 형식

```cpp
// include/luil/ui/undo_stack.h (app_message.h를 include한다)
inline constexpr std::size_t undo_stack_default_steps { 1000 };
inline constexpr std::size_t undo_stack_default_bytes { std::size_t { 32 } * 1024 * 1024 };  // 칸 기록의 상한과 같다

struct undo_step
{
    std::u8string label {};  // 메뉴에 보일 걸음의 이름이다("항목 삭제"). 글은 앱의 말이다.
    app_message undo {};  // 되돌릴 때와 다시 실행할 때 앱이 적용할 메시지다. 둘 다 비어 있지 않아야 한다.
    app_message redo {};
    std::u8string merge_key {};  // 비어 있지 않고 바로 앞에 쌓은 걸음의 키와 같으면 한 걸음으로 합친다.
    std::size_t weight { 0 };  // 걸음이 쥔 바이트의 어림이다. 메시지 안을 볼 수 없어 앱이 알린다. 0이면 걸음 수만 센다.
};

struct undo_stack_limits
{
    std::size_t steps { undo_stack_default_steps };  // 0이면 제한이 없다
    std::size_t bytes { undo_stack_default_bytes };  // 0이면 제한이 없다
};

using undo_apply = std::function<void(const app_message& message)>;

// logic thread 전용이다. 스레드 안전하지 않다.
class undo_stack
{
public:
    explicit undo_stack(undo_stack_limits limits = {});
    bool push(undo_step step);  // redo를 비운다. 합쳐도 참이다. 메시지가 비었거나 재생 중이면 거짓이고 바뀌는 것이 없다.
    void begin(std::u8string label);  // begin과 end 사이의 push는 한 걸음이 된다. 중첩할 수 있다.
    bool end();  // 짝 없는 end는 거짓이다.
    // 한 걸음을 되돌리거나 다시 실행한다. 걸음 안의 메시지를 undo는 거꾸로, redo는 쌓은 차례로 apply에 넘긴다. apply가 비었거나 걸음이 없으면 거짓이다.
    bool undo(const undo_apply& apply);
    bool redo(const undo_apply& apply);
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] std::u8string_view undo_label() const noexcept;  // 다음에 되돌릴(다시 실행할) 걸음의 이름이다. 없으면 빈 글이다.
    [[nodiscard]] std::u8string_view redo_label() const noexcept;
    [[nodiscard]] std::size_t undo_count() const noexcept;
    [[nodiscard]] std::size_t redo_count() const noexcept;
    [[nodiscard]] std::size_t bytes() const noexcept;  // 되돌릴 수 있는 걸음의 weight 합이다. redo 쪽은 세지 않는다
    void break_merge() noexcept;  // 다음 push는 합치지 않는다
    void set_clean() noexcept;  // 지금 자리를 저장 지점으로 삼는다
    [[nodiscard]] bool clean() const noexcept;
    void clear() noexcept;  // 걸음을 모두 버린다. 빈 자리가 저장 지점이다
    void set_limits(undo_stack_limits limits);
};

// begin/end의 범위 짝이다. 복사·이동할 수 없다.
class undo_macro
{
public:
    undo_macro(undo_stack& stack, std::u8string label);
    ~undo_macro();
};

// include/luil/ui/undo_commands.h (commands.h를 include한다)
inline constexpr std::u8string_view undo_command_id { u8"edit.undo" };
inline constexpr std::u8string_view redo_command_id { u8"edit.redo" };

struct undo_command_state
{
    std::u8string undo_label {};  // 메뉴·팔레트에 보일 최종 글이다. 앱이 이름과 낱말을 이어 짓는다. 키 판정용 표에서는 비워도 된다.
    std::u8string redo_label {};
    bool can_undo { true };
    bool can_redo { true };
};

// { 실행 취소(Ctrl+Z), 다시 실행(Ctrl+Y, 추가로 Ctrl+Shift+Z) }다. 범위는 window이고 enabled는 can_*를 따른다.
[[nodiscard]] std::array<app_command, 2> make_undo_commands(undo_command_state state);
```

```cpp
// 앱의 handle (logic thread). 사용자 동작은 걸음을 쌓은 뒤 redo와 같은 메시지를 적용한다.
if (const auto* const discard { message.get<discard_requested>() }; discard != nullptr)
{
    const luil::app_message remove { remove_items_intent { keys } };
    history_.push({ .label = u8"변경 버리기", .undo = luil::app_message { restore_items_intent { removed } }, .redo = remove, .weight = bytes_of(removed) });
    apply(remove);  // 처음 실행과 다시 실행이 한 길이다
}
else if (message.get<undo_requested>() != nullptr)
    history_.undo([this](const luil::app_message& step) { apply(step); });
```

## 규칙

**걸음.**

- 걸음은 앱이 정한 메시지 쌍이다. stack은 메시지를 열어 보지 않는다. 되돌리면 `undo` 메시지를 `apply`로 받은 앱이 자기 `handle`의 길로 상태를 바꾼다. 상태는 handle에서만 바뀐다는 [app-message.md](concepts/app-message.md)의 규칙이 그대로 선다.
- **메시지는 절대 값이다.** 합친 걸음은 앞 걸음의 `undo`와 뒤 걸음의 `redo`만 남기므로, "다음 값으로 토글"이 아니라 "이 값으로 정함"을 담아야 중간 걸음이 없어도 맞다. 상태 스냅샷도 같은 길이다. `restore_intent { before }`와 `{ after }`를 담으면 되고, `app_message`가 payload를 공유해 걸음이 복사본을 따로 쥐지 않는다. 쥔 크기는 `weight`로 알린다. 역 메시지가 맞는지는 라이브러리가 검증할 수 없어 앱의 test가 "동작 → 되돌림 → 처음 상태"를 잠근다.

**재생 중에는 쌓지 않는다.** `undo`·`redo`가 `apply`를 부르는 동안 stack은 재생 중이다.

- 이때 `push`·`end`·`undo`·`redo`는 거짓을 돌려주고 `begin`·`set_clean`·`break_merge`·`clear`·`set_limits`는 아무 일도 하지 않는다. 질의는 되고, 위치는 `apply`를 부르기 전에 이미 옮겨 둔다. `apply`가 예외를 던져도 재생 표시는 풀리고 위치는 옮긴 채로 남는다.
- 앱이 재생 메시지를 사용자 동작과 같은 타입으로 만들었거나 기록하는 `handle`에 그대로 흘려도, 그 `handle`이 낸 `push`가 무시되어 redo가 지워지거나 걸음이 두 번 쌓이지 않는다. 메시지 목록을 돌려주는 길로는 막을 수 없다(결정 2).

**합치기.** 시간 창을 두지 않는다. 앱이 키로 말한다.

- `merge_key`가 비어 있지 않고 바로 앞에 쌓은 걸음의 키와 같으면 한 걸음이 된다. 앞 걸음의 `undo`, 뒤 걸음의 `redo`와 이름이 남고 `weight`는 둘 중 큰 쪽이다. 같은 키는 같은 대상을 절대 값으로 바꾸는 걸음끼리만 쓴다.
- 합치기는 push 바로 뒤의 push에만 이어진다. `undo`·`redo`·`set_clean`·`clear`·`break_merge`·`begin`이나 키 없는 push가 사이에 오면 끊긴다. 저장 지점에 서 있을 때도 합치지 않는다. 합치면 저장한 상태로 돌아갈 수 없다. 묶음 안의 push는 합치지 않는다.
- 시계를 두지 않는 까닭은 stack이 순수 값이라 test가 시각 없이 돌아야 하고, 사용자가 천천히 두 번 한 일과 빠르게 두 번 한 일을 시간이 가르면 체감이 우연에 달리기 때문이다. 한 번의 끌기가 끝나면 앱이 `break_merge()`를 부르거나 끌기 번호를 키에 넣는다.

**묶음.** `begin`과 `end` 사이의 `push`는 한 걸음이 된다.

- 중첩할 수 있고 바깥 `end`에서 확정한다. 이름은 바깥 `begin`의 것이다. 걸음 안에서 `undo`는 쌓은 반대 차례, `redo`는 쌓은 차례로 `apply`에 넘긴다. 빈 묶음은 걸음을 만들지 않는다.
- 묶음이 열린 동안 `can_undo`·`can_redo`는 거짓이고 `undo_label`·`redo_label`은 빈 글이며 `undo`·`redo`·`set_clean`은 아무 일도 하지 않는다. 묶음에 담긴 메시지는 앱이 이미 적용했으므로 그때 `clean()`도 거짓이다. `clear`만 열린 묶음까지 버린다. 확정은 redo를 비우고 합치기를 끊는다. 빈 묶음은 redo도 비우지 않는다.

**용량과 저장 지점.** 칸 기록의 규칙(`text_edit_history_limit`, `text_edit_history_byte_limit`)을 따른다.

- 쌓을 때마다 걸음 수가 `steps`를, `weight` 합이 `bytes`를 넘으면 가장 오래된 걸음부터 버린다. 가장 새 걸음은 상한보다 커도 남겨 한 번은 되돌린다. redo 쪽은 줄이지 않는다. 쌓는 순간 비워지기 때문이다. `weight`를 알리지 않은 걸음은 0으로 센다. `bytes()`는 되돌릴 쪽의 `weight` 합이고 `bytes` 상한이 견주는 값이다. `set_limits`로 상한을 줄이면 되돌릴 쪽만 줄고, `redo`로 돌아온 걸음도 되돌릴 쪽 상한에 맞춰 가장 오래된 걸음부터 다시 버린다.
- 저장 지점은 `set_clean`이 찍은 자리(되돌릴 수 있는 걸음 수)다. 처음과 `clear` 뒤는 저장 지점이다. `clean()`은 지금 자리가 저장 지점이면 참이라 앱이 "수정됨" 표시를 이것으로 낸다.
- 저장 지점이 버려진 redo 쪽에 있었거나 맨 아래 걸음이 상한으로 버려져 걸음 밖으로 나가면 저장 지점은 사라진다. `clean()`은 `set_clean`을 다시 부를 때까지 거짓이다.

**밖에서 상태가 바뀔 때.** 파일을 다시 열거나 자동 갱신이 상태를 바꾸면 걸음의 메시지가 가리킬 대상이 사라진다. 앱은 그때 `clear()`를 부른다. `text_edit_set_text`가 글을 밖에서 바꿀 때 칸 기록을 비우는 것과 같은 규칙이다.

**키 경로와 칸의 undo.**

- **키는 칸이 먼저다.** `text_target_of`가 대상을 주는 kind에 초점이 있으면 Ctrl+Z·Ctrl+Shift+Z·Ctrl+Y는 칸의 `text_edit_command::undo`·`redo`가 된다. 컨트롤러는 칸의 기록을 보지 않으므로 칸을 다 되돌린 뒤 한 번 더 눌러도 앱 stack으로 넘어가지 않는다.
- **읽기 전용 칸은 지금 삼킨다.** 읽기 전용 칸은 글을 바꾸는 키로 Ctrl+Z·Ctrl+Y를 삼킨다(`process_text_input_key`의 `read_only` 갈래, [text_area_element_tests.cpp](../tests/text_area_element_tests.cpp) 641행이 잠근다). 되돌릴 기록이 없는 칸인데도 앱 단축키가 닿지 않는다. 데모 작업대의 차이 보기가 그 칸이다. 줄 3이 R2에서 고친다(읽기 전용 칸은 Ctrl+Z·Ctrl+Y를 앱 `on_key`로 넘긴다). 이 노드는 controller를 고치지 않으며, 앱은 단추나 메뉴로 같은 요청을 함께 낸다. 단축키가 없는 모바일도 같은 단추가 요청을 낸다.
- **열린 메뉴 뒤로는 가지 않는다.** 메뉴(팔레트 포함)가 열려 있으면 메뉴가 갖지 않은 키는 초점 칸까지만 가고 앱 `on_key`에 닿지 않는다. 앱이 활성 범위를 `modal`로 `match_command`에 넘기면 window 범위의 실행 취소는 맞지 않는다(`modal`은 window로 물러서지 않는다). 열린 대화 상자 뒤의 문서가 되돌아가지 않는다.
- **메뉴·팔레트의 실행 취소는 초점과 상관없이 stack의 것이다.** 클릭은 키 경로를 타지 않는다. 초점 칸의 기록을 메뉴로 되돌리는 길은 만들지 않는다(결정 3의 길 c).
- 칸 편집을 걸음으로 올릴지는 앱이 고른다. 확정 시점(blur, Enter)에 `restore_text_intent { before }`와 `{ after }`를 쌓을 수 있다. 되돌림이 칸의 글을 `text_edit_set_text`로 갈면 칸 기록은 비워진다.

**스레드와 명령표.**

- 단축키 판정(`on_key`)은 input thread의 정책이 하고, stack은 logic thread의 것이라 정책이 stack 상태를 읽을 수 없다([threading-model.md](concepts/threading-model.md)). 그래서 판정용 표는 정적이다. `make_undo_commands({})`는 두 명령을 모두 켠다. `match_command`가 꺼진 명령(`enabled == false`)을 건너뛰기 때문이다. 빈 stack에서 Ctrl+Z가 맞으면 앱의 handler가 거짓을 받고 끝난다.
- 메뉴·팔레트에 보이는 표만 logic thread가 stack 상태로 짓는다. 꺼진 명령은 꺼진 항목이고 `menu_item_from`이 실제 단축키를 단축키 열에 보인다. Ctrl+Z와 Ctrl+Shift+Z는 `match_command`가 수정자를 정확히 견주므로(`meta == false` 포함) 서로 맞지 않는다.

배율·테마·고대비·움직임 줄이기는 element와 그리기가 없어 닿지 않는다.

## 결정

**1. 걸음의 표현.** 질문: 걸음이 되돌림과 다시 실행을 어떤 모양으로 드는가.

- 길 a: `app_message` 쌍. 되는 것: stack이 값이라 복사와 시험이 싸고 앱 상태를 가리키는 참조를 쥐지 않는다. 되돌림이 앱 `handle`의 길을 타서 로직이 두 벌이 되지 않는다. 상태 스냅샷은 이 길의 한 쓰임이다. 대가: 되돌림마다 메시지 타입을 앱이 정의하고, 역 메시지가 맞는지 라이브러리가 검증하지 못한다.
- 길 b: `std::function` do/undo 쌍(`QUndoCommand`식). 되는 것: 메시지 타입 없이 람다로 쓴다. 대가: 람다가 앱 상태를 가리키는 참조를 쥔다. 문서를 닫거나 컨테이너가 다시 잡히면 허공을 가리키고, 걸음을 시험이나 기록으로 들여다볼 수 없다. 게시된 액션의 규칙과 부딪히지는 않는다. 함수는 logic thread에서만 불린다.
- 길 c: 상태 스냅샷 전용 `snapshot_undo_stack<state>`. 되는 것: 상태가 복사 싼 값이면 역 동작을 적을 일이 없다. 대가: 걸음마다 상태 전체를 쥐고(칸이 32MiB 상한을 둔 까닭), 일부만 바꾸는 걸음에 맞지 않으며, 템플릿이라 .cpp에 둘 수 없다.
- 권장: a. c는 a 위에서 앱이 `restore_intent`로 쓸 수 있어 따로 만들지 않는다. 대가: 위의 타입 정의와 검증 불가다. 승인: 길 a (2026-11-02).

**2. 되돌릴 메시지를 앱에 건네는 길.** 질문: stack이 메시지를 어떻게 돌려주는가.

- 길 a: `undo(apply)`가 걸음 안 메시지마다 `apply`를 부른다(저장하지 않는다). 되는 것: 부르는 동안 stack이 재생 중임을 알아 재진입을 막는다. 대가: 콜백 한 겹이 낀다.
- 길 b: 메시지 목록을 돌려준다. 되는 것: 호출이 값이라 시험이 쉽다. 대가: 앱이 재생하는 동안 stack은 모르므로, 재생 메시지가 기록하는 `handle`을 타면 redo가 지워지고 걸음이 쌓이는 오류를 막을 수 없다.
- 길 c: stack이 앱 inbox에 직접 넣는다. 되는 것: 앱 코드가 줄어든다. 대가: stack이 `app_host`를 알게 되어 순수 값이 아니고, 재생이 비동기라 위치와 앱 상태가 어긋난다.
- 권장: a. 대가: 위의 콜백이다. 권장대로 진행.

**3. 칸 undo와의 관계.** 질문: 초점이 텍스트 칸일 때 Ctrl+Z는 누구의 것인가.

- 길 a: 분리 유지. 칸이 먼저(지금 그대로)이고 칸 밖이면 앱 stack이다. 되는 것: 키 경로를 건드리지 않고 칸의 기록은 칸이 든다. 대가: 칸 안의 편집이 앱 stack에 보이지 않는다. 앱이 확정 시점에 걸음으로 올릴지 고른다.
- 길 b: 칸 편집 확정을 라이브러리가 걸음으로 올려 통합. 되는 것: 키 하나에 stack 하나다. 대가: controller의 키 경로를 고쳐야 하고(줄 3의 파일), 같은 Ctrl+Z에 칸과 앱이 이중으로 반응하는 실수가 나기 쉬우며, IME `replace_all` 확정과 칸 기록의 정합이 걸린다.
- 길 c: 칸의 되돌릴 수 있음을 snapshot에 노출해 메뉴가 초점 칸을 따르게 한다. 되는 것: 편집 메뉴가 초점 칸의 기록을 되돌린다. 대가: `text_input_snapshot`과 controller, 플랫폼 칸의 공개 모양이 바뀐다. 이 노드에서 하지 않는다.
- 권장: a. 대가: 위의 보이지 않음이다. 권장대로 진행.

**4. 합치기와 묶음.** 질문: 연속 동작을 어떻게 한 걸음으로 만드는가.

- 길 a: 병합 키 비교와 중첩되는 begin/end. 되는 것: stack이 메시지를 읽지 않고 합친다(`text_edit_group`과 같은 사상). 앱이 의도를 키로 말한다. 대가: 같은 키는 절대 값이라는 약속이 앱에 있고, 어기면 중간 상태를 잃는다. 시계가 없어 끌기가 끝나면 앱이 끊는다.
- 길 b: 앱이 합치는 함수를 준다(`mergeWith`식). 되는 것: 임의의 합치기다. 대가: 메시지를 여는 함수를 stack이 쥐고(결정 1의 길 b와 같은 수명 문제), 매 push마다 불린다.
- 길 c: 시간 창으로 합친다. 되는 것: 앱이 키를 정하지 않는다. 대가: stack이 시계를 쥐고, test가 시각을 줘야 하며, 사용자의 속도에 합쳐짐이 달린다.
- 권장: a와 중첩 허용. 묶음을 중첩하지 못하면 한 동작이 다른 동작의 helper를 부르는 앱에서 깨진다. 대가: 위의 약속이다. 권장대로 진행.

**5. 용량과 저장 지점.** 질문: 무엇으로 상한을 두고 저장한 시점을 누가 아는가.

- 길 a: 걸음 수 상한만, 저장 지점은 앱 몫. 되는 것: 가장 단순하다. 대가: 큰 payload를 쥔 걸음이 상한까지 쌓이면 메모리가 크게 는다(칸 기록이 바이트 상한을 둔 까닭이다). 앱은 합침과 버림을 몰라 저장 지점을 셀 수 없다.
- 길 b: 걸음 수와 앱이 알리는 `weight` 합, 저장 지점을 stack이 든다. 되는 것: 작은 걸음은 많이, 큰 걸음은 적게 남는다. "수정됨" 표시가 합침과 버림을 따른다. 대가: `weight`는 어림이고 앱이 알려야 한다(payload를 잴 수 없다).
- 길 c: 바이트 상한만. 되는 것: 메모리가 곧 상한이다. 대가: `weight`를 빠뜨린 앱은 제한이 없고 작은 걸음이 수십만 개 쌓일 수 있다.
- 권장: b. 기본은 걸음 1000개·32MiB다. 칸(100개)보다 크게 잡은 것은 문서 단위로 더 깊이 되돌리기 때문이다. 대가: 위의 어림이다. 권장대로 진행.

**6. 명령표 연결.** 질문: 실행 취소·다시 실행 명령을 라이브러리가 지어 주는가.

- 길 a: 새 헤더 `undo_commands.h`의 `make_undo_commands`. 글은 앱이 최종 문자열로 준다(기본 문구 없음). 되는 것: id·단축키 묶음·범위가 한곳이고 판정용과 메뉴용을 같은 함수로 짓는다. 어순과 구분자가 코드에 박히지 않는다. `undo_stack.h`가 `menu_element.h`를 끌고 오지 않는다. 대가: 헤더가 하나 늘고, "실행 취소: 체크"를 잇는 한 줄을 앱이 쓴다.
- 길 b: 같은 helper를 `commands.h`·`commands.cpp`에 덧붙인다. 되는 것: 헤더가 늘지 않는다. 대가: R3 hotkey가 `key_chord`·`shortcut_label`·`match_command`를 같은 파일에서 고친다.
- 길 c: helper 없이 앱이 짓는다. 되는 것: 표면이 작다. 대가: 앱마다 id와 추가 단축키를 다시 적어 칸의 단축키와 어긋날 수 있다.
- 권장: a. 글 조립을 라이브러리가 하면(G6 정책 a의 한국어 기본값) 어순과 구분자가 박혀 분할 버튼의 " 옵션"과 같은 부채가 되므로, 기본 문구를 두지 않는 쪽을 먼저 택했다. 대가: 위의 한 줄이다. R5 i18n의 자리 값이 서면 그 줄이 "{동작}: {이름}" 조회로 바뀐다. 권장대로 진행.

**7. 위치와 범위.** 질문: 어디에 두고 어디까지 하는가.

- 길 a: `include/luil/ui/`(`item_model.h`·`selection_model.h`·`spring.h`의 선례), logic thread 전용, 문서·탭별 stack 묶음(`QUndoGroup`)은 제외. 되는 것: `app_message.h`와 같은 층이라 의존이 한 방향이다. 앱이 stack을 여럿 들고 활성 하나의 상태로 `make_undo_commands`를 부르면 묶음이 하는 일이 끝난다. 대가: 활성 전환을 앱이 짠다.
- 길 b: `include/luil/app/`. 되는 것: 앱이 쓰는 도구와 한곳이다. 대가: `app/`은 host와 플랫폼 서비스 층이라 순수 상태 도우미가 서면 층의 뜻이 흐려진다.
- 길 c: 길 a에 `undo_group`을 더한다. 되는 것: 활성 stack 전환을 라이브러리가 한다. 대가: 첫 소비자가 없고(데모도 stack이 하나) 모양을 추측으로 정한다.
- 권장: a. 대가: 위의 활성 전환이다. 권장대로 진행.

**8. 상한이 세는 쪽.** 질문: `bytes()`와 `steps`·`bytes` 상한은 어느 쪽 걸음을 세는가. 구현에서 정했다.

- 길 a: 되돌릴 쪽만. 되는 것: 상한이 견주는 값과 `bytes()`가 같고, 쌓는 순간 redo가 비어 규칙이 한 줄이다. 대가: 방금 되돌린 걸음이 쥔 redo 쪽 메모리는 상한 밖이다. 그 양은 되돌릴 쪽이 이미 지킨 상한을 넘지 않는다.
- 길 b: 두 쪽의 합. 되는 것: 메모리 총량이 상한이다. 대가: `undo`·`redo`가 합을 바꾸지 못해 저장 지점 계산이 두 쪽에 걸치고, 되돌린 직후 상한을 다시 따지게 된다.
- 권장: a. `set_limits`로 줄일 때와 `redo`로 걸음이 돌아올 때도 되돌릴 쪽만 상한에 맞춘다. 대가: 위의 redo 쪽이다. 권장대로 진행.

**9. 열린 묶음 중의 질의.** 질문: 묶음이 열려 있을 때 `undo_label`·`redo_label`·`clean()`은 무엇을 답하는가. 구현에서 정했다.

- 길 a: 라벨은 빈 글이고, 묶음에 메시지가 담겼으면 `clean()`은 거짓이다. 되는 것: `can_undo`·`can_redo`가 거짓인 때와 어긋나지 않고, 앱이 이미 적용한 상태를 저장한 자리로 오인하지 않는다. 대가: 빈 묶음 위에서는 `clean()`이 그대로여서 묶음 안에 담긴 메시지 수에 따라 답이 갈린다.
- 길 b: 확정한 걸음만 따른다. 되는 것: 질의가 묶음과 무관하다. 대가: 적용된 상태가 저장 지점과 다른데 "수정됨" 표시가 꺼진다.
- 권장: a. 대가: 위의 갈림이다. 권장대로 진행.

**10. 데모의 끌기 경계.** 질문: 열 폭 끌기 하나를 한 걸음으로 만드는 길은 무엇인가. 구현에서 정했다.

- 길 a: 표의 `resize_column_end`(열 경계를 놓음)가 낸 메시지로 `break_merge()`를 부른다. 되는 것: 병합 키는 열 이름 하나이고, 이미 있는 놓음 메시지를 쓴다. 대가: 놓음 메시지를 받는 `handle` 분기가 하나 는다.
- 길 b: 끌기 번호를 병합 키에 넣는다. 되는 것: 분기가 없다. 대가: 놓음을 몰라 다음 끌기를 앱이 따로 세야 하고, 번호 증가가 시작 메시지에 걸린다.
- 권장: a. 대가: 위의 분기다. 권장대로 진행.

## 플랫폼

**Win32.** 플랫폼 호출이 없다. `src/win32`는 고치지 않는다. Ctrl+Z는 `key_pressed_event`로 오고 명령표가 맞춘다.

**Android.** Java 소스는 0줄이고 JNI도 unsupported도 없다. 순수 C++ 값이라 `luil_core_tests`가 기기에서 그대로 돈다. 외장 키보드의 Ctrl+Z는 `android_input.cpp`가 문자 키를 단축키 수정자와 함께일 때만 키로 올려(135행) `key_z`가 된다. 하드웨어 키보드가 없으면 단축키가 없으므로 모바일의 노출(앱 바 단추, 텍스트 편집 메뉴)은 앱 몫이다. 텍스트 편집 메뉴(`src/host/text_edit_menu.cpp`)에는 되돌리기 항목이 없고 이 노드가 더하지 않는다. 칸의 기록은 칸의 것이다.

## 다른 노드와의 약속

- 앞 라운드(R1) 문서는 이 노드로 미룬 것이 없다. 이 노드는 라이브러리 기본 문구(G6)도 두지 않는다. 앱이 최종 글을 주므로 줄 1의 비공개 문구 헤더로 옮길 낱말이 없다. 기본값이 필요해지면 옮길 낱말은 "실행 취소", "다시 실행"과 구분자 ": " 셋이다.
- **빈 글의 후속.** `make_undo_commands`는 `undo_label`·`redo_label`이 비면 `app_command::label`을 빈 채로 둔다. 통합 뒤 후속(줄 1 `default_text`가 main에 들어온 뒤)에는 비었을 때 `default_text::undo_command_undo()`(실행 취소)·`undo_command_redo()`(다시 실행)를 쓴다. 걸음 이름을 잇는 문장은 앱 몫이고 R5 자리 값에서 바뀐다. 판정용 표(`make_undo_commands({})`)는 id와 단축키만 쓰므로 영향이 없다.
- **style_sheet**(같은 줄의 다음): 기능 파일은 겹치지 않는다. 목록 파일 셋에 줄을 더하는 것만 겹친다. `src/CMakeLists.txt`(이 노드는 `ui/foundation` 필터의 소스 줄, style_sheet는 core 링크와 파서 소스), `tests/CMakeLists.txt`, `include/luil/luil.h`다. 모두 줄을 더하는 병합이라 기계적이다.
- **ink**(줄 3, 같은 라운드): 획 되돌리기를 이 stack에 올릴 수 있다. 서로 기대지 않으며 ink 설계가 절대 값 규칙과 합치기 키를 따른다. 이 노드는 `ui_events.h`·`ui_interaction.cpp`를 고치지 않는다.
- **rich_edit·hotkey·menu_bar**(R3): 칸의 기록은 칸의 것이라(결정 3) 서식 되돌리기를 `text_edit_state`에 넣어도 이 stack과 따로 간다. hotkey가 `key_chord`를 고치므로 `make_undo_commands`는 지정 초기화(`.key`, `.control`, `.shift`)로 짓는다. menu_bar의 편집 메뉴는 그 결과를 `menu_item_from`으로 짓고 글은 앱이 준다.
- **데모는 따로 커밋한다.** 라이브러리 커밋(헤더, 구현, test, 문서)을 먼저 내고 데모 연결 커밋은 통합 때 직렬로 쌓는다. 고치는 데모 파일은 `examples/demo`의 `workbench_page.*`·`palette.*`·`demo_main.cpp`와 `examples/mobile_demo/lists_page.*`다. `demo_main.cpp`는 여러 줄이 고치는 파일이라 `palette_run_intent` 갈래와 `make_frame`의 팔레트 popup 호출 한 줄만 바꾼다. 단축키는 `on_key`가 `palette_commands()`로 이미 맞추므로 `on_key`는 그대로다. `tests/CMakeLists.txt`에는 라이브러리 커밋의 `undo_stack_tests.cpp` 줄 말고도 연결 커밋이 모바일 데모 목록에 `lists_page.cpp` 한 줄을 더한다(확인 절).
- `tests/key_propagation_tests.cpp` 끝에 TEST_CASE 둘을 더한다. 같은 파일을 더하는 줄 3의 노드와는 끝에 덧붙이는 병합이다.
- 문서는 `docs/components.md`에 표 한 줄, `concepts/app-message.md`에 되돌림 메시지 한 절, `concepts/text-editing.md`에 칸과 앱 stack의 경계 한 문단을 더한다. 색인(`docs/README.md`)은 통합 때 붙인다.

## 확인

자동 test.

- `tests/undo_stack_tests.cpp`(core, 새 파일):
  - 왕복: `push` → `undo` → `redo`가 메시지를 걸음 차례로 `apply`에 넘기고 `can_*`·라벨·`undo_count`·`redo_count`가 맞는지. 빈 stack과 끝에서의 `undo`·`redo`, 빈 `apply`, 빈 메시지의 `push`가 거짓이고 무변경인지. `push`가 redo를 비우는지;
  - 재생 중 재진입: `apply` 안의 `push`·`begin`·`undo`·`clear`가 거짓이거나 무시되어 걸음과 redo가 그대로인지. 재생이 끝나면 다시 쌓이는지;
  - 합치기: 같은 키의 연속 `push`가 앞 `undo`와 뒤 `redo`·이름의 한 걸음인지. 다른 키·빈 키·`undo`·`redo`·`break_merge`·`set_clean` 뒤, 저장 지점 위, 묶음 안에서는 합치지 않는지;
  - 묶음: `undo`는 반대 차례, `redo`는 쌓은 차례인지. 중첩은 바깥 `end`에서 확정되고 이름이 바깥 것인지. 빈 묶음은 걸음이 없는지. 열린 동안 `can_undo`와 짝 없는 `end`가 거짓인지. `undo_macro`의 소멸이 닫고 `clear`가 열린 묶음을 버리는지;
  - 용량: 걸음 수와 `weight` 합 상한이 가장 오래된 것부터 버리는지. 가장 새 걸음은 상한보다 커도 남는지. 0은 제한 없음인지. `set_limits` 축소가 redo를 줄이지 않는지;
  - 저장 지점: 처음 참, `push` 뒤 거짓, `undo`로 돌아오면 참인지. redo 쪽 저장 지점이 `push`로 지워지거나 맨 아래 걸음이 버려져 걸음 밖으로 나가면 `set_clean` 전까지 거짓인지. `clear` 뒤 참인지;
  - 명령 helper: id·단축키 묶음(Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z)·범위. `match_command`가 세 키에 id를 돌려주고 Ctrl+Z와 Ctrl+Shift+Z가 서로 맞지 않는지. `can_undo`가 거짓이면 맞지 않는지. `find_conflicts`에 새 충돌이 없는지.
- `tests/key_propagation_tests.cpp`(끝에 둘): 텍스트 칸 초점의 Ctrl+Z가 `text_edit_command::undo` 하나를 내고 정책의 `on_key`를 부르지 않는지. 단추 초점의 Ctrl+Z는 정책의 `on_key`가 받는지. 읽기 전용 칸의 삼킴은 `text_area_element_tests.cpp`가 이미 잠근다.
- `tests/workbench_page_tests.cpp`: 체크·스테이지·버리기·열 폭을 되돌리고 다시 실행하면 처음 상태와 같은지, 새 동작이 redo를 비우는지, 같은 열의 연속 조절이 한 걸음인지, 커밋이 stack을 비우는지. `tests/mobile_demo_page_tests.cpp`: 재배열을 되돌리면 이전 순서인지. 이 test가 읽는 `examples/mobile_demo/lists_page.cpp`는 `luil_core_tests`의 모바일 데모 목록(`tests/CMakeLists.txt` 141~151행)에 없어서 연결 커밋이 목록에 올린다. 그 파일은 luil 헤더와 `mobile_demo/common.h`만 부르고 `common.cpp`는 목록에 이미 있어 대역이 필요하지 않다. 링크는 구현 때 확인했다. 데스크톱 `workbench_page.cpp`는 이미 등록돼 있다(155~156행).
- `luil_umbrella_header`(우산 헤더에 두 헤더), `luil_core_portability`, `luil_source_style`.

데모. 상태를 label로 그려 `luil-uia`로 되읽는다.

- 데스크톱 변경 작업대: 스테이지 바꿈(체크 칸, 메뉴의 stage·unstage, 전체 체크), 버리기, 열 폭 조절을 걸음으로 쌓는다. 같은 열의 연속 조절은 키 `resize:<열>`로 합치고, 열 경계를 놓으면 표의 `resize_column_end`가 `workbench_resize_end_intent`를 내 `break_merge()`를 부르므로 끌기 하나가 걸음 하나다. 재생 전용 메시지 `workbench_set_staged_intent`·`workbench_remove_intent`·`workbench_restore_intent`(지운 행과 자리)·`workbench_set_width_intent`는 절대 값을 담는다. 사용자 메시지는 걸음을 쌓은 뒤 같은 `apply`를 부른다.
  - 읽기 전용 차이 보기가 Ctrl+Z를 삼키므로 단추 `workbench-undo`·`workbench-redo`가 팔레트 명령과 같은 요청 메시지를 보낸다. 켜짐은 `can_undo`·`can_redo`를 따른다. 켜 둔 채 빈 동작을 내면 Invoke가 오류로 끝나기 때문이다. 요청 메시지는 `workbench_undo_intent`·`workbench_redo_intent`이고 머리 줄 아래의 한 줄(`history-bar`)에 단추 셋과 label이 선다. 툴팁과 켜짐은 `make_undo_commands(undo_state())`가 짓는다.
  - label `workbench-history`("되돌리기 2 · 다시 실행 1 · 다음: 변경 버리기 · 저장 지점 일치")가 상태를 말하고 단추 `workbench-mark-saved`가 `set_clean`을 부른다. 커밋 메시지 칸에서 Ctrl+Z는 칸의 것이고 표나 단추에 초점이 있을 때만 stack의 것이다. 커밋은 행이 저장소로 넘어가 되돌림 메시지가 가리킬 대상이 사라지므로 `clear()`를 부른다.
  - 팔레트의 정적 표(`palette_commands()`)에 `make_undo_commands({})`를 잇는다. `command_palette_host::make_popups`는 stack 상태(`undo_command_state`)를 인자로 받아 보이는 표의 글과 켜짐을 짓는다. 셸의 `palette_run_intent` 갈래가 `edit.undo`·`edit.redo`를 작업대 페이지가 보일 때만 작업대에 보낸다. 다른 페이지에서는 두 항목을 꺼 둔 채 보인다.
- 모바일 데모 목록 페이지: 재배열(`reorder_intent`)을 걸음으로 쌓고 되돌림은 순서 전체를 담은 `reorder_restore_intent`(스냅샷도 같은 길이다)다. 단추 `list-undo`·`list-redo`(손가락 높이)와 그 아래 label `list-history`("되돌리기 1 · 다시 실행 0 · 다음: 순서 바꿈")를 목록 위에 둔다. 켜짐은 `can_undo`·`can_redo`를 따른다.
- UIA(격리 데스크톱): 행을 체크하고 `workbench-undo`를 Invoke해 label이 처음으로 돌아오는지, 버리기를 되돌리면 같은 자리에 돌아오는지, 새 동작 뒤 `workbench-redo`가 꺼지는지 읽는다. 키는 PostMessage로 보내 표 초점의 Ctrl+Z는 되돌리고 커밋 메시지 칸 초점에서는 stack이 그대로인지 읽는다. 차이 보기 초점은 줄 3의 변경이 들어오기 전에는 stack이 그대로이고, 들어온 뒤에는 읽기 전용 칸이 이 키를 앱 `on_key`로 넘기므로 되돌아간다. 통합 뒤에는 기대를 바꿔 읽는다. 모바일 데모(Windows 창)는 같은 label을 읽는다. Android에는 접근성 계층이 없어 사람이 화면으로 읽는다.

사람 확인.

- 열 가장자리를 끌어 폭을 바꾼 뒤 Ctrl+Z 한 번이 끌기 전 폭으로 돌아오는 합치기의 체감.
- Android 실기기에서 재배열을 되돌리는 단추의 탭 크기와 읽기. 외장 키보드 Ctrl+Z(선택).

## 범위 밖

- 읽기 전용 칸이 Ctrl+Z·Ctrl+Y를 삼키는 것. 줄 3이 R2에서 고친다(읽기 전용 칸은 Ctrl+Z·Ctrl+Y를 앱 `on_key`로 넘긴다). controller(`ui_interaction.cpp`)는 이 노드가 고치지 않는다.
- 칸의 기록을 메뉴·단추로 부르는 길과 칸의 되돌릴 수 있음을 snapshot에 싣는 일(결정 3의 길 c).
- 문서·탭별 stack 묶음(`QUndoGroup`)과 걸음 목록 보기(`QUndoView`).
- 걸음의 직렬화와 저장, 시간 창 합치기, 걸음 사이의 상쇄, 역 메시지의 검증.
- 모바일의 되돌리기 노출(앱 바 단추, 텍스트 편집 메뉴 항목).

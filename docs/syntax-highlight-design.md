# 구문 강조 도우미

편집하는 코드 글의 구간 색을 편집에 맞춰 다시 칠한다. 앱이 줄 하나를 칠하는 렉서를 주면, 도우미가 바뀐 줄부터 렉서를 다시 돌려 `text_area_element`가 그리는 스타일 구간(`text_span`)을 만든다. 공개 형식은 [syntax_highlighter.h](../include/luil/ui/syntax_highlighter.h)의 `syntax_highlighter`, [text_change.h](../include/luil/text/text_change.h)의 `text_change_between`, 그리고 [text_document.h](../include/luil/ui/text_document.h)에 더하는 구문 색 역할과 `text_span::italic`이다. 언어별 렉서는 넣지 않는다. 앱은 구간만 준다는 [문서 보기 2단계](ui-gap-codex-track.md)(X5)의 원칙이 그대로다.

## 문제

- 구간 만들기가 앱 몫이고 증분이 없다. 문서 칸은 앱이 만든 구간을 받을 뿐이고(`text_area_config::spans`), 예제의 diff 칸은 글 전체를 훑어 구간을 짓는다(`decorate_diff`). 읽기 전용 글은 한 번이면 된다. 편집하는 글은 키마다 전체를 훑게 된다.
- 편집이 어디를 바꿨는지 알 길이 없다. `apply_text_edit`는 변경 구간을 남기지 않는다. IME 확정(`replace_all`), 되돌리기(`text_edit_undo`), `text_edit_set_text`는 글 전체를 통째로 바꾼다([text_edit.cpp](../src/text/text_edit.cpp)). 증분으로 고치려면 이전 글과 직접 견주어야 한다.
- 색 역할이 다섯뿐이다(`primary`·`secondary`·`accent`·`warning`·`error`). 키워드와 문자열을 `warning`·`error`로 가르면 오류 표시처럼 읽힌다. 구간 색은 팔레트 역할로만 받으므로 앱이 임의 색을 쓸 길도 없다.
- 구간에 기울임을 걸 수 없다. 배치의 `text_layout_style`은 `italic`을 갖고 배치 cache key도 해시하지만, `text_span`에 칸이 없어 `metric_styles`가 `bold`만 넘긴다.

## 공개 형식

### 구간과 색 ([text_document.h](../include/luil/ui/text_document.h))

```cpp
enum class text_color_role {
    primary, secondary, accent, warning, error,
    // 구문 색이다. 팔레트의 `syntax_*` 역할로 칠한다.
    keyword, string, comment, number, type, function
};

struct text_span
{
    // begin, end, color, bold, background는 지금과 같다.
    // `background` 뒤에 둔다. `{ begin, end, color, bold }` 같은 순서 초기화가 그대로 읽힌다.
    bool italic { false };
};
```

### 도우미 ([syntax_highlighter.h](../include/luil/ui/syntax_highlighter.h))

```cpp
// 줄 하나를 칠한다. 줄 바꿈은 `line`에 들지 않는다.
//  - `state`는 윗줄이 끝난 뒤의 상태이고, 첫 줄은 `initial_state`다.
//  - 구간은 줄 처음 기준 offset으로 `spans`에 더한다.
//  - 돌려주는 값은 이 줄이 끝난 뒤의 상태다.
// (줄 글, 입력 상태)의 순수 함수여야 한다. 줄 번호나 밖의 값을 읽지 않는다.
using syntax_line_lexer = std::function<int(std::u8string_view line, int state, std::vector<text_span>& spans)>;

inline constexpr std::chrono::microseconds syntax_unlimited_budget { std::chrono::microseconds::max() };

struct syntax_highlighter_config
{
    syntax_line_lexer lexer {};
    int initial_state { 0 };
    // 이보다 긴 줄은 렉서에 보내지 않고, 구간 없이 입력 상태를 그대로 다음 줄에 넘긴다.
    std::size_t max_line_size { 20'000 };
};

struct syntax_highlight_stats
{
    std::size_t lines { 0 };
    std::size_t spans { 0 };
    // 마지막 `update`·`advance`가 렉서에 보낸 줄 수다 (test·진단용).
    std::size_t relexed { 0 };
};

class syntax_highlighter
{
public:
    explicit syntax_highlighter(syntax_highlighter_config config);
    // 글이 바뀐 뒤 부른다. 지난 호출의 글과 견주어 바뀐 줄부터 다시 칠한다.
    // 편집이 닿은 줄은 늘 칠한다. 그 뒤(꼬리)는 `budget` 안에서만 칠하고 남기면 `pending()`이 참이다.
    void update(std::u8string_view text, std::chrono::microseconds budget = syntax_unlimited_budget);
    void advance(std::chrono::microseconds budget);
    [[nodiscard]] bool pending() const noexcept;
    // 다른 문서를 열 때 앞 문서의 줄 기록을 버린다.
    void reset();
    // 지금까지 칠한 구간이다. 앞 호출 뒤 바뀐 것이 없으면 같은 포인터를 돌려준다.
    [[nodiscard]] std::shared_ptr<const std::vector<text_span>> spans();
    [[nodiscard]] const syntax_highlight_stats& stats() const noexcept;
};
```

### 변경 비교 ([text_change.h](../include/luil/text/text_change.h))

```cpp
namespace luil::text {
    // `before`의 [offset, offset + removed)를 `after`의 [offset, offset + inserted)가 대신한다.
    struct text_change
    {
        std::size_t offset { 0 };
        std::size_t removed { 0 };
        std::size_t inserted { 0 };
        [[nodiscard]] constexpr bool empty() const noexcept { return removed == 0 && inserted == 0; }
    };

    [[nodiscard]] text_change text_change_between(std::u8string_view before, std::u8string_view after) noexcept;
}
```

### 팔레트

여섯 역할은 `neutral_color_palette`의 중립 색이다. 키 컬러와 무관하고, `warning_accent`·`error_accent`처럼 `compose_palette`가 그대로 옮긴다. 값은 초안이다.

| 역할 | JSON 키 | 팔레트 칸 | 다크 | 라이트 |
| --- | --- | --- | --- | --- |
| `keyword` | `syntaxKeyword` | `syntax_keyword` | `#6cb2f2` | `#0000ff` |
| `string` | `syntaxString` | `syntax_string` | `#dba184` | `#a31515` |
| `comment` | `syntaxComment` | `syntax_comment` | `#8dbb78` | `#006b00` |
| `number` | `syntaxNumber` | `syntax_number` | `#b5cea8` | `#0a6b4a` |
| `type` | `syntaxType` | `syntax_type` | `#4ec9b0` | `#1d6c84` |
| `function` | `syntaxFunction` | `syntax_function` | `#dcdcaa` | `#6e5320` |

- 여섯 키는 모두 `neutral_optional_fields`의 행이다(`key|field|fallback`). 빠진 키는 같은 테마의 `primaryForeground`로 채운다. 앱이 LUIL_STYLE로 준 옛 JSON은 구문 색 없이 본문색으로 그린다.
- 초안 값을 창 바탕, 표면 바탕, 그 위에 `input_background`를 합성한 바탕에 대 보면 모두 약 4.8:1 이상이다. 마지막이 입력 칸이 실제로 칠하는 색이다. 최종 값은 구현에서 대비 test로 고정한다.

구현은 [syntax_highlighter.cpp](../src/ui/syntax_highlighter.cpp)와 [text_change.cpp](../src/text/text_change.cpp)에 둔다. 둘 다 Skia와 플랫폼을 모르는 core다.

## 규칙

**줄과 상태**
- 줄은 LF, CRLF, 홀로 선 CR로 끝난다. `text_area_element`의 native 배치가 줄을 나누는 규칙과 같다. 줄 끝 바이트는 줄 글에 들지 않고 구간도 덮지 않는다. 글이 줄 바꿈으로 끝나면 마지막에 빈 줄이 하나 있다.
- 상태는 `int`이고 같은지만 본다. 줄 밖 문맥(여러 줄 주석, 여러 줄 문자열)은 상태 하나에 담긴다. 첫 줄만 다른 문법(셰뱅)은 `initial_state`로 가른다.
- 순수 함수라는 약속이 수렴의 근거다. 글이 같고 입력 상태가 같으면 그 줄의 결과는 지난번과 같다고 보고 다시 부르지 않는다.

**렉서 출력**
- 도우미가 다듬는다. 줄 글 밖과 빈 구간은 버리고, begin 순으로 정렬하고, 겹친 뒤쪽을 자른다. 양끝은 글자소 경계로 맞추고(begin은 앞으로, end는 뒤로) 겹친 몫은 앞 구간이 갖는다. ASCII뿐인 줄은 글자소 계산을 건너뛴다.
- 같은 모양의 이웃 구간을 줄 바꿈 너머로 합치지 않는다. 줄마다 독립이라 증분이 줄 단위로 끝난다.

**증분**
1. `text_change_between(지난 글, 새 글)`로 바뀐 구간을 얻는다. 비어 있으면 아무 일도 없다.
2. 바뀐 구간이 시작하는 줄부터, 새 글에서 바뀐 구간의 끝이 든 줄까지 렉서에 보낸다.
3. 그 뒤 줄은 앞 줄이 끝난 상태가 지난번 그 줄을 칠할 때와 같아지는 줄에서 멈춘다. 한 줄 안의 편집은 보통 그 줄 하나로 끝난다. 여러 줄 주석을 열고 닫는 편집은 상태가 되돌아오는 줄까지 간다.
4. 멈춘 뒤의 구간은 다시 칠하지 않고 `inserted - removed`만큼 옮겨 쓴다.

도우미는 지난 글을 한 벌 쥐고 편집마다 splice한다. 줄마다 길이·끝 상태·구간 수를 쥐고, 구간은 줄 순서의 평면 vector 하나에 문서 offset으로 쥔다. 메모리는 글 크기에 줄당 16바이트, 구간당 32바이트쯤이다.

**게시**
- 새 vector는 `spans()`가 부를 때 짓는다. 한 turn에 `update`가 여러 번이어도 한 번만 쓴다.
- 게시한 vector는 고치지 않는다(`spans`의 계약). 편집 뒤 모든 구간이 `delta`만큼 움직이므로, 구간 수에 비례한 쓰기가 편집마다 든다. 50만 구간이면 16MB다.
- 도우미는 thread-safe가 아니다. 앱 logic의 상태이고, 돌려준 vector는 어느 thread에서나 읽는다.

**꼬리 예산**
- `update`의 `budget`을 주면 편집이 닿은 줄까지는 늘 칠하고, 꼬리와 첫 불러오기는 예산 안에서만 칠한다. 남은 일은 `pending()`이 알리고 `advance`가 이어 받는다.
- 아직 칠하지 않은 줄의 구간은 지난 색이 남는다. offset은 늘 현재 글에 맞고 낡는 것은 색의 뜻뿐이다. 처음 열 때는 남은 색이 없어 맨 위부터 차례로 색이 든다.
- 예산을 주지 않으면 늘 수렴까지 칠한다. 기본이 이것이다.

**편집과 칸의 순서**
- 앱은 `apply_text_edit` 다음에 `update`를 같은 logic turn에서 부르고, 트리를 짓는 쪽이 `spans()`를 `text_area_config::spans`에 넘긴다. `update`가 늦으면 칸이 한 frame 낡은 offset으로 그린다. 칸은 구간을 글 끝으로 자르고 코드포인트 처음으로 당겨 깨지지는 않지만, 엉뚱한 글자에 색이 얹힌다.
- 조합 중에는 확정 글이 바뀌지 않아 도우미가 돌지 않는다. 표시 문서로 옮겨 그리는 일은 `composition_shift`가 이미 한다. 조합 자리를 품은 구간은 조합 글까지 늘어나 조합 글이 둘레 색을 따른다.
- 확정 때 `replace_all`이 글 전체를 건네도 비교가 실제 바뀐 구간만 찾는다. TSF와 Android 확정, UIA의 쓰기, 되돌리기와 `text_edit_set_text`가 모두 같은 길이다.

**칸의 그리기**
- `span_foreground`가 여섯 역할을 팔레트 칸으로 잇는다. 시험이 부를 수 있도록 비공개 `src/ui/text_span_color.h`로 옮긴다. 동작은 그대로다.
- 배치가 있는 길은 `metric_styles`가 `bold`와 `italic`을 `text_layout_style`로 넘기고, `fonts_for`가 진짜 Italic face를 고른다(없으면 눕힌다). 이웃 항목은 두 값이 모두 같을 때만 합친다.
- 배치가 없는 길(주입 측정기, shaping 끔)은 굵기의 덧칠처럼 폭을 바꾸지 않는 눕힘(`setSkewX`, `synthetic_skew`)만 남긴다. caret 자리를 보통 글꼴로 재기 때문이다.
- `text_area_wrap_cache`는 문단을 글과 `text_layout_style` 전체로 찾으므로 기울임 구간도 따로 쥔다. 캐시 코드는 바꾸지 않는다. 색 구간은 지금처럼 캐시를 무효로 하지 않는다.

**고대비·접근성·배율·움직임**
- 고대비는 여섯 역할을 모두 시스템 창 글자색(`high_contrast_colors::window_foreground`)으로 접는다. `warning_accent`·`error_accent`와 같다. 색으로 갈리던 토큰은 하나로 모이므로, 렉서는 의미를 색에만 싣지 않는다. 예제 렉서는 키워드를 굵게, 주석을 기울여 낸다.
- UIA가 싣는 글 속성은 읽기 전용 표시뿐이다. 구간과 색은 보조 기술에 가지 않고, 이번에도 싣지 않는다. 읽는 정보는 글에 이미 다 있다.
- 구간은 byte offset과 팔레트 역할이라 배율·테마와 무관하다. 테마가 바뀌면 색만 팔레트에서 다시 읽고 도우미는 돌지 않는다. 굵기·기울임 구간의 폭만 글자 크기를 따른다.
- 움직임이 없어 움직임 줄이기와 만나는 곳이 없다.

## 결정

**1. 토큰 색을 어디서 오게 하나.**
- a. 앱이 기존 다섯 역할에 매핑한다: 코드가 늘지 않는다. `warning`·`error`가 오류색이라 키워드가 경고로 읽히고, 토큰을 셋 넘게 가를 수 없다.
- b. 구문 역할 여섯을 팔레트에 더한다: 테마마다 맞춘 색, 고대비 접기, 대비 test를 얻는다. 공용 팔레트 파이프라인(구조체 둘, JSON, 생성기, 합성, 고대비)을 만진다.
- c. 앱이 임의 `ui_color`를 준다: 가장 자유롭다. X5의 임의 색 금지를 깨고 테마·고대비를 앱이 모두 맡는다.
- 권장 b. 대가: 역할이 여섯으로 고정이다. 연산자나 전처리 같은 것은 가까운 역할에 합친다. 역할을 더할 때마다 같은 여섯 군데를 다시 고친다.
- **승인 필요**

**2. 주석용 기울임을 `text_span`에 더하나.**
- a. 더하지 않는다: 필드 변경이 없다. 고대비에서 색이 모이면 주석이 코드와 같아진다.
- b. `text_span::italic`: 고대비에서도 남는 단서다. rich_edit가 같은 필드를 쓴다.
- c. 팔레트가 역할마다 모양을 정한다(`comment`는 늘 기울임): 앱이 끌 수 없다.
- 권장 b. 대가: 기울임 구간은 배치에 걸려 글 폭과 배치 cache 항목이 는다. 진짜 Italic face가 없는 글꼴은 눕힘으로 메운다.
- **권장대로 진행**

**3. 도우미 계약.**
- a. 줄 단위와 상태 정수 콜백(`QSyntaxHighlighter` 방식): 증분이 줄 단위로 닫히고 렉서가 작다.
- b. 보이는 범위 질의: 칸이 묻는 범위만 칠한다. 줄 상태 때문에 보이는 범위 앞을 어차피 칠하거나 중간 상태를 따로 쥐어야 하고, 칸이 도우미를 알게 된다.
- c. 전체 렉서(`std::vector<text_span>(std::u8string_view)`): 가장 단순하다. 증분이 없고 지금 앱이 하는 일 그대로다. 도우미 없이도 되는 길이라 남겨 둔다.
- 권장 a. 대가: 렉서가 줄 사이 문맥을 정수 하나에 담는 상태 기계여야 한다. 깊이가 정해지지 않은 중첩처럼 정수에 안 들어가는 문법은 c로 간다.
- **승인 필요**

**4. 편집 알림.**
- a. 이전·새 글의 공통 접두·접미 비교: 공개 API가 늘지 않고 어떤 편집 경로든 같은 답을 낸다. `replace_all`도 처리한다.
- b. `text_edit_state`에 변경 기록: 비교는 없다. 모든 변경 함수에 걸치고, `replace_all`·undo·`set_text`는 글 전체를 바꿔 기록이 결국 비교가 된다. 한 turn에 편집이 여럿이면 누적·초기화 규칙이 든다. `text_edit.h`를 만지는 rich_edit와 부딪힌다.
- c. 앱이 변경 구간을 알린다: 가장 싸다. 앱이 IME, UIA 쓰기, 붙여넣기, 되돌리기를 모두 알아야 하고 하나만 빠져도 낡은 색이 남는다.
- 권장 a. 대가: 비교가 바뀐 곳의 앞뒤 길이에 비례한다(최악은 글 전체를 한 번 훑는다). 도우미가 지난 글의 사본을 쥔다. 20MB에서 계측한다.
- **권장대로 진행**

**5. 출력 형태.**
- a. 편집마다 새 `shared_ptr<const std::vector<text_span>>`: 칸과 구간 계약이 그대로다. 구간 수에 비례해 쓴다.
- b. 칸이 구간 공급 함수를 받는다(줄 상대 offset을 쥐고 보이는 범위만 답한다): 복사가 없다. 칸의 설정이 둘이 되고 `metric_styles`·그리기·`text_span_bounds`가 바뀐다. 공급 함수가 logic과 UI thread에서 불려 불변 보장을 도우미가 진다.
- 권장 a. 대가: 20MB·50만 구간에서 편집마다 16MB를 쓴다. 칸이 frame마다 글 전체를 훑는 몫(2.5MB에 3ms, [여러 줄 칸의 자동 줄 넘김](text-area-wrap-design.md))은 글 크기에 비례해 20MB에서 이미 한 frame을 넘을 것이라 복사가 새 한계가 되지는 않을 것이다. 계측으로 확인하고, 느리면 b를 후속으로 더한다.
- **권장대로 진행**

**6. 20MB에서 동기 전체 재칠인가 시간 예산인가.**
- a. 동기: 단순하고 늘 맞다. 맨 위에 `/*`를 치는 편집과 첫 불러오기가 문서 전체의 렉서 시간만큼 멈춘다.
- b. 시간 예산: 편집이 닿은 줄은 동기, 꼬리와 첫 불러오기는 예산만큼. 칠하기 전까지 낡은 색이 남고, 앱이 틱마다 `advance`를 부른다.
- c. 앱의 작업 thread: 도우미를 그 thread가 소유하고 구간만 logic에 넘긴다. 도우미 코드는 그대로이고, 구간을 logic으로 넘기는 복사와 순서는 앱이 맡는다.
- 권장: `update`의 인자 하나로 a와 b를 함께 지원한다(기본은 예산 없음, 곧 a). 대가: `pending`과 낡은 색이라는 상태가 늘어 시험 표가 커진다. 계측하기 전에는 예산을 켜지 않는다.
- **권장대로 진행**

## 플랫폼

- Win32: 순수 C++이고 `src/win32`를 고치지 않는다. 고대비 접기는 `high_contrast_palette_for`가 받는 시스템 색을 그대로 쓴다. TSF 확정은 `replace_all`이라 위의 비교 길을 탄다.
- Android: 같은 C++이고 Java 소스는 0줄이다. JNI도 부르지 않는다. GameTextInput의 확정도 `replace_all`(`android_text_input.cpp`)이라 같은 길이다. unsupported인 부분은 없다.

## 다른 노드와의 약속

- **rich_edit(R3, 같은 줄)**: `text_change_between`를 그대로 다시 쓴다. 이 노드가 먼저 세우고 rich_edit는 부르기만 한다. 계약은 다섯이다. (1) `include/luil/text/`에 두어 ui와 Skia를 모른다. (2) 결과의 `offset`과 변경 끝은 두 글 모두에서 코드포인트 경계다. (3) 겹치지 않는 범위에서 접두를 먼저 가져간다(`aa`에서 `aaa`는 `{2, 0, 1}`). (4) 같은 글은 `empty()`다. (5) 비용은 접두·접미 길이에 비례한다. 글자소 경계는 계약에 없다. 필요한 호출자가 두 글에서 경계가 될 때까지 넓힌다. `text_span`의 필드는 앞으로도 끝에 더하고, `text_edit.h`는 이 노드가 건드리지 않아 두 노드가 그 파일에서 부딪히지 않는다.
- **style_sheet(R2, 같은 줄)**: 구문 여섯 키는 `neutral_optional_fields`의 연속한 여섯 행이다. 생성기 함수와 파서에는 손대지 않는다. G4가 정해져 키 표를 데이터 하나로 빼면 이 여섯 행이 그대로 옮겨 간다. 맞출 곳은 셋이다. 구조체 선언 순서(`neutral_color_palette`은 `backdrop_background` 뒤, `ui_color_palette`은 `document_search_foreground` 뒤), 표의 행 순서, `tests/style_tests.cmake`의 빠진 키 시험이다. `compose_palette`와 `high_contrast_palette_for`의 여섯 줄은 손으로 쓰는 곳이라 표에 없다. `// 구문 색` 주석으로 묶어 찾기 쉽게 둔다. 런타임 로더가 생기면 같은 행의 fallback을 읽어야 한다.
- **uax14(R1, 같은 줄)**: `text_area_element.cpp`를 같이 만진다. 이 노드는 `span_foreground`·`metric_styles`·`draw_content`의 글꼴 쪽이고, uax14는 `wrap_paragraph`와 wrap cache 쪽이라 함수가 겹치지 않는다.
- **info_bar·chart(다른 줄)**: 두 팔레트 구조체의 끝에 역할을 더하면 같은 자리다. 구문 칸을 한 덩어리로 두어 합칠 때 충돌이 한 곳이다.

## 확인

자동 test:

- `tests/text_change_tests.cpp`(`luil_core_tests`): 처음·가운데·끝의 삽입·삭제·교체, 같은 글이 `empty()`인지, `aa`에서 `aaa`처럼 접두와 접미가 겹치는 경우, `가`(EA B0 80)에서 `각`(EA B0 81)처럼 접두가 코드포인트 한가운데서 끝나는 경우의 경계, 깨진 UTF-8에서도 범위를 벗어나지 않는지, 빈 글.
- `tests/syntax_highlighter_tests.cpp`(`luil_core_tests`):
  - 무작위 편집(삽입·삭제·`replace_all`·undo·`text_edit_set_text`) 뒤에 증분 결과가 같은 글을 처음부터 칠한 결과와 구간까지 같은지;
  - 구간 불변식(begin 순서, 겹침 없음, 비어 있지 않음, 글자소 경계);
  - 한 줄 편집의 `stats().relexed`가 1인지, `/*`를 열면 뒤 줄이 바뀌고 `*/`로 닫으면 상태가 되돌아오는 줄에서 멈추는지;
  - 줄 병합·분할, CRLF·홀로 선 CR, `max_line_size`, 순서를 어기거나 겹치거나 글자소를 가르는 렉서 출력의 다듬기;
  - 예산을 0으로 준 `update`가 `pending()`이고 `advance`를 거듭하면 전체 칠과 같아지는지, 대기 중 편집이 맞게 이어지는지;
  - 구간이 그대로면 `spans()`가 같은 포인터인지.
  - 20MB·50만 줄 계측: 한 글자 편집(수렴), 상태를 뒤집는 편집, 첫 칠하기. 시간은 단언하지 않고 출력해 이 문서 아래에 기록한다.
- `tests/text_area_element_tests.cpp`(`luil_core_tests`): 기울임 구간이 있는 칸의 `lines()`·`line_rect`가 구간 없는 칸과 같은지(주입 측정기는 기울임을 재지 않는다), 구문 색 구간이 조합 글만큼 옮겨 읽히는지, 여섯 역할이 `span_foreground`로 각 팔레트 칸에 가는지.
- `tests/ui_theme_tests.cpp`(`luil_core_tests`): 여섯 역할이 두 테마에서 대비 4.5:1 이상인지, 고대비(검정·흰)에서 모두 `window_foreground`인지.
- `tests/style_tests.cmake`(CMake script 시험): 여섯 키의 생성, 빠진 키가 `primaryForeground`로 채워지는지, 잘못된 색을 막는지. `tests/ui_style_tests.cpp`(`luil_core_tests`): 내장 JSON과 생성값이 같은지.
- `tests/text_layout_native_tests.cpp`(`luil_tests`, Windows 실제 글꼴): 기울임 구간의 배치 폭이 `styles { italic }` 요청과 같은지, 기울임만 바뀐 문단만 다시 shape되는지. core raster는 글꼴이 없어 글자색을 못 본다.

데모 칸 (상태를 label로 그려 UIA로 되읽는다):

- `examples/demo/basics_page.cpp`에 "코드 칸"을 더한다. 편집 가능한 `face = code` 여러 줄 칸과 `examples/demo/code_lexer.h`의 작은 렉서(키워드, 문자열, 숫자, `//`, `/* */`)다. 렉서는 라이브러리가 아니라 데모에 둔다. label `code-status`가 "구간 N · 키워드 a · 문자열 b · 주석 c · 마지막에 다시 칠한 줄 d"를 그린다. UIA로 글을 넣고 되읽어 한 줄 편집의 d가 1인지, `/*`를 열면 주석 수가 늘고 닫으면 되돌아오는지 본다.
- `examples/mobile_demo/basics_page.cpp`에 같은 칸을 더한다. 렉서 파일은 그림 자산처럼 데스크톱 쪽 것을 그대로 쓴다.

사람 확인:

- 다크·라이트·고대비(검정·흰)에서 구문 색 대비를 눈으로 본다. 고대비에서 키워드 굵게와 주석 기울임이 갈리는지 본다.
- 큰 파일에서 타이핑 지연을 체감으로 본다.
- Windows 한글 IME로 조합하고 확정하는 동안 색이 한 frame도 어긋나지 않는지 본다.
- 실기기에서 소프트 키보드(한글 조합 포함)로 치는 동안 색 깜빡임과 낡은 구간이 없는지 본다.

## 범위 밖

- 언어별 렉서와 문법 정의 파일. 앱의 몫이다.
- 구간별 임의 색.
- 구간 공급 함수(결정 5의 b). 계측이 요구할 때 후속으로 한다.
- 도우미를 작업 thread에서 돌리는 일. 앱이 그 thread에 도우미를 두면 된다.
- 괄호 짝 강조, 줄 접기, 들여쓰기 안내선. 구문 색이 아니다.
- 보조 기술에 색·기울임 속성을 싣는 일.
- 줄 안에서 바뀐 토큰만 다시 칠하는 일. 증분은 줄 단위로 닫는다.

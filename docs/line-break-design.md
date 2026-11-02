# 줄 나눔 규칙

글을 폭에서 접는 자리를 [UAX #14](https://www.unicode.org/reports/tr14/)의 줄 나눔 규칙으로 고른다. 하이픈·슬래시 뒤, 한자·가나 사이에서 바꾸고, 닫는 부호를 줄 머리에 두지 않고, NBSP와 WJ로 붙인 글은 떼지 않는다. 한국어는 지금처럼 어절에서 바꾼다. 공개 형식은 [line_break.h](../include/luil/text/line_break.h)의 `line_break_policy`·`line_break_opportunities`와 `text_layout_request::line_break`다. label·`text_block`·markdown·여러 줄 칸이 같은 규칙으로 접는다.

## 문제

- 접기는 두 곳이었고 둘 다 ASCII 공백만 보았다. [text_layout.cpp](../src/ui/text_layout.cpp)의 `build_text_layout`은 `content[cut] == u8' '`와 `find_last_of(u8' ')`로 자리를 찾고, [text_area_element.cpp](../src/ui/text_area_element.cpp)의 `wrap_paragraph`도 같은 두 식이었다. 공백이 없으면 글자소 경계(`text::grapheme_boundaries`)에서 잘랐다.
- 그래서 `https://example.com/a/b`나 `alpha-beta-gamma`는 슬래시·하이픈이 아니라 폭이 다한 글자에서 갈렸다. 일본어·중국어 문단은 공백이 없어 늘 글자소 접기로만 바뀌고, `」`·`。`가 줄 머리에 오고 `「`가 줄 끝에 남았다. `100 km`처럼 NBSP로 붙인 글도 글자 사이에서 갈릴 수 있었다. 앱이 이것을 고칠 길이 없었다.
- 줄 끝에 매달려 폭에서 빠지는 것도 `u8' '`뿐이었다. 전각 공백(U+3000)은 줄 폭에 들어 오른쪽 정렬과 가운데 정렬이 밀렸다.
- 두 구현이 조금 달랐다. `build_text_layout`은 공백을 글자소 단위로 건너고 자른 자리를 글자소 경계로 맞췄다. `wrap_paragraph`는 `++cut`과 `cut = start + space + 1`로 byte 단위로 건너 맞추지 않았다. 공백 뒤에 결합 부호가 오면 측정기 길의 여러 줄 칸만 글자소 한가운데서 잘랐다.
- 길은 넷이다. label은 `label_element.cpp`의 `block_style`을 지나 `text_block.cpp`의 `wrap_lines`에서, markdown은 `markdown_element.cpp`에서, 여러 줄 칸의 native 길은 `layout_request`에서 `text_layout_request`를 짓고 `build_text_layout`에 닿는다. 여러 줄 칸의 측정기 길(`native_layout_`이 거짓)만 `wrap_paragraph`다.
- [텍스트 레이아웃 설계](text-layout-design.md)가 첫 단계에서 공백 우선 정책을 보존하고 UAX #14를 별도 단계로 미뤘다. 이 문서가 그 단계다.

## 공개 형식

### 규칙 ([line_break.h](../include/luil/text/line_break.h))

```cpp
namespace luil::text {
    enum class line_break_policy
    {
        // 한글(H2·H3·JL·JV·JT)을 AL로 본다. 한국어는 어절에서 바꾸고, 한자·가나는 글자 사이에서 바꾼다 (기본).
        hangul_words,
        // UAX #14 기본이다. 한글 음절 사이에서도 바꾼다.
        unicode
    };

    // `offset` 앞에서 줄을 바꿀 수 있다. 코드포인트 경계다.
    struct line_break_opportunity
    {
        std::size_t offset { 0 };
        // LB3·LB4·LB5의 반드시 바꾸는 자리다. 글 끝은 늘 참이다.
        bool mandatory { false };
    };

    // 0은 넣지 않고 글 끝은 늘 넣는다 (LB3). 빈 글은 빈 목록이다. 글자소 경계와 맞추는 일은 부르는 쪽의 몫이다.
    [[nodiscard]] std::vector<line_break_opportunity> line_break_opportunities(
        std::u8string_view text, line_break_policy policy = line_break_policy::hangul_words);
    // 접은 줄 끝에 매달려 줄 폭에서 빠지는 공백인가. 일반 분류 Zs 중 줄 나눔 분류가 GL이 아닌 것이다.
    [[nodiscard]] bool line_break_hangs(char32_t codepoint) noexcept;
}
```

### 배치와 컨트롤

```cpp
struct text_layout_request
{
    // text부터 styles까지는 지금과 같다. 끝에 더해 `{ .text = ..., .wrap = true }` 같은 지정 초기화가 그대로다.
    text::line_break_policy line_break { text::line_break_policy::hangul_words };
};

struct text_block_style { /* ... */ text::line_break_policy line_break { text::line_break_policy::hangul_words }; };
struct label_config     { /* ... */ text::line_break_policy line_break { text::line_break_policy::hangul_words }; };
struct markdown_config  { /* ... */ text::line_break_policy line_break { text::line_break_policy::hangul_words }; };
struct text_area_config { /* ... */ text::line_break_policy line_break { text::line_break_policy::hangul_words }; };
```

- `label_element`는 `block_style`에서 `text_block_style::line_break`로 넘긴다. 나머지는 자기 `text_layout_request`에 싣는다.
- config의 새 필드는 `wrap` 뒤(markdown은 끝)에 둔다. 지정 초기화는 필드를 건너뛸 수 있어 `{ .wrap = true, .max_lines = 2 }` 같은 기존 초기화가 그대로다.
- 공개 헤더는 [luil.h](../include/luil/luil.h)의 `luil/text/grapheme.h` 옆에 더한다. `verify_umbrella_header.cmake`가 본다.

### 비공개 구현

| 파일 | 하는 일 |
| --- | --- |
| `src/text/line_break.cpp` | LB2~LB31 규칙기. Skia와 플랫폼을 모르는 core다 |
| `src/text/line_break_data.h` | 생성한 분류 표. 머리말에 Unicode 판과 출처를 적는다 |
| `scripts/generate_line_break_data.py` | `generate_grapheme_data.py`와 같은 형태. UCD 디렉터리를 받고 판을 확인하고 CRLF로 쓴다 |
| `src/ui/line_fold.h`·`.cpp` | 두 접기가 함께 쓰는 자리 고르기 `choose_line_fold`와 물러서기 `previous_line_fold` (아래 "두 접기") |

생성기는 Unicode 16.0.0의 `LineBreak.txt`, `EastAsianWidth.txt`(LB19a·LB30), `emoji/emoji-data.txt`(LB30b의 Extended_Pictographic), `UnicodeData.txt`(LB15a·b의 Pi·Pf, LB1의 Mn·Mc, Zs, LB30b의 Cn)를 읽는다. 글자소 표와 같은 판이다.

- LB1은 생성 때 푼다. AI·SG·XX는 AL, CJ는 NS, SA는 Mn·Mc면 CM 아니면 AL로 적어 규칙기의 분류가 43개다. 파일에 없는 자리는 머리말이 적은 기본값(CJK 블록·Plane 2·3·U+1F000~1FAFF·U+1FC00~1FFFD는 ID, U+20A0~20CF는 PR)을 먼저 쓰고 나머지는 XX다.
- `UnicodeData.txt`가 `<..., First>`·`<..., Last>` 두 줄로 접은 범위를 펴서 Cn(미할당)을 정한다. 펴지 않으면 한자·한글 음절이 모두 미할당으로 보인다.
- 표에 없는 자리는 AL이다. 한글 음절(U+AC00~D7A3)은 코드가 H2(LV)·H3(LVT)를 계산하고, 생성기는 표의 값과 계산이 같은지 확인한다. ASCII 128칸 표를 따로 낸다.
- 보조 표는 다섯이다. East Asian(F·W·H), 미할당 Extended_Pictographic, QU 가운데 Pi, QU 가운데 Pf, 매달리는 공백(Zs이고 GL이 아닌 것).

## 규칙

**후보**
- 규칙기는 UAX #14의 기본 규칙과 LB1의 기본 해석(AI·SG·XX는 AL, SA는 Mn·Mc면 CM 아니면 AL, CJ는 NS)을 그대로 따른다. `unicode` 정책은 `LineBreakTest.txt`를 모두 통과한다(16.0.0의 16,672건).
- 위치마다 LB9로 결합 부호를 붙인 글자 하나(분류, 앞 글자의 코드포인트, 마지막이 ZWJ인지)를 두고 평가한다. 앞쪽 문맥은 걸으며 쥔다. 공백을 거슬러 건넌 글자(LB8·LB14·LB15a·LB16·LB17), 숫자 행렬 안인지(LB25), 이어진 RI 수(LB30a)다. 그래서 공백이나 숫자가 길게 이어져도 문단 길이에 비례한다. 뒤쪽은 두 글자까지 본다(LB15b·LB15c·LB25·LB28a).
- East Asian·Pi·Pf·미할당 Extended_Pictographic은 분류가 아니라 앞 글자의 코드포인트로 묻는다(LB15a·b, LB19a, LB21a, LB30, LB30b). LB28a는 분류가 AL인 점선 원(U+25CC)을 글자로 지목한다.
- `hangul_words`는 분류를 매긴 뒤 H2·H3·JL·JV·JT를 AL로 바꾼다. UAX #14는 띄어 쓰는 한국어에 LB26의 분류와 ID를 함께 AL로 바꾸는 tailoring을 든다. 이 정책은 그 가운데 한글 몫이고, 전부는 결정 2의 c다. 한글 음절 사이, 한글과 라틴·숫자 사이에 후보가 없어 그 자리는 지금 접기와 같다. `서울-부산`처럼 하이픈·슬래시 뒤는 새로 바꾼다. 한자·가나(ID)는 바꾸지 않아 글자 사이에서 바꾼다.
- 숫자 앞의 하이픈은 LB25(`HY × NU`)가 붙인다. `-5`나 `ABC-123-456`은 하이픈 뒤에서 바뀌지 않고, 칸보다 길면 비상 분할로 접힌다.
- 후보는 코드포인트 경계다. 접기는 글자소 경계에 없는 후보를 버린다. 켠 구성의 글자소 경계는 플랫폼 ICU에서 올 수 있어 표의 판이 달라도 자르는 자리는 늘 글자소 경계다.
- ASCII뿐인 문단은 128칸 표로 분류해 UTF-8 해독과 표 탐색을 건너뛴다.
- 깨진 UTF-8은 byte 하나를 U+FFFD(CB) 하나로 본다. 자리는 글의 범위를 벗어나지 않는다.

**접기**
1. 줄 처음에서 폭에 드는 가장 먼 글자소 경계(`fit`)를 찾는다. 찾는 방법은 두 접기가 지금 쓰는 그대로다.
2. `fit`에서 매달리는 공백(`line_break_hangs`)을 글자소 단위로 건넌다. 건넌 자리가 후보이거나 문단 끝이면 거기서 자른다. 지금 "넘치는 자리가 공백이면 이어진 공백을 윗줄에 매단다"와 같다.
3. 아니면 줄 처음 뒤, `fit` 이하의 마지막 후보에서 자른다.
4. 그런 후보가 없으면 `fit`에서 자른다(비상 분할). URL·긴 숫자·칸보다 긴 한국어 어절이 이 길이다. 한 글자소도 들지 않으면 한 글자소는 둔다.
5. 보이는 끝은 자른 자리에서 매달리는 공백을 걷어낸 자리다. 줄 폭과 `size()`에는 보이는 끝까지만 든다. 줄의 범위와 caret stop에는 매단 공백이 남는다. 접지 않은 마지막 줄의 공백은 지금처럼 입력 칸의 caret 자리라 폭에 남는다.
6. native 길에서 줄 경계 문맥으로 폭이 늘어 다시 줄일 때도 앞 후보로 물러선다. 후보가 없을 때만 글자소로 물러선다. 자른 자리 앞이 매단 공백이면 넘친 폭이 그 공백 몫이라 줄이지 않는다.

- 매다는 공백은 U+0020, U+3000, U+1680, U+205F, U+2000~U+200A 가운데 GL이 아닌 것 같은 너비 있는 Zs다. NBSP(U+00A0)·U+2007·U+202F는 GL이라 매달지 않는다. ZW(U+200B)는 Zs가 아니고 폭이 없어 걷어내지 않아도 같다. 탭은 지금처럼 글자로 둔다. U+3000의 줄 나눔 분류는 SP가 아니라 BA지만 Zs라 매단다.
- 매다는 것은 그 공백 하나뿐인 글자소다. 공백 뒤 결합 부호는 공백과 한 글자소이고 보이는 글자라 매달지 않는다. 두 접기가 같은 규칙이다.
- 문단은 지금처럼 CR·LF·CRLF로만 나눈다. 문단 안의 BK·NL(U+000B, U+000C, U+0085, U+2028, U+2029)은 `mandatory`로 보고하지만 접기는 바꿀 수 있는 후보로만 쓴다(결정 5).
- 접기가 필요 없는 문단(폭에 다 들거나 `wrap`이 거짓)은 후보를 구하지 않는다. 지금처럼 `full.width <= request.width`에서 끝난다. `wrap_paragraph`는 첫 자리를 자를 때 한 번 구한다.

**두 접기**
- `build_text_layout`과 `wrap_paragraph`는 `fit`을 찾는 방법이 다르고 그 뒤가 같다. 2~5단계를 `src/ui/line_fold.h`의 `choose_line_fold`로 모으고, 6단계의 물러서기는 `previous_line_fold`다. 두 곳은 후보와 글자소 경계, 줄 처음, `fit`을 넘기고 자른 자리·보이는 끝·끝이 매단 공백인지(`line_fold`)를 받는다.
- `wrap_paragraph`의 byte 단위 건너기는 이 함수로 바뀌어 글자소 경계에 맞는다.

**cache**
- `text_layout_request`의 기본 `operator==`가 새 필드를 비교한다. `text_layout_skia.cpp`의 `layout_key_hash`에도 섞는다. 정책만 다른 두 배치는 다른 항목이다.
- `normalize_text_layout_request`는 `wrap`이 거짓인 요청의 정책을 기본값으로 맞춘다. 폭처럼 접지 않으면 기하에 쓰이지 않아, 정책만 다른 한 줄 배치가 cache를 따로 채우지 않는다.
- `text_area_wrap_cache`는 쥔 정책을 하나 더 기억한다. `wrap_displayed`의 무효화 조건(`width_`·`wrap_`·`face_`·`font_revision_` 등)에 정책을 더해 `entries_`·`rows_`를 함께 비운다.
- 후보는 문단을 접을 때 한 번 구한다. 여러 줄 칸은 그 결과를 wrap cache의 접은 자리로 쥐므로 frame마다 다시 구하지 않는다.

**초점·접근성·배율·테마**
- 초점과 입력은 바뀌지 않는다. Ctrl+←·→ 같은 낱말 이동은 줄 나눔이 아니라 따로 둔다.
- 접근성 이름과 UIA 글 범위는 원문 그대로다. 화면 줄 단위로 걷는 범위는 새 접은 줄을 따른다.
- 후보는 글만의 함수라 배율·테마·고대비와 무관하다. 폭이 배율을 따를 뿐이다. 움직임이 없다.

## 결정

**1. 규칙의 출처.** 질문: 규칙을 어디서 오게 하나.
- 길 a: 내장 생성표만 쓴다. 끈 구성과 켠 구성, Windows와 Android, OS 판에 상관없이 같은 줄이다. 시험이 결정적이다. 표가 천여 줄 늘고(생성물), 태국어·라오어·크메르어·미얀마어의 사전 나눔은 없다(LB1의 SA를 AL로 본다).
- 길 b: 플랫폼 ICU 우선, 내장표로 물러서기(`SkUnicode::BreakType::kLines`). 글자소(`grapheme_skia.cpp`)와 같은 구조다. 켠 구성에서 사전 나눔을 얻는다. ICU는 Windows 10 1903(`icu.dll`)·1703(`icuuc.dll`)과 Android 12(API 31)부터 열리고 그 아래는 libgrapheme이나 내장표라([벡터 그림](vector-image-design.md)), 같은 글이 OS 판마다 다른 줄이 된다. `hangul_words`는 ICU 판마다 다른 locale 키워드에 기대거나 결과를 다시 걸러야 한다.
- 권장: a. 대가: 사전이 필요한 문자는 비상 분할로만 접힌다. Unicode 판을 올릴 때 글자소 표와 함께 생성기를 다시 돌린다. b는 a의 함수 모양을 그대로 두고 후속으로 더할 수 있다. **승인: 길 a (2026-11-02).**

**2. 한국어의 줄 바꿈 자리.** 질문: 한국어를 어디서 바꾸나.
- 길 a: 어절 우선(`hangul_words` 기본). 한글 사이와 한글·라틴·숫자 사이는 지금과 같다. 하이픈·슬래시 뒤와 한자·가나 사이는 새로 바꾼다. 칸보다 긴 어절은 비상 분할로 글자소에서 바꾼다(`label_wrap_tests.cpp`의 NFD `한` 여덟 개 시험이 그대로 통과한다).
- 길 b: 순수 UAX #14. 표준 그대로다. 한글 음절 사이 어디서나 바꿔 `설정을 저장했습니다`가 `설정을 저`와 `장했습니다`로 갈린다. 한국어 UI 전부가 회귀한다.
- 길 c: CSS `keep-all`처럼 한자·가나도 AL로 본다. 한자를 섞은 한국어 문단이 한자 사이에서 갈리지 않는다. 일본어·중국어 문단은 다시 글자소 접기로 돌아간다.
- 권장: a를 기본으로 두고 b를 정책 값(`unicode`)으로 연다. 대가: 기본이 표준과 다르다. 공백 없이 한글을 이어 쓴 긴 글은 지금처럼 폭 끝 글자에서 갈린다. c는 enum에 값을 더해 후속으로 연다. **승인: 길 a (2026-11-02).**

**3. 정책의 자리.** 질문: 정책을 어디에 두나.
- 길 a: `text_layout_request` 필드와 네 컨트롤 config의 같은 필드. 배치가 정책을 알고 cache key가 맞다. 컨트롤마다 고를 수 있다. 공개 필드가 다섯 곳에 는다.
- 길 b: 컨트롤 config에만 둔다. request는 정책을 모르고, 같은 글의 배치를 cache가 구별하지 못한다. 결국 request에 싣게 된다.
- 길 c: 프로세스 단일 값(`set_single_line_break` 선례). 필드가 늘지 않는다. 바꾸면 모든 배치 cache를 비워야 하고, 한 창 안에서 섞을 수 없고, 시험이 전역 상태에 기댄다.
- 길 d: 앱 스타일 키(`ui_style::text`). 앱 전체 기본을 JSON에서 고른다. 줄 2 style_sheet의 생성기·파서 순서(G4)에 묶인다. 배치 문맥은 `ui_metrics`만 나르고 `ui_style::text`를 나르지 않아, 값이 `make_text_layout`에 닿으려면 문맥이나 전역을 새로 지나야 한다.
- 권장: a. 대가: 앱 전체를 `unicode`로 바꾸려는 앱은 컨트롤마다 적는다. G4가 정해지면 스타일 키가 config 기본값을 채우는 길을 그때 더한다. 지금은 스타일 파일을 건드리지 않아 style_sheet와 순서가 묶이지 않는다. **승인: 길 a (2026-11-02).**

**4. 두 접기의 통합.** 질문: 두 접기를 하나로 모으나.
- 길 a: 자리 고르기만 모은다(`line_fold`). 같은 입력에 같은 자리다. `fit` 찾기는 각자 둔다.
- 길 b: `wrap_paragraph`를 지우고 측정기 길도 `make_text_layout(request, measurer)`로 접는다. 코드가 하나다. 그 측정기 overload는 글자소마다 문단 처음부터의 앞부분(`text.substr(0, next)`)을 재서 문단 길이의 제곱에 비례한다. `wrap_paragraph`의 걸음 늘리기와 이분 탐색이 피한 비용이고, [자동 줄 넘김](text-area-wrap-design.md)이 큰 글에서 잰 시간을 잃는다.
- 권장: a. 대가: `fit`을 찾는 두 길이 남아 측정이 다르면(측정기의 앞부분 차이와 부분 문자열 폭) 드물게 다른 `fit`이 나온다. 동치 시험은 앞부분 폭이 더해지는 고정 폭 측정기로 본다. **권장대로 진행.**

**5. 문단 안의 BK·NL.** 질문: 문단 안의 BK·NL을 어떻게 다루나.
- 길 a: 후보로만 쓴다. 문단 모델(CR·LF)이 그대로다. `text_edit`·한 줄 도우미·구문 강조의 줄 규칙과 어긋나지 않는다. U+2028이 줄을 강제로 바꾸지 않는다.
- 길 b: 접은 줄처럼 강제로 바꾼다. 표준에 가깝다. 화면 줄과 Home·End·↑↓가 새 줄 끝을 알아야 하고, 그 글자의 글리프를 그리지 않는 규칙이 더 든다.
- 권장: a. 대가: U+2028을 줄 바꿈으로 쓰는 글은 한 줄로 보인다. **권장대로 진행.**

**6. 비상 분할 끄기.** 질문: 비상 분할을 끌 수 있게 하나.
- 길 a: 늘 켠다. 지금과 같다. 칸 밖으로 넘치는 줄이 없다.
- 길 b: 끄는 필드를 둔다. 긴 URL을 한 줄로 두고 가로로 밀 수 있다. label·`text_block`은 넘친 줄을 자를 곳이 없다.
- 권장: a. 대가: 낱말 한가운데 갈림을 막을 길이 없다. 필요하면 앱이 WJ(U+2060)나 NBSP를 넣는다. **권장대로 진행.**

## 플랫폼

- Win32: 순수 C++이고 `src/win32`를 고치지 않는다. 끈 구성과 켠 구성이 같은 표를 쓴다. TSF 조합 글도 표시 문서의 일부로 같은 규칙에 접힌다.
- Android: 같은 C++이고 Java 소스는 0줄이다. JNI도 부르지 않는다. 결정 1의 b를 고르더라도 NDK `libicu.so`를 `dlopen`으로 여는 기존 공장 길이라 Java가 들지 않는다. unsupported인 부분은 없다.
- 고지: Unicode 데이터는 글자소 표와 같은 Unicode License V3다. [third_party/unicode-LICENSE.txt](../third_party/unicode-LICENSE.txt)가 이미 `cmake/generate_notices.cmake`와 `cmake/install.cmake`에 걸려 있어 새 고지 파일은 없다. 생성물 머리말에 판과 출처를 적는다.

## 다른 노드와의 약속

- **rich_edit(R3, 같은 줄)**: 문단별 접는 폭을 이 위에 얹는다. 약속은 넷이다. (1) `line_fold`의 자리 고르기는 폭을 모른다. 폭은 `fit`을 찾는 쪽에만 있어 문단마다 다른 폭을 넘겨도 된다. (2) 접은 자리는 문단 처음 기준 byte offset이고 글자소 경계다. (3) 정책은 `text_layout_request::line_break`로만 들어간다. (4) `text_area_wrap_cache`의 무효화 조건은 이 노드가 정책을 더한 그 자리에 rich_edit가 문단 폭을 더한다.
- **syntax(R1, 같은 줄)**: [구문 강조 도우미](syntax-highlight-design.md)에 적은 대로 `text_area_element.cpp`에서 함수가 겹치지 않는다. 이 노드는 `wrap_paragraph`·`wrap_displayed`·`layout_request`이고 syntax는 `span_foreground`·`metric_styles`·`draw_content`다. `text_layout_request`는 이 노드만 고친다.
- **style_sheet(R2, 같은 줄)**: 이번에는 스타일 키를 두지 않는다. G4가 정해져 `text` 절에 키를 더하면 `single_line_break` 옆이 자리이고, 값은 이 enum 이름 그대로다.
- **markup(R4, 같은 줄)**: 서식 글이 배치를 지으면 `text_layout_request::line_break`를 config에서 잇는다.
- **text_block_style을 쓰는 새 요소**: 기본 정책(`hangul_words`)을 따른다. config에 칸을 여는 것은 그 요소의 후속이다(같은 라운드의 info_bar가 그 예다).
- 구현 때 함께 고친 문서: [여러 줄 라벨](label-wrap-design.md), [직접 그리는 글](text-block-design.md), [자동 줄 넘김](text-area-wrap-design.md), [텍스트 레이아웃 설계](text-layout-design.md), [텍스트 입력](concepts/text-input.md)의 "공백에서 먼저 바꾼다" 서술. 색인(`docs/README.md`)은 통합 때 붙인다.

## 확인

자동 test:

- `tests/line_break_tests.cpp`(`luil_core_tests`):
  - 대표 사례를 내장한다. 하이픈·슬래시 뒤(`alpha-beta-gamma`, URL), 숫자 앞 하이픈은 붙임(`ABC-123-456`), 닫는 부호(`)`·`」`·`。`) 앞 금지, 여는 부호 뒤 금지, NBSP·WJ·ZW, 숫자 `1,234.5`·`$100`(LB25), 작은 가나(CJ→NS), 이모지 피부색(LB30b), 국기 쌍(LB30a), 결합 부호(LB9·LB10).
  - 두 정책: `한국어 문장`이 `hangul_words`에서 공백 뒤에만, `unicode`에서 음절 사이마다 후보를 갖는지, 한자·가나는 두 정책에서 같은지, 조합형 자모도 같은 정책을 따르는지.
  - 반드시 바꾸는 자리(LF·U+2028·CRLF)와 글 끝, ASCII 빠른 길이 해독 길과 같은 자리인지.
  - 환경 변수 `LUIL_UCD_DIRECTORY`가 가리키는 UCD의 `auxiliary/LineBreakTest.txt`를 `unicode` 정책으로 모두 돌린다. 첫 줄의 판이 16.0.0이 아니면 실패하고, 변수가 없으면 건너뛴다(`SKIP`). 원본은 저장소에 넣지 않는다. 2026-11-02에 16,672건이 모두 맞았다.
  - `line_break_hangs`가 U+0020·U+3000에 참, U+00A0·U+2007·U+202F·U+200B·탭에 거짓인지. 깨진 UTF-8에서 범위를 벗어나지 않는지, 빈 글.
- `tests/label_wrap_tests.cpp`·`tests/text_layout_tests.cpp`(`luil_core_tests`, 고정 폭 측정기): 기존 공백 우선 시험이 그대로 통과하는지. `alpha-beta-gamma`가 하이픈 뒤에서, URL이 슬래시 뒤에서, 일본어 문단이 `」` 앞이 아닌 자리에서 바뀌는지. 한국어 어절 보존과 `unicode` 정책의 음절 접기, 긴 어절의 비상 분할. 전각 공백이 줄 폭에서 빠지는지. 자른 자리가 모두 글자소 경계인지.
- `tests/text_area_element_tests.cpp`(`luil_core_tests`): 같은 글·폭에서 측정기 길의 `lines()`가 `make_text_layout(request, measurer)`의 줄과 같은지(공백 뒤 결합 부호, 전각 공백, URL이 든 글을 두 정책으로). 정책을 바꾸면 `text_area_wrap_cache`가 비워져 다시 재고, 다시 찬 자리가 cache 없이 접은 것과 같은지.
- `tests/text_layout_native_tests.cpp`(`luil_tests`, Windows 실제 글꼴): 정책만 다른 두 요청이 cache에서 따로 miss인지, 접지 않는 요청은 정책이 달라도 한 항목인지, 실제 글꼴로 접은 자리가 후보이거나 비상 분할 자리인지, 줄 경계 offset 왕복.
- 생성기는 판이 다른 UCD를 받으면 멈춘다. 다시 돌린 생성물이 커밋한 것과 같은지는 사람이 본다.

데모 칸 (상태를 label로 그려 UIA로 되읽는다):

- 견본 글과 칸 폭, label·칸 설정, 상태 글은 [examples/demo/line_break_sample.h](../examples/demo/line_break_sample.h) 하나에 두고 두 데모와 page test가 같이 쓴다.
- `examples/demo/basics_page.cpp`에 "줄 나눔" 칸을 더한다. 한국어·한자·가나·영문·URL을 섞은 여러 줄 label과 읽기 전용 여러 줄 칸을 같은 폭(220 논리 픽셀)으로 나란히 두고, 정책을 고르는 스위치 `line-break-policy`를 둔다. label `line-break-status`가 "정책 hangul_words · 라벨 N줄 · 칸 M줄 · 첫 줄 끝 '…'"를 그린다. 줄 수와 첫 줄 끝은 build에서 그 폭으로 label을 재고(`label_element::measure_wrap`) 칸을 배치해(`text_area_element::lines()`) 센 값이다. UIA로 스위치를 바꾸고 되읽어 줄 수와 첫 줄 끝이 바뀌는지 본다.
- `examples/mobile_demo/basics_page.cpp`에 같은 칸을 위아래로 더한다.
- `tests/mobile_demo_page_tests.cpp`가 고정 폭 측정기로 두 정책의 상태 글을 잠근다. 화면의 값은 실제 글꼴로 잰 값이라 다를 수 있어 검증 때 UIA와 화면으로 되읽는다.

사람 확인:

- 데스크톱 데모에서 창 폭을 줄이며 라벨·여러 줄 칸·마크다운의 줄을 눈으로 본다. 한국어 어절이 갈리지 않고 `」`가 줄 머리에 오지 않는지 본다.
- 실기기 mobile_demo에서 같은 칸을 세로·가로로 돌려 본다.

## 범위 밖

- 한 줄 도우미(`draw_text`·`measure_text`·`src/ui/single_line_text.h`). 줄을 바꾸지 않는다.
- 사전 나눔(태국어 등)과 하이픈 넣기.
- `keep-all`(결정 2의 c)과 스타일 키(결정 3의 d). 후속으로 연다.
- 양방향 문단의 줄 나눔 순서. [텍스트 레이아웃 설계](text-layout-design.md)가 양방향 편집과 함께 미뤘다.
- 낱말 이동(Ctrl+←·→)과 더블 클릭 선택의 낱말 경계(UAX #29 word).

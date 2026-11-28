# 설명서 문서 쓰는 법

luil 설명서(`docs/manual/`)의 문서를 더하거나 고칠 때 지키는 규칙이다. 대상 독자는 luil로 앱을 만드는 C++ 개발자다.
독자가 "트리 뷰를 어떻게 만드나", "펼침 상태는 누가 들고 있나" 같은 구체적인 물음의 답을 찾을 수 있어야 한다.

## 1. 정확성 규칙 (가장 먼저)

- **헤더와 소스가 정본이다.** 설계 문서(`docs/*.md`)와 코드가 다르면 코드를 따르고, 다른 점을 문서의 "함정"이나 주석에 적는다.
- **지어낸 API를 쓰지 않는다.** 본문의 `<code>` 안 이름은 `tools/check_ids.py`가 저장소에서 찾는다.
  앱 쪽 예시 이름(앱 함수, 가상의 intent)은 `<code class="free">`로 표시한다.
- **선언은 원문으로 보인다.** 설정 구조체·열거형·함수 선언은 손으로 옮기지 않고 `data-src` 발췌로 싣는다(아래 3.1).
- **예제 코드의 출처.** 먼저 `examples/`·`tests/`에서 발췌한다. 이 둘은 빌드되고 도는 코드다.
  저장소에 예제가 없을 때만 손으로 쓰고, 그 예제는 `data-compile`로 표시해 `tools/check_samples.py`가 컴파일하게 한다.
- **확인하지 못한 동작은 단정하지 않는다.** "코드에서 확인할 수 없다"고 적는다. 플랫폼에서 직접 돌려 보지 않은 것은 그렇다고 쓴다.
- 줄 번호는 `assets/manual.js`의 `DOCS.commit` 기준이다.

## 2. 문서의 짜임

### 2.1 파일 머리

```html
<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="claude:viewport-fit-sides" content="cover">
<title>{제목} · luil 설명서</title>
<meta name="description" content="luil 설명서 — {한 문장}">
(글꼴 link 셋과 assets/manual.css, 테마 스크립트 — 기존 문서에서 그대로 옮긴다)
<style> /* 이 문서만의 실습 스타일. 색은 manual.css의 토큰(var(--…))만 쓴다 */ </style>
</head>
<body data-page="{manual.js DOCS의 id}">
<header class="topbar"></header>
<div class="doc-shell">
  <nav class="sitenav" aria-label="설명서 목차"></nav>
  <main class="doc-main">
    <header class="doc-head">
      <div class="crumbs" data-auto></div>
      <h1>{제목}</h1>
      <p class="summary">{이 문서가 답하는 것, 두세 문장}</p>
      <div class="head-cols">
        <dl class="meta">
          <div><dt>관련 소스</dt><dd><code>list_element.h</code>·…</dd></div>
          <div><dt>실습</dt><dd>{실습 이름}</dd></div>
          <div><dt>먼저 읽기</dt><dd><a class="xref" href="ui-tree.html">불변 UI tree</a> · …</dd></div>
        </dl>
        <div class="goals">
          <h2><i class="ci" aria-hidden="true">&#xEB20;</i>이 문서를 읽으면</h2>
          <ol><li>…</li></ol>
        </div>
      </div>
    </header>
    <div class="doc-body">
      <section class="sec" id="…"> … </section>
      …
    </div>
    <nav class="pager" data-auto aria-label="앞뒤 문서"></nav>
  </main>
</div>
<footer class="site-foot"></footer>
<script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>
<script src="assets/manual.js"></script>
<script> /* 실습 스크립트 */ </script>
</body>
</html>
```

새 문서는 `assets/manual.js`의 `DOCS.sections`에 한 줄이 있어야 왼쪽 목차·앞뒤 문서·검색에 잡힌다.
`tools/manifest.json`의 `headers` 표가 그 문서 id를 가리키는 헤더들이 이 문서가 설명해야 할 범위다.

### 2.2 컴포넌트 문서의 조각 차례

조각마다 `<section class="sec" id="영문-이름">`이고 머리는 `<header class="sec-head"><h2>…</h2><p class="lede">한 문장</p></header>`다.
id는 영문 소문자·숫자·하이픈만 쓴다. `sidebar`, `settings`, `downloads`, `comment-…`는 쓰지 않는다(아티팩트 예약어).

1. **개요** (`overview`) — 무엇이고 언제 쓰나. 비슷한 요소와 고르는 기준(표).
2. **개념** (`concepts`) — 상태는 누가 들고 무엇이 메시지로 오가나. 그림(SVG)이나 흐름도가 잘 맞는다.
3. **만들기** (`build-…` 여럿) — 단계별 조리법. 조각마다 한 가지 일: "평평한 목록", "트리 뷰", "여러 개 선택"….
   코드 → 설명 → 결과. 앱 상태 구조체, frame을 짓는 함수, `handle`의 갈래를 모두 보인다.
4. **설정 레퍼런스** (`config`) — 구조체 원문 발췌(`data-src`)와 필드 표(이름·기본값·뜻·주의).
5. **사건과 메시지** (`messages`) — 무엇을 누르면 어떤 메시지가 어느 스레드로 가나. 앱이 할 일.
6. **키보드와 접근성** (`keyboard`) — 키 표(소스 줄 근거), 보조 기술이 읽는 역할·상태.
7. **플랫폼 차이** (`platforms`) — 터치·모바일·OS별로 다른 것. 같은 것도 "같다"고 적는다.
8. **함정** (`pitfalls`) — 조용히 틀리는 자리. 코드·문서의 경고를 근거로.
9. **자주 묻는 질문** (`faq`, `data-toc="자주 묻는 질문"`) — 개발자가 실제로 물을 법한 질문 10개 이상.
   개념 문서는 이 뒤에 **사례로 확인하기**(`data-toc="사례로 확인하기" data-kind="case"`)를 둘 수 있다. 입력과 상태를 하나 정해 두고 luil이 무엇을 하는지 따라간 결과다. 질문이 아니므로 `gen_search.py`는 이 절의 `details.faq`를 "사례"로 색인하고 검색에서 질문보다 낮게 친다.
10. **이 문서가 다루는 헤더** — `tools/gen_pages.py`가 자동으로 넣는다. 손으로 쓰지 않는다.

개념 문서(스레드, tree…)는 이 차례를 따르지 않아도 되지만 끝의 질문과 헤더 영역은 같다.

## 3. 부품

### 3.1 원문 발췌

```html
<div class="code" data-lang="cpp" data-src="include/luil/ui/list_element.h:21-36"><script type="text/plain">
</script></div>
```
`python3 docs/manual/tools/fill_excerpts.py`가 원문 줄을 채우고 `data-path`·`data-start`를 단다.
여러 범위는 `path:a-b,c-d`. 머리의 경로가 GitHub의 그 줄로 이어진다.

### 3.2 손으로 쓴 예제 (컴파일 확인)

```html
<div class="code" data-lang="cpp" data-label="앱 상태와 frame" data-compile="pre-lists"><script type="text/plain">
… 예제 …
</script></div>
<script type="text/plain" id="pre-lists" data-prelude>
#include "luil/ui/list_element.h"
namespace sample { /*SAMPLE*/ }
</script>
```
머리말(prelude)은 화면에 보이지 않는다. `/*SAMPLE*/` 자리에 예제가 들어가 `g++ -std=c++20 -fsyntax-only`로 컴파일된다.
`data-compile="tu"`면 예제 자체가 완전한 번역 단위다. 화면에는 "컴파일 확인" 표지가 붙는다.

### 3.3 코드 읽기 (줄 단위 해설)

```html
<div class="walk side">
  <div class="code" data-lang="cpp" data-src="…"><script type="text/plain"></script></div>
  <ol class="walk-steps-src">
    <li data-lines="21-24" data-title="단계 이름">설명</li>
  </ol>
</div>
```

### 3.4 그 밖의 부품

| 부품 | 표기 |
|---|---|
| 본문 | `<div class="prose"><p>…</p></div>` |
| 알림 판 | `<div class="callout key\|warn\|think"><i class="ci" aria-hidden="true">&#xEA61;</i><div><span class="ttl">제목</span>본문</div></div>` (`key`=핵심, `warn`=주의, 없음=참고) |
| 표 | `<div class="table-wrap"><table class="t"><thead>…</thead><tbody>…</tbody></table></div>` |
| 탭 | `<div class="tabs"><section data-tab="Windows">…</section>…</div>` |
| 실습 창 | `<div class="lw" data-title="이름" data-surface="대응하는 luil 형식" data-reset id="…"><div class="lw-body">…</div><div class="lw-foot">설명</div></div>` — 처음 상태로 돌리는 단추를 누르면 `lw-reset` 사건이 온다 |
| 판 | `<div class="panel"><h4>…</h4><p>…</p></div>` |
| 요약 | `<ol class="recap"><li>…</li></ol>` |
| 질문 | `<div class="faq-list"><details class="faq"><summary>질문</summary><div class="faq-a"><p class="faq-short">짧은 답</p><p>근거와 설명</p></div></details></div>` |
| 문서 링크 | `<a class="xref" href="ui-tree.html#s5">hit test</a>` |
| 용어 풍선 | `<span class="term" data-term="MPSC" data-tip="설명">MPSC</span>` |
| 아이콘 | Codicon 글리프 `<i class="ci" aria-hidden="true">&#xEB20;</i>`. 이모지는 쓰지 않는다 |

실습 스크립트는 `window.LU`의 `el`, `svg`, `icon`, `flow`, `sourceUrl`, `onTheme`, `reduced`, `store`를 쓴다.
색은 CSS 토큰으로만 칠하고(`var(--accent)` 따위), `onTheme`으로 테마가 바뀔 때 다시 그린다.
움직임은 `LU.reduced()`가 참이면 줄인다. 키보드로 다룰 수 있어야 하고, 390px 폭에서 가로로 넘치지 않아야 한다.

## 4. 실습(인터랙티브 재현)

- luil의 동작을 브라우저로 옮긴 것이다. 규칙마다 소스 줄 근거가 있어야 하고, 근거가 없는 동작은 넣지 않는다.
- 실습 아래(`lw-foot`)에 "무엇을 옮겼고 무엇은 옮기지 않았나"를 적는다. 그림은 luil의 실제 렌더링이 아니라 재현이다.
- 실습이 내는 메시지를 기록 창에 보이면 좋다: 사용자가 누른 것 → element가 낸 메시지 → 앱이 바꾼 상태.

## 5. 글

- 평서형(해라체): "~다", "~한다". 독자를 부르지 않는다.
- 문장은 짧게. 한 문단에 한 생각. 영어 형식 이름은 `<code>`로.
- 숫자에는 단위(논리 픽셀, ms)와 근거를 붙인다.

## 6. 다 쓴 뒤 돌리는 것

문서 하나를 쓸 때는 그 문서만 넘긴다 (여럿이 함께 쓸 때 서로의 파일을 덮지 않는다).

```bash
python3 docs/manual/tools/fill_excerpts.py docs/manual/lists.html          # 발췌 채우기
python3 docs/manual/tools/gen_pages.py docs/manual/lists.html              # 관련 헤더 영역
python3 docs/manual/tools/fill_excerpts.py --check docs/manual/lists.html  # 발췌 = 원문
python3 docs/manual/tools/check_ids.py docs/manual/lists.html              # 지어낸 이름 없음
python3 docs/manual/tools/check_samples.py docs/manual/lists.html          # 손으로 쓴 예제가 컴파일됨
node docs/manual/tools/check_page.mjs docs/manual/lists.html               # 브라우저 네 가지: 오류·넘침·빈 코드
```

설명서 전체를 손볼 때(관리자):

```bash
python3 docs/manual/tools/gen_api.py        # API가 바뀌었으면 (libclang 필요) → assets/api-index.js
python3 docs/manual/tools/gen_pages.py      # 모든 문서의 관련 헤더 영역
python3 docs/manual/tools/gen_search.py     # 검색 색인·문서 지도
python3 docs/manual/tools/check_coverage.py # 헤더마다 문서
```

## 7. 본보기

`lists.html`(목록과 트리 뷰)이 컴포넌트 문서의 본보기다. 조각 차례, 발췌와 손 예제의 비율, 표의 모양,
함정(`ol.recap`)과 질문(`details.faq`)의 글투를 그대로 따른다. 실습은 `assets/lists-lab.js`처럼 문서 이름의
`assets/<문서>-lab.js` 하나에 두고, 원본 함수를 옮겼으면 `assets/treemodel.js`처럼 따로 떼어 원본과 대조할 수 있게 한다.

여럿이 나눠 쓸 때 공용 파일(`assets/manual.css`, `assets/manual.js`, `tools/*`, `manifest.json`, 다른 문서)은
고치지 않는다. 문서만의 스타일은 그 문서의 `<style>`에 둔다. 차례(`DOCS`)와 manifest는 관리자가 맞춘다.

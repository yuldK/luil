/* luil 설명서 — 다국어 실습: 문자열 표 놀이터
 *
 * 계산은 전부 assets/i18nmodel.js(src/text/string_table.cpp의 이식, 컴파일한 원본과 대조)를 부른다. 이 파일은 화면만 짓는다.
 *  1. 언어 고르기: 선호 목록 → select_strings의 걸음 (시험 표) + 같은 목록의 라이브러리 표 답
 *  2. 문장 채우기: 고른 언어의 키 하나에 인자를 넣어 string_lookup::format의 칸 고르기와 채우기를 본다
 *  3. 복수 범주: plural_category_for(rule, count)
 * 선호 목록을 쉼표로 가르는 것과 입력 칸의 글을 int64로 읽는 것은 이 문서의 입력 처리다(luil의 것이 아니다).
 */
(function () {
  "use strict";
  const M = window.LuilI18n;
  const { el } = window.LU;

  // 키마다 처음 넣어 둘 인자다 (tests/string_table_tests.cpp가 쓰는 값과 같은 꼴).
  const PRESET_ARGS = {
    braces: [["value", "s", "x"]],
    date_line: [["year", "i", "2026"], ["month", "i", "11"], ["day", "i", "3"], ["month_name", "s", "November"]],
    echo: [["word", "s", "hop"]],
    file_summary: [["count", "i", "1"]],
    files_found: [["count", "i", "1"]],
    files_selected: [["count", "i", "1"]],
    greeting: [["name", "s", "Ada"], ["place", "s", "Seoul"]],
    number: [["number", "i", "1234567"]],
    save: [],
  };
  const PREF_PRESETS = ["zh-Hant-TW, ko", "en-US", "fr-CA, ko", "zh_hant_tw", "zh-Hans-CN", "zh", "de, xx-YY", ""];
  const COUNT_PRESETS = ["-1", "0", "1", "2", "-9223372036854775808"];
  const INITIAL = { prefs: "zh-Hant-TW, ko", key: "files_selected", rule: "one", count: "1" };

  // 이 문서의 입력 처리: 비었으면 빈 목록, 아니면 쉼표로 가르고 양끝 공백을 지운다(빈 태그는 남긴다).
  const parsePrefs = (t) => (t.trim() === "" ? [] : t.split(",").map((s) => s.trim()));
  const showTag = (t) => (t === "" ? "(빈 태그)" : t);
  const quote = (t) => "u8\"" + t + "\"";

  function setup(root) {
    if (!root || !M) return;
    const body = root.querySelector(".lw-body");
    const langs = M.DATA.languages;
    const lib = M.DATA.library;
    const S = { lang: 0 };

    // ---------- 1. 언어 고르기 ----------
    const prefInput = el("input", { type: "text", class: "il-prefs", value: INITIAL.prefs, "aria-label": "선호 목록 (쉼표로 가른다)", spellcheck: "false", autocomplete: "off" });
    const prefChips = el("div", { class: "controls il-chips" },
      el("span", { class: "field", text: "보기" }),
      ...PREF_PRESETS.map((p) => el("button", { type: "button", class: "btn sm", "data-p": p, text: p === "" ? "(빈 목록)" : p })));
    const stepsTable = el("table", { class: "t il-steps" });
    const pickOut = el("div", { class: "il-result", "aria-live": "polite" });
    const block1 = el("div", { class: "il-block" },
      el("h4", null, "1 · 언어 고르기 ", el("span", { class: "muted", text: "select_strings(test_strings::table, 선호)" })),
      el("div", { class: "il-tables", text: "시험 표의 언어: " + langs.map((l, i) => i + " " + l.tag + " (" + l.plural + ")").join(" · ") + " — 라이브러리 표: " + lib.map((l) => l.tag).join(" · ") }),
      el("label", { class: "il-label" }, el("span", { text: "선호 목록" }), prefInput),
      prefChips,
      el("div", { class: "table-wrap" }, stepsTable),
      pickOut);

    // ---------- 2. 문장 채우기 ----------
    const keySelect = el("select", { class: "il-key", "aria-label": "키" }, ...M.DATA.keys.map((k) => el("option", { value: k, text: k })));
    const langNote = el("span", { class: "il-lang" });
    const formsBox = el("div", { class: "il-forms" });
    const argRows = el("div", { class: "il-args", role: "group", "aria-label": "인자" });
    const addArg = el("button", { type: "button", class: "btn sm", text: "인자 더하기" });
    const fillOut = el("div", { class: "il-result", "aria-live": "polite" });
    const block2 = el("div", { class: "il-block" },
      el("h4", null, "2 · 문장 채우기 ", el("span", { class: "muted", text: "string_lookup::format(key, arguments)" })),
      el("div", { class: "controls" }, el("span", { class: "field", text: "언어" }), langNote, el("span", { class: "field", text: "키" }), keySelect),
      formsBox,
      el("div", { class: "il-args-head" }, el("span", { text: "인자 (이름 · 형 · 값). 같은 이름이면 앞의 것이 쓰인다" }), addArg),
      argRows,
      fillOut);

    // ---------- 3. 복수 범주 ----------
    const ruleSeg = el("div", { class: "seg", role: "group", "aria-label": "복수 규칙" },
      ...["one", "other"].map((r) => el("button", { type: "button", "data-v": r, text: "plural_rule::" + r })));
    const countInput = el("input", { type: "text", class: "il-count", value: INITIAL.count, inputmode: "numeric", "aria-label": "count", spellcheck: "false", autocomplete: "off" });
    const countChips = el("div", { class: "controls il-chips" }, ...COUNT_PRESETS.map((c) => el("button", { type: "button", class: "btn sm", "data-c": c, text: c })));
    const pluralOut = el("div", { class: "il-result", "aria-live": "polite" });
    const block3 = el("div", { class: "il-block" },
      el("h4", null, "3 · 복수 범주 ", el("span", { class: "muted", text: "plural_category_for(rule, count)" })),
      el("div", { class: "controls" }, ruleSeg, el("label", { class: "il-label inline" }, el("span", { text: "count" }), countInput)),
      countChips,
      pluralOut);

    body.append(block1, block2, block3);

    // ---------- 인자 줄 ----------
    function argRow(name, kind, value) {
      const nameIn = el("input", { type: "text", class: "il-an", value: name, "aria-label": "인자 이름", spellcheck: "false", autocomplete: "off" });
      const kindSeg = el("div", { class: "seg", role: "group", "aria-label": "인자의 형" },
        el("button", { type: "button", "data-k": "s", text: "글" }), el("button", { type: "button", "data-k": "i", text: "정수" }));
      const valueIn = el("input", { type: "text", class: "il-av", value, "aria-label": "인자 값", spellcheck: "false", autocomplete: "off" });
      const remove = el("button", { type: "button", class: "btn sm", text: "빼기" });
      const row = el("div", { class: "il-arg-row" }, nameIn, kindSeg, valueIn, remove);
      row.dataset.k = kind;
      const pressKind = () => kindSeg.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", b.dataset.k === row.dataset.k ? "true" : "false"));
      pressKind();
      kindSeg.addEventListener("click", (e) => { const b = e.target.closest("button[data-k]"); if (b) { row.dataset.k = b.dataset.k; pressKind(); drawFill(); } });
      nameIn.addEventListener("input", drawFill);
      valueIn.addEventListener("input", drawFill);
      remove.addEventListener("click", () => { row.remove(); drawFill(); });
      return row;
    }
    function setArgs(list) {
      argRows.textContent = "";
      for (const [n, k, v] of list) argRows.append(argRow(n, k, v));
    }
    // 줄을 인자로 읽는다. 정수 칸이 int64가 아니면 그 인자는 넘기지 않는다 (이 문서의 입력 처리).
    function readArgs() {
      const args = [];
      const skipped = [];
      argRows.querySelectorAll(".il-arg-row").forEach((row) => {
        const name = row.querySelector(".il-an").value;
        const raw = row.querySelector(".il-av").value;
        if (row.dataset.k === "i") {
          const n = M.parseInt64(raw);
          if (n === null) skipped.push(name);
          else args.push({ name, value: n });
        } else args.push({ name, value: raw });
      });
      return { args, skipped };
    }

    // ---------- 그리기 ----------
    function drawPick() {
      const prefs = parsePrefs(prefInput.value);
      const steps = [];
      const chosen = M.selectLanguage(langs, prefs, steps);
      const libChosen = M.selectLanguage(lib, prefs);
      S.lang = chosen;
      stepsTable.textContent = "";
      stepsTable.append(el("thead", null, el("tr", null, el("th", { text: "선호" }), el("th", { text: "견준 태그" }), el("th", { text: "시험 표에서" }))));
      const tb = el("tbody");
      if (!steps.length) tb.append(el("tr", null, el("td", { colspan: "3", class: "muted", text: "선호가 없다 — 견주지 않는다" })));
      steps.forEach((s, i) => {
        const tried = s.tried.length ? s.tried : [[null, null]];
        tried.forEach(([tag, found], j) => {
          const prefCell = j === 0 ? el("td", { rowspan: String(tried.length) }, el("span", { class: "il-n", text: String(i + 1) }), el("code", { text: showTag(s.preference) })) : null;
          const tagCell = el("td", null, tag === null ? el("span", { class: "muted", text: "빈 태그 — 견줄 것이 없다" }) : el("code", { text: tag }), j > 0 ? el("span", { class: "muted", text: " (끝을 뗐다)" }) : null);
          const resCell = el("td", null, tag === null ? el("span", { class: "muted", text: "—" }) : found === null ? el("span", { class: "muted", text: "없음" }) : el("b", { class: "il-hit", text: langs[found].tag + " (차례 " + found + ")" }));
          tb.append(el("tr", null, prefCell, tagCell, resCell));
        });
      });
      stepsTable.append(tb);
      const fellBack = !steps.some((s) => s.found !== null);
      pickOut.textContent = "";
      pickOut.append(
        el("p", null, el("code", { text: "select_strings(test_strings::table, {" + prefs.map(quote).join(", ") + "})" }), " → ", el("b", { text: langs[chosen].tag }), fellBack ? el("span", { class: "muted", text: " — 맞는 것이 없어 첫 언어(BASE)" }) : null),
        el("p", null, el("code", { text: "set_text_languages(같은 목록)" }), " → 라이브러리 표 ", el("b", { text: lib[libChosen].tag }), libChosen === 0 && !prefs.some((p) => M.findLanguage(lib, p) !== null) ? el("span", { class: "muted", text: " — 맞는 것이 없어 한국어" }) : null));
      drawFill();
    }

    function drawFill() {
      const lang = langs[S.lang];
      const key = M.DATA.keys.indexOf(keySelect.value);
      langNote.textContent = "";
      langNote.append(el("code", { text: lang.tag }), el("span", { class: "muted", text: " (위에서 고른 언어, plural: " + lang.plural + ")" }));
      const forms = M.forms(lang, key);
      formsBox.textContent = "";
      formsBox.append(
        el("div", null, el("span", { class: "il-k", text: "one 칸" }), forms[1] === "" ? el("span", { class: "muted", text: "비었다" }) : el("code", { text: forms[1] })),
        el("div", null, el("span", { class: "il-k", text: "other 칸" }), el("code", { text: forms[5] })));
      const { args, skipped } = readArgs();
      const d = {};
      M.format(lang, key, args, d);
      fillOut.textContent = "";
      const why = d.reason === "count"
        ? ["count = " + M.findArgument(args, "count").value.toString() + " (정수) → plural_category_for(" + lang.plural + ", …) = " + d.category + " → " + d.category + " 칸"]
        : d.reason === "text" ? ["count가 글이다 → other 칸"] : ["count 인자가 없다 → other 칸"];
      if (d.usedIndex !== d.formIndex) why.push("고른 칸이 비어 other 칸으로 물러선다");
      if (skipped.length) why.push("int64가 아닌 정수 칸은 넘기지 않았다: " + skipped.map(showTag).join(", "));
      const out = el("div", { class: "il-out" }, ...d.segments.map((s) => (s.kind === "lit" ? document.createTextNode(s.text) : el("span", { class: s.kind === "arg" ? "il-a" : "il-m", title: s.kind === "arg" ? "인자 값" : "인자가 없어 그대로 남은 자리", text: s.text }))));
      if (!d.segments.length) out.append(el("span", { class: "muted", text: "(빈 글)" }));
      fillOut.append(
        el("p", { class: "il-why", text: why.join(" · ") }),
        out,
        el("p", { class: "muted il-legend" }, el("span", { class: "il-a", text: "인자 값" }), " ", el("span", { class: "il-m", text: "{없는 이름}" }), " 나머지는 문장의 글자 그대로"));
    }

    function drawPlural() {
      ruleSeg.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", b.dataset.v === S.rule ? "true" : "false"));
      const n = M.parseInt64(countInput.value);
      pluralOut.textContent = "";
      if (n === null) {
        pluralOut.append(el("p", null, el("b", { text: "int64가 아니다" }), el("span", { class: "muted", text: " — 이 칸을 정수로 읽는 것은 이 문서의 입력 처리다. luil에는 글을 수로 읽는 일이 없다." })));
        return;
      }
      pluralOut.append(el("p", null, el("code", { text: "plural_category_for(plural_rule::" + S.rule + ", " + n.toString() + ")" }), " → ", el("b", { text: M.pluralCategoryFor(S.rule, n) })));
      const other = S.rule === "one" ? "other" : "one";
      pluralOut.append(el("p", { class: "muted", text: "plural_rule::" + other + "이면 " + M.pluralCategoryFor(other, n) + ". one 규칙은 1과 -1만 one이다 — 절댓값을 계산하지 않고 두 값과 견준다(INT64_MIN의 부호를 바꾸면 넘치기 때문이다)." }));
    }

    // ---------- 이벤트 ----------
    prefInput.addEventListener("input", drawPick);
    prefChips.addEventListener("click", (e) => { const b = e.target.closest("button[data-p]"); if (b) { prefInput.value = b.dataset.p; drawPick(); } });
    keySelect.addEventListener("change", () => { setArgs(PRESET_ARGS[keySelect.value] || []); drawFill(); });
    addArg.addEventListener("click", () => { argRows.append(argRow("", "s", "")); drawFill(); });
    ruleSeg.addEventListener("click", (e) => { const b = e.target.closest("button[data-v]"); if (b) { S.rule = b.dataset.v; drawPlural(); } });
    countInput.addEventListener("input", drawPlural);
    countChips.addEventListener("click", (e) => { const b = e.target.closest("button[data-c]"); if (b) { countInput.value = b.dataset.c; drawPlural(); } });

    function reset() {
      prefInput.value = INITIAL.prefs;
      keySelect.value = INITIAL.key;
      setArgs(PRESET_ARGS[INITIAL.key]);
      S.rule = INITIAL.rule;
      countInput.value = INITIAL.count;
      drawPick();
      drawPlural();
    }
    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilI18nLab = { setup };
})();

/* luil 설명서 — 텍스트 입력 실습 (숫자 칸, 주소 찾기)
 *
 * 판정은 전부 assets/textinputmodel.js(원본 C++과 대조한 이식)를 부른다.
 *  - 숫자 칸: 넣는 조각은 number_input_filter로 거르고(insert), 조합이 끝나면 글 전체를 거른다(replace_all).
 *    ↑↓·PageUp/PageDown·스핀 단추는 stepped_number_input의 답으로 글을 통째로 바꾼다 (change_to → replace_all).
 *    끝에 닿았거나 조합 중이면 키를 삼키기만 한다 (text_input_element::step_number).
 *  - 주소 찾기: detect_text_links가 낸 구간(UTF-8 byte offset)을 글 위에 칠하고 표로 보인다.
 * 칸 자체(caret·선택·IME·그리기)는 브라우저의 입력 칸이다. luil의 렌더링이 아니다.
 */
(function () {
  "use strict";
  const M = window.LuilTextInput;
  const { el, icon } = window.LU;

  const PROBLEM_TEXT = {
    none: "문제 없음",
    empty: "빈 칸 (데모는 오류로 보지 않는다)",
    not_a_number: "숫자로 읽을 수 없다",
    too_precise: "소수 자릿수보다 자세하다",
    below_minimum: "최솟값보다 작다",
    above_maximum: "최댓값보다 크다",
  };
  const PRESETS = [
    { id: "demo", label: "데모 칸: 0–3600, 5씩", range: { minimum: 0, maximum: 3600, step: 5, page_step: 0, decimals: 0 } },
    { id: "signed", label: "부호·소수: −10–10, 0.5씩, 소수 1자리", range: { minimum: -10, maximum: 10, step: 0.5, page_step: 0, decimals: 1 } },
    { id: "open", label: "범위 없음: 1씩", range: { step: 1, page_step: 0, decimals: 0 } },
  ];
  const PIECES = ["1 200 원", "３０", "−1.5", "1..2", "12초", "4.25"];
  const STEP_KEYS = { ArrowUp: "increase", ArrowDown: "decrease", PageUp: "increase_page", PageDown: "decrease_page" };
  const STEP_LABEL = { increase: "↑", decrease: "↓", increase_page: "PageUp", decrease_page: "PageDown" };

  const quote = (s) => "“" + s + "”";
  const shown = (v) => (v === null ? "없음 (nullopt)" : Object.is(v, -0) ? "-0" : String(v));

  function setupNumber(root) {
    const body = root.querySelector(".lw-body");
    let range = Object.assign({}, PRESETS[0].range);
    let log = [];

    const preset = el("select", { "aria-label": "범위 고르기" }, PRESETS.map((p) => el("option", { value: p.id, text: p.label })));
    const fields = {};
    const rangeRow = el("div", { class: "ti-range" });
    for (const [key, label] of [["minimum", "minimum"], ["maximum", "maximum"], ["step", "step"], ["page_step", "page_step"], ["decimals", "decimals"]]) {
      const input = el("input", { type: "number", step: "any", "aria-label": "number_input_range::" + key });
      fields[key] = input;
      rangeRow.append(el("label", { class: "ti-rf" }, el("span", { text: label }), input));
      input.addEventListener("input", () => {
        readRange();
        update();
      });
    }

    const field = el("input", { type: "text", class: "ti-input", inputmode: "decimal", autocomplete: "off", spellcheck: "false", "aria-label": "숫자 칸 (luil 재현)", placeholder: "숫자" });
    const spin = el("div", { class: "ti-spin" });
    const up = el("button", { type: "button", tabindex: "-1", title: "늘리기 (value_step::increase)", "aria-label": "늘리기" }, icon("EAB6"));
    const down = el("button", { type: "button", tabindex: "-1", title: "줄이기 (value_step::decrease)", "aria-label": "줄이기" }, icon("EAB6"));
    spin.append(up, down);
    const box = el("div", { class: "ti-field" }, field, spin);
    const error = el("div", { class: "ti-error", "aria-live": "polite" });
    const state = el("dl", { class: "ti-state", "aria-label": "판정" });
    const logBox = el("div", { class: "ti-log", "aria-live": "polite", "aria-label": "메시지 기록" });
    const tries = el("div", { class: "ti-tries" }, el("span", { class: "ti-k", text: "붙여 넣어 보기" }));
    for (const piece of PIECES) tries.append(el("button", { type: "button", class: "btn sm", text: piece, onclick: () => insertPiece(piece, "붙여넣기") }));

    body.append(
      el("div", { class: "ti-cols" },
        el("div", { class: "ti-side" },
          el("div", { class: "ti-row" }, el("span", { class: "ti-k", text: "범위" }), preset),
          rangeRow,
          el("div", { class: "ti-fieldwrap" }, el("span", { class: "ti-k", text: "숫자 칸 — 치고, 붙이고, ↑↓·PageUp/PageDown" }), box, error),
          tries
        ),
        el("div", { class: "ti-panel" }, state, logBox)
      )
    );

    function readRange() {
      const next = {};
      for (const key of Object.keys(fields)) {
        const raw = fields[key].value.trim();
        if (raw === "") continue;
        const v = Number(raw);
        if (Number.isFinite(v)) next[key] = key === "decimals" ? Math.trunc(v) : v;
      }
      range = next;
    }
    function writeRange() {
      for (const key of Object.keys(fields)) fields[key].value = range[key] === undefined ? "" : String(range[key]);
    }

    function push(entry) {
      log.unshift(entry);
      log = log.slice(0, 8);
    }

    function insertPiece(piece, how) {
      const filter = M.numberInputFilter(range);
      const kept = filter(piece);
      const start = field.selectionStart === null ? field.value.length : field.selectionStart;
      const end = field.selectionEnd === null ? field.value.length : field.selectionEnd;
      // text_edit_insert는 빈 조각이면 아무것도 하지 않는다 — 고른 글도 그대로 남는다
      if (kept !== "") field.setRangeText(kept, start, end, "end");
      const eff = kept === "" ? "거른 조각이 비었다. 글도 선택도 그대로다" : kept === piece ? "그대로 들어간다" : "거른 조각만 들어간다";
      push({ input: how + " " + quote(piece), msg: "insert " + quote(piece) + " → 거르기 → " + quote(kept), eff });
      update();
    }

    function step(which, how) {
      const next = M.steppedNumberInput(field.value, range, which);
      if (next === null) {
        push({ input: how, msg: "stepped_number_input(…, " + which + ") = nullopt", eff: "끝에 닿았다. 키는 삼키고 메시지는 없다" });
      } else {
        const before = field.value;
        field.value = next;
        field.setSelectionRange(next.length, next.length);
        push({ input: how, msg: "change_to(" + quote(next) + ")", eff: "앱이 replace_all로 " + quote(before) + " → " + quote(next) + ", caret은 끝" });
      }
      update();
    }

    field.addEventListener("beforeinput", (e) => {
      if (e.isComposing) return;
      const types = ["insertText", "insertFromPaste", "insertFromDrop", "insertReplacementText"];
      if (!types.includes(e.inputType)) return;
      const piece = e.data != null ? e.data : e.dataTransfer ? e.dataTransfer.getData("text/plain") : "";
      e.preventDefault();
      insertPiece(piece, e.inputType === "insertText" ? "타자" : e.inputType === "insertFromPaste" ? "붙여넣기" : "넣기");
    });
    field.addEventListener("compositionend", () => {
      // Windows·Android·iOS의 IME 확정은 글 전체를 실은 replace_all이다 — 거르기가 글 전체를 본다
      const whole = field.value;
      const kept = M.numberInputFilter(range)(whole);
      const caret = Math.min(field.selectionStart === null ? kept.length : field.selectionStart, kept.length);
      field.value = kept;
      field.setSelectionRange(caret, caret);
      push({ input: "IME 확정", msg: "replace_all " + quote(whole) + " → 거르기 → " + quote(kept), eff: "확정 글 전체를 거른다" });
      update();
    });
    field.addEventListener("input", (e) => {
      if (!e.isComposing) update();
    });
    field.addEventListener("keydown", (e) => {
      const which = STEP_KEYS[e.key];
      if (!which || e.ctrlKey || e.altKey || e.metaKey) return;
      e.preventDefault();
      if (e.isComposing) {
        push({ input: STEP_LABEL[which], msg: "조합 중의 걸음", eff: "삼킨다 (확정되지 않은 글을 바꾸지 않는다)" });
        update();
        return;
      }
      step(which, STEP_LABEL[which] + " 키");
    });
    up.addEventListener("mousedown", (e) => e.preventDefault());
    down.addEventListener("mousedown", (e) => e.preventDefault());
    up.addEventListener("click", () => step("increase", "스핀 단추 늘리기"));
    down.addEventListener("click", () => step("decrease", "스핀 단추 줄이기"));
    preset.addEventListener("change", () => {
      const p = PRESETS.find((x) => x.id === preset.value);
      range = Object.assign({}, p.range);
      writeRange();
      update();
    });

    function update() {
      const check = M.checkNumberInput(field.value, range);
      const bad = check.problem !== "none" && check.problem !== "empty";
      box.classList.toggle("invalid", bad);
      field.setAttribute("aria-invalid", bad ? "true" : "false");
      error.textContent = bad ? check.problem + " — " + PROBLEM_TEXT[check.problem] : "";
      const signedValues = range.minimum !== undefined && range.minimum < 0;
      const fractions = (range.decimals || 0) > 0;
      const keeps = ["숫자"].concat(fractions ? ["마침표"] : [], signedValues ? ["부호 (− +)"] : []).join(" · ");
      const nexts = ["increase", "decrease", "increase_page", "decrease_page"].map((w) => {
        const n = M.steppedNumberInput(field.value, range, w);
        return STEP_LABEL[w] + " " + (n === null ? "끝 (nullopt)" : quote(n));
      });
      state.replaceChildren(
        el("dt", { text: "check_number_input" }), el("dd", {}, el("code", { text: check.problem }), " · ", PROBLEM_TEXT[check.problem]),
        el("dt", { text: "value" }), el("dd", { text: shown(check.value) }),
        el("dt", { text: "number_input_filter" }), el("dd", { text: "남기는 글자: " + keeps + " (전각은 ASCII로)" }),
        el("dt", { text: "stepped_number_input" }), el("dd", { text: nexts.join(" · ") })
      );
      logBox.replaceChildren(
        ...(log.length
          ? log.map((x) => el("div", { class: "ti-entry" }, el("span", { class: "in", text: x.input }), el("span", { class: "msg", text: x.msg }), el("span", { class: "eff", text: x.eff })))
          : [el("div", { class: "ti-empty", text: "칸에 치거나 붙여 넣으면 칸이 낸 요청과 앱이 한 일이 여기에 쌓인다." })])
      );
    }

    function reset() {
      preset.value = PRESETS[0].id;
      range = Object.assign({}, PRESETS[0].range);
      writeRange();
      field.value = "1200";
      log = [];
      update();
    }
    root.addEventListener("lw-reset", reset);
    reset();
  }

  const SAMPLE =
    "설명서는 https://example.com/luil/docs 에 있고, 문의는 hello@example.com 으로 보낸다.\n" +
    "www.example.org 도 있다. 괄호 속 주소 (https://a.b/x_(y)) 와 문장 끝 주소 http://example.com/a.\n" +
    "조사가 붙은 https://example.com에서, 낱말 속 xhttp://no.link, mailto:team@example.com, 퍼센트 꼴 https://example.com/%ED%95%9C";

  // UTF-8 byte offset → JS 문자열 자리
  function byteIndex(text) {
    const enc = new TextEncoder();
    const map = new Map();
    let bytes = 0;
    let index = 0;
    for (const ch of text) {
      map.set(bytes, index);
      bytes += enc.encode(ch).length;
      index += ch.length;
    }
    map.set(bytes, index);
    return { map, bytes };
  }

  function setupLinks(root) {
    const body = root.querySelector(".lw-body");
    const area = el("textarea", { class: "ti-text", rows: "4", spellcheck: "false", "aria-label": "주소를 찾을 글" });
    const view = el("div", { class: "ti-view", "aria-label": "찾은 링크를 칠한 글" });
    const count = el("div", { class: "ti-count" });
    const table = el("tbody");
    body.append(
      el("div", { class: "ti-fieldwrap" }, el("span", { class: "ti-k", text: "글 — 고쳐 보면 바로 다시 찾는다" }), area),
      el("div", { class: "ti-fieldwrap" }, el("span", { class: "ti-k", text: "detect_text_links가 낸 구간" }), view, count),
      el("div", { class: "table-wrap" },
        el("table", { class: "t ti-links" },
          el("thead", {}, el("tr", {}, el("th", { text: "byte [begin, end)" }), el("th", { text: "보이는 글" }), el("th", { text: "destination" }))),
          table
        )
      )
    );

    function update() {
      const text = area.value;
      const links = M.detectTextLinks(text);
      const { map, bytes } = byteIndex(text);
      const parts = [];
      let at = 0;
      for (const link of links) {
        const b = map.get(link.begin);
        const e = map.get(link.end);
        if (b === undefined || e === undefined) continue;
        if (b > at) parts.push(document.createTextNode(text.slice(at, b)));
        parts.push(el("span", { class: "ti-link", title: link.destination, text: text.slice(b, e) }));
        at = e;
      }
      if (at < text.length) parts.push(document.createTextNode(text.slice(at)));
      view.replaceChildren(...parts);
      count.textContent = "링크 " + links.length + "개 · 글 " + text.length + " UTF-16 단위 = " + bytes + " UTF-8 byte";
      table.replaceChildren(
        ...(links.length
          ? links.map((link) => el("tr", {},
              el("td", { class: "ti-num" }, link.begin + " – " + link.end),
              el("td", {}, el("code", { class: "free", text: text.slice(map.get(link.begin), map.get(link.end)) })),
              el("td", {}, el("code", { class: "free", text: link.destination }))
            ))
          : [el("tr", {}, el("td", { colspan: "3", class: "ti-none", text: "찾은 주소가 없다." }))])
      );
    }
    area.addEventListener("input", update);
    function reset() {
      area.value = SAMPLE;
      update();
    }
    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilTextInputLab = { setupNumber, setupLinks };
})();

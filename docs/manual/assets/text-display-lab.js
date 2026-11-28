/* luil 설명서 — 글 표시와 글꼴: 글 상자 실습
 *
 * label_element 하나를 칸(slot)에 놓아 본다. 줄 바꿈·줄 수·말줄임·정렬의 판정은 전부 assets/textblockmodel.js
 * (원본 C++과 대조한 이식)가 한다. 이 파일은 조작과 그림뿐이다.
 *  - 글자 크기는 10 논리 픽셀로 고정이다 (단순 측정기의 폭이 정수 6·10이 되게).
 *  - 칸 = label의 bounds, 점선 = 좌우 padding을 뺀 글 상자, 줄 상자 = 그린 줄 하나.
 *  - 그림은 같은 치수로 그린 재현이지 luil의 실제 렌더링이 아니다. baseline은 그리지 않는다.
 */
(function () {
  "use strict";
  const TB = window.LuilTextBlock;
  const LB = window.LuilLineBreak;
  const { el, icon } = window.LU;
  const seg = typeof Intl !== "undefined" && Intl.Segmenter ? new Intl.Segmenter("ko", { granularity: "grapheme" }) : null;
  const graphemesOf = (s) => (seg ? Array.from(seg.segment(s), (x) => x.segment) : Array.from(s));

  const FONT = 10; // label_config::font_size (실습 고정)
  const MAX_WIDTH = 240;
  const MARGIN = 14; // 칸 둘레의 여백 (논리 픽셀)

  const PRESETS = [
    { name: "어절", text: "설정을 저장했습니다. 다시 시도하려면 아래 단추를 누르세요.", width: 130, height: 60, wrap: true, max: 0, align: "start", elide: "end", policy: "" },
    { name: "경로", text: "C:/Users/yul/Documents/luil/src/ui/text_block.cpp", width: 150, height: 17, wrap: false, max: 0, align: "start", elide: "start", policy: "" },
    { name: "끝 정렬", text: "Save your changes before closing the window", width: 110, height: 60, wrap: true, max: 0, align: "end", elide: "end", policy: "" },
    { name: "두 줄까지", text: "긴 설명은 두 줄까지만 보이고 나머지는 마지막 줄 끝에서 줄인다.", width: 120, height: 34, wrap: true, max: 2, align: "start", elide: "end", policy: "" },
    { name: "줄바꿈", text: "첫 줄\n둘째 줄\n\n넷째 줄", width: 160, height: 17, wrap: false, max: 0, align: "start", elide: "end", policy: "" },
    { name: "이모지", text: "회의 👩‍💻 일정 🇰🇷 공유하기", width: 70, height: 17, wrap: false, max: 0, align: "center", elide: "middle", policy: "" },
    { name: "낮은 칸", text: "칸이 줄 수보다 낮으면 label은 자르지 않고 칸 밖으로 그린다.", width: 110, height: 24, wrap: true, max: 0, align: "start", elide: "end", policy: "" },
  ];
  const INITIAL = Object.assign({ padding: 0 }, PRESETS[0]);

  function setup(root) {
    if (!root || !TB || !LB) return;
    const body = root.querySelector(".lw-body");
    let state = Object.assign({}, INITIAL);

    // ---- 조작 ----
    const text = el("textarea", { rows: "2", spellcheck: "false", "aria-label": "label_config::text" });
    const presets = el("div", { class: "chips", "aria-label": "견본" });
    PRESETS.forEach((p) => presets.append(el("button", { type: "button", class: "chip", onclick: () => { state = Object.assign({ padding: state.padding }, p); sync(); } }, p.name)));

    const range = (label, key, min, max, title) => {
      const input = el("input", { type: "range", min: String(min), max: String(max), step: "1", "aria-label": label, title: title || null });
      const out = el("output");
      input.addEventListener("input", () => { state[key] = Number(input.value); render(); });
      return { node: el("label", { class: "field" }, el("span", { text: label }), input, out), input, out, key };
    };
    const ranges = [
      range("칸 폭", "width", 4, MAX_WIDTH, "label의 bounds 폭 (논리 픽셀)"),
      range("칸 높이", "height", 8, 120, "label의 bounds 높이 (논리 픽셀)"),
      range("padding", "padding", 0, 16, "label_config::padding — 좌우만 들인다"),
    ];
    const fit = el("button", { type: "button", class: "chip", title: "칸 높이를 preferred_size의 높이로 — wrap이면 max(measure_wrap 높이, height_for), 한 줄이면 height_for", onclick: () => { state.height = preferredHeight(); render(); } }, "높이 맞추기");

    const segOf = (label, key, values) => {
      const group = el("div", { class: "seg", role: "group", "aria-label": label });
      values.forEach(([v, t]) => group.append(el("button", { type: "button", "data-v": String(v), "aria-pressed": "false", onclick: () => { state[key] = v; render(); } }, t)));
      return { node: group, key };
    };
    const wrap = el("input", { type: "checkbox", "aria-label": "label_config::wrap" });
    wrap.addEventListener("change", () => { state.wrap = wrap.checked; render(); });
    const segs = [
      segOf("max_lines", "max", [[0, "0"], [1, "1"], [2, "2"], [3, "3"]]),
      segOf("elide", "elide", [["end", "end"], ["middle", "middle"], ["start", "start"]]),
      segOf("align", "align", [["start", "start"], ["center", "center"], ["end", "end"]]),
      segOf("line_break", "policy", [["", "빔 (스타일)"], ["hangul_words", "hangul_words"], ["unicode", "unicode"]]),
    ];
    const labelled = (name, node) => el("span", { class: "tb-opt" }, el("code", { text: name }), node);

    const stage = el("div", { class: "tb-stage", role: "img" });
    const readout = el("div", { class: "readout tb-read", "aria-live": "polite" });
    const notes = el("ul", { class: "tb-notes" });

    body.append(
      el("label", { class: "field wide" }, el("span", { text: "글" }), text),
      presets,
      el("div", { class: "controls" }, ranges.map((r) => r.node), fit),
      el("div", { class: "controls" },
        el("label", { class: "field" }, wrap, el("code", { text: "wrap" })),
        labelled("max_lines", segs[0].node),
        labelled("elide", segs[1].node),
        labelled("align", segs[2].node),
        labelled("line_break", segs[3].node)),
      stage,
      readout,
      notes
    );
    text.addEventListener("input", () => { state.text = text.value; render(); });
    root.addEventListener("lw-reset", () => { state = Object.assign({}, INITIAL); sync(); });
    let resizeTimer = 0;
    window.addEventListener("resize", () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(render, 120); });

    const config = () => ({
      text: state.text, font_size: FONT, padding: state.padding, wrap: state.wrap, max_lines: state.max,
      elide: state.elide, align: state.align, line_break: state.policy || null,
    });
    const slot = () => ({ x: 0, y: 0, width: state.width, height: state.height });
    const ctx = { policy: "hangul_words", shaping: false };

    // label_element::preferred_size의 높이 — 칸 폭을 받은 폭으로 준다
    const preferredHeight = () => TB.preferredSize(config(), ctx, state.width).height;

    function sync() {
      text.value = state.text;
      render();
    }

    function render() {
      ranges.forEach((r) => { r.input.value = String(state[r.key]); r.out.textContent = String(state[r.key]); });
      wrap.checked = state.wrap;
      segs.forEach((s) => s.node.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.v === String(state[s.key])))));
      segs[0].node.classList.toggle("off", !state.wrap);
      segs[1].node.classList.toggle("off", state.wrap);

      const c = config();
      const s = slot();
      const r = TB.simulateLabel(c, s, ctx);
      const lineHeight = r.lineHeight;

      // 배율: 칸 폭의 최댓값이 무대에 들어가게 (1.1~2배)
      const avail = stage.clientWidth || body.clientWidth || 640;
      const k = Math.max(1.1, Math.min(2, (avail - 4) / (MAX_WIDTH + MARGIN * 2)));
      const px = (v) => (v * k).toFixed(2) + "px";

      // 그린 것 전체의 자리 (칸 밖으로 나간 줄도 보이게)
      let minX = 0;
      let maxX = s.width;
      let maxY = s.height;
      r.drawn.forEach((d) => {
        minX = Math.min(minX, d.x);
        maxX = Math.max(maxX, d.x + d.width);
        maxY = Math.max(maxY, d.top + lineHeight);
      });
      const ox = MARGIN - minX;
      const oy = MARGIN;

      const canvas = el("div", { class: "tb-canvas", style: { width: px(maxX - minX + MARGIN * 2), height: px(maxY + MARGIN * 2), fontSize: px(FONT) } });
      canvas.append(el("div", { class: "tb-slot", style: { left: px(ox), top: px(oy), width: px(s.width), height: px(s.height) } }));
      canvas.append(el("div", { class: "tb-tbox", style: { left: px(ox + r.textBox.x), top: px(oy), width: px(Math.max(0, r.textBox.width)), height: px(s.height) } }));
      const eps = 1e-6;
      // 보이는 폭: 줄 끝의 매단 공백(U+0020·U+3000 등)을 뺀 폭 — 칸 밖으로 나갔는지는 이것으로 본다
      const visibleWidth = (d) => TB.measureText(d.text.replace(/[ \u1680\u2000-\u2006\u2008-\u200A\u205F\u3000]+$/, ""), FONT);
      const outside = (d) => d.x < r.textBox.x - eps || d.x + visibleWidth(d) > r.textBox.x + r.textBox.width + eps;
      r.drawn.forEach((d) => {
        const out = d.top + lineHeight > s.height + eps || d.top < -eps || outside(d);
        const line = el("div", { class: "tb-line" + (out ? " out" : ""), style: { left: px(ox + d.x), top: px(oy + d.top), height: px(lineHeight) } });
        for (const g of graphemesOf(TB.singleLineText(d.text))) {
          const one = LB.byteLength(g) === 1;
          const cls = "tb-c" + (one ? " a" : "") + (g === " " ? " sp" : "") + (g === TB.ELLIPSIS ? " el" : "");
          line.append(el("span", { class: cls, style: { width: px(one ? 6 : 10) } }, g === " " ? "·" : g));
        }
        canvas.append(line);
      });
      stage.replaceChildren(canvas);
      stage.setAttribute("aria-label", r.drawn.length ? "그린 줄: " + r.drawn.map((d) => d.text).join(" / ") : "그린 글이 없다");

      // ---- 읽기 ----
      const m = r.measured;
      const lines = el("ol", { class: "tb-lines" });
      r.drawn.forEach((d) => {
        const range = r.wrapped ? r.wrapped.lines[d.line] : { begin: 0, end: LB.byteLength(state.text) };
        lines.append(el("li", null, `[${range.begin}, ${range.end})  x ${fmt(d.x)}  "${TB.singleLineText(d.text)}"`));
      });
      const pref = TB.preferredSize(c, ctx, s.width);
      readout.replaceChildren(
        el("div", null, el("b", { text: "measure_wrap" }), `  줄 ${m.lines.length} · truncated ${m.truncated} · width ${fmt(m.width)} · height ${fmt(m.height)}`),
        el("div", null, el("b", { text: "preferred_size" }), `  ${fmt(pref.width)} × ${fmt(pref.height)}   `, el("b", { text: "height_for" }), `  ${fmt(r.heightFor)}   `, el("b", { text: "글 상자" }), `  폭 ${fmt(r.textBox.width)}`),
        lines
      );

      // ---- 코드가 정한 사실 가운데 지금 보이는 것 ----
      const facts = [];
      if (!state.text.length) facts.push("글이 비면 label은 아무것도 그리지 않는다 (바탕도).");
      if (state.text.length && !r.drawn.length) {
        const elidedAway = (!state.wrap || m.truncated) && r.textBox.width < TB.measureText(TB.ELLIPSIS, FONT);
        facts.push(elidedAway ? "글 상자가 \"…\"보다 좁다. 말줄임은 \"…\"조차 들어가지 않으면 빈 글이라 아무것도 그리지 않는다." : "줄바꿈뿐이라 그릴 글자가 없다.");
      }
      if (state.wrap && m.height > s.height + 1e-6) facts.push(`줄 높이의 합(${fmt(m.height)})이 칸 높이(${s.height})보다 크다. label은 자르지 않으므로 아래 줄이 칸 밖에 그려진다 — 이웃을 덮는다.`);
      if (!state.wrap && /[\r\n]/.test(state.text)) facts.push("한 줄 label이라 줄바꿈을 공백 하나로 그리고 잰다 (single_line_break::space).");
      if (state.align !== "start" && r.drawn.some((d) => visibleWidth(d) < d.width - eps))
        facts.push("정렬은 줄 끝 공백까지 잰 폭으로 한다 — 접힌 줄 끝에 매단 공백도 들어간다. 그래서 그 줄의 글은 공백만큼(가운데 정렬은 절반) 안쪽에 서고, 공백까지 칸보다 넓으면 상자 왼쪽으로 나간다.");
      if (r.drawn.some((d) => visibleWidth(d) > r.textBox.width + eps))
        facts.push("후보가 없는 줄은 글자소 경계에서 자르되, 글자소 하나는 상자보다 넓어도 남긴다. 상자를 자르지 않는다.");
      if (state.wrap && m.truncated) facts.push("max_lines를 넘겨, 마지막 보이는 줄이 남은 글 전체를 한 줄로 펴서 끝을 줄였다 (elide 설정과 상관없이 끝이다).");
      notes.replaceChildren(...facts.map((f) => el("li", null, icon("EA74"), el("span", { text: f }))));
    }

    const fmt = (v) => (Math.round(v * 100) / 100).toString();
    sync();
  }

  window.LuilTextDisplayLab = { setup };
})();

/* luil 설명서 — 배치 요소 실습
 *
 * 세 실험판이다. 판정은 전부 assets/containermodel.js(원본 C++과 대조한 이식)를 부른다.
 *  - 격자 열 계산기: grid_element의 열 폭 (resolve_tracks + distribute_flexible), 맞춤 자리에서 답하는 원하는 폭
 *  - 줄바꿈 칸 수: wrap_columns_for · wrap_height_for와 arrange가 칸을 놓는 식
 *  - 창 폭 등급: make_window_metrics의 배율 다듬기 + classify_window_width
 * 그림은 luil의 실제 렌더링이 아니라 같은 값으로 그린 재현이다. 색은 CSS 토큰(클래스)으로만 칠한다.
 */
(function () {
  "use strict";
  const M = window.LuilContainers;
  const { el } = window.LU;

  const num = (v, fallback) => {
    const n = parseFloat(v);
    return Number.isFinite(n) ? n : fallback;
  };
  const fmt = (v) => (Number.isFinite(v) ? String(Math.round(v * 100) / 100) : v > 0 ? "∞" : "−∞");

  function pressPreset(group, value) {
    for (const b of group.querySelectorAll("button")) b.setAttribute("aria-pressed", String(b.dataset.v === value));
  }

  function observeWidth(node, fn) {
    if (window.ResizeObserver) new ResizeObserver(() => fn()).observe(node);
    else window.addEventListener("resize", fn);
  }

  /* ---------- 격자 열 계산기 ---------- */

  const KINDS = ["fixed", "fit", "flexible"];
  const track = (kind, o) => Object.assign({ kind, length: 100, weight: 1, minimum: 0, content: 0 }, o || {});
  const GRID_PRESETS = {
    // tests/content_layout_tests.cpp:200-232 — "Address" 일곱 자 × 12px의 절반 = 42
    form: { width: 300, spacing: 8, tracks: [track("fit", { content: 42 }), track("flexible")] },
    // tests/content_layout_tests.cpp:271-281 — 1:3을 하한 200이 200·200으로
    minimum: { width: 400, spacing: 0, tracks: [track("flexible", { weight: 1, minimum: 200 }), track("flexible", { weight: 3 })] },
    // tests/content_layout_tests.cpp:283-288 — 하한만으로 모자라면 그때만 넘친다
    tight: { width: 400, spacing: 0, tracks: [track("flexible", { weight: 1, minimum: 200 }), track("flexible", { weight: 1, minimum: 300 })] },
    // examples/demo/gallery_page.cpp:313-316 — 폭 720부터 3열, 간격 12
    cards: { width: 720, spacing: 12, tracks: [track("flexible"), track("flexible"), track("flexible")] },
    mixed: { width: 520, spacing: 8, tracks: [track("fixed", { length: 96 }), track("fit", { content: 60, minimum: 80 }), track("flexible", { weight: 2 }), track("flexible", { weight: 1, minimum: 120, content: 90 })] },
  };

  function gridLab(root) {
    if (!root || !M) return;
    const $ = (id) => root.querySelector("#" + id);
    const preset = $("gl-preset");
    const widthIn = $("gl-width");
    const widthOut = $("gl-width-out");
    const spacingIn = $("gl-spacing");
    const spacingOut = $("gl-spacing-out");
    const table = $("gl-tracks");
    const stage = $("gl-stage");
    const out = $("gl-out");
    let state = null;
    let cells = [];

    function load(name) {
      const p = GRID_PRESETS[name];
      state = { width: p.width, spacing: p.spacing, tracks: p.tracks.map((t) => Object.assign({}, t)) };
      widthIn.value = state.width;
      spacingIn.value = state.spacing;
      pressPreset(preset, name);
      buildTable();
      render();
    }

    function custom() {
      pressPreset(preset, "");
    }

    function buildTable() {
      table.textContent = "";
      cells = [];
      table.append(
        el("thead", null, el("tr", null, el("th", { text: "열" }), el("th", { text: "규칙" }), el("th", { text: "길이 (fixed)" }), el("th", { text: "비율 (flexible)" }), el("th", { text: "하한" }), el("th", { text: "내용 폭" }), el("th", { text: "열 폭" })))
      );
      const body = el("tbody");
      state.tracks.forEach((t, i) => {
        const label = `열 ${i + 1}`;
        const kind = el("select", { "aria-label": `${label} 규칙` }, ...KINDS.map((k) => el("option", { value: k, text: k, selected: t.kind === k })));
        const field = (key, min, step, aria) => el("input", { type: "number", min, max: "1000", step, value: String(t[key]), "aria-label": `${label} ${aria}`, "data-key": key });
        const length = field("length", "0", "1", "길이");
        const weight = field("weight", "-2", "0.5", "비율");
        const minimum = field("minimum", "0", "1", "하한");
        const content = field("content", "0", "1", "내용 폭");
        const result = el("output", { "aria-label": `${label} 열 폭` });
        kind.addEventListener("change", () => {
          t.kind = kind.value;
          custom();
          render();
        });
        for (const input of [length, weight, minimum, content])
          input.addEventListener("input", () => {
            t[input.dataset.key] = num(input.value, 0);
            custom();
            render();
          });
        body.append(el("tr", { class: "gl-track-row" }, el("td", { text: String(i + 1) }), el("td", null, kind), el("td", null, length), el("td", null, weight), el("td", null, minimum), el("td", null, content), el("td", null, result)));
        cells.push({ length, weight, minimum, content, result });
      });
      table.append(body);
    }

    function render() {
      state.width = num(widthIn.value, 0);
      state.spacing = num(spacingIn.value, 0);
      widthOut.textContent = fmt(state.width);
      spacingOut.textContent = fmt(state.spacing);
      const tracks = state.tracks.map((t) => ({ kind: t.kind, length: t.length, weight: t.weight, minimum: t.minimum }));
      const wanted = state.tracks.map((t) => t.content);
      const lengths = M.resolveTracks(tracks, wanted, state.width, state.spacing);
      const starts = M.trackStarts(lengths, state.spacing);
      const sum = M.sumWithSpacing(lengths, state.spacing);
      const preferred = M.sumWithSpacing(M.resolveTracks(tracks, wanted, Infinity, state.spacing), state.spacing);
      const atMinimum = state.tracks.map((t, i) => t.kind === "flexible" && t.minimum > 0 && lengths[i] === Math.fround(t.minimum));

      state.tracks.forEach((t, i) => {
        const c = cells[i];
        c.length.disabled = t.kind !== "fixed";
        c.weight.disabled = t.kind !== "flexible";
        c.minimum.disabled = t.kind === "fixed";
        c.content.disabled = t.kind === "fixed";
        c.result.textContent = fmt(lengths[i]) + (atMinimum[i] ? " (하한)" : "");
      });

      // 그림 — 안쪽 폭을 점선 상자로, 열을 그 안의 막대로 (넘친 열은 상자 밖으로 나간다)
      stage.textContent = "";
      const inner = Math.max(1, stage.clientWidth - 16);
      const extent = Math.max(state.width, sum, 1);
      const k = inner / extent;
      stage.append(el("div", { class: "gl-box", style: { left: "8px", width: Math.max(0, state.width * k) + "px" } }));
      lengths.forEach((len, i) => {
        const w = len * k;
        const over = starts[i] + len > state.width + 1e-4;
        const cls = "gl-bar " + (atMinimum[i] ? "min" : state.tracks[i].kind) + (over ? " over" : "");
        stage.append(el("div", { class: cls, style: { left: 8 + starts[i] * k + "px", width: Math.max(1, w) + "px" }, text: w > 38 ? `${i + 1} · ${fmt(len)}` : w > 14 ? String(i + 1) : "" }));
      });
      const ruler = el("div", { class: "gl-ruler" });
      ruler.append(el("span", { style: { left: "8px", transform: "none" }, text: "0" }));
      const edge = 8 + state.width * k;
      ruler.append(el("span", { style: { left: edge + "px", transform: edge > inner - 16 ? "translateX(-100%)" : edge < 30 ? "none" : "translateX(-50%)" }, text: fmt(state.width) }));
      if (sum > state.width + 1e-4 && (sum - state.width) * k > 30) ruler.append(el("span", { style: { left: Math.min(inner + 8, 8 + sum * k) + "px", transform: "translateX(-100%)" }, text: fmt(sum) }));
      stage.append(ruler);

      out.textContent = "";
      const free = state.width - sum;
      out.append(
        el("div", null, "열 폭: ", lengths.length ? lengths.map(fmt).join(" · ") : "(열 없음)"),
        el("div", null, `간격까지 더한 합 ${fmt(sum)} / 안쪽 폭 ${fmt(state.width)} → `, free < -1e-4 ? el("span", { class: "bad", text: `${fmt(-free)} 넘친다 (grid는 자르지 않는다)` }) : `남는 자리 ${fmt(Math.max(0, free))}`),
        el("div", null, `add_fit으로 담으면 답하는 원하는 폭: ${fmt(preferred)} — 유연 열도 내용 폭(하한 이상)으로 잰다`)
      );
    }

    preset.addEventListener("click", (e) => {
      const b = e.target.closest("button[data-v]");
      if (b) load(b.dataset.v);
    });
    widthIn.addEventListener("input", () => {
      custom();
      render();
    });
    spacingIn.addEventListener("input", () => {
      custom();
      render();
    });
    $("gl-add").addEventListener("click", () => {
      if (state.tracks.length >= 6) return;
      state.tracks.push(track("flexible"));
      custom();
      buildTable();
      render();
    });
    $("gl-remove").addEventListener("click", () => {
      if (state.tracks.length <= 1) return;
      state.tracks.pop();
      custom();
      buildTable();
      render();
    });
    root.addEventListener("lw-reset", () => load("form"));
    observeWidth(stage, () => state && render());
    load("form");
  }

  /* ---------- 줄바꿈 칸 수 ---------- */

  const WRAP_PRESETS = {
    // examples/mobile_demo/theme_page.cpp:119-123
    swatch: { width: 320, count: 12, iw: 40, ih: 40, sp: 10, ls: 10 },
    // examples/mobile_demo/sheets_page.cpp:176-180 (touch_height 44)
    sheet: { width: 320, count: 6, iw: 120, ih: 44, sp: 8, ls: 8 },
    // tests/wrap_element_tests.cpp:19, 49-60
    test: { width: 100, count: 9, iw: 20, ih: 10, sp: 4, ls: 6 },
  };

  function wrapLab(root) {
    if (!root || !M) return;
    const $ = (id) => root.querySelector("#" + id);
    const preset = $("wl-preset");
    const inputs = { width: $("wl-width"), count: $("wl-count"), iw: $("wl-iw"), ih: $("wl-ih"), sp: $("wl-sp"), ls: $("wl-ls"), height: $("wl-height") };
    const auto = $("wl-auto");
    const stage = $("wl-stage");
    const out = $("wl-out");

    function load(name) {
      const p = WRAP_PRESETS[name];
      for (const [k, v] of Object.entries(p)) inputs[k].value = v;
      pressPreset(preset, name);
      render();
    }

    function render() {
      const width = num(inputs.width.value, 0);
      const count = Math.max(0, Math.round(num(inputs.count.value, 0)));
      const config = { item_width: num(inputs.iw.value, 0), item_height: num(inputs.ih.value, 0), spacing: num(inputs.sp.value, 0), line_spacing: num(inputs.ls.value, 0) };
      $("wl-width-out").textContent = fmt(width);
      $("wl-count-out").textContent = String(count);
      inputs.height.disabled = auto.checked;
      const columns = M.wrapColumnsFor(config, width);
      const height = M.wrapHeightFor(config, width, count);
      const rows = count === 0 ? 0 : Math.floor((count + columns - 1) / columns);
      const box = auto.checked ? height : Math.max(0, num(inputs.height.value, 0));
      const { slots } = M.wrapSlots(config, width, count, 1);

      stage.textContent = "";
      const maxRight = slots.reduce((m, s) => Math.max(m, s.x + s.width), 0);
      const maxBottom = slots.reduce((m, s) => Math.max(m, s.y + s.height), 0);
      const k = Math.min((stage.clientWidth - 16) / Math.max(width, maxRight, 1), (stage.clientHeight - 16) / Math.max(box, maxBottom, 1), 3);
      stage.append(el("div", { class: "wl-box", style: { width: Math.max(0, width * k) + "px", height: Math.max(0, box * k) + "px" } }));
      let overflowing = 0;
      slots.forEach((s, i) => {
        const over = s.x + s.width > width + 1e-4 || s.y + s.height > box + 1e-4;
        if (over) overflowing += 1;
        const w = s.width * k;
        const h = s.height * k;
        stage.append(el("div", { class: "wl-tile" + (over ? " over" : ""), style: { left: 8 + s.x * k + "px", top: 8 + s.y * k + "px", width: Math.max(1, w) + "px", height: Math.max(1, h) + "px" }, text: w > 14 && h > 10 ? String(i + 1) : "" }));
      });

      out.textContent = "";
      const step = config.item_width + config.spacing;
      const why = config.item_width <= 0 ? " — item_width가 0 이하라 나누지 않고 한 줄에 하나" : step <= 0 ? " — 걸음(item_width + spacing)이 0 이하라 한 줄에 하나" : "";
      out.append(
        el("div", null, `wrap_columns_for: 한 줄에 ${columns}개${why}`),
        el("div", null, `${count}개 → ${rows}줄, wrap_height_for = ${fmt(height)}`),
        el("div", null, auto.checked ? "칸 높이 = wrap_height_for (넘치지 않는다)" : overflowing ? el("span", { class: "bad", text: `칸 높이 ${fmt(box)} — 항목 ${overflowing}개가 slot 밖으로 넘친다` }) : `칸 높이 ${fmt(box)} — 모두 들어간다`)
      );
    }

    preset.addEventListener("click", (e) => {
      const b = e.target.closest("button[data-v]");
      if (b) load(b.dataset.v);
    });
    for (const input of Object.values(inputs))
      input.addEventListener("input", () => {
        pressPreset(preset, "");
        render();
      });
    auto.addEventListener("change", render);
    root.addEventListener("lw-reset", () => {
      auto.checked = true;
      inputs.height.value = 60;
      load("swatch");
    });
    observeWidth(stage, render);
    load("swatch");
  }

  /* ---------- 창 폭 등급 ---------- */

  // tests/window_metrics_tests.cpp:49-53, 72-76의 기기들
  const WIDTH_PRESETS = {
    portrait: { physical: 1080, scale: "2.8125" },
    landscape: { physical: 2316, scale: "2.8125" },
    fold: { physical: 1893, scale: "2.8125" },
    desktop: { physical: 1920, scale: "1.5" },
  };
  // include/luil/ui/layout_metrics.h:12-20의 주석
  const CLASS_TEXT = {
    compact: "휴대폰 세로, 좁게 나눈 화면이다. 한 판씩 보인다.",
    medium: "휴대폰 가로, 편 접는 기기, 작은 태블릿이다. 두 판이 들어간다.",
    expanded: "태블릿 가로, 데스크톱 창이다. 옆 판·옆 시트가 늘 설 자리가 있다.",
  };

  function widthLab(root) {
    if (!root || !M) return;
    const $ = (id) => root.querySelector("#" + id);
    const preset = $("wc-preset");
    const physical = $("wc-physical");
    const scale = $("wc-scale");
    const medium = $("wc-medium");
    const expanded = $("wc-expanded");
    const out = $("wc-out");
    const panes = $("wc-panes");

    function load(name) {
      const p = WIDTH_PRESETS[name];
      physical.value = p.physical;
      scale.value = p.scale;
      pressPreset(preset, name);
      render();
    }

    function pane(cls, text, width) {
      return el("div", { class: "wc-pane" + (cls ? " " + cls : ""), style: width != null ? { flex: `0 0 ${width}%` } : null, text });
    }

    function render() {
      const p = num(physical.value, 0);
      const s = num(scale.value, 1);
      const breakpoints = { medium: num(medium.value, 600), expanded: num(expanded.value, 840) };
      $("wc-physical-out").textContent = fmt(p);
      const ls = M.logicalScale(s);
      const logical = Math.fround(p / ls);
      const cls = M.classifyWindowWidth(logical, breakpoints);
      const standard = M.classifyWindowWidth(logical);
      out.textContent = "";
      const lines = [
        el("div", null, `논리 폭 = ${fmt(p)} ÷ ${String(ls)} = ${fmt(logical)}${s > 0 ? "" : " (배율을 모르면 1로 본다)"}`),
        el("div", { class: "wc-class" }, "classify_window_width → ", el("b", { text: cls })),
        el("div", { text: CLASS_TEXT[cls] }),
        el("div", null, `window_metrics::width_class (기본 경계 600·840) → ${standard}`),
      ];
      if (breakpoints.expanded < breakpoints.medium) lines.push(el("div", { class: "bad", text: "expanded 경계가 medium보다 작다 — 추측해 고치지 않고 expanded를 세우지 않는다." }));
      out.append(...lines);
      // 그림: "등급 경계 바꾸기" 예제의 갈래 — compact 한 판, medium [목록 280 | 상세], expanded [목록 280 | 상세 | 속성 320]
      panes.textContent = "";
      if (cls === "compact") panes.append(pane("page", "한 판 (목록 또는 상세)"));
      else {
        const fixed = cls === "expanded" ? 600 : 280;
        const total = Math.max(logical, fixed, 1);
        panes.append(pane("", "목록 280", (280 / total) * 100));
        panes.append(pane("page", `상세 ${fmt(Math.max(0, logical - fixed))}`));
        if (cls === "expanded") panes.append(pane("", "속성 320", (320 / total) * 100));
      }
    }

    preset.addEventListener("click", (e) => {
      const b = e.target.closest("button[data-v]");
      if (b) load(b.dataset.v);
    });
    for (const input of [physical, scale, medium, expanded])
      input.addEventListener("input", () => {
        pressPreset(preset, "");
        render();
      });
    root.addEventListener("lw-reset", () => {
      medium.value = 600;
      expanded.value = 840;
      load("portrait");
    });
    load("portrait");
  }

  window.LuilContainersLab = { grid: gridLab, wrap: wrapLab, width: widthLab };
})();

/* luil 설명서 — 표 실습: 열과 가로 창.
 * 판정은 assets/tablemodel.js(원본을 옮긴 함수)가 하고, 이 파일은 그리기·입력·앱 상태만 맡는다.
 * 앱 쪽 규칙은 데모 작업대(examples/demo/workbench_page.cpp)를 따른다:
 *   - scroll_horizontal(Δ) → scroll_x = max(0, scroll_x + Δ)  (712-716행)
 *   - resize_column(key, Δ) → width += table_resize_delta(column, Δ)  (678-701행)
 */
(function () {
  "use strict";
  const { el, svg } = window.LU;
  const T = window.LuilTable;
  const ROW = 24;
  const HEIGHT = 236; // 머리글 28 + 행 8줄 + 가로 막대 16
  const PAD = 8; // table_cell_padding
  const COLUMNS = [
    { key: "path", header: "경로", width: 340, min_width: 120, align: "start" },
    { key: "status", header: "상태", width: 90, min_width: 40, align: "start" },
    { key: "size", header: "크기", width: 100, min_width: 40, align: "end" },
    { key: "folder", header: "폴더", width: 300, min_width: 80, align: "start" },
  ];
  const DATA = [
    ["src/ui/table_element.cpp", "수정됨", "38.2 KB", "src/ui"],
    ["include/luil/ui/table_element.h", "수정됨", "14.9 KB", "include/luil/ui"],
    ["tests/table_element_tests.cpp", "추가됨", "52.0 KB", "tests"],
    ["docs/table-design.md", "수정됨", "9.1 KB", "docs"],
    ["examples/demo/workbench_page.cpp", "수정됨", "61.7 KB", "examples/demo"],
    ["src/ui/scrollbar_element.cpp", "삭제됨", "7.3 KB", "src/ui"],
    ["docs/frozen-columns-design.md", "추가됨", "11.4 KB", "docs"],
    ["src/ui/virtual_list_element.cpp", "수정됨", "44.8 KB", "src/ui"],
    ["src/ui/scroll_area_element.cpp", "수정됨", "8.6 KB", "src/ui"],
    ["include/luil/ui/check_element.h", "추가됨", "3.2 KB", "include/luil/ui"],
    ["src/win32/uia_provider.cpp", "수정됨", "88.0 KB", "src/win32"],
    ["src/ui/ui_interaction.cpp", "수정됨", "171.5 KB", "src/ui"],
  ];
  const fmt = (v) => (Math.round(v * 100) / 100).toString();

  function setup(root) {
    if (!root) return;
    const q = (name) => root.querySelector(`[data-tl="${name}"]`);
    const view = q("view");
    const stateBox = q("state");
    const logBox = q("log");
    let app;
    let frame = null; // 지금 "게시된" 배치 결과
    let drag = null;

    function initial() {
      app = { columns: COLUMNS.map((c) => ({ ...c })), scrollX: 0, frozen: 0, rows: 8, mode: "automatic", clampBack: false, width: 0 };
    }

    function maxWidth() {
      return Math.max(160, Math.floor(view.clientWidth || 600));
    }

    function log(input, message, effect) {
      const entry = el("div", { class: "tl-entry" }, el("span", { class: "in", text: input }), message ? el("span", { class: "msg", text: message }) : null, el("span", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 40) logBox.lastChild.remove();
    }

    // logic thread의 handle과 make_frame을 한 번에 흉내 낸다.
    function send(input, message, apply) {
      const before = app.scrollX;
      const effect = apply();
      render();
      let note = effect || "";
      if (app.clampBack && frame && app.scrollX !== frame.scrollX) {
        const raw = app.scrollX;
        app.scrollX = frame.scrollX;
        note += ` · 배치 뒤 scroll_x() ${fmt(raw)} → ${fmt(app.scrollX)}`;
        render();
      }
      log(input, message, note || `scroll_x ${fmt(before)} → ${fmt(app.scrollX)}`);
    }

    function scrollMessage(input, delta) {
      if (delta === null) {
        log(input, null, "끝이다 — 메시지 없이 키만 삼킨다");
        return;
      }
      send(input, `scroll_horizontal(${fmt(delta)})`, () => {
        const before = app.scrollX;
        app.scrollX = Math.max(0, Math.fround(app.scrollX + delta));
        const drawn = frame ? T.clampScroll(app.scrollX, frame.contentWidth, frame.viewportWidth) : app.scrollX;
        return `scroll_x ${fmt(before)} → ${fmt(app.scrollX)}` + (drawn !== app.scrollX ? ` (그림은 다듬은 값 ${fmt(drawn)})` : "");
      });
    }

    function arrangeNow() {
      return T.arrange({
        width: app.width, height: HEIGHT, scale: 1, columns: app.columns, scrollX: app.scrollX, frozen: app.frozen,
        verticalBar: "automatic", bodyContent: app.rows * ROW, hasHorizontalBar: app.mode !== "never", horizontalMode: app.mode,
      });
    }

    function text(x, y, value, cls, anchor) {
      return svg("text", { x, y, class: cls, "text-anchor": anchor || "start", "dominant-baseline": "central" }, value);
    }

    function render() {
      app.width = Math.min(app.width || 560, maxWidth());
      const r = (frame = arrangeNow());
      const spans = T.spans(app.columns);
      const F = r.frozenWidth;
      const vw = r.viewWidth;
      const s = svg("svg", { viewBox: `0 0 ${app.width} ${HEIGHT}`, width: app.width, height: HEIGHT, role: "img", "aria-label": `표 재현: 가로 창 ${fmt(r.viewportWidth)}, scroll_x ${fmt(r.scrollX)}` });
      const defs = svg("defs");
      defs.append(svg("clipPath", { id: "tl-clip-frozen" }, svg("rect", { x: 0, y: 0, width: Math.max(0, vw), height: HEIGHT })));
      defs.append(svg("clipPath", { id: "tl-clip-flow" }, svg("rect", { x: F, y: 0, width: Math.max(0, vw - F), height: HEIGHT })));
      s.append(defs, svg("rect", { x: 0, y: 0, width: app.width, height: HEIGHT, class: "tl-bg" }));
      const frozenLayer = svg("g", { "clip-path": "url(#tl-clip-frozen)" });
      const flowLayer = svg("g", { "clip-path": "url(#tl-clip-flow)" });
      const rowsVisible = Math.min(app.rows, Math.floor(r.bodyHeight / ROW + 0.999));
      app.columns.forEach((c, i) => {
        const frozen = i < r.frozenColumns;
        const x = frozen ? spans[i].begin : spans[i].begin - r.scrollX;
        const w = spans[i].width;
        const layer = frozen ? frozenLayer : flowLayer;
        // 칸 밖으로 넘친 글은 자른다 (말줄임은 옮기지 않았다).
        defs.append(svg("clipPath", { id: `tl-c${i}` }, svg("rect", { x, y: 0, width: Math.max(0, w), height: HEIGHT })));
        const cell = svg("g", { "clip-path": `url(#tl-c${i})` });
        cell.append(svg("rect", { x, y: 0, width: w, height: r.headerHeight, class: "tl-head" }));
        const tx = c.align === "end" ? x + w - PAD : x + PAD;
        cell.append(text(tx, r.headerHeight / 2, c.header, "tl-htext", c.align === "end" ? "end" : "start"));
        for (let row = 0; row < rowsVisible; row++) {
          const y = r.headerHeight + row * ROW;
          if (y >= r.headerHeight + r.bodyHeight) break;
          cell.append(text(tx, y + ROW / 2, DATA[row % DATA.length][i], "tl-ctext", c.align === "end" ? "end" : "start"));
        }
        cell.append(svg("line", { x1: x + w - 0.5, x2: x + w - 0.5, y1: 0, y2: r.headerHeight, class: "tl-rule" }));
        layer.append(cell);
      });
      s.append(flowLayer, frozenLayer);
      // 행 구분선과 고정 경계
      for (let row = 1; row <= rowsVisible; row++) {
        const y = r.headerHeight + row * ROW;
        if (y <= r.headerHeight + r.bodyHeight) s.append(svg("line", { x1: 0, x2: vw, y1: y - 0.5, y2: y - 0.5, class: "tl-row-rule" }));
      }
      if (F > 0) s.append(svg("line", { x1: F - 0.5, x2: F - 0.5, y1: 0, y2: r.headerHeight + r.bodyHeight, class: "tl-edge" }));
      // 경계 손잡이 (칸 뒤에 담아 hit에서 이긴다). 고정 열의 손잡이가 마지막이다.
      const grips = svg("g", {});
      const order = app.columns.map((c, i) => i).sort((a, b) => (a < r.frozenColumns) - (b < r.frozenColumns));
      for (const i of order) {
        const frozen = i < r.frozenColumns;
        const edge = (frozen ? spans[i].begin : spans[i].begin - r.scrollX) + spans[i].width;
        if (!frozen && (edge < F || edge > vw + 4)) continue;
        const grip = svg("rect", { x: edge - 4, y: 0, width: 8, height: r.headerHeight, class: "tl-grip", "data-col": i });
        grips.append(grip);
      }
      s.append(grips);
      // 세로 막대 (본문의 것, 행이 넘칠 때)
      if (r.vertical) {
        const b = T.thumb(r.headerHeight, r.bodyHeight, 1, app.rows * ROW, r.bodyHeight, 0);
        s.append(svg("rect", { x: vw, y: r.headerHeight, width: app.width - vw, height: r.bodyHeight, class: "tl-track" }));
        s.append(svg("rect", { x: app.width - 8 - 2, y: b.top + 1, width: 6, height: Math.max(0, b.length - 2), rx: 3, class: "tl-thumb" }));
      }
      // 가로 막대 (표의 것, 열이 넘칠 때)
      let bar = null;
      if (r.horizontal) {
        bar = T.thumb(0, vw, 1, r.contentWidth, r.viewportWidth, r.scrollX);
        const y = r.headerHeight + r.bodyHeight;
        s.append(svg("rect", { x: 0, y, width: vw, height: r.horizontalHeight, class: "tl-track tl-hbar", "data-bar": "h" }));
        s.append(svg("rect", { x: bar.top + 1, y: y + r.horizontalHeight - 8 - 1, width: Math.max(0, bar.length - 2), height: 6, rx: 3, class: "tl-thumb", "data-bar": "h" }));
      }
      frame.bar = bar;
      view.replaceChildren(s);
      renderState(r);
      syncControls();
    }

    function renderState(r) {
      const rows = [
        ["앱 scroll_x", fmt(app.scrollX)],
        ["scroll_x()", `${fmt(r.scrollX)} (최대 ${fmt(Math.max(0, r.contentWidth - r.viewportWidth))})`],
        ["viewport_width()", `${fmt(r.viewportWidth)} = 표 폭 ${fmt(app.width)}${r.vertical ? " − 세로 막대 16" : ""}`],
        ["열 폭 합", fmt(r.contentWidth)],
        ["frozen_columns()", app.frozen === 0 ? "0" : `${r.frozenColumns} / 설정 ${app.frozen}${r.frozenColumns === 0 ? " (흐르는 창이 48보다 좁아 풀림)" : ""}`],
        ["frozen_width()", fmt(r.frozenWidth)],
        ["두 막대", `세로 ${r.vertical ? "섬" : "없음"} · 가로 ${r.horizontal ? "섬" : app.mode === "never" ? "짓지 않음" : "숨김"}`],
        ["열 폭", app.columns.map((c) => `${c.header} ${fmt(c.width)}`).join(" · ")],
      ];
      stateBox.replaceChildren(...rows.flatMap(([k, v]) => [el("dt", { text: k }), el("dd", {}, el("code", { text: v }))]));
    }

    // ---------- 조작판 ----------
    const tools = q("tools");
    const widthInput = el("input", { type: "range", min: 160, max: 900, step: 1, "aria-label": "표 폭 (논리 픽셀)" });
    const widthOut = el("output", {});
    const rowsInput = el("input", { type: "range", min: 2, max: 12, step: 1, "aria-label": "행 수" });
    const frozenSelect = el("select", { "aria-label": "frozen_columns" }, [0, 1, 2, 3, 4].map((n) => el("option", { value: n, text: `고정 ${n}열` })));
    const modeSelect = el("select", { "aria-label": "horizontal_bar" }, ["automatic", "always", "never"].map((m) => el("option", { value: m, text: `horizontal_bar ${m}` })));
    const clampBox = el("input", { type: "checkbox" });
    tools.append(
      el("label", { class: "tl-range" }, "표 폭 ", widthInput, widthOut),
      el("label", { class: "tl-range" }, "행 ", rowsInput),
      frozenSelect,
      modeSelect,
      el("label", { title: "배치 뒤 table_element::scroll_x()를 앱 상태에 되돌린다" }, clampBox, " 배치 뒤 scroll_x() 되돌리기")
    );
    const buttons = q("buttons");
    const button = (label, title, fn) => el("button", { type: "button", class: "btn", title, onclick: fn }, label);
    buttons.append(
      button("←", "← 키 (셀 탐색 없음)", () => scrollMessage("← 키", T.keyStep(false, frame.scrollX, frame.contentWidth, frame.viewportWidth))),
      button("→", "→ 키 (셀 탐색 없음)", () => scrollMessage("→ 키", T.keyStep(true, frame.scrollX, frame.contentWidth, frame.viewportWidth))),
      button("가로 휠 ◀", "가로 휠 한 눈금 왼쪽", () => scrollMessage("가로 휠 ◀", T.wheelDelta(frame.scrollX, -48, frame.contentWidth, frame.viewportWidth))),
      button("가로 휠 ▶", "가로 휠 한 눈금 오른쪽", () => scrollMessage("가로 휠 ▶", T.wheelDelta(frame.scrollX, 48, frame.contentWidth, frame.viewportWidth)))
    );

    function syncControls() {
      widthInput.max = String(maxWidth());
      widthInput.value = String(app.width);
      widthOut.textContent = String(app.width);
      rowsInput.value = String(app.rows);
      frozenSelect.value = String(app.frozen);
      modeSelect.value = app.mode;
      clampBox.checked = app.clampBack;
    }
    widthInput.addEventListener("input", () => {
      app.width = Number(widthInput.value);
      render();
    });
    rowsInput.addEventListener("input", () => {
      app.rows = Number(rowsInput.value);
      render();
    });
    frozenSelect.addEventListener("change", () => {
      app.frozen = Number(frozenSelect.value);
      render();
      log(`frozen_columns = ${app.frozen}`, null, frame.frozenColumns === app.frozen ? "이번 배치에 섰다" : "흐르는 창이 48보다 좁아 이번 배치에서 풀렸다");
    });
    modeSelect.addEventListener("change", () => {
      app.mode = modeSelect.value;
      render();
    });
    clampBox.addEventListener("change", () => {
      app.clampBack = clampBox.checked;
      send("되돌리기 " + (app.clampBack ? "켬" : "끔"), null, () => "");
    });

    // ---------- 포인터: 손잡이·가로 막대 ----------
    function localX(event) {
      const s = view.querySelector("svg");
      const box = s.getBoundingClientRect();
      return ((event.clientX - box.left) * app.width) / box.width;
    }
    view.addEventListener("pointerdown", (event) => {
      const target = event.target;
      if (!(target instanceof Element)) return;
      const x = localX(event);
      if (target.dataset.col !== undefined) {
        const i = Number(target.dataset.col);
        drag = { kind: "grip", index: i, x };
      } else if (target.dataset.bar === "h" && frame.bar) {
        const pressed = T.barPress(frame.bar, x);
        drag = { kind: "bar", x };
        if (pressed !== null) scrollMessage("가로 막대 트랙 누름", pressed);
      } else return;
      view.setPointerCapture(event.pointerId);
      event.preventDefault();
    });
    view.addEventListener("pointermove", (event) => {
      if (!drag) return;
      const x = localX(event);
      if (drag.kind === "grip") {
        const i = drag.index;
        const column = app.columns[i];
        const frozenTable = i < frame.configuredFrozen ? { frozenColumns: frame.frozenColumns, viewportWidth: frame.viewportWidth, frozenWidth: frame.frozenWidth } : null;
        const delta = T.gripDelta(column, drag.x, x, 1, frozenTable);
        drag.x = x;
        if (delta === null) return;
        send(`${column.header} 경계 끌기`, `resize_column("${column.key}", ${fmt(delta)})`, () => {
          const before = column.width;
          column.width = Math.fround(column.width + T.resizeDelta(column, delta));
          return `${column.header} 폭 ${fmt(before)} → ${fmt(column.width)}`;
        });
      } else if (drag.kind === "bar" && frame.bar) {
        const delta = T.barDrag(frame.bar, drag.x, x);
        drag.x = x;
        if (delta !== null) scrollMessage("가로 막대 끌기", delta);
      }
    });
    const endDrag = () => {
      if (drag && drag.kind === "grip") log(`${app.columns[drag.index].header} 경계 놓기`, `resize_column_end("${app.columns[drag.index].key}")`, "실행 취소 합치기를 끊는다");
      drag = null;
    };
    view.addEventListener("pointerup", endDrag);
    view.addEventListener("pointercancel", () => (drag = null));

    // 가로 휠(트랙패드 가로 밀기·틸트)만 받는다. 세로 휠은 쪽을 흘리게 둔다.
    view.addEventListener(
      "wheel",
      (event) => {
        if (Math.abs(event.deltaX) <= Math.abs(event.deltaY) || event.deltaX === 0) return;
        event.preventDefault();
        const delta = event.deltaX > 0 ? 48 : -48;
        scrollMessage(delta > 0 ? "가로 휠 ▶" : "가로 휠 ◀", T.wheelDelta(frame.scrollX, delta, frame.contentWidth, frame.viewportWidth));
      },
      { passive: false }
    );
    view.addEventListener("keydown", (event) => {
      if (event.ctrlKey || event.altKey || event.metaKey || event.shiftKey) return;
      if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
      event.preventDefault();
      const forward = event.key === "ArrowRight";
      scrollMessage(forward ? "→ 키" : "← 키", T.keyStep(forward, frame.scrollX, frame.contentWidth, frame.viewportWidth));
    });

    root.addEventListener("lw-reset", () => {
      initial();
      logBox.replaceChildren();
      render();
    });
    window.addEventListener("resize", () => render());
    initial();
    render();
    window.LU.onTheme(() => render());
  }

  window.LuilTableLab = { setup };
})();

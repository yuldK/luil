/* luil 설명서 — 스크롤과 확대 실습: 확대 보기.
 * 판정은 assets/zoommodel.js(원본을 옮긴 함수)가 하고, 이 파일은 그리기·입력·앱 상태만 맡는다.
 * 앱 쪽 규칙은 데모의 도면 페이지(examples/demo/zoom_page.cpp)를 따른다:
 *   - zoom_by → zoom_about, pan_by → pan_by, rotate_by → rotate_about을 지금 상태에 적용 (98-113행)
 *   - 짓기 전에 zoom_about(config(), viewport, 1, {})로 다듬는다 (120행). 경계는 rotated_bounds(page_bounds, rotation) (89행)
 */
(function () {
  "use strict";
  const { el, svg } = window.LU;
  const Z = window.LuilZoom;
  const f = Math.fround;
  const PAGE = { x: -500, y: -350, width: 1000, height: 700 }; // 데모의 page_bounds
  const HEIGHT = 300;
  const pct = (z) => `${Math.round(z * 100)}%`;
  const fmt = (v) => (Math.round(v * 10) / 10).toString();

  function setup(root) {
    if (!root) return;
    const q = (name) => root.querySelector(`[data-zl="${name}"]`);
    const view = q("view");
    const stateBox = q("state");
    const logBox = q("log");
    let st;
    let bounds = "overlap";
    let wheelMode = "zoom";
    let width = 600;
    let lastBlocked = null;
    let drag = null;

    const viewport = () => ({ x: f(width), y: f(HEIGHT) });
    function config() {
      return {
        zoom: st.zoom, origin: st.origin, minimum_zoom: f(0.1), maximum_zoom: 5, content_bounds: Z.rotatedBounds(PAGE, st.rotation),
        bounds, wheel: wheelMode, zoom_by: true, pan_by: true, zoom_to: true, rotate_by: true, rotation: st.rotation, rotation_step: 15,
        double_tap: { enabled: false, factor: 2, fit_margin: 16, maximum_fit_zoom: 1 },
      };
    }
    // make_frame: 짓기 전에 지금 상태를 다듬는다 (회전 뒤 새 경계로 다듬는 것도 이 줄이다).
    function settle() {
      st = Z.zoomAbout(config(), viewport(), 1, { x: 0, y: 0 });
    }
    function initial() {
      st = { zoom: 1, origin: { x: 0, y: 0 }, rotation: 0 };
      bounds = "overlap";
      wheelMode = "zoom";
      lastBlocked = null;
      width = measure();
      st = Z.zoomToFit(config(), viewport());
    }
    function measure() {
      return Math.max(200, Math.min(720, Math.floor(view.clientWidth || 600)));
    }

    const describe = (m) =>
      m.type === "zoom_by" ? `zoom_by(${+m.factor.toFixed(4)}, {${fmt(m.anchor.x)}, ${fmt(m.anchor.y)}})`
      : m.type === "pan_by" ? `pan_by({${fmt(m.delta.x)}, ${fmt(m.delta.y)}})`
      : `rotate_by(${fmt(m.degrees)}, {${fmt(m.anchor.x)}, ${fmt(m.anchor.y)}})`;
    const stateText = () => `배율 ${pct(st.zoom)} · 원점 (${fmt(st.origin.x)}, ${fmt(st.origin.y)}) · ${fmt(st.rotation)}°`;

    // logic thread의 handle: 메시지를 온 순서대로 지금 상태에 적용한다.
    function send(input, messages) {
      if (messages === null) return false;
      if (messages.length === 0) {
        log(input, null, input.endsWith("키") ? "보기가 키를 삼켰다 — 메시지 없음" : "이미 그 상태다 — 메시지 없음");
        return true;
      }
      for (const m of messages) {
        const c = config();
        if (m.type === "zoom_by") st = Z.zoomAbout(c, viewport(), m.factor, m.anchor);
        else if (m.type === "pan_by") {
          lastBlocked = Z.panBlocked(c, viewport(), m.delta);
          st = Z.panBy(c, viewport(), m.delta);
        } else if (m.type === "rotate_by") st = Z.rotateAbout(c, viewport(), m.degrees, m.anchor);
      }
      settle();
      render();
      log(input, messages.map(describe).join(" → "), stateText());
      return true;
    }

    function log(input, message, effect) {
      const last = logBox.firstElementChild;
      if (last && last.dataset.input === input && input.startsWith("끌기") && message) {
        last.querySelector(".msg").textContent = message;
        last.querySelector(".eff").textContent = effect;
        return;
      }
      const entry = el("div", { class: "zl-entry", dataset: { input } }, el("span", { class: "in", text: input }), message ? el("span", { class: "msg", text: message }) : null, el("span", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 40) logBox.lastChild.remove();
    }

    function render() {
      const W = width;
      const H = HEIGHT;
      const cx = W / 2 + st.origin.x;
      const cy = H / 2 + st.origin.y;
      const s = svg("svg", { viewBox: `0 0 ${W} ${H}`, width: W, height: H, role: "img", "aria-label": `확대 보기 재현: ${stateText()}` });
      s.append(svg("rect", { x: 0, y: 0, width: W, height: H, class: "zl-bg" }));
      // 내용: 격자와 쪽 테두리는 내용 요소가 돌려 그린다.
      const content = svg("g", { transform: `translate(${cx} ${cy}) scale(${st.zoom}) rotate(${st.rotation})` });
      content.append(svg("rect", { x: PAGE.x, y: PAGE.y, width: PAGE.width, height: PAGE.height, class: "zl-page" }));
      for (let x = PAGE.x; x <= PAGE.x + PAGE.width; x += 50) content.append(svg("line", { x1: x, y1: PAGE.y, x2: x, y2: PAGE.y + PAGE.height, class: "zl-grid" }));
      for (let y = PAGE.y; y <= PAGE.y + PAGE.height; y += 50) content.append(svg("line", { x1: PAGE.x, y1: y, x2: PAGE.x + PAGE.width, y2: y, class: "zl-grid" }));
      s.append(content);
      // 노드: 가운데만 돌리고 글자는 세운다 (데모의 diagram::arrange).
      for (let i = 0; i < 4; i++) {
        const c = Z.rotatePoint({ x: -240 + i * 160, y: i % 2 === 0 ? -70 : 60 }, st.rotation);
        const g = svg("g", { transform: `translate(${cx + c.x * st.zoom} ${cy + c.y * st.zoom}) scale(${st.zoom})` });
        g.append(svg("rect", { x: -60, y: -30, width: 120, height: 60, rx: 6, class: "zl-node" }), svg("text", { x: 0, y: 0, class: "zl-ntext", "text-anchor": "middle", "dominant-baseline": "central" }, `노드 ${i + 1}`));
        s.append(g);
      }
      // 앱이 준 경계(돌린 경계 상자)와 창 가운데
      const b = Z.rotatedBounds(PAGE, st.rotation);
      s.append(svg("rect", { x: cx + b.x * st.zoom, y: cy + b.y * st.zoom, width: b.width * st.zoom, height: b.height * st.zoom, class: "zl-bounds" }));
      s.append(svg("path", { d: `M${W / 2 - 6} ${H / 2}h12M${W / 2} ${H / 2 - 6}v12`, class: "zl-center" }));
      view.replaceChildren(s);
      renderState();
      syncTools();
    }

    function renderState() {
      const vp = viewport();
      const rows = [
        ["zoom", `${+st.zoom.toFixed(4)} (${pct(st.zoom)}, 범위 10%~500%)`],
        ["origin", `(${fmt(st.origin.x)}, ${fmt(st.origin.y)}) — 창 가운데 기준 논리 픽셀`],
        ["rotation", `${fmt(st.rotation)}°`],
        ["viewport", `${vp.x} × ${vp.y}`],
        ["content_bounds", (() => { const r = Z.rotatedBounds(PAGE, st.rotation); return `rotated_bounds → (${fmt(r.x)}, ${fmt(r.y)}, ${fmt(r.width)}, ${fmt(r.height)})`; })()],
      ];
      if (st.rotation === 0) {
        const left = (-vp.x / 2 - st.origin.x) / st.zoom;
        const top = (-vp.y / 2 - st.origin.y) / st.zoom;
        rows.push(["보이는 내용", `(${fmt(left)}, ${fmt(top)}, ${fmt(vp.x / st.zoom)}, ${fmt(vp.y / st.zoom)})`]);
      }
      if (lastBlocked) rows.push(["pan_blocked", `(${fmt(lastBlocked.x)}, ${fmt(lastBlocked.y)}) — 마지막 이동에서 경계가 버린 몫`]);
      stateBox.replaceChildren(...rows.flatMap(([k, v]) => [el("dt", { text: k }), el("dd", {}, el("code", { text: v }))]));
    }

    // ---------- 조작판: 확대 단추(zoom_controls_element의 규칙)·축 맞춤·회전·방식 ----------
    const tools = q("tools");
    const btn = (label, title, fn) => el("button", { type: "button", class: "btn", title, onclick: fn }, label);
    const dec = btn("축소", "zoom_by(1 / 1.2) — 최소 배율이면 비활성", () => send("축소 단추", [{ type: "zoom_by", factor: f(1 / Z.ZOOM_STEP), anchor: { x: 0, y: 0 } }]));
    const label = el("span", { class: "zl-pct" });
    const inc = btn("확대", "zoom_by(1.2) — 최대 배율이면 비활성", () => send("확대 단추", [{ type: "zoom_by", factor: Z.ZOOM_STEP, anchor: { x: 0, y: 0 } }]));
    const fit = btn("맞춤", "fit_actions(여백 16, 상한 1)", () => send("맞춤 단추", Z.fitActions(config(), viewport(), 16, 1)));
    const fitW = btn("폭 맞춤", "zoom_fit_actions(…, 16, 5, zoom_fit::width)", () => send("폭 맞춤", Z.fitActions(config(), viewport(), 16, 5, "width")));
    const fitH = btn("높이 맞춤", "zoom_fit_actions(…, 16, 5, zoom_fit::height)", () => send("높이 맞춤", Z.fitActions(config(), viewport(), 16, 5, "height")));
    const rl = btn("−15°", "rotate_by(−15)", () => send("−15° 단추", [{ type: "rotate_by", degrees: -15, anchor: { x: 0, y: 0 } }]));
    const rr = btn("+15°", "rotate_by(+15)", () => send("+15° 단추", [{ type: "rotate_by", degrees: 15, anchor: { x: 0, y: 0 } }]));
    const r0 = btn("0°", "rotate_by(−rotation) — 이미 0도면 비활성", () => send("0° 단추", [{ type: "rotate_by", degrees: -st.rotation, anchor: { x: 0, y: 0 } }]));
    const boundsSel = el("select", { "aria-label": "zoom_bounds" }, el("option", { value: "overlap", text: "bounds overlap" }), el("option", { value: "contain", text: "bounds contain" }));
    const wheelSel = el("select", { "aria-label": "zoom_wheel_mode" }, el("option", { value: "zoom", text: "wheel zoom" }), el("option", { value: "pan", text: "wheel pan" }));
    tools.append(el("span", { class: "zl-group" }, dec, label, inc, fit), el("span", { class: "zl-group" }, fitW, fitH), el("span", { class: "zl-group" }, rl, rr, r0), boundsSel, wheelSel);
    function syncTools() {
      const c = config();
      dec.disabled = !(c.zoom > c.minimum_zoom);
      inc.disabled = !(c.zoom < c.maximum_zoom);
      r0.disabled = st.rotation === 0;
      label.textContent = pct(Math.min(Math.max(st.zoom, c.minimum_zoom), c.maximum_zoom));
      boundsSel.value = bounds;
      wheelSel.value = wheelMode;
    }
    boundsSel.addEventListener("change", () => {
      bounds = boundsSel.value;
      settle();
      render();
      log(`bounds = ${bounds}`, null, stateText());
    });
    wheelSel.addEventListener("change", () => {
      wheelMode = wheelSel.value;
      render();
    });

    // ---------- 입력 ----------
    function local(event) {
      const box = view.querySelector("svg").getBoundingClientRect();
      return { x: ((event.clientX - box.left) * width) / box.width, y: ((event.clientY - box.top) * HEIGHT) / box.height };
    }
    view.addEventListener(
      "wheel",
      (event) => {
        event.preventDefault();
        const p = local(event);
        const horizontal = Math.abs(event.deltaX) > Math.abs(event.deltaY);
        const delta = horizontal ? (event.deltaX > 0 ? 120 : -120) : event.deltaY < 0 ? 120 : -120;
        const ev = { x: p.x, y: p.y, delta, horizontal, control: event.ctrlKey || event.metaKey, shift: event.shiftKey };
        const scrollDelta = f(-f(delta / 120) * 48);
        const name = `${ev.control ? "Ctrl+" : ""}${ev.shift ? "Shift+" : ""}${horizontal ? "가로 휠" : "휠"} ${delta > 0 ? (horizontal ? "▶" : "▲") : horizontal ? "◀" : "▼"}`;
        send(name, Z.wheel(config(), { x: 0, y: 0, width, height: HEIGHT }, 1, ev, scrollDelta));
      },
      { passive: false }
    );
    view.addEventListener("pointerdown", (event) => {
      drag = local(event);
      view.setPointerCapture(event.pointerId);
      view.focus({ preventScroll: true });
      event.preventDefault();
    });
    view.addEventListener("pointermove", (event) => {
      if (!drag) return;
      const p = local(event);
      const delta = Z.dragDelta(drag, p, 1);
      drag = p;
      if (delta.x !== 0 || delta.y !== 0) send(`끌기 (${event.pointerType || "mouse"})`, [{ type: "pan_by", delta }]);
    });
    view.addEventListener("pointerup", () => (drag = null));
    view.addEventListener("pointercancel", () => (drag = null));
    const KEYS = { ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down", PageUp: "page_up", PageDown: "page_down", Home: "home", "+": "zoom_in", "=": "zoom_in", "-": "zoom_out", "0": "0" };
    view.addEventListener("keydown", (event) => {
      const key = KEYS[event.key];
      if (!key) return;
      const result = Z.keys(config(), { width, height: HEIGHT }, 1, { key, shift: event.shiftKey, control: event.ctrlKey, alt: event.altKey, meta: event.metaKey });
      if (result === null) return; // 보기의 키가 아니다 — 브라우저에 맡긴다
      event.preventDefault();
      send(`${event.shiftKey ? "Shift+" : ""}${event.key} 키`, result);
    });

    root.addEventListener("lw-reset", () => {
      initial();
      logBox.replaceChildren();
      render();
    });
    window.addEventListener("resize", () => {
      const next = measure();
      if (next === width) return;
      width = next;
      settle();
      render();
    });
    initial();
    render();
  }

  window.LuilScrollingLab = { setup };
})();

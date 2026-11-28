/* 창과 셸 통합 — caption 판정 지도 실습.
 * 판정(어느 자리가 무엇인가, 누름과 뗌이 실행되는가)은 assets/captionhit.js(원본을 옮긴 함수)만 부른다.
 * 이 파일은 그 답을 칠하고, 누름과 뗌이 caption_surface.cpp:467-500의 어느 갈래로 가는지 적을 뿐이다.
 * 창 크기·caption의 자리·검색 칸의 배치는 앱 쪽 선택이다 (examples/demo_main.cpp:810-845의 배치를 본떴다).
 */
(function () {
  "use strict";

  const H = window.LuilCaptionHit;
  const f32 = Math.fround;

  // win32::window_frame_metrics의 기본값 (window_mode.h, win32_window.h:327-331).
  const FRAME = { caption: { height: 36, buttonWidth: 42, iconSlot: 40 }, border: 4, corner: 10 };

  // window_hit → caption_surface.cpp:53-84가 OS에 돌려주는 값.
  const HT = {
    client: "HTCLIENT", caption_drag: "HTCAPTION", system_menu: "HTSYSMENU",
    minimize_button: "HTMINBUTTON", maximize_button: "HTMAXBUTTON", close_button: "HTCLOSE",
    resize_left: "HTLEFT", resize_right: "HTRIGHT", resize_top: "HTTOP", resize_bottom: "HTBOTTOM",
    resize_top_left: "HTTOPLEFT", resize_top_right: "HTTOPRIGHT", resize_bottom_left: "HTBOTTOMLEFT", resize_bottom_right: "HTBOTTOMRIGHT",
  };
  const KO = {
    client: "client", caption_drag: "끌기 띠", system_menu: "시스템 메뉴 칸", minimize_button: "최소화", maximize_button: "최대화", close_button: "닫기",
    resize_left: "왼쪽 가장자리", resize_right: "오른쪽 가장자리", resize_top: "위 가장자리", resize_bottom: "아래 가장자리",
    resize_top_left: "왼쪽 위 모서리", resize_top_right: "오른쪽 위 모서리", resize_bottom_left: "왼쪽 아래 모서리", resize_bottom_right: "오른쪽 아래 모서리",
  };
  const isEdge = (hit) => hit.startsWith("resize_");
  const COMMAND = { minimize_button: "ui_command::window_minimize", maximize_button: "ui_command::window_toggle_maximize", close_button: "ui_command::window_close" };

  const DEFAULTS = { w: 1000, h: 600, dpi: 96, mode: "normal", place: "top", min: true, max: true, close: true, apps: 1, search: true };

  // 앱이 짓는 tree다: root 아래 caption(앱 단추 + 창 버튼)과, caption 다음에 담은 검색 칸.
  function build(s) {
    const scale = f32(s.dpi / 96);
    const buttons = { minimize: s.min, maximize: s.max, close: s.close };
    const frame = { caption: FRAME.caption, buttons, border: FRAME.border, corner: FRAME.corner };
    const capH = f32(FRAME.caption.height * scale);
    let slot = null;
    if (s.place === "top" || s.place === "hidden") slot = { x: 0, y: 0, width: s.w };
    else if (s.place === "bottom") slot = { x: 0, y: f32(s.h - capH), width: s.w };
    else if (s.place === "part") slot = { x: Math.round(s.w * 0.2), y: Math.round((s.h - capH) / 2), width: Math.round(s.w * 0.6) };
    const apps = [{ key: "menu", glyph: 0xeb94 }, { key: "info", glyph: 0xea74 }].slice(0, s.apps);
    const root = { children: [] };
    let arranged = null;
    let search = null;
    if (slot) {
      arranged = H.arrangeCaption(slot, scale, FRAME.caption, buttons, apps);
      const children = arranged.apps.map((a) => ({ captionClient: true, bounds: a.bounds, app: a.key }));
      for (const key of ["minimize", "maximize", "close"]) if (arranged.buttons[key]) children.push({ bounds: arranged.buttons[key] });
      root.children.push({ caption: true, visible: s.place !== "hidden", bounds: arranged.bounds, children });
      // examples/demo_main.cpp:824-836 — 폭 240, 안쪽 4. 창 일부만 덮은 caption이면 그 가운데에 둔다.
      if (s.search && s.place !== "hidden") {
        const sw = f32(240 * scale);
        const inset = f32(4 * scale);
        search = { x: f32(f32(slot.x) + f32(f32(slot.width - sw) / 2)), y: f32(arranged.bounds.y + inset), width: sw, height: f32(capH - f32(inset * 2)) };
        root.children.push({ captionClient: true, bounds: search, search: true });
      }
    }
    return { scale, frame, tree: root, arranged, search, apps };
  }

  // 판정은 옮긴 함수 하나다. 지도와 읽기가 같은 답을 본다.
  const judge = (s, m, x, y) => H.nonClientHit(m.frame, s.mode, s.w, s.h, s.dpi, x, y, m.tree);

  // 경계 후보. 모든 판정이 x·y를 이 값들과 <·≥로만 견주므로, 칸마다 왼쪽 위 정수 점 하나로 그 칸 전체의 답이다.
  function cuts(s, m, band) {
    const b = Math.max(1, H.scaleForDpi(FRAME.border, s.dpi));
    const c = Math.max(b, H.scaleForDpi(FRAME.corner, s.dpi));
    const layout = H.makeCaptionLayout(band, s.dpi, m.frame.caption, m.frame.buttons);
    const xs = [0, s.w, b, c, s.w - b, s.w - c, layout.left, layout.right, layout.minimizeLeft, layout.maximizeLeft, layout.closeLeft, layout.left + H.scaleForDpi(FRAME.caption.iconSlot, s.dpi)];
    const ys = [0, s.h, b, c, s.h - b, s.h - c, layout.top, layout.top + layout.height];
    const rects = [];
    if (m.search) rects.push(m.search);
    if (m.arranged) for (const a of m.arranged.apps) rects.push(a.bounds);
    for (const r of rects) {
      xs.push(Math.ceil(r.x), Math.ceil(f32(r.x + r.width)));
      ys.push(Math.ceil(r.y), Math.ceil(f32(r.y + r.height)));
    }
    const norm = (vs, max) => [...new Set(vs.filter((v) => v > 0 && v < max).concat([0, max]))].sort((p, q) => p - q);
    return { xs: norm(xs, s.w), ys: norm(ys, s.h) };
  }

  function regions(s, m) {
    const band = H.captionBandIn(m.tree, s.w, s.dpi, m.frame.caption);
    const { xs, ys } = cuts(s, m, band);
    const out = [];
    for (let j = 0; j + 1 < ys.length; j++) {
      let run = null;
      for (let i = 0; i + 1 < xs.length; i++) {
        const r = judge(s, m, xs[i], ys[j]);
        const kind = r.flipped ? "flip" : r.hit;
        if (run && run.kind === kind) run.x1 = xs[i + 1];
        else {
          if (run) out.push(run);
          run = { kind, x0: xs[i], x1: xs[i + 1], y0: ys[j], y1: ys[j + 1] };
        }
      }
      if (run) out.push(run);
    }
    return { band, cells: out.filter((r) => r.kind !== "client") };
  }

  function cls(kind) {
    if (kind === "flip") return "ch-h-flip";
    if (kind === "caption_drag") return "ch-h-drag";
    if (kind === "system_menu") return "ch-h-sys";
    if (kind === "close_button") return "ch-h-close";
    if (H.isCaptionButton(kind)) return "ch-h-btn";
    return "ch-h-edge";
  }

  // 판정이 어느 갈래에서 났는가를 글로 적는다 (답은 바꾸지 않는다).
  function reason(s, m, r, x, y) {
    const layout = H.makeCaptionLayout(r.band, s.dpi, m.frame.caption, m.frame.buttons);
    const slot = H.scaleForDpi(FRAME.caption.iconSlot, s.dpi);
    if (s.mode === "fullscreen") return ["전체 화면에는 비클라이언트가 없다. DWM에도 묻지 않는다.", "caption_surface.cpp:29-34 · window_mode.cpp:19-22"];
    if (isEdge(r.hit)) {
      const b = Math.max(1, H.scaleForDpi(FRAME.border, s.dpi));
      const c = Math.max(b, H.scaleForDpi(FRAME.corner, s.dpi));
      return [`보통 창의 크기 조절 (가장자리 ${b}px, 모서리 ${c}px). 모서리가 먼저다. 최대화 창에는 없다.`, "window_mode.cpp:24-51"];
    }
    if (r.hit === "system_menu") return [`띠 안이고 x < 띠 왼쪽 + ${slot}px. 아이콘이 없어도, 창 버튼보다 먼저다.`, "window_mode.cpp:55-57"];
    if (H.isCaptionButton(r.hit)) return [`띠 안이고 x ≥ ${r.hit === "close_button" ? "close_left " + layout.closeLeft : r.hit === "maximize_button" ? "maximize_left " + layout.maximizeLeft : "minimize_left " + layout.minimizeLeft}.`, "caption_layout.cpp:86-97"];
    if (r.hit === "caption_drag") return ["띠 안의 버튼 왼쪽. 여기에 보이는 client 칸이 없다.", "caption_layout.cpp:96 · caption_surface.cpp:438-448"];
    if (r.flipped) return ["판정은 끌기였지만 그 자리에 client 표시(set_caption_client)가 달린 element가 보여 client로 뒤집었다.", "caption_surface.cpp:440-446 · ui_tree.cpp:623-651"];
    if (r.band.width === 0 || r.band.height === 0) return ["띠가 없다 — caption이 숨었다. 어느 자리도 끌기가 아니다.", "caption_layout.cpp:45-46"];
    return ["띠 밖이다.", "caption_layout.cpp:88-89"];
  }

  function setup(root) {
    if (!root || !H || !window.LU) return;
    const LU = window.LU;
    const $ = (name) => root.querySelector(`[data-ch="${name}"]`);
    const mapBox = $("map");
    const read = $("read");
    const log = $("log");
    // 좁은 화면에서는 처음 창을 좁게 둔다 (지도가 읽힐 만한 크기로). 판정에는 상관이 없다.
    const start = () => ({ ...DEFAULTS, w: mapBox.clientWidth > 0 && mapBox.clientWidth < 520 ? 480 : DEFAULTS.w });
    let s = start();
    let m = build(s);
    let probe = { x: Math.floor(s.w / 2), y: 18 };

    const svgEl = LU.svg("svg", { viewBox: `0 0 ${s.w} ${s.h}`, role: "img", "aria-label": "창의 자리마다 비클라이언트 판정을 칠한 지도" });
    mapBox.append(svgEl);

    function controls() {
      $("w").value = s.w;
      $("h").value = s.h;
      $("wv").textContent = `${s.w}×${s.h}`;
      $("dpi").value = String(s.dpi);
      $("mode").value = s.mode;
      $("place").value = s.place;
      $("min").checked = s.min;
      $("max").checked = s.max;
      $("close").checked = s.close;
      $("apps").value = String(s.apps);
      $("search").checked = s.search;
    }

    function drawWindow(g) {
      g.append(LU.svg("rect", { x: 0, y: 0, width: s.w, height: s.h, class: "ch-win" }));
      if (!m.arranged || s.place === "hidden") return;
      const cb = m.arranged.bounds;
      if (cb.height <= 0) return;
      const k = m.scale;
      g.append(LU.svg("rect", { x: cb.x, y: cb.y, width: cb.width, height: cb.height, class: "ch-cap" }));
      g.append(LU.svg("text", { x: cb.x + 14 * k, y: cb.y + cb.height / 2, class: "ch-title", "font-size": 14 * k, "dominant-baseline": "central", text: "앱 창" }));
      const glyph = (key, r) => {
        const cx = r.x + r.width / 2;
        const cy = r.y + r.height / 2;
        const u = 5 * k;
        if (key === "minimize") g.append(LU.svg("line", { x1: cx - u, y1: cy, x2: cx + u, y2: cy, class: "ch-glyph" }));
        if (key === "maximize") g.append(LU.svg("rect", { x: cx - u, y: cy - u, width: 2 * u, height: 2 * u, class: "ch-glyph" }));
        if (key === "close") {
          g.append(LU.svg("line", { x1: cx - u, y1: cy - u, x2: cx + u, y2: cy + u, class: "ch-glyph" }));
          g.append(LU.svg("line", { x1: cx - u, y1: cy + u, x2: cx + u, y2: cy - u, class: "ch-glyph" }));
        }
      };
      for (const key of ["minimize", "maximize", "close"]) if (m.arranged.buttons[key]) glyph(key, m.arranged.buttons[key]);
      m.arranged.apps.forEach((a, i) => {
        const r = a.bounds;
        g.append(LU.svg("text", { x: r.x + r.width / 2, y: r.y + r.height / 2, class: "ch-appglyph", "font-size": 15 * k, "text-anchor": "middle", "dominant-baseline": "central", text: String.fromCodePoint(m.apps[i].glyph) }));
      });
      if (m.search) {
        const r = m.search;
        g.append(LU.svg("rect", { x: r.x, y: r.y, width: r.width, height: r.height, rx: 4 * k, class: "ch-search" }));
        g.append(LU.svg("text", { x: r.x + 10 * k, y: r.y + r.height / 2, class: "ch-appglyph", "font-size": 13 * k, "dominant-baseline": "central", text: String.fromCodePoint(0xea6d) }));
        g.append(LU.svg("text", { x: r.x + 30 * k, y: r.y + r.height / 2, class: "ch-title", "font-size": 12 * k, "dominant-baseline": "central", text: "명령 찾기…" }));
      }
    }

    function render() {
      m = build(s);
      probe = { x: Math.min(Math.max(probe.x, 0), s.w - 1), y: Math.min(Math.max(probe.y, 0), s.h - 1) };
      svgEl.setAttribute("viewBox", `0 0 ${s.w} ${s.h}`);
      svgEl.replaceChildren();
      const g1 = LU.svg("g", {});
      drawWindow(g1);
      const g2 = LU.svg("g", {});
      for (const c of regions(s, m).cells) g2.append(LU.svg("rect", { x: c.x0, y: c.y0, width: c.x1 - c.x0, height: c.y1 - c.y0, class: "ch-h " + cls(c.kind) }));
      const g3 = LU.svg("g", { "data-probe": "" });
      svgEl.append(g1, g2, g3);
      drawProbe();
      controls();
    }

    function drawProbe() {
      const g = svgEl.querySelector("[data-probe]");
      g.replaceChildren(
        LU.svg("line", { x1: probe.x + 0.5, y1: 0, x2: probe.x + 0.5, y2: s.h, class: "ch-probe" }),
        LU.svg("line", { x1: 0, y1: probe.y + 0.5, x2: s.w, y2: probe.y + 0.5, class: "ch-probe" }),
        LU.svg("circle", { cx: probe.x + 0.5, cy: probe.y + 0.5, r: 6 * m.scale, class: "ch-probe-dot" }),
      );
      readout();
    }

    function readout() {
      const r = judge(s, m, probe.x, probe.y);
      const [why, src] = reason(s, m, r, probe.x, probe.y);
      const layout = H.makeCaptionLayout(r.band, s.dpi, m.frame.caption, m.frame.buttons);
      const rows = [
        ["점", `(${probe.x}, ${probe.y}) 물리 px`],
        ["판정", `${KO[r.hit]}${r.flipped ? " (client 칸)" : ""}`],
        ["OS에 주는 값", HT[r.hit]],
        ["까닭", why],
        ["근거", src],
        ["띠", r.band.width && r.band.height ? `{${r.band.left}, ${r.band.top}, ${r.band.width}, ${r.band.height}}` : "없음"],
        ["버튼 왼쪽", r.band.width && r.band.height ? `최소 ${layout.minimizeLeft} · 최대 ${layout.maximizeLeft} · 닫기 ${layout.closeLeft} (폭 ${layout.buttonWidth})` : "—"],
      ];
      read.replaceChildren(...rows.flatMap(([k, v]) => [LU.el("dt", { text: k }), LU.el("dd", { text: v })]));
    }

    function entry(input, msg, eff) {
      log.prepend(LU.el("div", { class: "ch-entry" }, LU.el("span", { class: "in", text: input }), LU.el("span", { class: "msg", text: msg }), eff ? LU.el("span", { class: "eff", text: eff }) : null));
      while (log.children.length > 40) log.lastElementChild.remove();
    }

    // caption_surface.cpp:467-500 — 비클라이언트의 누름과 뗌. 단추 위의 누름이면 그 판정을 적어 두고(pressed_caption_hit_),
    // 뗌에서 caption_click_executes로 짝을 본다. client 자리의 누름·뗌은 이 처리에 오지 않는다 (client 영역의 메시지다).
    let pressedCaptionHit = "nowhere";
    let down = null;
    const named = (r) => `${KO[r.hit]}${r.flipped ? " · client 칸" : ""}`;
    function press(button, at) {
      const r = judge(s, m, at.x, at.y);
      down = { button, at, r, note: "" };
      if (button !== "left" || r.hit === "client") return;
      // WM_NCLBUTTONDOWN (전체 화면은 어디나 client라 여기에 오지 않는다)
      // 떠 있는 popup을 먼저 닫고, 단추면 그 판정을 적고 누름을 삼킨다. 아니면 기본 처리로 넘긴다 —
      // 끌기 띠와 가장자리는 그 기본 처리가 OS의 끌기·크기 조절 루프다 (caption_surface.cpp:468-470의 주석).
      pressedCaptionHit = H.isCaptionButton(r.hit) ? r.hit : "nowhere";
      down.note = H.isCaptionButton(r.hit) ? "누름: 단추 위라 삼킨다." : r.hit === "caption_drag" ? "누름: 기본 처리로 넘긴다 — OS의 창 끌기 루프가 잡는다." : isEdge(r.hit) ? "누름: 기본 처리로 넘긴다 — OS의 크기 조절 루프가 잡는다." : "누름: 기본 처리로 넘긴다 (그 뒤는 OS의 동작이다).";
      down.note += " 떠 있는 popup은 바깥 누름으로 닫힌다.";
    }
    function release(button, at) {
      const r = judge(s, m, at.x, at.y);
      const d = down && down.button === button ? down : null;
      down = null;
      const side = button === "right" ? "오른쪽" : "왼쪽";
      const same = d && d.at.x === at.x && d.at.y === at.y;
      const title = same ? `${side} 누르고 뗌 (${at.x}, ${at.y}) ${named(r)}` : `${side} 누름 ${d ? `(${d.at.x}, ${d.at.y}) ${named(d.r)}` : "?"} → 뗌 (${at.x}, ${at.y}) ${named(r)}`;
      const inner = r.flipped ? (m.search && H.contains(m.search, at.x, at.y) ? "그 자리의 element는 검색 칸(명령 팔레트 입구)이다." : "그 자리의 element는 앱 단추다.") : "";
      if (r.hit === "client") {
        // WM_LBUTTONUP·WM_RBUTTONUP — 비클라이언트 처리는 받지 않는다. 적어 둔 누름도 그대로 남는다.
        const msg = s.mode === "fullscreen" ? "HTCLIENT — 전체 화면에는 비클라이언트가 없다" : "HTCLIENT — client 영역의 뗌";
        if (d && d.r.hit === "client") return entry(title, msg, `input thread가 게시된 tree에서 hit test한다. 같은 element에서 누르고 떼야 클릭이다. ${inner}`.trim());
        return entry(title, msg, `${d && d.note ? d.note + " " : ""}비클라이언트 처리는 이 뗌을 받지 않는다. 누를 때 적어 둔 값도 그대로 남는다.`);
      }
      if (button === "right") {
        // WM_NCRBUTTONUP
        if (r.hit === "caption_drag" || r.hit === "system_menu") return entry(title, "WM_NCRBUTTONUP → 시스템 메뉴", "커서 자리에 창의 시스템 메뉴를 연다. 고른 명령은 WM_SYSCOMMAND로 간다.");
        return entry(title, `${HT[r.hit]} — 기본 처리로 넘긴다`, "luil이 따로 하는 일이 없다.");
      }
      // WM_NCLBUTTONUP
      const pressedHit = pressedCaptionHit;
      pressedCaptionHit = "nowhere";
      const before = d && d.note ? d.note + " " : "";
      if (H.captionClickExecutes(pressedHit, r.hit, s.mode)) {
        const close = r.hit === "close_button";
        return entry(title, `${HT[r.hit]} → tree의 단추 액션 → ${COMMAND[r.hit]}`, before + (close ? "뗌에서 UI thread가 닫힘 요청을 낸다: ui_frame::close가 있으면 그 동작만, 없으면 앱을 끝낸다." : "뗌에서 UI thread가 창 명령을 실행한다 (WM_SYSCOMMAND)."));
      }
      if (H.isCaptionButton(r.hit)) return entry(title, "짝이 맞지 않는 뗌 — 삼킨다", `${before}누를 때 적어 둔 값이 ${pressedHit === "nowhere" ? "HTNOWHERE" : HT[pressedHit]}이다. 누른 단추에서 뗀 것만 실행한다.`);
      const moved = d && (d.r.hit === "caption_drag" || isEdge(d.r.hit));
      return entry(title, `${HT[r.hit]} — 뗌은 기본 처리로 넘긴다`, before + (moved ? "끌기·크기 조절이 끝나면 배치 보고가 온다." : "luil은 이 뗌을 따로 다루지 않는다."));
    }

    function at(ev) {
      const rect = svgEl.getBoundingClientRect();
      const x = Math.floor(((ev.clientX - rect.left) / rect.width) * s.w);
      const y = Math.floor(((ev.clientY - rect.top) / rect.height) * s.h);
      return { x: Math.min(Math.max(x, 0), s.w - 1), y: Math.min(Math.max(y, 0), s.h - 1) };
    }
    svgEl.addEventListener("pointermove", (ev) => {
      probe = at(ev);
      drawProbe();
    });
    svgEl.addEventListener("pointerdown", (ev) => {
      if (ev.button !== 0 && ev.button !== 2) return;
      probe = at(ev);
      drawProbe();
      press(ev.button === 2 ? "right" : "left", probe);
    });
    svgEl.addEventListener("pointerup", (ev) => {
      if (ev.button !== 0 && ev.button !== 2) return;
      probe = at(ev);
      drawProbe();
      release(ev.button === 2 ? "right" : "left", probe);
    });
    svgEl.addEventListener("contextmenu", (ev) => ev.preventDefault());

    mapBox.addEventListener("keydown", (ev) => {
      const step = ev.shiftKey ? 10 : 1;
      const moves = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] };
      if (moves[ev.key]) {
        probe = { x: Math.min(Math.max(probe.x + moves[ev.key][0], 0), s.w - 1), y: Math.min(Math.max(probe.y + moves[ev.key][1], 0), s.h - 1) };
        drawProbe();
        ev.preventDefault();
      } else if (ev.key === "Enter" || ev.key === " ") {
        press("left", probe);
        release("left", probe);
        ev.preventDefault();
      } else if (ev.key === "ContextMenu" || (ev.key === "F10" && ev.shiftKey)) {
        press("right", probe);
        release("right", probe);
        ev.preventDefault();
      }
    });
    $("left").addEventListener("click", () => {
      press("left", probe);
      release("left", probe);
    });
    $("right").addEventListener("click", () => {
      press("right", probe);
      release("right", probe);
    });

    const bindNumber = (name, key) => $(name).addEventListener("input", (e) => {
      s = { ...s, [key]: Number(e.target.value) };
      render();
    });
    bindNumber("w", "w");
    bindNumber("h", "h");
    bindNumber("dpi", "dpi");
    bindNumber("apps", "apps");
    for (const key of ["mode", "place"]) $(key).addEventListener("change", (e) => {
      s = { ...s, [key]: e.target.value };
      render();
    });
    for (const key of ["min", "max", "close", "search"]) $(key).addEventListener("change", (e) => {
      s = { ...s, [key]: e.target.checked };
      render();
    });

    root.addEventListener("lw-reset", () => {
      s = start();
      probe = { x: Math.floor(s.w / 2), y: 18 };
      log.replaceChildren();
      render();
    });
    LU.onTheme(render);

    render();
    renderReference($("ref"));
  }

  /* ---------- 시험 기준값: tests/caption_layout_tests.cpp가 못 박은 값을 옮긴 함수로 다시 잰다 ---------- */
  function referenceChecks() {
    const M = { height: 36, buttonWidth: 42, iconSlot: 40 };
    const ALL = { minimize: true, maximize: true, close: true };
    const frame = (buttons = ALL, border = 4, corner = 10) => ({ caption: M, buttons, border, corner });
    const L = (w, dpi, buttons = ALL) => H.makeCaptionLayout(H.defaultCaptionBand(w, dpi, M), dpi, M, buttons);
    const band = (b) => `{${b.left},${b.top},${b.width},${b.height}}`;
    const placed = (slot, scale = 1, visible = true) => {
      const a = H.arrangeCaption(slot, scale, M, ALL, []);
      return { tree: { children: [{ caption: true, visible, bounds: a.bounds, children: [] }] }, arranged: a };
    };
    const hw = (b, x, y, w = 1000, h = 800, dpi = 96) => H.hitTestWindow(frame(), "normal", w, h, dpi, x, y, b);
    const cases = [];
    const add = (label, src, checks) => cases.push({ label, src, checks });

    let l = L(1000, 96);
    const s144 = L(1500, 144);
    add("DPI에 따른 버튼 자리", "tests/caption_layout_tests.cpp:15-27", [[l.height, 36], [l.buttonWidth, 42], [l.closeLeft, 958], [l.maximizeLeft, 916], [l.minimizeLeft, 874], [s144.height, 54], [s144.buttonWidth, 63]]);
    add("끌기와 창 버튼", "tests/caption_layout_tests.cpp:29-37", [[H.hitTestCaption(l, 100, 20), "drag"], [H.hitTestCaption(l, 880, 20), "minimize"], [H.hitTestCaption(l, 930, 20), "maximize"], [H.hitTestCaption(l, 980, 20), "close"], [H.hitTestCaption(l, 100, 60), "client"]]);
    l = L(1000, 96, { minimize: false, maximize: false, close: true });
    add("닫기만 둔 caption", "tests/caption_layout_tests.cpp:39-54", [[l.closeLeft, 958], [l.maximizeLeft, 958], [l.minimizeLeft, 958], [H.hitTestCaption(l, 980, 20), "close"], [H.hitTestCaption(l, 930, 20), "drag"], [H.hitTestCaption(l, 880, 20), "drag"]]);
    l = L(1000, 96, { minimize: true, maximize: false, close: true });
    const bare = L(1000, 96, { minimize: false, maximize: false, close: false });
    add("최대화를 뺀 caption · 버튼 없는 caption", "tests/caption_layout_tests.cpp:56-72", [[l.closeLeft, 958], [l.maximizeLeft, 958], [l.minimizeLeft, 916], [H.hitTestCaption(l, 980, 20), "close"], [H.hitTestCaption(l, 930, 20), "minimize"], [H.hitTestCaption(l, 880, 20), "drag"], [H.hitTestCaption(bare, 980, 20), "drag"]]);

    // 앱 단추: make_caption_tree(1000, 1.0, config) — 단추는 그린 자리(실수)를 정수로 자른 값이 판정 자리와 맞물린다.
    l = L(1000, 96);
    const apps = H.arrangeCaption({ x: 0, y: 0, width: 1000 }, 1, M, ALL, [{ key: "search" }, { key: "settings" }]);
    add("앱 단추는 창 버튼 왼쪽에 목록 차례로", "tests/caption_layout_tests.cpp:138-166", [[Math.trunc(apps.apps[1].bounds.x), l.minimizeLeft - l.buttonWidth], [Math.trunc(apps.apps[0].bounds.x), l.minimizeLeft - 2 * l.buttonWidth], [Math.trunc(apps.buttons.minimize.x), l.minimizeLeft]]);
    const two = H.arrangeCaption({ x: 0, y: 0, width: 1000 }, 1, M, ALL, [{ key: "settings" }, { key: "off", enabled: false }]);
    const tree2 = { children: [{ caption: true, bounds: two.bounds, children: two.apps.map((a) => ({ captionClient: true, bounds: a.bounds })) }] };
    const offX = l.minimizeLeft - Math.trunc(l.buttonWidth / 2);
    const setX = l.minimizeLeft - l.buttonWidth - Math.trunc(l.buttonWidth / 2);
    const plain = { children: [{ caption: true, bounds: two.bounds, children: [] }] };
    add("앱 단추 위는 client", "tests/caption_layout_tests.cpp:168-189", [[H.hitTestCaption(l, setX, 18), "drag"], [H.captionClientAt(tree2, setX, 18), true], [H.captionClientAt(tree2, offX, 18), true], [H.captionClientAt(tree2, 100, 18), false], [H.captionClientAt(tree2, setX, 60), false], [H.captionClientAt(null, setX, 18), false], [H.captionClientAt(plain, setX, 18), false]]);

    const field = (x, y, w, h, extra) => ({ captionClient: true, bounds: { x, y, width: w, height: h }, ...extra });
    const tree3 = { children: [{ caption: true, bounds: { x: 0, y: 0, width: 1000, height: 36 }, children: [] }, field(400, 4, 200, 28), field(100, 4, 28, 28), field(200, 4, 28, 28, { visible: false }), { clipChildren: true, bounds: { x: 700, y: 4, width: 40, height: 28 }, children: [field(700, 4, 120, 28)] }] };
    add("겹쳐 세운 칸: 비활성·숨김·잘림", "tests/caption_layout_tests.cpp:227-266", [[H.hitTestCaption(l, 500, 18), "drag"], [H.captionClientAt(tree3, 500, 18), true], [H.captionClientAt(tree3, 380, 18), false], [H.captionClientAt(tree3, 110, 18), true], [H.captionClientAt(tree3, 210, 18), false], [H.captionClientAt(tree3, 720, 18), true], [H.captionClientAt(tree3, 780, 18), false]]);
    const fs = frame(ALL, 8, 16);
    const C = H.captionClickExecutes;
    add("누른 단추에서 뗀 것만 실행", "tests/caption_layout_tests.cpp:276-288", [[C("close_button", "close_button", "normal"), true], [C("maximize_button", "maximize_button", "maximized"), true], [C("nowhere", "close_button", "normal"), false], [C("minimize_button", "close_button", "normal"), false], [C("caption_drag", "caption_drag", "normal"), false], [C("close_button", "close_button", "fullscreen"), false]]);
    add("전체 화면은 어디나 client", "tests/caption_layout_tests.cpp:268-274", [[H.hitTestWindow(fs, "fullscreen", 1000, 800, 96, 500, 18, H.defaultCaptionBand(1000, 96, M)), "client"], [H.hitTestWindow(fs, "normal", 1000, 800, 96, 500, 18, H.defaultCaptionBand(1000, 96, M)), "caption_drag"]]);

    let p = placed({ x: 0, y: 764, width: 1000 });
    let b = H.captionBandIn(p.tree, 1000, 96, M);
    add("창 아래에 둔 caption", "tests/caption_layout_tests.cpp:314-332", [[band(b), "{0,764,1000,36}"], [hw(b, 300, 780), "caption_drag"], [hw(b, 20, 780), "system_menu"], [hw(b, 880, 780), "minimize_button"], [hw(b, 930, 780), "maximize_button"], [hw(b, 980, 780), "close_button"], [hw(b, 300, 20), "client"], [hw(b, 980, 20), "client"], [hw(b, 300, 1), "resize_top"], [hw(b, 300, 798), "resize_bottom"], [hw(b, 998, 798), "resize_bottom_right"]]);
    p = placed({ x: 200, y: 300, width: 600 });
    b = H.captionBandIn(p.tree, 1000, 96, M);
    l = H.makeCaptionLayout(b, 96, M, ALL);
    add("창 일부만 덮은 caption", "tests/caption_layout_tests.cpp:334-356", [[band(b), "{200,300,600,36}"], [Math.trunc(p.arranged.buttons.close.x), l.closeLeft], [Math.trunc(p.arranged.buttons.minimize.x), l.minimizeLeft], [Math.trunc(p.arranged.buttons.close.y), l.top], [hw(b, 790, 310), "close_button"], [hw(b, 210, 310), "system_menu"], [hw(b, 400, 310), "caption_drag"], [hw(b, 100, 310), "client"], [hw(b, 900, 310), "client"], [hw(b, 400, 290), "client"], [hw(b, 400, 340), "client"]]);
    add("반 픽셀 자리는 반올림", "tests/caption_layout_tests.cpp:358-363", [[band(H.captionBandIn(placed({ x: 0, y: 400.4, width: 1000 }).tree, 1000, 96, M)), "{0,400,1000,36}"]]);
    b = H.captionBandIn(placed({ x: 0, y: 0, width: 1000 }, 1, false).tree, 1000, 96, M);
    add("숨긴 caption", "tests/caption_layout_tests.cpp:365-373", [[band(b), "{0,0,0,0}"], [hw(b, 300, 20), "client"], [hw(b, 980, 20), "client"], [hw(b, 20, 20), "client"]]);
    const top = H.defaultCaptionBand(1000, 96, M);
    add("tree가 없거나 caption이 없으면 맨 위 띠", "tests/caption_layout_tests.cpp:375-387", [[band(top), "{0,0,1000,36}"], [band(H.captionBandIn(null, 1000, 96, M)), band(top)], [band(H.captionBandIn({ children: [] }, 1000, 96, M)), band(top)]]);
    p = placed({ x: 0, y: 1146, width: 1500 }, 1.5);
    b = H.captionBandIn(p.tree, 1500, 144, M);
    l = H.makeCaptionLayout(b, 144, M, ALL);
    add("배율 150%", "tests/caption_layout_tests.cpp:389-405", [[band(b), "{0,1146,1500,54}"], [Math.trunc(p.arranged.buttons.close.x), l.closeLeft], [hw(b, 1480, 1170, 1500, 1200, 144), "close_button"]]);
    return cases;
  }

  function renderReference(table) {
    if (!table || !window.LU) return;
    const LU = window.LU;
    const rows = referenceChecks().map((c) => {
      const ok = c.checks.filter(([a, e]) => a === e).length;
      const all = ok === c.checks.length;
      const [path, range] = c.src.split(":");
      return LU.el("tr", {}, LU.el("td", { text: c.label }), LU.el("td", {}, LU.el("a", { href: LU.sourceUrl(path, ...range.split("-").map(Number)), target: "_blank", rel: "noopener", title: c.src, text: range })), LU.el("td", { class: all ? "ok" : "bad", text: `${ok}/${c.checks.length} ${all ? "같음" : "다름"}` }));
    });
    table.replaceChildren(LU.el("thead", {}, LU.el("tr", {}, LU.el("th", { text: "시험이 못 박은 것" }), LU.el("th", { text: "줄" }), LU.el("th", { text: "옮긴 함수" }))), LU.el("tbody", {}, rows));
  }

  // build·judge·cuts는 지도가 칸마다 한 점으로 칠해도 되는지 대조할 때 쓴다.
  window.LuilWindowLab = { setup, referenceChecks, model: { DEFAULTS, build, judge, cuts } };
})();

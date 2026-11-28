/* luil 설명서 — 이미지·영상·웹·잉크 실습
 *
 * 1) 잉크 획: ink_canvas_element + 앱 상태(ink_layer·진행 중 점·도구)를 브라우저로 재현한다.
 *    판정은 전부 assets/mediamodel.js(원본 C++과 대조한 이식)를 부른다 — ink_width_at, make_ink_stroke(원·사다리꼴),
 *    simplify_ink_points, ink_strokes_hit. 앱의 몫은 examples/demo/ink_sketch.h를 옮겼다(점을 모으다 end에서 굳힌다).
 * 2) 자막: read_srt·subtitle_at을 글 상자와 시간 막대에 붙였다.
 * 그림은 luil의 실제 렌더링(Skia)이 아니라 같은 모양 목록을 브라우저 canvas의 nonzero 채우기로 그린 재현이다.
 */
(function () {
  "use strict";
  const { el, icon, cssVar, onTheme } = window.LU;
  const M = window.LuilMedia;
  const f = Math.fround;

  const fmt = (v, digits) => (Number.isFinite(v) ? Number(v.toFixed(digits === undefined ? 2 : digits)).toString() : String(v));

  /* ---------------- 잉크 ---------------- */

  // examples/demo/ink_sketch.h의 값이다: style() {4, 0.25}, eraser_radius 10, simplify(0.25, 0.05)
  const DEFAULTS = { width: 4, ratio: 0.25, tolerance: 0.25, pressureTolerance: 0.05, eraserRadius: 10, scale: 1, tool: "pen", synthetic: false, syntheticPressure: 0.5, showDiscs: false, showPoints: true };

  function setupInk(root) {
    if (!root) return;
    const q = (name) => root.querySelector(`[data-ink="${name}"]`);
    const canvas = q("canvas");
    const ctx = canvas.getContext("2d");
    const state = {
      layer: [], // ink_layer — 굳은 획 (makeInkStroke 결과)
      points: [], // 진행 중 점 (ink_sketch::points_)
      device: "pen",
      reported: false,
      nextId: 1,
      last: null, // 마지막으로 굳힌 획의 기록
      contact: null, // { pointerId, eraser, previous }
      settings: Object.assign({}, DEFAULTS),
    };
    const style = () => ({ width: f(state.settings.width), minimum_ratio: f(state.settings.ratio) });
    const log = q("log");

    function addLog(kind, text) {
      const t = (performance.now() / 1000).toFixed(1);
      const row = el("div", { class: "row " + kind }, el("span", { class: "t", text: t }), el("span", { class: "m", text }));
      log.prepend(row);
      while (log.childElementCount > 40) log.lastElementChild.remove();
    }

    // 캔버스 크기: 내용 논리 픽셀 = CSS 픽셀 / 보기 배율
    let cssWidth = 0;
    let cssHeight = 0;
    function resize() {
      const rect = canvas.getBoundingClientRect();
      cssWidth = Math.max(1, Math.round(rect.width));
      cssHeight = Math.max(1, Math.round(rect.height));
      const dpr = window.devicePixelRatio || 1;
      canvas.width = Math.round(cssWidth * dpr);
      canvas.height = Math.round(cssHeight * dpr);
      draw();
    }

    // ink_content_point(slot, scale, rotation = 0, content_bounds = 없음, x, y) — 원점은 slot의 왼쪽 위다
    function contentPoint(event) {
      const rect = canvas.getBoundingClientRect();
      const scale = state.settings.scale;
      return { x: f((event.clientX - rect.left) / scale), y: f((event.clientY - rect.top) / scale) };
    }

    function strokePath(stroke) {
      const path = new Path2D();
      for (const op of stroke.ops) {
        if (op.kind === "circle") {
          path.moveTo(op.x + op.r, op.y);
          path.arc(op.x, op.y, op.r, 0, Math.PI * 2, false); // 시계 방향 (SkPathDirection::kCW, y가 아래로)
          path.closePath();
        } else {
          path.moveTo(op.pts[0][0], op.pts[0][1]);
          for (let i = 1; i < op.pts.length; i++) path.lineTo(op.pts[i][0], op.pts[i][1]);
          path.closePath();
        }
      }
      return path;
    }

    function draw() {
      const dpr = window.devicePixelRatio || 1;
      const scale = state.settings.scale;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.setTransform(dpr * scale, 0, 0, dpr * scale, 0, 0);
      const fg = cssVar("--fg") || "#000";
      const accent = cssVar("--accent") || "#007258";
      const muted = cssVar("--fg-3") || "#888";
      // 굳은 획: 색 없는 획은 그리는 시점 팔레트의 primary_foreground (여기서는 글자색)
      ctx.fillStyle = fg;
      for (const stroke of state.layer) ctx.fill(strokePath(stroke), "nonzero");
      // 진행 중 획: frame마다 make_ink_stroke(0, points_, style())로 다시 짓는다
      if (state.points.length) {
        const wet = M.ink.makeInkStroke(0, state.points, style());
        ctx.fill(strokePath(wet), "nonzero");
        if (state.settings.showDiscs) drawDiscs(wet, accent);
      }
      if (state.settings.showDiscs) for (const stroke of state.layer) drawDiscs(stroke, accent);
      if (state.settings.showPoints && state.last && state.layer.some((s) => s.id === state.last.id)) {
        const px = 1 / scale;
        ctx.fillStyle = muted;
        for (const p of state.last.raw) ctx.fillRect(p.x - 1.5 * px, p.y - 1.5 * px, 3 * px, 3 * px);
        ctx.strokeStyle = accent;
        ctx.lineWidth = 1.5 * px;
        for (const p of state.last.kept) {
          ctx.beginPath();
          ctx.arc(p.x, p.y, 3.5 * px, 0, Math.PI * 2);
          ctx.stroke();
        }
      }
    }

    function drawDiscs(stroke, color) {
      const px = 1 / state.settings.scale;
      ctx.strokeStyle = color;
      ctx.lineWidth = 0.75 * px;
      ctx.globalAlpha = 0.6;
      for (const d of stroke.discs) {
        if (d.r <= 0) continue;
        ctx.beginPath();
        ctx.arc(d.x, d.y, d.r, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.globalAlpha = 1;
    }

    // ink_sketch::apply — 점을 모으다가 끝에 굳힌다. 끊긴 끝(interrupted)도 받은 점까지가 획이다.
    function apply(input) {
      if (input.phase === "begin") {
        state.points = [input.point];
        state.device = input.device;
        state.reported = input.pressure_reported;
      } else if (input.phase === "extend") state.points.push(input.point);
      else if (input.phase === "end") {
        if (!state.points.length) return;
        const raw = state.points;
        state.points = [];
        const s = state.settings;
        const kept = M.ink.simplifyInkPoints(raw, f(s.tolerance), f(s.pressureTolerance));
        const stroke = M.ink.makeInkStroke(state.nextId++, kept, style());
        if (!stroke.empty) state.layer.push(stroke); // ink_layer::with — 빈 획은 더하지 않는다
        const pressures = kept.map((p) => p.pressure);
        state.last = { id: stroke.id, raw, kept, device: state.device, reported: state.reported, minP: Math.min(...pressures), maxP: Math.max(...pressures), discs: stroke.discs.length, bridges: stroke.ops.filter((o) => o.kind === "polygon").length };
        addLog("good", `앱: layer_ = layer_.with(make_ink_stroke(${stroke.id}, simplify_ink_points(점 ${raw.length}개, ${fmt(s.tolerance)}, ${fmt(s.pressureTolerance)}) → ${kept.length}개, style))`);
      } else state.points = []; // cancel
    }

    let extendCount = 0;
    function emitStroke(input) {
      if (input.phase === "extend") {
        extendCount++;
      } else {
        if (extendCount) addLog("in", `on_stroke × ${extendCount} — phase = extend`);
        extendCount = 0;
        const p = input.point;
        addLog("in", `on_stroke — phase = ${input.phase}, device = ${input.device}, point = (${fmt(p.x, 1)}, ${fmt(p.y, 1)}, 압력 ${fmt(p.pressure)}), pressure_reported = ${input.pressure_reported}`);
      }
      apply(input);
    }

    function erase(from, to) {
      const ids = M.ink.inkStrokesHit(state.layer, from, to, f(state.settings.eraserRadius));
      if (!ids.length) return;
      addLog("in", `on_erase({ ${ids.join(", ")} })`);
      const before = state.layer.length;
      state.layer = state.layer.filter((s) => !ids.includes(s.id)); // ink_layer::without — 없는 id는 무시한다
      addLog("good", `앱: layer_ = layer_.without(ids) — 지운 획 ${before - state.layer.length}`);
    }

    // 표본 하나: PointerEvent → ink_stroke_input (압력은 장치가 알릴 때만. 0~1로 접는다)
    function sampleOf(event) {
      const point = contentPoint(event);
      let pressure = 1;
      let reported = false;
      let device = event.pointerType === "pen" ? "pen" : event.pointerType === "touch" ? "touch" : "mouse";
      if (device === "pen") {
        pressure = f(Math.min(1, Math.max(0, event.pressure)));
        reported = true;
      } else if (device === "mouse" && state.settings.synthetic) {
        device = "pen";
        pressure = f(state.settings.syntheticPressure);
        reported = true;
      }
      return { device, point: { x: point.x, y: point.y, pressure }, reported };
    }

    canvas.addEventListener("pointerdown", (event) => {
      if (state.contact || (event.pointerType === "mouse" && event.button !== 0)) return;
      canvas.setPointerCapture(event.pointerId);
      event.preventDefault();
      const s = sampleOf(event);
      // 지우개 끝(펜의 buttons & 32)은 도구와 상관없이 지우개다
      const eraser = state.settings.tool === "eraser" || (event.pointerType === "pen" && (event.buttons & 32) !== 0);
      state.contact = { pointerId: event.pointerId, eraser, previous: s.point };
      if (eraser) erase(s.point, s.point);
      else emitStroke({ phase: "begin", device: s.device, point: s.point, pressure_reported: s.reported });
      draw();
      readout();
    });
    canvas.addEventListener("pointermove", (event) => {
      const c = state.contact;
      if (!c || c.pointerId !== event.pointerId) return;
      const events = event.getCoalescedEvents ? event.getCoalescedEvents() : [event];
      for (const e of events.length ? events : [event]) {
        const s = sampleOf(e);
        if (c.eraser) erase(c.previous, s.point);
        else emitStroke({ phase: "extend", device: s.device, point: s.point, pressure_reported: s.reported });
        c.previous = s.point;
      }
      draw();
    });
    const finish = (event, phase) => {
      const c = state.contact;
      if (!c || c.pointerId !== event.pointerId) return;
      state.contact = null;
      if (!c.eraser) {
        const s = sampleOf(event);
        emitStroke({ phase, device: s.device, point: phase === "end" ? s.point : c.previous, pressure_reported: s.reported });
      }
      draw();
      readout();
    };
    canvas.addEventListener("pointerup", (event) => finish(event, "end"));
    canvas.addEventListener("pointercancel", (event) => finish(event, "cancel"));

    // examples/demo/ink_sketch.h의 sample_actions — 정해 둔 물결 획 하나를 캔버스가 내는 것과 같은 메시지로 낸다
    function sampleStroke() {
      const shift = (state.layer.length % 6) * 24;
      const count = 16;
      for (let index = 0; index < count; index++) {
        const along = f(index / (count - 1));
        const point = { x: f(24 + along * 220), y: f(40 + shift + Math.sin(along * 6.2831853) * 16), pressure: f(0.2 + along * 0.8) };
        emitStroke({ phase: index === 0 ? "begin" : "extend", device: "pen", point, pressure_reported: true });
        if (index + 1 === count) emitStroke({ phase: "end", device: "pen", point, pressure_reported: true });
      }
      draw();
      readout();
    }

    // 폭 곡선: ink_width_at(style, p), p = 0…1
    function drawCurve() {
      const box = q("curve");
      box.replaceChildren();
      const W = 220;
      const H = 76;
      const s = style();
      const maxW = Math.max(1, s.width);
      const pts = [];
      for (let i = 0; i <= 20; i++) {
        const p = i / 20;
        const w = M.ink.widthAt(s, f(p));
        pts.push(`${(8 + p * (W - 16)).toFixed(1)},${(H - 14 - (w / maxW) * (H - 26)).toFixed(1)}`);
      }
      const svg = window.LU.svg;
      const g = svg("svg", { viewBox: `0 0 ${W} ${H}`, width: "100%", height: H, role: "img", "aria-label": `압력에 따른 폭: 압력 0에서 ${fmt(M.ink.widthAt(s, 0))}, 1에서 ${fmt(M.ink.widthAt(s, 1))}` });
      g.append(svg("line", { x1: 8, y1: H - 14, x2: W - 8, y2: H - 14, class: "ink-axis" }));
      g.append(svg("polyline", { points: pts.join(" "), class: "ink-curve" }));
      g.append(svg("text", { x: 8, y: H - 2, class: "ink-tick", text: "압력 0" }));
      g.append(svg("text", { x: W - 8, y: H - 2, class: "ink-tick", "text-anchor": "end", text: "1" }));
      g.append(svg("text", { x: 8, y: 10, class: "ink-tick", text: `폭 ${fmt(M.ink.widthAt(s, 0))} → ${fmt(M.ink.widthAt(s, 1))} (내용 논리 픽셀)` }));
      if (state.last) {
        for (const p of [state.last.minP, state.last.maxP]) {
          const w = M.ink.widthAt(s, p);
          g.append(svg("circle", { cx: (8 + p * (W - 16)).toFixed(1), cy: (H - 14 - (w / maxW) * (H - 26)).toFixed(1), r: 3, class: "ink-mark" }));
        }
      }
      box.append(g);
    }

    function readout() {
      const s = state.settings;
      const last = state.last;
      const stats = q("stats");
      stats.replaceChildren(
        el("div", {}, el("span", { class: "k", text: "획 수 (layer_.size())" }), el("span", { class: "v", text: String(state.layer.length) })),
        el("div", {}, el("span", { class: "k", text: "마지막 획: 받은 점 → 굳힌 점" }), el("span", { class: "v", text: last ? `${last.raw.length} → ${last.kept.length}` : "—" })),
        el("div", {}, el("span", { class: "k", text: "압력 범위 · 폭 범위" }), el("span", { class: "v", text: last ? `${fmt(last.minP)}~${fmt(last.maxP)} · ${fmt(M.ink.widthAt(style(), last.minP))}~${fmt(M.ink.widthAt(style(), last.maxP))}` : "—" })),
        el("div", {}, el("span", { class: "k", text: "원 · 사다리꼴" }), el("span", { class: "v", text: last ? `${last.discs} · ${last.bridges}` : "—" }))
      );
      q("cfg").textContent = `style { width = ${fmt(s.width, 1)}, minimum_ratio = ${fmt(s.ratio)} } · tool = ${s.tool === "pen" ? "ink_tool::pen" : "ink_tool::eraser"} · eraser_radius = ${s.eraserRadius} · mouse = true · touch = true · 배율 ${s.scale}`;
      q("last").textContent = last ? `마지막 획: ${last.device === "pen" ? "펜" : last.device === "touch" ? "손가락" : "마우스"}, ${last.reported ? "압력 있음" : "압력 없음"}` : "마지막 획: 없음";
      drawCurve();
    }

    // 손잡이
    function bindRange(name, key, digits) {
      const input = q(name);
      const out = q(name + "-out");
      const sync = () => {
        state.settings[key] = Number(input.value);
        if (out) out.textContent = fmt(state.settings[key], digits);
      };
      input.addEventListener("input", () => {
        sync();
        draw();
        readout();
      });
      return () => {
        input.value = String(state.settings[key]);
        if (out) out.textContent = fmt(state.settings[key], digits);
      };
    }
    const syncers = [bindRange("width", "width", 1), bindRange("ratio", "ratio", 2), bindRange("tol", "tolerance", 2), bindRange("ptol", "pressureTolerance", 2), bindRange("synp", "syntheticPressure", 2)];
    const toolSeg = q("tool");
    const scaleSeg = q("scale");
    function syncSeg(seg, value) {
      seg.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", b.dataset.value === String(value) ? "true" : "false"));
    }
    toolSeg.addEventListener("click", (e) => {
      const b = e.target.closest("button");
      if (!b) return;
      state.settings.tool = b.dataset.value;
      syncSeg(toolSeg, state.settings.tool);
      addLog("lg", `앱: tool_ = ${state.settings.tool === "pen" ? "ink_tool::pen" : "ink_tool::eraser"}`);
      readout();
    });
    scaleSeg.addEventListener("click", (e) => {
      const b = e.target.closest("button");
      if (!b) return;
      state.settings.scale = Number(b.dataset.value);
      syncSeg(scaleSeg, state.settings.scale);
      draw();
      readout();
    });
    const synth = q("syn");
    synth.addEventListener("change", () => {
      state.settings.synthetic = synth.checked;
      readout();
    });
    const discs = q("discs");
    discs.addEventListener("change", () => {
      state.settings.showDiscs = discs.checked;
      draw();
    });
    const pts = q("points");
    pts.addEventListener("change", () => {
      state.settings.showPoints = pts.checked;
      draw();
    });
    q("sample").addEventListener("click", sampleStroke);
    q("clear").addEventListener("click", () => {
      state.layer = [];
      state.points = [];
      state.last = null;
      addLog("good", "앱: layer_ = {} (모두 지우기)");
      draw();
      readout();
    });

    function reset() {
      state.layer = [];
      state.points = [];
      state.last = null;
      state.nextId = 1;
      state.contact = null;
      state.settings = Object.assign({}, DEFAULTS);
      syncers.forEach((fn) => fn());
      syncSeg(toolSeg, state.settings.tool);
      syncSeg(scaleSeg, state.settings.scale);
      synth.checked = false;
      discs.checked = false;
      pts.checked = true;
      log.replaceChildren();
      addLog("lg", "처음 상태 — 펜이나 마우스로 판에 그린다. 펜이 없으면 '견본 획'을 누른다.");
      draw();
      readout();
    }
    root.addEventListener("lw-reset", reset);
    onTheme(draw);
    window.addEventListener("resize", resize);
    reset();
    resize();
  }

  /* ---------------- 자막 ---------------- */

  const SAMPLE_SRT = [
    "1",
    "00:00:00,600 --> 00:00:03,700",
    "안개 사이로 능선이 겹쳐 보입니다.",
    "",
    "2",
    "00:00:03,700 --> 00:00:06,000",
    "첫 햇살이 바위 봉우리에 닿습니다.",
    "두 줄 자막입니다.",
    "",
    "3",
    "00:00:05,000 --> 00:00:07,650",
    "겹치는 자막은 줄을 나눠 함께 나온다.",
    "",
    "4",
    "00:00:08.000 --> 00:00:09,000",
    "점(.)으로 적은 시각은 버린다.",
    "",
    "5",
    "00:00:09,500 --> 00:00:09,500",
    "끝이 시작과 같으면 버린다.",
    "",
    "6",
    "0:00:10,000 --> 00:00:11,000",
    "한 자리 시도 버린다.",
    "",
  ].join("\n");

  function setupSrt(root) {
    if (!root) return;
    const q = (name) => root.querySelector(`[data-srt="${name}"]`);
    const text = q("text");
    const slider = q("time");
    const number = q("ms");
    const now = q("now");
    const bar = q("bar");
    const list = q("blocks");
    let parsed = { cues: [], blocks: [] };
    let span = 10000;

    function parse() {
      parsed = M.srt.readSrt(text.value);
      const ends = parsed.cues.map((c) => c.end_ms);
      span = Math.max(1000, Math.ceil((Math.max(0, ...ends) + 1000) / 1000) * 1000);
      slider.max = String(span);
      number.max = String(span);
      renderBlocks();
      update();
    }

    function renderBlocks() {
      list.replaceChildren();
      if (!parsed.blocks.length) {
        list.append(el("div", { class: "srt-empty", text: "\"-->\"가 든 줄이 없다 — cue가 0개다." }));
        return;
      }
      for (const b of parsed.blocks) {
        const head = b.kept ? `${b.start} ~ ${b.end} ms` : "버림";
        list.append(
          el("div", { class: "srt-block" + (b.kept ? "" : " dropped") },
            el("span", { class: "srt-line", text: `${b.line}행` }),
            el("span", { class: "srt-range", text: head }),
            el("span", { class: "srt-body", text: b.kept ? b.text.replace(/\n/g, " ⏎ ") : b.problem })
          )
        );
      }
    }

    function update() {
      const at = Math.max(0, Math.min(span, Math.round(Number(slider.value) || 0)));
      number.value = String(at);
      const shown = M.srt.subtitleAt(parsed.cues, at);
      now.textContent = shown || "(빈 글 — 이 시각에 선 cue가 없다)";
      now.classList.toggle("empty", !shown);
      drawBar(at);
    }

    function drawBar(at) {
      const svg = window.LU.svg;
      const W = 600;
      const lanes = [];
      const placed = parsed.cues.map((c) => {
        let lane = lanes.findIndex((end) => end <= c.start_ms);
        if (lane < 0) {
          lane = lanes.length;
          lanes.push(0);
        }
        lanes[lane] = c.end_ms;
        return { c, lane };
      });
      const H = 20 + Math.max(1, lanes.length) * 16;
      const x = (ms) => 4 + (ms / span) * (W - 8);
      const g = svg("svg", { viewBox: `0 0 ${W} ${H}`, width: "100%", height: H, role: "img", "aria-label": `cue ${parsed.cues.length}개의 시간 막대, 지금 ${at} ms` });
      for (let s = 0; s <= span; s += 1000) g.append(svg("line", { x1: x(s), x2: x(s), y1: 0, y2: H - 12, class: "srt-grid" }), svg("text", { x: x(s) + 2, y: H - 2, class: "srt-tick", text: `${s / 1000}s` }));
      for (const { c, lane } of placed) {
        const active = at >= c.start_ms && at < c.end_ms;
        g.append(svg("rect", { x: x(c.start_ms), y: 4 + lane * 16, width: Math.max(1, x(c.end_ms) - x(c.start_ms)), height: 12, rx: 2, class: "srt-cue" + (active ? " on" : "") }));
      }
      g.append(svg("line", { x1: x(at), x2: x(at), y1: 0, y2: H - 12, class: "srt-now" }));
      bar.replaceChildren(g);
    }

    text.addEventListener("input", parse);
    slider.addEventListener("input", update);
    number.addEventListener("change", () => {
      slider.value = number.value;
      update();
    });
    root.querySelectorAll("[data-srt-step]").forEach((b) =>
      b.addEventListener("click", () => {
        slider.value = String(Number(slider.value) + Number(b.dataset.srtStep));
        update();
      })
    );
    root.addEventListener("lw-reset", () => {
      text.value = SAMPLE_SRT;
      slider.value = "3700";
      parse();
    });
    onTheme(() => update());
    text.value = SAMPLE_SRT;
    slider.value = "3700";
    parse();
  }

  window.LuilMediaLab = { setupInk, setupSrt };
})();

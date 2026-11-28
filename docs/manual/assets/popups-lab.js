/* luil 설명서 — 드롭다운·콤보·팝업 실습: 필드 popup 자리
 *
 * 두 부분이다.
 *  1) LuilFieldPopup — src/ui/field_popup.cpp의 place_field_popup, place_field_popup_at_side, field_popup_key_handler를
 *     함수 단위로 옮겼다. 이름 옆 주석이 C++ 원본의 이름이다.
 *     - float(32비트) 연산은 단계마다 Math.fround로 반올림한다. 원본의 계산 순서(왼쪽부터)를 그대로 따른다.
 *     - std::max(a, b)는 (a < b) ? b : a, std::min(a, b)는 (b < a) ? b : a다. Math.max·Math.min은 NaN을 달리 다뤄 쓰지 않는다.
 *     - 원본을 컴파일해 링크한 대조기가 낸 무작위·경계 입력(NaN·무한·0·음수 배율과 간격, 딱 맞는 경계 포함)과 비트 단위로 견주었다.
 *     - 브라우저 밖(node)에서도 불러 쓸 수 있게 module.exports로도 내보낸다.
 *  2) LuilPopupsLab — 그 함수를 부르는 실습 판. 그림은 luil의 렌더링이 아니라 같은 수치로 그린 재현이다.
 */
(function (root) {
  "use strict";

  /* ---------- 1) 원본을 옮긴 함수 ---------- */

  const F = Math.fround;
  const max = (a, b) => (a < b ? b : a); // std::max
  const min = (a, b) => (b < a ? b : a); // std::min
  const finite = Number.isFinite; // std::isfinite

  // field_popup_side: "below" | "above" | "left" | "right"
  function oppositeSide(side) {
    // opposite_side
    switch (side) {
      case "above":
        return "below";
      case "left":
        return "right";
      case "right":
        return "left";
      default:
        return "above";
    }
  }
  const verticalSide = (side) => side === "below" || side === "above"; // vertical_side

  // push_inside: 처음 가장자리를 나중에 지켜, length가 extent보다 크면 처음 가장자리에 붙는다.
  const pushInside = (value, length, start, extent) => max(min(value, F(F(start + extent) - length)), start);

  // 입력을 float로 맞춘다 (rect_f·size_f의 칸은 float다).
  const rect = (r) => ({ x: F(r.x), y: F(r.y), width: F(r.width), height: F(r.height) });

  function metrics(anchor, size, scale) {
    const factor = finite(scale) && scale > 0 ? F(scale) : 1;
    const width = F(max(0, F(size.width)) * factor);
    const height = F(max(0, F(size.height)) * factor);
    const gap = finite(anchor.gap) ? F(max(0, F(anchor.gap)) * factor) : 0;
    return { factor, width, height, gap, field: rect(anchor.field), limit: rect(anchor.limit) };
  }

  // place_field_popup(anchor, size, scale) → { x, y, above } (논리 픽셀)
  //   anchor: { field: rect(물리), limit: rect(물리), gap(논리, 기본 2), alignment: "start" | "end" }
  //   size: { width, height } (논리)
  function placeFieldPopup(anchor, size, scale) {
    const { factor, width, height, gap, field, limit } = metrics(anchor, size, scale);
    // 가로는 맞춘 뒤 limit 안으로 민다. 오른쪽 끝을 먼저 들이고 왼쪽 끝을 나중에 지킨다.
    let x = anchor.alignment === "end" ? F(F(field.x + field.width) - width) : field.x;
    x = min(x, F(F(limit.x + limit.width) - width));
    x = max(x, limit.x);
    // 기본은 아래다. 아래가 모자라고 위가 들어갈 때만 뒤집는다.
    const below = F(F(field.y + field.height) + gap);
    const above = F(F(field.y - gap) - height);
    const flip = F(below + height) > F(limit.y + limit.height) && above >= limit.y;
    return { x: F(x / factor), y: F((flip ? above : below) / factor), above: flip };
  }

  // place_field_popup_at_side(anchor, size, scale, preferred) → { x, y, side, fits } (논리 픽셀)
  function placeFieldPopupAtSide(anchor, size, scale, preferred) {
    const { factor, width, height, gap, field, limit } = metrics(anchor, size, scale);
    const mainOf = (side) => {
      switch (side) {
        case "above":
          return F(F(field.y - gap) - height);
        case "left":
          return F(F(field.x - gap) - width);
        case "right":
          return F(F(field.x + field.width) + gap);
        default:
          return F(F(field.y + field.height) + gap);
      }
    };
    const fits = (side) => {
      const main = mainOf(side);
      if (verticalSide(side)) return main >= limit.y && F(main + height) <= F(limit.y + limit.height);
      return main >= limit.x && F(main + width) <= F(limit.x + limit.width);
    };
    // 바라는 방향, 반대, 남은 둘 순서다. 남은 둘은 아래·위면 오른쪽·왼쪽, 옆이면 아래·위다.
    const vertical = verticalSide(preferred);
    const order = [preferred, oppositeSide(preferred), vertical ? "right" : "below", vertical ? "left" : "above"];
    let chosen = preferred;
    let found = false;
    for (const side of order) {
      if (!fits(side)) continue;
      chosen = side;
      found = true;
      break;
    }
    // 다른 축은 필드 가운데에 맞춘 뒤 민다. 들어가는 방향이 없으면 주 축도 민다.
    let x = 0;
    let y = 0;
    if (verticalSide(chosen)) {
      x = pushInside(F(field.x + F(F(field.width - width) / 2)), width, limit.x, limit.width);
      y = mainOf(chosen);
      if (!found) y = pushInside(y, height, limit.y, limit.height);
    } else {
      x = mainOf(chosen);
      y = pushInside(F(field.y + F(F(field.height - height) / 2)), height, limit.y, limit.height);
      if (!found) x = pushInside(x, width, limit.x, limit.width);
    }
    return { x: F(x / factor), y: F(y / factor), side: chosen, fits: found };
  }

  // field_popup_key_handler(open, set_open, down_opens)가 키 하나에 내는 답.
  //   event: { key: "arrow_down" | "f4" | …, control, alt, shift, meta, primary_shortcut(없으면 control) }
  //   → "open"(set_open(true)) | "swallow"(빈 목록 — 이미 열려 있다) | null(nullopt — 다른 처리로 흐른다)
  function fieldPopupKey(open, event, downOpens) {
    const primary = event.primary_shortcut == null ? !!event.control : !!event.primary_shortcut; // primary_shortcut_down
    const modifier = !!event.control || !!event.alt || !!event.meta || primary; // shortcut_modifier_down
    const bare = !event.shift && !modifier; // bare_key
    const altOnly = !!event.alt && !event.shift && !event.control && !event.meta && !primary; // alt_only
    const arrowDown = event.key === "arrow_down";
    const opens = (arrowDown && altOnly) || (event.key === "f4" && bare) || (downOpens && arrowDown && bare);
    if (!opens) return null;
    if (open) return "swallow";
    return "open";
  }

  const FieldPopup = { placeFieldPopup, placeFieldPopupAtSide, fieldPopupKey };
  if (typeof module !== "undefined" && module.exports) module.exports = FieldPopup;
  root.LuilFieldPopup = FieldPopup;

  /* ---------- 2) 실습 판 ---------- */

  if (typeof document === "undefined") return;

  const LIMIT_W = 480; // 앵커 표면 client (논리 픽셀) — 실습에서 정한 값
  const LIMIT_H = 300;
  const MARGIN = 64; // limit 밖도 보이게 둘레를 둔다 (popup은 limit 밖으로 걸칠 수 있다)
  const FIELD_W = 150;
  const FIELD_H = 28; // dropdown_height
  const SIDE_NAMES = { below: "아래", above: "위", left: "왼쪽", right: "오른쪽" };
  const fmt = (v) => {
    if (Number.isNaN(v)) return "nan";
    const r = Math.round(v * 100) / 100;
    return (Object.is(r, -0) ? 0 : r).toString();
  };

  function setup(rootEl) {
    if (!rootEl || !window.LU) return;
    const { el, svg } = window.LU;
    const q = (name) => rootEl.querySelector(`[data-fp="${name}"]`);
    const stage = q("stage");
    const stateBox = q("state");
    const logBox = q("log");
    const controls = {
      mode: q("mode"),
      side: q("side"),
      align: q("align"),
      width: q("width"),
      height: q("height"),
      gap: q("gap"),
      scale: q("scale"),
      down: q("down"),
      fx: q("fx"),
      fy: q("fy"),
    };
    const outputs = { width: q("width-out"), height: q("height-out"), gap: q("gap-out"), fx: q("fx-out"), fy: q("fy-out") };

    const initial = () => ({ fx: 40, fy: 210, open: true });
    let S = initial();

    // 그림: viewBox는 논리 픽셀, limit은 (0,0)에서 시작한다.
    const view = svg("svg", { class: "fp-svg", viewBox: `${-MARGIN} ${-MARGIN} ${LIMIT_W + MARGIN * 2} ${LIMIT_H + MARGIN * 2}`, role: "img", "aria-label": "앵커 표면(limit)과 필드, 계산된 popup 자리" });
    const limitRect = svg("rect", { class: "fp-limit", x: 0, y: 0, width: LIMIT_W, height: LIMIT_H, rx: 4 });
    const limitLabel = svg("text", { class: "fp-cap", x: 8, y: 16, text: "limit — 앵커 표면 client" });
    const popupGroup = svg("g", { class: "fp-popup" });
    const popupRect = svg("rect", { class: "fp-pop", rx: 4 });
    const popupLabel = svg("text", { class: "fp-pop-text" });
    popupGroup.append(popupRect, popupLabel);
    const gapLine = svg("line", { class: "fp-gap" });
    const fieldGroup = svg("g", { class: "fp-field-g" });
    const fieldRect = svg("rect", { class: "fp-field", rx: 3, width: FIELD_W, height: FIELD_H });
    const fieldText = svg("text", { class: "fp-field-text", text: "보통" });
    const fieldChevron = svg("path", { class: "fp-chev" });
    fieldGroup.append(fieldRect, fieldText, fieldChevron);
    view.append(limitRect, limitLabel, gapLine, popupGroup, fieldGroup);
    const field = el("div", { class: "fp-field-hit", tabindex: "0", role: "combobox", "aria-haspopup": "menu", "aria-label": "필드 (끌어 옮기기 · 클릭 · Alt+↓ · F4)" });
    stage.append(view, field);

    function params() {
      const scale = Number(controls.scale.value);
      return {
        mode: controls.mode.value,
        side: controls.side.value,
        alignment: controls.align.value,
        width: Number(controls.width.value),
        height: Number(controls.height.value),
        gap: Number(controls.gap.value),
        scale,
        downOpens: controls.down.checked,
      };
    }

    function compute() {
      const p = params();
      const s = p.scale;
      // 필드와 limit은 물리 픽셀로 넘긴다 (bounds()와 같은 좌표). 크기와 간격은 논리다.
      const anchor = {
        field: { x: F(S.fx * s), y: F(S.fy * s), width: F(FIELD_W * s), height: F(FIELD_H * s) },
        limit: { x: 0, y: 0, width: F(LIMIT_W * s), height: F(LIMIT_H * s) },
        gap: p.gap,
        alignment: p.alignment,
      };
      const size = { width: p.width, height: p.height };
      const result = p.mode === "side" ? placeFieldPopupAtSide(anchor, size, s, p.side) : placeFieldPopup(anchor, size, s);
      return { p, anchor, size, result };
    }

    function describe(c) {
      const r = c.result;
      if (c.p.mode === "side") return `{ x: ${fmt(r.x)}, y: ${fmt(r.y)}, side: ${r.side}, fits: ${r.fits} }`;
      return `{ x: ${fmt(r.x)}, y: ${fmt(r.y)}, above: ${r.above} }`;
    }

    function call(c) {
      const a = c.anchor;
      const name = c.p.mode === "side" ? "place_field_popup_at_side" : "place_field_popup";
      const extra = c.p.mode === "side" ? `, field_popup_side::${c.p.side}` : "";
      return `${name}({ field ${fmt(a.field.x)},${fmt(a.field.y)} ${fmt(a.field.width)}×${fmt(a.field.height)}, gap ${fmt(c.p.gap)}${c.p.mode === "side" ? "" : ", " + c.p.alignment} }, ${fmt(c.size.width)}×${fmt(c.size.height)}, ${fmt(c.p.scale)}${extra})`;
    }

    function note(c) {
      const r = c.result;
      if (c.p.mode === "side") {
        if (!r.fits) return "네 방향 모두 모자라 바라는 방향에 두고 limit 안으로 밀었다 — 필드를 덮을 수 있다.";
        if (r.side !== c.p.side) return `바라는 방향(${SIDE_NAMES[c.p.side]})이 모자라 ${SIDE_NAMES[r.side]}에 놓았다.`;
        return `바라는 방향(${SIDE_NAMES[c.p.side]})에 들어간다. 다른 축은 필드 가운데에 맞춘 뒤 민다.`;
      }
      const bottom = F(r.y + c.p.height);
      if (r.above) return "아래가 limit을 넘고 위가 들어가서 위로 뒤집었다.";
      if (bottom > LIMIT_H) return "아래·위 모두 모자라 아래에 둔다. 나머지는 host가 작업 영역 안으로 민다.";
      return "기본 자리(필드 아래)에 들어간다.";
    }

    function draw() {
      const c = compute();
      const r = c.result;
      const p = c.p;
      fieldGroup.setAttribute("transform", `translate(${S.fx} ${S.fy})`);
      fieldText.setAttribute("x", 10);
      fieldText.setAttribute("y", FIELD_H / 2 + 4);
      fieldChevron.setAttribute("d", `M ${FIELD_W - 18} ${FIELD_H / 2 - 2} l 4 4 l 4 -4`);
      fieldRect.classList.toggle("open", S.open);
      field.style.left = `${((S.fx + MARGIN) / (LIMIT_W + MARGIN * 2)) * 100}%`;
      field.style.top = `${((S.fy + MARGIN) / (LIMIT_H + MARGIN * 2)) * 100}%`;
      field.style.width = `${(FIELD_W / (LIMIT_W + MARGIN * 2)) * 100}%`;
      field.style.height = `${(FIELD_H / (LIMIT_H + MARGIN * 2)) * 100}%`;
      field.setAttribute("aria-expanded", S.open ? "true" : "false");

      popupGroup.style.display = S.open ? "" : "none";
      gapLine.style.display = S.open ? "" : "none";
      const w = Math.max(0, p.width);
      const h = Math.max(0, p.height);
      popupRect.setAttribute("x", r.x);
      popupRect.setAttribute("y", r.y);
      popupRect.setAttribute("width", w);
      popupRect.setAttribute("height", h);
      const outside = r.x < 0 || r.y < 0 || r.x + w > LIMIT_W || r.y + h > LIMIT_H;
      popupRect.classList.toggle("outside", outside);
      popupLabel.setAttribute("x", r.x + 8);
      popupLabel.setAttribute("y", r.y + 16);
      popupLabel.textContent = p.mode === "side" ? `popup · ${SIDE_NAMES[r.side]}${r.fits ? "" : " (밀림)"}` : `popup${r.above ? " · 위로 뒤집힘" : ""}`;
      // 필드와 popup 사이 간격 표시 (아래·위일 때)
      const side = p.mode === "side" ? r.side : r.above ? "above" : "below";
      if (side === "below" || side === "above") {
        const xMid = S.fx + FIELD_W / 2;
        const y1 = side === "below" ? S.fy + FIELD_H : S.fy;
        const y2 = side === "below" ? r.y : r.y + h;
        gapLine.setAttribute("x1", xMid);
        gapLine.setAttribute("x2", xMid);
        gapLine.setAttribute("y1", y1);
        gapLine.setAttribute("y2", y2);
      } else {
        const yMid = S.fy + FIELD_H / 2;
        const x1 = side === "right" ? S.fx + FIELD_W : S.fx;
        const x2 = side === "right" ? r.x : r.x + w;
        gapLine.setAttribute("x1", x1);
        gapLine.setAttribute("x2", x2);
        gapLine.setAttribute("y1", yMid);
        gapLine.setAttribute("y2", yMid);
      }

      // 상태 판
      stateBox.textContent = "";
      const kv = (k, v) => stateBox.append(el("dt", { text: k }), el("dd", {}, v));
      const a = c.anchor;
      kv("열림 (앱 상태)", el("code", { text: S.open ? "open = true" : "open = false" }));
      kv("anchor.field (물리)", el("code", { text: `${fmt(a.field.x)}, ${fmt(a.field.y)}, ${fmt(a.field.width)} × ${fmt(a.field.height)}` }));
      kv("anchor.limit (물리)", el("code", { text: `0, 0, ${fmt(a.limit.width)} × ${fmt(a.limit.height)}` }));
      kv("size (논리) · scale", el("code", { text: `${fmt(c.size.width)} × ${fmt(c.size.height)} · ${fmt(p.scale)}` }));
      kv("gap · alignment", el("code", { text: `${fmt(p.gap)} · ${p.mode === "side" ? "(보지 않는다)" : p.alignment}` }));
      kv("결과 (논리 = ui_popup::x·y)", el("code", { text: describe(c) }));
      kv("읽기", el("span", { text: note(c) }));
      for (const [k, o] of Object.entries(outputs)) if (o) o.textContent = k === "fx" ? fmt(S.fx) : k === "fy" ? fmt(S.fy) : fmt(Number(controls[k].value));
      controls.side.disabled = p.mode !== "side";
      controls.align.disabled = p.mode === "side";
      return c;
    }

    function log(input, messages, effect) {
      const entry = el("div", { class: "fp-entry" });
      entry.append(el("div", { class: "in", text: input }));
      for (const m of messages) entry.append(el("div", { class: "msg", text: m }));
      if (effect) entry.append(el("div", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 30) logBox.lastElementChild.remove();
    }

    function placed(input) {
      const c = draw();
      log(input, [call(c)], "→ " + describe(c));
    }

    function setOpen(open, input, message) {
      S.open = open;
      const c = draw();
      log(input, [message], open ? "→ 앱이 popup을 싣는다: " + describe(c) : "→ 앱이 popup을 뺀다");
    }

    // 필드 자리: 슬라이더 둘 (키보드로도 옮긴다) + 끌기
    const clampField = () => {
      S.fx = Math.min(Math.max(S.fx, -40), LIMIT_W - FIELD_W + 40);
      S.fy = Math.min(Math.max(S.fy, -10), LIMIT_H - FIELD_H + 10);
    };
    controls.fx.min = "-40";
    controls.fx.max = String(LIMIT_W - FIELD_W + 40);
    controls.fy.min = "-10";
    controls.fy.max = String(LIMIT_H - FIELD_H + 10);
    const syncSliders = () => {
      controls.fx.value = String(S.fx);
      controls.fy.value = String(S.fy);
    };
    controls.fx.addEventListener("input", () => {
      S.fx = Number(controls.fx.value);
      draw();
    });
    controls.fy.addEventListener("input", () => {
      S.fy = Number(controls.fy.value);
      draw();
    });
    controls.fx.addEventListener("change", () => placed("필드 x를 옮겼다"));
    controls.fy.addEventListener("change", () => placed("필드 y를 옮겼다"));
    for (const k of ["mode", "side", "align", "width", "height", "gap", "scale"]) {
      controls[k].addEventListener("input", () => draw());
      controls[k].addEventListener("change", () => placed(`${controls[k].getAttribute("aria-label") || k}을(를) 바꿨다`));
    }
    controls.down.addEventListener("change", () => log(`down_opens = ${controls.down.checked}`, [`field_popup_key_handler(open, set_open, ${controls.down.checked})`], controls.down.checked ? "수식 키 없는 ↓도 연다" : "Alt+↓·F4만 연다"));

    // 끌기: 움직이지 않고 떼면 클릭(set_open(!open))이다.
    let drag = null;
    const toLogical = (e) => {
      const box = view.getBoundingClientRect();
      const sx = (LIMIT_W + MARGIN * 2) / box.width;
      const sy = (LIMIT_H + MARGIN * 2) / box.height;
      return { x: (e.clientX - box.left) * sx - MARGIN, y: (e.clientY - box.top) * sy - MARGIN };
    };
    field.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      const at = toLogical(e);
      drag = { id: e.pointerId, dx: at.x - S.fx, dy: at.y - S.fy, moved: false, sx: e.clientX, sy: e.clientY };
      field.setPointerCapture(e.pointerId);
      field.focus();
      e.preventDefault();
    });
    field.addEventListener("pointermove", (e) => {
      if (!drag || drag.id !== e.pointerId) return;
      if (!drag.moved && Math.hypot(e.clientX - drag.sx, e.clientY - drag.sy) < 4) return;
      drag.moved = true;
      const at = toLogical(e);
      S.fx = Math.round(at.x - drag.dx);
      S.fy = Math.round(at.y - drag.dy);
      clampField();
      syncSliders();
      draw();
    });
    const endDrag = (e) => {
      if (!drag || drag.id !== e.pointerId) return;
      const moved = drag.moved;
      drag = null;
      if (moved) placed("필드를 끌어 옮겼다");
      else setOpen(!S.open, "필드를 눌렀다", `set_open(${!S.open})  // 클릭 = 지금 open의 반대`);
    };
    field.addEventListener("pointerup", endDrag);
    field.addEventListener("pointercancel", (e) => {
      if (drag && drag.id === e.pointerId) drag = null;
    });
    // 판의 빈 곳을 누르면 popup 밖 누름이다. 이 실습의 앱은 그 계기에 닫는다.
    stage.addEventListener("pointerdown", (e) => {
      if (e.target === field || !S.open || popupGroup.contains(e.target)) return;
      setOpen(false, "popup 밖을 눌렀다", "dismiss(popup_dismiss_reason::pointer_press_outside)  // 앱의 선택: 닫는다");
    });

    field.addEventListener("keydown", (e) => {
      const keyName = e.key === "ArrowDown" ? "arrow_down" : e.key === "F4" ? "f4" : e.key === "Escape" ? "escape" : e.key === "Enter" ? "enter" : e.key === " " ? "space" : null;
      if (!keyName) return;
      const label = [e.ctrlKey && "Ctrl", e.altKey && "Alt", e.shiftKey && "Shift", e.metaKey && "Meta", e.key === " " ? "Space" : e.key === "ArrowDown" ? "↓" : e.key].filter(Boolean).join("+");
      if (keyName === "escape") {
        if (!S.open) return;
        e.preventDefault();
        setOpen(false, label, "dismiss(popup_dismiss_reason::escape_key)  // 앱의 선택: 닫는다");
        return;
      }
      if (keyName === "enter" || keyName === "space") {
        if (e.ctrlKey || e.altKey || e.metaKey) return;
        e.preventDefault();
        setOpen(!S.open, label, `set_open(${!S.open})  // 초점 필드의 Space·Enter = 클릭`);
        return;
      }
      const answer = fieldPopupKey(S.open, { key: keyName, control: e.ctrlKey, alt: e.altKey, shift: e.shiftKey, meta: e.metaKey }, controls.down.checked);
      if (answer === null) {
        log(label, ["field_popup_key_handler → std::nullopt"], "필드의 처리기가 받지 않는다 — 다른 처리로 흐른다");
        return;
      }
      e.preventDefault();
      if (answer === "swallow") log(label, ["field_popup_key_handler → {} (빈 목록)"], "이미 열려 있어 삼킨다 — 앱 단축키로 새지 않는다");
      else setOpen(true, label, "field_popup_key_handler → { set_open(true) }");
    });

    function reset() {
      S = initial();
      controls.mode.value = "below";
      controls.side.value = "below";
      controls.align.value = "start";
      controls.width.value = "180";
      controls.height.value = "120";
      controls.gap.value = "2";
      controls.scale.value = "1.5";
      controls.down.checked = true;
      syncSliders();
      logBox.textContent = "";
      placed("처음 상태");
    }
    rootEl.addEventListener("lw-reset", reset);
    reset();
  }

  root.LuilPopupsLab = { setup };
})(typeof window !== "undefined" ? window : globalThis);

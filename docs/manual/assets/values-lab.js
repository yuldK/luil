/* luil 설명서 — 슬라이더·숫자 칸 실습
 *
 * slider_element 하나와 숫자 칸(text_input_number) 하나를 브라우저로 재현한다. 판정은 전부 assets/valuemodel.js
 * (원본 C++과 대조한 이식)를 부른다.
 *  - 앱 상태: 막대의 값(float), 숫자 칸의 초안 글. 요소는 값을 들지 않는다.
 *  - 입력은 요소가 낼 메시지(change·change_to)로 바꿔 기록하고, 앱의 handle이 그 메시지로 상태를 바꾼다.
 * 그림은 luil의 실제 렌더링이 아니라 같은 치수(손잡이 12, 트랙 4, 지점 표식 2×10, 고리 22 논리 픽셀)를 배율 2로 그린 재현이다.
 */
(function () {
  "use strict";
  const V = window.LuilValues;
  const { el, icon } = window.LU;
  const C = V.constants;
  const SCALE = 2; // 그리는 배율 (논리 픽셀 × 2)

  // 데모의 자석 막대 셋 (examples/demo/gallery_page.cpp)
  const PRESETS = {
    volume: { name: "볼륨", minimum: 0, maximum: 150, value: 72, magnets: [100], text: (v) => `볼륨 ${roundAway(v)}%` },
    balance: { name: "좌우 균형", minimum: -100, maximum: 100, value: -30, magnets: [0], text: (v) => { const b = roundAway(v); return b === 0 ? "가운데" : b < 0 ? `왼쪽 ${-b}` : `오른쪽 ${b}`; } },
    position: { name: "재생 위치", minimum: 0, maximum: 1440, value: 500, magnets: [192, 580, 905, 1290], text: (v) => { const p = roundAway(v); return `${Math.trunc(p / 60)}:${String(p % 60).padStart(2, "0")} / 24:00`; } },
  };
  // 숫자 칸의 범위와 앱의 오류 글 (basics_page.cpp의 0–3600, 직접 쓴 예의 온도 칸, test의 −1..1)
  const RANGES = {
    seconds: { range: { minimum: 0, maximum: 3600, step: 5 }, start: "10", error: (p) => ({ not_a_number: "숫자로 적어 주세요", too_precise: "소수 없이 적어 주세요", below_minimum: "0에서 3600 사이로 적어 주세요", above_maximum: "0에서 3600 사이로 적어 주세요" })[p] || "" },
    temp: { range: { minimum: -40, maximum: 60, step: 0.5, decimals: 1 }, start: "21.5", error: (p) => (p === "none" || p === "empty" ? "" : "-40.0에서 60.0 사이, 소수 한 자리로 적어 주세요") },
    fine: { range: { minimum: -1, maximum: 1, step: 0.1, decimals: 2 }, start: "0", error: (p) => (p === "none" || p === "empty" ? "" : "-1.00에서 1.00 사이로 적어 주세요") },
  };

  function roundAway(x) {
    return x < 0 ? -Math.round(-x) : Math.round(x); // std::lround
  }
  // float 값을 읽기 좋게 (유효 숫자 7자리)
  function fmt(v) {
    const n = Number(Number(v).toPrecision(7));
    return Object.is(n, -0) ? "0" : String(n);
  }
  const signed = (v) => (v > 0 ? "+" : "") + fmt(v);
  const quote = (s) => `"${s}"`;
  const KEY_NAME = { ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", PageUp: "PageUp", PageDown: "PageDown", Home: "Home", End: "End" };

  function setup(root) {
    const q = (name) => root.querySelector(`[data-vl=${name}]`);
    const presetBox = q("preset");
    const clampBox = q("clamp");
    const changeToBox = q("changeto");
    const view = q("slider");
    const rangeBox = q("range");
    const input = q("number");
    const upButton = q("up");
    const downButton = q("down");
    const errorBox = q("error");
    const stateBox = q("state");
    const logBox = q("log");

    let S; // 앱 상태
    let drag = null; // { x, control } — 앞 포인터 (input thread의 끌기 상태)
    let snappedIndex = -1;

    function reset() {
      presetBox.value = "volume";
      rangeBox.value = "seconds";
      clampBox.checked = true;
      changeToBox.checked = true;
      const p = PRESETS[presetBox.value] || PRESETS.volume;
      const r = RANGES[rangeBox.value] || RANGES.seconds;
      S = { value: Math.fround(p.value), text: r.start };
      drag = null;
      logBox.textContent = "";
      input.value = S.text;
      frame();
    }

    const preset = () => PRESETS[presetBox.value] || PRESETS.volume;
    const numberSpec = () => RANGES[rangeBox.value] || RANGES.seconds;

    // 이번 frame의 slider (element는 frame마다 다시 지어진다)
    function slider() {
      const p = preset();
      return V.makeSlider({ minimum: p.minimum, maximum: p.maximum, value: S.value, magnets: p.magnets, changeTo: changeToBox.checked }, { x: 0, width: view.clientWidth, scale: SCALE });
    }

    /* ---------- 기록 ---------- */
    function log(what, message, effect) {
      const entry = el("div", { class: "vl-entry" }, el("div", { class: "in", text: what }));
      if (message) entry.append(el("div", { class: "msg", text: message }));
      if (effect) entry.append(el("div", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 60) logBox.lastElementChild.remove();
    }
    const describe = (msgs) => (msgs.length ? msgs.map((m) => (m.kind === "change" ? `change(${signed(m.value)})` : `change_to(${fmt(m.value)})`)).join(", ") : "메시지 없음");

    /* ---------- 앱의 handle (logic thread) ---------- */
    function handle(msgs) {
      const p = preset();
      const before = S.value;
      for (const m of msgs) {
        let v = m.kind === "change" ? Math.fround(S.value + m.value) : m.value;
        if (clampBox.checked) v = Math.min(Math.max(v, p.minimum), p.maximum); // std::clamp
        S.value = Math.fround(v);
      }
      if (!msgs.length) return "";
      const out = clampBox.checked ? "" : S.value < p.minimum || S.value > p.maximum ? " — 범위 밖에 쌓였다" : "";
      return `앱: 값 ${fmt(before)} → ${fmt(S.value)}${out}`;
    }

    /* ---------- 그리기 ---------- */
    function frame() {
      const s = slider();
      const p = preset();
      view.textContent = "";
      const k = C.KNOB * SCALE;
      const center = V.knobCenter(s);
      const value = V.clampValue(s);
      const range = s.maximum - s.minimum;
      view.append(el("div", { class: "vl-track" }), el("div", { class: "vl-fill", style: { width: Math.max(0, center) + "px" } }));
      let snapped = false;
      s.magnets.forEach((m, i) => {
        const mark = k / 2 + V.magnetOffset(s, i);
        view.append(el("div", { class: "vl-mark" + (mark < center ? " on" : ""), style: { left: mark + "px" } }));
        if (Math.abs(value - m) <= range * Math.fround(1e-5)) snapped = true;
      });
      if (snapped && drag) view.append(el("div", { class: "vl-ring", style: { left: center + "px" } }));
      view.append(el("div", { class: "vl-knob", style: { left: center + "px" } }));
      view.setAttribute("aria-valuemin", String(p.minimum));
      view.setAttribute("aria-valuemax", String(p.maximum));
      view.setAttribute("aria-valuenow", fmt(value));
      view.setAttribute("aria-valuetext", p.text(value));
      view.setAttribute("aria-label", `${p.name} (luil 재현)`);

      const spec = numberSpec();
      const check = V.checkNumberInput(S.text, spec.range);
      const error = spec.error(check.problem);
      errorBox.textContent = error;
      input.parentElement.classList.toggle("bad", !!error);
      if (input.value !== S.text) input.value = S.text;
      const r = spec.range;
      upButton.setAttribute("aria-label", `${fmt(r.step)} 늘리기`);
      downButton.setAttribute("aria-label", `${fmt(r.step)} 줄이기`);

      stateBox.textContent = "";
      const row = (k2, v2) => stateBox.append(el("dt", { text: k2 }), el("dd", {}, v2));
      row("value", el("code", { text: fmt(S.value) }));
      row("그린 값", el("span", {}, el("code", { text: fmt(value) }), ` · ${p.text(value)}`));
      row("손잡이", `중심 ${fmt(center)}px · 도는 거리 ${fmt(V.travel(s))}px`);
      row("초안", el("code", { text: quote(S.text) }));
      row("check", el("code", { text: `${check.problem}${check.value === null ? "" : " · " + fmt(check.value)}` }));
    }

    /* ---------- slider 입력 (input thread의 controller가 하는 일) ---------- */
    const pointerX = (e) => e.clientX - view.getBoundingClientRect().left;
    const ctrlOf = (e) => e.ctrlKey || e.metaKey; // ui_action_context::control (macOS는 Command도)

    view.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      view.focus({ preventScroll: true });
      view.setPointerCapture(e.pointerId);
      const s = slider();
      const x = pointerX(e);
      const control = ctrlOf(e);
      const t = V.knobOffsetFor(s, x, control);
      const msgs = V.sliderPress(s, x, control);
      drag = { x, control };
      snappedIndex = t.magnet;
      const effect = handle(msgs);
      log(`누름 x=${fmt(x)}${control ? " (Ctrl)" : ""}${t.magnet >= 0 ? ` · 지점 ${fmt(s.magnets[t.magnet])}에 붙음` : ""}`, "→ " + describe(msgs), effect);
      frame();
    });
    view.addEventListener("pointermove", (e) => {
      if (!drag || !view.hasPointerCapture(e.pointerId)) return;
      const s = slider();
      const x = pointerX(e);
      const control = ctrlOf(e);
      const t = V.knobOffsetFor(s, x, control);
      const msgs = V.sliderMove(s, drag.x, drag.control, x, control);
      const from = drag.x;
      drag = { x, control };
      if (!msgs.length && t.magnet === snappedIndex) return; // 같은 자리: 메시지 없음
      const what = `끌기 ${fmt(from)}→${fmt(x)}${control ? " (Ctrl: 자석 없이)" : ""}${t.magnet >= 0 && t.magnet !== snappedIndex ? ` · 지점 ${fmt(s.magnets[t.magnet])}에 붙음` : ""}`;
      snappedIndex = t.magnet;
      const effect = handle(msgs);
      if (msgs.length) log(what, "→ " + describe(msgs), effect);
      frame();
    });
    const release = (e) => {
      if (!drag) return;
      drag = null;
      snappedIndex = -1;
      if (view.hasPointerCapture(e.pointerId)) view.releasePointerCapture(e.pointerId);
      log("뗌", "→ 메시지 없음", "slider는 on_release를 두지 않는다 — 끝을 알 길이 없다");
      frame();
    };
    view.addEventListener("pointerup", release);
    view.addEventListener("pointercancel", release);

    view.addEventListener("keydown", (e) => {
      if (!(e.key in KEY_NAME)) return;
      const mods = { ctrl: e.ctrlKey, alt: e.altKey, meta: e.metaKey, shift: e.shiftKey };
      const name = (mods.ctrl ? "Ctrl+" : "") + (mods.alt ? "Alt+" : "") + (mods.meta ? "Meta+" : "") + KEY_NAME[e.key];
      const step = V.stepForKey(e.key, mods, { axis: "horizontal", upIncreases: false });
      if (step === null) {
        log(name, "→ 흐른다", mods.ctrl || mods.alt || mods.meta ? "수식 키와 함께인 걸음 키는 앱 단축키다" : "가로 막대라 ↑↓는 값 걸음이 아니다");
        return;
      }
      e.preventDefault();
      const s = slider();
      const msgs = V.sliderStep(s, step);
      const at = V.clampValue(s);
      const end = (step === "increase" || step === "increase_page") && at >= s.maximum ? " (이미 최대인데도)" : (step === "decrease" || step === "decrease_page") && at <= s.minimum ? " (이미 최소인데도)" : "";
      const effect = handle(msgs);
      log(`${name} → ${step}`, msgs.length ? "→ " + describe(msgs) + end : "→ 빈 목록 (삼킴)", effect);
      frame();
    });

    /* ---------- 숫자 칸 ---------- */
    function applyStep(step, label) {
      const spec = numberSpec();
      const next = V.steppedNumberInput(S.text, spec.range, step);
      if (next === null) {
        log(`${label} → ${step}`, "→ 빈 목록 (삼킴)", "다음 글이 지금 글과 같다 (끝에 닿았다)");
        return;
      }
      const before = S.text;
      S.text = next; // replace_all: 초안 전체를 바꾸고 caret은 끝에
      input.value = next;
      if (document.activeElement === input) input.setSelectionRange(next.length, next.length);
      log(`${label} → ${step}`, `→ change_to(${quote(next)})`, `앱: replace_all ${quote(before)} → ${quote(next)}`);
      frame();
    }

    input.addEventListener("keydown", (e) => {
      if (!["ArrowUp", "ArrowDown", "PageUp", "PageDown"].includes(e.key)) return;
      const mods = { ctrl: e.ctrlKey, alt: e.altKey, meta: e.metaKey, shift: e.shiftKey };
      const step = V.stepForKey(e.key, mods, { axis: "vertical", upIncreases: true });
      if (step === null) {
        log((mods.ctrl ? "Ctrl+" : mods.alt ? "Alt+" : "Meta+") + KEY_NAME[e.key], "→ 흐른다", "수식 키와 함께인 걸음 키는 앱 단축키다");
        return;
      }
      e.preventDefault();
      if (e.isComposing) {
        log(KEY_NAME[e.key], "→ 빈 목록 (삼킴)", "IME 조합 중에는 걸음을 삼킨다");
        return;
      }
      applyStep(step, KEY_NAME[e.key]);
    });
    upButton.addEventListener("click", () => applyStep("increase", "스핀 늘리기"));
    downButton.addEventListener("click", () => applyStep("decrease", "스핀 줄이기"));

    // 넣는 조각을 number_input_filter로 거른다 (apply_text_edit의 거르개). 지우기는 그대로다.
    input.addEventListener("beforeinput", (e) => {
      if (!["insertText", "insertFromPaste", "insertFromDrop", "insertReplacementText"].includes(e.inputType)) return;
      const data = e.data != null ? e.data : e.dataTransfer ? e.dataTransfer.getData("text/plain") : "";
      const kept = V.numberInputFilter(numberSpec().range)(data);
      if (kept === data) return;
      e.preventDefault();
      input.setRangeText(kept, input.selectionStart, input.selectionEnd, "end");
      S.text = input.value;
      log(`넣기 ${quote(data)}`, `→ number_input_filter → ${quote(kept)}`, `초안 ${quote(S.text)}`);
      frame();
    });
    input.addEventListener("input", () => {
      S.text = input.value;
      frame();
    });

    presetBox.addEventListener("change", () => {
      S.value = Math.fround(preset().value);
      log(`막대: ${presetBox.options[presetBox.selectedIndex].text}`, "", `지점 ${preset().magnets.join(", ")} (값 단위)`);
      frame();
    });
    rangeBox.addEventListener("change", () => {
      S.text = numberSpec().start;
      input.value = S.text;
      log(`숫자 칸: ${rangeBox.options[rangeBox.selectedIndex].text}`, "", "검사·거르기·증감이 같은 범위를 본다");
      frame();
    });
    clampBox.addEventListener("change", () => log(clampBox.checked ? "앱이 자른다" : "앱이 자르지 않는다", "", clampBox.checked ? "handle이 std::clamp로 범위에 넣는다" : "끝에서 → · PageDown을 눌러 보라 — 델타가 쌓인다"));
    changeToBox.addEventListener("change", () => {
      log(changeToBox.checked ? "change_to 달기" : "change_to 떼기", "", changeToBox.checked ? "지점에 붙는 순간 change_to(지점 값)" : "지점에 붙어도 델타로 온다");
      frame();
    });
    if (window.ResizeObserver) new ResizeObserver(() => frame()).observe(view);
    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilValuesLab = { setup };
})();

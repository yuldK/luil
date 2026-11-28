/* luil 설명서 — slider의 자석·키 걸음과 숫자 칸의 읽기·증감 규칙을 JS로 옮긴 것
 *
 * 함수 단위로 옮긴 원본:
 *  - src/ui/slider_element.cpp: 생성자의 지점 다듬기, key_step_target::on_step(키 걸음), travel, knob_center,
 *    value_delta_for, magnet_offset, knob_offset_for, move_knob, pointer_drag_target::on_press·on_move
 *  - include/luil/ui/slider_element.h: clamp_value와 상수
 *  - src/ui/ui_interaction.cpp: interaction_controller::process_step_key의 키 → value_step 표
 *  - src/ui/number_input.cpp: check_number_input(parse·normalized·ascii_of), stepped_number_input, number_input_filter
 * 이름 옆 주석이 C++ 원본의 이름이다.
 *
 * slider는 float(32비트) 셈이라 연산마다 Math.fround로 접는다. 숫자 칸은 double과 10^decimals 배의 정수(unit)다.
 * std::round·std::llround는 반을 0에서 먼 쪽으로 보낸다 — 음수에서 Math.round와 다르므로 roundAway로 옮겼다.
 * slider는 원본 줄을 잘라 그대로 컴파일한 대조기, 숫자 칸은 src/ui/number_input.cpp를 그대로 링크한 대조기에
 * 무작위 입력과 끝 값을 넣어 결과(float는 비트까지)가 같음을 확인했다 (x86-64 g++ -O2 -ffp-contract=off).
 *
 *   slider   { minimum, maximum, value, magnets: [], changeTo: bool, x, width, scale }   (makeSlider가 다듬는다)
 *   message  { kind: "change" | "change_to", value }
 *   key mods { ctrl, alt, meta, shift }       key_step_target { axis: "horizontal"|"vertical"|"both"|"none", upIncreases }
 *   number_input_range { minimum, maximum, step, page_step, decimals }   (빠진 칸은 C++ 기본값)
 */
(function () {
  "use strict";
  const f = Math.fround;

  // 상수 (slider_element.h)
  const STICK = 6; // slider_magnet_stick_distance
  const RELEASE = 12; // slider_magnet_release_distance
  const KNOB = 12; // slider_knob_size
  const STEP_RATIO = f(0.01); // slider_key_step_ratio
  const PAGE_RATIO = f(0.1); // slider_key_page_ratio

  // std::clamp·std::min과 같은 비교다.
  const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
  const smin = (a, b) => (b < a ? b : a);
  const copysign = (a, b) => (b < 0 || Object.is(b, -0) ? -Math.abs(a) : Math.abs(a));

  /* ---------- slider ---------- */

  // 생성자: 범위 밖·겹친 지점을 버리고 정렬한다. 범위가 뒤집히면 모두 버린다.
  function sliderMagnets(minimum, maximum, magnets) {
    let m = (magnets || []).map(f);
    m = maximum > minimum ? m.filter((v) => v >= minimum && v <= maximum) : [];
    m.sort((a, b) => a - b);
    return m.filter((v, i) => i === 0 || v !== m[i - 1]);
  }

  function makeSlider(config, geometry) {
    const minimum = f(config.minimum == null ? 0 : config.minimum);
    const maximum = f(config.maximum == null ? 1 : config.maximum);
    const g = geometry || {};
    return {
      minimum,
      maximum,
      value: f(config.value || 0),
      magnets: sliderMagnets(minimum, maximum, config.magnets),
      changeTo: !!config.changeTo,
      x: f(g.x || 0),
      width: f(g.width || 0),
      scale: g.scale > 0 ? f(g.scale) : 1, // arrange: context.scale > 0 ? context.scale : 1
    };
  }

  // slider_element::clamp_value
  function clampValue(s) {
    if (s.maximum <= s.minimum || s.value < s.minimum) return s.minimum;
    return s.value > s.maximum ? s.maximum : s.value;
  }

  const halfKnob = (s) => f(f(KNOB * s.scale) / 2);

  // travel
  function travel(s) {
    const distance = f(s.width - f(KNOB * s.scale));
    return distance > 0 ? distance : 0;
  }

  // knob_center
  function knobCenter(s) {
    const range = f(s.maximum - s.minimum);
    const ratio = range > 0 ? f(f(clampValue(s) - s.minimum) / range) : 0;
    return f(f(s.x + halfKnob(s)) + f(ratio * travel(s)));
  }

  // value_delta_for
  function valueDeltaFor(s, pixels) {
    const distance = travel(s);
    if (distance <= 0 || s.maximum <= s.minimum) return 0;
    return f(f(pixels / distance) * f(s.maximum - s.minimum));
  }

  // magnet_offset
  function magnetOffset(s, index) {
    return f(f(f(s.magnets[index] - s.minimum) / f(s.maximum - s.minimum)) * travel(s));
  }

  // knob_offset_for(x, free, &magnet) → { offset, magnet(번호 또는 -1) }
  function knobOffsetFor(s, x, free) {
    const distance = travel(s);
    if (distance <= 0) return { offset: 0, magnet: -1 };
    const pointer = clamp(f(f(f(x) - s.x) - halfKnob(s)), 0, distance);
    if (free) return { offset: pointer, magnet: -1 };
    const m = s.magnets;
    for (let index = 0; index < m.length; index++) {
      const point = magnetOffset(s, index);
      const left = index > 0 ? f(point - magnetOffset(s, index - 1)) : Infinity;
      const right = index + 1 < m.length ? f(magnetOffset(s, index + 1) - point) : Infinity;
      const reach = f(smin(left, right) / 2);
      const stick = smin(f(STICK * s.scale), reach);
      const release = smin(f(RELEASE * s.scale), reach);
      const offset = f(pointer - point);
      const away = Math.abs(offset);
      if (away <= stick) return { offset: point, magnet: index };
      // 붙는 거리에서 지점, 풀리는 거리에서 포인터에 닿는 직선이다.
      if (away < release) return { offset: f(point + copysign(f(f(f(away - stick) * release) / f(release - stick)), offset)), magnet: -1 };
    }
    return { offset: pointer, magnet: -1 };
  }

  // move_knob
  function moveKnob(s, from, target, magnet) {
    if (target === from) return [];
    if (magnet >= 0 && s.changeTo) return [{ kind: "change_to", value: s.magnets[magnet] }];
    const delta = valueDeltaFor(s, f(target - from));
    if (delta === 0) return [];
    return [{ kind: "change", value: delta }];
  }

  // pointer_drag_target::on_press — 누른 자리로 손잡이가 곧장 간다.
  function sliderPress(s, x, control) {
    const t = knobOffsetFor(s, x, control);
    return moveKnob(s, f(f(knobCenter(s) - s.x) - halfKnob(s)), t.offset, t.magnet);
  }

  // pointer_drag_target::on_move — 앞뒤 포인터 자리의 손잡이 차이만 낸다. 지금 값을 읽지 않는다.
  function sliderMove(s, previousX, previousControl, x, control) {
    const from = knobOffsetFor(s, previousX, previousControl).offset;
    const t = knobOffsetFor(s, x, control);
    return moveKnob(s, from, t.offset, t.magnet);
  }

  // key_step_target::on_step — 빈 목록은 "삼켰다"다. slider는 답이 없는(nullopt) 걸음이 없다.
  function sliderStep(s, step) {
    const range = f(s.maximum - s.minimum);
    const current = clampValue(s);
    let delta = 0;
    if (step === "decrease") delta = f(-range * STEP_RATIO);
    else if (step === "increase") delta = f(range * STEP_RATIO);
    else if (step === "decrease_page") delta = f(-range * PAGE_RATIO);
    else if (step === "increase_page") delta = f(range * PAGE_RATIO);
    else if (step === "minimum") delta = f(s.minimum - current);
    else if (step === "maximum") delta = f(s.maximum - current);
    if (range <= 0 || delta === 0) return [];
    return [{ kind: "change", value: delta }];
  }

  // process_step_key의 키 → value_step 표 (on_step만 든 요소). 답이 null이면 키가 흐른다.
  function stepForKey(key, mods, target) {
    if (mods.ctrl || mods.alt || mods.meta) return null; // shortcut_modifier_down()
    const takes = (horizontal) => target.axis === "both" || (horizontal ? target.axis === "horizontal" : target.axis === "vertical");
    let step = null;
    if (key === "ArrowLeft") step = takes(true) ? "decrease" : null;
    else if (key === "ArrowRight") step = takes(true) ? "increase" : null;
    else if (key === "ArrowUp") step = takes(false) ? "decrease" : null;
    else if (key === "ArrowDown") step = takes(false) ? "increase" : null;
    else if (key === "PageUp") step = "decrease_page";
    else if (key === "PageDown") step = "increase_page";
    else if (key === "Home") step = "minimum";
    else if (key === "End") step = "maximum";
    if (step === null) return null;
    if (target.upIncreases) {
      if (key === "ArrowUp" || key === "ArrowDown") step = key === "ArrowUp" ? "increase" : "decrease";
      else if (key === "PageUp" || key === "PageDown") step = key === "PageUp" ? "increase_page" : "decrease_page";
    }
    return step;
  }

  /* ---------- 숫자 칸 ---------- */

  const UNIT_LIMIT = 1e15; // unit_limit
  const MAX_DECIMALS = 9; // maximum_decimals
  const LOWEST = -Number.MAX_VALUE; // std::numeric_limits<double>::lowest()
  const HIGHEST = Number.MAX_VALUE; // std::numeric_limits<double>::max()

  // std::round·std::llround: 반은 0에서 먼 쪽이다.
  const roundAway = (x) => (x < 0 ? -Math.round(-x) : Math.round(x));

  function rangeOf(r) {
    const o = r || {};
    return {
      minimum: o.minimum == null ? LOWEST : o.minimum,
      maximum: o.maximum == null ? HIGHEST : o.maximum,
      step: o.step == null ? 1 : o.step,
      page_step: o.page_step == null ? 0 : o.page_step,
      decimals: o.decimals == null ? 0 : o.decimals,
    };
  }
  const decimalsOf = (range) => clamp(Math.trunc(range.decimals), 0, MAX_DECIMALS); // decimals_of
  const scaleOf = (range) => Math.pow(10, decimalsOf(range)); // scale_of (10^0..10^9, 정확하다)
  function toUnits(value, scale) {
    const scaled = value * scale;
    if (Number.isNaN(scaled)) return 0;
    if (scaled >= UNIT_LIMIT) return UNIT_LIMIT;
    if (scaled <= -UNIT_LIMIT) return -UNIT_LIMIT;
    return roundAway(scaled);
  }
  const hasMinimum = (range) => range.minimum > LOWEST;
  const hasMaximum = (range) => range.maximum < HIGHEST;
  const minimumUnits = (range) => (hasMinimum(range) ? toUnits(range.minimum, scaleOf(range)) : -UNIT_LIMIT);
  const maximumUnits = (range) => (hasMaximum(range) ? toUnits(range.maximum, scaleOf(range)) : UNIT_LIMIT);

  const isSpace = (c) => c === 0x20 || c === 0x09 || c === 0x0d || c === 0x0a || c === 0xa0 || c === 0x3000;
  // ascii_of: 숫자 꼴의 글자를 ASCII로. 읽을 수 없는 글자는 '?'다.
  function asciiOf(c) {
    if (c >= 0x30 && c <= 0x39) return String.fromCharCode(c);
    if (c >= 0xff10 && c <= 0xff19) return String.fromCharCode(0x30 + c - 0xff10);
    if (c === 0x2e || c === 0xff0e) return ".";
    if (c === 0x2d || c === 0xff0d || c === 0x2212 || c === 0xfe63) return "-";
    if (c === 0x2b || c === 0xff0b) return "+";
    if (isSpace(c)) return " ";
    return "?";
  }
  function normalized(text) {
    let s = "";
    for (const ch of text) s += asciiOf(ch.codePointAt(0));
    return s.replace(/^ +| +$/g, ""); // find_first_not_of(' ') … find_last_not_of(' ')
  }
  const isDigit = (c) => c >= "0" && c <= "9";

  // parse → { problem, value, units, precise, overflow }
  function parse(text, range) {
    const value = normalized(text);
    if (value === "") return { problem: "empty", value: 0, units: 0, precise: true, overflow: false };
    let at = 0;
    const negative = value[at] === "-";
    if (value[at] === "-" || value[at] === "+") ++at;
    let whole = "";
    let fraction = "";
    while (at < value.length && isDigit(value[at])) whole += value[at++];
    if (at < value.length && value[at] === ".") {
      ++at;
      while (at < value.length && isDigit(value[at])) fraction += value[at++];
    }
    if (at !== value.length || (whole === "" && fraction === "")) return { problem: "not_a_number", value: 0, units: 0, precise: true, overflow: false };
    fraction = fraction.replace(/0+$/, "");
    whole = whole.replace(/^0+/, "");
    const decimals = decimalsOf(range);
    const result = { problem: "none", value: 0, units: 0, precise: fraction.length <= decimals, overflow: whole.length + decimals > 15 };
    let approximate = 0;
    for (const d of whole) approximate = approximate * 10 + (d.charCodeAt(0) - 48);
    let place = 0.1;
    for (const d of fraction) {
      approximate += (d.charCodeAt(0) - 48) * place;
      place /= 10;
    }
    result.value = negative ? -approximate : approximate;
    if (result.overflow === false) {
      let units = 0;
      for (const d of whole) units = units * 10 + (d.charCodeAt(0) - 48);
      for (let i = 0; i < decimals; i++) units = units * 10 + (i < fraction.length ? fraction.charCodeAt(i) - 48 : 0);
      result.units = negative ? -units : units;
      if (units > UNIT_LIMIT) result.overflow = true;
    }
    return result;
  }

  // format_units
  function formatUnits(units, decimals) {
    const negative = units < 0;
    let digits = "";
    for (let rest = negative ? -units : units; rest > 0 || digits.length <= decimals; ) {
      const r = rest % 10;
      digits = String(r) + digits;
      rest = (rest - r) / 10;
    }
    if (decimals > 0) digits = digits.slice(0, digits.length - decimals) + "." + digits.slice(digits.length - decimals);
    return negative ? "-" + digits : digits;
  }

  // check_number_input → { problem, value(없으면 null) }
  function checkNumberInput(text, r) {
    const range = rangeOf(r);
    const parsed = parse(text, range);
    if (parsed.problem !== "none") return { problem: parsed.problem, value: null };
    const result = { problem: "none", value: parsed.value };
    if (parsed.precise === false) result.problem = "too_precise";
    else if (parsed.overflow) result.problem = parsed.value < 0 ? "below_minimum" : "above_maximum";
    else if (parsed.units < minimumUnits(range)) result.problem = "below_minimum";
    else if (parsed.units > maximumUnits(range)) result.problem = "above_maximum";
    return result;
  }

  // stepped_number_input → 다음 글, 답이 없으면 null
  function steppedNumberInput(text, r, step) {
    const range = rangeOf(r);
    const scale = scaleOf(range);
    const lowest = minimumUnits(range);
    const highest = Math.max(lowest, maximumUnits(range));
    const clamped = (u) => clamp(u, lowest, highest);
    const parsed = parse(text, range);
    let target;
    if (step === "minimum") {
      if (hasMinimum(range) === false) return null;
      target = lowest;
    } else if (step === "maximum") {
      if (hasMaximum(range) === false) return null;
      target = highest;
    } else if (parsed.problem !== "none") target = clamped(0);
    else {
      const stepUnits = Math.max(1, toUnits(range.step, scale));
      const page = step === "increase_page" || step === "decrease_page";
      const delta = page ? (range.page_step > 0 ? Math.max(1, toUnits(range.page_step, scale)) : stepUnits * 10) : stepUnits;
      const up = step === "increase" || step === "increase_page";
      const current = parsed.overflow ? (parsed.value < 0 ? -UNIT_LIMIT : UNIT_LIMIT) : parsed.precise ? parsed.units : parsed.value * scale;
      const base = hasMinimum(range) ? lowest : 0;
      const grid = (current - base) / stepUnits;
      const nearest = roundAway(grid);
      let next;
      if (Math.abs(grid - nearest) < 1e-9) next = base + nearest * stepUnits + (up ? delta : -delta);
      else next = base + (up ? Math.ceil(grid) : Math.floor(grid)) * stepUnits;
      target = clamped(clamp(next, -UNIT_LIMIT, UNIT_LIMIT));
    }
    const result = formatUnits(target, decimalsOf(range));
    return result === text ? null : result;
  }

  // number_input_filter(range) — 넣는 조각에서 그 범위의 수에 나올 수 있는 글자만 남긴다.
  function numberInputFilter(r) {
    const range = rangeOf(r);
    const signedValues = range.minimum < 0;
    const fractions = decimalsOf(range) > 0;
    return (text) => {
      let kept = "";
      for (const ch of text) {
        const c = asciiOf(ch.codePointAt(0));
        if (isDigit(c) || (c === "." && fractions) || ((c === "-" || c === "+") && signedValues)) kept += c;
      }
      return kept;
    };
  }

  window.LuilValues = {
    makeSlider, sliderMagnets, clampValue, travel, knobCenter, knobOffsetFor, sliderPress, sliderMove, sliderStep, stepForKey,
    checkNumberInput, steppedNumberInput, numberInputFilter, rangeOf,
    constants: { STICK, RELEASE, KNOB, ROW: 20, TRACK: 4, MARK_W: 2, MARK_H: 10, RING: 22 },
  };
})();

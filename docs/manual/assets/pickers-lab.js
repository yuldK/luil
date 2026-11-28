/* luil 설명서 — 날짜·색 고르기 실습
 *
 * calendar_element 하나(인라인 달력)와 색 공간 변환기를 브라우저로 재현한다. 판정은 전부 assets/pickermodel.js
 * (원본 C++과 대조한 이식)를 부른다.
 *  - 앱 상태: 고른 날, 커서(보이는 달), 오늘(앱이 브라우저 시계에서 읽는다), 범위, 주 첫 요일, 주말 막기 / HSV 값, 16진 초안.
 *  - 달력이 낼 메시지(select·move_to·turn_month)를 기록하고, 앱의 handle이 그 메시지로 상태를 바꾼다.
 *    move_to는 데모처럼 초점 요청을 싣고, turn_month는 싣지 않는다.
 * 그림은 luil의 실제 렌더링이 아니다. 칸은 32 논리 픽셀을 키운 재현이고 색은 설명서 테마의 색이다.
 */
(function () {
  "use strict";
  const P = window.LuilPickers;
  const { el, icon } = window.LU;
  const WEEKDAYS = ["일", "월", "화", "수", "목", "금", "토"];
  const MOVE_OF = {
    ArrowLeft: "previous_day", ArrowRight: "next_day", ArrowUp: "previous_week", ArrowDown: "next_week",
    PageUp: "previous_month", PageDown: "next_month", Home: "month_start", End: "month_end",
  };

  const iso = (t) => P.formatIsoDate(t);
  const sameDay = (a, b) => !!a && !!b && P.same(a, b);
  const weekend = (t) => {
    const w = P.weekdayOf(P.daysFromCivil(t));
    return w === 0 || w === 6;
  };
  function todayOf() {
    const d = new Date(); // 앱이 시계를 읽는다 (라이브러리는 읽지 않는다)
    return { y: d.getFullYear(), m: d.getMonth() + 1, d: d.getDate() };
  }
  const addDays = (t, n) => P.civilFromDays(P.daysFromCivil(t) + n);
  // 숫자를 읽기 좋게 (float 칸)
  function fmt(v) {
    const n = Number(Number(v).toPrecision(7));
    return Object.is(n, -0) ? "0" : String(n);
  }

  function setup(root) {
    const q = (name) => root.querySelector(`[data-pk=${name}]`);
    const firstBox = q("first");
    const boundsBox = q("bounds");
    const weekendBox = q("weekend");
    const jan31 = q("jan31");
    const cal = q("cal");
    const stateBox = q("state");
    const logBox = q("log");
    const hsvInputs = ["hue", "saturation", "value", "alpha"].map((k) => q(k));
    const swatch = q("swatch");
    const hexOut = q("hexout");
    const hexIn = q("hexin");
    const alphaBox = q("alpha8");
    const hexError = q("hexerror");

    let S; // 앱 상태
    let focusDay = false; // 다음 그리기 뒤 커서 날로 초점을 옮긴다 (초점 요청)
    let keyboardFocus = false;

    function reset() {
      const today = todayOf();
      S = { today, value: today, cursor: addDays(today, 7), firstDay: 0, color: P.clampHsv({ hue: 225, saturation: 0.8, value: 1, alpha: 0.5 }), hex: "", hexInvalid: false };
      firstBox.value = "0";
      boundsBox.value = "none";
      weekendBox.checked = false;
      alphaBox.checked = true;
      S.hex = P.formatHexColor(P.colorFromHsv(S.color), true);
      logBox.textContent = "";
      draw();
      drawColor();
    }

    function bounds() {
      if (boundsBox.value === "window") return { minimum: addDays(S.today, -30), maximum: addDays(S.today, 60) };
      if (boundsBox.value === "test") return { minimum: { y: 2026, m: 11, d: 5 }, maximum: { y: 2026, m: 11, d: 20 } };
      if (boundsBox.value === "flipped") return { minimum: { y: 2026, m: 11, d: 20 }, maximum: { y: 2026, m: 11, d: 5 } };
      return { minimum: null, maximum: null };
    }
    const available = (t) => !(weekendBox.checked && weekend(t));
    const selectable = (t) => P.calendarContains(bounds(), t) && available(t); // selectable
    // 그린 커서 (calendar_element 생성자): 틀린 커서는 시작 커서로, 범위 밖 커서는 범위로 접는다.
    const drawnCursor = () => (P.ok(S.cursor) ? P.clampDate(S.cursor, bounds()) : P.calendarStartCursor(S.value, S.today, bounds()));

    /* ---------- 기록 ---------- */
    function log(what, message, effect) {
      const entry = el("div", { class: "pk-entry" }, el("div", { class: "in", text: what }));
      if (message) entry.append(el("div", { class: "msg", text: message }));
      if (effect) entry.append(el("div", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 60) logBox.lastElementChild.remove();
    }

    /* ---------- 앱의 handle (logic thread) ---------- */
    function handle(msg) {
      if (msg.kind === "move_to") {
        S.cursor = msg.day;
        focusDay = true; // request_focus(day_id(owner, day))
        return `앱: 커서 ${iso(msg.day)}, 그 날로 초점 요청`;
      }
      if (msg.kind === "turn_month") {
        S.cursor = msg.day;
        return `앱: 커서 ${iso(msg.day)} (초점 요청 없음 — 초점이 단추에 남는다)`;
      }
      S.value = msg.day;
      S.cursor = msg.day;
      return `앱: 고른 날 ${iso(msg.day)}`;
    }
    const describe = (msgs) => (msgs.length ? msgs.map((m) => `${m.kind}(${iso(m.day)})`).join(", ") : "빈 목록");

    /* ---------- 달력 그리기 (build) ---------- */
    function draw() {
      const cursor = drawnCursor();
      const b = bounds();
      const nav = P.monthButtons(cursor, b);
      cal.textContent = "";
      const prev = el("button", { type: "button", class: "pk-nav", "aria-label": "이전 달", title: "이전 달", disabled: !nav.previousEnabled }, icon("EAB5"));
      const next = el("button", { type: "button", class: "pk-nav", "aria-label": "다음 달", title: "다음 달", disabled: !nav.nextEnabled }, icon("EAB6"));
      prev.addEventListener("click", () => turn(nav.previous, "이전 달", prev));
      next.addEventListener("click", () => turn(nav.next, "다음 달", next));
      const title = el("div", { class: "pk-title", text: `${cursor.y}년 ${cursor.m}월` });
      const head = el("div", { class: "pk-head" }, prev, title, next);
      const week = el("div", { class: "pk-week", "aria-hidden": "true" });
      for (let i = 0; i < 7; i++) week.append(el("span", { text: WEEKDAYS[(S.firstDay + i) % 7] }));
      const grid = el("div", { class: "pk-cells", role: "grid", "aria-label": `${cursor.y}년 ${cursor.m}월` });
      let row = null;
      let cursorCell = null;
      P.calendarMonthGrid(cursor.y, cursor.m, S.firstDay).forEach((day, index) => {
        if (index % 7 === 0) {
          row = el("div", { class: "pk-row", role: "row" });
          grid.append(row);
        }
        const can = selectable(day);
        const isCursor = sameDay(day, cursor);
        const classes = ["pk-day"];
        if (day.y !== cursor.y || day.m !== cursor.m) classes.push("out");
        if (!can) classes.push("off");
        if (sameDay(day, S.value)) classes.push("sel");
        if (sameDay(day, S.today)) classes.push("today");
        const name = `${day.y}년 ${day.m}월 ${day.d}일 ${WEEKDAYS[P.weekdayOf(P.daysFromCivil(day))]}요일` + (sameDay(day, S.today) ? ", 오늘" : "") + (can ? "" : ", 고를 수 없음");
        const cell = el("button", { type: "button", class: classes.join(" "), role: "gridcell", tabindex: isCursor ? "0" : "-1", "aria-label": name, "aria-selected": sameDay(day, S.value) ? "true" : "false", text: String(day.d) });
        cell.addEventListener("pointerdown", (e) => {
          keyboardFocus = false;
          e.preventDefault(); // 초점은 커서 날에 남는다 (set_pointer_focus_target)
          (cursorCell || cell).focus({ preventScroll: true });
        });
        cell.addEventListener("click", (e) => {
          if (e.detail === 0) return; // 키보드의 클릭은 keydown이 맡는다
          click(day, can);
        });
        if (isCursor) cursorCell = cell;
        row.append(cell);
      });
      grid.addEventListener("keydown", (e) => key(e, cursor));
      cal.append(head, week, grid);
      if (focusDay && cursorCell) {
        cursorCell.focus({ preventScroll: true });
        if (keyboardFocus) cursorCell.classList.add("kbd");
      }
      focusDay = false;
      if (cursorCell) {
        cursorCell.addEventListener("focus", () => cursorCell.classList.toggle("kbd", keyboardFocus));
        cursorCell.addEventListener("blur", () => cursorCell.classList.remove("kbd"));
      }
      drawState(cursor);
    }

    function drawState(cursor) {
      const b = bounds();
      stateBox.textContent = "";
      const row = (k, v) => stateBox.append(el("dt", { text: k }), el("dd", {}, v));
      row("value", el("code", { text: S.value ? iso(S.value) : "없음" }));
      row("cursor", el("span", {}, el("code", { text: iso(S.cursor) }), P.same(cursor, S.cursor) ? "" : ` → 그린 커서 ${iso(cursor)}`));
      row("today", el("code", { text: iso(S.today) }));
      row("bounds", el("code", { text: b.minimum ? `${iso(b.minimum)} ~ ${iso(b.maximum)}` : "0001-01-01 ~ 9999-12-31" }));
      row("first_day", el("code", { text: WEEKDAYS[S.firstDay] + "요일" }));
      const c = P.colorFromHsv(S.color);
      row("hsv", el("code", { text: `${fmt(S.color.hue)}° · ${fmt(S.color.saturation)} · ${fmt(S.color.value)} · ${fmt(S.color.alpha)}` }));
      row("ui_color", el("code", { text: "0x" + c.toString(16).toUpperCase().padStart(8, "0") }));
      row("16진 글", el("code", { text: P.formatHexColor(c, alphaBox.checked) }));
    }

    /* ---------- 달력 입력 (input thread의 controller와 요소가 하는 일) ---------- */
    function click(day, can) {
      const cursor = drawnCursor();
      if (!can) {
        log(`${iso(day)} 누름`, "메시지 없음", "고를 수 없는 날 — 누름은 받아 초점이 커서 날에 남는다");
        return;
      }
      const msgs = [];
      if (!sameDay(day, cursor)) msgs.push({ kind: "move_to", day });
      msgs.push({ kind: "select", day });
      const effects = msgs.map(handle);
      log(`${iso(day)} 클릭`, "→ " + describe(msgs), effects.join(" · "));
      focusDay = true; // 누름의 초점은 커서 날에 선다
      keyboardFocus = false;
      draw();
    }

    function turn(target, label, button) {
      const cursor = drawnCursor();
      if (sameDay(target, cursor)) {
        log(label, "→ 빈 목록", "목표가 커서와 같다");
        return;
      }
      const effect = handle({ kind: "turn_month", day: target });
      log(label, `→ turn_month(${iso(target)})`, effect);
      draw();
      const again = [...cal.querySelectorAll(".pk-nav")][label === "이전 달" ? 0 : 1];
      if (again && !again.disabled) again.focus({ preventScroll: true });
      else if (button) button.blur();
    }

    // calendar_element::handle_key + 실행 키(Enter·Space)
    function key(e, cursor) {
      const k = e.key === " " ? "Space" : e.key;
      if (!(k in MOVE_OF) && k !== "Enter" && k !== "Space") return;
      keyboardFocus = true;
      const label = (e.ctrlKey ? "Ctrl+" : "") + (e.altKey ? "Alt+" : "") + (e.metaKey ? "Meta+" : "") + (e.shiftKey ? "Shift+" : "") + (k === "Space" ? "Space" : k.replace("Arrow", "").replace(/^(Left|Right|Up|Down)$/, (m) => ({ Left: "←", Right: "→", Up: "↑", Down: "↓" })[m]));
      if (e.ctrlKey || e.altKey || e.metaKey) {
        log(label, "→ 흐른다", "수식 키와 함께인 키는 앱 단축키다");
        return;
      }
      const page = k === "PageUp" || k === "PageDown";
      if (e.shiftKey && !page) {
        log(label, "→ 흐른다", "Shift는 PageUp·PageDown에서만 뜻이 있다");
        return;
      }
      e.preventDefault();
      if (k === "Enter" || k === "Space") {
        if (selectable(cursor)) {
          const effect = handle({ kind: "select", day: cursor });
          log(label, `→ select(${iso(cursor)})`, effect + " (실행 키가 커서 날의 클릭으로 바꿨다)");
          focusDay = true;
          draw();
        } else log(label, "→ 빈 목록 (삼킴)", "고를 수 없는 날 — 기본 단추로 가지 않는다");
        return;
      }
      let move = MOVE_OF[k];
      if (e.shiftKey && page) move = k === "PageUp" ? "previous_year" : "next_year";
      const target = P.calendarMoved(cursor, move, bounds());
      if (sameDay(target, cursor)) {
        log(`${label} → ${move}`, "→ 빈 목록 (삼킴)", "범위 끝 — 더 갈 곳이 없다");
        return;
      }
      const effect = handle({ kind: "move_to", day: target });
      log(`${label} → ${move}`, `→ move_to(${iso(target)})`, effect);
      draw();
    }

    firstBox.addEventListener("change", () => {
      S.firstDay = Number(firstBox.value);
      log(`first_day = ${WEEKDAYS[S.firstDay]}요일`, "", "격자의 첫 칸이 바뀐다 (calendar_month_grid)");
      draw();
    });
    boundsBox.addEventListener("change", () => {
      const b = bounds();
      log(`bounds: ${boundsBox.options[boundsBox.selectedIndex].text}`, "", b.minimum && P.cmp(b.minimum, b.maximum) > 0 ? "최소가 최대보다 늦다 — 최대를 최소로 본다 (하루짜리 범위)" : "커서와 단추가 범위로 접힌다");
      draw();
    });
    weekendBox.addEventListener("change", () => {
      log(weekendBox.checked ? "available: 주말 막기" : "available 없음", "", weekendBox.checked ? "주말에도 커서는 서지만 고르지 못한다" : "범위 안의 모든 날을 고를 수 있다");
      draw();
    });
    jan31.addEventListener("click", () => {
      boundsBox.value = "none";
      S.cursor = { y: S.today.y + 1, m: 1, d: 31 };
      focusDay = true;
      keyboardFocus = true;
      log(`앱: 커서를 ${iso(S.cursor)}로 둔다`, "", "PageDown을 두 번 눌러 보라 — 2월 말일로 접힌 뒤 3월 28일이 된다");
      draw();
    });

    /* ---------- 색 ---------- */
    function drawColor() {
      const c = P.colorFromHsv(S.color);
      const values = [S.color.hue, S.color.saturation, S.color.value, S.color.alpha];
      hsvInputs.forEach((input, i) => {
        if (document.activeElement !== input) input.value = fmt(values[i]);
      });
      const r = (c >>> 16) & 255, g = (c >>> 8) & 255, b = c & 255, a = ((c >>> 24) & 255) / 255;
      swatch.style.setProperty("--pk-swatch", `rgba(${r}, ${g}, ${b}, ${a})`);
      hexOut.textContent = P.formatHexColor(c, alphaBox.checked);
      if (document.activeElement !== hexIn) hexIn.value = S.hex;
      hexIn.parentElement.classList.toggle("bad", S.hexInvalid);
      drawState(drawnCursor());
    }

    hsvInputs.forEach((input) => {
      input.addEventListener("change", () => {
        const next = { hue: Number(hsvInputs[0].value), saturation: Number(hsvInputs[1].value), value: Number(hsvInputs[2].value), alpha: Number(hsvInputs[3].value) };
        S.color = P.clampHsv(next); // 앱의 handle: clamp_hsv로 대입
        const c = P.colorFromHsv(S.color);
        S.hex = P.formatHexColor(c, alphaBox.checked); // change_to를 받으면 16진 초안을 다시 쓴다
        S.hexInvalid = false;
        log(`HSV ${fmt(S.color.hue)}° · ${fmt(S.color.saturation)} · ${fmt(S.color.value)} · ${fmt(S.color.alpha)}`, `→ color_from_hsv → 0x${c.toString(16).toUpperCase().padStart(8, "0")}`, `format_hex_color → ${S.hex}`);
        input.value = fmt(S.color[input.dataset.pk]);
        drawColor();
      });
    });

    function commit(how) {
      const check = P.checkHexColor(hexIn.value, alphaBox.checked);
      if (check.problem !== "none") {
        S.hex = hexIn.value;
        S.hexInvalid = true; // check.valid() == false
        hexError.textContent = { empty: "비었다", not_hex: "16진 밖 글자가 있다", wrong_length: alphaBox.checked ? "3·4·6·8자리가 아니다" : "3·6자리가 아니다" }[check.problem];
        log(`확정 (${how}) "${hexIn.value}"`, `→ check_hex_color → ${check.problem}`, "앱: 칸을 붉게 둔다 (invalid)");
        drawColor();
        return;
      }
      const before = S.color;
      S.color = P.hsvFromColor(check.color, before); // 지금 값을 keep으로 넘긴다
      S.hex = P.formatHexColor(P.colorFromHsv(S.color), alphaBox.checked);
      S.hexInvalid = false;
      hexError.textContent = "";
      const cr = (check.color >>> 16) & 255, cg = (check.color >>> 8) & 255, cb = check.color & 255;
      const kept = cr === 0 && cg === 0 && cb === 0 ? " — 검정이라 색상과 채도를 지금 값에서 지켰다" : cr === cg && cg === cb ? " — 회색이라 색상을 지금 값에서 지켰다" : "";
      log(`확정 (${how}) "${hexIn.value}"`, `→ check_hex_color → 0x${check.color.toString(16).toUpperCase().padStart(8, "0")} → hsv_from_color(색, 지금 값)`, `앱: ${fmt(S.color.hue)}° · ${fmt(S.color.saturation)} · ${fmt(S.color.value)} · ${fmt(S.color.alpha)}, 초안 ${S.hex}${kept}`);
      hexIn.value = S.hex;
      drawColor();
    }
    hexIn.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.ctrlKey && !e.altKey && !e.metaKey && !e.shiftKey && !e.isComposing) {
        e.preventDefault();
        commit("Enter");
      }
    });
    hexIn.addEventListener("blur", () => {
      if (hexIn.value !== S.hex || S.hexInvalid) commit("칸을 떠남");
    });
    // 넣는 조각을 hex_color_filter()로 거른다.
    hexIn.addEventListener("beforeinput", (e) => {
      if (!["insertText", "insertFromPaste", "insertFromDrop", "insertReplacementText"].includes(e.inputType)) return;
      const data = e.data != null ? e.data : e.dataTransfer ? e.dataTransfer.getData("text/plain") : "";
      const kept = P.hexColorFilter(data);
      if (kept === data) return;
      e.preventDefault();
      hexIn.setRangeText(kept, hexIn.selectionStart, hexIn.selectionEnd, "end");
      log(`넣기 "${data}"`, `→ hex_color_filter → "${kept}"`, "");
    });
    alphaBox.addEventListener("change", () => {
      S.hex = P.formatHexColor(P.colorFromHsv(S.color), alphaBox.checked);
      S.hexInvalid = false;
      hexError.textContent = "";
      hexIn.value = S.hex;
      log(alphaBox.checked ? "alpha = true" : "alpha = false", "", alphaBox.checked ? "8자리(#RRGGBBAA)를 읽고 쓴다" : "6자리만 읽는다 — 8자리는 wrong_length");
      drawColor();
    });

    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilPickersLab = { setup };
})();

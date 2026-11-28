/* luil 설명서 — 날짜 계산과 색 공간 함수를 JS로 옮긴 것
 *
 * 함수 단위로 옮긴 원본:
 *  - src/ui/calendar_date.cpp: calendar_month_grid, add_months_clamped, clamp_date(span_of), calendar_contains,
 *    calendar_start_cursor, calendar_moved, format_iso_date(append_padded), parse_iso_date(read_digits)
 *    std::chrono의 year_month_day ↔ sys_days는 그레고리력 날 수 셈(days_from_civil·civil_from_days)으로 옮겼다.
 *  - src/ui/calendar_element.cpp: 이전·다음 단추의 목표와 비활성 조건 (build의 prev·next)
 *  - src/ui/color_space.cpp: clamp_hsv, color_from_hsv, hsv_from_color, check_hex_color, format_hex_color, hex_color_filter
 * 이름 옆 주석이 C++ 원본의 이름이다. JS Date는 쓰지 않는다.
 *
 * hsv_color의 칸은 float라 들어올 때 Math.fround로 접고, color_from_hsv·hsv_from_color 안의 셈은 원본처럼 double이다.
 * 원본 소스(src/ui/calendar_date.cpp, src/ui/color_space.cpp, src/text/utf8_text.cpp)를 그대로 링크한 대조기에
 * 무작위 입력과 끝 값(말일, 윤년, 1년·9999년, 뒤집힌 범위, 0.5 경계의 채널)을 넣어 결과가 같음을 확인했다.
 *
 *   날 { y, m, d }   (m·d는 1부터. 틀린 날도 그대로 담는다 — ok()가 가른다)
 *   calendar_bounds { minimum: 날|null, maximum: 날|null }
 *   hsv_color { hue, saturation, value, alpha }   ui_color: 0xAARRGGBB (부호 없는 32비트 수)
 */
(function () {
  "use strict";
  const f = Math.fround;
  const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v); // std::clamp
  const div = (a, b) => Math.trunc(a / b); // C++ 정수 나눗셈

  /* ---------- 날짜 ---------- */

  const MOVES = ["previous_day", "next_day", "previous_week", "next_week", "previous_month", "next_month", "previous_year", "next_year", "month_start", "month_end"];
  const FIRST = { y: 1, m: 1, d: 1 }; // first_date
  const LAST = { y: 9999, m: 12, d: 31 }; // last_date
  const EPOCH = { y: 1970, m: 1, d: 1 }; // epoch_date
  const MONTH_LIMIT = 12 * 10000; // month_limit

  const isLeap = (y) => y % 4 === 0 && (y % 100 !== 0 || y % 400 === 0);
  const lastDay = (y, m) => (m === 2 ? (isLeap(y) ? 29 : 28) : [31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]);
  const yearOk = (y) => y >= -32767 && y <= 32767;
  // std::chrono::year는 short에 담긴다. 범위를 넘는 해는 16비트로 접힌다 (libstdc++).
  const wrapYear = (y) => ((((y + 32768) % 65536) + 65536) % 65536) - 32768;
  const monthOk = (m) => m >= 1 && m <= 12;
  // year_month_day::ok()
  const ok = (t) => !!t && yearOk(t.y) && monthOk(t.m) && t.d >= 1 && t.d <= 31 && t.d <= lastDay(t.y, t.m);
  const usable = (t) => t != null && ok(t);
  // year_month_day의 비교 (해, 달, 날 순)
  const cmp = (a, b) => (a.y !== b.y ? a.y - b.y : a.m !== b.m ? a.m - b.m : a.d - b.d);
  const same = (a, b) => cmp(a, b) === 0;
  const clampDay = (t, lo, hi) => (cmp(t, lo) < 0 ? lo : cmp(hi, t) < 0 ? hi : t);

  // sys_days { 날 } — 1970-01-01부터 센 날 수 (ok()인 날만 넣는다)
  function daysFromCivil(t) {
    const y = t.m <= 2 ? t.y - 1 : t.y;
    const era = div(y >= 0 ? y : y - 399, 400);
    const yoe = y - era * 400;
    const doy = div(153 * (t.m > 2 ? t.m - 3 : t.m + 9) + 2, 5) + t.d - 1;
    const doe = yoe * 365 + div(yoe, 4) - div(yoe, 100) + doy;
    return era * 146097 + doe - 719468;
  }
  // year_month_day { sys_days }
  function civilFromDays(z0) {
    const z = z0 + 719468;
    const era = div(z >= 0 ? z : z - 146096, 146097);
    const doe = z - era * 146097;
    const yoe = div(doe - div(doe, 1460) + div(doe, 36524) - div(doe, 146096), 365);
    const doy = doe - (365 * yoe + div(yoe, 4) - div(yoe, 100));
    const mp = div(5 * doy + 2, 153);
    const d = doy - div(153 * mp + 2, 5) + 1;
    const m = mp < 10 ? mp + 3 : mp - 9;
    return { y: wrapYear(yoe + era * 400 + (m <= 2 ? 1 : 0)), m, d };
  }
  // weekday { sys_days }.c_encoding() — 일요일 0
  const weekdayOf = (z) => (z >= -4 ? (z + 4) % 7 : ((z + 5) % 7) + 6);

  // span_of
  function spanOf(bounds) {
    const b = bounds || {};
    let low = FIRST;
    let high = LAST;
    if (usable(b.minimum)) low = clampDay(b.minimum, FIRST, LAST);
    if (usable(b.maximum)) high = clampDay(b.maximum, FIRST, LAST);
    if (cmp(low, high) > 0) high = low;
    return { low, high };
  }

  // calendar_month_grid(month, first_day) → 42칸
  function calendarMonthGrid(y, m, firstDay) {
    if (!(yearOk(y) && monthOk(m))) {
      y = 1970;
      m = 1;
    }
    const fd = Number.isInteger(firstDay) && firstDay >= 0 && firstDay <= 6 ? firstDay : 0; // weekday::ok()
    const first = daysFromCivil({ y, m, d: 1 });
    const back = (((weekdayOf(first) - fd) % 7) + 7) % 7; // 요일 빼기는 늘 0~6
    const cells = [];
    for (let i = 0; i < 42; i++) cells.push(civilFromDays(first - back + i));
    return cells;
  }

  // add_months_clamped(day, months)
  function addMonthsClamped(t, months) {
    if (!ok(t)) return t;
    const n = clamp(months, -MONTH_LIMIT, MONTH_LIMIT);
    const dmi = t.m - 1 + n;
    const dy = div(dmi >= 0 ? dmi : dmi - 11, 12);
    const y = wrapYear(t.y + dy);
    const m = dmi - dy * 12 + 1;
    return { y, m, d: Math.min(t.d, lastDay(y, m)) };
  }

  // clamp_date(day, bounds)
  function clampDate(t, bounds) {
    const s = spanOf(bounds);
    if (!ok(t)) return s.low;
    return clampDay(t, s.low, s.high);
  }

  // calendar_contains(bounds, day)
  function calendarContains(bounds, t) {
    if (!ok(t)) return false;
    const s = spanOf(bounds);
    return cmp(s.low, t) <= 0 && cmp(t, s.high) <= 0;
  }

  // calendar_start_cursor(value, today, bounds)
  function calendarStartCursor(value, today, bounds) {
    if (usable(value)) return clampDate(value, bounds);
    if (usable(today)) return clampDate(today, bounds);
    if (bounds && usable(bounds.minimum)) return clampDate(bounds.minimum, bounds);
    return clampDate(EPOCH, bounds);
  }

  // calendar_moved(cursor, move, bounds)
  function calendarMoved(cursor, move, bounds) {
    const from = clampDate(cursor, bounds);
    const z = daysFromCivil(from);
    let target = from;
    if (move === "previous_day") target = civilFromDays(z - 1);
    else if (move === "next_day") target = civilFromDays(z + 1);
    else if (move === "previous_week") target = civilFromDays(z - 7);
    else if (move === "next_week") target = civilFromDays(z + 7);
    else if (move === "previous_month") target = addMonthsClamped(from, -1);
    else if (move === "next_month") target = addMonthsClamped(from, 1);
    else if (move === "previous_year") target = addMonthsClamped(from, -12);
    else if (move === "next_year") target = addMonthsClamped(from, 12);
    else if (move === "month_start") target = { y: from.y, m: from.m, d: 1 };
    else if (move === "month_end") target = { y: from.y, m: from.m, d: lastDay(from.y, from.m) };
    return clampDate(target, bounds);
  }

  // append_padded
  function padded(value, width) {
    const s = String(Math.abs(value));
    return (value < 0 ? "-" : "") + "0".repeat(Math.max(0, width - s.length)) + s;
  }
  // format_iso_date
  const formatIsoDate = (t) => padded(t.y, 4) + "-" + padded(t.m, 2) + "-" + padded(t.d, 2);

  // read_digits
  function readDigits(text) {
    let value = 0;
    for (const c of text) {
      if (c < "0" || c > "9") return -1;
      value = value * 10 + (c.charCodeAt(0) - 48);
    }
    return value;
  }
  // parse_iso_date → 날 또는 null
  function parseIsoDate(text) {
    if (text.length !== 10 || text[4] !== "-" || text[7] !== "-") return null;
    const y = readDigits(text.slice(0, 4));
    const m = readDigits(text.slice(5, 7));
    const d = readDigits(text.slice(8, 10));
    if (y < 1 || m < 0 || d < 0) return null;
    const result = { y, m, d };
    return ok(result) ? result : null;
  }

  // calendar_element의 이전·다음 단추: 목표와 비활성 (build)
  function monthButtons(cursor, bounds) {
    const s = spanOf(bounds);
    const prevLast = addMonthsClamped({ y: cursor.y, m: cursor.m, d: 1 }, -1);
    const nextFirst = addMonthsClamped({ y: cursor.y, m: cursor.m, d: 1 }, 1);
    return {
      previous: clampDate(addMonthsClamped(cursor, -1), bounds),
      next: clampDate(addMonthsClamped(cursor, 1), bounds),
      previousEnabled: cmp({ y: prevLast.y, m: prevLast.m, d: lastDay(prevLast.y, prevLast.m) }, s.low) >= 0,
      nextEnabled: cmp(nextFirst, s.high) <= 0,
    };
  }

  /* ---------- 색 ---------- */

  const isSpace = (c) => c === 0x20 || c === 0x09 || c === 0x0d || c === 0x0a || c === 0xa0 || c === 0x3000;
  const roundAway = (x) => (x < 0 ? -Math.round(-x) : Math.round(x)); // std::lround

  // clamp_unit
  const clampUnit = (value, fallback) => (Number.isNaN(value) ? fallback : clamp(value, 0, 1));

  // clamp_hsv — 들어오는 칸은 float다.
  function clampHsv(color) {
    const hue = f(color.hue);
    return {
      hue: Number.isNaN(hue) ? 0 : clamp(hue, 0, 360),
      saturation: clampUnit(f(color.saturation), 0),
      value: clampUnit(f(color.value), 0),
      alpha: clampUnit(f(color.alpha == null ? 1 : color.alpha), 1),
    };
  }

  const makeUiColor = (r, g, b, a) => ((((a & 0xff) << 24) | ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff)) >>> 0); // make_ui_color
  const channelOf = (unit) => roundAway(clamp(unit, 0, 1) * 255); // channel_of

  // color_from_hsv — 계산은 double이다.
  function colorFromHsv(color) {
    const c = clampHsv(color);
    const hue = c.hue >= 360 ? 0 : c.hue;
    const saturation = c.saturation;
    const value = c.value;
    const channel = (n) => {
      const k = (n + hue / 60) % 6; // std::fmod (양수)
      return value - value * saturation * Math.max(0, Math.min(k, 4 - k, 1));
    };
    return makeUiColor(channelOf(channel(5)), channelOf(channel(3)), channelOf(channel(1)), channelOf(c.alpha));
  }

  // hsv_from_color(color, keep)
  function hsvFromColor(color, keep) {
    const kept = clampHsv(keep || { hue: 0, saturation: 0, value: 0, alpha: 1 });
    const red = ((color >>> 16) & 0xff) / 255;
    const green = ((color >>> 8) & 0xff) / 255;
    const blue = (color & 0xff) / 255;
    const maximum = Math.max(red, green, blue);
    const minimum = Math.min(red, green, blue);
    const range = maximum - minimum;
    const result = { hue: 0, saturation: 0, value: f(maximum), alpha: f(((color >>> 24) & 0xff) / 255) };
    if (maximum <= 0) {
      result.hue = kept.hue;
      result.saturation = kept.saturation;
      return result;
    }
    result.saturation = f(range / maximum);
    if (range <= 0) {
      result.hue = kept.hue;
      return result;
    }
    let hue;
    if (maximum === red) hue = ((green - blue) / range + 6) % 6;
    else if (maximum === green) hue = (blue - red) / range + 2;
    else hue = (red - green) / range + 4;
    result.hue = f(hue * 60);
    return result;
  }

  // ascii_of (16진 꼴) — 읽을 수 없는 글자는 '?'다.
  function hexAscii(c) {
    if ((c >= 0x30 && c <= 0x39) || (c >= 0x61 && c <= 0x66) || (c >= 0x41 && c <= 0x46) || c === 0x23) return String.fromCharCode(c);
    if (c >= 0xff10 && c <= 0xff19) return String.fromCharCode(0x30 + c - 0xff10);
    if (c >= 0xff21 && c <= 0xff26) return String.fromCharCode(0x41 + c - 0xff21);
    if (c >= 0xff41 && c <= 0xff46) return String.fromCharCode(0x61 + c - 0xff41);
    if (c === 0xff03) return "#";
    if (isSpace(c)) return " ";
    return "?";
  }
  function asciiForm(text) {
    let r = "";
    for (const ch of text) r += hexAscii(ch.codePointAt(0));
    return r;
  }
  const digitOf = (c) => (c >= "0" && c <= "9" ? c.charCodeAt(0) - 48 : c >= "a" && c <= "f" ? c.charCodeAt(0) - 87 : c >= "A" && c <= "F" ? c.charCodeAt(0) - 55 : -1);

  // check_hex_color(text, alpha) → { problem, color(없으면 null) }
  function checkHexColor(text, alpha) {
    const ascii = asciiForm(text);
    if (!/[^ ]/.test(ascii)) return { problem: "empty", color: null };
    let digits = ascii.replace(/^ +| +$/g, "");
    if (digits[0] === "#") digits = digits.slice(1);
    // 16진 밖 글자가 자릿수보다 먼저다.
    for (const c of digits) if (digitOf(c) < 0) return { problem: "not_hex", color: null };
    const length = digits.length;
    const shortForm = length === 3 || (alpha && length === 4);
    const longForm = length === 6 || (alpha && length === 8);
    if (!shortForm && !longForm) return { problem: "wrong_length", color: null };
    const channels = [0, 0, 0, 0xff];
    const count = shortForm ? length : length / 2;
    for (let i = 0; i < count; i++) {
      if (shortForm) {
        const d = digitOf(digits[i]);
        channels[i] = d * 16 + d;
      } else channels[i] = digitOf(digits[i * 2]) * 16 + digitOf(digits[i * 2 + 1]);
    }
    return { problem: "none", color: makeUiColor(channels[0], channels[1], channels[2], channels[3]) };
  }

  // format_hex_color(color, alpha)
  function formatHexColor(color, alpha) {
    const byte = (v) => ((v >>> 0) & 0xff).toString(16).toUpperCase().padStart(2, "0");
    return "#" + byte(color >>> 16) + byte(color >>> 8) + byte(color) + (alpha ? byte(color >>> 24) : "");
  }

  // hex_color_filter() — 넣는 조각에서 16진 글자와 '#'만 남긴다.
  const hexColorFilter = (text) => asciiForm(text).replace(/[ ?]/g, "");

  window.LuilPickers = {
    MOVES, ok, cmp, same, lastDay, daysFromCivil, civilFromDays, weekdayOf,
    calendarMonthGrid, addMonthsClamped, clampDate, calendarContains, calendarStartCursor, calendarMoved, formatIsoDate, parseIsoDate, monthButtons,
    clampHsv, colorFromHsv, hsvFromColor, checkHexColor, formatHexColor, hexColorFilter, makeUiColor,
  };
})();

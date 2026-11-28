/* luil 설명서 — 숫자 칸 도우미와 주소 찾기를 JS로 옮긴 것
 *
 * src/ui/number_input.cpp (check_number_input, format_number_input, stepped_number_input, number_input_filter)와
 * src/ui/text_document.cpp (detect_text_links)를 함수 단위로 옮겼다. 이름 옆 주석은 C++ 원본의 이름이다.
 * 글은 UTF-8 바이트(Uint8Array)로 다룬다. 원본이 바이트를 읽고, 링크 구간도 UTF-8 byte offset이기 때문이다.
 * JS 문자열을 주면 UTF-8로 바꿔 읽는다.
 *
 * 정수 값(unit = 값 × 10^decimals)은 원본에서 long long이다. 모든 중간값이 2^53 안에 있어 Number로 정확하다:
 * unit은 ±10^15로 접히고, 걸음 계산의 곱·합도 그 몇 배를 넘지 않는다. 반올림(std::llround·std::round)은
 * 0에서 먼 쪽으로 맞춘 roundHalfAway로 옮겼다.
 * 원본 두 .cpp를 그대로 컴파일한 대조기와 무작위·경계 입력을 견주어 같음을 확인했다 (text-input.html의 실습 설명).
 *
 *   number_input_range { minimum, maximum, step, page_step, decimals }   빠진 필드는 C++ 기본값
 *   number_input_check { problem, value }   problem: "none"|"empty"|"not_a_number"|"too_precise"|"below_minimum"|"above_maximum"
 *                                            value: 읽은 값, 못 읽었으면 null (std::optional)
 *   value_step: "decrease"|"increase"|"decrease_page"|"increase_page"|"minimum"|"maximum"
 *   text_link { begin, end, destination }    begin·end는 UTF-8 byte offset
 */
(function () {
  "use strict";

  const UNIT_LIMIT = 1e15; // unit_limit
  const MAXIMUM_DECIMALS = 9; // maximum_decimals
  const FOREIGN = 0x3f; // foreign_character (u8'?')
  const LOWEST = -Number.MAX_VALUE; // std::numeric_limits<double>::lowest()
  const HIGHEST = Number.MAX_VALUE; // std::numeric_limits<double>::max()

  const encoder = new TextEncoder();
  const bytesOf = (text) => (text instanceof Uint8Array ? text : encoder.encode(String(text)));
  const asciiString = (codes) => {
    let out = "";
    for (const c of codes) out += String.fromCharCode(c);
    return out;
  };

  // 0에서 먼 쪽으로 반올림한다 (std::llround, std::round). a - floor(a)는 double에서 정확하다.
  function roundHalfAway(x) {
    const a = Math.abs(x);
    const f = Math.floor(a);
    const r = a - f >= 0.5 ? f + 1 : f;
    return x < 0 ? -r : r;
  }

  function rangeOf(range) {
    const r = range || {};
    return {
      minimum: r.minimum === undefined ? LOWEST : r.minimum,
      maximum: r.maximum === undefined ? HIGHEST : r.maximum,
      step: r.step === undefined ? 1 : r.step,
      page_step: r.page_step === undefined ? 0 : r.page_step,
      decimals: r.decimals === undefined ? 0 : r.decimals,
    };
  }

  const decimalsOf = (range) => Math.min(Math.max(range.decimals, 0), MAXIMUM_DECIMALS); // decimals_of

  function scaleOf(range) {
    // scale_of
    let scale = 1;
    for (let i = 0; i < decimalsOf(range); i++) scale *= 10;
    return scale;
  }

  function toUnits(value, scale) {
    // to_units
    const scaled = value * scale;
    if (Number.isNaN(scaled)) return 0;
    if (scaled >= UNIT_LIMIT) return UNIT_LIMIT;
    if (scaled <= -UNIT_LIMIT) return -UNIT_LIMIT;
    return roundHalfAway(scaled) + 0; // -0을 0으로 (long long에는 -0이 없다)
  }

  const hasMinimum = (range) => range.minimum > LOWEST; // has_minimum
  const hasMaximum = (range) => range.maximum < HIGHEST; // has_maximum
  const minimumUnits = (range) => (hasMinimum(range) ? toUnits(range.minimum, scaleOf(range)) : -UNIT_LIMIT); // minimum_units
  const maximumUnits = (range) => (hasMaximum(range) ? toUnits(range.maximum, scaleOf(range)) : UNIT_LIMIT); // maximum_units

  // next_code_point — UTF-8 한 글자와 다음 자리. 깨진 바이트는 한 바이트짜리 U+FFFD다. 이음 바이트는 검사하지 않는다
  function nextCodePoint(b, at) {
    const lead = b[at];
    let length = 1;
    let value = lead;
    if (lead >= 0xf0) {
      length = 4;
      value = lead & 0x07;
    } else if (lead >= 0xe0) {
      length = 3;
      value = lead & 0x0f;
    } else if (lead >= 0xc0) {
      length = 2;
      value = lead & 0x1f;
    } else if (lead >= 0x80) {
      return [0xfffd, at + 1];
    }
    if (at + length > b.length) return [0xfffd, b.length];
    for (let i = 1; i < length; i++) value = (value << 6) | (b[at + i] & 0x3f);
    return [value, at + length];
  }

  const isSpace = (v) => v === 0x20 || v === 0x09 || v === 0x0d || v === 0x0a || v === 0x00a0 || v === 0x3000; // is_space

  function asciiOf(v) {
    // ascii_of — 숫자 꼴 글자를 ASCII로. 공백은 공백, 그 밖은 '?'
    if (v >= 0x30 && v <= 0x39) return v;
    if (v >= 0xff10 && v <= 0xff19) return 0x30 + (v - 0xff10);
    if (v === 0x2e || v === 0xff0e) return 0x2e;
    if (v === 0x2d || v === 0xff0d || v === 0x2212 || v === 0xfe63) return 0x2d;
    if (v === 0x2b || v === 0xff0b) return 0x2b;
    if (isSpace(v)) return 0x20;
    return FOREIGN;
  }

  function normalized(b) {
    // normalized — ASCII 코드 배열, 앞뒤 공백을 뺀다
    const out = [];
    for (let at = 0; at < b.length; ) {
      const [cp, next] = nextCodePoint(b, at);
      at = next;
      out.push(asciiOf(cp));
    }
    let first = 0;
    while (first < out.length && out[first] === 0x20) first++;
    if (first === out.length) return [];
    let last = out.length - 1;
    while (out[last] === 0x20) last--;
    return out.slice(first, last + 1);
  }

  const isDigit = (c) => c >= 0x30 && c <= 0x39;

  function parse(b, range) {
    // parse → parsed_number { problem, value, units, precise, overflow }
    const v = normalized(b);
    const failed = (problem) => ({ problem, value: 0, units: 0, precise: true, overflow: false });
    if (v.length === 0) return failed("empty");
    let at = 0;
    const negative = v[at] === 0x2d;
    if (v[at] === 0x2d || v[at] === 0x2b) at++;
    let whole = [];
    const fraction = [];
    while (at < v.length && isDigit(v[at])) whole.push(v[at++]);
    if (at < v.length && v[at] === 0x2e) {
      at++;
      while (at < v.length && isDigit(v[at])) fraction.push(v[at++]);
    }
    if (at !== v.length || (whole.length === 0 && fraction.length === 0)) return failed("not_a_number");

    while (fraction.length > 0 && fraction[fraction.length - 1] === 0x30) fraction.pop();
    const firstDigit = whole.findIndex((c) => c !== 0x30);
    whole = firstDigit < 0 ? [] : whole.slice(firstDigit);

    const decimals = decimalsOf(range);
    const result = { problem: "none", value: 0, units: 0, precise: fraction.length <= decimals, overflow: whole.length + decimals > 15 };
    let approximate = 0;
    for (const d of whole) approximate = approximate * 10 + (d - 0x30);
    let place = 0.1;
    for (const d of fraction) {
      approximate += (d - 0x30) * place;
      place /= 10;
    }
    result.value = negative ? -approximate : approximate;
    if (!result.overflow) {
      let units = 0;
      for (const d of whole) units = units * 10 + (d - 0x30);
      for (let i = 0; i < decimals; i++) units = units * 10 + (i < fraction.length ? fraction[i] - 0x30 : 0);
      result.units = negative ? -units + 0 : units;
      if (units > UNIT_LIMIT) result.overflow = true;
    }
    return result;
  }

  function formatUnits(units, decimals) {
    // format_units
    const negative = units < 0;
    const digits = [];
    for (let rest = negative ? -units : units; rest > 0 || digits.length <= decimals; rest = (rest - (rest % 10)) / 10) digits.unshift(0x30 + (rest % 10));
    if (decimals > 0) digits.splice(digits.length - decimals, 0, 0x2e);
    if (negative) digits.unshift(0x2d);
    return asciiString(digits);
  }

  function checkNumberInput(text, range) {
    // check_number_input
    const r = rangeOf(range);
    const parsed = parse(bytesOf(text), r);
    if (parsed.problem !== "none") return { problem: parsed.problem, value: null };
    const result = { problem: "none", value: parsed.value };
    if (!parsed.precise) result.problem = "too_precise";
    else if (parsed.overflow) result.problem = parsed.value < 0 ? "below_minimum" : "above_maximum";
    else if (parsed.units < minimumUnits(r)) result.problem = "below_minimum";
    else if (parsed.units > maximumUnits(r)) result.problem = "above_maximum";
    return result;
  }

  function formatNumberInput(value, range) {
    // format_number_input
    const r = rangeOf(range);
    return formatUnits(toUnits(value, scaleOf(r)), decimalsOf(r));
  }

  function sameAscii(result, b) {
    if (result.length !== b.length) return false;
    for (let i = 0; i < b.length; i++) if (result.charCodeAt(i) !== b[i]) return false;
    return true;
  }

  function steppedNumberInput(text, range, step) {
    // stepped_number_input — 다음 글, 이미 같은 글이면(끝에 닿았다) null (std::nullopt)
    const r = rangeOf(range);
    const b = bytesOf(text);
    const scale = scaleOf(r);
    const lowest = minimumUnits(r);
    const highest = Math.max(lowest, maximumUnits(r));
    const clamped = (u) => Math.min(Math.max(u, lowest), highest);
    const parsed = parse(b, r);

    let target;
    if (step === "minimum") {
      if (!hasMinimum(r)) return null;
      target = lowest;
    } else if (step === "maximum") {
      if (!hasMaximum(r)) return null;
      target = highest;
    } else if (parsed.problem !== "none") target = clamped(0);
    else {
      const stepUnits = Math.max(1, toUnits(r.step, scale));
      const page = step === "increase_page" || step === "decrease_page";
      const delta = page ? (r.page_step > 0 ? Math.max(1, toUnits(r.page_step, scale)) : stepUnits * 10) : stepUnits;
      const up = step === "increase" || step === "increase_page";
      // 넘친 값은 그 끝에서 시작한다. 자릿수보다 자세한 값은 어림한 자리가 눈금 사이에 선다
      const current = parsed.overflow ? (parsed.value < 0 ? -UNIT_LIMIT : UNIT_LIMIT) : parsed.precise ? parsed.units : parsed.value * scale;
      const base = hasMinimum(r) ? lowest : 0;
      const grid = (current - base) / stepUnits;
      const nearest = roundHalfAway(grid);
      let next;
      if (Math.abs(grid - nearest) < 1e-9) next = base + nearest * stepUnits + (up ? delta : -delta);
      else next = base + (up ? Math.ceil(grid) : Math.floor(grid)) * stepUnits;
      target = clamped(Math.min(Math.max(next, -UNIT_LIMIT), UNIT_LIMIT));
    }
    const result = formatUnits(target + 0, decimalsOf(r));
    if (sameAscii(result, b)) return null;
    return result;
  }

  function numberInputFilter(range) {
    // number_input_filter — 넣는 조각에서 그 범위의 숫자에 나올 글자만 남긴다
    const r = rangeOf(range);
    const signedValues = r.minimum < 0;
    const fractions = decimalsOf(r) > 0;
    return (text) => {
      const b = bytesOf(text);
      const kept = [];
      for (let at = 0; at < b.length; ) {
        const [cp, next] = nextCodePoint(b, at);
        at = next;
        const c = asciiOf(cp);
        if (isDigit(c) || (c === 0x2e && fractions) || ((c === 0x2d || c === 0x2b) && signedValues)) kept.push(c);
      }
      return asciiString(kept);
    };
  }

  // ---------- detect_text_links (text_document.cpp) ----------

  const asciiAlphanumeric = (c) => (c >= 0x30 && c <= 0x39) || (c >= 0x61 && c <= 0x7a) || (c >= 0x41 && c <= 0x5a); // ascii_alphanumeric
  const urlCharacter = (c) => c > 0x20 && c < 0x7f && c !== 0x3c && c !== 0x3e && c !== 0x22; // url_character
  const has = (set, c) => set.indexOf(String.fromCharCode(c)) >= 0;
  const mailLocalCharacter = (c) => asciiAlphanumeric(c) || has("._%+-", c); // mail_local_character
  const domainCharacter = (c) => asciiAlphanumeric(c) || c === 0x2e || c === 0x2d; // domain_character
  const joinsWord = (c) => asciiAlphanumeric(c) || has(".-_@/", c); // joins_word

  function startsWithFolded(b, at, prefix) {
    // starts_with_folded — prefix는 소문자 ASCII
    if (b.length - at < prefix.length) return false;
    for (let i = 0; i < prefix.length; i++) {
      const c = b[at + i];
      const folded = c >= 0x41 && c <= 0x5a ? c - 0x41 + 0x61 : c;
      if (folded !== prefix.charCodeAt(i)) return false;
    }
    return true;
  }

  function trimUrlEnd(b, begin, end) {
    // trim_url_end — 끝의 문장 부호와 짝 없는 닫는 괄호를 뺀다
    const count = (c) => {
      let n = 0;
      for (let i = begin; i < end; i++) if (b[i] === c) n++;
      return n;
    };
    const unmatched = (open, close) => b[end - 1] === close && count(close) > count(open);
    while (end > begin)
      if (has(".,;:!?'*", b[end - 1]) || unmatched(0x28, 0x29) || unmatched(0x5b, 0x5d) || unmatched(0x7b, 0x7d)) end--;
      else break;
    return end;
  }

  const PREFIXES = [
    // prefixes — { text, scheme, needs_dot }
    { text: "https://", scheme: "", needsDot: false },
    { text: "http://", scheme: "", needsDot: false },
    { text: "mailto:", scheme: "", needsDot: false },
    { text: "www.", scheme: "https://", needsDot: true },
  ];

  function detectAddresses(b) {
    // detect_addresses
    const links = [];
    let at = 0;
    while (at < b.length) {
      let matched = false;
      if (at === 0 || !joinsWord(b[at - 1]))
        for (const candidate of PREFIXES) {
          if (!startsWithFolded(b, at, candidate.text)) continue;
          let end = at + candidate.text.length;
          while (end < b.length && urlCharacter(b[end])) end++;
          end = trimUrlEnd(b, at, end);
          const body = at + candidate.text.length;
          const rest = b.subarray(body, body + (end > body ? end - body : 0));
          let alnum = false;
          let dot = false;
          for (const c of rest) {
            if (asciiAlphanumeric(c)) alnum = true;
            if (c === 0x2e) dot = true;
          }
          if (!alnum || (candidate.needsDot && !dot)) continue;
          links.push({ begin: at, end, destination: candidate.scheme + asciiString(b.subarray(at, end)) });
          at = end;
          matched = true;
          break;
        }
      if (!matched) at++;
    }
    return links;
  }

  function textLinkAt(links, offset) {
    // text_link_at — offset의 글자를 품은 링크 (끝 offset은 품지 않는다)
    for (const link of links) if (link.begin <= offset && offset < link.end) return link;
    return null;
  }

  function detectMail(b, addresses) {
    // detect_mail — `이름@도메인.최상위`. 이미 주소인 자리의 @는 건너뛴다
    const mail = [];
    for (let at = b.indexOf(0x40); at >= 0; at = b.indexOf(0x40, at + 1)) {
      if (textLinkAt(addresses, at)) continue;
      let begin = at;
      while (begin > 0 && mailLocalCharacter(b[begin - 1])) begin--;
      while (begin < at && b[begin] === 0x2e) begin++;
      let end = at + 1;
      while (end < b.length && domainCharacter(b[end])) end++;
      while (end > at + 1 && (b[end - 1] === 0x2e || b[end - 1] === 0x2d)) end--;
      const domain = b.subarray(at + 1, end);
      const dot = domain.lastIndexOf(0x2e);
      if (begin === at || dot < 0 || dot === 0) continue;
      const top = domain.subarray(dot + 1);
      let alphabetic = true;
      for (const c of top) if (!((c >= 0x61 && c <= 0x7a) || (c >= 0x41 && c <= 0x5a))) alphabetic = false;
      if (top.length < 2 || !alphabetic) continue;
      mail.push({ begin, end, destination: "mailto:" + asciiString(b.subarray(begin, end)) });
    }
    return mail;
  }

  function detectTextLinks(text) {
    // detect_text_links — begin 순, 겹치지 않는다
    const b = bytesOf(text);
    const links = detectAddresses(b);
    const mail = detectMail(b, links);
    if (mail.length === 0) return links;
    // std::merge: 같은 begin이면 앞 범위(주소)가 먼저다
    const merged = [];
    let i = 0;
    let j = 0;
    while (i < links.length || j < mail.length) {
      if (i >= links.length) merged.push(mail[j++]);
      else if (j >= mail.length) merged.push(links[i++]);
      else if (mail[j].begin < links[i].begin) merged.push(mail[j++]);
      else merged.push(links[i++]);
    }
    const result = [];
    for (const link of merged) if (result.length === 0 || result[result.length - 1].end <= link.begin) result.push(link);
    return result;
  }

  window.LuilTextInput = {
    bytesOf,
    nextCodePoint,
    checkNumberInput,
    formatNumberInput,
    steppedNumberInput,
    numberInputFilter,
    detectTextLinks,
  };
})();

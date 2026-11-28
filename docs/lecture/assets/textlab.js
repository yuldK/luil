/* luil 글자 도구의 브라우저 이식 — 8강 실험용
 *
 * 옮긴 것 (모두 UTF-8 byte offset):
 *   - src/text/utf8_text.cpp    : 엄격한 해독, 잘못된 바이트마다 U+FFFD 하나
 *   - src/text/grapheme.cpp     : Backspace 예외 (grapheme_backspace_offset)
 *   - src/text/text_edit.cpp    : ← → Delete, 낱말 이동 (is_word_byte 규칙)
 *   - src/text/case_fold.cpp    : 단순 대소문자 접기 표 전부, find_folded
 * 글자소 경계는 브라우저의 Intl.Segmenter다 (luil 내장 표는 Unicode 16.0.0).
 * 한글 조합기(Hangul)는 luil 밖이다 — 운영체제 IME가 하는 일을 흉내 낸 것이다.
 */
(function () {
  "use strict";
  const LB = window.LuilLineBreak;
  const cpLen = (cp) => (cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4);
  const enc = new TextEncoder();
  const bytesOf = (s) => enc.encode(s);
  const hex2 = (b) => b.toString(16).toUpperCase().padStart(2, "0");
  const U = (cp) => "U+" + cp.toString(16).toUpperCase().padStart(4, "0");

  // 코드포인트 목록: { cp, ch, byte, index, len }
  function codepoints(s) {
    const out = [];
    let byte = 0, index = 0;
    for (const ch of s) {
      const cp = ch.codePointAt(0);
      out.push({ cp, ch, byte, index, len: cpLen(cp) });
      byte += cpLen(cp);
      index += ch.length;
    }
    return out;
  }
  const byteLength = (s) => { let n = 0; for (const ch of s) n += cpLen(ch.codePointAt(0)); return n; };
  function indexOfByte(s, at) {
    let byte = 0, index = 0;
    for (const ch of s) { if (byte >= at) return index; byte += cpLen(ch.codePointAt(0)); index += ch.length; }
    return s.length;
  }
  const sliceBytes = (s, a, b) => s.slice(indexOfByte(s, a), indexOfByte(s, b));

  /* ---------- UTF-8 (utf8_text.cpp decode_strict / utf8_replace_invalid) ---------- */
  function decodeStrict(bytes, at) {
    const lead = bytes[at];
    if (lead < 0x80) return { ok: true, cp: lead, len: 1 };
    let len, min, cp;
    if ((lead & 0xe0) === 0xc0) { len = 2; min = 0x80; cp = lead & 0x1f; }
    else if ((lead & 0xf0) === 0xe0) { len = 3; min = 0x800; cp = lead & 0x0f; }
    else if ((lead & 0xf8) === 0xf0) { len = 4; min = 0x10000; cp = lead & 0x07; }
    else return { ok: false, why: "시작 바이트가 아니다" };
    if (at + len > bytes.length) return { ok: false, why: "이어짐 바이트가 모자란다" };
    for (let k = 1; k < len; ++k) {
      const b = bytes[at + k];
      if ((b & 0xc0) !== 0x80) return { ok: false, why: "이어짐 바이트(10xxxxxx)가 아니다" };
      cp = (cp << 6) | (b & 0x3f);
    }
    if (cp < min) return { ok: false, why: "과잉 인코딩(overlong)" };
    if (cp > 0x10ffff) return { ok: false, why: "U+10FFFF를 넘는다" };
    if (cp >= 0xd800 && cp <= 0xdfff) return { ok: false, why: "surrogate" };
    return { ok: true, cp, len };
  }
  // [{ at, len, ok, cp, why }] — 잘못된 자리는 U+FFFD 하나를 넣고 1 byte만 나아간다.
  function replaceInvalid(bytes) {
    const out = [];
    for (let at = 0; at < bytes.length;) {
      const d = decodeStrict(bytes, at);
      if (d.ok) { out.push({ at, len: d.len, ok: true, cp: d.cp }); at += d.len; }
      else { out.push({ at, len: 1, ok: false, cp: 0xfffd, why: d.why }); at += 1; }
    }
    return out;
  }

  /* ---------- 글자소와 편집 (text_edit.cpp, grapheme.cpp) ---------- */
  const boundaries = (s) => LB.graphemes(s).map((g) => g.byte);
  const prevBoundary = (B, at) => { let p = 0; for (const b of B) { if (b < at) p = b; else break; } return p; };
  const nextBoundary = (B, at, size) => { for (const b of B) if (b > at) return b; return size; };

  const reMark = /^\p{M}$/u;
  const rePict = /^\p{Extended_Pictographic}$/u;
  const inR = (cp, a, b) => cp >= a && cp <= b;
  const isJamoL = (cp) => inR(cp, 0x1100, 0x115f) || inR(cp, 0xa960, 0xa97c);
  const isJamoV = (cp) => inR(cp, 0x1160, 0x11a7) || inR(cp, 0xd7b0, 0xd7c6) || cp === 0x16d63 || inR(cp, 0x16d67, 0x16d6a);
  const isJamoT = (cp) => inR(cp, 0x11a8, 0x11ff) || inR(cp, 0xd7cb, 0xd7fb);
  // Backspace를 결합 부호 하나로 줄이지 못하게 하는 표식 (grapheme.cpp 286–293행)
  function keepsWhole(cp) {
    const ch = String.fromCodePoint(cp);
    if (cp === 0x200d) return "ZWJ";
    if (cp === 0x20e3) return "keycap U+20E3";
    if (inR(cp, 0xfe00, 0xfe0f) || inR(cp, 0xe0100, 0xe01ef)) return "이체 선택자";
    if (inR(cp, 0x1f3fb, 0x1f3ff)) return "피부색 수식";
    if (rePict.test(ch)) return "그림 문자";
    if (inR(cp, 0x1f1e6, 0x1f1ff)) return "RI(국기 글자)";
    if (isJamoL(cp) || isJamoV(cp) || isJamoT(cp)) return "한글 자모 L·V·T";
    return null;
  }
  // { from, to, whole, why } — 지울 구간
  function backspaceRange(s, caret) {
    if (caret <= 0) return null;
    const B = boundaries(s);
    const begin = prevBoundary(B, caret);
    const cps = codepoints(s).filter((c) => c.byte >= begin && c.byte < caret);
    const last = cps[cps.length - 1];
    if (!reMark.test(last.ch)) return { from: begin, to: caret, whole: true, why: "마지막 코드포인트 " + U(last.cp) + "가 결합 부호가 아니다" };
    for (const c of cps) {
      const k = keepsWhole(c.cp);
      if (k) return { from: begin, to: caret, whole: true, why: "끝은 결합 부호지만 " + k + "(" + U(c.cp) + ")가 들어 있다" };
    }
    return { from: last.byte, to: caret, whole: false, why: "결합 부호 " + U(last.cp) + " 하나만 벗긴다" };
  }
  const isWordByte = (b) => b >= 0x80 || (b >= 0x61 && b <= 0x7a) || (b >= 0x41 && b <= 0x5a) || (b >= 0x30 && b <= 0x39);
  function wordLeft(s, offset) {
    const bytes = bytesOf(s), B = boundaries(s);
    while (offset > 0 && !isWordByte(bytes[prevBoundary(B, offset)])) offset = prevBoundary(B, offset);
    while (offset > 0 && isWordByte(bytes[prevBoundary(B, offset)])) offset = prevBoundary(B, offset);
    return offset;
  }
  function wordRight(s, offset) {
    const bytes = bytesOf(s), B = boundaries(s), size = bytes.length;
    while (offset < size && isWordByte(bytes[offset])) offset = nextBoundary(B, offset, size);
    while (offset < size && !isWordByte(bytes[offset])) offset = nextBoundary(B, offset, size);
    return offset;
  }

  /* ---------- 대소문자 접기 (case_fold.cpp 34–104행의 표 전부) ---------- */
  const OFFSET_RANGES = [[0x41, 0x5a, 0x20], [0xc0, 0xd6, 0x20], [0xd8, 0xde, 0x20], [0x388, 0x38a, 0x25], [0x38e, 0x38f, 0x3f], [0x391, 0x3a1, 0x20], [0x3a3, 0x3ab, 0x20],
    [0x400, 0x40f, 0x50], [0x410, 0x42f, 0x20], [0x531, 0x556, 0x30], [0x2160, 0x216f, 0x10], [0x24b6, 0x24cf, 0x1a], [0xff21, 0xff3a, 0x20]];
  const PAIRED_RANGES = [[0x100, 0x12f, false], [0x132, 0x137, false], [0x139, 0x148, true], [0x14a, 0x177, false], [0x179, 0x17e, true], [0x1cd, 0x1dc, true], [0x1de, 0x1ef, false],
    [0x1f8, 0x21f, false], [0x222, 0x233, false], [0x246, 0x24f, false], [0x3d8, 0x3ef, false], [0x460, 0x481, false], [0x48a, 0x4bf, false], [0x4c1, 0x4ce, true], [0x4d0, 0x52f, false],
    [0x1e00, 0x1e95, false], [0x1ea0, 0x1eff, false]];
  const SINGLE = new Map([[0xb5, 0x3bc], [0x178, 0xff], [0x17f, 0x73], [0x1c4, 0x1c6], [0x1c5, 0x1c6], [0x1c7, 0x1c9], [0x1c8, 0x1c9], [0x1ca, 0x1cc], [0x1cb, 0x1cc], [0x1f1, 0x1f3],
    [0x1f2, 0x1f3], [0x1f4, 0x1f5], [0x386, 0x3ac], [0x38c, 0x3cc], [0x3c2, 0x3c3], [0x3cf, 0x3d7], [0x3d0, 0x3b2], [0x3d1, 0x3b8], [0x3d5, 0x3c6], [0x3d6, 0x3c0], [0x3f0, 0x3ba],
    [0x3f1, 0x3c1], [0x3f5, 0x3b5], [0x4c0, 0x4cf], [0x1e9e, 0xdf], [0x2126, 0x3c9], [0x212a, 0x6b], [0x212b, 0xe5]]);
  function foldCp(cp) {
    if (cp < 0x41) return cp;
    for (const [a, b, d] of OFFSET_RANGES) if (cp >= a && cp <= b) return cp + d;
    if (cp < 0xb5) return cp;
    for (const [a, b, upperOdd] of PAIRED_RANGES) if (cp >= a && cp <= b) return ((cp & 1) !== 0) === upperOdd ? cp + 1 : cp;
    return SINGLE.has(cp) ? SINGLE.get(cp) : cp;
  }
  const foldString = (s) => { let o = ""; for (const ch of s) o += String.fromCodePoint(foldCp(ch.codePointAt(0))); return o; };
  // 원문 byte 구간 { begin, end } 또는 null
  function findFolded(text, needle, backward) {
    const T = codepoints(text), N = codepoints(needle);
    const size = byteLength(text);
    if (!N.length) return backward ? { begin: size, end: size } : { begin: 0, end: 0 };
    const matchAt = (i) => {
      let j = 0;
      for (; j < N.length; ++j) {
        if (i + j >= T.length) return null;
        if (foldCp(N[j].cp) !== foldCp(T[i + j].cp)) return null;
      }
      const lastCp = T[i + j - 1];
      return lastCp.byte + lastCp.len;
    };
    let found = null;
    for (let i = 0; i < T.length; ++i) {
      const end = matchAt(i);
      if (end === null) continue;
      found = { begin: T[i].byte, end };
      if (!backward) break;
    }
    return found;
  }

  /* ---------- 두벌식 한글 조합기 (luil 밖 — 운영체제 IME 흉내) ---------- */
  const CHO = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
  const JUNG = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";
  const JONG = ["", "ㄱ", "ㄲ", "ㄳ", "ㄴ", "ㄵ", "ㄶ", "ㄷ", "ㄹ", "ㄺ", "ㄻ", "ㄼ", "ㄽ", "ㄾ", "ㄿ", "ㅀ", "ㅁ", "ㅂ", "ㅄ", "ㅅ", "ㅆ", "ㅇ", "ㅈ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ"];
  const VOWEL_PAIRS = { "ㅗㅏ": "ㅘ", "ㅗㅐ": "ㅙ", "ㅗㅣ": "ㅚ", "ㅜㅓ": "ㅝ", "ㅜㅔ": "ㅞ", "ㅜㅣ": "ㅟ", "ㅡㅣ": "ㅢ" };
  const FINAL_PAIRS = { "ㄱㅅ": "ㄳ", "ㄴㅈ": "ㄵ", "ㄴㅎ": "ㄶ", "ㄹㄱ": "ㄺ", "ㄹㅁ": "ㄻ", "ㄹㅂ": "ㄼ", "ㄹㅅ": "ㄽ", "ㄹㅌ": "ㄾ", "ㄹㅍ": "ㄿ", "ㄹㅎ": "ㅀ", "ㅂㅅ": "ㅄ" };
  const FINAL_SPLIT = Object.fromEntries(Object.entries(FINAL_PAIRS).map(([k, v]) => [v, [k[0], k[1]]]));
  const KEYMAP = { r: "ㄱ", R: "ㄲ", s: "ㄴ", e: "ㄷ", E: "ㄸ", f: "ㄹ", a: "ㅁ", q: "ㅂ", Q: "ㅃ", t: "ㅅ", T: "ㅆ", d: "ㅇ", w: "ㅈ", W: "ㅉ", c: "ㅊ", z: "ㅋ", x: "ㅌ", v: "ㅍ", g: "ㅎ",
    k: "ㅏ", o: "ㅐ", i: "ㅑ", O: "ㅒ", j: "ㅓ", p: "ㅔ", u: "ㅕ", P: "ㅖ", h: "ㅗ", y: "ㅛ", n: "ㅜ", b: "ㅠ", m: "ㅡ", l: "ㅣ" };
  const isVowel = (j) => JUNG.includes(j);
  function syllable(st) {
    if (!st) return "";
    if (st.cho && st.jung) return String.fromCodePoint(0xac00 + (CHO.indexOf(st.cho) * 21 + JUNG.indexOf(st.jung)) * 28 + JONG.indexOf(st.jong || ""));
    return (st.cho || "") + (st.jung || "");
  }
  // 조합 한 걸음: { commit: 확정 글, next: 새 조합 상태 } (상태는 { cho, jung, jong })
  function compose(st, jamo) {
    const s = st ? { ...st } : null;
    if (!isVowel(jamo)) {
      if (!s) return { commit: "", next: { cho: jamo } };
      if (s.cho && s.jung && !s.jong) {
        if (JONG.includes(jamo)) return { commit: "", next: { ...s, jong: jamo } };
        return { commit: syllable(s), next: { cho: jamo } };
      }
      if (s.cho && s.jung && s.jong) {
        const pair = FINAL_PAIRS[s.jong + jamo];
        if (pair) return { commit: "", next: { ...s, jong: pair } };
        return { commit: syllable(s), next: { cho: jamo } };
      }
      return { commit: syllable(s), next: { cho: jamo } };
    }
    if (!s) return { commit: "", next: { jung: jamo } };
    if (s.cho && !s.jung) return { commit: "", next: { ...s, jung: jamo } };
    if (s.jung && !s.jong) {
      const pair = VOWEL_PAIRS[s.jung + jamo];
      if (pair) return { commit: "", next: { ...s, jung: pair } };
      return { commit: syllable(s), next: { jung: jamo } };
    }
    // 받침 뒤 모음: 받침이 다음 글자의 초성으로 넘어간다 (겹받침은 뒤쪽만)
    const split = FINAL_SPLIT[s.jong];
    const keep = split ? split[0] : "";
    const move = split ? split[1] : s.jong;
    return { commit: syllable({ ...s, jong: keep }), next: { cho: move, jung: jamo } };
  }

  window.LuilText = {
    cpLen, bytesOf, hex2, U, codepoints, byteLength, indexOfByte, sliceBytes,
    decodeStrict, replaceInvalid,
    boundaries, prevBoundary, nextBoundary, backspaceRange, keepsWhole, wordLeft, wordRight, isWordByte,
    foldCp, foldString, findFolded,
    Hangul: { CHO, JUNG, JONG, KEYMAP, compose, syllable, isVowel },
  };
})();

/* luil 줄 나눔·접기의 브라우저 이식 — 8강 실험용
 *
 * src/text/line_break.cpp(UAX #14 규칙기)와 src/ui/line_fold.cpp·text_layout.cpp의 접기 고리를
 * 그대로 옮겼다. 분류 표는 linebreak-data.js(원본 헤더에서 생성)다. 모든 자리는 luil처럼 UTF-8
 * byte offset이다. 원본 C++과 무작위 글로 대조한 Node 이식본을 다시 대조해 만들었다.
 * 글자소 경계만 브라우저의 Intl.Segmenter를 쓴다 (luil 내장 표는 Unicode 16, 브라우저는 판이 다를 수 있다).
 */
(function () {
  "use strict";
  const D = window.LUIL_LINE_BREAK_DATA;
  const CLS = D.classes;

  function findRange(flat, stride, cp) {
    let lo = 0;
    let hi = flat.length / stride;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (flat[mid * stride + 1] < cp) lo = mid + 1;
      else hi = mid;
    }
    return lo < flat.length / stride && flat[lo * stride] <= cp ? lo : -1;
  }

  function classOf(cp) {
    if (cp < 0x80) return CLS[D.ascii[cp]];
    if (cp >= 0xac00 && cp <= 0xd7a3) return (cp - 0xac00) % 28 === 0 ? "h2" : "h3";
    const i = findRange(D.ranges, 3, cp);
    return i >= 0 ? CLS[D.ranges[i * 3 + 2]] : "al";
  }
  const inTable = (name, cp) => findRange(D[name], 2, cp) >= 0;
  const cpLen = (cp) => (cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4);
  const byteLength = (s) => { let n = 0; for (const ch of s) n += cpLen(ch.codePointAt(0)); return n; };

  const isHangul = (v) => v === "h2" || v === "h3" || v === "jl" || v === "jv" || v === "jt";
  const keepsMarksApart = (v) => v === "bk" || v === "cr" || v === "lf" || v === "nl" || v === "sp" || v === "zw";
  const any = (v, ...vs) => vs.includes(v);
  const DOTTED_CIRCLE = 0x25cc;

  function units(text, policy) {
    const out = [];
    let offset = 0;
    for (const ch of text) {
      const cp = ch.codePointAt(0);
      let value = classOf(cp);
      if (policy === "hangul_words" && isHangul(value)) value = "al";
      const mark = value === "cm" || value === "zwj";
      // LB9: X (CM | ZWJ)*는 X다. LB10: 홀로 선 부호는 AL이다.
      if (mark && out.length > 0 && !keepsMarksApart(out[out.length - 1].value)) out[out.length - 1].endsWithZwj = value === "zwj";
      else out.push({ offset, cp, value: mark ? "al" : value, endsWithZwj: value === "zwj" });
      offset += cpLen(cp);
    }
    return out;
  }

  // 규칙 이름을 함께 돌려준다 — 실험이 "왜 여기서 바꾸나"를 보여 준다.
  function decide(u, i, ctx) {
    const left = u[i - 1], right = u[i];
    const a = left.value, b = right.value;
    const hasNext = i + 1 < u.length;
    const next = hasNext ? u[i + 1].value : "al";
    const anchor = u[ctx.beforeSpaces].value;
    const ea = (cp) => inTable("east_asian", cp);
    if (a === "bk") return ["mandatory", "LB4"];
    if (a === "cr" && b === "lf") return ["no", "LB5"];
    if (any(a, "cr", "lf", "nl")) return ["mandatory", "LB5"];
    if (any(b, "bk", "cr", "lf", "nl", "sp", "zw")) return ["no", "LB6·7"];
    if (anchor === "zw") return ["yes", "LB8"];
    if (left.endsWithZwj) return ["no", "LB8a"];
    if (a === "wj" || b === "wj" || a === "gl") return ["no", "LB11·12"];
    if (b === "gl" && !any(a, "sp", "ba", "hy")) return ["no", "LB12a"];
    if (any(b, "cl", "cp", "ex", "sy")) return ["no", "LB13"];
    if (anchor === "op") return ["no", "LB14"];
    if (anchor === "qu" && inTable("initial_quote", u[ctx.beforeSpaces].cp) && (ctx.beforeSpaces === 0 || any(u[ctx.beforeSpaces - 1].value, "bk", "cr", "lf", "nl", "op", "qu", "gl", "sp", "zw"))) return ["no", "LB15a"];
    if (b === "qu" && inTable("final_quote", right.cp) && (!hasNext || any(next, "sp", "gl", "wj", "cl", "qu", "cp", "ex", "is", "sy", "bk", "cr", "lf", "nl", "zw"))) return ["no", "LB15b"];
    if (a === "sp" && b === "is" && next === "nu" && hasNext) return ["yes", "LB15c"];
    if (b === "is") return ["no", "LB15d"];
    if (any(anchor, "cl", "cp") && b === "ns") return ["no", "LB16"];
    if (anchor === "b2" && b === "b2") return ["no", "LB17"];
    if (a === "sp") return ["yes", "LB18"];
    if (b === "qu" && !inTable("initial_quote", right.cp)) return ["no", "LB19"];
    if (a === "qu" && !inTable("final_quote", left.cp)) return ["no", "LB19"];
    if (b === "qu") {
      if (!ea(left.cp)) return ["no", "LB19a"];
      if (!hasNext || !ea(u[i + 1].cp)) return ["no", "LB19a"];
    }
    if (a === "qu") {
      if (!ea(right.cp)) return ["no", "LB19a"];
      if (i < 2 || !ea(u[i - 2].cp)) return ["no", "LB19a"];
    }
    if (a === "cb" || b === "cb") return ["yes", "LB20"];
    if ((a === "hy" || left.cp === 0x2010) && b === "al" && (i < 2 || any(u[i - 2].value, "bk", "cr", "lf", "nl", "sp", "zw", "cb", "gl"))) return ["no", "LB20a"];
    if (any(b, "ba", "hy", "ns") || a === "bb") return ["no", "LB21"];
    if (i >= 2 && u[i - 2].value === "hl" && (a === "hy" || (a === "ba" && !ea(left.cp))) && b !== "hl") return ["no", "LB21a"];
    if ((a === "sy" && b === "hl") || b === "in") return ["no", "LB21b·22"];
    if ((any(a, "al", "hl") && b === "nu") || (a === "nu" && any(b, "al", "hl"))) return ["no", "LB23"];
    if ((a === "pr" && any(b, "id", "eb", "em")) || (any(a, "id", "eb", "em") && b === "po")) return ["no", "LB23a"];
    if ((any(a, "pr", "po") && any(b, "al", "hl")) || (any(a, "al", "hl") && any(b, "pr", "po"))) return ["no", "LB24"];
    if (any(b, "po", "pr") && (ctx.numeric || ctx.numericClose)) return ["no", "LB25"];
    if (any(a, "po", "pr")) {
      if (b === "nu") return ["no", "LB25"];
      if (b === "op" && hasNext && (next === "nu" || (next === "is" && i + 2 < u.length && u[i + 2].value === "nu"))) return ["no", "LB25"];
    }
    if (any(a, "hy", "is") && b === "nu") return ["no", "LB25"];
    if (b === "nu" && ctx.numeric) return ["no", "LB25"];
    if ((a === "jl" && any(b, "jl", "jv", "h2", "h3")) || (any(a, "jv", "h2") && any(b, "jv", "jt")) || (any(a, "jt", "h3") && b === "jt")) return ["no", "LB26"];
    if ((any(a, "jl", "jv", "jt", "h2", "h3") && b === "po") || (a === "pr" && any(b, "jl", "jv", "jt", "h2", "h3"))) return ["no", "LB27"];
    if (any(a, "al", "hl") && any(b, "al", "hl")) return ["no", "LB28"];
    const base = (x) => x.value === "ak" || x.value === "as" || x.cp === DOTTED_CIRCLE;
    if (a === "ap" && (b === "ak" || b === "as" || right.cp === DOTTED_CIRCLE)) return ["no", "LB28a"];
    if (base(left) && any(b, "vf", "vi")) return ["no", "LB28a"];
    if (i >= 2 && base(u[i - 2]) && a === "vi" && (b === "ak" || right.cp === DOTTED_CIRCLE)) return ["no", "LB28a"];
    if (base(left) && base(right) && next === "vf" && hasNext) return ["no", "LB28a"];
    if (a === "is" && any(b, "al", "hl")) return ["no", "LB29"];
    if (any(a, "al", "hl", "nu") && b === "op" && !ea(right.cp)) return ["no", "LB30"];
    if (a === "cp" && !ea(left.cp) && any(b, "al", "hl", "nu")) return ["no", "LB30"];
    if (a === "ri" && b === "ri" && ctx.regional % 2 === 1) return ["no", "LB30a"];
    if (b === "em" && (a === "eb" || inTable("unassigned_pictographic", left.cp))) return ["no", "LB30b"];
    return ["yes", "LB31"];
  }

  // 모든 코드포인트 경계의 판정 (offset, 결과, 규칙). 글 끝은 늘 반드시 바꾸는 자리다.
  function decisions(text, policy) {
    const out = [];
    if (!text.length) return out;
    const u = units(text, policy || "hangul_words");
    const ctx = { beforeSpaces: 0, numeric: false, numericClose: false, regional: 0 };
    const advance = (i) => {
      const v = u[i].value;
      if (v !== "sp") ctx.beforeSpaces = i;
      const numeric = ctx.numeric;
      ctx.numeric = v === "nu" || ((v === "sy" || v === "is") && numeric);
      ctx.numericClose = (v === "cl" || v === "cp") && numeric;
      ctx.regional = v === "ri" ? ctx.regional + 1 : 0;
    };
    advance(0);
    for (let i = 1; i < u.length; ++i) {
      const [action, rule] = decide(u, i, ctx);
      out.push({ offset: u[i].offset, action, rule, left: u[i - 1].value, right: u[i].value, lcp: u[i - 1].cp, rcp: u[i].cp });
      advance(i);
    }
    out.push({ offset: byteLength(text), action: "mandatory", rule: "LB3", left: u[u.length - 1].value, right: "eot" });
    return out;
  }

  function opportunities(text, policy) {
    return decisions(text, policy).filter((d) => d.action !== "no").map((d) => ({ offset: d.offset, mandatory: d.action === "mandatory" }));
  }

  /* ---- 글자소와 접기 ---- */
  const graphemeSeg = typeof Intl !== "undefined" && Intl.Segmenter ? new Intl.Segmenter("ko", { granularity: "grapheme" }) : null;

  // [{ byte, index }] — 0과 끝을 포함한 글자소 경계 (UTF-8 byte와 JS 문자열 index를 함께)
  function graphemes(text) {
    const out = [{ byte: 0, index: 0 }];
    let byte = 0;
    if (graphemeSeg) {
      for (const { segment, index } of graphemeSeg.segment(text)) {
        byte += byteLength(segment);
        out.push({ byte, index: index + segment.length });
      }
    } else {
      let index = 0;
      for (const ch of text) { byte += cpLen(ch.codePointAt(0)); index += ch.length; out.push({ byte, index }); }
    }
    return out;
  }

  // text_layout.cpp의 접기 고리 + line_fold.cpp의 choose_line_fold. measure(prefix)는 문단 처음부터의 폭.
  function wrap(content, width, measure, policy) {
    const G = graphemes(content);
    const B = G.map((g) => g.byte);
    const size = B[B.length - 1];
    const stops = G.map((g) => measure(content.slice(0, g.index)));
    const indexOfByte = new Map(G.map((g) => [g.byte, g.index]));
    if (!content.length || stops[stops.length - 1] <= width) return [{ begin: 0, cut: size, visible: size, emergency: false }];
    const opp = opportunities(content, policy).map((o) => o.offset);
    const oppSet = new Set(opp);
    const bSet = new Set(B);
    const nextBoundary = (at) => { for (const b of B) if (b > at) return b; return size; };
    const prevBoundary = (at) => { let p = 0; for (const b of B) { if (b < at) p = b; else break; } return p; };
    const hangsAt = (at) => {
      if (at >= size) return false;
      const end = nextBoundary(at);
      const idx = indexOfByte.get(at);
      const cp = content.codePointAt(idx);
      return cpLen(cp) === end - at && inTable("hanging", cp);
    };
    const withVisible = (start, cut, emergency) => {
      let visible = cut;
      while (cut < size && visible > start && hangsAt(prevBoundary(visible))) visible = prevBoundary(visible);
      return { cut, visible, emergency };
    };
    const lastOpportunity = (start, limit) => {
      for (let i = opp.length - 1; i >= 0; --i) {
        const o = opp[i];
        if (o > limit) continue;
        if (o <= start) break;
        if (bSet.has(o)) return o;
      }
      return 0;
    };
    const choose = (start, fit) => {
      let cut = fit;
      while (hangsAt(cut)) cut = nextBoundary(cut);
      if (cut >= size || oppSet.has(cut)) return withVisible(start, cut, false);
      const found = lastOpportunity(start, fit);
      if (found !== 0) return withVisible(start, found, false);
      return withVisible(start, fit, true);
    };
    const lines = [];
    let first = 0;
    while (first + 1 < B.length) {
      let fit = first + 1;
      while (fit + 1 < B.length && stops[fit + 1] - stops[first] <= width) ++fit;
      const start = B[first];
      const fold = choose(start, B[fit]);
      lines.push(Object.assign({ begin: start }, fold));
      first = B.findIndex((b) => b >= fold.cut);
      if (first < 0) break;
    }
    return lines;
  }

  // byte 구간을 JS 문자열로
  function sliceBytes(text, from, to) {
    let byte = 0, i = 0, start = -1, end = text.length;
    for (const ch of text) {
      if (byte === from && start < 0) start = i;
      if (byte === to) { end = i; break; }
      byte += cpLen(ch.codePointAt(0));
      i += ch.length;
    }
    if (start < 0) start = byte === from ? i : text.length;
    return text.slice(start, end);
  }

  window.LuilLineBreak = { classOf, units, decisions, opportunities, graphemes, wrap, sliceBytes, byteLength, cpLen, unicode: D.unicode };
})();

/* luil 설명서 — 글 상자(label·draw_text_block)의 줄 바꿈·줄 수·말줄임·정렬 규칙을 JS로 옮긴 것
 *
 * 옮긴 원본 (이름 옆 주석이 C++ 이름이다):
 *   src/ui/text_layout.cpp      build_text_layout의 문단 고리와 make_text_layout(측정기) — 접기 자체는
 *                               linebreak.js(line_break.cpp·line_fold.cpp의 이식)의 LuilLineBreak.wrap을 쓴다
 *   src/ui/line_fold.h          after_line_break
 *   src/ui/text_block.cpp       wrap_lines, single_line, elided, draw_lines, measure_text_block, draw_text_block,
 *                               text_block_line_height
 *   src/ui/draw_primitives.cpp  character_boundaries, single_line_text(space), elide_text·_middle·_start
 *   src/ui/label_element.cpp    block_style, measure_wrap, arrange, draw(글 부분), height_for
 *   src/ui/font_scale.cpp       scaled_font_size·text_line_height (시스템 글자 표가 없을 때)
 *
 * 글꼴만은 옮길 수 없어 단순 측정기로 대신한다: 글자소마다 ASCII 한 byte면 0.6em, 그 밖은 1em
 * (text.html 실습과 같은 측정기). 글자소는 브라우저의 Intl.Segmenter다. baseline은 글꼴 metrics라 정하지
 * 않는다 — 그린 줄은 줄 상자의 가운데(y)로 돌려준다.
 * 모든 구간은 luil처럼 UTF-8 byte offset이다. 원본 C++을 그대로 컴파일한 대조기와 무작위 입력으로 견주어 맞췄다.
 */
(function () {
  "use strict";
  const LB = window.LuilLineBreak;
  const ELLIPSIS = "…";

  // ---- 글자와 byte ----
  // byte offset(코드포인트 시작과 끝) → JS 문자열 index
  function byteMap(text) {
    const map = new Map([[0, 0]]);
    let byte = 0;
    let index = 0;
    for (const ch of text) {
      byte += LB.cpLen(ch.codePointAt(0));
      index += ch.length;
      map.set(byte, index);
    }
    return map;
  }
  const sliceBytes = (text, map, from, to) => text.slice(map.get(from), map.get(to));

  // single_line_text(text, single_line_break::space): CR, LF, CRLF 짝 하나가 공백 하나다
  const singleLineText = (text) => text.replace(/\r\n|\r|\n/g, " ");

  // 단순 측정기 (글꼴 대신): 한 byte 글자소 0.6em, 그 밖 1em. 크기 10이면 6과 10이다.
  function modelWidth(text, size) {
    const G = LB.graphemes(text);
    let width = 0;
    for (let i = 0; i + 1 < G.length; ++i) width += (G[i + 1].byte - G[i].byte === 1 ? 6 : 10) * (size / 10);
    return width;
  }

  // measure_text: 그리기와 같이 줄바꿈을 공백으로 바꿔 잰다
  const measureText = (text, size) => (text.length ? modelWidth(singleLineText(text), size) : 0);

  // character_boundaries: 0과 끝을 포함한 글자소 경계 (byte·index)
  const characterBoundaries = (text) => LB.graphemes(text);

  // ---- 말줄임 (draw_primitives.cpp) ----
  // elide_text: 들어가는 가장 긴 앞부분 + "…"
  function elideText(text, maxWidth, size, shaping) {
    if (!text.length || maxWidth <= 0) return "";
    if (measureText(text, size) <= maxWidth) return text;
    const ellipsisWidth = measureText(ELLIPSIS, size);
    if (ellipsisWidth > maxWidth) return "";
    const B = characterBoundaries(text);
    let low = 0;
    let high = B.length - 1;
    while (low < high) {
      const middle = low + Math.floor((high - low + 1) / 2);
      const prefix = text.slice(0, B[middle].index);
      const width = shaping ? measureText(prefix + ELLIPSIS, size) : measureText(prefix, size) + ellipsisWidth;
      if (width <= maxWidth) low = middle;
      else high = middle - 1;
    }
    return text.slice(0, B[low].index) + ELLIPSIS;
  }

  // elide_text_middle: 앞 (keep+1)/2, 뒤 keep/2 글자소를 남긴다
  function elideTextMiddle(text, maxWidth, size) {
    if (!text.length || maxWidth <= 0) return "";
    if (measureText(text, size) <= maxWidth) return text;
    if (measureText(ELLIPSIS, size) > maxWidth) return "";
    const B = characterBoundaries(text);
    const count = B.length - 1;
    const candidate = (keep) => {
      const front = Math.floor((keep + 1) / 2);
      const back = Math.floor(keep / 2);
      return text.slice(0, B[front].index) + ELLIPSIS + text.slice(B[count - back].index);
    };
    let low = 0;
    let high = count;
    while (low < high) {
      const middle = low + Math.floor((high - low + 1) / 2);
      if (measureText(candidate(middle), size) <= maxWidth) low = middle;
      else high = middle - 1;
    }
    return candidate(low);
  }

  // elide_text_start: "…" + 들어가는 가장 긴 뒷부분
  function elideTextStart(text, maxWidth, size) {
    if (!text.length || maxWidth <= 0) return "";
    if (measureText(text, size) <= maxWidth) return text;
    if (measureText(ELLIPSIS, size) > maxWidth) return "";
    const B = characterBoundaries(text);
    const candidate = (from) => ELLIPSIS + text.slice(B[from].index);
    let low = 0;
    let high = B.length - 1;
    while (high - low > 1) {
      const middle = low + Math.floor((high - low) / 2);
      if (measureText(candidate(middle), size) <= maxWidth) high = middle;
      else low = middle;
    }
    return candidate(high);
  }

  // ---- 배치 (text_layout.cpp: build_text_layout + make_text_layout(측정기)) ----
  // 줄마다 { begin, end, width } — byte offset, 폭은 매단 공백을 뺀 것
  function textLayoutLines(text, size, width, wrap, policy) {
    if (!wrap || !Number.isFinite(width) || width < 0) width = 0; // normalize_text_layout_request
    const measure = (s) => measureText(s, size); // 문맥의 측정기
    const lines = [];
    let index = 0;
    let byte = 0;
    for (;;) {
      let newline = -1;
      for (let i = index; i < text.length; ++i) {
        const c = text.charCodeAt(i);
        if (c === 13 || c === 10) { newline = i; break; }
      }
      const content = text.slice(index, newline < 0 ? text.length : newline);
      const contentBytes = LB.byteLength(content);
      const full = measure(content);
      if (!wrap || width <= 0 || full <= width || !content.length) {
        lines.push({ begin: byte, end: byte + contentBytes, width: full });
      } else {
        // 측정기가 더하는 폭이라 줄 경계 문맥으로 다시 물러서는 고리(previous_line_fold)는 서지 않는다
        for (const fold of LB.wrap(content, width, measure, policy)) {
          lines.push({ begin: byte + fold.begin, end: byte + fold.cut, width: measure(LB.sliceBytes(content, fold.begin, fold.visible)) });
        }
      }
      if (newline < 0) break;
      // after_line_break: CRLF 짝은 한 번이다
      const step = text.charCodeAt(newline) === 13 && newline + 1 < text.length && text.charCodeAt(newline + 1) === 10 ? 2 : 1;
      index = newline + step;
      byte += contentBytes + step;
    }
    return lines;
  }

  // ---- 글 상자 (text_block.cpp) ----
  const LINE_PADDING = 7; // line_padding
  // text_block_line_height: 0이면 label 한 줄 = text_line_height(size, 7) (시스템 글자 표 없음: size + 7)
  const textBlockLineHeight = (style) => (style.line_height > 0 ? style.line_height : style.font.size + LINE_PADDING);

  // wrap_lines
  function wrapLines(text, style, policy, width, pixelSize) {
    const result = { lines: [], truncated: false, width: 0, height: 0 };
    const lines = textLayoutLines(text, pixelSize, width, true, policy);
    const shown = style.max_lines > 0 ? Math.min(style.max_lines, lines.length) : lines.length;
    for (let i = 0; i < shown; ++i) {
      result.lines.push({ begin: lines[i].begin, end: lines[i].end });
      result.width = Math.max(result.width, lines[i].width);
    }
    if (shown < lines.length && shown > 0) {
      // 마지막 보이는 줄이 남은 글 전체를 맡고 그리기가 끝을 줄인다
      result.lines[result.lines.length - 1].end = LB.byteLength(text);
      result.truncated = true;
      result.width = width;
    }
    return result;
  }

  // single_line
  function singleLine(text, width, pixelSize) {
    const natural = measureText(text, pixelSize);
    return { lines: [{ begin: 0, end: LB.byteLength(text) }], truncated: natural > width, width: natural > width ? width : natural, height: 0 };
  }

  // elided
  function elided(text, elide, width, size, shaping) {
    if (elide === "middle") return elideTextMiddle(text, width, size);
    if (elide === "start") return elideTextStart(text, width, size);
    return elideText(text, width, size, shaping);
  }

  // measure_text_block (배율 1). ctx.policy는 앱 스타일의 text.wrap_policy다 (line_break_for)
  function measureTextBlock(text, style, availableWidth, ctx) {
    if (!text.length) return { lines: [], truncated: false, width: 0, height: 0 };
    const width = Math.max(1, availableWidth);
    const lineHeight = textBlockLineHeight(style);
    const policy = style.line_break || (ctx && ctx.policy) || "hangul_words";
    const result = style.wrap ? wrapLines(text, style, policy, width, style.font.size) : singleLine(text, width, style.font.size);
    if (result.truncated) result.width = width;
    result.height = result.lines.length * lineHeight;
    return result;
  }

  // draw_lines: 줄마다 그린 글과 x, 줄 상자의 가운데 y. 빈 글은 그리지 않는다 (draw_text)
  function drawLines(text, box, style, layout, ctx) {
    const out = { drawn: [], rect: { x: box.x, y: box.y, width: 0, height: 0 } };
    if (!text.length || !layout.lines.length) return out;
    const shaping = !!(ctx && ctx.shaping);
    const size = style.font.size;
    const map = byteMap(text);
    const total = LB.byteLength(text);
    const lineHeight = textBlockLineHeight(style);
    const height = layout.lines.length * lineHeight;
    const centeredSingle = layout.lines.length === 1 && style.vertical === "center";
    let top = box.y;
    if (style.vertical === "center") top = box.y + (box.height - height) * 0.5;
    else if (style.vertical === "bottom") top = box.y + box.height - height;
    let left = box.x + box.width;
    let right = box.x;
    layout.lines.forEach((line, index) => {
      const begin = Math.min(line.begin, total);
      const end = Math.min(Math.max(line.end, begin), total);
      let piece = sliceBytes(text, map, begin, end);
      if (style.wrap) {
        // 말줄임한 마지막 줄은 남은 글을 한 줄로 펴서(LF는 공백) 끝을 줄인다
        const lastTruncated = layout.truncated && index + 1 === layout.lines.length;
        if (lastTruncated) piece = piece.replace(/\n/g, " ");
        while (piece.length && (piece.endsWith("\n") || piece.endsWith("\r"))) piece = piece.slice(0, -1);
        if (lastTruncated) piece = elideText(piece, box.width, size, shaping);
      } else {
        piece = elided(piece, style.elide, box.width, size, shaping);
      }
      const width = measureText(piece, size);
      let offset = 0;
      if (style.align === "end") offset = box.width - width;
      else if (style.align === "center") offset = (box.width - width) * 0.5;
      const y = centeredSingle ? box.y + box.height * 0.5 : top + index * lineHeight + lineHeight * 0.5;
      if (piece.length) out.drawn.push({ text: piece, x: box.x + offset, y, line: index, width, top: centeredSingle ? y - lineHeight * 0.5 : top + index * lineHeight });
      if (width > 0) {
        left = Math.min(left, box.x + offset);
        right = Math.max(right, box.x + offset + width);
      }
    });
    out.rect = right < left ? { x: box.x, y: top, width: 0, height } : { x: left, y: top, width: right - left, height };
    return out;
  }

  // draw_text_block (배치에서 잰 줄이 있으면 그것으로, 없으면 그리기에서 접는다)
  function drawTextBlock(text, box, style, layout, ctx) {
    if (!text.length) return { drawn: [], rect: { x: box.x, y: box.y, width: 0, height: 0 } };
    if (!layout) {
      layout = style.wrap
        ? wrapLines(text, style, style.line_break || (ctx && ctx.drawPolicy) || "hangul_words", Math.max(1, box.width), style.font.size)
        : { lines: [{ begin: 0, end: LB.byteLength(text) }], truncated: false };
    }
    return drawLines(text, box, style, layout, ctx);
  }

  // ---- label_element (label_element.cpp) ----
  // block_style: 바꾼 글은 위에서부터, 한 줄은 세로 가운데
  const blockStyle = (config) => ({
    font: { size: config.font_size },
    align: config.align,
    vertical: config.wrap ? "top" : "center",
    elide: config.elide,
    wrap: config.wrap,
    line_break: config.line_break || null,
    max_lines: config.max_lines,
    line_height: 0,
  });
  const measureWrap = (config, ctx, availableWidth) => measureTextBlock(config.text, blockStyle(config), availableWidth - config.padding * 2, ctx);
  const heightFor = (config) => textBlockLineHeight(blockStyle(config));

  // arrange + draw: 칸(slot)에 놓인 label이 그리는 줄
  function simulateLabel(config, slot, ctx) {
    const wrapped = config.wrap ? measureWrap(config, ctx, slot.width) : null;
    const textBox = { x: slot.x + config.padding, y: slot.y, width: slot.width - config.padding * 2, height: slot.height };
    const draw = config.text.length ? drawTextBlock(config.text, textBox, blockStyle(config), wrapped, ctx) : { drawn: [], rect: null };
    return { wrapped, measured: measureWrap(config, ctx, slot.width), textBox, drawn: draw.drawn, rect: draw.rect, heightFor: heightFor(config), lineHeight: heightFor(config) };
  }

  window.LuilTextBlock = {
    ELLIPSIS,
    singleLineText,
    modelWidth,
    measureText,
    elideText,
    elideTextMiddle,
    elideTextStart,
    textLayoutLines,
    textBlockLineHeight,
    measureTextBlock,
    drawTextBlock,
    blockStyle,
    measureWrap,
    heightFor,
    simulateLabel,
  };
})();

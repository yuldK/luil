/* luil 설명서 — 표(table_element)의 가로 판정을 브라우저로 옮긴 것이다.
 *
 * 원본(커밋 563a855):
 *   - 순수 함수: src/ui/table_element.cpp 723-784 (table_column_spans … table_reveal_delta)
 *   - 배치 판정: src/ui/table_element.cpp 1014-1053, 1077 (두 막대·가로 창·scroll_x 다듬기·고정 열)
 *   - 가로 휠 원천: src/ui/table_element.cpp 1068-1071 (범위 안으로 깎은 변화량)
 *   - ←·→ 한 걸음: src/ui/table_element.cpp 867-872
 *   - 경계 손잡이 끌기: src/ui/table_element.cpp 451-455
 *   - 막대 thumb·끌기·누름: src/ui/scrollbar_element.cpp 29-43, 95-112, 135-147
 * C++은 float로 셈하므로 연산마다 Math.fround로 float에 맞춘다. 원본을 컴파일한 대조기와 무작위 입력으로 견주었다.
 */
(function () {
  "use strict";
  const f = Math.fround;
  const HEADER = 28; // table_header_height
  const BAR = 16; // scroll_area_bar_width = scrollbar_visual_width(8) + 8
  const KEY_STEP = 48; // table_key_scroll_step
  const MIN_THUMB = 24; // scrollbar_minimum_thumb
  // std::min/std::max/std::clamp의 비교 순서 그대로다 (같은 값이면 앞 인자).
  const min = (a, b) => (b < a ? b : a);
  const max = (a, b) => (a < b ? b : a);
  const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
  const positive = (w) => (w > 0 ? w : 0);

  function spans(columns) {
    const out = [];
    let begin = 0;
    for (const c of columns) {
      const width = positive(f(c.width));
      out.push({ begin, width });
      begin = f(begin + width);
    }
    return out;
  }

  function contentWidth(columns) {
    let total = 0;
    for (const c of columns) total = f(total + positive(f(c.width)));
    return total;
  }

  function resizeDelta(column, delta) {
    const room = min(0, f(f(column.min_width) - f(column.width)));
    return max(f(delta), room);
  }

  function clampScroll(x, content, viewport) {
    const maximum = max(0, f(f(content) - f(viewport)));
    return clamp(f(x), 0, maximum);
  }

  function frozenWidth(columns, count) {
    let total = 0;
    const end = Math.min(count, columns.length);
    for (let i = 0; i < end; i++) total = f(total + positive(f(columns[i].width)));
    return total;
  }

  function frozenFits(frozen, viewport) {
    return f(f(viewport) - f(frozen)) >= KEY_STEP;
  }

  function revealDelta(span, x, frozen, viewport) {
    const left = f(span.begin);
    const right = f(left + f(span.width));
    const windowLeft = f(f(frozen) + f(x));
    if (left < windowLeft) return f(left - windowLeft);
    if (right > f(f(x) + f(viewport))) return f(min(f(left - f(frozen)), f(right - f(viewport))) - f(x));
    return 0;
  }

  /* table_element::arrange의 앞부분이다. 본문을 배치하기 전에 두 막대·가로 창·고정을 정한다.
   * t = { width, height, scale, columns, scrollX, frozen(설정 값), verticalBar('automatic'|'always'|'never'),
   *       bodyContent(본문 내용 높이), hasHorizontalBar(가로 막대 객체가 있나), horizontalMode('automatic'|'always') } */
  function arrange(t) {
    const width = f(t.width);
    const height = f(t.height);
    const scale = t.scale > 0 ? f(t.scale) : 1;
    const headerHeight = min(f(HEADER * scale), height);
    const rowsHeight = f(height - headerHeight);
    const verticalWidth = min(f(BAR * scale), width);
    const horizontalFull = min(f(BAR * scale), rowsHeight);
    const content = contentWidth(t.columns);
    const verticalOver = (h) => t.verticalBar === "always" || (t.verticalBar === "automatic" && f(t.bodyContent) > f(h / scale));
    const horizontalOver = (w) => t.hasHorizontalBar && (t.horizontalMode === "always" || content > f(w / scale));
    let vertical = verticalOver(rowsHeight);
    const horizontal = horizontalOver(vertical ? f(width - verticalWidth) : width);
    if (horizontal && vertical === false) vertical = verticalOver(f(rowsHeight - horizontalFull));
    const horizontalHeight = horizontal ? horizontalFull : 0;
    const viewWidth = vertical ? f(width - verticalWidth) : width;
    const viewportWidth = f(viewWidth / scale);
    const scrollX = clampScroll(t.scrollX, content, viewportWidth);
    const configured = Math.min(t.frozen, t.columns.length);
    const frozenW = frozenWidth(t.columns, configured);
    const fits = configured > 0 && frozenFits(frozenW, viewportWidth);
    return {
      scale,
      headerHeight,
      rowsHeight,
      vertical,
      horizontal,
      verticalWidth,
      horizontalHeight,
      viewWidth,
      viewportWidth,
      contentWidth: content,
      scrollX,
      configuredFrozen: configured,
      frozenColumns: fits ? configured : 0,
      frozenWidth: fits ? frozenW : 0,
      bodyHeight: f(rowsHeight - horizontalHeight),
    };
  }

  // 가로 휠·터치 가로 쓸기·끌기 자동 스크롤이 부르는 원천: 범위 안으로 깎은 변화량. 끝이면 0을 그대로 낸다.
  function wheelDelta(current, delta, content, viewport) {
    return f(clampScroll(f(f(current) + f(delta)), content, viewport) - f(current));
  }

  // ←·→ (셀 탐색 없음): 깎은 변화량. 0이면 메시지 없이 키만 삼킨다(null).
  function keyStep(forward, current, content, viewport) {
    const step = forward ? KEY_STEP : -KEY_STEP;
    const delta = f(clampScroll(f(f(current) + step), content, viewport) - f(current));
    return delta === 0 ? null : delta;
  }

  // 경계 손잡이 끌기. frozenTable은 고정 열의 손잡이에만 있다: { frozenColumns, viewportWidth, frozenWidth }. 0이면 메시지 없음(null).
  function gripDelta(column, previousX, currentX, scale, frozenTable) {
    let delta = resizeDelta(column, f(f(f(currentX) - f(previousX)) / f(scale)));
    if (frozenTable && frozenTable.frozenColumns > 0)
      delta = min(delta, max(0, f(f(f(frozenTable.viewportWidth) - KEY_STEP) - f(frozenTable.frozenWidth))));
    return delta === 0 ? null : delta;
  }

  /* 막대 thumb (scrollbar_element::arrange). trackStart·trackLength는 물리 픽셀, 나머지는 논리 픽셀이다. */
  function thumb(trackStart, trackLength, scale, content, viewport, offset) {
    const s = scale > 0 ? f(scale) : 1;
    const top = f(trackStart);
    const track = f(trackLength);
    const scrollable = f(f(f(content) - f(viewport)) * s);
    if (scrollable <= 0 || track <= 0) return { top, length: track, scale: s, track, trackStart: top, content: f(content), viewport: f(viewport) };
    let length = f(track * f(f(viewport) / f(content)));
    const minimum = f(MIN_THUMB * s);
    if (length < minimum) length = minimum > track ? track : minimum;
    const ratio = f(f(f(offset) * s) / scrollable);
    const start = f(top + f(f(track - length) * (ratio < 0 ? 0 : ratio > 1 ? 1 : ratio)));
    return { top: start, length, scale: s, track, trackStart: top, content: f(content), viewport: f(viewport) };
  }

  function draggable(bar) {
    return f(bar.content - bar.viewport) > 0 && f(bar.track - bar.length) > 0;
  }

  // 막대의 물리 픽셀 이동을 논리 변화량으로 바꾼다. **범위를 보지 않는다.**
  function barDeltaFor(bar, pixels) {
    if (draggable(bar) === false) return 0;
    const scrollable = f(f(bar.content - bar.viewport) * bar.scale);
    return f(f(f(pixels) * f(scrollable / f(bar.track - bar.length))) / bar.scale);
  }

  // 트랙 누름: thumb 밖이면 thumb 가운데를 누른 자리로 한 번. 안이면 잡기만(null).
  function barPress(bar, position) {
    if (draggable(bar) === false || (position >= bar.top && position <= f(bar.top + bar.length))) return null;
    return barDeltaFor(bar, f(f(position) - f(bar.top + f(bar.length * 0.5))));
  }

  function barDrag(bar, previous, current) {
    if (draggable(bar) === false) return null;
    const delta = barDeltaFor(bar, f(f(current) - f(previous)));
    return delta === 0 ? null : delta;
  }

  window.LuilTable = { HEADER, BAR, KEY_STEP, spans, contentWidth, resizeDelta, clampScroll, frozenWidth, frozenFits, revealDelta, arrange, wheelDelta, keyStep, gripDelta, thumb, draggable, barDeltaFor, barPress, barDrag };
})();

/* luil 설명서 — 배치 계산을 JS로 옮긴 것 (배치 요소 문서의 실습이 부른다)
 *
 * 원본을 함수 단위로 옮겼다. 이름 옆 주석은 C++ 원본의 이름이다.
 *  - src/ui/flexible_lengths.h        distribute_flexible
 *  - src/ui/grid_element.cpp          resolve_tracks (익명 namespace), 줄 시작점(arrange의 starts), sum_with_spacing
 *  - include/luil/ui/wrap_element.h   wrap_columns_for, wrap_height_for
 *  - src/ui/wrap_element.cpp          arrange가 칸을 놓는 식
 *  - include/luil/ui/layout_metrics.h classify_window_width
 *  - src/host/app_delegate.cpp        make_window_metrics의 배율 다듬기
 * C++은 float(32비트)로 센다. 같은 답을 내려고 연산마다 Math.fround로 float에 맞춘다.
 * 원본 C++을 컴파일한 대조기로 무작위 입력을 견주어 같음을 확인했다 (containers.html의 실습 설명).
 *
 *   flexible_share { weight, minimum, maximum }              (물리든 논리든 같은 단위)
 *   grid_track     { kind: "fixed"|"fit"|"flexible", length, weight, minimum }   (논리 픽셀)
 *   wrap_config    { item_width, item_height, spacing, line_spacing }          (논리 픽셀)
 *   window_width_breakpoints { medium, expanded }                              (논리 픽셀)
 */
(function (root) {
  "use strict";
  const f = Math.fround;

  // distribute_flexible(items, pool) — 하한·상한을 지키는 비율 분배 (flexible_lengths.h)
  function distributeFlexible(items, pool) {
    const n = items.length;
    const lengths = new Array(n).fill(0);
    const frozen = new Array(n).fill(false);
    pool = f(pool);
    // 한 걸음에 적어도 하나를 세우므로 항목 수만큼이면 끝난다.
    for (let step = 0; step <= n; ++step) {
      let used = 0;
      let open = 0;
      for (let i = 0; i < n; ++i) {
        if (frozen[i]) used = f(used + lengths[i]);
        else open = f(open + f(items[i].weight));
      }
      if (open <= 0) break;
      const left = f(pool - used);
      const free = left > 0 ? left : 0;
      let violation = 0;
      const excess = new Array(n).fill(0);
      for (let i = 0; i < n; ++i) {
        if (frozen[i]) continue;
        const item = items[i];
        const share = f(free * f(f(item.weight) / open));
        let clamped = share;
        const maximum = f(item.maximum || 0);
        const minimum = f(item.minimum || 0);
        if (maximum > 0 && clamped > maximum) clamped = maximum;
        if (minimum > 0 && clamped < minimum) clamped = minimum;
        lengths[i] = clamped;
        excess[i] = f(clamped - share);
        violation = f(violation + excess[i]);
      }
      let froze = false;
      for (let i = 0; i < n; ++i) {
        if (frozen[i] || excess[i] === 0) continue;
        // 합이 0이면(늘림과 줄임이 맞먹으면) 어긴 것을 모두 세운다.
        if (violation === 0 || violation > 0 === excess[i] > 0) {
          frozen[i] = true;
          froze = true;
        }
      }
      if (!froze) break;
    }
    return lengths;
  }

  const maxf = (a, b) => (a < b ? b : a); // std::max(a, b)

  // resolve_tracks(tracks, wanted, available, spacing) — 줄 길이 (grid_element.cpp)
  //  고정은 그 값, 맞춤은 잰 값(하한 이상), 유연은 남는 자리를 비율로 나눈 값(하한 이상).
  //  available이 무한대면 남는 자리가 없다. 유연 줄도 잰 값이다 (원하는 크기를 물을 때).
  function resolveTracks(tracks, wanted, available, spacing) {
    const n = tracks.length;
    const lengths = new Array(n).fill(0);
    spacing = f(spacing);
    let fixed = n === 0 ? 0 : f(spacing * (n - 1));
    let weights = 0;
    const bounded = Number.isFinite(available);
    for (let i = 0; i < n; ++i) {
      const track = tracks[i];
      if (track.kind === "fixed") lengths[i] = f(track.length || 0);
      else if (track.kind === "fit" || !bounded) lengths[i] = maxf(f(track.minimum || 0), f(wanted[i] || 0));
      else {
        weights = f(weights + (f(track.weight || 0) > 0 ? f(track.weight) : 1));
        continue;
      }
      fixed = f(fixed + lengths[i]);
    }
    if (!bounded || weights <= 0) return lengths;
    // 유연 줄은 stack의 유연 항목과 같은 규칙으로 나눈다.
    const remaining = maxf(0, f(f(available) - fixed));
    const shares = [];
    const owners = [];
    for (let i = 0; i < n; ++i) {
      const track = tracks[i];
      if (track.kind !== "flexible") continue;
      shares.push({ weight: f(track.weight || 0) > 0 ? f(track.weight) : 1, minimum: f(track.minimum || 0), maximum: 0 });
      owners.push(i);
    }
    const resolved = distributeFlexible(shares, remaining);
    for (let s = 0; s < owners.length; ++s) lengths[owners[s]] = resolved[s];
    return lengths;
  }

  // arrange의 starts — 줄의 시작점 (논리 픽셀, 안쪽 영역 기준)
  function trackStarts(lengths, spacing) {
    const positions = [];
    let position = 0;
    for (const length of lengths) {
      positions.push(position);
      position = f(position + f(length + f(spacing))); // position += lengths[index] + spacing
    }
    return positions;
  }

  // sum_with_spacing — 줄들이 차지하는 길이 (사이 간격 포함)
  function sumWithSpacing(lengths, spacing) {
    if (!lengths.length) return 0;
    let total = 0;
    for (const length of lengths) total = f(total + length);
    return f(total + f(f(spacing) * (lengths.length - 1)));
  }

  // wrap_columns_for(config, available_width) — 한 줄에 드는 항목 수 (적어도 1)
  function wrapColumnsFor(config, availableWidth) {
    const itemWidth = f(config.item_width || 0);
    const spacing = f(config.spacing || 0);
    if (itemWidth <= 0) return 1;
    const step = f(itemWidth + spacing);
    if (step <= 0) return 1;
    // 마지막 항목 뒤에는 간격이 붙지 않으므로 폭에 간격 한 칸을 더해 나눈다.
    const fits = f(f(f(availableWidth) + spacing) / step);
    return fits < 1 ? 1 : Math.trunc(fits);
  }

  // wrap_height_for(config, available_width, count) — count개를 담은 높이
  function wrapHeightFor(config, availableWidth, count) {
    if (count === 0) return 0;
    const columns = wrapColumnsFor(config, availableWidth);
    const rows = Math.floor((count + columns - 1) / columns);
    return f(f(rows * f(config.item_height || 0)) + f((rows - 1) * f(config.line_spacing || 0)));
  }

  // wrap_element::arrange가 칸을 놓는 식 (slot 원점 0, 배율 scale)
  function wrapSlots(config, slotWidth, count, scale) {
    const s = scale > 0 ? scale : 1;
    const columns = wrapColumnsFor(config, f(slotWidth / s));
    const itemWidth = f(f(config.item_width || 0) * s);
    const itemHeight = f(f(config.item_height || 0) * s);
    const stepX = f(itemWidth + f(f(config.spacing || 0) * s));
    const stepY = f(itemHeight + f(f(config.line_spacing || 0) * s));
    const slots = [];
    for (let index = 0; index < count; ++index)
      slots.push({ x: f((index % columns) * stepX), y: f(Math.floor(index / columns) * stepY), width: itemWidth, height: itemHeight });
    return { columns, slots };
  }

  // classify_window_width(width, breakpoints) — 논리 폭의 등급
  function classifyWindowWidth(width, breakpoints) {
    const medium = breakpoints && breakpoints.medium != null ? f(breakpoints.medium) : 600;
    const expanded = breakpoints && breakpoints.expanded != null ? f(breakpoints.expanded) : 840;
    width = f(width);
    if (width >= expanded && expanded >= medium) return "expanded";
    if (width >= medium) return "medium";
    return "compact";
  }

  // make_window_metrics의 배율 다듬기 — 유한하고 0보다 커야 그 값, 아니면 1
  function logicalScale(scale) {
    return Number.isFinite(scale) && scale > 0 ? f(scale) : 1;
  }

  const api = { distributeFlexible, resolveTracks, trackStarts, sumWithSpacing, wrapColumnsFor, wrapHeightFor, wrapSlots, classifyWindowWidth, logicalScale };
  root.LuilContainers = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);

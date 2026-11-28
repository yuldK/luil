/* luil 설명서 — 잉크 획과 SRT 자막의 판정을 JS로 옮긴 것
 *
 * src/ui/ink.cpp: ink_width_at, fold_pressure, catmull_rom, ink_discs, add_bridge, ink_path(원·사다리꼴 목록),
 *   make_ink_stroke(점 거르기·압력 접기·경계), distance_to_segment, segment_distance, simplify_ink_points, ink_strokes_hit
 * src/text/srt.cpp: timestamp, trim_spacing, read_srt(파일 대신 글 하나), subtitle_at
 * 를 함수 단위로 옮겼다. 이름 옆 주석이 C++ 원본의 이름이다.
 *
 * float(32비트) 셈은 연산마다 Math.fround로 접는다. std::hypot(float, float)은 glibc처럼 double로 셈한 뒤 접는다.
 * 원본 C++(src/ui/ink.cpp, src/text/srt.cpp)을 그대로 들여와 x86-64 g++ -O2 -ffp-contract=off로 지은 대조기에
 * 무작위 입력을 넣어 결과 비트가 같음을 확인했다. FMA로 곱셈·덧셈을 묶는 빌드(ARM64의 clang 기본값 등)에서는
 * 마지막 자리가 다를 수 있다.
 *
 *   ink_point  { x, y, pressure }          ink_style { width, minimum_ratio }  (color는 그리기의 일이라 뺐다)
 *   ink_disc   { x, y, r }                 path op  { kind: "circle", x, y, r } | { kind: "polygon", pts: [[x, y] × 4] }
 *   stroke     { id, points, style, discs, ops, bounds: { x, y, width, height }, empty }
 *   subtitle_cue { start_ms, end_ms, text }
 */
(function () {
  "use strict";

  const f = Math.fround;
  const finite = Number.isFinite;
  // std::clamp·std::min·std::max와 같은 비교다 (NaN을 만나도 C++과 같은 쪽을 고른다).
  const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
  const min2 = (a, b) => (b < a ? b : a);
  const max2 = (a, b) => (a < b ? b : a);
  // std::hypot(float, float): glibc의 hypotf는 두 제곱을 double로 더해 제곱근을 구한 뒤 float로 접는다.
  const hypot = (x, y) => f(Math.sqrt(x * x + y * y));

  const SPACING_RATIO = f(0.5); // ink_spacing_ratio
  const MAX_STEPS = 32; // ink_maximum_steps

  // fold_pressure
  function foldPressure(pressure) {
    return finite(pressure) ? clamp(pressure, 0, 1) : 1;
  }

  // ink_width_at — 폭 = width × (minimum_ratio + (1 − minimum_ratio) × pressure)
  function widthAt(style, pressure) {
    if (!finite(style.width) || style.width < 0 || !finite(style.minimum_ratio)) return 0;
    const ratio = clamp(style.minimum_ratio, 0, 1);
    return f(style.width * f(ratio + f(f(1 - ratio) * foldPressure(pressure))));
  }

  // catmull_rom
  function catmullRom(p0, p1, p2, p3, t) {
    const t2 = f(t * t);
    const t3 = f(t2 * t);
    const first = f(f(2 * p1) + f(f(p2 - p0) * t));
    const second = f(f(f(f(2 * p0) - f(5 * p1)) + f(4 * p2)) - p3);
    const third = f(f(f(f(3 * p1) - p0) - f(3 * p2)) + p3);
    return f(0.5 * f(f(first + f(second * t2)) + f(third * t3)));
  }

  // ink_discs — 저장한 점 사이를 Catmull-Rom으로 보간한 원들이다. 반지름은 두 점 반지름의 선형 보간이다.
  function inkDiscs(points, style) {
    const discs = [];
    if (!points.length) return discs;
    const radiusOf = (pressure) => f(widthAt(style, pressure) / 2);
    discs.push({ x: points[0].x, y: points[0].y, r: radiusOf(points[0].pressure) });
    for (let index = 0; index + 1 < points.length; index++) {
      const p0 = points[index > 0 ? index - 1 : index];
      const p1 = points[index];
      const p2 = points[index + 1];
      const p3 = points[index + 2 < points.length ? index + 2 : index + 1];
      const r1 = radiusOf(p1.pressure);
      const r2 = radiusOf(p2.pressure);
      const length = hypot(f(p2.x - p1.x), f(p2.y - p1.y));
      const spacing = max2(0.5, f(f(min2(r1, r2) * SPACING_RATIO) * 2));
      const steps = clamp(Math.trunc(Math.ceil(f(length / spacing))), 1, MAX_STEPS);
      for (let step = 1; step <= steps; step++) {
        const t = f(step / steps);
        discs.push({ x: catmullRom(p0.x, p1.x, p2.x, p3.x, t), y: catmullRom(p0.y, p1.y, p2.y, p3.y, t), r: f(r1 + f(f(r2 - r1) * t)) });
      }
    }
    return discs;
  }

  // add_bridge — 두 원을 잇는 바깥 접선의 사다리꼴이다. 한 원이 다른 원을 품으면 없다(null).
  //   원과 같은 방향(화면 좌표에서 시계 방향, 신발끈 넓이가 양수)으로 감는다.
  function addBridge(from, to) {
    const dx = f(to.x - from.x);
    const dy = f(to.y - from.y);
    const distance = hypot(dx, dy);
    if (distance <= Math.abs(f(from.r - to.r)) || distance <= 0) return null;
    const ux = f(dx / distance);
    const uy = f(dy / distance);
    const sine = f(f(from.r - to.r) / distance);
    const cosine = f(Math.sqrt(max2(0, f(1 - f(sine * sine)))));
    const leftX = f(f(-uy * cosine) + f(ux * sine));
    const leftY = f(f(ux * cosine) + f(uy * sine));
    const rightX = f(f(uy * cosine) + f(ux * sine));
    const rightY = f(f(-ux * cosine) + f(uy * sine));
    let corners = [
      [f(from.x + f(from.r * leftX)), f(from.y + f(from.r * leftY))],
      [f(to.x + f(to.r * leftX)), f(to.y + f(to.r * leftY))],
      [f(to.x + f(to.r * rightX)), f(to.y + f(to.r * rightY))],
      [f(from.x + f(from.r * rightX)), f(from.y + f(from.r * rightY))],
    ];
    let area = 0;
    for (let index = 0; index < corners.length; index++) {
      const a = corners[index];
      const b = corners[(index + 1) % corners.length];
      area = f(area + f(f(a[0] * b[1]) - f(b[0] * a[1])));
    }
    if (area < 0) corners = corners.reverse();
    return corners;
  }

  // ink_path — 반지름이 양수인 원마다 원 하나, 이웃 원 사이마다 사다리꼴 하나 (nonzero로 채운다).
  function inkPath(points, style) {
    const discs = inkDiscs(points, style);
    const ops = [];
    for (let index = 0; index < discs.length; index++) {
      const disc = discs[index];
      if (disc.r > 0) ops.push({ kind: "circle", x: disc.x, y: disc.y, r: disc.r });
      if (index + 1 < discs.length) {
        const corners = addBridge(disc, discs[index + 1]);
        if (corners) ops.push({ kind: "polygon", pts: corners });
      }
    }
    return { discs, ops };
  }

  // SkPath::getBounds — 모든 점의 최소·최대 (원은 x ± r, y ± r)
  function pathBounds(ops) {
    let any = false;
    let minX = 0, minY = 0, maxX = 0, maxY = 0;
    const grow = (x, y) => {
      if (!any) {
        any = true;
        minX = maxX = x;
        minY = maxY = y;
        return;
      }
      minX = min2(minX, x);
      maxX = max2(maxX, x);
      minY = min2(minY, y);
      maxY = max2(maxY, y);
    };
    for (const op of ops) {
      if (op.kind === "circle") {
        grow(f(op.x - op.r), f(op.y - op.r));
        grow(f(op.x + op.r), f(op.y + op.r));
      } else {
        for (const p of op.pts) grow(p[0], p[1]);
      }
    }
    return any ? { x: minX, y: minY, width: f(maxX - minX), height: f(maxY - minY) } : { x: 0, y: 0, width: 0, height: 0 };
  }

  // make_ink_stroke — 유한하지 않은 점은 버리고 압력은 0~1로 접는다. 점이 없으면 빈 획, 점 하나는 둥근 점이다.
  function makeInkStroke(id, points, style) {
    const kept = [];
    for (const p of points) if (finite(p.x) && finite(p.y)) kept.push({ x: p.x, y: p.y, pressure: foldPressure(p.pressure) });
    const path = inkPath(kept, style);
    return { id, points: kept, style, discs: path.discs, ops: path.ops, bounds: path.ops.length ? pathBounds(path.ops) : { x: 0, y: 0, width: 0, height: 0 }, empty: kept.length === 0 };
  }

  // distance_to_segment — [거리, 선분 위의 자리 t(0~1)]
  function distanceToSegment(x, y, a, b) {
    const dx = f(b.x - a.x);
    const dy = f(b.y - a.y);
    const lengthSquared = f(f(dx * dx) + f(dy * dy));
    let t = 0;
    if (lengthSquared > 0) t = clamp(f(f(f(f(x - a.x) * dx) + f(f(y - a.y) * dy)) / lengthSquared), 0, 1);
    return [hypot(f(x - f(a.x + f(dx * t))), f(y - f(a.y + f(dy * t)))), t];
  }

  // cross
  function cross(o, a, b) {
    return f(f(f(a.x - o.x) * f(b.y - o.y)) - f(f(a.y - o.y) * f(b.x - o.x)));
  }

  // segment_distance — 두 선분 사이의 거리다. 엇갈려 만나면 0이다.
  function segmentDistance(a, b, c, d) {
    const d1 = cross(c, d, a);
    const d2 = cross(c, d, b);
    const d3 = cross(a, b, c);
    const d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return 0;
    const values = [distanceToSegment(a.x, a.y, c, d)[0], distanceToSegment(b.x, b.y, c, d)[0], distanceToSegment(c.x, c.y, a, b)[0], distanceToSegment(d.x, d.y, a, b)[0]];
    let best = values[0];
    for (let index = 1; index < values.length; index++) if (values[index] < best) best = values[index];
    return best;
  }

  // simplify_ink_points — Ramer-Douglas-Peucker. 자리 오차와 압력 오차를 각자의 허용치로 나눠 큰 쪽을 본다.
  //   onSpan(first, last, farthest, worst)를 주면 구간을 볼 때마다 알린다 (실습의 그림용, 판정과 무관하다).
  function simplifyInkPoints(points, tolerance, pressureTolerance, onSpan) {
    const source = points.map((p) => ({ x: p.x, y: p.y, pressure: p.pressure }));
    if (!finite(tolerance) || tolerance <= 0 || source.length < 3) return source;
    const watchPressure = finite(pressureTolerance) && pressureTolerance > 0;
    const keep = new Array(source.length).fill(false);
    keep[0] = true;
    keep[source.length - 1] = true;
    const spans = [[0, source.length - 1]];
    while (spans.length) {
      const [first, last] = spans.pop();
      if (last <= first + 1) continue;
      let farthest = first;
      let worst = 0;
      for (let index = first + 1; index < last; index++) {
        const [distance, along] = distanceToSegment(source[index].x, source[index].y, source[first], source[last]);
        const expected = f(source[first].pressure + f(f(source[last].pressure - source[first].pressure) * along));
        let error = f(distance / tolerance);
        if (watchPressure) error = max2(error, f(Math.abs(f(source[index].pressure - expected)) / pressureTolerance));
        if (error > worst) {
          worst = error;
          farthest = index;
        }
      }
      if (onSpan) onSpan(first, last, farthest, worst);
      if (worst <= 1) continue;
      keep[farthest] = true;
      spans.push([first, farthest]);
      spans.push([farthest, last]);
    }
    return source.filter((_, index) => keep[index]);
  }

  // ink_strokes_hit — 지우개가 from에서 to까지 반지름 radius로 쓸 때 닿는 획 id다 (묶음의 순서).
  function inkStrokesHit(strokes, from, to, radius) {
    const ids = [];
    if (!finite(from.x) || !finite(from.y) || !finite(to.x) || !finite(to.y)) return ids;
    const reach = finite(radius) && radius > 0 ? radius : 0;
    const sweep = {
      x: f(min2(from.x, to.x) - reach),
      y: f(min2(from.y, to.y) - reach),
      width: f(Math.abs(f(to.x - from.x)) + f(reach * 2)),
      height: f(Math.abs(f(to.y - from.y)) + f(reach * 2)),
    };
    for (const stroke of strokes) {
      const box = stroke.bounds;
      if (stroke.empty || box.x > f(sweep.x + sweep.width) || f(box.x + box.width) < sweep.x || box.y > f(sweep.y + sweep.height) || f(box.y + box.height) < sweep.y) continue;
      const points = stroke.points;
      const segments = points.length > 1 ? points.length - 1 : 1;
      let hit = false;
      for (let index = 0; index < segments && !hit; index++) {
        const a = points[index];
        const b = points[Math.min(index + 1, points.length - 1)];
        const half = f(max2(widthAt(stroke.style, a.pressure), widthAt(stroke.style, b.pressure)) / 2);
        hit = segmentDistance(from, to, a, b) <= f(reach + half);
      }
      if (hit) ids.push(stroke.id);
    }
    return ids;
  }

  /* ---------- SRT ---------- */

  // trim_spacing — 앞뒤의 공백과 탭만 다듬는다.
  function trimSpacing(value) {
    let first = 0;
    while (first < value.length && (value[first] === " " || value[first] === "\t")) first++;
    if (first === value.length) return "";
    let last = value.length - 1;
    while (value[last] === " " || value[last] === "\t") last--;
    return value.slice(first, last + 1);
  }

  // timestamp의 거절 까닭이다. 받으면 null이다 (실습이 보이는 글 — 판정은 timestamp와 같은 조건이다).
  function timestampProblem(value) {
    if (value.length !== 12) return "12글자가 아니다";
    if (value[2] !== ":" || value[5] !== ":" || value[8] !== ",") return "구분 문자가 HH:MM:SS,mmm 자리에 없다";
    const digits = (offset, size) => {
      const part = value.substr(offset, size);
      return /^[0-9]+$/.test(part) && part.length === size ? parseInt(part, 10) : -1;
    };
    const hours = digits(0, 2);
    const minutes = digits(3, 2);
    const seconds = digits(6, 2);
    const millis = digits(9, 3);
    if (hours < 0 || minutes < 0 || seconds < 0 || millis < 0) return "숫자가 아닌 칸이 있다";
    if (minutes >= 60) return "분이 60 이상이다";
    if (seconds >= 60) return "초가 60 이상이다";
    return null;
  }

  // timestamp — 정확히 "HH:MM:SS,mmm" 12글자. 분·초 < 60. 받으면 ms, 아니면 null (std::nullopt).
  function timestamp(value) {
    if (timestampProblem(value) !== null) return null;
    const n = (offset, size) => parseInt(value.substr(offset, size), 10);
    return ((n(0, 2) * 60 + n(3, 2)) * 60 + n(6, 2)) * 1000 + n(9, 3);
  }

  // read_srt — 파일 내용 대신 글 하나를 받는다 (줄은 \n으로 나누고, 첫 줄의 BOM과 줄 끝의 \r 하나를 지운다).
  //   돌려주는 것: { cues: [subtitle_cue], blocks: [{ line, start, end, text, kept, problem }] }
  //   blocks는 실습이 까닭을 보이려고 모은 것이고 cues가 원본의 반환값이다.
  function readSrt(text) {
    const raw = text.split("\n");
    if (raw.length && raw[raw.length - 1] === "") raw.pop(); // 마지막 \n 뒤에는 줄이 없다 (std::getline)
    let index = 0;
    const nextLine = () => {
      if (index >= raw.length) return null;
      let line = raw[index];
      if (index === 0 && line.startsWith("﻿")) line = line.slice(1);
      index++;
      if (line.endsWith("\r")) line = line.slice(0, -1);
      return line;
    };
    const cues = [];
    const blocks = [];
    let line;
    while ((line = nextLine()) !== null) {
      const arrow = line.indexOf("-->");
      if (arrow < 0) continue;
      const lineNumber = index;
      const left = trimSpacing(line.slice(0, arrow));
      const right = trimSpacing(line.slice(arrow + 3));
      const start = timestamp(left);
      const end = timestamp(right);
      let body = "";
      let next;
      while ((next = nextLine()) !== null && trimSpacing(next) !== "") {
        if (body) body += "\n";
        body += next;
      }
      const kept = start !== null && end !== null && end > start && body !== "";
      let problem = null;
      if (start === null) problem = "시작 시각: " + timestampProblem(left);
      else if (end === null) problem = "끝 시각: " + timestampProblem(right);
      else if (!(end > start)) problem = "끝이 시작보다 늦지 않다";
      else if (body === "") problem = "글이 없다";
      blocks.push({ line: lineNumber, start, end, text: body, kept, problem });
      if (kept) cues.push({ start_ms: start, end_ms: end, text: body });
    }
    return { cues, blocks };
  }

  // subtitle_at — 시작은 포함, 끝은 제외. 겹치는 cue는 파일 순서대로 \n으로 잇는다.
  function subtitleAt(cues, position) {
    let result = "";
    for (const cue of cues) {
      if (position < cue.start_ms || position >= cue.end_ms) continue;
      if (result) result += "\n";
      result += cue.text;
    }
    return result;
  }

  window.LuilMedia = {
    ink: { foldPressure, widthAt, catmullRom, inkDiscs, addBridge, inkPath, makeInkStroke, distanceToSegment, segmentDistance, simplifyInkPoints, inkStrokesHit },
    srt: { trimSpacing, timestamp, timestampProblem, readSrt, subtitleAt },
  };
})();

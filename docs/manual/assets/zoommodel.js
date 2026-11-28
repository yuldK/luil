/* luil 설명서 — 확대 보기(zoom_view_element)의 도우미 함수를 브라우저로 옮긴 것이다.
 *
 * 원본(커밋 563a855):
 *   - src/ui/zoom_view_element.cpp 16-274: valid_config, clamp_state(overlap·contain), fit_actions, zoom_about,
 *     rotate_about, rotate_point, rotated_bounds, pan_by, pan_blocked, judge_edge_swipe, double_tap_zoom, zoom_to_fit
 *   - src/ui/zoom_view_element.cpp 374-418: 키 (회전·+/−·0·화살표·Page)
 *   - src/ui/ui_interaction.cpp 1039-1052: 휠 (확대·이동의 갈래와 기준점), 580: 끌기 이동량
 * C++의 float 연산은 연산마다 Math.fround로, double 연산은 그대로 옮겼다. 메시지는 { type, ... } 값으로 돌려준다.
 * 설정의 factory는 있으면 참인 표시(zoom_by·pan_by·zoom_to·rotate_by)로 받는다.
 */
(function () {
  "use strict";
  const f = Math.fround;
  const fin = Number.isFinite;
  const finitePoint = (p) => fin(p.x) && fin(p.y);
  const minF = (a, b) => (b < a ? b : a); // std::min
  const maxF = (a, b) => (a < b ? b : a); // std::max
  const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v); // std::clamp
  const ZOOM_STEP = f(1.2);

  // std::remainder(x, 360): 가장 가까운 정수배를 뺀다 (반이면 짝수 쪽).
  function remainder360(x) {
    let n = Math.round(x / 360);
    let r = x - n * 360;
    if (r > 180) {
      n += 1;
      r -= 360;
    } else if (r < -180) {
      n -= 1;
      r += 360;
    }
    if (Math.abs(r) === 180 && Math.abs(n) % 2 === 1) {
      if (r === 180) r -= 360;
      else r += 360;
    }
    return r;
  }
  function foldDegrees(d) {
    const folded = remainder360(d);
    return folded <= -180 ? folded + 360 : folded;
  }
  function rotationTerms(folded) {
    if (folded === 0) return { c: 1, s: 0 };
    if (folded === 90) return { c: 0, s: 1 };
    if (folded === -90) return { c: 0, s: -1 };
    if (folded === 180) return { c: -1, s: 0 };
    const radians = (folded * Math.PI) / 180;
    return { c: Math.cos(radians), s: Math.sin(radians) };
  }

  function validConfig(c) {
    if (!fin(c.zoom) || c.zoom <= 0 || !finitePoint(c.origin) || !fin(c.minimum_zoom) || c.minimum_zoom <= 0 || !fin(c.maximum_zoom) || c.maximum_zoom < c.minimum_zoom || !fin(c.rotation) || !fin(c.rotation_step) || c.rotation_step <= 0 || c.rotation_step > 180)
      return false;
    const tap = c.double_tap;
    if (tap.enabled && (!fin(tap.factor) || tap.factor <= 0 || !fin(tap.fit_margin) || tap.fit_margin < 0 || !fin(tap.maximum_fit_zoom) || tap.maximum_fit_zoom <= 0)) return false;
    if (!c.content_bounds) return true;
    const b = c.content_bounds;
    return finitePoint(b) && fin(b.width) && fin(b.height) && b.width > 0 && b.height > 0 && fin(f(b.x + b.width)) && fin(f(b.y + b.height));
  }

  function clampState(c, viewport, value) {
    if (!validConfig(c)) return { zoom: 1, origin: { x: 0, y: 0 }, rotation: 0 };
    const zoom = clamp(value.zoom, c.minimum_zoom, c.maximum_zoom);
    const origin = finitePoint(value.origin) ? { x: value.origin.x, y: value.origin.y } : { x: c.origin.x, y: c.origin.y };
    const rotation = fin(value.rotation) ? f(foldDegrees(value.rotation)) : 0;
    if (!c.content_bounds || !finitePoint(viewport) || viewport.x <= 0 || viewport.y <= 0) return { zoom, origin, rotation };
    const box = c.content_bounds;
    const contain = c.bounds === "contain";
    const axis = (o, extent, start, length) => {
      const size = length * zoom;
      if (contain) {
        if (size <= extent) return f(-(start + length / 2) * zoom);
        const low = extent / 2 - (start + length) * zoom;
        const high = -extent / 2 - start * zoom;
        return f(clamp(o, low, high));
      }
      const overlap = Math.min(1, extent, size);
      const low = -extent / 2 - (start + length) * zoom + overlap;
      const high = extent / 2 - start * zoom - overlap;
      return f(clamp(o, low, high));
    };
    return { zoom, origin: { x: axis(origin.x, viewport.x, box.x, box.width), y: axis(origin.y, viewport.y, box.y, box.height) }, rotation };
  }

  const currentOf = (c, viewport) => clampState(c, viewport, { zoom: c.zoom, origin: c.origin, rotation: c.rotation });

  function zoomAbout(c, viewport, factor, anchor) {
    const current = currentOf(c, viewport);
    if (!validConfig(c) || !fin(factor) || factor <= 0 || !finitePoint(anchor)) return current;
    const next = f(clamp(current.zoom * factor, c.minimum_zoom, c.maximum_zoom));
    if (next === current.zoom) return current;
    const ratio = next / current.zoom;
    const origin = { x: f(anchor.x - (anchor.x - current.origin.x) * ratio), y: f(anchor.y - (anchor.y - current.origin.y) * ratio) };
    return clampState(c, viewport, { zoom: next, origin, rotation: current.rotation });
  }

  function rotateAbout(c, viewport, degrees, anchor) {
    const current = currentOf(c, viewport);
    if (!validConfig(c) || !fin(degrees) || !finitePoint(anchor)) return current;
    const folded = foldDegrees(degrees);
    if (folded === 0) return current;
    const t = rotationTerms(folded);
    const across = anchor.x - current.origin.x;
    const down = anchor.y - current.origin.y;
    const origin = { x: f(anchor.x - (across * t.c - down * t.s)), y: f(anchor.y - (across * t.s + down * t.c)) };
    if (!finitePoint(origin)) return current;
    return { zoom: current.zoom, origin, rotation: f(foldDegrees(current.rotation + folded)) };
  }

  function rotatePoint(p, degrees) {
    if (!fin(degrees) || !finitePoint(p)) return p;
    const folded = foldDegrees(degrees);
    if (folded === 0) return p;
    const t = rotationTerms(folded);
    return { x: f(p.x * t.c - p.y * t.s), y: f(p.x * t.s + p.y * t.c) };
  }

  function rotatedBounds(b, degrees) {
    if (!fin(degrees) || foldDegrees(degrees) === 0) return b;
    const corners = [
      rotatePoint({ x: b.x, y: b.y }, degrees),
      rotatePoint({ x: f(b.x + b.width), y: b.y }, degrees),
      rotatePoint({ x: b.x, y: f(b.y + b.height) }, degrees),
      rotatePoint({ x: f(b.x + b.width), y: f(b.y + b.height) }, degrees),
    ];
    let left = corners[0].x, top = corners[0].y, right = corners[0].x, bottom = corners[0].y;
    for (const p of corners) {
      left = minF(left, p.x);
      top = minF(top, p.y);
      right = maxF(right, p.x);
      bottom = maxF(bottom, p.y);
    }
    return { x: left, y: top, width: f(right - left), height: f(bottom - top) };
  }

  function panBy(c, viewport, delta) {
    const current = currentOf(c, viewport);
    if (!finitePoint(delta)) return current;
    current.origin = { x: f(current.origin.x + delta.x), y: f(current.origin.y + delta.y) };
    return clampState(c, viewport, current);
  }

  function panBlocked(c, viewport, delta) {
    if (!finitePoint(delta)) return { x: 0, y: 0 };
    const current = currentOf(c, viewport);
    const next = panBy(c, viewport, delta);
    return { x: f(delta.x - f(next.origin.x - current.origin.x)), y: f(delta.y - f(next.origin.y - current.origin.y)) };
  }

  function judgeEdgeSwipe(config, moved, blocked, velocity) {
    const result = { blocked, velocity, horizontal: 0, vertical: 0 };
    if (!finitePoint(moved) || !finitePoint(blocked) || !finitePoint(velocity)) return result;
    const judge = (m, b, v) => {
      const direction = b < 0 ? -1 : 1;
      if (Math.abs(b) < config.minimum_distance || Math.abs(m) >= config.minimum_distance || f(v * direction) < config.minimum_velocity) return 0;
      return b < 0 ? -1 : 1;
    };
    result.horizontal = judge(moved.x, blocked.x, velocity.x);
    result.vertical = judge(moved.y, blocked.y, velocity.y);
    return result;
  }

  function zoomToFit(c, viewport, margin = 16, maximumFit = 1, axis = "whole") {
    const current = currentOf(c, viewport);
    const needsWidth = axis !== "height";
    const needsHeight = axis !== "width";
    const twice = f(margin * 2);
    if (!validConfig(c) || !c.content_bounds || !finitePoint(viewport) || !fin(margin) || margin < 0 || !fin(maximumFit) || maximumFit <= 0 || (needsWidth && viewport.x <= twice) || (needsHeight && viewport.y <= twice))
      return current;
    const box = c.content_bounds;
    const across = f(f(viewport.x - twice) / box.width);
    const down = f(f(viewport.y - twice) / box.height);
    const fitted = axis === "whole" ? minF(across, down) : axis === "width" ? across : down;
    const zoom = clamp(minF(fitted, maximumFit), c.minimum_zoom, c.maximum_zoom);
    const centered = { x: f(-f(box.x + f(box.width / 2)) * zoom), y: f(-f(box.y + f(box.height / 2)) * zoom) };
    const ratio = f(zoom / current.zoom);
    const origin = { x: axis === "height" ? f(current.origin.x * ratio) : centered.x, y: axis === "width" ? f(current.origin.y * ratio) : centered.y };
    return clampState(c, viewport, { zoom, origin, rotation: current.rotation });
  }

  function fitActions(c, viewport, margin = 16, maximum = 1, axis = "whole") {
    if (!c.content_bounds || !c.zoom_by || !c.pan_by) return [];
    const current = zoomAbout(c, viewport, 1, { x: 0, y: 0 });
    const next = zoomToFit(c, viewport, margin, maximum, axis);
    const ratio = f(next.zoom / current.zoom);
    const out = [];
    if (ratio !== 1) out.push({ type: "zoom_by", factor: ratio, anchor: { x: 0, y: 0 } });
    const scaled = zoomAbout(c, viewport, ratio, { x: 0, y: 0 });
    const delta = { x: f(next.origin.x - scaled.origin.x), y: f(next.origin.y - scaled.origin.y) };
    if (delta.x !== 0 || delta.y !== 0) out.push({ type: "pan_by", delta });
    return out;
  }

  function doubleTapZoom(c, viewport, anchor) {
    if (!c.zoom_by || !validConfig(c)) return [];
    const tap = c.double_tap;
    const current = zoomAbout(c, viewport, 1, { x: 0, y: 0 });
    const fit = !!c.content_bounds;
    const base = fit ? zoomToFit(c, viewport, tap.fit_margin, tap.maximum_fit_zoom).zoom : 1;
    if (Math.abs(f(current.zoom - base)) <= f(base * f(0.01))) {
      const target = clamp(f(base * tap.factor), c.minimum_zoom, c.maximum_zoom);
      if (target === current.zoom || !finitePoint(anchor)) return [];
      return [{ type: "zoom_by", factor: f(target / current.zoom), anchor }];
    }
    if (fit) return fitActions(c, viewport, tap.fit_margin, tap.maximum_fit_zoom);
    return [{ type: "zoom_by", factor: f(1 / current.zoom), anchor: { x: 0, y: 0 } }];
  }

  /* 보기에 초점이 있을 때의 키다. key: "left" | "right" | "up" | "down" | "page_up" | "page_down" | "home" | "zoom_in" | "zoom_out" | "0" | 그 밖.
   * bounds = 보기의 물리 크기 { width, height }, scale = 배율. 보기의 것이 아니면 null, 삼키기만 하면 []. */
  function keys(c, bounds, scale, event) {
    if (event.control || event.alt || event.meta) return null;
    if (c.rotate_by && event.shift && (event.key === "left" || event.key === "right" || event.key === "home")) {
      const degrees = event.key === "home" ? -c.rotation : event.key === "right" ? c.rotation_step : -c.rotation_step;
      if (degrees === 0) return [];
      return [{ type: "rotate_by", degrees, anchor: { x: 0, y: 0 } }];
    }
    if (event.key === "zoom_in" || event.key === "zoom_out") return c.zoom_by ? [{ type: "zoom_by", factor: event.key === "zoom_in" ? ZOOM_STEP : f(1 / ZOOM_STEP), anchor: { x: 0, y: 0 } }] : [];
    const viewport = { x: f(bounds.width / scale), y: f(bounds.height / scale) };
    if (event.key === "0") {
      if (c.content_bounds) return fitActions(c, viewport);
      return c.zoom_by ? [{ type: "zoom_by", factor: f(1 / c.zoom), anchor: { x: 0, y: 0 } }] : [];
    }
    const delta = { x: 0, y: 0 };
    switch (event.key) {
      case "left": delta.x = 32; break;
      case "right": delta.x = -32; break;
      case "up": delta.y = 32; break;
      case "down": delta.y = -32; break;
      case "page_up": delta.y = f(viewport.y * f(0.9)); break;
      case "page_down": delta.y = -f(viewport.y * f(0.9)); break;
      default: return null;
    }
    return c.pan_by ? [{ type: "pan_by", delta }] : [];
  }

  /* 확대 보기가 임자인 휠이다. ev = { x, y, delta(OS 눈금, 위로 +120), horizontal, control, shift },
   * box = 보기의 물리 bounds, scrollDelta = controller가 바꾼 논리 값(−delta/120 × 48). */
  function wheel(c, box, scale, ev, scrollDelta) {
    const s = fin(scale) && scale > 0 ? f(scale) : 1;
    if (ev.control || (c.wheel === "zoom" && !ev.horizontal)) {
      if (!c.zoom_by || ev.delta === 0) return [];
      const anchor = { x: f(f(f(ev.x - box.x) - f(box.width / 2)) / s), y: f(f(f(ev.y - box.y) - f(box.height / 2)) / s) };
      const notches = clamp(f(ev.delta / 120), -64, 64);
      return [{ type: "zoom_by", factor: f(Math.pow(ZOOM_STEP, notches)), anchor }];
    }
    if (!c.pan_by || scrollDelta === 0) return [];
    const d = ev.horizontal ? f(scrollDelta) : -f(scrollDelta);
    return [{ type: "pan_by", delta: ev.horizontal || ev.shift ? { x: d, y: 0 } : { x: 0, y: d } }];
  }

  // 마우스·펜으로 빈 곳을 끈 이동량이다 (물리 → 논리).
  function dragDelta(previous, current, scale) {
    const s = fin(scale) && scale > 0 ? f(scale) : 1;
    return { x: f(f(current.x - previous.x) / s), y: f(f(current.y - previous.y) / s) };
  }

  window.LuilZoom = { foldDegrees, validConfig, clampState, zoomAbout, rotateAbout, rotatePoint, rotatedBounds, panBy, panBlocked, judgeEdgeSwipe, zoomToFit, fitActions, doubleTapZoom, keys, wheel, dragDelta, ZOOM_STEP };
})();

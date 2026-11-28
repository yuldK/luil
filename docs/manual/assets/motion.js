/* luil 움직임 식의 브라우저 이식 — 9강 실험용
 *
 * include/luil/ui/transition.h, include/luil/ui/spring.h(닫힌 해), src/ui/page_transition.cpp,
 * src/ui/page_transition_element.cpp(poses_at)를 그대로 옮겼다. 시각은 ms number다.
 * C++에서 float인 칸은 Math.fround로 32비트에 맞춘다. 안쪽 계산은 double(JS number)이다.
 * 컴파일한 spring.h와 수치를 대조했다.
 */
(function () {
  "use strict";
  const f32 = Math.fround;
  const FRAME_MS = 16;                 // transition_frame
  const TRANSITION_MS = 140;           // transition_duration
  const PAGE_MS = 250;                 // page_transition_duration
  const CRITICAL_BAND = 1e-6;          // spring_detail::critical_band
  const DEFAULT_REST = f32(0.01);      // spring_detail::default_rest_threshold
  const PAGE_REST_CAP = f32(0.001);    // page_transition.cpp progress_rest_threshold

  const config = (responseMs, dampingFraction, restThreshold = 0.01) => ({ responseMs: Math.round(responseMs), dampingFraction: f32(dampingFraction), restThreshold: f32(restThreshold) });
  const SMOOTH = config(350, 1.0), SNAPPY = config(300, 0.75), BOUNCY = config(500, 0.5);
  const atRest = (v) => ({ from: f32(v), to: f32(v), startedMs: 0, config: config(350, 1.0), velocity: 0 });

  const usable = (s) => Number.isFinite(s.from) && Number.isFinite(s.to) && Number.isFinite(s.velocity)
    && s.config.responseMs > 0 && Number.isFinite(s.config.dampingFraction) && s.config.dampingFraction > 0;
  const settled = (s) => (Number.isFinite(s.to) ? s.to : Number.isFinite(s.from) ? s.from : 0);
  const seconds = (s, now) => (now <= s.startedMs ? 0 : (now - s.startedMs) / 1000);
  const omegaOf = (s) => (2 * Math.PI) / (s.config.responseMs / 1000);

  // spring_detail::evaluate — 감쇠비 구간 셋의 닫힌 해. y = 값 − to
  function evaluate(s, t) {
    const w = omegaOf(s);
    const z = s.config.dampingFraction, y0 = s.from - s.to, v0 = s.velocity;
    let y, dy, regime;
    if (Math.abs(z - 1) < CRITICAL_BAND) {
      regime = "critical";
      const decay = Math.exp(-w * t), slope = v0 + w * y0;
      y = decay * (y0 + slope * t);
      dy = decay * (v0 - w * slope * t);
    } else if (z < 1) {
      regime = "under";
      const rate = z * w, wd = w * Math.sqrt((1 - z) * (1 + z));
      const b = (v0 + rate * y0) / wd, decay = Math.exp(-rate * t);
      const c = Math.cos(wd * t), sn = Math.sin(wd * t);
      y = decay * (y0 * c + b * sn);
      dy = decay * ((b * wd - rate * y0) * c - (y0 * wd + rate * b) * sn);
    } else {
      regime = "over";
      const q = Math.sqrt((z - 1) * (z + 1));
      const fast = -w * (z + q), slow = -w / (z + q);
      const cf = (v0 - slow * y0) / (fast - slow), cs = y0 - cf;
      const ef = Math.exp(fast * t), es = Math.exp(slow * t);
      y = cs * es + cf * ef;
      dy = cs * slow * es + cf * fast * ef;
    }
    const given = s.config.restThreshold;
    const threshold = Number.isFinite(given) && given > 0 ? given : DEFAULT_REST;
    const measure = Math.hypot(y, dy / w);
    return { y, dy, w, measure, threshold, regime, finished: measure <= threshold };
  }
  const springFinished = (s, now) => !usable(s) || evaluate(s, seconds(s, now)).finished;
  function springValue(s, now) {
    if (!usable(s)) return settled(s);
    const a = evaluate(s, seconds(s, now));
    return a.finished ? s.to : f32(s.to + a.y);
  }
  function springVelocity(s, now) {
    if (!usable(s)) return 0;
    const a = evaluate(s, seconds(s, now));
    return a.finished ? 0 : f32(a.dy);
  }
  function springNextUpdate(s, now) {
    if (springFinished(s, now)) return null;
    return now < s.startedMs ? s.startedMs : now + FRAME_MS;
  }
  const springTo = (s, target, now, cfg, reduce) => (reduce ? atRest(target)
    : { from: springValue(s, now), to: f32(target), startedMs: now, config: cfg, velocity: springVelocity(s, now) });
  const springRelease = (value, velocity, target, now, cfg, reduce) => (reduce ? atRest(target)
    : { from: f32(value), to: f32(target), startedMs: now, config: cfg, velocity: f32(velocity) });
  function springRestTime(s, limitMs = 10000) {
    if (!usable(s)) return s.startedMs;
    const last = s.startedMs + Math.max(limitMs, 0);
    for (let at = s.startedMs; at < last; at += FRAME_MS) if (evaluate(s, seconds(s, at)).finished) return at;
    return last;
  }
  function springFromPhysics(m, k, c, thr = 0.01) {
    if (!(m > 0 && k > 0 && c > 0) || !isFinite(m) || !isFinite(k) || !isFinite(c)) return config(0, 0, thr);
    const ms = Math.min(Math.max(Math.round(2 * Math.PI * Math.sqrt(m / k) * 1000), 1), 1e9);
    return config(ms, Math.min(c / (2 * Math.sqrt(k * m)), 3.4028234663852886e38), thr);
  }

  /* ---------- transition.h ---------- */
  function easeInOut(t) {
    t = f32(t);
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    if (t < 0.5) return f32(2 * t * t);
    const back = f32(-2 * t + 2);
    return f32(1 - f32(back * back) / 2);
  }
  const motionDuration = (ms, reduce) => (reduce ? 0 : ms);
  const transitionFinished = (tr, now) => tr.durationMs <= 0 || now >= tr.startedMs + tr.durationMs;
  function transitionValue(tr, now) {
    if (transitionFinished(tr, now)) return tr.to;
    if (now <= tr.startedMs) return tr.from;
    const elapsed = Math.trunc(now - tr.startedMs); // duration_cast<milliseconds>: 정수 ms로 자른다
    // float 연산마다 32비트로 반올림한다 (C++의 float 식과 같은 값)
    const span = f32(tr.to - tr.from);
    return f32(tr.from + f32(span * easeInOut(f32(elapsed / tr.durationMs))));
  }
  function transitionNextUpdate(tr, now) {
    if (transitionFinished(tr, now)) return null;
    return now < tr.startedMs ? tr.startedMs : now + FRAME_MS;
  }
  const transitionTo = (tr, target, now, ms = TRANSITION_MS) => ({ from: transitionValue(tr, now), to: f32(target), startedMs: now, durationMs: ms });
  // 기울기(값/초)는 luil이 계산하지 않는다 — 그림의 보조선용으로만 수치 미분한다.
  const transitionSlope = (tr, now) => (transitionValue(tr, now + 0.5) - transitionValue(tr, now - 0.5)) * 1000;

  /* ---------- page_transition.cpp ---------- */
  // curve = { kind: "timing", durationMs, easing? } | { kind: "spring", config, velocity }
  function progressSpring(curve, startedMs) {
    const g = curve.config.restThreshold;
    const cfg = { ...curve.config, restThreshold: Number.isFinite(g) && g > 0 ? Math.min(g, PAGE_REST_CAP) : PAGE_REST_CAP };
    return { from: 0, to: 1, startedMs, config: cfg, velocity: f32(curve.velocity || 0) };
  }
  const moves = (curve, startedMs) => (curve.kind === "timing" ? curve.durationMs > 0 : !springFinished(progressSpring(curve, startedMs), startedMs));
  // start_page_transition: 움직임 줄이기거나 움직이지 않는 곡선이면 prior를 버린다
  const startPage = (hasPrior, curve, now, reduce) => ({ hasPrior: hasPrior && !reduce && moves(curve, now), startedMs: now, curve });
  function pageEnd(pt) {
    if (!pt.hasPrior) return null;
    if (pt.curve.kind === "timing") return pt.startedMs + Math.max(pt.curve.durationMs, 0);
    return springRestTime(progressSpring(pt.curve, pt.startedMs));
  }
  function pageProgress(pt, now) {
    const end = pageEnd(pt);
    if (end === null || now >= end) return 1;
    if (now <= pt.startedMs) return 0;
    const clamp01 = (x) => (Number.isFinite(x) ? Math.min(Math.max(x, 0), 1) : 1);
    if (pt.curve.kind === "timing") {
      const t = Math.min(Math.max((now - pt.startedMs) / pt.curve.durationMs, 0), 1);
      return clamp01((pt.curve.easing || easeInOut)(t));
    }
    return clamp01(springValue(progressSpring(pt.curve, pt.startedMs), now));
  }
  function pageNextUpdate(pt, now) {
    const end = pageEnd(pt);
    if (end === null || now >= end) return null;
    return now < pt.startedMs ? pt.startedMs : now + FRAME_MS;
  }
  // page_transition_element.cpp poses_at — box는 물리 픽셀
  function posesAt(effect, axis, forward, p, box) {
    const prior = { dx: 0, dy: 0, scale: 1, alpha: 1 }, next = { dx: 0, dy: 0, scale: 1, alpha: 1 };
    if (effect === "slide") {
      const extent = axis === "horizontal" ? box.width : box.height;
      const shift = Math.round(p * extent), sign = forward ? 1 : -1;
      const key = axis === "horizontal" ? "dx" : "dy";
      prior[key] = -sign * shift;
      next[key] = sign * (extent - shift);
      return { prior, next };
    }
    prior.alpha = Math.min(Math.max(1 - 2 * p, 0), 1);
    next.alpha = Math.min(Math.max(2 * p - 1, 0), 1);
    if (effect === "zoom") {
      const lerp = (a, b, t) => a + (b - a) * t;
      prior.scale = lerp(1, forward ? f32(1.04) : f32(0.94), p);
      next.scale = lerp(forward ? f32(0.94) : f32(1.04), 1, p);
    }
    return { prior, next };
  }

  window.LuilMotion = {
    FRAME_MS, TRANSITION_MS, PAGE_MS, SMOOTH, SNAPPY, BOUNCY, config, atRest, usable, evaluate, omegaOf,
    springValue, springVelocity, springFinished, springNextUpdate, springTo, springRelease, springRestTime, springFromPhysics,
    easeInOut, motionDuration, transitionFinished, transitionValue, transitionNextUpdate, transitionTo, transitionSlope,
    progressSpring, startPage, pageEnd, pageProgress, pageNextUpdate, posesAt,
  };
})();

/* luil 설명서 — 시트와 확인 대화 상자의 판정을 JS로 옮긴 것
 *
 * 함수 단위로 옮긴 원본:
 *  - src/ui/sheet_host_element.cpp: resolve_sheet_kind, sheet_detent_height, sheet_visible_height, settle_sheet,
 *    sheet_surface_bounds, release_action(익명 namespace)
 *  - src/ui/ui_interaction.cpp: interaction_controller::motion_track::add·velocity_at (놓은 순간의 속도, 100ms 창)
 *  - src/ui/message_box_element.cpp: message_box_default_index·entry_index·dismiss_index·stacks_buttons
 * float 연산은 Math.fround로 원본과 같은 단정밀도로 맞췄고, std::max·min·clamp는 NaN에서도 원본과 같은 쪽을 고르게 했다.
 * 원본 줄을 잘라 그대로 컴파일한 대조기의 무작위 입력과 견주어 같음을 확인했다 (설명서 실습의 lw-foot).
 *
 *   sheet config { kind: "bottom"|"end"|"start", side_from_width: number|null, detent: "half"|"full", drag_offset,
 *                  side_width, full_top_margin, bottom_max_width, fling_velocity, release, dismiss, settle (참·거짓: 칸이 있는가) }
 *   rect { x, y, width, height }   time: ms (motion track)
 */
(function () {
  "use strict";
  const f = Math.fround;
  const fin = Number.isFinite;
  // std::max(a, b) = (a < b) ? b : a,  std::min(a, b) = (b < a) ? b : a,  std::clamp = min(max(v, lo), hi)
  const cmax = (a, b) => (a < b ? b : a);
  const cmin = (a, b) => (b < a ? b : a);
  const cclamp = (v, lo, hi) => cmin(cmax(v, lo), hi);
  const finiteOr = (v, fallback) => (fin(v) ? v : fallback); // finite_or
  const positiveScale = (s) => (fin(s) && s > 0 ? s : 1); // positive_scale

  // sheet_host_element.cpp의 비공개 치수 (논리 픽셀)
  const HANDLE_STRIP = 24; // handle_strip_height
  const HANDLE_BAR_W = 32; // handle_bar_width
  const HANDLE_BAR_H = 4; // handle_bar_height
  const SIDE_RESERVED = 56; // side_reserved_width
  const CORNER = 16; // corner_radius

  // sheet_host_config의 기본값 (include/luil/ui/sheet_host_element.h)
  const DEFAULTS = { kind: "bottom", side_from_width: null, modal: true, detent: "half", drag_offset: 0, side_width: 360, full_top_margin: 24, bottom_max_width: 640, scrim_opacity: 0.32, fling_velocity: 300 };

  // resolve_sheet_kind
  function resolveSheetKind(c, width) {
    if (c.kind === "bottom" && c.side_from_width != null && fin(width) && width >= c.side_from_width) return "end";
    return c.kind;
  }

  // sheet_detent_height
  function sheetDetentHeight(c, detent, height) {
    const available = cmax(0, finiteOr(height, 0));
    const half = f(available / 2);
    const full = cmax(half, f(available - cmax(0, finiteOr(c.full_top_margin, 0))));
    return detent === "full" ? full : half;
  }

  // sheet_visible_height
  function sheetVisibleHeight(c, height) {
    const base = sheetDetentHeight(c, c.detent, height);
    const full = sheetDetentHeight(c, "full", height);
    return cclamp(f(base - finiteOr(c.drag_offset, 0)), 0, full);
  }

  // settle_sheet — velocity는 초당 논리 픽셀, 아래가 양수
  function settleSheet(c, height, velocity) {
    const v = velocity === undefined ? 0 : velocity;
    const visible = sheetVisibleHeight(c, height);
    const half = sheetDetentHeight(c, "half", height);
    const full = sheetDetentHeight(c, "full", height);
    const fling = finiteOr(c.fling_velocity, 0);
    if (fling > 0 && fin(v) && Math.abs(v) >= fling) {
      if (v > 0) return visible > half ? { detent: "half", dismiss: false } : { detent: "half", dismiss: true };
      return visible < half ? { detent: "half", dismiss: false } : { detent: "full", dismiss: false };
    }
    const toClosed = visible;
    const toHalf = Math.abs(f(visible - half));
    const toFull = Math.abs(f(full - visible));
    if (toClosed < toHalf && toClosed < toFull) return { detent: "half", dismiss: true };
    return { detent: toFull < toHalf ? "full" : "half", dismiss: false };
  }

  // sheet_surface_bounds — slot·결과는 물리 픽셀, scale은 물리 / 논리
  function sheetSurfaceBounds(c, slot, scale) {
    const factor = positiveScale(scale);
    const width = f(slot.width / factor);
    const height = f(slot.height / factor);
    const kind = resolveSheetKind(c, width);
    if (kind === "bottom") {
      const sheetWidth = cmax(0, cmin(width, finiteOr(c.bottom_max_width, width)));
      const visible = sheetVisibleHeight(c, height);
      const w = f(sheetWidth * factor);
      const h = f(visible * factor);
      return { x: f(slot.x + f(f(slot.width - w) / 2)), y: f(f(slot.y + slot.height) - h), width: w, height: h };
    }
    const side = cclamp(finiteOr(c.side_width, 0), 0, cmax(0, f(width - SIDE_RESERVED)));
    const w = f(side * factor);
    if (kind === "end") return { x: f(f(slot.x + slot.width) - w), y: slot.y, width: w, height: slot.height };
    return { x: slot.x, y: slot.y, width: w, height: slot.height };
  }

  // release_action — 놓은 자리에서 낼 메시지 하나 (없으면 null)
  //   { kind: "release", release: { settle, visible_height, target_height, velocity(높아지는 쪽이 양수) } }
  //   { kind: "dismiss", reason: "drag" } | { kind: "settle", detent }
  function releaseAction(c, height, velocity) {
    const settle = settleSheet(c, height, velocity);
    if (c.release) {
      const target = settle.dismiss ? 0 : sheetDetentHeight(c, settle.detent, height);
      const rising = fin(velocity) && velocity !== 0 ? -velocity : 0;
      return { kind: "release", release: { settle, visible_height: sheetVisibleHeight(c, height), target_height: target, velocity: rising } };
    }
    if (settle.dismiss && c.dismiss) return { kind: "dismiss", reason: "drag" };
    if (c.settle) return { kind: "settle", detent: settle.dismiss ? "half" : settle.detent };
    return null;
  }

  // interaction_controller::motion_track — 놓은 순간의 속도를 놓기 전 100ms(motion_window)의 이동으로 잰다
  //  시각의 단위는 ms다 (unitsPerMs로 바꿀 수 있다 — 대조기는 µs로 1000을 준다).
  const MOTION_WINDOW_MS = 100;
  function MotionTrack(unitsPerMs) {
    this.k = unitsPerMs || 1;
    this.samples = [];
  }
  // add: 이동 하나를 더하고 그 시각에서 100ms보다 오래된 표본을 버린다
  MotionTrack.prototype.add = function (time, x, y) {
    const windowUnits = MOTION_WINDOW_MS * this.k;
    this.samples.push({ time, x: f(x), y: f(y) });
    const stale = this.samples.findIndex((s) => time - s.time <= windowUnits);
    this.samples.splice(0, stale < 0 ? this.samples.length : stale);
  };
  // velocity_at: time에서 100ms 안에 남은 표본의 처음과 끝으로 잰 속도 (초당 픽셀). 둘보다 적으면 0
  MotionTrack.prototype.velocityAt = function (time) {
    const windowUnits = MOTION_WINDOW_MS * this.k;
    const fresh = this.samples.findIndex((s) => time - s.time <= windowUnits);
    if (fresh < 0 || this.samples.length - fresh < 2) return { x: 0, y: 0 };
    const first = this.samples[fresh];
    const last = this.samples[this.samples.length - 1];
    // duration<float>(nanoseconds).count() = float(ns) / float(1e9)
    const seconds = f(f((last.time - first.time) * (1e6 / this.k)) / f(1e9));
    if (seconds <= 0) return { x: 0, y: 0 };
    return { x: f(f(last.x - first.x) / seconds), y: f(f(last.y - first.y) / seconds) };
  };

  // 확인 대화 상자의 역할 규칙 (roles: "normal"|"accept"|"reject"|"destructive")
  function lastIndexOf(roles, role) {
    let found = -1;
    roles.forEach((r, i) => {
      if (r === role) found = i;
    });
    return found;
  }
  // message_box_default_index — 마지막 accept (Enter의 대상)
  const messageBoxDefaultIndex = (roles) => lastIndexOf(roles, "accept");
  // message_box_entry_index — 마지막 accept, 없으면 마지막 reject, 그것도 없으면 첫 단추
  function messageBoxEntryIndex(roles) {
    const accept = messageBoxDefaultIndex(roles);
    if (accept >= 0) return accept;
    const reject = lastIndexOf(roles, "reject");
    if (reject >= 0) return reject;
    return roles.length ? 0 : -1;
  }
  // message_box_dismiss_index — 마지막 reject, 없고 단추가 하나면 그 단추
  function messageBoxDismissIndex(roles) {
    const reject = lastIndexOf(roles, "reject");
    if (reject >= 0) return reject;
    return roles.length === 1 ? 0 : -1;
  }
  // message_box_stacks_buttons — 폭의 합 + 8 × (개수 − 1)이 안쪽 폭 + 0.01을 넘으면 쌓는다
  const BUTTON_GAP = 8; // button_gap
  const FIT_TOLERANCE = f(0.01); // fit_tolerance
  function messageBoxStacksButtons(widths, innerWidth) {
    if (!widths.length) return false;
    let sum = f(f(widths.length - 1) * BUTTON_GAP);
    for (const w of widths) sum = f(sum + f(w));
    return sum > f(f(innerWidth) + FIT_TOLERANCE);
  }

  const api = {
    DEFAULTS,
    HANDLE_STRIP,
    HANDLE_BAR_W,
    HANDLE_BAR_H,
    SIDE_RESERVED,
    CORNER,
    MOTION_WINDOW_MS,
    resolveSheetKind,
    sheetDetentHeight,
    sheetVisibleHeight,
    settleSheet,
    sheetSurfaceBounds,
    releaseAction,
    MotionTrack,
    messageBoxDefaultIndex,
    messageBoxEntryIndex,
    messageBoxDismissIndex,
    messageBoxStacksButtons,
  };
  if (typeof window !== "undefined") window.LuilDialogs = api;
  if (typeof module !== "undefined") module.exports = api;
})();

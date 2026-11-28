/* luil 설명서 — 토스트 쌓음의 시간·자리 규칙을 JS로 옮긴 것
 *
 * src/ui/toast_element.cpp의 toast_element::opacity_at·next_update, toast_stack_element의 생성자(개수 상한)와
 * arrange(자리)를 함수 단위로 옮겼다. 시각은 ms다 (std::chrono::steady_clock에 해당). float 연산은 Math.fround로
 * 원본과 같은 단정밀도로 맞췄다. 원본 줄을 잘라 컴파일한 대조기의 무작위 입력과 견주어 같음을 확인했다.
 *
 *   toast { shown_at(ms), duration(ms) }   stack config { corner, maximum_count, spacing, margin, width }
 */
(function () {
  "use strict";
  const f = Math.fround;

  const TOAST_HEIGHT = 40; // toast_height
  const ACTION_WIDTH = 64; // toast_action_width
  const FADE_MS = 300; // toast_fade
  const DEFAULTS = { corner: "bottom_right", maximum_count: 3, spacing: 8, margin: 16, width: 320 }; // toast_stack_config

  // opacity_at — 남은 시간이 300ms 아래로 내려가면 비례해 줄고, 끝나면 0. duration 0은 늘 1
  //   unitsPerMs: 시각의 단위 (대조기는 µs로 1000을 준다)
  function opacityAt(toast, now, unitsPerMs) {
    const k = unitsPerMs || 1;
    if (toast.duration <= 0) return 1;
    const remaining = toast.shown_at + toast.duration * k - now;
    if (remaining <= 0) return 0;
    if (remaining >= FADE_MS * k) return 1;
    // duration<float, milli>(ns).count() / duration<float, milli>(300ms).count()
    return f(f(f(remaining * (1e6 / k)) / f(1e6)) / f(FADE_MS));
  }

  // next_update — 흐려지기 시작할 때까지 쉬고, 흐려지는 동안은 "지금"(계속 그림), 끝나면 null
  function nextUpdate(toast, now, unitsPerMs) {
    const k = unitsPerMs || 1;
    if (toast.duration <= 0) return null;
    const fadeBegin = toast.shown_at + toast.duration * k - FADE_MS * k;
    if (now < fadeBegin) return fadeBegin;
    if (now < toast.shown_at + toast.duration * k) return now;
    return null;
  }

  // toast_stack_element 생성자 — 목록의 앞에서부터 maximum_count장까지만 element가 된다
  const builtCount = (cfg, given) => (given < cfg.maximum_count ? given : cfg.maximum_count);

  // toast_stack_element::arrange — 한 장마다 { x, y, width, height } (물리 픽셀). slot·scale은 배치 문맥
  function stackRects(cfg, count, slot, scale) {
    const s = scale > 0 ? scale : 1;
    const width = f(cfg.width * s);
    const height = f(TOAST_HEIGHT * s);
    const margin = f(cfg.margin * s);
    const stride = f(f(TOAST_HEIGHT + cfg.spacing) * s);
    const atLeft = cfg.corner === "top_left" || cfg.corner === "bottom_left";
    const atTop = cfg.corner === "top_left" || cfg.corner === "top_right";
    const x = atLeft ? f(slot.x + margin) : f(f(f(slot.x + slot.width) - margin) - width);
    const rects = [];
    for (let index = 0; index < count; index++) {
      const offset = f(margin + f(f(index) * stride));
      const y = atTop ? f(slot.y + offset) : f(f(f(slot.y + slot.height) - offset) - height);
      rects.push({ x, y, width, height });
    }
    return rects;
  }

  // toast_element::arrange·draw의 안쪽 자리 (그림 재현용, 논리 픽셀): 동작 단추 64×24·오른쪽 안쪽 8, 글리프 칸 16·왼쪽 10, 글은 글리프 뒤 8
  function innerLayout(rect, hasAction) {
    const button = hasAction ? { x: rect.x + rect.width - 8 - ACTION_WIDTH, y: rect.y + (rect.height - 24) / 2, width: ACTION_WIDTH, height: 24 } : null;
    const textLeft = rect.x + 10 + 16 + 8;
    const reserved = hasAction ? ACTION_WIDTH + 16 : 10;
    return { glyph: { x: rect.x + 10, y: rect.y, width: 16, height: rect.height }, textLeft, textWidth: rect.x + rect.width - reserved - textLeft, button };
  }

  const api = { TOAST_HEIGHT, ACTION_WIDTH, FADE_MS, DEFAULTS, opacityAt, nextUpdate, builtCount, stackRects, innerLayout };
  if (typeof window !== "undefined") window.LuilToasts = api;
  if (typeof module !== "undefined") module.exports = api;
})();

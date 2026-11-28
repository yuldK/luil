/* caption 비클라이언트 판정을 옮긴 함수들이다 (창과 셸 통합 문서의 실습이 쓴다).
 *
 * 원본 (기준 커밋 563a855):
 *   src/host/caption_layout.cpp   scale_for_dpi · default_caption_band · caption_band_in · make_caption_layout · hit_test_caption
 *   src/win32/window_mode.cpp     hit_test_window (가장자리 → 시스템 메뉴 칸 → 버튼 → 끌기 → client)
 *   src/win32/caption_surface.cpp caption_hit_test의 전체 화면 갈래와 WM_NCHITTEST의 client 뒤집기
 *   src/ui/ui_tree.cpp            caption_client_under · intersect (ui_tree::caption_client_at)
 *   src/ui/ui_element.cpp         rect_f::contains
 *   src/ui/caption_element.cpp    caption_element::arrange (그린 단추의 자리)
 *
 * 대조: make_caption_layout · hit_test_caption · hit_test_window · default_caption_band는 원본 .cpp 두 개를
 * 손대지 않고 컴파일한 대조기에 무작위 입력을 넣어 이 함수들과 견주었다. caption_band_in의 반올림,
 * caption_client_at, caption_element::arrange는 원본을 읽어 옮기고 tests/caption_layout_tests.cpp의 기준값에만 맞추었다
 * (그 셋은 Skia에 기대는 element 계층 위에 있어 따로 컴파일하지 않았다).
 * DwmDefWindowProc의 답(caption_surface.cpp)은 코드로 알 수 없어 옮기지 않았다.
 *
 * 정수 식은 원본처럼 버림 나눗셈이고, 실수(rect_f) 식은 Math.fround로 float을 흉내 낸다.
 */
(function () {
  "use strict";

  const f32 = Math.fround;

  // caption_layout.cpp:11-14, window_mode.cpp:10-13 — (값 × dpi + 48) / 96, 부호 없는 정수 나눗셈.
  // 원본은 값을 uint64로 바꾼다. 음수 치수는 실습이 내지 않으므로 옮기지 않았다.
  function scaleForDpi(value, dpi) {
    return Math.floor((value * dpi + 48) / 96);
  }

  // std::lround — 반은 0에서 먼 쪽으로 올린다 (Math.round와 다르다).
  function lround(v) {
    const a = Math.abs(v);
    let r = Math.floor(a);
    if (a - r >= 0.5) r += 1;
    return v < 0 ? -r : r;
  }

  // caption_layout.cpp:33-36
  function defaultCaptionBand(clientWidth, dpi, metrics) {
    return { left: 0, top: 0, width: Math.max(0, clientWidth), height: scaleForDpi(metrics.height, dpi) };
  }

  // caption_layout.cpp:18-30 — 그리기 순서(전위)의 첫 caption. 숨은 가지도 내려간다.
  function findCaption(node, hiddenAbove) {
    const nodeHidden = hiddenAbove || node.visible === false;
    if (node.caption) return { node, hidden: nodeHidden };
    for (const child of node.children || []) {
      const found = findCaption(child, nodeHidden);
      if (found) return found;
    }
    return null;
  }

  // caption_layout.cpp:38-56 — 없으면 맨 위 띠, 숨었으면 빈 띠, 아니면 네 변을 각각 반올림한다.
  function captionBandIn(tree, clientWidth, dpi, metrics) {
    const found = tree ? findCaption(tree, false) : null;
    if (!found) return defaultCaptionBand(clientWidth, dpi, metrics);
    if (found.hidden) return { left: 0, top: 0, width: 0, height: 0 };
    const b = found.node.bounds;
    const left = lround(b.x);
    const top = lround(b.y);
    const right = lround(f32(b.x + b.width));
    const bottom = lround(f32(b.y + b.height));
    return { left, top, width: Math.max(0, right - left), height: Math.max(0, bottom - top) };
  }

  // caption_layout.cpp:58-79 — 있는 버튼만 오른쪽부터 한 칸씩 뗀다. 없는 버튼은 그 시점의 끝이다.
  function makeCaptionLayout(band, dpi, metrics, buttons) {
    const layout = { left: band.left, top: band.top, height: Math.max(0, band.height), buttonWidth: scaleForDpi(metrics.buttonWidth, dpi), right: band.left + Math.max(0, band.width) };
    let edge = layout.right;
    const take = (present) => {
      if (present) edge = Math.max(layout.left, edge - layout.buttonWidth);
      return edge;
    };
    layout.closeLeft = take(buttons.close);
    layout.maximizeLeft = take(buttons.maximize);
    layout.minimizeLeft = take(buttons.minimize);
    return layout;
  }

  // caption_layout.cpp:86-97
  function hitTestCaption(layout, x, y) {
    if (x < layout.left || x >= layout.right || y < layout.top || y >= layout.top + layout.height) return "client";
    if (x >= layout.closeLeft) return "close";
    if (x >= layout.maximizeLeft) return "maximize";
    if (x >= layout.minimizeLeft) return "minimize";
    return "drag";
  }

  // window_mode.cpp:16-73. mode는 "normal" · "maximized" · "fullscreen".
  // frame = { caption: { height, buttonWidth, iconSlot }, buttons: { minimize, maximize, close }, border, corner }
  function hitTestWindow(frame, mode, width, height, dpi, x, y, band) {
    if (mode === "fullscreen") return "client";
    if (mode === "normal") {
      const border = Math.max(1, scaleForDpi(frame.border, dpi));
      const corner = Math.max(border, scaleForDpi(frame.corner, dpi));
      const cornerLeft = x < corner;
      const cornerRight = x >= width - corner;
      const cornerTop = y < corner;
      const cornerBottom = y >= height - corner;
      if (cornerTop && cornerLeft) return "resize_top_left";
      if (cornerTop && cornerRight) return "resize_top_right";
      if (cornerBottom && cornerLeft) return "resize_bottom_left";
      if (cornerBottom && cornerRight) return "resize_bottom_right";
      if (x < border) return "resize_left";
      if (x >= width - border) return "resize_right";
      if (y < border) return "resize_top";
      if (y >= height - border) return "resize_bottom";
    }
    const layout = makeCaptionLayout(band, dpi, frame.caption, frame.buttons);
    const hit = hitTestCaption(layout, x, y);
    if (hit !== "client" && x < layout.left + scaleForDpi(frame.caption.iconSlot, dpi)) return "system_menu";
    switch (hit) {
      case "drag": return "caption_drag";
      case "minimize": return "minimize_button";
      case "maximize": return "maximize_button";
      case "close": return "close_button";
      default: return "client";
    }
  }

  // ui_element.cpp:39-42 — 오른쪽·아래 끝은 포함하지 않는다. 비교는 float이다.
  function contains(r, px, py) {
    return px >= r.x && px < f32(r.x + r.width) && py >= r.y && py < f32(r.y + r.height);
  }

  // ui_tree.cpp:583-592
  function intersect(a, b) {
    const left = Math.max(a.x, b.x);
    const top = Math.max(a.y, b.y);
    const right = Math.min(f32(a.x + a.width), f32(b.x + b.width));
    const bottom = Math.min(f32(a.y + a.height), f32(b.y + b.height));
    if (right <= left || bottom <= top) return null;
    return { x: left, y: top, width: f32(right - left), height: f32(bottom - top) };
  }

  // ui_tree.cpp:623-645 — 숨은 가지는 자리를 내지 않고, 자르는 조상 안으로 자른다. 비활성은 보지 않는다.
  function captionClientUnder(node, x, y, clip) {
    if (node.visible === false) return false;
    if (node.captionClient) {
      const shown = clip ? intersect(node.bounds, clip) : node.bounds;
      if (shown && contains(shown, x, y)) return true;
    }
    let nextClip = clip;
    if (node.clipChildren) {
      nextClip = clip ? intersect(clip, node.bounds) : node.bounds;
      if (!nextClip) return false;
    }
    for (const child of node.children || []) if (captionClientUnder(child, x, y, nextClip)) return true;
    return false;
  }

  // ui_tree.cpp:648-651, caption_surface.cpp:87-91 — 정수 점을 float으로 바꿔 묻는다.
  function captionClientAt(tree, x, y) {
    return !!tree && captionClientUnder(tree, f32(x), f32(y), null);
  }

  // caption_surface.cpp:438-448 — HTCAPTION 자리에 client 칸이 보이면 HTCLIENT다. 그 밖의 답은 그대로다.
  function nonClientHit(frame, mode, width, height, dpi, x, y, tree) {
    const band = captionBandIn(tree, width, dpi, frame.caption);
    const hit = hitTestWindow(frame, mode, width, height, dpi, x, y, band);
    if (hit === "caption_drag" && captionClientAt(tree, x, y)) return { hit: "client", flipped: true, band };
    return { hit, flipped: false, band };
  }

  // caption_element.cpp:135-151 — 높이는 height_for × 배율, 단추는 오른쪽 끝에서 닫기 → 최대화 → 최소화 → 앱 단추(거꾸로) 순.
  // 판정과 달리 왼쪽 끝에서 멈추지 않고, 폭은 실수 (버튼 폭 × 배율)이다.
  function arrangeCaption(slot, scale, metrics, buttons, appButtons) {
    const s = scale > 0 ? f32(scale) : 1;
    const captionHeight = f32(metrics.height * s);
    const bounds = { x: f32(slot.x), y: f32(slot.y), width: f32(slot.width), height: captionHeight };
    const buttonWidth = f32(metrics.buttonWidth * s);
    let edge = f32(bounds.x + bounds.width);
    const placed = {};
    const place = (key) => {
      edge = f32(edge - buttonWidth);
      placed[key] = { x: edge, y: bounds.y, width: buttonWidth, height: captionHeight };
    };
    if (buttons.close) place("close");
    if (buttons.maximize) place("maximize");
    if (buttons.minimize) place("minimize");
    const apps = [];
    for (let i = appButtons.length - 1; i >= 0; i--) {
      edge = f32(edge - buttonWidth);
      apps[i] = { key: appButtons[i].key, enabled: appButtons[i].enabled !== false, bounds: { x: edge, y: bounds.y, width: buttonWidth, height: captionHeight } };
    }
    return { bounds, buttons: placed, apps };
  }

  window.LuilCaptionHit = { scaleForDpi, lround, defaultCaptionBand, captionBandIn, makeCaptionLayout, hitTestCaption, hitTestWindow, contains, intersect, captionClientAt, nonClientHit, arrangeCaption };
})();

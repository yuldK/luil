/* luil 설명서 — 탭 막대 실습
 *
 * tab_bar_element의 배치와 놓는 자리, 그리고 데모 앱의 처리를 JS로 옮겼다. 이름 옆 주석은 C++ 원본이다.
 *  - 대조한 것 (원본 줄을 그대로 오려 컴파일한 대조기와 무작위 입력으로 견줬다):
 *      fitted_tab_width·tab_step_offset (src/ui/tab_bar_element.cpp:900-937), clamp_scroll (layout_metrics.h:118-124),
 *      tab_bar_element::arrange (tab_bar_element.cpp:1113-1182)와 띠·레인의 배치 (strip_element.cpp:45-57·77-92,
 *      tab_bar_element.cpp:790-805), step_delta (1108), side_at·changes_order (74-78·94-103),
 *      데스크톱 데모의 이동·닫기·드러내기 (examples/demo/tabs_page.cpp:174-195·408-423·497-511),
 *      모바일 데모의 순서 바꾸기 (examples/mobile_demo/tabs_page.cpp:67-84).
 *  - 놓는 자리의 조합(탭이 먼저 답하고, 거절되면 막대의 끝 틈 — tab_bar_element.cpp:355-362·1059-1063·1076-1085)은
 *    원본을 읽고 옮겼고, 대조기에도 같은 조합을 손으로 써서 견줬다.
 *  - 읽고만 옮긴 것: 메뉴 명령(다음 탭·왼쪽/오른쪽으로 이동, examples/demo/tabs_page.cpp:527-556), 끌기 문턱 6,
 *    끄는 동안 Esc 취소, 가운데 버튼 닫기.
 * C++의 float 계산은 Math.fround로 한 연산씩 맞춘다. 배율은 1이다 (논리 픽셀 = CSS 픽셀).
 */
(function () {
  "use strict";

  const f = Math.fround;
  const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v); // std::clamp
  const max = (a, b) => (a < b ? b : a); // std::max
  const min = (a, b) => (b < a ? b : a); // std::min

  const TAB_OVERFLOW_WIDTH = 28; // tab_overflow_width
  const DRAG_START_DISTANCE = 6; // interaction_config::drag_start_distance

  // clamp_scroll(content_length, viewport_length, offset)
  function clampScroll(content, viewport, offset) {
    const maximum = content > viewport ? f(content - viewport) : 0;
    if (offset < 0) return 0;
    return offset > maximum ? maximum : offset;
  }

  // fitted_tab_width(count, tab_width, min_tab_width, available)
  function fittedTabWidth(count, tabWidth, minTabWidth, available) {
    if (count === 0 || tabWidth <= 0) return tabWidth;
    const floor = minTabWidth > 0 && minTabWidth < tabWidth ? minTabWidth : tabWidth;
    return clamp(f(available / f(count)), floor, tabWidth);
  }

  // tab_step_offset(offset, tab_width, viewport, maximum, steps)
  function tabStepOffset(offset, tabWidth, viewport, maximum, steps) {
    const upper = max(0, maximum);
    let target = clamp(offset, 0, upper);
    if (tabWidth <= 0 || steps === 0) return target;
    const epsilon = 0.5;
    const count = steps > 0 ? steps : -steps;
    for (let step = 0; step < count; ++step) {
      let next = target;
      if (steps > 0) {
        const index = Math.floor(f(f(f(target + viewport) + epsilon) / tabWidth));
        next = min(upper, max(target, f(f(f(index + 1) * tabWidth) - viewport)));
      } else {
        const index = f(Math.ceil(f(f(target - epsilon) / tabWidth)) - 1);
        next = max(0, min(target, f(index * tabWidth)));
      }
      if (next === target) break;
      target = next;
    }
    return target;
  }

  // tab_bar_element::arrange + strip_element::arrange + 레인의 arrange
  //   c: { count, tabWidth, minTabWidth, total, scale, trailing, trailingWidth, overflow, steps, scrollOffset, x0 }
  //   steps는 "앞·뒤 단추가 지어졌다"(overflow_mode가 step이고 scroll factory가 있다)는 뜻이다.
  function arrange(c) {
    const scale = c.scale > 0 ? c.scale : 1; // positive_scale
    const x0 = c.x0 || 0;
    const count = c.count;
    const total = c.total;
    const trailingWidth = c.trailing ? f(max(0, c.trailingWidth) * scale) : 0;
    const smallest = fittedTabWidth(count, c.tabWidth, c.minTabWidth, 0);
    const overflowing = f(f(f(f(count) * smallest) * scale) + trailingWidth) > total;
    const tabWidth = overflowing ? smallest : fittedTabWidth(count, c.tabWidth, c.minTabWidth, f(f(total - trailingWidth) / scale));
    const laneWidth = f(f(f(count) * tabWidth) * scale);

    let left = x0;
    let right = f(x0 + total);
    const buttonWidth = f(TAB_OVERFLOW_WIDTH * scale);
    const out = { overflowing, tabWidth, overflowButton: null, back: null, forward: null, trailing: null };
    if (overflowing && c.trailing) {
      right = f(right - trailingWidth);
      out.trailing = { x: right, width: trailingWidth };
    }
    if (c.overflow && overflowing) {
      right = f(right - buttonWidth);
      out.overflowButton = { x: right, width: buttonWidth };
    }
    const stepping = overflowing && c.steps;
    let forwardX = right;
    if (stepping) {
      right = f(right - buttonWidth);
      forwardX = right;
      left = f(left + buttonWidth);
    }

    // 띠 (strip_element::arrange). 처음 값은 앱의 scroll_offset 그대로다 (tab_bar_element.cpp:992).
    const contentWidth = f(f(count) * tabWidth);
    const strip = { x: left, width: max(0, f(right - left)) };
    const viewportWidth = f(strip.width / scale);
    const maximum = clampScroll(contentWidth, viewportWidth, contentWidth);
    const offset = clampScroll(contentWidth, viewportWidth, c.scrollOffset);
    const laneX = f(strip.x - f(offset * scale));
    // 레인: 탭을 같은 폭으로 맞붙인다
    const width = f(tabWidth * scale);
    const tabs = [];
    let x = laneX;
    for (let i = 0; i < count; ++i) {
      tabs.push({ x, width });
      x = f(x + width);
    }
    out.strip = strip;
    out.tabs = tabs;
    out.viewport = f(max(0, f(right - left)) / scale);
    out.maximum = maximum;
    out.offset = offset;
    if (stepping) {
      out.back = { x: x0, width: buttonWidth, enabled: offset > 0 };
      out.forward = { x: forwardX, width: buttonWidth, enabled: offset < maximum };
    }
    if (!overflowing && c.trailing) out.trailing = { x: f(x0 + min(laneWidth, max(0, f(total - trailingWidth)))), width: trailingWidth };
    out.laneWidth = laneWidth;
    return out;
  }

  // tab_bar_element::step_delta(steps)
  const stepDelta = (layout, steps) => f(tabStepOffset(layout.offset, layout.tabWidth, layout.viewport, layout.maximum, steps) - layout.offset);

  // side_at(box, x)
  const sideAt = (box, x) => (x < f(box.x + f(box.width / 2)) ? "before" : "after");

  // changes_order(order, moved, slot)
  function changesOrder(order, moved, slot) {
    const from = order.indexOf(moved);
    if (from < 0) return false;
    return slot !== from && slot !== from + 1;
  }

  // 놓는 자리: 띠 안의 탭이 먼저 답하고(accepts_at), 거절되면 막대의 끝 틈이다 (move가 있을 때만).
  //   답: null(받는 곳 없음) | { kind: "tab"|"end", index, side }
  function dropAt(layout, order, moved, moving, x, y, height) {
    const s = layout.strip;
    const inStrip = x >= s.x && y >= 0 && x < f(s.x + s.width) && y < height; // rect_f::contains
    if (inStrip) {
      for (let i = 0; i < layout.tabs.length; ++i) {
        const box = layout.tabs[i];
        if (!(x >= box.x && x < f(box.x + box.width))) continue;
        const side = sideAt(box, x);
        const ok = moving ? changesOrder(order, moved, i + (side === "after" ? 1 : 0)) : moved !== order[i];
        if (ok) return { kind: "tab", index: i, side };
        break;
      }
    }
    if (moving) {
      const last = layout.tabs[layout.tabs.length - 1];
      const endX = last ? f(last.x + last.width) : s.x; // end_gap_x
      if (inStrip && x >= endX && changesOrder(order, moved, order.length)) return { kind: "end", index: order.length - 1, side: "after" };
    }
    return null;
  }

  // 데스크톱 데모의 reveal_selected — 고른 탭을 레인 안으로 들이는 앱의 식이다.
  function revealSelected(keys, selected, tabWidth, laneWidth, scroll) {
    if (laneWidth <= 0) return scroll;
    const index = keys.indexOf(selected);
    if (index < 0) return 0;
    const left = f(f(index) * tabWidth);
    if (left < scroll) return left;
    if (f(left + tabWidth) > f(scroll + laneWidth)) return f(f(left + tabWidth) - laneWidth);
    return scroll;
  }

  // 데스크톱 데모의 이동 처리 (tab_reorder_intent { moved, target, side }). 답: { keys, revealed }
  function demoMove(keys, moved, target, side) {
    if (moved === target) return { keys: keys.slice(), revealed: false };
    const from = keys.indexOf(moved);
    if (from < 0) return { keys: keys.slice(), revealed: false };
    const next = keys.slice();
    next.splice(from, 1);
    let at = next.indexOf(target);
    if (at < 0) at = next.length;
    else if (side === "after") at += 1;
    next.splice(at, 0, moved);
    return { keys: next, revealed: true };
  }

  // 모바일 데모의 순서 바꾸기 (tab_reorder_intent { moved, target }) — 대상의 앞에 끼운다.
  function mobileReorder(keys, moved, target) {
    if (moved === target) return keys.slice();
    const from = keys.indexOf(moved);
    if (from < 0) return keys.slice();
    const next = keys.slice();
    next.splice(from, 1);
    let at = next.indexOf(target);
    if (at < 0) at = next.length;
    next.splice(at, 0, moved);
    return next;
  }

  // 데스크톱 데모의 close_tab — 오른쪽 이웃, 없으면 마지막. 답: { keys, selected, found }
  function closeTab(keys, selected, key) {
    const at = keys.indexOf(key);
    if (at < 0) return { keys: keys.slice(), selected, found: false };
    const wasSelected = selected === key;
    const next = keys.slice();
    next.splice(at, 1);
    let sel = selected;
    if (wasSelected && next.length) sel = next[at < next.length ? at : next.length - 1];
    if (!next.length) sel = "";
    return { keys: next, selected: sel, found: true };
  }

  const model = { clampScroll, fittedTabWidth, tabStepOffset, arrange, stepDelta, sideAt, changesOrder, dropAt, revealSelected, demoMove, mobileReorder, closeTab, TAB_OVERFLOW_WIDTH, DRAG_START_DISTANCE };

  /* ---------------- 화면 ---------------- */

  const HEIGHT = 36; // 데모의 tab_bar_height
  const TRAILING_WIDTH = 32; // 데스크톱 데모의 new_tab_width

  function setup(root) {
    const LU = window.LU;
    const { el, icon } = LU;
    const q = (k) => root.querySelector(`[data-tb="${k}"]`);
    const stage = q("stage");
    const logBox = q("log");
    const readout = q("readout");
    const status = q("status");
    const orderBox = q("order");

    const fresh = () => ({
      keys: ["d1", "d2", "d3", "d4", "d5", "d6"],
      names: { d1: "문서 1", d2: "문서 2", d3: "문서 3", d4: "문서 4", d5: "문서 5", d6: "문서 6" },
      nextId: 7,
      selected: "d1",
      scroll: 0,
      tabWidth: 140,
      minTabWidth: 0,
      barWidth: 520,
      mode: "scroll",
      overflow: true,
      trailing: true,
      drop: "move",
    });
    let s = fresh();
    let layout = null;
    let drag = null; // { key, x0, y0, live, hit }
    let menu = null; // { kind: "overflow"|"context", key }

    const ctl = {};
    for (const k of ["count", "tw", "mtw", "bw", "off"]) ctl[k] = { input: q(k), out: q(k + "-out") };

    function log(input, message, effect) {
      const entry = el("div", { class: "tb-entry" }, el("div", { class: "in", text: input }), message ? el("div", { class: "msg", text: message }) : null, effect ? el("div", { class: "eff", text: effect }) : null);
      logBox.prepend(entry);
      while (logBox.children.length > 40) logBox.lastChild.remove();
    }
    const name = (k) => s.names[k] || k;
    // 받침에 따라 을/를 — 이름은 "문서 N"이라 끝 숫자의 읽기로 고른다 (2·4·5·9는 받침이 없다)
    const obj = (k) => {
      const n = name(k);
      const last = n.slice(-1);
      return n + ("2459".includes(last) ? "를" : "을");
    };
    const orderText = (keys) => "[" + keys.map(name).join(", ") + "]";

    function maxBarWidth() {
      return Math.max(160, Math.min(720, Math.floor(stage.clientWidth || 520)));
    }

    function computeLayout() {
      return arrange({ count: s.keys.length, tabWidth: s.tabWidth, minTabWidth: s.minTabWidth, total: s.barWidth, scale: 1, trailing: s.trailing, trailingWidth: TRAILING_WIDTH, overflow: s.overflow, steps: s.mode === "step", scrollOffset: s.scroll, x0: 0 });
    }

    // 앱이 다음 frame에 쓸 값을 되읽는다 (scroll_offset() — 배치가 다듬은 값)
    function settle() {
      layout = computeLayout();
      s.scroll = layout.offset;
    }

    function reveal() {
      const before = s.scroll;
      s.scroll = revealSelected(s.keys, s.selected, layout.tabWidth, layout.viewport, s.scroll);
      return before !== s.scroll;
    }

    function render() {
      settle();
      const L = layout;
      stage.replaceChildren();
      const bar = el("div", { class: "tb-bar", style: { width: s.barWidth + "px", height: HEIGHT + "px" } });
      const strip = el("div", { class: "tb-strip", style: { left: L.strip.x + "px", width: L.strip.width + "px" } });
      L.tabs.forEach((box, i) => {
        const key = s.keys[i];
        const tab = el(
          "div",
          { class: "tb-tab" + (key === s.selected ? " sel" : "") + (drag && drag.live && drag.key === key ? " dragging" : ""), style: { left: box.x - L.strip.x + "px", width: box.width + "px" }, dataset: { key } },
          el("span", { class: "tb-name", text: name(key) }),
          el("span", { class: "tb-x", title: "닫기 (포인터만)", dataset: { close: key } }, icon("EA76")),
        );
        if (drag && drag.live && drag.hit && !drag.moving && drag.hit.kind === "tab" && drag.hit.index === i) tab.classList.add("target");
        strip.append(tab);
      });
      bar.append(strip);
      // 받는 틈의 세로 막대 (move): 탭의 왼쪽·오른쪽 끝, 끝 틈은 띠 안으로 자른다
      if (drag && drag.live && drag.hit && drag.moving) {
        const h = drag.hit;
        const box = L.tabs[h.index];
        let x = h.side === "before" ? box.x : box.x + box.width;
        if (h.kind === "end") x = clamp(x, L.strip.x, Math.max(L.strip.x, L.strip.x + L.strip.width - 2));
        bar.append(el("div", { class: "tb-gap", style: { left: x + "px" } }));
      }
      const button = (cls, box, glyph, label, act, enabled) =>
        el("button", { type: "button", class: "tb-btn " + cls, style: { left: box.x + "px", width: box.width + "px" }, title: label, "aria-label": label, disabled: enabled === false, dataset: { act } }, glyph.length > 1 ? icon(glyph) : glyph);
      if (L.back) bar.append(button("", L.back, "EAB5", "앞 탭 보기", "back", L.back.enabled));
      if (L.forward) bar.append(button("", L.forward, "EAB6", "뒤 탭 보기", "forward", L.forward.enabled));
      if (L.overflowButton) bar.append(button("", L.overflowButton, "⋯", "넘친 탭 보기", "overflow", true));
      if (L.trailing) bar.append(button("", L.trailing, "EA60", "새 탭", "new", true));
      stage.append(bar);
      if (menu) stage.append(renderMenu());

      readout.replaceChildren(
        ...[
          ["tab_width()", fmt(L.tabWidth)],
          ["overflowing()", L.overflowing ? "참" : "거짓"],
          ["띠 폭", fmt(L.viewport)],
          ["maximum_scroll()", fmt(L.maximum)],
          ["scroll_offset()", fmt(L.offset)],
        ].map(([k, v]) => el("div", {}, el("span", { class: "k", text: k }), el("span", { class: "v", text: v }))),
      );
      orderBox.textContent = "순서 " + orderText(s.keys) + " · 고른 탭 " + (s.selected ? name(s.selected) : "없음");
      // 조절기
      ctl.count.out.textContent = String(s.keys.length);
      ctl.tw.out.textContent = String(s.tabWidth);
      ctl.mtw.out.textContent = String(s.minTabWidth);
      ctl.bw.input.max = String(maxBarWidth());
      ctl.bw.out.textContent = String(s.barWidth);
      ctl.off.input.max = String(Math.max(0, Math.round(L.maximum)));
      ctl.off.input.value = String(Math.round(L.offset));
      ctl.off.input.disabled = L.maximum <= 0;
      ctl.off.out.textContent = fmt(L.offset);
      root.querySelectorAll("[data-seg] button").forEach((b) => b.setAttribute("aria-pressed", String(s[b.parentElement.dataset.seg] === b.dataset.v)));
      q("chk-overflow").checked = s.overflow;
      q("chk-trailing").checked = s.trailing;
      q("cmd-close").disabled = !s.selected;
      const at = s.keys.indexOf(s.selected);
      q("cmd-left").disabled = at <= 0;
      q("cmd-right").disabled = at < 0 || at + 1 >= s.keys.length;
      q("cmd-next").disabled = s.keys.length < 2;
    }

    const fmt = (v) => (Math.round(v * 100) / 100).toString();

    function renderMenu() {
      // 항목을 누르면 menu를 먼저 비우므로 키는 여기서 잡아 둔다.
      const key = menu.key;
      const items =
        menu.kind === "overflow"
          ? s.keys.map((k) => ({ label: name(k) + (k === s.selected ? " (고름)" : ""), run: () => selectKey(k, "넘침 메뉴에서 " + name(k)) }))
          : [
              { label: "닫기", run: () => command("close", key) },
              { label: "왼쪽으로 이동", run: () => command("move-left", key), disabled: s.keys.indexOf(key) <= 0 },
              { label: "오른쪽으로 이동", run: () => command("move-right", key), disabled: s.keys.indexOf(key) + 1 >= s.keys.length },
            ];
      const box = el("div", { class: "tb-menu", role: "menu", style: { left: Math.max(0, Math.min(menu.x, s.barWidth - 150)) + "px", top: HEIGHT + 4 + "px" } });
      items.forEach((it) => {
        box.append(el("button", { type: "button", role: "menuitem", disabled: !!it.disabled, text: it.label, onclick: () => { menu = null; it.run(); } }));
      });
      // 처음 열 때만 첫 항목에 초점을 준다 (다시 그릴 때마다 초점을 빼앗지 않는다).
      if (!menu.focused) {
        menu.focused = true;
        queueMicrotask(() => { const first = box.querySelector("button:not([disabled])"); if (first) first.focus(); });
      }
      return box;
    }

    function selectKey(key, input) {
      const was = s.selected;
      s.selected = key;
      settle();
      const moved = reveal();
      log(input, `select("${key}")`, was === key ? "이미 고른 탭 — 앱 상태 그대로" : `selected = ${name(key)}` + (moved ? ` · 드러내려고 scroll_offset = ${fmt(s.scroll)}` : ""));
      render();
    }

    function closeKey(key, input, message) {
      const r = closeTab(s.keys, s.selected, key);
      s.keys = r.keys;
      const was = s.selected;
      s.selected = r.selected;
      settle();
      if (r.found) reveal();
      log(input, message, r.found ? `${obj(key)} 뺐다` + (was !== s.selected ? ` · 고른 탭 → ${s.selected ? name(s.selected) : "없음"}` : "") : "없는 탭 — 그대로");
      render();
    }

    // 데스크톱 데모의 메뉴 명령 (읽고 옮김)
    function command(cmd, key) {
      const at = s.keys.indexOf(key);
      if (cmd === "close") return closeKey(key, `메뉴 "닫기" (${name(key)})`, `tab_menu_command_intent { "close" }`);
      if (at < 0) return;
      const keys = s.keys.slice();
      let effect = "";
      if (cmd === "move-left" && at > 0) { [keys[at - 1], keys[at]] = [keys[at], keys[at - 1]]; effect = "왼쪽 이웃과 맞바꿨다"; }
      if (cmd === "move-right" && at + 1 < keys.length) { [keys[at + 1], keys[at]] = [keys[at], keys[at + 1]]; effect = "오른쪽 이웃과 맞바꿨다"; }
      if (cmd === "next") { s.selected = keys[(at + 1) % keys.length]; effect = `selected = ${name(s.selected)}`; }
      s.keys = keys;
      settle();
      reveal();
      log({ "move-left": "메뉴 \"왼쪽으로 이동\"", "move-right": "메뉴 \"오른쪽으로 이동\"", next: "Ctrl+Tab (앱 명령 \"다음 탭\")" }[cmd] + ` (${name(key)})`, `tab_menu_command_intent { "${cmd}" }`, effect + " → " + orderText(s.keys));
      render();
    }

    /* ---------- 포인터 ---------- */

    const barPoint = (ev) => {
      const bar = stage.querySelector(".tb-bar");
      const r = bar.getBoundingClientRect();
      return { x: ev.clientX - r.left, y: ev.clientY - r.top };
    };

    stage.addEventListener("pointerdown", (ev) => {
      const tab = ev.target.closest(".tb-tab");
      if (!tab) return;
      const key = tab.dataset.key;
      const closer = ev.target.closest("[data-close]");
      if (ev.button === 1) {
        ev.preventDefault();
        drag = { key, middle: true, closer: !!closer, ...barPoint(ev) };
        return;
      }
      if (ev.button !== 0) return;
      menu = null;
      drag = { key, closer: !!closer, start: barPoint(ev), live: false, hit: null, moving: s.drop === "move" };
      stage.setPointerCapture(ev.pointerId);
    });

    stage.addEventListener("pointermove", (ev) => {
      if (!drag || drag.middle) return;
      const p = barPoint(ev);
      const starting = !drag.live;
      if (starting) {
        if (drag.closer || Math.hypot(p.x - drag.start.x, p.y - drag.start.y) <= DRAG_START_DISTANCE) return;
        drag.live = true;
      }
      const hit = dropAt(layout, s.keys, drag.key, drag.moving, p.x, p.y, HEIGHT);
      const changed = JSON.stringify(hit) !== JSON.stringify(drag.hit);
      drag.hit = hit;
      status.textContent = hit
        ? `${name(drag.key)} → ${hit.kind === "end" ? "마지막 탭 뒤의 빈 곳" : name(s.keys[hit.index])}` + (drag.moving ? (hit.side === "before" ? "의 앞" : "의 뒤") : " 자리")
        : `${name(drag.key)} 끄는 중 — 이 자리는 받지 않는다` + (drag.moving ? " (순서가 그대로이거나 막대 밖)" : " (자기 자신이거나 막대 밖)");
      if (changed || starting) render();
    });

    function finish(ev) {
      if (!drag) return;
      const d = drag;
      drag = null;
      status.textContent = "";
      if (d.middle) {
        // 가운데 버튼 닫기 (middle_click_close 기본 참) — 고른 탭은 그대로다
        if (ev.button === 1) closeKey(d.key, `가운데 버튼 (${name(d.key)})`, `close("${d.key}")`);
        return;
      }
      if (!d.live) {
        if (d.closer) closeKey(d.key, `× 누름 (${name(d.key)})`, `close("${d.key}")`);
        else selectKey(d.key, `탭 누름 (${name(d.key)})`);
        return;
      }
      const hit = d.hit;
      if (!hit) {
        log(`${obj(d.key)} 놓음`, "", "받는 자리가 아니다 — 메시지가 없다");
        render();
        return;
      }
      const target = s.keys[hit.index];
      const before = orderText(s.keys);
      if (d.moving) {
        const r = demoMove(s.keys, d.key, target, hit.side);
        s.keys = r.keys;
        settle();
        if (r.revealed) reveal();
        log(`${obj(d.key)} ${hit.kind === "end" ? "끝 틈" : name(target) + (hit.side === "before" ? "의 앞 반쪽" : "의 뒤 반쪽")}에 놓음`, `move("${d.key}", "${target}", drop_side::${hit.side})`, `${before} → ${orderText(s.keys)}`);
      } else {
        s.keys = mobileReorder(s.keys, d.key, target);
        log(`${obj(d.key)} ${name(target)} 위에 놓음`, `reorder("${d.key}", "${target}")`, `모바일 데모의 처리: ${before} → ${orderText(s.keys)}` + (orderText(s.keys) === before ? " (그대로다)" : ""));
      }
      render();
    }
    stage.addEventListener("pointerup", finish);
    stage.addEventListener("pointercancel", () => { drag = null; status.textContent = ""; render(); });
    stage.addEventListener("contextmenu", (ev) => {
      const tab = ev.target.closest(".tb-tab");
      if (!tab) return;
      ev.preventDefault();
      const key = tab.dataset.key;
      const p = barPoint(ev);
      menu = { kind: "context", key, x: p.x };
      log(`우클릭 (${name(key)})`, `context_menu("${key}", context)`, "앱이 메뉴를 띄운다");
      render();
    });
    document.addEventListener("keydown", (ev) => {
      if (ev.key !== "Escape") return;
      if (drag && drag.live) {
        drag = null;
        status.textContent = "";
        log("Esc", "", "끌기를 거뒀다 — 메시지가 없다");
        render();
      } else if (menu) {
        menu = null;
        render();
      }
    });
    document.addEventListener("pointerdown", (ev) => {
      if (menu && !ev.target.closest(".tb-menu") && !ev.target.closest('[data-act="overflow"]') && !ev.target.closest(".tb-tab")) {
        menu = null;
        render();
      }
    });

    stage.addEventListener("click", (ev) => {
      const b = ev.target.closest("button[data-act]");
      if (!b) return;
      const act = b.dataset.act;
      if (act === "back" || act === "forward") {
        const steps = act === "back" ? -1 : 1;
        const delta = stepDelta(layout, steps);
        s.scroll = f(s.scroll + delta);
        log(act === "back" ? "앞 단추" : "뒤 단추", `scroll(step_delta(${steps})) = scroll(${fmt(delta)})`, `scroll_offset += ${fmt(delta)}`);
        render();
      } else if (act === "overflow") {
        menu = menu && menu.kind === "overflow" ? null : { kind: "overflow", x: b.offsetLeft };
        log("넘침 메뉴 단추", "overflow (앱의 액션)", menu ? "앱이 탭 목록을 띄운다" : "닫았다");
        render();
      } else if (act === "new") {
        const key = "d" + s.nextId;
        s.names[key] = "문서 " + s.nextId;
        s.nextId += 1;
        s.keys = s.keys.concat([key]);
        s.selected = key;
        settle();
        reveal();
        log("꼬리 단추 (새 탭)", 'tab_menu_command_intent { "new" }', `${obj(key)} 끝에 더하고 골랐다`);
        render();
      }
    });

    /* ---------- 조절기 ---------- */

    function setCount(n) {
      while (s.keys.length < n) {
        const key = "d" + s.nextId;
        s.names[key] = "문서 " + s.nextId;
        s.nextId += 1;
        s.keys.push(key);
      }
      if (s.keys.length > n) s.keys = s.keys.slice(0, n);
      if (!s.keys.includes(s.selected)) s.selected = s.keys[0] || "";
    }
    ctl.count.input.addEventListener("input", () => { setCount(+ctl.count.input.value); render(); });
    ctl.tw.input.addEventListener("input", () => { s.tabWidth = +ctl.tw.input.value; render(); });
    ctl.mtw.input.addEventListener("input", () => { s.minTabWidth = +ctl.mtw.input.value; render(); });
    ctl.bw.input.addEventListener("input", () => { s.barWidth = +ctl.bw.input.value; render(); });
    ctl.off.input.addEventListener("input", () => { s.scroll = +ctl.off.input.value; render(); });
    root.querySelectorAll("[data-seg]").forEach((g) =>
      g.addEventListener("click", (ev) => {
        const b = ev.target.closest("button");
        if (!b) return;
        s[g.dataset.seg] = b.dataset.v;
        render();
      }),
    );
    q("chk-overflow").addEventListener("change", (ev) => { s.overflow = ev.target.checked; menu = null; render(); });
    q("chk-trailing").addEventListener("change", (ev) => { s.trailing = ev.target.checked; render(); });
    q("cmd-close").addEventListener("click", () => s.selected && closeKey(s.selected, `Ctrl+W (앱 명령 "현재 탭 닫기")`, `tab_menu_command_intent { "close-current" }`));
    q("cmd-left").addEventListener("click", () => command("move-left", s.selected));
    q("cmd-right").addEventListener("click", () => command("move-right", s.selected));
    q("cmd-next").addEventListener("click", () => command("next", s.selected));

    function reset() {
      s = fresh();
      drag = null;
      menu = null;
      s.barWidth = Math.min(s.barWidth, maxBarWidth());
      ctl.count.input.value = String(s.keys.length);
      ctl.tw.input.value = String(s.tabWidth);
      ctl.mtw.input.value = String(s.minTabWidth);
      ctl.bw.input.value = String(s.barWidth);
      logBox.replaceChildren();
      status.textContent = "";
      render();
    }
    root.addEventListener("lw-reset", reset);
    window.addEventListener("resize", () => {
      const m = maxBarWidth();
      if (s.barWidth > m) { s.barWidth = m; ctl.bw.input.value = String(m); }
      render();
    });
    reset();
  }

  window.LuilTabsLab = { model, setup };
})();

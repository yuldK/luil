/* luil 설명서 — 선택 묶음의 키보드 규칙과 칸 배치, 삼상 체크를 JS로 옮긴 것
 *
 * 옮긴 것 (이름 옆 주석이 C++ 원본이다):
 *   - src/ui/ui_tree.cpp          collect_focus_stops · group_entry · focus_stop_of (Tab 순서와 묶음 진입)
 *   - src/ui/ui_interaction.cpp   process_activation_key · move_focus · process_group_key · process_typeahead ·
 *                                 누름의 초점(process_press 중 초점 부분)과 그 순서(process(key_pressed_event))
 *   - src/ui/choice_group_element.cpp  fitted_segment_width · track_width_for · arrange · preferred_size · 축과 진입
 *   - src/ui/check_element.cpp    aggregate_check · accessibility · height_for · indicator_width_for
 *   - src/text/case_fold.cpp      starts_with_folded (접기 표는 textlab.js의 foldCp가 옮겨 둔 것을 쓴다)
 * 글 폭(measure_context::text_width)은 옮길 수 없어 부르는 쪽이 재는 함수를 넘긴다.
 * 맞춘 방법: tests/ui_interaction_tests.cpp(2136-2233 화살표·Home·End, 3175-3225 글자 찾기)와
 * tests/choice_group_element_tests.cpp(76-104 폭, 179-191 진입)의 장면을 node에서 같은 순서로 되풀이했다.
 */
(function (root) {
  "use strict";

  // text::fold_case(char32_t) — textlab.js가 옮긴 표를 쓴다. 없으면 글자 찾기가 원본과 달라지므로 조용히 물러서지 않는다
  if (!root.LuilText || !root.LuilText.foldCp) throw new Error("choicemodel.js: textlab.js(LuilText)를 먼저 불러야 한다");
  const foldCp = root.LuilText.foldCp;

  // starts_with_folded (case_fold.cpp 177-180 → match_at): 코드포인트마다 접어 견준다
  function startsWithFolded(text, prefix) {
    const t = [...text], p = [...prefix];
    if (p.length > t.length) return false;
    for (let i = 0; i < p.length; ++i) if (foldCp(t[i].codePointAt(0)) !== foldCp(p[i].codePointAt(0))) return false;
    return true;
  }

  /* ---------- 삼상 체크 (check_element.cpp) ---------- */

  // aggregate_check (88-99): 자식 없음 → 꺼짐, 첫 자식이 일부면 일부, 하나라도 다르면 일부
  function aggregateCheck(states) {
    if (!states.length) return "unchecked";
    const first = states[0];
    if (first === "mixed") return "mixed";
    for (const s of states) if (s !== first) return "mixed";
    return first;
  }

  // accessibility (266-296)
  function checkAccess(config) {
    const style = config.style || "checkbox";
    const role = style === "toggle_button" || style === "toggle_chip" ? "button" : style === "checkbox" ? "check_box" : style === "radio" ? "radio_button" : "toggle_switch";
    const info = { role, name: config.label ? config.label : config.accessName || "", checked: false, selected: false, mixed: false };
    if (style === "radio") info.selected = !!config.checked;
    else info.checked = !!config.checked;
    info.mixed = !!config.mixed && style === "checkbox";
    return info;
  }

  const buttonLike = (style) => style === "toggle_button" || style === "toggle_chip";
  const checkHeight = (style) => (buttonLike(style) ? 28 : 24); // height_for (113-116): text_button_height 28, check_row_height 24
  const indicatorWidth = (style) => (buttonLike(style) ? 0 : style === "toggle_switch" ? 30 : 14); // indicator_width_for (118-123)

  /* ---------- 선택 묶음의 배치 (choice_group_element.cpp) ---------- */

  const RADIO_ROW = 24, TOGGLE_HEIGHT = 26, TRACK_PADDING = 2, SEGMENT_GAP = 2, SEGMENT_PADDING = 14; // choice_group_element.h 63-70

  // fitted_segment_width (170-180): 두 굵기(body_text·selected_choice) 가운데 넓은 쪽, 올림, 좌우 14
  //   measure(label, weight) → 논리 px 폭. weight는 "regular" | "semibold"
  function fittedSegmentWidth(items, measure) {
    let widest = 0;
    for (const item of items) {
      widest = Math.max(widest, measure(item.label, "regular"));
      widest = Math.max(widest, measure(item.label, "semibold"));
    }
    return Math.ceil(widest) + SEGMENT_PADDING * 2;
  }

  // track_width_for (183-189)
  const trackWidthFor = (segment, count) => (count === 0 ? 0 : segment * count + SEGMENT_GAP * (count - 1) + TRACK_PADDING * 2);

  // arrange (218-254), scale 1. 돌려주는 값: { bounds: {x,y,w,h}, children: [{x,y,w,h}] }
  function arrangeChoices(config, slot, measure) {
    const n = config.items.length;
    if ((config.style || "radio") === "radio") {
      return { bounds: { x: slot.x, y: slot.y, w: slot.w, h: n * RADIO_ROW }, children: config.items.map((_, i) => ({ x: slot.x, y: slot.y + i * RADIO_ROW, w: slot.w, h: RADIO_ROW })) };
    }
    let track = slot.w;
    if ((config.width || "fit") === "fit") track = Math.min(slot.w, trackWidthFor(fittedSegmentWidth(config.items, measure), n));
    const out = { bounds: { x: slot.x, y: slot.y, w: track, h: TOGGLE_HEIGHT }, children: [] };
    if (!n) return out;
    const width = Math.max(0, (track - TRACK_PADDING * 2 - SEGMENT_GAP * (n - 1)) / n);
    const height = Math.max(0, TOGGLE_HEIGHT - TRACK_PADDING * 2);
    let x = slot.x + TRACK_PADDING;
    for (let i = 0; i < n; ++i) {
      out.children.push({ x, y: slot.y + TRACK_PADDING, w: width, h: height });
      x += width + SEGMENT_GAP;
    }
    return out;
  }

  // preferred_size (271-278): 라디오는 없다(null), 채움은 받은 폭, 맞춤은 트랙 폭
  function choicePreferredSize(config, availableWidth, measure) {
    if ((config.style || "radio") === "radio") return null;
    if (config.width === "fill") return { w: availableWidth, h: TOGGLE_HEIGHT };
    return { w: trackWidthFor(fittedSegmentWidth(config.items, measure), config.items.length), h: TOGGLE_HEIGHT };
  }

  const choiceHeight = (config) => ((config.style || "radio") === "toggle" ? TOGGLE_HEIGHT : config.items.length * RADIO_ROW); // height_for (211-216)

  /* ---------- 초점 (ui_tree.cpp, ui_interaction.cpp) ----------
   * tree: 차례대로 놓인 마디 목록이다.
   *   { id, label, action: bool, enabled: bool }                       — 낱개 자리 (단추·체크)
   *   { group: { id, axis: "vertical"|"horizontal", entry, members: [ {id, label, action, enabled} ] } } — 묶음
   * 자리(focusable)는 액션이 있고 켜진 것이다 (ui_element.cpp 420-424의 요지 — 보임·배치는 실습에서 늘 참).
   */
  const focusable = (n) => !!n && n.action && n.enabled !== false;

  // group_entry (476-486)
  function groupEntry(group) {
    const members = group.members.filter(focusable).map((m) => m.id);
    if (!members.length) return null;
    return members.includes(group.entry) ? group.entry : members[0];
  }

  // focus_order (collect_focus_stops 403-420): 묶음은 자리 하나로 접는다
  function focusOrder(tree) {
    const order = [];
    for (const node of tree) {
      if (node.group) {
        const entry = groupEntry(node.group);
        if (entry !== null) order.push(entry);
      } else if (focusable(node)) order.push(node.id);
    }
    return order;
  }

  function findNode(tree, id) {
    for (const node of tree) {
      if (node.group) {
        const m = node.group.members.find((x) => x.id === id);
        if (m) return m;
      } else if (node.id === id) return node;
    }
    return null;
  }

  // focus_group_of (422-433): 그 id를 품은 묶음의 축과 구성원(자리인 것만)
  function focusGroupOf(tree, id) {
    for (const node of tree)
      if (node.group && node.group.members.some((m) => m.id === id))
        return { axis: node.group.axis, members: node.group.members.filter(focusable).map((m) => m.id), group: node.group };
    return { axis: "none", members: [], group: null };
  }

  // focus_stop_of (435-452)
  function focusStopOf(tree, id) {
    const scope = focusGroupOf(tree, id);
    if (scope.group) return groupEntry(scope.group);
    const node = findNode(tree, id);
    return focusable(node) ? id : null;
  }

  const shortcutModifierDown = (e) => !!(e.control || e.alt || e.meta || e.primary_shortcut); // key_pressed_event::shortcut_modifier_down

  /* 컨트롤러 — interaction_controller의 이 실습에 닿는 길만이다.
   *   run(id) → 그 자리의 액션이 낸 메시지 설명 (앱이 상태를 바꾼다)
   */
  class Controller {
    constructor(resetTime) {
      this.focused = null;
      this.focusVisible = false;
      this.query = "";
      this.lastTypeahead = -Infinity;
      this.typeaheadFocus = null;
      this.resetTime = resetTime === undefined ? 1000 : resetTime; // interaction_config::typeahead_reset_time (ui_interaction.h 415)
    }

    setKeyboardFocus(id) {
      // set_keyboard_focus (4405-4414): 키보드로 옮긴 초점이라 테를 그린다
      this.focused = id;
      this.focusVisible = true;
    }

    // process(key_pressed_event)의 차례 가운데 이 실습에 있는 것: 실행 키 → Tab → 묶음 → (앱으로 흐름)
    //   돌려주는 값 { by: "activation"|"tab"|"group"|"flow", run: id|null, from, to, why }
    key(tree, event) {
      const from = this.focused;
      // process_activation_key (4319-4347)
      if ((event.key === "space" || event.key === "enter") && !shortcutModifierDown(event) && this.focused !== null) {
        const node = findNode(tree, this.focused);
        if (node && node.enabled !== false && node.action) return { by: "activation", run: node.id, from, to: this.focused };
      }
      // Tab (3655-3659 → move_focus 4363-4403)
      if (event.key === "tab" && !shortcutModifierDown(event)) {
        const order = focusOrder(tree);
        if (order.length) {
          const forward = !event.shift;
          let next = forward ? 0 : order.length - 1;
          if (this.focused !== null) {
            let index = order.indexOf(this.focused);
            if (index < 0) {
              const stop = focusStopOf(tree, this.focused);
              if (stop !== null) index = order.indexOf(stop);
            }
            if (index >= 0) next = forward ? (index + 1) % order.length : (index + order.length - 1) % order.length;
          }
          this.setKeyboardFocus(order[next]);
          return { by: "tab", run: null, from, to: this.focused, wrapped: this.focused !== null && from !== null && (forward ? order.indexOf(this.focused) === 0 : order.indexOf(this.focused) === order.length - 1) };
        }
      }
      // process_group_key (4571-4616)
      const group = this.groupKey(tree, event);
      if (group) return { by: "group", run: null, from, to: this.focused, why: group };
      return { by: "flow", run: null, from, to: this.focused, why: this.flowReason(tree, event) };
    }

    groupKey(tree, event) {
      if (shortcutModifierDown(event) || this.focused === null) return null;
      const horizontal = event.key === "arrow_left" || event.key === "arrow_right";
      const vertical = event.key === "arrow_up" || event.key === "arrow_down";
      const jump = event.key === "home" || event.key === "end";
      if (!horizontal && !vertical && !jump) return null;
      const scope = focusGroupOf(tree, this.focused);
      const matching = scope.axis === "both" || (horizontal && scope.axis === "horizontal") || (vertical && scope.axis === "vertical");
      const mine = jump ? scope.axis !== "none" : matching;
      if (!mine) return null;
      const index = scope.members.indexOf(this.focused);
      if (index < 0 || scope.members.length < 2) return null;
      if (jump) {
        const target = event.key === "home" ? scope.members[0] : scope.members[scope.members.length - 1];
        if (target !== this.focused) this.setKeyboardFocus(target);
        return "jump";
      }
      const forward = event.key === "arrow_right" || event.key === "arrow_down";
      const next = forward ? (index + 1) % scope.members.length : (index + scope.members.length - 1) % scope.members.length;
      this.setKeyboardFocus(scope.members[next]);
      return (forward && next === 0) || (!forward && next === scope.members.length - 1) ? "wrap" : "step";
    }

    // 묶음이 받지 않은 까닭 (설명용 — 판정은 groupKey와 같은 조건이다)
    flowReason(tree, event) {
      const arrow = ["arrow_left", "arrow_right", "arrow_up", "arrow_down", "home", "end"].includes(event.key);
      if (!arrow) return event.key === "space" || event.key === "enter" ? (shortcutModifierDown(event) ? "modifier" : "no-action") : "not-group-key";
      if (shortcutModifierDown(event)) return "modifier";
      if (this.focused === null) return "no-focus";
      const scope = focusGroupOf(tree, this.focused);
      if (scope.axis === "none") return "outside";
      const horizontal = event.key === "arrow_left" || event.key === "arrow_right";
      const vertical = event.key === "arrow_up" || event.key === "arrow_down";
      const matching = scope.axis === "both" || (horizontal && scope.axis === "horizontal") || (vertical && scope.axis === "vertical");
      if (!matching && event.key !== "home" && event.key !== "end") return "axis";
      return "single"; // 축은 맞지만 구성원이 하나뿐이다 (members < 2)
    }

    // process_typeahead (4425-4498) — 묶음 안의 글자 찾기. time은 ms
    type(tree, character, time) {
      const cp = character.codePointAt(0);
      if (cp < 0x20 || cp === 0x7f || this.focused === null) return { moved: false, ignored: true };
      const scope = focusGroupOf(tree, this.focused);
      if (scope.axis === "none" || !scope.members.length) return { moved: false, ignored: true };
      const reset = time - this.lastTypeahead > this.resetTime || this.focused !== this.typeaheadFocus;
      if (reset) this.query = "";
      const first = this.query === "";
      this.query += character;
      this.lastTypeahead = time;
      const found = scope.members.indexOf(this.focused);
      const current = found >= 0 ? found : 0;
      const count = scope.members.length;
      const start = first ? current + 1 : current;
      for (let step = 0; step < count; ++step) {
        const candidate = scope.members[(start + step) % count];
        const node = findNode(tree, candidate);
        if (!node || !startsWithFolded(node.label || "", this.query)) continue;
        const from = this.focused;
        if (candidate !== this.focused) this.setKeyboardFocus(candidate);
        this.typeaheadFocus = candidate;
        return { moved: candidate !== from, ignored: false, query: this.query, first, reset, to: candidate };
      }
      this.typeaheadFocus = this.focused;
      return { moved: false, ignored: false, query: this.query, first, reset, to: this.focused };
    }

    // 누름 (process_press의 초점 1441-1471): 자리면 초점을 주되 테는 그리지 않는다, 아니면 거둔다
    press(tree, id) {
      const node = id === null ? null : findNode(tree, id);
      if (!node || node.enabled === false) {
        this.focused = null;
        this.focusVisible = false;
        return { run: null };
      }
      if (focusable(node)) {
        this.focused = node.id;
        this.focusVisible = false;
      } else {
        this.focused = null;
        this.focusVisible = false;
      }
      return { run: node.action ? node.id : null };
    }
  }

  root.LuilChoices = {
    startsWithFolded, aggregateCheck, checkAccess, checkHeight, indicatorWidth,
    RADIO_ROW, TOGGLE_HEIGHT, TRACK_PADDING, SEGMENT_GAP, SEGMENT_PADDING,
    fittedSegmentWidth, trackWidthFor, arrangeChoices, choicePreferredSize, choiceHeight,
    groupEntry, focusOrder, focusGroupOf, focusStopOf, findNode, Controller,
  };
})(typeof window !== "undefined" ? window : globalThis);

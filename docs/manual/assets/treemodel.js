/* luil 설명서 — 계층 모델과 다중 선택 규칙을 JS로 옮긴 것
 *
 * src/ui/item_model.cpp (flatten_tree, tree_row_items, tree_nodes_to_load, request_ledger)와
 * src/ui/selection_model.cpp (selection_pick/move/toggle/select_all/reconcile),
 * src/ui/virtual_list_element.cpp의 키보드 판정(virtual_list_step_target, virtual_list_search_target, page_rows, step_tree)을
 * 함수 단위로 옮겼다.
 * 이름 옆 주석은 C++ 원본의 이름이다. 원본을 컴파일한 시험 대조기로 무작위 입력을 견주어 맞췄다.
 *
 *   tree_node      { key, label, children: [], load: "loaded"|"unloaded"|"loading"|"failed", enabled }
 *   virtual_list_item { key, label, height, enabled, depth, expansion: "none"|"collapsed"|"expanded" }
 *   item_selection { anchor, cursor, keys: [] }
 *   ui_action_context(일부) { control, shift }
 *   virtual_list_move_context { extend, keep_selection }
 */
(function () {
  "use strict";

  const LOADING_SUFFIX = "\u001Floading"; // tree_loading_suffix
  const FAILED_SUFFIX = "\u001Ffailed"; // tree_failed_suffix

  // 가지: 자식이 있거나 아직 다 불러오지 않은 노드 (branch)
  const isBranch = (node) => (node.children && node.children.length > 0) || (node.load || "loaded") !== "loaded";

  // flatten_tree(roots, expanded, options)
  //   options: { loading_label, failed_label, less(a, b) → bool, keep(node) → bool, expand_matches }
  function flattenTree(roots, expanded, options) {
    const opt = options || {};
    const rows = [];
    const matches = new Map(); // filter_results: node → "none" | "descendant" | "self"

    function matchNode(node) {
      if (!opt.keep) return "self";
      if (matches.has(node)) return matches.get(node);
      let match = "self";
      if (!opt.keep(node)) match = (node.children || []).some((c) => matchNode(c) !== "none") ? "descendant" : "none";
      matches.set(node, match);
      return match;
    }

    function ordered(nodes) {
      const order = (nodes || []).filter((n) => matchNode(n) !== "none");
      if (opt.less) order.sort((a, b) => (opt.less(a, b) ? -1 : opt.less(b, a) ? 1 : 0)); // 안정 정렬 (std::stable_sort)
      return order;
    }

    function statusRow(node, depth, kind) {
      const loading = kind === "loading";
      return {
        item: { key: node.key + (loading ? LOADING_SUFFIX : FAILED_SUFFIX), label: loading ? opt.loading_label || "" : opt.failed_label || "", height: 0, enabled: !loading, depth, expansion: "none" },
        kind,
        node: node.key,
      };
    }

    function flatten(nodes, depth) {
      for (const node of ordered(nodes)) {
        const pathOnly = !!opt.keep && matches.get(node) === "descendant";
        const open = expanded.has(node.key) || (!!opt.expand_matches && pathOnly);
        const branch = isBranch(node);
        rows.push({
          item: { key: node.key, label: node.label || "", height: 0, enabled: node.enabled !== false, depth, expansion: branch ? (open ? "expanded" : "collapsed") : "none" },
          kind: "node",
          node: node.key,
        });
        if (!open || !branch) continue;
        const load = node.load || "loaded";
        if (load === "unloaded" || load === "loading") rows.push(statusRow(node, depth + 1, "loading"));
        else if (load === "failed") rows.push(statusRow(node, depth + 1, "failed"));
        else flatten(node.children, depth + 1);
      }
    }

    flatten(roots, 0);
    return rows;
  }

  // tree_row_items
  const treeRowItems = (rows) => rows.map((r) => r.item);

  // tree_nodes_to_load
  function treeNodesToLoad(roots, expanded) {
    const keys = [];
    (function collect(nodes) {
      for (const node of nodes || []) {
        if (!expanded.has(node.key)) continue;
        const load = node.load || "loaded";
        if (load === "unloaded") keys.push(node.key);
        else if (load === "loaded") collect(node.children);
      }
    })(roots);
    return keys;
  }

  // request_ledger
  class RequestLedger {
    constructor() {
      this.latest = new Map();
      this.next = 0;
    }
    issue(key) {
      this.next += 1; // 0은 "낸 적 없음"이라 1부터 낸다
      this.latest.set(key, this.next);
      return this.next;
    }
    current(key, token) {
      return this.latest.has(key) && this.latest.get(key) === token;
    }
    settle(key, token) {
      if (!this.current(key, token)) return false;
      this.latest.delete(key);
      return true;
    }
    cancel(key) {
      this.latest.delete(key);
    }
    pending() {
      return this.latest.size;
    }
  }

  /* ---------- 다중 선택 (selection_model.cpp) ---------- */

  const clone = (s) => ({ anchor: s.anchor || "", cursor: s.cursor || "", keys: (s.keys || []).slice() });

  function indexOf(items, key) {
    if (!key) return -1;
    for (let i = 0; i < items.length; i++) if (items[i].key === key) return i;
    return -1;
  }

  // 모델 순서로 다시 세운다. 모델에 없거나 비활성인 키는 빠진다 (in_model_order)
  function inModelOrder(items, chosen) {
    const keys = [];
    if (!chosen.size) return keys;
    for (const it of items) if (it.enabled !== false && chosen.has(it.key)) keys.push(it.key);
    return keys;
  }

  function addRange(items, from, to, chosen) {
    const first = Math.min(from, to);
    const last = Math.max(from, to);
    for (let i = first; i <= last; i++) if (items[i].enabled !== false) chosen.add(items[i].key);
  }

  // Shift 범위의 기준점 자리 (anchor_index). 바꿔 썼으면 selection.anchor도 고친다.
  function anchorIndex(sel, items, fallback) {
    const a = indexOf(items, sel.anchor);
    if (a >= 0) return a;
    const c = indexOf(items, sel.cursor);
    const chosen = c >= 0 ? c : fallback;
    sel.anchor = items[chosen].key;
    return chosen;
  }

  function extendTo(sel, items, index, keep) {
    const anchor = anchorIndex(sel, items, index);
    const chosen = keep ? new Set(sel.keys) : new Set();
    addRange(items, anchor, index, chosen);
    sel.keys = inModelOrder(items, chosen);
  }

  function nearestSurviving(previous, surviving, key) {
    const origin = indexOf(previous, key);
    if (origin < 0) return null;
    for (let i = origin + 1; i < previous.length; i++) if (surviving.has(previous[i].key)) return previous[i].key;
    for (let i = origin; i > 0; i--) if (surviving.has(previous[i - 1].key)) return previous[i - 1].key;
    return null;
  }

  const selectionContains = (sel, key) => sel.keys.includes(key);

  function selectionToggle(selection, items, key) {
    const sel = clone(selection);
    const index = indexOf(items, key);
    if (index < 0 || items[index].enabled === false) return sel;
    const chosen = new Set(sel.keys);
    if (!chosen.delete(items[index].key)) chosen.add(items[index].key);
    sel.keys = inModelOrder(items, chosen);
    sel.anchor = items[index].key;
    sel.cursor = items[index].key;
    return sel;
  }

  function selectionPick(selection, items, key, ctx) {
    const sel = clone(selection);
    const index = indexOf(items, key);
    if (index < 0 || items[index].enabled === false) return sel;
    if (ctx && ctx.shift) {
      extendTo(sel, items, index, !!ctx.control);
      sel.cursor = items[index].key;
      return sel;
    }
    if (ctx && ctx.control) return selectionToggle(sel, items, key);
    sel.keys = [items[index].key];
    sel.anchor = items[index].key;
    sel.cursor = items[index].key;
    return sel;
  }

  function selectionMove(selection, items, key, ctx) {
    const sel = clone(selection);
    const index = indexOf(items, key);
    if (index < 0) return sel;
    if (ctx && ctx.extend) {
      extendTo(sel, items, index, !!ctx.keep_selection);
      sel.cursor = items[index].key;
      return sel;
    }
    sel.cursor = items[index].key;
    if (ctx && ctx.keep_selection) return sel;
    sel.keys = []; // 비활성 항목에는 커서가 설 수는 있어도 고를 수는 없다
    if (items[index].enabled !== false) sel.keys.push(items[index].key);
    sel.anchor = items[index].key;
    return sel;
  }

  function selectionSelectAll(selection, items) {
    const sel = clone(selection);
    sel.keys = items.filter((it) => it.enabled !== false).map((it) => it.key);
    return sel;
  }

  function selectionReconcile(selection, previous, items) {
    if (!items.length) return { anchor: "", cursor: "", keys: [] };
    const sel = clone(selection);
    const surviving = new Set(items.map((it) => it.key));
    sel.keys = inModelOrder(items, new Set(sel.keys));
    if (!surviving.has(sel.cursor)) {
      const nearest = nearestSurviving(previous, surviving, sel.cursor);
      if (nearest !== null) sel.cursor = nearest;
      else {
        const first = items.find((it) => it.enabled !== false);
        sel.cursor = first ? first.key : items[0].key;
      }
    }
    if (!surviving.has(sel.anchor)) {
      const nearest = nearestSurviving(previous, surviving, sel.anchor);
      sel.anchor = nearest !== null ? nearest : sel.cursor;
    }
    return sel;
  }


  /* ---------- 키보드 (virtual_list_element.cpp) ---------- */

  // 키보드가 설 수 있는 자리인가 (selectable)
  const selectable = (it) => it.enabled !== false;

  // from에서 direction 쪽으로 count칸 옮긴 활성 항목. 끝에서 멈춘다 (stepped)
  function stepped(items, from, direction, count) {
    let best = -1;
    let current = from;
    for (let moved = 0; moved < count; moved++) {
      let advanced = false;
      while (true) {
        if (direction < 0) {
          if (current === 0) break;
          current--;
        } else {
          if (current + 1 >= items.length) break;
          current++;
        }
        if (selectable(items[current])) {
          advanced = true;
          break;
        }
      }
      if (!advanced) break;
      best = current;
    }
    return best;
  }

  // 처음(끝)의 활성 항목 (edge_item)
  function edgeItem(items, front) {
    for (let step = 0; step < items.length; step++) {
      const index = front ? step : items.length - 1 - step;
      if (selectable(items[index])) return index;
    }
    return -1;
  }

  // virtual_list_step_target. step: "decrease"|"increase"|"decrease_page"|"increase_page"|"minimum"|"maximum".
  // from이 없으면 -1. 옮길 자리가 없으면 -1.
  function stepTarget(items, from, step, pageRows) {
    if (!items.length) return -1;
    if (step === "minimum") return edgeItem(items, true);
    if (step === "maximum") return edgeItem(items, false);
    const known = from >= 0 && from < items.length;
    const start = known ? from : edgeItem(items, true);
    if (start < 0) return -1;
    if (!known) return start;
    const rows = pageRows > 0 ? pageRows : 1;
    if (step === "decrease") return stepped(items, start, -1, 1);
    if (step === "increase") return stepped(items, start, 1, 1);
    if (step === "decrease_page") return stepped(items, start, -1, rows);
    if (step === "increase_page") return stepped(items, start, 1, rows);
    return -1;
  }

  // virtual_list_search_target. fold: 대소문자 접기 함수 (text::fold_case의 단순 접기)
  function searchTarget(items, from, query, first, fold) {
    if (!items.length || !query) return -1;
    const f = fold || ((x) => x.toLowerCase());
    const q = f(query);
    const current = from >= 0 && from < items.length ? from : 0;
    const start = first ? current + 1 : current;
    for (let step = 0; step < items.length; step++) {
      const index = (start + step) % items.length;
      const it = items[index];
      if (selectable(it) && f(it.label || "").startsWith(q)) return index;
    }
    return -1;
  }

  // 창 하나에 드는 Page 걸음 수 (virtual_list_element::page_rows). heights: 항목 높이 (0이면 rowHeight)
  function pageRows(items, cursor, backwards, viewportHeight, rowHeight) {
    if (viewportHeight <= 0 || !items.length) return 1;
    let used = 0;
    let rows = 0;
    let index = cursor >= 0 ? cursor : 0;
    const h = (it) => (it.height > 0 ? it.height : rowHeight);
    while (true) {
      if (backwards) {
        if (index === 0) break;
        index--;
        used += h(items[index]);
      } else {
        if (index + 1 >= items.length) break;
        used += h(items[index]);
        index++;
      }
      if (used > viewportHeight) break;
      if (items[index].enabled !== false) rows++;
    }
    return rows > 0 ? rows : 1;
  }

  // 계층 목록의 ←(forward 거짓)·→ (virtual_list_element::step_tree)
  //   { kind: "expand"|"collapse", key } | { kind: "move", index } | { kind: "swallow" } | null(소비하지 않음)
  function stepTree(items, cursor, forward, movesCursor) {
    if (cursor < 0 || cursor >= items.length) return null;
    const item = items[cursor];
    if (forward) {
      if (item.expansion === "collapsed") return { kind: "expand", key: item.key };
      if (item.expansion !== "expanded") return null;
      if (cursor + 1 < items.length && items[cursor + 1].depth > item.depth && movesCursor !== false) return { kind: "move", index: cursor + 1 };
      return { kind: "swallow" };
    }
    if (item.expansion === "expanded") return { kind: "collapse", key: item.key };
    if (item.depth > 0 && movesCursor !== false)
      for (let parent = cursor; parent > 0; parent--) if (items[parent - 1].depth < item.depth) return { kind: "move", index: parent - 1 };
    return null;
  }

  window.LuilTree = {
    LOADING_SUFFIX,
    FAILED_SUFFIX,
    isBranch,
    flattenTree,
    treeRowItems,
    treeNodesToLoad,
    RequestLedger,
    selectionContains,
    selectionPick,
    selectionMove,
    selectionToggle,
    selectionSelectAll,
    selectionReconcile,
    selectable,
    stepTarget,
    searchTarget,
    pageRows,
    stepTree,
  };
})();

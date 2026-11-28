/* luil 설명서 — 목록과 트리 뷰 실습
 *
 * virtual_list_element 하나를 브라우저로 재현한다. 판정은 전부 assets/treemodel.js(원본 C++과 대조한 이식)를 부른다.
 *  - 앱 상태: 모델(tree_node), 펼침 집합, 선택(item_selection 또는 단일 선택), 스크롤 값, 요청 장부(request_ledger)
 *  - frame마다 flatten_tree로 줄을 다시 펴고, 창에 걸치는 행(+overscan 2)과 커서 행만 "짓는다"
 *  - 입력은 element가 낼 메시지(config의 factory 호출)로 바꿔 기록하고, 앱이 그 메시지로 상태를 바꾼다
 * 그림은 luil의 실제 렌더링이 아니라 같은 치수(행 24, 들여쓰기 14, 삼각형 16 논리 픽셀)로 그린 재현이다.
 */
(function () {
  "use strict";
  const T = window.LuilTree;
  const X = window.LuilText;
  const { el, icon } = window.LU;
  const fold = X && X.foldString ? X.foldString : (s) => s.toLowerCase();

  const ROW = 24; // virtual_list_config::row_height 기본값
  const INDENT = 14; // list_indent_step
  const EXPANDER = 16; // list_expander_width
  const OVERSCAN = 2; // virtual_list_config::overscan 기본값
  const VIEW_ROWS = 11;
  const WHEEL = 48; // input_wheel_scroll_step
  const TYPEAHEAD_RESET = 1000; // interaction_config::typeahead_reset_time

  const G = { folder: "EA83", open: "EAF7", file: "EA7B", code: "EAE9", md: "EB1D", lock: "EA75", load: "EB19", fail: "EA87", chev: "EAB6" };

  // 앱이 든 모델의 처음 모양
  function initialModel() {
    const f = (key, label, children, extra) => Object.assign({ key, label, children: children || [], load: "loaded", enabled: true }, extra || {});
    return [
      f("include", "include", [
        f("include/luil", "luil", [
          f("include/luil/ui", "ui", [
            f("include/luil/ui/list_element.h", "list_element.h"),
            f("include/luil/ui/virtual_list_element.h", "virtual_list_element.h"),
            f("include/luil/ui/item_model.h", "item_model.h"),
            f("include/luil/ui/selection_model.h", "selection_model.h"),
            f("include/luil/ui/grouped_list_element.h", "grouped_list_element.h"),
          ]),
          f("include/luil/app", "app", [], { load: "unloaded" }),
          f("include/luil/text", "text", [], { load: "unloaded" }),
        ]),
      ]),
      f("src", "src", [], { load: "unloaded" }),
      f("docs", "docs", [
        f("docs/list-view-design.md", "list-view-design.md"),
        f("docs/virtual-list-design.md", "virtual-list-design.md"),
        f("docs/item-model-design.md", "item-model-design.md"),
        f("docs/keyboard-selection-design.md", "keyboard-selection-design.md"),
      ]),
      f("examples", "examples", [], { load: "unloaded" }),
      f("share", "\\\\server\\share", [], { load: "unloaded" }),
      f("third_party", "third_party (잠김)", [], { enabled: false, load: "unloaded" }),
      f("CMakeLists.txt", "CMakeLists.txt"),
      f("README.md", "README.md"),
    ];
  }
  // 펼칠 때 "불러오는" 자식들 (실제 앱이라면 파일 시스템 질의)
  const LAZY = {
    "include/luil/app": ["app_host.h", "file_dialog.h", "menu_popup.h", "notification.h"],
    "include/luil/text": ["case_fold.h", "line_break.h", "utf8_text.h"],
    src: ["host/", "ui/", "text/", "win32/", "android/", "macos/", "ios/"],
    "src/ui": ["list_element.cpp", "virtual_list_element.cpp", "item_model.cpp", "selection_model.cpp", "ui_interaction.cpp", "ui_tree.cpp"],
    "src/host": ["app_host.cpp"],
    "src/text": ["case_fold.cpp", "utf8_text.cpp"],
    "src/win32": ["uia_provider.cpp", "win32_window.cpp"],
    "src/android": ["android_app.cpp"],
    "src/macos": ["accessibility_macos.mm", "macos_app.mm"],
    "src/ios": ["ios_app.mm"],
    examples: ["hello/", "widgets/", "demo/", "mobile_demo/"],
    "examples/hello": ["main.cpp", "hello_app.cpp", "hello_app.h"],
    "examples/widgets": ["app.cpp", "controls_section.cpp", "inputs_section.cpp"],
    "examples/demo": ["lists_page.cpp", "lists_page.h", "workbench_page.cpp"],
    "examples/mobile_demo": ["lists_page.cpp"],
    share: ["2026-보고서.md", "회의록.md", "design/"],
    "share/design": ["mockup.png", "palette.json"],
  };

  function findNode(nodes, key) {
    for (const n of nodes) {
      if (n.key === key) return n;
      const c = findNode(n.children, key);
      if (c) return c;
    }
    return null;
  }

  const fmtKey = (k) => 'u8"' + String(k).replace(/\u001f/g, "\\u001F") + '"';
  const fmtSel = (s) => `anchor ${s.anchor ? fmtKey(s.anchor) : "—"} · cursor ${s.cursor ? fmtKey(s.cursor) : "—"} · keys [${s.keys.map(fmtKey).join(", ")}]`;

  function setup(root) {
    const view = root.querySelector("[data-tv=view]");
    const canvas = root.querySelector("[data-tv=canvas]");
    const statusBox = root.querySelector("[data-tv=status]");
    const stateBox = root.querySelector("[data-tv=state]");
    const logBox = root.querySelector("[data-tv=log]");
    const q = root.querySelector("[data-tv=filter]");
    const multi = root.querySelector("[data-tv=multi]");
    const expm = root.querySelector("[data-tv=expm]");
    const sort = root.querySelector("[data-tv=sort]");

    let S; // 앱 상태
    let items = []; // 이번 frame의 virtual_list_item들
    let rows = []; // tree_row들
    let typeahead = { query: "", last: -1e9 };
    let timers = [];

    function reset() {
      timers.forEach(clearTimeout);
      timers = [];
      S = {
        roots: initialModel(),
        expanded: new Set(["include", "include/luil", "include/luil/ui", "docs"]),
        selection: { anchor: "", cursor: "", keys: [] }, // multiple_selection 켬
        selected: "", // multiple_selection 끔: 고른 키 하나 = 커서
        cursor: "",
        scroll: 0,
        ledger: new T.RequestLedger(),
        tokens: new Map(),
        failOnce: new Set(["share"]),
      };
      q.value = "";
      expm.checked = true;
      sort.value = "model";
      multi.checked = true;
      logBox.textContent = "";
      typeahead = { query: "", last: -1e9 };
      frame([]);
      log("처음 상태", [], "모델 9개 뿌리, 펼침 4개. 목록을 눌러 초점을 주고 키보드로 다뤄 본다.");
    }

    function options() {
      const query = q.value.trim();
      const opt = { loading_label: "불러오는 중…", failed_label: "불러오지 못했습니다 — Enter로 다시 시도", expand_matches: expm.checked };
      if (query) {
        const fq = fold(query);
        opt.keep = (n) => fold(n.label).includes(fq);
      }
      if (sort.value === "folders") {
        // 앱이 정하는 형제 순서: 폴더 먼저, 그다음 이름 (byte 순서는 C++ u8string 비교와 같다)
        const isDir = (n) => T.isBranch(n);
        opt.less = (a, b) => (isDir(a) !== isDir(b) ? isDir(a) : a.label < b.label);
      }
      return opt;
    }

    // 앱의 make_frame: 모델에서 줄을 다시 펴고, 바뀐 모델에 선택을 맞춘다 (selection_reconcile)
    function frame(previousItems) {
      rows = T.flattenTree(S.roots, S.expanded, options());
      items = T.treeRowItems(rows);
      if (multi.checked) {
        if (previousItems) S.selection = items.length ? T.selectionReconcile(S.selection, previousItems, items) : { anchor: "", cursor: "", keys: [] };
      } else if (S.cursor && !items.some((it) => it.key === S.cursor)) {
        S.cursor = items.length ? items[0].key : "";
        S.selected = "";
      }
      const max = Math.max(0, items.length * ROW - VIEW_ROWS * ROW);
      S.scroll = Math.min(Math.max(0, S.scroll), max); // arrange가 범위 안으로 다듬는다
      draw();
    }

    const cursorKey = () => (multi.checked ? S.selection.cursor : S.cursor || S.selected);
    // 목록이 보는 커서다: 앱이 이름 댄 항목, 그 키가 모델에 없으면 첫 활성 항목 (virtual_list_element 생성자)
    function cursorIndex() {
      const k = cursorKey();
      const i = k ? items.findIndex((it) => it.key === k) : -1;
      return i >= 0 ? i : items.findIndex((it) => it.enabled);
    }
    const isSelected = (key) => (multi.checked ? T.selectionContains(S.selection, key) : S.selected === key);

    function visibleRange() {
      if (!items.length) return [0, 0];
      let begin = Math.floor(S.scroll / ROW);
      let end = Math.min(items.length, Math.ceil((S.scroll + VIEW_ROWS * ROW) / ROW));
      begin = Math.max(0, begin - OVERSCAN);
      end = Math.min(items.length, end + OVERSCAN);
      return [begin, end];
    }

    function draw() {
      canvas.textContent = "";
      canvas.style.height = items.length * ROW + "px";
      canvas.style.transform = `translateY(${-S.scroll}px)`;
      const [b, e] = visibleRange();
      const built = new Set();
      for (let i = b; i < e; i++) built.add(i);
      const ci = cursorIndex();
      if (ci >= 0) built.add(ci); // 커서 행은 창 밖이어도 짓는다
      let offSel = 0;
      items.forEach((it, i) => {
        if (!built.has(i) && isSelected(it.key) && offSel < 64) {
          built.add(i); // 창 밖의 고른 행도 짓는다 (virtual_list_offscreen_selection_limit)
          offSel++;
        }
      });
      for (const i of [...built].sort((x, y) => x - y)) canvas.append(rowEl(i, ci));
      if (!items.length) canvas.append(el("div", { class: "tv-empty", text: q.value.trim() ? "맞는 항목이 없다." : "항목이 없다." }));
      const outside = [...built].filter((i) => i < b || i >= e).length;
      statusBox.textContent = `모델 ${items.length}줄 · 지은 행 ${built.size}개 (창 ${b}–${Math.max(b, e - 1)}${outside ? ` + 창 밖 ${outside}` : ""}) · 스크롤 ${S.scroll}`;
      drawState();
    }

    function rowEl(i, ci) {
      const it = items[i];
      const row = rows[i];
      const cls = ["tv-row"];
      if (isSelected(it.key)) cls.push("sel");
      if (i === ci) cls.push("cur");
      if (!it.enabled) cls.push("dis");
      if (row.kind !== "node") cls.push("status", row.kind);
      const r = el("div", { class: cls.join(" "), style: { top: i * ROW + "px", paddingLeft: 6 + it.depth * INDENT + "px" }, "data-i": String(i), role: "treeitem", "aria-level": String(it.depth + 1), "aria-selected": isSelected(it.key) ? "true" : "false" });
      if (it.expansion !== "none") r.setAttribute("aria-expanded", it.expansion === "expanded" ? "true" : "false");
      const exp = el("span", { class: "tv-exp" + (it.expansion === "expanded" ? " open" : ""), "data-exp": it.expansion !== "none" ? "1" : null });
      if (it.expansion !== "none") exp.append(icon(G.chev));
      const node = row.kind === "node" ? findNode(S.roots, row.node) : null;
      let g = G.file;
      if (row.kind === "loading") g = G.load;
      else if (row.kind === "failed") g = G.fail;
      else if (node && T.isBranch(node)) g = !node.enabled ? G.lock : it.expansion === "expanded" ? G.open : G.folder;
      else if (/\.(h|cpp|mm)$/.test(it.label)) g = G.code;
      else if (/\.md$/.test(it.label)) g = G.md;
      r.append(exp, icon(g, "tv-ico"), el("span", { class: "tv-label", text: it.label }));
      return r;
    }

    function drawState() {
      stateBox.textContent = "";
      const kv = (k, v) => stateBox.append(el("dt", { text: k }), el("dd", null, v));
      kv("expanded", el("code", { text: "{ " + [...S.expanded].map(fmtKey).join(", ") + " }" }));
      if (multi.checked) kv("selection", el("code", { text: fmtSel(S.selection) }));
      else kv("selected · cursor", el("code", { text: (S.selected ? fmtKey(S.selected) : "—") + " · " + (S.cursor ? fmtKey(S.cursor) : "—") }));
      const pend = [...S.tokens.entries()].filter(([k, t]) => S.ledger.current(k, t));
      kv("request_ledger", el("code", { text: pend.length ? pend.map(([k, t]) => `${fmtKey(k)} #${t}`).join(", ") : "기다리는 요청 없음" }));
      kv("scroll_offset", el("code", { text: S.scroll + ".0f" }));
    }

    function log(input, messages, effect) {
      const entry = el("div", { class: "tv-entry" });
      entry.append(el("div", { class: "in", text: input }));
      for (const m of messages) entry.append(el("div", { class: "msg", text: m }));
      if (effect) entry.append(el("div", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 40) logBox.lastElementChild.remove();
    }

    // 펼친 가지 중 아직 묻지 않은 것에 불러오기를 시작한다 (tree_nodes_to_load → request_ledger::issue)
    function startLoads() {
      const keys = T.treeNodesToLoad(S.roots, S.expanded);
      const notes = [];
      for (const key of keys) {
        const node = findNode(S.roots, key);
        if (!node || !node.enabled) continue;
        const token = S.ledger.issue(key);
        S.tokens.set(key, token);
        node.load = "loading";
        notes.push(`${fmtKey(key)} #${token}`);
        const delay = 650 + Math.random() * 500;
        timers.push(setTimeout(() => arrive(key, token), delay));
      }
      return notes;
    }

    // 결과가 왔다: 그 키의 마지막 표인지 settle로 가른다
    function arrive(key, token) {
      const node = findNode(S.roots, key);
      if (!S.ledger.settle(key, token)) {
        log(`결과 도착 ${fmtKey(key)} #${token}`, [], "request_ledger::settle → 거짓: 낡은 결과라 버린다 (그사이 접었거나 다시 펼쳤다)");
        drawState();
        return;
      }
      S.tokens.delete(key);
      if (!node) return;
      const prev = items;
      if (S.failOnce.has(key)) {
        S.failOnce.delete(key);
        node.load = "failed";
        log(`결과 도착 ${fmtKey(key)} #${token}`, [], "settle → 참. 질의가 실패했다 → node_load::failed (실패 줄이 선다)");
      } else {
        node.children = (LAZY[key] || []).map((name) => {
          const dir = name.endsWith("/");
          const label = dir ? name.slice(0, -1) : name;
          const k = key + "/" + label;
          return { key: k, label, children: [], load: dir ? (LAZY[k] ? "unloaded" : "loaded") : "loaded", enabled: true };
        });
        node.load = "loaded";
        log(`결과 도착 ${fmtKey(key)} #${token}`, [], `settle → 참. 자식 ${node.children.length}개 → node_load::loaded`);
      }
      frame(prev);
    }

    function setExpanded(key, open, inputLabel) {
      const prev = items;
      const msgs = [`set_expanded(${fmtKey(key)}, ${open})`];
      let eff;
      if (open) {
        S.expanded.add(key);
        const node = findNode(S.roots, key);
        if (node && node.load === "failed") node.load = "unloaded"; // 다시 시도
        const loads = startLoads();
        eff = `expanded에 더한다` + (loads.length ? ` · tree_nodes_to_load → 요청 ${loads.join(", ")}` : "");
      } else {
        S.expanded.delete(key);
        const node = findNode(S.roots, key);
        eff = "expanded에서 뺀다";
        if (node && node.load === "loading") {
          S.ledger.cancel(key);
          node.load = "unloaded";
          eff += " · 불러오는 중이라 request_ledger::cancel — 늦게 오는 결과는 버려진다";
        }
      }
      frame(prev);
      log(inputLabel, msgs, eff + " · 다시 flatten_tree");
    }

    // element의 move_cursor: 커서 메시지 + 창 밖이면 scroll 메시지
    function moveCursor(index, ctx, inputLabel) {
      if (index < 0 || index >= items.length) {
        log(inputLabel, [], "옮길 자리가 없다 — 키는 목록이 삼키고 아무 메시지도 내지 않는다");
        return;
      }
      const key = items[index].key;
      const msgs = [];
      const moved = cursorIndex() !== index;
      let eff = "";
      if (multi.checked) {
        if (moved || !ctx.keep_selection || ctx.extend) {
          const parts = [];
          if (ctx.step) parts.push(".step = value_step::" + ctx.step);
          if (ctx.extend) parts.push(".extend = true");
          if (ctx.keep_selection) parts.push(".keep_selection = true");
          msgs.push(`move_with(${fmtKey(key)}, { ${parts.join(", ")} })`);
          S.selection = T.selectionMove(S.selection, items, key, { extend: !!ctx.extend, keep_selection: !!ctx.keep_selection });
          eff = "selection_move → " + fmtSel(S.selection);
        } else eff = "커서가 제자리인 Ctrl 걸음 — 메시지를 내지 않는다";
      } else if (moved) {
        msgs.push(`move(${fmtKey(key)})`);
        S.cursor = key;
        S.selected = items[index].enabled ? key : S.selected;
        eff = "앱 정책: 화살표가 곧 선택 → selected·cursor = " + fmtKey(key);
      } else eff = "이미 그 자리 — 커서 메시지를 내지 않는다";
      // 창 안으로 들이기 (virtual_list_reveal::nearest)
      const top = index * ROW;
      let delta = 0;
      if (top < S.scroll) delta = top - S.scroll;
      else if (top + ROW > S.scroll + VIEW_ROWS * ROW) delta = top + ROW - (S.scroll + VIEW_ROWS * ROW);
      if (delta !== 0) {
        msgs.push(`scroll(${delta}.0f)`);
        S.scroll += delta;
      }
      draw();
      log(inputLabel, msgs, eff);
    }

    function stepLabel(e, name) {
      const mods = [];
      if (e.ctrlKey || e.metaKey) mods.push("Ctrl");
      if (e.shiftKey) mods.push("Shift");
      return (mods.length ? mods.join("+") + "+" : "") + name;
    }

    view.addEventListener("keydown", (e) => {
      if (e.altKey) return;
      // 단계 키·←→·Ctrl+Space·Ctrl+A는 Ctrl만 본다. Alt·Meta와 함께면 목록이 쓰지 않고 앱으로 흐른다.
      const ctrl = e.ctrlKey;
      const primary = e.ctrlKey || e.metaKey;
      const ci = cursorIndex();
      const STEP = { ArrowUp: "decrease", ArrowDown: "increase", PageUp: "decrease_page", PageDown: "increase_page", Home: "minimum", End: "maximum" };
      const NAME = { ArrowUp: "↑", ArrowDown: "↓", PageUp: "PageUp", PageDown: "PageDown", Home: "Home", End: "End", ArrowLeft: "←", ArrowRight: "→" };
      if (STEP[e.key]) {
        if (e.metaKey || (!multi.checked && ctrl)) {
          log(stepLabel(e, NAME[e.key]), [], multi.checked ? "Meta와 함께면 목록의 키가 아니다 — 앱 정책(on_key)으로 흐른다" : "move만 든 목록에서 Ctrl+단계 키는 앱 단축키다 — 목록이 쓰지 않는다");
          return;
        }
        e.preventDefault();
        const step = STEP[e.key];
        const pr = step.endsWith("_page") ? T.pageRows(items, ci, step === "decrease_page", VIEW_ROWS * ROW, ROW) : 1;
        const target = T.stepTarget(items, ci, step, pr);
        const ctx = multi.checked ? { step, extend: e.shiftKey, keep_selection: ctrl } : { step };
        moveCursor(target, ctx, stepLabel(e, NAME[e.key]) + (step.endsWith("_page") ? ` (page_rows ${pr})` : ""));
        return;
      }
      if ((e.key === "ArrowLeft" || e.key === "ArrowRight") && !ctrl && !e.shiftKey && !e.metaKey) {
        e.preventDefault();
        const r = T.stepTree(items, ci, e.key === "ArrowRight", true);
        if (!r) log(NAME[e.key], [], "갈 곳이 없다 — 키를 소비하지 않아 위로 흐른다 (표라면 가로 스크롤이 된다)");
        else if (r.kind === "expand") setExpanded(r.key, true, NAME[e.key] + " (접힌 가지)");
        else if (r.kind === "collapse") setExpanded(r.key, false, NAME[e.key] + " (펼친 가지)");
        else if (r.kind === "move") moveCursor(r.index, {}, NAME[e.key] + (e.key === "ArrowRight" ? " (펼친 가지 → 첫 자식)" : " (부모로)"));
        else log(NAME[e.key], [], "자식이 없는 펼친 가지 — 키만 삼킨다");
        return;
      }
      if (e.key === " " && ctrl && !e.shiftKey && multi.checked) {
        e.preventDefault();
        if (ci < 0 || !items[ci].enabled) {
          log("Ctrl+Space", [], "커서가 비활성 항목 — 삼키기만 한다");
          return;
        }
        S.selection = T.selectionToggle(S.selection, items, items[ci].key);
        draw();
        log("Ctrl+Space", [`toggle_at_cursor(${fmtKey(items[ci].key)})`], "selection_toggle → " + fmtSel(S.selection));
        return;
      }
      if ((e.key === "a" || e.key === "A") && e.metaKey && multi.checked) {
        log("Cmd+A", [], "가상 목록의 key handler는 Alt·Meta가 함께면 키를 쓰지 않는다 — luil의 macOS host는 Command를 meta로도 실으므로 Command+A가 select_all에 닿지 않는다 (코드에서 확인한 동작)");
        return;
      }
      if ((e.key === "a" || e.key === "A") && ctrl && !e.shiftKey && multi.checked) {
        e.preventDefault();
        S.selection = T.selectionSelectAll(S.selection, items);
        draw();
        log("Ctrl+A", ["select_all()"], `selection_select_all → 활성 항목 ${S.selection.keys.length}개`);
        return;
      }
      if ((e.key === "Enter" || e.key === " ") && !ctrl && !e.metaKey) {
        e.preventDefault();
        if (ci < 0 || !items[ci].enabled) {
          log(e.key === " " ? "Space" : "Enter", [], "커서가 없거나 비활성 — activate를 내지 않는다");
          return;
        }
        activate(ci, e.key === " " ? "Space" : "Enter", "activate");
        return;
      }
      if (e.key.length === 1 && !primary && e.key !== " ") {
        e.preventDefault();
        const now = performance.now();
        if (now - typeahead.last > TYPEAHEAD_RESET) typeahead.query = "";
        const first = typeahead.query === "";
        typeahead.query += e.key;
        typeahead.last = now;
        const t = T.searchTarget(items, ci, typeahead.query, first, fold);
        if (t < 0) {
          log(`글자 "${e.key}" (질의 "${typeahead.query}")`, [], "맞는 항목이 없다 — 질의는 남겨 다음 글자가 이어 붙는다");
          return;
        }
        moveCursor(t, {}, `글자 "${e.key}" (질의 "${typeahead.query}", ${first ? "다음 항목부터" : "지금 항목부터"})`);
      }
    });

    function activate(i, inputLabel, factory) {
      const it = items[i];
      const row = rows[i];
      const msgs = [`${factory}(${fmtKey(it.key)})`];
      if (row.kind === "failed") {
        log(inputLabel, msgs, "실패 줄을 실행 → 앱이 node_load를 unloaded로 돌려 다시 묻는다");
        const node = findNode(S.roots, row.node);
        node.load = "unloaded";
        const loads = startLoads();
        frame(items);
        log("다시 시도", [], "tree_nodes_to_load → 요청 " + loads.join(", "));
        return;
      }
      const node = findNode(S.roots, row.node);
      if (node && T.isBranch(node)) {
        log(inputLabel, msgs, "앱 정책: 폴더를 실행하면 펼침을 뒤집는다");
        setExpanded(it.key, it.expansion !== "expanded", "→ 앱이 펼침을 바꾼다");
      } else log(inputLabel, msgs, `앱 정책: 파일을 연다 — "${it.label}"`);
    }

    // 초점 테: luil은 키보드로 옮긴 초점(Tab)에만 테를 그린다. 누름으로 잡은 초점은 화살표를 눌러도 테가 없다 (focus_visible)
    let pointerPress = false;
    view.addEventListener(
      "mousedown",
      () => {
        pointerPress = true;
        view.classList.remove("kbd");
        setTimeout(() => (pointerPress = false), 0);
      },
      true
    );
    view.addEventListener("focus", () => view.classList.toggle("kbd", !pointerPress));
    view.addEventListener("blur", () => view.classList.remove("kbd"));

    // 포인터: 클릭·두 번 누름은 누를 때가 아니라 뗄 때 난다. 누른 것(행 또는 삼각형) 위에서 떼야 하고, 판정은 뗄 때의 줄로 한다
    let press = null;
    let lastClick = { key: null, t: 0, x: 0, y: 0 };
    function partAt(e) {
      const r = e.target.closest(".tv-row");
      if (!r) return null;
      const i = Number(r.dataset.i);
      return items[i] ? { i, key: items[i].key, exp: !!e.target.closest("[data-exp]") } : null;
    }
    canvas.addEventListener("mousedown", (e) => {
      const p = partAt(e);
      if (!p) return;
      e.preventDefault();
      view.focus({ preventScroll: true });
      press = e.button === 0 ? p : null;
    });
    canvas.addEventListener("mouseup", (e) => {
      const p = partAt(e);
      const pressed = press;
      press = null;
      if (e.button !== 0 || !p || !pressed || p.key !== pressed.key || p.exp !== pressed.exp) return;
      const i = p.i;
      const it = items[i];
      if (p.exp) {
        lastClick = { key: null, t: 0, x: 0, y: 0 }; // 삼각형은 행과 다른 element라 두 번 누름의 짝이 끊긴다
        setExpanded(it.key, it.expansion !== "expanded", "삼각형 클릭");
        return;
      }
      if (!it.enabled) {
        log("비활성 행 클릭", [], "비활성 행은 눌리지 않는다");
        return;
      }
      const now = performance.now();
      // interaction_config::double_click_time 500ms · double_click_distance 4
      if (lastClick.key === it.key && now - lastClick.t <= 500 && Math.hypot(e.clientX - lastClick.x, e.clientY - lastClick.y) <= 4) {
        lastClick = { key: null, t: 0, x: 0, y: 0 };
        activate(i, "두 번 누름", "double_click");
        return;
      }
      lastClick = { key: it.key, t: now, x: e.clientX, y: e.clientY };
      const ctrl = e.ctrlKey || e.metaKey;
      // 누름은 고르기(pick·select)와 커서 옮기기를 함께 낸다. 커서가 이미 그 행이면 커서 메시지는 없다 (move_cursor)
      const moved = cursorIndex() !== i;
      if (multi.checked) {
        const ctx = { control: ctrl, shift: e.shiftKey };
        S.selection = T.selectionPick(S.selection, items, it.key, ctx);
        const parts = [];
        if (ctrl) parts.push(".control = true");
        if (e.shiftKey) parts.push(".shift = true");
        const msgs = [`pick(${fmtKey(it.key)}, { ${parts.join(", ")} })`];
        if (moved) {
          msgs.push(`move_with(${fmtKey(it.key)}, { .keep_selection = true })`);
          S.selection = T.selectionMove(S.selection, items, it.key, { extend: false, keep_selection: true });
        }
        draw();
        log(stepLabel(e, "클릭"), msgs, "selection_pick → " + fmtSel(S.selection));
      } else {
        S.selected = it.key;
        S.cursor = it.key;
        draw();
        log("클릭", [`select(${fmtKey(it.key)})`].concat(moved ? [`move(${fmtKey(it.key)})`] : []), "앱: selected·cursor = " + fmtKey(it.key));
      }
    });
    canvas.addEventListener("contextmenu", (e) => {
      const r = e.target.closest(".tv-row");
      if (!r) return;
      e.preventDefault();
      if (e.target.closest("[data-exp]")) {
        log("삼각형 우클릭", [], "가상 목록의 삼각형에는 우클릭 액션이 없고, 우클릭은 행으로 올라가지 않는다 — 메시지가 없다");
        return;
      }
      const it = items[Number(r.dataset.i)];
      if (!it.enabled) return;
      log("우클릭", [`context_menu(${fmtKey(it.key)}, { .position = … })`], "고르지도 커서를 옮기지도 않는다 — 고른 행을 먼저 고를지는 앱이 정한다");
    });
    view.addEventListener(
      "wheel",
      (e) => {
        if (!items.length) return;
        e.preventDefault();
        const notches = Math.sign(e.deltaY) * Math.max(1, Math.round(Math.abs(e.deltaY) / 100));
        const delta = notches * WHEEL;
        const before = S.scroll;
        S.scroll += delta;
        frame(null);
        if (S.scroll !== before) log("휠", [`scroll(${delta}.0f)`], `앱이 scroll_offset에 더한다 → arrange가 다듬어 ${S.scroll}`);
      },
      { passive: false }
    );

    q.addEventListener("input", () => {
      const prev = items;
      frame(prev);
      log(`거르기 "${q.value}"`, [], `앱이 tree_flatten_options::keep을 바꾼다 → 줄 ${items.length}개 · selection_reconcile`);
    });
    expm.addEventListener("change", () => {
      const prev = items;
      frame(prev);
      log(`expand_matches = ${expm.checked}`, [], "찾는 동안 맞는 자손으로 가는 길을 펼쳐 보인다. 펼침 상태(expanded)는 그대로다");
    });
    sort.addEventListener("change", () => {
      const prev = items;
      frame(prev);
      log(sort.value === "folders" ? "정렬: 폴더 먼저·이름순" : "정렬: 모델 순서", [], "tree_flatten_options::less — 같은 부모 아래 형제끼리만 견준다 (안정 정렬)");
    });
    multi.addEventListener("change", () => {
      const k = cursorKey();
      if (multi.checked) S.selection = { anchor: k, cursor: k, keys: k ? [k] : [] };
      else {
        S.cursor = S.selection.cursor;
        S.selected = S.selection.cursor;
      }
      draw();
      log(`multiple_selection = ${multi.checked}`, [], multi.checked ? "pick·move_with·toggle_at_cursor·select_all을 단다" : "select·move만 단다 (단일 선택)");
    });
    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilListsLab = { setup };
})();

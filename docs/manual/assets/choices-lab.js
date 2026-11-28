/* luil 설명서 — 체크·라디오·선택 묶음 실습
 *
 * 둘이다. 판정은 전부 assets/choicemodel.js(원본을 옮기고 저장소 시험 장면으로 맞춘 것)를 부른다.
 *  1. 선택 묶음 — 앞 단추 · choice_group_element · 뒤 단추가 선 tree 하나. Tab·화살표·Home·End·Space·Enter·글자 찾기·누름.
 *     앱 상태는 selected 하나이고, select(value) 메시지를 받으면 바꾼다. 다음 "frame"이 진입 자리(focus_entry)를 새 값으로 짓는다.
 *  2. 삼상 체크 — 부모 체크 하나와 자식 넷. 부모 상태는 aggregate_check, 누름은 toggle 메시지 하나, 다음 상태는 앱 규칙.
 * 글 폭은 브라우저의 글꼴로 잰다 — luil의 글꼴 측정을 옮길 수 없어 segmented 칸 폭은 근사다.
 */
(function () {
  "use strict";
  const M = window.LuilChoices;
  const { el } = window.LU;

  /* ---------- 1. 선택 묶음 ---------- */

  // tests/ui_interaction_tests.cpp 3175-3225의 항목 이름이다 (글자 찾기 시험)
  const ITEMS = [
    { value: "apple", label: "apple" },
    { value: "apricot", label: "apricot" },
    { value: "banana", label: "banana" },
    { value: "cherry", label: "cherry" },
  ];

  let canvas = null;
  function measure(text, weight) {
    if (!canvas) canvas = document.createElement("canvas");
    const g = canvas.getContext("2d");
    const family = getComputedStyle(document.body).fontFamily || "sans-serif";
    g.font = (weight === "semibold" ? "600 " : "400 ") + "12px " + family; // body_font_size 12, selected_choice semibold
    return g.measureText(text).width;
  }

  function setupGroup(root) {
    if (!root) return;
    const body = root.querySelector(".lw-body");
    const A = { style: "radio", width: "fit", inert: false, selected: "banana" };
    const initial = Object.assign({}, A);
    let ctl = new M.Controller();
    let pointer = false;

    // 앱이 짓는 tree — 앞 단추 · 묶음 · 뒤 단추 (choice_group_element.cpp 192-209의 축과 진입)
    const tree = () => [
      { id: "before", label: "앞 단추", action: true, enabled: true },
      { group: { id: "fruit", axis: A.style === "radio" ? "vertical" : "horizontal", entry: A.selected, members: ITEMS.map((it) => ({ id: it.value, label: it.label, action: !A.inert, enabled: true })) } },
      { id: "after", label: "뒤 단추", action: true, enabled: true },
    ];

    const seg = (name, options, get, set) => {
      const box = el("div", { class: "seg", role: "group", "aria-label": name });
      for (const [value, label] of options) box.append(el("button", { type: "button", "data-v": value, "aria-pressed": "false", onclick: () => { set(value); render(); } }, label));
      box._sync = () => box.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.v === get())));
      return box;
    };
    const styleSeg = seg("모양 (choice_style)", [["radio", "라디오 묶음"], ["toggle", "segmented"]], () => A.style, (v) => { A.style = v; log("모양을 바꿨다", null, v === "radio" ? "축 vertical — ↑/↓가 돈다" : "축 horizontal — ←/→가 돈다"); });
    const widthSeg = seg("폭 (choice_width)", [["fit", "fit"], ["fill", "fill"]], () => A.width, (v) => (A.width = v));
    const inertBox = el("input", { type: "checkbox", onchange: () => { A.inert = inertBox.checked; if (A.inert && ctl.focused && ITEMS.some((i) => i.value === ctl.focused)) { ctl.focused = null; ctl.focusVisible = false; } log(A.inert ? "select를 비웠다" : "select를 다시 달았다", null, A.inert ? "항목이 눌리지 않고 Tab 자리도 없다 — 묶음이 순서에서 빠진다" : "묶음이 Tab 자리로 돌아온다"); render(); } });

    const stage = el("div", { class: "cg-stage", tabindex: "0", role: "group", "aria-label": "선택 묶음 재현 — 초점을 두고 Tab·화살표·Space·글자로 다룬다" });
    const stateOut = el("dl", { class: "cg-state", "aria-label": "상태" });
    const access = el("ul", { class: "cg-access", "aria-label": "보조 기술이 읽는 것" });
    const logBox = el("div", { class: "cg-log", "aria-label": "메시지 기록" });

    body.append(
      el("div", { class: "cg-tools" }, styleSeg, widthSeg, el("label", { class: "cg-check" }, inertBox, " select 비움 (표시 전용)")),
      el("div", { class: "cg-grid" },
        el("div", { class: "cg-side" }, stage, el("div", { class: "cg-keys" }, el("span", {}, el("kbd", {}, "Tab"), " 자리"), el("span", {}, el("kbd", {}, "↑"), el("kbd", {}, "↓"), " / ", el("kbd", {}, "←"), el("kbd", {}, "→"), " 초점"), el("span", {}, el("kbd", {}, "Home"), el("kbd", {}, "End")), el("span", {}, el("kbd", {}, "Space"), el("kbd", {}, "Enter"), " 고르기"), el("span", {}, "글자 찾기 (a·b·c…)")), stateOut),
        el("div", { class: "cg-side" }, el("div", { class: "cg-sub", text: "보조 기술이 읽는 것" }), access, logBox),
      ),
    );

    function log(what, msg, eff) {
      logBox.prepend(el("div", { class: "cg-entry" }, el("div", { class: "in", text: what }), msg ? el("div", { class: "msg", text: msg }) : null, eff ? el("div", { class: "eff", text: eff }) : null));
      while (logBox.childNodes.length > 40) logBox.lastChild.remove();
    }

    // 앱의 handle: select(value) → selected를 바꾼다 (examples/widgets/app.cpp 67-71과 같다)
    function runAction(id, how) {
      if (id === "before" || id === "after") {
        log(how, (id === "before" ? "앞" : "뒤") + " 단추의 left_click 액션", "이 실습에서는 아무 상태도 바꾸지 않는다");
        return;
      }
      const was = A.selected;
      A.selected = id;
      log(how, "select(\"" + id + "\") → choose_fruit_intent { " + id + " }", was === id ? "이미 고른 값이어도 메시지는 나간다 (앱이 같은 값을 다시 넣는다)" : "앱이 selected를 " + was + " → " + id + "로 바꾼다. 다음 frame의 진입 자리도 " + id + "다");
    }

    const keyName = { ArrowUp: "arrow_up", ArrowDown: "arrow_down", ArrowLeft: "arrow_left", ArrowRight: "arrow_right", Home: "home", End: "end", Enter: "enter", " ": "space", Tab: "tab", Escape: "escape" };
    const arrowText = { arrow_up: "↑", arrow_down: "↓", arrow_left: "←", arrow_right: "→", home: "Home", end: "End", enter: "Enter", space: "Space", tab: "Tab", escape: "Esc" };
    const reasonText = {
      axis: "축이 맞지 않는 화살표다 — 묶음이 받지 않고 key_handler·정책(on_key)으로 흐른다",
      modifier: "수식 키(Ctrl·Alt·Meta)가 있다 — 앱 단축키로 흐른다",
      outside: "초점이 묶음 밖이다 — 이 키는 앱으로 흐른다",
      single: "구성원이 하나뿐이다 — 묶음이 받지 않는다",
      "no-focus": "초점이 없다",
      "no-action": "초점 자리에 액션이 없다",
      "not-group-key": "묶음의 키가 아니다 — 앱으로 흐른다",
    };

    stage.addEventListener("mousedown", () => (pointer = true));
    stage.addEventListener("focus", () => {
      // 문서의 Tab으로 들어왔으면 luil의 Tab 한 번으로 친다 (앞 단추부터)
      if (!pointer && ctl.focused === null) {
        const r = ctl.key(tree(), { key: "tab", shift: false });
        if (r.by === "tab") log("Tab (들어옴)", null, "초점 → " + ctl.focused);
        render();
      }
      pointer = false;
    });
    stage.addEventListener("blur", () => render());
    stage.addEventListener("keydown", (e) => {
      const key = keyName[e.key];
      const ev = { key, shift: e.shiftKey, control: e.ctrlKey, alt: e.altKey, meta: e.metaKey };
      if (key === "tab") {
        if (e.ctrlKey || e.altKey || e.metaKey) return;
        const order = M.focusOrder(tree());
        const at = ctl.focused === null ? -1 : order.indexOf(M.focusStopOf(tree(), ctl.focused));
        const leaving = at >= 0 && (e.shiftKey ? at === 0 : at === order.length - 1);
        if (leaving || !order.length) {
          // luil 창에서는 처음으로 돈다. 실습은 문서로 나간다
          log(e.shiftKey ? "Shift+Tab" : "Tab", null, "luil에서는 끝에서 처음으로 돈다 — 실습은 문서의 다음 자리로 나간다");
          ctl.focused = null;
          ctl.focusVisible = false;
          render();
          return;
        }
        e.preventDefault();
        const from = ctl.focused;
        ctl.key(tree(), ev);
        log(e.shiftKey ? "Shift+Tab" : "Tab", null, "초점 " + (from || "없음") + " → " + ctl.focused + (ITEMS.some((i) => i.value === ctl.focused) && from && !ITEMS.some((i) => i.value === from) ? " (묶음의 진입 자리 = selected)" : ""));
        render();
        return;
      }
      if (key) {
        e.preventDefault();
        const from = ctl.focused;
        const r = ctl.key(tree(), ev);
        const name = (e.ctrlKey ? "Ctrl+" : "") + (e.altKey ? "Alt+" : "") + (e.metaKey ? "Meta+" : "") + (e.shiftKey && key !== "tab" ? "Shift+" : "") + arrowText[key];
        if (r.by === "activation") runAction(r.run, name);
        else if (r.by === "group") log(name, null, r.why === "jump" ? (from === ctl.focused ? "이미 끝이다 — 돌지 않지만 키는 묶음이 가진다" : "초점 " + from + " → " + ctl.focused + " (돌지 않는다, 메시지 없음)") : "초점 " + from + " → " + ctl.focused + (r.why === "wrap" ? " (끝에서 돌았다)" : "") + " — 메시지 없음, 고르지 않는다");
        else log(name, null, reasonText[r.why] || "앱으로 흐른다");
        // Windows(WM_CHAR ' ')와 Android는 Space를 키와 글자로 함께 보낸다. 글자는 묶음의 글자 찾기로 간다
        if (key === "space" && !e.ctrlKey && !e.altKey && !e.metaKey) {
          const t = ctl.type(tree(), " ", e.timeStamp);
          if (!t.ignored) log("글자 ' ' (Space가 함께 보낸 글자)", null, "질의 “" + t.query + "” — 맞는 항목이 없어 자리는 그대로다. 1초 안에 친 글자는 이 공백 뒤에 붙는다");
        }
        render();
        return;
      }
      if (e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey) {
        e.preventDefault();
        const t = ctl.type(tree(), e.key, e.timeStamp);
        if (t.ignored) log("글자 '" + e.key + "'", null, "묶음 밖이다 — 글자는 그냥 사라진다");
        else log("글자 '" + e.key + "'", null, "질의 “" + t.query + "”" + (t.reset ? " (새 질의)" : "") + (t.first ? " — 첫 글자: 다음 항목부터" : " — 이어 친 글자: 지금 항목부터") + " → " + (t.moved ? "초점 " + t.to : "자리 그대로") + ". 메시지 없음");
        render();
      }
    });

    let rects = [];
    function draw() {
      stage.replaceChildren();
      const ring = (id) => ctl.focused === id && ctl.focusVisible && document.activeElement === stage;
      const button = (id, label) => {
        const b = el("div", { class: "cg-btn" + (ring(id) ? " ring" : ""), "data-id": id, text: label });
        b.addEventListener("click", () => press(id));
        return b;
      };
      stage.append(button("before", "앞 단추"));
      const inner = el("div", { class: "cg-group " + A.style, "data-id": "fruit" });
      if (A.style === "radio") {
        ITEMS.forEach((it) => {
          const sel = it.value === A.selected;
          const row = el("div", { class: "cg-radio" + (ring(it.value) ? " ring" : "") + (A.inert ? " inert" : ""), "data-id": it.value },
            el("span", { class: "cg-ring" + (sel ? " on" : "") }), el("span", { class: "cg-text", text: it.label }));
          row.addEventListener("click", (e) => { e.stopPropagation(); press(it.value); });
          inner.append(row);
        });
      } else {
        const slotWidth = Math.max(0, stage.clientWidth - 24);
        const laid = M.arrangeChoices({ style: "toggle", width: A.width, items: ITEMS }, { x: 0, y: 0, w: slotWidth, h: M.TOGGLE_HEIGHT }, measure);
        rects = laid.children;
        inner.style.width = laid.bounds.w + "px";
        ITEMS.forEach((it, i) => {
          const r = laid.children[i];
          const sel = it.value === A.selected;
          const cell = el("div", { class: "cg-seg" + (sel ? " on" : "") + (ring(it.value) ? " ring" : "") + (A.inert ? " inert" : ""), "data-id": it.value, style: { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" } }, el("span", { text: it.label }));
          cell.addEventListener("click", (e) => { e.stopPropagation(); press(it.value); });
          inner.append(cell);
        });
      }
      stage.append(inner, button("after", "뒤 단추"));
    }

    function press(id) {
      const r = ctl.press(tree(), id);
      if (r.run) runAction(r.run, "누름 (" + id + ")");
      else log("누름 (" + id + ")", null, "액션이 없다 — 초점을 거둔다");
      stage.focus();
      render();
    }
    stage.addEventListener("click", (e) => {
      if (e.target === stage) {
        ctl.press(tree(), null);
        render();
      }
    });

    function render() {
      styleSeg._sync();
      widthSeg._sync();
      widthSeg.querySelectorAll("button").forEach((b) => (b.disabled = A.style === "radio"));
      inertBox.checked = A.inert;
      draw();
      const t = tree();
      const order = M.focusOrder(t);
      const kv = (k, v) => [el("dt", { text: k }), el("dd", {}, v)];
      const q = ctl.query ? "“" + ctl.query + "”" : "비어 있음";
      stateOut.replaceChildren(
        ...kv("selected (앱)", el("code", { text: A.selected })),
        ...kv("focused (controller)", ctl.focused === null ? "없음" : el("code", { text: ctl.focused })),
        ...kv("focus_visible", ctl.focusVisible ? "참 — 키보드로 옮긴 초점, 테를 그린다" : "거짓 — 누름으로 잡았거나 없다"),
        ...kv("Tab 순서", order.join(" → ") || "없음"),
        ...kv("묶음 축", A.style === "radio" ? "vertical (↑/↓)" : "horizontal (←/→)"),
        ...kv("글자 찾기 질의", q),
      );
      if (A.style === "toggle") {
        const pref = M.choicePreferredSize({ style: "toggle", width: A.width, items: ITEMS }, Math.max(0, stage.clientWidth - 24), measure);
        stateOut.append(...kv("칸 폭", (rects[0] ? Math.round(rects[0].w * 10) / 10 : 0) + " (fit 맞춤 " + M.fittedSegmentWidth(ITEMS, measure) + ", preferred " + Math.round(pref.w) + ")"));
      }
      access.replaceChildren(
        el("li", {}, el("code", { text: "radio_group" }), " “과일” (choice_group_config::name)"),
        ...ITEMS.map((it) => el("li", { class: "sub" }, el("code", { text: "radio_button" }), " “" + it.label + "”" + (it.value === A.selected ? " · selected" : "") + (A.inert ? " · 누를 수 없음" : ""))),
      );
    }

    root.addEventListener("lw-reset", () => {
      Object.assign(A, initial);
      ctl = new M.Controller();
      logBox.replaceChildren();
      log("처음 상태", null, "앱의 selected는 banana다. 묶음에 들어오면 banana에 선다");
      render();
    });
    if (window.ResizeObserver) new ResizeObserver(() => render()).observe(stage);
    log("처음 상태", null, "앱의 selected는 banana다. 묶음에 들어오면 banana에 선다");
    render();
  }

  /* ---------- 2. 삼상 체크 ---------- */

  function setupTristate(root) {
    if (!root) return;
    const body = root.querySelector(".lw-body");
    const FILES = ["main.cpp", "app.h", "app.cpp", "README.md"];
    const T = { selected: { "main.cpp": true, "app.h": true, "app.cpp": false, "README.md": false }, style: "checkbox" };
    const initial = JSON.parse(JSON.stringify(T));

    const styleSel = el("select", { "aria-label": "부모 체크의 check_style" }, ["checkbox", "toggle_switch", "toggle_button", "toggle_chip", "radio"].map((s) => el("option", { value: s }, "부모: " + s)));
    const list = el("div", { class: "tri-list" });
    const info = el("div", { class: "table-wrap" });
    const logBox = el("div", { class: "cg-log short", "aria-label": "메시지 기록" });
    body.append(el("div", { class: "cg-tools" }, styleSel), el("div", { class: "cg-grid" }, el("div", { class: "cg-side" }, list, info), logBox));

    const log = (what, msg, eff) => {
      logBox.prepend(el("div", { class: "cg-entry" }, el("div", { class: "in", text: what }), msg ? el("div", { class: "msg", text: msg }) : null, eff ? el("div", { class: "eff", text: eff }) : null));
      while (logBox.childNodes.length > 30) logBox.lastChild.remove();
    };

    const parentState = () => M.aggregateCheck(FILES.map((f) => (T.selected[f] ? "checked" : "unchecked")));

    function checkRow(owner, label, cfg, onToggle, child) {
      const a = M.checkAccess(Object.assign({ label }, cfg));
      const mixedShown = a.mixed; // 그림도 mixed && checkbox일 때만 가운데 줄이다 (check_element.cpp 174)
      const on = cfg.style === "radio" ? a.selected : a.checked;
      const b = el("button", { type: "button", class: "tri-row" + (child ? " child" : "") + " st-" + cfg.style, role: cfg.style === "toggle_button" || cfg.style === "toggle_chip" ? "button" : cfg.style === "radio" ? "radio" : cfg.style === "toggle_switch" ? "switch" : "checkbox" },
        el("span", { class: "tri-box" + (mixedShown ? " mixed" : on ? " on" : "") }),
        el("span", { class: "tri-label", text: label }));
      if (b.getAttribute("role") === "checkbox") b.setAttribute("aria-checked", mixedShown ? "mixed" : String(on));
      else if (b.getAttribute("role") === "button") b.setAttribute("aria-pressed", String(on));
      else b.setAttribute("aria-checked", String(on));
      b.addEventListener("click", onToggle);
      return b;
    }

    function render() {
      styleSel.value = T.style;
      const state = parentState();
      const parentCfg = { style: T.style, checked: state === "checked", mixed: state === "mixed" };
      list.replaceChildren(
        checkRow("folder", "src", parentCfg, () => {
          // examples/demo/workbench_page.cpp 766-774의 규칙: 모두 켜졌으면 모두 끄고, 그 밖(일부 포함)은 모두 켠다
          const all = FILES.every((f) => T.selected[f]);
          FILES.forEach((f) => (T.selected[f] = !all));
          log("부모 체크를 누름", "toggle → toggle_folder_intent {}", "앱 규칙: " + (all ? "모두 켜져 있었다 → 모두 끈다" : "모두 켜져 있지 않았다(" + (state === "mixed" ? "일부" : "꺼짐") + ") → 모두 켠다"));
          render();
        }),
        ...FILES.map((f) => checkRow("file:" + f, f, { style: "checkbox", checked: T.selected[f] }, () => {
          T.selected[f] = !T.selected[f];
          log("자식 " + f + " 을(를) 누름", "toggle → toggle_file_intent { " + f + " }", "앱이 그 파일만 뒤집는다. 부모는 다음 frame에 aggregate_check로 다시 모은다 → " + parentState());
          render();
        }, true)),
      );
      const a = M.checkAccess(Object.assign({ label: "src" }, parentCfg));
      const toggleState = a.role === "check_box" ? (a.mixed ? "Indeterminate" : a.checked ? "On" : "Off") : a.role === "radio_button" ? "SelectionItem (selected " + (a.selected ? "참" : "거짓") + ")" : a.checked ? "On" : "Off";
      info.replaceChildren(el("table", { class: "t tri-table" },
        el("thead", {}, el("tr", {}, el("th", { text: "부모의 값" }), el("th", { text: "지금" }))),
        el("tbody", {},
          el("tr", {}, el("td", {}, el("code", { text: "aggregate_check(자식)" })), el("td", { text: state })),
          el("tr", {}, el("td", {}, el("code", { text: "check_config" })), el("td", { text: "checked " + (parentCfg.checked ? "참" : "거짓") + " · mixed " + (parentCfg.mixed ? "참" : "거짓") })),
          el("tr", {}, el("td", {}, el("code", { text: "access_info" })), el("td", { text: a.role + " · checked " + (a.checked ? "참" : "거짓") + " · mixed " + (a.mixed ? "참" : "거짓") + (a.role === "radio_button" ? " · selected " + (a.selected ? "참" : "거짓") : "") })),
          el("tr", {}, el("td", { text: "UIA (Windows)" }), el("td", { text: toggleState })),
        )));
    }

    styleSel.addEventListener("change", () => {
      T.style = styleSel.value;
      log("부모 모양 → " + T.style, null, T.style === "checkbox" ? "일부 상태를 가운데 줄로 그리고 Indeterminate로 읽힌다" : "mixed는 checkbox에만 뜻이 있다 — 이 모양은 일부를 그리지도 읽히지도 않는다");
      render();
    });
    root.addEventListener("lw-reset", () => {
      Object.assign(T, JSON.parse(JSON.stringify(initial)));
      logBox.replaceChildren();
      render();
    });
    log("처음 상태", null, "자식 넷 가운데 둘이 켜져 있다 → 부모는 일부(mixed)");
    render();
  }

  window.LuilChoicesLab = { setupGroup, setupTristate };
})();

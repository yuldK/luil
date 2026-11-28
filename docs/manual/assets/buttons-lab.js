/* luil 설명서 — 단추와 명령 실습
 *
 * 둘이다. 판정은 전부 assets/commandmodel.js(commands.cpp·command_palette.cpp를 옮기고 원본과 대조한 것)를 부른다.
 *  1. 단축키 판정기 — 물리 키 → key_pressed_event → match_command·chord_of·shortcut_label·system_reserved_chord
 *     물리 키를 이벤트로 바꾸는 갈래는 플랫폼 host의 것을 옮겼다:
 *       Win32  window_surface.cpp post_key_event + surface_input.cpp modified_key_from_virtual
 *       macOS  macos_input.cpp key_event_for (Command+화살표 바꾸기, 글자 키 관문)
 *  2. 명령 팔레트 — 질의 → search_query → filter_commands·command_palette_items → ↑/↓·Enter·Esc (process_menu_key)
 * 브라우저의 키 코드를 key_code 이름으로 옮기는 짝은 이 실습이 지은 것이다 (미국 배열 자리 이름).
 */
(function () {
  "use strict";
  const C = window.LuilCommands;
  const { el, icon } = window.LU;

  /* ---------- 키 이름 ---------- */

  const LETTERS = "abcdefghijklmnopqrstuvwxyz".split("");
  const DIGITS = "0123456789".split("");
  const PUNCT = ["minus", "equal", "comma", "period", "slash", "semicolon", "quote", "bracket_left", "bracket_right", "backslash", "grave"];
  const KEY_OPTIONS = [
    ...LETTERS.map((c) => "key_" + c),
    ...DIGITS.map((c) => "key_" + c),
    "enter", "escape", "tab", "space", "backspace", "delete_forward", "insert", "home", "end", "page_up", "page_down",
    "arrow_left", "arrow_right", "arrow_up", "arrow_down",
    "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
    ...PUNCT,
    "numpad_0", "numpad_1", "numpad_5", "numpad_add", "numpad_subtract", "numpad_multiply", "numpad_divide", "numpad_decimal",
  ];
  const keyCaption = (k) => C.keyText(k) || k;

  // 브라우저의 code → key_code 이름 (실습이 지은 짝)
  function keyOfCode(code) {
    let m = /^Key([A-Z])$/.exec(code);
    if (m) return "key_" + m[1].toLowerCase();
    m = /^Digit([0-9])$/.exec(code);
    if (m) return "key_" + m[1];
    m = /^F([0-9]{1,2})$/.exec(code);
    if (m && +m[1] >= 1 && +m[1] <= 12) return "f" + m[1];
    m = /^Numpad([0-9])$/.exec(code);
    if (m) return "numpad_" + m[1];
    const map = {
      Enter: "enter", NumpadEnter: "enter", Escape: "escape", Tab: "tab", Space: "space", Backspace: "backspace", Delete: "delete_forward", Insert: "insert",
      Home: "home", End: "end", PageUp: "page_up", PageDown: "page_down", ArrowLeft: "arrow_left", ArrowRight: "arrow_right", ArrowUp: "arrow_up", ArrowDown: "arrow_down",
      Minus: "minus", Equal: "equal", Comma: "comma", Period: "period", Slash: "slash", Semicolon: "semicolon", Quote: "quote", BracketLeft: "bracket_left", BracketRight: "bracket_right",
      Backslash: "backslash", Backquote: "grave", NumpadAdd: "numpad_add", NumpadSubtract: "numpad_subtract", NumpadMultiply: "numpad_multiply", NumpadDivide: "numpad_divide", NumpadDecimal: "numpad_decimal",
    };
    return map[code] || null;
  }

  // 글자를 만드는 키 — Win32는 MapVirtualKey가 글자를 주는 키, macOS는 자리 이름이 있는 키다 (영문자·숫자·구두점·키패드)
  const characterKey = (k) => /^key_[a-z0-9]$/.test(k) || PUNCT.includes(k) || k.startsWith("numpad_");

  // 물리 키 → key_pressed_event. 키가 아니면(글자 입력으로 간다) { none: 까닭 }
  function eventFor(platform, phys) {
    if (platform === "mac") {
      // macos_input.cpp key_event_for 343-383
      let key = phys.key;
      let primary = phys.meta, command = phys.meta;
      let remap = "";
      if (phys.meta && !phys.ctrl) {
        if (key === "arrow_left" || key === "arrow_right") {
          key = key === "arrow_left" ? "home" : "end";
          primary = false;
          command = false;
          remap = "Command+" + (phys.key === "arrow_left" ? "←" : "→") + "는 " + keyCaption(key) + "로 바뀌고 Command가 빠진다";
        } else if (key === "arrow_up" || key === "arrow_down") {
          key = key === "arrow_up" ? "home" : "end";
          command = false;
          remap = "Command+" + (phys.key === "arrow_up" ? "↑" : "↓") + "는 주 단축키+" + keyCaption(key) + "가 된다 (meta는 빠진다)";
        }
      }
      if (characterKey(phys.key) && key !== "space" && !phys.meta && !phys.ctrl) return { none: "macOS는 Command나 Control이 없는 글자 키를 키로 보내지 않는다 — 문자 입력이다 (Option 글자도 그렇다)" };
      return { event: { key, control: phys.ctrl, shift: phys.shift, alt: phys.alt, meta: command, primary_shortcut: primary, repeat: phys.repeat }, remap };
    }
    // Win32: window_surface.cpp post_key_event 1585-1611, surface_input.cpp modified_key_from_virtual 218-240
    if (characterKey(phys.key) && !phys.ctrl && !phys.alt) return { none: "Win32는 Ctrl이나 Alt가 없는 글자 키를 키로 보내지 않는다 — 문자 입력(WM_CHAR)이다" };
    return { event: { key: phys.key, control: phys.ctrl, shift: phys.shift, alt: phys.alt, meta: phys.meta, primary_shortcut: null, repeat: phys.repeat }, remap: "" };
  }

  const yesno = (b) => (b ? "참" : "거짓");
  const ch = (key, control, shift, alt) => C.chord(key, control, shift, alt);

  /* ---------- 1. 단축키 판정기 ---------- */

  // 실습의 명령표 — 저장소의 명령들이다 (괄호는 출처)
  function shortcutTable(override) {
    const find = { id: "edit.find", label: "Find in Files", shortcut: ch("key_f", true), user_shortcuts: override ? [ch("key_f", true, true)] : null };
    return [
      { id: "file.save", label: "저장", shortcut: ch("key_s", true), from: "command_palette_tests" },
      { id: "view.sidebar", label: "사이드바 접기·펴기", shortcut: ch("key_b", true), from: "demo palette.cpp" },
      { id: "edit.undo", label: "실행 취소", shortcut: ch("key_z", true), from: "make_undo_commands" },
      { id: "edit.redo", label: "다시 실행", shortcut: ch("key_y", true), extra_shortcuts: [ch("key_z", true, true)], from: "make_undo_commands" },
      { id: "format.bold", label: "굵게", shortcut: ch("key_b", true), scope: "focused", checkable: true, from: "make_rich_text_commands" },
      { id: "dialog.confirm", label: "확인", shortcut: ch("enter", true), scope: "modal", from: "위 범위 예제" },
      Object.assign(find, { from: "command_palette_tests" }),
    ];
  }

  function setupShortcuts(root) {
    if (!root) return;
    const body = root.querySelector(".lw-body");
    const S = { platform: "win", scope: "window", ctrl: true, shift: false, alt: false, meta: false, key: "key_s", repeat: false, override: true, parse: "Ctrl+Shift+P" };
    const initial = Object.assign({}, S);

    const seg = (name, options, get, set) => {
      const box = el("div", { class: "seg", role: "group", "aria-label": name });
      for (const [value, label] of options)
        box.append(el("button", { type: "button", "data-v": value, "aria-pressed": "false", onclick: () => { set(value); render(); } }, label));
      box._sync = () => box.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.v === get())));
      return box;
    };
    const toggle = (label, prop, title) => {
      const b = el("button", { type: "button", class: "btn sm sc-mod", "aria-pressed": "false", title, onclick: () => { S[prop] = !S[prop]; render(); } }, label);
      b._sync = () => b.setAttribute("aria-pressed", String(!!S[prop]));
      return b;
    };

    const platformSeg = seg("플랫폼", [["win", "Windows·Android"], ["mac", "macOS·iOS"]], () => S.platform, (v) => (S.platform = v));
    const scopeSeg = seg("지금 범위", [["window", "window"], ["focused", "focused"], ["modal", "modal"]], () => S.scope, (v) => (S.scope = v));
    const mods = [toggle("Ctrl", "ctrl", "물리 Ctrl (macOS에서는 Control)"), toggle("Shift", "shift", "물리 Shift"), toggle("Alt", "alt", "물리 Alt (macOS에서는 Option)"), toggle("Meta", "meta", "Windows 키 / macOS Command")];
    const keySel = el("select", { "aria-label": "키 (key_code)", onchange: () => { S.key = keySel.value; render(); } }, KEY_OPTIONS.map((k) => el("option", { value: k }, keyCaption(k) + "  ·  " + k)));
    const repeatBox = el("input", { type: "checkbox", onchange: () => { S.repeat = repeatBox.checked; render(); } });
    const overrideBox = el("input", { type: "checkbox", onchange: () => { S.override = overrideBox.checked; render(); } });
    const capture = el("div", { class: "sc-capture", tabindex: "0", role: "button", "aria-label": "키보드로 누르기: 이 칸에 초점을 두고 조합을 누른다" }, icon("EAC4"), el("span", { text: "여기를 누르고 조합을 친다 (Tab·Shift+Tab은 빠져나간다)" }));
    capture.addEventListener("keydown", (e) => {
      if (e.key === "Tab" && !e.ctrlKey && !e.altKey && !e.metaKey) return; // 문서의 Tab은 막지 않는다
      const key = keyOfCode(e.code);
      if (!key) return; // 수정자 키 자체는 키가 아니다
      e.preventDefault();
      Object.assign(S, { key, ctrl: e.ctrlKey, shift: e.shiftKey, alt: e.altKey, meta: e.metaKey, repeat: e.repeat });
      render();
    });
    const parseInput = el("input", { type: "text", value: S.parse, "aria-label": "글로 적은 단축키 (parse_shortcut)", spellcheck: "false", autocomplete: "off", oninput: () => { S.parse = parseInput.value; render(); } });

    const evOut = el("dl", { class: "sc-kv", "aria-label": "판정" });
    const tableBody = el("tbody");
    const parseOut = el("div", { class: "sc-parse" });
    const note = el("div", { class: "sc-note", "aria-live": "polite" });

    body.append(
      el("div", { class: "sc-row" }, el("span", { class: "sc-lab", text: "플랫폼" }), platformSeg, el("span", { class: "sc-lab", text: "지금 범위" }), scopeSeg),
      el("div", { class: "sc-row" }, el("span", { class: "sc-lab", text: "물리 키" }), ...mods, keySel, el("label", { class: "sc-check" }, repeatBox, " 반복(repeat)")),
      capture,
      el("div", { class: "sc-grid" },
        el("div", { class: "sc-col" }, evOut, note),
        el("div", { class: "sc-col" },
          el("div", { class: "table-wrap" }, el("table", { class: "t sc-table" }, el("thead", {}, el("tr", {}, el("th", { text: "명령" }), el("th", { text: "범위" }), el("th", { text: "실제 단축키" }), el("th", { text: "판정" }))), tableBody)),
          el("label", { class: "sc-check" }, overrideBox, " edit.find에 사용자 단축키 Ctrl+Shift+F (user_shortcuts)"),
        ),
      ),
      el("div", { class: "sc-row sc-parse-row" }, el("span", { class: "sc-lab", text: "글로 적은 단축키" }), parseInput, parseOut),
    );

    const kv = (k, v, cls) => [el("dt", { text: k }), el("dd", { class: cls || null }, v)];

    function render() {
      platformSeg._sync();
      scopeSeg._sync();
      mods.forEach((m) => m._sync());
      mods[2].textContent = S.platform === "mac" ? "Option" : "Alt";
      mods[3].textContent = S.platform === "mac" ? "Command" : "Win";
      mods[0].textContent = S.platform === "mac" ? "Control" : "Ctrl";
      keySel.value = S.key;
      repeatBox.checked = S.repeat;
      overrideBox.checked = S.override;
      const mac = S.platform === "mac";
      const table = shortcutTable(S.override);
      const made = eventFor(S.platform, { key: S.key, ctrl: S.ctrl, shift: S.shift, alt: S.alt, meta: S.meta, repeat: S.repeat });

      evOut.replaceChildren();
      note.replaceChildren();
      let matchedId = null, scopeHit = null;
      if (made.none) {
        evOut.append(...kv("key_pressed_event", "만들지 않는다"));
        note.append(el("p", { text: made.none + ". 단축키 판정까지 가지 않는다." }));
      } else {
        const e = made.event;
        const prim = e.primary_shortcut === null ? "비어 있음 → control을 따른다" : yesno(e.primary_shortcut);
        evOut.append(
          ...kv("key", el("code", { text: e.key })),
          ...kv("control · shift · alt · meta", [yesno(e.control), yesno(e.shift), yesno(e.alt), yesno(e.meta)].join(" · ")),
          ...kv("primary_shortcut", prim),
          ...kv("주 단축키 수정자", C.primaryDown(e) ? "눌림" : "안 눌림"),
          ...kv("여분 수정자", C.extraModifierDown(e) ? "있음 — 어느 조합과도 맞지 않는다" : "없음", C.extraModifierDown(e) ? "warn" : null),
        );
        const c = C.chordOf(e);
        const wl = C.shortcutLabel(c, false), ml = C.shortcutLabel(c, true);
        evOut.append(...kv("chord_of", wl ? wl + "   (macOS 글 " + ml + ")" : "글이 없는 키"));
        if (C.systemReservedChord(c)) evOut.append(...kv("system_reserved_chord", "참 — 단축키로 정하지 않는다", "warn"));
        if (made.remap) note.append(el("p", { text: made.remap + "." }));
        matchedId = C.matchCommand(table, e, S.scope);
        if (matchedId !== null) scopeHit = table.find((cmd) => cmd.id === matchedId).scope || "window";
        let verdict;
        if (matchedId !== null) verdict = el("span", {}, el("code", { text: matchedId }), scopeHit === S.scope ? " — " + S.scope + " 범위에서 맞았다" : " — focused에 없어 window로 물러섰다");
        else verdict = S.scope === "modal" ? "없다 — modal은 window로 물러서지 않는다" : "없다 — on_key가 빈 목록을 돌려준다";
        evOut.append(...kv("match_command", verdict, matchedId !== null ? "ok" : null));
        if (e.repeat) note.append(el("p", { text: "repeat가 참이어도 match_command는 같은 답을 낸다. 한 번만 반응할 명령은 앱이 on_key에서 event.repeat를 보고 거른다." }));
      }

      tableBody.replaceChildren();
      for (const cmd of table) {
        const eff = C.effectiveShortcuts(cmd).map((x) => C.shortcutLabel(x, mac)).join(", ") || "없음";
        const scope = cmd.scope || "window";
        let state = "";
        const hits = !made.none && C.effectiveShortcuts(cmd).some((x) => C.matches(x, made.event));
        if (matchedId === cmd.id) state = "맞음";
        else if (hits && scope === S.scope) state = "가려짐 — 표에서 앞선 명령이 이긴다";
        else if (hits && S.scope === "focused" && scope === "window") state = scopeHit === "focused" ? "가려짐 — focused 명령이 먼저다" : "가려짐 — 표에서 앞선 명령이 이긴다";
        else if (hits) state = "조합은 같지만 범위 밖";
        tableBody.append(el("tr", { class: matchedId === cmd.id ? "hit" : null },
          el("td", {}, el("code", { text: cmd.id }), el("div", { class: "sc-dim", text: cmd.label + " · " + cmd.from })),
          el("td", { text: scope }),
          el("td", { class: "sc-keys", text: eff }),
          el("td", { text: state || "—" })));
      }

      const parsed = C.parseShortcut(S.parse);
      parseOut.replaceChildren(parsed
        ? el("span", {}, "→ ", el("code", { text: parsed.key }), " · Ctrl " + yesno(parsed.control) + " · Shift " + yesno(parsed.shift) + " · Alt " + yesno(parsed.alt) + " → 글 " + C.shortcutLabel(parsed, false) + " / " + C.shortcutLabel(parsed, true))
        : el("span", { class: "warn", text: "→ 해석하지 못한다 (nullopt): 모르는 이름, 키가 없는 글, 같은 수정자를 두 번 쓴 글" }));
    }

    root.addEventListener("lw-reset", () => {
      Object.assign(S, initial);
      parseInput.value = S.parse;
      render();
    });
    render();
  }

  /* ---------- 2. 명령 팔레트 ---------- */

  function paletteCommands() {
    // tests/command_palette_tests.cpp의 sample_commands와 데모 팔레트(examples/demo/palette.cpp)의 명령이다
    return [
      { id: "file.save", label: "저장", shortcut: ch("key_s", true) },
      { id: "file.save_as", label: "다른 이름으로 저장", shortcut: ch("key_s", true, true) },
      { id: "view.zoom_in", label: "Zoom In" },
      { id: "view.toggle_sidebar", label: "Toggle Sidebar", enabled: false },
      { id: "edit.find", label: "Find in Files", shortcut: ch("key_f", true), user_shortcuts: [ch("key_f", true, true)] },
      { id: "page:lists", label: "페이지 열기: 목록" },
      { id: "page:table", label: "페이지 열기: 표" },
      { id: "page:theme", label: "페이지 열기: 테마" },
      { id: "view.sidebar", label: "사이드바 접기·펴기", shortcut: ch("key_b", true) },
      { id: "edit.undo", label: "실행 취소", shortcut: ch("key_z", true) },
      { id: "edit.redo", label: "다시 실행", shortcut: ch("key_y", true), extra_shortcuts: [ch("key_z", true, true)] },
    ];
  }
  const RANK_TEXT = ["이름 시작", "낱말 시작", "이름에 듦", "id에 듦"];

  function setupPalette(root) {
    if (!root) return;
    const body = root.querySelector(".lw-body");
    const commands = paletteCommands();
    const P = { open: true, composingSim: false, realComposing: false, committed: "", highlight: null, mac: false, maximum: 12 };

    const input = el("input", { type: "search", class: "pl-field", placeholder: "명령 찾기", "aria-label": "명령 찾기 (command_palette_search)", spellcheck: "false", autocomplete: "off" });
    const simBox = el("input", { type: "checkbox" });
    const platSeg = el("div", { class: "seg", role: "group", "aria-label": "단축키 글" },
      el("button", { type: "button", "data-v": "win", "aria-pressed": "true" }, "Ctrl+S"),
      el("button", { type: "button", "data-v": "mac", "aria-pressed": "false" }, "⌘S"));
    const maxSel = el("select", { "aria-label": "maximum_rows" }, [12, 5, 3].map((n) => el("option", { value: String(n) }, "줄 상한 " + n + (n === 12 ? " (기본)" : ""))));
    const list = el("div", { class: "pl-rows", role: "listbox", "aria-label": "명령 팔레트 결과" });
    const status = el("div", { class: "pl-status", "aria-live": "polite" });
    const log = el("div", { class: "pl-log", "aria-label": "메시지 기록" });
    const reopen = el("button", { type: "button", class: "btn sm", onclick: () => openPalette() }, icon("EA6D"), "다시 열기 (palette_open_intent)");
    const shell = el("div", { class: "pl-shell" }, input, list);
    const closed = el("div", { class: "pl-closed" }, el("span", { text: "팔레트가 닫혔다." }), reopen);

    body.append(
      el("div", { class: "sc-row" }, el("label", { class: "sc-check", title: "마지막 글자를 IME 조합 중인 글로 본다 (text_input_view::composing)" }, simBox, " 마지막 글자는 조합 중"), platSeg, maxSel),
      el("div", { class: "pl-grid" }, el("div", { class: "pl-main" }, shell, closed, status), log),
    );

    const view = () => {
      if (P.realComposing) return { text: P.committed, composing: true, composition_text: input.value };
      if (P.composingSim && [...input.value].length > 0) {
        const chars = [...input.value];
        return { text: chars.slice(0, -1).join(""), composing: true, composition_text: input.value };
      }
      return { text: input.value, composing: false, composition_text: "" };
    };
    const items = () => C.commandPaletteItems({ commands, query: view(), empty_label: "일치하는 명령 없음", maximum_rows: P.maximum }, P.mac);

    function addLog(what, msg, eff) {
      log.prepend(el("div", { class: "pl-entry" }, el("div", { class: "in", text: what }), msg ? el("div", { class: "msg", text: msg }) : null, eff ? el("div", { class: "eff", text: eff }) : null));
      while (log.childNodes.length > 40) log.lastChild.remove();
    }

    function render() {
      shell.hidden = !P.open;
      closed.hidden = P.open;
      platSeg.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String((b.dataset.v === "mac") === P.mac)));
      const rows = items();
      const v = view();
      const anyMatch = (q) => C.filterCommands(commands, q).length > 0;
      const used = C.searchQuery(v, anyMatch);
      const needle = window.LuilText.foldString(used.replace(/^[ \t]+|[ \t]+$/g, ""));
      list.replaceChildren();
      rows.forEach((it) => {
        const cmd = commands.find((c) => c.id === it.key);
        const rank = cmd && needle ? C.rankOf(cmd, needle) : null;
        const lit = P.highlight !== null && it.key === P.highlight;
        const def = C.shownAsDefault(rows, P.highlight, it);
        const row = el("div", { class: "pl-row" + (it.enabled ? "" : " dis") + (lit ? " hl" : "") + (def ? " def" : ""), role: "option", "aria-selected": String(lit || def), "aria-disabled": it.enabled ? null : "true" },
          el("span", { class: "pl-label", text: it.label }),
          rank !== null && rank !== undefined ? el("span", { class: "pl-rank r" + rank, text: RANK_TEXT[rank] }) : null,
          it.shortcut_text ? el("span", { class: "pl-short", text: it.shortcut_text }) : null);
        if (it.enabled) row.addEventListener("click", () => run(it.key, "줄을 누름"));
        list.append(row);
      });
      const parts = [];
      if (v.composing) parts.push(used === v.composition_text ? "조합 글 “" + v.composition_text + "”로 거른다" : "조합 글 “" + v.composition_text + "”이 맞는 것이 없어 확정 글 “" + used + "”로 물러섰다");
      else parts.push(used.trim() ? "질의 “" + used + "”" : "빈 질의 — 전부를 명령표 차례로");
      parts.push("결과 " + C.filterCommands(commands, used).length + "개, 보이는 줄 " + rows.filter((r) => r.key !== "" || r.enabled).length);
      const dk = C.defaultKey(rows);
      parts.push(P.highlight !== null && rows.some((r) => r.key === P.highlight) ? "Enter → 강조한 " + P.highlight : dk ? "Enter → 첫 결과 " + dk + " (default_key)" : "Enter → 아무 일 없음");
      status.textContent = parts.join(" · ");
    }

    function run(id, why) {
      addLog(why, "run(\"" + id + "\")  →  palette_run_intent { " + id + " }", "다음에 close가 함께 나간다 (after_select) → 앱이 팔레트를 닫는다");
      P.open = false;
      render();
      reopen.focus();
    }
    function openPalette() {
      // examples/demo/palette.cpp 66-73: 열 때마다 빈 질의, 조합 없음. 메뉴가 열리면 강조가 지워진다 (ui_interaction.cpp 4719-4729)
      P.open = true;
      P.highlight = null;
      P.realComposing = false;
      input.value = "";
      addLog("팔레트를 연다", "palette_open_intent", "질의를 비우고 검색 칸으로 초점을 옮긴다 (focus_request)");
      render();
      input.focus();
    }

    input.addEventListener("input", () => render());
    input.addEventListener("compositionstart", () => {
      P.committed = input.value;
      P.realComposing = true;
    });
    input.addEventListener("compositionupdate", () => setTimeout(render, 0));
    input.addEventListener("compositionend", () => {
      P.realComposing = false;
      setTimeout(render, 0);
    });
    input.addEventListener("keydown", (e) => {
      if (e.isComposing || e.keyCode === 229) return;
      const map = { ArrowDown: "arrow_down", ArrowUp: "arrow_up", Enter: "enter", Escape: "escape" };
      const key = map[e.key];
      if (!key) return; // 그 밖의 키는 검색 칸의 것이다
      const ev = { key, control: e.ctrlKey, shift: e.shiftKey, alt: e.altKey, meta: e.metaKey, repeat: e.repeat };
      const rows = items();
      const r = C.menuKey(rows, P.highlight, ev);
      e.preventDefault();
      if (!r.consumed) {
        addLog((e.ctrlKey ? "Ctrl+" : "") + (e.altKey ? "Alt+" : "") + (e.metaKey ? "Meta+" : "") + "Enter", null, "메뉴의 키가 아니다 → 검색 칸도 쓰지 않는다 → 삼킨다. 팔레트가 열린 동안 on_key 단축키는 서지 않는다");
        return;
      }
      if (key === "enter" && e.repeat) {
        addLog("누른 채 둔 Enter (repeat)", null, "메뉴가 삼킨다 — 반복은 고른 것이 아니다");
        return;
      }
      if (key === "escape") {
        addLog("Esc", "close_menu()", "정책의 닫기다 (Win32는 popup의 dismiss(escape_key)가 먼저 받는다)");
        P.open = false;
        render();
        reopen.focus();
        return;
      }
      if (r.run) {
        const how = P.highlight !== null && rows.some((x) => x.key === P.highlight) ? "Enter (강조한 줄)" : "Enter (강조 없음 → 첫 결과)";
        run(r.run, how);
        return;
      }
      if (key === "enter") {
        addLog("Enter", null, "고를 수 있는 줄이 없다 — 아무 일도 없다");
        return;
      }
      const before = P.highlight;
      P.highlight = r.highlight;
      addLog(key === "arrow_down" ? "↓" : "↑", null, before === P.highlight ? "끝이다 — 강조가 그대로다 (돌지 않는다)" : "강조 " + (before === null ? "없음" : before) + " → " + P.highlight + " (메시지 없음)");
      render();
    });
    simBox.addEventListener("change", () => {
      P.composingSim = simBox.checked;
      render();
    });
    platSeg.addEventListener("click", (e) => {
      const b = e.target.closest("button");
      if (!b) return;
      P.mac = b.dataset.v === "mac";
      render();
    });
    maxSel.addEventListener("change", () => {
      P.maximum = Number(maxSel.value);
      render();
    });
    root.addEventListener("lw-reset", () => {
      log.replaceChildren();
      simBox.checked = false;
      P.composingSim = false;
      P.mac = false;
      P.maximum = 12;
      maxSel.value = "12";
      openPalette();
    });
    addLog("팔레트가 열려 있다", null, "↑/↓로 강조, Enter로 실행, Esc로 닫기. 글자는 검색 칸으로 간다");
    render();
  }

  window.LuilButtonsLab = { setupShortcuts, setupPalette, eventFor, keyOfCode };
})();

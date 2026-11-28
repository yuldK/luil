/* luil 설명서 — 명령표·단축키·명령 팔레트 규칙을 JS로 옮긴 것
 *
 * 옮긴 것 (이름 옆 주석이 C++ 원본이다):
 *   - src/ui/commands.cpp 전부: effective_shortcuts, match_command, shortcut_label, parse_shortcut, label_with_shortcut,
 *     command_tooltip, system_reserved_chord, chord_of, menu_item_from, find_conflicts
 *   - src/ui/command_palette.cpp: filter_commands, command_palette_items, 기본 항목(palette_menu의 default_key)
 *   - src/ui/ui_interaction.cpp: search_query, 열린 메뉴의 ↑/↓·Enter·Esc 판정(process_menu_key의 팔레트에 닿는 갈래)
 *   - src/ui/menu_element.cpp: 강조가 없을 때 기본 항목을 강조처럼 그리는 판정(shown_as_default)
 * 맞춘 방법:
 *   - commands.cpp·command_palette.cpp·case_fold.cpp를 컴파일해 링크한 대조기에 무작위 입력 여섯 벌, 약 6만 줄(단축키 글 두 플랫폼,
 *     해석, 맞추기, 겹침, 팔레트 거르기·줄)을 넣어 이 파일의 답과 한 줄씩 견줬다. search_query는 ui_interaction.cpp를
 *     통째로 링크할 수 없어 그 함수 본문(1270-1281행)을 그대로 옮겨 붙여 대조했다.
 *   - 메뉴 키 판정은 controller를 혼자 링크할 수 없어 tests/command_palette_tests.cpp(170-210행)의 장면을 node에서 되풀이했다.
 * 키 이름은 luil::key_code 열거자 이름("key_s", "enter", "f4", "numpad_add" …)이다. 이름 없는 플랫폼 대역 키는 다루지 않는다.
 *   key_chord  { key, control, shift, alt }            — control은 주 단축키 수정자다 (macOS는 Command)
 *   key_pressed_event(일부) { key, control, shift, alt, meta, primary_shortcut: true|false|null, repeat }
 *   app_command { id, label, shortcut, scope: "window"|"focused"|"modal", enabled, checked, checkable,
 *                 extra_shortcuts: [], user_shortcuts: null | [] }
 */
(function (root) {
  "use strict";

  // text::fold_case — textlab.js가 옮긴 표를 쓴다. 없으면 거르기가 원본과 달라지므로 조용히 물러서지 않는다
  if (!root.LuilText || !root.LuilText.foldString) throw new Error("commandmodel.js: textlab.js(LuilText)를 먼저 불러야 한다");
  const fold = root.LuilText.foldString;

  // key_names (commands.cpp 38-92): 영문자·숫자 밖의 이름 키의 글이다
  const KEY_NAMES = [
    ["enter", "Enter"], ["escape", "Esc"], ["tab", "Tab"], ["space", "Space"], ["backspace", "Backspace"], ["delete_forward", "Delete"],
    ["insert", "Insert"], ["home", "Home"], ["end", "End"], ["page_up", "PageUp"], ["page_down", "PageDown"],
    ["arrow_left", "Left"], ["arrow_right", "Right"], ["arrow_up", "Up"], ["arrow_down", "Down"],
    ["f1", "F1"], ["f2", "F2"], ["f3", "F3"], ["f4", "F4"], ["f5", "F5"], ["f6", "F6"], ["f7", "F7"], ["f8", "F8"], ["f9", "F9"], ["f10", "F10"], ["f11", "F11"], ["f12", "F12"],
    ["minus", "-"], ["equal", "="], ["comma", ","], ["period", "."], ["slash", "/"], ["semicolon", ";"], ["quote", "'"],
    ["bracket_left", "["], ["bracket_right", "]"], ["backslash", "\\"], ["grave", "`"],
    ["numpad_0", "Num0"], ["numpad_1", "Num1"], ["numpad_2", "Num2"], ["numpad_3", "Num3"], ["numpad_4", "Num4"],
    ["numpad_5", "Num5"], ["numpad_6", "Num6"], ["numpad_7", "Num7"], ["numpad_8", "Num8"], ["numpad_9", "Num9"],
    ["numpad_add", "Num+"], ["numpad_subtract", "Num-"], ["numpad_multiply", "Num*"], ["numpad_divide", "Num/"], ["numpad_decimal", "Num."],
  ];
  // key_aliases (commands.cpp 95-99): 해석만 받는 다른 이름이다
  const KEY_ALIASES = [["escape", "Escape"], ["delete_forward", "Del"], ["enter", "Return"]];

  const chord = (key, control, shift, alt) => ({ key: key || "none", control: !!control, shift: !!shift, alt: !!alt });
  const sameChord = (a, b) => a.key === b.key && a.control === b.control && a.shift === b.shift && a.alt === b.alt; // key_chord::operator==

  // lowered·same_name·starts_with_name (101-114): ASCII A–Z만 접는다
  const lowered = (s) => s.replace(/[A-Z]/g, (c) => c.toLowerCase());
  const sameName = (a, b) => a.length === b.length && lowered(a) === lowered(b);
  const startsWithName = (text, prefix) => text.length >= prefix.length && sameName(text.slice(0, prefix.length), prefix);

  // key_text (116-125)
  function keyText(key) {
    let m = /^key_([a-z])$/.exec(key);
    if (m) return m[1].toUpperCase();
    m = /^key_([0-9])$/.exec(key);
    if (m) return m[1];
    for (const [k, label] of KEY_NAMES) if (k === key) return label;
    return "";
  }

  // key_from_text (127-144)
  function keyFromText(text) {
    if ([...text].length === 1 && new TextEncoder().encode(text).length === 1) {
      const c = lowered(text);
      if (c >= "a" && c <= "z") return "key_" + c;
      if (c >= "0" && c <= "9") return "key_" + c;
    }
    for (const [k, label] of KEY_NAMES) if (sameName(label, text)) return k;
    for (const [k, label] of KEY_ALIASES) if (sameName(label, text)) return k;
    return "none";
  }

  // key_pressed_event::primary_shortcut_down / shortcut_modifier_down (ui_events.h 509-517)
  const primaryDown = (e) => (e.primary_shortcut === true || e.primary_shortcut === false ? e.primary_shortcut : !!e.control);
  const shortcutModifierDown = (e) => !!(e.control || e.alt || e.meta || primaryDown(e));

  // extra_modifier_down (15-20)
  function extraModifierDown(e) {
    const primary = primaryDown(e);
    const primaryOnMeta = primary && !e.control;
    return (!!e.control && !primary) || (!!e.meta && !primaryOnMeta);
  }

  // matches (22-26)
  const matches = (c, e) => c.key !== "none" && c.key === e.key && c.control === primaryDown(e) && c.shift === !!e.shift && c.alt === !!e.alt && !extraModifierDown(e);

  // effective_shortcuts (147-164)
  function effectiveShortcuts(command) {
    const result = [];
    const add = (c) => {
      if (c && c.key !== "none" && !result.some((r) => sameChord(r, c))) result.push(chord(c.key, c.control, c.shift, c.alt));
    };
    if (Array.isArray(command.user_shortcuts)) {
      for (const c of command.user_shortcuts) add(c);
      return result;
    }
    add(command.shortcut || chord());
    for (const c of command.extra_shortcuts || []) add(c);
    return result;
  }

  const enabledOf = (c) => c.enabled !== false;
  const scopeOf = (c) => c.scope || "window";

  // match_command (166-182): 앱이 넘긴 범위에서 찾고, focused면 window로 물러선다
  function matchCommand(commands, event, active) {
    const find = (scope) => {
      for (const command of commands) {
        if (!enabledOf(command) || scopeOf(command) !== scope) continue;
        for (const c of effectiveShortcuts(command)) if (matches(c, event)) return command.id;
      }
      return null;
    };
    const id = find(active);
    if (id !== null) return id;
    return active === "focused" ? find("window") : null;
  }

  // shortcut_label (184-210): macOS(command_key_shortcuts)는 ⌥⇧⌘ 차례로 붙이고 '+'가 없다
  function shortcutLabel(c, commandKeys) {
    if (!c || c.key === "none") return "";
    const key = keyText(c.key);
    if (!key) return "";
    let result = "";
    if (commandKeys) {
      if (c.alt) result += "⌥";
      if (c.shift) result += "⇧";
      if (c.control) result += "⌘";
      return result + key;
    }
    if (c.control) result += "Ctrl+";
    if (c.shift) result += "Shift+";
    if (c.alt) result += "Alt+";
    return result + key;
  }

  // parse_shortcut (212-259)
  function parseShortcut(input) {
    let text = input;
    while (text.length && text[0] === " ") text = text.slice(1);
    while (text.length && text[text.length - 1] === " ") text = text.slice(0, -1);
    const c = chord();
    const modifiers = [
      ["Ctrl+", "control"], ["Control+", "control"], ["Cmd+", "control"], ["Command+", "control"], ["⌘", "control"],
      ["Shift+", "shift"], ["⇧", "shift"],
      ["Alt+", "alt"], ["Option+", "alt"], ["Opt+", "alt"], ["⌥", "alt"],
    ];
    for (;;) {
      let flag = null, length = 0;
      for (const [name, target] of modifiers)
        if (startsWithName(text, name) && text.length > name.length) {
          flag = target;
          length = name.length;
          break;
        }
      if (flag === null) break;
      if (c[flag]) return null;
      c[flag] = true;
      text = text.slice(length);
    }
    c.key = keyFromText(text);
    if (c.key === "none") return null;
    return c;
  }

  // label_with_shortcut (261-267) · command_tooltip (269-273)
  function labelWithShortcut(label, c, commandKeys) {
    const s = shortcutLabel(c, commandKeys);
    return s ? label + " (" + s + ")" : label;
  }
  function commandTooltip(command, commandKeys) {
    const chords = effectiveShortcuts(command);
    return labelWithShortcut(command.label || "", chords.length ? chords[0] : chord(), commandKeys);
  }

  // system_reserved_chord (275-282)
  function systemReservedChord(c) {
    if (c.alt && !c.control && !c.shift) return c.key === "f4" || c.key === "tab" || c.key === "escape" || c.key === "space";
    if (c.alt && c.control && c.key === "delete_forward") return true;
    return c.control && !c.alt && !c.shift && c.key === "escape";
  }

  // chord_of (284-287): meta·여분 수정자는 담지 않는다
  const chordOf = (e) => chord(e.key, primaryDown(e), e.shift, e.alt);

  // menu_item_from (289-300)
  function menuItemFrom(command, commandKeys) {
    const chords = effectiveShortcuts(command);
    return {
      key: command.id || "",
      label: command.label || "",
      enabled: enabledOf(command),
      kind: command.checkable || command.checked ? "check" : "action",
      checked: !!command.checked,
      shortcut_text: chords.length ? shortcutLabel(chords[0], commandKeys) : "",
    };
  }

  // find_conflicts (302-319): 같은 범위에서 실제로 쓰는 단축키가 겹치는 쌍
  function findConflicts(commands) {
    const result = [];
    const chords = commands.map(effectiveShortcuts);
    for (let first = 0; first < commands.length; ++first)
      for (let second = first + 1; second < commands.length; ++second) {
        if (scopeOf(commands[first]) !== scopeOf(commands[second])) continue;
        for (const c of chords[first])
          if (chords[second].some((d) => sameChord(d, c))) result.push({ first: commands[first].id, second: commands[second].id, scope: scopeOf(commands[first]), chord: c });
      }
    return result;
  }

  /* ---------- 명령 팔레트 (command_palette.cpp) ---------- */

  const RANK = ["label_start", "word_start", "label_contains", "id_contains"]; // match_rank (17-23)

  // separates_words (26-30): 접은 글의 ASCII 바이트만 본다
  const separatesWords = (ch) => ch !== undefined && ch.charCodeAt(0) < 0x80 && " -_./([:".includes(ch);

  // rank_of (32-49)
  function rankOf(command, needle) {
    const label = fold(command.label || "");
    if (label.startsWith(needle)) return 0;
    let contains = false;
    for (let at = label.indexOf(needle); at !== -1; at = label.indexOf(needle, at + 1)) {
      contains = true;
      if (separatesWords(label[at - 1])) return 1;
    }
    if (contains) return 2;
    if (fold(command.id || "").includes(needle)) return 3;
    return null;
  }

  // trimmed (51-58): 앞뒤의 공백과 탭만 벗긴다
  const trimmed = (s) => s.replace(/^[ \t]+/, "").replace(/[ \t]+$/, "");

  // filter_commands (80-101): 단계로 안정 정렬, 같은 단계는 명령표 차례
  function filterCommands(commands, query) {
    const needle = fold(trimmed(query));
    const ranked = [];
    commands.forEach((command, index) => {
      if (!needle) {
        ranked.push([0, index]);
        return;
      }
      const rank = rankOf(command, needle);
      if (rank !== null) ranked.push([rank, index]);
    });
    ranked.sort((a, b) => a[0] - b[0] || a[1] - b[1]); // stable_sort와 같은 결과 (색인이 이미 오름차순)
    return ranked.map((r) => r[1]);
  }

  // search_query (ui_interaction.cpp 1270-1281): 조합 글이 아무것도 맞히지 못하면 확정 글로 물러선다
  //   view: { text, composing, composition_text }
  function searchQuery(view, anyMatch) {
    const displayed = view.composing ? view.composition_text || "" : view.text || "";
    if (!view.composing || !view.text || !anyMatch) return displayed;
    if (anyMatch(displayed)) return displayed;
    return view.text;
  }

  // command_palette_items (103-123)
  //   config: { commands, query: view, empty_label, maximum_rows }
  function commandPaletteItems(config, commandKeys) {
    const commands = config.commands || [];
    const anyMatch = (candidate) => filterCommands(commands, candidate).length > 0;
    const query = searchQuery(config.query || { text: "" }, anyMatch);
    const maximum = config.maximum_rows === undefined ? 12 : config.maximum_rows;
    const items = [];
    for (const index of filterCommands(commands, query)) {
      if (items.length >= maximum) break;
      items.push(menuItemFrom(commands[index], commandKeys));
    }
    if (!items.length) items.push({ key: "", label: config.empty_label || "", enabled: false, kind: "action", checked: false, shortcut_text: "" });
    return items;
  }

  // palette_menu의 default_key (69-75): 고를 수 있는 첫 결과
  const defaultKey = (items) => {
    for (const item of items) if (item.enabled) return item.key;
    return "";
  };

  // 열린 메뉴의 키 (ui_interaction.cpp process_menu_key의 팔레트 갈래 — 하위 메뉴·메뉴 막대·접근 키는 팔레트에 없다)
  //   state: { highlight: key | null }   items: menu_item_config[] (보이는 차례)
  //   돌려주는 값: { consumed: bool, highlight, run: key | null, close: bool }
  //    - consumed=false면 메뉴의 키가 아니어서 검색 칸으로 간다 (Home·End·글자…)
  function menuKey(items, highlight, event) {
    const out = { consumed: true, highlight, run: null, close: false };
    if (event.repeat && event.key === "enter") return out; // 3857-3858: 누른 채 둔 Enter는 삼킨다
    const enabledAt = (i) => items[i] && items[i].enabled;
    const present = highlight !== null && items.some((it) => it.key === highlight);
    switch (event.key) {
      case "arrow_down":
      case "arrow_up": {
        let current = items.length;
        items.forEach((it, i) => {
          if (highlight !== null && it.key === highlight) current = i;
        });
        if (event.key === "arrow_down") {
          for (let i = current === items.length ? 0 : current + 1; i < items.length; ++i)
            if (enabledAt(i)) {
              out.highlight = items[i].key;
              break;
            }
        } else {
          for (let i = current === items.length ? items.length : current; i > 0; --i)
            if (enabledAt(i - 1)) {
              out.highlight = items[i - 1].key;
              break;
            }
        }
        return out;
      }
      case "enter": {
        if (shortcutModifierDown(event)) return { consumed: false, highlight, run: null, close: false }; // Ctrl·Alt Enter는 메뉴의 것이 아니다
        let item = present ? items.find((it) => it.key === highlight) : null;
        if (!item) {
          const fallback = defaultKey(items);
          if (fallback) item = items.find((it) => it.key === fallback) || null;
        }
        if (!item || !item.enabled) return out;
        out.run = item.key;
        out.close = true; // menu_config::after_select = close
        return out;
      }
      case "escape":
        out.close = true; // interaction_policy::close_menu()
        return out;
      default:
        return { consumed: false, highlight, run: null, close: false };
    }
  }

  // 강조가 이 메뉴에 없을 때 기본 항목을 강조처럼 그린다 (menu_element.cpp 156-168)
  const shownAsDefault = (items, highlight, item) => {
    const key = defaultKey(items);
    if (!item.enabled || !key || key !== item.key) return false;
    return !(highlight !== null && items.some((it) => it.key === highlight));
  };

  const api = {
    KEY_NAMES, KEY_ALIASES, RANK,
    chord, sameChord, keyText, keyFromText, primaryDown, shortcutModifierDown, extraModifierDown, matches,
    effectiveShortcuts, matchCommand, shortcutLabel, parseShortcut, labelWithShortcut, commandTooltip,
    systemReservedChord, chordOf, menuItemFrom, findConflicts,
    rankOf, filterCommands, searchQuery, commandPaletteItems, defaultKey, menuKey, shownAsDefault,
  };
  root.LuilCommands = api;
})(typeof window !== "undefined" ? window : globalThis);

/* luil 설명서 — 테마와 스타일 실습: 팔레트 조합기
 *
 * 테마(dark·light·기본 고대비)와 내장 키 컬러 25개 가운데 하나를 고르면 luil이 그 frame에 짓는 팔레트를 보인다.
 * 계산은 전부 assets/thememodel.js(원본 C++과 칸마다 대조한 이식)를 부른다. 이 파일은 화면만 짓는다.
 *  - 역할 표: color_palette_for(theme, accent_for(id))의 모든 칸과 그 칸을 만든 식
 *  - 채운 강조 위의 글자: foreground_on의 0.179 규칙과 쉼·hover·눌림의 대비
 *    눌림의 대비는 luil이 셈하는 값이 아니다. accent_pressed를 고른 바탕에 알파로 얹은 색(test의 composite_over 꼴)에 대고
 *    이 문서가 셈한 것이다.
 * 견본 칸은 luil이 계산한 실제 색 값을 칠한다(내용의 색). 판·글자 같은 껍데기만 설명서 토큰을 쓴다.
 */
(function () {
  "use strict";
  const M = window.LuilTheme;
  const { el } = window.LU;

  // 역할 표의 묶음이다. 식은 compose_palette(ui_theme.cpp:170-229)와 high_contrast_palette_for(:231-309)의 그 칸이다.
  const GROUPS = [
    { name: "바탕", rows: [
      ["window_background", "neutral.window_background", "colors.window_background"],
      ["surface_background", "neutral.surface_background", "colors.window_background"],
      ["input_background", "neutral.input_background", "colors.window_background"],
      ["raised_background", "neutral.raised_background", "colors.window_background"],
      ["thumb_background", "neutral.thumb_background", "colors.highlight_background"],
      ["notice_background", "neutral.notice_background", "colors.window_background"],
      ["tooltip_background", "neutral.tooltip_background", "colors.window_background"],
      ["backdrop_background", "neutral.backdrop_background", "colors.window_background"],
    ] },
    { name: "글자", rows: [
      ["primary_foreground", "neutral.primary_foreground", "colors.window_foreground"],
      ["secondary_foreground", "neutral.secondary_foreground", "colors.window_foreground"],
      ["disabled_foreground", "neutral.disabled_foreground", "colors.disabled_foreground"],
    ] },
    { name: "선", rows: [
      ["divider", "neutral.divider", "colors.window_foreground"],
      ["input_border", "neutral.input_border", "colors.window_foreground"],
      ["control_border", "neutral.control_border", "colors.window_foreground"],
      ["group_border", "neutral.group_border", "colors.window_foreground"],
      ["tooltip_border", "neutral.tooltip_border", "colors.window_foreground"],
    ] },
    { name: "키 컬러 4역할", rows: [
      ["accent", "accent.accent", "colors.emphasis"],
      ["accent_hover", "accent.hover", "colors.emphasis"],
      ["accent_soft", "accent.soft", "colors.highlight_background"],
      ["accent_emphasis_foreground", "accent.emphasis_foreground", "colors.highlight_foreground"],
    ] },
    { name: "채운 바탕 위 글자", rows: [
      ["accent_foreground", "foreground_on(accent.accent)", "colors.window_background"],
      ["error_foreground", "foreground_on(neutral.error_accent)", "colors.window_background"],
    ] },
    { name: "키 컬러 파생 (tone)", rows: [
      ["accent_pressed", "with_alpha(accent.accent, tones.pressed)", "colors.highlight_background"],
      ["soft_button_background", "with_alpha(accent.soft, tones.soft_button)", "colors.highlight_background"],
      ["soft_button_hover_background", "with_alpha(accent.hover, tones.soft_button_hover)", "colors.highlight_background"],
      ["active_toggle_background", "with_alpha(accent.soft, tones.active_toggle)", "colors.highlight_background"],
      ["selection_background", "with_alpha(accent.soft, tones.selection)", "colors.highlight_background"],
      ["selection_foreground", "neutral.primary_foreground", "colors.highlight_foreground"],
      ["row_selection_background", "with_alpha(accent.soft, tones.row_selection)", "알파 0 (채움을 접는다)"],
      ["drop_target_background", "with_alpha(accent.accent, tones.drop_target)", "colors.highlight_background"],
    ] },
    { name: "뜻 있는 색", rows: [
      ["warning_accent", "neutral.warning_accent", "colors.window_foreground"],
      ["error_accent", "neutral.error_accent", "colors.window_foreground"],
      ["danger_button_background", "with_alpha(neutral.error_accent, tones.danger_button)", "colors.button_background"],
      ["danger_button_hover_background", "with_alpha(neutral.error_accent, tones.danger_button_hover)", "colors.highlight_background"],
      ["danger_button_pressed_background", "with_alpha(neutral.error_accent, tones.danger_button_pressed)", "colors.highlight_background"],
    ] },
    { name: "일반 단추·그림자", rows: [
      ["button_hover_background", "neutral.button_hover_background", "colors.highlight_background"],
      ["button_hover_foreground", "neutral.button_hover_foreground", "colors.highlight_foreground"],
      ["button_pressed_background", "neutral.button_pressed_background", "colors.highlight_background"],
      ["content_shadow", "neutral.content_shadow", "colors.window_foreground"],
    ] },
    { name: "caption", rows: [
      ["caption.background", "neutral.caption.background", "colors.button_background"],
      ["caption.foreground", "neutral.caption.foreground", "colors.button_foreground"],
      ["caption.button_hover_background", "neutral.caption.button_hover_background", "colors.highlight_background"],
      ["caption.button_hover_foreground", "neutral.caption.button_hover_foreground", "colors.highlight_foreground"],
      ["caption.close_button_hover_background", "neutral.caption.close_button_hover_background", "colors.highlight_background"],
      ["caption.close_button_hover_foreground", "neutral.caption.close_button_hover_foreground", "colors.highlight_foreground"],
    ] },
    { name: "문서 비교·찾기", rows: [
      ["document_added_background", "with_alpha(accent.accent, 0.12)", "colors.window_background"],
      ["document_removed_background", "with_alpha(neutral.error_accent, 0.12)", "colors.window_background"],
      ["document_changed_background", "with_alpha(neutral.warning_accent, 0.12)", "colors.window_background"],
      ["document_search_background", "with_alpha(accent.soft, 0.35)", "colors.highlight_background"],
      ["document_search_foreground", "neutral.primary_foreground", "colors.highlight_foreground"],
    ] },
    { name: "구문 색", rows: [
      ["syntax_keyword", "neutral.syntax_keyword", "colors.window_foreground"],
      ["syntax_string", "neutral.syntax_string", "colors.window_foreground"],
      ["syntax_comment", "neutral.syntax_comment", "colors.window_foreground"],
      ["syntax_number", "neutral.syntax_number", "colors.window_foreground"],
      ["syntax_type", "neutral.syntax_type", "colors.window_foreground"],
      ["syntax_function", "neutral.syntax_function", "colors.window_foreground"],
    ] },
  ];
  const THEME_CODE = { dark: "color_theme::dark", light: "color_theme::light", high_contrast: "color_theme::high_contrast" };

  const css = (c) => "rgba(" + ((c >>> 16) & 255) + "," + ((c >>> 8) & 255) + "," + (c & 255) + "," + ((c >>> 24) / 255).toFixed(3) + ")";
  const roleOf = (p, name) => (name.startsWith("caption.") ? p.caption[name.slice(8)] : p[name]);
  const ratio = (x) => x.toFixed(2) + ":1";
  const lum = (x) => x.toFixed(4);
  // 긴 식별자가 좁은 폭에서 낱자 단위로 끊기지 않게 '_'·'.'·'('·',' 뒤에만 줄을 바꿀 자리를 둔다.
  function breakable(tag, text, attrs) {
    const node = el(tag, attrs || null);
    // 뒤돌아보기 정규식은 오래된 Safari가 읽지 못해 쓰지 않는다.
    text.replace(/([_.(,])/g, "$1\u0000").split("\u0000").filter((part) => part !== "").forEach((part, i) => {
      if (i) node.append(document.createElement("wbr"));
      node.append(document.createTextNode(part));
    });
    return node;
  }

  function setup(root) {
    if (!root || !M) return;
    const body = root.querySelector(".lw-body");
    const INITIAL = { theme: "dark", accent: "mint", base: "surface_background" };
    const S = Object.assign({}, INITIAL);

    // --- 고르는 칸 ---
    const themeSeg = el("div", { class: "seg", role: "group", "aria-label": "테마" },
      ...[["dark", "dark"], ["light", "light"], ["high_contrast", "고대비 (기본 색)"]].map(([v, t]) => el("button", { type: "button", "data-v": v, text: t })));
    const baseSeg = el("div", { class: "seg", role: "group", "aria-label": "눌림 대비를 셀 아래 바탕" },
      ...[["surface_background", "표면 위"], ["window_background", "창 위"]].map(([v, t]) => el("button", { type: "button", "data-v": v, text: t })));
    const swatches = el("div", { class: "tl-swatches", role: "group", "aria-label": "내장 키 컬러 25개" },
      ...M.ACCENTS.map((a) => el("button", { type: "button", class: "tl-sw", "data-v": a.id, title: a.label + " (" + a.id + ")", "aria-label": a.label + " " + a.id },
        el("span", { class: "dot", style: { background: css(a.swatch) } }), el("span", { class: "id", text: a.id }))));
    const controls = el("div", { class: "tl-controls" },
      el("div", { class: "controls" }, el("span", { class: "field", text: "테마" }), themeSeg, el("span", { class: "field", text: "눌림 대비" }), baseSeg),
      swatches);

    // --- 결과 ---
    const call = el("div", { class: "tl-call" });
    const states = el("div", { class: "tl-states", "aria-live": "polite" });
    const meter = el("div", { class: "tl-meter", "aria-hidden": "true" });
    const verdict = el("div", { class: "tl-verdict" });
    const sample = el("div", { class: "tl-sample", "aria-label": "고른 팔레트로 그린 견본 (재현)" });
    const table = el("table", { class: "t tl-roles" });
    const tableHead = el("h4", { class: "tl-table-h" });
    body.append(controls,
      el("div", { class: "tl-grid" },
        el("div", { class: "tl-left" }, call, el("h4", { text: "채운 강조 위의 글자" }), states, meter, verdict),
        el("div", { class: "tl-right" }, el("h4", { text: "견본 (같은 색으로 그린 재현)" }), sample)),
      tableHead, el("div", { class: "table-wrap" }, table));

    function press(seg, value) {
      seg.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", b.dataset.v === value ? "true" : "false"));
    }
    function draw() {
      press(themeSeg, S.theme);
      press(baseSeg, S.base);
      swatches.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", b.dataset.v === S.accent ? "true" : "false"));
      const accent = M.accentFor(S.accent);
      const p = M.colorPaletteFor(S.theme, accent);
      const hc = S.theme === "high_contrast";

      call.textContent = "";
      call.append(breakable("code", "color_palette_for(" + THEME_CODE[S.theme] + ", accent_for(u8\"" + accent.id + "\"))"),
        el("span", { class: "muted", text: hc ? " — 고대비는 키 컬러를 쓰지 않는다. 여기 값은 OS 색을 모를 때의 기본값이다." : " — " + accent.label + " · " + (S.theme === "dark" ? "dark" : "light") }));

      // 쉼·hover·눌림: 바탕과 같은 글자(accent_foreground)를 그린다 (dialog_elements.cpp:217-218, 244-245)
      const base = p[S.base];
      const fg = p.accent_foreground;
      const pressedOnBase = M.compositeOver(p.accent_pressed, base);
      const rows = [
        ["쉼", p.accent, "accent"],
        ["hover", p.accent_hover, "accent_hover"],
        ["눌림", pressedOnBase, "accent_pressed를 " + (S.base === "surface_background" ? "표면" : "창") + "에 얹은 색 (셈)"],
      ];
      states.textContent = "";
      for (const [name, bg, note] of rows) {
        const r = M.contrastRatio(fg, bg);
        states.append(el("div", { class: "tl-state" },
          el("div", { class: "tl-btn", style: { background: css(bg), color: css(fg) }, text: "확인" }),
          el("div", { class: "tl-st-txt" }, el("b", { text: name + " " }), el("span", { class: r >= 4.5 ? "ok" : "low", text: ratio(r) }), el("div", { class: "muted", text: note }))));
      }

      // 0.179 경계와 지금 바탕의 밝기
      meter.textContent = "";
      const scale = (v) => Math.min(100, Math.max(0, (v / 0.4) * 100));
      const track = el("div", { class: "track" }, el("div", { class: "edge", style: { left: scale(0.179) + "%" } }, el("span", { text: "0.179" })));
      const la = M.relativeLuminance(p.accent);
      const lh = M.relativeLuminance(p.accent_hover);
      if (!hc) {
        track.append(el("div", { class: "mark a", style: { left: scale(la) + "%", background: css(p.accent) }, title: "accent " + lum(la) }));
        track.append(el("div", { class: "mark h", style: { left: scale(lh) + "%", background: css(p.accent_hover) }, title: "accent_hover " + lum(lh) }));
      }
      meter.append(el("div", { class: "lbl" }, el("span", { text: "← 흰 글자" }), el("span", { text: "밝기 0~0.4" }), el("span", { text: "검은 글자 →" })), track);

      verdict.textContent = "";
      const word = (c) => ((c & 0xffffff) === 0xffffff ? "흰색 #ffffff" : (c & 0xffffff) === 0 ? "검정 #000000" : M.hex(c));
      if (hc) {
        verdict.append(el("p", { text: "고대비는 foreground_on을 쓰지 않는다. 채운 강조 위의 글자는 창 바탕색(" + M.hex(p.accent_foreground) + ")이다." }));
      } else {
        verdict.append(el("p", null, "accent ", el("code", { text: M.hex(p.accent) }), " 밝기 " + lum(la) + (la <= Math.fround(0.179) ? " ≤ 0.179 → 어두운 바탕 → " : " > 0.179 → 밝은 바탕 → "), el("b", { text: word(p.accent_foreground) })));
        const le = M.relativeLuminance(p.error_accent);
        verdict.append(el("p", null, "error_accent ", el("code", { text: M.hex(p.error_accent) }), " 밝기 " + lum(le) + " → error_foreground ", el("b", { text: word(p.error_foreground) })));
      }

      // 견본 (재현): 표면 위의 단추·고른 행·선택·링크
      sample.textContent = "";
      sample.style.background = css(p.surface_background);
      sample.style.color = css(p.primary_foreground);
      sample.style.borderColor = css(p.divider);
      const chip = (text, bg, color, extra) => el("span", { class: "tl-chip" + (extra ? " " + extra : ""), style: { background: css(bg), color: css(color) }, text });
      sample.append(
        el("div", { class: "row" },
          chip("기본 단추", p.accent, p.accent_foreground),
          chip("옅은 강조", M.compositeOver(p.soft_button_background, p.surface_background), p.accent_emphasis_foreground),
          chip("위험", M.compositeOver(p.danger_button_background, p.surface_background), p.error_accent),
          el("span", { class: "tl-link", style: { color: css(p.accent) }, text: "링크" })),
        el("div", { class: "tl-rowsel", style: { background: css(M.compositeOver(p.row_selection_background, p.surface_background)), borderLeftColor: css(p.accent) } }, "고른 행 (왼쪽 표식은 accent)"),
        el("div", { class: "row" },
          el("span", null, "본문 ", el("span", { style: { background: css(M.compositeOver(p.selection_background, p.surface_background)), color: css(p.selection_foreground) }, text: "고른 글" }), " 이어지는 글"),
          el("span", { style: { color: css(p.secondary_foreground) }, text: "보조 글" }),
          el("span", { style: { color: css(p.disabled_foreground) }, text: "꺼진 글" })));

      // 역할 표
      const count = GROUPS.reduce((n, g) => n + g.rows.length, 0);
      tableHead.textContent = "";
      tableHead.append("팔레트의 모든 칸 — 색 " + count + "개 · ",
        breakable("code", hc ? "high_contrast_palette_for(high_contrast_colors {})" : "compose_palette(style.neutral_for(theme), accent.for_theme(theme), style.tones)"));
      table.textContent = "";
      table.append(el("thead", null, el("tr", null, el("th", { text: hc ? "역할 · 고대비 식" : "역할 · 합성 식" }), el("th", { text: "색" }), el("th", { text: "값" }))));
      const tb = el("tbody");
      for (const g of GROUPS) {
        tb.append(el("tr", { class: "grp" }, el("th", { colspan: "3" }, g.name, el("span", { class: "n", text: String(g.rows.length) }))));
        for (const [name, formula, hcFormula] of g.rows) {
          const c = roleOf(p, name);
          tb.append(el("tr", null,
            el("td", null, breakable("code", name), breakable("span", "= " + (hc ? hcFormula : formula), { class: "f" })),
            el("td", null, el("span", { class: "tl-swatch" }, el("span", { style: { background: css(c) } }))),
            el("td", { class: "v" }, el("code", { text: M.hex(c) }))));
        }
      }
      table.append(tb);
    }

    themeSeg.addEventListener("click", (e) => { const b = e.target.closest("button[data-v]"); if (b) { S.theme = b.dataset.v; draw(); } });
    baseSeg.addEventListener("click", (e) => { const b = e.target.closest("button[data-v]"); if (b) { S.base = b.dataset.v; draw(); } });
    swatches.addEventListener("click", (e) => { const b = e.target.closest("button[data-v]"); if (b) { S.accent = b.dataset.v; draw(); } });
    root.addEventListener("lw-reset", () => { Object.assign(S, INITIAL); draw(); });
    draw();
  }

  window.LuilThemingLab = { setup };
})();

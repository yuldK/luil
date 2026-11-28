/* luil 설명서 — 팔레트 합성을 JS로 옮긴 것 (DOM 없음)
 *
 * 원본(커밋 563a855):
 *   include/luil/theme/ui_theme.h   make_ui_color, with_alpha
 *   src/theme/ui_theme.cpp           relative_luminance, is_dark_background, foreground_on,
 *                                    compose_palette, high_contrast_palette_for, accent_for, for_theme
 *   src/theme/ui_style.cpp           color_palette_for(style, theme, accent), neutral_for
 *   cmake/generate_style.cmake       "#rrggbb"·"#rrggbbaa" → 0xAARRGGBB
 * 데이터(DATA)는 assets/style.json의 dark·light·tones와 assets/accents.json 25항목을 스크립트로 그대로 옮겼다.
 * 이 커밋의 src/theme를 컴파일한 프로그램의 출력과 모든 칸을 견주어 맞췄다 (25 키 컬러 × dark·light + 기본 고대비).
 * 색은 0xAARRGGBB 부호 없는 32비트 수다.
 */
(function (root) {
  "use strict";

  const DATA = {"style":{"dark":{"windowBackground":"#1e1e1e","surfaceBackground":"#2d2d30","primaryForeground":"#ffffff","secondaryForeground":"#ffffffbf","disabledForeground":"#ffffff73","divider":"#ffffff1f","inputBackground":"#ffffff14","inputBorder":"#ffffff59","controlBorder":"#ffffff8c","groupBorder":"#ffffff40","warningAccent":"#dcaa2d","errorAccent":"#e06c75","buttonHoverBackground":"#ffffff1a","buttonHoverForeground":"#ffffff","buttonPressedBackground":"#ffffff2d","tooltipBackground":"#252526","tooltipBorder":"#5a5a5c","contentShadow":"#000000","noticeBackground":"#422427","raisedBackground":"#38383c","thumbBackground":"#ffffff29","backdropBackground":"#1e1e1e59","syntaxKeyword":"#6cb2f2","syntaxString":"#dba184","syntaxComment":"#8dbb78","syntaxNumber":"#b5cea8","syntaxType":"#4ec9b0","syntaxFunction":"#dcdcaa","caption":{"background":"#252526","foreground":"#ffffff","buttonHoverBackground":"#3f3f40","buttonHoverForeground":"#ffffff","closeButtonHoverBackground":"#c42b1c","closeButtonHoverForeground":"#ffffff"}},"light":{"windowBackground":"#f8f8f8","surfaceBackground":"#ffffff","primaryForeground":"#1f1f1f","secondaryForeground":"#1f1f1fbf","disabledForeground":"#1f1f1f73","divider":"#1f1f1f1f","inputBackground":"#1f1f1f14","inputBorder":"#1f1f1f59","controlBorder":"#1f1f1f8c","groupBorder":"#1f1f1f40","warningAccent":"#9a6700","errorAccent":"#c0303a","buttonHoverBackground":"#00000014","buttonHoverForeground":"#1f1f1f","buttonPressedBackground":"#00000024","tooltipBackground":"#ffffff","tooltipBorder":"#c8c8c8","contentShadow":"#000000","noticeBackground":"#fde7e9","raisedBackground":"#f0f0f0","thumbBackground":"#ffffff","backdropBackground":"#f8f8f866","syntaxKeyword":"#0000ff","syntaxString":"#a31515","syntaxComment":"#006b00","syntaxNumber":"#0a6b4a","syntaxType":"#1d6c84","syntaxFunction":"#6e5320","caption":{"background":"#f0f0f0","foreground":"#1f1f1f","buttonHoverBackground":"#dadada","buttonHoverForeground":"#1f1f1f","closeButtonHoverBackground":"#c42b1c","closeButtonHoverForeground":"#ffffff"}},"tones":{"pressed":0.7,"softButton":0.25,"softButtonHover":0.35,"activeToggle":0.22,"selection":0.35,"rowSelection":0.18,"dropTarget":0.3,"dangerButton":0.2,"dangerButtonHover":0.32,"dangerButtonPressed":0.42}},"accents":[{"id":"blue","label":{"ko":"블루","en":"Blue"},"swatch":"#a2cfff","dark":{"accent":"#3f7ebb","accentHover":"#5392d1","accentSoft":"#a2cfff","accentEmphasisFg":"#c6e1ff"},"light":{"accent":"#0062ab","accentHover":"#005391","accentSoft":"#00487f","accentEmphasisFg":"#00487f"}},{"id":"slate","label":{"ko":"슬레이트","en":"Slate"},"swatch":"#b6cde7","dark":{"accent":"#607d9d","accentHover":"#7390b2","accentSoft":"#b6cde7","accentEmphasisFg":"#d0e0f3"},"light":{"accent":"#416389","accentHover":"#325378","accentSoft":"#2a486a","accentEmphasisFg":"#2a486a"}},{"id":"indigo","label":{"ko":"인디고","en":"Indigo"},"swatch":"#b8c8ff","dark":{"accent":"#6375be","accentHover":"#7588d4","accentSoft":"#b8c8ff","accentEmphasisFg":"#d2ddff"},"light":{"accent":"#4657af","accentHover":"#38479d","accentSoft":"#303d8b","accentEmphasisFg":"#303d8b"}},{"id":"violet","label":{"ko":"바이올렛","en":"Violet"},"swatch":"#c7c2ff","dark":{"accent":"#786eba","accentHover":"#8a82cf","accentSoft":"#c7c2ff","accentEmphasisFg":"#dcd9ff"},"light":{"accent":"#5f4faa","accentHover":"#503f98","accentSoft":"#453687","accentEmphasisFg":"#453687"}},{"id":"lavender","label":{"ko":"라벤더","en":"Lavender"},"swatch":"#d5bef9","dark":{"accent":"#8769b3","accentHover":"#9b7cc8","accentSoft":"#d5bef9","accentEmphasisFg":"#e5d6ff"},"light":{"accent":"#7049a1","accentHover":"#603890","accentSoft":"#54307f","accentEmphasisFg":"#54307f"}},{"id":"mauve","label":{"ko":"모브","en":"Mauve"},"swatch":"#d7c2de","dark":{"accent":"#896f92","accentHover":"#9d83a6","accentSoft":"#d7c2de","accentEmphasisFg":"#e7d8ec"},"light":{"accent":"#72537b","accentHover":"#62446b","accentSoft":"#553a5e","accentEmphasisFg":"#553a5e"}},{"id":"orchid","label":{"ko":"오키드","en":"Orchid"},"swatch":"#e7b8e9","dark":{"accent":"#9c61a0","accentHover":"#b174b4","accentSoft":"#e7b8e9","accentEmphasisFg":"#f3d1f4"},"light":{"accent":"#863f8b","accentHover":"#762f7b","accentSoft":"#67276c","accentEmphasisFg":"#67276c"}},{"id":"magenta","label":{"ko":"마젠타","en":"Magenta"},"swatch":"#f1b5dc","dark":{"accent":"#a75d90","accentHover":"#bc71a4","accentSoft":"#f1b5dc","accentEmphasisFg":"#facfeb"},"light":{"accent":"#923a79","accentHover":"#802a69","accentSoft":"#71225c","accentEmphasisFg":"#71225c"}},{"id":"pink","label":{"ko":"핑크","en":"Pink"},"swatch":"#f9b4cb","dark":{"accent":"#af5b7b","accentHover":"#c56e8f","accentSoft":"#f9b4cb","accentEmphasisFg":"#ffcfde"},"light":{"accent":"#9b3661","accentHover":"#892552","accentSoft":"#781e47","accentEmphasisFg":"#781e47"}},{"id":"rose","label":{"ko":"로즈","en":"Rose"},"swatch":"#fdb4bc","dark":{"accent":"#b35b68","accentHover":"#c96e7b","accentSoft":"#fdb4bc","accentEmphasisFg":"#ffd0d5"},"light":{"accent":"#9f364b","accentHover":"#8d243c","accentSoft":"#7c1d33","accentEmphasisFg":"#7c1d33"}},{"id":"rosewood","label":{"ko":"로즈우드","en":"Rosewood"},"swatch":"#e6c0c0","dark":{"accent":"#9b6c6d","accentHover":"#af7f80","accentSoft":"#e6c0c0","accentEmphasisFg":"#f2d6d6"},"light":{"accent":"#854f51","accentHover":"#744042","accentSoft":"#663638","accentEmphasisFg":"#663638"}},{"id":"salmon","label":{"ko":"살몬","en":"Salmon"},"swatch":"#fdb6a9","dark":{"accent":"#b45e4f","accentHover":"#ca7162","accentSoft":"#fdb6a9","accentEmphasisFg":"#ffd2c9"},"light":{"accent":"#a0392a","accentHover":"#8e281a","accentSoft":"#7d2014","accentEmphasisFg":"#7d2014"}},{"id":"coral","label":{"ko":"코럴","en":"Coral"},"swatch":"#fbb99e","dark":{"accent":"#b2623e","accentHover":"#c77551","accentSoft":"#fbb99e","accentEmphasisFg":"#ffd3c1"},"light":{"accent":"#9e3f05","accentHover":"#873300","accentSoft":"#762c00","accentEmphasisFg":"#762c00"}},{"id":"peach","label":{"ko":"피치","en":"Peach"},"swatch":"#f4be92","dark":{"accent":"#ab6829","accentHover":"#c07b3e","accentSoft":"#f4be92","accentEmphasisFg":"#fdd5b7"},"light":{"accent":"#8e4e00","accentHover":"#794100","accentSoft":"#693800","accentEmphasisFg":"#693800"}},{"id":"mocha","label":{"ko":"모카","en":"Mocha"},"swatch":"#dfc5ad","dark":{"accent":"#937354","accentHover":"#a88767","accentSoft":"#dfc5ad","accentEmphasisFg":"#eddac9"},"light":{"accent":"#7d5732","accentHover":"#6d4822","accentSoft":"#5f3e1b","accentEmphasisFg":"#5f3e1b"}},{"id":"amber","label":{"ko":"앰버","en":"Amber"},"swatch":"#e8c58b","dark":{"accent":"#9e7112","accentHover":"#b2852e","accentSoft":"#e8c58b","accentEmphasisFg":"#f3dab2"},"light":{"accent":"#7e5900","accentHover":"#6b4a00","accentSoft":"#5d4000","accentEmphasisFg":"#5d4000"}},{"id":"lemon","label":{"ko":"레몬","en":"Lemon"},"swatch":"#d7cc8b","dark":{"accent":"#8a7b13","accentHover":"#9e8f2f","accentSoft":"#d7cc8b","accentEmphasisFg":"#e7e0b2"},"light":{"accent":"#6e6100","accentHover":"#5d5200","accentSoft":"#514700","accentEmphasisFg":"#514700"}},{"id":"olive","label":{"ko":"올리브","en":"Olive"},"swatch":"#c7cfaf","dark":{"accent":"#777f57","accentHover":"#8a9369","accentSoft":"#c7cfaf","accentEmphasisFg":"#dce1ca"},"light":{"accent":"#5d6634","accentHover":"#4e5625","accentSoft":"#434b1e","accentEmphasisFg":"#434b1e"}},{"id":"lime","label":{"ko":"라임","en":"Lime"},"swatch":"#bad69a","dark":{"accent":"#668736","accentHover":"#799b4a","accentSoft":"#bad69a","accentEmphasisFg":"#d2e6bc"},"light":{"accent":"#4b6d00","accentHover":"#3f5c00","accentSoft":"#365000","accentEmphasisFg":"#365000"}},{"id":"green","label":{"ko":"그린","en":"Green"},"swatch":"#a8daa9","dark":{"accent":"#4a8c4e","accentHover":"#5ea061","accentSoft":"#a8daa9","accentEmphasisFg":"#c6e9c6"},"light":{"accent":"#1a7426","accentHover":"#006317","accentSoft":"#005612","accentEmphasisFg":"#005612"}},{"id":"sage","label":{"ko":"세이지","en":"Sage"},"swatch":"#b7d2c3","dark":{"accent":"#618371","accentHover":"#749784","accentSoft":"#b7d2c3","accentEmphasisFg":"#d1e3d8"},"light":{"accent":"#416b55","accentHover":"#325b46","accentSoft":"#2a4f3c","accentEmphasisFg":"#2a4f3c"}},{"id":"mint","label":{"ko":"민트","en":"Mint"},"swatch":"#91ddc2","dark":{"accent":"#009070","accentHover":"#2ca483","accentSoft":"#91ddc2","accentEmphasisFg":"#b7ecd8"},"light":{"accent":"#007258","accentHover":"#00604a","accentSoft":"#005440","accentEmphasisFg":"#005440"}},{"id":"teal","label":{"ko":"티얼","en":"Teal"},"swatch":"#87ddd3","dark":{"accent":"#008d83","accentHover":"#00a398","accentSoft":"#87ddd3","accentEmphasisFg":"#b1ece4"},"light":{"accent":"#007068","accentHover":"#005e57","accentSoft":"#00524c","accentEmphasisFg":"#00524c"}},{"id":"aqua","label":{"ko":"아쿠아","en":"Aqua"},"swatch":"#84dbe5","dark":{"accent":"#008b96","accentHover":"#00a0ad","accentSoft":"#84dbe5","accentEmphasisFg":"#b0eaf1"},"light":{"accent":"#006e77","accentHover":"#005d65","accentSoft":"#005058","accentEmphasisFg":"#005058"}},{"id":"skyblue","label":{"ko":"스카이블루","en":"Sky blue"},"swatch":"#8ed6f6","dark":{"accent":"#0086af","accentHover":"#289bc4","accentSoft":"#8ed6f6","accentEmphasisFg":"#b5e7fe"},"light":{"accent":"#006a8b","accentHover":"#005a76","accentSoft":"#004e67","accentEmphasisFg":"#004e67"}}],"defaultAccentId":"mint"};

  // generate_style.cmake·generate_accents.cmake의 색 읽기다. 알파를 생략하면 불투명이다.
  function parseColor(text) {
    const m = /^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/.exec(text);
    if (!m) throw new Error("bad color " + text);
    const rgb = parseInt(m[1], 16);
    const alpha = m[2] ? parseInt(m[2], 16) : 0xff;
    return ((alpha << 24) | rgb) >>> 0;
  }

  // make_ui_color(r, g, b, a = 255)
  const makeColor = (r, g, b, a) => (((a === undefined ? 255 : a) << 24) | (r << 16) | (g << 8) | b) >>> 0;

  // with_alpha: float로 자르고 0.5를 더해 버림한다 (ui_theme.h:30-35).
  function withAlpha(color, alpha) {
    const a = Math.fround(alpha);
    const clamped = a < 0 ? 0 : a > 1 ? 1 : a;
    const value = Math.trunc(Math.fround(Math.fround(clamped * Math.fround(255)) + Math.fround(0.5)));
    return ((color & 0x00ffffff) | (value << 24)) >>> 0;
  }

  // relative_luminance: double로 셈하고 float로 돌려준다. 알파는 보지 않는다 (ui_theme.cpp:67-70, 149-155).
  const linearChannel = (v) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
  function relativeLuminance(color) {
    const red = linearChannel(((color >>> 16) & 0xff) / 255);
    const green = linearChannel(((color >>> 8) & 0xff) / 255);
    const blue = linearChannel((color & 0xff) / 255);
    return Math.fround(0.2126 * red + 0.7152 * green + 0.0722 * blue);
  }
  // is_dark_background: 밝기 ≤ 0.179f (ui_theme.cpp:157-162)
  const isDarkBackground = (color) => relativeLuminance(color) <= Math.fround(0.179);
  // foreground_on: 흰색 또는 검정 (ui_theme.cpp:164-168)
  const foregroundOn = (background) => (isDarkBackground(background) ? makeColor(255, 255, 255) : makeColor(0, 0, 0));

  // test의 대비식 (tests/ui_theme_tests.cpp:59-64)
  function contrastRatio(first, second) {
    const a = relativeLuminance(first);
    const b = relativeLuminance(second);
    return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
  }
  // test의 composite_over (tests/ui_theme_tests.cpp:390-395): 위 색을 아래 바탕에 얹은 불투명 색
  function compositeOver(top, bottom) {
    const alpha = top >>> 24;
    const mix = (shift) => Math.trunc((((top >>> shift) & 0xff) * alpha + ((bottom >>> shift) & 0xff) * (255 - alpha) + 127) / 255);
    return makeColor(mix(16), mix(8), mix(0));
  }

  // 스타일 JSON의 한 테마 → neutral_color_palette (빠진 나중 역할은 생성기의 대신 키로)
  const OPTIONAL = { raisedBackground: "noticeBackground", thumbBackground: "buttonPressedBackground", backdropBackground: "windowBackground", syntaxKeyword: "primaryForeground", syntaxString: "primaryForeground", syntaxComment: "primaryForeground", syntaxNumber: "primaryForeground", syntaxType: "primaryForeground", syntaxFunction: "primaryForeground" };
  const NEUTRAL_KEYS = ["windowBackground", "surfaceBackground", "primaryForeground", "secondaryForeground", "disabledForeground", "divider", "inputBackground", "inputBorder", "controlBorder", "groupBorder", "warningAccent", "errorAccent", "buttonHoverBackground", "buttonHoverForeground", "buttonPressedBackground", "tooltipBackground", "tooltipBorder", "contentShadow", "noticeBackground", "raisedBackground", "thumbBackground", "backdropBackground", "syntaxKeyword", "syntaxString", "syntaxComment", "syntaxNumber", "syntaxType", "syntaxFunction"];
  const snake = (key) => key.replace(/([A-Z])/g, "_$1").toLowerCase();
  function neutralFrom(json) {
    const out = {};
    for (const key of NEUTRAL_KEYS) out[snake(key)] = parseColor(json[key] !== undefined ? json[key] : json[OPTIONAL[key]]);
    const c = json.caption;
    out.caption = {
      background: parseColor(c.background),
      foreground: parseColor(c.foreground),
      button_hover_background: parseColor(c.buttonHoverBackground),
      button_hover_foreground: parseColor(c.buttonHoverForeground),
      close_button_hover_background: parseColor(c.closeButtonHoverBackground),
      close_button_hover_foreground: parseColor(c.closeButtonHoverForeground),
    };
    return out;
  }
  const TONE_KEYS = { pressed: "pressed", soft_button: "softButton", soft_button_hover: "softButtonHover", active_toggle: "activeToggle", selection: "selection", row_selection: "rowSelection", drop_target: "dropTarget", danger_button: "dangerButton", danger_button_hover: "dangerButtonHover", danger_button_pressed: "dangerButtonPressed" };
  function tonesFrom(json) {
    const out = {};
    for (const [field, key] of Object.entries(TONE_KEYS)) out[field] = Math.fround(json[key]);
    return out;
  }
  const STYLE = { dark: neutralFrom(DATA.style.dark), light: neutralFrom(DATA.style.light), tones: tonesFrom(DATA.style.tones) };

  const ACCENTS = DATA.accents.map((a) => ({
    id: a.id,
    label: typeof a.label === "string" ? a.label : a.label.ko,
    names: typeof a.label === "string" ? {} : a.label,
    swatch: parseColor(a.swatch),
    dark: { accent: parseColor(a.dark.accent), hover: parseColor(a.dark.accentHover), soft: parseColor(a.dark.accentSoft), emphasis_foreground: parseColor(a.dark.accentEmphasisFg) },
    light: { accent: parseColor(a.light.accent), hover: parseColor(a.light.accentHover), soft: parseColor(a.light.accentSoft), emphasis_foreground: parseColor(a.light.accentEmphasisFg) },
  }));
  // accent_for: 없는 id는 기본 id다 (ui_theme.cpp:329-347, 실행 시점 system 항목은 옮기지 않았다)
  function accentFor(id) {
    return ACCENTS.find((a) => a.id === id) || ACCENTS.find((a) => a.id === DATA.defaultAccentId) || ACCENTS[0];
  }
  // accent_definition::for_theme: light면 light, 그 밖이면 dark (ui_theme.cpp:311-322)
  const forTheme = (accent, theme) => (theme === "light" ? accent.light : accent.dark);
  // ui_style::neutral_for: light면 light, 그 밖이면 dark (ui_style.cpp:6-9)
  const neutralFor = (style, theme) => (theme === "light" ? style.light : style.dark);

  // compose_palette (ui_theme.cpp:170-229) — 칸 차례도 원본 그대로다
  function composePalette(neutral, accent, tones) {
    return {
      window_background: neutral.window_background,
      surface_background: neutral.surface_background,
      primary_foreground: neutral.primary_foreground,
      secondary_foreground: neutral.secondary_foreground,
      disabled_foreground: neutral.disabled_foreground,
      divider: neutral.divider,
      input_background: neutral.input_background,
      input_border: neutral.input_border,
      control_border: neutral.control_border,
      group_border: neutral.group_border,
      accent: accent.accent,
      accent_hover: accent.hover,
      accent_soft: accent.soft,
      accent_emphasis_foreground: accent.emphasis_foreground,
      accent_foreground: foregroundOn(accent.accent),
      error_foreground: foregroundOn(neutral.error_accent),
      accent_pressed: withAlpha(accent.accent, tones.pressed),
      soft_button_background: withAlpha(accent.soft, tones.soft_button),
      soft_button_hover_background: withAlpha(accent.hover, tones.soft_button_hover),
      active_toggle_background: withAlpha(accent.soft, tones.active_toggle),
      selection_background: withAlpha(accent.soft, tones.selection),
      selection_foreground: neutral.primary_foreground,
      row_selection_background: withAlpha(accent.soft, tones.row_selection),
      drop_target_background: withAlpha(accent.accent, tones.drop_target),
      danger_button_background: withAlpha(neutral.error_accent, tones.danger_button),
      danger_button_hover_background: withAlpha(neutral.error_accent, tones.danger_button_hover),
      danger_button_pressed_background: withAlpha(neutral.error_accent, tones.danger_button_pressed),
      warning_accent: neutral.warning_accent,
      error_accent: neutral.error_accent,
      button_hover_background: neutral.button_hover_background,
      button_hover_foreground: neutral.button_hover_foreground,
      button_pressed_background: neutral.button_pressed_background,
      tooltip_background: neutral.tooltip_background,
      tooltip_border: neutral.tooltip_border,
      content_shadow: neutral.content_shadow,
      notice_background: neutral.notice_background,
      raised_background: neutral.raised_background,
      thumb_background: neutral.thumb_background,
      backdrop_background: neutral.backdrop_background,
      caption: Object.assign({}, neutral.caption),
      document_added_background: withAlpha(accent.accent, 0.12),
      document_removed_background: withAlpha(neutral.error_accent, 0.12),
      document_changed_background: withAlpha(neutral.warning_accent, 0.12),
      document_search_background: withAlpha(accent.soft, 0.35),
      document_search_foreground: neutral.primary_foreground,
      syntax_keyword: neutral.syntax_keyword,
      syntax_string: neutral.syntax_string,
      syntax_comment: neutral.syntax_comment,
      syntax_number: neutral.syntax_number,
      syntax_type: neutral.syntax_type,
      syntax_function: neutral.syntax_function,
    };
  }

  // high_contrast_colors의 기본값: OS를 읽을 수 없을 때의 검정 바탕 (ui_theme.h:322-338)
  const DEFAULT_HIGH_CONTRAST = {
    window_background: makeColor(0, 0, 0),
    window_foreground: makeColor(255, 255, 255),
    highlight_background: makeColor(255, 255, 255),
    highlight_foreground: makeColor(0, 0, 0),
    emphasis: makeColor(255, 255, 255),
    button_background: makeColor(0, 0, 0),
    button_foreground: makeColor(255, 255, 255),
    disabled_foreground: makeColor(0, 255, 0),
  };

  // high_contrast_palette_for (ui_theme.cpp:231-309)
  function highContrastPalette(colors) {
    return {
      window_background: colors.window_background,
      surface_background: colors.window_background,
      primary_foreground: colors.window_foreground,
      secondary_foreground: colors.window_foreground,
      disabled_foreground: colors.disabled_foreground,
      divider: colors.window_foreground,
      input_background: colors.window_background,
      input_border: colors.window_foreground,
      control_border: colors.window_foreground,
      group_border: colors.window_foreground,
      accent: colors.emphasis,
      accent_hover: colors.emphasis,
      accent_soft: colors.highlight_background,
      accent_emphasis_foreground: colors.highlight_foreground,
      accent_foreground: colors.window_background,
      error_foreground: colors.window_background,
      accent_pressed: colors.highlight_background,
      soft_button_background: colors.highlight_background,
      soft_button_hover_background: colors.highlight_background,
      active_toggle_background: colors.highlight_background,
      selection_background: colors.highlight_background,
      selection_foreground: colors.highlight_foreground,
      row_selection_background: makeColor(0, 0, 0, 0),
      drop_target_background: colors.highlight_background,
      danger_button_background: colors.button_background,
      danger_button_hover_background: colors.highlight_background,
      danger_button_pressed_background: colors.highlight_background,
      warning_accent: colors.window_foreground,
      error_accent: colors.window_foreground,
      button_hover_background: colors.highlight_background,
      button_hover_foreground: colors.highlight_foreground,
      button_pressed_background: colors.highlight_background,
      tooltip_background: colors.window_background,
      tooltip_border: colors.window_foreground,
      content_shadow: colors.window_foreground,
      notice_background: colors.window_background,
      raised_background: colors.window_background,
      thumb_background: colors.highlight_background,
      backdrop_background: colors.window_background,
      caption: {
        background: colors.button_background,
        foreground: colors.button_foreground,
        button_hover_background: colors.highlight_background,
        button_hover_foreground: colors.highlight_foreground,
        close_button_hover_background: colors.highlight_background,
        close_button_hover_foreground: colors.highlight_foreground,
      },
      document_added_background: colors.window_background,
      document_removed_background: colors.window_background,
      document_changed_background: colors.window_background,
      document_search_background: colors.highlight_background,
      document_search_foreground: colors.highlight_foreground,
      syntax_keyword: colors.window_foreground,
      syntax_string: colors.window_foreground,
      syntax_comment: colors.window_foreground,
      syntax_number: colors.window_foreground,
      syntax_type: colors.window_foreground,
      syntax_function: colors.window_foreground,
    };
  }

  // color_palette_for(style, theme, accent) (ui_style.cpp:40-47): 고대비는 기본 시스템 색(검정 바탕)이다
  function colorPaletteFor(theme, accent, style) {
    const s = style || STYLE;
    if (theme === "high_contrast") return highContrastPalette(DEFAULT_HIGH_CONTRAST);
    return composePalette(neutralFor(s, theme), forTheme(accent, theme), s.tones);
  }

  const hex = (color) => "#" + (color & 0xffffff).toString(16).padStart(6, "0") + ((color >>> 24) === 0xff ? "" : (color >>> 24).toString(16).padStart(2, "0"));

  const api = { DATA, STYLE, ACCENTS, DEFAULT_HIGH_CONTRAST, parseColor, makeColor, withAlpha, relativeLuminance, isDarkBackground, foregroundOn, contrastRatio, compositeOver, accentFor, forTheme, neutralFor, composePalette, highContrastPalette, colorPaletteFor, hex };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.LuilTheme = api;
})(typeof window !== "undefined" ? window : globalThis);

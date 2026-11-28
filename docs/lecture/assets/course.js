/* luil 해부학 — 강의 사이트 공용 스크립트
 *
 * 모든 페이지가 쓰는 것만 둔다: caption 막대와 강의 메뉴, 슬라이드 목차, 코드 강조와
 * 줄 단위 해설(코드 읽기), 확인 문제, 용어 풍선, 탭, 실험 창 틀, 발표 모드.
 * 강의마다의 실험은 그 페이지의 스크립트가 아래 LU 도우미를 써서 만든다.
 */
(function () {
  "use strict";

  const COURSE = {
    title: "luil 해부학",
    repo: "https://github.com/yuldK/luil",
    commit: "563a8552f11b4b9f3067f01ce2855bd107590ba5",
    commitShort: "563a855",
    commitDate: "2026-11-25",
    parts: [
      { id: "p1", name: "1부 · 큰 그림", lectures: [1, 2] },
      { id: "p2", name: "2부 · 뼈대", lectures: [3, 4, 5] },
      { id: "p3", name: "3부 · 사람과 화면 사이", lectures: [6, 7, 8] },
      { id: "p4", name: "4부 · 앱다운 앱", lectures: [9, 10] },
      { id: "p5", name: "5부 · 바깥 세계", lectures: [11, 12] },
    ],
    lectures: [
      { n: 1, file: "01-orientation.html", title: "luil이라는 프레임워크" },
      { n: 2, file: "02-threads.html", title: "세 스레드와 한 방향 흐름" },
      { n: 3, file: "03-messaging.html", title: "스레드 경계 메시징" },
      { n: 4, file: "04-tree.html", title: "불변 UI tree" },
      { n: 5, file: "05-layout.html", title: "배치: 자리를 나눠 주는 법" },
      { n: 6, file: "06-input.html", title: "입력의 여정" },
      { n: 7, file: "07-rendering.html", title: "그리기: frame에서 픽셀까지" },
      { n: 8, file: "08-text.html", title: "글자의 과학" },
      { n: 9, file: "09-motion.html", title: "움직임의 물리" },
      { n: 10, file: "10-undo.html", title: "되돌리기의 설계" },
      { n: 11, file: "11-platforms.html", title: "하나의 앱, 네 플랫폼" },
      { n: 12, file: "12-quality.html", title: "품질과 빌드" },
    ],
  };

  const pad2 = (n) => String(n).padStart(2, "0");

  /* ---------- 작은 도우미 ---------- */

  function el(tag, props, ...children) {
    const node = document.createElement(tag);
    if (props) {
      for (const [k, v] of Object.entries(props)) {
        if (v == null || v === false) continue;
        if (k === "class") node.className = v;
        else if (k === "text") node.textContent = v;
        else if (k === "html") node.innerHTML = v;
        else if (k === "style" && typeof v === "object") Object.assign(node.style, v);
        else if (k.startsWith("on") && typeof v === "function") node.addEventListener(k.slice(2), v);
        else if (k === "dataset") Object.assign(node.dataset, v);
        else node.setAttribute(k, v === true ? "" : v);
      }
    }
    for (const c of children.flat()) {
      if (c == null || c === false) continue;
      node.append(c instanceof Node ? c : document.createTextNode(String(c)));
    }
    return node;
  }

  const SVG_NS = "http://www.w3.org/2000/svg";
  function svg(tag, attrs, ...children) {
    const node = document.createElementNS(SVG_NS, tag);
    if (attrs) {
      for (const [k, v] of Object.entries(attrs)) {
        if (v == null || v === false) continue;
        if (k === "text") node.textContent = v;
        else node.setAttribute(k, v);
      }
    }
    for (const c of children.flat()) {
      if (c == null || c === false) continue;
      node.append(c instanceof Node ? c : document.createTextNode(String(c)));
    }
    return node;
  }

  function icon(code, extra) {
    return el("i", { class: "ci" + (extra ? " " + extra : ""), "aria-hidden": "true", text: String.fromCodePoint(parseInt(code, 16)) });
  }

  function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  const reducedQuery = window.matchMedia ? window.matchMedia("(prefers-reduced-motion: reduce)") : null;
  const reduced = () => !!(reducedQuery && reducedQuery.matches);

  const themeListeners = new Set();
  function onTheme(fn) {
    themeListeners.add(fn);
    return () => themeListeners.delete(fn);
  }
  function emitTheme() {
    for (const fn of themeListeners) {
      try {
        fn();
      } catch (e) {
        console.error(e);
      }
    }
  }

  function store(key, value) {
    try {
      if (value === undefined) return localStorage.getItem("luil-lecture:" + key);
      if (value === null) localStorage.removeItem("luil-lecture:" + key);
      else localStorage.setItem("luil-lecture:" + key, value);
    } catch (e) {
      return null;
    }
    return null;
  }

  function sourceUrl(path, start, end) {
    let url = COURSE.repo + "/blob/" + COURSE.commit + "/" + path;
    if (start) url += "#L" + start + (end && end !== start ? "-L" + end : "");
    return url;
  }

  /* ---------- 테마 ---------- */

  const THEMES = ["system", "light", "dark"];
  const THEME_LABEL = { system: "시스템", light: "밝게", dark: "어둡게" };

  function currentTheme() {
    const t = document.documentElement.getAttribute("data-theme");
    return t === "light" || t === "dark" ? t : "system";
  }

  function setTheme(t) {
    if (t === "system") document.documentElement.removeAttribute("data-theme");
    else document.documentElement.setAttribute("data-theme", t);
    store("theme", t === "system" ? null : t);
    updateThemeButton();
    emitTheme();
  }

  function updateThemeButton() {
    const btn = document.querySelector('.topbar [data-act="theme"]');
    if (!btn) return;
    const t = currentTheme();
    btn.title = "테마: " + THEME_LABEL[t] + " (눌러서 바꾸기)";
    btn.querySelector(".label").textContent = THEME_LABEL[t];
  }

  if (window.matchMedia) {
    const mq = window.matchMedia("(prefers-color-scheme: dark)");
    const handler = () => {
      if (currentTheme() === "system") emitTheme();
    };
    if (mq.addEventListener) mq.addEventListener("change", handler);
    else if (mq.addListener) mq.addListener(handler);
  }

  /* ---------- caption 막대 ---------- */

  function buildTopbar() {
    const bar = document.querySelector("header.topbar");
    if (!bar) return;
    const n = Number(document.body.dataset.lecture || 0);
    const lec = COURSE.lectures.find((l) => l.n === n);
    bar.textContent = "";
    // DOM append는 null을 글 "null"로 넣으므로 빈 자리를 걸러 낸다 (강의가 아닌 쪽: 홈)
    const parts = [
      el("a", { class: "brand", href: "index.html", title: "강의 홈" }, icon("EB21"), el("span", { text: COURSE.title })),
      lec ? el("span", { class: "crumb", text: pad2(lec.n) + " " + lec.title }) : null,
      el("span", { class: "spacer" }),
      lec ? el("button", { class: "cap-btn only-narrow", type: "button", "data-act": "toc", title: "이 강의의 슬라이드 목차" }, icon("EB86"), el("span", { class: "label", text: "목차" })) : null,
      el("button", { class: "cap-btn", type: "button", "data-act": "lectures", "aria-haspopup": "true", "aria-expanded": "false", title: "강의 목록" }, icon("EB9C"), el("span", { class: "label", text: "강의" })),
      el("button", { class: "cap-btn", type: "button", "data-act": "theme" }, icon("EAC6"), el("span", { class: "label", text: "시스템" })),
      lec ? el("button", { class: "cap-btn", type: "button", "data-act": "present", title: "발표 모드 (슬라이드를 한 장씩, ← → 로 넘김)" }, icon("EB4C"), el("span", { class: "label", text: "발표" })) : null,
    ];
    bar.append(...parts.filter(Boolean));
    updateThemeButton();

    bar.addEventListener("click", (e) => {
      const btn = e.target.closest("[data-act]");
      if (!btn) return;
      const act = btn.dataset.act;
      if (act === "theme") setTheme(THEMES[(THEMES.indexOf(currentTheme()) + 1) % THEMES.length]);
      else if (act === "lectures") toggleLectureMenu(btn);
      else if (act === "toc") openTocDrawer();
      else if (act === "present") enterPresent();
    });
  }

  let menuEl = null;
  function closeLectureMenu() {
    if (!menuEl) return;
    menuEl.remove();
    menuEl = null;
    const btn = document.querySelector('.topbar [data-act="lectures"]');
    if (btn) btn.setAttribute("aria-expanded", "false");
  }

  function toggleLectureMenu(btn) {
    if (menuEl) {
      closeLectureMenu();
      return;
    }
    const n = Number(document.body.dataset.lecture || 0);
    menuEl = el("nav", { class: "menu-pop", "aria-label": "강의 목록" });
    menuEl.append(el("a", { href: "index.html", "aria-current": n === 0 ? "page" : null }, el("span", { class: "n", text: "홈" }), el("span", { text: "강의 소개와 기능 지도" })));
    for (const part of COURSE.parts) {
      menuEl.append(el("div", { class: "part", text: part.name }));
      for (const ln of part.lectures) {
        const l = COURSE.lectures.find((x) => x.n === ln);
        menuEl.append(el("a", { href: l.file, "aria-current": l.n === n ? "page" : null }, el("span", { class: "n", text: pad2(l.n) }), el("span", { text: l.title })));
      }
    }
    btn.setAttribute("aria-expanded", "true");
    document.querySelector("header.topbar").append(menuEl);
    const first = menuEl.querySelector('[aria-current="page"]') || menuEl.querySelector("a");
    if (first) first.focus({ preventScroll: true });
  }

  document.addEventListener("click", (e) => {
    if (menuEl && !e.target.closest(".menu-pop") && !e.target.closest('[data-act="lectures"]')) closeLectureMenu();
  });

  /* ---------- 슬라이드 목차 ---------- */

  function slideList() {
    return Array.from(document.querySelectorAll("section.slide[id]"));
  }

  function slideTitle(s) {
    const h = s.querySelector(".slide-head h2, h2");
    return s.dataset.toc || (h ? h.textContent.trim() : s.id);
  }

  function buildToc(container) {
    const slides = slideList();
    if (!slides.length) return null;
    container.textContent = "";
    container.append(el("div", { class: "toc-title", text: "이 강의의 슬라이드" }));
    const ol = el("ol");
    for (const s of slides) {
      const num = s.dataset.num || "";
      ol.append(el("li", null, el("a", { href: "#" + s.id, "data-slide": s.id }, el("span", { class: "n", text: num }), el("span", { text: slideTitle(s) }))));
    }
    container.append(ol);
    return container;
  }

  function setupToc() {
    const toc = document.querySelector("nav.toc");
    if (!toc) return;
    buildToc(toc);
    const slides = slideList();
    if (!("IntersectionObserver" in window) || !slides.length) return;
    const visible = new Map();
    const io = new IntersectionObserver(
      (entries) => {
        for (const en of entries) visible.set(en.target.id, en.isIntersecting);
        const cur = slides.find((s) => visible.get(s.id));
        if (cur) markToc(cur.id);
      },
      { rootMargin: "-30% 0px -60% 0px" }
    );
    slides.forEach((s) => io.observe(s));
  }

  function markToc(id) {
    document.querySelectorAll(".toc a[data-slide]").forEach((a) => a.classList.toggle("current", a.dataset.slide === id));
  }

  function openTocDrawer() {
    const drawer = el("div", { class: "toc-drawer", role: "dialog", "aria-modal": "true", "aria-label": "슬라이드 목차" });
    const sheet = el("div", { class: "sheet" });
    const close = el("button", { class: "cap-btn", type: "button", title: "닫기" }, icon("EA76"));
    const nav = el("nav", { class: "toc" });
    buildToc(nav);
    sheet.append(el("div", { class: "sheet-head" }, el("span", { text: "목차" }), close), nav);
    drawer.append(sheet);
    const done = () => {
      drawer.remove();
      document.removeEventListener("keydown", onKey, true);
    };
    const onKey = (e) => {
      if (e.key === "Escape") {
        e.stopPropagation();
        done();
      }
    };
    drawer.addEventListener("click", (e) => {
      if (e.target === drawer || e.target.closest("a") || e.target.closest("button") === close) done();
    });
    document.addEventListener("keydown", onKey, true);
    document.body.append(drawer);
    close.focus();
  }

  /* ---------- 코드 ---------- */

  const GAP = /^\s*⋯(?:@(\d+))?\s*$/;

  function escapeHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  // 강조한 HTML을 줄로 자른다. 줄을 넘는 span은 줄 끝에서 닫고 다음 줄에서 다시 연다.
  function splitHighlighted(html) {
    const lines = [];
    const stack = [];
    let cur = "";
    const re = /(<span[^>]*>)|(<\/span>)|(\n)|([^<\n]+)|(<)/g;
    let m;
    while ((m = re.exec(html))) {
      if (m[1]) {
        stack.push(m[1]);
        cur += m[1];
      } else if (m[2]) {
        stack.pop();
        cur += m[2];
      } else if (m[3]) {
        cur += "</span>".repeat(stack.length);
        lines.push(cur);
        cur = stack.join("");
      } else cur += m[0];
    }
    cur += "</span>".repeat(stack.length);
    lines.push(cur);
    return lines;
  }

  function dedent(text) {
    const lines = text.replace(/\r\n?/g, "\n").replace(/^\n+/, "").replace(/\s+$/, "").split("\n");
    let min = Infinity;
    for (const l of lines) {
      if (!l.trim()) continue;
      const ind = l.match(/^[ \t]*/)[0].length;
      if (ind < min) min = ind;
    }
    if (!isFinite(min) || min === 0) return lines;
    return lines.map((l) => l.slice(Math.min(min, l.match(/^[ \t]*/)[0].length)));
  }

  function renderCode(box) {
    if (box.dataset.rendered) return;
    box.dataset.rendered = "1";
    const src = box.querySelector('script[type="text/plain"]');
    const raw = src ? src.textContent : box.textContent;
    const lines = dedent(raw);
    const lang = box.dataset.lang || "cpp";
    const start = Number(box.dataset.start || 1);

    // 생략 줄(⋯@N)은 강조기의 상태를 깨지 않도록 주석으로 바꿔 넘긴다.
    const gapInfo = lines.map((l) => {
      const m = l.match(GAP);
      return m ? { next: m[1] ? Number(m[1]) : null } : null;
    });
    const commentFor = lang === "cmake" || lang === "powershell" || lang === "bash" || lang === "python" ? "# ⋯" : "// ⋯";
    const plain = lines.map((l, i) => (gapInfo[i] ? commentFor : l)).join("\n");
    let html;
    if (window.hljs && lang !== "text") {
      try {
        html = window.hljs.highlight(plain, { language: lang, ignoreIllegals: true }).value;
      } catch (e) {
        html = escapeHtml(plain);
      }
    } else html = escapeHtml(plain);
    const hl = splitHighlighted(html);

    const code = el("code");
    let n = start;
    let last = start;
    for (let i = 0; i < lines.length; i++) {
      if (gapInfo[i]) {
        code.append(el("span", { class: "ln gap", "aria-hidden": "true" }, el("span", { class: "lc", text: "    " + (box.dataset.gapText || "생략") })));
        if (gapInfo[i].next) n = gapInfo[i].next;
        continue;
      }
      const row = el("span", { class: "ln", "data-n": String(n) });
      const lc = el("span", { class: "lc" });
      lc.innerHTML = hl[i] || " ";
      row.append(lc);
      code.append(row);
      last = n;
      n++;
    }
    const pre = el("pre", { tabindex: "0" }, code);

    const path = box.dataset.path;
    const head = el("div", { class: "code-head" });
    head.append(icon(path ? "EAE9" : "EAC4"));
    if (path) {
      const label = box.dataset.label || path + (box.dataset.start ? ":" + start : "");
      if (box.dataset.nolink != null) head.append(el("span", { class: "path" }, el("bdi", { text: label })));
      else
        head.append(
          el("a", { class: "path", href: sourceUrl(path, box.dataset.start ? start : 0, box.dataset.start ? last : 0), target: "_blank", rel: "noopener", title: "GitHub에서 원본 보기 (" + COURSE.commitShort + ")" }, el("bdi", { text: label }))
        );
    } else head.append(el("span", { class: "path" }, el("bdi", { text: box.dataset.label || lang })));
    const tools = el("div", { class: "tools" });
    const copy = el("button", { type: "button", title: "코드 복사" }, icon("EBCC"), el("span", { text: "복사" }));
    copy.addEventListener("click", () => {
      const text = lines.map((l, i) => (gapInfo[i] ? "// ..." : l)).join("\n");
      const ok = () => {
        copy.lastChild.textContent = "복사했습니다";
        setTimeout(() => (copy.lastChild.textContent = "복사"), 1600);
      };
      const fallback = () => {
        const range = document.createRange();
        range.selectNodeContents(code);
        const sel = window.getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
        copy.lastChild.textContent = "선택했습니다 — Ctrl+C";
        setTimeout(() => (copy.lastChild.textContent = "복사"), 2400);
      };
      try {
        navigator.clipboard.writeText(text).then(ok, fallback);
      } catch (e) {
        fallback();
      }
    });
    tools.append(copy);
    head.append(tools);

    if (src) src.remove();
    box.textContent = "";
    box.append(head, pre);
  }

  function parseLines(spec) {
    const out = new Set();
    if (!spec) return out;
    for (const part of String(spec).split(",")) {
      const [a, b] = part.split("-").map((x) => Number(x.trim()));
      if (!a) continue;
      for (let i = a; i <= (b || a); i++) out.add(i);
    }
    return out;
  }

  function highlightLines(box, spec, scroll) {
    const set = parseLines(spec);
    let first = null;
    box.querySelectorAll(".ln[data-n]").forEach((ln) => {
      const on = set.has(Number(ln.dataset.n));
      ln.classList.toggle("hi", on);
      if (on && !first) first = ln;
    });
    box.classList.toggle("focusing", set.size > 0);
    if (scroll && first) {
      const pre = box.querySelector("pre");
      if (pre && pre.scrollHeight > pre.clientHeight + 4) {
        const top = first.offsetTop - pre.clientHeight * 0.25;
        pre.scrollTo({ top: Math.max(0, top), behavior: reduced() ? "auto" : "smooth" });
      }
    }
  }

  /* ---------- 코드 읽기 ---------- */

  function setupWalk(walk) {
    const box = walk.querySelector(".code");
    const srcList = walk.querySelector(".walk-steps-src");
    if (!box || !srcList) return;
    renderCode(box);
    const steps = Array.from(srcList.children).map((li) => ({ lines: li.dataset.lines, title: li.dataset.title || "", html: li.innerHTML }));
    srcList.remove();
    if (!steps.length) return;

    const panel = el("div", { class: "walk-panel" });
    const dots = el("div", { class: "dots", role: "tablist", "aria-label": "해설 단계" });
    const prev = el("button", { class: "btn sm ghost", type: "button", title: "이전 단계" }, icon("EAB5"));
    const next = el("button", { class: "btn sm soft", type: "button", title: "다음 단계" }, el("span", { text: "다음" }), icon("EAB6"));
    const bar = el("div", { class: "walk-bar" }, el("span", { text: "읽는 순서" }), dots, el("div", { class: "nav" }, prev, next));
    const body = el("div", { class: "walk-step", "aria-live": "polite" });
    panel.append(bar, body);
    walk.append(panel);

    let i = 0;
    const show = (k, scroll) => {
      i = Math.max(0, Math.min(steps.length - 1, k));
      const s = steps[i];
      body.innerHTML = "";
      const where = s.lines ? s.lines.replace(/-/g, "–").replace(/,/g, ", ") + "행" : "";
      body.append(el("div", { class: "where", text: (i + 1) + " / " + steps.length + (where ? " · " + where : "") }));
      if (s.title) body.append(el("h4", { text: s.title }));
      const p = el("div");
      p.innerHTML = s.html;
      body.append(p);
      highlightLines(box, s.lines, scroll);
      dots.querySelectorAll("button").forEach((b, j) => b.setAttribute("aria-current", j === i ? "step" : "false"));
      prev.disabled = i === 0;
      next.disabled = i === steps.length - 1;
    };
    steps.forEach((s, j) => {
      dots.append(el("button", { type: "button", role: "tab", text: String(j + 1), title: s.title || "단계 " + (j + 1), onclick: () => show(j, true) }));
    });
    prev.addEventListener("click", () => show(i - 1, true));
    next.addEventListener("click", () => show(i + 1, true));
    show(0, false);
  }

  /* ---------- 확인 문제 ---------- */

  function setupQuizzes() {
    const sets = document.querySelectorAll(".quiz-set");
    sets.forEach((set) => {
      const quizzes = Array.from(set.querySelectorAll(".quiz"));
      const score = el("p", { class: "quiz-score", "aria-live": "polite" });
      let answered = 0;
      let right = 0;
      const update = () => {
        score.textContent = answered === 0 ? quizzes.length + "문제입니다. 고르면 바로 해설이 열립니다." : quizzes.length + "문제 중 " + answered + "문제를 풀었고 " + right + "문제를 맞혔습니다.";
      };
      quizzes.forEach((q, qi) => {
        const ans = Number(q.dataset.answer);
        const qp = q.querySelector(".q");
        if (qp && !qp.querySelector(".qn")) {
          const inner = qp.innerHTML;
          qp.innerHTML = "";
          qp.append(el("span", { class: "qn", text: "Q" + (qi + 1) }), el("span", { html: inner }));
        }
        const items = Array.from(q.querySelectorAll(".choices > li"));
        const explain = q.querySelector(".explain");
        if (explain) explain.hidden = true;
        const buttons = items.map((li, k) => {
          const b = el("button", { type: "button" }, el("span", { class: "mk", text: "①②③④⑤⑥"[k] || String(k + 1) }), el("span", { html: li.innerHTML }), el("span", { class: "res ci", "aria-hidden": "true" }));
          li.textContent = "";
          li.append(b);
          return b;
        });
        let done = false;
        buttons.forEach((b, k) => {
          b.addEventListener("click", () => {
            if (done) return;
            done = true;
            answered++;
            const ok = k + 1 === ans;
            if (ok) right++;
            b.classList.add(ok ? "right" : "wrong");
            b.querySelector(".res").textContent = ok ? "" : "";
            b.setAttribute("aria-label", (ok ? "정답: " : "오답: ") + b.textContent);
            if (!ok && buttons[ans - 1]) {
              buttons[ans - 1].classList.add("right");
              buttons[ans - 1].querySelector(".res").textContent = "";
            }
            if (explain) {
              explain.hidden = false;
              if (!explain.querySelector(".verdict")) explain.prepend(el("b", { class: "verdict", text: ok ? "맞았습니다. " : "정답은 " + "①②③④⑤⑥"[ans - 1] + "입니다. " }));
            }
            update();
          });
        });
      });
      update();
      set.prepend(score);
    });
  }

  /* ---------- 탭 ---------- */

  function setupTabs() {
    document.querySelectorAll(".tabs").forEach((tabs, ti) => {
      const panels = Array.from(tabs.querySelectorAll(":scope > [data-tab]"));
      if (!panels.length) return;
      const list = el("div", { class: "tablist", role: "tablist" });
      const buttons = panels.map((p, i) => {
        const id = "tab-" + ti + "-" + i;
        p.id = p.id || id + "-panel";
        p.setAttribute("role", "tabpanel");
        p.setAttribute("aria-labelledby", id);
        const b = el("button", { type: "button", role: "tab", id, "aria-controls": p.id, text: p.dataset.tab });
        b.addEventListener("click", () => select(i));
        b.addEventListener("keydown", (e) => {
          if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
            e.preventDefault();
            const k = (i + (e.key === "ArrowRight" ? 1 : panels.length - 1)) % panels.length;
            select(k);
            buttons[k].focus();
          }
        });
        list.append(b);
        return b;
      });
      const select = (k) => {
        panels.forEach((p, i) => (p.hidden = i !== k));
        buttons.forEach((b, i) => {
          b.setAttribute("aria-selected", i === k ? "true" : "false");
          b.tabIndex = i === k ? 0 : -1;
        });
        tabs.dispatchEvent(new CustomEvent("tabchange", { detail: { index: k } }));
      };
      tabs.prepend(list);
      select(Number(tabs.dataset.initial || 0));
    });
  }

  /* ---------- 용어 풍선 ---------- */

  let termPop = null;
  function showTerm(t) {
    hideTerm();
    const tip = t.dataset.tip;
    if (!tip) return;
    termPop = el("div", { class: "term-pop", role: "tooltip" });
    if (t.dataset.term) termPop.append(el("b", { text: t.dataset.term }));
    termPop.append(el("span", { text: tip }));
    document.body.append(termPop);
    const r = t.getBoundingClientRect();
    const pr = termPop.getBoundingClientRect();
    let left = r.left + window.scrollX + r.width / 2 - pr.width / 2;
    left = Math.max(window.scrollX + 12, Math.min(left, window.scrollX + document.documentElement.clientWidth - pr.width - 12));
    let top = r.bottom + window.scrollY + 8;
    if (r.bottom + pr.height + 16 > window.innerHeight && r.top > pr.height + 16) top = r.top + window.scrollY - pr.height - 8;
    termPop.style.left = left + "px";
    termPop.style.top = top + "px";
  }
  function hideTerm() {
    if (termPop) termPop.remove();
    termPop = null;
  }
  function setupTerms() {
    document.querySelectorAll(".term").forEach((t) => {
      t.tabIndex = 0;
      t.setAttribute("aria-describedby", "");
      t.addEventListener("mouseenter", () => showTerm(t));
      t.addEventListener("mouseleave", hideTerm);
      t.addEventListener("focus", () => showTerm(t));
      t.addEventListener("blur", hideTerm);
      t.addEventListener("click", (e) => {
        e.preventDefault();
        if (termPop) hideTerm();
        else showTerm(t);
      });
    });
    window.addEventListener("scroll", hideTerm, { passive: true });
  }

  /* ---------- 실험 창 틀 ---------- */

  function setupWindows() {
    document.querySelectorAll(".lw[data-title]").forEach((w) => {
      if (w.querySelector(":scope > .lw-cap")) return;
      const cap = el("div", { class: "lw-cap" });
      cap.append(icon(w.dataset.icon || "EB7F", "mark"), el("span", { class: "lw-title", text: w.dataset.title }));
      if (w.dataset.surface) cap.append(el("span", { class: "lw-surface", text: w.dataset.surface }));
      const tools = el("div", { class: "lw-tools" });
      if (w.hasAttribute("data-reset")) {
        tools.append(el("button", { type: "button", title: "처음 상태로", onclick: () => w.dispatchEvent(new CustomEvent("lw-reset")) }, icon("EAD2")));
      }
      if (w.requestFullscreen) {
        tools.append(
          el("button", {
            type: "button",
            title: "이 실험만 크게 보기",
            onclick: () => {
              if (document.fullscreenElement === w) document.exitFullscreen().catch(() => {});
              else w.requestFullscreen().catch(() => {});
            },
          }, icon("EB4C"))
        );
      }
      cap.append(tools);
      w.prepend(cap);
    });
  }

  /* ---------- 발표 모드 ---------- */

  let presentIndex = 0;
  let presentBar = null;

  function presentSlides() {
    const hero = document.querySelector(".hero");
    return [hero, ...slideList()].filter(Boolean);
  }

  function enterPresent() {
    const slides = presentSlides();
    if (!slides.length) return;
    // 지금 화면 위쪽에 걸린 슬라이드부터 시작한다.
    let start = 0;
    const hashId = location.hash.slice(1);
    const byHash = slides.findIndex((s) => s.id && s.id === hashId);
    if (byHash >= 0) start = byHash;
    else {
      for (let i = 0; i < slides.length; i++) {
        if (slides[i].getBoundingClientRect().top <= window.innerHeight * 0.4) start = i;
      }
    }
    document.documentElement.classList.add("presenting");
    if (!presentBar) {
      presentBar = el("div", { class: "present-bar", role: "toolbar", "aria-label": "발표 모드" });
      const prev = el("button", { class: "btn sm ghost", type: "button", title: "이전 (←)", onclick: () => showPresent(presentIndex - 1) }, icon("EAB5"));
      const next = el("button", { class: "btn sm soft", type: "button", title: "다음 (→)", onclick: () => showPresent(presentIndex + 1) }, el("span", { text: "다음" }), icon("EAB6"));
      const exit = el("button", { class: "btn sm ghost", type: "button", title: "발표 끝내기 (Esc)", onclick: exitPresent }, icon("EB4D"), el("span", { text: "끝내기" }));
      presentBar.append(prev, el("span", { class: "where" }), el("span", { class: "progress" }, el("i")), next, exit);
      document.body.append(presentBar);
    }
    presentBar.hidden = false;
    showPresent(start);
    if (document.documentElement.requestFullscreen && !document.fullscreenElement) {
      document.documentElement.requestFullscreen().catch(() => {});
    }
    document.addEventListener("keydown", presentKeys);
  }

  function exitPresent() {
    const slides = presentSlides();
    const cur = slides[presentIndex];
    document.documentElement.classList.remove("presenting");
    slides.forEach((s) => s.classList.remove("current"));
    if (presentBar) presentBar.hidden = true;
    document.removeEventListener("keydown", presentKeys);
    if (document.fullscreenElement === document.documentElement) document.exitFullscreen().catch(() => {});
    if (cur) cur.scrollIntoView({ block: "start" });
  }

  function showPresent(k) {
    const slides = presentSlides();
    presentIndex = Math.max(0, Math.min(slides.length - 1, k));
    slides.forEach((s, i) => s.classList.toggle("current", i === presentIndex));
    const cur = slides[presentIndex];
    cur.scrollTop = 0;
    const where = presentBar.querySelector(".where");
    const num = cur.dataset.num || pad2(Number(document.body.dataset.lecture || 0));
    where.textContent = num + " · " + (cur.classList.contains("hero") ? "표지" : slideTitle(cur)) + "  (" + (presentIndex + 1) + "/" + slides.length + ")";
    presentBar.querySelector(".progress i").style.width = ((presentIndex + 1) / slides.length) * 100 + "%";
    if (cur.id) {
      try {
        history.replaceState(null, "", "#" + cur.id);
      } catch (e) {
        /* 액자 안에서는 막힐 수 있다 */
      }
    }
  }

  function presentKeys(e) {
    if (e.defaultPrevented) return;
    const t = e.target;
    if (t && (t.closest("input, textarea, select, [contenteditable], [data-keys]") || (t.closest && t.closest(".lw") && e.key === " "))) return;
    if (e.altKey || e.ctrlKey || e.metaKey) return;
    const k = e.key;
    if (k === "ArrowRight" || k === "PageDown" || (k === " " && !e.shiftKey)) {
      e.preventDefault();
      showPresent(presentIndex + 1);
    } else if (k === "ArrowLeft" || k === "PageUp" || (k === " " && e.shiftKey)) {
      e.preventDefault();
      showPresent(presentIndex - 1);
    } else if (k === "Home") {
      e.preventDefault();
      showPresent(0);
    } else if (k === "End") {
      e.preventDefault();
      showPresent(presentSlides().length - 1);
    } else if (k === "Escape") {
      e.preventDefault();
      exitPresent();
    }
  }

  document.addEventListener("fullscreenchange", () => {
    // 브라우저의 Esc가 전체 화면만 끝낸 경우에도 발표는 이어 간다 — 다시 Esc를 누르면 끝난다.
  });

  /* ---------- 강의 끝의 이전·다음 ---------- */

  function setupPager() {
    const pager = document.querySelector(".pager[data-auto]");
    if (!pager) return;
    const n = Number(document.body.dataset.lecture || 0);
    const prev = COURSE.lectures.find((l) => l.n === n - 1);
    const next = COURSE.lectures.find((l) => l.n === n + 1);
    pager.textContent = "";
    pager.append(
      prev
        ? el("a", { class: "prev", href: prev.file }, el("span", { class: "dir" }, icon("EA9B"), "이전 강의"), el("span", { class: "ttl", text: pad2(prev.n) + " " + prev.title }))
        : el("a", { class: "prev", href: "index.html" }, el("span", { class: "dir" }, icon("EA9B"), "강의 홈"), el("span", { class: "ttl", text: "강의 소개와 기능 지도" })),
      next
        ? el("a", { class: "next", href: next.file }, el("span", { class: "dir" }, "다음 강의", icon("EA9C")), el("span", { class: "ttl", text: pad2(next.n) + " " + next.title }))
        : el("a", { class: "next", href: "index.html#atlas" }, el("span", { class: "dir" }, "마무리", icon("EA9C")), el("span", { class: "ttl", text: "기능 지도에서 더 둘러보기" }))
    );
  }

  function setupFooter() {
    const foot = document.querySelector("footer.site-foot");
    if (!foot || foot.dataset.filled) return;
    foot.dataset.filled = "1";
    foot.innerHTML = "";
    const inner = el("div", { class: "inner" });
    inner.append(
      el(
        "div",
        null,
        el("div", null, "luil 해부학 — C++20 UI 프레임워크 luil을 읽는 강의. 코드 발췌는 ", el("a", { href: COURSE.repo + "/tree/" + COURSE.commit, target: "_blank", rel: "noopener", text: "main " + COURSE.commitShort }), " (" + COURSE.commitDate + ") 기준입니다."),
        el("div", null, "luil 소스와 문서는 MIT 라이선스입니다. 아이콘은 Microsoft의 Codicons(CC BY 4.0)를 이 사이트에 쓰는 글리프만 남겨 실었습니다.")
      ),
      el("div", null, el("a", { href: COURSE.repo, target: "_blank", rel: "noopener", text: "github.com/yuldK/luil" }))
    );
    foot.append(inner);
  }

  /* ---------- 흐름도: 세 스레드를 가로지르는 사건 ----------
   *
   * 넓은 화면에서는 스레드가 가로 띠(시간은 왼쪽 → 오른쪽), 좁은 화면에서는 세로 칸
   * (시간은 위 → 아래)이다. viewBox를 그릇의 실제 폭으로 잡아 글자가 줄지 않는다.
   * hop(a, b)이 점 하나를 a에서 b로 옮기고 Promise를 돌려주므로 장면을 async로 짠다.
   */
  function flow(container, model) {
    const api = { gen: 0, speed: 1, horizontal: true };
    let nodes = {};
    let root = null;
    let layer = null;
    let lastW = 0;

    const lanesIndex = {};
    model.lanes.forEach((l, i) => (lanesIndex[l.id] = i));

    function clip(cx, cy, tx, ty, w, h) {
      // (cx,cy)에서 (tx,ty)로 가는 선이 (cx,cy) 중심의 w×h 상자를 나가는 점
      const dx = tx - cx;
      const dy = ty - cy;
      if (!dx && !dy) return [cx, cy];
      const sx = dx ? w / 2 / Math.abs(dx) : Infinity;
      const sy = dy ? h / 2 / Math.abs(dy) : Infinity;
      const s = Math.min(sx, sy);
      return [cx + dx * s, cy + dy * s];
    }

    function render() {
      const W = Math.max(300, Math.round(container.clientWidth || 720));
      lastW = W;
      const labelW = 118;
      const horizontal = (W - labelW - 8) / model.cols >= (model.minCol || 98);
      api.horizontal = horizontal;
      nodes = {};
      const markerId = "fa-" + Math.random().toString(36).slice(2, 8);
      let H;
      const geo = {};
      const L = model.lanes.length;
      if (horizontal) {
        const laneH = model.laneH || 92;
        const colW = (W - labelW - 8) / model.cols;
        H = L * laneH + 4;
        model.lanes.forEach((ln, i) => (geo["lane:" + ln.id] = { x: 0, y: 2 + i * laneH, w: W, h: laneH - 4 }));
        for (const s of model.stations) {
          const i = lanesIndex[s.lane];
          geo[s.id] = { cx: labelW + colW * (s.col + 0.5), cy: 2 + i * laneH + (laneH - 4) / 2, w: Math.min(colW - 12, 132), h: 50 };
        }
      } else {
        const headH = 46;
        const rowH = model.rowH || 68;
        const laneW = W / L;
        H = headH + model.cols * rowH + 10;
        model.lanes.forEach((ln, i) => (geo["lane:" + ln.id] = { x: i * laneW + 1, y: 0, w: laneW - 2, h: H }));
        for (const s of model.stations) {
          const i = lanesIndex[s.lane];
          geo[s.id] = { cx: (i + 0.5) * laneW, cy: headH + s.col * rowH + rowH / 2, w: laneW - 14, h: 48 };
        }
      }

      const s = svg("svg", { viewBox: `0 0 ${W} ${H}`, width: W, height: H, role: "img", "aria-label": model.label || "스레드 사이의 흐름" });
      s.append(
        svg("defs", null,
          svg("marker", { id: markerId, viewBox: "0 0 10 10", refX: "9", refY: "5", markerWidth: "9", markerHeight: "9", markerUnits: "userSpaceOnUse", orient: "auto-start-reverse" },
            svg("path", { d: "M0,0 L10,5 L0,10 z", class: "d-arrowhead" })))
      );
      // 띠
      model.lanes.forEach((ln) => {
        const g = geo["lane:" + ln.id];
        s.append(svg("rect", { x: g.x, y: g.y, width: g.w, height: g.h, rx: 4, class: "d-lane " + ln.id }));
        if (horizontal) {
          s.append(svg("circle", { cx: 16, cy: g.y + 22, r: 4, class: "d-dot " + ln.id }));
          s.append(svg("text", { x: 26, y: g.y + 26, class: "d-text b", text: ln.name }));
          if (ln.sub) s.append(svg("text", { x: 14, y: g.y + 46, class: "d-text sm dim", text: ln.sub }));
          if (ln.sub2) s.append(svg("text", { x: 14, y: g.y + 62, class: "d-text sm dim", text: ln.sub2 }));
        } else {
          s.append(svg("circle", { cx: g.x + g.w / 2 - textWidthGuess(ln.short || ln.name) / 2 - 8, cy: 19, r: 4, class: "d-dot " + ln.id }));
          s.append(svg("text", { x: g.x + g.w / 2 + 4, y: 23, "text-anchor": "middle", class: "d-text b", text: ln.short || ln.name }));
        }
      });
      // 선
      const edgeLayer = svg("g");
      s.append(edgeLayer);
      for (const e of model.edges || []) {
        const a = geo[e.from];
        const b = geo[e.to];
        if (!a || !b) continue;
        const [x1, y1] = clip(a.cx, a.cy, b.cx, b.cy, a.w + 6, a.h + 6);
        const [x2, y2] = clip(b.cx, b.cy, a.cx, a.cy, b.w + 8, b.h + 8);
        edgeLayer.append(svg("line", { x1, y1, x2, y2, class: "d-line" + (e.dash ? " dash" : ""), "marker-end": `url(#${markerId})` }));
        if (e.label && horizontal) {
          const mx = (x1 + x2) / 2;
          const my = (y1 + y2) / 2;
          const vertical = Math.abs(y2 - y1) > Math.abs(x2 - x1);
          edgeLayer.append(svg("text", { x: vertical ? mx + 6 : mx, y: vertical ? my + 4 : my - 6, "text-anchor": vertical ? "start" : "middle", class: "d-text mono faint d-halo", text: e.label }));
        }
      }
      // 정거장
      for (const st of model.stations) {
        const g = geo[st.id];
        const grp = svg("g", { class: "d-station " + st.lane, "data-id": st.id });
        grp.append(svg("rect", { x: g.cx - g.w / 2, y: g.cy - g.h / 2, width: g.w, height: g.h, rx: 4, class: "d-box" }));
        grp.append(svg("text", { x: g.cx, y: g.cy - (st.sub ? 3 : -4), "text-anchor": "middle", class: "d-text b", text: st.title }));
        if (st.sub) grp.append(svg("text", { x: g.cx, y: g.cy + 14, "text-anchor": "middle", class: "d-text mono dim", text: st.sub }));
        s.append(grp);
        nodes[st.id] = { g: grp, geo: g };
      }
      layer = svg("g");
      s.append(layer);
      container.textContent = "";
      container.append(s);
      root = s;
      fitStationText();
    }

    // 상자보다 긴 글은 글자 폭을 눌러 상자 안에 맞춘다 (좁은 화면의 긴 타입 이름).
    function fitStationText() {
      for (const n of Object.values(nodes)) {
        const max = n.geo.w - 10;
        n.g.querySelectorAll("text").forEach((t) => {
          try {
            if (t.getComputedTextLength() > max) {
              t.setAttribute("textLength", max);
              t.setAttribute("lengthAdjust", "spacingAndGlyphs");
            }
          } catch (e) {
            /* 아직 그려지지 않았으면 건너뛴다 */
          }
        });
      }
    }

    function textWidthGuess(t) {
      return String(t).length * 7.2;
    }

    api.render = render;

    api.mark = (id, on) => {
      const n = nodes[id];
      if (n) n.g.classList.toggle("on", on !== false);
    };

    api.clear = () => {
      Object.values(nodes).forEach((n) => n.g.classList.remove("on", "done"));
      if (layer) layer.textContent = "";
    };

    api.reset = () => {
      api.gen++;
      api.clear();
    };

    api.hop = (from, to, opts) => {
      opts = opts || {};
      const gen = api.gen;
      const a = nodes[from];
      const b = nodes[to];
      if (!a || !b || !layer) return Promise.resolve(false);
      api.mark(from, true);
      const base = (opts.ms || 620) / api.speed;
      if (reduced()) {
        return new Promise((res) => {
          setTimeout(() => {
            if (gen !== api.gen) return res(false);
            a.g.classList.remove("on");
            a.g.classList.add("done");
            api.mark(to, true);
            res(true);
          }, Math.min(420, base));
        });
      }
      const dot = svg("circle", { r: 6, cx: a.geo.cx, cy: a.geo.cy, class: "d-flow-dot " + (opts.kind || "accent") });
      layer.append(dot);
      return new Promise((res) => {
        const t0 = performance.now();
        const step = (t) => {
          if (gen !== api.gen) {
            dot.remove();
            return res(false);
          }
          // 화면 크기가 바뀌어 다시 그렸으면 새 자리를 따른다.
          const A = nodes[from].geo;
          const B = nodes[to].geo;
          const k = Math.min(1, (t - t0) / base);
          const e = k < 0.5 ? 2 * k * k : 1 - Math.pow(-2 * k + 2, 2) / 2;
          dot.setAttribute("cx", A.cx + (B.cx - A.cx) * e);
          dot.setAttribute("cy", A.cy + (B.cy - A.cy) * e);
          if (k < 1) requestAnimationFrame(step);
          else {
            dot.remove();
            const na = nodes[from];
            if (na && !opts.keep) {
              na.g.classList.remove("on");
              na.g.classList.add("done");
            }
            api.mark(to, true);
            res(true);
          }
        };
        requestAnimationFrame(step);
      });
    };

    api.wait = (ms) => {
      const gen = api.gen;
      return new Promise((res) => setTimeout(() => res(gen === api.gen), ms / api.speed));
    };

    render();
    if ("ResizeObserver" in window) {
      const ro = new ResizeObserver(() => {
        const W = Math.round(container.clientWidth);
        if (W && Math.abs(W - lastW) > 6) {
          const marked = Object.entries(nodes).filter(([, n]) => n.g.classList.contains("on")).map(([id]) => id);
          const doneIds = Object.entries(nodes).filter(([, n]) => n.g.classList.contains("done")).map(([id]) => id);
          const savedLayer = layer ? Array.from(layer.childNodes) : [];
          render();
          marked.forEach((id) => api.mark(id, true));
          doneIds.forEach((id) => nodes[id] && nodes[id].g.classList.add("done"));
          savedLayer.forEach((n) => layer.append(n));
        }
      });
      ro.observe(container);
    }
    return api;
  }

  /* ---------- 시작 ---------- */

  function boot() {
    buildTopbar();
    setupToc();
    document.querySelectorAll(".walk").forEach(setupWalk);
    document.querySelectorAll(".code").forEach(renderCode);
    setupQuizzes();
    setupTabs();
    setupTerms();
    setupWindows();
    setupPager();
    setupFooter();
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        closeLectureMenu();
        hideTerm();
      }
    });
  }

  window.LU = { COURSE, el, svg, icon, cssVar, onTheme, reduced, store, sourceUrl, highlightLines, renderCode, pad2, flow };

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();

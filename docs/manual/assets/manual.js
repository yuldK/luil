/* luil 설명서 — 공용 스크립트
 *
 * 모든 문서가 쓰는 것만 둔다: caption 막대와 검색, 왼쪽 설명서 목차(지금 문서의 차례 포함),
 * 코드 강조와 줄 단위 해설(코드 읽기), 탭, 용어 풍선, 실습 창 틀, 앞뒤 문서.
 * 문서마다의 실습은 그 페이지의 스크립트가 아래 LU 도우미를 써서 만든다.
 */
(function () {
  "use strict";

  // 왼쪽 목차와 앞뒤 문서의 차례. 문서를 더하면 여기에 한 줄을 더한다.
  const DOCS = {
    title: "luil 설명서",
    repo: "https://github.com/yuldK/luil",
    commit: "563a8552f11b4b9f3067f01ce2855bd107590ba5",
    commitShort: "563a855",
    commitDate: "2026-11-25",
    sections: [
      {
        id: "start",
        name: "시작하기",
        pages: [
          { id: "home", file: "index.html", title: "설명서 홈" },
          { id: "overview", file: "overview.html", title: "luil 개요" },
          { id: "start", file: "start.html", title: "설치와 첫 앱" },
        ],
      },
      {
        id: "concepts",
        name: "핵심 개념",
        pages: [
          { id: "threads", file: "threads.html", title: "스레드 모델" },
          { id: "messaging", file: "messaging.html", title: "스레드 경계 메시징" },
          { id: "ui-tree", file: "ui-tree.html", title: "불변 UI tree" },
          { id: "layout", file: "layout.html", title: "배치" },
          { id: "input", file: "input.html", title: "입력과 초점" },
          { id: "rendering", file: "rendering.html", title: "그리기와 렌더러" },
          { id: "text", file: "text.html", title: "텍스트 처리" },
          { id: "motion", file: "motion.html", title: "애니메이션" },
          { id: "undo", file: "undo.html", title: "앱 수준 실행 취소" },
          { id: "platforms", file: "platforms.html", title: "플랫폼과 host" },
          { id: "testing", file: "testing.html", title: "시험과 빌드" },
        ],
      },
      {
        id: "ui",
        name: "컴포넌트",
        pages: [
          { id: "containers", file: "containers.html", title: "배치 요소" },
          { id: "scrolling", file: "scrolling.html", title: "스크롤과 확대" },
          { id: "buttons", file: "buttons.html", title: "단추와 명령" },
          { id: "choices", file: "choices.html", title: "체크·라디오·선택 묶음" },
          { id: "values", file: "values.html", title: "슬라이더·숫자·별점·진행" },
          { id: "pickers", file: "pickers.html", title: "날짜·시간·색 고르기" },
          { id: "text-display", file: "text-display.html", title: "글 표시와 글꼴" },
          { id: "text-input", file: "text-input.html", title: "텍스트 입력과 서식 글" },
          { id: "lists", file: "lists.html", title: "목록과 트리 뷰" },
          { id: "table", file: "table.html", title: "표" },
          { id: "tabs", file: "tabs.html", title: "탭" },
          { id: "menus", file: "menus.html", title: "메뉴와 메뉴 막대" },
          { id: "popups", file: "popups.html", title: "드롭다운·콤보·팝업" },
          { id: "dialogs", file: "dialogs.html", title: "대화 상자와 시트" },
          { id: "notifications", file: "notifications.html", title: "알림" },
          { id: "media", file: "media.html", title: "이미지·영상·웹·잉크" },
        ],
      },
      {
        id: "app",
        name: "앱과 플랫폼",
        pages: [
          { id: "window", file: "window.html", title: "창과 셸 통합" },
          { id: "services", file: "services.html", title: "플랫폼 서비스와 네트워크" },
          { id: "theming", file: "theming.html", title: "테마와 스타일" },
          { id: "accessibility", file: "accessibility.html", title: "접근성" },
          { id: "i18n", file: "i18n.html", title: "다국어" },
        ],
      },
      {
        id: "ref",
        name: "레퍼런스",
        pages: [{ id: "api", file: "api.html", title: "API 찾아보기" }],
      },
    ],
  };
  // 이전 이름. 실습 스크립트가 LU.COURSE.repo 따위를 읽어도 그대로 돈다.
  const COURSE = DOCS;
  const ALL_PAGES = DOCS.sections.flatMap((s) => s.pages.map((p) => Object.assign({ section: s }, p)));

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
      if (value === undefined) return localStorage.getItem("luil-manual:" + key);
      if (value === null) localStorage.removeItem("luil-manual:" + key);
      else localStorage.setItem("luil-manual:" + key, value);
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

  function currentPage() {
    const id = document.body.dataset.page || "home";
    return ALL_PAGES.find((p) => p.id === id) || ALL_PAGES[0];
  }

  function buildTopbar() {
    const bar = document.querySelector("header.topbar");
    if (!bar) return;
    const page = currentPage();
    bar.textContent = "";
    // DOM append는 null을 글 "null"로 넣으므로 빈 자리를 걸러 낸다
    const parts = [
      el("button", { class: "cap-btn only-narrow", type: "button", "data-act": "nav", title: "설명서 목차", "aria-label": "설명서 목차" }, icon("EB94")),
      el("a", { class: "brand", href: "index.html", title: "설명서 홈" }, icon("EAA4"), el("span", { class: "brand-name", text: DOCS.title })),
      page.id !== "home" ? el("span", { class: "crumb", text: page.title }) : null,
      el("span", { class: "spacer" }),
      buildSearch(),
      el("button", { class: "cap-btn", type: "button", "data-act": "theme" }, icon("EAC6"), el("span", { class: "label", text: "시스템" })),
      el("a", { class: "cap-btn", href: DOCS.repo, target: "_blank", rel: "noopener", title: "GitHub 저장소", "aria-label": "GitHub 저장소" }, icon("EA84")),
    ];
    bar.append(...parts.filter(Boolean));
    updateThemeButton();

    bar.addEventListener("click", (e) => {
      const btn = e.target.closest("[data-act]");
      if (!btn) return;
      const act = btn.dataset.act;
      if (act === "theme") setTheme(THEMES[(THEMES.indexOf(currentTheme()) + 1) % THEMES.length]);
      else if (act === "nav") openNavDrawer();
    });
  }

  /* ---------- 검색 ---------- */

  // 색인은 tools/gen_search.py가 만든 assets/search-index.js다. 처음 검색 칸에 들어올 때 싣는다.
  let searchLoading = null;
  let searchEntries = null;

  // API 기호의 주소. luil:: 를 떼고 :: 는 --, _ 는 - 로 쓴다. 겹치면 차례대로 -2, -3 …을 붙인다.
  // 차례가 같아야 주소가 같으므로 api.html과 검색이 이 함수 하나를 쓴다.
  function apiSlugs(qnames) {
    const used = new Set();
    return qnames.map((q, i) => {
      let base = q.replace(/^luil::/, "").replace(/::/g, "--").replace(/_/g, "-").replace(/[^A-Za-z0-9-]/g, "");
      if (!base) base = "sym-" + i;
      let slug = base;
      let k = 2;
      while (used.has(slug)) slug = base + "-" + k++;
      used.add(slug);
      return slug;
    });
  }

  const API_KIND = { S: "구조체", C: "클래스", U: "공용체", E: "열거형", e: "열거값", f: "함수", m: "멤버 함수", c: "생성자", v: "필드", a: "별칭", k: "상수", K: "개념" };

  function expandSearch(raw) {
    if (!raw || !raw.docs) return [];
    const out = raw.docs.map((r) => ({ k: r[0], p: r[1], s: r[2], pt: r[3], st: r[4], t: r[5] }));
    const slugs = apiSlugs(raw.api.map((r) => r[0]));
    raw.api.forEach((r, i) => {
      out.push({ k: "api", p: "api.html", s: slugs[i], pt: (API_KIND[r[1]] || "기호") + " · " + String(raw.headers[r[2]] || "").replace(/^include\//, ""), st: r[0].replace(/^luil::/, ""), t: r[3] });
    });
    return out;
  }

  function loadSearch() {
    if (searchEntries) return Promise.resolve(searchEntries);
    if (searchLoading) return searchLoading;
    searchLoading = new Promise((resolve) => {
      const done = () => {
        searchEntries = expandSearch(window.LUIL_SEARCH);
        resolve(searchEntries);
      };
      if (window.LUIL_SEARCH) return done();
      const s = document.createElement("script");
      s.src = "assets/search-index.js";
      s.onload = done;
      s.onerror = () => resolve([]);
      document.head.append(s);
    });
    return searchLoading;
  }

  function norm(s) {
    return String(s || "").normalize("NFC").toLowerCase();
  }

  // 낱말마다 제목·본문에서 찾는다. 한국어는 띄어쓰기가 흔들리므로 공백을 지운 글에서도 찾는다.
  function searchIndex(data, query) {
    const q = norm(query).trim();
    if (!q) return [];
    const words = q.split(/\s+/).filter(Boolean);
    const qJoined = q.replace(/\s+/g, "");
    const out = [];
    for (const d of data) {
      const title = d._t || (d._t = norm(d.st + " " + (d.pt || "")));
      const text = d._x || (d._x = norm(d.t));
      const textJ = d._j || (d._j = text.replace(/\s+/g, ""));
      const titleJ = d._tj || (d._tj = title.replace(/\s+/g, ""));
      let score = 0;
      let ok = true;
      for (const w of words) {
        const inTitle = title.includes(w) || titleJ.includes(w);
        const inText = text.includes(w) || textJ.includes(w);
        if (!inTitle && !inText) {
          ok = false;
          break;
        }
        score += inTitle ? 10 : 1;
      }
      if (!ok) {
        // 낱말로 나누면 놓치는 "트리뷰" 같은 붙여 쓴 질의
        if (words.length > 1 && (titleJ.includes(qJoined) || textJ.includes(qJoined))) score = titleJ.includes(qJoined) ? 12 : 3;
        else continue;
      }
      if (norm(d.st).startsWith(words[0])) score += 6;
      if (d.k === "api" && norm(d.st).split("::").pop() === q) score += 30;
      if (d.k === "page") score += 2;
      out.push({ d, score });
    }
    out.sort((a, b) => b.score - a.score);
    return out.slice(0, 24).map((r) => r.d);
  }

  function snippet(text, query) {
    const t = String(text || "");
    const words = norm(query).split(/\s+/).filter(Boolean);
    const lower = norm(t);
    let at = -1;
    for (const w of words) {
      at = lower.indexOf(w);
      if (at >= 0) break;
    }
    const start = Math.max(0, at - 36);
    const piece = (start > 0 ? "…" : "") + t.slice(start, start + 120) + (start + 120 < t.length ? "…" : "");
    let html = escapeHtml(piece);
    for (const w of words) {
      if (!w) continue;
      const re = new RegExp(w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
      html = html.replace(re, (m) => "<mark>" + m + "</mark>");
    }
    return html;
  }

  const KIND_LABEL = { page: "문서", sec: "문서", faq: "질문", api: "API" };

  function buildSearch() {
    const wrap = el("div", { class: "search", role: "search" });
    const input = el("input", { type: "search", placeholder: "검색 (예: 트리 뷰, list_config)", "aria-label": "설명서 검색", autocomplete: "off", spellcheck: "false" });
    const pop = el("div", { class: "search-pop", role: "listbox", hidden: true });
    wrap.append(icon("EA6D", "s-ico"), input, el("kbd", { class: "s-key", text: "/" }), pop);
    let results = [];
    let sel = -1;
    let timer = 0;

    const close = () => {
      pop.hidden = true;
      sel = -1;
    };
    const mark = () => {
      pop.querySelectorAll("a").forEach((a, i) => a.setAttribute("aria-selected", i === sel ? "true" : "false"));
      const a = pop.querySelectorAll("a")[sel];
      if (a) a.scrollIntoView({ block: "nearest" });
    };
    const render = async () => {
      const q = input.value;
      if (!q.trim()) {
        close();
        return;
      }
      const data = await loadSearch();
      if (q !== input.value) return;
      results = searchIndex(data, q);
      pop.textContent = "";
      if (!results.length) {
        pop.append(el("div", { class: "s-empty", text: data.length ? "찾은 것이 없습니다. 다른 낱말이나 API 이름으로 찾아 보세요." : "검색 색인을 싣지 못했습니다." }));
      }
      for (const r of results) {
        const href = r.p + (r.s ? "#" + r.s : "");
        const a = el("a", { href, role: "option" });
        a.append(
          el("span", { class: "s-head" }, el("span", { class: "s-kind", text: KIND_LABEL[r.k] || "문서" }), el("span", { class: "s-title", text: r.st })),
          el("span", { class: "s-where", text: r.pt }),
          el("span", { class: "s-snip", html: snippet(r.t, q) })
        );
        pop.append(a);
      }
      sel = results.length ? 0 : -1;
      pop.hidden = false;
      mark();
    };

    input.addEventListener("focus", () => {
      loadSearch();
      if (input.value.trim()) render();
    });
    input.addEventListener("input", () => {
      clearTimeout(timer);
      timer = setTimeout(render, 80);
    });
    input.addEventListener("keydown", (e) => {
      const links = pop.querySelectorAll("a");
      if (e.key === "ArrowDown" && links.length) {
        e.preventDefault();
        sel = (sel + 1) % links.length;
        mark();
      } else if (e.key === "ArrowUp" && links.length) {
        e.preventDefault();
        sel = (sel - 1 + links.length) % links.length;
        mark();
      } else if (e.key === "Enter" && sel >= 0 && links[sel]) {
        e.preventDefault();
        const href = links[sel].getAttribute("href");
        close();
        input.blur();
        location.href = href;
      } else if (e.key === "Escape") {
        if (!pop.hidden) {
          e.stopPropagation();
          close();
        } else input.blur();
      }
    });
    pop.addEventListener("click", (e) => {
      if (e.target.closest("a")) close();
    });
    document.addEventListener("click", (e) => {
      if (!wrap.contains(e.target)) close();
    });
    document.addEventListener("keydown", (e) => {
      const t = e.target;
      const typing = t && t.closest && t.closest("input, textarea, select, [contenteditable]");
      if ((e.key === "/" && !typing && !e.ctrlKey && !e.metaKey && !e.altKey) || ((e.ctrlKey || e.metaKey) && (e.key === "k" || e.key === "K"))) {
        e.preventDefault();
        input.focus();
        input.select();
      }
    });
    return wrap;
  }

  /* ---------- 설명서 목차 (왼쪽) ---------- */

  function sectionList() {
    return Array.from(document.querySelectorAll(".doc-body > section.sec[id]"));
  }

  function sectionTitle(s) {
    const h = s.querySelector(".sec-head h2, h2");
    return s.dataset.toc || (h ? h.textContent.trim() : s.id);
  }

  function fillNav(nav) {
    const page = currentPage();
    nav.textContent = "";
    for (const sec of DOCS.sections) {
      const box = el("div", { class: "nav-sec" }, el("div", { class: "nav-sec-title", text: sec.name }));
      const ul = el("ul");
      for (const p of sec.pages) {
        const cur = p.id === page.id;
        const li = el("li", null, el("a", { href: p.file, "aria-current": cur ? "page" : null, text: p.title }));
        if (cur) {
          const secs = sectionList();
          if (secs.length) {
            const sub = el("ul", { class: "page-toc", "aria-label": "이 문서의 차례" });
            for (const s of secs) sub.append(el("li", null, el("a", { href: "#" + s.id, "data-sec": s.id, text: sectionTitle(s) })));
            li.append(sub);
          }
        }
        ul.append(li);
      }
      box.append(ul);
      nav.append(box);
    }
  }

  let currentSection = null;
  function markSection(id) {
    currentSection = id;
    document.querySelectorAll("a[data-sec]").forEach((a) => a.classList.toggle("current", a.dataset.sec === id));
    const nav = document.querySelector("nav.sitenav:not(.in-drawer)");
    const a = nav && nav.querySelector("a[data-sec].current");
    if (a && nav.scrollHeight > nav.clientHeight) {
      const r = a.getBoundingClientRect();
      const nr = nav.getBoundingClientRect();
      if (r.top < nr.top + 32 || r.bottom > nr.bottom - 32) nav.scrollTop += r.top - nr.top - nav.clientHeight / 3;
    }
  }

  function setupSitenav() {
    const nav = document.querySelector("nav.sitenav");
    if (nav) fillNav(nav);
    const secs = sectionList();
    if (!secs.length || !("IntersectionObserver" in window)) return;
    const visible = new Map();
    const io = new IntersectionObserver(
      (entries) => {
        for (const en of entries) visible.set(en.target.id, en.isIntersecting);
        const cur = secs.find((s) => visible.get(s.id));
        if (cur) markSection(cur.id);
      },
      { rootMargin: "-20% 0px -70% 0px" }
    );
    secs.forEach((s) => io.observe(s));
  }

  function openNavDrawer() {
    const drawer = el("div", { class: "nav-drawer", role: "dialog", "aria-modal": "true", "aria-label": "설명서 목차" });
    const close = el("button", { class: "cap-btn", type: "button", title: "닫기", "aria-label": "닫기" }, icon("EA76"));
    const nav = el("nav", { class: "sitenav in-drawer", "aria-label": "설명서 목차" });
    fillNav(nav);
    if (currentSection) nav.querySelectorAll("a[data-sec]").forEach((a) => a.classList.toggle("current", a.dataset.sec === currentSection));
    const sheet = el("div", { class: "sheet" }, el("div", { class: "sheet-head" }, el("span", { text: DOCS.title }), close), nav);
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
    const cur = nav.querySelector("a[data-sec].current") || nav.querySelector('a[aria-current="page"]');
    if (cur) cur.scrollIntoView({ block: "center" });
    close.focus();
  }

  function setupCrumbs() {
    const c = document.querySelector(".crumbs[data-auto]");
    if (!c) return;
    const page = currentPage();
    c.textContent = "";
    c.append(el("a", { href: "index.html", text: DOCS.title }), el("span", { class: "sep", text: "/" }), el("span", { text: page.section.name }));
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
    // tools/check_samples.py가 공개 헤더에 대고 컴파일해 본 예제다
    if (box.dataset.compile) tools.append(el("span", { class: "compiled", title: "tools/check_samples.py가 기준 커밋의 공개 헤더에 대고 컴파일해 확인한 예제 (-fsyntax-only)" }, icon("EAB2"), el("span", { text: "컴파일 확인" })));
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

  /* ---------- 실습 창 틀 ---------- */

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

  /* ---------- 앞뒤 문서 ---------- */

  function setupPager() {
    const pager = document.querySelector(".pager[data-auto]");
    if (!pager) return;
    const page = currentPage();
    const i = ALL_PAGES.findIndex((p) => p.id === page.id);
    const prev = ALL_PAGES[i - 1];
    const next = ALL_PAGES[i + 1];
    pager.textContent = "";
    pager.append(
      prev ? el("a", { class: "prev", href: prev.file }, el("span", { class: "dir" }, icon("EA9B"), "이전 문서"), el("span", { class: "ttl", text: prev.title })) : el("span"),
      next ? el("a", { class: "next", href: next.file }, el("span", { class: "dir" }, "다음 문서", icon("EA9C")), el("span", { class: "ttl", text: next.title })) : el("span")
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
        el("div", null, "luil 설명서 — C++20 UI 프레임워크 luil의 사용 설명서. 코드 발췌와 줄 번호는 ", el("a", { href: DOCS.repo + "/tree/" + DOCS.commit, target: "_blank", rel: "noopener", text: "main " + DOCS.commitShort }), " (" + DOCS.commitDate + ") 기준입니다."),
        el("div", null, "luil 소스와 문서는 MIT 라이선스입니다. 아이콘은 Microsoft의 Codicons(CC BY 4.0)를 이 설명서에 쓰는 글리프만 남겨 실었습니다.")
      ),
      el("div", null, el("a", { href: DOCS.repo, target: "_blank", rel: "noopener", text: "github.com/yuldK/luil" }))
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
    setupCrumbs();
    setupSitenav();
    document.querySelectorAll(".walk").forEach(setupWalk);
    document.querySelectorAll(".code").forEach(renderCode);
    setupTabs();
    setupTerms();
    setupWindows();
    setupPager();
    setupFooter();
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") hideTerm();
    });
  }

  window.LU = { DOCS, COURSE, PAGES: ALL_PAGES, apiSlugs, el, svg, icon, cssVar, onTheme, reduced, store, sourceUrl, highlightLines, renderCode, escapeHtml, pad2, flow };

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();

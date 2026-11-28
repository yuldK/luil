/* luil 설명서 — 알림 실습 (토스트 쌓음)
 *
 * 판정은 assets/toastmodel.js(원본 C++ 줄을 잘라 컴파일한 대조기와 견준 이식)를 부른다.
 *  - 라이브러리 쪽(UI thread): toast_element::opacity_at로 그리고, next_update가 예고한 시각에만 다시 그린다.
 *    흐려지기 시작할 때까지 쉬고(timer 하나), 흐려지는 동안 매 frame, 다 흐려지면 멈춘다.
 *  - 앱 쪽(logic thread): 위젯 예제처럼 목록을 들고 next_tick에 가장 이른 만료 시각을 답하고 tick에서 지운다.
 *    실행 취소의 두 메시지(toast_undo_intent, toast_request_intent)는 데스크톱 데모의 것이다.
 *  - 다 흐려진 토스트도 지우기 전까지 tree에 남아 몸이 누름을 흡수한다 (hit_opaque). 점선은 그 자리를 보이는 표시다.
 * 그림은 luil의 실제 렌더링이 아니라 같은 치수(높이 40, 폭 320, 단추 64×24, 논리 픽셀)로 그린 재현이다.
 */
(function () {
  "use strict";
  const T = window.LuilToasts;
  const { el, icon } = window.LU;

  const STAGE_H = 260; // 창 높이 (논리 픽셀)
  const GLYPH = { info: "EA74", success: "EBA4", warning: "EA6C", error: "EA87" };
  const WORD = { info: "정보", success: "성공", warning: "경고", error: "오류" };
  const TEXT = { info: "새 메시지가 3개 왔습니다.", success: "저장했습니다.", warning: "디스크 공간이 얼마 남지 않았습니다.", error: "보내지 못했습니다. 연결을 확인하세요." };
  const UNDO_DONE_MS = 3000; // toast_request_intent::duration 기본값

  function setup(root) {
    const q = (k) => root.querySelector(`[data-ts="${k}"]`);
    const pushRow = q("push-row");
    const durationSel = q("duration");
    const cornerSel = q("corner");
    const maxSel = q("max");
    const widthIn = q("width");
    const widthOut = q("width-out");
    const pruneCb = q("prune");
    const ghostCb = q("ghost");
    const outlineCb = q("outline");
    const wrap = q("wrap");
    const stage = q("stage");
    const stateBox = q("state");
    const logBox = q("log");

    for (const sev of ["info", "success", "warning", "error"]) {
      const b = el("button", { type: "button", class: "dl-btn", dataset: { sev } }, icon(GLYPH[sev], "sev-" + sev), WORD[sev]);
      b.addEventListener("click", () => pushTimed(sev));
      pushRow.append(b);
    }

    // 창 재현: 뒤 화면, 그 위에 쌓음 층 (쌓음 자체는 누름을 흡수하지 않는다 — 토스트 한 장의 몸만 흡수한다)
    const under = el("button", { type: "button", class: "dl-btn ts-under", tabindex: "-1", text: "뒤 화면 단추" });
    const behind = el(
      "div",
      { class: "ts-behind" },
      el("div", { class: "bar", text: "편집기" }),
      ["보고서 초안.md", "회의록 11월.md", "예산표.csv", "발표 자료.pptx", "그림 모음"].map((t) => el("div", { class: "row", text: t }))
    );
    const layer = el("div", { class: "ts-layer" });
    stage.append(behind, under, layer);

    let S; // 앱 상태 (logic thread)
    let W = 560;
    let vs = 1;
    const nodes = new Map(); // id → 토스트 element (UI thread가 지은 것)
    let raf = 0;
    let redrawTimer = 0;
    let tickTimer = 0;

    const now = () => performance.now();
    const t = (ms) => `${((ms - S.t0) / 1000).toFixed(1)}s`;
    const cfg = () => Object.assign({}, T.DEFAULTS, { corner: cornerSel.value, maximum_count: Number(maxSel.value) });
    const log = (input, messages, effect) => {
      const entry = el("div", { class: "dl-entry" });
      entry.append(el("div", { class: "in", text: input }));
      for (const m of messages) entry.append(el("div", { class: "msg", text: m }));
      if (effect) entry.append(el("div", { class: "eff", text: effect }));
      logBox.prepend(entry);
      while (logBox.childElementCount > 40) logBox.lastElementChild.remove();
    };

    function fit() {
      const avail = Math.max(120, wrap.clientWidth - 4);
      vs = Math.min(1, avail / W);
      stage.style.width = W + "px";
      stage.style.height = STAGE_H + "px";
      stage.style.left = Math.max(2, (wrap.clientWidth - W * vs) / 2) + "px";
      stage.style.transform = vs < 1 ? `scale(${vs})` : "";
      wrap.style.height = STAGE_H * vs + 4 + "px";
    }

    // ---- UI thread: 지금 tree를 그린다. 다시 그리기는 next_update가 정한다.
    function makeNode(toast) {
      const body = el(
        "div",
        { class: "ts-body" },
        el("span", { class: "g" }, icon(GLYPH[toast.severity], "sev-" + toast.severity)),
        el("span", { class: "t", style: { right: (toast.action ? T.ACTION_WIDTH + 16 : 10) + "px" } }, el("span", { text: toast.text }))
      );
      if (toast.action) {
        // 동작 단추는 Tab 자리가 아니다 (set_tab_stop(false)) — 재현도 그렇다
        const a = el("button", { type: "button", class: "a", tabindex: "-1", text: "실행 취소" });
        a.addEventListener("click", (e) => {
          e.stopPropagation();
          pressAction(toast.id, "토스트의 '실행 취소'를 눌렀다");
        });
        body.append(a);
      }
      const node = el("div", { class: "ts-toast", dataset: { id: toast.id } }, body);
      node.addEventListener("click", () => {
        const live = S.toasts.find((x) => x.id === toast.id);
        const gone = live && T.opacityAt(live, now(), 1) <= 0;
        log(`${toast.id}의 몸을 눌렀다`, [], gone ? "다 흐려져 보이지 않지만 tree에 남아 몸이 누름을 흡수한다 — 뒤 화면에 닿지 않는다" : "몸은 누름을 흡수만 한다 (메시지 없음)");
      });
      return node;
    }

    function built() {
      return S.toasts.slice(0, T.builtCount(cfg(), S.toasts.length));
    }

    function draw() {
      raf = 0;
      redrawTimer = 0;
      const at = now();
      S.frames++;
      S.lastDraw = at;
      const list = built();
      const rects = T.stackRects(cfg(), list.length, { x: 0, y: 0, width: W, height: STAGE_H }, 1);
      const keep = new Set();
      list.forEach((toast, i) => {
        keep.add(toast.id);
        let node = nodes.get(toast.id);
        if (!node) {
          node = makeNode(toast);
          nodes.set(toast.id, node);
          layer.append(node);
        }
        const r = rects[i];
        Object.assign(node.style, { left: r.x + "px", top: r.y + "px", width: r.width + "px", height: r.height + "px" });
        const opacity = T.opacityAt(toast, at, 1);
        // 다 흐려지면 그리지 않는다(draw가 돌아간다). 그래도 몸과 단추는 tree에 남아 누름을 받는다 — 불투명도 0으로만 숨긴다
        node.firstChild.style.opacity = String(opacity);
        node.classList.toggle("gone", opacity <= 0);
        node.classList.toggle("show-ghost", outlineCb.checked);
      });
      for (const [id, node] of nodes) {
        if (!keep.has(id)) {
          node.remove();
          nodes.delete(id);
        }
      }
      // 뒤 화면 단추는 첫 장의 자리 밑에 둔다
      const first = T.stackRects(cfg(), 1, { x: 0, y: 0, width: W, height: STAGE_H }, 1)[0];
      Object.assign(under.style, { left: Math.max(4, first.x + first.width - 150) + "px", top: first.y + 7 + "px" });
      schedule(at, list);
      drawState(at, list);
    }

    // next_update: 흐려지기 시작할 때까지 쉬고, 흐려지는 동안은 "지금"(매 frame), 끝나면 예고가 없다
    function schedule(at, list) {
      if (raf) cancelAnimationFrame(raf);
      if (redrawTimer) clearTimeout(redrawTimer);
      raf = 0;
      redrawTimer = 0;
      let next = null;
      let who = null;
      for (const toast of list) {
        const n = T.nextUpdate(toast, at, 1);
        if (n !== null && (next === null || n < next)) {
          next = n;
          who = toast;
        }
      }
      S.nextDraw = next === null ? null : { at: next, id: who.id, fading: next <= at };
      if (next === null) return;
      if (next <= at) raf = requestAnimationFrame(draw);
      else redrawTimer = setTimeout(draw, next - at);
    }

    // ---- logic thread: 앱의 next_tick·tick
    function scheduleTick() {
      if (tickTimer) clearTimeout(tickTimer);
      tickTimer = 0;
      S.nextTick = null;
      if (!pruneCb.checked) return;
      for (const toast of S.toasts) {
        if (toast.duration <= 0) continue;
        const at = toast.shown_at + toast.duration;
        if (S.nextTick === null || at < S.nextTick.at) S.nextTick = { at, id: toast.id };
      }
      if (S.nextTick) tickTimer = setTimeout(tick, Math.max(0, S.nextTick.at - now()));
    }
    function tick() {
      tickTimer = 0;
      const at = now();
      const expired = (x) => x.duration > 0 && at >= x.shown_at + x.duration;
      const gone = S.toasts.filter(expired).map((x) => x.id);
      S.toasts = S.toasts.filter((x) => !expired(x));
      if (gone.length) log(`tick(now) — t = ${t(at)}`, [], `앱이 시간이 다 된 것을 목록에서 지운다: ${gone.join(", ")}. frame이 다시 게시된다`);
      frame();
    }
    // 앱 상태가 바뀌었다 → 새 tree를 게시한다 → UI thread가 곧바로 그린다
    function frame() {
      scheduleTick();
      draw();
    }

    function push(text, severity, duration, action) {
      const id = `toast-${++S.next}`;
      S.toasts.push({ id, text, severity, shown_at: now(), duration, action });
      return id;
    }
    function placeNote(id) {
      const index = S.toasts.findIndex((x) => x.id === id);
      const max = cfg().maximum_count;
      return index >= max ? ` — 앞의 ${max}장이 maximum_count를 채워 아직 짓지 않는다. 그동안에도 시계는 간다` : "";
    }
    function pushTimed(sev) {
      const duration = Number(durationSel.value);
      const action = ghostCb.checked;
      const id = push(TEXT[sev], sev, duration, action);
      frame();
      const ms = duration > 0 ? `${duration}ms` : "0ms";
      const extra = duration > 0 ? `흐려지기 시작 t = ${t(S.toasts[S.toasts.length - 1].shown_at + duration - T.FADE_MS)}` : "계속 남는다";
      log(`${WORD[sev]} 토스트 올리기`, [`toast_request_intent { u8"${TEXT[sev]}", toast_severity::${sev}, ${ms} }`], `앱: 목록 끝에 ${id} (shown_at = t ${t(S.toasts[S.toasts.length - 1].shown_at)}). ${extra}${action && duration > 0 ? " · 동작 단추를 달았다 (데모가 피하는 조합)" : ""}${placeNote(id)}`);
    }
    function pushSticky() {
      const id = push("항목을 지웠습니다.", "info", 0, true);
      frame();
      log("실행 취소 토스트 올리기", ['toast_request_intent { u8"항목을 지웠습니다.", toast_severity::info, 0ms }'], `앱: 목록 끝에 ${id}. duration 0이라 흐려지지 않고 다시 그리기 예고도 없다${placeNote(id)}`);
    }
    function pressAction(id, input) {
      const toast = S.toasts.find((x) => x.id === id);
      if (!toast) return;
      const invisible = T.opacityAt(toast, now(), 1) <= 0;
      S.toasts = S.toasts.filter((x) => x.id !== id);
      const added = push("되돌렸습니다.", "success", UNDO_DONE_MS, false);
      frame();
      log(input + (invisible ? " (보이지 않는 단추)" : ""), [`toast_undo_intent { u8"${id}" }`, 'toast_request_intent { u8"되돌렸습니다.", toast_severity::success }'], `한 누름이 메시지 둘을 낸다. 앱: ${id} 지움, ${added} 더함${invisible ? ". 다 흐려진 토스트의 단추도 지우기 전까지 눌린다" : ""}`);
    }

    function drawState(at, list) {
      stateBox.textContent = "";
      const kv = (k, ...v) => stateBox.append(el("dt", { text: k }), el("dd", null, ...v));
      const code = (text, cls) => el("code", { class: cls || null, text });
      kv("마지막으로 그린 때", code(`t = ${t(S.lastDraw)} · 그린 횟수 ${S.frames}`));
      let nd = "없음 — 다시 그릴 것이 없다";
      if (S.nextDraw) nd = S.nextDraw.fading ? `지금 — 흐려지는 중 (${S.nextDraw.id}), 매 frame` : `t = ${t(S.nextDraw.at)} — ${S.nextDraw.id} 흐려짐 시작. 그때까지 그리지 않는다`;
      kv("next_update", code(nd));
      kv("next_tick", code(!pruneCb.checked ? "끔 — 앱이 지우지 않는다" : S.nextTick ? `t = ${t(S.nextTick.at)} — ${S.nextTick.id}의 만료` : "없음 — 시간 토스트가 없다"));
      if (!S.toasts.length) kv("목록 (앱 상태)", code("비었다 — 쌓음을 짓지 않는다"));
      else {
        const items = S.toasts.map((x, i) => {
          const isBuilt = i < list.length;
          const op = T.opacityAt(x, at, 1);
          const dur = x.duration > 0 ? `${(x.duration / 1000).toFixed(1)}s` : "계속";
          let tail;
          if (!isBuilt) tail = "짓지 않음 (maximum_count 밖)";
          else if (op <= 0) tail = "다 흐려짐 — tree에 남아 있다";
          else tail = `불투명도 ${op.toFixed(2)}`;
          return code(`${x.id} · ${WORD[x.severity]} · ${dur}${x.action ? " · 동작" : ""} · ${tail}`, "item" + (isBuilt && op > 0 ? "" : " off"));
        });
        kv("목록 (앱 상태)", ...items);
      }
      kv("쌓음", code(`${cfg().corner} · ${list.length}/${S.toasts.length}장 지음 · 자리 폭 ${W}`));
    }

    q("sticky").addEventListener("click", pushSticky);
    q("press-action").addEventListener("click", () => {
      const target = built().find((x) => x.action);
      if (target) pressAction(target.id, "재현 도구로 맨 앞 동작을 눌렀다");
      else log("맨 앞 동작 누르기", [], "동작 단추가 있는 토스트가 지어져 있지 않다");
    });
    under.addEventListener("click", () => log("뒤 화면 단추", [], "토스트가 덮지 않은 자리라 뒤 화면이 받았다"));
    cornerSel.addEventListener("change", () => {
      draw();
      log(`corner = toast_corner::${cornerSel.value}`, [], cornerSel.value.startsWith("top") ? "위 모서리는 첫 장이 가장 위다" : "아래 모서리는 첫 장이 가장 아래(모서리 쪽)다");
    });
    maxSel.addEventListener("change", () => {
      draw();
      log(`maximum_count = ${maxSel.value}`, [], "목록의 앞에서부터 이만큼만 element가 된다");
    });
    widthIn.addEventListener("input", () => {
      W = Number(widthIn.value);
      widthOut.textContent = String(W);
      fit();
      draw();
    });
    widthIn.addEventListener("change", () => {
      log(`자리 폭 ${W}`, [], W < 352 ? `폭 320 + 여백 16은 자리로 줄지 않는다. ${W}에서는 반대편 가장자리를 넘는다` : "쌓음은 자리 폭과 무관하게 320이다");
    });
    pruneCb.addEventListener("change", () => {
      log(pruneCb.checked ? "앱이 만료를 지운다" : "앱이 만료를 지우지 않는다", [], pruneCb.checked ? "next_tick이 가장 이른 만료 시각을 답한다. 이미 지난 것은 곧바로 tick이 지운다" : "next_tick이 비었다. 다 흐려진 토스트가 목록과 tree에 남는다");
      frame();
    });
    ghostCb.addEventListener("change", () => log(ghostCb.checked ? "시간 토스트에도 동작을 단다" : "동작은 계속 남는 토스트에만", [], ghostCb.checked ? "다음에 올리는 시간 토스트부터. 흐려진 뒤에도 지우기 전까지 단추가 눌린다" : "데모의 규칙이다"));
    outlineCb.addEventListener("change", () => draw());

    function reset() {
      if (raf) cancelAnimationFrame(raf);
      if (redrawTimer) clearTimeout(redrawTimer);
      if (tickTimer) clearTimeout(tickTimer);
      raf = redrawTimer = tickTimer = 0;
      for (const node of nodes.values()) node.remove();
      nodes.clear();
      S = { toasts: [], next: 0, t0: now(), frames: 0, lastDraw: now(), nextDraw: null, nextTick: null };
      durationSel.value = "3000";
      cornerSel.value = "bottom_right";
      maxSel.value = "4";
      pruneCb.checked = true;
      ghostCb.checked = false;
      outlineCb.checked = true;
      // 처음 자리 폭은 보이는 폭에 맞춘다 (360~560). 좁은 화면에서 그림이 지나치게 줄지 않게 한다
      W = Math.min(560, Math.max(360, Math.round((wrap.clientWidth - 4) / 10) * 10 || 560));
      widthIn.value = String(W);
      widthOut.textContent = String(W);
      logBox.textContent = "";
      fit();
      frame();
      log("처음 상태", [], "심각도 단추로 토스트를 올려 본다. 상태판의 '마지막으로 그린 때'가 흐려질 때만 움직이는 것을 본다. 앱의 지우기를 끄면 다 흐려진 토스트가 남아 뒤 화면 단추를 가린다.");
    }

    let seenWidth = -1;
    const refit = () => {
      if (wrap.clientWidth === seenWidth) return;
      seenWidth = wrap.clientWidth;
      requestAnimationFrame(fit);
    };
    if (typeof ResizeObserver === "function") new ResizeObserver(refit).observe(wrap);
    else window.addEventListener("resize", refit);
    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilNotificationsLab = { setup };
})();

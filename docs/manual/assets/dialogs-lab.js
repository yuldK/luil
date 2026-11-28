/* luil 설명서 — 대화 상자와 시트 실습
 *
 * 판정은 전부 assets/dialogmodel.js(원본 C++ 줄을 잘라 컴파일한 대조기와 견준 이식)를 부른다.
 *  아래 시트: 앱 상태(열림·detent·drag_offset)를 들고, 손잡이 끌기의 이동을 drag(Δ)로, 놓기를 release_action으로 바꾼다.
 *   - 이동마다 앱이 메시지를 곧바로 반영해 다시 짓는다고 둔다. 실제로는 놓는 순간 input thread가 가진 tree의
 *     drag_offset으로 정한다 (sheet_host_element::arrange가 설정을 붙잡는다).
 *   - 놓은 속도는 motion_track(놓기 전 100ms의 이동 표본)을 그대로 쓴다. 누른 자리와 뗀 자리는 표본이 아니다.
 *   - 시트 몸 끌기(터치의 넘겨받기)와 시트 안 목록은 옮기지 않았다.
 *  확인 대화 상자: 기본 버튼·첫 초점·Esc의 단추와 쌓기 판정은 옮긴 함수이고, 키 차례(초점 실행 → Tab → 가둠의 Esc →
 *   기본 버튼)는 ui_interaction.cpp를 읽어 옮겼다. 단추 글 폭은 브라우저가 잰 근사다 (luil은 플랫폼 글꼴로 잰다).
 * 그림은 luil의 실제 렌더링이 아니라 같은 치수(논리 픽셀)로 그린 재현이다.
 */
(function () {
  "use strict";
  const D = window.LuilDialogs;
  const { el, icon } = window.LU;
  const f = Math.fround;

  // 화면에 적는 수 — 한 자리 소수, -0은 0
  const n1 = (v) => (Math.abs(v) < 0.05 ? 0 : v).toFixed(1);
  const lit = (v) => n1(v) + "f";
  const speed = (v) => (v === 0 ? "0 (표본이 모자라거나 취소)" : `${v > 0 ? "+" : "−"}${Math.round(Math.abs(v))}/s ${v > 0 ? "아래로" : "위로"}`);

  function makeLog(box) {
    return function log(input, messages, effect) {
      const entry = el("div", { class: "dl-entry" });
      entry.append(el("div", { class: "in", text: input }));
      for (const m of messages) entry.append(el("div", { class: "msg", text: m }));
      if (effect) entry.append(el("div", { class: "eff", text: effect }));
      box.prepend(entry);
      while (box.childElementCount > 40) box.lastElementChild.remove();
      return entry;
    };
  }

  // ---------------------------------------------------------------- 아래 시트
  const SHEET_H = 480; // 자리 높이 (논리 픽셀)
  const ADAPT_WIDTH = 600; // side_from_width 예 (창 폭 등급 medium의 시작)
  const FLING_TEST = 1800; // 튕겨 놓기 단추가 주는 속도

  function setupSheet(root) {
    const q = (k) => root.querySelector(`[data-sh="${k}"]`);
    const kindSel = q("kind");
    const modalCb = q("modal");
    const adaptCb = q("adaptive");
    const widthIn = q("width");
    const widthOut = q("width-out");
    const cb = { drag: q("cb-drag"), settle: q("cb-settle"), dismiss: q("cb-dismiss"), release: q("cb-release") };
    const wrap = q("wrap");
    const screen = q("screen");
    const stateBox = q("state");
    const log = makeLog(q("log"));

    // 화면 재현은 한 번 짓고 frame마다 자리만 고친다 — 끄는 동안 손잡이 element가 바뀌면 포인터 잡기를 잃는다.
    const openBehind = el("button", { type: "button", class: "dl-btn", text: "거르개 열기" });
    const behind = el(
      "div",
      { class: "sh-behind" },
      el("div", { class: "bar", text: "받은 편지함" }),
      ["회의 일정이 바뀌었다", "빌드 결과: 통과", "설계 문서 검토 요청", "주간 보고", "새 댓글 3개", "배포 안내"].map((t) => el("div", { class: "row", text: t })),
      openBehind
    );
    const scrim = el("div", { class: "sh-scrim" });
    const handle = el("div", { class: "sh-handle", title: "끌기 손잡이" }, el("i"));
    const body = el(
      "div",
      { class: "sh-body" },
      el("div", { class: "ttl", text: "거르개" }),
      ["읽지 않음", "별표", "첨부 있음", "오늘 받은 것", "이번 주", "보낸 사람: 팀", "1MB 이상", "보관함 제외", "라벨: 설계", "라벨: 빌드", "라벨: 회의", "스팸 제외", "답장한 것"].map((t) => el("div", { class: "row", text: t }))
    );
    const sheet = el("div", { class: "sh-sheet bottom" }, handle, body);
    const guides = el("div", { class: "sh-guides" });
    screen.append(behind, scrim, sheet, guides);

    let S; // 앱 상태
    let W = 390; // 자리 폭 (논리 픽셀)
    let vs = 1; // 그림을 줄인 비율 (논리 픽셀 → CSS 픽셀)
    let drag = null; // 끄는 중인 손잡이
    let lastRelease = null;

    function config() {
      return Object.assign({}, D.DEFAULTS, {
        kind: kindSel.value,
        side_from_width: adaptCb.checked ? ADAPT_WIDTH : null,
        modal: modalCb.checked,
        detent: S.detent,
        drag_offset: S.drag_offset,
        drag: cb.drag.checked,
        settle: cb.settle.checked,
        dismiss: cb.dismiss.checked,
        release: cb.release.checked,
      });
    }

    function fit() {
      const avail = Math.max(120, wrap.clientWidth - 4);
      vs = Math.min(1, avail / W);
      screen.style.width = W + "px";
      screen.style.height = SHEET_H + "px";
      screen.style.left = Math.max(2, (wrap.clientWidth - W * vs) / 2) + "px";
      screen.style.transform = vs < 1 ? `scale(${vs})` : "";
      wrap.style.height = SHEET_H * vs + 4 + "px";
    }

    // 앱의 make_frame — 열렸으면 host를 그 frame의 설정으로 짓는다
    function draw() {
      const c = config();
      const kind = D.resolveSheetKind(c, W);
      const b = D.sheetSurfaceBounds(c, { x: 0, y: 0, width: W, height: SHEET_H }, 1);
      scrim.hidden = !(S.open && c.modal);
      sheet.hidden = !S.open;
      sheet.className = "sh-sheet " + kind;
      Object.assign(sheet.style, { left: b.x + "px", top: b.y + "px", width: b.width + "px", height: b.height + "px" });
      handle.hidden = kind !== "bottom"; // 옆 시트·서랍에는 손잡이가 없다
      handle.classList.toggle("inert", !c.drag);
      guides.textContent = "";
      if (S.open && kind === "bottom") {
        const half = D.sheetDetentHeight(c, "half", SHEET_H);
        const full = D.sheetDetentHeight(c, "full", SHEET_H);
        const line = (h, cls, text) => guides.append(el("div", { class: "sh-guide" + cls, style: { top: SHEET_H - h + "px" } }, el("span", { text })));
        line(full, "", `full ${n1(full)}`);
        line(half, "", `half ${n1(half)}`);
        line((half + full) / 2, " edge", `half | full ${n1((half + full) / 2)}`);
        line(half / 2, " edge close", `닫힘 | half ${n1(half / 2)}`);
      }
      drawState(c, kind, b);
    }

    function drawState(c, kind, b) {
      stateBox.textContent = "";
      const kv = (k, v) => stateBox.append(el("dt", { text: k }), el("dd", null, el("code", { text: v })));
      kv("열림", S.open ? "참 — host를 tree에 담는다" : "거짓 — 짓지 않는다");
      kv("kind", c.kind === kind ? kind : `${c.kind} → ${kind} (폭 ${W} ≥ ${ADAPT_WIDTH})`);
      kv("detent · drag_offset", `${S.detent} · ${lit(S.drag_offset)}`);
      if (kind === "bottom") {
        const half = D.sheetDetentHeight(c, "half", SHEET_H);
        const full = D.sheetDetentHeight(c, "full", SHEET_H);
        kv("보이는 높이", `${n1(D.sheetVisibleHeight(c, SHEET_H))} (half ${n1(half)} · full ${n1(full)})`);
        kv("표면 폭", `${n1(b.width)} (min(폭, 640), 가운데)`);
      } else kv("표면 폭", `${n1(b.width)} (360, 자리 − 56까지)`);
      kv("자리", `${W} × ${SHEET_H} 논리 픽셀`);
      kv("마지막 놓기", lastRelease ? `속도 ${speed(lastRelease.v)}${lastRelease.samples != null ? ` · 100ms 안 표본 ${lastRelease.samples}개` : ""}` : "—");
      kv("scrim", c.modal ? "있음 — 진하기 0.32, 뒤를 막는다" : "없음 — 비modal, 뒤 화면을 쓴다");
    }

    function local(e) {
      const r = screen.getBoundingClientRect();
      return { x: (e.clientX - r.left) / vs, y: (e.clientY - r.top) / vs };
    }

    function openSheet(input) {
      if (S.open) return false;
      S.open = true;
      S.detent = "half";
      S.drag_offset = 0;
      draw();
      log(input, [], "앱이 열림을 켠다(detent = half) → 다음 frame부터 host를 짓는다");
      return true;
    }
    function closeSheet() {
      S.open = false;
      S.drag_offset = 0;
      draw();
    }

    // 놓기 — release_action이 고른 메시지 하나를 앱이 받는다
    function release(v, how, samples) {
      const c = config();
      const visible = D.sheetVisibleHeight(c, SHEET_H);
      const settle = D.settleSheet(c, SHEET_H, v);
      const act = D.releaseAction(c, SHEET_H, v);
      const fling = Math.abs(v) >= c.fling_velocity;
      lastRelease = { v, samples };
      const input = `${how} — 보이는 높이 ${n1(visible)}, 속도 ${speed(v)}`;
      const why = fling ? "|속도| ≥ 300이라 그 방향의 다음 자리다" : "느린 놓기라 가장 가까운 자리다";
      if (!act) {
        draw();
        log(input, [], `settle도 release도 비어 메시지가 없다. 앱의 drag_offset(${lit(S.drag_offset)})이 끈 자리에 남는다`);
        return;
      }
      if (act.kind === "dismiss") {
        closeSheet();
        log(input, ["dismiss(sheet_dismiss_reason::drag)"], `${why} — 닫힘. 앱이 시트를 뺀다`);
        return;
      }
      if (act.kind === "settle") {
        S.detent = act.detent;
        S.drag_offset = 0;
        draw();
        const note = settle.dismiss ? `${why} — 닫힘이지만 dismiss가 비어 half로 돌아온다` : why;
        log(input, [`settle(sheet_detent::${act.detent})`], `${note}. 앱: detent = ${act.detent}, drag_offset = 0 — 그 frame에 바로 그 자리에 선다`);
        return;
      }
      const r = act.release;
      const msg = `release({ .settle = { sheet_detent::${r.settle.detent}, ${r.settle.dismiss} }, .visible_height = ${lit(r.visible_height)}, .target_height = ${lit(r.target_height)}, .velocity = ${lit(r.velocity)} })`;
      if (r.settle.dismiss) {
        closeSheet();
        log(input, [msg], `${why} — 닫는 쪽이다. 닫을지는 앱이 정한다. 이 앱은 바로 닫는다`);
      } else {
        S.detent = r.settle.detent;
        S.drag_offset = 0;
        draw();
        log(input, [msg], `${why}. 이 앱은 스프링 없이 detent = ${r.settle.detent}, drag_offset = 0으로 바로 멈춘다. velocity는 높아지는 쪽이 양수다`);
      }
    }

    // 손잡이 끌기 — 누르는 즉시 끌기다 (start_distance 0). 마우스·펜·터치가 같다.
    handle.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 || drag) return;
      e.preventDefault();
      screen.focus({ preventScroll: true });
      const c = config();
      if (D.resolveSheetKind(c, W) !== "bottom") return;
      if (!c.drag) {
        log("손잡이를 눌렀다", [], "drag가 비어 끌기 대상이 서지 않는다. 손잡이는 그려지지만 누름은 시트 표면이 흡수한다");
        return;
      }
      try {
        handle.setPointerCapture(e.pointerId);
      } catch (err) {
        /* 잡기를 못 해도 이동은 받는다 */
      }
      drag = { id: e.pointerId, lastY: local(e).y, track: new D.MotionTrack(1), moves: 0, sum: 0, first: null, last: null, entry: null };
      handle.classList.add("dragging");
    });
    handle.addEventListener("pointermove", (e) => {
      if (!drag || e.pointerId !== drag.id) return;
      const list = typeof e.getCoalescedEvents === "function" ? e.getCoalescedEvents() : [];
      for (const ev of list.length ? list : [e]) {
        const p = local(ev);
        drag.track.add(ev.timeStamp, p.x, p.y); // 이동마다 표본 하나 (on_move보다 먼저)
        const delta = f(p.y - drag.lastY); // (current.y - previous.y) / scale
        drag.lastY = p.y;
        if (delta === 0) continue; // 0이면 내지 않는다
        S.drag_offset = f(S.drag_offset + delta); // 앱: drag_offset에 더한다
        drag.moves++;
        drag.sum += delta;
        if (drag.first === null) drag.first = delta;
        drag.last = delta;
      }
      if (!drag.moves) return;
      draw();
      const text = drag.moves === 1 ? `drag(${lit(drag.first)})` : `drag(${lit(drag.first)}) … drag(${lit(drag.last)})  — ${drag.moves}번, 합 ${n1(drag.sum)}`;
      if (!drag.entry) drag.entry = log("손잡이를 끈다", [text], "");
      else drag.entry.querySelector(".msg").textContent = text;
      const eff = drag.entry.querySelector(".eff") || drag.entry.appendChild(el("div", { class: "eff" }));
      eff.textContent = `앱이 drag_offset에 더한다 → ${lit(S.drag_offset)} (아래가 양수)`;
    });
    function endDrag(e, cancelled) {
      if (!drag || e.pointerId !== drag.id) return;
      const track = drag.track;
      drag = null;
      handle.classList.remove("dragging");
      // 취소로 끝난 끌기는 끝을 모르므로 속도 0이다. 뗀 자리는 표본이 아니다.
      const v = cancelled ? 0 : track.velocityAt(e.timeStamp).y;
      const samples = cancelled ? null : track.samples.filter((s) => e.timeStamp - s.time <= D.MOTION_WINDOW_MS).length;
      release(v, cancelled ? "접촉이 취소됐다" : "놓았다", samples);
    }
    handle.addEventListener("pointerup", (e) => endDrag(e, false));
    handle.addEventListener("pointercancel", (e) => endDrag(e, true));
    handle.addEventListener("lostpointercapture", (e) => endDrag(e, true));

    // 누름 — scrim은 닫힘 계기, 시트 표면은 흡수, 비modal이면 뒤 화면이 받는다
    scrim.addEventListener("click", () => {
      if (cb.dismiss.checked) {
        closeSheet();
        log("scrim을 눌렀다", ["dismiss(sheet_dismiss_reason::scrim)"], "앱이 시트를 뺀다");
      } else log("scrim을 눌렀다", [], "dismiss가 비어 아무것도 내지 않는다. 누름은 scrim이 흡수한다 (뒤로 새지 않는다)");
    });
    openBehind.addEventListener("click", () => {
      if (!openSheet("뒤 화면의 '거르개 열기'를 눌렀다")) log("뒤 화면의 '거르개 열기'를 눌렀다", [], "비modal 시트는 뒤를 막지 않는다 — 뒤 화면의 단추가 그대로 눌린다 (이미 열려 있다)");
    });
    screen.addEventListener("keydown", (e) => {
      if (e.key !== "Escape") return;
      e.preventDefault();
      const c = config();
      if (!S.open || !c.modal) {
        log("Esc", [], S.open ? "비modal 시트에는 가둠이 없다 — Esc는 앱의 키 정책으로 흐른다" : "열린 가둠이 없다 — Esc는 앱의 키 정책으로 흐른다");
        return;
      }
      if (!c.dismiss) {
        log("Esc", [], "dismiss가 비어 가둠의 dismiss 액션이 없다 — Esc가 앱의 키 정책으로 흐른다");
        return;
      }
      closeSheet();
      log("Esc", ["dismiss(sheet_dismiss_reason::escape)"], "앱이 시트를 뺀다");
    });

    q("open").addEventListener("click", () => {
      if (!openSheet("열기")) log("열기", [], "이미 열려 있다");
    });
    for (const d of ["half", "full"]) {
      q(d).addEventListener("click", () => {
        if (!S.open) {
          log(`detent = ${d}`, [], "시트가 닫혀 있다. 열림도 앱 상태다");
          return;
        }
        S.detent = d;
        S.drag_offset = 0;
        draw();
        const kind = D.resolveSheetKind(config(), W);
        log(`앱이 detent = ${d}`, [], kind === "bottom" ? "다음 frame에 그 높이로 바로 선다 — 시트는 스스로 움직이지 않는다" : "옆 시트에는 멈춤점이 쓰이지 않는다. 값은 앱 상태라 아래 시트로 돌아오면 그대로다");
      });
    }
    function flingButton(v) {
      const label = v < 0 ? "↑ 튕겨 놓기" : "↓ 튕겨 놓기";
      const c = config();
      if (!S.open) log(label, [], "시트가 닫혀 있다");
      else if (D.resolveSheetKind(c, W) !== "bottom" || !c.drag) log(label, [], "끌기 대상이 없어 놓기도 없다 (아래 시트가 아니거나 drag가 비었다)");
      else release(v, v < 0 ? "위로 튕겨 놓았다" : "아래로 튕겨 놓았다", null);
    }
    q("fling-up").addEventListener("click", () => flingButton(-FLING_TEST));
    q("fling-down").addEventListener("click", () => flingButton(FLING_TEST));
    q("back").addEventListener("click", () => {
      if (S.open) {
        closeSheet();
        log("뒤로 가기", ["frame->back: sheet_dismiss_intent { sheet_dismiss_reason::back }"], "host가 아니라 앱이 frame에 실은 메시지다. 앱이 시트를 뺀다");
      } else log("뒤로 가기", [], "열린 것이 없어 앱이 frame->back을 비웠다 → 플랫폼 기본 동작 (Android는 Activity를 끝낸다)");
    });

    // 설정 바꾸기 — 앱이 다음 frame의 설정을 바꾼다
    function kindChanged(input) {
      const before = D.resolveSheetKind(Object.assign(config(), { kind: S.kindSeen.kind, side_from_width: S.kindSeen.adapt ? ADAPT_WIDTH : null }), S.kindSeen.width);
      const after = D.resolveSheetKind(config(), W);
      S.kindSeen = { kind: kindSel.value, adapt: adaptCb.checked, width: W };
      let note = "";
      if (after !== "bottom" && S.drag_offset !== 0) {
        S.drag_offset = 0;
        note = " 옆 시트에는 끌기 종료가 없어 앱이 끄던 양을 거둔다 (drag_offset = 0).";
      }
      fit();
      draw();
      if (before !== after) log(input, [], `실제로 선 종류가 ${before} → ${after}로 바뀐다. 메시지는 없다 — 같은 앱 상태로 다시 배치될 뿐이다.${note}`);
      return before !== after;
    }
    kindSel.addEventListener("change", () => kindChanged(`kind = sheet_kind::${kindSel.value}`) || log(`kind = sheet_kind::${kindSel.value}`, [], "같은 종류로 선다"));
    adaptCb.addEventListener("change", () => kindChanged(`side_from_width = ${adaptCb.checked ? ADAPT_WIDTH : "비움"}`) || log(`side_from_width = ${adaptCb.checked ? ADAPT_WIDTH : "비움"}`, [], adaptCb.checked ? `폭 ${W} < ${ADAPT_WIDTH}이라 아직 아래 시트다` : "바뀌지 않는다"));
    widthIn.addEventListener("input", () => {
      W = Number(widthIn.value);
      widthOut.textContent = String(W);
      kindChanged(`자리 폭 ${W}`);
    });
    modalCb.addEventListener("change", () => {
      draw();
      log(`modal = ${modalCb.checked}`, [], modalCb.checked ? "scrim으로 뒤를 막고 초점을 가두고 Esc를 받는다 (역할 dialog)" : "scrim·가둠·Esc가 없다. 뒤 화면을 그대로 쓴다 (역할 pane)");
    });
    const notes = {
      drag: ["손잡이를 끌 수 있다", "손잡이는 그려지지만 끌리지 않는다"],
      settle: ["놓으면 settle(detent)이 나간다", "release도 비면 놓아도 메시지가 없다"],
      dismiss: ["scrim·Esc·끌어 닫기가 dismiss(이유)를 낸다", "scrim·Esc가 아무것도 내지 않고, 닫는 쪽 놓기는 settle(half)로 돌아온다"],
      release: ["놓은 결과가 settle·dismiss(drag) 대신 release 하나로 나간다", "놓은 결과가 settle이나 dismiss(drag)로 나간다"],
    };
    for (const [name, box] of Object.entries(cb)) {
      box.addEventListener("change", () => {
        draw();
        log(`sheet_host_config::${name}를 ${box.checked ? "단다" : "비운다"}`, [], notes[name][box.checked ? 0 : 1]);
      });
    }

    function reset() {
      drag = null;
      lastRelease = null;
      S = { open: true, detent: "half", drag_offset: 0, kindSeen: { kind: "bottom", adapt: false, width: 390 } };
      kindSel.value = "bottom";
      modalCb.checked = true;
      adaptCb.checked = false;
      cb.drag.checked = true;
      cb.settle.checked = true;
      cb.dismiss.checked = true;
      cb.release.checked = false;
      W = 390;
      widthIn.value = "390";
      widthOut.textContent = "390";
      q("log").textContent = "";
      fit();
      draw();
      log("처음 상태", [], "손잡이를 끌어 놓아 본다. 천천히 놓으면 가장 가까운 자리, 빠르게 튕기면 그 방향의 다음 자리다. 화면을 누른 뒤 Esc, 어두운 바깥 누르기, 뒤로 가기도 된다.");
    }

    // 폭이 바뀔 때만 다시 맞춘다 (높이는 fit이 정한다 — 높이까지 보면 관찰이 제 결과를 다시 받는다)
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

  // ---------------------------------------------------------------- 확인 대화 상자
  const ROLES = [
    ["none", "— (없음)"],
    ["accept", "accept"],
    ["reject", "reject"],
    ["destructive", "destructive"],
    ["normal", "normal"],
  ];
  const PRESETS = [
    {
      name: "초기화 — 취소·초기화",
      severity: "warning",
      title: "카운터를 0으로 되돌릴까요?",
      body: "누른 횟수가 사라진다.",
      buttons: [
        { key: "cancel", text: "취소", role: "reject" },
        { key: "reset", text: "초기화", role: "destructive" },
      ],
      check: false,
    },
    {
      name: "저장 — 단추 셋과 확인 칸",
      severity: "info",
      title: "변경 내용을 저장할까요?",
      body: "이 문서에는 저장하지 않은 변경이 있다.",
      buttons: [
        { key: "later", text: "나중에 다시 묻기", role: "reject" },
        { key: "discard", text: "저장하지 않고 닫기", role: "destructive" },
        { key: "ok", text: "저장", role: "accept" },
      ],
      check: true,
    },
    {
      name: "위험한 단추 하나",
      severity: "warning",
      title: "카운터를 0으로 되돌릴까요?",
      body: "누른 횟수가 사라진다.",
      buttons: [{ key: "reset", text: "초기화", role: "destructive" }],
      check: false,
    },
    {
      name: "reject 없는 둘",
      severity: "warning",
      title: "카운터를 0으로 되돌릴까요?",
      body: "누른 횟수가 사라진다.",
      buttons: [
        { key: "reset", text: "초기화", role: "destructive" },
        { key: "keep", text: "그대로 두기", role: "normal" },
      ],
      check: false,
    },
    {
      name: "보통 둘",
      severity: null,
      title: "어느 쪽에 둘까요?",
      body: "두 단추의 역할이 모두 normal이다.",
      buttons: [
        { key: "left", text: "왼쪽", role: "normal" },
        { key: "right", text: "오른쪽", role: "normal" },
      ],
      check: false,
    },
  ];
  const GLYPH = { warning: "EA6C", info: "EA74" };
  const SLOTS = 3;

  function setupBox(root) {
    const q = (k) => root.querySelector(`[data-mb="${k}"]`);
    const rowsBox = q("rows");
    const presetSel = q("preset");
    const checkCb = q("check");
    const outsideCb = q("outside");
    const resultCb = q("result");
    const stage = q("stage");
    const stateBox = q("state");
    const logBox = q("log");
    const log = makeLog(logBox);

    PRESETS.forEach((p, i) => presetSel.append(el("option", { value: String(i), text: p.name })));
    const rows = [];
    for (let i = 0; i < SLOTS; i++) {
      const text = el("input", { type: "text", "aria-label": `단추 ${i + 1}의 글`, maxlength: "24" });
      const role = el("select", { "aria-label": `단추 ${i + 1}의 역할` }, ROLES.map(([v, t]) => el("option", { value: v, text: t })));
      rowsBox.append(el("div", { class: "mb-row" }, el("span", { text: `단추 ${i + 1}` }), text, role));
      rows.push({ key: `b${i + 1}`, text, role });
    }

    // 무대: 뒤 화면, scrim, 상자, 여는 단추 (상자 element는 그대로 두고 안만 다시 짓는다 — 브라우저 초점을 지킨다)
    const behind = el("div", { class: "behind", text: "뒤 화면 — 누른 횟수 7" });
    const scrim = el("div", { class: "mb-scrim" });
    const box = el("div", { class: "mb-box", tabindex: "0", role: "application", "aria-label": "확인 대화 상자 재현 — Tab, Enter, Space, Esc" });
    const openBtn = el("button", { type: "button", class: "dl-btn mb-open", text: "대화 열기" });
    stage.append(behind, scrim, box, openBtn);

    let preset = PRESETS[0];
    let M; // 앱 상태와 controller의 초점

    function boxConfig() {
      const buttons = rows.filter((r) => r.role.value !== "none").map((r) => ({ key: r.key, text: r.text.value, role: r.role.value }));
      return { buttons, roles: buttons.map((b) => b.role), check: checkCb.checked, outside: outsideCb.checked, result: resultCb.checked };
    }
    const focusOrder = (cfg) => (cfg.check ? ["check"] : []).concat(cfg.buttons.map((b) => b.key)); // 칸, 그다음 준 순서의 단추
    function nameOf(cfg, key) {
      if (key === "check") return "확인 칸";
      const b = cfg.buttons.find((x) => x.key === key);
      return b ? `“${b.text}” (${b.role})` : "—";
    }

    const ctx = document.createElement("canvas").getContext("2d");
    function naturalWidth(text) {
      // text_button_element::preferred_size: max(64, ceil(글 폭) + 2 × 12), 확인 대화 상자는 88보다 좁지 않다
      ctx.font = `400 12px ${getComputedStyle(document.body).fontFamily}`;
      const content = Math.ceil(ctx.measureText(text).width);
      return Math.max(88, Math.max(64, content + 24));
    }

    function render() {
      const cfg = boxConfig();
      scrim.hidden = !M.open;
      box.hidden = !M.open;
      openBtn.hidden = M.open;
      openBtn.classList.toggle("ring", M.returned);
      box.classList.toggle("mb-ring", M.ring);
      box.textContent = "";
      const boxW = Math.max(0, Math.min(360, stage.clientWidth - 32));
      const inner = Math.max(0, boxW - 40);
      box.style.width = boxW + "px";
      const head = el("div", { class: "mb-head" });
      if (preset.severity) head.append(icon(GLYPH[preset.severity], preset.severity));
      head.append(el("div", { class: "mb-texts" }, el("div", { class: "mb-title", text: preset.title }), el("div", { class: "mb-body", text: preset.body })));
      box.append(head);
      if (cfg.check) {
        const check = el("div", { class: "mb-check" + (M.checked ? " on" : "") + (M.focus === "check" ? " cur" : "") }, el("i", { text: M.checked ? "✓" : "" }), "다시 묻지 않기");
        box.append(check);
      }
      const widths = cfg.buttons.map((b) => naturalWidth(b.text));
      const stacked = D.messageBoxStacksButtons(widths, inner);
      const defaultIndex = D.messageBoxDefaultIndex(cfg.roles);
      const row = el("div", { class: "mb-buttons" + (stacked ? " stacked" : "") });
      cfg.buttons.forEach((b, i) => {
        const cls = ["mb-b", b.role, i === defaultIndex ? "default" : "", M.focus === b.key ? "cur" : ""].filter(Boolean).join(" ");
        row.append(el("div", { class: cls, dataset: { key: b.key }, style: { width: (stacked ? inner : widths[i]) + "px" }, text: b.text }));
      });
      if (cfg.buttons.length) box.append(row);
      drawState(cfg, widths, inner, stacked);
    }

    function drawState(cfg, widths, inner, stacked) {
      stateBox.textContent = "";
      const kv = (k, v) => stateBox.append(el("dt", { text: k }), el("dd", null, el("code", { text: v })));
      const di = D.messageBoxDefaultIndex(cfg.roles);
      const ei = D.messageBoxEntryIndex(cfg.roles);
      const xi = D.messageBoxDismissIndex(cfg.roles);
      const lastReject = cfg.roles.lastIndexOf("reject");
      kv("기본 버튼 (채움)", di >= 0 ? `${nameOf(cfg, cfg.buttons[di].key)} — 마지막 accept` : "없음 — accept가 없다");
      kv("첫 초점", ei >= 0 ? `${nameOf(cfg, cfg.buttons[ei].key)} — ${di >= 0 ? "마지막 accept" : lastReject >= 0 ? "accept가 없어 마지막 reject" : "역할로 못 갈라 첫 단추"}` : "없음 — 단추가 없다");
      let esc;
      if (!cfg.result) esc = "없음 — result가 비었다. Esc는 앱으로, 바깥 누름은 흡수";
      else if (xi < 0) esc = "없음 — reject가 없고 단추가 하나가 아니다. Esc는 앱으로, 바깥 누름은 흡수";
      else esc = `${nameOf(cfg, cfg.buttons[xi].key)} — ${xi === lastReject ? "마지막 reject" : "하나뿐인 단추"}${cfg.outside ? "" : " (바깥 누름은 끔)"}`;
      kv("Esc · 바깥 누름", esc);
      if (M.open) kv("초점 · 테", M.focus ? `${nameOf(cfg, M.focus)} · 테 ${M.ring ? "켬" : "끔"}` : "없음");
      else kv("초점 · 테", M.returned ? "여는 단추 (focus_return) · 테 켬" : "—");
      const sum = widths.reduce((a, w) => a + w, 0) + 8 * Math.max(0, widths.length - 1);
      kv("단추 배치", widths.length ? `${stacked ? "쌓음" : "한 행"} — 폭 합 ${sum} ${stacked ? ">" : "≤"} 안쪽 ${inner} (글 폭은 근사)` : "단추 없음");
      kv("확인 칸", cfg.check ? `checked = ${M.checked} (앱 상태)` : "없음");
    }

    // 앱이 대화를 연다 — 진입: 초점이 없고 가둠이 자리를 이름 지었으면 거기, 테는 끈다
    function openBox(input, takeFocus) {
      const cfg = boxConfig();
      const ei = D.messageBoxEntryIndex(cfg.roles);
      M.open = true;
      M.returned = false;
      M.focus = ei >= 0 ? cfg.buttons[ei].key : null;
      M.ring = false;
      render();
      if (takeFocus) box.focus({ preventScroll: true });
      log(input, [], M.focus ? `첫 초점: ${nameOf(cfg, M.focus)} — 테 없이 선다. 첫 Tab이 테를 켠다` : "단추가 없어 첫 초점이 없다");
    }
    // 앱이 닫는다 — 가둠이 사라지면 focus_return 자리로, 테를 켠 채
    function closeBox() {
      M.open = false;
      M.returned = true;
      M.focus = null;
      render();
      openBtn.focus({ preventScroll: true });
    }

    function press(key, input) {
      const cfg = boxConfig();
      if (!cfg.result) {
        render();
        log(input, [], "result가 비어 단추가 메시지를 내지 않는다 — 대화가 열린 채 남는다");
        return;
      }
      closeBox();
      log(input, [`result(u8"${key}")`], "앱이 key로 가르고 닫는다 → 다음 frame에 짓지 않는다. 초점은 여는 단추로 돌아간다 (테 켬)");
    }
    function toggleCheck(input) {
      M.checked = !M.checked;
      render();
      log(input, ["check->toggle"], `앱이 칸을 뒤집는다 → checked = ${M.checked}. 대화는 닫지 않는다`);
    }
    function dismiss(input, outside) {
      const cfg = boxConfig();
      const xi = D.messageBoxDismissIndex(cfg.roles);
      if (outside && !cfg.outside) {
        log(input, [], "outside_dismisses가 거짓이다 — 바깥 누름은 흡수만 한다");
        return;
      }
      if (!cfg.result || xi < 0) {
        const why = !cfg.result ? "result가 비어 닫는 액션이 없다" : "reject가 없고 단추가 하나가 아니라 닫는 길이 없다";
        if (outside) log(input, [], `${why} — 바깥 누름은 흡수만 한다`);
        else {
          // 앱의 키 정책이 받은 Esc다. 재현의 앱은 초점을 재현 밖(본보기 선택)으로 내보낸다 — 가둠에서 나갈 길이다
          presetSel.focus({ preventScroll: true });
          log(input, [], `${why} — Esc가 앱의 키 정책으로 흐른다. 이 재현의 앱은 키보드 초점을 재현 밖으로 내보낸다`);
        }
        return;
      }
      const key = cfg.buttons[xi].key;
      const which = nameOf(cfg, key);
      closeBox();
      log(input, [`result(u8"${key}")`], `${which}의 key다${outside ? " (Esc와 같은 결과)" : ""}. 앱이 닫는다`);
    }

    box.addEventListener("keydown", (e) => {
      if (!M.open) return;
      const cfg = boxConfig();
      if (e.key === "Tab") {
        const order = focusOrder(cfg);
        if (!order.length) return; // 자리가 하나도 없으면 삼키지 않는다
        e.preventDefault();
        const i = order.indexOf(M.focus);
        const step = e.shiftKey ? -1 : 1;
        M.focus = i < 0 ? order[e.shiftKey ? order.length - 1 : 0] : order[(i + step + order.length) % order.length];
        M.ring = true;
        render();
        log(`${e.shiftKey ? "Shift+" : ""}Tab`, [], `초점 → ${nameOf(cfg, M.focus)} · 테 켬. 가둠 안만 돈다 (칸, 그다음 준 순서의 단추)`);
        return;
      }
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        const name = e.key === "Enter" ? "Enter" : "Space";
        if (e.ctrlKey || e.altKey || e.metaKey) {
          log(`${e.ctrlKey ? "Ctrl+" : ""}${e.altKey ? "Alt+" : ""}${e.metaKey ? "Meta+" : ""}${name}`, [], "Shift 말고 다른 수정자와 함께라 앱 단축키다 — 초점 실행도 기본 버튼도 건너뛴다");
          return;
        }
        // 초점이 이긴다: 초점 element의 left_click이 먼저다
        if (M.focus === "check") toggleCheck(`${name} — 초점: 확인 칸`);
        else if (M.focus) press(M.focus, `${name} — 초점: ${nameOf(cfg, M.focus)}`);
        else {
          const di = D.messageBoxDefaultIndex(cfg.roles);
          if (name === "Enter" && di >= 0) press(cfg.buttons[di].key, "Enter — 초점이 없어 기본 버튼");
          else log(name, [], "초점도 기본 버튼도 없다 — 키가 앱으로 흐른다");
        }
        return;
      }
      if (e.key === "Escape") {
        e.preventDefault();
        dismiss("Esc", false);
      }
    });
    box.addEventListener("click", (e) => {
      const cfg = boxConfig();
      const b = e.target.closest(".mb-b");
      if (b) {
        M.focus = b.dataset.key; // 누름으로 잡은 초점에는 테를 그리지 않는다
        M.ring = false;
        press(b.dataset.key, `“${(cfg.buttons.find((x) => x.key === b.dataset.key) || {}).text || ""}”를 눌렀다`);
        return;
      }
      if (e.target.closest(".mb-check")) {
        M.focus = "check";
        M.ring = false;
        toggleCheck("확인 칸을 눌렀다");
      }
      // 그 밖의 자리는 상자가 흡수한다 (hit_opaque)
    });
    scrim.addEventListener("click", () => dismiss("바깥(scrim)을 눌렀다", true));
    openBtn.addEventListener("click", () => openBox("대화 열기", true));

    // 설정이 바뀌면 앱이 다시 짓는다. 쥔 초점이 사라졌으면 controller가 거두고 진입이 다시 선다 (테 끔).
    function rebuilt(input, effect) {
      if (!M.open) {
        render();
        log(input, [], effect);
        return;
      }
      const cfg = boxConfig();
      const order = focusOrder(cfg);
      let note = "";
      if (M.focus && !order.includes(M.focus)) {
        const ei = D.messageBoxEntryIndex(cfg.roles);
        M.focus = ei >= 0 ? cfg.buttons[ei].key : null;
        M.ring = false;
        note = ` 쥔 초점이 사라져 거두고 진입이 다시 선다 → ${M.focus ? nameOf(cfg, M.focus) : "초점 없음"} (테 끔).`;
      }
      render();
      log(input, [], effect + note);
    }
    function loadPreset(p) {
      preset = p;
      rows.forEach((r, i) => {
        const b = p.buttons[i];
        r.key = b ? b.key : `b${i + 1}`;
        r.text.value = b ? b.text : "";
        r.role.value = b ? b.role : "none";
      });
      checkCb.checked = p.check;
    }
    presetSel.addEventListener("change", () => {
      loadPreset(PRESETS[Number(presetSel.value)]);
      M.checked = false;
      openBox(`본보기: ${preset.name}`);
    });
    rows.forEach((r, i) => {
      r.role.addEventListener("change", () => rebuilt(`단추 ${i + 1}의 역할 = ${r.role.value === "none" ? "없음" : r.role.value}`, "앱이 다른 단추 목록으로 다시 짓는다."));
      r.text.addEventListener("input", () => render());
    });
    checkCb.addEventListener("change", () => rebuilt(`확인 칸 ${checkCb.checked ? "켬" : "끔"}`, checkCb.checked ? "Tab 순서의 맨 앞에 칸이 든다." : "칸이 빠진다."));
    outsideCb.addEventListener("change", () => rebuilt(`outside_dismisses = ${outsideCb.checked}`, outsideCb.checked ? "바깥 누름이 Esc와 같은 결과를 낸다." : "바깥 누름은 흡수만 한다. Esc는 그대로다."));
    resultCb.addEventListener("change", () => rebuilt(`result ${resultCb.checked ? "달기" : "비우기"}`, resultCb.checked ? "단추·Esc·바깥 누름이 result(key)를 낸다." : "단추도 Esc도 바깥 누름도 메시지를 내지 않는다."));

    function reset() {
      presetSel.value = "0";
      loadPreset(PRESETS[0]);
      outsideCb.checked = true;
      resultCb.checked = true;
      logBox.textContent = "";
      M = { open: false, returned: false, focus: null, ring: false, checked: false };
      const cfg = boxConfig();
      const ei = D.messageBoxEntryIndex(cfg.roles);
      M.open = true;
      M.focus = ei >= 0 ? cfg.buttons[ei].key : null;
      render();
      log("처음 상태", [], "상자를 누른 뒤 키를 쳐 본다. 본보기와 역할을 바꾸면 기본 버튼·첫 초점·Esc가 어디로 가는지 보인다. 첫 초점에는 테가 없다.");
    }
    let seenWidth = -1;
    const rewidth = () => {
      if (stage.clientWidth === seenWidth) return;
      seenWidth = stage.clientWidth;
      requestAnimationFrame(() => M && render());
    };
    if (typeof ResizeObserver === "function") new ResizeObserver(rewidth).observe(stage);
    else window.addEventListener("resize", rewidth);
    root.addEventListener("lw-reset", reset);
    reset();
  }

  window.LuilDialogsLab = { setupSheet, setupBox };
})();

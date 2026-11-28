/* luil 설명서 — 플랫폼 서비스와 네트워크 실습: 응답 몸의 갈래
 *
 * Content-Type 한 줄, 몸 바이트, 요청의 parse 설정(assume_kind·html_as_text)을 받아 decode_http_body가 고르는
 * 갈래와 그 출처, 그리고 슬롯을 채우는 길을 보인다.
 * 판정은 전부 assets/httpbodymodel.js(원본 C++을 컴파일한 대조기와 견준 이식)가 한다. 이 파일은 입력과 그림만 맡는다.
 * JSON 파싱·그림 디코딩·charset 갈고리의 결과는 옮기지 않았으므로 "무엇을 부르는가"까지만 적는다.
 */
(function () {
  "use strict";
  const B = window.LuilHttpBody;
  const { el, icon } = window.LU;

  const MIB = 1024 * 1024;
  const decoder = new TextDecoder("utf-8");

  // Content-Type 보기. 빈 글은 "헤더 없음"이다 (find_header가 빈 값을 돌려준다).
  const CT_PRESETS = [
    "application/json; charset=utf-8",
    "application/problem+json",
    "text/plain",
    'text/html; charset="EUC-KR"',
    "image/png",
    "image/svg+xml",
    "application/octet-stream",
    "",
    'Text/Plain ; Charset = "UTF\\-8" ; charset=latin1',
    "json",
    "text/csv; charset=iso-8859-1",
  ];

  const hexBytes = (text) => text.trim().split(/\s+/).map((h) => parseInt(h, 16));
  const repeat = (byte, n) => new Array(n).fill(byte);
  const BODY_PRESETS = [
    { id: "empty", label: "빈 몸 (204·HEAD처럼 0 바이트)", bytes: [] },
    { id: "json", label: 'JSON 글 {"a":1}', bytes: B.bytesOf('{"a":1}') },
    { id: "korean", label: "UTF-8 글 \"안녕하세요\"", bytes: B.bytesOf("안녕하세요") },
    { id: "broken", label: "깨진 UTF-8 (ab FF cd C3 28)", bytes: hexBytes("61 62 FF 63 64 C3 28") },
    { id: "bom", label: "BOM이 붙은 글 (EF BB BF + hi)", bytes: hexBytes("EF BB BF 68 69") },
    { id: "euckr", label: "EUC-KR로 쓴 \"한글\" (C7 D1 B1 DB)", bytes: hexBytes("C7 D1 B1 DB") },
    { id: "png", label: "PNG 서명", bytes: hexBytes("89 50 4E 47 0D 0A 1A 0A 00 00 00 0D 49 48 44 52") },
    { id: "jpeg", label: "JPEG 서명", bytes: hexBytes("FF D8 FF E0 00 10 4A 46 49 46") },
    { id: "gif", label: "GIF89a 서명", bytes: B.bytesOf("GIF89a").concat([1, 0, 1, 0]) },
    { id: "webp", label: "WEBP (RIFF····WEBP)", bytes: B.bytesOf("RIFF").concat([0x24, 0, 0, 0]).concat(B.bytesOf("WEBPVP8 ")) },
    { id: "wav", label: "WAV (RIFF····WAVE) — 그림이 아니다", bytes: B.bytesOf("RIFF").concat([0x24, 0, 0, 0]).concat(B.bytesOf("WAVEfmt ")) },
    { id: "ico", label: "ICO (00 00 01 00) — 맡지 않는다", bytes: hexBytes("00 00 01 00 01 00 10 10") },
    { id: "deep", label: "260겹 [[[…]]] JSON", bytes: repeat(0x5b, 260).concat(repeat(0x5d, 260)) },
  ];

  const ASSUME = ["empty", "json", "image", "text", "html", "bytes"];
  const INITIAL = { ct: "application/json; charset=utf-8", body: "json", assume: "", html: false };

  const toHex = (bytes) => bytes.map((b) => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
  const asText = (bytes) => decoder.decode(new Uint8Array(bytes));
  const show = (bytes) => (bytes.length ? asText(bytes) : "");
  const code = (text) => el("code", { text });
  const sizeText = (n) => (n >= MIB ? (n / MIB).toFixed(1) + " MiB" : n + " 바이트");

  function setup(root) {
    if (!root || !B) return;
    const q = (name) => root.querySelector('[data-bk="' + name + '"]');
    const ctInput = q("ct");
    const chips = q("ct-chips");
    const preset = q("body-preset");
    const hexBox = q("hex");
    const hexNote = q("hex-note");
    const assumeBox = q("assume");
    const htmlBox = q("html");
    const mtBox = q("mt");
    const stepsBox = q("steps");
    const verdictBox = q("verdict");
    const slotsBox = q("slots");

    const state = { ct: INITIAL.ct, bytes: BODY_PRESETS[1].bytes.slice(), body: INITIAL.body, assume: INITIAL.assume, html: INITIAL.html };

    // ---------- 입력 ----------
    for (const value of CT_PRESETS) {
      const b = el("button", { type: "button", "aria-pressed": "false", title: value === "" ? "Content-Type 헤더가 없다" : value, text: value === "" ? "(헤더 없음)" : value });
      b.dataset.value = value;
      b.addEventListener("click", () => {
        state.ct = value;
        ctInput.value = value;
        render();
      });
      chips.append(b);
    }
    for (const p of BODY_PRESETS) preset.append(el("option", { value: p.id, text: p.label }));
    preset.append(el("option", { value: "custom", text: "직접 고친 바이트" }));
    assumeBox.append(el("option", { value: "", text: "(없음) — 서버의 말을 따른다" }));
    for (const k of ASSUME) assumeBox.append(el("option", { value: k, text: "http_body_kind::" + k }));

    ctInput.addEventListener("input", () => {
      state.ct = ctInput.value;
      render();
    });
    preset.addEventListener("change", () => {
      const p = BODY_PRESETS.find((x) => x.id === preset.value);
      if (!p) return;
      state.body = p.id;
      state.bytes = p.bytes.slice();
      hexBox.value = toHex(state.bytes);
      hexNote.textContent = "";
      hexNote.classList.remove("bad");
      render();
    });
    hexBox.addEventListener("input", () => {
      const raw = hexBox.value.replace(/\s+/g, "");
      if (!/^[0-9a-fA-F]*$/.test(raw) || raw.length % 2 !== 0) {
        hexNote.textContent = "16진 두 자리씩 적는다 (예: 7B 22 61 22 7D). 고칠 때까지 앞의 바이트로 판정한다.";
        hexNote.classList.add("bad");
        return;
      }
      hexNote.classList.remove("bad");
      state.bytes = raw.match(/../g) ? raw.match(/../g).map((h) => parseInt(h, 16)) : [];
      state.body = "custom";
      preset.value = "custom";
      render();
    });
    assumeBox.addEventListener("change", () => {
      state.assume = assumeBox.value;
      render();
    });
    htmlBox.addEventListener("change", () => {
      state.html = htmlBox.checked;
      render();
    });
    root.addEventListener("lw-reset", () => {
      state.ct = INITIAL.ct;
      state.body = INITIAL.body;
      state.bytes = BODY_PRESETS.find((x) => x.id === INITIAL.body).bytes.slice();
      state.assume = INITIAL.assume;
      state.html = INITIAL.html;
      syncInputs();
      render();
    });

    function syncInputs() {
      ctInput.value = state.ct;
      preset.value = state.body;
      hexBox.value = toHex(state.bytes);
      hexNote.textContent = "";
      hexNote.classList.remove("bad");
      assumeBox.value = state.assume;
      htmlBox.checked = state.html;
    }

    // ---------- 판정과 그림 ----------
    function stepRow(status, title, body) {
      const glyph = status === "hit" ? icon("EAB6") : status === "pass" ? icon("EA76") : el("span", { class: "ci", "aria-hidden": "true", text: "·" });
      const label = status === "hit" ? "여기서 정해졌다: " : status === "pass" ? "해당 없음: " : "닿지 않음: ";
      return el("li", { class: status }, glyph, el("div", null, el("span", { class: "sr-only", text: label }), el("strong", { text: title }), " — ", body));
    }

    function mtRow(name, value) {
      return [el("dt", { text: name }), el("dd", null, value)];
    }

    function slot(name, value, cls) {
      return el("div", null, el("span", { class: "k", text: name }), el("div", { class: "v" + (cls ? " " + cls : "") }, value));
    }

    function render() {
      for (const b of chips.children) b.setAttribute("aria-pressed", b.dataset.value === state.ct ? "true" : "false");
      const media = B.parseMediaType(B.bytesOf(state.ct));
      const bytes = state.bytes;
      const options = { assumeKind: state.assume || null, htmlAsText: state.html };
      const d = B.decodeHttpBody(bytes, media, options);

      // parse_media_type
      mtBox.textContent = "";
      const essence = show(media.essence);
      const charset = show(media.charset);
      mtBox.append(
        ...mtRow("valid", code(media.valid ? "true" : "false")),
        ...mtRow("essence", essence ? code(essence) : el("span", { text: "(빈 값)" })),
        ...mtRow("charset", charset ? code(charset) : el("span", { text: "(빈 값)" }))
      );

      // 갈래를 정하는 차례 (decode_http_body → decide_kind)
      stepsBox.textContent = "";
      const empty = bytes.length === 0;
      stepsBox.append(stepRow(empty ? "hit" : "pass", "몸이 비었나", empty ? el("span", null, "비었다 → ", code("empty"), " · ", code("absent"), ". Content-Type은 보지 않는다.") : el("span", { text: sizeText(bytes.length) + "가 있다." })));
      const assumed = !empty && options.assumeKind != null;
      stepsBox.append(
        stepRow(empty ? "skip" : assumed ? "hit" : "pass", "parse.assume_kind", empty ? el("span", { text: "앞에서 정해졌다." }) : assumed ? el("span", null, "앱이 정했다 → ", code(d.kind), " · ", code("assumed"), ". 서버의 말도 바이트도 보지 않는다.") : el("span", { text: "없다." }))
      );
      const reason = B.invitesSniffingReason(media);
      const sniffReached = !empty && !assumed;
      let sniffBody;
      if (!sniffReached) sniffBody = el("span", { text: "앞에서 정해졌다." });
      else if (reason) {
        const s = B.sniffBodyKindExplained(bytes);
        sniffBody = el("span", null, reason + " — 앞 바이트를 본다. ", s.signature ? el("span", null, s.signature + " 서명 → ", code("image")) : el("span", null, "그림 서명(png·jpeg·gif·bmp·webp)이 없다 → ", code("bytes")), " · ", code("sniffed"), ".");
      } else sniffBody = el("span", { text: "Content-Type이 형식을 말했다. 바이트를 보지 않는다." });
      stepsBox.append(stepRow(!sniffReached ? "skip" : reason ? "hit" : "pass", "냄새 맡기", sniffBody));
      const declared = sniffReached && !reason;
      let tableBody;
      if (!declared) tableBody = el("span", { text: "앞에서 정해졌다." });
      else {
        const c = B.classifyMediaTypeExplained(media);
        tableBody = el("span", null, c.rule + " → ", code(c.kind), " · ", code("declared"), ".");
      }
      stepsBox.append(stepRow(declared ? "hit" : "skip", "Content-Type 표", tableBody));

      // 결론
      verdictBox.textContent = "";
      verdictBox.append(
        el("span", { class: "tag", text: "kind = " + d.kind }),
        el("span", { class: "tag src", text: "kind_source = " + d.source }),
        el("span", { text: B.httpBodyIsParsed(d.kind) ? "값으로 푸는 갈래다" : "값으로 풀지 않는 갈래다" })
      );

      // 슬롯
      slotsBox.textContent = "";
      slotsBox.append(slot("bytes", el("span", { text: bytes.length ? sizeText(bytes.length) + " — 어느 갈래든 받은 그대로 든다 (BOM도)." : "비어 있다." })));
      const charsetName = charset || "(빈 값)";
      let work;
      let cls = "";
      switch (d.fill) {
        case "json-parse":
          work = el("span", null, "크기(상한 16 MiB)와 깊이(" + d.depth + ", 상한 256)가 상한 안이다 → nlohmann이 푼다. 깨진 JSON이면 갈래는 ", code("json"), "인 채 ", code("json"), " 슬롯이 비고 ", code("parse_error"), "가 선다. 파싱의 성패는 이 재현에 없다.");
          break;
        case "json-too-deep":
          work = el("span", null, "깊이(" + d.depth + ")가 상한(256)을 넘어 파서를 부르지 않는다. ", code("json"), " 슬롯은 비어 있다.");
          cls = "err";
          break;
        case "json-too-large":
          work = el("span", null, "크기(" + sizeText(bytes.length) + ")가 상한(16 MiB)을 넘어 파서를 부르지 않는다. ", code("json"), " 슬롯은 비어 있다.");
          cls = "err";
          break;
        case "image":
          work = el("span", null, code("decode_animated_image_bytes"), "로 푼다 (", code("parse.image.animated"), "가 거짓이면 첫 장만). 디코딩의 성패는 이 재현에 없다. 실패해도 갈래는 ", code("image"), "이고 이유가 ", code("parse_error"), "에 선다.");
          break;
        case "utf8":
          work = el("span", { text: "charset " + charsetName + " → UTF-8로 읽는다. BOM을 넘기고, 올바른 UTF-8이라 그대로 담는다." });
          break;
        case "utf8-replaced":
          work = el("span", { text: "charset " + charsetName + " → UTF-8로 읽는다. 잘못된 바이트 " + d.replaced + "개를 U+FFFD로 바꿔 담는다. 글은 쓸 수 있고, 원문과 다르다는 사실이 parse_error에 남는다." });
          cls = "err";
          break;
        case "transcode": {
          const bare = B.decodeHttpBody(bytes, media, Object.assign({}, options, { transcoder: false }));
          work = el("span", null, "charset이 UTF-8 갈래가 아니다 (" + charsetName + ") → ", code("transcode"), " 갈고리가 옮긴다 (기본 ", code("codepage_text_transcoder"), "). 결과는 플랫폼마다 다르다 — Windows는 옮기지 못한 바이트를 바꿔 넣고, Android·Apple은 실패면 빈 글과 이유를 준다. 변환은 이 재현에 없다. 갈고리가 비어 있으면: ", code(bare.parseError));
          break;
        }
        case "html":
          work = el("span", null, "읽어 두기만 한다. 바이트와 charset(" + charsetName + ")만 남고 ", code("text"), "는 비어 있다. ", code("html_as_text"), "를 켜면 text 규칙을 함께 건다.");
          break;
        default:
          work = d.kind === "empty" ? el("span", { text: "몸이 없다. 아무것도 풀지 않는다." }) : el("span", { text: "풀지 않는다. 실패가 아니라 \"우리가 풀지 않는 몸\"이다. 바이트는 온전하다." });
      }
      slotsBox.append(slot("푸는 일", work, cls));
      if (d.text) {
        const shown = asText(d.text);
        slotsBox.append(slot("text", el("pre", { text: shown.length > 240 ? shown.slice(0, 240) + "…" : shown || "(빈 글)" }), "on"));
      }
      if (d.parseError) slotsBox.append(slot("parse_error", code(d.parseError), "err"));
      let parsed;
      if (d.kind === "json") parsed = d.fill === "json-parse" ? "json 슬롯이 서면 참 (파싱 성패에 달렸다)" : "거짓 — json 슬롯이 비었다";
      else if (d.kind === "image") parsed = "그림이 서면 참 (디코딩 성패에 달렸다)";
      else if (d.kind === "text") parsed = d.text ? "참 — text 슬롯이 섰다" : "갈고리가 글을 돌려주면 참";
      else if (d.kind === "html") parsed = "거짓 — html은 html_as_text를 켜도 거짓이다";
      else parsed = "거짓";
      slotsBox.append(slot("parsed()", el("span", { text: parsed })));
    }

    syncInputs();
    render();
  }

  window.LuilServicesLab = { setup };
})();

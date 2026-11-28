/* luil 설명서 — 응답 몸의 갈래를 정하는 규칙을 JS로 옮긴 것
 *
 * src/net/http_media_type.cpp (parse_media_type, classify_media_type, media_type_invites_sniffing, sniff_body_kind)와
 * src/net/http_body.cpp (decode_http_body의 갈래 정하기 decide_kind, is_utf8_charset, without_bom, json_nesting_depth,
 * fill_json의 크기·깊이 검사, fill_text의 UTF-8 갈래), src/text/utf8_text.cpp (utf8_is_valid, utf8_replace_invalid)를
 * 함수 단위로 옮겼다. 이름 옆 주석은 C++ 원본의 이름이다.
 *
 * 글은 C++과 같이 **바이트 배열**로 다룬다 (Content-Type 줄도 UTF-8 바이트다). 소문자화는 ASCII에만, 다듬기는 공백·탭에만
 * 건다 — JS의 toLowerCase()·trim()과 다르다.
 *
 * 옮기지 않은 것: nlohmann의 JSON 파싱, 그림 디코딩, charset 갈고리(transcode)의 변환 결과. 이 셋은 결과 대신
 * "무엇을 부르는가"만 답한다 (fill 값).
 */
(function () {
  "use strict";

  const encoder = new TextEncoder();

  const KIND = ["empty", "json", "image", "text", "html", "bytes"]; // http_body_kind
  const SOURCE = ["absent", "declared", "sniffed", "assumed"]; // http_body_kind_source

  const ascii = (text) => Array.from(text, (c) => c.charCodeAt(0));
  const bytesOf = (text) => Array.from(encoder.encode(text)); // u8"…"

  // ---------- src/net/http_media_type.cpp ----------

  const lowerAscii = (b) => (b >= 0x41 && b <= 0x5a ? b - 0x41 + 0x61 : b); // lower_ascii
  const isSpace = (b) => b === 0x20 || b === 0x09; // is_space

  // trim
  function trim(bytes) {
    let a = 0;
    let e = bytes.length;
    while (a < e && isSpace(bytes[a])) a++;
    while (e > a && isSpace(bytes[e - 1])) e--;
    return bytes.slice(a, e);
  }

  const lowered = (bytes) => bytes.map(lowerAscii); // lowered

  function same(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }

  // ends_with · starts_with
  const endsWith = (text, suffix) => text.length >= suffix.length && same(text.slice(text.length - suffix.length), suffix);
  const startsWith = (text, prefix) => text.length >= prefix.length && same(text.slice(0, prefix.length), prefix);

  // is_essence: "/"가 정확히 하나이고 양쪽이 비지 않았다.
  function isEssence(essence) {
    const slash = essence.indexOf(0x2f);
    return slash !== -1 && slash !== 0 && slash + 1 < essence.length && essence.indexOf(0x2f, slash + 1) === -1;
  }

  // find_parameter_end: 따옴표 밖의 첫 ";". 따옴표 안의 "\"는 다음 바이트를 건너뛴다.
  function findParameterEnd(text) {
    let quoted = false;
    for (let i = 0; i < text.length; i++) {
      const b = text[i];
      if (quoted) {
        if (b === 0x5c && i + 1 < text.length) i++;
        else if (b === 0x22) quoted = false;
        continue;
      }
      if (b === 0x22) quoted = true;
      else if (b === 0x3b) return i;
    }
    return text.length;
  }

  // unquote: 따옴표를 벗기고 "\" 이스케이프를 푼다. 닫는 따옴표가 없어도 끝까지 담는다.
  function unquote(text) {
    if (text.length < 2 || text[0] !== 0x22) return text.slice();
    const out = [];
    for (let i = 1; i < text.length; i++) {
      const b = text[i];
      if (b === 0x22) break;
      if (b === 0x5c && i + 1 < text.length) i++;
      out.push(text[i]);
    }
    return out;
  }

  const CHARSET = ascii("charset");

  // parse_media_type(value) → { essence, charset, valid } (essence·charset는 바이트 배열)
  function parseMediaType(value) {
    const head = value.slice(0, findParameterEnd(value));
    const essence = lowered(trim(head));
    if (!isEssence(essence)) return { essence: [], charset: [], valid: false };
    const parsed = { essence, charset: [], valid: true };
    let rest = value.slice(head.length);
    let charsetSeen = false;
    while (rest.length > 0) {
      rest = rest.slice(1); // ";"
      const end = findParameterEnd(rest);
      const parameter = trim(rest.slice(0, end));
      rest = rest.slice(end);
      const equals = parameter.indexOf(0x3d);
      if (equals === -1) continue; // 값 없는 ";"와 ";;"는 넘긴다
      if (!same(lowered(trim(parameter.slice(0, equals))), CHARSET)) continue;
      if (charsetSeen) continue; // 앞엣것이 이긴다 (빈 값이어도)
      charsetSeen = true;
      parsed.charset = lowered(trim(unquote(trim(parameter.slice(equals + 1)))));
    }
    return parsed;
  }

  // classify_media_type — 차례가 규칙이다. 걸린 줄(rule)도 함께 돌려준다.
  const T = (s) => ascii(s);
  const RULES = {
    appJson: T("application/json"),
    textJson: T("text/json"),
    plusJson: T("+json"),
    image: T("image/"),
    svg: T("image/svg+xml"),
    html: T("text/html"),
    xhtml: T("application/xhtml+xml"),
    text: T("text/"),
    appXml: T("application/xml"),
    plusXml: T("+xml"),
    js: T("application/javascript"),
    form: T("application/x-www-form-urlencoded"),
    octet: T("application/octet-stream"),
  };

  function classifyMediaTypeExplained(media) {
    const e = media.essence;
    if (same(e, RULES.appJson)) return { kind: "json", rule: "application/json" };
    if (same(e, RULES.textJson)) return { kind: "json", rule: "text/json" };
    if (endsWith(e, RULES.plusJson)) return { kind: "json", rule: "+json으로 끝난다" };
    if (startsWith(e, RULES.image)) return same(e, RULES.svg) ? { kind: "bytes", rule: "image/svg+xml (svg는 bytes)" } : { kind: "image", rule: "image/로 시작한다" };
    if (same(e, RULES.html)) return { kind: "html", rule: "text/html" };
    if (same(e, RULES.xhtml)) return { kind: "html", rule: "application/xhtml+xml" };
    if (startsWith(e, RULES.text)) return { kind: "text", rule: "text/로 시작한다" };
    if (same(e, RULES.appXml)) return { kind: "text", rule: "application/xml" };
    if (endsWith(e, RULES.plusXml)) return { kind: "text", rule: "+xml로 끝난다" };
    if (same(e, RULES.js)) return { kind: "text", rule: "application/javascript" };
    if (same(e, RULES.form)) return { kind: "text", rule: "application/x-www-form-urlencoded" };
    return { kind: "bytes", rule: "표에 없다" };
  }
  const classifyMediaType = (media) => classifyMediaTypeExplained(media).kind;

  // media_type_invites_sniffing — 참이 되는 자리는 셋뿐이다.
  function invitesSniffingReason(media) {
    if (media.valid === false) return "Content-Type이 없거나 읽을 수 없다 (valid 거짓)";
    if (media.essence.length === 0) return "essence가 비었다";
    if (same(media.essence, RULES.octet)) return "application/octet-stream";
    return null;
  }
  const mediaTypeInvitesSniffing = (media) => invitesSniffingReason(media) !== null;

  // sniff_body_kind — 그림 서명 다섯만 본다.
  const SIGNATURES = [
    ["png", [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]],
    ["jpeg", [0xff, 0xd8, 0xff]],
    ["gif87a", ascii("GIF87a")],
    ["gif89a", ascii("GIF89a")],
    ["bmp", ascii("BM")],
  ];
  const RIFF = ascii("RIFF");
  const WEBP = ascii("WEBP");

  // has_signature
  const hasSignature = (bytes, signature, offset) => bytes.length >= (offset || 0) + signature.length && same(bytes.slice(offset || 0, (offset || 0) + signature.length), signature);

  function sniffBodyKindExplained(bytes) {
    for (const [name, signature] of SIGNATURES) if (hasSignature(bytes, signature, 0)) return { kind: "image", signature: name };
    if (hasSignature(bytes, RIFF, 0) && hasSignature(bytes, WEBP, 8)) return { kind: "image", signature: "webp" };
    return { kind: "bytes", signature: null };
  }
  const sniffBodyKind = (bytes) => sniffBodyKindExplained(bytes).kind;

  // http_body_is_parsed
  const httpBodyIsParsed = (kind) => kind === "json" || kind === "image" || kind === "text";

  // ---------- src/text/utf8_text.cpp ----------

  // decode_strict — 그 자리에서 읽은 길이. 0이면 잘못된 자리다 (과잉 인코딩·surrogate·0x10FFFF 초과 포함).
  function decodeStrictLength(t, offset) {
    const available = t.length - offset;
    const lead = t[offset];
    let length = 0;
    let codepoint = 0;
    let minimum = 0;
    if (lead < 0x80) return 1;
    if ((lead & 0xe0) === 0xc0) {
      length = 2;
      codepoint = lead & 0x1f;
      minimum = 0x80;
    } else if ((lead & 0xf0) === 0xe0) {
      length = 3;
      codepoint = lead & 0x0f;
      minimum = 0x800;
    } else if ((lead & 0xf8) === 0xf0) {
      length = 4;
      codepoint = lead & 0x07;
      minimum = 0x10000;
    } else return 0;
    if (available < length) return 0;
    for (let i = 1; i < length; i++) {
      const b = t[offset + i];
      if ((b & 0xc0) !== 0x80) return 0;
      codepoint = (codepoint << 6) | (b & 0x3f);
    }
    if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
    return length;
  }

  // utf8_is_valid
  function utf8IsValid(t) {
    let offset = 0;
    while (offset < t.length) {
      const length = decodeStrictLength(t, offset);
      if (length === 0) return false;
      offset += length;
    }
    return true;
  }

  // utf8_replace_invalid — 잘못된 바이트 하나마다 U+FFFD 하나.
  function utf8ReplaceInvalid(t) {
    const out = [];
    let offset = 0;
    let replaced = 0;
    while (offset < t.length) {
      const length = decodeStrictLength(t, offset);
      if (length === 0) {
        out.push(0xef, 0xbf, 0xbd);
        offset += 1;
        replaced++;
        continue;
      }
      for (let i = 0; i < length; i++) out.push(t[offset + i]);
      offset += length;
    }
    return { bytes: out, replaced };
  }

  // ---------- src/net/http_body.cpp ----------

  const BOM = [0xef, 0xbb, 0xbf];
  const withoutBom = (bytes) => (hasSignature(bytes, BOM, 0) ? bytes.slice(3) : bytes); // without_bom

  const UTF8_NAMES = ["utf-8", "utf8", "us-ascii", "ascii"].map(ascii);
  const isUtf8Charset = (charset) => charset.length === 0 || UTF8_NAMES.some((n) => same(charset, n)); // is_utf8_charset

  // json_nesting_depth — 문자열 안의 괄호는 세지 않는다.
  function jsonNestingDepth(bytes) {
    let depth = 0;
    let deepest = 0;
    let inString = false;
    let escaped = false;
    for (const b of bytes) {
      if (inString) {
        if (escaped) escaped = false;
        else if (b === 0x5c) escaped = true;
        else if (b === 0x22) inString = false;
        continue;
      }
      if (b === 0x22) inString = true;
      else if (b === 0x5b || b === 0x7b) deepest = Math.max(deepest, ++depth);
      else if ((b === 0x5d || b === 0x7d) && depth > 0) --depth;
    }
    return deepest;
  }

  // fill_text의 "No transcoder" 글: ASCII 밖의 바이트는 "?"로 눕힌다.
  const printable = (bytes) => bytes.map((b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : "?")).join("");

  const DEFAULTS = { assumeKind: null, htmlAsText: false, maxJsonBytes: 16 * 1024 * 1024, maxJsonDepth: 256, transcoder: true };

  // fill_text — UTF-8 갈래는 그대로 옮겼다. 그 밖의 charset은 갈고리를 부른다는 것만 답한다.
  function fillText(out, bytes, media, options) {
    if (isUtf8Charset(media.charset)) {
      const view = withoutBom(bytes);
      if (utf8IsValid(view)) {
        out.text = view;
        out.fill = "utf8";
        return;
      }
      const replaced = utf8ReplaceInvalid(view);
      out.text = replaced.bytes;
      out.replaced = replaced.replaced;
      out.parseError = "The body is not valid UTF-8; invalid bytes were replaced.";
      out.fill = "utf8-replaced";
      return;
    }
    if (!options.transcoder) {
      out.parseError = "No transcoder for charset '" + printable(media.charset) + "'.";
      out.fill = "no-transcoder";
      return;
    }
    out.fill = "transcode"; // 갈고리(기본 codepage_text_transcoder)의 결과는 옮기지 않았다
  }

  // fill_json — 크기와 깊이 검사만 옮겼다. 그 뒤의 nlohmann 파싱은 옮기지 않았다.
  function fillJson(out, bytes, options) {
    if (bytes.length > options.maxJsonBytes) {
      out.parseError = "The JSON body is larger than the parse limit (" + bytes.length + " > " + options.maxJsonBytes + " bytes).";
      out.fill = "json-too-large";
      return;
    }
    const limit = options.maxJsonDepth < 0 ? 0 : options.maxJsonDepth;
    out.depth = jsonNestingDepth(withoutBom(bytes));
    if (out.depth > limit) {
      out.parseError = "The JSON body is nested deeper than the limit (" + limit + " levels).";
      out.fill = "json-too-deep";
      return;
    }
    out.fill = "json-parse";
  }

  // decode_http_body(bytes, content_type, options[, transcode])
  //   → { kind, source, fill, text, parseError, … }
  //   fill: "none" | "utf8" | "utf8-replaced" | "transcode" | "no-transcoder" | "json-parse" | "json-too-large" | "json-too-deep" | "image" | "html"
  function decodeHttpBody(bytes, media, options) {
    const o = Object.assign({}, DEFAULTS, options || {});
    const out = { kind: "empty", source: "absent", fill: "none", text: null, parseError: "" };
    if (bytes.length === 0) return out; // 비어 있음이 형식 글보다 먼저다
    // decide_kind
    if (o.assumeKind != null) {
      out.kind = o.assumeKind;
      out.source = "assumed";
    } else if (mediaTypeInvitesSniffing(media)) {
      out.kind = sniffBodyKind(bytes);
      out.source = "sniffed";
    } else {
      out.kind = classifyMediaType(media);
      out.source = "declared";
    }
    if (out.kind === "json") fillJson(out, bytes, o);
    else if (out.kind === "image") out.fill = "image";
    else if (out.kind === "text") fillText(out, bytes, media, o);
    else if (out.kind === "html") {
      if (o.htmlAsText) fillText(out, bytes, media, o);
      else out.fill = "html";
    }
    return out;
  }

  window.LuilHttpBody = {
    KIND,
    SOURCE,
    bytesOf,
    parseMediaType,
    classifyMediaType,
    classifyMediaTypeExplained,
    mediaTypeInvitesSniffing,
    invitesSniffingReason,
    sniffBodyKind,
    sniffBodyKindExplained,
    httpBodyIsParsed,
    utf8IsValid,
    utf8ReplaceInvalid,
    withoutBom,
    isUtf8Charset,
    jsonNestingDepth,
    decodeHttpBody,
  };
})();

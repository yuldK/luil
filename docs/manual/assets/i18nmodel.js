/* luil 설명서 — 다국어 실습의 논리 이식 (DOM 없음)
 *
 * 원본: src/text/string_table.cpp (커밋 563a855)
 *   folded_tag_unit·same_language_tag·parent_language_tag  → foldUnit·sameLanguageTag·parentLanguageTag (:21-26, :219-235)
 *   find_language·select_language (select_strings)          → findLanguage·selectLanguage (:28-50, :187-190)
 *   plural_category_for                                     → pluralCategoryFor (:118-129)
 *   find_argument·append_number·append_argument·fill_arguments → findArgument·fillSegments·fillArguments (:52-60, :62-115)
 *   string_lookup::text·format                              → text·format (:144-172)
 * 수(int64)는 BigInt다. 글은 UTF-16 code unit으로 다루지만 특별한 글자({ } - _ A-Z)가 모두 ASCII라 UTF-8 바이트로 다루는 원본과 결과가 같다.
 * DATA는 시험 표(tests/strings)를 생성기로 지은 test_strings.h를 컴파일한 프로그램이 낸 값이다. 라이브러리 표는 언어 태그만 싣는다.
 * 대조: 설명서를 쓸 때 이 커밋의 원본을 링크해 컴파일한 대조기의 출력(고르기 4,013, 복수 1,230, text 45, format 12,015, 깨진 괄호 6,000)과 견줬다. 대조기는 저장소에 없다.
 */
(function (root) {
  "use strict";
  const DATA = {"source":"tests/strings/*.json → cmake/generate_strings.cmake (BASE en, NAMESPACE test_strings)","keys":["braces","date_line","echo","file_summary","files_found","files_selected","greeting","number","save"],"languages":[{"tag":"en","plural":"one","messages":[["","{{{value}}} and {{value}}"],["","{month_name} {day}, {year}"],["","{word} and {word}"],["a file","{count} files"],["","{count} files found"],["{count} file selected","{count} files selected"],["","Hello, {name} of {place}"],["","n={number}"],["","Save"]]},{"tag":"fr","plural":"one","messages":[["","{{{value}}} et {{value}}"],["","{day} {month_name} {year}"],["","{word} et {word}"],["un fichier","{count} fichiers"],["{count} fichier","{count} fichiers"],["{count} fichier","{count} fichiers"],["","Bonjour {name} de {place}"],["","n={number}"],["","Enregistrer"]]},{"tag":"ko","plural":"other","messages":[["","{{{value}}} 그리고 {{value}}"],["","{year}년 {month}월 {day}일"],["","{word} 그리고 {word}"],["","파일 {count}개"],["","파일 {count}개 찾음"],["","파일 {count}개 선택됨"],["","{place}의 {name}님 안녕하세요"],["","수={number}"],["","저장"]]},{"tag":"zh","plural":"other","messages":[["","{{{value}}} {{value}}"],["","{year}-{month}-{day}"],["","{word} {word}"],["","{count} 个文件"],["","找到 {count} 个文件"],["","已选择 {count} 个文件"],["","{place} 的 {name}"],["","n={number}"],["","保存"]]},{"tag":"zh-Hant","plural":"other","messages":[["","{{{value}}} {{value}}"],["","{year}-{month}-{day}"],["","{word} {word}"],["","{count} 個檔案"],["","找到 {count} 個檔案"],["","已選取 {count} 個檔案"],["","{place} 的 {name}"],["","n={number}"],["","儲存"]]}],"library":[{"tag":"ko","plural":"other"},{"tag":"en","plural":"one"}]};

  // plural_category의 칸 차례다 (string_table.h:21-29). 생성기가 채우는 칸은 one(1)과 other(5)뿐이다.
  const CATEGORY = ["zero", "one", "two", "few", "many", "other"];
  const OTHER = 5;

  // folded_tag_unit: '_'는 '-', A~Z는 소문자. 그 밖은 그대로.
  function foldUnit(code) {
    if (code === 0x5f) return 0x2d;
    return code >= 0x41 && code <= 0x5a ? code + 0x20 : code;
  }
  function sameLanguageTag(left, right) {
    if (left.length !== right.length) return false;
    for (let i = 0; i < left.length; i++) if (foldUnit(left.charCodeAt(i)) !== foldUnit(right.charCodeAt(i))) return false;
    return true;
  }
  // find_last_of("-_") 앞까지. 없으면 빈 글.
  function parentLanguageTag(tag) {
    const cut = Math.max(tag.lastIndexOf("-"), tag.lastIndexOf("_"));
    return cut < 0 ? "" : tag.slice(0, cut);
  }
  // 태그와 끝을 하나씩 뗀 태그를 차례로 찾는다. trace가 있으면 견준 태그마다 [태그, 맞은 차례|null]을 담는다.
  function findLanguage(languages, tag, trace) {
    for (; tag !== ""; tag = parentLanguageTag(tag)) {
      for (let index = 0; index < languages.length; index++) {
        if (sameLanguageTag(languages[index].tag, tag)) {
          if (trace) trace.push([tag, index]);
          return index;
        }
      }
      if (trace) trace.push([tag, null]);
    }
    return null;
  }
  // select_language: 선호 차례로 찾고, 없으면 0(첫 언어).
  function selectLanguage(languages, preferences, steps) {
    for (const preference of preferences) {
      const trace = steps ? [] : null;
      const found = findLanguage(languages, preference, trace);
      if (steps) steps.push({ preference, tried: trace, found });
      if (found !== null) return found;
    }
    return 0;
  }

  // plural_category_for: one 규칙은 1과 -1이 one. 절댓값을 짓지 않는다 (INT64_MIN).
  function pluralCategoryFor(rule, count) {
    if (rule === "one") return count === 1n || count === -1n ? "one" : "other";
    return "other";
  }

  // 인자는 { name, value }이고 value는 글(string)이거나 정수(BigInt)다. 같은 이름이면 앞의 것이다.
  function findArgument(args, name) {
    for (const a of args) if (a.name === name) return a;
    return null;
  }
  // 정수는 십진 숫자 글자다 (to_chars: 자릿점 없음, 음수 '-').
  function argumentText(a) {
    return typeof a.value === "bigint" ? a.value.toString() : String(a.value);
  }
  // fill_arguments를 조각으로 짓는다: lit(글자 그대로), arg(인자 값), missing(인자가 없어 남긴 {이름}).
  function fillSegments(form, args) {
    const out = [];
    const push = (kind, text) => {
      const last = out[out.length - 1];
      if (kind === "lit" && last && last.kind === "lit") last.text += text;
      else out.push({ kind, text });
    };
    let index = 0;
    while (index < form.length) {
      const unit = form[index];
      const doubled = index + 1 < form.length && form[index + 1] === unit;
      if ((unit === "{" || unit === "}") && doubled) {
        push("lit", unit);
        index += 2;
        continue;
      }
      if (unit === "{") {
        let close = -1;
        for (let j = index + 1; j < form.length; j++) {
          if (form[j] === "{" || form[j] === "}") { close = j; break; }
        }
        if (close !== -1 && form[close] === "}") {
          const placeholder = form.slice(index, close + 1);
          const argument = findArgument(args, placeholder.slice(1, -1));
          if (argument) push("arg", argumentText(argument));
          else push("missing", placeholder);
          index = close + 1;
          continue;
        }
      }
      push("lit", unit);
      index += 1;
    }
    return out;
  }
  function fillArguments(form, args) {
    return fillSegments(form, args).map((s) => s.text).join("");
  }

  // 시험 표의 언어 하나 (messages는 [one, other] 짝). forms(…)는 원본의 6칸 차례로 돌려준다.
  function forms(language, key) {
    const pair = language.messages[key];
    return ["", pair[0], "", "", "", pair[1]];
  }
  // string_lookup::text: other 꼴, 범위 밖 키는 빈 글.
  function text(language, key) {
    return key < language.messages.length ? forms(language, key)[OTHER] : "";
  }
  // string_lookup::format. detail을 주면 고른 칸과 물러섬을 적는다.
  function format(language, key, args, detail) {
    if (key >= language.messages.length) return "";
    const message = forms(language, key);
    let formIndex = OTHER;
    let reason = "none";
    let category = null;
    const count = findArgument(args, "count");
    if (count) {
      if (typeof count.value === "bigint") {
        category = pluralCategoryFor(language.plural, count.value);
        formIndex = CATEGORY.indexOf(category);
        reason = "count";
      } else {
        reason = "text";
      }
    }
    const form = message[formIndex];
    const used = form === "" ? OTHER : formIndex;
    const segments = fillSegments(message[used], args);
    if (detail) Object.assign(detail, { reason, category, formIndex, usedIndex: used, form: message[used], segments });
    return segments.map((s) => s.text).join("");
  }

  // 입력 칸의 글을 int64로 읽는다 — 이 문서의 입력 처리이고 luil의 것이 아니다. 아니면 null.
  const INT64_MIN = -(2n ** 63n);
  const INT64_MAX = 2n ** 63n - 1n;
  function parseInt64(textValue) {
    const t = String(textValue).trim();
    if (!/^[+-]?[0-9]+$/.test(t)) return null;
    const n = BigInt(t);
    return n < INT64_MIN || n > INT64_MAX ? null : n;
  }

  const api = { DATA, CATEGORY, OTHER, INT64_MIN, INT64_MAX, foldUnit, sameLanguageTag, parentLanguageTag, findLanguage, selectLanguage, pluralCategoryFor, findArgument, fillSegments, fillArguments, forms, text, format, parseInt64 };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.LuilI18n = api;
})(typeof window !== "undefined" ? window : globalThis);

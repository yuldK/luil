#!/usr/bin/env python3
"""설명서 전체 검색 색인과 문서 지도를 만든다.

    python3 docs/manual/tools/gen_search.py

만드는 것:
    assets/search-index.js  window.LUIL_SEARCH  — 문서 조각·자주 묻는 질문·API 기호 (위 검색 칸이 처음 쓸 때 싣는다)
    assets/doc-map.js       window.LUIL_DOCMAP  — 공개 헤더 → 그 헤더를 다루는 문서 (API 찾아보기가 잇는다)

문서 목록과 제목은 assets/manual.js의 DOCS가 정본이다. API 기호는 gen_api.py가 먼저 만든 api-index.js에서 읽는다.
헤더를 다루는 문서는 manifest.json의 headers 표가 정본이고, 표에 없으면 문서가 data-src로 인용한 헤더로 채운다.
"""
import html
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANUAL = HERE.parent
ASSETS = MANUAL / "assets"


def docs_pages():
    js = (ASSETS / "manual.js").read_text(encoding="utf-8")
    pages = []
    for m in re.finditer(r'\{ id: "([^"]+)", file: "([^"]+)", title: "([^"]+)" \}', js):
        pages.append({"id": m.group(1), "file": m.group(2), "title": m.group(3)})
    return pages


TAG = re.compile(r"<[^>]+>")


def text_of(fragment):
    fragment = re.sub(r"<script\b[^>]*>.*?</script>", " ", fragment, flags=re.S)
    fragment = re.sub(r"<style\b[^>]*>.*?</style>", " ", fragment, flags=re.S)
    fragment = re.sub(r"<(br|/p|/li|/h\d|/div|/tr|/td|/th|/dd|/dt)\b[^>]*>", " ", fragment)
    t = TAG.sub("", fragment)
    t = html.unescape(t)
    return re.sub(r"\s+", " ", t).strip()


def main():
    pages = docs_pages()
    entries = []
    header_pages = {}

    manifest = {}
    mpath = HERE / "manifest.json"
    if mpath.exists():
        manifest = json.loads(mpath.read_text(encoding="utf-8"))

    for pg in pages:
        f = MANUAL / pg["file"]
        if not f.exists() or pg["id"] == "api":
            continue
        src = f.read_text(encoding="utf-8")
        body = src.split("<body", 1)[1] if "<body" in src else src
        h1 = re.search(r"<h1[^>]*>(.*?)</h1>", body, re.S)
        summary = re.search(r'<p class="summary">(.*?)</p>', body, re.S)
        entries.append({"k": "page", "p": pg["file"], "s": "", "pt": "설명서", "st": pg["title"], "t": text_of(summary.group(1)) if summary else ""})
        for m in re.finditer(r'<section class="sec" id="([^"]+)"([^>]*)>(.*?)</section>', body, re.S):
            sid, attrs, inner = m.group(1), m.group(2), m.group(3)
            toc = re.search(r'data-toc="([^"]+)"', attrs)
            h2 = re.search(r"<h2[^>]*>(.*?)</h2>", inner, re.S)
            title = html.unescape(toc.group(1)) if toc else (text_of(h2.group(1)) if h2 else sid)
            faqs = list(re.finditer(r'<details class="faq"[^>]*>\s*<summary>(.*?)</summary>(.*?)</details>', inner, re.S))
            rest = re.sub(r'<details class="faq"[^>]*>.*?</details>', " ", inner, flags=re.S)
            # 코드 발췌는 경로만 남긴다 (원문은 API 찾아보기가 따로 찾는다)
            paths = " ".join(sorted(set(re.findall(r'data-src="([^":]+)', inner))))
            text = text_of(rest)
            if h2:
                text = text.replace(text_of(h2.group(1)), "", 1).strip()
            entries.append({"k": "sec", "p": pg["file"], "s": sid, "pt": pg["title"], "st": title, "t": (text + (" " + paths if paths else "")).strip()})
            # data-kind="case" 절(개념 문서의 "사례로 확인하기")은 질문이 아니라 사례라서 따로 표시하고 검색에서 낮게 친다
            case = 'data-kind="case"' in attrs
            for q in faqs:
                entries.append({"k": "case" if case else "faq", "p": pg["file"], "s": sid, "pt": pg["title"] + (" · 사례" if case else " · 자주 묻는 질문"), "st": text_of(q.group(1)), "t": text_of(q.group(2))})
        # 헤더 → 문서 (인용으로 짐작)
        for hp in sorted(set(re.findall(r'data-src="(include/luil/[^":]+)', src))):
            header_pages.setdefault(hp, [])
            if not any(x["file"] == pg["file"] for x in header_pages[hp]):
                header_pages[hp].append({"file": pg["file"], "title": pg["title"]})

    # manifest가 정한 것이 앞에 선다
    for hp, ids in (manifest.get("headers") or {}).items():
        lst = []
        for pid in ids:
            pg = next((p for p in pages if p["id"] == pid), None)
            if pg and (MANUAL / pg["file"]).exists():
                lst.append({"file": pg["file"], "title": pg["title"]})
        for x in header_pages.get(hp, []):
            if not any(y["file"] == x["file"] for y in lst):
                lst.append(x)
        header_pages[hp] = lst

    # API 기호
    api_js = (ASSETS / "api-index.js").read_text(encoding="utf-8")
    api = json.loads(api_js[api_js.index("{") : api_js.rstrip().rindex("}") + 1])
    syms = api["symbols"]
    # API 기호는 짧은 줄로 싣는다: [이름(luil:: 뗌), 종류 글자, 헤더 번호, 주석 앞부분]
    # 주소(slug)는 manual.js가 같은 차례·같은 규칙으로 다시 만든다 (api.html과 같다).
    KCODE = {"struct": "S", "class": "C", "union": "U", "enum": "E", "enumerator": "e", "function": "f", "method": "m", "constructor": "c", "field": "v", "alias": "a", "constant": "k", "concept": "K"}
    api_rows = []
    for s in syms:
        top = s["p"] is None
        limit = 140 if top else 48
        c = re.sub(r"\s+", " ", s["c"]).strip()
        api_rows.append([s["q"], KCODE.get(s["k"], "?"), s["h"], c[:limit] + ("…" if len(c) > limit else "")])
    docs_rows = [[e["k"], e["p"], e["s"], e["pt"], e["st"], e["t"]] for e in entries]
    payload = {"headers": [h["p"] for h in api["headers"]], "docs": docs_rows, "api": api_rows}
    (ASSETS / "search-index.js").write_text(
        "/* tools/gen_search.py가 만든다. 손으로 고치지 않는다. */\nwindow.LUIL_SEARCH = " + json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + ";\n",
        encoding="utf-8",
    )
    (ASSETS / "doc-map.js").write_text(
        "/* tools/gen_search.py가 만든다. 손으로 고치지 않는다. */\nwindow.LUIL_DOCMAP = " + json.dumps({"headerPages": header_pages}, ensure_ascii=False, separators=(",", ":")) + ";\n",
        encoding="utf-8",
    )
    kinds = {}
    for e in entries:
        kinds[e["k"]] = kinds.get(e["k"], 0) + 1
    print("docs", len(entries), kinds, "api", len(api_rows), "headers mapped", len(header_pages))


if __name__ == "__main__":
    main()

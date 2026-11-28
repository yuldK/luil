#!/usr/bin/env python3
"""문서 안의 자동 영역을 다시 채운다.

    python3 docs/manual/tools/gen_pages.py

<!-- auto:headers --> … <!-- /auto:headers -->
    manifest.json이 이 문서에 배정한 공개 헤더마다 설명과 대표 기호(API 찾아보기로 가는 링크)를 늘어놓는다.
    영역이 없는 문서에는 doc-body의 끝에 새로 넣는다.

API 주소(slug) 규칙은 assets/manual.js의 apiSlugs와 같다. 둘 중 하나를 고치면 다른 쪽도 고친다.
"""
import html
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANUAL = HERE.parent
REPO = "https://github.com/yuldK/luil"

KCODE = {"struct": ("S", "t"), "class": ("C", "t"), "union": ("U", "t"), "enum": ("E", "e"), "function": ("f", "f"), "alias": ("=", "t"), "constant": ("k", ""), "concept": ("K", "t")}
KIND = {"struct": "구조체", "class": "클래스", "union": "공용체", "enum": "열거형", "function": "함수", "alias": "별칭", "constant": "상수", "concept": "개념"}


def api_slugs(qnames):
    used, out = set(), []
    for i, q in enumerate(qnames):
        base = re.sub(r"[^A-Za-z0-9-]", "", q.removeprefix("luil::").replace("::", "--").replace("_", "-")) or f"sym-{i}"
        slug, k = base, 2
        while slug in used:
            slug = f"{base}-{k}"
            k += 1
        used.add(slug)
        out.append(slug)
    return out


def main():
    manifest = json.loads((HERE / "manifest.json").read_text(encoding="utf-8"))
    js = (MANUAL / "assets" / "manual.js").read_text(encoding="utf-8")
    docs = {m.group(1): m.group(2) for m in re.finditer(r'\{ id: "([^"]+)", file: "([^"]+)", title: "[^"]+" \}', js)}
    commit = re.search(r'commit: "([0-9a-f]{40})"', js).group(1)
    t = (MANUAL / "assets" / "api-index.js").read_text(encoding="utf-8")
    api = json.loads(t[t.index("{") : t.rstrip().rindex("}") + 1])
    syms = api["symbols"]
    slugs = api_slugs([s["q"] for s in syms])
    by_header = {}
    for s in syms:
        if s["p"] is None:
            by_header.setdefault(api["headers"][s["h"]]["p"], []).append(s)
    hdesc = {h["p"]: h["d"] for h in api["headers"]}

    pages_headers = {}
    for h, ids in manifest["headers"].items():
        for pid in ids:
            pages_headers.setdefault(pid, []).append(h)

    changed = 0
    for pid, file in docs.items():
        f = MANUAL / file
        if not f.exists() or pid in ("api", "home"):
            continue
        hs = sorted(pages_headers.get(pid, []))
        if not hs:
            continue
        rows = []
        for h in hs:
            tops = by_header.get(h, [])
            desc = hdesc.get(h) or ""
            links = []
            for s in tops[:40]:
                code, cls = KCODE.get(s["k"], ("?", ""))
                links.append(f'<a href="api.html#{slugs[s["i"]]}" title="{KIND.get(s["k"], s["k"])}"><span class="kb {cls}">{code}</span>{html.escape(s["n"])}</a>')
            more = f'<span class="more">외 {len(tops) - 40}개</span>' if len(tops) > 40 else ""
            rows.append(
                '          <div class="hdr">\n'
                f'            <div class="hdr-head"><a class="path" href="{REPO}/blob/{commit}/{h}" target="_blank" rel="noopener">{h}</a><span class="n">최상위 기호 {len(tops)}</span></div>\n'
                + (f'            <p class="hdr-desc">{html.escape(desc)}</p>\n' if desc else "")
                + (f'            <div class="hdr-syms">{"".join(links)}{more}</div>\n' if links else "")
                + "          </div>"
            )
        block = (
            "<!-- auto:headers — tools/gen_pages.py가 manifest.json에서 채운다. 손으로 고치지 않는다. -->\n"
            '      <section class="sec" id="headers" data-toc="관련 헤더">\n'
            '        <header class="sec-head">\n'
            "          <h2>이 문서가 다루는 헤더</h2>\n"
            '          <p class="lede">이 문서의 내용이 기대는 공개 헤더다. 기호를 누르면 API 찾아보기에서 주석과 선언 원문을 본다.</p>\n'
            "        </header>\n"
            '        <div class="hdr-list">\n' + "\n".join(rows) + "\n        </div>\n"
            "      </section>\n"
            "      <!-- /auto:headers -->"
        )
        s = f.read_text(encoding="utf-8")
        if "<!-- auto:headers" in s:
            new = re.sub(r"<!-- auto:headers.*?<!-- /auto:headers -->", lambda m: block, s, flags=re.S)
        else:
            marker = '\n    </div>\n    <nav class="pager" data-auto'
            if marker not in s:
                print(f"건너뜀 {file}: doc-body 끝을 찾지 못했다")
                continue
            new = s.replace(marker, "\n\n      " + block + marker, 1)
        if new != s:
            f.write_text(new, encoding="utf-8")
            changed += 1
    print(f"관련 헤더 영역을 고친 문서 {changed}개")


if __name__ == "__main__":
    main()

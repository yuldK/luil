#!/usr/bin/env python3
"""설명서가 공개 헤더를 빠짐없이 덮는지 검사한다.

    python3 docs/manual/tools/check_coverage.py [--strict]

- include/luil/ 아래의 모든 헤더가 manifest.json의 headers 표에 있어야 한다 (없으면 "배정 안 됨").
- 배정된 문서 id는 manual.js의 DOCS에 있고 파일이 있어야 한다. manifest의 planned에만 있으면 "쓸 차례".
- 문서가 있는 헤더라도, 그 문서 본문이 헤더 파일 이름이나 헤더의 대표 기호를 한 번도 말하지 않으면 "말하지 않음"으로 알린다.
--strict면 쓸 차례·말하지 않음도 실패로 친다. 배정 안 됨은 늘 실패다.
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANUAL = HERE.parent
ROOT = MANUAL.parents[1]


def main():
    strict = "--strict" in sys.argv
    manifest = json.loads((HERE / "manifest.json").read_text(encoding="utf-8"))
    js = (MANUAL / "assets" / "manual.js").read_text(encoding="utf-8")
    docs = {m.group(1): m.group(2) for m in re.finditer(r'\{ id: "([^"]+)", file: "([^"]+)", title: "[^"]+" \}', js)}
    api_js = (MANUAL / "assets" / "api-index.js").read_text(encoding="utf-8")
    api = json.loads(api_js[api_js.index("{") : api_js.rstrip().rindex("}") + 1])
    tops = {}
    for s in api["symbols"]:
        if s["p"] is None:
            tops.setdefault(api["headers"][s["h"]]["p"], []).append(s["n"])

    headers = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "include" / "luil").rglob("*.h"))
    unmapped, todo, silent = [], {}, []
    page_text = {}
    for h in headers:
        ids = manifest["headers"].get(h)
        if not ids:
            unmapped.append(h)
            continue
        for pid in ids:
            if pid not in docs or not (MANUAL / docs[pid]).exists():
                todo.setdefault(pid, []).append(h)
                continue
            if pid not in page_text:
                t = (MANUAL / docs[pid]).read_text(encoding="utf-8")
                page_text[pid] = re.sub(r"<[^>]+>", " ", t)
            t = page_text[pid]
            name = h.rsplit("/", 1)[1]
            syms = tops.get(h, [])
            if name not in t and not any(re.search(r"\b" + re.escape(sym) + r"\b", t) for sym in syms):
                silent.append(f"{h} → {pid}")
    for h in unmapped:
        print("배정 안 됨:", h)
    for pid, hs in sorted(todo.items()):
        title = manifest.get("planned", {}).get(pid, {}).get("title", "?")
        print(f"쓸 차례: {pid} ({title}) — 헤더 {len(hs)}개")
    for x in silent:
        print("말하지 않음:", x)
    covered = len([h for h in headers if h in manifest["headers"] and any(pid in docs and (MANUAL / docs[pid]).exists() for pid in manifest["headers"][h])])
    print(f"헤더 {len(headers)}개 중 문서가 있는 것 {covered}개, 배정 안 됨 {len(unmapped)}, 쓸 차례 문서 {len(todo)}, 말하지 않음 {len(silent)}")
    fail = bool(unmapped) or (strict and (todo or silent))
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()

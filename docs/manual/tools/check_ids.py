#!/usr/bin/env python3
"""설명서 본문의 <code> 안 식별자가 저장소에 실제로 있는지 검사한다.

    python3 docs/manual/tools/check_ids.py [page.html ...]

- 본문의 <code>…</code>에서 C++ 식별자처럼 생긴 낱말(밑줄이나 ::가 든 것, 또는 뒤에 ()가 붙은 것)을 뽑는다.
- include/·src/·examples/·tests/에 그 낱말이 하나도 없으면 "없는 이름"이다. 지어낸 API를 잡는다.
- `형식::멤버` 꼴이고 형식이 API 색인(assets/api-index.js)에 있으면, 멤버가 그 형식에 실제로 있는지도 본다.
- 코드 발췌(<script type="text/plain">)와 페이지 스크립트는 보지 않는다. 발췌는 fill_excerpts.py --check가 원문과 대조한다.
문제가 있으면 1로 끝난다.
"""
import html
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANUAL = HERE.parent
ROOT = MANUAL.parents[1]

# 저장소에 없어도 되는 낱말: 표준 라이브러리, 플랫폼 API, 웹, 일반 낱말
ALLOW = set("""
std size_t int32_t uint32_t uint64_t int64_t uint8_t char8_t char32_t u8string u8string_view string_view nullptr noexcept constexpr
static_assert unique_ptr shared_ptr make_unique make_shared optional nullopt variant function vector span chrono steady_clock
condition_variable mutex atomic memory_order_relaxed memory_order_acquire memory_order_release jthread stop_token
try_receive post wait_limit
""".split())
ALLOW_PREFIX = ("std::", "Sk", "WM_", "NS", "UI", "AX", "UIA_", "VK_", "IDXGI", "ID3D", "D3D", "DXGI", "Win", "CF", "JNI", "AInput", "ANativeWindow", "GameActivity", "HWND", "TSF", "ITf", "Uia", "kAX", "LUIL_", "CMAKE_", "_WIN32", "__", "Catch", "REQUIRE", "TEST_CASE")


def repo_words():
    words = set()
    for d in ("include", "src", "examples", "tests", "cmake", "scripts"):
        for p in (ROOT / d).rglob("*"):
            if p.is_file() and p.suffix in (".h", ".hpp", ".cpp", ".mm", ".m", ".cmake", ".txt", ".json", ".in", ".py", ".ps1", ".java", ".kts"):
                try:
                    words.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", p.read_text(encoding="utf-8", errors="replace")))
                except OSError:
                    pass
    for p in ROOT.glob("CMake*.json"):
        words.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", p.read_text(encoding="utf-8", errors="replace")))
    words.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")))
    return words


def api_members():
    p = MANUAL / "assets" / "api-index.js"
    if not p.exists():
        return {}, set()
    t = p.read_text(encoding="utf-8")
    api = json.loads(t[t.index("{") : t.rstrip().rindex("}") + 1])
    syms = api["symbols"]
    members = {}
    types = set()
    for s in syms:
        if s["k"] in ("struct", "class", "enum", "union"):
            types.add(s["n"])
            members.setdefault(s["n"], set()).update(syms[i]["n"] for i in s.get("m", []))
    return members, types


# class="free"는 본문이 일부러 지어 쓴 예시 이름이다 (앱 쪽 함수 이름, 가상의 intent 따위).
CODE = re.compile(r"<code\b(?![^>]*class=\"[^\"]*\bfree\b)[^>]*>(.*?)</code>", re.S)
TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_~][A-Za-z0-9_]*)*(?:\(\))?")


def candidates(code_text):
    t = html.unescape(re.sub(r"<[^>]+>", "", code_text)).strip()
    if not t or re.search(r"\.(h|hpp|cpp|mm|md|json|cmake|ps1|txt|js|html)\b|/", t):
        return []
    out = []
    for m in TOKEN.finditer(t):
        tok = m.group(0)
        if t[m.end() : m.end() + 1] == "*":
            continue  # take_* 같은 묶음 표기
        bare = tok[:-2] if tok.endswith("()") else tok
        if "_" in bare or "::" in bare or tok.endswith("()"):
            out.append(bare)
    return out


def repo_text():
    chunks = []
    for d in ("include", "src", "examples", "tests"):
        for p in (ROOT / d).rglob("*"):
            if p.is_file() and p.suffix in (".h", ".hpp", ".cpp", ".mm"):
                chunks.append(p.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(chunks)


def main():
    files = [Path(a) for a in sys.argv[1:]] or sorted(p for p in MANUAL.glob("*.html") if p.name != "api.html")
    words = repo_words()
    members, types = api_members()
    text = repo_text()
    bad = 0
    for f in files:
        s = f.read_text(encoding="utf-8")
        body = s.split("<body", 1)[-1]
        body = re.sub(r"<script\b[^>]*>.*?</script>", " ", body, flags=re.S)
        body = re.sub(r"<style\b[^>]*>.*?</style>", " ", body, flags=re.S)
        problems = []
        for m in CODE.finditer(body):
            for tok in candidates(m.group(1)):
                if tok.startswith(ALLOW_PREFIX) or tok in ALLOW:
                    continue
                parts = [p for p in tok.split("::") if p]
                if parts and parts[0] == "luil":
                    parts = parts[1:]
                missing = [p for p in parts if p not in words and p.lstrip("~") not in words]
                if missing:
                    problems.append(f"없는 이름 {tok} ({', '.join(missing)})")
                    continue
                # 형식::멤버 — 형식이 색인에 있고 멤버 목록이 있으면 대조한다
                if len(parts) >= 2 and "::".join(parts[-2:]) in text:
                    continue  # 원문에 그대로 있는 한정 이름 (비공개 멤버 포함)
                if len(parts) >= 2 and parts[-2] in members and members[parts[-2]] and parts[-1] not in members[parts[-2]] and parts[-1] not in types:
                    problems.append(f"{parts[-2]}에 {parts[-1]}이(가) 없다 ({tok})")
        for p in sorted(set(problems)):
            print(f"{f.name}: {p}")
        bad += len(set(problems))
    print(f"검사한 문서 {len(files)}개, 문제 {bad}개")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

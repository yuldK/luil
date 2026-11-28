#!/usr/bin/env python3
"""공개 헤더(include/luil/**/*.h)를 libclang으로 읽어 설명서의 API 색인을 만든다.

    pip install "clang==18.1.8"          # 시스템 libclang과 큰 판이 같은 파이썬 바인딩
    python3 docs/manual/tools/gen_api.py [--libclang /usr/lib/x86_64-linux-gnu/libclang-18.so.1]

만드는 것:
    docs/manual/assets/api-index.js   window.LUIL_API      — 기호(형식·함수·필드·열거값…)와 주석, 줄 범위
    docs/manual/assets/api-src.js     window.LUIL_API_SRC  — 헤더 원문 (API 문서가 선언을 원문 그대로 보인다)

빌드 산출물이 없어도 된다. 생성 헤더(luil/generated/*.h)는 cmake/*.h.in에서 그 자리에서 채우고,
공개 헤더가 include하는 Skia·nlohmann 머리는 빈 흉내로 넣는다(형식 이름만 있으면 선언을 읽을 수 있다).
"""
import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
INCLUDE = ROOT / "include"
OUT_DIR = ROOT / "docs" / "manual" / "assets"

STUBS = {
    "include/core/SkRefCnt.h": "#pragma once\ntemplate <typename T> class sk_sp { public: sk_sp() = default; T* get() const { return nullptr; } explicit operator bool() const { return false; } };\n",
    "include/core/SkTypeface.h": '#pragma once\n#include "include/core/SkRefCnt.h"\nclass SkTypeface {};\n',
    "nlohmann/json_fwd.hpp": "#pragma once\nnamespace nlohmann { class json {}; }\n",
}


def make_stub_dir(tmp: Path):
    gen = tmp / "luil" / "generated"
    gen.mkdir(parents=True)
    accent = (ROOT / "cmake" / "accent_defaults.h.in").read_text(encoding="utf-8").replace("@LUIL_DEFAULT_ACCENT_ID@", "mint")
    (gen / "accent_defaults.h").write_text(accent, encoding="utf-8")
    m = re.search(r"project\(luil VERSION (\d+)\.(\d+)\.(\d+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    major, minor, patch = m.groups()
    ver = (ROOT / "cmake" / "version.h.in").read_text(encoding="utf-8")
    for k, v in {"PROJECT_VERSION_MAJOR": major, "PROJECT_VERSION_MINOR": minor, "PROJECT_VERSION_PATCH": patch, "PROJECT_VERSION": f"{major}.{minor}.{patch}"}.items():
        ver = ver.replace(f"@{k}@", v)
    (gen / "version.h").write_text(ver, encoding="utf-8")
    for rel, text in STUBS.items():
        p = tmp / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")


def clean_comment(raw):
    if not raw:
        return ""
    lines = []
    for ln in raw.splitlines():
        ln = ln.strip()
        if ln.startswith("///"):
            ln = ln[3:]
        elif ln.startswith("//"):
            ln = ln[2:]
        elif ln.startswith("/*"):
            ln = ln[2:]
        if ln.endswith("*/"):
            ln = ln[:-2]
        if ln.startswith(" "):
            ln = ln[1:]
        lines.append(ln.rstrip())
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines)


SRC_LINES = {}


def leading_comment(path, line, end_line):
    """선언 바로 위에 붙은 // 줄들과, 한 줄 선언이면 같은 줄 끝의 // 주석을 모은다.

    libclang의 raw_comment는 주석 없는 열거값에도 앞 열거값의 주석을 붙이므로 쓰지 않는다.
    이 저장소의 규약대로 빈 줄이 끼면 그 위의 주석은 이 선언의 것이 아니다.
    """
    lines = SRC_LINES[path]
    out = []
    i = line - 2
    while i >= 0:
        t = lines[i].strip()
        if t.startswith("//"):
            out.append(t)
            i -= 1
            continue
        break
    out.reverse()
    if line == end_line:
        code = lines[line - 1]
        m = re.search(r"//(.*)$", code)
        if m and '"' not in code[: m.start()]:
            out.append("//" + m.group(1))
    return clean_comment("\n".join(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--libclang", default="/usr/lib/x86_64-linux-gnu/libclang-18.so.1")
    args = ap.parse_args()

    import clang.cindex as ci

    ci.Config.set_library_file(args.libclang)
    CK = ci.CursorKind

    headers = sorted(p for p in (INCLUDE / "luil").rglob("*.h"))
    rel = {str(p.resolve()): p.relative_to(ROOT).as_posix() for p in headers}
    header_index = {r: i for i, r in enumerate(sorted(rel.values()))}
    sources = {r: (ROOT / r).read_bytes() for r in header_index}
    for r, b in sources.items():
        SRC_LINES[r] = b.decode("utf-8", "replace").replace("\r\n", "\n").split("\n")

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        make_stub_dir(tmp)
        tu_src = "".join(f'#include "{r[len("include/"):]}"\n' for r in sorted(rel.values()))
        index = ci.Index.create()
        tu = index.parse(
            "all_headers.cpp",
            args=["-x", "c++", "-std=c++20", "-fparse-all-comments", "-I", str(INCLUDE), "-I", str(tmp), "-Wno-everything"],
            unsaved_files=[("all_headers.cpp", tu_src)],
            options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES | ci.TranslationUnit.PARSE_INCOMPLETE,
        )
        errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
        for d in errors[:20]:
            print("diag:", d, file=sys.stderr)

        symbols = []
        by_key = {}

        def file_of(c):
            f = c.location.file
            return rel.get(str(Path(f.name).resolve())) if f else None

        def text_between(path, start, end):
            return sources[path][start:end].decode("utf-8", "replace")

        def signature(c, path):
            """선언의 머리. 몸통이 있으면 몸통 앞에서 자른다."""
            ext = c.extent
            start, end = ext.start.offset, ext.end.offset
            depth = 0
            seen_paren = False
            cut = end
            for t in c.get_tokens():
                s = t.spelling
                if s in "([":
                    depth += 1
                    seen_paren = seen_paren or s == "("
                elif s in ")]":
                    depth -= 1
                elif s == "{" and depth == 0 and seen_paren and c.kind in (CK.FUNCTION_DECL, CK.CXX_METHOD, CK.CONSTRUCTOR, CK.FUNCTION_TEMPLATE, CK.CONVERSION_FUNCTION):
                    cut = t.extent.start.offset
                    break
                elif s == ":" and depth == 0 and seen_paren and c.kind == CK.CONSTRUCTOR:
                    cut = t.extent.start.offset
                    break
            txt = text_between(path, start, cut).strip()
            return re.sub(r"\s+", " ", txt)

        def qname(c):
            parts = []
            p = c
            while p is not None and p.kind != CK.TRANSLATION_UNIT:
                if p.spelling:
                    parts.append(p.spelling)
                p = p.semantic_parent
            return "::".join(reversed(parts))

        KIND = {
            CK.STRUCT_DECL: "struct",
            CK.CLASS_DECL: "class",
            CK.UNION_DECL: "union",
            CK.CLASS_TEMPLATE: "class",
            CK.ENUM_DECL: "enum",
            CK.FUNCTION_DECL: "function",
            CK.FUNCTION_TEMPLATE: "function",
            CK.TYPE_ALIAS_DECL: "alias",
            CK.TYPEDEF_DECL: "alias",
            CK.TYPE_ALIAS_TEMPLATE_DECL: "alias",
            CK.VAR_DECL: "constant",
            CK.CONCEPT_DECL: "concept",
        }

        def add(c, kind, parent, path, sig=None):
            key = (parent, qname(c), kind)
            line = c.extent.start.line
            end = c.extent.end.line
            comment = leading_comment(path, c.extent.start.line, c.extent.end.line)
            if kind in ("function", "method", "constructor") and key in by_key:
                s = symbols[by_key[key]]
                s["s"].append(sig or signature(c, path))
                if comment and comment not in s["c"]:
                    s["c"] = (s["c"] + "\n\n" + comment).strip()
                s["e"] = max(s["e"], end)
                return s
            s = {
                "i": len(symbols),
                "n": c.spelling,
                "q": qname(c),
                "k": kind,
                "h": header_index[path],
                "l": line,
                "e": end,
                "c": comment,
                "s": [sig or signature(c, path)],
                "p": parent,
            }
            symbols.append(s)
            by_key[key] = s["i"]
            if parent is not None:
                symbols[parent].setdefault("m", []).append(s["i"])
            return s

        def visit_record(c, path, rec):
            is_class = c.kind == CK.CLASS_DECL or (c.kind == CK.CLASS_TEMPLATE and any(t.spelling == "class" for t in list(c.get_tokens())[:8]))
            access = "private" if is_class else "public"
            for m in c.get_children():
                if m.kind == CK.CXX_ACCESS_SPEC_DECL:
                    access = m.access_specifier.name.lower()
                    continue
                if access == "private":
                    continue
                if file_of(m) != path:
                    continue
                k = m.kind
                prot = access == "protected"
                if k == CK.FIELD_DECL:
                    s = add(m, "field", rec["i"], path, signature(m, path))
                    s["t"] = m.type.spelling
                elif k in (CK.CXX_METHOD, CK.FUNCTION_TEMPLATE, CK.CONVERSION_FUNCTION):
                    if m.spelling.startswith("operator") and m.spelling in ("operator=",):
                        continue
                    add(m, "method", rec["i"], path)
                elif k == CK.CONSTRUCTOR:
                    if m.is_copy_constructor() or m.is_move_constructor():
                        continue
                    add(m, "constructor", rec["i"], path)
                elif k in (CK.STRUCT_DECL, CK.CLASS_DECL, CK.CLASS_TEMPLATE, CK.UNION_DECL) and m.is_definition() and m.spelling:
                    r = add(m, KIND[k], rec["i"], path, "")
                    r["s"] = [record_head(m, path)]
                    visit_record(m, path, r)
                elif k == CK.ENUM_DECL and m.is_definition() and m.spelling:
                    visit_enum(m, path, rec["i"])
                elif k in (CK.TYPE_ALIAS_DECL, CK.TYPEDEF_DECL, CK.TYPE_ALIAS_TEMPLATE_DECL):
                    add(m, "alias", rec["i"], path)
                elif k == CK.VAR_DECL:
                    add(m, "constant", rec["i"], path)
                else:
                    continue
                if prot and symbols[-1]["p"] == rec["i"]:
                    symbols[-1]["x"] = "protected"

        def record_head(c, path):
            """struct 이름 : 기반 { 까지"""
            start = c.extent.start.offset
            for t in c.get_tokens():
                if t.spelling == "{":
                    return re.sub(r"\s+", " ", text_between(path, start, t.extent.start.offset).strip())
            return re.sub(r"\s+", " ", text_between(path, start, c.extent.end.offset).strip())

        def visit_enum(c, path, parent):
            e = add(c, "enum", parent, path, record_head(c, path))
            for v in c.get_children():
                if v.kind == CK.ENUM_CONSTANT_DECL:
                    s = add(v, "enumerator", e["i"], path, signature(v, path))
            return e

        def visit(cur):
            for c in cur.get_children():
                path = file_of(c)
                if c.kind == CK.NAMESPACE:
                    visit(c)
                    continue
                if path is None:
                    continue
                k = c.kind
                if k in (CK.STRUCT_DECL, CK.CLASS_DECL, CK.CLASS_TEMPLATE, CK.UNION_DECL):
                    if not c.is_definition() or not c.spelling:
                        continue
                    r = add(c, KIND[k], None, path, record_head(c, path))
                    visit_record(c, path, r)
                elif k == CK.ENUM_DECL:
                    if c.is_definition() and c.spelling:
                        visit_enum(c, path, None)
                elif k in (CK.FUNCTION_DECL, CK.FUNCTION_TEMPLATE):
                    add(c, "function", None, path)
                elif k in (CK.TYPE_ALIAS_DECL, CK.TYPEDEF_DECL, CK.TYPE_ALIAS_TEMPLATE_DECL, CK.CONCEPT_DECL):
                    add(c, KIND[k], None, path)
                elif k == CK.VAR_DECL:
                    add(c, "constant", None, path)

        visit(tu.cursor)

    # 같은 줄에 선 기호(전방 선언의 재등장 등)를 걸러 내고 줄 순서로 정렬한 id를 다시 매긴다
    paths = sorted(header_index, key=lambda p: header_index[p])
    # 헤더 첫 주석(파일 설명)
    header_desc = []
    for p in paths:
        text = sources[p].decode("utf-8", "replace").splitlines()
        desc = []
        for ln in text:
            s = ln.strip()
            if s.startswith("#pragma") or s.startswith("#include") or not s:
                if desc and not s:
                    break
                continue
            if s.startswith("//"):
                desc.append(s[2:].strip())
            else:
                break
        header_desc.append(" ".join(desc))

    api = {
        "commit": None,
        "headers": [{"p": p, "n": sources[p].count(b"\n") + 1, "d": header_desc[i]} for i, p in enumerate(paths)],
        "symbols": symbols,
    }
    head = (ROOT / ".git" / "HEAD").read_text().strip()
    if head.startswith("ref:"):
        ref = ROOT / ".git" / head[5:]
        api["commit"] = ref.read_text().strip() if ref.exists() else None
    else:
        api["commit"] = head

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "api-index.js").write_text(
        "/* tools/gen_api.py가 만든다. 손으로 고치지 않는다. */\nwindow.LUIL_API = " + json.dumps(api, ensure_ascii=False, separators=(",", ":")) + ";\n",
        encoding="utf-8",
    )
    src = {p: sources[p].decode("utf-8", "replace").replace("\r\n", "\n") for p in paths}
    (OUT_DIR / "api-src.js").write_text(
        "/* tools/gen_api.py가 만든다. 공개 헤더 원문. */\nwindow.LUIL_API_SRC = " + json.dumps(src, ensure_ascii=False, separators=(",", ":")) + ";\n",
        encoding="utf-8",
    )
    kinds = {}
    for s in symbols:
        kinds[s["k"]] = kinds.get(s["k"], 0) + 1
    print(f"headers {len(paths)}, symbols {len(symbols)}, errors {len(errors)}", kinds)


if __name__ == "__main__":
    main()

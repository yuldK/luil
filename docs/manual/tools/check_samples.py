#!/usr/bin/env python3
"""설명서의 손으로 쓴 C++ 예제가 공개 헤더에 대고 컴파일되는지 검사한다.

    python3 docs/manual/tools/check_samples.py [page.html ...] [--cxx g++] [-v]

표시:
    <div class="code" data-lang="cpp" data-compile="pre-lists">…<script type="text/plain">예제</script></div>
        data-compile의 값은 같은 페이지의 머리말 id다. 머리말은 화면에 보이지 않는 블록이다:
    <script type="text/plain" id="pre-lists" data-prelude>
    #include "luil/ui/list_element.h"
    namespace sample { /*SAMPLE*/ }
    </script>
        머리말의 /*SAMPLE*/ 자리에 예제가 들어간다. 값이 "tu"면 예제 자체가 완전한 번역 단위다.

검사는 -fsyntax-only다. 링크하거나 실행하지 않는다. 그래서 결과는 "공개 헤더에 대고 컴파일된다"이지 "돈다"가 아니다.
빌드 산출물 없이 돈다 — 생성 헤더와 Skia·nlohmann 머리는 gen_api.py와 같은 흉내를 쓴다.
하나라도 실패하면 1로 끝난다.
"""
import html
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANUAL = HERE.parent
ROOT = MANUAL.parents[1]
sys.path.insert(0, str(HERE))
from gen_api import make_stub_dir  # noqa: E402

CODE = re.compile(r'<div class="code[^"]*"([^>]*\bdata-compile="([^"]+)"[^>]*)>\s*<script type="text/plain">(.*?)</script>', re.S)
PRELUDE = re.compile(r'<script type="text/plain" id="([^"]+)" data-prelude>(.*?)</script>', re.S)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    verbose = "-v" in sys.argv
    cxx = "g++"
    if "--cxx" in sys.argv:
        cxx = sys.argv[sys.argv.index("--cxx") + 1]
        args = [a for a in args if a != cxx]
    files = [Path(a) for a in args] or sorted(MANUAL.glob("*.html"))
    total = failed = 0
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        stubs = tmp / "stubs"
        make_stub_dir(stubs)
        for f in files:
            s = f.read_text(encoding="utf-8")
            preludes = {m.group(1): html.unescape(m.group(2)) for m in PRELUDE.finditer(s)}
            for n, m in enumerate(CODE.finditer(s), 1):
                attrs, key, code = m.group(1), m.group(2), html.unescape(m.group(3))
                label = re.search(r'data-label="([^"]*)"', attrs)
                name = f"{f.name} #{n}" + (f" ({label.group(1)})" if label else "")
                if key == "tu":
                    src = code
                elif key in preludes:
                    if "/*SAMPLE*/" not in preludes[key]:
                        print(f"FAIL {name}: 머리말 {key}에 /*SAMPLE*/ 자리가 없다")
                        failed += 1
                        total += 1
                        continue
                    src = preludes[key].replace("/*SAMPLE*/", code)
                else:
                    print(f"FAIL {name}: 머리말 {key}이(가) 페이지에 없다")
                    failed += 1
                    total += 1
                    continue
                cpp = tmp / f"sample_{f.stem}_{n}.cpp"
                cpp.write_text(src, encoding="utf-8")
                cmd = [cxx, "-std=c++20", "-fsyntax-only", "-I", str(ROOT / "include"), "-I", str(stubs), str(cpp)]
                r = subprocess.run(cmd, capture_output=True, text=True)
                total += 1
                if r.returncode != 0:
                    failed += 1
                    print(f"FAIL {name}")
                    print("\n".join(r.stderr.splitlines()[:30]))
                elif verbose:
                    print(f"ok   {name}")
    print(f"예제 {total}개 중 실패 {failed}개 ({cxx} -fsyntax-only)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

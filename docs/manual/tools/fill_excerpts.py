"""강의 HTML의 코드 발췌를 저장소 원문으로 채운다.

코드 상자에 data-src="경로:시작-끝[,시작-끝...]"를 적으면, 그 안의
<script type="text/plain"> 내용을 저장소의 해당 줄로 바꾸고 data-path·data-start를
맞춘다. 범위 사이에는 생략 줄(⋯@다음 줄 번호)을 넣는다.

    python3 docs/manual/tools/fill_excerpts.py            # 채운다
    python3 docs/manual/tools/fill_excerpts.py --check    # 바뀔 것이 있으면 1로 끝난다
    python3 docs/manual/tools/fill_excerpts.py docs/manual/lists.html   # 그 문서만

발췌는 강의가 기준으로 삼은 커밋(assets/course.js의 COURSE.commit)의 줄 번호와
맞아야 GitHub 링크가 같은 줄을 가리킨다. 소스가 바뀐 뒤 다시 채울 때는 그 커밋도 함께 올린다.
"""
import pathlib
import re
import sys
from pathlib import Path

ROOT = pathlib.Path(__file__).resolve().parents[3]
MANUAL = ROOT / "docs" / "manual"

BOX = re.compile(
    r'(<div class="code[^"]*"(?P<attrs>[^>]*?)data-src="(?P<src>[^"]+)"(?P<rest>[^>]*)>\s*<script type="text/plain">)(?P<body>.*?)(</script>)',
    re.S,
)


def read_lines(path):
    text = (ROOT / path).read_text(encoding="utf-8-sig")
    return text.replace("\r\n", "\n").replace("\r", "\n").split("\n")


def excerpt(src):
    path, _, spec = src.partition(":")
    lines = read_lines(path)
    ranges = []
    for part in spec.split(","):
        a, _, b = part.strip().partition("-")
        a = int(a)
        b = int(b) if b else a
        if not (1 <= a <= b <= len(lines)):
            raise SystemExit(f"{src}: 범위 {a}-{b}가 파일 길이 {len(lines)}를 벗어난다")
        ranges.append((a, b))
    out = []
    for i, (a, b) in enumerate(ranges):
        if i:
            out.append(f"⋯@{a}")
        out.extend(lines[a - 1 : b])
    return path, ranges[0][0], "\n".join(out)


def fill(html):
    def repl(m):
        src = m.group("src")
        path, start, body = excerpt(src)
        opening = m.group(1)
        div = opening[: opening.index(">")]
        div = re.sub(r'\s*data-(path|start)="[^"]*"', "", div)
        div = div.replace(f'data-src="{src}"', f'data-src="{src}" data-path="{path}" data-start="{start}"')
        return div + '><script type="text/plain">\n' + body.replace("</script", "<\\/script") + "\n" + m.group(6)

    return BOX.sub(repl, html)


def main():
    check = "--check" in sys.argv
    only = [Path(a).resolve() for a in sys.argv[1:] if not a.startswith("-")]
    changed = []
    for page in only or sorted(MANUAL.glob("*.html")):
        before = page.read_text(encoding="utf-8")
        after = fill(before)
        if after != before:
            changed.append(page.name)
            if not check:
                page.write_text(after, encoding="utf-8")
    for name in changed:
        print(("다를 것: " if check else "채움: ") + name)
    if check and changed:
        sys.exit(1)


if __name__ == "__main__":
    main()

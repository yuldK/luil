"""src/text/line_break_data.h의 분류 표를 강의 사이트용 JS로 옮긴다.

    python3 docs/manual/tools/gen_line_break_data.py

assets/linebreak-data.js를 쓴다. 8강의 줄 나눔 실험이 이 표로 luil의 UAX #14 규칙기
(src/text/line_break.cpp를 옮긴 assets/linebreak.js)를 돌린다.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCE = ROOT / "src" / "text" / "line_break_data.h"
OUT = ROOT / "docs" / "manual" / "assets" / "linebreak-data.js"

text = SOURCE.read_text(encoding="utf-8-sig")
ascii_block = re.search(r"ascii_line_break_classes\[128\] \{([\s\S]*?)\};", text).group(1)
ascii = re.findall(r"line_break_class::(\w+)", ascii_block)
assert len(ascii) == 128, len(ascii)


def table(name):
    body = re.search(name + r"\[\] \{([\s\S]*?)\};", text).group(1)
    return [(int(a, 16), int(b, 16), c) for a, b, c in re.findall(r"\{ 0x([0-9A-F]+), 0x([0-9A-F]+)(?:, line_break_class::(\w+))? \}", body)]


ranges = table("line_break_ranges")
aux = {key: table(name) for key, name in [("east_asian", "east_asian_ranges"), ("unassigned_pictographic", "unassigned_pictographic_ranges"), ("initial_quote", "initial_quote_ranges"), ("final_quote", "final_quote_ranges"), ("hanging", "hanging_space_ranges")]}
version = re.search(r"Unicode (\d+\.\d+\.\d+)", text)
classes = sorted(set(ascii) | {c for _, _, c in ranges})
index = {c: i for i, c in enumerate(classes)}


def flat(rows, with_class):
    out = []
    for a, b, c in rows:
        out += [a, b] + ([index[c]] if with_class else [])
    return ",".join(str(v) for v in out)


lines = [
    "/* luil 줄 나눔 분류 표 — src/text/line_break_data.h에서 tools/gen_line_break_data.py가 만든다. 손으로 고치지 않는다. */",
    "window.LUIL_LINE_BREAK_DATA = {",
    f'  unicode: "{version.group(1) if version else "?"}",',
    "  classes: [" + ",".join(f'"{c}"' for c in classes) + "],",
    "  ascii: [" + ",".join(str(index[c]) for c in ascii) + "],",
    "  ranges: [" + flat(ranges, True) + "],",
]
for key, rows in aux.items():
    lines.append(f"  {key}: [" + flat(rows, False) + "],")
lines.append("};")
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"{OUT.name}: classes {len(classes)}, ranges {len(ranges)}, " + ", ".join(f"{k} {len(v)}" for k, v in aux.items()) + f", {OUT.stat().st_size} bytes")

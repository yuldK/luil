"""Unicode 16.0.0 UCD에서 내장 글자소 표를 만든다.

입력 디렉터리에 GraphemeBreakProperty.txt, emoji-data.txt, UnicodeData.txt를 둔다.
원본: https://www.unicode.org/Public/16.0.0/ucd/ (Unicode License V3).
다운로드한 파일은 저장소에 넣지 않고 판과 출처를 생성물에 남긴다.
"""
from pathlib import Path
import argparse


def property_rows(path, selected=None):
    rows = []
    for line in path.read_text().splitlines():
        value = line.split('#')[0].strip()
        if not value:
            continue
        bounds, prop = (x.strip() for x in value.split(';')[:2])
        if selected is not None and prop != selected:
            continue
        parts = bounds.split('..')
        rows.append((int(parts[0], 16), int(parts[-1], 16), prop))
    return sorted(rows)


def merge_ranges(rows):
    result = []
    for begin, end in sorted(rows):
        if result and begin <= result[-1][1] + 1:
            result[-1] = (result[-1][0], max(result[-1][1], end))
        else:
            result.append((begin, end))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ucd', type=Path)
    parser.add_argument('--output', type=Path, default=Path('src/text/grapheme_data.h'))
    args = parser.parse_args()
    mapping = {'CR': 'cr', 'LF': 'lf', 'Control': 'control', 'Extend': 'extend',
               'ZWJ': 'zwj', 'Regional_Indicator': 'regional_indicator', 'Prepend': 'prepend',
               'SpacingMark': 'spacing_mark', 'L': 'l', 'V': 'v', 'T': 't'}
    lines = ['#pragma once', '', '// Unicode 16.0.0, UAX #29의 편집용 부분 집합이다.',
             '// 출처: unicode.org/Public/16.0.0/ucd/ (Unicode License V3).',
             '// scripts/generate_grapheme_data.py로 다시 만든다.',
             '// 한글 LV/LVT는 코드에서 계산하므로 이 표에는 넣지 않는다.', '']
    lines += ['inline constexpr grapheme_range grapheme_ranges[] {']
    for begin, end, prop in property_rows(args.ucd / 'auxiliary/GraphemeBreakProperty.txt'):
        if prop in mapping:
            lines.append(f'    {{ 0x{begin:X}, 0x{end:X}, grapheme_property::{mapping[prop]} }},')
    lines += ['};', '', 'inline constexpr codepoint_range pictographic_ranges[] {']
    for begin, end in merge_ranges([(a, b) for a, b, _ in property_rows(args.ucd / 'emoji/emoji-data.txt', 'Extended_Pictographic')]):
        lines.append(f'    {{ 0x{begin:X}, 0x{end:X} }},')
    lines += ['};', '', 'inline constexpr codepoint_range combining_ranges[] {']
    marks = []
    for line in (args.ucd / 'UnicodeData.txt').read_text().splitlines():
        fields = line.split(';')
        if fields[2] in ('Mn', 'Mc', 'Me'):
            marks.append((int(fields[0], 16), int(fields[0], 16)))
    for begin, end in merge_ranges(marks):
        lines.append(f'    {{ 0x{begin:X}, 0x{end:X} }},')
    lines += ['};', '']
    args.output.write_bytes('\r\n'.join(lines).encode())


if __name__ == '__main__':
    main()

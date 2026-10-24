"""Unicode 16.0.0 UCD에서 내장 글자소 표를 만든다.

입력 디렉터리에 GraphemeBreakProperty.txt, emoji-data.txt, UnicodeData.txt를 둔다.
원본: https://www.unicode.org/Public/16.0.0/ucd/ (Unicode License V3).
다운로드한 파일은 저장소에 넣지 않고 판과 출처를 생성물에 남긴다.
"""
from pathlib import Path
import argparse
import sys

UNICODE_VERSION = '16.0.0'
# emoji-data.txt는 첫 줄에 판을 적지 않고 머리말에 Emoji 판(주.부)만 적는다.
EMOJI_VERSION = '.'.join(UNICODE_VERSION.split('.')[:2])


def read_ucd(path):
    # UCD는 UTF-8이다. 지역 codec(cp949 등)으로 읽으면 emoji-data.txt의 주석에서 깨진다.
    try:
        return path.read_text(encoding='utf-8')
    except OSError as error:
        sys.exit(f'{path}: 읽지 못했다 ({error})')


def require_version(path, text):
    # 다른 판의 파일로 만들면 생성물 머리말의 판과 표가 조용히 어긋난다. 판이 맞지 않으면 멈춘다.
    lines = text.splitlines()
    first = lines[0].strip() if lines else ''
    if path.name == 'GraphemeBreakProperty.txt':
        expected = f'# GraphemeBreakProperty-{UNICODE_VERSION}.txt'
        if first != expected:
            sys.exit(f'{path}: 첫 줄이 "{expected}"가 아니다 (읽은 줄: "{first}")')
    elif path.name == 'emoji-data.txt':
        header = []
        for line in lines:
            if not line.startswith('#'):
                break
            header.append(line)
        if first != f'# emoji-data-{UNICODE_VERSION}.txt' \
                and not any(f'Emoji Version {EMOJI_VERSION}' in line for line in header):
            sys.exit(f'{path}: Emoji {EMOJI_VERSION} 판의 머리말이 아니다 (첫 줄: "{first}")')
    elif path.name == 'UnicodeData.txt':
        # UnicodeData.txt에는 판을 적은 머리말이 없다. 열 수만 보아 다른 파일을 잘못 넘긴 것을 잡는다.
        if len(first.split(';')) != 15:
            sys.exit(f'{path}: UnicodeData.txt 형식이 아니다 (첫 줄: "{first}")')


def property_rows(path, selected=None):
    rows = []
    text = read_ucd(path)
    require_version(path, text)
    for line in text.splitlines():
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
    lines = ['#pragma once', '', f'// Unicode {UNICODE_VERSION}, UAX #29의 편집용 부분 집합이다.',
             f'// 출처: unicode.org/Public/{UNICODE_VERSION}/ucd/ (Unicode License V3).',
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
    unicode_data = args.ucd / 'UnicodeData.txt'
    unicode_text = read_ucd(unicode_data)
    require_version(unicode_data, unicode_text)
    for line in unicode_text.splitlines():
        fields = line.split(';')
        if fields[2] in ('Mn', 'Mc', 'Me'):
            marks.append((int(fields[0], 16), int(fields[0], 16)))
    for begin, end in merge_ranges(marks):
        lines.append(f'    {{ 0x{begin:X}, 0x{end:X} }},')
    lines += ['};', '']
    args.output.write_bytes('\r\n'.join(lines).encode())


if __name__ == '__main__':
    main()

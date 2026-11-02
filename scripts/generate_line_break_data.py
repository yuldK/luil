"""Unicode 16.0.0 UCD에서 내장 줄 나눔 표(UAX #14)를 만든다.

입력 디렉터리에 LineBreak.txt, EastAsianWidth.txt, UnicodeData.txt, emoji/emoji-data.txt를 둔다
(UCD 압축의 배치 그대로다). 원본: https://www.unicode.org/Public/16.0.0/ucd/ (Unicode License V3).
다운로드한 파일은 저장소에 넣지 않고 판과 출처를 생성물에 남긴다.
"""
from pathlib import Path
import argparse
import sys

UNICODE_VERSION = '16.0.0'
# emoji-data.txt는 첫 줄에 판을 적지 않고 머리말에 Emoji 판(주.부)만 적는다.
EMOJI_VERSION = '.'.join(UNICODE_VERSION.split('.')[:2])

# LB1을 생성 때 끝낸 분류다. AI·SG·XX는 AL, CJ는 NS, SA는 Mn·Mc면 CM 아니면 AL이 되어 표에 남지 않는다.
CLASSES = ['al', 'bk', 'cr', 'lf', 'nl', 'sp', 'zw', 'wj', 'gl', 'cm', 'zwj', 'ak', 'ap', 'as', 'b2', 'ba', 'bb',
           'cb', 'cl', 'cp', 'eb', 'em', 'ex', 'h2', 'h3', 'hl', 'hy', 'id', 'in', 'is', 'jl', 'jt', 'jv', 'ns',
           'nu', 'op', 'po', 'pr', 'qu', 'ri', 'sy', 'vf', 'vi']

# LineBreak.txt 머리말이 적은 기본값이다. 파일에 없는 코드포인트에 @missing(XX)보다 앞서 쓴다.
DEFAULT_RANGES = [
    (0x3400, 0x4DBF, 'ID'), (0x4E00, 0x9FFF, 'ID'), (0xF900, 0xFAFF, 'ID'),
    (0x20000, 0x2FFFD, 'ID'), (0x30000, 0x3FFFD, 'ID'),
    (0x1F000, 0x1FAFF, 'ID'), (0x1FC00, 0x1FFFD, 'ID'),
    (0x20A0, 0x20CF, 'PR'),
]

# 한글 음절은 코드에서 계산한다 (LV는 H2, LVT는 H3). 표에 넣으면 800줄 가까이 는다.
HANGUL_FIRST = 0xAC00
HANGUL_LAST = 0xD7A3


def read_ucd(path):
    # UCD는 UTF-8이다. 지역 codec(cp949 등)으로 읽으면 주석에서 깨진다.
    try:
        return path.read_text(encoding='utf-8')
    except OSError as error:
        sys.exit(f'{path}: 읽지 못했다 ({error})')


def require_version(path, text):
    # 다른 판의 파일로 만들면 생성물 머리말의 판과 표가 조용히 어긋난다. 판이 맞지 않으면 멈춘다.
    lines = text.splitlines()
    first = lines[0].strip() if lines else ''
    if path.name in ('LineBreak.txt', 'EastAsianWidth.txt'):
        expected = f'# {path.stem}-{UNICODE_VERSION}.txt'
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


def general_categories(path):
    # UnicodeData.txt는 큰 범위를 "<..., First>"·"<..., Last>" 두 줄로 접는다. Cn(미할당)은 이 표에 없는 자리라
    # 접은 범위를 펴지 않으면 한자·한글 음절이 모두 미할당으로 보인다.
    text = read_ucd(path)
    require_version(path, text)
    categories = {}
    first = None
    for line in text.splitlines():
        fields = line.split(';')
        if len(fields) < 3:
            continue
        codepoint = int(fields[0], 16)
        if fields[1].endswith(', First>'):
            first = codepoint
            continue
        if fields[1].endswith(', Last>'):
            if first is None:
                sys.exit(f'{path}: {fields[0]}의 First 줄이 없다')
            for value in range(first, codepoint + 1):
                categories[value] = fields[2]
            first = None
            continue
        categories[codepoint] = fields[2]
    return categories


def merge_property_ranges(rows):
    # 값이 같은 이웃 범위를 하나로 묶는다. 값이 다르면 붙어 있어도 따로 둔다.
    result = []
    for begin, end, prop in sorted(rows):
        if result and result[-1][2] == prop and begin <= result[-1][1] + 1:
            result[-1] = (result[-1][0], max(result[-1][1], end), prop)
        else:
            result.append((begin, end, prop))
    return result


def merge_ranges(values):
    result = []
    for value in sorted(values):
        if result and value <= result[-1][1] + 1:
            result[-1] = (result[-1][0], max(result[-1][1], value))
        else:
            result.append((value, value))
    return result


def resolve(codepoint, raw, categories):
    # LB1의 기본 해석이다.
    if raw in ('AI', 'SG', 'XX'):
        return 'al'
    if raw == 'CJ':
        return 'ns'
    if raw == 'SA':
        return 'cm' if categories.get(codepoint) in ('Mn', 'Mc') else 'al'
    value = raw.lower()
    if value not in CLASSES:
        sys.exit(f'모르는 Line_Break 값이다: {raw}')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ucd', type=Path)
    parser.add_argument('--output', type=Path, default=Path('src/text/line_break_data.h'))
    args = parser.parse_args()

    categories = general_categories(args.ucd / 'UnicodeData.txt')
    raw = {}
    for begin, end, prop in DEFAULT_RANGES:
        for value in range(begin, end + 1):
            raw[value] = prop
    for begin, end, prop in property_rows(args.ucd / 'LineBreak.txt'):
        for value in range(begin, end + 1):
            raw[value] = prop
    resolved = {}
    for codepoint in range(0x110000):
        resolved[codepoint] = resolve(codepoint, raw.get(codepoint, 'XX'), categories)

    for codepoint in range(HANGUL_FIRST, HANGUL_LAST + 1):
        expected = 'h2' if (codepoint - HANGUL_FIRST) % 28 == 0 else 'h3'
        if resolved[codepoint] != expected:
            sys.exit(f'U+{codepoint:04X}: 한글 음절 계산({expected})과 표({resolved[codepoint]})가 다르다')

    # AL은 표에 없는 자리의 기본값이라 넣지 않는다. 한글 음절은 코드가 계산한다.
    rows = []
    for codepoint in range(0x110000):
        value = resolved[codepoint]
        if value == 'al' or HANGUL_FIRST <= codepoint <= HANGUL_LAST:
            continue
        rows.append((codepoint, codepoint, value))
    ranges = merge_property_ranges(rows)

    east_asian = []
    for begin, end, prop in property_rows(args.ucd / 'EastAsianWidth.txt'):
        if prop in ('F', 'W', 'H'):
            east_asian.extend(range(begin, end + 1))
    pictographic = set()
    for begin, end, _ in property_rows(args.ucd / 'emoji/emoji-data.txt', 'Extended_Pictographic'):
        pictographic.update(range(begin, end + 1))
    unassigned_pictographic = [value for value in pictographic if value not in categories]
    initial_quotes = [value for value in range(0x110000) if resolved[value] == 'qu' and categories.get(value) == 'Pi']
    final_quotes = [value for value in range(0x110000) if resolved[value] == 'qu' and categories.get(value) == 'Pf']
    hanging = [value for value in range(0x110000) if categories.get(value) == 'Zs' and resolved[value] != 'gl']

    lines = ['#pragma once', '', f'// Unicode {UNICODE_VERSION}, UAX #14의 줄 나눔 분류다.',
             f'// 출처: unicode.org/Public/{UNICODE_VERSION}/ucd/ (Unicode License V3).',
             '// scripts/generate_line_break_data.py로 다시 만든다.',
             '// LB1(AI·SG·XX는 AL, CJ는 NS, SA는 Mn·Mc면 CM 아니면 AL)은 이미 풀었다.',
             '// 표에 없는 자리는 AL이다. 한글 음절(U+AC00..U+D7A3)은 코드가 H2·H3를 계산한다.', '']
    lines += ['inline constexpr line_break_class ascii_line_break_classes[128] {']
    for row in range(0, 128, 8):
        values = ', '.join(f'line_break_class::{resolved[value]}' for value in range(row, row + 8))
        lines.append(f'    {values},')
    lines += ['};', '', 'inline constexpr line_break_range line_break_ranges[] {']
    for begin, end, prop in ranges:
        lines.append(f'    {{ 0x{begin:X}, 0x{end:X}, line_break_class::{prop} }},')
    tables = [
        ('// East_Asian_Width가 F·W·H인 자리다 (LB19a·LB30).', 'east_asian_ranges', east_asian),
        ('// Extended_Pictographic이면서 미할당(Cn)인 자리다 (LB30b).', 'unassigned_pictographic_ranges', unassigned_pictographic),
        ('// QU 가운데 일반 분류가 Pi인 자리다 (LB15a·LB19).', 'initial_quote_ranges', initial_quotes),
        ('// QU 가운데 일반 분류가 Pf인 자리다 (LB15b·LB19).', 'final_quote_ranges', final_quotes),
        ('// 접은 줄 끝에 매달리는 공백이다. 일반 분류 Zs 가운데 GL이 아닌 것이다.', 'hanging_space_ranges', hanging),
    ]
    for comment, name, values in tables:
        if not values:
            sys.exit(f'{name}: 값이 없다')
        lines += ['};', '', comment, f'inline constexpr codepoint_range {name}[] {{']
        for begin, end in merge_ranges(values):
            lines.append(f'    {{ 0x{begin:X}, 0x{end:X} }},')
    lines += ['};', '']
    args.output.write_bytes('\r\n'.join(lines).encode())


if __name__ == '__main__':
    main()

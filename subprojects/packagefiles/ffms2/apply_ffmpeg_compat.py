#!/usr/bin/env python3
import re
import sys
from pathlib import Path
from typing import List, Tuple


def wrap_block(lines: List[str], start: int, macro: str) -> Tuple[List[str], int, bool]:
    """Wrap the block starting at ``start`` with an ``#if defined(macro)`` guard."""
    result: List[str] = []
    line = lines[start]
    indent = re.match(r"\s*", line).group(0) if line else ""
    guard = f"{indent}#if defined({macro})"
    end_guard = f"{indent}#endif"

    # Avoid wrapping a block that's already guarded.
    if line.strip().startswith('#if defined('):
        return [line], start + 1, False

    result.append(guard)
    result.append(line)

    consumed = 1
    stripped = line.strip()
    if stripped.endswith('{'):
        depth = line.count('{') - line.count('}')
        while start + consumed < len(lines) and depth > 0:
            current = lines[start + consumed]
            result.append(current)
            depth += current.count('{') - current.count('}')
            consumed += 1
    elif stripped.endswith(';') or stripped.endswith(')'):
        # Single line statement; nothing additional required.
        pass
    else:
        # Assume the next logical line belongs to this block.
        if start + consumed < len(lines):
            result.append(lines[start + consumed])
            consumed += 1

    result.append(end_guard)
    return result, start + consumed, True


def guard_tokens(text: List[str]) -> Tuple[List[str], bool]:
    i = 0
    output: List[str] = []
    modified = False

    while i < len(text):
        line = text[i]
        if (
            'AV_DISPOSITION_MULTILAYER' in line
            and not line.lstrip().startswith('#if defined(')
            and not (output and output[-1].strip() == '#if defined(AV_DISPOSITION_MULTILAYER)')
        ):
            wrapped, next_index, changed = wrap_block(text, i, 'AV_DISPOSITION_MULTILAYER')
            output.extend(wrapped)
            i = next_index
            modified = modified or changed
            continue

        if (
            'primary_eye' in line
            and not line.lstrip().startswith('#if defined(')
            and not (output and output[-1].strip() == '#if defined(AV_PRIMARY_EYE_RIGHT)')
        ):
            wrapped, next_index, changed = wrap_block(text, i, 'AV_PRIMARY_EYE_RIGHT')
            output.extend(wrapped)
            i = next_index
            modified = modified or changed
            continue

        output.append(line)
        i += 1

    return output, modified


def main() -> int:
    if len(sys.argv) != 2:
        print('usage: apply_ffmpeg_compat.py <source-root>', file=sys.stderr)
        return 1

    root = Path(sys.argv[1])
    target = root / 'src' / 'core' / 'videosource.cpp'

    if not target.exists():
        print(f'skipping ffms2 compatibility patch; {target} not found', file=sys.stderr)
        return 0

    contents = target.read_text().splitlines()
    updated, changed = guard_tokens(contents)

    if changed:
        target.write_text('\n'.join(updated) + '\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())

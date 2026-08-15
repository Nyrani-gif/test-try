#!/usr/bin/env python3
"""Compare ASS event structure before/after an Aegisub++ save.

By default the visible Text field may change, but event type, Layer, timing,
Style, Actor, margins, Effect, event order, and Aegisub extradata references
must remain byte-for-byte identical. Use --strict-text for a pure save/open
round-trip where Text must also be identical.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


EXTRADATA = re.compile(r"^(\{(?:=-?\d+)+\})")


@dataclass(frozen=True)
class Event:
    kind: str
    fields: tuple[str, ...]
    text_index: int
    line_number: int

    @property
    def text(self) -> str:
        return self.fields[self.text_index]

    @property
    def extradata(self) -> str:
        match = EXTRADATA.match(self.text)
        return match.group(1) if match else ""


def read_events(path: Path) -> list[Event]:
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    in_events = False
    event_format: list[str] | None = None
    events: list[Event] = []

    for line_number, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_events = stripped.casefold() == "[events]"
            continue
        if not in_events or not stripped or stripped.startswith(";"):
            continue
        if stripped.casefold().startswith("format:"):
            event_format = [field.strip() for field in stripped.split(":", 1)[1].split(",")]
            continue
        if not (stripped.casefold().startswith("dialogue:") or stripped.casefold().startswith("comment:")):
            continue
        if not event_format:
            raise ValueError(f"{path}:{line_number}: event before Events/Format")

        kind, payload = line.split(":", 1)
        fields = tuple(payload.lstrip().split(",", len(event_format) - 1))
        if len(fields) != len(event_format):
            raise ValueError(f"{path}:{line_number}: expected {len(event_format)} event fields, got {len(fields)}")
        try:
            text_index = next(i for i, name in enumerate(event_format) if name.casefold() == "text")
        except StopIteration as error:
            raise ValueError(f"{path}:{line_number}: Events/Format has no Text field") from error
        events.append(Event(kind.strip(), fields, text_index, line_number))
    return events


def compare(before: Path, after: Path, strict_text: bool) -> list[str]:
    old = read_events(before)
    new = read_events(after)
    errors: list[str] = []
    if len(old) != len(new):
        return [f"event count changed: {len(old)} -> {len(new)}"]

    for index, (left, right) in enumerate(zip(old, new), 1):
        if left.kind != right.kind:
            errors.append(f"event {index}: type changed: {left.kind} -> {right.kind}")
        if left.text_index != right.text_index or len(left.fields) != len(right.fields):
            errors.append(f"event {index}: Events/Format structure changed")
            continue
        for field_index, (old_value, new_value) in enumerate(zip(left.fields, right.fields)):
            if field_index == left.text_index and not strict_text:
                continue
            if old_value != new_value:
                label = "Text" if field_index == left.text_index else f"field {field_index + 1}"
                errors.append(f"event {index}: {label} changed unexpectedly")
        if left.extradata != right.extradata:
            errors.append(
                f"event {index}: extradata reference changed: {left.extradata or '<none>'} -> "
                f"{right.extradata or '<none>'}"
            )
    return errors


def self_test() -> None:
    sample = """[Script Info]
Title: sanae integrity self-test

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:47.03,0:00:54.05,opsound,ASign,0,0,0,,{=189}{\\an7\\pos(0,0)\\p1\\c&HFFFFFF&}m 2014.24 1213.43 l -138.72 1213.43 -138.72 -155.36 2014.24 -155.36{\\p0}
Dialogue: 2,0:00:47.03,0:00:54.05,op1,ASign,0,0,0,,{\\c&H1900BD&\\fs480\\fscx105\\pos(950,539.2)}Исто  ия
Dialogue: 2,0:00:47.03,0:00:54.05,opsound - Копировать,ASign,0,0,0,,{\\c&H00E0FB&\\fs138\\fscx99\\pos(960,874)}Эврика Эврика!
Dialogue: 2,0:00:47.03,0:00:54.05,opsound - Копировать,ASign,0,0,0,,{=188}{\\c&H000000&\\fs68\\fscx99\\pos(960,986)}—ГЛАВА ШЕСТАЯ: ЗВУК ПЕРЕМЕН—
"""
    from tempfile import TemporaryDirectory

    with TemporaryDirectory() as directory:
        before = Path(directory) / "before.ass"
        after = Path(directory) / "after.ass"
        before.write_text(sample, encoding="utf-8")
        after.write_text(sample, encoding="utf-8")
        assert not compare(before, after, strict_text=True)

        changed = sample.replace("Эврика Эврика!", "Эврика!")
        after.write_text(changed, encoding="utf-8")
        assert not compare(before, after, strict_text=False)
        assert compare(before, after, strict_text=True)

        broken = changed.replace("{=188}", "", 1)
        after.write_text(broken, encoding="utf-8")
        assert any("extradata" in error for error in compare(before, after, strict_text=False))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", nargs="?", type=Path)
    parser.add_argument("after", nargs="?", type=Path)
    parser.add_argument("--strict-text", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("ASS integrity self-test: OK")
        return 0
    if not args.before or not args.after:
        parser.error("provide BEFORE.ass AFTER.ass, or use --self-test")

    errors = compare(args.before, args.after, args.strict_text)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("ASS event structure and extradata references are intact.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

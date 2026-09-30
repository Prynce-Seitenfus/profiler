#!/usr/bin/env python3
"""Parse PROF-DUMP v1 logs and report matched function calls."""

import argparse
import csv
import re
import subprocess
import sys
from collections import defaultdict
from decimal import Decimal
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Sequence, TextIO, Tuple


UINT32_MASK = 0xFFFFFFFF
HEADER_PATTERN = re.compile(r"^#\s*PROF-DUMP\s+v1\s+(.*)$")
HEADER_FIELD_PATTERN = re.compile(r"([A-Za-z_]+)=([^\s]+)")
CSV_FIELDS = ("index", "timestamp", "event", "function", "call_site")
OUTPUT_FIELDS = (
    "function",
    "function_address",
    "caller",
    "call_site",
    "entry_index",
    "exit_index",
    "entry_timestamp",
    "exit_timestamp",
    "cycles",
    "time_us",
    "function_source",
    "call_site_source",
)


class Event(NamedTuple):
    index: int
    timestamp: int
    event: str
    function: int
    call_site: int


class Invocation(NamedTuple):
    entry: Event
    exit: Event


def parse_frequency(value: str) -> int:
    """Parse a frequency in Hz, optionally using k, M, or G suffixes."""
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)\s*([kKmMgG]?)", value)
    if match is None:
        raise ValueError("frequency must be a positive number in Hz (optional k/M/G suffix)")

    scale = {"": 1, "k": 1000, "m": 1000000, "g": 1000000000}
    frequency = int(Decimal(match.group(1)) * scale[match.group(2).lower()])
    if frequency <= 0:
        raise ValueError("frequency must be greater than zero")
    return frequency


def read_dump(path: Path) -> Tuple[Dict[str, str], List[Event], List[str]]:
    text = path.read_text(encoding="utf-8-sig")
    metadata: Dict[str, str] = {}
    csv_start: Optional[int] = None
    end_line: Optional[int] = None

    lines = text.splitlines()
    for line_number, line in enumerate(lines):
        header_match = HEADER_PATTERN.match(line.strip())
        if header_match is not None:
            metadata.update(dict(HEADER_FIELD_PATTERN.findall(header_match.group(1))))
        if tuple(field.strip() for field in line.split(",")) == CSV_FIELDS:
            csv_start = line_number
        if line.strip() == "# END":
            end_line = line_number
            break

    if "frequency_hz" not in metadata:
        raise ValueError("dump is missing frequency_hz in its PROF-DUMP header")
    if csv_start is None:
        raise ValueError("dump is missing the profiler CSV header")

    data_end = end_line if end_line is not None else len(lines)
    reader = csv.DictReader(lines[csv_start:data_end])
    events: List[Event] = []
    warnings: List[str] = []
    for row_number, row in enumerate(reader, start=csv_start + 2):
        try:
            if any(row.get(field) is None for field in CSV_FIELDS):
                raise ValueError("missing CSV field")
            event_name = row["event"].strip()
            if event_name not in ("ENTER", "EXIT"):
                raise ValueError("event must be ENTER or EXIT")
            timestamp = int(row["timestamp"], 10)
            if not 0 <= timestamp <= UINT32_MASK:
                raise ValueError("timestamp is outside the uint32 range")
            events.append(
                Event(
                    index=int(row["index"], 10),
                    timestamp=timestamp,
                    event=event_name,
                    function=int(row["function"], 0),
                    call_site=int(row["call_site"], 0),
                )
            )
        except (KeyError, TypeError, ValueError) as error:
            warnings.append(
                "invalid event at line {}: {}; skipping row".format(row_number, error)
            )

    return metadata, events, warnings


def resolve_symbols(
    addresses: Sequence[int],
    elf_path: Optional[Path],
    addr2line: str,
) -> Dict[int, Tuple[str, str]]:
    symbols = {address: ("0x{:08x}".format(address), "") for address in addresses}
    if elf_path is None:
        return symbols
    if not elf_path.is_file():
        raise ValueError("ELF file does not exist: {}".format(elf_path))
    if not addresses:
        return symbols

    address_arguments = ["0x{:x}".format(address) for address in addresses]
    result = subprocess.run(
        [addr2line, "-e", str(elf_path), "-f", "-C"] + address_arguments,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        raise ValueError(
            "addr2line failed with exit code {}{}".format(
                result.returncode,
                ": " + detail if detail else "",
            )
        )
    lines = result.stdout.splitlines()
    if len(lines) != len(addresses) * 2:
        raise ValueError("addr2line returned an unexpected number of result lines")

    for index, address in enumerate(addresses):
        function_name = lines[index * 2].strip()
        source = lines[index * 2 + 1].strip()
        if function_name == "??":
            function_name = "0x{:08x}".format(address)
        if source == "??:0":
            source = ""
        symbols[address] = (function_name, source)
    return symbols


def match_invocations(events: Sequence[Event]) -> Tuple[List[Invocation], int, int]:
    pending: Dict[int, List[Event]] = defaultdict(list)
    invocations: List[Invocation] = []
    unmatched_exits = 0

    for event in events:
        if event.event == "ENTER":
            pending[event.function].append(event)
        elif pending[event.function]:
            invocations.append(Invocation(pending[event.function].pop(), event))
        else:
            unmatched_exits += 1

    unmatched_enters = sum(len(entries) for entries in pending.values())
    invocations.sort(key=lambda invocation: invocation.entry.index)
    return invocations, unmatched_enters, unmatched_exits


def write_trace(
    output: TextIO,
    invocations: Sequence[Invocation],
    symbols: Dict[int, Tuple[str, str]],
    frequency: int,
) -> None:
    writer = csv.DictWriter(output, fieldnames=OUTPUT_FIELDS)
    writer.writeheader()
    for invocation in invocations:
        entry = invocation.entry
        exit_event = invocation.exit
        cycles = (exit_event.timestamp - entry.timestamp) & UINT32_MASK
        function_name, function_source = symbols[entry.function]
        caller_name, call_site_source = symbols[entry.call_site]
        writer.writerow(
            {
                "function": function_name,
                "function_address": "0x{:08x}".format(entry.function),
                "caller": caller_name,
                "call_site": "0x{:08x}".format(entry.call_site),
                "entry_index": entry.index,
                "exit_index": exit_event.index,
                "entry_timestamp": entry.timestamp,
                "exit_timestamp": exit_event.timestamp,
                "cycles": cycles,
                "time_us": "{:.3f}".format(cycles * 1000000.0 / frequency),
                "function_source": function_source,
                "call_site_source": call_site_source,
            }
        )


def write_summary(
    output: TextIO,
    invocations: Sequence[Invocation],
    symbols: Dict[int, Tuple[str, str]],
    frequency: int,
    unmatched_enters: int,
    unmatched_exits: int,
) -> None:
    by_function: Dict[int, List[int]] = defaultdict(list)
    for invocation in invocations:
        cycles = (invocation.exit.timestamp - invocation.entry.timestamp) & UINT32_MASK
        by_function[invocation.entry.function].append(cycles)

    writer = csv.writer(output)
    writer.writerow(("frequency_hz", frequency))
    writer.writerow(("matched_calls", len(invocations)))
    writer.writerow(("unmatched_enter", unmatched_enters, "unmatched_exit", unmatched_exits))
    writer.writerow(("function", "calls", "total_cycles", "min_us", "avg_us", "max_us"))
    for address, cycle_counts in sorted(by_function.items()):
        total = sum(cycle_counts)
        durations = [cycles * 1000000.0 / frequency for cycles in cycle_counts]
        writer.writerow(
            (
                symbols[address][0],
                len(cycle_counts),
                total,
                "{:.3f}".format(min(durations)),
                "{:.3f}".format(sum(durations) / len(durations)),
                "{:.3f}".format(max(durations)),
            ),
        )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Resolve and time matched calls in a PROF-DUMP v1 log."
    )
    parser.add_argument("log_file", type=Path, help="prof-dump text file")
    parser.add_argument("-e", "--elf", type=Path, help="ELF file used by arm-none-eabi-addr2line")
    parser.add_argument(
        "-f",
        "--frequency",
        help="tick frequency in Hz (default: frequency_hz from the dump header)",
    )
    parser.add_argument(
        "--addr2line",
        default="arm-none-eabi-addr2line",
        help="addr2line executable (default: arm-none-eabi-addr2line)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="write the per-call CSV to this file (default: input filename with .csv suffix)",
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="print aggregate call statistics to stderr",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    try:
        metadata, events, warnings = read_dump(args.log_file)
        for warning in warnings:
            print("warning: {}".format(warning), file=sys.stderr)
        frequency = (
            parse_frequency(args.frequency)
            if args.frequency is not None
            else parse_frequency(metadata["frequency_hz"])
        )
        addresses = sorted(
            {address for event in events for address in (event.function, event.call_site)}
        )
        symbols = resolve_symbols(addresses, args.elf, args.addr2line)
        invocations, unmatched_enters, unmatched_exits = match_invocations(events)

        output_path = (
            args.output if args.output is not None else args.log_file.with_suffix(".csv")
        )
        with output_path.open("w", encoding="utf-8", newline="") as trace_output:
            write_trace(trace_output, invocations, symbols, frequency)

        if args.summary:
            write_summary(
                sys.stderr,
                invocations,
                symbols,
                frequency,
                unmatched_enters,
                unmatched_exits,
            )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    sys.exit(main())

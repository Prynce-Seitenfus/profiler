#!/usr/bin/env python3
"""Parse PROF-BIN binary profiler dumps and report execution statistics."""

import argparse
import csv
import struct
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Sequence, TextIO, Tuple


HEADER_FORMAT = "<4sHHIIHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
RECORD_FORMAT = "<IIQII"
RECORD_SIZE = struct.calcsize(RECORD_FORMAT)
TRAILER_FORMAT = "<I"
TRAILER_SIZE = struct.calcsize(TRAILER_FORMAT)

OUTPUT_FIELDS = (
    "function",
    "function_address",
    "calls",
    "total_cycles",
    "min_cycles",
    "max_cycles",
    "avg_cycles",
    "avg_us",
    "min_us",
    "max_us",
    "source",
)


class ProfilerHeader(NamedTuple):
    magic: bytes
    version: int
    record_count: int
    frequency: int
    dropped_functions: int
    stack_overflows: int


class MetricRecord(NamedTuple):
    fn_address: int
    call_count: int
    total_cycles: int
    min_cycles: int
    max_cycles: int


def calculate_crc32(seed: int, data: bytes) -> int:
    crc = seed
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc


def read_binary_dump(path: Path) -> Tuple[ProfilerHeader, List[MetricRecord], int]:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE + TRAILER_SIZE:
        raise ValueError("dump file is smaller than minimum binary packet size")

    magic, version, count, frequency, dropped, stack_of = struct.unpack_from(
        HEADER_FORMAT, data, 0
    )
    if magic != b"PROF":
        raise ValueError(f"invalid magic signature: {magic!r} (expected b'PROF')")
    if version != 2:
        raise ValueError(f"unsupported PROF-BIN protocol version: {version}")

    expected_len = HEADER_SIZE + (count * RECORD_SIZE) + TRAILER_SIZE
    if len(data) < expected_len:
        raise ValueError(
            f"truncated binary dump: expected {expected_len} bytes, got {len(data)} bytes"
        )

    records: List[MetricRecord] = []
    offset = HEADER_SIZE
    for _ in range(count):
        fn_addr, calls, total_cyc, min_cyc, max_cyc = struct.unpack_from(
            RECORD_FORMAT, data, offset
        )
        offset += RECORD_SIZE
        records.append(
            MetricRecord(
                fn_address=fn_addr,
                call_count=calls,
                total_cycles=total_cyc,
                min_cycles=min_cyc,
                max_cycles=max_cyc,
            )
        )

    (trailer_crc,) = struct.unpack_from(TRAILER_FORMAT, data, offset)

    # Validate CRC-32 over header and records payload
    crc = calculate_crc32(0xFFFFFFFF, data[:offset]) ^ 0xFFFFFFFF
    if crc != trailer_crc:
        raise ValueError(
            f"CRC-32 checksum mismatch: calculated 0x{crc:08X}, expected 0x{trailer_crc:08X}"
        )

    header = ProfilerHeader(
        magic=magic,
        version=version,
        record_count=count,
        frequency=frequency,
        dropped_functions=dropped,
        stack_overflows=stack_of,
    )
    return header, records, trailer_crc


def resolve_symbols(
    addresses: Sequence[int], elf_path: Optional[Path], addr2line: str
) -> Dict[int, Tuple[str, str]]:
    if not addresses:
        return {}

    if elf_path is None:
        return {address: ("0x{:08x}".format(address), "") for address in addresses}

    command = [
        addr2line,
        "-f",
        "-C",
        "-e",
        str(elf_path),
    ] + ["0x{:08x}".format(address) for address in addresses]

    process = subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    lines = [line.strip() for line in process.stdout.splitlines()]
    if len(lines) != len(addresses) * 2:
        raise ValueError("addr2line returned an unexpected number of lines")

    symbols: Dict[int, Tuple[str, str]] = {}
    for index, address in enumerate(addresses):
        function_name = lines[index * 2]
        source_location = lines[index * 2 + 1]
        if function_name == "??":
            function_name = "0x{:08x}".format(address)
        symbols[address] = (function_name, source_location)
    return symbols


def write_metrics_csv(
    output: TextIO,
    records: Sequence[MetricRecord],
    symbols: Dict[int, Tuple[str, str]],
    frequency: int,
) -> None:
    writer = csv.DictWriter(output, fieldnames=OUTPUT_FIELDS)
    writer.writeheader()
    for record in records:
        func_name, source = symbols.get(
            record.fn_address, ("0x{:08x}".format(record.fn_address), "")
        )
        avg_cycles = (
            record.total_cycles / record.call_count if record.call_count > 0 else 0
        )
        avg_us = (avg_cycles * 1000000.0) / frequency if frequency > 0 else 0.0
        min_us = (record.min_cycles * 1000000.0) / frequency if frequency > 0 else 0.0
        max_us = (record.max_cycles * 1000000.0) / frequency if frequency > 0 else 0.0

        writer.writerow(
            {
                "function": func_name,
                "function_address": "0x{:08x}".format(record.fn_address),
                "calls": record.call_count,
                "total_cycles": record.total_cycles,
                "min_cycles": record.min_cycles,
                "max_cycles": record.max_cycles,
                "avg_cycles": f"{avg_cycles:.1f}",
                "avg_us": f"{avg_us:.3f}",
                "min_us": f"{min_us:.3f}",
                "max_us": f"{max_us:.3f}",
                "source": source,
            }
        )


def write_summary(
    output: TextIO,
    header: ProfilerHeader,
    records: Sequence[MetricRecord],
    symbols: Dict[int, Tuple[str, str]],
) -> None:
    writer = csv.writer(output)
    writer.writerow(("frequency_hz", header.frequency))
    writer.writerow(("unique_functions", header.record_count))
    writer.writerow(("dropped_functions", header.dropped_functions))
    writer.writerow(("stack_overflows", header.stack_overflows))
    writer.writerow(
        ("function", "calls", "total_cycles", "min_cycles", "avg_cycles", "max_cycles", "avg_us")
    )
    for record in sorted(records, key=lambda r: r.total_cycles, reverse=True):
        func_name = symbols.get(
            record.fn_address, ("0x{:08x}".format(record.fn_address), "")
        )[0]
        avg_cycles = (
            record.total_cycles / record.call_count if record.call_count > 0 else 0
        )
        avg_us = (avg_cycles * 1000000.0) / header.frequency if header.frequency > 0 else 0.0
        writer.writerow(
            (
                func_name,
                record.call_count,
                record.total_cycles,
                record.min_cycles,
                f"{avg_cycles:.1f}",
                record.max_cycles,
                f"{avg_us:.3f}",
            )
        )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Decode and resolve execution statistics from a PROF-BIN binary profiler dump."
    )
    parser.add_argument("dump_file", type=Path, help="PROF-BIN binary dump file")
    parser.add_argument("-e", "--elf", type=Path, help="ELF file used by arm-none-eabi-addr2line")
    parser.add_argument(
        "--addr2line",
        default="arm-none-eabi-addr2line",
        help="addr2line executable (default: arm-none-eabi-addr2line)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="write metrics CSV to this file (default: input filename with .csv suffix)",
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="print aggregate summary table to stderr",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    try:
        header, records, _ = read_binary_dump(args.dump_file)
        addresses = [record.fn_address for record in records]
        symbols = resolve_symbols(addresses, args.elf, args.addr2line)

        output_path = (
            args.output if args.output is not None else args.dump_file.with_suffix(".csv")
        )
        with output_path.open("w", encoding="utf-8", newline="") as csv_out:
            write_metrics_csv(csv_out, records, symbols, header.frequency)

        if args.summary:
            write_summary(sys.stderr, header, records, symbols)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    sys.exit(main())

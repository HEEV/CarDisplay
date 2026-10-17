#!/usr/bin/env python3
"""Convert timestamped packed sh_packet_t records to CSV (Python stdlib only).

Layout: int64_t Unix milliseconds, followed by the packed packet:
float speed, float airspeed, float temps[4], uint16_t analog[4],
uint8_t digital_in, uint8_t digital_out, uint16_t sequence.
Each entry is 44 bytes. Default byte order is little-endian.
"""

import argparse
import csv
from pathlib import Path
import struct
import sys


HEADER = [
    "timestamp_ms", "elapsed_s", "speed_mph", "airspeed_mph",
    *[f"temp_{i}_f" for i in range(4)],
    *[f"analog_{i}" for i in range(4)],
    "digital_in", "digital_out", "sequence",
]


def convert_records(input_path, output_path, endian="little", overwrite=False):
    """Stream entries to CSV. Return the number of complete records written.

    Digital bitfields are exported as decimal integers. Incomplete trailing
    entries are ignored with a warning, as can occur after an interrupted write.
    """
    input_path, output_path = Path(input_path), Path(output_path)
    if input_path.resolve() == output_path.resolve() or (
        output_path.exists() and input_path.samefile(output_path)
    ):
        raise ValueError("Input and output must be different files")

    prefix = {"little": "<", "big": ">", "native": "="}[endian]
    decoder = struct.Struct(prefix + "qff4f4HBBH")
    count = 0
    start_ms = None

    with input_path.open("rb") as source:
        with output_path.open("w" if overwrite else "x", newline="", encoding="utf-8") as target:
            writer = csv.writer(target)
            writer.writerow(HEADER)
            while True:
                raw = source.read(decoder.size)
                if not raw:
                    break
                if len(raw) != decoder.size:
                    print(
                        f"Warning: ignored {len(raw)} trailing bytes at offset "
                        f"{count * decoder.size}; expected a {decoder.size}-byte entry.",
                        file=sys.stderr,
                    )
                    break
                fields = decoder.unpack(raw)
                timestamp_ms = fields[0]
                if start_ms is None:
                    start_ms = timestamp_ms
                writer.writerow([
                    timestamp_ms, (timestamp_ms - start_ms) / 1000, *fields[1:]
                ])
                count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path, help="Binary records file")
    parser.add_argument("output", nargs="?", type=Path,
                        help="CSV path (default: input filename with .csv extension)")
    parser.add_argument("--endian", choices=("little", "big", "native"), default="little",
                        help="Byte order of the writer (default: little)")
    parser.add_argument("--overwrite", action="store_true", help="Replace an existing CSV")
    args = parser.parse_args()
    output = args.output if args.output is not None else args.input.with_suffix(".csv")
    try:
        count = convert_records(args.input, output, args.endian, args.overwrite)
    except FileExistsError:
        parser.exit(1, f"Error: {output} already exists; choose another output or use --overwrite\n")
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Error: {error}\n")
    if count == 0:
        print("Warning: no complete records; CSV contains only the header.", file=sys.stderr)
    print(f"Wrote {count} records to {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

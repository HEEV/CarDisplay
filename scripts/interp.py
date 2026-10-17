#!/usr/bin/env python3
"""Read timestamped packed sh_packet_t entries and plot speed against time.

Install: python -m pip install matplotlib
Run: python plot_speed.py records.bin

Defaults: SH_TEMP_COUNT = 4, SH_ANALOG_COUNT = 4 (44 bytes per entry).
Override with --temps N --analog M if the C header changes.
Each entry is int64_t Unix milliseconds immediately followed by sh_packet_t.
Floats must be IEEE-754 binary32. No file header or padding is expected.
Use --endian to match the machine that wrote the file (default: little).
An incomplete trailing entry is ignored with a warning. Appending after a
partial entry requires repairing that entry first, or alignment will be lost.
"""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import struct
import sys


@dataclass(frozen=True)
class Packet:
    timestamp_ms: int
    speed: float
    airspeed: float
    temps: tuple
    analog: tuple
    digital_in: int
    digital_out: int
    sequence: int


def packet_format(temp_count, analog_count, endian="little"):
    if temp_count < 0 or analog_count < 0:
        raise ValueError("Array counts must be nonnegative")
    prefix = {"little": "<", "big": ">", "native": "="}[endian]
    # Explicit byte order disables alignment/padding, matching the packed C type.
    return struct.Struct(f"{prefix}qff{temp_count}f{analog_count}HBBH")


def read_packets(path, temp_count, analog_count, endian="little"):
    """Yield all complete entries, in file order, without loading raw file bytes."""
    decoder = packet_format(temp_count, analog_count, endian)
    with open(path, "rb") as file:
        index = 0
        while True:
            raw = file.read(decoder.size)
            if not raw:
                break
            if len(raw) != decoder.size:
                print(
                    f"Warning: ignored {len(raw)} trailing bytes at offset "
                    f"{index * decoder.size}; entry size is {decoder.size} bytes.",
                    file=sys.stderr,
                )
                break
            values = decoder.unpack(raw)
            analog_start = 3 + temp_count
            digital_start = analog_start + analog_count
            yield Packet(
                timestamp_ms=values[0], speed=values[1], airspeed=values[2],
                temps=values[3:analog_start],
                analog=values[analog_start:digital_start],
                digital_in=values[digital_start],
                digital_out=values[digital_start + 1],
                sequence=values[digital_start + 2],
            )
            index += 1


def nonnegative_int(text):
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", type=Path, help="Binary log file")
    parser.add_argument("--temps", default=4, type=nonnegative_int,
                        help="SH_TEMP_COUNT from the C header (default: 4)")
    parser.add_argument("--analog", default=4, type=nonnegative_int,
                        help="SH_ANALOG_COUNT from the C header (default: 4)")
    parser.add_argument("--endian", choices=("little", "big", "native"), default="little")
    parser.add_argument("--airspeed", action="store_true", help="Also plot airspeed")
    parser.add_argument("--wall-time", action="store_true", help="Use UTC clock time on x-axis")
    parser.add_argument("--output", type=Path, help="Save graph, e.g. speed.png or speed.pdf")
    parser.add_argument("--no-show", action="store_true", help="Save without opening a window")
    args = parser.parse_args()
    if args.no_show and args.output is None:
        parser.error("--no-show requires --output")

    try:
        packets = list(read_packets(args.file, args.temps, args.analog, args.endian))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Error: {error}\n")
    if not packets:
        parser.exit(1, "Error: no complete entries found\n")
    if any(b.timestamp_ms < a.timestamp_ms for a, b in zip(packets, packets[1:])):
        print("Warning: timestamps go backward; plotting in file order.", file=sys.stderr)

    try:
        import matplotlib
        if args.no_show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import matplotlib.dates as mdates
    except ImportError:
        parser.exit(1, "Install matplotlib: python -m pip install matplotlib\n")

    if args.wall_time:
        try:
            times = [datetime.fromtimestamp(p.timestamp_ms / 1000, timezone.utc)
                     for p in packets]
        except (ValueError, OverflowError, OSError) as error:
            parser.exit(1, f"Invalid timestamp; check array counts and byte order: {error}\n")
        xlabel = "Time (UTC)"
    else:
        start = packets[0].timestamp_ms
        times = [(p.timestamp_ms - start) / 1000 for p in packets]
        xlabel = "Elapsed time (s)"

    fig, ax = plt.subplots(figsize=(11, 5), layout="constrained")
    ax.plot(times, [p.speed for p in packets], label="Wheel speed",
            linewidth=1.5, marker="." if len(packets) == 1 else None)
    if args.airspeed:
        ax.plot(times, [p.airspeed for p in packets], label="Airspeed", linewidth=1.2,
                marker="." if len(packets) == 1 else None)
        ax.legend()
    ax.set(title="Speed over time", xlabel=xlabel, ylabel="Speed (mph)")
    ax.grid(True, alpha=0.25)
    if args.wall_time:
        locator = mdates.AutoDateLocator(tz=timezone.utc)
        ax.xaxis.set_major_locator(locator)
        ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator, tz=timezone.utc))

    print(f"Read {len(packets)} entries; "
          f"{packet_format(args.temps, args.analog, args.endian).size} bytes per entry")
    if args.output:
        fig.savefig(args.output, dpi=160)
        print(f"Saved {args.output}")
    if not args.no_show:
        plt.show()
    plt.close(fig)
    return 0


if __name__ == "__main__":
    sys.exit(main())

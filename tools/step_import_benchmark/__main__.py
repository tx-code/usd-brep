# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path

from .profile import census, select_cohorts, write_json


def main():
    parser = argparse.ArgumentParser(description="Local STEP library census and scoped import benchmarks")
    commands = parser.add_subparsers(dest="command", required=True)
    inventory = commands.add_parser("census")
    inventory.add_argument("--root", type=Path, required=True)
    inventory.add_argument("--output", type=Path, required=True)
    run = commands.add_parser("run")
    run.add_argument("--census", type=Path, required=True)
    run.add_argument("--cohorts", type=Path, required=True)
    run.add_argument("--source-probe", type=Path, required=True)
    run.add_argument("--native-probe", type=Path, required=True)
    run.add_argument("--occt-probe", type=Path, required=True)
    run.add_argument("--occt-bin", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--scope", choices=("source-scan", "paired", "large"), required=True)
    run.add_argument("--repeat", type=int, default=1)
    run.add_argument("--warmup", type=int, default=0)
    run.add_argument("--timeout", type=float, default=90)
    run.add_argument("--maximum-private-bytes", type=int, default=2*1024*1024*1024)
    run.add_argument("--resume", action="store_true")
    report = commands.add_parser("report")
    report.add_argument("--census", type=Path, required=True)
    report.add_argument("--cohorts", type=Path, required=True)
    report.add_argument("--run", type=Path, action="append", required=True)
    report.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "census":
        if args.output.exists():
            parser.error("Census output already exists")
        if not args.root.is_dir():
            parser.error("Model root must be a directory")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        report = census(args.root, args.output, lambda text: print(text, flush=True))
        write_json(args.output.with_name("cohorts.json"), select_cohorts(report))
        print(f"Profiled {report['summary']['profiled_files']} STEP files", flush=True)
        return 1 if report["errors"] else 0
    if args.command == "run":
        from .runner import batch
        if (not 1 <= args.repeat <= 50 or not 0 <= args.warmup <= 5 or
                args.timeout <= 0 or args.maximum_private_bytes <= 0):
            parser.error("Positive, bounded benchmark parameters required")
        probes = {name: getattr(args, name+"_probe").resolve(strict=True) for name in ("source", "native", "occt")}
        batch(args.census, args.cohorts, args.output, probes, args.scope, repeat=args.repeat, warmup=args.warmup,
              timeout=args.timeout, maximum_private=args.maximum_private_bytes,
              occt_bin=args.occt_bin.resolve(strict=True),
              resume=args.resume)
        print("Benchmark batch complete", flush=True)
        return 0
    if args.command == "report":
        from .report import render
        render(args.census, args.cohorts, args.run, args.output)
        print(str(args.output), flush=True)
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

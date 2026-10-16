# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""CLI for the ticket-01 synthetic corpus. No product reader is invoked."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from .common import InvalidEvidence, load_json
from .controls import execute
from .corpus import build, verify
from .evidence import check, run


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("build", help="materialize a NEW, never-overwritten corpus directory")
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    for name in ("verify", "run", "controls"):
        command = commands.add_parser(name)
        command.add_argument("--corpus", type=Path, required=True)
        if name in {"verify", "run"}:
            command.add_argument("--case", action="append", dest="selected")
        if name in {"run", "controls"}:
            command.add_argument("--output-root", type=Path, required=True)
    validate = commands.add_parser("check-evidence")
    validate.add_argument("--run", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            result = build(args.repo.resolve(), args.output)
        elif args.command == "verify":
            full = verify(args.corpus.resolve(), selected=args.selected)
            result = {key: value for key, value in full.items() if key != "cases"}
        elif args.command == "run":
            path = run(args.corpus.resolve(), args.output_root, args.selected)
            result = {"run": str(path), **check(path)}
        elif args.command == "controls":
            path = execute(args.corpus.resolve(), args.output_root)
            summary = load_json(path / "summary.json")
            result = {"report": str(path), "variants": summary["variants"], "passed": summary["passed"],
                      "protected_materials_unchanged": summary["protected_materials_unchanged"],
                      "subject_execution": "NotRun"}
            if not summary["all_passed"]:
                print(json.dumps(result, ensure_ascii=False, indent=2))
                return 1
        else:
            result = check(args.run)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except InvalidEvidence as exc:
        print(json.dumps({"scope": "Ticket01Tool", "code": exc.code, "detail": exc.detail},
                         ensure_ascii=False, indent=2, default=str), file=sys.stderr)
        return 2
    except (OSError, KeyError, TypeError, ValueError, RecursionError) as exc:
        print(json.dumps({"scope": "Ticket01Tool", "code": "Tool.InfrastructureOrContractFailure",
                          "detail": str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

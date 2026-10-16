# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from tools.step_import_validation.common import InvalidEvidence
from .runner import check, run


def main():
    parser = argparse.ArgumentParser(description="Run actual C++ source inspection; no geometric import.")
    commands = parser.add_subparsers(dest="command", required=True)
    execute = commands.add_parser("run")
    execute.add_argument("--binary", type=Path, required=True)
    execute.add_argument("--corpus", type=Path, required=True)
    execute.add_argument("--output-root", type=Path, required=True)
    execute.add_argument("--case", action="append", dest="selected")
    inspect = commands.add_parser("check-evidence")
    inspect.add_argument("--run", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "run":
            result = run(args.binary, args.corpus, args.output_root, args.selected)
            print(json.dumps({"run": str(result), **check(result)}, indent=2))
        else:
            print(json.dumps(check(args.run), indent=2))
        return 0
    except (InvalidEvidence, OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as exc:
        print(json.dumps({"scope": "Ticket02Harness", "error": str(exc)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

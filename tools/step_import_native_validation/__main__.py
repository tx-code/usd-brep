# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Run all frozen contracts with supplied current binaries; save results locally."""
import argparse
import json
import subprocess
import sys

from .qualify import STAGES, qualify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for stage in STAGES:
        parser.add_argument("--" + stage, required=True, help="Actual " + stage + " executable")
    parser.add_argument("--output", required=True, help="New local directory; must not already exist")
    args = parser.parse_args()
    try:
        report = qualify({stage: getattr(args, stage) for stage in STAGES}, args.output)
    except (OSError, ValueError, AssertionError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps({"status": report["status"], "native_outcomes": report["native_outcomes"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

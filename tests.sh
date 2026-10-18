#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
platform="${1:?Usage: tests.sh platform [test options]}"
shift
runtime="${SMLIB_RUNTIME_DIR:-$repo_root/_build/$platform/release}"
exec "$repo_root/_build/target-deps/python/bin/python3" "$repo_root/tools/agent/run_geometry_tests.py" --runtime "$runtime" --native "$@"

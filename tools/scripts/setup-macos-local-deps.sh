#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "${script_dir}/../.." && pwd -P)"
python_root="${SMLIB_PYTHON_ROOT:?Set SMLIB_PYTHON_ROOT to a Python 3.12 prefix}"
[[ -d "$python_root/include/python3.12" ]] || { echo "Missing Python 3.12 headers" >&2; exit 1; }
mkdir -p "$repo_root/_build/target-deps"
ln -sfn "$python_root" "$repo_root/_build/target-deps/python"

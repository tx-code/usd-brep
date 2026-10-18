#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/../.." && pwd -P)"
# The shared Python runner accepts --target and --dev. SMLIB_RUNTIME_DIR
# overrides CMake discovery for custom generators or installed SDK layouts.
python_bin="$repo_root/_build/target-deps/python/bin/python3"
runtime="${SMLIB_RUNTIME_DIR:-${SMLIB_CMAKE_BUILD_DIR:-$repo_root/_build/cam}/bin}"
if [[ -z ${SMLIB_RUNTIME_DIR:-} && -d "$runtime/Release" ]]; then runtime="$runtime/Release"; fi
exec "$python_bin" "$repo_root/tools/agent/run_geometry_tests.py" --runtime "$runtime" "$@"

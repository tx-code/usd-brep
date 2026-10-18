#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/../.." && pwd -P)"
# Accept the former platform argument; CMake owns the actual build layout.
if [[ ${1:-} == linux-* || ${1:-} == macos-* ]]; then shift; fi
runtime="${SMLIB_RUNTIME_DIR:-${SMLIB_CMAKE_BUILD_DIR:-$repo_root/_build/cam}/bin}"
if [[ -z ${SMLIB_RUNTIME_DIR:-} && -d "$runtime/Release" ]]; then runtime="$runtime/Release"; fi
runtime="$(cd "$runtime" && pwd -P)"
export LD_LIBRARY_PATH="$runtime${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DYLD_LIBRARY_PATH="$runtime${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
cd "$script_dir"
exec "$runtime/SM_API_test_app" "$@"

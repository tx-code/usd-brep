# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Expose the shared CMake driver through the retained repo launcher."""

from tools.scripts.smlib_build import add_arguments, run


def setup_build_tool(parser, config):
    add_arguments(parser, "build")
    return lambda options, _config: run(options)


def setup_fetch_tool(parser, config):
    add_arguments(parser, "fetch")
    return lambda options, _config: run(options)

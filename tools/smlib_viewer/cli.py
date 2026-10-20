# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""CLI parsing stays independent of Qt/VTK, including in a frozen application."""
import argparse
from pathlib import Path
import sys


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if args[:1] == ["--mesh-worker"]:
        from .mesh_worker import run
        return run(*args[1:]) if len(args) == 3 else 2
    parser = argparse.ArgumentParser(prog="brep-view", description="轻量只读模型查看器：STEP / STL / OBJ / PLY / VTP / VTK")
    parser.add_argument("file", nargs="?", help="模型文件；也可在窗口中拖入或打开")
    parser.add_argument("--profile", choices=["development", "tolerance-study", "work-tolerance-study"],
                        default="development",
                        help="显式开发/研究配置，不自动放宽或重试（默认 development）")
    parser.add_argument("--normalize-periodic-faces", action="store_true")
    parser.add_argument("--restrict-unused-support-domains", action="store_true")
    parser.add_argument("--strict-source", action="store_true", help="禁用非几何互操作；默认保留已证明无关的原诊断")
    parser.add_argument("--edges", action="store_true", help="显示 BRep 边界（网格文件显示特征边）")
    parser.add_argument("--wireframe", action="store_true", help="显示三角网格")
    parser.add_argument("--screenshot", type=Path, help="仅在本地保存画面")
    parser.add_argument("--quit-after-load", action="store_true", help="载入/截图完成后退出；失败返回非零")
    options = parser.parse_args(args)
    from .app import run
    return run(options)

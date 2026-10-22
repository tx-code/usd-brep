# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""CLI parsing stays independent of Qt/VTK, including in a frozen application."""
import argparse
from pathlib import Path
import sys
from .interpretation import SCHEMA_POLICIES, CARRIER_POLICIES, UNIT_POLICIES, validate


def main(argv=None):
    from .runtime_cache import configure
    configure()
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
    parser.add_argument("--schema-interpretation", choices=SCHEMA_POLICIES, default="DeclaredOnly")
    parser.add_argument("--unit-interpretation", choices=UNIT_POLICIES, default="DeclaredOnly")
    parser.add_argument("--carrier-interpretation", choices=CARRIER_POLICIES, default="DeclaredOnly")
    parser.add_argument("--preserve-opaque-file-name-text", action="store_true",
                        help="显式保留 FILE_NAME 未知编码元信息，不猜测代码页")
    parser.add_argument("--assume-ascii-invariant-data", action="store_true",
                        help="显式假定 DATA 高位字节不表示 ASCII；保留未知文本和诊断")
    parser.add_argument("--report-budget-mib", type=int, choices=(16, 32, 64, 128, 256), default=16,
                        help="显式报告容量上限；不改变几何预算（默认 16 MiB）")
    parser.add_argument("--edges", action="store_true", help="显示 BRep 边界（网格文件显示特征边）")
    parser.add_argument('--repair-source-sha256', default='')
    parser.add_argument('--max-face-orientation-edits', type=int, default=0, dest='maximum_face_orientation_edits')
    parser.add_argument('--max-repair-proof-controls', type=int, default=0, dest='maximum_repair_proof_controls')
    parser.add_argument('--max-repair-proof-products', type=int, default=0, dest='maximum_repair_proof_products')
    parser.add_argument('--repair-face-orientation', action='append', default=[], dest='face_orientation_edits',
                        help='显式单文件朝向修复：definition:shape:face:original:effective，朝向为 T/F')
    parser.add_argument("--wireframe", action="store_true", help="显示三角网格")
    parser.add_argument("--screenshot", type=Path, help="仅在本地保存画面")
    parser.add_argument("--quit-after-load", action="store_true", help="载入/截图完成后退出；失败返回非零")
    parser.add_argument("--result", type=Path, help="将本次查看结果写入新的本地 JSON 文件")
    parser.add_argument("--scan", type=Path, action="append", help="递归扫描目录，可重复指定多个目录")
    parser.add_argument("--output", type=Path, help="扫描结果目录，必须是不存在的新目录")
    parser.add_argument("--thumbnails", action="store_true", help="扫描时保存本地缩略图")
    parser.add_argument("--timeout", type=float, default=150, help="扫描单文件完整查看流程的秒数上限（默认 150）")
    parser.add_argument("--scan-child", action="store_true", help=argparse.SUPPRESS)
    options = parser.parse_args(args)
    try:
        from .repair import arguments as repair_arguments
        repair = repair_arguments(options)
        if repair and (options.scan or not options.file):
            raise ValueError('RepairPlanRequiresOneExplicitFile')
        validate(options.schema_interpretation, options.carrier_interpretation, options.strict_source,
                 options.preserve_opaque_file_name_text, options.assume_ascii_invariant_data,
                 options.unit_interpretation)
    except ValueError as error:
        parser.error(str(error))
    if options.scan:
        if options.file or not options.output or options.result or options.screenshot or options.scan_child:
            parser.error("--scan 需要 --output，不能同时指定单文件、--result 或 --screenshot")
        from .scan import run_scan
        try:
            return run_scan(options)
        except (OSError, ValueError) as error:
            print(str(error), file=sys.stderr)
            return 2
        except KeyboardInterrupt:
            print("扫描已取消", file=sys.stderr)
            return 130
    if options.output or options.thumbnails:
        parser.error("--output 和 --thumbnails 仅适用于 --scan")
    if options.result and (not options.file or not options.quit_after_load or options.result.exists()):
        parser.error("--result 需要单文件输入和 --quit-after-load，且目标文件必须不存在")
    if options.file:
        source = Path(options.file).expanduser().resolve()
        for output in (options.result, options.screenshot):
            if output and (output.resolve() == source or
                           (source.exists() and output.exists() and output.samefile(source))):
                parser.error("输出不能覆盖输入模型")
    if options.scan_child:
        if not options.result or not options.quit_after_load or sys.stdin.readline().strip() != "start":
            return 2
        import os
        os.environ["PYVISTA_OFF_SCREEN"] = "true"
    from .app import run
    return run(options)

# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Offline local results. All model-derived strings are escaped before display."""
from html import escape
import json
from pathlib import Path
import statistics


def _table(headers, rows):
    head = "".join("<th>"+escape(str(value))+"</th>" for value in headers)
    body = "".join("<tr>"+"".join("<td>"+escape(str(value))+"</td>" for value in row)+"</tr>" for row in rows)
    return "<table><thead><tr>"+head+"</tr></thead><tbody>"+body+"</tbody></table>"


def render(census_path, cohorts_path, batches, output):
    census = json.loads(census_path.read_bytes())
    cohorts = json.loads(cohorts_path.read_bytes())
    summary = census["summary"]
    parts = ["<h1>STEP 模型库：读取覆盖与性能基线</h1>",
             "<p>本地原始文件，Release 新进程；报告中的成功只指对应阶段完成。原始解析、OCCT schema 读取、"
             "严格导入与默认修复转换的工作范围不同，因此本报告不计算跨引擎加速比。</p>",
             f"<div class='cards'><b>{summary['profiled_files']}<small>已画像文件</small></b>"
             f"<b>{summary['unique_contents']}<small>独立内容 SHA256</small></b>"
             f"<b>{summary['total_bytes']/1e9:.2f} GB<small>STEP 总字节</small></b>"
             f"<b>{len(cohorts['holdout'])}<small>保留盲测内容，本轮不执行</small></b></div>",
             "<h2>协议画像</h2><p>HEADER 受限词法提取，按每个文件计数；不是协议符合性证明。</p>",
             _table(("FILE_SCHEMA", "文件数"), summary["schema_files"].items()),
             "<h2>常用实体组件</h2><p>排除注释与字符串，包含复合实体的所有外部组件；统计出现该组件的文件数。</p>",
             _table(("组件", "文件数"), list(summary["entity_component_files"].items())[:35])]
    geometry = ("LINE", "CIRCLE", "ELLIPSE", "B_SPLINE_CURVE_WITH_KNOTS", "RATIONAL_B_SPLINE_CURVE",
                "SURFACE_CURVE", "SEAM_CURVE", "PCURVE", "PLANE", "CYLINDRICAL_SURFACE", "CONICAL_SURFACE",
                "SPHERICAL_SURFACE", "TOROIDAL_SURFACE", "B_SPLINE_SURFACE_WITH_KNOTS", "RATIONAL_B_SPLINE_SURFACE",
                "SURFACE_OF_REVOLUTION", "SURFACE_OF_LINEAR_EXTRUSION", "BREP_WITH_VOIDS",
                "NEXT_ASSEMBLY_USAGE_OCCURRENCE", "CONTEXT_DEPENDENT_SHAPE_REPRESENTATION", "MAPPED_ITEM")
    parts += ["<h2>几何与装配扩展的实际需求</h2>",
              _table(("实体组件", "文件数"),
                     [(name, summary["entity_component_files"].get(name, 0)) for name in geometry])]
    paired = None
    for path in batches:
        data = json.loads((path/"results.json").read_bytes())
        scope = data["config"]["scope"]
        if scope == "paired":
            paired = data
        parts += ["<h2>"+escape(path.name)+"</h2>",
                  "<p>文件计数；重复测量先取每文件中位数，再汇总。成功耗时排除 Unsupported、失败和输入预算未执行。</p>"]
        rows = []
        diagnostics = []
        memory_rows = []
        for name, group in data["summary"]["groups"].items():
            timing = group["successful_operation_ms"]
            outcomes = "; ".join(f"{k}: {v}" for k, v in group["outcomes"].items())
            failures = "; ".join(f"{k}: {v}" for k, v in group["supervisor_status"].items() if k != "Completed")
            rows.append((name, group["files"], outcomes, group["model_files"],
                         f"{timing['median']:.3f}" if timing["median"] is not None else "—",
                         f"{timing['p95']:.3f}" if timing["p95"] is not None else "—", failures or "—"))
            diagnostics += [(name, key, count) for key, count in group["diagnostics"].items()]
            peaks = []
            for row in data["rows"]:
                if row["engine"]+":"+row["mode"] != name or not row["observation"]["result"]:
                    continue
                values = [t["memory_after"]["process_lifetime_peak_commit_bytes"]
                          for t in row["observation"]["result"]["trials"] if t["memory_after"]]
                if values:
                    peaks.append(max(values)/1024**2)
            memory_rows.append((name, f"{statistics.median(peaks):.2f}" if peaks else "—",
                                f"{max(peaks):.2f}" if peaks else "—"))
        parts.append(_table(("引擎:阶段", "文件", "阶段结果", "有模型", "成功中位数 ms", "成功 P95 ms", "监管停止/进程失败"), rows))
        parts.append(_table(("引擎:阶段", "首个诊断", "文件数"), diagnostics))
        parts.append("<p>下表为进程生命周期峰值提交量的观测，含 DLL、warmup 和多次测量；不是单模型净分配。</p>")
        parts.append(_table(("引擎:阶段", "峰值提交量中位数 MiB", "最大观测 MiB"), memory_rows))
    joint = 0
    if paired:
        by_case = {}
        for row in paired["rows"]:
            by_case.setdefault(row["input_sha256"], {})[row["engine"]+":"+row["mode"]] = row["observation"]
        for case in by_case.values():
            own, occt = case.get("native:import", {}), case.get("occt:transfer", {})
            if not own.get("result") or not occt.get("result"):
                continue
            own_trials = [t for t in own["result"]["trials"] if not t["warmup"]]
            occt_trials = [t for t in occt["result"]["trials"] if not t["warmup"]]
            joint += all(t["status"] == "Accepted" and t["has_model"] for t in own_trials) and all(
                t["status"] == "Transferred" and t["has_model"] and t["model"]["brep_check_valid"] for t in occt_trials)
    parts += ["<h2>比较边界</h2>", f"<p>开发样本中，两边均完成模型且 OCCT 自检通过：<strong>{joint}</strong> 个。"
              "这仍不是独立几何真值验证。没有共同成功的真实模型时，无法建立完整导入性能比。</p>",
              "<ul><li>自研 source 包含捕获、SHA256、词法、引用索引；OCCT ReadFile 还解释 schema 实体。</li>"
              "<li>自研 import 包含严格验收和必需报告；OCCT operation 为初始化＋读取＋TransferRoots，默认 FixShape；"
              "其 BRepCheck 和量测单独计时。它们不是等价精度/修复契约。</li>"
              "<li>文件在启动子进程前经 SHA256 校验，会预热文件缓存；不声称冷盘性能。DLL/进程启动仅在 process_wall_ms。</li>"
              "<li>大模型单列：自研仍按 64MiB 读取上限执行准入，OCCT 大型组上限 512MiB；不同预算不组成速度比。</li>"
              "<li>父进程按20ms采样私有提交量，超限结束子进程，可能有超调；超时、内存停止、崩溃保留为独立结果。</li>"
              "<li>OCCT 版本与加载 DLL、各次原始状态/时间/内存、输入及二进制 SHA256 在对应 JSON 中保留。</li></ul>"]
    html = ("<!doctype html><html lang='zh-CN'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width'>"
            "<title>STEP 模型库基线</title><style>body{font:16px/1.65 system-ui,'Microsoft YaHei';max-width:1240px;"
            "margin:40px auto;padding:0 24px;color:#203044;background:#f4f7fa}h1{font-size:30px}h2{margin-top:38px}"
            "table{border-collapse:collapse;width:100%;background:white;font-size:14px;margin:14px 0 24px}"
            "td,th{padding:10px;"
            "border:1px solid #dce3ec;text-align:left;overflow-wrap:anywhere}th{background:#e7eef6}.cards{display:flex;"
            "gap:20px;flex-wrap:wrap}.cards b{font-size:28px;background:white;padding:18px 24px;border-radius:10px}"
            "small{display:block;font-size:13px;font-weight:400}p,li{color:#46596e}</style><body>" +
            "".join(parts) + "</body></html>")
    output.write_text(html, encoding="utf-8")
    return dict(common_model_results=joint, speedup_ratio=None)

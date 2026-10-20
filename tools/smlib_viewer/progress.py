# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Bounded line protocol; stage counts never imply an overall completion ratio."""
from dataclasses import dataclass


LABELS = {
    "start": "启动导入进程", "capture": "读取文件", "check": "检查源模型", "build": "构建与验证 BRep",
    "report": "整理导入报告", "copy": "准备显示副本", "mesh": "生成显示网格",
    "mesh-points": "写入网格顶点", "mesh-faces": "写入网格面片", "boundary": "生成边界线",
    "boundary-points": "写入边界顶点", "boundary-lines": "写入边界线", "publish": "准备显示数据",
    "display": "载入显示数据", "render": "绘制模型",
}


@dataclass(frozen=True)
class Progress:
    phase: str
    completed: int = 0
    total: int = 0

    @property
    def text(self):
        count = f" · {self.completed:,} / {self.total:,}" if self.total else ""
        return LABELS[self.phase] + count + "…"


class ProgressReader:
    def __init__(self):
        self.pending = b""

    def feed(self, data: bytes):
        # QProcess reads arbitrary chunks, including split UTF-8/line boundaries.
        # Ignore foreign output and oversized lines instead of retaining it.
        lines = (self.pending + data).split(b"\n")
        self.pending = lines.pop()[-256:]
        result = []
        for line in lines:
            if not line.startswith(b"@brep-view:") or len(line) > 256:
                continue
            try:
                phase, completed, total = line[11:].decode("ascii").strip().split(",")
                completed, total = int(completed), int(total)
                if phase in LABELS and 0 <= completed <= total <= 2**63 - 1:
                    result.append(Progress(phase, completed, total))
            except (ValueError, UnicodeError):
                continue
        return result

# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Ticket-02 evidence adapter; comparison and qualification stay stage-specific."""
from tools.step_evidence.package import Runner, Stage
from .oracle import compare, position, require

FORMAT = "t02.evidence.v1"
_adapter = Runner(Stage(
    ticket="02", scope="SyntaxIndexReferences", status_kind="source",
    description='02检查语法、索引和引用；AP规则、单位、几何与SMLib构建仍未运行。',
    components=('STEP_SOURCE',),
    dependencies=('step_import_validation', 'step_inspect_validation'),
    compare=compare, witness=None, truth=False,
))
run = _adapter.run
check = _adapter.check
source_identity = _adapter.source_identity
page = _adapter.page

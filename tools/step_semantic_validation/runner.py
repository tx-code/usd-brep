# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Ticket-03 evidence adapter; comparison and qualification stay stage-specific."""
from tools.step_evidence.package import Runner, Stage, Witness
from .oracle import compare, REFERENCE, REFERENCE_SHA

FORMAT = "t03.evidence.v1"
_adapter = Runner(Stage(
    ticket="03", scope="SinglePartTypesSI", status_kind="semantic",
    description='03检查名称/OID、字段/祖先、单零件与SI单位；全局规则、conversion、几何与SMLib构建仍未运行。',
    components=('STEP_SOURCE', 'STEP_SEMANTICS'),
    dependencies=('step_import_validation', 'step_inspect_validation', 'step_semantic_validation'),
    compare=compare, witness=Witness(REFERENCE, REFERENCE_SHA), truth=False,
))
run = _adapter.run
check = _adapter.check
source_identity = _adapter.source_identity
page = _adapter.page

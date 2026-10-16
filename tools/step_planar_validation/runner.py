# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Ticket-04 evidence adapter; comparison and qualification stay stage-specific."""
from tools.step_evidence.package import Runner, Stage, Witness
from .oracle import compare, REFERENCE, REFERENCE_SHA

FORMAT = "t04.evidence.v1"
_adapter = Runner(Stage(
    ticket="04", scope="PlanarSourceRulesAndRegions", status_kind="planar",
    description='04检查必要源规则和有向平面区域；conversion、目标UV、SMLib构建与完整导入仍未运行。',
    components=('STEP_SOURCE', 'STEP_SEMANTICS', 'STEP_PLANAR'),
    dependencies=('step_import_validation', 'step_inspect_validation',
                  'step_semantic_validation', 'step_planar_validation'),
    compare=compare, witness=Witness(REFERENCE, REFERENCE_SHA), truth=True,
    native_options=("--fixture-limits", "--quality", "Q-P0-mm-01"),
))
run = _adapter.run
check = _adapter.check
source_identity = _adapter.source_identity
page = _adapter.page

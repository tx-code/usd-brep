# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Versioned project-owned source inventories, checked without live source files.

This protocol inventory is intentionally versioned: changed dependency structure requires review of this contract.
File contents may change, and historical hashes never need to match the current checkout.
"""

INVENTORIES = {
    'native:STEP_PLANAR': (
        'source/STEP_PLANAR/CMakeLists.txt',
        'source/STEP_PLANAR/app/step_planar.cpp',
        'source/STEP_PLANAR/inc/StepPlanar.h',
        'source/STEP_PLANAR/premake5.lua',
        'source/STEP_PLANAR/src/StepExact.cpp',
        'source/STEP_PLANAR/src/StepPlanar.cpp',
        'source/STEP_PLANAR/src/StepPlanarGeometry.cpp',
        'source/STEP_PLANAR/src/StepPlanarInternal.h',
        'source/STEP_PLANAR/src/StepPlanarJson.cpp',
        'source/STEP_PLANAR/src/StepPlanarRules.cpp',
        'source/STEP_PLANAR/src/StepRuleCatalog.h',
        'source/STEP_PLANAR/tests/StepPlanarTests.cpp',
    ),
    'native:STEP_SEMANTICS': (
        'source/STEP_SEMANTICS/CMakeLists.txt',
        'source/STEP_SEMANTICS/app/step_semantics.cpp',
        'source/STEP_SEMANTICS/inc/StepSemantics.h',
        'source/STEP_SEMANTICS/premake5.lua',
        'source/STEP_SEMANTICS/src/StepCatalog.cpp',
        'source/STEP_SEMANTICS/src/StepCatalog.h',
        'source/STEP_SEMANTICS/src/StepSemanticJson.cpp',
        'source/STEP_SEMANTICS/src/StepSemantics.cpp',
        'source/STEP_SEMANTICS/tests/StepSemanticsTests.cpp',
    ),
    'native:STEP_SOURCE': (
        'source/STEP_SOURCE/CMakeLists.txt',
        'source/STEP_SOURCE/app/step_inspect.cpp',
        'source/STEP_SOURCE/inc/StepSource.h',
        'source/STEP_SOURCE/premake5.lua',
        'source/STEP_SOURCE/src/StepInspectJson.cpp',
        'source/STEP_SOURCE/src/StepSource.cpp',
        'source/STEP_SOURCE/tests/StepSourceTests.cpp',
    ),
    'tool:step_evidence': (
        'tools/step_evidence/__init__.py',
        'tools/step_evidence/dependencies.py',
        'tools/step_evidence/package.py',
        'tools/step_evidence/presentation.py',
        'tools/step_evidence/tests/test_packages.py',
    ),
    'tool:step_import_validation': (
        'tools/step_import_validation/__init__.py',
        'tools/step_import_validation/__main__.py',
        'tools/step_import_validation/common.py',
        'tools/step_import_validation/controls.py',
        'tools/step_import_validation/corpus.py',
        'tools/step_import_validation/evidence.py',
        'tools/step_import_validation/fixtures.py',
        'tools/step_import_validation/oracle.py',
        'tools/step_import_validation/records.py',
        'tools/step_import_validation/reference.py',
        'tools/step_import_validation/tests/test_validation.py',
    ),
    'tool:step_inspect_validation': (
        'tools/step_inspect_validation/__init__.py',
        'tools/step_inspect_validation/__main__.py',
        'tools/step_inspect_validation/oracle.py',
        'tools/step_inspect_validation/runner.py',
        'tools/step_inspect_validation/tests/test_inspect.py',
    ),
    'tool:step_planar_validation': (
        'tools/step_planar_validation/__main__.py',
        'tools/step_planar_validation/build_reference.py',
        'tools/step_planar_validation/contracts/n2635-rules-v1.json',
        'tools/step_planar_validation/oracle.py',
        'tools/step_planar_validation/runner.py',
        'tools/step_planar_validation/tests/test_planar.py',
    ),
    'tool:step_semantic_validation': (
        'tools/step_semantic_validation/__init__.py',
        'tools/step_semantic_validation/__main__.py',
        'tools/step_semantic_validation/build_reference.py',
        'tools/step_semantic_validation/contracts/n2635-reference-v2.json',
        'tools/step_semantic_validation/oracle.py',
        'tools/step_semantic_validation/runner.py',
        'tools/step_semantic_validation/tests/test_semantics.py',
    ),
}

PROVENANCE = "execution-dependencies.v3"
VERSIONS = {
    "execution-dependencies.v1": INVENTORIES,
    "execution-dependencies.v2": {
        **INVENTORIES,
        "native:STEP_SOURCE": INVENTORIES["native:STEP_SOURCE"] + (
            "source/STEP_SOURCE/inc/StepSourceReport.h",
            "source/STEP_SOURCE/src/StepSourceReport.cpp",
        ),
        "native:STEP_SEMANTICS": INVENTORIES["native:STEP_SEMANTICS"] + (
            "source/STEP_SEMANTICS/inc/StepSemanticReport.h",
        ),
    },
}

# Historical inventories validate saved source identities without consulting the
# checkout. Keep v1/v2 intact; new runs no longer depend on retired project files.
VERSIONS[PROVENANCE] = {
    group: tuple(path for path in paths if not path.endswith("/premake5.lua"))
    for group, paths in VERSIONS["execution-dependencies.v2"].items()
}


def required_sources(components, dependencies, provenance):
    groups = ["tool:step_evidence"]
    groups.extend("native:" + name for name in components)
    groups.extend("tool:" + name for name in dependencies)
    return {path for group in groups for path in VERSIONS[provenance][group]}

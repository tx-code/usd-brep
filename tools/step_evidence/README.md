# Import evidence package

`package.py` owns the shared execution, staging, sealing, publication and offline-recheck protocol for the existing
02/03/04 evidence adapters. Their `run`/`check` imports and CLI commands remain the test surface. The adapters declare
their actual scope, comparison function, witness, native options, observation filename and existing status/counter
conventions; they do not implement the publication protocol again.

Native stdout is saved verbatim. Comparison is still independent: raw observation checking lives in
`step_inspect_validation/oracle.py`, without execution imports; the semantic and planar comparators retain their logic.
The frozen 01 tool, corpus and mathematical/EXPRESS witnesses are unchanged. This module does not read STEP geometry,
build SMLib models or grant complete-import qualification.

New packages retain the stage's `t02/t03/t04.evidence.v1` envelope and explicitly record
`package_protocol=step-evidence.v2`, `source_provenance=execution-dependencies.v3` and their captured execution directory.
All three producer adapters capture transitive tool/witness identities plus native source identities and verify those
files and the executable again before publication. HTML receives the stage description directly, without replacing
another stage's prose.

`dependencies.py` defines the versioned project-owned source inventory for that provenance policy. New-protocol
recheck requires the complete relevant inventory (including stage, comparator, common tool, native source and witness
identities) without reading live source files. A changed dependency structure requires reviewing this contract.
The v2 inventory added the shared native report writer and composition headers. The v3 inventory
removes retired project-generator files. Packages declaring v1 or v2 still use their frozen inventory;
they do not acquire new source-coverage claims or require deleted files in the current checkout.

An absent `package_protocol` denotes the historical stage protocol. Unknown declared protocols are rejected.
Historical packages keep their recorded source coverage and qualification: recheck never requires the original binary,
original staging input path or current source hashes to exist or match. Captured commands are checked lexically against
the saved run/case identities. This also permits relocation and paths captured on a different OS.

Recheck resolves the actual package location before deciding whether it is published; validates canonical payload
paths, hashes and complete payload coverage; checks preparation/oracle/input/command/status/counter consistency; and
replays each stage's comparator from saved bytes. New packages also require exactly their declared selected payload.
Preparation counts and retained input proofs are replayed against the input through the unchanged 01 ledger and
reviewed anchor/oracle functions, rather than accepting a Passed label or a truth hash alone.
Historical packages may retain indexed additional payload, with canonical paths and full census checked. Neither
integrity checks nor source hashes authenticate an adversary's rewritten claims about which program actually ran.

Failures retain their append-only staging identity. The final source/binary stability guard and staged recheck precede
publication by directory rename; retries use a new run ID. A staging alias is not a published run. A failure after sealing
can retain a sealed `.partial` directory, which public recheck still rejects as unpublished.

The shared package tests use real executable dependencies through the existing stage interfaces:

```powershell
$env:STEP_INSPECT_BINARY='<step_inspect.exe>'
$env:STEP_SEMANTIC_BINARY='<step_semantic_inspect.exe>'
$env:STEP_PLANAR_BINARY='<step_planar_inspect.exe>'
# Optional outside the historical acceptance machine; set for the historical compatibility control.
$env:STEP_LEGACY_EVIDENCE='{"02":"<old-run02>","03":"<old-run03>","04":"<old-run04>"}'
python -m unittest tools.step_evidence.tests.test_packages -v
```

Protocol changes and explicit acceptance scope: [refactor contract](../../docs/step-import/evidence-runner-refactor.md).
The native report writer has its own [bounded-report contract](../../docs/step-import/report-mechanics-refactor.md).
Worker cancellation, process hard limits and native model lifetime remain outside this refactor.

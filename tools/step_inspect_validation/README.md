# Ticket 02: execute the self-developed C++ reader

This standard-library Python 3.10+ adapter invokes the compiled
[STEP_SOURCE](../../source/STEP_SOURCE/README.md) reader. It reuses the frozen
ticket 01 input/hash/independent material checks without modifying that tool or
its published `v1` corpus.

```powershell
$env:STEP_INSPECT_BINARY='C:/temp/step-source-build/Debug/step_inspect.exe'
python -m unittest discover -s tools/step_inspect_validation/tests -v
python -m tools.step_inspect_validation run --binary $env:STEP_INSPECT_BINARY --corpus tools/step_import_validation/corpus/ap203e2-planar/v1 --output-root C:/temp/step-source-evidence
python -m tools.step_inspect_validation check-evidence --run C:/temp/step-source-evidence/runs/<actual-run-id>
```

Repeat `--case` to select individual frozen IDs. The C++ reader captures the actual
evidence input copy, whose bytes/hash must match ticket 01's verified input.
The adapter records the executable path/hash, command, exit code and raw JSON,
then independently checks complete HEADER/DATA/raw-value/constituent coverage,
slot spans, reference paths/targets, and the declared scope. Expected values are
never copied into observations.

The source-only expected result is:

- N02: actual Rejected, D0 DuplicateInstanceId, both #100 source positions.
- N03: actual Rejected, D1 UnresolvedRequiredReference, #9 slot 1 and target #99999.
- Other 47 cases: source inspection completes and retains their exact raw graph.
  Their later AP/type/unit/geometry assertions remain deferred, including N01,
  N04–N36. An Inspected source can contain those semantic defects.

`source_contract=Matched` is restricted to ticket 02. All 49 complete-import
contracts remain NotChecked and complete-import executions NotRun; import success
count is null. Neither this adapter nor the reader creates a SMLib model.

The new evidence format `t02.evidence.v1` uses separate output roots and checkers
from ticket 01's unconfigured-product reports. Each append-only run records:

| Artifact | Meaning |
|---|---|
| `run.json` | Input corpus/reference identity, selection, source hashes, actual executable hash, scope |
| `input.step` / receipt | Captured input identity and source material bindings |
| `inventory.json`, preparation/oracle | Frozen ledger and actual independent material validation |
| `expected.json` | Future full-import expectation, kept separate from actual source inspection |
| `inspect.json` | Unedited stdout from the real compiled reader |
| `invocation.json` | Exact command, exit code, stderr, executable identity and execution state |
| `comparison.json` | Independently checked source scope; full import NotChecked |
| HTML / summary | Same machine evidence, clearly separating source-only and full-import scope |
| artifacts / completion | Payload hashes and the final run-bound commit marker |

Staging, hash/required-role validation and publication follow ticket 01's process,
using a different schema. Partial or mismatched evidence is not reused. The package
checker also rechecks raw observation against its captured input and frozen ledger;
it does not rerun the binary or certify native execution from a completion marker.
SHA256 is an integrity binding, not an authentication signature.

Native C++ contract tests cover owning snapshot lifetime, original-path mutation,
const views, ID lookup/reverse references, raw graph cycles, exact limits and hash
vectors. CLI tests cover all 49 cases, multiline/comments/reordering, sparse IDs,
lexical errors, rejected carriers/encoding, each basic limit at N/N−1, bounded
report publication, and checker corruption controls. No CAD reader is used as oracle.


The execution/publication/recheck implementation is now shared by
[`step_evidence`](../step_evidence/README.md). The existing stage `run`/`check` and CLI interfaces remain;
its comparison logic stays independent. New packages explicitly declare `step-evidence.v2` while retaining this
stage's existing envelope. Historical packages retain their recorded scope and can be rechecked offline.
Current refactor acceptance is recorded separately from the original ticket acceptance.

# AP203e2 ticket 01: synthetic inputs and independent evidence

Standalone, standard-library Python 3.10+ tooling for the
[01 implementation plan](../../.scratch/ap203e2-planar-import/plans/01-fixture-evidence-plan.md).
It materializes 13 positive and 36 single-mutation negative fixtures, checks their
actual bytes and independent rational definitions, and writes readable evidence.
It does not invoke a product STEP reader, SMLib, OCCT, USD, or the external model library.

Run from the repository root:

```powershell
python -m tools.step_import_validation verify --corpus tools/step_import_validation/corpus/ap203e2-planar/v1
python -m tools.step_import_validation run --corpus tools/step_import_validation/corpus/ap203e2-planar/v1 --case S203-P01 --output-root C:/temp/step-evidence
python -m tools.step_import_validation check-evidence --run C:/temp/step-evidence/runs/<actual-run-id>
python -m tools.step_import_validation controls --corpus tools/step_import_validation/corpus/ap203e2-planar/v1 --output-root C:/temp/step-controls
python -m unittest discover -s tools/step_import_validation/tests -v
```

Repeat `--case` to select several cases; omit it to select all 49. Unknown and
duplicate selections are errors. Parent prerequisites are validated without being
counted as selected results. CLI exit codes: 0 successful requested tool operation,
1 failed control verdict, 2 invalid evidence/contract or infrastructure failure.
None of these codes signifies a product import result.

To reproduce materials in a **new** directory:

```powershell
python -m tools.step_import_validation build --output C:/temp/step-corpus-new
```

Existing build destinations are rejected. The frozen corpus is not overwritten.
Changing input, truth, assertions or sources after publication requires a new
revision and verification. The generator records all tool source hashes and the
source document hashes; complete material bytes reproduce on the same tool/source
revision. Runtime timestamps and absolute paths appear only in external evidence.

The files have distinct responsibilities:

| Module | Responsibility |
|---|---|
| `fixtures.py` | Literal author records and exact, ordered byte edits |
| `records.py` | Bounded inventory for the fixed ASCII, one-record-per-line fixture format |
| `reference.py` | Separately transcribed world geometry, identity, mutation and diagnostic anchors |
| `oracle.py` | Rational equations, signed areas, determinants, finite segment distances and interval proofs |
| `corpus.py` | Materialization, hash/lineage/ledger checks and independent definition/truth checks |
| `evidence.py` | Captured input snapshots, state separation, required roles and append-only publication |
| `controls.py` | 12 groups/14 real control variants, actual validator traces and protection receipts |

`records.py` deliberately preserves duplicate ID occurrences, invalid tokens,
unresolved references and cycles. It is not the product Part 21 lexer/parser or an
EXPRESS validator. Its depth measurement counts parentheses inside each DATA record
value, excluding quoted text; this is not a general STEP reference-graph depth limit.

The oracle does not import fixture construction. It checks generated positive
inputs against separately transcribed coordinates, frames, roles, source identities
and equations. Truth uses rectangle dimensions versus shoelace and axis products
versus tetrahedral determinants. Queries include explicit inputs and closest points;
square roots have integer-certified rational enclosures. Negative truth proves only
the specified input facts, not a complete valid shape.

The trust root is the reviewed design and `reference/anchors.json`. Normal verification
checks its registered hash; control verification pins the original corpus reference
externally while resealing only a candidate copy. SHA256 is an integrity binding, not
an authentication mechanism. A coordinated change to the trusted design/reference
still needs review; two copies of a mistake do not establish correctness. Reference
review is recorded as agent transcription, with no human review claimed.

`Q-P0-mm-01` is the explicit synthetic test configuration. Source uncertainty does
not change it. Resource limits are finite material-tool limits with measured input
size/count/depth; they grant no native, worker, time or memory qualification.

Reports keep four dimensions separate:

- `preparation=Passed`: the actual material and its registered defect match the case.
- `oracle=Passed`: independent math/input-fact checks actually ran successfully.
- Product configuration/execution: always `NotConfigured` / `NotRun`, observations null.
- Comparison: always `NotChecked`; evidence can be `Complete` without a product result.

There is no import success count (null), invented zero error, native call count or
leak count. Negative cases are not reported as correctly rejected by a product reader.
Full AP203e2 conformance, native construction, lifecycle and modeling remain later tickets.

Each evidence run writes a new `staging/<id>.partial`, seals required payloads,
checks the completed staging package, then renames it under `runs/<id>` on the same
filesystem. Publication paths are checked against the configured root. A failed write
leaves a partial package; retries use a new ID. `check-evidence` verifies package
integrity, required roles, selected identities and state consistency; independent
math is rerun with `verify`/`run`, not inferred from an HTML page or completion marker.
Artifacts exclude their own index and completion marker to avoid recursive hashes.

The [internal format contract](contracts/v1/README.md) describes current field roles.
The complete byte ledger lives in `inventory.json`; `recipe.json`/`mutation.json`
holds parent and ordered edits. This is the implemented split of the plan's combined
recipe/ledger description. These are internal evidence formats, not a stable product API.

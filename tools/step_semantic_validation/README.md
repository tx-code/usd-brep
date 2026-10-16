# Ticket 03: source semantic evidence

Execute the real [C++ semantic reader](../../source/STEP_SEMANTICS/README.md) and
compare it with ticket 01's frozen inputs/diagnostics and an independent extraction
of the pinned N2635 declarations. No native result is used to fill expected data.
The product reader has no Python/OCCT/STEPcode runtime dependency.

```powershell
$env:STEP_SEMANTIC_BINARY='C:/temp/step-semantic-build/Debug/step_semantic_inspect.exe'
python -m unittest discover -s tools/step_semantic_validation/tests -v
python -m tools.step_semantic_validation run --binary $env:STEP_SEMANTIC_BINARY --corpus tools/step_import_validation/corpus/ap203e2-planar/v1 --output-root C:/temp/step-semantic-evidence
python -m tools.step_semantic_validation check-evidence --run C:/temp/step-semantic-evidence/runs/<actual-run-id>
```

`--case` may be repeated. Actual selected cases are counted, with new run IDs on
every invocation. Inputs, witnesses and prior evidence are never overwritten.
The ticket-01 and ticket-02 tools, their code and their frozen materials are unchanged.

For all 49 fixed cases, this stage's expected results are:

| Status | Cases | Meaning |
|---|---|---|
| SourceFailure, 2 | N02/N03 | Existing D0/D1 raw-source rejection |
| Rejected, 8 | N04–N09, N14/N15 | Missing value, derived marker, BOOLEAN, target type, LIST UNIQUE/SET, fixed enum |
| Unsupported, 7 | N01/N16/N17/N18/N28/N35/N36 | Exact registry or explicit profile guard |
| Deferred, 6 | P11/P13/N31–N34 | Mandatory conversion interpretation belongs to later tickets |
| Checked, 26 | Remaining cases | Ticket-03 scope completed; later semantic/geometric assertions are not accepted |

Seventeen in-scope blockers must match ticket 01's actual key/stage and independent
field/cause evidence. N28 retains all eleven orphan identities. Checked negative
cases keep their later expected rejection visible. All full-import comparisons
remain NotChecked and all native BRep/full-import executions NotRun; success count null.

The declaration witness in `contracts/n2635-reference-v2.json` contains 72 entity
declarations and the independently selected 36 simple leaves. It is extracted
from the already fixed `.exp` bytes, never the C++ catalog or reader output.
It verifies ancestors, diamond deduplication, source population completeness,
local/inherited field origin, optionality, aggregate bounds/uniqueness and derived
redeclarations. SI prefixes/scales and fixture identities are checked separately.
The checker also invokes the unchanged ticket-02 raw-output comparison.

For an audit reproduction, explicitly provide the fixed schema and a **new** output:

```powershell
python -m tools.step_semantic_validation.build_reference --schema <pinned-N2635.exp> --output C:/temp/n2635-reference-new.json
```

The extractor rejects a schema SHA mismatch and existing output. It is a narrow
source-fact audit helper, not an EXPRESS compiler/evaluator or product dependency.
Its section recognition is anchored to declaration lines, so `OF UNIQUE` stays
part of PATH.edge_list. An unpublished first extraction had incorrectly truncated
that field; it was quarantined outside the repository, the source-derived witness
was corrected, and all comparisons were rerun. The pinned `.exp` and golden STEP
files were not changed. The checked witness identity is explicit in `oracle.py`.

Evidence uses `t03.evidence.v1`, in a separate caller-selected output root:

- `run.json` binds input manifest, selection, fixed witness, tool/source and executable hashes.
- Each case saves verified input, original inventory/expected, material/oracle results,
  unedited C++ stdout, command/exit/stderr and an independent comparison.
- A captured declaration witness is checked by hash; the report checker rechecks
  raw observations and their declared semantic coverage against it.
- Staging payloads are sealed and checked before publishing a new `runs/<id>`.
  Missing roles, partial evidence, stale hashes or scope overclaims cannot be reused.

Native tests check owning views/reverse queries and unknown-query handling. CLI tests
cover the frozen corpus, precise metadata, schema/edition contradictions, transport
types, all fixed SI prefixes, missing/conflicting units, rad/sr nominal separation,
legacy source identity, conversion deferral, reordered/sparse IDs, traversal/report
limits and six checker-corruption controls. General allocation fault cleanup,
worker cancellation, native modeling and full schema conformance are not qualified.

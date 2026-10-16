# Ticket 04 validation

The adapter invokes the actual C++ `step_planar_inspect` executable against the frozen 49-case ticket-01 corpus.
It reuses the independent 01 material/math verification and the 02/03 source observations checks. New checks bind the
N2635 WHERE/UNIQUE/INVERSE/global declarations to an independently extracted, hash-pinned witness, verify all applicable
owners and FOR populations, and recompute endpoint/plane errors from the input bytes using Python `Fraction`.

Region area and centroid checks use 3D vector areas and triangle fans, separate from the C++ projected shoelace method.
These results also meet the reviewed analytic dimensions in the frozen 01 truth. The adapter checks source parameter
intervals, source UV origins, unit scaling, identities and all four sense layers. No product result is copied from an
expected value, and no native BRep is built.

`contracts/n2635-rules-v1.json` comes from the pinned EXPRESS bytes, not the C++ catalog or executable output.
`build_reference.py` is a development-only extractor requiring the exact schema SHA and a new destination; it is not
a runtime dependency or a general schema compiler. The original witness, schema, corpus and previous adapters remain
unchanged. The current witness hash is pinned by `oracle.py`.

```powershell
python -m tools.step_planar_validation run --binary <step_planar_inspect.exe> `
  --corpus tools/step_import_validation/corpus/ap203e2-planar/v1 --output-root <external-evidence-root>
python -m tools.step_planar_validation check-evidence --run <external-evidence-root>/runs/<run-id>
$env:STEP_PLANAR_BINARY='<step_planar_inspect.exe>'
python -m unittest tools.step_planar_validation.tests.test_planar -v
```

Evidence format `t04.evidence.v1` has its own append-only root. Each case saves real input, frozen expected/inventory/
truth, invocation/exit/binary identity, native stdout and independent comparison. Completion and artifact hashes are
checked in staging before directory publication. Incomplete attempts stay in staging. Recheck verifies frozen truth
identity as well as stdout, comparisons and declared status counts. It never upgrades full-import `NotChecked`.

After ticket 09 all 49 current-stage contracts must match: 13 `Checked`, 19 `Rejected`, 17 `UpstreamFailure`.
The 17 preceding failures contain 10 source failures and 7 unsupported cases. The 36 total blockers are not all source
invalidity. A single mutation may have further consequences after its first blocker; reporting those is not required.
For every case, full import/native build remains `NotRun` and import-success count remains null.
The 03 source-type/SI contract retains its explicit conversion `Deferred`; 04 completes dimensions, positive scale and
cycle interpretation before geometry. The independent checker expands source unit definitions, not native output.
Historical evidence retains the scope of its original comparator/version; this adds no migration or publication mechanism.


The execution/publication/recheck implementation is now shared by
[`step_evidence`](../step_evidence/README.md). The existing stage `run`/`check` and CLI interfaces remain;
its comparison logic stays independent. New packages explicitly declare `step-evidence.v2` while retaining this
stage's existing envelope. Historical packages retain their recorded scope and can be rechecked offline.
Current refactor acceptance is recorded separately from the original ticket acceptance.

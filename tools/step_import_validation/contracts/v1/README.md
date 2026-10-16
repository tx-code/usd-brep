# Internal material and evidence format `t01.v1`

All JSON is UTF-8 without BOM, LF, four spaces, sorted object keys and no NaN or
infinity. Array order is significant. Duplicate JSON keys are rejected; metadata
is bounded to 10 MB and nesting depth 64. Paths must resolve inside the configured
root. A file reference has `path`, `role`, `sha256` and `byte_length`, checked against
the actual payload. Source IDs and record occurrence indices are separate integers.
Byte spans are zero based with exclusive end.

The manifest is the entry point. It registers exactly P01–P13/N01–N36, profile,
revision, trusted reference and all materials. The manifest does not hash itself;
each actual run records its hash. There are no absolute machine paths in the corpus.

| Material | Required content / checked relationship |
|---|---|
| `sources.lock.json` | Design file hashes, fixed N2635 implementation-release identity, tool file hashes, reference hash |
| `reference/anchors.json` | Reviewed coordinates, counts, identity paths, measures, negative facts, exact mutation tokens and diagnostic keys |
| `quality/Q-P0-mm-01.json` | Explicit rational quality fields, canonical mm, repair None, production_default false |
| `resources/T01-corpus-limits.json` | Positive finite byte/count/depth/report bounds, measured fixture maxima and depth definition |
| `case.json` | Matching case/kind/profile/Q, selection #7, parent, repair None, exactly five material roles |
| `recipe.json` / `mutation.json` | Input hash, exact parent/hash and ordered old/new spans; negative has exactly one edit and base64 pre/post bytes |
| `inventory.json` | Actual ordered DATA occurrences, source ID, type, raw value, byte span and reference occurrences; no ID deduplication |
| `truth.json` | Independent rational shape data/query definitions or negative facts; explicit material-volume applicability |
| `assertions.json` | Expected result/model presence, exact logical stage/key/span and repair None; never a subject observation |
| `review/*.json` | Method, source/independence limits and designed coverage; not fabricated execution receipts |

For derived cases the verifier reads the exact parent bytes, replays registered
edits and compares the complete child payload. Negative pre/post tokens and
first-blocking diagnostics must also match the trusted reference. Selected cases
validate their parent chain. Positive units, world geometry, curve parameters,
source identities, oriented paths, frames and roles are checked independently;
negative facts preserve ambiguity rather than constructing a valid shape.

Exact rational values are objects with **only** `kind=rational`, decimal string
`num` and positive decimal string `den`. They must be reduced and canonical
(zero is `0/1`). A JSON float cannot substitute for an exact value. A square-root
distance carries a rational radicand, rational lower/upper bounds and the checked
inequality `0 <= lower`, `lower² <= radicand <= upper²`. Current width is at most
10^-12 mm, below one tenth of the smallest applicable distance budget.

Source LINE endpoint parameters are 0/1 for these 49 fixtures; unit scaling does
not scale these dimensionless values. This does not grant support for arbitrary
parameter ranges. P03 keeps its shifted source plane origin; query `frame_uv`
uses the independently reviewed F2 world frame and is not P03's shifted source UV.
Surface models have no material volume or InsideSolid qualification.

| Evidence role | Required content |
|---|---|
| `run.json` | Unique run ID/selection, actual UTC, manifest/reference/config hashes, tool hashes, Python path/version/platform, no reader/kernel binary |
| `input.step` + receipt | Captured verified bytes, hash/length, case/attempt identity and source material bindings |
| `preparation.json` | Actual hash/parent/edit/ledger/definition checks and counts |
| `oracle.json` | Actual independent proof, truth hash, exact results and applicability |
| `observed.json` | NotConfigured/NotRun, null result/model, reason |
| `comparison.json` | NotChecked with missing-observation reason |
| `diagnostics.json` | Expected contract separately from subject_observed=null and actual tool diagnostics |
| `case.html`, `summary.json/html` | Present the same per-case evidence, exact selected coverage and separate counts |
| `artifacts.json` | Hash/length for every frozen payload except index and completion |
| `completion.json` | Same run ID and actual artifacts index hash |

`Complete` requires the formal run location, identity-bound completion, valid hashes
and all required roles. It also requires selected rows, preparation/oracle and the
unrun-product states to agree. It is not source certification or imported-model
acceptance. A partial package is never reused as a complete one. The checker is
for integrity and contract consistency; untrusted resealing cannot certify execution.

Controls are distinct from the 49 corpus cases. Each isolated attempt retains its
baseline fingerprint, exact changed/removed files, actual target-check trace and
verdict. Hash maintenance belongs only to the injector; trusted anchors are unchanged.
`Passed` means the designated validator guard was reached (or the legal unit positive
control passed), with original materials preserved. An unrelated exception is a
failed control, even if it rejected the candidate. Control packages are retained as
test evidence and do not use the normal import-run completion schema.

# STEP_PLANAR

Internal source validation above STEP_SEMANTICS. No SMLib, OCCT, USD, STEPcode runtime, native BRep or repair dependency.
The transport and closed AP203e2 N2635 profile remain those of tickets 02/03. This layer checks supported source rules
and directed LINE/PLANE boundary regions; `Checked` is not complete AP validity or an imported model.

AP214e3's source-only semantic profile is refused at `UnsupportedRuleProfile/D3`
before creating this validator. A checked/deferred AP214 source snapshot does not
authorize the N2635 local/global rule implementation. Those checks, geometry and
conversion evaluation remain `NotRun`, no rule evidence is fabricated, and native
import publishes no model or native calls from that source profile.

`Check(semanticOutcome, explicitQuality, limits)` returns owning diagnostic evidence sharing the immutable semantic/source
snapshot. Source IDs, attributes and raw bytes retain their original meanings. Partial evidence can accompany failure;
`qualified_source_evidence` is true only after all current checks complete. The JSON report embeds the preceding report
once and always reports `model=null`, native build/full import/target UV/tolerance setting/material volume/shell
self-intersection as `NotRun`.

Field reads, resolved references and field diagnostic locations come from the semantic snapshot's shared bindings.
The validator does not rebuild field-name or entity-ID tables. Its established per-record admission charge remains,
so moving binding work into the existing semantic validation visits does not silently relax planar work limits.

Private typed progress runs local rules, global rules and geometry in that order. Outcome strings remain presentation
fields written when Check returns. An admission-budget stop leaves all three phases NotRun; an entered phase that
cannot complete preserves its failure classification and blocks the remaining phases, retaining partial evidence.

The compiled N2635 clause metadata covers the 72 registered entity declarations and their 77 WHERE/UNIQUE/INVERSE
clauses, including guard populations with no instances. Active clauses are evaluated per instance; ancestor populations
include actual subtypes. Singleton product guards prove the two UNIQUE clauses. Explicit metadata-assignment exclusions
prove inverse WHERE counts, while context inverse cardinalities use actual role-filtered references. Named length TYPE
predicates include their inherited predicates. This is a dedicated equivalent evaluation inside the closed profile,
not a general EXPRESS interpreter.

All 77 entity clauses and three named TYPE predicates have private named identities in one static registry.
Registry storage order does not select evaluation or execution. The fixed entity execution list, per-value length
predicate chains and final dimension predicate retain their distinct expansion, evidence insertion and work charges.
C++20 compile-time checks reject duplicate identities/declarations, incomplete execution and invalid subject plans.
Failure fields retain the existing diagnostic policy; identity integers are not serialized.

The 47 global rules have internal named identities. A separate fixed identity list preserves execution and report
order, independently of registry storage order. Each entry retains its source name, line, FOR populations and failure
location. C++20 compile-time checks reject duplicate identities/names or incomplete execution coverage; evaluation
dispatches exhaustively on identity. Internal numeric IDs are not serialized.

Complete type populations are shared through the embedded semantic snapshot. Per-instance local rows state
`population_scope=OwnerInstance` and list only their owner; global FOR populations use `FullFile`. This keeps local
evidence storage linear in applicable instance/clause pairs rather than copying a whole class into every row.

The 47 global rules record 5 mandatory, 1 conditional and 41 guarded classifications. Every FOR population is reported,
including nonempty PRPC/PD/representation populations whose name filter or outer loop is inactive. An unregistered type
is never an empty public query result; only the known global rule declarations use the explicitly validated closed
signature universe to establish exclusion. Source LOGICAL UNKNOWN is a computed value: G36 records its ordinary-name
correlation UNKNOWN and the outer QUERY's empty failing set. Unevaluated expressions have no logical value. MSSR
WR6–WR14 use the ADVANCED_FACE first-disjunct proof; the original plain-FS access paths remain unexecuted and unchanged.

Geometry decisions use reduced signed 63-bit-magnitude rational arithmetic with checked intermediate operations.
Finite LINE parameter intervals, squared endpoint/plane residuals and threshold comparisons are exact. Placement axes
are kept in an unnormalised orthogonal basis; normalisation radicals and metric factors are explicit in evidence.
Thus irrational normalisation need not round the defining geometry. Numerals or intermediate operations exceeding this
bounded representation return `Indeterminate`, with source provenance and no numeric fallback. This is a current
capability limit, not a source-invalid diagnosis; it does not limit the source parser's exact numeric text retention.

Rational values maintain a reduced numerator and positive denominator through construction and arithmetic.
`Numerator()` and `Denominator()` return read-only facts; callers replace the whole value through validated construction
rather than writing fraction members. The bounded range, exact decisions and serialized fraction format are unchanged.

LINE parameters are dimensionless; PLANE u/v have source length units. Evidence gives the exact algebraic mapping back
to source UV. `projected_region_*` values describe the independently checked projection of the source vertex boundary
onto the source plane. Residuals remain visible, even when nonzero within Q. This projection is diagnostic evidence;
no source vertex, curve, plane, topology or sense is rewritten. No target trim/window is created here.

Regions require explicit outer bounds, identity-connected simple loops, nonzero area, correct effective winding,
strict hole containment and disjoint, nonnested holes. Incidence checks detect free edges in claimed closed shells,
wrong shared-use direction, disconnected shells and repeated/nonmanifold uses. Passing these checks does not establish
shell nonintersection, material interior, volume, modeling/query eligibility or native construction.

The CLI requires both `--fixture-limits` and `--quality Q-P0-mm-01`. The frozen test candidate supplies 1e-7 mm consistency
error; it is not a production default. The internal API also accepts an explicit separately named quality configuration.
Source uncertainty is checked as source semantics, and does not enlarge this threshold. Ticket 09 resumes only the
explicitly deferred typed-unit handoff from 03, after binding/types/part passed. Local rules check seven-value declared
dimensions against nominal quantities and factor units before geometric interpretation. Scale resolution is a separate
bounded iterative walk with a shared memo, a positive SI root and concrete cycle diagnostics; it never guesses a scale
from a unit name. `conversion_units` reports its actual progress; resolved definitions retain factors, base identities,
typed values and exact canonical scales. Zero/negative scales and cycles are project D4 failures, not invented WHEREs.

```powershell
cmake -S source/STEP_PLANAR -B <external-build> -DCMAKE_CXX_STANDARD=20
cmake --build <external-build> --config Release
ctest --test-dir <external-build> -C Release --output-on-failure
<external-build>/Release/step_planar_inspect --fixture-limits --quality Q-P0-mm-01 input.step
```

C++20 is required by the STEP_SOURCE/STEP_SEMANTICS/STEP_PLANAR targets in the root and standalone CMake builds.
This does not change the repository's global standard or build SMLib.

Exit codes: 0 checked source evidence; 1 source rejection; 2 upstream profile/transport unsupported; 3 resource limit;
4 input I/O; 5 report limit (no partial stdout); 6 infrastructure; 7 conversion deferred; 8 unestablished numeric/derived
evidence; 64 invalid configuration. `UpstreamFailure` retains the preceding layer's precise diagnostic and exit code.

Reusable checks and their scope: [planar validation](../../tools/step_planar_validation/README.md).
Source units and conversion contracts are documented here and in
[STEP_SEMANTICS](../STEP_SEMANTICS/README.md); execution receipts stay local.

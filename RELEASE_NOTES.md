# SDK 2.0.0-alpha.1

Prepared 2026-10-24 as a fork development preview.

This is a local release-preparation record. It does not create a tag or publish a package.

The first independently versioned fork preview consolidates the native geometry SDK and explicitly scoped STEP development work. It keeps upstream history as provenance and requires a matching C++20 rebuild; no production or full STEP qualification is implied.

## Delivered changes

### SDK product and build compatibility

The fork provides a C++20 SMLib/SM_API geometry SDK and Python modeling package for CAM. Default SDK builds use CMake and no longer include OpenUSD, Hydra, BrepArray schemas, scene converters or TBB. Retained shell/Repo commands configure, build and stage the same production SDK layout. The native portions of upstream 1.0.1 are integrated while the fork's source, ownership and publication contracts remain in force.

Integrator action: Rebuild libraries, headers and all native consumers together with matching configuration, CRT and kernel layout macros. Migrate removed USD entrypoints; repository/distribution/import names remain usd-brep/usd_brep for compatibility.

Compatibility: This is a new fork major-version line. Historical upstream USD capabilities in earlier changelog entries are not part of this SDK.

### Fixed STEP source profiles and compile-time catalogs

Fixed STEP profiles use immutable C++20 catalogs and checked source bindings. Source inspection retains declared products, all registered material items, composite-curve dependencies, spherical declarations, pcurves and revolution prerequisites. It distinguishes source-rule rejection from unsupported native scope. The explicitly identified modified N916 subset retains its unverified edition and its own necessary clauses rather than inheriting a different profile's rules.

Integrator action: Rebuild STEP C++ consumers: registered types and nested aggregate descriptors now expose read-only spans. Keep owning snapshots alive and inspect the exact selected profile and diagnostics.

Compatibility: Catalog recognition and necessary source checks do not establish full EXPRESS validation or native conversion. Source composite support does not imply general native composite construction.

### Checked STEP presentation and attached metadata

Source inspection retains checked curve/point styles, predefined colours, scalar kinds, units, product-category hierarchies and visibility instructions. Explicit nongeometric interoperability may preserve audited attached metadata or unavailable text after ownership and physical-dependency exclusion checks. Original values, references and strict diagnostics remain available. Hidden geometry still belongs to complete import.

Integrator action: Select interoperability explicitly and consume the retained scalar/text kind and its original unit context. Unavailable decoded text is not a fabricated string, width, geometric root or physical unit.

Compatibility: Metadata exceptions stay within the documented fields and profiles; they do not relax defining geometry or grant arbitrary style/metadata conversion.

### Explicit source and carrier interpretations

Applications can explicitly request the documented schema, carrier, text and registered-facet interpretations, including an input-SHA-bound AP214 interpretation. Immutable bytes, original identity/encoding diagnostics and assumptions remain owned by the source and accepted snapshots. Unverified OID tokens remain strings in valid JSON. Complete interpreted native results retain AcceptedUnderInterpretation separately from ordinary acceptance.

Integrator action: Bind interpretation to the intended input and inspect requested versus applied assumptions. Keep strict/default behavior for requests that do not explicitly opt in; do not treat an assumed HEADER as authenticated.

Compatibility: Source-reader interpretations have their own scope and do not all enable native import. Semantic, geometric, budget and publication gates still apply.

### Complete definitions, surface roles, collections and assemblies

Explicit native entrypoints preserve independent material bodies, product definitions and assembly occurrences with owning local-coordinate views and placement lineage. General construction also preserves qualified OpenSurfaceModel and ClosedSurfaceModel roles, including free/shared-edge incidence and area centroids; a closed surface stays nonmaterial. Associated wireframe, points and supporting geometry retain separate source roles. Complete selected models and required nested reports share one final cancellation/budget/publication decision. Proven source selection may isolate only the documented unselected errors and returns AcceptedWithSourceIsolation when applied.

Integrator action: Use the entrypoint for the complete requested structure, inspect each body's actual role, and retain interpretation/isolation evidence. Material collections preserve independent Breps rather than Boolean-unioning them or selecting one survivor.

Compatibility: Surface roles have no material volume or material point classification. Multi-shell surface collections and unestablished surface repair/late-normalization combinations remain unsupported. Native closure does not grant material semantics.

### Analytic, NURBS and source-master pcurve definitions

Qualified general construction retains trimmed conic cuts, spherical and ring-toroidal definitions, finite B-spline revolutions including rational generators, and supported source-master pcurves as surface/curve compositions. Generator knots and source parameters remain meaningful; UV unit conversion follows the basis surface. Auxiliary geometric images are checked without assuming equal parameters between representations.

Integrator action: Keep source/basis identities, trim senses and actual parameter mappings. Supply the documented finite geometry and unit prerequisites; required unsupported or unestablished geometry remains a failure.

Compatibility: These are scoped development factories, not complete STEP geometry coverage. Parameter-domain refusal is preserved; there is no implicit extrapolation, fitting or replacement of a selected defining representation.

### Derived UV, periodic correspondence and topology diagnostics

Controlled UV construction preserves paired parameter semantics and avoids retry-driven tolerance growth in the admitted cases. Explicit periodic normalization retains original-basis parameters and rebuilds source-to-target correspondence after topology changes, including ring subdivisions, spherical poles and seam origins. Region checks retain all relevant bounds, and reports distinguish known orientation and source-vertex near-miss predicates from generic native refusal.

Integrator action: Retain final UV, topology and working-tolerance checks. Select normalization explicitly and inspect rebuilt target mappings rather than reusing pre-normalization handles or treating SET order as the outer region.

Compatibility: Improved construction or diagnostics do not skip failed native validation or widen geometric budgets. Numerical parameter-roundoff handling is separate from permission to move geometry.

### Explicit bounded repairs and support-domain restriction

Source-bound face-orientation and cylindrical-band plans retain their explicit role and displacement allowances, original input and owning repair evidence. Optional unused-support restriction preserves the original basis and parameters, requiring a closed chart and a continuous surface-difference bound on the whole retained rectangle. Regular trimmed horn/spindle toroidal regions additionally require a generator-radius bound away from the axis.

Integrator action: Choose each repair or restriction explicitly and inspect its applicability, displacement, local-feature and cumulative-budget evidence. Repaired results remain separately identified; retain originals when they are needed.

Compatibility: Strict paths retain Repair=None. Support normalization is not general repair; an open chart or unestablished continuous bound refuses publication. Source uncertainty never silently enlarges a repair allowance.

### Exact units, explicit quality and complete reports

General import resolves supported conversion-based length chains with bounded exact arithmetic, retains unit evidence and applies physical scaling once. Working-tolerance studies remain explicit quality configurations. Complete reports retain failed-candidate tolerance observations and actual integration policy; optional CompactV2 factors repeated source data while preserving original bytes and diagnostics. Material classification retries grazing rays and returns Indeterminate when bounded attempts cannot establish a region.

Integrator action: Inspect exact units, the selected quality ID and checks actually completed. Configure required report capacity explicitly; budget exhaustion or failed evidence must not trigger a partial report/model or a looser fallback.

Compatibility: Physical error budgets, native working tolerances, source uncertainty and repair allowances remain separate. Compact reporting changes representation, not acceptance scope.

### Face integration and measured properties

Face-property quadrature respects actual B-spline trim knot spans. Selected-group integration supports independent area and volume reference origins; general material imports reuse per-face results for model and reported face properties and avoid unused second moments. Bounds-midpoint origins improve moment conditioning. Explicit tighter integration settings retain requested/applied values and completion in native and viewer evidence.

Integrator action: Rebuild matching consumers for the added native overload. Expect corrected numerical results in affected cases and retain the caller's precision request, complete reports and cancellation.

Compatibility: Numerical integration stopping controls are not continuous absolute geometry-error certificates. Surface area-only integration and material volume integration keep distinct roles.

### Curve bounds, validation and reusable numerical work

Curve validation consumes curvature results and uses conservative coefficient bounds only for eligible runtime types, retaining numerical fallback. Tight bounds respect requested intervals and stationary turns, including arches after constant spans. Coincident-topology results no longer depend on requested output arrays. Surface coincidence pairs keep their own parameters/distances, torus domain checks use the correct analytic axis, and available point-solver evaluations are reused. Validation-only UV-trim creation preserves working tolerances.

Integrator action: Retain diagnostic selection, original curve intervals and derived-evaluator fallback. Review corrected validation outcomes rather than weakening the existing threshold to reproduce an earlier success.

Compatibility: Coefficient certificates cover their stated bases and arithmetic prerequisites. Other cases retain existing solvers; this release does not promise universal import speedups or complete geometric validity.

### Healing and stitching error propagation

Healer failures from degenerate-face squeezing/cleanup, seam movement/insertion and recursive property refresh propagate through the affected native/API paths. The optional native UV-trim stage returns failed face-generation status, and direct seam insertion preserves U/V errors. Single-Brep face stitching derives default tolerance from its actual target instead of crashing.

Integrator action: Handle returned failures and Python RuntimeError. Healing is in-place: preceding topology edits remain after failure, so heal a copy when the original must survive and reset stale retained healer properties before reuse.

Compatibility: This fixes specific swallowed failures, not every healing helper. It does not grant rollback, complete repair, or permission to bypass remaining KNOWN_ISSUES.md limitations.

### Copy ownership, native archives and array moves

Virtual composed-curve Copy propagates owned-child failures and reclaims unpublished candidates; STEP surface wrappers copy their definitions independently. Explicit native archive readers preserve stored geometry classes, parameter conventions and positive working tolerances instead of reclassifying or version-healing them. Polar caches retain real seam endpoint representatives. SmTArray adds move construction with transferred owned storage, independent borrowed storage and an empty reusable source.

Integrator action: Use the documented owning copy/archive paths and matching runtime. Preserve borrowed-view lifetimes around representation changes; pointer element ownership does not change merely because an array is moved.

Compatibility: Normal copy/archive checks do not establish all legacy whole-Brep constructor failure paths or failure atomicity. Native storage format is not changed by these reader and ownership fixes.

### Optional native viewer and workbench

The optional viewer/workbench loads native CAD through cancellable isolated workers, displays complete qualified definitions and associated geometry, and provides face/edge/native-vertex selection and measured queries. Local directory scans deduplicate content and keep per-file outcomes local. Explicit source/report controls are retained in worker evidence and qualification labels. Stale or failed loads preserve the prior accepted scene; lighting and current-hover reuse improve display behavior.

Integrator action: Install the optional GUI dependencies and matching worker runtime. Read interpreted/repaired/isolated labels and unavailable-measurement reasons; configure report capacity and source choices explicitly.

Compatibility: Rendering, thumbnails and process isolation do not qualify source correctness, geometric validity or kernel thread safety. Viewer support does not add USD or TBB to the default SDK.

### Validation evidence and fork release preparation

Synthetic corpus tools retain their frozen design inputs independently of evolving guidance. Independent OCCT volume comparison records adaptive integration at the requested precision and its native estimate. VERSION now drives the CMake numeric core and wheel prerelease identity. Release tooling captures a fixed Git cut, consolidates hashed changesets, preserves new/deferred notes and verifies prepared records without committing, tagging or publishing.

Integrator action: Use the recorded method and independent evidence scope when comparing geometry. Review release plans and compatibility limits before consumption; rebuild/reconfigure matching consumers after version changes.

Compatibility: A note mapping, another kernel's agreement, a rendered mesh or a validator pass alone is not proof of complete import correctness. This record remains a local alpha release preparation.

## Compatibility and migration

- Treat the 2.x fork as a compatibility boundary from the earlier USD product. Rebuild matching headers, native libraries, Python extensions and consumers with the same compiler configuration, CRT and kernel layout macros.
- Migrate removed USD/Hydra/schema/converter entrypoints. The repository and usd-brep/usd_brep names remain for compatibility; the default native SDK has no USD or TBB dependency.
- Rebuild STEP C++ consumers for changed spans, outcomes and internal interfaces. Retain owning source/model snapshots, actual source roles, unit contexts and source-to-target mappings.
- Handle source rejection, unsupported scope, indeterminate numerical evidence, native failure and cancellation distinctly. Interpreted, repaired and source-isolated results are separately qualified; never silently relabel them as ordinary strict acceptance.
- Healing can return a failure after earlier in-place edits. Copy first when preserving inputs, reset failed retained sessions, and follow the documented borrowing/representation-change rules.
- Use the full VERSION identity 2.0.0-alpha.1, numeric CMake core 2.0.0 and wheel identity 2.0.0a1. Prepared records do not publish artifacts; future changed binaries need their own release identity before distribution.

## Qualification limits

- This is a development preview. Fixed schema/profile recognition and necessary source checks do not establish full EXPRESS/AP conformance, complete STEP coverage or production CAM qualification.
- STEP acceptance remains qualified by the exact source profile, explicit options, quality configuration, checks actually run and the owning immutable snapshot. A modeling copy requires its own validation.
- Strict paths retain Repair=None. Interpretations, selected-source isolation, bounded repairs and support/periodic normalization require their documented explicit choices; immutable source bytes and original diagnostics remain available.
- Native closure does not imply a material solid. Surface models have no material volume/classification, and unestablished multi-shell surface or repair combinations remain unsupported. Required unsupported composite or out-of-domain definitions still refuse complete publication.
- Sampling and quadrature observations are not continuous geometric error bounds. Coefficient/domain certificates cover only their stated interval, basis and arithmetic prerequisites; source uncertainty does not enlarge geometric, working-tolerance or repair budgets.
- All selected models and mandatory reports must pass the final budget/cancellation decision. A partial model, truncated JSON report, render, native validator pass or independent-kernel agreement cannot establish complete import correctness.
- Known kernel and healing limitations remain in [KNOWN_ISSUES.md](KNOWN_ISSUES.md). Positive copy/archive results do not prove all legacy whole-Brep failure-cleanup paths; healing is not transactional rollback.
- Native STEP is a C++ library with optional CLIs; these changes do not provide a Python STEP binding or a general kernel thread-safety guarantee. Integrators must qualify matching builds and application workflows within the documented component scopes.

## Release inputs

Integrated source: `6032bdc3ef996c07348869007fab210a9ce8261b`; comparison baseline: `3fd5b9548d19aa8c12c29bc9637a96d42a06f8a5`.

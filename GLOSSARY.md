# Boundary representation geometry

## Planar geometry

**Planar region**: A portion of a plane selected by closed boundaries, with
included interiors and excluded holes. It is independent of its application.
_Avoid_: CAM region (when referring to the general geometry).

**Boundary loop**: A closed traversal of curves delimiting a planar region or
one of its holes.

**Hole**: An excluded interior enclosed directly by a material boundary loop.

**Island**: An included region nested inside a hole; it can contain its own holes.

**Projected linework**: Curves visible from a projection direction, which may
include internal contours and creases as well as exterior boundaries.
_Avoid_: Footprint (unless the intended material boundary has been determined).

**Curve transfer**: A change in which object owns a defining curve during construction.
Individual curves can transfer before the complete operation succeeds; a returned curve is again owned by its previous owner.
_Avoid_: Successful construction (when only curve ownership has changed).

## Imported models

**Part definition**: The geometry and topology of a reusable part, independent of where that part is placed in an assembly.

**Part occurrence**: One use of a part definition in an assembly, with its own placement and assembly relationship. Multiple occurrences can refer to the same definition.
_Avoid_: Part copy (when the geometry is shared).

**Independent curve definition**: A source curve definition selected through a wireframe or supporting representation.
Shared source uses retain its identity; selection through these roles does not make it a material boundary.

**Independent point definition**: A source point selected through a wireframe or supporting representation.
Equal positions do not make distinct source identities equivalent.

**Strict import**: Acceptance of a supported source model only when its required geometry, topology, and assembly dependencies are accounted for. Missing or unsupported required content is an import failure.

**Bounded repair**: A change to an imported model constrained by an explicit allowance for geometric, tolerance, and topological changes, with the original model retained.
_Avoid_: Successful healing (when only an operation status is known).

**Representation normalization**: A verified change in how a model is represented that preserves its shape and the meaning of its source uses, with a recorded correspondence between the original and resulting entities.
_Avoid_: Repair (when no inconsistent defining geometry is being changed).

**Source uncertainty**: Precision information declared by the source representation. It is distinct from measured geometric gaps and from permission to change the model.

**Repair budget**: The permitted geometric displacement, tolerance growth, and topological change for a bounded repair.

**Source pcurve**: A curve in a source surface's parameter space, retained with that surface and its role in defining or describing an edge.

**Target UV trim**: A parameter-space boundary used by the target model, with its correspondence to the defining geometry preserved. It is distinct from the original source pcurve.

**Import quality configuration**: The explicitly selected requirements for accepted geometric error, derived parameter data, permitted repair changes, and effective working tolerances of an import request.

**Import candidate**: A model under construction or evaluation for an import request that has not yet been accepted as its result.

**Imported model snapshot**: An accepted, immutable view of a selected source model, including its part definitions, occurrences, and source correspondence. Acceptance evidence applies to that snapshot.

**Source snapshot**: An immutable record of the input used for import and its available source semantics. It is distinct from a target model or import candidate.

**Import report**: The findings and evidence for an import request, covering diagnostics, representation changes, and repair attempts. Its conclusions apply to the identified input, quality requirements, and accepted model snapshot when present.

**Import evidence package**: A retained record of an import validation run, binding its selected inputs, observations and independent comparisons to its declared validation scope. A complete record does not by itself establish model acceptance.

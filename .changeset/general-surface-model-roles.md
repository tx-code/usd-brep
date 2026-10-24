---
change: feature
---

Explicit general STEP import constructs qualified open and closed surface models while retaining
their source role. Surface results provide area, area centroid, tight bounds and free-edge
observations without acquiring material volume or material point classification. Complete
definition and assembly reports retain the actual role and native incidence evidence.

## Scope

[General nonmaterial surface contract](../source/STEP_IMPORT/README.md#通用构建的非材料曲面角色).

## Integrator action

Rebuild matching STEP libraries and consumers. Inspect the source role and applicable checks;
native closure alone does not qualify material semantics. General reports add role, face and
adjacency observations, and distinguish area-only integration from material integration.
Native-call limits also cover the additional role and incidence verification.

## Compatibility

Existing material integration and the strict planar path retain their geometry and precision
contracts. Source bytes, units and parameters, public C++ layouts, kernel ABI and native archive
format are unchanged. General surface support replaces the former material-only role refusal;
unsupported surface repair and late periodic-normalization combinations still fail explicitly.
Multiple-shell surface collections and complete STEP or downstream modeling qualification are
not granted. Normal copy/archive validation does not establish all legacy BRep failure paths.

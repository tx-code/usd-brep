---
change: feature
---

General native STEP imports offer an explicit unused B-spline support-domain
restriction. The selected bounded UV region must remain covered and an outward
interval Bernstein bound must verify same-parameter surface agreement on the
whole retained rectangle. Required geometry, native validation and atomic
publication remain mandatory. Source definitions stay in the owning snapshot.

A separately selected native work-tolerance study keeps 0.01 mm source/UV error
budgets while declaring a 0.01 mm initial, 0.02 mm maximum and 0.01 mm growth
budget. It does not select normalization or change existing frozen profiles.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the importer and every C++ consumer for the changed Options/Report
layout. Select general geometry, quality and support restriction explicitly;
retain normalization and quality evidence with a saved model.

## Compatibility

C++ Options/Report layout changes; all libraries and consumers must match.
Default reading, source schemas, existing quality identities and report bytes
are unchanged. Opted-in reports identify the request, actual domain mapping and
numeric evidence. This is target representation normalization, not smoothing,
source repair, full STEP conformance or a production accuracy claim.

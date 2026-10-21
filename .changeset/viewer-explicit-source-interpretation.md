---
change: feature
---

The standalone viewer and scanner accept explicit schema/carrier interpretation
options and retain native acceptance status. The HUD marks models accepted under
interpretation; default and strict source behavior remain unchanged.

## Scope

[Standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Update the worker and Python application together. Select assumptions explicitly
through `--schema-interpretation` and `--carrier-interpretation` when needed.

## Compatibility

The worker retains its default seven-argument entry and adds two optional policy
arguments. Scene and result records gain native status/requested-policy metadata.
Legacy scenes lacking native status remain unqualified; no geometry budget changes.

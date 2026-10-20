---
change: feature
---

The standalone viewer can scan local model directories with content deduplication,
isolated per-file import/mesh/render attempts, CLI progress, local JSON summaries
and optional thumbnails. Timeouts and Ctrl+C clean up the complete worker tree;
failures remain failures and never trigger a tolerance or reader fallback.
The portable runtime reuses font discovery across launches of the same installation.

## Scope

[BRep Viewer](../tools/smlib_viewer/README.md).

## Integrator action

Update the viewer application modules with the matching portable runtime, or
rebuild the optional distribution. Use `--scan DIRECTORY --output NEW_DIRECTORY`.

## Compatibility

Single-file viewing and SDK/native import contracts retain their existing behavior.
Scan success establishes viewer-pipeline completion only. Model libraries, reports
and screenshots remain local and are not included in the distribution.

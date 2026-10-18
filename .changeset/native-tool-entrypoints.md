---
change: fix
---

The native GUI operation package imports successfully after removal of scene
converters, and Windows smoke validation initializes its hidden WGL window.
Wheel packaging has explicit staging settings and accepts CMake runtime outputs.

## Scope

Maintenance

## Integrator action

Use Python 3.12 and the selected native runtime. Keep optional GUI dependencies
in the locked GUI environment.

## Compatibility

The `usd_brep` and `_omni_solid` names and stable modeling API are retained.
Removed converter functions remain unavailable; native SMB/SMP workbench I/O
continues through the supported file dispatcher.

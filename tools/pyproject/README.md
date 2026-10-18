# SMLib geometry SDK

The historical `usd_brep` package name is retained for compatibility. This build
contains the SMLib geometry kernel and SM_API Python bindings, without OpenUSD.
Requires CPython 3.12 and the Microsoft VC++ runtime on Windows.

```python
import usd_brep as sm
box = sm.create_box((0, 0, 0), 10, 20, 30)
print(box.volume())
mesh = sm.tessellate(box)
```

STEP import is available through the native STEP_IMPORT C++ library. Debug and
GUI tools are separate development components.

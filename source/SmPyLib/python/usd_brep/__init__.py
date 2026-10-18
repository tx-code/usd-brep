# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""SMLib geometry SDK. The historical package name is retained for compatibility."""
import importlib.util
import os
import sys
import types
from pathlib import Path

_HERE = Path(__file__).parent
# Hold handles for the lifetime of the extension; closing one removes its path
# from Windows' extension dependency search. Support build, SDK and wheel layouts.
_dll_handles = []
if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
    for _libs in (_HERE, _HERE.parent, _HERE.parent.parent / "lib"):
        if _libs.is_dir():
            _dll_handles.append(os.add_dll_directory(str(_libs)))

if importlib.util.find_spec(f"{__name__}._omni_solid") is not None:
    from ._omni_solid import *  # noqa: F403
    from . import _omni_solid as _core
else:
    from _omni_solid import *  # noqa: F403
    import _omni_solid as _core

__all__ = [_name for _name in dir(_core) if not _name.startswith("_")]
for _name in __all__:
    _attr = getattr(_core, _name)
    if isinstance(_attr, types.ModuleType):
        sys.modules[f"{__name__}.{_name}"] = _attr

# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Map the repo VERSION onto a PEP 440 version for the Python wheel.

VERSION carries release-channel markers that PEP 440 has no room for::

    0.10              ->  0.10
    0.15-EA           ->  0.15a0            early access -> alpha
    0.16-EA-internal  ->  0.16a0, internal
    0.2.0-RC          ->  0.2.0rc0          release candidate
    0.2.0-RC2         ->  0.2.0rc2          numbered release candidate (also RC.2)
    2.0.0-alpha.1     ->  2.0.0a1           fork development preview
    2.0.0-beta.2      ->  2.0.0b2
    2.0.0-rc.3        ->  2.0.0rc3

`internal` is a distribution gate rather than a version component, and a wheel
cannot carry it: encoding it would need a local version label, which KitMaker
rejects outright. It is returned separately so publishing can refuse instead.

Unknown markers raise. A wrong version silently published to PyPI is permanent --
releases there are immutable and filenames cannot be reused.
"""

import re
from typing import NamedTuple


_CORE = re.compile(r"^\d+(\.\d+)*$")
_PART = r"(?:0|[1-9][0-9]*)"
_REPOSITORY = re.compile(
    rf"(?P<core>{_PART}\.{_PART}\.{_PART})(?:-(?P<stage>alpha|beta|rc)\.(?P<number>{_PART}))?"
)

# Channel markers, in the order they may appear after the numeric core.
_PRE_RELEASE = {"EA": "a0", "RC": "rc0"}
_NUMBERED_RC = re.compile(r"^RC\.?(\d+)$", re.IGNORECASE)
_INTERNAL = "internal"


class WheelVersion(NamedTuple):
    version: str
    internal: bool


class RepositoryVersion(NamedTuple):
    version: str
    core: str
    wheel: str
    stage: str
    number: int


def repository_version(raw: str) -> RepositoryVersion:
    """Parse the canonical fork VERSION; legacy packaging channels stay separate."""
    value = raw.strip()
    match = _REPOSITORY.fullmatch(value) if len(value) <= 128 else None
    if match is None:
        raise ValueError("VERSION must be MAJOR.MINOR.PATCH[-alpha.N|-beta.N|-rc.N] without leading zeros")
    core, stage = match["core"], match["stage"] or ""
    number = int(match["number"] or 0)
    suffix = f"{dict(alpha='a', beta='b', rc='rc')[stage]}{number}" if stage else ""
    return RepositoryVersion(value, core, core + suffix, stage, number)


def wheel_version(raw: str) -> WheelVersion:
    """Convert the contents of the VERSION file to a PEP 440 version."""
    try:
        canonical = repository_version(raw)
    except ValueError:
        pass
    else:
        return WheelVersion(canonical.wheel, False)
    core, *markers = raw.strip().split("-")
    if not _CORE.match(core):
        raise ValueError(f"VERSION does not start with a numeric release: {raw!r}")

    internal = False
    pre_release = ""
    for marker in markers:
        if marker == _INTERNAL:
            internal = True
        elif marker.upper() in _PRE_RELEASE and not pre_release:
            pre_release = _PRE_RELEASE[marker.upper()]
        elif _NUMBERED_RC.match(marker) and not pre_release:
            pre_release = f"rc{int(_NUMBERED_RC.match(marker).group(1))}"
        else:
            raise ValueError(f"unrecognized marker {marker!r} in VERSION {raw!r}")

    return WheelVersion(f"{core}{pre_release}", internal)

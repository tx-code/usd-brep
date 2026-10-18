# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""File import/export operations for the SMLib GUI."""

from __future__ import annotations

import os

from ..model import ActiveObject, make_active_objects
from ..runtime import sm

SMB_EXTENSION = ".smb"
SMP_EXTENSION = ".smp"
SUPPORTED_LOAD_EXTENSIONS = frozenset({SMB_EXTENSION, SMP_EXTENSION})
LOAD_FILE_FILTER = "All supported (*.smb *.smp);;SMLib BRep (*.smb);;SMLib Part (*.smp);;All files (*)"
SAVE_FILE_FILTER = "SMLib BRep (*.smb);;All files (*)"


def file_kind_for_path(filename: str) -> str:
    """Return the supported GUI file kind for ``filename``."""
    ext = os.path.splitext(filename)[1].lower()
    if ext == SMB_EXTENSION:
        return "smb"
    if ext == SMP_EXTENSION:
        return "smp"
    return ""


def extension_for_filter(selected_filter: str) -> str:
    """Return the default extension implied by a Qt file-dialog filter."""
    normalized = selected_filter.lower()
    if normalized.count("*.") != 1:
        return ""
    if "*.smb" in normalized:
        return ".smb"
    if "*.smp" in normalized:
        return ".smp"
    return ""


def ensure_extension_for_filter(filename: str, selected_filter: str) -> str:
    """Append a selected-filter extension when the dialog path has none."""
    if os.path.splitext(filename)[1]:
        return filename
    ext = extension_for_filter(selected_filter)
    if not ext:
        return filename
    return filename + ext


def native_brep_load_unavailable_reason() -> str:
    return "Native SMLib BRep (*.smb) load is not exposed by this _omni_solid build."


def native_brep_file_is_ascii(filename: str) -> bool:
    """Return whether a native SMLib BRep or part file uses the ASCII format."""
    with open(filename, "rb") as file:
        header = file.read(16)
    if header.startswith(b"Version "):
        return True
    if header.startswith(b"//Brep Starts"):
        return True
    if header.startswith(b"//[Output Summar"):
        return True
    if len(header) >= 2 and header[0] == ord("V"):
        return False
    raise RuntimeError(f"Could not determine native SMLib BRep file format: {filename}")


def native_file_is_part(filename: str) -> bool:
    """Return whether a native SMLib file uses the multi-object part format."""
    if os.path.splitext(filename)[1].lower() == SMP_EXTENSION:
        return True
    with open(filename, "rb") as file:
        return file.read(16).startswith(b"//[Output Summar")


def _stem_name(filename: str) -> str:
    stem = os.path.splitext(os.path.basename(filename))[0]
    return stem or "imported"


def import_native_brep_object(filename: str, starting_id: int = 1, ascii: bool | None = None) -> list[ActiveObject]:
    """Import a native SMLib single-BRep or multi-object part file."""
    if sm is None:
        raise RuntimeError("_omni_solid module not found")

    read_ascii = native_brep_file_is_ascii(filename) if ascii is None else ascii
    if native_file_is_part(filename):
        read_part = getattr(sm, "read_part_from_file", None)
        if read_part is None:
            raise RuntimeError("Native SMLib part (*.smp) load is not exposed by this _omni_solid build.")
        curves, surfaces, _boolean_tree_nodes, breps = read_part(filename, ascii=read_ascii)
        base_name = _stem_name(filename)
        imported = [
            *((f"{base_name}_curve{i}", curve) for i, curve in enumerate(curves, start=1)),
            *((f"{base_name}_surface{i}", surface) for i, surface in enumerate(surfaces, start=1)),
            *((f"{base_name}_brep{i}", brep) for i, brep in enumerate(breps, start=1)),
        ]
        if not imported:
            raise RuntimeError(f"No displayable SMLib objects found in part file {filename}")
        return make_active_objects(imported, starting_id=starting_id)

    read_brep = getattr(sm, "read_brep_from_file", None)
    if read_brep is None:
        raise RuntimeError(native_brep_load_unavailable_reason())
    brep = read_brep(filename, ascii=read_ascii)
    return make_active_objects([(_stem_name(filename), brep)], starting_id=starting_id)


def import_file_objects(filename: str, starting_id: int = 1) -> list[ActiveObject]:
    """Import any model format supported by the GUI."""
    kind = file_kind_for_path(filename)
    if kind in ("smb", "smp"):
        return import_native_brep_object(filename, starting_id=starting_id)
    supported = ", ".join(sorted(SUPPORTED_LOAD_EXTENSIONS))
    raise RuntimeError(f"Unsupported file type for {filename}; expected one of: {supported}")


def export_native_brep(obj: ActiveObject, filename: str, ascii: bool = True) -> None:
    """Export one selected BRep through the native BRep write API."""
    if obj.kind != "Brep":
        raise RuntimeError("Select a BRep object before saving native BRep data")
    obj.handle.write_to_file(filename, ascii=ascii)

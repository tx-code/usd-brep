# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Keep application Python modules replaceable without recollecting Qt/VTK."""

# Dependencies stay in PYZ. Only our application packages use ordinary modules
# beside the embedded runtime; a UI-only update can replace them and restart.
module_collection_mode = {"tools.smlib_viewer": "py", "tools.smlib_gui": "py"}

# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit native quality selection; no fallback after a model is refused."""

QUALITY_PROFILES = ('development', 'tolerance-study', 'work-tolerance-study', 'fine-work-tolerance-study')
FINE_WORK_QUALITY_ID = 'Q-AP214-fine-work-tolerance-study-mm-01'


def validate_receipt(manifest, settings):
    fine = settings.profile == 'fine-work-tolerance-study'
    actual = manifest.get('quality_id')
    # Historical/default scenes retain their existing payload. A new study
    # cannot borrow an older worker's Ready result or another profile's model.
    if fine and actual != FINE_WORK_QUALITY_ID:
        raise ValueError('MissingOrMismatchedFineWorkQualityReceipt')
    if not fine and actual == FINE_WORK_QUALITY_ID:
        raise ValueError('UnexpectedFineWorkQualityReceipt')

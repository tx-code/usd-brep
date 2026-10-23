# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit import integration requests; independent of Qt and picking queries."""
import math


def relative_accuracy(value):
    if type(value) not in (int, float) or not 1e-8 <= value <= 1e-4 or not math.isfinite(value):
        raise ValueError('IntegrationRelativeAccuracyRequiresFiniteValueInRange1e-8To1e-4')
    return float(value)


def arguments(settings):
    value = getattr(settings, 'integration_relative_accuracy', None)
    return [] if value is None else ['--integration-relative-accuracy', format(relative_accuracy(value), '.17g')]


def validate_receipt(manifest, settings):
    requested = getattr(settings, 'integration_relative_accuracy', None)
    observation = manifest.get('numerical_integration')
    if requested is None:
        if observation is not None:
            raise ValueError('UnexpectedIntegrationObservation')
        return
    if not isinstance(observation, dict):
        raise ValueError('MissingIntegrationObservation')
    actual_request = relative_accuracy(observation.get('requested_relative_accuracy'))
    applied = observation.get('applied_relative_accuracy')
    completed = observation.get('completed')
    # A request does not prove execution. Surface/container roles may retain
    # NotRun; completed fused material integrals must have an applied value.
    if (actual_request != requested or (applied is not None and relative_accuracy(applied) != requested)
            or type(completed) is not bool or (completed and applied is None)
            or (manifest.get('role') in {'MaterialSolid', 'MaterialCollection'} and not completed)
            or observation.get('continuous_error_bound_established') is not False
            or observation.get('control_scope') != 'DimensionlessNumericalStoppingOnly'):
        raise ValueError('IntegrationObservationMismatch')

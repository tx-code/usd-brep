// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Observe the exact form found behind SM_DEFINED_HASH_ORDER. This compiles:
// the string is the condition, not a message attached to a false condition.
static_assert("not implemented");

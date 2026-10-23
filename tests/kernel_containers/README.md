<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Kernel container contracts

`SmTArray` move construction transfers an owned data block, its used size and its
capacity. It does not clone or delete pointed-to objects. A borrowed block is copied
into independent owned storage before the source is detached, so external stack
storage cannot escape through the move. A successful source becomes empty with zero
capacity and can be reused. Real allocation failure leaves a borrowed source intact
and propagates as `std::bad_alloc`; the constructor is not unconditionally `noexcept`.
The class retains its existing static-element/raw-storage restrictions.

The base `SmObject` copy-construction policy controls context association: ordinary
in-place/stack construction inherits the source association, while construction
through `SmObject::operator new` retains that allocation's destination association.
The context is nonowning. Moving array storage does not transfer context ownership.
Lvalue copying, copy assignment and the point-set subclass's copy behavior retain
their existing contracts. This change does not add move assignment or change class
layout/vtables.

`kernel_array_move_contracts` covers owned, reserved-empty, borrowed, grown/detached
and pointer arrays, source reuse, explicit placement contexts, derived copying and
two transfers through an expected value and implicit return of a local array into it.
Owned data retains its original address through construction and extraction,
replacing the two data-buffer copies that the
old copy-only implementation performs. This establishes storage behavior, not a
whole-model import speedup or complete allocator-failure qualification.

`kernel_array_allocation_contracts` replaces the allocator boundary only in its
test executable. It counts zero data-buffer allocations for owned transfer and
one for borrowed copying, then injects `std::bad_alloc` and verifies that the
borrowed source remains intact and can be retried. This avoids host-memory
exhaustion and the Debug kernel's null-allocation assertion path.

Run with the validation-enabled root SDK build and matching Release/Debug native
libraries. A standalone CMake build is also supported with `SMLIB_LIBRARY_DIR` and
`SMLIB_RUNTIME_DIR`. Build headers, CRT and kernel-layout macros must match.
For the shared-header change, also run native API/geometry controls and the STEP
import contracts. Compare new imports against frozen pre-change report bytes;
running an old importer against the new DLL alone cannot detect a kernel regression.

Directed isolated mutants should replace owned transfer with a copy, let borrowed
storage escape as a view, forget the source reset or lose its context association.
Unchanged controls must pass and mutants must reach a contract assertion after
compiling. Keep mutated headers and per-run receipts in ignored local directories.

---
change: feature
---

`SmTArray` supports move construction. Owned array buffers transfer without element
copies or data-buffer reallocation; borrowed buffers become independent owned data.
Successful moves leave the source empty and reusable. Pointer elements keep their
existing ownership, and context association follows `SmObject` construction rules.

## Scope

[Native container contract](../tests/kernel_containers/README.md).

## Integrator action

Rebuild C++ consumers with matching SDK headers, libraries and CRT. Reacquire array
views after a move and transfer any caller-managed pointee cleanup responsibility
with the values. Populate moved-from arrays before using their elements again.

## Compatibility

C++ rvalue construction now consumes array storage instead of invoking the copy
constructor. Lvalue copying and copy assignment retain their contracts. Borrowed
storage may allocate and propagate actual allocation failure as `std::bad_alloc`.
No object layout, vtable, native-file, STEP/report, precision or quality change.

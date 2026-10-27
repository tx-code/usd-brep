# Serial import allocation retention

`StepImportResources.cpp` checks complete imports, failures at native-call budget
boundaries, numerical refusal, report exhaustion, cancellation and editable-copy
cleanup. The retention allowance remains **4096 bytes and eight blocks**, and
the positive control still retains 64 real snapshots, requiring growth above
65536 bytes and 256 blocks before checking their release.

## Measurement scope

`ResourceObserver` records the shared CRT's underlying `HeapAlloc`, `HeapReAlloc`
and `HeapFree` requests in the test process. Allocations originate on the serial
import thread; a tracked request can subsequently be freed or resized on any
thread. This covers the matching shared-CRT SDK and adapter, including ordinary
`new`, `malloc`, `calloc` and their cleanup. Sizes are underlying CRT heap-request
sizes, including runtime headers where applicable, rather than model sizes.

The Windows-only helper temporarily redirects the loaded matching CRT's three
heap imports. It preserves the original dispatch and Win32 last-error behavior,
and restores the imports when the session ends. It does not alter SDK/system
files, inject into another process or change system settings. Recording uses a
fixed table outside the observed allocator. Missing imports, installation or
restoration failure, counter overflow and table exhaustion fail observation;
they never silently drop requests or turn an unqualified run into a pass.

The table's process memory is included in the diagnostic process totals. It is
not charged as a dynamic model allocation. The importing execution's configured
resource limits, report budgets and final publication decision remain unchanged.

Whole-process heaps mix application requests, runtime bookkeeping and unrelated
thread lifetimes. SMLib's Windows DLL thread-attach path itself creates TLS for
threads that never call a geometry API; the loader and CRT have additional thread
storage. A process-heap increase therefore does not by itself establish retained
import candidates. `heap_busy_*`, global debug `crt_*`, `private_bytes` and the
process lifetime peak remain diagnostics. `import_thread_crt_*` identifies the
scoped ledger used by the unchanged retention assertions.

This is not a general leak detector. Static/different CRTs, native work on worker
threads, direct virtual allocation and other SDK allocators are outside its
qualification. Separate contexts do not establish kernel thread safety. The
resource fixture's imports remain serial; only cancellation control and the
observer's allocator controls use other threads.

## Reusable controls

`step_import_resource_observer_contracts` runs against the actual shared CRT and
SMLib DLL. Sixteen idle threads, which perform no geometry operation, reproduce
the old process-heap finding while leaving the importing-thread ledger unchanged.
This reproduces the scope error; it does not claim that every block in a past
intermittent run has been individually attributed.

Independent controls retain 4097 bytes or nine small requests, which must still
violate the original allowance. They check resize, balanced free, cross-thread
free, duplicate session refusal, restoration, restart and fail-closed capacity
exhaustion. The ordinary resource suite additionally calibrates with real native
snapshots, so interception limited to driver allocations cannot suffice.

Build with the root validation option and matching runtime, then run:

```shell
cmake --build <build> --config Release --target step_import_resources step_import_resource_observer_tests
ctest --test-dir <build> -C Release --output-on-failure -R '^step_import_.*resources$|^step_import_resource_observer_contracts$'
```

Repeat the relevant cases with the matching Debug build. Native completion and
resource statuses remain separate from observer qualification. Per-run receipts
and allocation diagnostics stay under ignored `.local/` directories.

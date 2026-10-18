# STEP source inspection (ticket 02)

An internal C++20 source layer with no SMLib, OCCT, USD or third-party parser link.
It captures input bytes, parses raw values, indexes all DATA records and resolves
all reference occurrences. `Inspected` means this source-only work completed;
AP/schema rules, types, units, geometry and native BRep construction remain unrun.
The JSON field `native_build` means SMLib BRep construction, not C++ compilation.

An explicit `HeaderNamePolicy::PreserveOpaqueBytes` (CLI
`--preserve-opaque-header-name`) can retain nonstandard raw high bytes in exactly
`HEADER.FILE_NAME.name`. It publishes that one value as `OpaqueString`, with
`text: null`, original byte span/raw token and an `OpaqueHeaderFileName` notice.
Its encoding and effective text remain unestablished: no locale, code-page,
UTF-8 or Unicode normalization is guessed. Names without high bytes still use
the existing Part21 decoder. The default remains strict ASCII.

This exception does not admit high bytes in other HEADER fields, DATA, comments
or identifiers; DEL/control bytes also remain unsupported. The complete carrier
is checked against the single allowed token span before DATA parsing. Original
string/record/input/report budgets still apply. The unknown name is retained
metadata, not proof of standard string conformance; `Inspected` still means only
completion of the explicitly selected source inspection.

Standalone build, from the repository root:

```powershell
cmake -S source/STEP_SOURCE -B C:/temp/step-source-build
cmake --build C:/temp/step-source-build --config Debug
ctest --test-dir C:/temp/step-source-build -C Debug --output-on-failure
& C:/temp/step-source-build/Debug/step_inspect.exe --fixture-limits tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P01/input.step
```

Choose a generator/installation for the local compiler as needed. The root Premake
also registers the `STEP_SOURCE` static library and `step_inspect` console target,
outside the USD-dependent stack. Standalone CMake does not build the rest of the kernel.

```cpp
#include "StepSource.h"

auto result = step_source::InspectFile(path, step_source::Limits::PlanarFixtures());
if (result.source)
{
    auto snapshot = result.source; // Shared owning, immutable source.
    const auto* record = snapshot->Find("203"); // Canonical ID, no '#'.
    const auto& references = snapshot->References();
    const auto& incoming = snapshot->Incoming("203"); // Indices into References().
}
```

`InspectBytes` accepts owned bytes by value; `InspectFile` captures a bounded byte
stream before parsing and never reopens the path. Snapshots own the bytes, value
tree and indices; changing/removing the path or releasing the outcome does not
change a retained snapshot. Const views remain valid while a snapshot owner lives.
Capture is not a filesystem transaction against simultaneous writers: its exact
captured bytes and SHA256 are the input identity. There is no mutable global parser state.

Failures expose no partial record graph. If capture completed, `captured` and its
hash remain available for diagnostics/retry. Incomplete I/O or an input-byte-limit
rejection has no complete captured input. Allocation/infrastructure exceptions are
separate from source invalidity; arbitrary fault cleanup/cancellation is a later ticket.

The current carrier slice is deliberately narrow:

| Supported | Explicitly unsupported |
|---|---|
| Self-contained ASCII Part21 text, FILE_DESCRIPTION `2;1`, exactly one unparameterized DATA | Other implementation levels, multiple/parameterized DATA, extra HEADER records, ANCHOR/REFERENCE/SIGNATURE |
| Required FILE_DESCRIPTION/FILE_NAME/FILE_SCHEMA values; complete DATA and end marker | ZIP, gzip, XML, BOM/raw non-ASCII and other control-byte encodings |
| Spaces/tabs, LF/CRLF/CR separators, block comments, multiline records | Print directives, binary values, user-defined keywords, value-instance forms |
| Simple and external complex syntax, all constituent names/values in source order | ISO 8859 alphabets other than part 1 (`PB` through `PI`) |
| ASCII string envelope; `X`, `X2`, `X4`, Latin-1 `S` and `PA`; doubled apostrophe/backslash | Print control directives (unknown/malformed directives are rejected) |
| CR/LF/tab layout inside strings is ignored, including within directives/doubled delimiters | Layout inserted inside non-string tokens is outside this slice |
| Exact integer/real lexemes, enum tokens, typed values, aggregates, `$`, `*`, IDs/references | Numeric interpretation, enum membership, required/derived-slot and EXPRESS validation |

Unknown ordinary entity/type keywords and unknown complex constituents are retained
by this raw layer, without granting profile support. The six approved complex
signatures are all preserved by the frozen corpus tests. Profile allowlisting and
schema binding belong to ticket 03 and later. Raw reference cycles are indexed
without recursive graph expansion; later semantic relations determine which cycles
are forbidden.

Lexical implementation was checked against the editor-hosted
[Part21 text, clauses 5–8](https://www.steptools.com/stds/step/IS_final_p21e3.html).
Numbers remain strings, so exponent size, leading zeroes and signed zero never
pass through `double`. This narrow implementation does not claim edition-3 or
complete Part21/P1 conformance.

String values expose decoded UTF-8, without Unicode normalization. `X` consumes
two uppercase hexadecimal digits; `X2` and `X4` consume nonempty groups of four
and eight respectively, terminated by `X0`. Groups denote UCS code points, not
a UTF-16 stream: surrogate code points (including paired surrogate groups) and
values above U+10FFFF return `InvalidStringCodePoint`. Malformed directives
return `InvalidStringEncoding`; neither case publishes a partial source graph.
Well-formed unsupported alphabets and print controls return
`UnsupportedStringEncoding`. Literal high bytes are not decoded by guessing the
machine locale or a legacy code page; direct UTF-8 requires a separate carrier
profile and is not enabled by this increment.

Physical CR/LF/tab inside a string contributes no effective character. A value
newline is encoded with `X` (for example `\X\0A`). Spaces are always preserved.
Raw bytes, SHA256, spans and byte locations retain the complete original token,
including physical layout and escape directives. String budgets and
`max_string_bytes` count those original bytes, including both apostrophes; they
do not count decoded Unicode characters or decoded UTF-8 bytes. Decoding cannot
expand beyond the original string token's byte size. This is a reader contract,
not a claim of complete Part21 encoding conformance.

JSON `text` fields contain Unicode values. Raw byte fields and diagnostic byte
strings retain the existing byte-preserving escaping; decoded value reporting
uses a separate UTF-8 path in the same bounded writer. The actual emitted byte
count, including UTF-8 and JSON escapes, controls the report limit.

IDs are positive decimal strings, canonicalized only for identity lookup. `#0001`
and `#1` collide; their original spellings/spans remain in captured bytes. No storage
is sized by the largest numerical ID. Reference validation happens after the end
marker and all DATA records, allowing forward references and reordering.

Spans are zero-based/exclusive byte ranges. Locations are one-based byte line/column.
Record spans run from `#` through `;`, excluding following separators; component
spans cover `NAME(...)`; value spans cover exact tokens/containers. The immutable
bytes retain comments and separators too. IDs are strings in JSON to avoid loss
through JavaScript integer limits. Parameter paths use one-based slots/children;
record/component indices in the reference table are zero-based.

`Limits::PlanarFixtures()` is an explicitly selected test preset, not a production
default: 1MiB input, 10,000 DATA records, 200,000 values (including HEADER), 100,000
references, value depth 64, string token bytes 32,769, HEADER/DATA record bytes
65,536, report bytes 16MiB and ID digits 128. Callers may supply smaller limits or
use the CLI overrides. Source value depth counts parameter/aggregate/typed-child
levels and differs from ticket 01's raw-parenthesis measurement.

Independent safety ceilings reject excessive configurations before capture:
64MiB input, 1,000,000 records, 2,000,000 values/references, depth 128, 1MiB string,
16MiB record, 256MiB report, 1,024 ID digits. These bounds do not claim a measured
process-memory/time cap. HEADER/DATA record-byte bounds are checked at each record's
end; total input, node, depth and string bounds also limit parser construction.

`InspectJson` enforces the actual emitted UTF-8 byte budget and clears output on
overflow. The CLI emits no partial success JSON. Windows stdout is binary to keep
the final LF within that same budget. The CLI requires `--fixture-limits` and accepts
`--max-input-bytes`, `--max-records`, `--max-values`, `--max-references`, `--max-depth`,
`--max-string-bytes`, `--max-record-bytes`, `--max-report-bytes`, `--max-id-digits`.

Exit codes: 0 Inspected, 1 Rejected, 2 Unsupported, 3 ResourceLimit, 4 IoFailure,
5 report byte limit (no JSON), 6 infrastructure/report I/O failure, 64 bad arguments.
None is a geometric model acceptance code.

N02 returns `DuplicateInstanceId/D0`, with the second ID as primary span and the
first as related. N03 returns `UnresolvedRequiredReference/D1`, with owner #9,
`SHAPE_DEFINITION_REPRESENTATION`, parameter path `[1]` (definition), target #99999
and its token span. Attribute names/type views are not guessed for arbitrary entities.

See the [ticket 02 evidence adapter](../../tools/step_inspect_validation/README.md)
for actual-reader corpus checks and additional carrier/resource tests.

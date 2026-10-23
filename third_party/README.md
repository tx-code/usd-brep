# Third-party source dependencies

| Dependency | Version | License | Integration |
|---|---|---|---|
| [Microsoft GSL](https://github.com/microsoft/GSL/releases/tag/v5.0.1) | 5.0.1 | MIT | Unmodified headers in `microsoft-gsl/include`; private STEP implementation dependency |
| [Boost.Multiprecision](https://github.com/boostorg/multiprecision) and [Boost.Config](https://github.com/boostorg/config) | 1.89.0 | BSL-1.0 | Unmodified standalone headers in `boost-multiprecision/include`; private STEP unit arithmetic dependency |
| [tl::expected](https://github.com/TartanLlama/expected/tree/v1.3.1) | 1.3.1 | CC0-1.0 | Unmodified single header in `tl-expected/include`; private STEP import result vocabulary |

`microsoft-gsl/UPSTREAM.json` pins the source commit, archive SHA256 and each
vendored file. The upstream license and notices are retained. CMake uses
`cmake/MicrosoftGsl.cmake`. No dependency
download or upstream test build happens during configuration.

Use `gsl::not_null` for internal handles that are nonnull by construction and
`Expects`/`Ensures` for programmer contracts. GSL contract violations terminate
in Debug and Release. Source-file errors, resource limits and public caller-error
contracts retain their existing diagnostics or exceptions. Test assertions retain
informative failure reports. Standard-library vocabulary remains preferred where
it already expresses the required contract.

`boost-multiprecision/UPSTREAM.json` pins both source commits, archive hashes and
every header. `cmake/BoostMultiprecision.cmake` enables `BOOST_MP_STANDALONE` with
no configure-time network access. The implementation uses checked fixed storage,
not unrestricted arbitrary-precision allocation. Only `StepImportUnits.cpp`
includes the library; SDK headers expose exact integer strings and no Boost types.
Keep `LICENSE_1_0.txt` with source and compiled distributions.

`tl-expected/UPSTREAM.json` pins the upstream commit, archive SHA256, header and
CC0 dedication. `cmake/TlExpected.cmake` supplies the private C++20 include target
without configuration-time network access or an upstream build. Keep the header
bytes unmodified. The native SDK carries the source/provenance and
`PACKAGE-LICENSES/TlExpected/COPYING`; the shared notice inventory carries the
dedication for compiled distributions. Public SDK headers do not expose tl types.

# Third-party source dependencies

| Dependency | Version | License | Integration |
|---|---|---|---|
| [Microsoft GSL](https://github.com/microsoft/GSL/releases/tag/v5.0.1) | 5.0.1 | MIT | Unmodified headers in `microsoft-gsl/include`; private STEP implementation dependency |

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

# Changelog

All notable changes to the XYO C++ SDK will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Removed
- Deleted the unused `openapi/` generated `cpp-restsdk` client and its `openapitools.json` generator config. The transport moved to `cpr` in 2.0.0, but the generated tree was left behind and kept being regenerated despite never being referenced by any build, package or test.

### Added
- `scripts/check_spec_coverage.py`, which fails when the upstream specification declares a request path the hand-written client does not issue.

### Fixed
- Corrected the Conan recipe version, which was still declared as `2.0.0` while CMake, the vcpkg port and the documentation were all at `2.1.0`. A local `conan create .` produced a mislabelled package as a result.

### Changed
- Extended the release pipeline tag check to verify `conanfile.py` and `packaging/vcpkg/vcpkg.json` alongside `CMakeLists.txt`, so no packaging manifest can silently fall behind a release tag again.
- Replaced the `generate.yml` regeneration workflow with `spec-check.yml`. It consumes the same `spec_tagged`/`spec_updated` dispatch, verifies path coverage, builds and tests the SDK, and opens a `spec-drift` issue instead of raising a pull request against generated code.
- Corrected documentation that described the SDK as a wrapper over a generated `cpprestsdk` layer, including the `client.hpp` header comment, the CONTRIBUTING architecture and toolchain sections, and the `clang-format` include ordering.
- Enterprise banking resilience: added environment variable fallback (`XYO_API_BASE_URL`) support across `ClientConfig` constructors for seamless enterprise container and test harness configuration.
- Multi-MIME stream negotiation: extended bulk enrichment collection archive download `Accept` header to negotiate `application/gzip, application/x-tar, application/octet-stream;q=0.9, */*;q=0.8`.
- WAF and reverse-proxy diagnostics: added early `Content-Type` header inspection on archive downloads to identify intermediate proxy/WAF challenge pages.

### Security
- Zero-trust domain validation: enforced strict destination domain validation during bulk enrichment collection archive downloads, restricting downloads strictly to matching API host endpoints or trusted `.amazonaws.com` (S3) storage domains to prevent Server-Side Request Forgery (SSRF).
- SSRF error preview mitigation: eliminated raw response body extraction and preview logging on unexpected Content-Types during archive downloads to prevent leaking sensitive internal infrastructure details or internal error responses.
- Tar bomb & archive slip mitigations: added defensive decompression safeguards when parsing bulk collection archives, including per-entry size limits (`10 MiB`), maximum archive entry count bounds (`50,000` entries), and path traversal rejection (`..` or absolute leading paths).

### Changed
- Release pipeline workflow: enhanced GitHub Actions release pipeline (`release.yml`) with automated `run-tests` step using CMake and CTest prior to publishing release artifacts.
- Upgraded GitHub Actions in release workflow to v4/v1 with SLSA build provenance attestations.
- Aligned `LICENSE` with exact standard Apache 2.0 reference text for license scanner compliance.

## [2.0.0] - 2026-08-14

### Added
- Generated OpenAPI C++ client module based on canonical OpenAPI 3.0 specification using `cpprestsdk`.
- Deterministic OpenAPI Generator CLI setup and workflow automation (`.github/workflows/generate.yml`).
- Modern C++17 `xyo::Client` with PIMPL idiom (`std::unique_ptr<Impl>`) providing ABI stability and clean public interface.
- Comprehensive in-memory mock HTTP server unit test suite covering single transaction, collection, status, archive downloads, and error handling.

### Changed
- Modernized SDK baseline to C++17.
- Standardized CMake project target name to `XYOSDK::XYOSDK`.
- Bumped SDK version to 2.0.0 across CMake, Conan 2 recipe, vcpkg port, and documentation.

## [1.1.1] - 2026-08-06

### Changed
- Updated repository URLs to `https://github.com/xyo-financial/sdk-cpp`.
- Bumped SDK version to 1.1.1.

## [1.1.0] - 2026-07-20

### Changed
- Relicensed project under BSD 3-Clause License.
- Bumped SDK version to 1.1.0.

## [1.0.1] - 2026-07-18

### Fixed
- Fixed release pipeline artifact publishing and verification workflows.
- Added CI workflow badges and C++ mascot to README documentation.

## [1.0.0] - 2026-07-10

### Added
- Initial release of the C++ SDK mapping the AI Banking Transaction Enrichment API.
- Support for single-transaction and bulk transaction enrichment requests.
- Conan 2 recipe support (`conanfile.py` and `test_package/`).
- vcpkg port config (`packaging/vcpkg/`).
- CMake configuration for package generation and installation.
- GitHub Actions CI/CD workflows for automated builds, testing, and release artifact generation.

[Unreleased]: https://github.com/xyo-financial/sdk-cpp/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/xyo-financial/sdk-cpp/compare/v1.1.1...v2.0.0
[1.1.1]: https://github.com/xyo-financial/sdk-cpp/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/xyo-financial/sdk-cpp/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/xyo-financial/sdk-cpp/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/xyo-financial/sdk-cpp/releases/tag/v1.0.0

# Changelog

All notable changes to the XYO C++ SDK will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2026-08-09

### Changed
- Bumped SDK version to 2.0.0 across CMake, Conan, vcpkg, and documentation.
- Standardized project name in CMake to `XYOSDK`.

## [1.1.1] - 2026-08-06

### Changed
- Updated repository URLs to `xyo-financial/sdk-cpp`.
- Bumped SDK version to 1.1.1.

## [1.1.0] - 2026-07-20

### Changed
- Relicensed project under BSD 3-Clause License.
- Bumped SDK version to 1.1.0.

## [1.0.1] - 2026-07-18

### Added
- Initial implementation of the C++ SDK mapping the AI Banking Transaction Enrichment API.
- Support for single-transaction and bulk transaction enrichment requests.
- Conan 2 recipe support (`conanfile.py` and `test_package/`).
- vcpkg port config (`packaging/vcpkg/`).
- CMake configuration for package generation and installation.
- GitHub Actions CI/CD workflows for automated builds, testing, and release artifact generation.

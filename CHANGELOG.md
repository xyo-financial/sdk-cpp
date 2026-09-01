# Changelog

All notable changes to the XYO C++ SDK will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.2.0] - 2026-09-01

### Added
- **Per-Instance & Per-Thread Connection Pooling**: Replaced static global handle sharing with `(Impl*, thread)` keyed `cpr::Session` pooling, ensuring complete isolation across distinct client configurations, tenants, and worker threads.
- **RFC 7807 Problem Details Support**: Full structured error extraction (`problem_type`, `problem_title`, `problem_detail`, `problem_instance`, `correlation_id`) surfaced via strongly-typed accessors on `xyo::Error`.
- **Locale-Independent HTTP-Date Parsing**: Imbued `std::locale::classic()` in RFC 9110 date parser so non-English host application locales (e.g. `de_DE.UTF-8`) parse `Retry-After` headers reliably.
- **Log Injection Defense (CWE-117)**: Sanitised control characters (`\r`, `\n`) and bounded lengths of `problem_title` and `problem_type` before inclusion in exception `what()` messages.
- **Configurable Storage Domain Allowlisting**: Added `allowed_download_domains` vector to `ClientConfig` with label-boundary validation (`host_matches`) to secure archive downloads across private object stores.
- **Per-Call Timeout Overrides**: Added `request_timeout_ms` override on `EnrichmentRequestOptions` applied across all synchronous and asynchronous operations.
- **Concatenated Multi-Member Gzip & Tar Support**: Resilient multi-member decompression loop using `inflateReset2` and inter-member tar zero-block scanning.
- **UTF-8 Code Point Validation**: Multi-byte character counting (`utf8_length`) to accurately enforce the 128-character limit on non-ASCII transaction narratives.
- **Defensive Transport Safeguards**: Added `allow_insecure_transport` check, API key trailing newline/CRLF rejection, job ID allowlisting (`[a-zA-Z0-9_-]`), and JSON response size ceilings (`MAX_JSON_RESPONSE_SIZE = 10 MiB`).

### Changed
- **Zero-Allocation Batch Serialization**: Optimized `enrichTransactions` to validate against const references and format ISO 3166-1 alpha-2 uppercase codes in-place, eliminating tens of thousands of heap allocations during 50,000-item bulk batches.
- **Raised Default Batch Size**: Aligned `ClientConfig::max_collection_size` default to `50,000` items matching API limits.
- **ABI Version Bump**: Bumped version to `2.2.0` across CMake, Conan, and vcpkg manifests to reflect public ABI additions on `ClientConfig`, `EnrichmentRequestOptions`, and `Error`.

## [2.1.0] - 2026-08-30

### Removed
- Deleted the unused `openapi/` generated `cpp-restsdk` client and its `openapitools.json` generator config.

### Added
- `scripts/check_spec_coverage.py`, which verifies path coverage against the canonical OpenAPI specification.

### Fixed
- Corrected the documented behaviour of `ClientConfig::connect_timeout_ms` in headers and README.
- Synchronized Conan recipe version with CMake, vcpkg port, and documentation.

## [2.0.0] - 2026-08-14

### Added
- Modern C++17 `xyo::Client` with PIMPL idiom (`std::unique_ptr<Impl>`) providing ABI stability and clean public interface.
- Comprehensive in-memory mock HTTP server unit test suite covering single transaction, collection, status, archive downloads, and error handling.

[2.2.0]: https://github.com/xyo-financial/sdk-cpp/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/xyo-financial/sdk-cpp/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/xyo-financial/sdk-cpp/releases/tag/v2.0.0

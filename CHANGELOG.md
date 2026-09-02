# Changelog

All notable changes to the XYO C++ SDK will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.1.0] - 2026-09-01

### Added
- **Bounded Session Pool**: Replaced unbound per-thread session map with an RAII-leased `SessionPool` (`SessionPool::Lease`), guaranteeing bounded socket handles and memory during high-concurrency multi-threaded workloads.
- **RFC 7807 Problem Details Support**: Full structured error extraction (`problem_type`, `problem_title`, `problem_detail`, `problem_instance`, `correlation_id`) surfaced via strongly-typed accessors on `xyo::Error`.
- **Locale-Independent HTTP-Date Parsing**: Imbued `std::locale::classic()` in RFC 9110 date parser so non-English host application locales (e.g. `de_DE.UTF-8`) parse `Retry-After` headers reliably.
- **Log Injection Defense (CWE-117)**: Sanitised control characters (`\r`, `\n`) and bounded lengths of `problem_title`, `problem_type`, `problem_detail`, `problem_instance`, and `correlation_id` directly at `xyo::Error` construction.
- **Strict SSRF Authority & Allowlist Parsing**: Split authority at `/`, `?`, and `#`, added host charset validation, IPv6 bracketed literal parsing, userinfo (`@`) rejection, and case-insensitive scheme matching.
- **Tar Header USTAR Magic & Checksum Validation**: Strict checksum verification with no zero bypass, explicit POSIX `ustar` magic verification, and GNU base-256 binary size rejection.
- **Linear Tar Scan & DoS Prevention**: Replaced quadratic scans with a linear single-pass zero-run walk and hard block cap (`MAX_TAR_BLOCKS_EXAMINED`).
- **Per-Call Timeout Overrides & Non-Positive Validation**: Added `request_timeout_ms` override on `EnrichmentRequestOptions` and enforced positive validation checks on all timeout fields.
- **Moved-from Object Guards**: Added null-checks preventing crashes on moved-from `Client` instances.
- `scripts/check_spec_coverage.py`, which verifies path coverage against the canonical OpenAPI specification.

### Changed
- **Eliminated Per-Item Request Copies**: Optimized `enrichTransactions` to validate against const references and format ISO 3166-1 alpha-2 uppercase codes in-place, eliminating tens of thousands of intermediate string copies during 50,000-item bulk batches.
- **Raised Default Batch Size**: Aligned `ClientConfig::max_collection_size` default to `50,000` items matching API limits.

### Fixed
- Corrected the documented behaviour of `ClientConfig::connect_timeout_ms` in headers and README.
- Synchronized Conan recipe version with CMake, vcpkg port, and documentation.

## [2.0.0] - 2026-08-14

### Added
- Modern C++17 `xyo::Client` with PIMPL idiom (`std::unique_ptr<Impl>`) providing ABI stability and clean public interface.
- Comprehensive in-memory mock HTTP server unit test suite covering single transaction, collection, status, archive downloads, and error handling.

[2.1.0]: https://github.com/xyo-financial/sdk-cpp/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/xyo-financial/sdk-cpp/releases/tag/v2.0.0

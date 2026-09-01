# 🤝 Contributing to XYO C++ SDK

Thank you for your interest in contributing to the **XYO C++ SDK**. This repository provides the institutional-grade modern C++17 client library for the [XYO.Financial](https://xyo.financial) transaction enrichment platform.

To maintain strict performance, memory safety, ABI stability, and consistency across financial integrations, all contributions must adhere to the engineering guidelines, architectural constraints, and quality gates detailed below.

---

## 📑 Table of Contents
- [🏗 Architecture](#architecture)
  - [🔹 1. Public API (`include/xyo/client.hpp`)](#1-public-api-includexyoclienthpp)
  - [🔹 2. Implementation (`src/client.cpp`)](#2-implementation-srcclientcpp)
- [🔀 Contribution Workflow & Decision Matrix](#contribution-workflow-decision-matrix)
- [⚙️ Specification Synchronization](#specification-synchronization)
  - [🔹 Why the reference is not the product](#why-the-reference-is-not-the-product)
  - [🔹 How to use it](#how-to-use-it)
  - [🔹 The isolation is structural](#the-isolation-is-structural)
  - [🔹 Automated Cross-Repository Verification](#automated-cross-repository-verification)
  - [🔹 Running the Coverage Check Locally](#running-the-coverage-check-locally)
  - [⚙️ Applying a Specification Change](#applying-a-specification-change)
- [🛠 Prerequisites & Development Environment](#prerequisites-development-environment)
  - [🔹 Required Toolchain](#required-toolchain)
  - [🔹 Package Installation](#package-installation)
- [🛡 Build & Quality Gates](#build-quality-gates)
  - [🛡 1. CMake Build](#1-cmake-build)
  - [🧪 2. CTest & Mock HTTP Test Suite](#2-ctest-mock-http-test-suite)
  - [🔹 3. Code Formatting & Style](#3-code-formatting-style)
  - [🔹 4. Sanitizers & Memory Safety](#4-sanitizers-memory-safety)
- [📦 Packaging Verification](#packaging-verification)
  - [🧪 CMake Install Test](#cmake-install-test)
  - [🧪 Conan 2.x Package Test (Optional)](#conan-2x-package-test-optional)
- [🚀 Submitting a Pull Request](#submitting-a-pull-request)
- [📦 Release & Versioning Process](#release-versioning-process)
- [🔒 Security](#security)

## 🏗 Architecture

The XYO C++ SDK is hand-written against the [`xyo-financial/specs`](https://github.com/xyo-financial/specs) contract. It is **not** machine-generated: the public API is split from its implementation by the PIMPL idiom so that no third-party header ever reaches a consuming translation unit.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Application Consumer                            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ #include <xyo/client.hpp>
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                    Public API (C++17, header-only surface)             │
│  - include/xyo/client.hpp (zero external headers, stable ABI)          │
│  - Domain structs, xyo::Error hierarchy, xyo::Client facade            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ std::unique_ptr<Impl> (PIMPL boundary)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     Implementation (src/client.cpp)                    │
│  - cpr / libcurl transport, nlohmann::json serialization               │
│  - zlib gunzip + ustar parsing, OpenSSL key cleansing                  │
│  - Validation, SSRF guards, error mapping                              │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ HTTPS / REST (JSON)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     XYO Financial Enrichment API                       │
└────────────────────────────────────────────────────────────────────────┘
```

### 🔹 1. Public API (`include/xyo/client.hpp`)
- **Design Pattern**: Pointer to Implementation (**PIMPL**) idiom (`std::unique_ptr<Impl> impl_`).
- **Zero Leaky Abstractions**: Exposes only standard C++17 types (`std::string`, `std::optional`, `std::vector`, `std::unique_ptr`). Third-party headers such as `<cpr/*>`, `<nlohmann/*>` and `<zlib.h>` are strictly confined to `src/client.cpp`.
- **Type-Safe Domain Models**: Simple domain structs (`xyo::EnrichmentRequest`, `xyo::EnrichmentResponse`, `xyo::BulkEnrichmentResponse`, `xyo::EnrichmentStatus`).
- **Structured Error Hierarchy**: `xyo::Error` with strongly typed `xyo::ErrorCategory` (`validation`, `transport`, `http`, `parsing`, `rate_limit`), HTTP status code accessors and optional `xyo::RateLimitInfo`.

### 🔹 2. Implementation (`src/client.cpp`)
- **Transport**: `cpr` over system libcurl; `nlohmann::json` for serialization.
- **Archive Handling**: In-memory `zlib` decompression and a bespoke POSIX ustar reader for bulk result archives, bounded by explicit size, entry-count and path-traversal guards.
- **Credential Hygiene**: API keys are wiped with `OPENSSL_cleanse` on destruction; header values are validated to prevent CRLF injection.
- **Public Surface**: `enrichTransaction()`, `enrichTransactions()`, `getEnrichmentStatus()` and `downloadEnrichmentCollection()`.

---

## 🔀 Contribution Workflow & Decision Matrix

Before writing code, identify which repository is authoritative for your proposed change:

| Type of Proposed Change | Target Repository | Procedure |
| :--- | :--- | :--- |
| **New API Endpoints / Routes** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Submit PR to update `openapi.yml`. After merge and tag, this SDK's spec check flags the new route for hand implementation. |
| **Schema / Field / Type Changes** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Propose JSON schema modifications in upstream specification repo. |
| **HTTP Status Codes / Wire Contracts** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Update API response contracts in `specs/openapi.yml`. |
| **SDK Ergonomics & Public API** | **This Repository** (`sdk-cpp`) | Propose wrapper improvements in `include/xyo/client.hpp` and `src/client.cpp`. |
| **Error Handling & Exception Logic** | **This Repository** (`sdk-cpp`) | Refine `xyo::ErrorCategory` classification or error reporting in `src/client.cpp`. |
| **Unit & Integration Tests** | **This Repository** (`sdk-cpp`) | Add test cases in `tests/client_test.cpp`. |
| **Build Systems & Packaging** | **This Repository** (`sdk-cpp`) | Update `CMakeLists.txt`, `conanfile.py`, or vcpkg configurations. |
| **Documentation & Examples** | **This Repository** (`sdk-cpp`) | Update `README.md`, `example/`, or Doxygen docstrings. |

---

## ⚙️ Specification Synchronization

> [!IMPORTANT]
> **`openapi/` is a development reference. It is never built, linked or shipped.**
> The code that ships is `src/client.cpp` and `include/xyo/client.hpp`, written and maintained by hand.

### 🔹 Why the reference is not the product

The `cpp-restsdk` generator emits code against Microsoft's cpprestsdk. That library was archived on 1 June 2026 and removed from vcpkg the following day ([microsoft/vcpkg#52130](https://github.com/microsoft/vcpkg/pull/52130)), so there is no longer a port to install on any triplet. Anything linking it would be uninstallable everywhere, not merely heavy on Android. This is why C++ does not follow the fleet convention of generating and wrapping; see [#29](https://github.com/xyo-financial/sdk-cpp/issues/29).

Generating it anyway is still worth the effort. It turns a specification change into a concrete, reviewable diff in C++ terms, rather than prose someone has to notice and interpret.

### 🔹 How to use it

When a specification tag lands, `.github/workflows/generate.yml` regenerates `openapi/` and opens a pull request. **Read that diff as a description of what changed in the contract**, then implement anything material by hand in `src/client.cpp`, expose it in `include/xyo/client.hpp`, and cover it in `tests/client_test.cpp`. Merging the pull request updates the reference only; it changes nothing that ships.

### 🔹 The isolation is structural

Four properties keep the reference out of everything that reaches a consumer, and the workflow asserts each on every run rather than trusting them:

| Boundary | Mechanism |
| :--- | :--- |
| SDK build | `CMakeLists.txt` never references `openapi/`; the reference is compiled by a separate CMake invocation in CI only |
| Conan package | `exports_sources` in `conanfile.py` omits it |
| Installed package | `install()` covers `include/` only |
| Release archive | `.gitattributes` marks it `export-ignore` |

CI compiles the reference in isolation using Ubuntu's `libcpprest-dev`, which still packages cpprestsdk even though vcpkg does not. That is purely so the reference cannot rot into code that no longer builds. It never becomes a dependency of the SDK.

**Never edit `openapi/` by hand**, and never wire it into the build. If it is wrong, fix the specification upstream or the generator invocation in the workflow.


**There is no code generation in this repository.** The client is written and maintained by hand. A specification change therefore does not apply itself; it has to be implemented deliberately, and CI exists to make sure it is not forgotten.

### 🔹 Automated Cross-Repository Verification
When a new release tag or specification update is pushed to [`xyo-financial/specs`](https://github.com/xyo-financial/specs), an automated GitHub Actions workflow triggers a `repository_dispatch` event (`types: [spec_tagged, spec_updated]`) to this repository. The [`.github/workflows/spec-check.yml`](.github/workflows/spec-check.yml) workflow:

1. Checks out `xyo-financial/specs` at the designated tag or ref (`${{ github.event.client_payload.tag || inputs.spec_ref || 'main' }}`).
2. Runs `scripts/check_spec_coverage.py`, which fails if the specification declares a request path the client never issues.
3. Configures, compiles and runs the CTest suite against the current source.
4. Opens a `spec-drift` issue if any of the above fails, so a maintainer can implement the change by hand.

You can also trigger verification manually via GitHub Actions **Workflow Dispatch** with an optional `spec_ref` parameter.

### 🔹 Running the Coverage Check Locally

#### Prerequisites
- Python 3.8+ with PyYAML (`pip install pyyaml`)
- A local clone of `xyo-financial/specs`, or any path to an `openapi.yml`

#### Command
Run from the root of this repository (`sdks/cpp`):

```bash
python3 scripts/check_spec_coverage.py ../specs/openapi.yml
```

The check covers request paths only. It deliberately does not attempt to verify HTTP methods, schemas or field names against hand-written code, because doing so produces false positives that train maintainers to ignore the result. Schema-level drift is caught by the mock-server suite in `tests/client_test.cpp`.

### ⚙️ Applying a Specification Change
1. Review the specification diff upstream in `xyo-financial/specs`.
2. Implement the new or changed operation in `src/client.cpp`, keeping third-party headers behind the PIMPL boundary.
3. Expose it on `xyo::Client` in `include/xyo/client.hpp` using standard C++17 types only.
4. Add mock-server coverage in `tests/client_test.cpp`.
5. Confirm `python3 scripts/check_spec_coverage.py ../specs/openapi.yml` passes.

---

## 🛠 Prerequisites & Development Environment

### 🔹 Required Toolchain
- **C++ Compiler**: C++17 compliant compiler (GCC 9+, Clang 10+, Apple Clang 12+, MSVC 2019+)
- **Build System**: CMake 3.16+
- **Core Dependencies**:
  - `cpr` (1.10+) and system libcurl
  - `nlohmann_json` (3.11+)
  - `OpenSSL` (1.1+ or 3.0+)
  - `ZLIB` (1.2.8+)

`cpr` and `nlohmann_json` are resolved by `find_package` first and fetched via `FetchContent` when absent, so only libcurl, OpenSSL and zlib need to be present on the system.

### 🔹 Package Installation

#### macOS (Homebrew)
```bash
brew install cmake openssl zlib
```

#### Ubuntu / Debian (`apt`)
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  libcurl4-openssl-dev \
  libssl-dev \
  zlib1g-dev
```

#### Fedora / RHEL (`dnf`)
```bash
sudo dnf install -y \
  gcc-c++ \
  cmake \
  libcurl-devel \
  openssl-devel \
  zlib-devel
```

---

## 🛡 Build & Quality Gates

All pull requests must cleanly pass every quality gate before being considered for review.

### 🛡 1. CMake Build

Configure and compile the library and test targets in `Debug` and `Release` modes:

```bash
# 🤝 Configure debug build with tests enabled
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DXYO_BUILD_TESTS=ON

# 🤝 Compile with parallel build jobs
cmake --build build --parallel
```

Using CMake Presets (CMake 3.20+):
```bash
cmake --preset debug
cmake --build --preset debug
```

### 🧪 2. CTest & Mock HTTP Test Suite

The test suite in `tests/client_test.cpp` features an embedded loopback HTTP server built directly on BSD/Winsock sockets, with no external test framework, that validates:
- Real HTTP wire transport and header propagation (e.g. `Authorization: Bearer <token>`, `Content-Type: application/json`).
- Synchronous single transaction enrichment (`enrichTransaction`).
- Asynchronous bulk batch processing (`enrichTransactions`).
- Asynchronous job status polling (`getEnrichmentStatus`).
- Full error category mapping:
  - `ErrorCategory::validation` (empty API keys, empty request arrays, empty job IDs).
  - `ErrorCategory::http` (HTTP 400 Bad Request, 401 Unauthorized, 404 Not Found, 422 Unprocessable Entity, 500 Internal Server Error).
  - `ErrorCategory::parsing` (corrupted payloads, unexpected status strings).
  - `ErrorCategory::transport` (network connection failures, unreachable ports).

Run CTest with verbose output on failure:

```bash
ctest --test-dir build --output-on-failure --verbose
```

Or execute the test binary directly:
```bash
./build/xyo_sdk_tests
```

### 🔹 3. Code Formatting & Style

This project strictly adheres to modern C++ Core Guidelines and LLVM/Google C++ formatting standards:
- 2 spaces indentation, no tabs.
- Explicit variable types for clarity unless using structured bindings.
- All non-throwing member functions marked `noexcept`.
- Const-correctness on all inspect-only methods and parameters.
- File-scoped helper functions enclosed in anonymous namespaces.

Format C++ source and header files using `clang-format` (build directories are excluded via `.clang-format-ignore`):

```bash
clang-format -i \
  include/xyo/client.hpp \
  src/client.cpp \
  tests/client_test.cpp \
  example/main.cpp
```

### 🔹 4. Sanitizers & Memory Safety

To guarantee zero memory leaks, thread-safety, and lack of undefined behavior, run builds under LLVM sanitizers (AddressSanitizer and UndefinedBehaviorSanitizer):

```bash
# 🤝 Configure with ASan and UBSan
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DXYO_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

# 🤝 Build and execute sanitized test suite
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

## 📦 Packaging Verification

Ensure CMake packaging, target exports, and header installations work seamlessly for downstream consumers:

### 🧪 CMake Install Test
```bash
# 🤝 Install to temporary staging directory
cmake --install build --prefix /tmp/xyo_install_test

# 🤝 Verify exported headers and CMake configuration files
ls -la /tmp/xyo_install_test/include/xyo/client.hpp
ls -la /tmp/xyo_install_test/lib/cmake/XYOSDK/XYOSDKConfig.cmake
```

### 🧪 Conan 2.x Package Test (Optional)
```bash
# 🤝 Export and build recipe in Conan cache with version override
conan create . --version=2.1.0 --build=missing -s compiler.cppstd=gnu17

# 🤝 Verify downstream example application build against Conan package
cd example
conan install . --build=missing
cmake --preset conan-release
cmake --build build/Release
./build/Release/xyo_example
cd ..
```

---

## 🚀 Submitting a Pull Request

1. **Branch Naming**: Use descriptive branch names:
   - `feature/add-enrichment-retry-policy`
   - `fix/error-category-transport-timeout`
   - `docs/update-installation-guide`
2. **Commit Hygiene**:
   - Write clear, imperative commit messages adhering to [Conventional Commits](https://www.conventionalcommits.org/) (e.g. `feat: add client connection timeout configuration`).
   - Keep commits focused and atomic.
3. **PR Checklist**:
   - [ ] `scripts/check_spec_coverage.py` passes against the current `openapi.yml`.
   - [ ] Public API in `include/xyo/client.hpp` maintains PIMPL encapsulation (no leaking headers).
   - [ ] All CMake builds compile with zero warnings under `-Wall -Wextra -Wpedantic`.
   - [ ] CTest suite passes 100% cleanly.
   - [ ] Hand-crafted code formatted with `clang-format`.
   - [ ] Documentation (`README.md`, docstrings) updated where relevant.

---

## 📦 Release & Versioning Process

The XYO C++ SDK follows strict [Semantic Versioning (SemVer 2.0.0)](https://semver.org/):

### 📋 Version Increment Checklist

When cutting a new release (e.g. bumping from `2.1.0` to `X.Y.Z`), update the following files:

1. **`CMakeLists.txt`**: Update root project version:
   ```cmake
   project(XYOSDK VERSION X.Y.Z LANGUAGES CXX)
   ```
2. **`packaging/vcpkg/vcpkg.json`**: Update package manifest version:
   ```json
   "version": "X.Y.Z"
   ```
3. **`example/conanfile.txt`**: Update example dependency requirement:
   ```ini
   [requires]
   xyo-sdk/X.Y.Z
   ```
4. **`packaging/conan/config.yml`** & **`packaging/conan/all/conandata.yml`**: Add the new version mapping and tarball checksum:
   ```yaml
   # packaging/conan/config.yml
   versions:
     "X.Y.Z":
       folder: "all"
   ```
5. **`conanfile.py`**: Update recipe version attribute:
   ```python
   version = "X.Y.Z"
   ```
6. **`CHANGELOG.md`**: Document all user-facing additions, fixes, and breaking changes under the release header:
   ```markdown
   ## [X.Y.Z] - YYYY-MM-DD
   ```

### 🚀 Release Workflow & Tagging

1. **PR & Merge**: Submit a release PR (e.g. `release/vX.Y.Z`) to `main`. Merge after all peer reviews and CI checks pass.
2. **Git Release Tag**: Create an annotated git tag pointing to the merged commit on `main`:
   ```bash
   git tag -a vX.Y.Z -m "Release vX.Y.Z"
   git push origin vX.Y.Z
   ```
3. **Automated CI/CD**: The `.github/workflows/release.yml` pipeline triggers automatically upon tag push to:
   - Run unit and integration tests across Linux GCC, Clang, macOS, and Windows MSVC.
   - Generate Software Bill of Materials (SPDX SBOM) and SLSA cryptographic attestations.
   - Publish the GitHub Release with source tarball archives and SHA-256 checksums.
   - Export the release version into the Conan cache and verify downstream example build.
   - Dispatch SDK version updates to the central repository.

---

## 🔒 Security

If you discover a security vulnerability, please do NOT create a public issue. Follow the disclosure instructions outlined in [SECURITY.md](SECURITY.md) or email security@syniol.com directly.

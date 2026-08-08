# Contributing to XYO C++ SDK

Thank you for your interest in contributing to the **XYO C++ SDK**. This repository provides the institutional-grade modern C++17 client library for the [XYO.Financial](https://xyo.financial) transaction enrichment platform.

To maintain strict performance, memory safety, ABI stability, and consistency across financial integrations, all contributions must adhere to the engineering guidelines, architectural constraints, and quality gates detailed below.

---

## Table of Contents

1. [Two-Layer Architecture](#two-layer-architecture)
   - [Generated Layer (`openapi/`)](#1-generated-layer-openapi)
   - [Wrapper Layer (`include/xyo/client.hpp`, `src/client.cpp`)](#2-wrapper-layer-includexyoclienthpp-srcclientcpp)
2. [Contribution Workflow & Decision Matrix](#contribution-workflow--decision-matrix)
   - [API & Data Model Changes](#api--data-model-changes)
   - [SDK Ergonomics, Helpers & Tests](#sdk-ergonomics-helpers--tests)
3. [Local Code Generation](#local-code-generation)
4. [Prerequisites & Development Environment](#prerequisites--development-environment)
5. [Build & Quality Gates](#build--quality-gates)
   - [1. CMake Build](#1-cmake-build)
   - [2. CTest & Mock HTTP Test Suite](#2-ctest--mock-http-test-suite)
   - [3. Code Formatting & Style](#3-code-formatting--style)
   - [4. Sanitizers & Memory Safety](#4-sanitizers--memory-safety)
6. [Packaging Verification](#packaging-verification)
7. [Submitting a Pull Request](#submitting-a-pull-request)
8. [Release & Versioning Process](#release--versioning-process)

---

## Two-Layer Architecture

The XYO C++ SDK is strictly organized into two distinct layers to separate machine-generated wire-protocol code from the developer-facing, idiomatic C++17 API.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Application Consumer                            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ #include <xyo/client.hpp>
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                      Wrapper Layer (C++17 PIMPL)                       │
│  - include/xyo/client.hpp (Public API, zero external headers)          │
│  - src/client.cpp (PIMPL Implementation & Error Mapping)              │
│  - Modern types: std::optional, std::string, std::vector, RAII        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Internal C++ delegation
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     Generated Layer (Read-Only)                        │
│  - openapi/ (Auto-generated via OpenAPI Generator cpp-restsdk)         │
│  - DTO serialization, cpprestsdk ApiClient, Boost integration          │
│  - STRICTLY READ-ONLY: DO NOT EDIT MANUALLY                            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ HTTPS / REST (JSON)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     XYO Financial Enrichment API                       │
└────────────────────────────────────────────────────────────────────────┘
```

### 1. Generated Layer (`openapi/`)
- **Source**: Automatically generated from upstream OpenAPI specifications in [`xyo-financial/specs`](https://github.com/xyo-financial/specs) using `openapi-generator-cli` with the `cpp-restsdk` generator target.
- **Contents**: Low-level HTTP transport management, REST client bindings (`xyo_api::ApiClient`, `xyo_api::EnrichmentApi`), raw serialization logic, and JSON DTOs (`xyo_model::EnrichmentRequest`, `xyo_model::EnrichmentResponse`, `xyo_model::APIError`).
- **Policy**: **STRICTLY READ-ONLY**. Never make manual code edits directly inside the `openapi/` directory. Any manual modifications will be permanently lost during the next specification synchronization run.

### 2. Wrapper Layer (`include/xyo/client.hpp`, `src/client.cpp`)
- **Design Pattern**: Pointer to Implementation (**PIMPL**) idiom (`std::unique_ptr<Impl> impl_`).
- **Characteristics**:
  - **Zero Leaky Abstractions**: Public headers (`include/xyo/client.hpp`) expose only standard C++17 types (`std::string`, `std::optional`, `std::vector`, `std::unique_ptr`). Third-party headers such as `<cpprest/*>` or `<boost/*>` are strictly isolated to `src/client.cpp`.
  - **Type-Safe Domain Models**: Simple domain structs (`xyo::EnrichmentRequest`, `xyo::EnrichmentResponse`, `xyo::BulkEnrichmentResponse`, `xyo::EnrichmentStatus`).
  - **Structured Error Hierarchy**: `xyo::Error` with strongly typed `xyo::ErrorCategory` (`validation`, `transport`, `http`, `parsing`) and HTTP status code accessors.
  - **Synchronous & Asynchronous Ergonomics**: High-level methods `enrichTransaction()`, `enrichTransactions()`, and `getEnrichmentStatus()`.

---

## Contribution Workflow & Decision Matrix

Before writing code, identify which repository is authoritative for your proposed change:

| Type of Proposed Change | Target Repository | Procedure |
| :--- | :--- | :--- |
| **New API Endpoints / Routes** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Submit PR to update `openapi.yml`. After merge, regenerate `openapi/` here. |
| **Schema / Field / Type Changes** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Propose JSON schema modifications in upstream specification repo. |
| **HTTP Status Codes / Wire Contracts** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Update API response contracts in `specs/openapi.yml`. |
| **SDK Ergonomics & Public API** | **This Repository** (`sdk-cpp`) | Propose wrapper improvements in `include/xyo/client.hpp` and `src/client.cpp`. |
| **Error Handling & Exception Logic** | **This Repository** (`sdk-cpp`) | Refine `xyo::ErrorCategory` classification or error reporting in `src/client.cpp`. |
| **Unit & Integration Tests** | **This Repository** (`sdk-cpp`) | Add test cases in `tests/client_test.cpp`. |
| **Build Systems & Packaging** | **This Repository** (`sdk-cpp`) | Update `CMakeLists.txt`, `conanfile.py`, or vcpkg configurations. |
| **Documentation & Examples** | **This Repository** (`sdk-cpp`) | Update `README.md`, `example/`, or Doxygen docstrings. |

---

## Local Code Generation

When upstream OpenAPI specification updates are released in `xyo-financial/specs`, regenerate the `openapi/` layer using the OpenAPI Generator CLI.

### Generator Prerequisites
- Node.js 18+ and `npx`
- Java Runtime Environment (JRE 11+)
- `@openapitools/openapi-generator-cli` (configured in `openapitools.json`)

### Generation Command
From the root of this repository (`sdks/cpp`):

```bash
npx @openapitools/openapi-generator-cli generate \
  -i ../specs/openapi.yml \
  -g cpp-restsdk \
  -o ./openapi \
  --additional-properties=packageName=XYOSDK,modelPackage=xyo_model,apiPackage=xyo_api
```

### Post-Generation Verification
1. Inspect the Git status and diff in `openapi/`:
   ```bash
   git status openapi/
   git diff openapi/
   ```
2. Verify if new source files were added. If so, update the `GENERATED_SOURCES` list in `CMakeLists.txt`.
3. Update the wrapper implementation in `src/client.cpp` and public headers in `include/xyo/client.hpp` to expose any new endpoints or fields.
4. Execute the complete test suite to verify compatibility.

---

## Prerequisites & Development Environment

### Required Toolchain
- **C++ Compiler**: C++17 compliant compiler (GCC 9+, Clang 10+, Apple Clang 12+, MSVC 2019+)
- **Build System**: CMake 3.16+
- **Core Dependencies**:
  - `cpprestsdk` (Casablanca, 2.10+)
  - `Boost` (1.70+ with system/thread/chrono components)
  - `OpenSSL` (1.1+ or 3.0+)

### Package Installation

#### macOS (Homebrew)
```bash
brew install cmake cpprestsdk boost openssl
```

#### Ubuntu / Debian (`apt`)
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  libcpprest-dev \
  libboost-all-dev \
  libssl-dev
```

#### Fedora / RHEL (`dnf`)
```bash
sudo dnf install -y \
  gcc-c++ \
  cmake \
  cpprest-devel \
  boost-devel \
  openssl-devel
```

---

## Build & Quality Gates

All pull requests must cleanly pass every quality gate before being considered for review.

### 1. CMake Build

Configure and compile the library and test targets in `Debug` and `Release` modes:

```bash
# Configure debug build with tests enabled
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DXYO_BUILD_TESTS=ON

# Compile with parallel build jobs
cmake --build build --parallel
```

Using CMake Presets (CMake 3.20+):
```bash
cmake --preset debug
cmake --build --preset debug
```

### 2. CTest & Mock HTTP Test Suite

The test suite in `tests/client_test.cpp` features an embedded loopback HTTP server (`MockHttpServer` using `cpprest::http_listener`) that validates:
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

### 3. Code Formatting & Style

This project strictly adheres to modern C++ Core Guidelines and LLVM/Google C++ formatting standards:
- 2 spaces indentation, no tabs.
- Explicit variable types for clarity unless using structured bindings.
- All non-throwing member functions marked `noexcept`.
- Const-correctness on all inspect-only methods and parameters.
- File-scoped helper functions enclosed in anonymous namespaces.

Format all modified C++ source and header files using `clang-format`:

```bash
clang-format -i \
  include/xyo/client.hpp \
  src/client.cpp \
  tests/client_test.cpp \
  example/main.cpp
```

### 4. Sanitizers & Memory Safety

To guarantee zero memory leaks, thread-safety, and lack of undefined behavior, run builds under LLVM sanitizers (AddressSanitizer and UndefinedBehaviorSanitizer):

```bash
# Configure with ASan and UBSan
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DXYO_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

# Build and execute sanitized test suite
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

## Packaging Verification

Ensure CMake packaging, target exports, and header installations work seamlessly for downstream consumers:

### CMake Install Test
```bash
# Install to temporary staging directory
cmake --install build --prefix /tmp/xyo_install_test

# Verify exported headers and CMake configuration files
ls -la /tmp/xyo_install_test/include/xyo/client.hpp
ls -la /tmp/xyo_install_test/lib/cmake/XYOSDK/XYOSDKConfig.cmake
```

### Conan 2.x Package Test (Optional)
```bash
conan create . --build=missing
```

---

## Submitting a Pull Request

1. **Branch Naming**: Use descriptive branch names:
   - `feature/add-enrichment-retry-policy`
   - `fix/error-category-transport-timeout`
   - `docs/update-installation-guide`
2. **Commit Hygiene**:
   - Write clear, imperative commit messages (e.g. `feat: add client connection timeout configuration`).
   - Keep commits focused and atomic.
3. **PR Checklist**:
   - [ ] No manual edits made to `openapi/` (use generator if spec changed).
   - [ ] Public API in `include/xyo/client.hpp` maintains PIMPL encapsulation (no leaking headers).
   - [ ] All CMake builds compile with zero warnings under `-Wall -Wextra -Wpedantic`.
   - [ ] CTest suite passes 100% cleanly.
   - [ ] Formatted with `clang-format`.
   - [ ] Documentation (`README.md`, docstrings) updated where relevant.

---

## Release & Versioning Process

The XYO C++ SDK follows strict [Semantic Versioning (SemVer 2.0.0)](https://semver.org/):

1. **Version Bump**: Update the project version in `CMakeLists.txt`:
   ```cmake
   project(XYOSDK VERSION X.Y.Z LANGUAGES CXX)
   ```
2. **Changelog**: Document all user-facing additions, fixes, and breaking changes in `CHANGELOG.md` under the version header `[X.Y.Z] - YYYY-MM-DD`.
3. **PR & Merge**: Merge into the `main` branch after peer review and green CI builds across GCC, Clang, macOS, and Windows.
4. **Git Release Tag**: Tag the release commit with matching `vX.Y.Z`:
   ```bash
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```
5. **Automated CI/CD**: The `.github/workflows/release.yml` pipeline triggers automatically to compile binary packages, generate Software Bill of Materials (SBOMs), generate SLSA cryptographic attestations, and publish the GitHub Release.

---

## Security

If you discover a security vulnerability, please do NOT create a public issue. Follow the disclosure instructions outlined in [SECURITY.md](SECURITY.md) or email security@xyo.financial directly.

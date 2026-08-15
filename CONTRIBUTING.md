# Contributing to XYO C++ SDK

Thank you for your interest in contributing to the **XYO C++ SDK**. This repository provides the institutional-grade modern C++17 client library for the [XYO.Financial](https://xyo.financial) transaction enrichment platform.

To maintain strict performance, memory safety, ABI stability, and consistency across financial integrations, all contributions must adhere to the engineering guidelines, architectural constraints, and quality gates detailed below.

---

## 📑 Table of Contents

1. [Two-Layer Architecture](#two-layer-architecture)
   - [Generated Layer (`openapi/`)](#1-generated-layer-openapi)
   - [Wrapper Layer (`include/xyo/client.hpp`, `src/client.cpp`)](#2-wrapper-layer-includexyoclienthpp-srcclientcpp)
2. [Contribution Workflow & Decision Matrix](#contribution-workflow--decision-matrix)
   - [API & Data Model Changes](#api--data-model-changes)
   - [SDK Ergonomics, Helpers & Tests](#sdk-ergonomics-helpers--tests)
3. [Code Generation](#code-generation)
   - [Automated Cross-Repository Synchronization](#automated-cross-repository-synchronization)
   - [Manual / Local Code Generation](#manual--local-code-generation)
   - [Immutable Rule for Generated Code](#immutable-rule-for-generated-code)
4. [Prerequisites & Development Environment](#prerequisites--development-environment)
5. [Build & Quality Gates](#build--quality-gates)
   - [1. CMake Build](#1-cmake-build)
   - [2. CTest & Mock HTTP Test Suite](#2-ctest--mock-http-test-suite)
   - [3. Code Formatting & Style (Excluding Generated Code)](#3-code-formatting--style-excluding-generated-code)
   - [4. Sanitizers & Memory Safety](#4-sanitizers--memory-safety)
6. [Packaging Verification](#packaging-verification)
7. [Submitting a Pull Request](#submitting-a-pull-request)
8. [Release & Versioning Process](#release--versioning-process)
9. [Security](#security)

---

## 🏗 Two-Layer Architecture

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
│  - ⚠️ STRICTLY READ-ONLY & IMMUTABLE: DO NOT EDIT OR FORMAT MANUALLY   │
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
- **Policy**: **STRICTLY READ-ONLY & IMMUTABLE**. Never make manual code edits or format files directly inside the `openapi/` directory. Any manual modifications will be permanently lost during the next specification synchronization run.

### 2. Wrapper Layer (`include/xyo/client.hpp`, `src/client.cpp`)
- **Design Pattern**: Pointer to Implementation (**PIMPL**) idiom (`std::unique_ptr<Impl> impl_`).
- **Characteristics**:
  - **Zero Leaky Abstractions**: Public headers (`include/xyo/client.hpp`) expose only standard C++17 types (`std::string`, `std::optional`, `std::vector`, `std::unique_ptr`). Third-party headers such as `<cpprest/*>` or `<boost/*>` are strictly isolated to `src/client.cpp`.
  - **Type-Safe Domain Models**: Simple domain structs (`xyo::EnrichmentRequest`, `xyo::EnrichmentResponse`, `xyo::BulkEnrichmentResponse`, `xyo::EnrichmentStatus`).
  - **Structured Error Hierarchy**: `xyo::Error` with strongly typed `xyo::ErrorCategory` (`validation`, `transport`, `http`, `parsing`) and HTTP status code accessors.
  - **Synchronous & Asynchronous Ergonomics**: High-level methods `enrichTransaction()`, `enrichTransactions()`, and `getEnrichmentStatus()`.

---

## 🔀 Contribution Workflow & Decision Matrix

Before writing code, identify which repository is authoritative for your proposed change:

| Type of Proposed Change | Target Repository | Procedure |
| :--- | :--- | :--- |
| **New API Endpoints / Routes** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Submit PR to update `openapi.yml`. After merge and tag, the C++ SDK regenerates automatically. |
| **Schema / Field / Type Changes** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Propose JSON schema modifications in upstream specification repo. |
| **HTTP Status Codes / Wire Contracts** | [`xyo-financial/specs`](https://github.com/xyo-financial/specs) | Update API response contracts in `specs/openapi.yml`. |
| **SDK Ergonomics & Public API** | **This Repository** (`sdk-cpp`) | Propose wrapper improvements in `include/xyo/client.hpp` and `src/client.cpp`. |
| **Error Handling & Exception Logic** | **This Repository** (`sdk-cpp`) | Refine `xyo::ErrorCategory` classification or error reporting in `src/client.cpp`. |
| **Unit & Integration Tests** | **This Repository** (`sdk-cpp`) | Add test cases in `tests/client_test.cpp`. |
| **Build Systems & Packaging** | **This Repository** (`sdk-cpp`) | Update `CMakeLists.txt`, `conanfile.py`, or vcpkg configurations. |
| **Documentation & Examples** | **This Repository** (`sdk-cpp`) | Update `README.md`, `example/`, or Doxygen docstrings. |

---

## ⚙️ Code Generation

### Automated Cross-Repository Synchronization
When a new release tag or specification update is pushed to [`xyo-financial/specs`](https://github.com/xyo-financial/specs), an automated GitHub Actions workflow triggers a `repository_dispatch` event (`types: [spec_tagged, spec_updated]`) to this repository. The [`.github/workflows/generate.yml`](.github/workflows/generate.yml) workflow:

1. Checks out the `xyo-financial/specs` repository at the designated tag or ref (`${{ github.event.client_payload.tag || inputs.spec_tag || 'main' }}`).
2. Executes `openapi-generator-cli` with the `cpp-restsdk` generator target.
3. Automatically purges generator scaffolding and metadata noise (`openapi/git_push.sh`, `openapi/.travis.yml`, `openapi/README.md`, `openapi/test/`, `openapi/docs/`, `openapi/api/`, and `specs/`).
4. Configures and compiles the CMake build and executes `ctest` to ensure the generated code integrates cleanly with the wrapper layer.
5. Commits the updated `openapi/` directory back to the repository using `stefanzweifel/git-auto-commit-action`.

You can also trigger generation manually via GitHub Actions **Workflow Dispatch** with an optional `spec_tag` parameter.

### Manual / Local Code Generation
If you need to regenerate the low-level `openapi/` layer locally:

#### Prerequisites
- Node.js (v18+) with `npx`
- Java Runtime Environment (JRE 11+)
- Sibling clone of `xyo-financial/specs` or path to `openapi.yml`

#### Command
Run from the root of this repository (`sdks/cpp`):

```bash
npx -y @openapitools/openapi-generator-cli generate \
  -i ../specs/openapi.yml \
  -g cpp-restsdk \
  -o ./openapi \
  --additional-properties=packageName=XYOSDK,apiPackage=xyo_api,modelPackage=xyo_model
```

*Note: Replace `-i ../specs/openapi.yml` with the portable relative or absolute path to your local `openapi.yml` if located elsewhere.*

#### Post-Generation Clean-Up
After code generation completes, remove unnecessary generator noise and scaffolding files:

```bash
rm -f openapi/git_push.sh openapi/.travis.yml openapi/README.md
rm -rf openapi/test openapi/docs openapi/api specs
```

### Immutable Rule for Generated Code
- **Never manually edit or format code in `openapi/`.**
- Code formatters (`clang-format`) and linters (`clang-tidy`) are configured via `.clang-format-ignore` and `.clang-tidy` to strictly ignore `openapi/`.
- All custom behavior, helper methods, and domain abstractions belong in the wrapper layer (`include/xyo/` and `src/`).

---

## 🛠 Prerequisites & Development Environment

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

## 🛡 Build & Quality Gates

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

### 3. Code Formatting & Style (Excluding Generated Code)

This project strictly adheres to modern C++ Core Guidelines and LLVM/Google C++ formatting standards:
- 2 spaces indentation, no tabs.
- Explicit variable types for clarity unless using structured bindings.
- All non-throwing member functions marked `noexcept`.
- Const-correctness on all inspect-only methods and parameters.
- File-scoped helper functions enclosed in anonymous namespaces.

Format hand-crafted C++ source and header files using `clang-format` (the generated `openapi/` directory is ignored by `.clang-format-ignore`):

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

## 📦 Packaging Verification

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

## 🚀 Submitting a Pull Request

1. **Branch Naming**: Use descriptive branch names:
   - `feature/add-enrichment-retry-policy`
   - `fix/error-category-transport-timeout`
   - `docs/update-installation-guide`
2. **Commit Hygiene**:
   - Write clear, imperative commit messages adhering to [Conventional Commits](https://www.conventionalcommits.org/) (e.g. `feat: add client connection timeout configuration`).
   - Keep commits focused and atomic.
3. **PR Checklist**:
   - [ ] No manual edits or formatting applied to `openapi/` (use generator if spec changed).
   - [ ] Public API in `include/xyo/client.hpp` maintains PIMPL encapsulation (no leaking headers).
   - [ ] All CMake builds compile with zero warnings under `-Wall -Wextra -Wpedantic`.
   - [ ] CTest suite passes 100% cleanly.
   - [ ] Hand-crafted code formatted with `clang-format`.
   - [ ] Documentation (`README.md`, docstrings) updated where relevant.

---

## 📦 Release & Versioning Process

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

## 🔒 Security

If you discover a security vulnerability, please do NOT create a public issue. Follow the disclosure instructions outlined in [SECURITY.md](SECURITY.md) or email security@syniol.com directly.

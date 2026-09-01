<p align="center">
  <a href="https://xyo.financial" target="_blank" rel="noopener noreferrer">
    <img alt="XYO Financial C++ Mascot" width="380" src="https://raw.githubusercontent.com/xyo-financial/sdk-cpp/main/docs/mascot.png" />
  </a>
</p>

<h1 align="center">XYO Financial SDK for C++</h1>

<p align="center">
  <a href="https://github.com/xyo-financial/sdk-cpp/actions/workflows/ci.yml"><img src="https://github.com/xyo-financial/sdk-cpp/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI Build Pipeline" /></a>
  <a href="https://github.com/xyo-financial/sdk-cpp/actions/workflows/release.yml"><img src="https://github.com/xyo-financial/sdk-cpp/actions/workflows/release.yml/badge.svg" alt="Release Pipeline" /></a>
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg?logo=c%2B%2B" alt="C++ Standard" /></a>
  <a href="https://en.cppreference.com/w/cpp/language/pimpl"><img src="https://img.shields.io/badge/Architecture-PIMPL%20Wrapper-blueviolet" alt="Architecture" /></a>
  <a href="https://docs.libcpr.org/"><img src="https://img.shields.io/badge/Transport-CPR%20%2F%20libcurl-informational" alt="Transport" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache_2.0-green.svg" alt="License" /></a>
</p>

<p align="center">
  <strong>The official C++ SDK for <a href="https://xyo.financial">XYO Financial</a>.</strong><br>
  Seamlessly enrich raw financial transactions into clean merchant profiles, intelligent business categorizations, high-res logos, and geolocated address metadata using AI-powered enrichment pipelines.
</p>

---

## 📖 Summary

The **XYO Financial SDK for C++** provides an institutional-grade, modern C++17 client library for integrating XYO's AI-driven transaction enrichment engine into high-performance financial systems, payment gateways, low-latency banking microservices, and core banking ledgers.

Engineered for Tier-1 banks, payment service providers (PSPs), quantitative hedge funds, and fintech platforms, this SDK transforms raw, cryptic merchant statement strings (e.g. `AMZN MKTP UK*1M23456`, `SQ *COSTA GREENWICH SE10`, `UBER *TRIP HELP.UBER.COM`) into verified, structured merchant intelligence complete with official merchant identities, industry classification taxonomies, brand logos, geocoded coordinates, and physical street addresses.

Maintained by [Syniol Limited](https://syniol.com) as the official C++ distribution for [XYO.Financial](https://xyo.financial).

---

## 🏗 Architectural Principles

1. **Modern PIMPL Architecture**:
   - **Public Header (`xyo/client.hpp`)**: Clean, idiomatic public API exposing strictly standard C++17 types (`std::string`, `std::optional`, `std::vector`, `std::unique_ptr`). 
   - **Zero Leaky Abstractions**: Third-party headers (`<cpr/*>`, `<nlohmann/*>`, `<zlib.h>`) are completely encapsulated in `src/client.cpp`. Consuming applications never pull third-party headers into their translation units.
2. **Thread-Safe & Re-Entrant**: `xyo::Client` instances are thread-safe and can be safely shared across concurrent worker pools, async task executors, and multithreaded backend pipelines.
3. **Memory Safety & Move Semantics**: Fully RAII-compliant with move-only configuration objects and zero manual pointer management, eliminating memory leaks and phantom key allocations.
4. **In-Memory Archive Streaming**: Direct in-memory `.tar.gz` decompression (via `zlib`) and custom zero-copy POSIX ustar/GNU tar archive parsing for instantaneous bulk result ingestion without disk I/O.
5. **Structured Error Hierarchy**: Tagged error categories (`validation`, `transport`, `http`, `parsing`) paired with numeric HTTP status codes and transport error diagnostics.

---

## ⚙️ System Requirements

- **C++ Standard**: C++17 or newer.
- **Compilers**:
  - GCC 9.0+ (Linux / WSL)
  - Clang 10.0+ / Apple Clang 12.0+ (macOS / Linux)
  - Microsoft Visual Studio (MSVC) 2019+ / 2022+ (Windows)
- **Build System**: CMake 3.16 or newer.
- **Dependencies**:
  - `cpr` (1.10+)
  - `nlohmann_json` (3.11+)
  - `OpenSSL` (1.1+ or 3.0+)
  - `ZLIB` (1.2.8+)
- **Network**: Outbound HTTPS connectivity to `api.xyo.financial` over port `443` (TLS 1.2+ mandatory).
- **Authentication**: A valid API Bearer token obtained from the [XYO Financial Dashboard](https://xyo.financial/dashboard).

---

## 📦 Installation & Integration

### Option 1: Conan 2 (Recommended)

Add `xyo-sdk` to your `conanfile.txt`:

```ini
[requires]
xyo-sdk/2.2.0

[generators]
CMakeDeps
CMakeToolchain

[layout]
cmake_layout
```

Install and build your project:

```sh
conan install . --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
```

---

### Option 2: vcpkg

Install the package via vcpkg:

```sh
vcpkg install xyo-sdk
```

Configure your CMake project with the vcpkg toolchain:

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

---

### Option 3: CMake `find_package` (Installed Binaries)

When linking against an installed copy of the SDK:

```cmake
cmake_minimum_required(VERSION 3.16)
project(FinancialApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Discover SDK target
find_package(XYOSDK CONFIG REQUIRED)

add_executable(financial_app main.cpp)
target_link_libraries(financial_app PRIVATE XYO::SDK)
```

---

### Option 4: CMake `FetchContent` (Direct from Source)

Integrate the SDK directly into your CMake project without pre-installing dependencies:

```cmake
include(FetchContent)

FetchContent_Declare(
  xyo_sdk
  GIT_REPOSITORY https://github.com/xyo-financial/sdk-cpp.git
  GIT_TAG        v2.2.0
)
FetchContent_MakeAvailable(xyo_sdk)

add_executable(financial_app main.cpp)
target_link_libraries(financial_app PRIVATE XYO::SDK)
```

---

## 🚀 Quickstart Guide

```cpp
#include <xyo/client.hpp>
#include <iostream>

int main() {
  try {
    // 1. Initialize client configuration
    xyo::ClientConfig config("YOUR_XYO_API_KEY");
    config.request_timeout_ms = 10000; // 10s request timeout

    xyo::Client client(std::move(config));

    // 2. Enrich a single transaction
    xyo::EnrichmentRequest request{
        /* content      = */ "COSTA PICKUP GREENWICH",
        /* country_code = */ "GB"
    };

    xyo::EnrichmentResponse response = client.enrichTransaction(request);

    std::cout << "--- Transaction Enrichment Result ---\n";
    std::cout << "Merchant:    " << response.merchant << "\n";
    std::cout << "Description: " << response.description << "\n";
    std::cout << "Categories:  ";
    for (const auto& cat : response.categories) {
      std::cout << "[" << cat << "] ";
    }
    std::cout << "\n";

    if (response.location) {
      std::cout << "Location:    " << *response.location << "\n";
    }
    if (response.address) {
      std::cout << "Address:     " << *response.address << "\n";
    }

  } catch (const xyo::Error& e) {
    std::cerr << "XYO Error (" << static_cast<int>(e.category()) 
              << "): " << e.what() 
              << " [HTTP " << e.http_status_code() << "]\n";
    return 1;
  }

  return 0;
}
```

---

## 📚 Comprehensive Usage Workflows

### 1. Single Transaction Enrichment (`enrichTransaction`)

Synchronously enrich individual transactions in real-time. Ideal for payment authorization hooks, mobile banking applications, and personal finance ledgers:

```cpp
#include <xyo/client.hpp>
#include <iostream>

void enrichSingle(const xyo::Client& client) {
  xyo::EnrichmentRequest req{
      "AMZN MKTP US*2819873", // Raw bank statement narrative (max 128 chars)
      "US"                    // ISO 3166-1 alpha-2 country code
  };

  xyo::EnrichmentResponse res = client.enrichTransaction(req);

  std::cout << "Merchant:    " << res.merchant << "\n";
  std::cout << "Description: " << res.description << "\n";
  std::cout << "Logo (B64):  " << (res.logo.empty() ? "N/A" : "Available") << "\n";

  if (res.location.has_value()) {
    std::cout << "Location:    " << res.location.value() << "\n";
  }
  if (res.address.has_value()) {
    std::cout << "Address:     " << res.address.value() << "\n";
  }
}
```

#### Response Model Fields:

| Field | Type | Description |
| :--- | :--- | :--- |
| `merchant` | `std::string` | Official, normalized merchant brand name (e.g. `"Amazon"`). |
| `description` | `std::string` | Detailed business description and merchant operational profile. |
| `categories` | `std::vector<std::string>` | Industry classification taxonomy categories. |
| `logo` | `std::string` | Base64-encoded brand logo (PNG/JPEG format). |
| `location` | `std::optional<std::string>` | Geocoded city or regional market (if detected). |
| `address` | `std::optional<std::string>` | Verified street address of the transaction point (if detected). |

---

### 2. Bulk Transaction Submission (`enrichTransactions`)

Submit high-volume transaction batches asynchronously for ETL processing, nightly batch reconciliation, and core banking statement generation:

```cpp
#include <xyo/client.hpp>
#include <iostream>
#include <vector>

xyo::BulkEnrichmentResponse submitBatch(const xyo::Client& client) {
  std::vector<xyo::EnrichmentRequest> batch = {
      {"UBER *TRIP HELP.UBER.COM", "US"},
      {"NETFLIX.COM PAYMENT", "GB"},
      {"AMZN MKTP US*2819873", "US"},
      {"TESCO STORES 2891", "GB"}
  };

  // Submit batch job (returns job identifier and download URL)
  xyo::BulkEnrichmentResponse bulk = client.enrichTransactions(batch);

  std::cout << "Bulk Job ID:     " << bulk.id << "\n";
  std::cout << "Download Link:   " << bulk.link << "\n";

  return bulk;
}
```

---

### 3. Bulk Job Status Polling (`getEnrichmentStatus`)

Poll the processing status of an asynchronous bulk enrichment job until the results archive is ready for ingestion:

```cpp
#include <xyo/client.hpp>
#include <chrono>
#include <iostream>
#include <thread>

void pollJob(const xyo::Client& client, const std::string& jobId) {
  std::cout << "Polling status for job: " << jobId << "...\n";

  while (true) {
    xyo::EnrichmentStatus status = client.getEnrichmentStatus(jobId);
    std::cout << "Status: " << xyo::to_string(status) << "\n";

    if (status == xyo::EnrichmentStatus::ready) {
      std::cout << "Job is READY! Proceeding to download archive.\n";
      break;
    } else if (status == xyo::EnrichmentStatus::failed) {
      std::cerr << "Job FAILED during processing on server.\n";
      break;
    }

    // Wait before next polling iteration
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}
```

---

### 4. Direct Bulk Results Download & Decompression (`downloadEnrichmentCollection`)

Once a bulk enrichment job reaches `ready` status, download and unpack the compressed archive directly into a vector of domain models without touching disk:

```cpp
#include <xyo/client.hpp>
#include <iostream>
#include <vector>

void processBulkResults(const xyo::Client& client, const std::string& downloadUrl) {
  // downloadUrl is the `link` URL from BulkEnrichmentResponse
  std::vector<xyo::EnrichmentResponse> records =
      client.downloadEnrichmentCollection(downloadUrl);

  std::cout << "Successfully downloaded and unpacked " << records.size() << " enriched transactions:\n";

  for (const auto& record : records) {
    std::cout << "----------------------------------------\n";
    std::cout << "Merchant:    " << record.merchant << "\n";
    std::cout << "Description: " << record.description << "\n";
    std::cout << "Categories:  ";
    for (const auto& cat : record.categories) {
      std::cout << "[" << cat << "] ";
    }
    std::cout << "\n";
  }
}
```

---

## 🚀 Framework & Architecture Integration

### 1. Modern C++17 Microservice Integration (Crow / Drogon)

Integrate `xyo::Client` into modern, asynchronous C++ web microservices to enrich incoming payment streams at high concurrency. The SDK is thread-safe and re-entrant, allowing a single client instance to be shared across worker thread pools.

#### Crow Microservice Recipe

```cpp
#include <xyo/client.hpp>
#include <crow.h>
#include <cstdlib>

int main() {
    crow::SimpleApp app;

    // 1. Initialize thread-safe XYO client with strict ledger SLA budget
    const char* apiKey = std::getenv("XYO_API_KEY");
    xyo::ClientConfig config(apiKey ? apiKey : "YOUR_API_KEY");
    config.request_timeout_ms = 500; // Strict 500ms ledger budget
    xyo::Client xyoClient(std::move(config));

    // 2. High-performance REST enrichment endpoint
    CROW_ROUTE(app, "/api/enrich").methods("POST"_method)
    ([&xyoClient](const crow::request& req) {
        auto payload = crow::json::load(req.body);
        if (!payload) {
            return crow::response(400, "{\"error\": \"Malformed JSON payload\"}");
        }

        try {
            xyo::EnrichmentRequest request{
                payload["content"].s(),
                payload["countryCode"].s()
            };
            auto response = xyoClient.enrichTransaction(request);

            crow::json::wvalue res;
            res["merchant"]    = response.merchant;
            res["description"] = response.description;
            res["categories"]  = response.categories;
            if (response.location.has_value()) res["location"] = response.location.value();
            if (response.address.has_value())  res["address"]  = response.address.value();

            return crow::response{200, res};
        } catch (const xyo::Error& err) {
            crow::json::wvalue errRes;
            errRes["error"] = err.what();
            errRes["category"] = static_cast<int>(err.category());
            return crow::response{
                err.http_status_code() ? static_cast<int>(err.http_status_code()) : 500,
                errRes
            };
        }
    });

    // Run multithreaded server with worker threads
    app.port(8080).multithreaded().run();
}
```

#### Drogon Asynchronous Controller Recipe

```cpp
#include <xyo/client.hpp>
#include <drogon/drogon.h>
#include <cstdlib>
#include <memory>

class EnrichmentController : public drogon::HttpController<EnrichmentController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(EnrichmentController::enrich, "/api/enrich", drogon::Post);
    METHOD_LIST_END

    EnrichmentController() {
        const char* apiKey = std::getenv("XYO_API_KEY");
        xyo::ClientConfig config(apiKey ? apiKey : "YOUR_API_KEY");
        config.request_timeout_ms = 500; // Strict 500ms ledger budget
        client_ = std::make_unique<xyo::Client>(std::move(config));
    }

    void enrich(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto json = req->getJsonObject();
        if (!json) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("{\"error\": \"Missing or invalid JSON payload\"}");
            callback(resp);
            return;
        }

        try {
            xyo::EnrichmentRequest request{
                (*json)["content"].asString(),
                (*json)["countryCode"].asString()
            };
            auto response = client_->enrichTransaction(request);

            Json::Value res;
            res["merchant"]    = response.merchant;
            res["description"] = response.description;
            for (const auto& cat : response.categories) {
                res["categories"].append(cat);
            }
            if (response.location) res["location"] = *response.location;
            if (response.address)  res["address"]  = *response.address;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(res);
            callback(resp);
        } catch (const xyo::Error& err) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(err.http_status_code() 
                ? static_cast<drogon::HttpStatusCode>(err.http_status_code()) 
                : drogon::k500InternalServerError);
            resp->setBody(err.what());
            callback(resp);
        }
    }

private:
    std::unique_ptr<xyo::Client> client_;
};
```

---

### 2. High-Frequency Trading & Low-Latency Banking Ledgers

For institutional core ledgers, settlement engines, and high-frequency transaction processing pipelines, C++ provides deterministic execution guarantees that are impossible with managed runtimes:

- **Zero Garbage Collection Pause Jitter**: Managed runtimes (Java, Node.js, Go, .NET) experience unpredictable stop-the-world GC pauses that degrade tail latency (p99/p99.9). The C++ SDK utilizes deterministic RAII memory management, achieving **zero Garbage Collection pause jitter (deterministic sub-millisecond p99 execution)** for critical financial rails and real-time fraud mitigation gates.
- **Thread-Safe Concurrency & Zero-Copy In-Memory Decompression**: Public client methods are re-entrant and thread-safe without requiring application-level locking. Internal connection pooling maintains isolated sessions per worker thread. Bulk transaction collections are decompressed directly in memory through zero-copy streaming, bypassing disk I/O bottlenecks.

---

## 🛡 Structured Error Handling (`xyo::Error`)

All SDK exceptions inherit from `std::runtime_error` and provide strongly typed error categorization and diagnostic codes:

```cpp
#include <xyo/client.hpp>
#include <iostream>

void safeExecute(const xyo::Client& client) {
  try {
    xyo::EnrichmentRequest req{"", "GB"}; // Empty string triggers validation error
    auto res = client.enrichTransaction(req);
  } catch (const xyo::Error& err) {
    std::cerr << "[Error Caught] " << err.what() << "\n";

    switch (err.category()) {
      case xyo::ErrorCategory::validation:
        std::cerr << "Classification: Client validation error (e.g. empty API key or payload).\n";
        break;

      case xyo::ErrorCategory::transport:
        std::cerr << "Classification: Network/transport layer failure (code: " 
                  << err.transport_code() << ").\n";
        break;

      case xyo::ErrorCategory::http:
        std::cerr << "Classification: Server HTTP error (Status: " 
                  << err.http_status_code() << ").\n";
        if (err.http_status_code() == 401) {
          std::cerr << "Action: Verify your API key at https://xyo.financial/dashboard.\n";
        } else if (err.http_status_code() == 429) {
          std::cerr << "Action: Rate limit reached. Backoff and retry.\n";
        }
        break;

      case xyo::ErrorCategory::parsing:
        std::cerr << "Classification: Response parsing / deserialization failure.\n";
        break;
    }
  }
}
```

### HTTP Status Code Reference

| HTTP Code | Classification | Root Cause & Mitigation |
| :--- | :--- | :--- |
| `400` | Bad Request | Malformed JSON payload or invalid ISO 3166-1 country code. |
| `401` | Unauthorized | Bearer token is missing, expired, or invalid. |
| `403` | Forbidden | Insufficient plan permissions or account quota exhausted. |
| `404` | Not Found | Bulk work-ID not located or expired. |
| `422` | Unprocessable Entity | Content string cannot be parsed into a recognizable merchant format. |
| `429` | Rate Limited | API request volume quota exceeded. Apply backoff and retry. |
| `500` / `502` / `503` | Server Error | Upstream infrastructure degradation. Reroute to secondary queue. |

---

## ⚙️ Advanced Configuration Reference

The `xyo::ClientConfig` struct governs connection, authentication, and timeout parameters:

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `api_key` | `std::string` | *(Required)* | Secret API Bearer token from the XYO dashboard |
| `base_url` | `std::string` | `"https://api.xyo.financial"` | API gateway base endpoint URL (must use HTTPS unless `allow_insecure_transport` is true) |
| `allow_insecure_transport` | `bool` | `false` | Explicit opt-in flag to permit plaintext HTTP endpoints for local test servers |
| `connect_timeout_ms` | `long` | `5000` (5s) | Timeout for establishing the connection alone, applied to every request |
| `request_timeout_ms` | `long` | `30000` (30s) | End-to-end HTTP request timeout, connection included |
| `max_collection_size` | `std::size_t` | `50000` | Maximum item limit per batch request (1 to 50,000 items) |
| `allowed_download_domains` | `std::vector<std::string>` | `{".amazonaws.com"}` | Allowlist of trusted storage domains for bulk archive downloads |

---

## 🔒 Security & Compliance

- **Data Minimisation**: Transmit only transaction string narratives and ISO country codes. Never transmit Primary Account Numbers (PANs), CVVs, passwords, or Personally Identifiable Information (PII).
- **Transport Security**: Outbound communication strictly enforces TLS 1.2+ HTTPS encryption.
- **Memory Hardening**: `ClientConfig` securely manages API key memory lifecycles using RAII move semantics.

---

## 🧪 Build from Source & Run Tests

```sh
# 1. Configure build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXYO_BUILD_TESTS=ON

# 2. Compile SDK library and test suite
cmake --build build --parallel

# 3. Execute unit and integration tests
ctest --test-dir build --output-on-failure
```

---

## 📞 Support

- **Developer Dashboard**: [https://xyo.financial/dashboard](https://xyo.financial/dashboard)
- **Technical Support**: [support@syniol.com](mailto:support@syniol.com)
- **Maintainer**: [Syniol Limited](https://syniol.com)

---

## 📄 License

This project is licensed under the **Apache License, Version 2.0** - see the [LICENSE](LICENSE) file for details.

Copyright &copy; 2025–2026 Syniol Limited. All rights reserved.
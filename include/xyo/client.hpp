// Copyright 2026 Syniol Limited
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ---------------------------------------------------------------------------
// XYO C++ SDK: hand-written C++17 client for the XYO Financial enrichment API.
// HTTP and serialisation are handled in src/client.cpp via cpr (libcurl) and
// nlohmann::json; those headers stay behind the PIMPL and never reach consumers.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef XYO_SDK_EXPORTS
    #define XYO_SDK_API __declspec(dllexport)
  #elif defined(XYO_SDK_SHARED)
    #define XYO_SDK_API __declspec(dllimport)
  #else
    #define XYO_SDK_API
  #endif
#else
  #if __GNUC__ >= 4
    #define XYO_SDK_API __attribute__((visibility("default")))
  #else
    #define XYO_SDK_API
  #endif
#endif

namespace xyo {

// ---------------------------------------------------------------------------
// Options & Domain types – purposely simple, independent of internals.
// ---------------------------------------------------------------------------

struct XYO_SDK_API EnrichmentRequestOptions {
  std::optional<std::string> x_correlation_id;
  std::optional<std::string> traceparent;
  std::optional<std::string> x_api_user;
};

struct XYO_SDK_API EnrichmentRequest {
  std::string content;      ///< Payment description, max 128 chars.
  std::string country_code; ///< ISO 3166-1 alpha-2 (e.g. "GB").
};

struct XYO_SDK_API EnrichmentResponse {
  std::string merchant;
  std::string description;
  std::vector<std::string> categories;
  std::string logo;                    ///< Base64-encoded PNG/JPEG.
  std::optional<std::string> location; ///< May be absent if API returns null.
  std::optional<std::string> address;  ///< May be absent if API returns null.
};

struct XYO_SDK_API BulkEnrichmentResponse {
  std::string id;   ///< Work-ID for the enrichment job.
  std::string link; ///< URL to downloadable tar.gz results archive.
};

enum class XYO_SDK_API EnrichmentStatus { ready, failed, pending };

[[nodiscard]] XYO_SDK_API std::string to_string(EnrichmentStatus status);

// ---------------------------------------------------------------------------
// Client configuration
// ---------------------------------------------------------------------------

struct XYO_SDK_API ClientConfig {
  std::string api_key;
  std::string base_url = "https://api.xyo.financial";

  // Optional timeout overrides (milliseconds). Both are applied to every request.
  long connect_timeout_ms  = 5'000;  ///< Cap on establishing the connection alone.
  long request_timeout_ms  = 30'000; ///< Cap on the whole operation, connection included.
  std::size_t max_collection_size = 1'000;

  ClientConfig();
  explicit ClientConfig(std::string key,
                        std::string url = "");

  ClientConfig(const ClientConfig&) = delete;
  ClientConfig& operator=(const ClientConfig&) = delete;
  ClientConfig(ClientConfig&&) noexcept;
  ClientConfig& operator=(ClientConfig&&) noexcept;
  ~ClientConfig() noexcept;
};

// ---------------------------------------------------------------------------
// SDK error type & Rate Limit Info
// ---------------------------------------------------------------------------

struct XYO_SDK_API RateLimitInfo {
  std::optional<int64_t> limit;
  std::optional<int64_t> remaining;
  std::optional<int64_t> reset;       ///< Epoch timestamp or seconds until reset
  std::optional<int64_t> retry_after; ///< Seconds until retry allowed
};

enum class XYO_SDK_API ErrorCategory { validation, transport, http, parsing, rate_limit };

[[nodiscard]] XYO_SDK_API std::string to_string(ErrorCategory category);

class XYO_SDK_API Error : public std::runtime_error {
 public:
  Error(ErrorCategory category, const std::string& message,
        long http_status_code = 0, int transport_code = 0,
        std::optional<RateLimitInfo> rate_limit_info = std::nullopt);

  ErrorCategory category()         const noexcept { return category_; }
  long          http_status_code() const noexcept { return http_status_code_; }
  int           transport_code()   const noexcept { return transport_code_; }
  const std::optional<RateLimitInfo>& rate_limit_info() const noexcept { return rate_limit_info_; }

 private:
  ErrorCategory category_         = ErrorCategory::validation;
  long          http_status_code_ = 0;
  int           transport_code_   = 0;
  std::optional<RateLimitInfo> rate_limit_info_;
};

using XyoException = Error;

// ---------------------------------------------------------------------------
// Client – primary entry point.
//
// Thread-safety: safe to call concurrently.
// ---------------------------------------------------------------------------

class XYO_SDK_API Client {
 public:
  explicit Client(ClientConfig config);

  Client(const Client&)            = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) noexcept;
  Client& operator=(Client&&) noexcept;
  ~Client() noexcept;

  /// Enrich a single financial transaction (synchronous).
  [[nodiscard]] EnrichmentResponse enrichTransaction(
      const EnrichmentRequest& request,
      const EnrichmentRequestOptions& options = {}) const;

  /// Enrich a batch of transactions asynchronously; returns a job handle.
  [[nodiscard]] BulkEnrichmentResponse enrichTransactions(
      const std::vector<EnrichmentRequest>& requests,
      const EnrichmentRequestOptions& options = {}) const;

  /// Poll the status of an async bulk enrichment job.
  [[nodiscard]] EnrichmentStatus getEnrichmentStatus(
      const std::string& id,
      const EnrichmentRequestOptions& options = {}) const;

  /// Download and decode a completed enrichment collection archive.
  [[nodiscard]] std::vector<EnrichmentResponse> downloadEnrichmentCollection(
      const std::string& downloadUrl,
      const EnrichmentRequestOptions& options = {}) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xyo

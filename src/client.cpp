// Copyright 2026 Syniol Limited
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// XYO C++ SDK – Modern C++17 client powered by CPR (libcurl) and nlohmann::json.
// ---------------------------------------------------------------------------

#include "xyo/client.hpp"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xyo {

// ---------------------------------------------------------------------------
// Helpers & Sanitizers
// ---------------------------------------------------------------------------

namespace {

constexpr char ascii_lower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char ascii_upper(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

inline std::string to_ascii_lower(std::string_view sv) {
  std::string res;
  res.reserve(sv.size());
  for (char c : sv) res.push_back(ascii_lower(c));
  return res;
}

void secure_erase(std::string& str) noexcept {
  if (!str.empty()) {
    OPENSSL_cleanse(str.data(), str.size());
    str.clear();
  }
}

inline bool is_valid_header_value(const std::string& val) {
  for (unsigned char c : val) {
    if (c != 0x09 && (c < 32 || c > 126)) {
      return false;
    }
  }
  return true;
}

inline std::size_t utf8_length(std::string_view s) {
  std::size_t len = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c & 0xC0) != 0x80) {
      ++len;
    }
  }
  return len;
}

inline void validate_and_normalize_request(EnrichmentRequest& req, const char* op_name) {
  if (req.content.empty()) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request content must not be empty");
  }
  if (utf8_length(req.content) > 128) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request content exceeds maximum length of 128 characters");
  }
  if (req.country_code.empty()) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request country_code must not be empty");
  }
  if (req.country_code.size() != 2 ||
      !((req.country_code[0] >= 'a' && req.country_code[0] <= 'z') || (req.country_code[0] >= 'A' && req.country_code[0] <= 'Z')) ||
      !((req.country_code[1] >= 'a' && req.country_code[1] <= 'z') || (req.country_code[1] >= 'A' && req.country_code[1] <= 'Z'))) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request country_code must be a 2-letter ISO 3166-1 alpha-2 code");
  }
  req.country_code[0] = ascii_upper(req.country_code[0]);
  req.country_code[1] = ascii_upper(req.country_code[1]);
}

inline void validate_batch_size(std::size_t size, std::size_t max_collection_size) {
  if (size == 0) {
    throw Error(ErrorCategory::validation,
                "enrichTransactions: request list must not be empty (must contain between 1 and 50000 items)");
  }
  if (size > 50'000) {
    throw Error(ErrorCategory::validation,
                "enrichTransactions: batch size " + std::to_string(size) +
                " exceeds maximum limit of 50000 items");
  }
  if (size > max_collection_size) {
    throw Error(ErrorCategory::validation,
                "enrichTransactions: batch size " + std::to_string(size) +
                " exceeds configured max_collection_size of " +
                std::to_string(max_collection_size));
  }
}

inline void validate_job_id(const std::string& id) {
  if (id.empty() || id.size() > 128) {
    throw Error(ErrorCategory::validation,
                "getEnrichmentStatus: id must be 1-128 characters");
  }
  const bool ok = std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
  if (!ok) {
    throw Error(ErrorCategory::validation,
                "getEnrichmentStatus: id contains characters that are not permitted in a job identifier");
  }
}

inline std::optional<RateLimitInfo> parse_rate_limit_info(const cpr::Header& headers) {
  RateLimitInfo info;
  bool found = false;

  auto find_val = [&](const std::initializer_list<const char*>& keys) -> std::optional<std::string> {
    for (const char* key : keys) {
      auto it = headers.find(key);
      if (it != headers.end()) {
        return it->second;
      }
    }
    return std::nullopt;
  };

  if (auto val = find_val({"retry-after"})) {
    try {
      info.retry_after = std::stoll(*val);
      found = true;
    } catch (const std::invalid_argument&) {
      // Parse RFC 7231 / RFC 9110 HTTP-date string (e.g. "Wed, 21 Oct 2015 07:28:00 GMT")
      std::tm tm_buf{};
      std::istringstream ss(*val);
      ss >> std::get_time(&tm_buf, "%a, %d %b %Y %H:%M:%S GMT");
      if (!ss.fail()) {
#ifdef _WIN32
        auto target_time = _mkgmtime(&tm_buf);
#else
        auto target_time = timegm(&tm_buf);
#endif
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (target_time > now) {
          info.retry_after = static_cast<int64_t>(target_time - now);
        } else {
          info.retry_after = 0;
        }
        found = true;
      }
    } catch (const std::out_of_range&) {
    }
  }
  if (auto val = find_val({"ratelimit-limit", "x-ratelimit-limit"})) {
    try {
      info.limit = std::stoll(*val);
      found = true;
    } catch (const std::invalid_argument&) {
    } catch (const std::out_of_range&) {
    }
  }
  if (auto val = find_val({"ratelimit-remaining", "x-ratelimit-remaining"})) {
    try {
      info.remaining = std::stoll(*val);
      found = true;
    } catch (const std::invalid_argument&) {
    } catch (const std::out_of_range&) {
    }
  }
  if (auto val = find_val({"ratelimit-reset", "x-ratelimit-reset"})) {
    try {
      info.reset = std::stoll(*val);
      found = true;
    } catch (const std::invalid_argument&) {
    } catch (const std::out_of_range&) {
    }
  }

  if (found) return info;
  return std::nullopt;
}

inline cpr::Header build_headers(const std::string& api_key,
                                 const std::string& content_type,
                                 const std::string& accept,
                                 const EnrichmentRequestOptions& options) {
  cpr::Header headers;
  if (!api_key.empty()) {
    headers.insert({"Authorization", "Bearer " + api_key});
  }
  if (!content_type.empty()) {
    headers.insert({"Content-Type", content_type});
  }
  if (!accept.empty()) {
    headers.insert({"Accept", accept});
  }
  if (options.x_correlation_id.has_value() && !options.x_correlation_id->empty()) {
    if (!is_valid_header_value(options.x_correlation_id.value())) {
      throw Error(ErrorCategory::validation, "x_correlation_id contains invalid header characters");
    }
    headers.insert({"x-correlation-id", options.x_correlation_id.value()});
  }
  if (options.traceparent.has_value() && !options.traceparent->empty()) {
    if (!is_valid_header_value(options.traceparent.value())) {
      throw Error(ErrorCategory::validation, "traceparent contains invalid header characters");
    }
    headers.insert({"traceparent", options.traceparent.value()});
  }
  if (options.x_api_user.has_value() && !options.x_api_user->empty()) {
    if (!is_valid_header_value(options.x_api_user.value())) {
      throw Error(ErrorCategory::validation, "x_api_user contains invalid header characters");
    }
    headers.insert({"x-api-user", options.x_api_user.value()});
  }
  return headers;
}

inline void check_and_throw_http_error(const cpr::Response& res, const char* op_name) {
  std::optional<std::string> correlation_id;
  auto corr_it = res.header.find("x-correlation-id");
  if (corr_it != res.header.end()) {
    correlation_id = corr_it->second;
  }

  if (res.status_code == 200) {
    return;
  }

  std::string prob_type;
  std::string prob_title;
  std::string prob_detail;
  std::string prob_instance;

  std::string body_text = res.text;
  if (!body_text.empty()) {
    try {
      auto j = nlohmann::json::parse(body_text);
      if (j.contains("errors") && j["errors"].is_array() && !j["errors"].empty()) {
        const auto& err_obj = j["errors"][0];
        if (err_obj.contains("type") && err_obj["type"].is_string()) prob_type = err_obj["type"].get<std::string>();
        if (err_obj.contains("title") && err_obj["title"].is_string()) prob_title = err_obj["title"].get<std::string>();
        if (err_obj.contains("detail") && err_obj["detail"].is_string()) prob_detail = err_obj["detail"].get<std::string>();
        if (err_obj.contains("instance") && err_obj["instance"].is_string()) prob_instance = err_obj["instance"].get<std::string>();
      } else {
        if (j.contains("type") && j["type"].is_string()) prob_type = j["type"].get<std::string>();
        if (j.contains("title") && j["title"].is_string()) prob_title = j["title"].get<std::string>();
        if (j.contains("detail") && j["detail"].is_string()) prob_detail = j["detail"].get<std::string>();
        if (j.contains("instance") && j["instance"].is_string()) prob_instance = j["instance"].get<std::string>();
      }
    } catch (...) {
    }
  }

  std::string error_msg = "HTTP error from " + std::string(op_name) + ": HTTP " + std::to_string(res.status_code);
  if (!prob_title.empty()) {
    error_msg += " " + prob_title;
    if (!prob_type.empty()) {
      error_msg += " (" + prob_type + ")";
    }
  }

  auto rli = parse_rate_limit_info(res.header);
  if (res.status_code == 429) {
    throw Error(ErrorCategory::rate_limit, error_msg, res.status_code, 0, rli,
                correlation_id, prob_type, prob_title, prob_detail, prob_instance);
  }
  if (res.status_code >= 400) {
    throw Error(ErrorCategory::http, error_msg, res.status_code, 0, rli,
                correlation_id, prob_type, prob_title, prob_detail, prob_instance);
  }
  throw Error(ErrorCategory::http,
              "Unexpected HTTP status from " + std::string(op_name) + ": HTTP " +
                  std::to_string(res.status_code) + " (expected 200)",
              res.status_code, 0, rli, correlation_id, prob_type, prob_title, prob_detail, prob_instance);
}

// Simple URL parser helper
struct ParsedUrl {
  std::string scheme;
  std::string host;
  int port = 0;
  std::string path;
};

ParsedUrl parse_url(const std::string& url_str) {
  ParsedUrl res;
  if (url_str.find('@') != std::string::npos || url_str.find(' ') != std::string::npos) {
    throw Error(ErrorCategory::validation, "invalid URL format");
  }
  std::size_t scheme_end = url_str.find("://");
  if (scheme_end == std::string::npos) {
    throw Error(ErrorCategory::validation, "invalid URL format: missing scheme");
  }
  res.scheme = to_ascii_lower(url_str.substr(0, scheme_end));

  std::size_t host_start = scheme_end + 3;
  std::size_t path_start = url_str.find('/', host_start);
  std::string host_port;
  if (path_start == std::string::npos) {
    host_port = url_str.substr(host_start);
    res.path = "/";
  } else {
    host_port = url_str.substr(host_start, path_start - host_start);
    res.path = url_str.substr(path_start);
  }

  std::size_t port_pos = host_port.find(':');
  if (port_pos != std::string::npos) {
    res.host = to_ascii_lower(host_port.substr(0, port_pos));
    std::string port_str = host_port.substr(port_pos + 1);
    if (port_str.empty() || !std::all_of(port_str.begin(), port_str.end(), [](unsigned char c) { return std::isdigit(c); })) {
      throw Error(ErrorCategory::validation, "invalid URL format: bad port number");
    }
    try {
      int p = std::stoi(port_str);
      if (p < 1 || p > 65535) {
        throw Error(ErrorCategory::validation, "invalid URL format: bad port number");
      }
      res.port = p;
    } catch (const Error&) {
      throw;
    } catch (...) {
      throw Error(ErrorCategory::validation, "invalid URL format: bad port number");
    }
  } else {
    res.host = to_ascii_lower(host_port);
    if (res.scheme == "https") res.port = 443;
    else if (res.scheme == "http") res.port = 80;
  }

  if (res.host.empty()) {
    throw Error(ErrorCategory::validation, "invalid URL format: missing host");
  }
  return res;
}

EnrichmentResponse parse_enrichment_response(const nlohmann::json& json_data) {
  if (!json_data.is_object()) {
    throw Error(ErrorCategory::parsing,
                "parse_enrichment_response: expected JSON object for enrichment response");
  }
  EnrichmentResponse out;
  if (auto it = json_data.find("merchant"); it != json_data.end() && it->is_string()) {
    out.merchant = it->get<std::string>();
  } else {
    out.merchant = "";
  }
  if (auto it = json_data.find("description"); it != json_data.end() && it->is_string()) {
    out.description = it->get<std::string>();
  } else {
    out.description = "";
  }
  if (auto it = json_data.find("logo"); it != json_data.end() && it->is_string()) {
    out.logo = it->get<std::string>();
  } else {
    out.logo = "";
  }

  if (auto it = json_data.find("categories"); it != json_data.end() && it->is_array()) {
    for (const auto& cat : *it) {
      if (cat.is_string()) {
        out.categories.push_back(cat.get<std::string>());
      }
    }
  }

  if (auto it = json_data.find("location"); it != json_data.end() && !it->is_null() && it->is_string()) {
    out.location = it->get<std::string>();
  }
  if (auto it = json_data.find("address"); it != json_data.end() && !it->is_null() && it->is_string()) {
    out.address = it->get<std::string>();
  }

  return out;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public free functions
// ---------------------------------------------------------------------------

std::string to_string(EnrichmentStatus status) {
  switch (status) {
    case EnrichmentStatus::ready:   return "READY";
    case EnrichmentStatus::failed:  return "FAILED";
    case EnrichmentStatus::pending: return "PENDING";
    default:                        return "UNKNOWN";
  }
}

std::string to_string(ErrorCategory category) {
  switch (category) {
    case ErrorCategory::validation: return "validation";
    case ErrorCategory::transport:  return "transport";
    case ErrorCategory::http:       return "http";
    case ErrorCategory::parsing:    return "parsing";
    case ErrorCategory::rate_limit: return "rate_limit";
    default:                        return "unknown";
  }
}

// ---------------------------------------------------------------------------
// ClientConfig
// ---------------------------------------------------------------------------

ClientConfig::ClientConfig() {
  const char* env_url = std::getenv("XYO_API_BASE_URL");
  if (env_url && *env_url) {
    base_url = env_url;
  }
}

ClientConfig::ClientConfig(std::string key, std::string url, bool allow_insecure)
    : api_key(std::move(key)), base_url(std::move(url)), allow_insecure_transport(allow_insecure) {
  if (base_url.empty()) {
    const char* env_url = std::getenv("XYO_API_BASE_URL");
    if (env_url && *env_url) {
      base_url = env_url;
    } else {
      base_url = "https://api.xyo.financial";
    }
  }
}

ClientConfig::ClientConfig(ClientConfig&& other) noexcept
    : api_key(std::move(other.api_key)),
      base_url(std::move(other.base_url)),
      allow_insecure_transport(other.allow_insecure_transport),
      connect_timeout_ms(other.connect_timeout_ms),
      request_timeout_ms(other.request_timeout_ms),
      max_collection_size(other.max_collection_size),
      allowed_download_domains(std::move(other.allowed_download_domains)) {}

ClientConfig& ClientConfig::operator=(ClientConfig&& other) noexcept {
  if (this != &other) {
    secure_erase(api_key);
    api_key = std::move(other.api_key);
    base_url = std::move(other.base_url);
    allow_insecure_transport = other.allow_insecure_transport;
    connect_timeout_ms = other.connect_timeout_ms;
    request_timeout_ms = other.request_timeout_ms;
    max_collection_size = other.max_collection_size;
    allowed_download_domains = std::move(other.allowed_download_domains);
  }
  return *this;
}

ClientConfig::~ClientConfig() noexcept {
  secure_erase(api_key);
}

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------

Error::Error(ErrorCategory category, const std::string& message,
             long http_status_code, int transport_code,
             std::optional<RateLimitInfo> rate_limit_info,
             std::optional<std::string> correlation_id,
             std::string problem_type,
             std::string problem_title,
             std::string problem_detail,
             std::string problem_instance)
    : std::runtime_error(message),
      category_(category),
      http_status_code_(http_status_code),
      transport_code_(transport_code),
      rate_limit_info_(std::move(rate_limit_info)),
      correlation_id_(std::move(correlation_id)),
      problem_type_(std::move(problem_type)),
      problem_title_(std::move(problem_title)),
      problem_detail_(std::move(problem_detail)),
      problem_instance_(std::move(problem_instance)) {}

// ---------------------------------------------------------------------------
// Client::Impl
// ---------------------------------------------------------------------------

struct Client::Impl {
  ClientConfig config;

  explicit Impl(ClientConfig cfg) : config(std::move(cfg)) {}

  cpr::Session& session() const {
    thread_local cpr::Session s;
    return s;
  }
};

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(ClientConfig config) {
  if (config.api_key.empty()) {
    throw Error(ErrorCategory::validation, "api_key must not be empty");
  }
  if (!is_valid_header_value(config.api_key)) {
    throw Error(ErrorCategory::validation,
                "api_key contains characters that are not valid in an HTTP header value (check for a trailing newline)");
  }
  ParsedUrl u = parse_url(config.base_url);
  if (u.scheme != "https" && !config.allow_insecure_transport) {
    throw Error(ErrorCategory::validation,
                "base_url must use https; set allow_insecure_transport to permit plaintext against a local test server");
  }
  impl_ = std::make_unique<Impl>(std::move(config));
}

Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;
Client::~Client() noexcept = default;

// ---------------------------------------------------------------------------
// enrichTransaction – single transaction, synchronous.
// ---------------------------------------------------------------------------
EnrichmentResponse Client::enrichTransaction(
    const EnrichmentRequest& request,
    const EnrichmentRequestOptions& options) const {
  EnrichmentRequest req_copy = request;
  validate_and_normalize_request(req_copy, "enrichTransaction");

  nlohmann::json body = {
      {"content", req_copy.content},
      {"countryCode", req_copy.country_code}
  };

  std::string url = impl_->config.base_url;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/v1/ai/finance/enrichment/transaction";

  cpr::Header headers = build_headers(impl_->config.api_key, "application/json", "application/json", options);

  auto& s = impl_->session();
  s.SetUrl(cpr::Url{url});
  s.SetHeader(headers);
  s.SetBody(cpr::Body{body.dump()});
  s.SetConnectTimeout(cpr::ConnectTimeout{std::chrono::milliseconds(impl_->config.connect_timeout_ms)});
  s.SetTimeout(cpr::Timeout{std::chrono::milliseconds(options.request_timeout_ms.value_or(impl_->config.request_timeout_ms))});

  cpr::Response res = s.Post();

  if (res.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "enrichTransaction transport error: " + res.error.message,
                0, static_cast<int>(res.error.code));
  }

  check_and_throw_http_error(res, "enrichTransaction");

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(res.text);
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from enrichTransaction: " + std::string(e.what()));
  }

  return parse_enrichment_response(json_data);
}

// ---------------------------------------------------------------------------
// enrichTransactions – bulk, async (returns a job handle).
// ---------------------------------------------------------------------------
BulkEnrichmentResponse Client::enrichTransactions(
    const std::vector<EnrichmentRequest>& requests,
    const EnrichmentRequestOptions& options) const {

  validate_batch_size(requests.size(), impl_->config.max_collection_size);

  nlohmann::json body_array = nlohmann::json::array();
  for (const auto& r : requests) {
    EnrichmentRequest req_copy = r;
    validate_and_normalize_request(req_copy, "enrichTransactions");
    body_array.push_back({
        {"content", std::move(req_copy.content)},
        {"countryCode", std::move(req_copy.country_code)}
    });
  }

  std::string url = impl_->config.base_url;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/v1/ai/finance/enrichment/transactions";

  cpr::Header headers = build_headers(impl_->config.api_key, "application/json", "application/json", options);

  auto& s = impl_->session();
  s.SetUrl(cpr::Url{url});
  s.SetHeader(headers);
  s.SetBody(cpr::Body{body_array.dump()});
  s.SetConnectTimeout(cpr::ConnectTimeout{std::chrono::milliseconds(impl_->config.connect_timeout_ms)});
  s.SetTimeout(cpr::Timeout{std::chrono::milliseconds(options.request_timeout_ms.value_or(impl_->config.request_timeout_ms))});

  cpr::Response res = s.Post();

  if (res.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "enrichTransactions transport error: " + res.error.message,
                0, static_cast<int>(res.error.code));
  }

  check_and_throw_http_error(res, "enrichTransactions");

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(res.text);
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from enrichTransactions: " + std::string(e.what()));
  }

  auto id_it = json_data.find("id");
  auto link_it = json_data.find("link");
  if (id_it == json_data.end() || !id_it->is_string() ||
      link_it == json_data.end() || !link_it->is_string()) {
    throw Error(ErrorCategory::parsing,
                "enrichTransactions: response missing required 'id' or 'link' field");
  }

  BulkEnrichmentResponse out;
  out.id   = id_it->get<std::string>();
  out.link = link_it->get<std::string>();
  return out;
}

// ---------------------------------------------------------------------------
// getEnrichmentStatus – poll the status of an async bulk job.
// ---------------------------------------------------------------------------
EnrichmentStatus Client::getEnrichmentStatus(
    const std::string& id,
    const EnrichmentRequestOptions& options) const {
  validate_job_id(id);

  std::string url = impl_->config.base_url;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/v1/ai/finance/enrichment/status/" + id;

  cpr::Header headers = build_headers(impl_->config.api_key, "", "application/json", options);

  auto& s = impl_->session();
  s.SetUrl(cpr::Url{url});
  s.SetHeader(headers);
  s.SetConnectTimeout(cpr::ConnectTimeout{std::chrono::milliseconds(impl_->config.connect_timeout_ms)});
  s.SetTimeout(cpr::Timeout{std::chrono::milliseconds(options.request_timeout_ms.value_or(impl_->config.request_timeout_ms))});

  cpr::Response res = s.Get();

  if (res.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "getEnrichmentStatus transport error: " + res.error.message,
                0, static_cast<int>(res.error.code));
  }

  check_and_throw_http_error(res, "getEnrichmentStatus");

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(res.text);
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from getEnrichmentStatus: " + std::string(e.what()));
  }

  std::string status_str = to_ascii_lower(json_data.value("status", ""));

  if (status_str == "ready") return EnrichmentStatus::ready;
  if (status_str == "failed") return EnrichmentStatus::failed;
  if (status_str == "pending") return EnrichmentStatus::pending;

  throw Error(ErrorCategory::parsing,
              "getEnrichmentStatus: unrecognised status value '" + status_str + "'");
}

// ---------------------------------------------------------------------------
// downloadEnrichmentCollection – GET tar.gz, decompress, parse JSON entries.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t MAX_DECOMPRESSED_SIZE = 100 * 1024 * 1024; // 100 MB safety limit
constexpr std::size_t MAX_ARCHIVE_BYTES     = 50 * 1024 * 1024;  // 50 MB safety limit
constexpr std::size_t MAX_TAR_ENTRIES       = 50'000;
constexpr std::size_t MAX_ENTRY_BYTES       = 10 * 1024 * 1024; // 10 MiB
constexpr std::size_t TAR_BLOCK_SIZE        = 512;
constexpr std::size_t TAR_SIZE_OFFSET       = 124;
constexpr std::size_t TAR_SIZE_LEN          = 12;
constexpr std::size_t TAR_TYPE_OFFSET       = 156;

inline bool is_path_traversal(std::string_view path) {
  if (path.empty()) return false;
  if (path.front() == '/' || path.front() == '\\') return true;
  std::size_t start = 0;
  while (start < path.size()) {
    std::size_t end = path.find_first_of("/\\", start);
    if (end == std::string_view::npos) end = path.size();
    std::string_view segment = path.substr(start, end - start);
    if (segment == "..") return true;
    start = end + 1;
  }
  return false;
}

std::string gunzip(const std::string& compressed) {
  if (compressed.empty()) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     "downloadEnrichmentCollection: empty compressed data");
  }
  if (compressed.size() > static_cast<std::size_t>((std::numeric_limits<uInt>::max)())) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     "downloadEnrichmentCollection: compressed payload exceeds maximum supported size");
  }

  z_stream zs{};
  if (inflateInit2(&zs, 31) != Z_OK) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     "downloadEnrichmentCollection: zlib inflateInit2 failed");
  }

  struct ZStreamGuard {
    z_stream* zs_ptr;
    ~ZStreamGuard() {
      if (zs_ptr) {
        inflateEnd(zs_ptr);
      }
    }
  } guard{&zs};

  zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  zs.avail_in = static_cast<uInt>(compressed.size());

  std::string out;
  out.reserve(std::min<std::size_t>(compressed.size() * 4, MAX_DECOMPRESSED_SIZE));

  std::vector<char> buf(65536);
  int ret = Z_OK;
  for (;;) {
    zs.next_out  = reinterpret_cast<Bytef*>(buf.data());
    zs.avail_out = static_cast<uInt>(buf.size());
    ret = inflate(&zs, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: gzip decompression failed");
    }
    std::size_t decompressed_bytes = buf.size() - zs.avail_out;
    if (out.size() + decompressed_bytes > MAX_DECOMPRESSED_SIZE) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: decompressed data exceeds safety limit (100MB)");
    }
    out.append(buf.data(), decompressed_bytes);

    if (ret == Z_STREAM_END) {
      if (zs.avail_in == 0) break;
      if (inflateReset2(&zs, 31) != Z_OK) {
        throw xyo::Error(xyo::ErrorCategory::parsing,
                         "downloadEnrichmentCollection: zlib inflateReset2 failed for multi-member gzip");
      }
    }
  }

  return out;
}

std::vector<std::string_view> parse_tar_entries(const std::string& tar_bytes) {
  std::vector<std::string_view> entries;
  const std::size_t total = tar_bytes.size();
  std::size_t offset = 0;

  while (offset + TAR_BLOCK_SIZE <= total) {
    const char* hdr = tar_bytes.data() + offset;

    bool all_zero = true;
    for (std::size_t i = 0; i < TAR_BLOCK_SIZE && all_zero; ++i) {
      if (hdr[i] != '\0') all_zero = false;
    }
    if (all_zero) break;

    unsigned int expected_chk = 0;
    for (std::size_t i = 0; i < TAR_BLOCK_SIZE; ++i) {
      if (i >= 148 && i < 156) {
        expected_chk += ' ';
      } else {
        expected_chk += static_cast<unsigned char>(hdr[i]);
      }
    }
    char chk_field[9] = {};
    std::memcpy(chk_field, hdr + 148, 8);
    unsigned int actual_chk = static_cast<unsigned int>(std::strtoul(chk_field, nullptr, 8));
    if (actual_chk != 0 && actual_chk != expected_chk) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: tar header checksum mismatch");
    }

    char typeflag = hdr[TAR_TYPE_OFFSET];
    char size_field[TAR_SIZE_LEN + 1] = {};
    std::memcpy(size_field, hdr + TAR_SIZE_OFFSET, TAR_SIZE_LEN);
    std::size_t file_size = static_cast<std::size_t>(std::strtoull(size_field, nullptr, 8));

    offset += TAR_BLOCK_SIZE;

    std::string entry_name(hdr, ::strnlen(hdr, 100));

    if (is_path_traversal(entry_name)) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: path traversal detected in tar archive entry '" +
                       entry_name + "'");
    }

    if (typeflag == '5') {
      // Directory entry in tar archive, skip content
    } else if (typeflag != '0' && typeflag != '\0') {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: unsupported tar entry typeflag '" +
                       std::string(1, typeflag) + "' for entry '" + entry_name + "'");
    } else if (file_size > 0) {
      if (file_size > MAX_ENTRY_BYTES) {
        throw xyo::Error(xyo::ErrorCategory::parsing,
                         "downloadEnrichmentCollection: tar entry size exceeds safety limit (10MB)");
      }
      if (entries.size() >= MAX_TAR_ENTRIES) {
        throw xyo::Error(xyo::ErrorCategory::parsing,
                         "downloadEnrichmentCollection: tar archive contains too many entries (exceeded limit)");
      }
      if (file_size > total || offset > total - file_size) {
        throw xyo::Error(xyo::ErrorCategory::parsing,
                         "downloadEnrichmentCollection: truncated tar archive");
      }
      entries.emplace_back(tar_bytes.data() + offset, file_size);
    }

    std::size_t padded = file_size + (TAR_BLOCK_SIZE - 1);
    if (padded < file_size) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: tar size padding overflow");
    }
    padded &= ~static_cast<std::size_t>(TAR_BLOCK_SIZE - 1);

    if (padded > total - offset) {
      offset = total;
    } else {
      offset += padded;
    }
  }
  return entries;
}

xyo::EnrichmentResponse parse_enrichment_json(std::string_view json_view) {
  nlohmann::json jv;
  try {
    jv = nlohmann::json::parse(json_view);
  } catch (const std::exception& e) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     std::string("downloadEnrichmentCollection: JSON parse error: ") + e.what());
  }

  return parse_enrichment_response(jv);
}

}  // anonymous namespace

std::vector<EnrichmentResponse>
Client::downloadEnrichmentCollection(
    const std::string& downloadUrl,
    const EnrichmentRequestOptions& options) const {
  if (downloadUrl.empty()) {
    throw Error(ErrorCategory::validation,
                "downloadEnrichmentCollection: downloadUrl must not be empty");
  }

  std::string full_url = downloadUrl;
  if (downloadUrl.rfind("http://", 0) != 0 && downloadUrl.rfind("https://", 0) != 0) {
    std::string base = impl_->config.base_url;
    if (!base.empty() && base.back() == '/' && !full_url.empty() && full_url.front() == '/') {
      full_url = base + full_url.substr(1);
    } else if (!base.empty() && base.back() != '/' && !full_url.empty() && full_url.front() != '/') {
      full_url = base + "/" + full_url;
    } else {
      full_url = base + full_url;
    }
  }

  ParsedUrl target_url = parse_url(full_url);
  ParsedUrl base_url   = parse_url(impl_->config.base_url);

  bool base_is_https   = (base_url.scheme == "https");
  bool target_is_https = (target_url.scheme == "https");

  if (base_is_https && !target_is_https) {
    throw Error(ErrorCategory::validation,
                "downloadEnrichmentCollection: refusing insecure HTTP download link for HTTPS client");
  }

  bool is_same_host = (target_url.host == base_url.host);
  bool is_same_port = (target_url.port == base_url.port);
  bool is_allowed_domain = false;
  for (const auto& domain : impl_->config.allowed_download_domains) {
    if (target_url.host.size() >= domain.size() &&
        target_url.host.rfind(domain) == (target_url.host.size() - domain.size())) {
      is_allowed_domain = true;
      break;
    }
  }

  if (!(is_same_host && is_same_port) && !is_allowed_domain) {
    throw Error(ErrorCategory::validation,
                "downloadEnrichmentCollection: domain \"" + target_url.host +
                    "\" is not permitted for secure archive downloads");
  }

  std::string auth_key = (is_same_host && is_same_port) ? impl_->config.api_key : "";
  EnrichmentRequestOptions safe_options = options;
  if (!(is_same_host && is_same_port)) {
    safe_options.x_api_user = std::nullopt;
    safe_options.x_correlation_id = std::nullopt;
    safe_options.traceparent = std::nullopt;
  }

  cpr::Header headers = build_headers(
      auth_key, "",
      "application/gzip, application/x-tar, application/octet-stream;q=0.9, */*;q=0.8",
      safe_options);

  cpr::Response response = cpr::Get(
      cpr::Url{full_url},
      headers,
      cpr::Redirect{false},
      cpr::VerifySsl{true},
      cpr::ProgressCallback{
          [](cpr::cpr_off_t dlTotal, cpr::cpr_off_t dlNow,
             cpr::cpr_off_t, cpr::cpr_off_t, intptr_t) -> bool {
            if (dlNow > 0 && static_cast<std::size_t>(dlNow) > MAX_ARCHIVE_BYTES) return false;
            if (dlTotal > 0 && static_cast<std::size_t>(dlTotal) > MAX_ARCHIVE_BYTES) return false;
            return true;
          }},
      cpr::ConnectTimeout{std::chrono::milliseconds(impl_->config.connect_timeout_ms)},
      cpr::Timeout{std::chrono::milliseconds(options.request_timeout_ms.value_or(impl_->config.request_timeout_ms))}
  );

  if (response.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "downloadEnrichmentCollection: request failed: " + response.error.message,
                0, static_cast<int>(response.error.code));
  }

  check_and_throw_http_error(response, "downloadEnrichmentCollection");

  auto ct_it = response.header.find("content-type");
  if (ct_it != response.header.end()) {
    std::string ct_str = ct_it->second;
    std::string ct_lower = to_ascii_lower(ct_str);
    if (ct_lower.find("gzip") == std::string::npos &&
        ct_lower.find("tar") == std::string::npos &&
        ct_lower.find("octet-stream") == std::string::npos &&
        ct_lower.find("binary") == std::string::npos) {
      throw Error(ErrorCategory::http,
                  "downloadEnrichmentCollection: unexpected Content-Type '" + ct_str +
                      "' received when expecting binary archive",
                  response.status_code);
    }
  }

  if (response.text.size() > MAX_ARCHIVE_BYTES) {
    throw Error(ErrorCategory::parsing,
                "downloadEnrichmentCollection: response payload (" +
                std::to_string(response.text.size()) +
                " bytes) exceeds maximum allowable compressed archive limit of 50MB");
  }

  if (response.text.empty()) {
    throw Error(ErrorCategory::parsing,
                "downloadEnrichmentCollection: empty response body");
  }

  std::string tar_bytes;
  try {
    tar_bytes = gunzip(response.text);
  } catch (const Error&) {
    throw;
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                std::string("downloadEnrichmentCollection: decompression error: ") + e.what());
  }

  std::vector<std::string_view> raw_entries;
  try {
    raw_entries = parse_tar_entries(tar_bytes);
  } catch (const Error&) {
    throw;
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                std::string("downloadEnrichmentCollection: tar parsing error: ") + e.what());
  }

  if (raw_entries.empty()) {
    throw Error(ErrorCategory::parsing,
                "downloadEnrichmentCollection: tar archive contains no valid data entries");
  }

  std::vector<EnrichmentResponse> results;
  results.reserve(raw_entries.size());
  for (const auto& entry : raw_entries) {
    results.push_back(parse_enrichment_json(entry));
  }

  return results;
}

}  // namespace xyo

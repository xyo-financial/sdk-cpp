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
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xyo {

// ---------------------------------------------------------------------------
// Helpers & Sanitizers
// ---------------------------------------------------------------------------

namespace {

void secure_erase(std::string& str) noexcept {
  if (str.capacity() > 0) {
    OPENSSL_cleanse(str.data(), str.capacity());
    str.clear();
  }
}

inline void validate_request(const EnrichmentRequest& req, const char* op_name) {
  if (req.content.empty()) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request content must not be empty");
  }
  if (req.content.size() > 128) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request content exceeds maximum length of 128 characters");
  }
  if (req.country_code.empty()) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request country_code must not be empty");
  }
  if (req.country_code.size() != 2) {
    throw Error(ErrorCategory::validation,
                std::string(op_name) + ": request country_code must be a 2-letter ISO 3166-1 alpha-2 code");
  }
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
  if (url_str.find(' ') != std::string::npos) {
    throw Error(ErrorCategory::validation, "downloadEnrichmentCollection: invalid URL format");
  }
  std::size_t scheme_end = url_str.find("://");
  if (scheme_end == std::string::npos) {
    throw Error(ErrorCategory::validation, "downloadEnrichmentCollection: invalid URL format: missing scheme");
  }
  res.scheme = url_str.substr(0, scheme_end);
  std::transform(res.scheme.begin(), res.scheme.end(), res.scheme.begin(),
                 [](unsigned char c) { return std::tolower(c); });

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
    res.host = host_port.substr(0, port_pos);
    try {
      res.port = std::stoi(host_port.substr(port_pos + 1));
    } catch (...) {
      throw Error(ErrorCategory::validation, "downloadEnrichmentCollection: invalid URL format: bad port number");
    }
  } else {
    res.host = host_port;
    if (res.scheme == "https") res.port = 443;
    else if (res.scheme == "http") res.port = 80;
  }
  std::transform(res.host.begin(), res.host.end(), res.host.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (res.host.empty()) {
    throw Error(ErrorCategory::validation, "downloadEnrichmentCollection: invalid URL format: missing host");
  }
  return res;
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
  }
  return "UNKNOWN";
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

ClientConfig::ClientConfig(std::string key, std::string url)
    : api_key(std::move(key)), base_url(std::move(url)) {
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
      connect_timeout_ms(other.connect_timeout_ms),
      request_timeout_ms(other.request_timeout_ms),
      max_collection_size(other.max_collection_size) {}

ClientConfig& ClientConfig::operator=(ClientConfig&& other) noexcept {
  if (this != &other) {
    secure_erase(api_key);
    api_key = std::move(other.api_key);
    base_url = std::move(other.base_url);
    connect_timeout_ms = other.connect_timeout_ms;
    request_timeout_ms = other.request_timeout_ms;
    max_collection_size = other.max_collection_size;
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
             long http_status_code, int transport_code)
    : std::runtime_error(message),
      category_(category),
      http_status_code_(http_status_code),
      transport_code_(transport_code) {}

// ---------------------------------------------------------------------------
// Client::Impl
// ---------------------------------------------------------------------------

struct Client::Impl {
  ClientConfig config;

  explicit Impl(ClientConfig cfg) : config(std::move(cfg)) {}
};

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(ClientConfig config) {
  if (config.api_key.empty()) {
    throw Error(ErrorCategory::validation, "api_key must not be empty");
  }
  impl_ = std::make_unique<Impl>(std::move(config));
}

Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;
Client::~Client() noexcept = default;

// ---------------------------------------------------------------------------
// enrichTransaction – single transaction, synchronous.
// ---------------------------------------------------------------------------
EnrichmentResponse Client::enrichTransaction(const EnrichmentRequest& request) const {
  validate_request(request, "enrichTransaction");

  nlohmann::json body = {
      {"content", request.content},
      {"countryCode", request.country_code}
  };

  std::string url = impl_->config.base_url;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/v1/ai/finance/enrichment/transaction";

  cpr::Response res = cpr::Post(
      cpr::Url{url},
      cpr::Header{
          {"Authorization", "Bearer " + impl_->config.api_key},
          {"Content-Type", "application/json"},
          {"Accept", "application/json"}
      },
      cpr::Body{body.dump()},
      cpr::Timeout{std::chrono::milliseconds(impl_->config.request_timeout_ms)}
  );

  if (res.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "enrichTransaction transport error: " + res.error.message,
                0, static_cast<int>(res.error.code));
  }

  if (res.status_code >= 400) {
    throw Error(ErrorCategory::http,
                "HTTP error from enrichTransaction: HTTP " + std::to_string(res.status_code) + ": " + res.text,
                res.status_code);
  }

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(res.text);
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from enrichTransaction: " + std::string(e.what()));
  }

  EnrichmentResponse out;
  try {
    out.merchant    = json_data.value("merchant", "");
    out.description = json_data.value("description", "");
    out.logo        = json_data.value("logo", "");

    if (json_data.contains("categories") && json_data["categories"].is_array()) {
      for (const auto& cat : json_data["categories"]) {
        if (cat.is_string()) {
          out.categories.push_back(cat.get<std::string>());
        }
      }
    }

    if (json_data.contains("location") && !json_data["location"].is_null() && json_data["location"].is_string()) {
      out.location = json_data["location"].get<std::string>();
    }
    if (json_data.contains("address") && !json_data["address"].is_null() && json_data["address"].is_string()) {
      out.address = json_data["address"].get<std::string>();
    }
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "Parsing error from enrichTransaction: " + std::string(e.what()));
  }

  return out;
}

// ---------------------------------------------------------------------------
// enrichTransactions – bulk, async (returns a job handle).
// ---------------------------------------------------------------------------
BulkEnrichmentResponse Client::enrichTransactions(
    const std::vector<EnrichmentRequest>& requests) const {

  if (requests.empty()) {
    throw Error(ErrorCategory::validation,
                "enrichTransactions: request list must not be empty");
  }
  if (requests.size() > impl_->config.max_collection_size) {
    throw Error(ErrorCategory::validation,
                "enrichTransactions: batch size " + std::to_string(requests.size()) +
                " exceeds configured max_collection_size of " +
                std::to_string(impl_->config.max_collection_size));
  }

  nlohmann::json body_array = nlohmann::json::array();
  for (const auto& r : requests) {
    validate_request(r, "enrichTransactions");
    body_array.push_back({
        {"content", r.content},
        {"countryCode", r.country_code}
    });
  }

  std::string url = impl_->config.base_url;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/v1/ai/finance/enrichment/transactions";

  cpr::Response res = cpr::Post(
      cpr::Url{url},
      cpr::Header{
          {"Authorization", "Bearer " + impl_->config.api_key},
          {"Content-Type", "application/json"},
          {"Accept", "application/json"}
      },
      cpr::Body{body_array.dump()},
      cpr::Timeout{std::chrono::milliseconds(impl_->config.request_timeout_ms)}
  );

  if (res.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "enrichTransactions transport error: " + res.error.message,
                0, static_cast<int>(res.error.code));
  }

  if (res.status_code >= 400) {
    throw Error(ErrorCategory::http,
                "HTTP error from enrichTransactions: HTTP " + std::to_string(res.status_code) + ": " + res.text,
                res.status_code);
  }

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(res.text);
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from enrichTransactions: " + std::string(e.what()));
  }

  BulkEnrichmentResponse out;
  try {
    out.id   = json_data.value("id", "");
    out.link = json_data.value("link", "");
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "Parsing error from enrichTransactions: " + std::string(e.what()));
  }

  return out;
}

// ---------------------------------------------------------------------------
// getEnrichmentStatus – poll the status of an async bulk job.
// ---------------------------------------------------------------------------
EnrichmentStatus Client::getEnrichmentStatus(const std::string& id) const {
  if (id.empty()) {
    throw Error(ErrorCategory::validation,
                "getEnrichmentStatus: id must not be empty");
  }

  std::string url = impl_->config.base_url;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/v1/ai/finance/enrichment/status/" + id;

  cpr::Response res = cpr::Get(
      cpr::Url{url},
      cpr::Header{
          {"Authorization", "Bearer " + impl_->config.api_key},
          {"Accept", "application/json"}
      },
      cpr::Timeout{std::chrono::milliseconds(impl_->config.request_timeout_ms)}
  );

  if (res.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "getEnrichmentStatus transport error: " + res.error.message,
                0, static_cast<int>(res.error.code));
  }

  if (res.status_code >= 400) {
    throw Error(ErrorCategory::http,
                "HTTP error from getEnrichmentStatus: HTTP " + std::to_string(res.status_code) + ": " + res.text,
                res.status_code);
  }

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(res.text);
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from getEnrichmentStatus: " + std::string(e.what()));
  }

  std::string status_str = json_data.value("status", "");
  std::transform(status_str.begin(), status_str.end(), status_str.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  if (status_str == "READY") return EnrichmentStatus::ready;
  if (status_str == "FAILED") return EnrichmentStatus::failed;
  if (status_str == "PENDING") return EnrichmentStatus::pending;

  throw Error(ErrorCategory::parsing,
              "getEnrichmentStatus: unrecognised status value '" + status_str + "'");
}

// ---------------------------------------------------------------------------
// downloadEnrichmentCollection – GET tar.gz, decompress, parse JSON entries.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t MAX_DECOMPRESSED_SIZE = 100 * 1024 * 1024; // 100 MB safety limit
constexpr std::size_t MAX_TAR_ENTRIES       = 50'000;
constexpr std::size_t MAX_ENTRY_BYTES       = 10 * 1024 * 1024; // 10 MiB
constexpr std::size_t TAR_BLOCK_SIZE        = 512;
constexpr std::size_t TAR_SIZE_OFFSET       = 124;
constexpr std::size_t TAR_SIZE_LEN          = 12;
constexpr std::size_t TAR_TYPE_OFFSET       = 156;

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
  out.reserve(compressed.size() * 4);

  std::vector<char> buf(65536);
  int ret = Z_OK;
  while (ret != Z_STREAM_END) {
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
    bool is_traversal = (entry_name.find("..") != std::string::npos ||
                         (!entry_name.empty() && (entry_name.front() == '/' || entry_name.front() == '\\')));

    if ((typeflag == '0' || typeflag == '\0') && file_size > 0 && !is_traversal) {
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

  auto get_str = [&](const char* key) -> std::string {
    if (!jv.contains(key) || !jv[key].is_string()) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       std::string("downloadEnrichmentCollection: missing field: ") + key);
    }
    return jv[key].get<std::string>();
  };

  xyo::EnrichmentResponse out;
  out.merchant    = get_str("merchant");
  out.description = get_str("description");
  out.logo        = get_str("logo");

  if (jv.contains("categories") && jv["categories"].is_array()) {
    for (const auto& cat : jv["categories"]) {
      if (cat.is_string()) {
        out.categories.push_back(cat.get<std::string>());
      }
    }
  }

  if (jv.contains("location") && !jv["location"].is_null() && jv["location"].is_string()) {
    out.location = jv["location"].get<std::string>();
  }

  if (jv.contains("address") && !jv["address"].is_null() && jv["address"].is_string()) {
    out.address = jv["address"].get<std::string>();
  }

  return out;
}

}  // anonymous namespace

std::vector<EnrichmentResponse>
Client::downloadEnrichmentCollection(const std::string& downloadUrl) const {
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
  bool is_s3 = (target_url.host.size() >= 14 &&
                target_url.host.rfind(".amazonaws.com") == (target_url.host.size() - 14));

  if (!(is_same_host && is_same_port) && !is_s3) {
    throw Error(ErrorCategory::validation,
                "downloadEnrichmentCollection: domain \"" + target_url.host +
                    "\" is not permitted for secure archive downloads");
  }

  cpr::Header headers = {
      {"Accept", "application/gzip, application/x-tar, application/octet-stream;q=0.9, */*;q=0.8"}
  };

  if (is_same_host && is_same_port) {
    headers.insert({"Authorization", "Bearer " + impl_->config.api_key});
  }

  cpr::Response response = cpr::Get(
      cpr::Url{full_url},
      headers,
      cpr::Timeout{std::chrono::milliseconds(impl_->config.request_timeout_ms)}
  );

  if (response.error.code != cpr::ErrorCode::OK) {
    throw Error(ErrorCategory::transport,
                "downloadEnrichmentCollection: request failed: " + response.error.message,
                0, static_cast<int>(response.error.code));
  }

  if (response.status_code != 200) {
    throw Error(ErrorCategory::http,
                "downloadEnrichmentCollection: HTTP error",
                response.status_code);
  }

  auto ct_it = response.header.find("content-type");
  if (ct_it != response.header.end()) {
    std::string ct_str = ct_it->second;
    std::string ct_lower = ct_str;
    std::transform(ct_lower.begin(), ct_lower.end(), ct_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
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

  std::vector<EnrichmentResponse> results;
  results.reserve(raw_entries.size());
  for (const auto& entry : raw_entries) {
    results.push_back(parse_enrichment_json(entry));
  }

  return results;
}

}  // namespace xyo

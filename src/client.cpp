// Copyright 2026 Syniol Limited
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// XYO C++ SDK – thin wrapper over the OpenAPI-generated cpp-restsdk client.
// All actual HTTP and (de)serialisation is handled by the generated layer.
// ---------------------------------------------------------------------------

#include "xyo/client.hpp"

// Generated API + model headers (cpp-restsdk)
#include "XYOSDK/api/EnrichmentApi.h"
#include "XYOSDK/ApiClient.h"
#include "XYOSDK/ApiConfiguration.h"
#include "XYOSDK/ApiException.h"
#include "XYOSDK/model/EnrichmentRequest.h"
#include "XYOSDK/model/EnrichmentResponse.h"
#include "XYOSDK/model/EnrichTransactions_request_inner.h"
#include "XYOSDK/model/EnrichTransactionCollectionResponse.h"
#include "XYOSDK/model/EnrichmentCollectionStatusResponse.h"

#include <cpprest/details/basic_types.h>
#include <cpprest/http_client.h>
#include <cpprest/json.h>
#include <boost/optional.hpp>
#include <boost/none.hpp>
#include <zlib.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xyo {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Convert a cpprestsdk string_t to std::string portably.
inline std::string to_std(const utility::string_t& s) {
#ifdef _UTF16_STRINGS
  return utility::conversions::to_utf8string(s);
#else
  return s;
#endif
}

/// Convert std::string to cpprestsdk string_t portably.
inline utility::string_t to_sdk(const std::string& s) {
#ifdef _UTF16_STRINGS
  return utility::conversions::to_string_t(s);
#else
  return s;
#endif
}

}  // namespace

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

ClientConfig::ClientConfig(ClientConfig&& other) noexcept
    : api_key(std::move(other.api_key)),
      base_url(std::move(other.base_url)),
      connect_timeout_ms(other.connect_timeout_ms),
      request_timeout_ms(other.request_timeout_ms),
      max_collection_size(other.max_collection_size) {
  if (other.api_key.capacity() > 0) {
    volatile char* p = const_cast<volatile char*>(other.api_key.data());
    for (std::size_t i = 0; i < other.api_key.capacity(); ++i) {
      p[i] = '\0';
    }
  }
}

ClientConfig& ClientConfig::operator=(ClientConfig&& other) noexcept {
  if (this != &other) {
    if (api_key.capacity() > 0) {
      volatile char* p = const_cast<volatile char*>(api_key.data());
      for (std::size_t i = 0; i < api_key.capacity(); ++i) {
        p[i] = '\0';
      }
    }
    api_key = std::move(other.api_key);
    base_url = std::move(other.base_url);
    connect_timeout_ms = other.connect_timeout_ms;
    request_timeout_ms = other.request_timeout_ms;
    max_collection_size = other.max_collection_size;
    if (other.api_key.capacity() > 0) {
      volatile char* p = const_cast<volatile char*>(other.api_key.data());
      for (std::size_t i = 0; i < other.api_key.capacity(); ++i) {
        p[i] = '\0';
      }
    }
  }
  return *this;
}

ClientConfig::~ClientConfig() noexcept {
  if (api_key.capacity() > 0) {
    volatile char* p = const_cast<volatile char*>(api_key.data());
    for (std::size_t i = 0; i < api_key.capacity(); ++i) {
      p[i] = '\0';
    }
  }
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
// Client::Impl – owns the generated ApiClient and EnrichmentApi.
// ---------------------------------------------------------------------------

struct Client::Impl {
  std::shared_ptr<xyo_api::ApiClient>     api_client;
  std::shared_ptr<xyo_api::EnrichmentApi> enrichment_api;

  explicit Impl(const ClientConfig& cfg) {
    auto configuration = std::make_shared<xyo_api::ApiConfiguration>();
    configuration->setBaseUrl(to_sdk(cfg.base_url));
    // Bearer-token authentication: set the Authorization header.
    configuration->getDefaultHeaders()[to_sdk("Authorization")] =
        to_sdk("Bearer " + cfg.api_key);

    if (cfg.request_timeout_ms > 0) {
      web::http::client::http_client_config http_config;
      http_config.set_timeout(utility::seconds(std::max<long>(1, cfg.request_timeout_ms / 1000)));
      configuration->setHttpConfig(http_config);
    }

    api_client = std::make_shared<xyo_api::ApiClient>(configuration);
    enrichment_api = std::make_shared<xyo_api::EnrichmentApi>(api_client);
  }
};

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(ClientConfig config) {
  if (config.api_key.empty()) {
    throw Error(ErrorCategory::validation, "api_key must not be empty");
  }
  impl_ = std::make_unique<Impl>(config);
}

Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;
Client::~Client() noexcept = default;

// ---------------------------------------------------------------------------
// enrichTransaction – single transaction, synchronous.
// ---------------------------------------------------------------------------
EnrichmentResponse Client::enrichTransaction(const EnrichmentRequest& request) const {
  auto req = std::make_shared<xyo_model::EnrichmentRequest>();
  req->setContent(to_sdk(request.content));
  req->setCountryCode(to_sdk(request.country_code));

  std::shared_ptr<xyo_model::EnrichmentResponse> resp;
  try {
    resp = impl_->enrichment_api
               ->enrichTransaction(boost::optional<std::shared_ptr<xyo_model::EnrichmentRequest>>(req))
               .get();  // block until the async pplx::task resolves
  } catch (const xyo_api::ApiException& e) {
    throw Error(ErrorCategory::http,
                "HTTP error from enrichTransaction: " + std::string(e.what()),
                e.error_code().value());
  } catch (const std::invalid_argument& e) {
    throw Error(ErrorCategory::parsing,
                "Parsing error from enrichTransaction: " + std::string(e.what()));
  } catch (const web::json::json_exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from enrichTransaction: " + std::string(e.what()));
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::transport, e.what());
  }

  if (!resp) {
    throw Error(ErrorCategory::parsing, "enrichTransaction: null response");
  }

  EnrichmentResponse out;
  out.merchant    = to_std(resp->getMerchant());
  out.description = to_std(resp->getDescription());

  for (const auto& cat : resp->getCategories()) {
    out.categories.push_back(to_std(cat));
  }

  out.logo = to_std(resp->getLogo());

  if (resp->locationIsSet()) {
    out.location = to_std(resp->getLocation());
  }
  if (resp->addressIsSet()) {
    out.address = to_std(resp->getAddress());
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

  std::vector<std::shared_ptr<xyo_model::EnrichTransactions_request_inner>> items;
  items.reserve(requests.size());
  for (const auto& r : requests) {
    auto item = std::make_shared<xyo_model::EnrichTransactions_request_inner>();
    item->setContent(to_sdk(r.content));
    item->setCountryCode(to_sdk(r.country_code));
    items.push_back(std::move(item));
  }

  std::shared_ptr<xyo_model::EnrichTransactionCollectionResponse> resp;
  try {
    resp = impl_->enrichment_api
               ->enrichTransactions(
                   boost::none,  // x-api-user header (optional, unused)
                   boost::optional<std::vector<std::shared_ptr<xyo_model::EnrichTransactions_request_inner>>>(items))
               .get();
  } catch (const xyo_api::ApiException& e) {
    throw Error(ErrorCategory::http,
                "HTTP error from enrichTransactions: " + std::string(e.what()),
                e.error_code().value());
  } catch (const std::invalid_argument& e) {
    throw Error(ErrorCategory::parsing,
                "Parsing error from enrichTransactions: " + std::string(e.what()));
  } catch (const web::json::json_exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from enrichTransactions: " + std::string(e.what()));
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::transport, e.what());
  }

  if (!resp) {
    throw Error(ErrorCategory::parsing, "enrichTransactions: null response");
  }

  return BulkEnrichmentResponse{
      to_std(resp->getId()),
      to_std(resp->getLink()),
  };
}

// ---------------------------------------------------------------------------
// getEnrichmentStatus – poll the status of an async bulk job.
// ---------------------------------------------------------------------------
EnrichmentStatus Client::getEnrichmentStatus(const std::string& id) const {
  if (id.empty()) {
    throw Error(ErrorCategory::validation,
                "getEnrichmentStatus: id must not be empty");
  }

  std::shared_ptr<xyo_model::EnrichmentCollectionStatusResponse> resp;
  try {
    resp = impl_->enrichment_api
               ->getEnrichmentStatus(
                   to_sdk(id),
                   boost::none)  // x-api-user header (optional, unused)
               .get();
  } catch (const xyo_api::ApiException& e) {
    throw Error(ErrorCategory::http,
                "HTTP error from getEnrichmentStatus: " + std::string(e.what()),
                e.error_code().value());
  } catch (const std::invalid_argument& e) {
    throw Error(ErrorCategory::parsing,
                "Parsing error from getEnrichmentStatus: " + std::string(e.what()));
  } catch (const web::json::json_exception& e) {
    throw Error(ErrorCategory::parsing,
                "JSON parsing error from getEnrichmentStatus: " + std::string(e.what()));
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::transport, e.what());
  }

  if (!resp || !resp->statusIsSet()) {
    throw Error(ErrorCategory::parsing, "getEnrichmentStatus: null response");
  }

  switch (resp->getStatus()) {
    case xyo_model::EnrichmentCollectionStatusResponse::StatusEnum::READY:
      return EnrichmentStatus::ready;
    case xyo_model::EnrichmentCollectionStatusResponse::StatusEnum::FAILED:
      return EnrichmentStatus::failed;
    case xyo_model::EnrichmentCollectionStatusResponse::StatusEnum::PENDING:
      return EnrichmentStatus::pending;
  }

  throw Error(ErrorCategory::parsing,
              "getEnrichmentStatus: unrecognised status value");
}

// ---------------------------------------------------------------------------
// downloadEnrichmentCollection – GET tar.gz, decompress, parse JSON entries.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t MAX_DECOMPRESSED_SIZE = 100 * 1024 * 1024; // 100 MB safety limit
constexpr std::size_t TAR_BLOCK_SIZE        = 512;
constexpr std::size_t TAR_SIZE_OFFSET       = 124;
constexpr std::size_t TAR_SIZE_LEN          = 12;
constexpr std::size_t TAR_TYPE_OFFSET       = 156;

/// Decompress a gzip byte blob into a std::string using zlib.
std::string gunzip(const std::vector<uint8_t>& compressed) {
  if (compressed.empty()) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     "downloadEnrichmentCollection: empty compressed data");
  }

  // inflateInit2 with windowBits=31 enables automatic gzip header detection.
  z_stream zs{};
  if (inflateInit2(&zs, 31) != Z_OK) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     "downloadEnrichmentCollection: zlib inflateInit2 failed");
  }

  // RAII Guard guarantees inflateEnd is called even if std::bad_alloc or custom Error is thrown.
  struct ZStreamGuard {
    z_stream* zs_ptr;
    ~ZStreamGuard() {
      if (zs_ptr) {
        inflateEnd(zs_ptr);
      }
    }
  } guard{&zs};

  zs.next_in  = const_cast<Bytef*>(compressed.data());
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

/// Walk POSIX ustar/GNU tar blocks and return each regular-file's content as string_view.
/// Each block is 512 bytes; the header occupies block 0, data follows.
std::vector<std::string_view> parse_tar_entries(const std::string& tar_bytes) {
  std::vector<std::string_view> entries;
  const std::size_t total = tar_bytes.size();
  std::size_t offset = 0;

  while (offset + TAR_BLOCK_SIZE <= total) {
    const char* hdr = tar_bytes.data() + offset;

    // Two consecutive all-zero blocks mark end-of-archive.
    bool all_zero = true;
    for (std::size_t i = 0; i < TAR_BLOCK_SIZE && all_zero; ++i) {
      if (hdr[i] != '\0') all_zero = false;
    }
    if (all_zero) break;

    // Filename is at offset 0 (100 bytes, NUL-padded).
    // Typeflag is at offset 156: '0' or '\0' = regular file.
    char typeflag = hdr[TAR_TYPE_OFFSET];

    // File size is stored as an octal ASCII string at offset 124 (12 bytes).
    char size_field[TAR_SIZE_LEN + 1] = {};
    std::memcpy(size_field, hdr + TAR_SIZE_OFFSET, TAR_SIZE_LEN);
    std::size_t file_size = static_cast<std::size_t>(std::strtoull(size_field, nullptr, 8));

    offset += TAR_BLOCK_SIZE;  // advance past header block

    if ((typeflag == '0' || typeflag == '\0') && file_size > 0) {
      // Check for integer overflow on file boundary
      if (file_size > total || offset > total - file_size) {
        throw xyo::Error(xyo::ErrorCategory::parsing,
                         "downloadEnrichmentCollection: truncated tar archive");
      }
      entries.emplace_back(tar_bytes.data() + offset, file_size);
    }

    // Round file_size up to the next 512-byte boundary with overflow protection
    std::size_t padded = file_size + (TAR_BLOCK_SIZE - 1);
    if (padded < file_size) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       "downloadEnrichmentCollection: tar size padding overflow");
    }
    padded &= ~static_cast<std::size_t>(TAR_BLOCK_SIZE - 1);

    if (offset > total - padded) {
      offset = total;
    } else {
      offset += padded;
    }
  }
  return entries;
}

/// Parse a single JSON-encoded EnrichmentResponse entry string.
xyo::EnrichmentResponse parse_enrichment_json(std::string_view json_view) {
  web::json::value jv;
  try {
    jv = web::json::value::parse(to_sdk(std::string(json_view)));
  } catch (const std::exception& e) {
    throw xyo::Error(xyo::ErrorCategory::parsing,
                     std::string("downloadEnrichmentCollection: JSON parse error: ") + e.what());
  }

  auto get_str = [&](const char* key) -> std::string {
    auto k = utility::conversions::to_string_t(key);
    if (!jv.has_field(k) || !jv.at(k).is_string()) {
      throw xyo::Error(xyo::ErrorCategory::parsing,
                       std::string("downloadEnrichmentCollection: missing field: ") + key);
    }
    return utility::conversions::to_utf8string(jv.at(k).as_string());
  };

  xyo::EnrichmentResponse out;
  out.merchant    = get_str("merchant");
  out.description = get_str("description");
  out.logo        = get_str("logo");

  auto cats_key = utility::conversions::to_string_t("categories");
  if (jv.has_field(cats_key) && jv.at(cats_key).is_array()) {
    for (const auto& cat : jv.at(cats_key).as_array()) {
      if (cat.is_string()) {
        out.categories.push_back(utility::conversions::to_utf8string(cat.as_string()));
      }
    }
  }

  auto loc_key = utility::conversions::to_string_t("location");
  if (jv.has_field(loc_key) && jv.at(loc_key).is_string()) {
    out.location = utility::conversions::to_utf8string(jv.at(loc_key).as_string());
  }

  auto addr_key = utility::conversions::to_string_t("address");
  if (jv.has_field(addr_key) && jv.at(addr_key).is_string()) {
    out.address = utility::conversions::to_utf8string(jv.at(addr_key).as_string());
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

  auto api_cfg = impl_->api_client->getConfiguration();
  std::string full_url = downloadUrl;
  if (downloadUrl.rfind("http://", 0) != 0 && downloadUrl.rfind("https://", 0) != 0) {
    std::string base = to_std(api_cfg->getBaseUrl());
    if (!base.empty() && base.back() == '/' && !full_url.empty() && full_url.front() == '/') {
      full_url = base + full_url.substr(1);
    } else if (!base.empty() && base.back() != '/' && !full_url.empty() && full_url.front() != '/') {
      full_url = base + "/" + full_url;
    } else {
      full_url = base + full_url;
    }
  }

  // -------------------------------------------------------------------------
  // Issue GET request with Bearer auth and Accept: application/gzip.
  // -------------------------------------------------------------------------
  web::http::client::http_client_config http_cfg = api_cfg->getHttpConfig();

  web::http::client::http_client http_client(
      to_sdk(full_url), http_cfg);

  web::http::http_request get_req(web::http::methods::GET);

  // Protocol Downgrade & SSRF Protection:
  // 1. If configured base_url is HTTPS, reject unencrypted HTTP download links to prevent token leakage
  web::uri target_uri(to_sdk(full_url));
  web::uri base_uri(api_cfg->getBaseUrl());
  bool base_is_https   = (base_uri.scheme() == utility::conversions::to_string_t("https"));
  bool target_is_https = (target_uri.scheme() == utility::conversions::to_string_t("https"));

  if (base_is_https && !target_is_https) {
    throw Error(ErrorCategory::validation,
                "downloadEnrichmentCollection: refusing insecure HTTP download link for HTTPS client");
  }

  // 2. Only attach Authorization header if target host and port match the configured base_url
  bool is_same_host = (target_uri.host() == base_uri.host());
  bool is_same_port = (target_uri.port() == base_uri.port());

  if (is_same_host && is_same_port) {
    const auto& default_headers = api_cfg->getDefaultHeaders();
    auto auth_it = default_headers.find(utility::conversions::to_string_t("Authorization"));
    if (auth_it != default_headers.end()) {
      get_req.headers()[utility::conversions::to_string_t("Authorization")] = auth_it->second;
    }
  }
  get_req.headers()[utility::conversions::to_string_t("Accept")] =
      utility::conversions::to_string_t("application/gzip");

  web::http::http_response response;
  try {
    response = http_client.request(get_req).get();
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::transport,
                std::string("downloadEnrichmentCollection: request failed: ") + e.what());
  }

  const auto status = response.status_code();
  if (status != web::http::status_codes::OK) {
    throw Error(ErrorCategory::http,
                "downloadEnrichmentCollection: HTTP error",
                static_cast<long>(status));
  }

  // -------------------------------------------------------------------------
  // Read compressed body into a byte vector.
  // -------------------------------------------------------------------------
  std::vector<uint8_t> compressed_body;
  try {
    auto body_task = response.extract_vector();
    compressed_body = body_task.get();
  } catch (const std::exception& e) {
    throw Error(ErrorCategory::transport,
                std::string("downloadEnrichmentCollection: failed to read body: ") + e.what());
  }

  if (compressed_body.empty()) {
    throw Error(ErrorCategory::parsing,
                "downloadEnrichmentCollection: empty response body");
  }

  // -------------------------------------------------------------------------
  // Decompress gzip, parse tar, decode each JSON entry.
  // -------------------------------------------------------------------------
  std::string tar_bytes;
  try {
    tar_bytes = gunzip(compressed_body);
  } catch (const Error&) {
    throw;  // already tagged with correct category
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

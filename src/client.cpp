// Copyright (c) 2025 Syniol Limited
// SPDX-License-Identifier: BSD-3-Clause

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
#include <boost/optional.hpp>
#include <boost/none.hpp>
#include <chrono>
#include <stdexcept>
#include <string>

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

ClientConfig::~ClientConfig() noexcept = default;

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

}  // namespace xyo

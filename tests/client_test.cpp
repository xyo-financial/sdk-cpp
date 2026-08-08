// Copyright (c) 2025 Syniol Limited
// SPDX-License-Identifier: BSD-3-Clause

#include "xyo/client.hpp"

#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

inline void test_check(bool condition, const char* condition_str, const char* file, int line) {
  if (!condition) {
    std::cerr << "Assertion failed: (" << condition_str << ") at " << file << ":" << line << "\n";
    std::exit(1);
  }
}

#define TEST_ASSERT(cond) test_check(!!(cond), #cond, __FILE__, __LINE__)

void expects_error(xyo::ErrorCategory expected_category,
                   const std::string& expected_message_substring,
                   const std::function<void()>& operation) {
  try {
    operation();
    std::cerr << "Expected error with category and message containing '"
              << expected_message_substring << "' but no exception was thrown.\n";
    std::exit(1);
  } catch (const xyo::Error& e) {
    TEST_ASSERT(e.category() == expected_category);
    TEST_ASSERT(e.what() != nullptr);
    std::string msg(e.what());
    if (!expected_message_substring.empty()) {
      TEST_ASSERT(msg.find(expected_message_substring) != std::string::npos);
    }
  }
}

int get_free_port() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return 19876;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(sock);
    return 19876;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
    close(sock);
    return 19876;
  }
  int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}

class MockHttpServer {
 public:
  struct RecordedRequest {
    std::string method;
    std::string path;
    std::string authorization_header;
    std::string content_type;
    std::string body;
  };

  using Handler = std::function<web::http::http_response(const RecordedRequest&)>;

  explicit MockHttpServer(int port)
      : port_(port),
        listener_(utility::conversions::to_string_t("http://127.0.0.1:" + std::to_string(port))) {
    listener_.support([this](web::http::http_request req) {
      RecordedRequest rec;
      rec.method = utility::conversions::to_utf8string(req.method());
      rec.path = utility::conversions::to_utf8string(req.relative_uri().path());

      if (req.headers().has(utility::conversions::to_string_t("Authorization"))) {
        rec.authorization_header = utility::conversions::to_utf8string(
            req.headers()[utility::conversions::to_string_t("Authorization")]);
      }
      if (req.headers().has(utility::conversions::to_string_t("Content-Type"))) {
        rec.content_type = utility::conversions::to_utf8string(
            req.headers()[utility::conversions::to_string_t("Content-Type")]);
      }
      rec.body = req.extract_utf8string(true).get();

      std::lock_guard<std::mutex> lock(mutex_);
      recorded_requests_.push_back(rec);

      web::http::http_response resp;
      if (handler_) {
        resp = handler_(rec);
      } else {
        resp.set_status_code(web::http::status_codes::NotFound);
      }
      req.reply(resp).wait();
    });
  }

  void start() {
    listener_.open().wait();
  }

  void stop() {
    try {
      listener_.close().wait();
    } catch (...) {
    }
  }

  void set_handler(Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handler_ = std::move(handler);
  }

  std::vector<RecordedRequest> get_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    return recorded_requests_;
  }

  void clear_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    recorded_requests_.clear();
  }

  std::string base_url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
  }

 private:
  int port_;
  web::http::experimental::listener::http_listener listener_;
  std::mutex mutex_;
  Handler handler_;
  std::vector<RecordedRequest> recorded_requests_;
};

web::http::http_response json_response(web::http::status_code status, const std::string& json_str) {
  web::http::http_response resp(status);
  resp.headers().set_content_type(utility::conversions::to_string_t("application/json"));
  resp.set_body(utility::conversions::to_string_t(json_str));
  return resp;
}

}  // namespace

int main() {
  std::cout << "[XYO SDK Tests] Starting test suite...\n";

  // ---------------------------------------------------------------------------
  // 1. ClientConfig and Error tests
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] ClientConfig and Error basics\n";
    xyo::ClientConfig default_cfg;
    TEST_ASSERT(default_cfg.base_url == "https://api.xyo.financial");
    TEST_ASSERT(default_cfg.connect_timeout_ms == 5000);
    TEST_ASSERT(default_cfg.request_timeout_ms == 30000);
    TEST_ASSERT(default_cfg.max_collection_size == 1000);

    xyo::ClientConfig custom_cfg("my-key", "https://custom.xyo.financial");
    TEST_ASSERT(custom_cfg.api_key == "my-key");
    TEST_ASSERT(custom_cfg.base_url == "https://custom.xyo.financial");

    // Move constructor & move assignment
    xyo::ClientConfig moved_cfg(std::move(custom_cfg));
    TEST_ASSERT(moved_cfg.api_key == "my-key");
    TEST_ASSERT(moved_cfg.base_url == "https://custom.xyo.financial");

    xyo::ClientConfig assigned_cfg;
    assigned_cfg = std::move(moved_cfg);
    TEST_ASSERT(assigned_cfg.api_key == "my-key");

    // Error class getters
    xyo::Error err(xyo::ErrorCategory::http, "test error message", 404, 12);
    TEST_ASSERT(err.category() == xyo::ErrorCategory::http);
    TEST_ASSERT(err.http_status_code() == 404);
    TEST_ASSERT(err.transport_code() == 12);
    TEST_ASSERT(std::string(err.what()) == "test error message");

    // to_string for EnrichmentStatus
    TEST_ASSERT(xyo::to_string(xyo::EnrichmentStatus::ready) == "READY");
    TEST_ASSERT(xyo::to_string(xyo::EnrichmentStatus::failed) == "FAILED");
    TEST_ASSERT(xyo::to_string(xyo::EnrichmentStatus::pending) == "PENDING");
    TEST_ASSERT(xyo::to_string(static_cast<xyo::EnrichmentStatus>(99)) == "UNKNOWN");
  }

  // ---------------------------------------------------------------------------
  // 2. Client construction & validation
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] Client construction validation\n";
    // Empty api_key must throw validation Error
    expects_error(xyo::ErrorCategory::validation, "api_key must not be empty", [] {
      xyo::Client client(xyo::ClientConfig("", "https://api.xyo.financial"));
    });

    // Valid construction
    xyo::Client client(xyo::ClientConfig("valid-token", "https://api.xyo.financial"));

    // Move operations for Client
    xyo::Client moved_client(std::move(client));
    xyo::Client assigned_client(xyo::ClientConfig("another-token"));
    assigned_client = std::move(moved_client);
  }

  // ---------------------------------------------------------------------------
  // Start local mock HTTP server for integration-style wrapper tests
  // ---------------------------------------------------------------------------
  int port = get_free_port();
  MockHttpServer server(port);
  server.start();

  const std::string test_api_key = "xyo-secret-test-bearer-token-12345";
  xyo::ClientConfig client_cfg(test_api_key, server.base_url());
  client_cfg.request_timeout_ms = 5000;
  xyo::Client client(std::move(client_cfg));

  // ---------------------------------------------------------------------------
  // 3. enrichTransaction - Valid input and full response parsing
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] enrichTransaction with full response\n";
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.method == "POST");
      TEST_ASSERT(req.path == "/v1/ai/finance/enrichment/transaction");
      TEST_ASSERT(req.authorization_header == "Bearer xyo-secret-test-bearer-token-12345");
      TEST_ASSERT(req.content_type.find("application/json") != std::string::npos);

      // Verify request payload
      auto json_body = web::json::value::parse(utility::conversions::to_string_t(req.body));
      TEST_ASSERT(json_body.has_field(utility::conversions::to_string_t("content")));
      TEST_ASSERT(json_body[utility::conversions::to_string_t("content")].as_string() ==
                  utility::conversions::to_string_t("Costa Coffee Oxford St"));
      TEST_ASSERT(json_body.has_field(utility::conversions::to_string_t("countryCode")));
      TEST_ASSERT(json_body[utility::conversions::to_string_t("countryCode")].as_string() ==
                  utility::conversions::to_string_t("GB"));

      return json_response(web::http::status_codes::OK,
                           R"({
                             "merchant": "Costa Coffee",
                             "description": "British coffeehouse chain",
                             "categories": ["Food & Drink", "Coffee Shops", "Quick Service"],
                             "logo": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAA...",
                             "location": "London, UK",
                             "address": "123 Oxford Street, London W1D 2HG"
                           })");
    });

    xyo::EnrichmentRequest req;
    req.content = "Costa Coffee Oxford St";
    req.country_code = "GB";

    xyo::EnrichmentResponse resp = client.enrichTransaction(req);
    TEST_ASSERT(resp.merchant == "Costa Coffee");
    TEST_ASSERT(resp.description == "British coffeehouse chain");
    TEST_ASSERT(resp.categories.size() == 3);
    TEST_ASSERT(resp.categories[0] == "Food & Drink");
    TEST_ASSERT(resp.categories[1] == "Coffee Shops");
    TEST_ASSERT(resp.categories[2] == "Quick Service");
    TEST_ASSERT(resp.logo == "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAA...");
    TEST_ASSERT(resp.location.has_value());
    TEST_ASSERT(resp.location.value() == "London, UK");
    TEST_ASSERT(resp.address.has_value());
    TEST_ASSERT(resp.address.value() == "123 Oxford Street, London W1D 2HG");

    auto requests = server.get_requests();
    TEST_ASSERT(requests.size() == 1);
  }

  // ---------------------------------------------------------------------------
  // 4. enrichTransaction - Optional fields absent
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] enrichTransaction with optional fields absent\n";
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::OK,
                           R"({
                             "merchant": "Online Subscription",
                             "description": "SaaS service",
                             "categories": ["Software"],
                             "logo": "base64logo"
                           })");
    });

    xyo::EnrichmentResponse resp = client.enrichTransaction({"Online SaaS", "US"});
    TEST_ASSERT(resp.merchant == "Online Subscription");
    TEST_ASSERT(resp.description == "SaaS service");
    TEST_ASSERT(resp.categories.size() == 1);
    TEST_ASSERT(resp.categories[0] == "Software");
    TEST_ASSERT(resp.logo == "base64logo");
    TEST_ASSERT(!resp.location.has_value());
    TEST_ASSERT(!resp.address.has_value());
  }

  // ---------------------------------------------------------------------------
  // 5. enrichTransaction - HTTP error category mapping
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] enrichTransaction HTTP error category mapping\n";
    // 400 Bad Request
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::BadRequest,
                           R"({"error": "invalid content parameter"})");
    });

    try {
      client.enrichTransaction({"bad-input", "GB"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 400);
      TEST_ASSERT(std::string(e.what()).find("HTTP error from enrichTransaction") != std::string::npos);
    }

    // 401 Unauthorized
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::Unauthorized,
                           R"({"error": "invalid bearer token"})");
    });
    try {
      client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 401);
    }

    // 404 Not Found
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::NotFound,
                           R"({"error": "endpoint not found"})");
    });
    try {
      client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 404);
    }

    // 500 Internal Server Error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::InternalError,
                           R"({"error": "server crash"})");
    });
    try {
      client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 500);
    }
  }

  // ---------------------------------------------------------------------------
  // 6. enrichTransactions - Bulk operation
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] enrichTransactions bulk operation\n";
    // Empty vector validation check
    expects_error(xyo::ErrorCategory::validation,
                  "enrichTransactions: request list must not be empty", [&] {
                    client.enrichTransactions({});
                  });

    // Valid bulk request
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.method == "POST");
      TEST_ASSERT(req.path == "/v1/ai/finance/enrichment/transactions");
      TEST_ASSERT(req.authorization_header == "Bearer xyo-secret-test-bearer-token-12345");

      auto json_body = web::json::value::parse(utility::conversions::to_string_t(req.body));
      TEST_ASSERT(json_body.is_array());
      TEST_ASSERT(json_body.as_array().size() == 2);

      return json_response(web::http::status_codes::OK,
                           R"({
                             "id": "job-bulk-98765",
                             "link": "https://api.xyo.financial/downloads/results-98765.tar.gz"
                           })");
    });

    std::vector<xyo::EnrichmentRequest> bulk_reqs = {
        {"Pret A Manger London", "GB"},
        {"Whole Foods Austin", "US"}};

    xyo::BulkEnrichmentResponse bulk_resp = client.enrichTransactions(bulk_reqs);
    TEST_ASSERT(bulk_resp.id == "job-bulk-98765");
    TEST_ASSERT(bulk_resp.link == "https://api.xyo.financial/downloads/results-98765.tar.gz");

    // HTTP 422 Bulk Error mapping
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(static_cast<web::http::status_code>(422),
                           R"({"error": "batch size exceeds limit"})");
    });
    try {
      client.enrichTransactions(bulk_reqs);
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 422);
    }
  }

  // ---------------------------------------------------------------------------
  // 7. getEnrichmentStatus - Status polling
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] getEnrichmentStatus status polling\n";
    // Empty id validation check
    expects_error(xyo::ErrorCategory::validation,
                  "getEnrichmentStatus: id must not be empty", [&] {
                    client.getEnrichmentStatus("");
                  });

    // PENDING state
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.method == "GET");
      TEST_ASSERT(req.path == "/v1/ai/finance/enrichment/status/job-bulk-98765");
      TEST_ASSERT(req.authorization_header == "Bearer xyo-secret-test-bearer-token-12345");
      return json_response(web::http::status_codes::OK, R"({"status": "PENDING"})");
    });
    TEST_ASSERT(client.getEnrichmentStatus("job-bulk-98765") == xyo::EnrichmentStatus::pending);

    // READY state
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::OK, R"({"status": "READY"})");
    });
    TEST_ASSERT(client.getEnrichmentStatus("job-bulk-98765") == xyo::EnrichmentStatus::ready);

    // FAILED state
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::OK, R"({"status": "FAILED"})");
    });
    TEST_ASSERT(client.getEnrichmentStatus("job-bulk-98765") == xyo::EnrichmentStatus::failed);

    // Unrecognised status string mapping
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::OK, R"({"status": "UNKNOWN_CUSTOM_STATE"})");
    });
    expects_error(xyo::ErrorCategory::parsing, "unrecognised status value", [&] {
      client.getEnrichmentStatus("job-bulk-98765");
    });

    // HTTP 404 Job Not Found error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(web::http::status_codes::NotFound, R"({"error": "Job not found"})");
    });
    try {
      client.getEnrichmentStatus("nonexistent-job");
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 404);
    }
  }

  // ---------------------------------------------------------------------------
  // 8. Transport error handling (unreachable server)
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] Transport error handling\n";
    // Point client to a port with no listening server
    int unreachable_port = get_free_port();
    xyo::Client unreachable_client(
        xyo::ClientConfig("valid-key", "http://127.0.0.1:" + std::to_string(unreachable_port)));

    try {
      unreachable_client.enrichTransaction({"test", "GB"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::transport);
      TEST_ASSERT(e.what() != nullptr);
    }
  }

  server.stop();
  std::cout << "[XYO SDK Tests] All tests passed successfully!\n";
  return 0;
}

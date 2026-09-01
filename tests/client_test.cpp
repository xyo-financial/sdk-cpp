// Copyright 2026 Syniol Limited
// SPDX-License-Identifier: Apache-2.0

#include "xyo/client.hpp"

#include <nlohmann/json.hpp>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include <zlib.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
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
    std::cerr << "Expected error with category " << static_cast<int>(expected_category)
              << " and message containing '" << expected_message_substring
              << "' but no exception was thrown.\n";
    std::exit(1);
  } catch (const xyo::Error& e) {
    if (e.category() != expected_category) {
      std::cerr << "Category mismatch: expected category " << static_cast<int>(expected_category)
                << " but got category " << static_cast<int>(e.category())
                << " with message: " << e.what() << "\n";
    }
    TEST_ASSERT(e.category() == expected_category);
    TEST_ASSERT(e.what() != nullptr);
    std::string msg(e.what());
    if (!expected_message_substring.empty()) {
      if (msg.find(expected_message_substring) == std::string::npos) {
        std::cerr << "Message substring mismatch: expected to find '"
                  << expected_message_substring << "' in '" << msg << "'\n";
      }
      TEST_ASSERT(msg.find(expected_message_substring) != std::string::npos);
    }
  }
}

int get_free_port() {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) return 19876;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(sock);
    return 19876;
  }
  int len = sizeof(addr);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    closesocket(sock);
    return 19876;
  }
  int port = ntohs(addr.sin_port);
  closesocket(sock);
  return port;
#else
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
#endif
}

struct HttpResponse {
  int status_code = 200;
  std::string content_type = "application/json";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

inline HttpResponse json_response(int status, const std::string& json_str) {
  return HttpResponse{status, "application/json", json_str, {}};
}

inline HttpResponse gzip_response(int status, const std::vector<uint8_t>& data) {
  return HttpResponse{status, "application/gzip", std::string(reinterpret_cast<const char*>(data.data()), data.size()), {}};
}

class MockHttpServer {
 public:
  struct RecordedRequest {
    std::string method;
    std::string path;
    std::string authorization_header;
    std::string accept_header;
    std::string content_type;
    std::string x_correlation_id;
    std::string traceparent;
    std::string body;
  };

  using Handler = std::function<HttpResponse(const RecordedRequest&)>;

  explicit MockHttpServer(int port) : port_(port), running_(false) {}

  ~MockHttpServer() {
    stop();
  }

  void start() {
    running_ = true;
    server_thread_ = std::thread(&MockHttpServer::run_server, this);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void stop() {
    if (running_) {
      running_ = false;
#ifdef _WIN32
      if (server_fd_ != INVALID_SOCKET) {
        shutdown(server_fd_, SD_BOTH);
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
      }
      SOCKET dummy_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (dummy_sock != INVALID_SOCKET) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        connect(dummy_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        closesocket(dummy_sock);
      }
#else
      if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
      }
      int dummy_sock = socket(AF_INET, SOCK_STREAM, 0);
      if (dummy_sock >= 0) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        connect(dummy_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        close(dummy_sock);
      }
#endif
      if (server_thread_.joinable()) {
        server_thread_.join();
      }
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
  void run_server() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    server_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (server_fd_ < 0) return;

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
      closesocket(server_fd_);
#else
      close(server_fd_);
#endif
      return;
    }

    if (listen(server_fd_, 10) != 0) return;

    while (running_) {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
#ifdef _WIN32
      SOCKET client_sock = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
      if (client_sock == INVALID_SOCKET) break;
#else
      int client_sock = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
      if (client_sock < 0) break;
#endif

      std::string raw_req;
      char buf[4096];
      while (true) {
        int n = recv(client_sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw_req.append(buf, n);
        auto header_end = raw_req.find("\r\n\r\n");
        if (header_end != std::string::npos) {
          auto cl_pos = raw_req.find("Content-Length: ");
          if (cl_pos == std::string::npos) cl_pos = raw_req.find("content-length: ");
          if (cl_pos != std::string::npos) {
            auto cl_end = raw_req.find("\r\n", cl_pos);
            int cl = std::stoi(raw_req.substr(cl_pos + 16, cl_end - (cl_pos + 16)));
            if (raw_req.size() >= header_end + 4 + cl) {
              break;
            }
          } else {
            break;
          }
        }
      }

      if (raw_req.empty()) {
#ifdef _WIN32
        closesocket(client_sock);
#else
        close(client_sock);
#endif
        continue;
      }

      RecordedRequest rec;
      auto header_end = raw_req.find("\r\n\r\n");
      std::string header_part = raw_req.substr(0, header_end);
      if (header_end != std::string::npos && header_end + 4 < raw_req.size()) {
        rec.body = raw_req.substr(header_end + 4);
      }

      std::istringstream stream(header_part);
      std::string req_line;
      if (std::getline(stream, req_line)) {
        if (!req_line.empty() && req_line.back() == '\r') req_line.pop_back();
        std::istringstream req_stream(req_line);
        std::string method, path;
        req_stream >> method >> path;
        rec.method = method;
        rec.path = path;
      }

      std::string line;
      while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
          std::string name = line.substr(0, colon);
          std::string val = line.substr(colon + 1);
          while (!val.empty() && val.front() == ' ') val.erase(val.begin());
          std::string lower_name = name;
          std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if (lower_name == "authorization") rec.authorization_header = val;
          else if (lower_name == "accept") rec.accept_header = val;
          else if (lower_name == "content-type") rec.content_type = val;
          else if (lower_name == "x-correlation-id") rec.x_correlation_id = val;
          else if (lower_name == "traceparent") rec.traceparent = val;
        }
      }

      HttpResponse resp;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        recorded_requests_.push_back(rec);
        if (handler_) {
          resp = handler_(rec);
        } else {
          resp.status_code = 404;
        }
      }

      std::string status_msg = "OK";
      if (resp.status_code == 400) status_msg = "Bad Request";
      else if (resp.status_code == 401) status_msg = "Unauthorized";
      else if (resp.status_code == 404) status_msg = "Not Found";
      else if (resp.status_code == 422) status_msg = "Unprocessable Entity";
      else if (resp.status_code == 429) status_msg = "Too Many Requests";
      else if (resp.status_code == 500) status_msg = "Internal Server Error";

      std::string raw_resp = "HTTP/1.1 " + std::to_string(resp.status_code) + " " + status_msg + "\r\n";
      raw_resp += "Content-Type: " + resp.content_type + "\r\n";
      raw_resp += "Content-Length: " + std::to_string(resp.body.size()) + "\r\n";
      for (const auto& h : resp.headers) {
        raw_resp += h.first + ": " + h.second + "\r\n";
      }
      raw_resp += "Connection: close\r\n\r\n";
      raw_resp += resp.body;

      send(client_sock, raw_resp.data(), static_cast<int>(raw_resp.size()), 0);
#ifdef _WIN32
      closesocket(client_sock);
#else
      close(client_sock);
#endif
    }
  }

  int port_;
  std::atomic<bool> running_;
#ifdef _WIN32
  SOCKET server_fd_ = INVALID_SOCKET;
#else
  int server_fd_ = -1;
#endif
  std::thread server_thread_;
  std::mutex mutex_;
  Handler handler_;
  std::vector<RecordedRequest> recorded_requests_;
};

/// Helper to create a POSIX ustar tar archive in-memory
std::string create_tar_archive(const std::vector<std::pair<std::string, std::string>>& files) {
  std::string tar;
  for (const auto& item : files) {
    const std::string& name = item.first;
    const std::string& content = item.second;

    char hdr[512] = {};
    std::strncpy(hdr, name.c_str(), std::min<std::size_t>(name.size(), 99));
    std::snprintf(hdr + 100, 8, "%07o", 0644);
    std::snprintf(hdr + 108, 8, "%07o", 0);
    std::snprintf(hdr + 116, 8, "%07o", 0);
    std::snprintf(hdr + 124, 12, "%011o", static_cast<unsigned int>(content.size()));
    std::snprintf(hdr + 136, 12, "%011lo", 0L);
    hdr[156] = '0';
    std::memcpy(hdr + 257, "ustar ", 6);
    std::memcpy(hdr + 263, " \0", 2);

    std::memset(hdr + 148, ' ', 8);
    unsigned int checksum = 0;
    for (int i = 0; i < 512; ++i) {
      checksum += static_cast<unsigned char>(hdr[i]);
    }
    std::snprintf(hdr + 148, 8, "%06o", checksum);

    tar.append(hdr, 512);
    tar.append(content);
    std::size_t pad = (512 - (content.size() % 512)) % 512;
    if (pad > 0) {
      tar.append(pad, '\0');
    }
  }
  tar.append(1024, '\0');
  return tar;
}

/// Helper to compress data using gzip (windowBits = 31)
std::vector<uint8_t> gzip_compress(const std::string& data) {
  z_stream zs{};
  if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    throw std::runtime_error("deflateInit2 failed");
  }

  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());

  std::vector<uint8_t> out;
  std::vector<uint8_t> buf(65536);
  int ret = Z_OK;
  do {
    zs.next_out = buf.data();
    zs.avail_out = static_cast<uInt>(buf.size());
    ret = deflate(&zs, Z_FINISH);
    out.insert(out.end(), buf.data(), buf.data() + (buf.size() - zs.avail_out));
  } while (ret == Z_OK);

  deflateEnd(&zs);
  if (ret != Z_STREAM_END) {
    throw std::runtime_error("deflate failed");
  }
  return out;
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
    TEST_ASSERT(default_cfg.max_collection_size == 50000);

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
    xyo::Error err(xyo::ErrorCategory::http, "test error message", 404, 12,
                   std::nullopt, "corr-123", "https://xyo.financial/errors/not_found",
                   "Resource Not Found", "The requested record was missing", "/transactions/1");
    TEST_ASSERT(err.category() == xyo::ErrorCategory::http);
    TEST_ASSERT(err.http_status_code() == 404);
    TEST_ASSERT(err.transport_code() == 12);
    TEST_ASSERT(std::string(err.what()) == "test error message");
    TEST_ASSERT(!err.rate_limit_info().has_value());
    TEST_ASSERT(err.correlation_id().value_or("") == "corr-123");
    TEST_ASSERT(err.problem_type() == "https://xyo.financial/errors/not_found");
    TEST_ASSERT(err.problem_title() == "Resource Not Found");
    TEST_ASSERT(err.problem_detail() == "The requested record was missing");
    TEST_ASSERT(err.problem_instance() == "/transactions/1");

    // to_string for EnrichmentStatus and ErrorCategory
    TEST_ASSERT(xyo::to_string(xyo::EnrichmentStatus::ready) == "READY");
    TEST_ASSERT(xyo::to_string(xyo::EnrichmentStatus::failed) == "FAILED");
    TEST_ASSERT(xyo::to_string(xyo::EnrichmentStatus::pending) == "PENDING");
    TEST_ASSERT(xyo::to_string(static_cast<xyo::EnrichmentStatus>(99)) == "UNKNOWN");

    TEST_ASSERT(xyo::to_string(xyo::ErrorCategory::validation) == "validation");
    TEST_ASSERT(xyo::to_string(xyo::ErrorCategory::transport) == "transport");
    TEST_ASSERT(xyo::to_string(xyo::ErrorCategory::http) == "http");
    TEST_ASSERT(xyo::to_string(xyo::ErrorCategory::parsing) == "parsing");
    TEST_ASSERT(xyo::to_string(xyo::ErrorCategory::rate_limit) == "rate_limit");
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

    // API key with trailing newline must throw validation Error (C5)
    expects_error(xyo::ErrorCategory::validation, "check for a trailing newline", [] {
      xyo::Client client(xyo::ClientConfig("my-key\n", "https://api.xyo.financial"));
    });

    // Plaintext HTTP base_url without allow_insecure_transport must throw validation Error (C4)
    expects_error(xyo::ErrorCategory::validation, "base_url must use https", [] {
      xyo::Client client(xyo::ClientConfig("my-key", "http://insecure.xyo.financial"));
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
  xyo::ClientConfig client_cfg(test_api_key, server.base_url(), true);
  client_cfg.request_timeout_ms = 5000;
  client_cfg.max_collection_size = 5;
  xyo::Client client(std::move(client_cfg));

  // ---------------------------------------------------------------------------
  // 2b. Request validation tests (validate_request & max_collection_size)
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] Request input validation\n";
    // Empty content
    expects_error(xyo::ErrorCategory::validation, "request content must not be empty", [&] {
      (void)client.enrichTransaction({"", "US"});
    });

    // Content > 128 chars
    std::string long_content(129, 'A');
    expects_error(xyo::ErrorCategory::validation, "request content exceeds maximum length of 128 characters", [&] {
      (void)client.enrichTransaction({long_content, "US"});
    });

    // Empty country code
    expects_error(xyo::ErrorCategory::validation, "request country_code must not be empty", [&] {
      (void)client.enrichTransaction({"Valid transaction", ""});
    });

    // Country code != 2 characters
    expects_error(xyo::ErrorCategory::validation, "request country_code must be a 2-letter ISO 3166-1 alpha-2 code", [&] {
      (void)client.enrichTransaction({"Valid transaction", "USA"});
    });

    // Country code containing non-alpha characters (S13)
    expects_error(xyo::ErrorCategory::validation, "request country_code must be a 2-letter ISO 3166-1 alpha-2 code", [&] {
      (void)client.enrichTransaction({"Valid transaction", "12"});
    });
    expects_error(xyo::ErrorCategory::validation, "request country_code must be a 2-letter ISO 3166-1 alpha-2 code", [&] {
      (void)client.enrichTransaction({"Valid transaction", "G!"});
    });

    // Arabic UTF-8 multi-byte character count (S3): 90 Arabic chars (~180 bytes) is valid <= 128 chars
    std::string arabic_90_chars = "سوق مرجان دبي التجاري الدولي للخدمات المالية السريعة وتجارة التجزئة والحلول المصرفية المتطورة";
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(200, R"({"merchant":"Marjan","description":"Dubai","categories":[]})");
    });
    {
      auto res = client.enrichTransaction({arabic_90_chars, "ae"});
      TEST_ASSERT(res.merchant == "Marjan");
    }

    // Batch item validation
    expects_error(xyo::ErrorCategory::validation, "request content must not be empty", [&] {
      (void)client.enrichTransactions({{"Valid 1", "US"}, {"", "GB"}});
    });

    // Batch exceeding max_collection_size (configured as 5)
    std::vector<xyo::EnrichmentRequest> oversized_batch(6, {"Tx", "US"});
    expects_error(xyo::ErrorCategory::validation, "exceeds configured max_collection_size", [&] {
      (void)client.enrichTransactions(oversized_batch);
    });
  }

  // ---------------------------------------------------------------------------
  // 2c. Batch array bounds validation (1 to 50,000 items)
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] Batch array bounds validation (1 to 50,000 items)\n";
    // 0 items
    expects_error(xyo::ErrorCategory::validation, "must not be empty", [&] {
      (void)client.enrichTransactions({});
    });

    // > 50,000 items
    std::vector<xyo::EnrichmentRequest> oversized_batch(50001, {"Tx", "US"});
    expects_error(xyo::ErrorCategory::validation, "exceeds maximum limit of 50000 items", [&] {
      (void)client.enrichTransactions(oversized_batch);
    });
  }

  // ---------------------------------------------------------------------------
  // 2d. Distributed Tracing Headers (x_correlation_id & traceparent)
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] Distributed Tracing Headers\n";
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.x_correlation_id == "corr-xyz-789");
      TEST_ASSERT(req.traceparent == "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");

      return json_response(200,
                           R"({
                             "merchant": "Tracing Merchant",
                             "description": "Tracing Description",
                             "categories": ["Tech"],
                             "logo": "data:image/png;base64,123"
                           })");
    });

    xyo::EnrichmentRequestOptions opts;
    opts.x_correlation_id = "corr-xyz-789";
    opts.traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";

    xyo::EnrichmentResponse resp = client.enrichTransaction({"Transaction with trace", "US"}, opts);
    TEST_ASSERT(resp.merchant == "Tracing Merchant");

    auto requests = server.get_requests();
    TEST_ASSERT(requests.size() == 1);
    TEST_ASSERT(requests[0].x_correlation_id == "corr-xyz-789");
    TEST_ASSERT(requests[0].traceparent == "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");

    // Header injection validation tests
    xyo::EnrichmentRequestOptions bad_corr;
    bad_corr.x_correlation_id = "header\r\ninjection";
    expects_error(xyo::ErrorCategory::validation, "x_correlation_id contains invalid header characters", [&] {
      (void)client.enrichTransaction({"Tx", "US"}, bad_corr);
    });

    xyo::EnrichmentRequestOptions bad_trace;
    bad_trace.traceparent = "traceparent\nval";
    expects_error(xyo::ErrorCategory::validation, "traceparent contains invalid header characters", [&] {
      (void)client.enrichTransaction({"Tx", "US"}, bad_trace);
    });
  }

  // ---------------------------------------------------------------------------
  // 2e. HTTP 429 Rate Limit error handling & header parsing
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] HTTP 429 Rate Limit error handling & header parsing\n";
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      HttpResponse resp = json_response(429, R"({"error": "Too Many Requests", "message": "Rate limit exceeded"})");
      resp.headers = {
          {"Retry-After", "30"},
          {"RateLimit-Limit", "100"},
          {"RateLimit-Remaining", "0"},
          {"RateLimit-Reset", "2524608000"} // Y2K38 epoch test (>2^31-1)
      };
      return resp;
    });

    try {
      (void)client.enrichTransaction({"RateLimit Test", "GB"});
      TEST_ASSERT(false);
    } catch (const xyo::XyoException& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::rate_limit);
      TEST_ASSERT(e.http_status_code() == 429);
      TEST_ASSERT(e.rate_limit_info().has_value());
      const auto& info = e.rate_limit_info().value();
      TEST_ASSERT(info.retry_after.has_value() && info.retry_after.value() == 30);
      TEST_ASSERT(info.limit.has_value() && info.limit.value() == 100);
      TEST_ASSERT(info.remaining.has_value() && info.remaining.value() == 0);
      TEST_ASSERT(info.reset.has_value() && info.reset.value() == 2524608000LL);
    }
  }

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
      auto json_body = nlohmann::json::parse(req.body);
      TEST_ASSERT(json_body.contains("content"));
      TEST_ASSERT(json_body["content"].get<std::string>() == "Costa Coffee Oxford St");
      TEST_ASSERT(json_body.contains("countryCode"));
      TEST_ASSERT(json_body["countryCode"].get<std::string>() == "GB");

      return json_response(200,
                           R"({
                             "merchant": "Costa Coffee",
                             "description": "British coffeehouse chain",
                             "categories": ["Food & Drink", "Coffee Shops", "Quick Service"],
                             "logo": "data:image/png;base64,iVBORw0KGgoAAA...",
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
    TEST_ASSERT(resp.logo == "data:image/png;base64,iVBORw0KGgoAAA...");
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
      return json_response(200,
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
      return json_response(400, R"({"error": "invalid content parameter"})");
    });

    try {
      (void)client.enrichTransaction({"bad-input", "GB"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 400);
      TEST_ASSERT(std::string(e.what()).find("HTTP error from enrichTransaction") != std::string::npos);
    }

    // 401 Unauthorized
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(401, R"({"error": "invalid bearer token"})");
    });
    try {
      (void)client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 401);
    }

    // 404 Not Found
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(404, R"({"error": "endpoint not found"})");
    });
    try {
      (void)client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 404);
    }

    // 500 Internal Server Error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(500, R"({"error": "server crash"})");
    });
    try {
      (void)client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 500);
    }

    // RFC 7807 Problem Details
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(422, R"({"type": "https://example.com/probs/out-of-credit", "title": "You do not have enough credit.", "detail": "Your current balance is 30, but that costs 50."})");
    });
    try {
      (void)client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 422);
      std::string msg(e.what());
      TEST_ASSERT(msg.find("You do not have enough credit.") != std::string::npos);
      TEST_ASSERT(msg.find("Your current balance is 30, but that costs 50.") != std::string::npos);
    }
  }

  // ---------------------------------------------------------------------------
  // 6. enrichTransactions - Bulk operation
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] enrichTransactions bulk operation\n";
    // Empty vector validation check
    expects_error(xyo::ErrorCategory::validation,
                  "must not be empty", [&] {
                    (void)client.enrichTransactions({});
                  });

    // Valid bulk request
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.method == "POST");
      TEST_ASSERT(req.path == "/v1/ai/finance/enrichment/transactions");
      TEST_ASSERT(req.authorization_header == "Bearer xyo-secret-test-bearer-token-12345");

      auto json_body = nlohmann::json::parse(req.body);
      TEST_ASSERT(json_body.is_array());
      TEST_ASSERT(json_body.size() == 2);

      return json_response(200,
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

    // Client-side batch size exceeds max_collection_size validation check
    xyo::ClientConfig limited_cfg(test_api_key, server.base_url(), true);
    limited_cfg.max_collection_size = 1;
    xyo::Client limited_client(std::move(limited_cfg));
    expects_error(xyo::ErrorCategory::validation,
                  "exceeds configured max_collection_size", [&] {
                    (void)limited_client.enrichTransactions(bulk_reqs);
                  });

    // HTTP 422 Bulk Error mapping
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(422, R"({"error": "batch size exceeds limit"})");
    });
    try {
      (void)client.enrichTransactions(bulk_reqs);
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
                    (void)client.getEnrichmentStatus("");
                  });

    // Job ID injection rejection (C3)
    expects_error(xyo::ErrorCategory::validation,
                  "characters that are not permitted in a job identifier", [&] {
                    (void)client.getEnrichmentStatus("../../../admin/keys");
                  });

    // PENDING state
    server.clear_requests();
    server.set_handler([](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.method == "GET");
      TEST_ASSERT(req.path == "/v1/ai/finance/enrichment/status/job-bulk-98765");
      TEST_ASSERT(req.authorization_header == "Bearer xyo-secret-test-bearer-token-12345");
      return json_response(200, R"({"status": "PENDING"})");
    });
    TEST_ASSERT(client.getEnrichmentStatus("job-bulk-98765") == xyo::EnrichmentStatus::pending);

    // READY state
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(200, R"({"status": "READY"})");
    });
    TEST_ASSERT(client.getEnrichmentStatus("job-bulk-98765") == xyo::EnrichmentStatus::ready);

    // FAILED state
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(200, R"({"status": "FAILED"})");
    });
    TEST_ASSERT(client.getEnrichmentStatus("job-bulk-98765") == xyo::EnrichmentStatus::failed);

    // Unrecognised status string mapping
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(200, R"({"status": "UNKNOWN_CUSTOM_STATE"})");
    });
    expects_error(xyo::ErrorCategory::parsing, "unrecognised status value", [&] {
      (void)client.getEnrichmentStatus("job-bulk-98765");
    });

    // HTTP 404 Job Not Found error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(404, R"({"error": "Job not found"})");
    });
    try {
      (void)client.getEnrichmentStatus("nonexistent-job");
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
    int unreachable_port = get_free_port();
    xyo::Client unreachable_client(
        xyo::ClientConfig("valid-key", "http://127.0.0.1:" + std::to_string(unreachable_port), true));

    try {
      (void)unreachable_client.enrichTransaction({"test", "GB"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::transport);
      TEST_ASSERT(e.what() != nullptr);
    }
  }

  // ---------------------------------------------------------------------------
  // 9. downloadEnrichmentCollection - Valid tar.gz download and parsing
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] downloadEnrichmentCollection valid tar.gz responses\n";
    std::string json1 = R"({
      "merchant": "Uber",
      "description": "Ridesharing and mobility platform",
      "categories": ["Transportation", "Rideshare"],
      "logo": "data:image/png;base64,iVBORw0KGgoAAA...",
      "location": "San Francisco, CA",
      "address": "1455 Market St"
    })";

    std::string json2 = R"({
      "merchant": "Netflix",
      "description": "Subscription video streaming service",
      "categories": ["Entertainment", "Streaming"],
      "logo": "data:image/png;base64,netflixlogo..."
    })";

    std::string json3 = R"({
      "merchant": "Tesco Stores",
      "description": "Supermarket and retail chain",
      "categories": ["Groceries", "Food & Drink"],
      "logo": "data:image/png;base64,tescologo...",
      "location": "Welwyn Garden City, UK",
      "address": null
    })";

    std::string tar = create_tar_archive({
        {"result_001.json", json1},
        {"result_002.json", json2},
        {"result_003.json", json3},
    });
    std::vector<uint8_t> gzipped = gzip_compress(tar);

    server.clear_requests();
    server.set_handler([&gzipped](const MockHttpServer::RecordedRequest& req) {
      TEST_ASSERT(req.method == "GET");
      TEST_ASSERT(req.path == "/downloads/results-98765.tar.gz");
      TEST_ASSERT(req.accept_header.find("application/gzip") != std::string::npos);
      return gzip_response(200, gzipped);
    });

    // 9a. Full URL download
    std::string download_url = server.base_url() + "/downloads/results-98765.tar.gz";
    std::vector<xyo::EnrichmentResponse> items = client.downloadEnrichmentCollection(download_url);
    TEST_ASSERT(items.size() == 3);

    // Entry 1: Full payload
    TEST_ASSERT(items[0].merchant == "Uber");
    TEST_ASSERT(items[0].description == "Ridesharing and mobility platform");
    TEST_ASSERT(items[0].categories.size() == 2);
    TEST_ASSERT(items[0].categories[0] == "Transportation");
    TEST_ASSERT(items[0].categories[1] == "Rideshare");
    TEST_ASSERT(items[0].logo == "data:image/png;base64,iVBORw0KGgoAAA...");
    TEST_ASSERT(items[0].location.has_value());
    TEST_ASSERT(items[0].location.value() == "San Francisco, CA");
    TEST_ASSERT(items[0].address.has_value());
    TEST_ASSERT(items[0].address.value() == "1455 Market St");

    // Entry 2: Optional location and address absent
    TEST_ASSERT(items[1].merchant == "Netflix");
    TEST_ASSERT(items[1].description == "Subscription video streaming service");
    TEST_ASSERT(items[1].categories.size() == 2);
    TEST_ASSERT(items[1].categories[0] == "Entertainment");
    TEST_ASSERT(items[1].categories[1] == "Streaming");
    TEST_ASSERT(items[1].logo == "data:image/png;base64,netflixlogo...");
    TEST_ASSERT(!items[1].location.has_value());
    TEST_ASSERT(!items[1].address.has_value());

    // Entry 3: Location present, address null
    TEST_ASSERT(items[2].merchant == "Tesco Stores");
    TEST_ASSERT(items[2].description == "Supermarket and retail chain");
    TEST_ASSERT(items[2].categories.size() == 2);
    TEST_ASSERT(items[2].categories[0] == "Groceries");
    TEST_ASSERT(items[2].categories[1] == "Food & Drink");
    TEST_ASSERT(items[2].logo == "data:image/png;base64,tescologo...");
    TEST_ASSERT(items[2].location.has_value());
    TEST_ASSERT(items[2].location.value() == "Welwyn Garden City, UK");
    TEST_ASSERT(!items[2].address.has_value());

    // 9b. Relative URL resolution download
    std::vector<xyo::EnrichmentResponse> items_rel =
        client.downloadEnrichmentCollection("/downloads/results-98765.tar.gz");
    TEST_ASSERT(items_rel.size() == 3);
    TEST_ASSERT(items_rel[0].merchant == "Uber");
  }

  // ---------------------------------------------------------------------------
  // 10. downloadEnrichmentCollection - Validation and error handling
  // ---------------------------------------------------------------------------
  {
    std::cout << "[Test] downloadEnrichmentCollection validation and error handling\n";

    // 10a. Empty URL validation error
    expects_error(xyo::ErrorCategory::validation,
                  "downloadEnrichmentCollection: downloadUrl must not be empty", [&] {
                    (void)client.downloadEnrichmentCollection("");
                  });

    // 10b. HTTP 401 Unauthorized
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(401, R"({"error": "Unauthorized"})");
    });
    try {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/results-98765.tar.gz");
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 401);
    }

    // 10c. HTTP 404 Not Found
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(404, R"({"error": "Archive not found"})");
    });
    try {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/nonexistent.tar.gz");
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 404);
    }

    // 10d. HTTP 500 Internal Server Error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return json_response(500, R"({"error": "Internal server error"})");
    });
    try {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/error.tar.gz");
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::http);
      TEST_ASSERT(e.http_status_code() == 500);
    }

    // 10e. Empty body parsing error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return HttpResponse{200, "application/gzip", "", {}};
    });
    expects_error(xyo::ErrorCategory::parsing, "empty response body", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/empty.tar.gz");
    });

    // 10f. Corrupted gzip data
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      std::string corrupt = "this is not valid gzip compressed data at all";
      std::vector<uint8_t> data(corrupt.begin(), corrupt.end());
      return gzip_response(200, data);
    });
    expects_error(xyo::ErrorCategory::parsing, "gzip decompression failed", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/corrupted.tar.gz");
    });

    // 10g. Corrupted / truncated tar archive
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      char hdr[512] = {};
      std::strncpy(hdr, "entry.json", 100);
      std::snprintf(hdr + 100, 8, "%07o", 0644);
      std::snprintf(hdr + 124, 12, "%011llo", static_cast<unsigned long long>(1000));
      hdr[156] = '0';
      std::memset(hdr + 148, ' ', 8);
      unsigned int checksum = 0;
      for (int i = 0; i < 512; ++i) checksum += static_cast<unsigned char>(hdr[i]);
      std::snprintf(hdr + 148, 8, "%06o", checksum);

      std::string partial_tar(hdr, 512);
      partial_tar.append(100, 'x'); // only 100 bytes of 1000 bytes declared
      auto gz = gzip_compress(partial_tar);
      return gzip_response(200, gz);
    });
    expects_error(xyo::ErrorCategory::parsing, "truncated tar archive", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/truncated.tar.gz");
    });

    // 10h. Invalid JSON inside tar archive
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      std::string bad_json = "{ merchant: invalid json }";
      std::string tar = create_tar_archive({{"bad.json", bad_json}});
      auto gz = gzip_compress(tar);
      return gzip_response(200, gz);
    });
    expects_error(xyo::ErrorCategory::parsing, "JSON parse error", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/badjson.tar.gz");
    });

    // 10i. Missing field in JSON entry defaults to empty string
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      std::string incomplete_json = R"({"description": "no merchant"})";
      std::string tar = create_tar_archive({{"incomplete.json", incomplete_json}});
      auto gz = gzip_compress(tar);
      return gzip_response(200, gz);
    });
    {
      auto results = client.downloadEnrichmentCollection(server.base_url() + "/downloads/incomplete.tar.gz");
      TEST_ASSERT(results.size() == 1);
      TEST_ASSERT(results[0].merchant == "");
      TEST_ASSERT(results[0].description == "no merchant");
    }

    // 10j. Transport error for unreachable host
    int unreachable_port = get_free_port();
    xyo::Client unreachable_client(xyo::ClientConfig("key", "http://127.0.0.1:" + std::to_string(unreachable_port), true));
    try {
      (void)unreachable_client.downloadEnrichmentCollection("http://127.0.0.1:" + std::to_string(unreachable_port) + "/downloads/file.tar.gz");
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::transport);
    }

    // 10k. Malformed URL throws validation error (spaces or userinfo @)
    expects_error(xyo::ErrorCategory::validation, "invalid URL format", [&] {
      (void)client.downloadEnrichmentCollection("http://invalid uri with spaces/downloads");
    });
    expects_error(xyo::ErrorCategory::validation, "invalid URL format", [&] {
      (void)client.downloadEnrichmentCollection("http://user:password@127.0.0.1/downloads");
    });

    // 10k2. Invalid port numbers throw bad port number validation error
    expects_error(xyo::ErrorCategory::validation, "invalid URL format: bad port number", [&] {
      (void)client.downloadEnrichmentCollection("http://127.0.0.1:0/downloads");
    });
    expects_error(xyo::ErrorCategory::validation, "invalid URL format: bad port number", [&] {
      (void)client.downloadEnrichmentCollection("http://127.0.0.1:65536/downloads");
    });
    expects_error(xyo::ErrorCategory::validation, "invalid URL format: bad port number", [&] {
      (void)client.downloadEnrichmentCollection("http://127.0.0.1:abc/downloads");
    });

    // 10l. Tar header checksum mismatch
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      char hdr[512] = {};
      std::strncpy(hdr, "corrupt_checksum.json", 100);
      std::snprintf(hdr + 100, 8, "%07o", 0644);
      std::snprintf(hdr + 124, 12, "%011llo", static_cast<unsigned long long>(10));
      hdr[156] = '0';
      std::snprintf(hdr + 148, 8, "%06o", 12345);

      std::string corrupt_tar(hdr, 512);
      corrupt_tar.append(512, '\0');
      auto gz = gzip_compress(corrupt_tar);
      return gzip_response(200, gz);
    });
    expects_error(xyo::ErrorCategory::parsing, "tar header checksum mismatch", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/bad_chk.tar.gz");
    });

    // 10m. WAF Security Challenge HTML Response
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return HttpResponse{200, "text/html; charset=UTF-8", "<html><body><h1>Cloudflare Security Challenge</h1></body></html>", {}};
    });
    expects_error(xyo::ErrorCategory::http, "unexpected Content-Type", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/waf.tar.gz");
    });

    // 10n. Untrusted rogue domain download is rejected
    expects_error(xyo::ErrorCategory::validation, "is not permitted for secure archive downloads", [&] {
      (void)client.downloadEnrichmentCollection("https://evil-untrusted-domain.com/downloads/job.tar.gz");
    });

    // 10o. ClientConfig environment variable fallback
    #ifdef _WIN32
    _putenv("XYO_API_BASE_URL=https://custom-env.xyo.financial");
    #else
    ::setenv("XYO_API_BASE_URL", "https://custom-env.xyo.financial", 1);
    #endif
    {
      xyo::ClientConfig env_cfg;
      TEST_ASSERT(env_cfg.base_url == "https://custom-env.xyo.financial");
      xyo::ClientConfig env_key_cfg("test-key");
      TEST_ASSERT(env_key_cfg.base_url == "https://custom-env.xyo.financial");
    }
    #ifdef _WIN32
    _putenv("XYO_API_BASE_URL=");
    #else
    ::unsetenv("XYO_API_BASE_URL");
    #endif

    // 10p. Tar Zip Slip / path traversal entry throws parsing error
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      std::string evil_json = R"({"merchant":"EvilCo","description":"Evil","logo":"url","categories":[]})";
      std::string tar = create_tar_archive({
          {"../../etc/passwd.json", evil_json}
      });
      auto gz = gzip_compress(tar);
      return gzip_response(200, gz);
    });
    expects_error(xyo::ErrorCategory::parsing, "path traversal detected", [&] {
      (void)client.downloadEnrichmentCollection(server.base_url() + "/downloads/zipslip.tar.gz");
    });

    // 10q. Multi-member gzip decompression (C6)
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      std::string json1 = R"({"merchant":"Member1","description":"Desc1","categories":[]})";
      std::string json2 = R"({"merchant":"Member2","description":"Desc2","categories":[]})";
      std::string tar1 = create_tar_archive({{"1.json", json1}});
      std::string tar2 = create_tar_archive({{"2.json", json2}});
      std::string gz1 = gzip_compress(tar1);
      std::string gz2 = gzip_compress(tar2);
      std::string multi_member_gz = gz1 + gz2;
      return gzip_response(200, multi_member_gz);
    });
    {
      auto results = client.downloadEnrichmentCollection(server.base_url() + "/downloads/multimember.tar.gz");
      TEST_ASSERT(results.size() == 2);
      TEST_ASSERT(results[0].merchant == "Member1");
      TEST_ASSERT(results[1].merchant == "Member2");
    }

    // 10q. Rate limiting with RFC 7231 / RFC 9110 HTTP-date Retry-After
    server.set_handler([](const MockHttpServer::RecordedRequest&) {
      return HttpResponse{429, "application/json", R"({"title":"Too Many Requests"})", {{"retry-after", "Wed, 21 Oct 2099 07:28:00 GMT"}, {"ratelimit-limit", "100"}}};
    });
    try {
      (void)client.enrichTransaction({"test", "US"});
      TEST_ASSERT(false);
    } catch (const xyo::Error& e) {
      TEST_ASSERT(e.category() == xyo::ErrorCategory::rate_limit);
      TEST_ASSERT(e.rate_limit_info().has_value());
      TEST_ASSERT(e.rate_limit_info()->retry_after.has_value());
      TEST_ASSERT(e.rate_limit_info()->retry_after.value() > 0);
      TEST_ASSERT(e.rate_limit_info()->limit == 100);
    }
  }

  server.stop();
  std::cout << "[XYO SDK Tests] All tests passed successfully!\n";
  return 0;
}

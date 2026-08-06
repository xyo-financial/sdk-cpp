#include "xyo/client.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

namespace xyo {
namespace {

struct Json {
  enum class Kind { null_value, string, scalar, array, object } kind = Kind::null_value;
  std::string text;
  std::vector<Json> array;
  std::map<std::string, Json> object;
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& input, std::size_t max_depth = 64, std::size_t max_nodes = 100'000)
      : input_(input), max_depth_(max_depth), max_nodes_(max_nodes) {}

  Json parse() {
    Json result = value();
    whitespace();
    if (position_ != input_.size()) fail("trailing content");
    return result;
  }

 private:
  const std::string& input_;
  std::size_t position_ = 0;
  std::size_t max_depth_;
  std::size_t max_nodes_;
  std::size_t current_depth_ = 0;
  std::size_t nodes_parsed_ = 0;

  [[noreturn]] void fail(const std::string& message) const {
    throw Error(ErrorCategory::parsing, "invalid JSON response at byte " + std::to_string(position_) +
                ": " + message);
  }

  void whitespace() {
    while (position_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
  }

  bool consume(char expected) {
    whitespace();
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void increment_nodes() {
    ++nodes_parsed_;
    if (nodes_parsed_ > max_nodes_) {
      fail("exceeded maximum JSON node limit of " + std::to_string(max_nodes_));
    }
  }

  void increment_depth() {
    ++current_depth_;
    if (current_depth_ > max_depth_) {
      fail("exceeded maximum JSON depth limit of " + std::to_string(max_depth_));
    }
  }

  void decrement_depth() {
    if (current_depth_ > 0) {
      --current_depth_;
    }
  }

  Json value() {
    increment_nodes();
    whitespace();
    if (position_ >= input_.size()) fail("expected a value");
    if (input_[position_] == '"') {
      Json result;
      result.kind = Json::Kind::string;
      result.text = string();
      return result;
    }
    if (input_[position_] == '{') return object();
    if (input_[position_] == '[') return array();
    if (input_.compare(position_, 4, "null") == 0) {
      position_ += 4;
      return {};
    }
    if (input_.compare(position_, 4, "true") == 0) return scalar(4);
    if (input_.compare(position_, 5, "false") == 0) return scalar(5);
    if (input_[position_] == '-' ||
        std::isdigit(static_cast<unsigned char>(input_[position_]))) return number();
    fail("unsupported value");
  }

  Json scalar(std::size_t length) {
    Json result;
    result.kind = Json::Kind::scalar;
    result.text = input_.substr(position_, length);
    position_ += length;
    return result;
  }

  Json number() {
    std::size_t start = position_;
    if (input_[position_] == '-') ++position_;
    if (position_ >= input_.size()) fail("invalid number");
    if (input_[position_] == '0') ++position_;
    else {
      if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) fail("invalid number");
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      if (position_ >= input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) fail("invalid number");
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
    }
    if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
        ++position_;
      if (position_ >= input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) fail("invalid number");
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
    }
    return scalar_from_range(start);
  }

  Json scalar_from_range(std::size_t start) {
    Json result;
    result.kind = Json::Kind::scalar;
    result.text = input_.substr(start, position_ - start);
    return result;
  }

  std::string string() {
    if (input_[position_++] != '"') fail("expected string");
    std::string result;
    while (position_ < input_.size()) {
      char c = input_[position_++];
      if (c == '"') return result;
      if (c != '\\') {
        result += c;
        continue;
      }
      if (position_ >= input_.size()) fail("unfinished escape");
      switch (input_[position_++]) {
        case '"': result += '"'; break;
        case '\\': result += '\\'; break;
        case '/': result += '/'; break;
        case 'b': result += '\b'; break;
        case 'f': result += '\f'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'u': {
          unsigned int codepoint = hex_quad();
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                input_[position_ + 1] != 'u') fail("invalid Unicode surrogate pair");
            position_ += 2;
            unsigned int low = hex_quad();
            if (low < 0xDC00 || low > 0xDFFF) fail("invalid Unicode surrogate pair");
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            fail("unexpected low Unicode surrogate");
          }
          append_utf8(result, codepoint);
          break;
        }
        default: fail("unsupported escape");
      }
    }
    fail("unterminated string");
  }

  unsigned int hex_quad() {
    if (position_ + 4 > input_.size()) fail("unfinished Unicode escape");
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      char c = input_[position_++];
      value <<= 4;
      if (c >= '0' && c <= '9') value += static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f') value += static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value += static_cast<unsigned int>(c - 'A' + 10);
      else fail("invalid Unicode escape");
    }
    return value;
  }

  static void append_utf8(std::string& output, unsigned int codepoint) {
    if (codepoint <= 0x7F) output += static_cast<char>(codepoint);
    else if (codepoint <= 0x7FF) {
      output += static_cast<char>(0xC0 | (codepoint >> 6));
      output += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
      output += static_cast<char>(0xE0 | (codepoint >> 12));
      output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      output += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
      output += static_cast<char>(0xF0 | (codepoint >> 18));
      output += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
      output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
  }

  Json array() {
    increment_depth();
    ++position_;
    Json result;
    result.kind = Json::Kind::array;
    if (consume(']')) {
      decrement_depth();
      return result;
    }
    do { result.array.push_back(value()); } while (consume(','));
    if (!consume(']')) fail("expected ']'");
    decrement_depth();
    return result;
  }

  Json object() {
    increment_depth();
    ++position_;
    Json result;
    result.kind = Json::Kind::object;
    if (consume('}')) {
      decrement_depth();
      return result;
    }
    do {
      whitespace();
      if (position_ >= input_.size() || input_[position_] != '"') fail("expected key");
      std::string key = string();
      if (!consume(':')) fail("expected ':'");
      auto insertion = result.object.emplace(std::move(key), value());
      if (!insertion.second) {
        fail("duplicate key in JSON object");
      }
    } while (consume(','));
    if (!consume('}')) fail("expected '}'");
    decrement_depth();
    return result;
  }
};

const Json& required(const Json& object, const std::string& name, Json::Kind kind) {
  if (object.kind != Json::Kind::object) throw Error(ErrorCategory::validation, "JSON response must be an object");
  auto item = object.object.find(name);
  if (item == object.object.end() || item->second.kind != kind)
    throw Error(ErrorCategory::validation, "JSON response field '" + name + "' is missing or has the wrong type");
  return item->second;
}

std::optional<std::string> optional_string(const Json& object, const std::string& name) {
  auto item = object.object.find(name);
  if (item == object.object.end() || item->second.kind == Json::Kind::null_value)
    return std::nullopt;
  if (item->second.kind != Json::Kind::string)
    throw Error(ErrorCategory::validation, "JSON response field '" + name + "' has the wrong type");
  return item->second.text;
}

std::string json_string(const std::string& input) {
  std::string output;
  output.reserve(input.size() + 8);
  output += '"';
  for (unsigned char c : input) {
    switch (c) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<int>(c));
          output += buf;
        } else {
          output += c;
        }
    }
  }
  output += '"';
  return output;
}

std::string request_json(const EnrichmentRequest& request) {
  return "{\"content\":" + json_string(request.content) +
         ",\"countryCode\":" + json_string(request.country_code) + "}";
}

struct WriteContext {
  std::string* body;
  std::size_t max_bytes;
  bool limit_exceeded = false;
};

size_t write_body(char* data, size_t size, size_t count, void* target) {
  auto* context = static_cast<WriteContext*>(target);
  size_t bytes = size * count;
  if (context->body->size() + bytes > context->max_bytes) {
    context->limit_exceeded = true;
    return 0;
  }
  context->body->append(data, bytes);
  return bytes;
}

class CurlTransport final : public HttpTransport {
 private:
  long connect_timeout_ms_;
  long request_timeout_ms_;
  long low_speed_timeout_seconds_;
  long low_speed_limit_bytes_per_second_;
  std::size_t max_response_bytes_;
  bool allow_insecure_http_;

 public:
  explicit CurlTransport(const ClientConfig& config)
      : connect_timeout_ms_(config.connect_timeout_ms),
        request_timeout_ms_(config.request_timeout_ms),
        low_speed_timeout_seconds_(config.low_speed_timeout_seconds),
        low_speed_limit_bytes_per_second_(config.low_speed_limit_bytes_per_second),
        max_response_bytes_(config.max_response_bytes),
        allow_insecure_http_(config.allow_insecure_http) {}

  HttpResponse send(const HttpRequest& request) override {
    static std::once_flag init;
    std::call_once(init, [] {
      if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw Error(ErrorCategory::transport, "failed to initialize libcurl");
    });
    CURL* curl = curl_easy_init();
    if (!curl) throw Error(ErrorCategory::transport, "failed to create libcurl request");

    auto curl_deleter = [](CURL* c) { if (c) curl_easy_cleanup(c); };
    std::unique_ptr<CURL, decltype(curl_deleter)> curl_guard(curl, curl_deleter);

    struct curl_slist* headers = nullptr;
    for (const auto& header : request.headers) {
      std::string header_str = header.first + ": " + header.second;
      headers = curl_slist_append(headers, header_str.c_str());
    }
    auto headers_deleter = [](curl_slist* h) { if (h) curl_slist_free_all(h); };
    std::unique_ptr<curl_slist, decltype(headers_deleter)> headers_guard(headers, headers_deleter);

    HttpResponse response;
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, low_speed_timeout_seconds_);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, low_speed_limit_bytes_per_second_);

    if (allow_insecure_http_) {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }

    WriteContext context{&response.body, max_response_bytes_, false};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xyo-sdk-cpp/1.1.1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    }

    if (code != CURLE_OK) {
      std::string error = curl_easy_strerror(code);
      if (code == CURLE_WRITE_ERROR && context.limit_exceeded) {
        throw Error(ErrorCategory::transport, "HTTP request failed: response size exceeded maximum limit of " + std::to_string(max_response_bytes_) + " bytes", 0, static_cast<int>(code));
      }
      throw Error(ErrorCategory::transport, "HTTP request failed: " + error, 0, static_cast<int>(code));
    }
    return response;
  }
};

}  // namespace

std::string to_string(EnrichmentCollectionStatus status) {
  switch (status) {
    case EnrichmentCollectionStatus::ready: return "READY";
    case EnrichmentCollectionStatus::failed: return "FAILED";
    case EnrichmentCollectionStatus::pending: return "PENDING";
  }
  throw Error(ErrorCategory::validation, "unknown enrichment collection status");
}

Client::Client(ClientConfig config) : config_(std::move(config)) {
  if (config_.api_key.empty()) throw Error(ErrorCategory::validation, "api_key must not be empty");
  if (config_.api_base_url.empty()) throw Error(ErrorCategory::validation, "api_base_url must not be empty");
  while (config_.api_base_url.size() > 1 && config_.api_base_url.back() == '/')
    config_.api_base_url.pop_back();
  if (!config_.allow_insecure_http) {
    if (config_.api_base_url.rfind("https://", 0) != 0) {
      throw Error(ErrorCategory::validation, "api_base_url must use HTTPS unless allow_insecure_http is enabled");
    }
  }
  if (!config_.http_transport) config_.http_transport = std::make_shared<CurlTransport>(config_);
}

Client::~Client() noexcept {
  // config_.api_key is wiped by ClientConfig::~ClientConfig()
}

HttpResponse Client::post(const std::string& path, const std::string& body) const {
  HttpRequest request{"POST", config_.api_base_url + path,
                      {{"Content-Type", "application/json"}, {"Accept", "application/json"},
                       {"Authorization", "Bearer " + config_.api_key}}, body};
  HttpResponse response;
  int attempts = 0;
  int max_attempts = (std::max)(1, config_.max_retries + 1);

  while (attempts < max_attempts) {
    response = config_.http_transport->send(request);
    
    if (response.status_code == 429 || response.status_code >= 500) {
      attempts++;
      if (attempts < max_attempts) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> jitter(0, 100);
        long delay_ms = (1L << attempts) * 100 + jitter(gen);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        continue;
      }
    }
    break;
  }

  if (response.status_code != 200) {
    std::string error_msg = "XYO API returned status code " + std::to_string(response.status_code);
    if (!response.body.empty()) {
      error_msg += ": " + response.body;
    }
    throw Error(ErrorCategory::http, error_msg, response.status_code);
  }
  return response;
}

EnrichmentResponse Client::enrich_transaction(const EnrichmentRequest& request) const {
  Json root = JsonParser(post("/v1/ai/finance/enrichment/transaction", request_json(request)).body,
                         config_.max_json_depth, config_.max_json_nodes).parse();
  EnrichmentResponse response;
  response.merchant = required(root, "merchant", Json::Kind::string).text;
  response.description = required(root, "description", Json::Kind::string).text;
  response.logo = required(root, "logo", Json::Kind::string).text;
  for (const auto& category : required(root, "categories", Json::Kind::array).array) {
    if (category.kind != Json::Kind::string) throw Error(ErrorCategory::validation, "categories must contain strings");
    response.categories.push_back(category.text);
  }
  response.location = optional_string(root, "location");
  response.address = optional_string(root, "address");
  return response;
}

EnrichTransactionCollectionResponse Client::enrich_transaction_collection(
    const std::vector<EnrichmentRequest>& requests) const {
  if (requests.size() > config_.max_collection_size) {
    throw Error(ErrorCategory::validation, "collection size exceeds max_collection_size limit of " + std::to_string(config_.max_collection_size));
  }
  std::string body = "[";
  for (std::size_t i = 0; i < requests.size(); ++i) {
    if (i) body += ',';
    body += request_json(requests[i]);
  }
  body += ']';
  Json root = JsonParser(post("/v1/ai/finance/enrichment/transactions", body).body,
                         config_.max_json_depth, config_.max_json_nodes).parse();
  return {required(root, "id", Json::Kind::string).text,
          required(root, "link", Json::Kind::string).text};
}

EnrichmentCollectionStatus Client::enrich_transaction_collection_status(
    const std::string& id) const {
  if (id.empty()) throw Error(ErrorCategory::validation, "id must not be empty");
  for (char c : id) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      throw Error(ErrorCategory::validation, "invalid transaction collection ID format");
    }
  }
  // NOTE: XYO API status endpoint requires a POST request with an empty body
  Json root = JsonParser(post("/v1/ai/finance/enrichment/transactions/status/" + id, "").body,
                         config_.max_json_depth, config_.max_json_nodes).parse();
  const std::string& status = required(root, "status", Json::Kind::string).text;
  if (status == "READY") return EnrichmentCollectionStatus::ready;
  if (status == "FAILED") return EnrichmentCollectionStatus::failed;
  if (status == "PENDING") return EnrichmentCollectionStatus::pending;
  throw Error(ErrorCategory::validation, "unknown enrichment collection status: " + status);
}

Error::Error(ErrorCategory category, const std::string& message, long http_status_code, int transport_code)
    : std::runtime_error(message), category_(category), http_status_code_(http_status_code), transport_code_(transport_code) {}

ClientConfig::~ClientConfig() noexcept {
  std::fill(api_key.begin(), api_key.end(), '\0');
}

}  // namespace xyo

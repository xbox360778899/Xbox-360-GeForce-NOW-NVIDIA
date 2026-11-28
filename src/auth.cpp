#include "auth.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#include <curl/curl.h>
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/thread.h"

namespace {
constexpr char kClientId[] = "q61ddeJrVt7O90Nl-P-N7I36yctih4Ml6FyXLrb6j-U";
constexpr char kNvidiaIdpId[] = "PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg";
constexpr char kScope[] = "openid consent email tk_client age";
constexpr char kAuthorizeUrl[] = "https://login.nvidia.com/device/authorize";
constexpr char kTokenUrl[] = "https://login.nvidia.com/token";
constexpr char kOrigin[] = "https://play.geforcenow.com";
constexpr char kUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64; Steam Deck) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";
constexpr size_t kMaxHttpBody = 64 * 1024;
constexpr uint32_t kDefaultPollSeconds = 5;
constexpr uint32_t kDefaultExpirySeconds = 600;
constexpr uint32_t kMaxExpirySeconds = 3600;
constexpr uint32_t kRequestTimeoutSeconds = 10;

std::string g_lastError;
std::string g_deviceId;

void wipeString(std::string& value) {
  if (!value.empty()) {
    volatile char* p = &value[0];
    for (size_t i = 0; i < value.size(); ++i) p[i] = 0;
  }
  value.clear();
}

struct WipeOnExit {
  std::string* value;
  ~WipeOnExit() { if (value) wipeString(*value); }
};

void setError(const char* message) {
  g_lastError = message ? message : "Unknown authentication error";
}

struct ResponseBuffer {
  std::string data;
  bool overflow = false;
};

size_t receiveBody(char* ptr, size_t size, size_t count, void* context) {
  auto* buffer = static_cast<ResponseBuffer*>(context);
  if (size && count > std::numeric_limits<size_t>::max() / size) {
    buffer->overflow = true;
    return 0;
  }
  const size_t bytes = size * count;
  if (!bytes) return 0;
  if (buffer->data.size() > kMaxHttpBody ||
      bytes > kMaxHttpBody - buffer->data.size()) {
    buffer->overflow = true;
    return 0;
  }
  buffer->data.append(ptr, bytes);
  return bytes;
}

bool appendHeader(curl_slist*& list, const char* value) {
  curl_slist* next = curl_slist_append(list, value);
  if (!next) return false;
  list = next;
  return true;
}

bool appendFormField(CURL* curl, std::string& body, const char* key,
                     const std::string& value) {
  char* escaped = curl_easy_escape(curl, value.c_str(),
                                   static_cast<int>(value.size()));
  if (!escaped) return false;
  if (!body.empty()) body.push_back('&');
  body.append(key);
  body.push_back('=');
  body.append(escaped);
  curl_free(escaped);
  return true;
}

// Synchronous POST helper. Response bodies are capped; TLS verification is
// mandatory and redirects are disabled so auth data is never forwarded.
bool postForm(const char* url, const std::string& body,
              const std::string& deviceId, ResponseBuffer& response,
              CURLcode& curlResult, long& httpStatus, double& elapsedSeconds) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    curlResult = CURLE_FAILED_INIT;
    return false;
  }

  curl_slist* headers = nullptr;
  const std::string deviceHeader = "x-device-id: " + deviceId;
  const std::string clientHeader = std::string("nv-client-id: ") + kClientId;
  const char* fixedHeaders[] = {
      "Accept: application/json, text/plain, */*",
      "Content-Type: application/x-www-form-urlencoded",
      "Origin: https://play.geforcenow.com",
      "Referer: https://play.geforcenow.com/",
      "nv-client-streamer: WEBRTC",
      "nv-client-type: BROWSER",
      "nv-client-platform-name: browser",
      "nv-browser-type: CHROME",
      "nv-device-os: STEAMOS",
      "nv-device-type: CONSOLE",
      "nv-device-model: STEAMDECK",
      "nv-device-make: VALVE",
  };
  bool headersOk = appendHeader(headers, deviceHeader.c_str()) &&
                   appendHeader(headers, clientHeader.c_str());
  for (const char* header : fixedHeaders) {
    if (!headersOk || !appendHeader(headers, header)) {
      headersOk = false;
      break;
    }
  }
  if (!headersOk) {
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curlResult = CURLE_OUT_OF_MEMORY;
    return false;
  }

  response.data.clear();
  response.overflow = false;
  httpStatus = 0;
  elapsedSeconds = 0.0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(kRequestTimeoutSeconds));
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_CAINFO, "game:\\cacert.pem");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  curlResult = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsedSeconds);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return curlResult == CURLE_OK && !response.overflow;
}

void skipWhitespace(const std::string& json, size_t& pos) {
  while (pos < json.size() &&
         (json[pos] == ' ' || json[pos] == '\t' ||
          json[pos] == '\r' || json[pos] == '\n')) ++pos;
}

bool readHex4(const std::string& json, size_t& pos, uint32_t& value) {
  if (pos + 4 > json.size()) return false;
  value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const char c = json[pos++];
    value <<= 4;
    if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
    else return false;
  }
  return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
  else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool parseJsonString(const std::string& json, size_t& pos, std::string& out) {
  if (pos >= json.size() || json[pos] != '"') return false;
  ++pos;
  out.clear();
  while (pos < json.size()) {
    const unsigned char c = static_cast<unsigned char>(json[pos++]);
    if (c == '"') return true;
    if (c < 0x20) return false;
    if (c != '\\') {
      out.push_back(static_cast<char>(c));
      continue;
    }
    if (pos >= json.size()) return false;
    const char escaped = json[pos++];
    switch (escaped) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        uint32_t cp = 0;
        if (!readHex4(json, pos, cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          if (pos + 2 > json.size() || json[pos] != '\\' || json[pos + 1] != 'u') return false;
          pos += 2;
          uint32_t low = 0;
          if (!readHex4(json, pos, low) || low < 0xDC00 || low > 0xDFFF) return false;
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          return false;
        }
        appendUtf8(out, cp);
        break;
      }
      default: return false;
    }
  }
  return false;
}

bool skipJsonValue(const std::string& json, size_t& pos, unsigned depth) {
  if (depth > 24) return false;
  skipWhitespace(json, pos);
  if (pos >= json.size()) return false;
  if (json[pos] == '"') {
    std::string ignored;
    return parseJsonString(json, pos, ignored);
  }
  if (json[pos] == '{') {
    ++pos;
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == '}') { ++pos; return true; }
    while (pos < json.size()) {
      std::string key;
      if (!parseJsonString(json, pos, key)) return false;
      skipWhitespace(json, pos);
      if (pos >= json.size() || json[pos++] != ':') return false;
      if (!skipJsonValue(json, pos, depth + 1)) return false;
      skipWhitespace(json, pos);
      if (pos >= json.size()) return false;
      if (json[pos] == '}') { ++pos; return true; }
      if (json[pos++] != ',') return false;
      skipWhitespace(json, pos);
    }
    return false;
  }
  if (json[pos] == '[') {
    ++pos;
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ']') { ++pos; return true; }
    while (pos < json.size()) {
      if (!skipJsonValue(json, pos, depth + 1)) return false;
      skipWhitespace(json, pos);
      if (pos >= json.size()) return false;
      if (json[pos] == ']') { ++pos; return true; }
      if (json[pos++] != ',') return false;
      skipWhitespace(json, pos);
    }
    return false;
  }
  const size_t start = pos;
  while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
         json[pos] != ']' && json[pos] != ' ' && json[pos] != '\t' &&
         json[pos] != '\r' && json[pos] != '\n') ++pos;
  return pos > start;
}

// Read one top-level object member. stringValue is false for numeric and
// composite JSON values; nonmatching values are still fully skipped/validated.
bool findJsonField(const std::string& json, const char* field,
                   std::string& value, bool& stringValue) {
  size_t pos = 0;
  skipWhitespace(json, pos);
  if (pos >= json.size() || json[pos++] != '{') return false;
  skipWhitespace(json, pos);
  if (pos < json.size() && json[pos] == '}') return false;
  while (pos < json.size()) {
    std::string key;
    if (!parseJsonString(json, pos, key)) return false;
    skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos++] != ':') return false;
    skipWhitespace(json, pos);
    if (key == field) {
      value.clear();
      if (pos < json.size() && json[pos] == '"') {
        stringValue = true;
        return parseJsonString(json, pos, value);
      }
      stringValue = false;
      const size_t start = pos;
      if (!skipJsonValue(json, pos, 0)) return false;
      value.assign(json, start, pos - start);
      return true;
    }
    if (!skipJsonValue(json, pos, 0)) return false;
    skipWhitespace(json, pos);
    if (pos >= json.size()) return false;
    if (json[pos] == '}') return false;
    if (json[pos++] != ',') return false;
    skipWhitespace(json, pos);
  }
  return false;
}

bool getJsonString(const std::string& json, const char* key, std::string& out) {
  bool isString = false;
  return findJsonField(json, key, out, isString) && isString;
}

bool getJsonUInt(const std::string& json, const char* key, uint32_t& out) {
  std::string value;
  bool isString = false;
  if (!findJsonField(json, key, value, isString) || isString || value.empty()) return false;
  for (char c : value) if (c < '0' || c > '9') return false;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (!end || *end || parsed > std::numeric_limits<uint32_t>::max()) return false;
  out = static_cast<uint32_t>(parsed);
  return true;
}

bool appendSuccessTokens(const std::string& json, AuthTokens& tokens) {
  if (!getJsonString(json, "access_token", tokens.accessToken) ||
      tokens.accessToken.empty()) return false;
  getJsonString(json, "id_token", tokens.idToken);
  getJsonString(json, "refresh_token", tokens.refreshToken);
  getJsonString(json, "client_token", tokens.clientToken);
  getJsonUInt(json, "expires_in", tokens.expiresInSeconds);
  return true;
}

bool isTransientCurlError(CURLcode code) {
  return code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT ||
         code == CURLE_OPERATION_TIMEDOUT || code == CURLE_RECV_ERROR ||
         code == CURLE_SEND_ERROR || code == CURLE_GOT_NOTHING;
}

}  // namespace

const std::string& auth_device_id() {
  if (g_deviceId.empty()) {
    unsigned char bytes[16]{};
    XeCryptRandom(bytes, sizeof(bytes));
    static constexpr char hex[] = "0123456789abcdef";
    g_deviceId.reserve(sizeof(bytes) * 2);
    for (unsigned char byte : bytes) {
      g_deviceId.push_back(hex[(byte >> 4) & 0x0F]);
      g_deviceId.push_back(hex[byte & 0x0F]);
    }
    std::memset(bytes, 0, sizeof(bytes));
  }
  return g_deviceId;
}

void auth_clear_tokens(AuthTokens* tokens) {
  if (!tokens) return;
  wipeString(tokens->accessToken);
  wipeString(tokens->idToken);
  wipeString(tokens->refreshToken);
  wipeString(tokens->clientToken);
  tokens->expiresInSeconds = 0;
  // deviceId is an identifier, not an authentication secret; keep it available
  // for CloudMatch within this process.
}

const char* auth_last_error() {
  return g_lastError.c_str();
}

const char* auth_bearer_token(const AuthTokens* tokens) {
  if (!tokens) return nullptr;
  if (!tokens->idToken.empty()) return tokens->idToken.c_str();
  if (!tokens->accessToken.empty()) return tokens->accessToken.c_str();
  return nullptr;
}

bool auth_device_login(AuthTokens* tokens, AuthCodeCallback onCode,
                      void* userData) {
  if (!tokens) {
    setError("AuthTokens pointer is null");
    return false;
  }
  auth_clear_tokens(tokens);
  g_lastError.clear();

  const std::string deviceId = auth_device_id();
  WipeOnExit wipeDeviceIdCopy{nullptr}; // deviceId is not a secret; retained only for this call.
  CURL* encoder = curl_easy_init();
  if (!encoder) {
    setError("libcurl could not create an authorization request");
    return false;
  }

  std::string authorizeBody;
  const bool formOk = appendFormField(encoder, authorizeBody, "client_id", kClientId) &&
      appendFormField(encoder, authorizeBody, "scope", kScope) &&
      appendFormField(encoder, authorizeBody, "device_id", deviceId) &&
      appendFormField(encoder, authorizeBody, "display_name", "Xbox 360") &&
      appendFormField(encoder, authorizeBody, "idp_id", kNvidiaIdpId);
  curl_easy_cleanup(encoder);
  if (!formOk) {
    setError("Could not encode the device authorization request");
    return false;
  }

  ResponseBuffer authorizeResponse;
  WipeOnExit wipeAuthorizeResponse{&authorizeResponse.data};
  CURLcode curlResult = CURLE_OK;
  long httpStatus = 0;
  double requestSeconds = 0.0;
  if (!postForm(kAuthorizeUrl, authorizeBody, deviceId, authorizeResponse,
                curlResult, httpStatus, requestSeconds)) {
    xsdk_log_write("auth: device authorization transport failed (curl=%d)",
                   static_cast<int>(curlResult));
    setError("Device authorization request failed; check network, clock, and cacert.pem");
    wipeString(authorizeBody);
    return false;
  }
  wipeString(authorizeBody);
  if (httpStatus < 200 || httpStatus >= 300) {
    xsdk_log_write("auth: device authorization rejected (HTTP %ld)", httpStatus);
    setError("NVIDIA rejected the device authorization request");
    return false;
  }

  std::string deviceCode;
  WipeOnExit wipeDeviceCode{&deviceCode};
  std::string userCode;
  WipeOnExit wipeUserCode{&userCode};
  std::string verificationUrl;
  WipeOnExit wipeVerificationUrl{&verificationUrl};
  if (!getJsonString(authorizeResponse.data, "device_code", deviceCode) || deviceCode.empty() ||
      !getJsonString(authorizeResponse.data, "user_code", userCode) || userCode.empty() ||
      !getJsonString(authorizeResponse.data, "verification_uri_complete", verificationUrl) ||
      verificationUrl.empty()) {
    setError("Device authorization response was missing required JSON fields");
    return false;
  }
  uint32_t expirySeconds = kDefaultExpirySeconds;
  uint32_t pollSeconds = kDefaultPollSeconds;
  if (getJsonUInt(authorizeResponse.data, "expires_in", expirySeconds)) {
    expirySeconds = std::max(1u, std::min(expirySeconds, kMaxExpirySeconds));
  } else {
    expirySeconds = kDefaultExpirySeconds;
  }
  if (getJsonUInt(authorizeResponse.data, "interval", pollSeconds)) {
    pollSeconds = std::max(1u, std::min(pollSeconds, 300u));
  } else {
    pollSeconds = kDefaultPollSeconds;
  }
  wipeString(authorizeResponse.data);

  // The user code is short-lived but can authorize the account, so show it
  // only on the local test screen. Neither code nor URL enters the logs.
  xsdk_log_write("auth: device code received; waiting for user approval (expires in %u seconds)",
                 expirySeconds);
  if (onCode) onCode(userCode.c_str(), verificationUrl.c_str(), userData);
  tokens->deviceId = deviceId;

  double elapsedSeconds = requestSeconds;
  uint32_t consecutiveNetworkErrors = 0;
  while (elapsedSeconds < expirySeconds) {
    const uint32_t remaining = static_cast<uint32_t>(expirySeconds - elapsedSeconds);
    const uint32_t waitSeconds = std::min(pollSeconds, remaining);
    if (!waitSeconds) break;
    xsdk_thread_sleep(waitSeconds * 1000);
    elapsedSeconds += waitSeconds;
    if (elapsedSeconds >= expirySeconds) break;

    CURL* pollEncoder = curl_easy_init();
    if (!pollEncoder) {
      setError("libcurl could not create a token poll request");
      return false;
    }
    std::string pollBody;
    const bool pollFormOk =
        appendFormField(pollEncoder, pollBody, "grant_type",
                        "urn:ietf:params:oauth:grant-type:device_code") &&
        appendFormField(pollEncoder, pollBody, "device_code", deviceCode) &&
        appendFormField(pollEncoder, pollBody, "client_id", kClientId);
    curl_easy_cleanup(pollEncoder);
    if (!pollFormOk) {
      wipeString(pollBody);
      setError("Could not encode the token polling request");
      return false;
    }

    ResponseBuffer pollResponse;
    WipeOnExit wipePollResponse{&pollResponse.data};
    requestSeconds = 0.0;
    const bool transportOk = postForm(kTokenUrl, pollBody, deviceId,
                                      pollResponse, curlResult, httpStatus,
                                      requestSeconds);
    wipeString(pollBody);
    elapsedSeconds += requestSeconds;
    if (!transportOk) {
      if (isTransientCurlError(curlResult) && elapsedSeconds < expirySeconds) {
        ++consecutiveNetworkErrors;
        if (consecutiveNetworkErrors == 1 ||
            (consecutiveNetworkErrors % 6) == 0) {
          xsdk_log_write("auth: temporary token poll network error; retrying (attempt %u)",
                         consecutiveNetworkErrors);
        }
        continue;
      }
      xsdk_log_write("auth: token poll failed (curl=%d)", static_cast<int>(curlResult));
      setError("Token polling failed; check the network connection");
      return false;
    }
    consecutiveNetworkErrors = 0;

    if (httpStatus >= 200 && httpStatus < 300) {
      if (!appendSuccessTokens(pollResponse.data, *tokens)) {
        auth_clear_tokens(tokens);
        setError("Token response was missing a valid access_token");
        return false;
      }
      tokens->deviceId = deviceId;
      wipeString(pollResponse.data);
      xsdk_log_write("auth: NVIDIA authorization succeeded; token held in memory, not logged");
      return true;
    }

    std::string oauthError;
    const bool hasOauthError = getJsonString(pollResponse.data, "error", oauthError);
    if (hasOauthError && oauthError == "authorization_pending") {
      continue;
    }
    if (hasOauthError && oauthError == "slow_down") {
      pollSeconds = std::min(pollSeconds + 5, 300u);
      continue;
    }
    if (hasOauthError && oauthError == "access_denied") {
      setError("Sign-in was denied on the verification page");
      return false;
    }
    if (hasOauthError && oauthError == "expired_token") {
      setError("Device code expired before sign-in was approved");
      return false;
    }
    if (httpStatus == 408 || httpStatus == 429 || httpStatus >= 500) {
      if (httpStatus == 429) pollSeconds = std::min(pollSeconds + 5, 300u);
      xsdk_log_write("auth: temporary token endpoint response (HTTP %ld); retrying", httpStatus);
      continue;
    }
    xsdk_log_write("auth: token endpoint rejected polling (HTTP %ld)", httpStatus);
    setError("NVIDIA rejected the token polling request");
    return false;
  }

  setError("Device code expired before sign-in was approved");
  return false;
}

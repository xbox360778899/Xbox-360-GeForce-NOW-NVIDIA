#include "cloudmatch.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#include <curl/curl.h>
#include "xsdk/kernel.h"

namespace {
constexpr char kCloudMatchBase[] = "https://prod.cloudmatchbeta.nvidiagrid.net";
constexpr char kGraphQlUrl[] = "https://games.geforce.com/graphql";
constexpr char kGraphQlClientId[] = "ec7e38d4-03af-4b58-b131-cfb0495903ab";
constexpr char kClientVersion[] = "2.0.89.154";
constexpr char kUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36";
constexpr size_t kMaxResponseBytes = 128 * 1024;
constexpr long kRequestTimeoutSeconds = 10;

std::string g_error;

struct ResponseBuffer {
  std::string text;
  bool overflow = false;
};

size_t receiveBody(char* data, size_t size, size_t count, void* context) {
  auto* response = static_cast<ResponseBuffer*>(context);
  if (size && count > std::numeric_limits<size_t>::max() / size) {
    response->overflow = true;
    return 0;
  }
  const size_t bytes = size * count;
  if (bytes > kMaxResponseBytes - response->text.size()) {
    response->overflow = true;
    return 0;
  }
  response->text.append(data, bytes);
  return bytes;
}

bool addHeader(curl_slist*& headers, const std::string& value) {
  curl_slist* next = curl_slist_append(headers, value.c_str());
  if (!next) return false;
  headers = next;
  return true;
}

bool httpRequest(const char* url, const char* method,
                 const std::string* body, curl_slist* headers,
                 ResponseBuffer& response, long& statusCode) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    g_error = "libcurl could not create the CloudMatch request";
    return false;
  }
  response.text.clear();
  response.overflow = false;
  statusCode = 0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, kRequestTimeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_CAINFO, "game:\\cacert.pem");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  if (std::strcmp(method, "GET") == 0) {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  } else if (std::strcmp(method, "DELETE") == 0) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  } else {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body->c_str() : "");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body ? body->size() : 0));
  }
  const CURLcode result = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK || response.overflow) {
    g_error = response.overflow ? "CloudMatch response exceeded the safe size limit"
                                : "CloudMatch network request failed or timed out";
    return false;
  }
  if (statusCode < 200 || statusCode >= 300) {
    char message[96];
    std::snprintf(message, sizeof(message), "NVIDIA endpoint returned HTTP %ld", statusCode);
    g_error = message;
    return false;
  }
  return true;
}

void skipSpace(const std::string& text, size_t& pos) {
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
         text[pos] == '\r' || text[pos] == '\n')) ++pos;
}

bool readString(const std::string& text, size_t& pos, std::string& out) {
  if (pos >= text.size() || text[pos++] != '"') return false;
  out.clear();
  while (pos < text.size()) {
    const char c = text[pos++];
    if (c == '"') return true;
    if (c != '\\') {
      if (static_cast<unsigned char>(c) < 0x20) return false;
      out.push_back(c);
      continue;
    }
    if (pos >= text.size()) return false;
    const char escaped = text[pos++];
    switch (escaped) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u':
        if (pos + 4 > text.size()) return false;
        // The fields used here (IDs, URLs, title) are ASCII. Preserve valid JSON
        // while avoiding a heavyweight Unicode dependency on the console.
        out.push_back('?');
        pos += 4;
        break;
      default: return false;
    }
  }
  return false;
}

bool skipValue(const std::string& text, size_t& pos, unsigned depth = 0) {
  if (depth > 24) return false;
  skipSpace(text, pos);
  if (pos >= text.size()) return false;
  if (text[pos] == '"') {
    std::string ignored;
    return readString(text, pos, ignored);
  }
  if (text[pos] == '{' || text[pos] == '[') {
    const char close = text[pos++] == '{' ? '}' : ']';
    skipSpace(text, pos);
    if (pos < text.size() && text[pos] == close) { ++pos; return true; }
    while (pos < text.size()) {
      if (close == '}') {
        std::string key;
        if (!readString(text, pos, key)) return false;
        skipSpace(text, pos);
        if (pos >= text.size() || text[pos++] != ':') return false;
      }
      if (!skipValue(text, pos, depth + 1)) return false;
      skipSpace(text, pos);
      if (pos >= text.size()) return false;
      if (text[pos] == close) { ++pos; return true; }
      if (text[pos++] != ',') return false;
      skipSpace(text, pos);
    }
    return false;
  }
  const size_t start = pos;
  while (pos < text.size() && text[pos] != ',' && text[pos] != '}' &&
         text[pos] != ']' && text[pos] != ' ' && text[pos] != '\t' &&
         text[pos] != '\r' && text[pos] != '\n') ++pos;
  return pos > start;
}

bool getRawField(const std::string& object, const char* name,
                 std::string& raw, bool& isString) {
  size_t pos = 0;
  skipSpace(object, pos);
  if (pos >= object.size() || object[pos++] != '{') return false;
  skipSpace(object, pos);
  while (pos < object.size() && object[pos] != '}') {
    std::string key;
    if (!readString(object, pos, key)) return false;
    skipSpace(object, pos);
    if (pos >= object.size() || object[pos++] != ':') return false;
    skipSpace(object, pos);
    if (key == name) {
      raw.clear();
      if (pos < object.size() && object[pos] == '"') {
        isString = true;
        return readString(object, pos, raw);
      }
      isString = false;
      const size_t start = pos;
      if (!skipValue(object, pos)) return false;
      raw.assign(object, start, pos - start);
      return true;
    }
    if (!skipValue(object, pos)) return false;
    skipSpace(object, pos);
    if (pos >= object.size() || object[pos] == '}') return false;
    if (object[pos++] != ',') return false;
    skipSpace(object, pos);
  }
  return false;
}

bool getFlexibleString(const std::string& object, const char* name,
                       std::string& value) {
  bool isString = false;
  if (!getRawField(object, name, value, isString)) return false;
  if (isString) return true;
  if (value.empty() || value == "null" || value[0] == '{' || value[0] == '[') return false;
  return true;
}

bool getStringField(const std::string& object, const char* name,
                    std::string& value) {
  bool isString = false;
  return getRawField(object, name, value, isString) && isString;
}

bool nextArrayObject(const std::string& array, size_t& pos, std::string& item) {
  if (pos == 0) {
    skipSpace(array, pos);
    if (pos >= array.size() || array[pos++] != '[') return false;
  } else {
    skipSpace(array, pos);
    if (pos >= array.size() || array[pos] == ']') return false;
    if (array[pos++] != ',') return false;
  }
  skipSpace(array, pos);
  if (pos >= array.size() || array[pos] == ']') return false;
  const size_t start = pos;
  if (!skipValue(array, pos)) return false;
  item.assign(array, start, pos - start);
  return !item.empty() && item[0] == '{';
}

std::string jsonEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (unsigned char c : input) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          out += escaped;
        } else out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

bool appendAuthHeaders(curl_slist*& headers, const char* token,
                       const char* clientId, const char* deviceId,
                       bool cloudMatch) {
  const std::string authorization = std::string("Authorization: GFNJWT ") + token;
  bool ok = addHeader(headers, "Accept: application/json") &&
            addHeader(headers, authorization);
  if (cloudMatch) {
    ok = ok && addHeader(headers, "Content-Type: application/json") &&
         addHeader(headers, "Origin: https://play.geforcenow.com") &&
         addHeader(headers, "Referer: https://play.geforcenow.com/") &&
         addHeader(headers, "nv-browser-type: CHROME") &&
         addHeader(headers, std::string("nv-client-id: ") + clientId) &&
         addHeader(headers, "nv-client-streamer: WEBRTC") &&
         addHeader(headers, "nv-client-type: BROWSER") &&
         addHeader(headers, std::string("nv-client-version: ") + kClientVersion) &&
         addHeader(headers, "nv-device-make: UNKNOWN") &&
         addHeader(headers, "nv-device-model: UNKNOWN") &&
         addHeader(headers, "nv-device-os: WINDOWS") &&
         addHeader(headers, "nv-device-type: DESKTOP") &&
         addHeader(headers, std::string("x-device-id: ") + deviceId);
  } else {
    ok = ok && addHeader(headers, std::string("nv-client-id: ") + kGraphQlClientId) &&
         addHeader(headers, "nv-client-type: BROWSER") &&
         addHeader(headers, std::string("nv-client-version: ") + kClientVersion) &&
         addHeader(headers, "nv-client-streamer: WEBRTC") &&
         addHeader(headers, "nv-device-os: WINDOWS") &&
         addHeader(headers, "nv-device-type: DESKTOP") &&
         addHeader(headers, "nv-device-make: UNKNOWN") &&
         addHeader(headers, "nv-device-model: UNKNOWN") &&
         addHeader(headers, "nv-browser-type: CHROME") &&
         addHeader(headers, std::string("x-device-id: ") + deviceId);
  }
  return ok;
}

bool appendCatalogHeaders(curl_slist*& headers, const char* token) {
  const std::string authorization = std::string("Authorization: GFNJWT ") + token;
  return addHeader(headers, "Accept: application/json, text/plain, */*") &&
         addHeader(headers, "Content-Type: application/json") &&
         addHeader(headers, "Origin: https://play.geforcenow.com") &&
         addHeader(headers, "Referer: https://play.geforcenow.com/") &&
         addHeader(headers, authorization) &&
         addHeader(headers, std::string("nv-client-id: ") + kGraphQlClientId) &&
         addHeader(headers, "nv-client-type: NATIVE") &&
         addHeader(headers, std::string("nv-client-version: ") + kClientVersion) &&
         addHeader(headers, "nv-client-streamer: NVIDIA-CLASSIC") &&
         addHeader(headers, "nv-device-os: WINDOWS") &&
         addHeader(headers, "nv-device-type: DESKTOP") &&
         addHeader(headers, "nv-device-make: UNKNOWN") &&
         addHeader(headers, "nv-device-model: UNKNOWN") &&
         addHeader(headers, "nv-browser-type: CHROME") &&
         addHeader(headers, std::string("User-Agent: ") + kUserAgent);
}

bool randomUuid(std::string& uuid) {
  unsigned char bytes[16]{};
  XeCryptRandom(bytes, sizeof(bytes));
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
  char text[37];
  std::snprintf(text, sizeof(text),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
      bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]);
  std::memset(bytes, 0, sizeof(bytes));
  uuid = text;
  return true;
}

bool getVpcId(const char* token, const char* deviceId, std::string& vpcId) {
  curl_slist* headers = nullptr;
  if (!appendAuthHeaders(headers, token, "", deviceId, false)) {
    curl_slist_free_all(headers);
    g_error = "Could not prepare NVIDIA serverInfo headers";
    return false;
  }
  ResponseBuffer response;
  long httpStatus = 0;
  const std::string url = std::string(kCloudMatchBase) + "/v2/serverInfo";
  const bool ok = httpRequest(url.c_str(), "GET", nullptr,
                              headers, response, httpStatus);
  curl_slist_free_all(headers);
  if (!ok) return false;
  std::string requestStatus;
  bool isString = false;
  if (!getRawField(response.text, "requestStatus", requestStatus, isString) ||
      !getFlexibleString(requestStatus, "serverId", vpcId) || vpcId.empty()) {
    g_error = "NVIDIA serverInfo did not return a VPC/server ID";
    return false;
  }
  return true;
}

bool resolveCatalogAppId(const char* token, const std::string& vpcId,
                         const char* gameTitle,
                         std::string& appId) {
  static const char query[] =
      "query GetCatalogSearchApps($vpcId:String!,$locale:String!,"
      "$sortString:String!,$fetchCount:Int!,$cursor:String!,"
      "$searchString:String!,$filters:AppFilterFields!){"
      "apps(vpcId:$vpcId,language:$locale,orderBy:$sortString,"
      "first:$fetchCount,after:$cursor,searchQuery:$searchString,filters:$filters){"
      "items{id title variants{id appStore gfn{library{lastPlayedDate}}}"
      "images{GAME_BOX_ART KEY_IMAGE KEY_ART}}"
      "pageInfo{hasNextPage endCursor totalCount}}}";
  const std::string body = std::string("{\"query\":\"") + query +
      "\",\"variables\":{\"vpcId\":\"" + jsonEscape(vpcId) +
      "\",\"locale\":\"en_US\",\"sortString\":\"itemMetadata.relevance:DESC,sortName:ASC\","
      "\"fetchCount\":200,\"cursor\":\"\",\"searchString\":\"" +
      jsonEscape(gameTitle) + "\",\"filters\":{}}}";
  curl_slist* headers = nullptr;
  if (!appendCatalogHeaders(headers, token)) {
    curl_slist_free_all(headers);
    g_error = "Could not prepare NVIDIA catalog headers";
    return false;
  }
  ResponseBuffer response;
  long httpStatus = 0;
  const bool ok = httpRequest(kGraphQlUrl, "POST", &body, headers,
                              response, httpStatus);
  curl_slist_free_all(headers);
  if (!ok) return false;

  std::string errors;
  bool isString = false;
  if (getRawField(response.text, "errors", errors, isString)) {
    size_t errorPos = 0;
    std::string errorObject;
    if (nextArrayObject(errors, errorPos, errorObject)) {
      std::string message;
      if (getStringField(errorObject, "message", message) && !message.empty()) {
        g_error = "NVIDIA catalog GraphQL error: " + message;
        return false;
      }
    }
  }

  std::string data, apps, items;
  if (!getRawField(response.text, "data", data, isString) ||
      !getRawField(data, "apps", apps, isString) ||
      !getRawField(apps, "items", items, isString)) {
    g_error = "NVIDIA catalog response did not contain game results";
    return false;
  }

  const auto isNumericId = [](const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) if (c < '0' || c > '9') return false;
    return true;
  };
  const auto normalizeTitle = [](const std::string& title) {
    std::string normalized;
    normalized.reserve(title.size());
    for (unsigned char c : title) {
      if (std::isalnum(c)) normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return normalized;
  };
  const std::string requestedTitle = normalizeTitle(gameTitle);

  size_t resultCount = 0;
  bool matchedTitleWithoutId = false;
  size_t pos = 0;
  std::string item;
  while (nextArrayObject(items, pos, item)) {
    ++resultCount;
    std::string title;
    if (!getStringField(item, "title", title) ||
        normalizeTitle(title) != requestedTitle) continue;
    std::string itemId;
    getFlexibleString(item, "id", itemId);
    std::string variants;
    std::string firstVariantId;
    std::string numericVariantId;
    if (getRawField(item, "variants", variants, isString)) {
      size_t variantPos = 0;
      std::string variant;
      while (nextArrayObject(variants, variantPos, variant)) {
        std::string variantId;
        if (getFlexibleString(variant, "id", variantId) && !variantId.empty()) {
          if (firstVariantId.empty()) firstVariantId = variantId;
          if (isNumericId(variantId)) {
            numericVariantId = variantId;
            break;
          }
        }
      }
    }
    if (!numericVariantId.empty()) appId = numericVariantId;
    else if (isNumericId(itemId)) appId = itemId;
    else if (!firstVariantId.empty()) appId = firstVariantId;
    else appId = itemId;
    if (!appId.empty()) return true;
    matchedTitleWithoutId = true;
  }
  if (resultCount == 0) {
    g_error = std::string("NVIDIA catalog returned 0 items for ") + gameTitle;
  } else if (matchedTitleWithoutId) {
    g_error = std::string(gameTitle) +
        " matched, but no app or variant ID was returned";
  } else {
    char message[96];
    std::snprintf(message, sizeof(message),
                  "%.32s title not matched; catalog results: %lu", gameTitle,
                  static_cast<unsigned long>(resultCount));
    g_error = message;
  }
  return false;
}

std::string buildSessionBody(const std::string& appId,
                             const std::string& deviceId,
                             const std::string& subSessionId) {
  char metadata[1024];
  std::snprintf(metadata, sizeof(metadata),
      "[{\"key\":\"SubSessionId\",\"value\":\"%s\"},"
      "{\"key\":\"wssignaling\",\"value\":\"1\"},"
      "{\"key\":\"GSStreamerType\",\"value\":\"WebRTC\"},"
      "{\"key\":\"networkType\",\"value\":\"Unknown\"},"
      "{\"key\":\"ClientImeSupport\",\"value\":\"0\"},"
      "{\"key\":\"clientPhysicalResolution\",\"value\":\"{\\\"horizontalPixels\\\":1280,\\\"verticalPixels\\\":720}\"},"
      "{\"key\":\"surroundAudioInfo\",\"value\":\"2\"}]", subSessionId.c_str());

  std::string body = "{\"sessionRequestData\":{";
  body += "\"appId\":\"" + jsonEscape(appId) + "\",\"internalTitle\":null,";
  body += "\"availableSupportedControllers\":[],\"networkTestSessionId\":null,";
  body += "\"parentSessionId\":null,\"clientIdentification\":\"GFN-PC\",\"deviceHashId\":\"";
  body += jsonEscape(deviceId) + "\",\"clientVersion\":\"30.0\",\"sdkVersion\":\"1.0\",\"streamerVersion\":1,";
  body += "\"clientPlatformName\":\"browser\",\"clientRequestMonitorSettings\":[{";
  body += "\"monitorId\":0,\"positionX\":0,\"positionY\":0,\"widthInPixels\":1280,";
  body += "\"heightInPixels\":720,\"framesPerSecond\":60,\"sdrHdrMode\":0,\"displayData\":{},";
  body += "\"hdr10PlusGamingData\":null,\"dpi\":0}],\"useOps\":true,\"audioMode\":2,";
  body += "\"metaData\":" + std::string(metadata) + ",\"sdrHdrMode\":0,";
  body += "\"clientDisplayHdrCapabilities\":null,\"surroundAudioInfo\":0,\"remoteControllersBitmap\":0,";
  body += "\"clientTimezoneOffset\":0,\"enhancedStreamMode\":1,\"appLaunchMode\":1,";
  body += "\"secureRTSPSupported\":false,\"partnerCustomData\":\"\",\"accountLinked\":true,";
  body += "\"enablePersistingInGameSettings\":false,\"requestedStreamingFeatures\":{";
  body += "\"reflex\":false,\"bitDepth\":0,\"cloudGsync\":false,\"enabledL4S\":false,";
  body += "\"supportedHidDevices\":0,\"profile\":0,\"fallbackToLogicalResolution\":false,";
  body += "\"chromaFormat\":0,\"prefilterMode\":1,\"prefilterSharpness\":50,";
  body += "\"prefilterNoiseReduction\":0,\"hudStreamingMode\":0}}}";
  return body;
}

bool createSessionRequest(const char* token, const char* deviceId,
                          const std::string& appId,
                          CloudMatchSession* session) {
  std::string clientId, subSessionId;
  randomUuid(clientId);
  randomUuid(subSessionId);
  session->clientId = clientId;
  const std::string body = buildSessionBody(appId, deviceId, subSessionId);
  curl_slist* headers = nullptr;
  if (!appendAuthHeaders(headers, token, clientId.c_str(), deviceId, true)) {
    curl_slist_free_all(headers);
    g_error = "Could not prepare CloudMatch session headers";
    return false;
  }
  const std::string url = std::string(kCloudMatchBase) +
      "/v2/session?keyboardLayout=us&languageCode=en_US";
  ResponseBuffer response;
  long httpStatus = 0;
  const bool ok = httpRequest(url.c_str(), "POST", &body, headers,
                              response, httpStatus);
  curl_slist_free_all(headers);
  if (!ok) return false;

  std::string requestStatus, statusCode;
  bool isString = false;
  if (!getRawField(response.text, "requestStatus", requestStatus, isString) ||
      !getFlexibleString(requestStatus, "statusCode", statusCode) || statusCode != "1") {
    g_error = "CloudMatch rejected the session request or returned no success status";
    return false;
  }
  std::string sessionJson;
  if (!getRawField(response.text, "session", sessionJson, isString)) {
    g_error = "CloudMatch response did not include a session object";
    return false;
  }
  if (!getFlexibleString(sessionJson, "sessionId", session->sessionId) ||
      session->sessionId.empty()) {
    g_error = "CloudMatch response did not include a session ID";
    return false;
  }

  std::string serverIp;
  std::string resourcePath;
  std::string connections;
  if (getRawField(sessionJson, "connectionInfo", connections, isString)) {
    size_t pos = 0;
    std::string connection;
    while (nextArrayObject(connections, pos, connection)) {
      std::string usage, ip, path;
      getFlexibleString(connection, "usage", usage);
      getFlexibleString(connection, "ip", ip);
      getStringField(connection, "resourcePath", path);
      if (usage == "14") {
        if (!ip.empty()) serverIp = ip;
        if (!path.empty()) resourcePath = path;
        if (!serverIp.empty() || !resourcePath.empty()) break;
      }
      if (resourcePath.empty() && !path.empty()) resourcePath = path;
    }
  }
  if (serverIp.empty()) {
    std::string control;
    if (getRawField(sessionJson, "sessionControlInfo", control, isString))
      getFlexibleString(control, "ip", serverIp);
  }

  if (resourcePath.compare(0, 6, "wss://") == 0) {
    session->serverUrl = resourcePath;
  } else if (resourcePath.compare(0, 8, "rtsps://") == 0 ||
             resourcePath.compare(0, 7, "rtsp://") == 0) {
    const size_t scheme = resourcePath.find("://");
    const size_t hostStart = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t hostEnd = resourcePath.find_first_of(":/", hostStart);
    const std::string host = resourcePath.substr(hostStart, hostEnd - hostStart);
    if (!host.empty()) session->serverUrl = "wss://" + host + "/nvst/";
  } else if (!serverIp.empty()) {
    if (!resourcePath.empty() && resourcePath[0] == '/')
      session->serverUrl = "wss://" + serverIp + ":443" + resourcePath;
    else
      session->serverUrl = "wss://" + serverIp + ":443/nvst/";
  }
  if (session->serverUrl.empty()) {
    g_error = "CloudMatch returned a session but no signaling server URL could be derived";
    return false;
  }
  return true;
}
}  // namespace

const char* cloudmatch_last_error() {
  return g_error.c_str();
}

bool cloudmatch_resolve_app_id(const AuthTokens* tokens, const char* gameTitle,
                               std::string* appId) {
  g_error.clear();
  if (!tokens || !gameTitle || !*gameTitle || !appId) {
    g_error = "Catalog lookup is missing required input";
    return false;
  }
  const char* token = auth_bearer_token(tokens);
  if (!token || !*token) {
    g_error = "Sign-in token is not available in memory";
    return false;
  }
  const std::string& deviceId = tokens->deviceId.empty() ? auth_device_id() : tokens->deviceId;
  std::string vpcId;
  if (!getVpcId(token, deviceId.c_str(), vpcId)) return false;
  appId->clear();
  return resolveCatalogAppId(token, vpcId, gameTitle, *appId);
}

bool cloudmatch_create_session_for_app_id(const AuthTokens* tokens,
                                          const char* appId,
                                          CloudMatchSession* session) {
  g_error.clear();
  if (!tokens || !appId || !*appId || !session) {
    g_error = "CloudMatch request is missing a catalog app ID or session output";
    return false;
  }
  const char* token = auth_bearer_token(tokens);
  if (!token || !*token) {
    g_error = "Sign-in token is not available in memory";
    return false;
  }
  const std::string& deviceId = tokens->deviceId.empty() ? auth_device_id() : tokens->deviceId;
  return createSessionRequest(token, deviceId.c_str(), appId, session);
}

bool cloudmatch_create_session(const AuthTokens* tokens, const char* gameTitle,
                               CloudMatchSession* session) {
  std::string appId;
  if (!cloudmatch_resolve_app_id(tokens, gameTitle, &appId)) return false;
  return cloudmatch_create_session_for_app_id(tokens, appId.c_str(), session);
}

bool cloudmatch_stop_session(const AuthTokens* tokens,
                             const CloudMatchSession* session) {
  g_error.clear();
  if (!tokens || !session || session->sessionId.empty() || session->clientId.empty()) {
    g_error = "Cannot stop the CloudMatch session without its session identity";
    return false;
  }
  const char* token = auth_bearer_token(tokens);
  if (!token || !*token) {
    g_error = "Sign-in token is not available in memory for session cleanup";
    return false;
  }
  const std::string& deviceId = tokens->deviceId.empty() ? auth_device_id() : tokens->deviceId;
  curl_slist* headers = nullptr;
  if (!appendAuthHeaders(headers, token, session->clientId.c_str(),
                         deviceId.c_str(), true)) {
    curl_slist_free_all(headers);
    g_error = "Could not prepare CloudMatch stop headers";
    return false;
  }
  const std::string url = std::string(kCloudMatchBase) +
      "/v2/session/" + session->sessionId;
  ResponseBuffer response;
  long httpStatus = 0;
  const bool ok = httpRequest(url.c_str(), "DELETE", nullptr,
                              headers, response, httpStatus);
  curl_slist_free_all(headers);
  if (ok || httpStatus == 404) {
    g_error.clear();
    return true;
  }
  return false;
}

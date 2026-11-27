#pragma once

#include <cstdint>
#include <string>

// OAuth values are held in memory only. Never log the access, ID, refresh, or
// device token: each is a bearer credential or a secret used to obtain one.
struct AuthTokens {
  std::string accessToken;
  std::string idToken;
  std::string refreshToken;
  std::string clientToken;
  uint32_t expiresInSeconds = 0;
  std::string deviceId;
};

// Synchronous prototype: requests a device code, logs its user code and
// verification URL, then polls until approved, denied, expired, or failed.
// The caller must have called curl_global_init() before invoking this.
bool auth_device_login(AuthTokens* tokens);

// Clear token text from the current string contents before releasing them.
void auth_clear_tokens(AuthTokens* tokens);

// Return a brief, non-secret reason for the most recent failure.
const char* auth_last_error();

// Stable for this app process. Reuse this exact value for later CloudMatch
// requests; persistence across app launches is a separate follow-up step.
const std::string& auth_device_id();

// GFN API requests in OpenNOW use the ID token when one was returned, with the
// access token as fallback. This returns a borrowed pointer into AuthTokens.
const char* auth_bearer_token(const AuthTokens* tokens);

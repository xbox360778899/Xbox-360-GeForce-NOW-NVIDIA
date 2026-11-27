#pragma once

#include <string>

#include "auth.h"

struct CloudMatchSession {
  std::string sessionId;
  std::string serverUrl;
  // Kept in memory so the session can be stopped with the same client identity.
  std::string clientId;
};

// Resolve a game through NVIDIA's GraphQL catalog and return its launchable ID.
bool cloudmatch_resolve_app_id(const AuthTokens* tokens, const char* gameTitle,
                               std::string* appId);

// Create a session with the catalog ID returned by cloudmatch_resolve_app_id.
bool cloudmatch_create_session_for_app_id(const AuthTokens* tokens,
                                          const char* appId,
                                          CloudMatchSession* session);

// Resolve the title through NVIDIA's catalog (never pass its Steam store ID),
// then request a CloudMatch session. The returned serverUrl is the WebSocket
// signaling URL derived from the session's connectionInfo/resourcePath.
bool cloudmatch_create_session(const AuthTokens* tokens, const char* gameTitle,
                               CloudMatchSession* session);

// Best-effort DELETE /v2/session/{sessionId}; 404 means it already expired.
bool cloudmatch_stop_session(const AuthTokens* tokens,
                             const CloudMatchSession* session);

// Non-secret status for the bare-metal test screen. Never includes response
// bodies or bearer credentials.
const char* cloudmatch_last_error();

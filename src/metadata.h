/*
obs-stream-metadata — T-031 product metadata core (Twitch / YouTube / Kick)

Pure QtCore: platform model, limits (AGENTS.md §20), multi-platform
validation, minimal update payloads and HTTP outcome mapping (§28).

Deliberately NOT shared with poc/t030: the PoC stays untouched as a
regression tool; this is the minimal product subset (§4 migration).
Wire formats mirror the P3-validated ones (F-020, F-021, F-023).
*/

#pragma once

#include <QString>

#include <optional>

namespace meta {

enum class Platform { Twitch, YouTube, Kick };

// T-049: connection mode (ADR-012). Explicit domain concept, not a bool or
// UI string: Independent = user-supplied App Identity (direct/BYO-app),
// Managed = service-supplied App Identity (backend). Orthogonal to Platform:
// every (Platform, ConnectionMode) pair is representable; support per pair
// is decided by T-041/T-048, never by this enum. Holds no secrets.
enum class ConnectionMode { Independent, Managed };

const char *connectionModeName(ConnectionMode m); // "Independent"/"Managed"

// Default compatible with all pre-T-049 behavior and persisted config:
// what exists today IS Independent (direct/BYO-app). Legacy data without a
// mode maps to Independent; never to Managed (ADR-012 §10).
ConnectionMode defaultConnectionMode();

// Canonical wire strings for future persistence (T-041). Strict lowercase;
// anything else -> nullopt. Reserved now so the format cannot drift later.
std::optional<ConnectionMode> parseConnectionMode(const QString &s);

// Documented limits (§20). Kick documents no public stream_title limit:
// only non-empty is checked locally, the server decides (204 vs 400).
extern const int kTwitchTitleMax;
extern const int kYouTubeTitleMin;
extern const int kYouTubeTitleMax;
extern const int kYouTubeDescMax;

struct Selection {
	bool twitch = false;
	bool youtube = false;
	bool kick = false;
	bool any() const { return twitch || youtube || kick; }
};

struct Metadata {
	QString title;
	QString description;
};

const char *platformName(Platform p);

// Only YouTube has a stream-description equivalent (F-001).
bool supportsDescription(Platform p);

// Validates title/description against the most restrictive SELECTED
// platform. Never truncates silently (§9). Empty string = ok.
// A non-empty description with YouTube unselected is ignored, never
// a blocker (§11).
QString validate(const Metadata &m, const Selection &s);

// Minimal update payloads (P3-validated wire format).
QString twitchPayload(const QString &title); // {"title":"..."}
QString kickPayload(const QString &title);   // {"stream_title":"..."}
// YouTube PUT needs id + full snippet from a prior GET (F-021:
// no contentDetails with part=snippet).
QString youTubePayload(const QString &fetchedJson, const QString &title,
		       const QString &desc);

enum class Outcome {
	Success,      // 2xx (incl. 204 with no body, Kick)
	BadRequest,   // 400
	AuthRequired, // 401 -> refresh, else reconnect
	Forbidden,    // 403 -> missing scopes
	NotFound,     // 404 -> broadcast/resource missing
	Conflict,     // 409 -> state restriction
	RateLimited,  // 429 -> no aggressive retry
	ServerRetry,  // 5xx -> limited backoff
	NetworkError, // 0 / out of range
};

Outcome classifyStatus(int code);

// User-facing message. Never leaks JSON, headers, tokens or URLs (§16).
QString userMessage(Outcome o, Platform p);

// T-032: bounded retry policy for 429/5xx on Apply (§28). At most
// kBackoffMaxRetries resends per platform with backoffDelayMs spacing;
// never a loop. Refresh-once (§25) is preserved across resends.
extern const int kBackoffMaxRetries; // 2
extern const int kBackoffBaseMs;     // 2000
int backoffDelayMs(int attempt);     // 1-based: 2000, 4000, ...; <=0 -> 0

// T-032: provider revoke endpoints (P3-validated wire format, F-018/024).
// Twitch: POST url?client_id=..&token=.. with empty form body.
// YouTube/Kick: POST form with the token field below (Kick: browser UA).
struct RevokeEndpoint {
	const char *url;
	const char *tokenField; // "" for Twitch (token goes in query)
	bool browserUa;
};
RevokeEndpoint revokeEndpoint(Platform p);

} // namespace meta

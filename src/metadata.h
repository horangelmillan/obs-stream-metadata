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

namespace meta {

enum class Platform { Twitch, YouTube, Kick };

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

} // namespace meta

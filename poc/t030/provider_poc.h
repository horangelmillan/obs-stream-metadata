/*
obs-stream-metadata — T-030 PoC providers (Twitch / YouTube / Kick)

Lógica pura QtCore, sin red, sin secretos, sin almacenamiento.
Cubre: PKCE S256 + state, URLs de autorización, validadores de título,
payloads JSON de actualización y clasificación de HTTP status (§28).

Nada aquí toca OBS ni el dock: la verificación viva (OAuth real + cambio
de título visible en plataforma) se hace con docs/T030-POC.md.
*/

#pragma once

#include <QString>

namespace t030 {

// --- Twitch (OAuth 2.0; Device Flow recomendado en desktop público) ---
extern const char *kTwitchDeviceUrl;   // POST https://id.twitch.tv/oauth2/device
extern const char *kTwitchTokenUrl;    // POST https://id.twitch.tv/oauth2/token
extern const char *kTwitchValidateUrl; // GET  https://id.twitch.tv/oauth2/validate
extern const char *kTwitchChannelsUrl; // PATCH https://api.twitch.tv/helix/channels
extern const char *kTwitchScope;       // channel:manage:broadcast (único scope MVP)
extern const int kTwitchTitleMax;      // 140

// --- YouTube (OAuth 2.0 installed-app + PKCE, loopback) ---
extern const char *kYouTubeAuthUrl;  // https://accounts.google.com/o/oauth2/v2/auth
extern const char *kYouTubeTokenUrl; // POST https://oauth2.googleapis.com/token
extern const char *kYouTubeScope;    // youtube.force-ssl (mínimo para update)
extern const int kYouTubeTitleMin;   // 1
extern const int kYouTubeTitleMax;   // 100
extern const int kYouTubeDescMax;    // 5000

// --- Kick (OAuth 2.1 + PKCE S256, state obligatorio) ---
extern const char *kKickAuthUrl;     // GET  https://id.kick.com/oauth/authorize
extern const char *kKickTokenUrl;    // POST https://id.kick.com/oauth/token
extern const char *kKickRevokeUrl;   // POST https://id.kick.com/oauth/revoke
extern const char *kKickChannelsUrl; // PATCH https://api.kick.com/public/v1/channels
extern const char *kKickScopeWrite;  // channel:write (MVP)

// --- PKCE / state (RFC 7636 S256) ---
QString generateCodeVerifier();                // 64 chars [A-Za-z0-9-._~]
QString codeChallengeS256(const QString &verifier);
QString generateState(); // 32 hex chars, un uso por flujo

// --- URLs de autorización (abrir en navegador del sistema) ---
QString twitchAuthCodeUrl(const QString &clientId, const QString &redirectUri,
			  const QString &state, const QString &scope = QString());
QString youTubeAuthUrl(const QString &clientId, const QString &redirectUri,
		       const QString &state, const QString &codeChallenge,
		       const QString &scope = QString());
QString kickAuthUrl(const QString &clientId, const QString &redirectUri,
		    const QString &state, const QString &codeChallenge,
		    const QString &scope = QString());

// --- Validadores locales (§20). Devuelven true si el título puede enviarse. ---
bool twitchTitleOk(const QString &title, QString *reason = nullptr);
bool youTubeTitleOk(const QString &title, QString *reason = nullptr);
bool youTubeDescOk(const QString &desc, QString *reason = nullptr);
// Kick no documenta límite público de stream_title: solo no-vacío;
// el servidor valida (204 = éxito, 400 = inválido).
bool kickTitleOk(const QString &title, QString *reason = nullptr);

// --- Payloads JSON mínimos ---
QString twitchTitlePayload(const QString &title); // {"title":"..."}
QString kickTitlePayload(const QString &title);   // {"stream_title":"..."}
// YouTube exige PUT de recurso completo: fusiona título/desc sobre el
// snippet obtenido por GET (preserva scheduledStartTime, categoryId...).
QString youTubeSnippetPayload(const QString &fetchedJson, const QString &title,
			      const QString &desc);

// --- Clasificación HTTP (§28). 204 = éxito (Kick). Sin reintentos agresivos. ---
enum class HttpOutcome {
	Success,       // 2xx (incl. 204 sin body)
	BadRequest,    // 400 (+ invalidTitle/invalidDescription/...)
	AuthRequired,  // 401 → refresh; si falla, reconexión
	ForbiddenScope,// 403 → scopes insuficientes
	NotFound,      // 404 → broadcast/recurso inexistente
	ConflictState, // 409 → restricción de estado
	RateLimited,   // 429 → no reintentar en bucle
	ServerRetry,   // 5xx → backoff limitado
	NetworkError,  // 0 / fuera de rango
};
HttpOutcome classifyHttpStatus(int code);
const char *outcomeName(HttpOutcome o);

} // namespace t030

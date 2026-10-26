/*
obs-stream-metadata — T-031 product metadata core (Twitch / YouTube / Kick)

Pure QtCore: platform model, limits (AGENTS.md §20), multi-platform
validation, minimal update payloads and HTTP outcome mapping (§28).

Deliberately NOT shared with poc/t030: the PoC stays untouched as a
regression tool; this is the minimal product subset (§4 migration).
Wire formats mirror the P3-validated ones (F-020, F-021, F-023).
*/

#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace meta {

enum class Platform { Twitch, YouTube, Kick, Facebook };

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
// T-071 FB-2: Facebook title 1-254 (D2, user/page live_videos reference).
// Description SI existe (a diferencia de Twitch/Kick): sin limite inventado.
extern const int kFacebookTitleMax;

struct Selection {
	bool twitch = false;
	bool youtube = false;
	bool kick = false;
	bool facebook = false;
	bool any() const { return twitch || youtube || kick || facebook; }
};

struct Metadata {
	QString title;
	QString description;
};

const char *platformName(Platform p);

// YouTube + Facebook tienen descripcion de stream equivalente (F-001,
// T-071 D2). Twitch/Kick no.
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

// T-071 FB-2: POST /{live-video-id} solo title/description (D1).
// Jamas channel_description/stream_title/snippet (D2, AGENTS §47).
QString facebookPayload(const QString &title, const QString &desc);
// T-071 FB-2: clasificacion §28 + tabla brief §5 (190/1363120/1363144/10).
// T-073 FB-4 (F-080): subcode 33 (GraphMethodException objeto web/ID
// desconocido) es NotFound, no BadRequest de titulo.
Outcome classifyFb(int httpStatus, int metaCode);
// T-073 FB-4: warnings especificos elegibilidad/permisos (D9 §B.5).
// 1363120 = 60+ dias, 1363144 = 100+ seguidores, 10 = permisos/review,
// 33/100 = objeto no gestionable. Vacio si no hay warning especifico.
QString fbEligibilityMessage(int metaCode);
// T-073 FB-4: indicador por status del LiveVideo (D7). Mapeo lectura
// real: LIVE/LIVE_NOW = en directo, UNPUBLISHED/SCHEDULED_* = vista
// previa, VOD/LIVE_STOPPED = terminado, resto = Unknown.
enum class FbLiveState { Unknown, Preview, Live, Ended };
FbLiveState fbLiveState(const QString &status);
QString fbLiveStateLabel(FbLiveState s);
// Adopt explicito (solo lectura): normaliza status (trim+upper);
// LIVE/LIVE_NOW cuentan; exactamente uno -> ok+id; cero -> no-active;
// varios -> ambiguous; vacios ignorados. Puro QtCore, sin I/O/logs/red.
struct FbLiveItem {
	QString id;
	QString status;
};
struct FbAdoptResult {
	bool ok = false;
	QString id;
	QString reason; // "", "no-active" o "ambiguous"
};
FbAdoptResult facebookAdoptActive(const QList<FbLiveItem> &items);
// T-071 FB-2: scopes minimos (D3). Perfil -> publish_video; Page ->
// pages_manage_posts + pages_read_engagement + pages_show_list.
// Jamas publish_to_groups/email.
QStringList fbRequiredScopes(bool page);
// T-071 FB-2: flujo manual desktop OIDC+PKCE sin secret (D4, F-065).
// dialog/oauth v26.0 + state + code_challenge S256. Sin client_secret.
QString facebookAuthUrl(const QString &clientId, const QString &redirectUri,
			const QString &state, const QString &scope,
			const QString &challenge);
// Verificacion previa a GoLive (puro, sin I/O): la audiencia solo se da
// por verificada con privacy EVERYONE devuelta por la API; "" nunca es
// exito. dict {"value":X} -> X trim; string -> trim; resto -> "".
QString facebookPrivacyValue(const QJsonValue &v);
enum class FbVerify { Verified, AudienceUnverified, Blocked };
struct FbVerifyResult {
	FbVerify outcome;
	QString reason;
};
// Exacto y case-sensitive en status ("UNPUBLISHED"); titulo/descripcion
// exactos contra lo enviado al crear; EVERYONE case-insensitive.
FbVerifyResult facebookVerifyPrepared(const QString &expTitle,
				     const QString &expDesc,
				     const QString &actTitle,
				     const QString &actDesc,
				     const QString &actStatus,
				     const QString &actPrivacy);

// User-facing message. Never leaks JSON, headers, tokens or URLs (§16).
QString userMessage(Outcome o, Platform p);

// Fase 4 (cuentas de usuario Managed, spec §§4–5): gate login previo a
// Managed. Sin user-session válida en memoria, TODAS las acciones
// Managed (Connect/Apply/erase por proveedor + status/broadcasts)
// quedan funcionalmente deshabilitadas con UN mensaje exacto,
// reutilizado en las 4 plataformas, sin red ni fallback silencioso a
// Independent. Puro QtCore, sin I/O/red/logs.
QString managedLoginRequiredMessage();
// true solo con user-session válida en memoria. El llamador NO hace red
// cuando esto es false (gate previo, sin fallback a Independent).
bool managedActionAllowed(bool hasUserSession);
// Nombre canónico del header de sesión de usuario (debe coincidir con
// el backend: `X-User-Session`). Independent jamás lo envía ni lo exige.
QString userSessionHeaderName();
// Managed adjunta X-User-Session; Independent nunca (ADR-008/012).
bool managedHeadersIncludeUserSession(bool isManaged);
// Formas de serialización prohibidas en accounts.json/DPAPI: la sesión
// de usuario vive SOLO en memoria (spec §5). El selfcheck las busca en
// el fichero crudo tras un save completo.
QStringList forbiddenUserSessionShapes();
// Aviso v1 sin email (spec §7): sin recuperación de cuenta. Solo UI.
QString userNoRecoveryNotice();

// f7 (bug 2026-10-26): resolucion del texto de sesion cuando NO hay
// sesion de usuario. El llamador pasa el mensaje vigente (p. ej. el
// error especifico de signup/login); si esta vacio se muestra el gate
// "login required", que SOLO aparece cuando no hay otro mensaje.
// Puro QtCore, sin I/O/red/logs. La vista lo usa para que
// refreshManagedLoginUi() no pise el error recien fijado (QLabel::
// setText limpia el contenido previo; https://doc.qt.io/qt-6/qlabel.html).
QString managedLoggedOutStatus(const QString &pending);
// f7: mensaje 409/nombre posiblemente usado: sugiere pulsar Log in en
// vez de reintentar Sign up. Sin secretos, sin detalles del backend.
QString managedSignupTakenMessage();

// f8 REDISENO ENTRAR (2026-10-26 [D]): dos modos excluyentes en la
// pantalla ENTRAR ("Entrar | Crear cuenta"). La vista los muestra con
// un QTabWidget (doc oficial Qt6 QTabWidget,
// https://doc.qt.io/qt-6/qtabwidget.html: tab bar + page area, solo la
// pagina actual visible); estos predicados puros son su gate, como f6
// lo es del QStackedWidget ENTRAR/PANEL. Sin I/O/red/logs.
enum class ManagedAuthMode { Login, Signup };
const char *managedAuthModeName(ManagedAuthMode m);
bool managedAuthLoginVisible(ManagedAuthMode m);
bool managedAuthSignupVisible(ManagedAuthMode m);
// f8: validacion LOCAL inmediata (sin red) antes de enviar, con las
// reglas exactas del backend (backend/users.py 2026-10-26, solo
// lectura): username strip + 3-32 + [A-Za-z0-9._-], password 12-128
// sin complejidad inventada. Vacio = valido; si no, mensaje claro
// para mostrar inline junto al campo. Puro QtCore.
QString validateManagedUsername(const QString &raw);
QString validateManagedPassword(const QString &raw);
// Username primero, luego password (el primer error gana, como el
// formulario muestra un campo a la vez). Vacio = listo para enviar.
QString validateManagedCredentials(const QString &user,
				  const QString &pass);
// f8: aviso visible de la politica en "Crear cuenta" (minimo 12, sin
// email, sin recuperacion). Solo UI, sin secretos.
QString managedPasswordPolicyNotice();

// f6 ENTRAR-primero: dos pantallas excluyentes en Managed (ENTRAR sin
// sesión, PANEL con sesión); Independent siempre una sola pantalla
// (panel, sin login). Puro QtCore, sin I/O/red/logs. La vista los usa
// como gates de un QStackedWidget (doc Qt6 QStackedWidget,
// https://doc.qt.io/qt-6/qstackedwidget.html).
bool managedEnterVisible(bool isManaged, bool hasUserSession);
bool managedPanelVisible(bool isManaged, bool hasUserSession);

// Fase 5: borrado de CUENTA (/users/erase) separado del borrado de DATOS
// F-C2 (/privacy/erase). Rutas canónicas distintas; la respuesta lleva
// solo conteos por tabla + revocaciones por proveedor (jamás secretos).
const char *accountErasePath(); // "/users/erase"
const char *managedDataErasePath(); // "/privacy/erase" (F-C2, intacto)
// Mensaje UI con solo conteos (enteros como texto; nada de tokens,
// secretos, hashes ni IDs). Puro QtCore, sin I/O/red/logs.
QString accountEraseCountsMessage(int users, int installations, int sessions,
				  int connections, int tokens);

// T-032: bounded retry policy for 429/5xx on Apply (§28). At most
// kBackoffMaxRetries resends per platform with backoffDelayMs spacing;
// never a loop. Refresh-once (§25) is preserved across resends.
extern const int kBackoffMaxRetries; // 2
extern const int kBackoffBaseMs;     // 2000
int backoffDelayMs(int attempt);     // 1-based: 2000, 4000, ...; <=0 -> 0

// T-047: distributed Twitch Client ID (public, DCF without secret).
// Resolution: explicit field value (trimmed, BYO/dev override) wins;
// otherwise the build-time distributed ID (possibly empty -> caller
// errors exactly as before). Never a secret: no secret field is read.
QString twitchClientId(const QString &fieldValue);

// T-047: pure DCF poll decision (Twitch device flow, no I/O).
// pollsLeft = remaining attempts AFTER this response (caller decrements).
// success/pending/slow_down/denied/expired per Twitch protocol; never a
// busy-loop (caller re-arms a single-shot timer only on polling actions).
enum class TwPollAction {
	Consume,     // HTTP 200: read tokens
	KeepPolling, // authorization_pending + pollsLeft > 0
	SlowDown,    // slow_down + pollsLeft > 0 (caller adds +5s)
	FailDenied,  // access_denied
	FailExpired, // expired/denied-other/exhausted/no-polls-left
};
TwPollAction classifyTwPoll(int http, bool netFail, const QString &message,
			    int pollsLeft);

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

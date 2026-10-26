/*
T-031: product metadata core. QtCore only, no I/O, no network, no secrets.
*/

#include "metadata.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace meta {

const int kTwitchTitleMax = 140;
const int kYouTubeTitleMin = 1;
const int kYouTubeTitleMax = 100;
const int kYouTubeDescMax = 5000;
const int kFacebookTitleMax = 254;

const char *platformName(Platform p)
{
	switch (p) {
	case Platform::Twitch:
		return "Twitch";
	case Platform::YouTube:
		return "YouTube";
	case Platform::Kick:
		return "Kick";
	case Platform::Facebook:
		return "Facebook";
	}
	return "?";
}

// T-049: mode names are distinct by construction; default is Independent
// (all existing behavior and persisted config predate modes).
const char *connectionModeName(ConnectionMode m)
{
	switch (m) {
	case ConnectionMode::Independent:
		return "Independent";
	case ConnectionMode::Managed:
		return "Managed";
	}
	return "?";
}

ConnectionMode defaultConnectionMode()
{
	return ConnectionMode::Independent;
}

std::optional<ConnectionMode> parseConnectionMode(const QString &s)
{
	if (s == QStringLiteral("independent"))
		return ConnectionMode::Independent;
	if (s == QStringLiteral("managed"))
		return ConnectionMode::Managed;
	return std::nullopt;
}

bool supportsDescription(Platform p)
{
	return p == Platform::YouTube || p == Platform::Facebook;
}

QString validate(const Metadata &m, const Selection &s)
{
	if (!s.any())
		return QStringLiteral("Select at least one platform.");
	if (m.title.isEmpty())
		return QStringLiteral("Title is required.");
	// Most restrictive documented limit among the SELECTED platforms.
	int maxTitle = -1; // -1 = server decides (Kick only)
	if (s.youtube)
		maxTitle = kYouTubeTitleMax;
	else if (s.twitch)
		maxTitle = kTwitchTitleMax;
	else if (s.facebook)
		maxTitle = kFacebookTitleMax;
	if (maxTitle > 0 && m.title.size() > maxTitle)
		return QStringLiteral("Title too long for the selected "
				      "platforms (max %1 characters).")
			.arg(maxTitle);
	if (s.youtube && m.description.size() > kYouTubeDescMax)
		return QStringLiteral("Description too long (max %1 characters).")
			.arg(kYouTubeDescMax);
	// Facebook description: SI existe, sin limite inventado (D2).
	return QString();
}

QString twitchPayload(const QString &title)
{
	QJsonObject o;
	o[QStringLiteral("title")] = title;
	return QString::fromUtf8(
		QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString kickPayload(const QString &title)
{
	QJsonObject o;
	o[QStringLiteral("stream_title")] = title;
	return QString::fromUtf8(
		QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString youTubePayload(const QString &fetchedJson, const QString &title,
		       const QString &desc)
{
	const QJsonObject root =
		QJsonDocument::fromJson(fetchedJson.toUtf8()).object();
	QJsonObject out;
	out[QStringLiteral("id")] = root.value(QStringLiteral("id"));
	QJsonObject snippet =
		root.value(QStringLiteral("snippet")).toObject();
	snippet[QStringLiteral("title")] = title;
	snippet[QStringLiteral("description")] = desc;
	out[QStringLiteral("snippet")] = snippet;
	return QString::fromUtf8(
		QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// T-071 FB-2: POST /{live-video-id} minimo (D1). Titulo 1-254 + descripcion
// opcional. Sin channel_description (canal, no stream), sin stream_title,
// sin snippet (AGENTS §47, F-001).
QString facebookPayload(const QString &title, const QString &desc)
{
	QJsonObject o;
	o[QStringLiteral("title")] = title;
	if (!desc.isEmpty())
		o[QStringLiteral("description")] = desc;
	return QString::fromUtf8(
		QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// T-077 (Fase A): cuerpo de create puro (ver facebookCreatePayload
// en metadata.h). F-092 citado alli: sin promesa de persistencia.
QJsonObject facebookCreatePayload(const QString &title,
				  const QString &desc,
				  bool persistentOptIn)
{
	QJsonObject o;
	o[QStringLiteral("title")] = title;
	if (!desc.isEmpty())
		o[QStringLiteral("description")] = desc;
	// Preview propiedad del dock: jamas LIVE_NOW en create/verify.
	o[QStringLiteral("status")] = QStringLiteral("UNPUBLISHED");
	QJsonObject privacy;
	privacy[QStringLiteral("value")] = QStringLiteral("EVERYONE");
	o[QStringLiteral("privacy")] = privacy;
	// Opt-in explicito (defecto OFF): solo entonces se envia.
	if (persistentOptIn)
		o[QStringLiteral("persistent_stream_key_status")] =
			QStringLiteral("ENABLE");
	return o;
}

// T-077 (Fase A): primer intento Status SIN privacy (paridad lab).
QString facebookStatusFields()
{
	return QStringLiteral("id,title,description,status");
}

// T-077 (Fase A): espejo del criterio del backend (adapters/
// facebook.py): el mensaje Meta manda sobre el HTTP 200.
bool facebookIsPersistentKeyError(const QString &metaMessage)
{
	return metaMessage.contains(
		QStringLiteral("persistent stream key"),
		Qt::CaseInsensitive);
}

// T-077 (Fase A): rechazo de clave persistente (F-092: ni con ENABLE
// la URL es unica; cada LiveVideo trae su propia ingesta).
QString fbPersistentNotAllowedMessage()
{
	return QStringLiteral(
		"Facebook: this account does not allow a persistent key "
		"for this live video. Retry with the persistent-key "
		"option off (each live video carries its own ingest).");
}

// T-071 FB-2: taxonomia §28 + tabla brief §5. metaCode = Graph `code`
// (190, 1363120/1363144 elegibilidad, 10, 613/4/17 rate). httpStatus =
// HTTP real. Los codigos Meta mandan sobre el HTTP 200.
// T-073 FB-4 (F-080): subcode 33 = objeto web/ID desconocido
// (GraphMethodException) -> NotFound para no mentir con "titulo invalido".
Outcome classifyFb(int httpStatus, int metaCode)
{
	if (metaCode == 1363120 || metaCode == 1363144 || metaCode == 10)
		return Outcome::Forbidden;
	if (metaCode == 190)
		return Outcome::AuthRequired;
	if (metaCode == 613 || metaCode == 4 || metaCode == 17)
		// F-097 (T-078 lab): Meta limita re-handshakes RTMPS
		// seguidos (cooldown observado ~3min entre emisiones).
		// Clasificacion sin reintento agresivo: el dock jamas
		// reencola go_live/end solo (sin scheduleBackoff ahi);
		// la redaccion del cooldown la pone la vista (ux-ui).
		return Outcome::RateLimited;
	if (metaCode == 33)
		return Outcome::NotFound;
	return classifyStatus(httpStatus);
}

QString fbEligibilityMessage(int metaCode)
{
	if (metaCode == 1363120)
		return QStringLiteral(
			"Facebook: account must be at least 60 days old "
			"to go live (code 1363120).");
	if (metaCode == 1363144)
		return QStringLiteral(
			"Facebook: Page needs at least 100 followers "
			"to go live (code 1363144).");
	if (metaCode == 10)
		return QStringLiteral(
			"Facebook: missing permissions or app approval "
			"(publish_video / pages_*).");
	if (metaCode == 33 || metaCode == 100)
		return QStringLiteral(
			"Facebook: live video not manageable via API "
			"(unknown or web object). Create one from the dock.");
	if (metaCode == 190)
		return QStringLiteral(
			"Facebook: session expired. Reconnect your account.");
	return QString();
}

FbLiveState fbLiveState(const QString &status)
{
	const QString s = status.trimmed().toUpper();
	if (s == QStringLiteral("LIVE") ||
	    s == QStringLiteral("LIVE_NOW"))
		return FbLiveState::Live;
	if (s == QStringLiteral("UNPUBLISHED") ||
	    s.startsWith(QStringLiteral("SCHEDULED")))
		return FbLiveState::Preview;
	if (s == QStringLiteral("VOD") ||
	    s == QStringLiteral("LIVE_STOPPED") ||
	    // F-098 (T-078 lab): al retirar la senal el objeto genera
	    // VOD automaticamente (End -> LIVE_STOPPED + VOD salvo
	    // borrado explicito). Ambos estados son "terminado".
	    s == QStringLiteral("COMPLETE"))
		return FbLiveState::Ended;
	return FbLiveState::Unknown;
}

QString fbLiveStateLabel(FbLiveState s)
{
	switch (s) {
	case FbLiveState::Live:
		return QStringLiteral("LIVE");
	case FbLiveState::Preview:
		return QStringLiteral("Preview");
	case FbLiveState::Ended:
		return QStringLiteral("Ended");
	default:
		break;
	}
	return QStringLiteral("Unknown");
}

FbAdoptResult facebookAdoptActive(const QList<FbLiveItem> &items)
{
	QList<QString> live;
	for (const FbLiveItem &it : items) {
		const QString id = it.id.trimmed();
		if (id.isEmpty())
			continue;
		const QString st = it.status.trimmed().toUpper();
		if (st != QStringLiteral("LIVE") &&
		    st != QStringLiteral("LIVE_NOW"))
			continue;
		live.append(id);
	}
	if (live.isEmpty())
		return {false, QString(), QStringLiteral("no-active")};
	if (live.size() > 1)
		return {false, QString(), QStringLiteral("ambiguous")};
	return {true, live.first(), QString()};
}

QStringList fbRequiredScopes(bool page)
{
	if (page)
		return QStringList{QStringLiteral("pages_manage_posts"),
				   QStringLiteral("pages_read_engagement"),
				   QStringLiteral("pages_show_list")};
	return QStringList{QStringLiteral("publish_video")};
}

QString facebookAuthUrl(const QString &clientId, const QString &redirectUri,
			const QString &state, const QString &scope,
			const QString &challenge)
{
	QStringList parts;
	parts << QStringLiteral("client_id=") +
			 QUrl::toPercentEncoding(clientId);
	parts << QStringLiteral("redirect_uri=") +
			 QUrl::toPercentEncoding(redirectUri);
	parts << QStringLiteral("state=") +
			 QUrl::toPercentEncoding(state);
	parts << QStringLiteral("scope=") + QUrl::toPercentEncoding(scope);
	parts << QStringLiteral("response_type=code");
	parts << QStringLiteral("code_challenge=") +
			 QUrl::toPercentEncoding(challenge);
	parts << QStringLiteral("code_challenge_method=S256");
	return QStringLiteral(
		       "https://www.facebook.com/v26.0/dialog/oauth?") +
	       parts.join(QLatin1Char('&'));
}

QString facebookPrivacyValue(const QJsonValue &v)
{
	if (v.isObject())
		return v.toObject()
			.value(QStringLiteral("value"))
			.toString()
			.trimmed();
	if (v.isString())
		return v.toString().trimmed();
	return QString();
}

FbVerifyResult facebookVerifyPrepared(const QString &expTitle,
				      const QString &expDesc,
				      const QString &actTitle,
				      const QString &actDesc,
				      const QString &actStatus,
				      const QString &actPrivacy)
{
	if (actStatus.isEmpty())
		return {FbVerify::Blocked,
			QStringLiteral("status-missing")};
	if (actStatus != QStringLiteral("UNPUBLISHED"))
		return {FbVerify::Blocked,
			QStringLiteral("status-not-unpublished")};
	if (actTitle != expTitle || actDesc != expDesc)
		return {FbVerify::Blocked, QStringLiteral("mismatch")};
	if (actPrivacy.isEmpty())
		return {FbVerify::AudienceUnverified,
			QStringLiteral("privacy-unverified")};
	if (actPrivacy.compare(QStringLiteral("EVERYONE"),
				Qt::CaseInsensitive) != 0)
		return {FbVerify::AudienceUnverified,
			QStringLiteral("privacy-not-public")};
	return {FbVerify::Verified, QStringLiteral("verified")};
}

// Fase A auto-preparacion (puro): fallback titulo/desc.
QString fbAutoCreateTitle(const QString &widgetTitle,
			 const QString &prevTitle)
{
	const QString w = widgetTitle.trimmed();
	if (!w.isEmpty())
		return w;
	return prevTitle.trimmed();
}

QString fbAutoCreateDesc(const QString &widgetDesc,
			const QString &prevDesc)
{
	const QString w = widgetDesc.trimmed();
	if (!w.isEmpty())
		return w;
	return prevDesc.trimmed();
}

bool fbPrevIsReusablePreview(const QString &prevStatus)
{
	return prevStatus == QStringLiteral("UNPUBLISHED");
}

bool fbAutoShouldDeletePrev(const QString &prevStatus,
			   const QString &prevId, const QString &newId)
{
	const QString p = prevId.trimmed();
	const QString n = newId.trimmed();
	if (p.isEmpty() || n.isEmpty() || p == n)
		return false;
	return fbPrevIsReusablePreview(prevStatus.trimmed());
}

QString fbApplyChainNote(const FbVerifyResult &v)
{
	if (v.outcome == FbVerify::Verified)
		return QString();
	if (v.outcome == FbVerify::AudienceUnverified)
		return QStringLiteral(
			" — title/desc ok, audience not verified");
	return QStringLiteral(" — title/desc written, not verified");
}

// Plan v3 Fase 1: puros de escucha/colas (sin I/O/red/logs).
const char *fbSignalDirLeaf()
{
	return "obs-stream-metadata-relay";
}

const char *fbSignalOnFile()
{
	return "signal.on";
}

const char *fbSignalOffFile()
{
	return "signal.off";
}

long long fbSignalBackupMs()
{
	return 3000;
}

long long fbSignalFastMs(int step)
{
	if (step <= 0)
		return 2000;
	return 1000;
}

bool fbSignalDropCountsAsGone(int consecutiveFails)
{
	return consecutiveFails >= 3;
}

bool fbSignalBallotUnstable(int presentCount, int total)
{
	if (total != 3)
		return false;
	return presentCount == 1 || presentCount == 2;
}

bool fbAutoEndGuardsOk(qint64 goneContinuousMs, qint64 goLiveAtMs,
		       qint64 nowMs, bool sameIdFreshConfirmed,
		       bool initiatedByPlugin)
{
	if (!initiatedByPlugin || !sameIdFreshConfirmed)
		return false;
	if (goneContinuousMs < 30000)
		return false;
	if (goLiveAtMs <= 0 || nowMs - goLiveAtMs < 300000)
		return false;
	return true;
}

bool fbRefireDue(qint64 fireAtMs, qint64 nowMs)
{
	if (fireAtMs <= 0)
		return false;
	return nowMs >= fireAtMs;
}

Outcome classifyStatus(int code)
{
	if (code >= 200 && code < 300)
		return Outcome::Success;
	switch (code) {
	case 400:
		return Outcome::BadRequest;
	case 401:
		return Outcome::AuthRequired;
	case 403:
		return Outcome::Forbidden;
	case 404:
		return Outcome::NotFound;
	case 409:
		return Outcome::Conflict;
	case 429:
		return Outcome::RateLimited;
	default:
		break;
	}
	if (code >= 500 && code < 600)
		return Outcome::ServerRetry;
	return Outcome::NetworkError;
}

const int kBackoffMaxRetries = 2;
const int kBackoffBaseMs = 2000;

int backoffDelayMs(int attempt)
{
	if (attempt <= 0)
		return 0;
	return kBackoffBaseMs * attempt;
}

RevokeEndpoint revokeEndpoint(Platform p)
{
	switch (p) {
	case Platform::Twitch:
		return {"https://id.twitch.tv/oauth2/revoke", "", false};
	case Platform::YouTube:
		return {"https://oauth2.googleapis.com/revoke", "token",
			false};
	case Platform::Kick:
		return {"https://id.kick.com/oauth/revoke", "token", true};
	case Platform::Facebook:
		// D10: DELETE /{user-id}/permissions (aqui via /me) con bearer.
		// El dock envia DELETE, no form (tokenField "DELETE" = sentinel).
		return {"https://graph.facebook.com/v26.0/me/permissions",
			"DELETE", false};
	}
	return {"", "", false};
}

QString userMessage(Outcome o, Platform p)
{	switch (o) {
	case Outcome::Success:
		return QStringLiteral("%1 updated.").arg(platformName(p));
	case Outcome::BadRequest:
		if (p == Platform::YouTube)
			return QStringLiteral(
				"YouTube rejected the input (title 1-100, "
				"description up to 5000).");
		if (p == Platform::Facebook)
			return QStringLiteral(
				"Facebook rejected the input (title 1-254).");
		return QStringLiteral("%1 rejected the title.").arg(
			platformName(p));
	case Outcome::AuthRequired:
		if (p == Platform::Facebook)
			return QStringLiteral(
				"Facebook: session expired. Reconnect your "
				"account.");
		return QStringLiteral(
			"%1: not authorized. Reconnect your account.")
			.arg(platformName(p));
	case Outcome::Forbidden:
		if (p == Platform::Facebook)
			return QStringLiteral(
				"Facebook: the account did not grant the "
				"required permissions (publish_video / pages_*). "
				"Accounts need 60+ days and Pages 100+ "
				"followers to go live.");
		return QStringLiteral(
			"%1: the account did not grant the required "
			"permissions.")
			.arg(platformName(p));
	case Outcome::NotFound:
		if (p == Platform::YouTube)
			return QStringLiteral(
				"YouTube: no valid broadcast found. Refresh "
				"broadcasts and select one.");
		if (p == Platform::Facebook)
			return QStringLiteral(
				"Facebook: live video not found or not "
				"manageable via API. Create one from the dock "
				"or paste a valid ID.");
		return QStringLiteral("%1: resource not found.")
			.arg(platformName(p));
	case Outcome::Conflict:
		return QStringLiteral(
			"%1: not modifiable in its current state.")
			.arg(platformName(p));
	case Outcome::RateLimited:
		return QStringLiteral("%1: rate limited. Wait and retry.")
			.arg(platformName(p));
	case Outcome::ServerRetry:
		return QStringLiteral(
			"%1: temporary provider error. Retry shortly.")
			.arg(platformName(p));
	case Outcome::NetworkError:
		return QStringLiteral("%1: network error. Check connection.")
			.arg(platformName(p));
	}
	return QStringLiteral("%1: unexpected error.").arg(platformName(p));
}

// T-047: distributed Client ID. OBS_TWITCH_CLIENT_ID comes from the
// build (CMake OBS_TWITCH_CLIENT_ID, default empty); the value is
// public per Twitch docs (DCF needs no secret), never logged.
QString twitchClientId(const QString &fieldValue)
{
	const QString field = fieldValue.trimmed();
	if (!field.isEmpty())
		return field;
#ifdef OBS_TWITCH_CLIENT_ID
	return QString::fromLatin1(OBS_TWITCH_CLIENT_ID);
#else
	return QString();
#endif
}

// T-047: mirrors the Op::TwPoll branches exactly (pending/slow_down
// consume one attempt; anything else terminates). pollsLeft is the
// count remaining AFTER this response.
TwPollAction classifyTwPoll(int http, bool netFail, const QString &message,
			    int pollsLeft)
{
	if (!netFail && http == 200)
		return TwPollAction::Consume;
	if (message.contains(QStringLiteral("authorization_pending")) &&
	    pollsLeft > 0)
		return TwPollAction::KeepPolling;
	if (message.contains(QStringLiteral("slow_down")) && pollsLeft > 0)
		return TwPollAction::SlowDown;
	if (message.contains(QStringLiteral("access_denied")))
		return TwPollAction::FailDenied;
	return TwPollAction::FailExpired;
}

// Fase 4: gate login previo a Managed (spec §5). Texto exacto único,
// reutilizado por las 4 plataformas en Connect/Apply/erase/status/
// broadcasts. Sin secretos, sin nombres de plataforma: el mismo para
// todas, para no filtrar ni inventar mapeos.
QString managedLoginRequiredMessage()
{
	return QStringLiteral(
		"Managed login required. Log in to use Managed.");
}

bool managedActionAllowed(bool hasUserSession)
{
	return hasUserSession;
}

QString userSessionHeaderName()
{
	return QStringLiteral("X-User-Session");
}

bool managedHeadersIncludeUserSession(bool isManaged)
{
	return isManaged;
}

QStringList forbiddenUserSessionShapes()
{
	return QStringList{QStringLiteral("user_session"),
			   QStringLiteral("userSession"),
			   QStringLiteral("X-User-Session")};
}

QString userNoRecoveryNotice()
{
	return QStringLiteral(
		"v1 account has no email: no account recovery. "
		"Keep your password safe.");
}

// f7: sin mensaje vigente (vacio) => gate; con mensaje (error
// especifico) => se preserva. El gate nunca pisa un error recien
// fijado: el llamador (vista) pasa el texto a mostrar.
QString managedLoggedOutStatus(const QString &pending)
{
	if (!pending.trimmed().isEmpty())
		return pending;
	return managedLoginRequiredMessage();
}

// f7: 409 signup => sugerir Log in en vez de reintentar Sign up.
QString managedSignupTakenMessage()
{
	return QStringLiteral(
		"Sign up failed (username may be taken or invalid). "
		"Try Log in with that username instead of signing up again.");
}

// f8 REDISENO ENTRAR (2026-10-26 [D]): modos excluyentes + validacion
// local con las reglas exactas del backend (backend/users.py, solo
// lectura: USERNAME_MIN_LEN=3, USERNAME_MAX_LEN=32,
// _USERNAME_RE=[A-Za-z0-9._-] fullmatch con strip previo;
// MIN_PASSWORD_LEN=12, MAX_PASSWORD_LEN=128, sin strip ni complejidad
// inventada).
const char *managedAuthModeName(ManagedAuthMode m)
{
	switch (m) {
	case ManagedAuthMode::Login:
		return "Login";
	case ManagedAuthMode::Signup:
		return "Signup";
	}
	return "?";
}

bool managedAuthLoginVisible(ManagedAuthMode m)
{
	return m == ManagedAuthMode::Login;
}

bool managedAuthSignupVisible(ManagedAuthMode m)
{
	return m == ManagedAuthMode::Signup;
}

static bool managedUserCharOk(QChar c)
{
	const ushort u = c.unicode();
	return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
	       (u >= '0' && u <= '9') || c == QLatin1Char('.') ||
	       c == QLatin1Char('_') || c == QLatin1Char('-');
}

QString validateManagedUsername(const QString &raw)
{
	const QString user = raw.trimmed();
	if (user.size() < 3 || user.size() > 32)
		return QStringLiteral(
			"Username must be 3-32 characters.");
	for (const QChar c : user) {
		if (!managedUserCharOk(c))
			return QStringLiteral(
				"Username may only contain letters, numbers, "
				"dot, underscore or hyphen.");
	}
	return QString();
}

QString validateManagedPassword(const QString &raw)
{
	if (raw.size() < 12)
		return QStringLiteral(
			"Password must be at least 12 characters.");
	if (raw.size() > 128)
		return QStringLiteral(
			"Password must be at most 128 characters.");
	return QString();
}

QString validateManagedCredentials(const QString &user,
				  const QString &pass)
{
	const QString uErr = validateManagedUsername(user);
	if (!uErr.isEmpty())
		return uErr;
	return validateManagedPassword(pass);
}

QString managedPasswordPolicyNotice()
{
	return QStringLiteral(
		"Password: minimum 12 characters (up to 128). "
		"v1 account has no email: no account recovery. "
		"Keep your password safe.");
}

// f6 ENTRAR-primero: Managed sin sesión => ENTRAR; con sesión => PANEL.
// Independent => jamás ENTRAR (una sola pantalla, intacto).
bool managedEnterVisible(bool isManaged, bool hasUserSession)
{
	return isManaged && !hasUserSession;
}

bool managedPanelVisible(bool isManaged, bool hasUserSession)
{
	return !isManaged || hasUserSession;
}

// AUTORIZACION [D usuario 2026-10-26]: solo con sesion en memoria tras
// el restore tiene sentido una llamada de red (me); sin sesion, ENTRAR
// directo sin red. Puro QtCore.
bool managedStartupShouldRevalidate(bool hasUserSession)
{
	return hasUserSession;
}

// Bugfix visual 2026-10-26 [D]: clasificacion pura estado->tono.
// Espeja exactamente la regla que la vista aplica en updateCardStyle:
// tono Error si el status empieza por "Error", es "Needs
// reconnection." o el gate login-required, o el result lleva " ✗ "
// (formato "%1 ✗ %3" de setResult); Brand si conectada sin error;
// Neutral en otro caso (limpio/nunca-tocado/logout = gris, sin texto
// de error). La vista garantiza no escribir textos de error en
// plataformas limpias, asi el rojo nunca se pega.
CardTone cardToneFor(bool connected, const QString &statusText,
		     const QString &resultText)
{
	const bool failed =
		statusText.startsWith(QStringLiteral("Error")) ||
		statusText == QStringLiteral("Needs reconnection.") ||
		statusText == managedLoginRequiredMessage() ||
		resultText.contains(QStringLiteral(" \u2717 "));
	if (failed)
		return CardTone::Error;
	if (connected)
		return CardTone::Brand;
	return CardTone::Neutral;
}

// Fase 5: borrado de CUENTA (/users/erase) separado de F-C2
// (/privacy/erase). Rutas canónicas distintas por construcción; el
// resumen de respuesta solo incrusta enteros (conteos por tabla).
const char *accountErasePath()
{
	return "/users/erase";
}

const char *managedDataErasePath()
{
	return "/privacy/erase";
}

QString accountEraseCountsMessage(int users, int installations, int sessions,
				  int connections, int tokens)
{
	return QStringLiteral("Account erased (users=%1, installations=%2, "
			      "sessions=%3, connections=%4, tokens=%5).")
		.arg(users)
		.arg(installations)
		.arg(sessions)
		.arg(connections)
		.arg(tokens);
}

} // namespace meta

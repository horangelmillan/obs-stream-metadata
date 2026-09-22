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

// T-071 FB-2: taxonomia §28 + tabla brief §5. metaCode = Graph `code`
// (190, 1363120/1363144 elegibilidad, 10, 613/4/17 rate). httpStatus =
// HTTP real. Los codigos Meta mandan sobre el HTTP 200.
Outcome classifyFb(int httpStatus, int metaCode)
{
	if (metaCode == 1363120 || metaCode == 1363144 || metaCode == 10)
		return Outcome::Forbidden;
	if (metaCode == 190)
		return Outcome::AuthRequired;
	if (metaCode == 613 || metaCode == 4 || metaCode == 17)
		return Outcome::RateLimited;
	return classifyStatus(httpStatus);
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
{
	switch (o) {
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
				"Facebook: live video not found. Refresh the "
				"list and select one.");
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

} // namespace meta

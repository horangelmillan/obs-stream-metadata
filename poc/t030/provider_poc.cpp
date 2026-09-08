/*
T-030: implementaciones QtCore. Sin E/S, sin red, sin secretos.
*/

#include "provider_poc.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace t030 {

const char *kTwitchDeviceUrl = "https://id.twitch.tv/oauth2/device";
const char *kTwitchTokenUrl = "https://id.twitch.tv/oauth2/token";
const char *kTwitchValidateUrl = "https://id.twitch.tv/oauth2/validate";
const char *kTwitchChannelsUrl = "https://api.twitch.tv/helix/channels";
const char *kTwitchScope = "channel:manage:broadcast";
const int kTwitchTitleMax = 140;

const char *kYouTubeAuthUrl = "https://accounts.google.com/o/oauth2/v2/auth";
const char *kYouTubeTokenUrl = "https://oauth2.googleapis.com/token";
const char *kYouTubeScope = "https://www.googleapis.com/auth/youtube.force-ssl";
const int kYouTubeTitleMin = 1;
const int kYouTubeTitleMax = 100;
const int kYouTubeDescMax = 5000;

const char *kKickAuthUrl = "https://id.kick.com/oauth/authorize";
const char *kKickTokenUrl = "https://id.kick.com/oauth/token";
const char *kKickRevokeUrl = "https://id.kick.com/oauth/revoke";
const char *kKickChannelsUrl = "https://api.kick.com/public/v1/channels";
const char *kKickScopeWrite = "channel:write";

// ponytail: rand determinista de Qt basta para PoC; CSPRNG del SO en P5.
static QString randToken(int bytes)
{
	QString out;
	quint32 s = (quint32)QUuid::createUuid().data1 ^ (quint32)qHash(QUrl());
	while (out.size() < bytes) {
		s = s * 1664525u + 1013904223u;
		out += QString::number(s, 36);
	}
	return out.left(bytes);
}

QString generateCodeVerifier()
{
	// 64 chars del alfabeto unreserved RFC 7636.
	const char *alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			    "abcdefghijklmnopqrstuvwxyz"
			    "0123456789-._~";
	QString v = randToken(64);
	for (int i = 0; i < v.size(); ++i)
		v[i] = QChar(alpha[v.at(i).unicode() % 66]);
	return v;
}

QString codeChallengeS256(const QString &verifier)
{
	const QByteArray hash = QCryptographicHash::hash(
		verifier.toLatin1(), QCryptographicHash::Sha256);
	return QString::fromLatin1(
		hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString generateState()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces)
		.remove(QLatin1Char('-'))
		.left(32);
}

static QString authUrl(const char *base, const QString &clientId,
		       const QString &redirectUri, const QString &state,
		       const QString &challenge, const QString &scope,
		       bool pkce)
{
	QUrl url(base);
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
	q.addQueryItem(QStringLiteral("client_id"), clientId);
	q.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
	q.addQueryItem(QStringLiteral("scope"), scope);
	q.addQueryItem(QStringLiteral("state"), state);
	if (pkce) {
		q.addQueryItem(QStringLiteral("code_challenge"), challenge);
		q.addQueryItem(QStringLiteral("code_challenge_method"),
			       QStringLiteral("S256"));
	}
	url.setQuery(q);
	return url.toString(QUrl::FullyEncoded);
}

QString twitchAuthCodeUrl(const QString &clientId, const QString &redirectUri,
			  const QString &state, const QString &scope)
{
	return authUrl("https://id.twitch.tv/oauth2/authorize", clientId,
		       redirectUri, state, QString(),
		       scope.isEmpty() ? QString::fromLatin1(kTwitchScope) : scope,
		       false);
}

QString youTubeAuthUrl(const QString &clientId, const QString &redirectUri,
		       const QString &state, const QString &codeChallenge,
		       const QString &scope)
{
	return authUrl(kYouTubeAuthUrl, clientId, redirectUri, state,
		       codeChallenge,
		       scope.isEmpty() ? QString::fromLatin1(kYouTubeScope) : scope,
		       true);
}

QString kickAuthUrl(const QString &clientId, const QString &redirectUri,
		    const QString &state, const QString &codeChallenge,
		    const QString &scope)
{
	return authUrl(kKickAuthUrl, clientId, redirectUri, state, codeChallenge,
		       scope.isEmpty() ? QString::fromLatin1(kKickScopeWrite)
				       : scope,
		       true);
}

bool twitchTitleOk(const QString &title, QString *reason)
{
	if (title.isEmpty()) {
		if (reason)
			*reason = QStringLiteral("empty");
		return false;
	}
	if (title.size() > kTwitchTitleMax) {
		if (reason)
			*reason = QStringLiteral("over-140");
		return false;
	}
	return true;
}

bool youTubeTitleOk(const QString &title, QString *reason)
{
	if (title.size() < kYouTubeTitleMin) {
		if (reason)
			*reason = QStringLiteral("empty");
		return false;
	}
	if (title.size() > kYouTubeTitleMax) {
		if (reason)
			*reason = QStringLiteral("over-100");
		return false;
	}
	return true;
}

bool youTubeDescOk(const QString &desc, QString *reason)
{
	if (desc.size() > kYouTubeDescMax) {
		if (reason)
			*reason = QStringLiteral("over-5000");
		return false;
	}
	return true;
}

bool kickTitleOk(const QString &title, QString *reason)
{
	if (title.isEmpty()) {
		if (reason)
			*reason = QStringLiteral("empty");
		return false;
	}
	return true;
}

QString twitchTitlePayload(const QString &title)
{
	QJsonObject o;
	o[QStringLiteral("title")] = title;
	return QString::fromUtf8(
		QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString kickTitlePayload(const QString &title)
{
	QJsonObject o;
	o[QStringLiteral("stream_title")] = title;
	return QString::fromUtf8(
		QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString youTubeSnippetPayload(const QString &fetchedJson, const QString &title,
			      const QString &desc)
{
	// Parte del recurso obtenido por liveBroadcasts.list (GET previo).
	const QJsonObject root =
		QJsonDocument::fromJson(fetchedJson.toUtf8()).object();
	QJsonObject out;
	out[QStringLiteral("id")] = root.value(QStringLiteral("id"));
	QJsonObject snippet =
		root.value(QStringLiteral("snippet")).toObject();
	snippet[QStringLiteral("title")] = title;
	snippet[QStringLiteral("description")] = desc;
	out[QStringLiteral("snippet")] = snippet;
	// Con part=snippet el body NO debe incluir contentDetails: la API
	// viva lo rechaza con 400 unexpectedPart (corrige F-016). El snippet
	// se envia completo (preserva scheduledStartTime, categoryId...).
	return QString::fromUtf8(
		QJsonDocument(out).toJson(QJsonDocument::Compact));
}

HttpOutcome classifyHttpStatus(int code)
{
	if (code >= 200 && code < 300)
		return HttpOutcome::Success;
	switch (code) {
	case 400:
		return HttpOutcome::BadRequest;
	case 401:
		return HttpOutcome::AuthRequired;
	case 403:
		return HttpOutcome::ForbiddenScope;
	case 404:
		return HttpOutcome::NotFound;
	case 409:
		return HttpOutcome::ConflictState;
	case 429:
		return HttpOutcome::RateLimited;
	default:
		break;
	}
	if (code >= 500 && code < 600)
		return HttpOutcome::ServerRetry;
	return HttpOutcome::NetworkError;
}

const char *outcomeName(HttpOutcome o)
{
	switch (o) {
	case HttpOutcome::Success:
		return "success";
	case HttpOutcome::BadRequest:
		return "bad-request";
	case HttpOutcome::AuthRequired:
		return "auth-required";
	case HttpOutcome::ForbiddenScope:
		return "forbidden-scope";
	case HttpOutcome::NotFound:
		return "not-found";
	case HttpOutcome::ConflictState:
		return "conflict-state";
	case HttpOutcome::RateLimited:
		return "rate-limited";
	case HttpOutcome::ServerRetry:
		return "server-retry";
	case HttpOutcome::NetworkError:
		return "network-error";
	}
	return "unknown";
}

} // namespace t030

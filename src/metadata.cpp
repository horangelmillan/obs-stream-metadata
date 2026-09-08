/*
T-031: product metadata core. QtCore only, no I/O, no network, no secrets.
*/

#include "metadata.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace meta {

const int kTwitchTitleMax = 140;
const int kYouTubeTitleMin = 1;
const int kYouTubeTitleMax = 100;
const int kYouTubeDescMax = 5000;

const char *platformName(Platform p)
{
	switch (p) {
	case Platform::Twitch:
		return "Twitch";
	case Platform::YouTube:
		return "YouTube";
	case Platform::Kick:
		return "Kick";
	}
	return "?";
}

bool supportsDescription(Platform p)
{
	return p == Platform::YouTube;
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
	if (maxTitle > 0 && m.title.size() > maxTitle)
		return QStringLiteral("Title too long for the selected "
				      "platforms (max %1 characters).")
			.arg(maxTitle);
	if (s.youtube && m.description.size() > kYouTubeDescMax)
		return QStringLiteral("Description too long (max %1 characters).")
			.arg(kYouTubeDescMax);
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
		return QStringLiteral("%1 rejected the title.").arg(
			platformName(p));
	case Outcome::AuthRequired:
		return QStringLiteral(
			"%1: not authorized. Reconnect your account.")
			.arg(platformName(p));
	case Outcome::Forbidden:
		return QStringLiteral(
			"%1: the account did not grant the required "
			"permissions.")
			.arg(platformName(p));
	case Outcome::NotFound:
		if (p == Platform::YouTube)
			return QStringLiteral(
				"YouTube: no valid broadcast found. Refresh "
				"broadcasts and select one.");
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

} // namespace meta

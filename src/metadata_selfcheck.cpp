// T-031 product selfcheck: validation + payloads + outcome mapping.
// T-032: + backoff policy + revoke endpoints + DPAPI store round-trip.
// T-033 (P6): + offline negatives AGENTS.md §40 (no network).
// No network. Test-only fake values, never real credentials.
// Usage: metadata-selfcheck.exe -> exit 0.
#include "metadata.h"
#include "secure_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

#define CHECK(cond, label)                                           \
	do {                                                         \
		if (!(cond)) {                                       \
			std::printf("FAIL %s\n", label);             \
			return 1;                                    \
		}                                                    \
		std::printf("PASS %s\n", label);                     \
	} while (0)

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	using namespace meta;

	Selection none{};
	Metadata m{QStringLiteral("Hello"), QString()};
	CHECK(!validate(m, none).isEmpty(), "none-selected");

	Selection tw{};
	tw.twitch = true;
	CHECK(!validate(Metadata{QString(), QString()}, tw).isEmpty(),
	      "title-required");
	CHECK(!validate(Metadata{QString(141, QLatin1Char('x')), QString()},
			tw)
		      .isEmpty(),
	      "twitch-141");
	CHECK(validate(Metadata{QString(140, QLatin1Char('x')), QString()},
		       tw)
		      .isEmpty(),
	      "twitch-140");

	Selection yt{};
	yt.youtube = true;
	CHECK(!validate(Metadata{QString(101, QLatin1Char('x')), QString()},
			yt)
		      .isEmpty(),
	      "youtube-101");
	CHECK(!validate(Metadata{QStringLiteral("T"),
				 QString(5001, QLatin1Char('x'))},
			yt)
		      .isEmpty(),
	      "ytdesc-5001");
	CHECK(validate(Metadata{QStringLiteral("T"), QStringLiteral("D")},
		       yt)
		      .isEmpty(),
	      "youtube-ok");

	// Most restrictive wins: Twitch+YouTube with 101 chars fails.
	Selection both{};
	both.twitch = true;
	both.youtube = true;
	CHECK(!validate(Metadata{QString(101, QLatin1Char('x')), QString()},
			both)
		      .isEmpty(),
	      "mixed-101-fails");

	// Description without YouTube never blocks the title (§11).
	CHECK(validate(Metadata{QStringLiteral("T"),
				QString(5001, QLatin1Char('x'))},
		       tw)
		      .isEmpty(),
	      "desc-ignored-without-youtube");

	// Kick-only: long titles are the server's call (no silent truncate).
	Selection kk{};
	kk.kick = true;
	CHECK(validate(Metadata{QString(200, QLatin1Char('x')), QString()},
		       kk)
		      .isEmpty(),
	      "kick-server-decides");

	CHECK(!supportsDescription(Platform::Twitch), "twitch-no-desc");
	CHECK(supportsDescription(Platform::YouTube), "youtube-desc");
	CHECK(!supportsDescription(Platform::Kick), "kick-no-desc");

	CHECK(twitchPayload(QStringLiteral("A")) ==
		      QStringLiteral("{\"title\":\"A\"}"),
	      "twitch-payload");
	CHECK(kickPayload(QStringLiteral("A")) ==
		      QStringLiteral("{\"stream_title\":\"A\"}"),
	      "kick-payload");

	CHECK(classifyStatus(204) == Outcome::Success, "http-204");
	CHECK(classifyStatus(401) == Outcome::AuthRequired, "http-401");
	CHECK(classifyStatus(429) == Outcome::RateLimited, "http-429");
	const QString msg = userMessage(Outcome::AuthRequired,
					Platform::Twitch);
	CHECK(!msg.isEmpty() && !msg.contains(QStringLiteral("Bearer")) &&
		      !msg.contains(QStringLiteral("token")),
	      "message-safe");

	// T-033 (P6): §40 negative boundaries offline — no network.
	// YouTube title minimum (empty) and upper edges.
	CHECK(!validate(Metadata{QString(), QString()}, yt).isEmpty(),
	      "youtube-empty-title");
	CHECK(validate(Metadata{QString(100, QLatin1Char('x')), QString()},
		       yt)
		      .isEmpty(),
	      "youtube-100");
	CHECK(validate(Metadata{QStringLiteral("T"),
				QString(5000, QLatin1Char('x'))},
		       yt)
		      .isEmpty(),
	      "ytdesc-5000");
	// Mixed Twitch+YouTube stays restrictive at the Twitch edge too.
	CHECK(!validate(Metadata{QString(141, QLatin1Char('x')), QString()},
			both)
		      .isEmpty(),
	      "mixed-141-fails");

	// Full §28 status map (offline classification only).
	CHECK(classifyStatus(200) == Outcome::Success, "http-200");
	CHECK(classifyStatus(400) == Outcome::BadRequest, "http-400");
	CHECK(classifyStatus(403) == Outcome::Forbidden, "http-403");
	CHECK(classifyStatus(404) == Outcome::NotFound, "http-404");
	CHECK(classifyStatus(409) == Outcome::Conflict, "http-409");
	CHECK(classifyStatus(500) == Outcome::ServerRetry, "http-500");
	CHECK(classifyStatus(503) == Outcome::ServerRetry, "http-503");
	CHECK(classifyStatus(0) == Outcome::NetworkError, "http-0");
	CHECK(classifyStatus(999) == Outcome::NetworkError, "http-999");

	// Every user message is non-empty, safe, and specific (§16, §28).
	{
		const Outcome all[] = {
			Outcome::Success,     Outcome::BadRequest,
			Outcome::AuthRequired, Outcome::Forbidden,
			Outcome::NotFound,    Outcome::Conflict,
			Outcome::RateLimited, Outcome::ServerRetry,
			Outcome::NetworkError,
		};
		const Platform plats[] = {Platform::Twitch,
					  Platform::YouTube, Platform::Kick};
		for (Outcome o : all) {
			for (Platform p : plats) {
				const QString um = userMessage(o, p);
				if (um.isEmpty() ||
				    um.contains(QStringLiteral("Bearer")) ||
				    um.contains(QStringLiteral("token")) ||
				    um.contains(QStringLiteral("http"))) {
					std::printf("FAIL message-safe-all\n");
					return 1;
				}
			}
		}
		std::printf("PASS message-safe-all\n");
	}
	CHECK(userMessage(Outcome::BadRequest, Platform::YouTube)
		      .contains(QStringLiteral("1-100")),
	      "msg-yt-400");
	CHECK(userMessage(Outcome::NotFound, Platform::YouTube)
		      .contains(QStringLiteral("broadcast")),
	      "msg-yt-404");
	CHECK(userMessage(Outcome::Forbidden, Platform::Kick)
		      .contains(QStringLiteral("permissions")),
	      "msg-kick-403");
	CHECK(userMessage(Outcome::Conflict, Platform::YouTube)
		      .contains(QStringLiteral("modifiable")),
	      "msg-yt-409");
	CHECK(userMessage(Outcome::RateLimited, Platform::Twitch)
		      .contains(QStringLiteral("rate limited")),
	      "msg-tw-429");
	CHECK(userMessage(Outcome::Success, Platform::Kick)
		      .contains(QStringLiteral("updated")),
	      "msg-kick-204");

	// YouTube PUT keeps id + fetched snippet fields, sets title/desc,
	// and never sends contentDetails with part=snippet (F-021).
	{
		const QString fetched = QStringLiteral(
			"{\"id\":\"B1\",\"snippet\":{\"title\":\"Old\","
			"\"description\":\"OldD\",\"categoryId\":\"22\","
			"\"scheduledStartTime\":\"2026-09-09T00:00:00Z\"},"
			"\"contentDetails\":{\"monitorStream\":{}}}");
		const QString out =
			youTubePayload(fetched, QStringLiteral("New"),
				       QStringLiteral("NewD"));
		const QJsonObject o =
			QJsonDocument::fromJson(out.toUtf8()).object();
		const QJsonObject sn =
			o.value(QStringLiteral("snippet")).toObject();
		CHECK(o.value(QStringLiteral("id")).toString() ==
			      QStringLiteral("B1"),
		      "ytpl-id");
		CHECK(sn.value(QStringLiteral("title")).toString() ==
			      QStringLiteral("New"),
		      "ytpl-title");
		CHECK(sn.value(QStringLiteral("description")).toString() ==
			      QStringLiteral("NewD"),
		      "ytpl-desc");
		CHECK(sn.value(QStringLiteral("categoryId")).toString() ==
			      QStringLiteral("22"),
		      "ytpl-preserve-cat");
		CHECK(sn.value(QStringLiteral("scheduledStartTime"))
			      .toString() ==
		      QStringLiteral("2026-09-09T00:00:00Z"),
		      "ytpl-preserve-start");
		CHECK(!o.contains(QStringLiteral("contentDetails")),
		      "ytpl-no-contentdetails");
	}

	// T-032: bounded backoff policy (no loops by construction).
	CHECK(kBackoffMaxRetries == 2, "backoff-max");
	CHECK(backoffDelayMs(0) == 0, "backoff-0");
	CHECK(backoffDelayMs(1) == 2000, "backoff-1");
	CHECK(backoffDelayMs(2) == 4000, "backoff-2");
	CHECK(backoffDelayMs(3) == 6000, "backoff-3");
	CHECK(backoffDelayMs(-1) == 0, "backoff-neg");

	// T-032: revoke endpoints mirror the P3-validated runners.
	CHECK(QString::fromLatin1(
		      revokeEndpoint(Platform::Twitch).url) ==
		      QStringLiteral("https://id.twitch.tv/oauth2/revoke"),
	      "revoke-twitch");
	CHECK(QString::fromLatin1(
		      revokeEndpoint(Platform::YouTube).url) ==
		      QStringLiteral("https://oauth2.googleapis.com/revoke"),
	      "revoke-youtube");
	CHECK(QString::fromLatin1(
		      revokeEndpoint(Platform::Kick).url) ==
		      QStringLiteral("https://id.kick.com/oauth/revoke"),
	      "revoke-kick");
	// T-033 (P6): revoke wire shape mirrors the P3 runners (F-030).
	// Twitch carries the token in the query (no form field, no browser
	// UA); YouTube posts a form field; Kick posts a form field with a
	// browser UA (F-022).
	CHECK(QString::fromLatin1(
		      revokeEndpoint(Platform::Twitch).tokenField) ==
		      QStringLiteral(""),
	      "revoke-tw-nofield");
	CHECK(!revokeEndpoint(Platform::Twitch).browserUa,
	      "revoke-tw-noua");
	CHECK(QString::fromLatin1(
		      revokeEndpoint(Platform::YouTube).tokenField) ==
		      QStringLiteral("token"),
	      "revoke-yt-field");
	CHECK(!revokeEndpoint(Platform::YouTube).browserUa,
	      "revoke-yt-noua");
	CHECK(QString::fromLatin1(
		      revokeEndpoint(Platform::Kick).tokenField) ==
		      QStringLiteral("token"),
	      "revoke-kick-field");
	CHECK(revokeEndpoint(Platform::Kick).browserUa, "revoke-kick-ua");

#ifdef Q_OS_WIN
	// T-032: DPAPI store round-trip with fake values; the file must
	// never contain them in plaintext.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "store-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		d.twitch.connected = true;
		d.twitch.display = QStringLiteral("fake-login");
		d.twitch.broadcaster = QStringLiteral("000001");
		d.twitch.access = QStringLiteral("fk-tok-4cc3ss-9z");
		d.twitch.refresh = QStringLiteral("fk-tok-r3fr3sh-9z");
		d.twitch.clientId = QStringLiteral("fk-client-id-9z");
		CHECK(store.save(d), "store-save");
		QFile raw(path);
		CHECK(raw.open(QIODevice::ReadOnly), "store-raw");
		const QByteArray bytes = raw.readAll();
		raw.close();
		CHECK(!bytes.contains("fk-tok-4cc3ss-9z"),
		      "store-no-plaintext-access");
		CHECK(!bytes.contains("fk-tok-r3fr3sh-9z"),
		      "store-no-plaintext-refresh");
		CHECK(bytes.contains("fake-login"),
		      "store-label-plaintext");
		secure::Data back;
		CHECK(store.load(back) && back.twitch.connected &&
			      back.twitch.access ==
				      QStringLiteral("fk-tok-4cc3ss-9z") &&
			      back.twitch.refresh ==
				      QStringLiteral("fk-tok-r3fr3sh-9z") &&
			      back.twitch.clientId ==
				      QStringLiteral("fk-client-id-9z") &&
			      back.twitch.display ==
				      QStringLiteral("fake-login") &&
			      back.twitch.broadcaster ==
				      QStringLiteral("000001") &&
			      !back.youtube.connected && !back.kick.connected,
		      "store-roundtrip");
		CHECK(store.clear() && !QFile::exists(path),
		      "store-clear");
		secure::Data empty;
		CHECK(!store.load(empty), "store-load-missing");
	}
#else
	CHECK(true, "store-skipped-non-windows");
#endif

	// T-049: ConnectionMode domain concept (no UI, no persistence yet).
	CHECK(QString::fromLatin1(connectionModeName(
					 ConnectionMode::Independent)) ==
		      QStringLiteral("Independent"),
	      "mode-name-independent");
	CHECK(QString::fromLatin1(connectionModeName(
					 ConnectionMode::Managed)) ==
		      QStringLiteral("Managed"),
	      "mode-name-managed");
	CHECK(defaultConnectionMode() == ConnectionMode::Independent,
	      "mode-default-independent");
	CHECK(parseConnectionMode(QStringLiteral("independent")) ==
		      ConnectionMode::Independent,
	      "mode-parse-independent");
	CHECK(parseConnectionMode(QStringLiteral("managed")) ==
		      ConnectionMode::Managed,
	      "mode-parse-managed");
	CHECK(!parseConnectionMode(QStringLiteral("BYO")).has_value() &&
		      !parseConnectionMode(QStringLiteral("Independent"))
			       .has_value() &&
		      !parseConnectionMode(QString()).has_value(),
	      "mode-parse-strict");
	{
		// Every (Platform, ConnectionMode) pair is representable;
		// support per pair is decided by T-041/T-048, not here.
		const Platform platforms[] = {Platform::Twitch,
					      Platform::YouTube, Platform::Kick};
		const ConnectionMode modes[] = {ConnectionMode::Independent,
						ConnectionMode::Managed};
		int pairs = 0;
		for (Platform p : platforms) {
			for (ConnectionMode m : modes) {
				if (platformName(p)[0] != '?' &&
				    connectionModeName(m)[0] != '?')
					++pairs;
			}
		}
		CHECK(pairs == 6, "mode-provider-pairs");
	}

	std::printf("SELFCHECK OK\n");
	return 0;
}

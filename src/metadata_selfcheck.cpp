// T-031 product selfcheck: validation + payloads + outcome mapping.
// T-032: + backoff policy + revoke endpoints + DPAPI store round-trip.
// No network. Test-only fake values, never real credentials.
// Usage: metadata-selfcheck.exe -> exit 0.
#include "metadata.h"
#include "secure_store.h"

#include <QCoreApplication>
#include <QFile>
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

	// T-032: bounded backoff policy (no loops by construction).
	CHECK(kBackoffMaxRetries == 2, "backoff-max");
	CHECK(backoffDelayMs(0) == 0, "backoff-0");
	CHECK(backoffDelayMs(1) == 2000, "backoff-1");
	CHECK(backoffDelayMs(2) == 4000, "backoff-2");

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

	std::printf("SELFCHECK OK\n");
	return 0;
}

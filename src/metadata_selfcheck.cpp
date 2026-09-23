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

	// T-041: connection mode persistence/migration (no DPAPI needed for
	// key-less files: empty records short-circuit before unprotect).
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "mode-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		QFile f(path);
		// Legacy file without the key -> Independent, no providers.
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "mode-write");
		f.write(QByteArrayLiteral("{}"));
		f.close();
		CHECK(!store.load(d) &&
			      d.connectionMode ==
				      QStringLiteral("independent"),
		      "mode-legacy-default");
		// Unknown value -> Independent (never silent Managed).
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "mode-write");
		f.write(QByteArrayLiteral("{\"connection_mode\":\"byo\"}"));
		f.close();
		CHECK(!store.load(d) &&
			      d.connectionMode ==
				      QStringLiteral("independent"),
		      "mode-invalid-default");
		// Explicit managed with zero providers restores the mode.
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "mode-write");
		f.write(QByteArrayLiteral(
			"{\"connection_mode\":\"managed\"}"));
		f.close();
		CHECK(!store.load(d) &&
			      d.connectionMode ==
				      QStringLiteral("managed"),
		      "mode-managed-alone");
	}
#ifdef Q_OS_WIN
	// T-041: round-trip with accounts + idempotent re-migration.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "mode-rt-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		d.twitch.connected = true;
		d.twitch.display = QStringLiteral("fake-login");
		d.twitch.access = QStringLiteral("fk-tok-4cc3ss-9z");
		d.twitch.clientId = QStringLiteral("fk-client-id-9z");
		d.connectionMode = QStringLiteral("managed");
		CHECK(store.save(d), "mode-rt-save");
		secure::Data back;
		CHECK(store.load(back) && back.twitch.connected &&
			      back.twitch.access ==
				      QStringLiteral("fk-tok-4cc3ss-9z") &&
			      back.twitch.clientId ==
				      QStringLiteral("fk-client-id-9z") &&
			      back.connectionMode ==
				      QStringLiteral("managed"),
		      "mode-rt-roundtrip");
		// Idempotence: load -> save -> load keeps mode + accounts.
		secure::Data again;
		CHECK(store.load(again), "mode-rt-reload");
		CHECK(store.save(again), "mode-rt-resave");
		secure::Data third;
		CHECK(store.load(third) && third.twitch.connected &&
			      third.connectionMode ==
				      QStringLiteral("managed"),
		      "mode-rt-idempotent");
	}
#else
	CHECK(true, "mode-rt-skipped-non-windows");
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

	// T-051: Managed snapshots (plaintext identity only, no DPAPI).
	// Key-less files need no crypto: empty records short-circuit.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "msnap-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		QFile f(path);
		// Legacy file without managed keys -> snapshots disconnected,
		// Independent intact.
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "msnap-write");
		f.write(QByteArrayLiteral(
			"{\"connection_mode\":\"managed\"}"));
		f.close();
		CHECK(!store.load(d) && !d.anyManaged() &&
			      !d.managedYoutube.connected &&
			      !d.managedKick.connected &&
			      d.connectionMode ==
				      QStringLiteral("managed"),
		      "msnap-legacy-clean");
		// Malformed managed objects -> disconnected, never half-open.
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "msnap-write");
		f.write(QByteArrayLiteral(
			"{\"managed_youtube\":{\"connected\":true},"
			"\"managed_kick\":{\"connected\":\"yes\","
			"\"user_id\":42,\"display\":[]}}"));
		f.close();
		CHECK(!store.load(d) && !d.anyManaged(),
		      "msnap-malformed-safe");
		// Snapshot objects carry no blob/secrets by construction.
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "msnap-write");
		f.write(QByteArrayLiteral(
			"{\"managed_youtube\":{\"connected\":true,"
			"\"user_id\":\"UC9z\",\"display\":\"Canal 9z\"}}"));
		f.close();
		CHECK(!store.load(d) && d.managedYoutube.connected &&
			      d.managedYoutube.userId ==
				      QStringLiteral("UC9z") &&
			      d.managedYoutube.display ==
				      QStringLiteral("Canal 9z") &&
			      !d.managedKick.connected,
		      "msnap-restore");
		CHECK(f.open(QIODevice::ReadOnly), "msnap-reread");
		const QByteArray raw = f.readAll();
		f.close();
		CHECK(!raw.isEmpty() && !raw.contains("\"blob\"") && !raw.contains("access_token") &&
			      !raw.contains("refresh_token") &&
			      !raw.contains("client_secret"),
		      "msnap-no-secret-shapes");
		// FB-3: snapshot managed_facebook (solo id+display, sin
		// secretos por construcción); legacy sin la clave sigue
		// desconectado.
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "msnap-fb-write");
		f.write(QByteArrayLiteral(
			"{\"managed_facebook\":{\"connected\":true,"
			"\"user_id\":\"12345\",\"display\":\"Fb Name\"}}"));
		f.close();
		CHECK(!store.load(d) && d.managedFacebook.connected &&
			      d.managedFacebook.userId ==
				      QStringLiteral("12345") &&
			      d.managedFacebook.display ==
				      QStringLiteral("Fb Name") &&
			      d.anyManaged() && !d.managedKick.connected,
		      "msnap-fb-restore");
	}
#ifdef Q_OS_WIN
	// T-051: round-trip with Independent accounts + backendInstall:
	// nothing is lost, nothing crosses modes.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "msnap-rt-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		d.twitch.connected = true;
		d.twitch.display = QStringLiteral("fake-login");
		d.twitch.access = QStringLiteral("fk-tok-4cc3ss-9z");
		d.twitch.clientId = QStringLiteral("fk-client-id-9z");
		d.backendInstall.connected = true;
		d.backendInstall.clientId = QStringLiteral("fk-install-9z");
		d.backendInstall.secret = QStringLiteral("fk-isecret-9z");
		d.connectionMode = QStringLiteral("managed");
		d.managedYoutube.connected = true;
		d.managedYoutube.userId = QStringLiteral("UC9z");
		d.managedYoutube.display = QStringLiteral("Canal 9z");
		CHECK(store.save(d), "msnap-rt-save");
		secure::Data back;
		CHECK(store.load(back) && back.twitch.connected &&
			      back.twitch.access ==
				      QStringLiteral("fk-tok-4cc3ss-9z") &&
			      back.backendInstall.connected &&
			      back.backendInstall.secret ==
				      QStringLiteral("fk-isecret-9z") &&
			      back.connectionMode ==
				      QStringLiteral("managed") &&
			      back.managedYoutube.connected &&
			      back.managedYoutube.userId ==
				      QStringLiteral("UC9z") &&
			      back.managedYoutube.display ==
				      QStringLiteral("Canal 9z") &&
			      !back.managedKick.connected &&
			      !back.youtube.connected && !back.kick.connected,
		      "msnap-rt-roundtrip");
		// Idempotence: load -> save -> load is a fixed point.
		secure::Data again;
		CHECK(store.load(again), "msnap-rt-reload");
		CHECK(store.save(again), "msnap-rt-resave");
		secure::Data third;
		CHECK(store.load(third) && third.twitch.connected &&
			      third.backendInstall.connected &&
			      third.managedYoutube.connected &&
			      third.connectionMode ==
				      QStringLiteral("managed"),
		      "msnap-rt-idempotent");
		// Raw file: labels visible, secrets only inside DPAPI blobs.
		QFile raw(path);
		CHECK(raw.open(QIODevice::ReadOnly), "msnap-rt-raw");
		const QByteArray bytes = raw.readAll();
		raw.close();
		CHECK(bytes.contains("Canal 9z") &&
			      bytes.contains("\"managed_youtube\"") &&
			      !bytes.contains("fk-tok-4cc3ss-9z") &&
			      !bytes.contains("fk-isecret-9z"),
		      "msnap-rt-plaintext-shape");
	}
	// T-053: installation↔backend binding round-trip. The URL is a
	// plaintext endpoint label (never a secret); the install secret
	// stays DPAPI-bound and is only usable with the matching backend.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "envbind-rt-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		// load() reports provider-connection presence, so keep one
		// connected record (as in the T-051 battery) alongside the install.
		d.twitch.connected = true;
		d.twitch.display = QStringLiteral("fake-login");
		d.twitch.access = QStringLiteral("fk-tok-4cc3ss-9z");
		d.twitch.clientId = QStringLiteral("fk-client-id-9z");
		d.backendInstall.connected = true;
		d.backendInstall.clientId = QStringLiteral("fk-install-9z");
		d.backendInstall.secret = QStringLiteral("fk-isecret-9z");
		d.backendBaseUrl =
			QStringLiteral("http://127.0.0.1:8080");
		CHECK(store.save(d), "envbind-rt-save");
		secure::Data back;
		CHECK(store.load(back) && back.backendInstall.connected &&
			      back.backendInstall.secret ==
				      QStringLiteral("fk-isecret-9z") &&
			      back.backendBaseUrl ==
				      QStringLiteral("http://127.0.0.1:8080"),
		      "envbind-rt-roundtrip");
		QFile raw(path);
		CHECK(raw.open(QIODevice::ReadOnly), "envbind-rt-raw");
		const QByteArray bytes = raw.readAll();
		raw.close();
		CHECK(bytes.contains("\"backend_base_url\"") &&
			      bytes.contains("http://127.0.0.1:8080") &&
			      !bytes.contains("fk-isecret-9z"),
		      "envbind-rt-plaintext-shape");
	}
#else
	CHECK(true, "msnap-rt-skipped-non-windows");
#endif
	// T-053: binding predicate (platform-independent, same predicate the
	// client uses in loadInstallation: fail-closed, no fallback).
	{
		CHECK(secure::installationUrlMatches(
			      QStringLiteral("http://127.0.0.1:8080"),
			      QStringLiteral("http://127.0.0.1:8080")),
		      "envbind-match-same");
		CHECK(!secure::installationUrlMatches(
			      QStringLiteral("http://127.0.0.1:8080"),
			      QStringLiteral("https://backend.example.com")),
		      "envbind-mismatch-cross");
		CHECK(!secure::installationUrlMatches(QStringLiteral(""),
						      QStringLiteral(
							      "http://127.0.0.1:8080")),
		      "envbind-legacy-empty");
		CHECK(!secure::installationUrlMatches(
			      QStringLiteral("http://127.0.0.1:8080"),
			      QStringLiteral("")),
		      "envbind-empty-current");
	}
	// T-047: distributed Twitch Client ID + DCF poll decision.
	// (selfcheck builds metadata.cpp with OBS_TWITCH_CLIENT_ID fake.)
	{
		using meta::TwPollAction;
		CHECK(meta::twitchClientId(QStringLiteral("")) ==
			      QStringLiteral("fk-dist-tw-id-9z"),
		      "twid-distributed-default");
		CHECK(meta::twitchClientId(QStringLiteral("  byo-id-7 ")) ==
			      QStringLiteral("byo-id-7"),
		      "twid-field-override-trimmed");
		CHECK(meta::classifyTwPoll(200, false, QStringLiteral(""),
					   5) == TwPollAction::Consume,
		      "twpoll-success");
		CHECK(meta::classifyTwPoll(
			      400, false,
			      QStringLiteral("authorization_pending"), 3) ==
			      TwPollAction::KeepPolling,
		      "twpoll-pending");
		CHECK(meta::classifyTwPoll(
			      400, false,
			      QStringLiteral("authorization_pending"), 0) ==
			      TwPollAction::FailExpired,
		      "twpoll-pending-exhausted");
		CHECK(meta::classifyTwPoll(400, false,
					   QStringLiteral("slow_down"),
					   2) == TwPollAction::SlowDown,
		      "twpoll-slowdown");
		CHECK(meta::classifyTwPoll(400, false,
					   QStringLiteral("slow_down"),
					   0) == TwPollAction::FailExpired,
		      "twpoll-slowdown-exhausted");
		CHECK(meta::classifyTwPoll(400, false,
					   QStringLiteral("access_denied"),
					   2) == TwPollAction::FailDenied,
		      "twpoll-denied");
		CHECK(meta::classifyTwPoll(400, false,
					   QStringLiteral("expired_token"),
					   2) == TwPollAction::FailExpired,
		      "twpoll-expired");
		CHECK(meta::classifyTwPoll(0, true, QStringLiteral(""), 2) ==
			      TwPollAction::FailExpired,
		      "twpoll-netfail");
	}

	// T-071 FB-2 Independent: Facebook metadata core (D1/D2/D3/D8/D10).
	// Title 1-254, description SI existe (a diferencia de Twitch/Kick),
	// payload POST /{live-video-id} solo title/description, jamas
	// channel_description/stream_title/snippet. Clasificacion §28+§5
	// (190/1363120/1363144/10/613) y scopes sin groups/email.
	{
		CHECK(meta::kFacebookTitleMax == 254, "fb-title-max");
		CHECK(QString::fromLatin1(meta::platformName(
						  meta::Platform::Facebook)) ==
			      QStringLiteral("Facebook"),
		      "fb-name");
		CHECK(meta::supportsDescription(meta::Platform::Facebook),
		      "fb-desc-supported");
		CHECK(!meta::supportsDescription(meta::Platform::Twitch),
		      "fb-tw-still-no-desc");
		CHECK(!meta::supportsDescription(meta::Platform::Kick),
		      "fb-kk-still-no-desc");
		meta::Selection fb{};
		fb.facebook = true;
		CHECK(!meta::validate(meta::Metadata{QString(), QString()}, fb)
			       .isEmpty(),
		      "fb-empty-title");
		CHECK(meta::validate(
			      meta::Metadata{QString(254, QLatin1Char('x')),
					     QString()},
			      fb)
			      .isEmpty(),
		      "fb-254-ok");
		CHECK(!meta::validate(
			       meta::Metadata{QString(255, QLatin1Char('x')),
					      QString()},
			       fb)
			       .isEmpty(),
		      "fb-255-rejected");
		CHECK(meta::validate(
			      meta::Metadata{QStringLiteral("T"),
					     QStringLiteral("D")},
			      fb)
			      .isEmpty(),
		      "fb-title-desc-ok");
		const QString fbp = meta::facebookPayload(
			QStringLiteral("T"), QStringLiteral("D"));
		const QJsonObject fbo =
			QJsonDocument::fromJson(fbp.toUtf8()).object();
		CHECK(fbo.value(QStringLiteral("title")).toString() ==
			      QStringLiteral("T"),
		      "fb-payload-title");
		CHECK(fbo.value(QStringLiteral("description")).toString() ==
			      QStringLiteral("D"),
		      "fb-payload-desc");
		CHECK(!fbo.contains(QStringLiteral("channel_description")) &&
			      !fbo.contains(QStringLiteral("stream_title")) &&
			      !fbo.contains(QStringLiteral("snippet")),
		      "fb-payload-no-foreign");
		CHECK(meta::classifyFb(200, 0) == meta::Outcome::Success,
		      "fb-200-ok");
		CHECK(meta::classifyFb(400, 0) == meta::Outcome::BadRequest,
		      "fb-400-invalid");
		CHECK(meta::classifyFb(401, 0) ==
			      meta::Outcome::AuthRequired,
		      "fb-401-auth");
		CHECK(meta::classifyFb(200, 190) ==
			      meta::Outcome::AuthRequired,
		      "fb-190-expired");
		CHECK(meta::classifyFb(200, 1363120) ==
			      meta::Outcome::Forbidden,
		      "fb-eligibility-60d");
		CHECK(meta::classifyFb(200, 1363144) ==
			      meta::Outcome::Forbidden,
		      "fb-eligibility-100");
		CHECK(meta::classifyFb(200, 10) == meta::Outcome::Forbidden,
		      "fb-10-auth");
		CHECK(meta::classifyFb(429, 0) ==
			      meta::Outcome::RateLimited,
		      "fb-429-limited");
		CHECK(meta::classifyFb(200, 613) ==
			      meta::Outcome::RateLimited,
		      "fb-613-limited");
		CHECK(meta::classifyFb(500, 0) ==
			      meta::Outcome::ServerRetry,
		      "fb-5xx-retry");
		const QStringList prof = meta::fbRequiredScopes(false);
		CHECK(prof.contains(QStringLiteral("publish_video")) &&
			      !prof.join(QStringLiteral(","))
				       .contains(QStringLiteral("groups")) &&
			      !prof.join(QStringLiteral(","))
				       .contains(QStringLiteral("email")),
		      "fb-scopes-profile");
		const QStringList page = meta::fbRequiredScopes(true);
		CHECK(page.contains(QStringLiteral("pages_manage_posts")) &&
			      page.contains(
				      QStringLiteral("pages_read_engagement")) &&
			      page.contains(
				      QStringLiteral("pages_show_list")),
		      "fb-scopes-page");
		const QString fauth = meta::facebookAuthUrl(
			QStringLiteral("123"), QStringLiteral("http://localhost:9/cb"),
			QStringLiteral("st8"), QStringLiteral("publish_video"),
			QStringLiteral("ch4llenge"));
		CHECK(fauth.contains(
			      QStringLiteral("facebook.com/v26.0/dialog/oauth")) &&
			      fauth.contains(QStringLiteral("client_id=123")) &&
			      !fauth.contains(QStringLiteral("client_secret")),
		      "fb-auth-url-no-secret");
		CHECK(meta::userMessage(meta::Outcome::Forbidden,
					meta::Platform::Facebook)
			      .contains(QStringLiteral("permissions")) ||
			      meta::userMessage(meta::Outcome::Forbidden,
						meta::Platform::Facebook)
				      .contains(QStringLiteral("60")) ||
			      meta::userMessage(meta::Outcome::Forbidden,
						meta::Platform::Facebook)
				      .contains(QStringLiteral("100")),
		      "fb-msg-403");
	}

#ifdef Q_OS_WIN
	// T-071 FB-2: custodia DPAPI Facebook (BYO-app, igual que YT/Kick).
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "fb-store-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		d.facebook.connected = true;
		d.facebook.display = QStringLiteral("fb-user-9z");
		d.facebook.access = QStringLiteral("fk-fb-4cc3ss-9z");
		d.facebook.clientId = QStringLiteral("fk-fb-app-9z");
		CHECK(store.save(d), "fb-store-save");
		QFile raw(path);
		CHECK(raw.open(QIODevice::ReadOnly), "fb-store-raw");
		const QByteArray bytes = raw.readAll();
		raw.close();
		CHECK(!bytes.contains("fk-fb-4cc3ss-9z"),
		      "fb-store-no-plaintext-access");
		CHECK(!bytes.contains("fk-fb-app-9z"),
		      "fb-store-no-plaintext-client");
		CHECK(bytes.contains("fb-user-9z"),
		      "fb-store-label-plaintext");
		secure::Data back;
		CHECK(store.load(back) && back.facebook.connected &&
			      back.facebook.access ==
				      QStringLiteral("fk-fb-4cc3ss-9z") &&
			      back.facebook.clientId ==
				      QStringLiteral("fk-fb-app-9z") &&
			      back.facebook.display ==
				      QStringLiteral("fb-user-9z") &&
			      !back.youtube.connected &&
			      !back.kick.connected &&
			      !back.twitch.connected,
		      "fb-store-roundtrip");
		CHECK(store.clear() && !QFile::exists(path),
		      "fb-store-clear");
	}
#endif

	std::printf("SELFCHECK OK\n");
	return 0;
}

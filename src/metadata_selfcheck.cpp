// T-031 product selfcheck: validation + payloads + outcome mapping.
// T-032: + backoff policy + revoke endpoints + DPAPI store round-trip.
// T-033 (P6): + offline negatives AGENTS.md §40 (no network).
// No network. Test-only fake values, never real credentials.
// Usage: metadata-selfcheck.exe -> exit 0.
#include "metadata.h"
#include "relay.h"
#include "secure_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
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
		// T-073 FB-4: LiveVideo ID del dock (plaintext, sobrevive sin
		// conexion; solo DELETE lo limpia).
		CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
		      "fblive-write");
		f.write(QByteArrayLiteral(
			"{\"facebook_live_id\":\"28237427212586319\"}"));
		f.close();
		CHECK(!store.load(d) && d.facebookLiveId ==
			      QStringLiteral("28237427212586319"),
		      "fblive-restore");
		CHECK(f.open(QIODevice::ReadOnly), "fblive-reread");
		const QByteArray fbliveRaw = f.readAll();
		f.close();
		CHECK(!fbliveRaw.isEmpty() &&
			      !fbliveRaw.contains("\"blob\"") &&
			      !fbliveRaw.contains("access_token"),
		      "fblive-no-secret-shapes");
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
		// T-073 FB-4: F-080 objeto web/ID desconocido (100/33) no es
		// "titulo invalido": es NotFound gestionable creando desde el
		// dock. TDD rojo-primero.
		CHECK(meta::classifyFb(400, 33) ==
			      meta::Outcome::NotFound,
		      "fb-33-notfound");
		CHECK(meta::classifyFb(400, 100) ==
			      meta::Outcome::BadRequest,
		      "fb-100-badrequest");
		CHECK(meta::userMessage(meta::Outcome::NotFound,
					meta::Platform::Facebook)
			      .contains(QStringLiteral("dock")) ||
			      meta::userMessage(meta::Outcome::NotFound,
						meta::Platform::Facebook)
				      .contains(QStringLiteral("manageable")),
		      "fb-msg-404-dock");
		// T-073 FB-4: warnings especificos elegibilidad (D9 §B.5).
		CHECK(meta::fbEligibilityMessage(1363120).contains(
			      QStringLiteral("60")),
		      "fb-warn-60d");
		CHECK(meta::fbEligibilityMessage(1363144).contains(
			      QStringLiteral("100")),
		      "fb-warn-100");
		CHECK(!meta::fbEligibilityMessage(0).isEmpty() ||
			      meta::fbEligibilityMessage(0).isEmpty(),
		      "fb-warn-shape");
		CHECK(meta::fbEligibilityMessage(10).contains(
			      QStringLiteral("permission")) ||
			      meta::fbEligibilityMessage(10).contains(
				      QStringLiteral("approval")),
		      "fb-warn-10");
		// T-073 FB-4: indicador por status del LiveVideo (D7).
		CHECK(meta::fbLiveState(QStringLiteral("LIVE")) ==
			      meta::FbLiveState::Live,
		      "fb-state-live");
		CHECK(meta::fbLiveState(QStringLiteral("LIVE_NOW")) ==
			      meta::FbLiveState::Live,
		      "fb-state-livenow");
		CHECK(meta::fbLiveState(QStringLiteral("UNPUBLISHED")) ==
			      meta::FbLiveState::Preview,
		      "fb-state-preview");
		CHECK(meta::fbLiveState(QStringLiteral("VOD")) ==
			      meta::FbLiveState::Ended,
		      "fb-state-vod");
		CHECK(meta::fbLiveState(QStringLiteral("LIVE_STOPPED")) ==
			      meta::FbLiveState::Ended,
		      "fb-state-stopped");
		CHECK(!meta::fbLiveStateLabel(
			      meta::FbLiveState::Live)
			       .isEmpty(),
		      "fb-state-label");
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

	// Verificacion previa a GoLive (pura, sin red/UI). La audiencia
	// solo se verifica con privacy EVERYONE devuelta por la API.
	{
		using meta::FbVerify;
		QJsonObject pd;
		pd[QStringLiteral("value")] =
			QStringLiteral("  EVERYONE  ");
		CHECK(meta::facebookPrivacyValue(QJsonValue(pd)) ==
			      QStringLiteral("EVERYONE"),
		      "fbpriv-dict-trim");
		CHECK(meta::facebookPrivacyValue(QJsonValue(
				QStringLiteral(" everyone "))) ==
			      QStringLiteral("everyone"),
		      "fbpriv-str-trim");
		CHECK(meta::facebookPrivacyValue(QJsonValue(42)).isEmpty() &&
			      meta::facebookPrivacyValue(QJsonValue())
				      .isEmpty(),
		      "fbpriv-other-empty");
		CHECK(meta::facebookVerifyPrepared(
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("UNPUBLISHED"),
				QStringLiteral("EVERYONE"))
			      .outcome == FbVerify::Verified,
		      "fbverify-ok");
		CHECK(meta::facebookVerifyPrepared(
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("UNPUBLISHED"),
				meta::facebookPrivacyValue(QJsonValue(pd)))
			      .outcome == FbVerify::Verified,
		      "fbverify-dict-everyone");
		CHECK(meta::facebookVerifyPrepared(
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("UNPUBLISHED"),
				QStringLiteral("everyone"))
			      .outcome == FbVerify::Verified,
		      "fbverify-case-insensitive");
		{
			const meta::FbVerifyResult r =
				meta::facebookVerifyPrepared(
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("UNPUBLISHED"),
					QString());
			CHECK(r.outcome ==
				      FbVerify::AudienceUnverified &&
			      r.reason ==
				      QStringLiteral("privacy-unverified"),
			      "fbverify-no-privacy");
		}
		{
			const meta::FbVerifyResult r =
				meta::facebookVerifyPrepared(
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("UNPUBLISHED"),
					QStringLiteral("SELF"));
			CHECK(r.outcome ==
				      FbVerify::AudienceUnverified &&
			      r.reason ==
				      QStringLiteral("privacy-not-public"),
			      "fbverify-privacy-other");
		}
		{
			const meta::FbVerifyResult r =
				meta::facebookVerifyPrepared(
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("T"),
					QStringLiteral("D"), QString(),
					QStringLiteral("EVERYONE"));
			CHECK(r.outcome == FbVerify::Blocked &&
			      r.reason ==
				      QStringLiteral("status-missing"),
			      "fbverify-status-missing");
		}
		CHECK(meta::facebookVerifyPrepared(
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("LIVE"),
				QStringLiteral("EVERYONE"))
			      .outcome == FbVerify::Blocked,
		      "fbverify-status-live");
		CHECK(meta::facebookVerifyPrepared(
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("unpublished"),
				QStringLiteral("EVERYONE"))
			      .outcome == FbVerify::Blocked,
		      "fbverify-status-case");
		{
			const meta::FbVerifyResult r =
				meta::facebookVerifyPrepared(
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("X"),
					QStringLiteral("D"),
					QStringLiteral("UNPUBLISHED"),
					QStringLiteral("EVERYONE"));
			CHECK(r.outcome == FbVerify::Blocked &&
			      r.reason == QStringLiteral("mismatch"),
			      "fbverify-title-mismatch");
		}
		{
			const meta::FbVerifyResult r =
				meta::facebookVerifyPrepared(
					QStringLiteral("T"),
					QStringLiteral("D"),
					QStringLiteral("T"),
					QStringLiteral("X"),
					QStringLiteral("UNPUBLISHED"),
					QStringLiteral("EVERYONE"));
			CHECK(r.outcome == FbVerify::Blocked &&
			      r.reason == QStringLiteral("mismatch"),
			      "fbverify-desc-mismatch");
		}
	}

	// Adopt explicito (puro, sin red): LIVE/LIVE_NOW cuentan (trim+upper);
	// exactamente uno -> ok; cero -> no-active; varios -> ambiguous.
	{
		using meta::FbAdoptResult;
		using meta::FbLiveItem;
		CHECK(meta::facebookAdoptActive(
			      {FbLiveItem{QStringLiteral("A1"),
					  QStringLiteral("LIVE")},
			       FbLiveItem{QStringLiteral("A2"),
					  QStringLiteral("VOD")}})
			      .ok,
		      "adopt-single-live");
		{
			const FbAdoptResult r =
				meta::facebookAdoptActive(
					{FbLiveItem{QStringLiteral("B1"),
						    QStringLiteral(
							    " live_now ")}});
			CHECK(r.ok && r.id == QStringLiteral("B1") &&
				      r.reason.isEmpty(),
			      "adopt-live-now-spaces");
		}
		{
			const FbAdoptResult r =
				meta::facebookAdoptActive(
					{FbLiveItem{QStringLiteral("C1"),
						    QStringLiteral("VOD")},
					 FbLiveItem{QStringLiteral("C2"),
						    QStringLiteral(
							    "UNPUBLISHED")}});
			CHECK(!r.ok && r.id.isEmpty() &&
				      r.reason ==
					      QStringLiteral("no-active"),
			      "adopt-zero-no-active");
		}
		{
			const FbAdoptResult r =
				meta::facebookAdoptActive(
					{FbLiveItem{QStringLiteral("D1"),
						    QStringLiteral("LIVE")},
					 FbLiveItem{QStringLiteral("D2"),
						    QStringLiteral(
							    "live_now")}});
			CHECK(!r.ok && r.id.isEmpty() &&
				      r.reason ==
					      QStringLiteral("ambiguous"),
			      "adopt-multi-ambiguous");
		}
		{
			// Ids vacios ignorados (no cuentan ni como LIVE).
			const FbAdoptResult r =
				meta::facebookAdoptActive(
					{FbLiveItem{QString(), QStringLiteral(
								      "LIVE")},
					 FbLiveItem{QStringLiteral("  "),
						    QStringLiteral(
							    "LIVE_NOW")},
					 FbLiveItem{QStringLiteral("E1"),
						    QStringLiteral("live")}});
			CHECK(r.ok && r.id == QStringLiteral("E1"),
			      "adopt-empty-ignored");
		}
		CHECK(meta::facebookAdoptActive(
			      {FbLiveItem{QStringLiteral("F1"),
					  QStringLiteral("Live")}})
			      .ok,
		      "adopt-case-insensitive");
	}

	// Fase 4: gate login previo a Managed (spec §5). Sin user-session
	// en memoria: TODAS las acciones Managed deshabilitadas con UN
	// mensaje exacto reutilizado en las 4 plataformas; sin red ni
	// fallback silencioso a Independent. Sesión solo-memoria: jamás
	// en accounts.json/DPAPI. Independent jamás exige user header.
	{
		const QString gate =
			meta::managedLoginRequiredMessage();
		CHECK(!gate.isEmpty(), "f4-gate-nonempty");
		CHECK(gate == meta::managedLoginRequiredMessage(),
		      "f4-gate-stable");
		CHECK(!gate.contains(QStringLiteral("Bearer")) &&
			      !gate.contains(QStringLiteral("token")) &&
			      !gate.contains(QStringLiteral("secret")) &&
			      !gate.contains(QStringLiteral("password")) &&
			      !gate.contains(QStringLiteral("http")),
		      "f4-gate-safe");
		CHECK(!meta::managedActionAllowed(false),
		      "f4-gate-blocked");
		CHECK(meta::managedActionAllowed(true), "f4-gate-allowed");
		CHECK(meta::userSessionHeaderName() ==
			      QStringLiteral("X-User-Session"),
		      "f4-user-header");
		CHECK(!meta::managedHeadersIncludeUserSession(false),
		      "f4-independent-no-user-header");
		CHECK(meta::managedHeadersIncludeUserSession(true),
		      "f4-managed-user-header");
		const QStringList forbidden =
			meta::forbiddenUserSessionShapes();
		CHECK(forbidden.contains(QStringLiteral("user_session")) &&
			      forbidden.contains(
				      QStringLiteral("userSession")) &&
			      forbidden.contains(
				      QStringLiteral("X-User-Session")),
		      "f4-forbidden-shapes");
		CHECK(!meta::userNoRecoveryNotice().isEmpty() &&
			      meta::userNoRecoveryNotice().contains(
				      QStringLiteral("no account recovery")),
		      "f4-no-recovery");
	}
#ifdef Q_OS_WIN
	// Fase 4: memoria-only probado en disco. Con TODO conectado
	// (Independent + backendInstall + snapshots Managed), el fichero
	// crudo jamás contiene formas de user-session.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "f4-store-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		d.twitch.connected = true;
		d.twitch.display = QStringLiteral("fake-login");
		d.twitch.access = QStringLiteral("fk-tok-4cc3ss-9z");
		d.twitch.clientId = QStringLiteral("fk-client-id-9z");
		d.youtube.connected = true;
		d.youtube.display = QStringLiteral("fake-chan");
		d.youtube.access = QStringLiteral("fk-yt-4cc3ss-9z");
		d.youtube.clientId = QStringLiteral("fk-yt-id-9z");
		d.youtube.secret = QStringLiteral("fk-yt-secret-9z");
		d.backendInstall.connected = true;
		d.backendInstall.clientId = QStringLiteral("fk-install-9z");
		d.backendInstall.secret = QStringLiteral("fk-isecret-9z");
		d.connectionMode = QStringLiteral("managed");
		d.managedYoutube.connected = true;
		d.managedYoutube.userId = QStringLiteral("UC9z");
		d.managedYoutube.display = QStringLiteral("Canal 9z");
		d.managedTwitch.connected = true;
		d.managedTwitch.userId = QStringLiteral("123");
		d.managedTwitch.display = QStringLiteral("Tw 9z");
		d.managedKick.connected = true;
		d.managedKick.userId = QStringLiteral("456");
		d.managedKick.display = QStringLiteral("Kk 9z");
		d.managedFacebook.connected = true;
		d.managedFacebook.userId = QStringLiteral("789");
		d.managedFacebook.display = QStringLiteral("Fb 9z");
		CHECK(store.save(d), "f4-store-save");
		QFile raw(path);
		CHECK(raw.open(QIODevice::ReadOnly), "f4-store-raw");
		const QByteArray bytes = raw.readAll();
		raw.close();
		for (const QString &shape :
		     meta::forbiddenUserSessionShapes()) {
			if (bytes.contains(shape.toUtf8())) {
				std::printf("FAIL f4-store-no-user-session\n");
				return 1;
			}
		}
		std::printf("PASS f4-store-no-user-session\n");
		CHECK(bytes.contains("Canal 9z"), "f4-store-labels-kept");
	}
#endif

	// Fase 5: borrado de CUENTA (/users/erase) separado de F-C2.
	// Exige login (gate §5), usa auth doble (instalación + usuario),
	// la respuesta muestra solo conteos (jamás secretos) y F-C2 queda
	// intacto (ruta propia, operación distinta).
	{
		CHECK(QString::fromLatin1(meta::accountErasePath()) ==
			      QStringLiteral("/users/erase"),
		      "f5-erase-path");
		CHECK(QString::fromLatin1(meta::managedDataErasePath()) ==
			      QStringLiteral("/privacy/erase"),
		      "f5-fc2-path");
		CHECK(QString::fromLatin1(meta::accountErasePath()) !=
			      QString::fromLatin1(
				      meta::managedDataErasePath()),
		      "f5-paths-distinct");
		CHECK(!meta::managedActionAllowed(false),
		      "f5-erase-requires-login");
		CHECK(meta::managedActionAllowed(true),
		      "f5-erase-allowed-logged");
		CHECK(meta::managedHeadersIncludeUserSession(true),
		      "f5-erase-dual-managed");
		CHECK(!meta::managedHeadersIncludeUserSession(false),
		      "f5-erase-no-user-header-independent");
		CHECK(meta::userSessionHeaderName() ==
			      QStringLiteral("X-User-Session"),
		      "f5-erase-user-header");
		const QString em = meta::accountEraseCountsMessage(
			1, 2, 3, 4, 5);
		CHECK(!em.isEmpty(), "f5-erase-msg-nonempty");
		CHECK(em.contains(QStringLiteral("users=1")) &&
			      em.contains(QStringLiteral("installations=2")) &&
			      em.contains(QStringLiteral("sessions=3")) &&
			      em.contains(QStringLiteral("connections=4")) &&
			      em.contains(QStringLiteral("tokens=5")),
		      "f5-erase-msg-counts");
		const QStringList shapes{
			QStringLiteral("token="), QStringLiteral("secret"),
			QStringLiteral("password"), QStringLiteral("Bearer"),
			QStringLiteral("http"),
			QStringLiteral("user_session"),
			QStringLiteral("userSession"),
			QStringLiteral("X-User-Session")};
		for (const QString &shape : shapes) {
			if (em.contains(shape)) {
				std::printf("FAIL f5-erase-msg-safe\n");
				return 1;
			}
		}
		std::printf("PASS f5-erase-msg-safe\n");
	}

	// f6 ENTRAR-primero: Managed dos pantallas excluyentes (sin
	// sesión se ve ENTRAR y no el panel; con sesión se ve PANEL y no
	// ENTRAR; salir vuelve a ENTRAR); Independent intacto (siempre
	// panel, jamás ENTRAR). Puro QtCore; la vista lo usa como gate
	// del QStackedWidget.
	CHECK(meta::managedEnterVisible(true, false),
	      "f6-enter-nologin");
	CHECK(!meta::managedPanelVisible(true, false),
	      "f6-panel-hidden-nologin");
	CHECK(!meta::managedEnterVisible(true, true),
	      "f6-enter-hidden-login");
	CHECK(meta::managedPanelVisible(true, true), "f6-panel-login");
	CHECK(!meta::managedEnterVisible(false, false) &&
		      !meta::managedEnterVisible(false, true),
	      "f6-independent-no-enter");
	CHECK(meta::managedPanelVisible(false, false) &&
		      meta::managedPanelVisible(false, true),
	      "f6-independent-panel-always");

	// f7 Managed login/signup: el error especifico sobrevive al
	// refresh; el 409 sugiere pulsar Log in; el gate solo aparece
	// sin otro mensaje vigente. TDD rojo-primero (bug 2026-10-26:
	// refreshManagedLoginUi pisaba 201/409/401/red con el gate).
	CHECK(meta::managedLoggedOutStatus(QString()) ==
		      meta::managedLoginRequiredMessage(),
	      "f7-gate-when-empty");
	CHECK(meta::managedLoggedOutStatus(QStringLiteral("Wrong username or password.")) ==
		      QStringLiteral("Wrong username or password."),
	      "f7-error-survives");
	CHECK(meta::managedLoggedOutStatus(
		      meta::managedSignupTakenMessage()) ==
		      meta::managedSignupTakenMessage(),
	      "f7-taken-survives");
	CHECK(meta::managedSignupTakenMessage().contains(
		      QStringLiteral("Log in")),
	      "f7-409-suggests-login");

	// f8 REDISEÑO ENTRAR (2026-10-26 [D]): dos modos excluyentes
	// (QTabWidget en la vista), validación local inmediata (sin red)
	// con las reglas exactas del backend (backend/users.py:
	// username 3–32 [A-Za-z0-9._-], password 12–128), política
	// visible y 409 de signup sugiriendo Entrar como respaldo.
	CHECK(meta::managedAuthLoginVisible(
		      meta::ManagedAuthMode::Login) &&
		      !meta::managedAuthSignupVisible(
			      meta::ManagedAuthMode::Login),
	      "f8-login-tab-only");
	CHECK(!meta::managedAuthLoginVisible(
		       meta::ManagedAuthMode::Signup) &&
		      meta::managedAuthSignupVisible(
			      meta::ManagedAuthMode::Signup),
	      "f8-signup-tab-only");
	CHECK(meta::validateManagedUsername(QStringLiteral("ab"))
			      .contains(QStringLiteral("3-32")),
	      "f8-user-short");
	CHECK(meta::validateManagedUsername(
		      QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))
			      .contains(QStringLiteral("3-32")),
	      "f8-user-long");
	CHECK(meta::validateManagedUsername(QStringLiteral("a!b"))
			      .contains(QStringLiteral("letters")),
	      "f8-user-charset");
	CHECK(meta::validateManagedUsername(QStringLiteral("abc"))
			      .isEmpty() &&
		      meta::validateManagedUsername(
			      QStringLiteral("a.b_c-d9")).isEmpty(),
	      "f8-user-ok");
	CHECK(meta::validateManagedPassword(
		      QString(11, QLatin1Char('x')))
			      .contains(QStringLiteral("12")),
	      "f8-pass-short");
	CHECK(meta::validateManagedPassword(
		      QString(129, QLatin1Char('x')))
			      .contains(QStringLiteral("128")),
	      "f8-pass-long");
	CHECK(meta::validateManagedPassword(
		      QString(12, QLatin1Char('x')))
			      .isEmpty(),
	      "f8-pass-ok");
	CHECK(!meta::validateManagedCredentials(
		      QStringLiteral("ab"),
		      QString(11, QLatin1Char('x')))
			      .isEmpty(),
	      "f8-local-blocks");
	CHECK(meta::validateManagedCredentials(
		      QStringLiteral("valid.user_9"),
		      QString(12, QLatin1Char('x')))
			      .isEmpty(),
	      "f8-local-allows");
	CHECK(meta::managedPasswordPolicyNotice().contains(
		      QStringLiteral("12")) &&
		      meta::managedPasswordPolicyNotice().contains(
			      QStringLiteral("no account recovery")),
	      "f8-policy-visible");
	CHECK(meta::managedSignupTakenMessage().contains(
		      QStringLiteral("Log in")),
	      "f8-409-suggests-login");

	// Bugfix visual 2026-10-26 [D]: tres estados visuales excluyentes
	// por plataforma. Rojo SOLO tras accion fallida del usuario o
	// ruptura de una conectada; limpio/nunca-tocado o
	// desconectado-por-logout = gris neutro. Predicado puro
	// estado->estilo (la vista lo usa en updateCardStyle).
	CHECK(meta::cardToneFor(
		      false, QStringLiteral("Not connected"),
		      QStringLiteral("Twitch \u2014")) ==
		      meta::CardTone::Neutral,
	      "card-clean-neutral");
	CHECK(meta::cardToneFor(
		      false, QStringLiteral("Not connected"),
		      QStringLiteral("Twitch \u2713 disconnected")) ==
		      meta::CardTone::Neutral,
	      "card-logout-neutral");
	CHECK(meta::cardToneFor(
		      true, QStringLiteral("Connected as canal"),
		      QStringLiteral("YouTube \u2713 connected")) ==
		      meta::CardTone::Brand,
	      "card-connected-brand");
	CHECK(meta::cardToneFor(
		      false, QStringLiteral("Error: boom"),
		      QStringLiteral("Twitch \u2014")) ==
		      meta::CardTone::Error,
	      "card-action-error");
	CHECK(meta::cardToneFor(
		      false, QStringLiteral("Not connected"),
		      QStringLiteral("Twitch \u2717 boom")) ==
		      meta::CardTone::Error,
	      "card-result-error");
	// El rojo se limpia al cambiar de estado: mismos inputs limpios
	// vuelven a gris (documenta que la vista debe reescribir
	// status+result al limpiar, no pegarse).
	CHECK(meta::cardToneFor(
		      false, QStringLiteral("Not connected"),
		      QStringLiteral("Twitch \u2014")) ==
		      meta::CardTone::Neutral,
	      "card-error-cleared");
	// Mecanismo del bug: el gate "login required" y el "Needs
	// reconnection." cuentan como tono Error; por eso la vista JAMAS
	// debe escribirlos en plataformas limpias (logout/login limpio
	// usa "Not connected" neutro).
	CHECK(meta::cardToneFor(false,
				meta::managedLoginRequiredMessage(),
				QStringLiteral("Twitch \u2014")) ==
		      meta::CardTone::Error,
	      "card-gate-is-error");
	CHECK(meta::cardToneFor(false,
				QStringLiteral("Needs reconnection."),
				QStringLiteral("Twitch \u2014")) ==
		      meta::CardTone::Error,
	      "card-reconnect-is-error");

#ifdef Q_OS_WIN
	// [D 2026-10-26 usuario]: managedUser persistido (DPAPI, ligado a
	// backendBaseUrl). SUSTITUYE spec §5 memoria-solo: "si entraste
	// antes, al abrir OBS vuelve directo al panel". Round-trip DPAPI con
	// valores falsos (convencion fk-...): el fichero crudo NUNCA contiene
	// el token, la etiqueta ni formas user_session; la URL de binding es
	// plaintext (no es secreto); expirada se ignora; logout limpia.
	{
		QTemporaryDir tmp;
		CHECK(tmp.isValid(), "muser-tmpdir");
		const QString path =
			tmp.filePath(QStringLiteral("accounts.json"));
		secure::Store store(path);
		secure::Data d;
		d.managedUser.loggedIn = true;
		d.managedUser.username = QStringLiteral("fake-user-9z");
		d.managedUser.token = QStringLiteral("fk-usersess-9z");
		d.managedUser.expiry =
			QDateTime::currentDateTimeUtc().addSecs(1800);
		d.backendBaseUrl =
			QStringLiteral("http://127.0.0.1:8080");
		CHECK(store.save(d), "muser-save");
		QFile raw(path);
		CHECK(raw.open(QIODevice::ReadOnly), "muser-raw");
		const QByteArray bytes = raw.readAll();
		raw.close();
		CHECK(!bytes.contains("fk-usersess-9z"),
		      "muser-no-plaintext-token");
		CHECK(!bytes.contains("fake-user-9z"),
		      "muser-no-plaintext-username");
		CHECK(bytes.contains("\"managed_user\""),
		      "muser-key-present");
		CHECK(bytes.contains("http://127.0.0.1:8080"),
		      "muser-url-plaintext-binding");
		for (const QString &shape :
		     meta::forbiddenUserSessionShapes()) {
			if (bytes.contains(shape.toUtf8())) {
				std::printf("FAIL muser-no-user-session\n");
				return 1;
			}
		}
		std::printf("PASS muser-no-user-session\n");
		secure::Data back;
		// load() reporta presencia de proveedores (aquí ninguno):
		// false con la sesion restaurada, igual que los snapshots.
		CHECK(!store.load(back) && back.managedUser.loggedIn &&
			      back.managedUser.username ==
				      QStringLiteral("fake-user-9z") &&
			      back.managedUser.token ==
				      QStringLiteral("fk-usersess-9z") &&
			      back.managedUser.expiry.toSecsSinceEpoch() ==
				      d.managedUser.expiry
					      .toSecsSinceEpoch() &&
			      back.backendBaseUrl ==
				      QStringLiteral("http://127.0.0.1:8080"),
		      "muser-roundtrip");
		// Mismatch de URL = sin sesion (mismo predicado fail-closed que
		// loadInstallation/tryRestoreUserSession, sin duplicar logica).
		CHECK(secure::installationUrlMatches(
			      back.backendBaseUrl,
			      QStringLiteral("http://127.0.0.1:8080")),
		      "muser-match-same");
		CHECK(!secure::installationUrlMatches(
			      back.backendBaseUrl,
			      QStringLiteral("https://backend.example.com")),
		      "muser-mismatch-cross");
		CHECK(!secure::installationUrlMatches(QStringLiteral(""),
						      QStringLiteral(
							      "http://127.0.0.1:8080")),
		      "muser-legacy-empty");
		// Expirada se ignora (login de nuevo): save la guarda, load no
		// la restaura. (load() devuelve presencia de proveedores, aquí
		// ninguno: se comprueba el registro, no el retorno.)
		secure::Data old = back;
		old.managedUser.expiry =
			QDateTime::currentDateTimeUtc().addSecs(-60);
		CHECK(store.save(old), "muser-exp-save");
		secure::Data gone;
		store.load(gone);
		CHECK(!gone.managedUser.loggedIn &&
			      gone.managedUser.token.isEmpty(),
		      "muser-expired-ignored");
		// Logout limpia: reset del registro + save, sin rastro en crudo.
		secure::Data out = back;
		out.managedUser = secure::ManagedUserSession{};
		CHECK(store.save(out), "muser-logout-save");
		secure::Data clean;
		store.load(clean);
		CHECK(!clean.managedUser.loggedIn &&
			      clean.managedUser.username.isEmpty() &&
			      clean.managedUser.token.isEmpty(),
		      "muser-logout-clean");
		QFile rawAfter(path);
		CHECK(rawAfter.open(QIODevice::ReadOnly),
		      "muser-logout-raw");
		const QByteArray after = rawAfter.readAll();
		rawAfter.close();
		CHECK(!after.contains("fk-usersess-9z") &&
			      !after.contains("fake-user-9z"),
		      "muser-logout-no-trace");
	}
#else
	CHECK(true, "muser-skipped-non-windows");
#endif

	// AUTORIZACION [D usuario 2026-10-26]: arranque Managed directo al
	// PANEL. Sin sesion restaurable -> ENTRAR sin red; con sesion ->
	// revalidar (me) y PANEL directo; 401/expirada -> ENTRAR normal.
	CHECK(!meta::managedStartupShouldRevalidate(false),
	      "startup-no-restore-no-net");
	CHECK(meta::managedStartupShouldRevalidate(true),
	      "startup-restore-revalidate");
	CHECK(meta::managedEnterVisible(true, false) &&
		      !meta::managedPanelVisible(true, false),
	      "startup-enter-without-session");
	CHECK(!meta::managedEnterVisible(true, true) &&
		      meta::managedPanelVisible(true, true),
	      "startup-panel-with-session");

	// T-077 Fase A (port lab->plugin): Status SIN privacy de entrada,
	// persistent opt-in/off, mapeo persistent-key-not-allowed y
	// audiencia-no-verificada por lectura (F-092/F-093). Sin red.
	CHECK(meta::facebookStatusFields() ==
		      QStringLiteral("id,title,description,status"),
	      "fbstatus-fields-no-privacy");
	CHECK(!meta::facebookStatusFields().contains(
		      QStringLiteral("privacy")),
	      "fbstatus-fields-never-privacy");
	{
		// persistent OFF por defecto: el body no lleva el campo.
		const QJsonObject off = meta::facebookCreatePayload(
			QStringLiteral("T"), QStringLiteral("D"), false);
		CHECK(off.value(QStringLiteral("title")).toString() ==
			      QStringLiteral("T") &&
			      off.value(QStringLiteral("description"))
				      .toString() ==
				      QStringLiteral("D") &&
			      off.value(QStringLiteral("status"))
				      .toString() ==
				      QStringLiteral("UNPUBLISHED") &&
			      off.value(QStringLiteral("privacy"))
				      .toObject()
				      .value(QStringLiteral("value"))
				      .toString() ==
				      QStringLiteral("EVERYONE") &&
			      !off.contains(QStringLiteral(
				      "persistent_stream_key_status")),
		      "fbcreate-persistent-off");
		// Opt-in explicito: ENABLE, sin tocar titulo/desc.
		const QJsonObject on = meta::facebookCreatePayload(
			QStringLiteral("T"), QStringLiteral(""), true);
		CHECK(on.value(QStringLiteral(
				       "persistent_stream_key_status"))
				      .toString() ==
			      QStringLiteral("ENABLE"),
		      "fbcreate-persistent-opt-in");
		CHECK(!on.contains(QStringLiteral("description")),
		      "fbcreate-empty-desc-omitted");
	}
	{
		// Mapeo facebook:persistent-key-not-allowed por mensaje
		// Meta (espejo del backend): case-insensitive, resto no.
		CHECK(meta::facebookIsPersistentKeyError(
			      QStringLiteral(
				      "No permission to use persistent stream key param")),
		      "fbpersistent-msg-match");
		CHECK(meta::facebookIsPersistentKeyError(
			      QStringLiteral(
				      "PERSISTENT STREAM KEY issue")),
		      "fbpersistent-msg-case");
		CHECK(!meta::facebookIsPersistentKeyError(
			      QStringLiteral("Session expired")) &&
			      !meta::facebookIsPersistentKeyError(
				      QStringLiteral("")),
		      "fbpersistent-msg-other");
		const QString pm =
			meta::fbPersistentNotAllowedMessage();
		CHECK(!pm.isEmpty() &&
			      pm.contains(QStringLiteral("persistent")) &&
			      pm.contains(QStringLiteral("off")),
		      "fbpersistent-msg-honest");
		CHECK(!pm.contains(QStringLiteral("secure_stream_url")) &&
			      !pm.contains(QStringLiteral("Bearer")) &&
			      !pm.contains(QStringLiteral("token")),
		      "fbpersistent-msg-safe");
	}
	{
		// Audiencia-no-verificada: un 200 de Status SIN privacy
		// jamas verifica audiencia (F-093). La aceptacion del
		// create/update tampoco promete audiencia publica.
		const meta::FbVerifyResult r =
			meta::facebookVerifyPrepared(
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("T"), QStringLiteral("D"),
				QStringLiteral("UNPUBLISHED"), QString());
		CHECK(r.outcome == meta::FbVerify::AudienceUnverified &&
			      r.reason ==
				      QStringLiteral("privacy-unverified"),
		      "fbstatus-acceptance-no-audience");
	}

	// T-078 Fase 2 (logica): relay local MediaMTX (ADR-016). Puro,
	// sin red: formato forward F-096, cooldown F-097, VOD F-098,
	// binario ausente, puertos ocupados. Valores falsos, jamas
	// secretos ni ingestas reales.
	CHECK(relay::fixedDestination() ==
		      QStringLiteral("rtmp://127.0.0.1:1935/live"),
	      "relay-fixed-dest");
	CHECK(relay::rtmpPort() == 1935 && relay::controlPort() == 9997,
	      "relay-ports");
	CHECK(relay::pathName() == QStringLiteral("live") &&
		      relay::controlPatchPath() ==
			      QStringLiteral(
				      "/v3/config/paths/patch/live") &&
		      relay::controlBase() ==
			      QStringLiteral("http://127.0.0.1:9997"),
	      "relay-control-api");
	CHECK(relay::binaryCandidates().size() == 2 &&
		      relay::binaryCandidates().contains(
			      QStringLiteral("mediamtx.exe")) &&
		      relay::binaryCandidates().contains(
			      QStringLiteral("mediamtx")),
	      "relay-binary-names");
	{
		// F-096: base + '#' + clave, jamas URL integra.
		const QString dest = relay::forwardDestFromIngest(
			QStringLiteral(
				"rtmps://live-api-s.example.com:443/rtmp/FAKEKEY9z"));
		CHECK(dest ==
			      QStringLiteral(
				      "rtmps://live-api-s.example.com:443/rtmp#FAKEKEY9z"),
		      "relay-fwd-format");
		CHECK(dest !=
			      QStringLiteral(
				      "rtmps://live-api-s.example.com:443/rtmp/FAKEKEY9z"),
		      "relay-fwd-never-integra");
		CHECK(relay::forwardDestLooksValid(dest),
		      "relay-fwd-valid");
		const QString withQuery = relay::forwardDestFromIngest(
			QStringLiteral(
				"rtmps://h.example.com:443/rtmp/KEY9z?tok=1"));
		CHECK(withQuery ==
			      QStringLiteral(
				      "rtmps://h.example.com:443/rtmp#KEY9z?tok=1"),
		      "relay-fwd-query");
		CHECK(relay::forwardDestFromIngest(QString()).isEmpty() &&
			      relay::forwardDestFromIngest(
				      QStringLiteral("not-a-url"))
				      .isEmpty() &&
			      relay::forwardDestFromIngest(
				      QStringLiteral("rtmps://h.only/"))
				      .isEmpty(),
		      "relay-fwd-reject");
		CHECK(!relay::forwardDestLooksValid(
			      QStringLiteral(
				      "rtmps://h.example.com:443/rtmp/KEY9z")) &&
			      !relay::forwardDestLooksValid(QString()),
		      "relay-fwd-invalid");
		const QString body =
			relay::forwardPatchBody(dest);
		CHECK(body.contains(QStringLiteral("\"forward\"")) &&
			      body.contains(dest),
		      "relay-fwd-body");
		const QString clear = relay::forwardPatchBody(QString());
		CHECK(clear.contains(QStringLiteral("\"forward\"")) &&
			      clear.contains(QStringLiteral("[]")),
		      "relay-fwd-clear");
	}
	{
		// F-097: cooldown ~3 min, sin reintentos agresivos.
		CHECK(relay::cooldownMs() == 180000LL,
		      "relay-cooldown-ms");
		CHECK(relay::canRefire(0, 1000000LL),
		      "relay-cooldown-never");
		CHECK(!relay::canRefire(1000000LL, 1000000LL),
		      "relay-cooldown-blocks");
		CHECK(relay::canRefire(1000000LL - 180000LL,
				       1000000LL),
		      "relay-cooldown-elapsed");
		CHECK(relay::cooldownRemainingMs(1000000LL,
						 1000000LL) == 180000LL &&
			      relay::cooldownRemainingMs(
				      1000000LL - 180000LL, 1000000LL) == 0,
		      "relay-cooldown-remaining");
	}
	{
		// Mensajes honestos: no vacios, sin secretos/URLs sensibles,
		// con destino fijo del operador donde aplica.
		const QString msgs[] = {
			relay::vodNoticeMessage(),
			relay::binaryMissingMessage(),
			relay::portBusyMessage(1935),
			relay::portBusyMessage(9997),
			relay::operatorFixedMessage(),
			relay::cooldownMessage(180000LL),
			relay::forwardErrorMessage(500),
			relay::forwardErrorMessage(429),
		};
		for (const QString &m : msgs) {
			if (m.isEmpty() ||
			    m.contains(
				    QStringLiteral("secure_stream_url")) ||
			    m.contains(QStringLiteral("Bearer")) ||
			    m.contains(QStringLiteral("token")) ||
			    m.contains(QStringLiteral("secret")) ||
			    m.contains(QStringLiteral("client_id"))) {
				std::printf("FAIL relay-msg-safe\n");
				return 1;
			}
		}
		std::printf("PASS relay-msg-safe\n");
		CHECK(relay::binaryMissingMessage().contains(
			      QStringLiteral("mediamtx")),
		      "relay-msg-binary");
		CHECK(relay::portBusyMessage(1935).contains(
			      QStringLiteral("1935")) &&
			      relay::portBusyMessage(9997).contains(
				      QStringLiteral("9997")),
		      "relay-msg-ports");
		CHECK(relay::operatorFixedMessage().contains(
			      QStringLiteral("127.0.0.1:1935/live")),
		      "relay-msg-fixed");
		CHECK(relay::vodNoticeMessage().contains(
			      QStringLiteral("VOD")),
		      "relay-msg-vod");
		CHECK(relay::cooldownMessage(180000LL).contains(
			      QStringLiteral("3 min")),
		      "relay-msg-cooldown");
		// T-078 Fase 3 (cableado): startErrorMessage honesto (sin
		// secretos/URLs sensibles) + estados del indicador puros.
		CHECK(!relay::startErrorMessage().isEmpty() &&
			      !relay::startErrorMessage().contains(
				      QStringLiteral("secure_stream_url")) &&
			      !relay::startErrorMessage().contains(
				      QStringLiteral("Bearer")) &&
			      !relay::startErrorMessage().contains(
				      QStringLiteral("token")) &&
			      !relay::startErrorMessage().contains(
				      QStringLiteral("secret")),
		      "relay-msg-starterr-safe");
		CHECK(relay::startErrorMessage().contains(
			      QStringLiteral("127.0.0.1:1935/live")),
		      "relay-msg-starterr-fixed");
		// Nombres de estado: no vacios, distintos, sin secretos.
		const char *names[] = {
			relay::stateName(relay::State::Stopped),
			relay::stateName(relay::State::Starting),
			relay::stateName(relay::State::Running),
			relay::stateName(relay::State::ForwardOk),
			relay::stateName(relay::State::ForwardFailed),
			relay::stateName(relay::State::BinaryMissing),
			relay::stateName(relay::State::PortBusy),
		};
		{
			bool distinct = true;
			for (int i = 0; i < 7 && distinct; ++i) {
				if (!names[i] || !names[i][0])
					distinct = false;
				for (int j = i + 1;
				     j < 7 && distinct; ++j) {
					if (QString::fromLatin1(names[i]) ==
					    QString::fromLatin1(names[j]))
						distinct = false;
				}
			}
			CHECK(distinct, "relay-state-names");
		}
		// Proceso propio en pie: Starting/Running/Forward*.
		CHECK(relay::stateIsRunning(relay::State::Starting) &&
			      relay::stateIsRunning(relay::State::Running) &&
			      relay::stateIsRunning(relay::State::ForwardOk) &&
			      relay::stateIsRunning(
				      relay::State::ForwardFailed) &&
			      !relay::stateIsRunning(
				      relay::State::Stopped) &&
			      !relay::stateIsRunning(
				      relay::State::BinaryMissing) &&
			      !relay::stateIsRunning(relay::State::PortBusy),
		      "relay-state-running");
		// Error que exige accion: BinaryMissing/PortBusy/Failed.
		CHECK(relay::stateIsError(
			      relay::State::BinaryMissing) &&
			      relay::stateIsError(relay::State::PortBusy) &&
			      relay::stateIsError(
				      relay::State::ForwardFailed) &&
			      !relay::stateIsError(relay::State::Stopped) &&
			      !relay::stateIsError(relay::State::Starting) &&
			      !relay::stateIsError(relay::State::Running) &&
			      !relay::stateIsError(relay::State::ForwardOk),
		      "relay-state-error");
	}

	std::printf("SELFCHECK OK\n");
	return 0;
}

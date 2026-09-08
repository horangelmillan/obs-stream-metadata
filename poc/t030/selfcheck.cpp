// T-030 selfcheck: única prueba ejecutable de la lógica PoC (sin red).
// Uso: provider-poc-selfcheck.exe → salida PASS + exit 0.
#include "provider_poc.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#define CHECK(cond, label)                                              \
	do {                                                            \
		if (!(cond)) {                                          \
			std::printf("FAIL %s\n", label);                \
			return 1;                                       \
		}                                                       \
		std::printf("PASS %s\n", label);                        \
	} while (0)

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	using namespace t030;

	// PKCE: vector oficial RFC 7636 §4.2.
	CHECK(codeChallengeS256(QStringLiteral(
		      "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")) ==
		      QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"),
	      "pkce-rfc7636");
	const QString v = generateCodeVerifier();
	CHECK(v.size() == 64, "pkce-verifier-len");
	CHECK(generateState().size() == 32, "state-len");

	// URLs contienen endpoint + scope + state.
	const QString yt = youTubeAuthUrl(QStringLiteral("CID"),
					  QStringLiteral("http://127.0.0.1:9004"),
					  QStringLiteral("ST"), QStringLiteral("CH"));
	CHECK(yt.contains("accounts.google.com") && yt.contains("youtube.force-ssl") &&
		      yt.contains("code_challenge") && yt.contains("ST"),
	      "youtube-url");
	const QString kk = kickAuthUrl(QStringLiteral("CID"),
				       QStringLiteral("http://localhost:3000/cb"),
				       QStringLiteral("ST"), QStringLiteral("CH"));
	CHECK(kk.contains("id.kick.com") && kk.contains("channel") &&
		      kk.contains("S256"),
	      "kick-url");
	const QString tw = twitchAuthCodeUrl(QStringLiteral("CID"),
					     QStringLiteral("http://localhost:3000"),
					     QStringLiteral("ST"));
	CHECK(tw.contains("id.twitch.tv") && tw.contains("channel") &&
		      !tw.contains("code_challenge"),
	      "twitch-url");

	// Validadores §20.
	QString r;
	CHECK(!twitchTitleOk(QString(), &r), "twitch-empty");
	CHECK(!twitchTitleOk(QString(141, QLatin1Char('x')), &r), "twitch-141");
	CHECK(twitchTitleOk(QString(140, QLatin1Char('x'))), "twitch-140");
	CHECK(!youTubeTitleOk(QString(), &r), "youtube-empty");
	CHECK(!youTubeTitleOk(QString(101, QLatin1Char('x'))), "youtube-101");
	CHECK(youTubeTitleOk(QStringLiteral("T-030 PASS")), "youtube-ok");
	CHECK(!youTubeDescOk(QString(5001, QLatin1Char('x'))), "ytdesc-5001");
	CHECK(youTubeDescOk(QString()), "ytdesc-empty-ok");
	CHECK(!kickTitleOk(QString()), "kick-empty");
	CHECK(kickTitleOk(QStringLiteral("hola")), "kick-ok");

	// Payloads exactos.
	CHECK(twitchTitlePayload(QStringLiteral("A")) ==
		      QStringLiteral("{\"title\":\"A\"}"),
	      "twitch-payload");
	CHECK(kickTitlePayload(QStringLiteral("A")) ==
		      QStringLiteral("{\"stream_title\":\"A\"}"),
	      "kick-payload");
	const QString fetched =
		QStringLiteral("{\"id\":\"B1\",\"snippet\":{\"title\":\"old\","
			       "\"description\":\"d\",\"scheduledStartTime\":\"2026-09-08T20:00:00Z\","
			       "\"categoryId\":\"20\"},\"contentDetails\":{\"monitorStream\":"
			       "{\"enableMonitorStream\":true,\"broadcastStreamDelayMs\":0}}}");
	const QString put = youTubeSnippetPayload(fetched, QStringLiteral("N"),
						  QStringLiteral("D"));
	const QJsonObject o =
		QJsonDocument::fromJson(put.toUtf8()).object();
	const QJsonObject sn = o.value(QStringLiteral("snippet")).toObject();
	CHECK(o.value(QStringLiteral("id")).toString() == QStringLiteral("B1") &&
		      sn.value(QStringLiteral("title")).toString() == QStringLiteral("N") &&
		      sn.value(QStringLiteral("scheduledStartTime")).toString() ==
			      QStringLiteral("2026-09-08T20:00:00Z") &&
		      sn.value(QStringLiteral("categoryId")).toString() == QStringLiteral("20") &&
		      o.contains(QStringLiteral("contentDetails")),
	      "youtube-merge");

	// HTTP §28: 204 = éxito (Kick).
	CHECK(classifyHttpStatus(200) == HttpOutcome::Success, "http-200");
	CHECK(classifyHttpStatus(204) == HttpOutcome::Success, "http-204");
	CHECK(classifyHttpStatus(400) == HttpOutcome::BadRequest, "http-400");
	CHECK(classifyHttpStatus(401) == HttpOutcome::AuthRequired, "http-401");
	CHECK(classifyHttpStatus(403) == HttpOutcome::ForbiddenScope, "http-403");
	CHECK(classifyHttpStatus(404) == HttpOutcome::NotFound, "http-404");
	CHECK(classifyHttpStatus(409) == HttpOutcome::ConflictState, "http-409");
	CHECK(classifyHttpStatus(429) == HttpOutcome::RateLimited, "http-429");
	CHECK(classifyHttpStatus(503) == HttpOutcome::ServerRetry, "http-503");

	std::printf("SELFCHECK OK\n");
	return 0;
}
